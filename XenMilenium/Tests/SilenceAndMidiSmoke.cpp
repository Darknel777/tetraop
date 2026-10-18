#include "PluginProcessor.h"

int runStateAndParameterTests();

#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
bool bufferIsSilent(const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() != 2 || buffer.getNumSamples() <= 0)
        return false;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const float* samples = buffer.getReadPointer(channel);
        for (int index = 0; index < buffer.getNumSamples(); ++index)
        {
            const float sample = samples[index];
            std::uint32_t bits = 1;
            static_assert(sizeof(sample) == sizeof(bits));
            std::memcpy(&bits, &sample, sizeof(bits));
            if (bits != 0u)
                return false;
        }
    }

    return true;
}

int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}
}

int main()
{
    XenMileniumAudioProcessor processor;

    juce::AudioProcessor::BusesLayout stereoOut;
    stereoOut.outputBuses.add(juce::AudioChannelSet::stereo());
    if (! processor.isBusesLayoutSupported(stereoOut))
        return fail("stereo output layout was rejected");

    juce::AudioProcessor::BusesLayout monoOut;
    monoOut.outputBuses.add(juce::AudioChannelSet::mono());
    if (processor.isBusesLayoutSupported(monoOut))
        return fail("mono output layout was accepted");

    juce::AudioProcessor::BusesLayout stereoInOut;
    stereoInOut.inputBuses.add(juce::AudioChannelSet::stereo());
    stereoInOut.outputBuses.add(juce::AudioChannelSet::stereo());
    if (processor.isBusesLayoutSupported(stereoInOut))
        return fail("input bus layout was accepted");

    constexpr int blockSize = 64;
    processor.prepareToPlay(48000.0, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            buffer.setSample(channel, index, (index % 2 == 0) ? 0.25f : std::nanf(""));

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 70, static_cast<juce::uint8>(0)), 5);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 10);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(90)), 20);

    processor.processBlock(buffer, midi);

    if (! bufferIsSilent(buffer))
        return fail("processBlock left a non-silent or non-finite sample");

    if (processor.getLastNoteNumber() != 64 || processor.getNoteOnCount() != 2)
    {
        std::cerr << "midi state last=" << processor.getLastNoteNumber()
                  << " count=" << processor.getNoteOnCount() << '\n';
        return 1;
    }

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            buffer.setSample(channel, index, 0.75f);

    juce::MidiBuffer emptyMidi;
    processor.processBlock(buffer, emptyMidi);

    if (! bufferIsSilent(buffer))
        return fail("second processBlock left a non-silent sample");

    if (processor.getLastNoteNumber() != 64 || processor.getNoteOnCount() != 2)
        return fail("empty midi buffer changed the note state");

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            buffer.setSample(channel, index, -0.5f);

    juce::MidiBuffer bypassMidi;
    bypassMidi.addEvent(juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(80)), 0);
    processor.processBlockBypassed(buffer, bypassMidi);

    if (! bufferIsSilent(buffer) || bypassMidi.getNumEvents() != 0)
        return fail("bypassed processBlock did not clear audio and midi");

    if (processor.getLastNoteNumber() != 64 || processor.getNoteOnCount() != 2)
        return fail("bypass changed the note state");

    juce::AudioBuffer<float> emptyBlock(2, 0);
    juce::MidiBuffer emptyBlockMidi;
    processor.processBlock(emptyBlock, emptyBlockMidi);
    if (emptyBlock.getNumChannels() != 2 || emptyBlock.getNumSamples() != 0)
        return fail("zero-length block changed its dimensions");

    processor.releaseResources();

    if (const int stateResult = runStateAndParameterTests())
        return stateResult;

    std::cout << "smoke ok\n";
    return 0;
}
