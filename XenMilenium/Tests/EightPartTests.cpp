#include "Parameters/ParameterRegistry.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
int fail(const juce::String& message)
{
    std::cerr << message << '\n';
    return 1;
}

bool isZero(float sample)
{
    std::uint32_t bits = 1;
    std::memcpy(&bits, &sample, sizeof(bits));
    return bits == 0u;
}

bool bufferIsFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            if (! std::isfinite(buffer.getSample(channel, index)))
                return false;
    return true;
}

bool bufferHasEnergy(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            if (! isZero(buffer.getSample(channel, index)))
                return true;
    return false;
}

bool bufferIsSilent(const juce::AudioBuffer<float>& buffer)
{
    return bufferIsFinite(buffer) && ! bufferHasEnergy(buffer);
}

double meanAbs(const juce::AudioBuffer<float>& buffer)
{
    double sum = 0.0;
    int count = 0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < buffer.getNumSamples(); ++index)
        {
            sum += std::abs(buffer.getSample(channel, index));
            ++count;
        }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

juce::AudioParameterBool* boolParameter(XenMileniumAudioProcessor& processor, const char* path)
{
    const auto* spec = xenmilenium::ParameterRegistry::findByStatePath(path);
    return spec == nullptr ? nullptr : dynamic_cast<juce::AudioParameterBool*>(processor.getValueTreeState().getParameter(spec->apvtsId));
}

juce::AudioParameterChoice* choiceParameter(XenMileniumAudioProcessor& processor, const char* path)
{
    const auto* spec = xenmilenium::ParameterRegistry::findByStatePath(path);
    return spec == nullptr ? nullptr : dynamic_cast<juce::AudioParameterChoice*>(processor.getValueTreeState().getParameter(spec->apvtsId));
}

juce::AudioParameterFloat* floatParameter(XenMileniumAudioProcessor& processor, const char* path)
{
    const auto* spec = xenmilenium::ParameterRegistry::findByStatePath(path);
    return spec == nullptr ? nullptr : dynamic_cast<juce::AudioParameterFloat*>(processor.getValueTreeState().getParameter(spec->apvtsId));
}

void setBool(juce::AudioParameterBool* parameter, bool value)
{
    static_cast<juce::AudioProcessorParameter*>(parameter)->setValue(value ? 1.0f : 0.0f);
}

void setPlain(juce::RangedAudioParameter* parameter, float plain)
{
    static_cast<juce::AudioProcessorParameter*>(parameter)->setValue(parameter->convertTo0to1(plain));
}

int render(XenMileniumAudioProcessor& processor, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            buffer.setSample(channel, index, std::nanf(""));
    processor.processBlock(buffer, midi);
    if (! bufferIsFinite(buffer))
        return fail("non-finite sample");
    return 0;
}

void enableOnly(XenMileniumAudioProcessor& processor, int partNumber)
{
    for (int part = 1; part <= 8; ++part)
        setBool(boolParameter(processor, ("part/" + juce::String(part) + "/enabled").toRawUTF8()), part == partNumber);
}

int checkIndependentParts()
{
    XenMileniumAudioProcessor processor;
    for (int part = 1; part <= 8; ++part)
        setPlain(floatParameter(processor, ("part/" + juce::String(part) + "/env/amp/release").toRawUTF8()), 0.001f);
    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);

    for (int part = 1; part <= 8; ++part)
    {
        enableOnly(processor, part);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        if (const int result = render(processor, buffer, midi))
            return result;
        if (processor.getActiveVoiceCountForPart(part) != 1 || ! bufferHasEnergy(buffer))
            return fail("part " + juce::String(part) + " did not sound on its own");
        for (int other = 1; other <= 8; ++other)
            if (other != part && processor.getActiveVoiceCountForPart(other) != 0)
                return fail("part " + juce::String(part) + " started a voice in part " + juce::String(other));
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        if (const int result = render(processor, buffer, off))
            return result;
        juce::MidiBuffer empty;
        if (const int result = render(processor, buffer, empty))
            return result;
        if (processor.getActiveVoiceCount() != 0)
            return fail("part " + juce::String(part) + " left a voice after release");
    }

    enableOnly(processor, 0);
    setBool(boolParameter(processor, "part/1/enabled"), true);
    setBool(boolParameter(processor, "part/2/enabled"), true);
    juce::MidiBuffer both;
    both.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(110)), 0);
    both.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(110)), 10);
    if (const int result = render(processor, buffer, both))
        return result;
    if (processor.getActiveVoiceCountForPart(1) != 2 || processor.getActiveVoiceCountForPart(2) != 2)
        return fail("two enabled parts did not each track both notes");

    setBool(boolParameter(processor, "part/1/enabled"), false);
    juce::MidiBuffer empty;
    if (const int result = render(processor, buffer, empty))
        return result;
    if (processor.getActiveVoiceCountForPart(2) != 2)
        return fail("disabling part 1 stopped part 2");
    juce::MidiBuffer extra;
    extra.addEvent(juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(100)), 0);
    if (const int result = render(processor, buffer, extra))
        return result;
    if (processor.getActiveVoiceCountForPart(1) > 2 || processor.getActiveVoiceCountForPart(2) != 3)
        return fail("a disabled part accepted a note or the other part missed it");

    processor.releaseResources();
    return 0;
}

int checkChannelAndOffsets()
{
    XenMileniumAudioProcessor processor;
    setBool(boolParameter(processor, "part/1/enabled"), false);
    setBool(boolParameter(processor, "part/2/enabled"), true);
    setPlain(choiceParameter(processor, "part/2/midiChannel"), 2.0f);
    processor.prepareToPlay(48000.0, 160);
    juce::AudioBuffer<float> buffer(2, 160);

    juce::MidiBuffer wrong;
    wrong.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(120)), 0);
    if (const int result = render(processor, buffer, wrong))
        return result;
    if (! bufferIsSilent(buffer) || processor.getActiveVoiceCount() != 0)
        return fail("a filtered channel produced a voice");

    juce::MidiBuffer right;
    right.addEvent(juce::MidiMessage::noteOn(2, 69, static_cast<juce::uint8>(120)), 80);
    if (const int result = render(processor, buffer, right))
        return result;
    if (! processor.isNoteActive(2, 2, 69) || processor.getActiveVoiceCountForPart(1) != 0)
        return fail("the selected channel did not reach part 2");
    for (int index = 0; index < 80; ++index)
        if (! isZero(buffer.getSample(0, index)) || ! isZero(buffer.getSample(1, index)))
            return fail("audio appeared before the MIDI offset");
    if (std::abs(buffer.getSample(0, 80)) > 1.0e-6f)
        return fail("the note did not start at silence on its offset");
    bool later = false;
    for (int index = 81; index < buffer.getNumSamples(); ++index)
        later = later || ! isZero(buffer.getSample(0, index));
    if (! later)
        return fail("the note produced no samples after its offset");
    if (std::memcmp(buffer.getReadPointer(0), buffer.getReadPointer(1), sizeof(float) * static_cast<std::size_t>(buffer.getNumSamples())) != 0)
        return fail("stereo channels diverged");

    processor.releaseResources();
    return 0;
}

int checkStealingAndRelease()
{
    XenMileniumAudioProcessor processor;
    setPlain(floatParameter(processor, "part/1/env/amp/attack"), 0.001f);
    setPlain(floatParameter(processor, "part/1/env/amp/decay"), 0.001f);
    setPlain(floatParameter(processor, "part/1/env/amp/sustain"), 1.0f);
    setPlain(floatParameter(processor, "part/1/env/amp/release"), 0.01f);
    setPlain(choiceParameter(processor, "part/1/midiChannel"), 1.0f);
    setPlain(choiceParameter(processor, "part/2/midiChannel"), 2.0f);
    setBool(boolParameter(processor, "part/2/enabled"), true);
    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);

    juce::MidiBuffer held;
    held.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    held.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 1);
    held.addEvent(juce::MidiMessage::noteOn(2, 72, static_cast<juce::uint8>(100)), 2);
    held.addEvent(juce::MidiMessage::noteOn(2, 76, static_cast<juce::uint8>(100)), 3);
    if (const int result = render(processor, buffer, held))
        return result;
    juce::MidiBuffer offOne;
    offOne.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    if (const int result = render(processor, buffer, offOne))
        return result;
    if (! processor.isNoteActive(1, 1, 64) || processor.getActiveVoiceCountForPart(2) != 2)
        return fail("note off released the wrong voice or the other part");

    for (int repeat = 0; repeat < 6; ++repeat)
    {
        juce::MidiBuffer again;
        again.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(127)), 0);
        if (const int result = render(processor, buffer, again))
            return result;
    }
    if (processor.getActiveVoiceCountForPart(1) != 1 || ! processor.isNoteActive(1, 1, 64))
        return fail("repeated note on left extra voices");

    juce::MidiBuffer velocityOff;
    velocityOff.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(0)), 0);
    if (const int result = render(processor, buffer, velocityOff))
        return result;
    if (! processor.isNoteActive(1, 1, 64) || processor.getActiveVoiceCountForPart(1) != 1)
        return fail("velocity 0 did not act as note off of the matching voice");

    juce::AudioBuffer<float> tail(2, 2048);
    juce::MidiBuffer empty;
    if (const int result = render(processor, tail, empty))
        return result;
    if (processor.isNoteActive(1, 1, 64) || processor.isNoteActive(1, 1, 60))
        return fail("released voices stayed active");
    if (processor.getActiveVoiceCountForPart(2) != 2)
        return fail("part 2 lost voices while part 1 released");

    juce::MidiBuffer fill;
    for (int note = 40; note <= 47; ++note)
        fill.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(90)), note - 40);
    if (const int result = render(processor, buffer, fill))
        return result;
    juce::MidiBuffer ninth;
    ninth.addEvent(juce::MidiMessage::noteOn(1, 90, static_cast<juce::uint8>(90)), 0);
    if (const int result = render(processor, buffer, ninth))
        return result;
    if (processor.getActiveVoiceCountForPart(1) != 8 || processor.isNoteActive(1, 1, 40) || ! processor.isNoteActive(1, 1, 90))
        return fail("part 1 stole outside itself or dropped the new note");
    if (processor.getActiveVoiceCountForPart(2) != 2)
        return fail("a full part stole from another part");

    processor.releaseResources();
    return 0;
}

int checkGlobalStealMonoAndPedal()
{
    XenMileniumAudioProcessor processor;
    for (int part = 1; part <= 8; ++part)
    {
        const auto prefix = "part/" + juce::String(part);
        setBool(boolParameter(processor, (prefix + "/enabled").toRawUTF8()), true);
        setPlain(choiceParameter(processor, (prefix + "/midiChannel").toRawUTF8()), static_cast<float>(part));
        setPlain(floatParameter(processor, (prefix + "/env/amp/attack").toRawUTF8()), 0.001f);
        setPlain(floatParameter(processor, (prefix + "/env/amp/decay").toRawUTF8()), 0.001f);
        setPlain(floatParameter(processor, (prefix + "/env/amp/sustain").toRawUTF8()), 1.0f);
        setPlain(floatParameter(processor, (prefix + "/env/amp/release").toRawUTF8()), 0.2f);
    }
    processor.prepareToPlay(48000.0, 64);
    juce::AudioBuffer<float> buffer(2, 64);
    juce::MidiBuffer fill;
    int offset = 0;
    for (int part = 2; part <= 8; ++part)
        for (int note = 0; note < 4; ++note)
            fill.addEvent(juce::MidiMessage::noteOn(part, 48 + note, static_cast<juce::uint8>(80)), offset++);
    for (int note = 0; note < 4; ++note)
        fill.addEvent(juce::MidiMessage::noteOn(1, 70 + note, static_cast<juce::uint8>(80)), offset++);
    if (const int result = render(processor, buffer, fill))
        return result;
    if (processor.getActiveVoiceCount() != 32 || processor.getActiveVoiceCountForPart(1) != 4 || processor.getActiveVoiceCountForPart(2) != 4)
        return fail("global pool did not fill to the bench limit");

    juce::MidiBuffer extra;
    extra.addEvent(juce::MidiMessage::noteOn(1, 90, static_cast<juce::uint8>(100)), 0);
    if (const int result = render(processor, buffer, extra))
        return result;
    if (processor.getActiveVoiceCount() != 32 || processor.getActiveVoiceCountForPart(1) != 5 || processor.getActiveVoiceCountForPart(2) != 3)
        return fail("global steal did not take the oldest voice from another part");
    if (processor.isNoteActive(2, 2, 48) || ! processor.isNoteActive(1, 1, 90))
        return fail("global steal replaced the wrong note");

    processor.releaseResources();

    XenMileniumAudioProcessor mono;
    setBool(boolParameter(mono, "part/1/mono"), true);
    setPlain(floatParameter(mono, "part/1/env/amp/attack"), 0.05f);
    setPlain(floatParameter(mono, "part/1/env/amp/decay"), 0.001f);
    setPlain(floatParameter(mono, "part/1/env/amp/sustain"), 0.25f);
    setPlain(floatParameter(mono, "part/1/env/amp/release"), 0.2f);
    setPlain(floatParameter(mono, "master/volume"), 1.0f);
    mono.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> level(2, 256);
    juce::MidiBuffer first;
    first.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(mono, level, first))
        return result;
    juce::MidiBuffer settle;
    if (const int result = render(mono, level, settle))
        return result;
    const double held = meanAbs(level);

    juce::MidiBuffer second;
    second.addEvent(juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(mono, level, second))
        return result;
    juce::AudioBuffer<float> risen(2, 1024);
    if (const int result = render(mono, risen, settle))
        return result;
    if (mono.getActiveVoiceCountForPart(1) != 1 || mono.isNoteActive(1, 1, 60) || ! mono.isNoteActive(1, 1, 67))
        return fail("mono kept more than one note");
    if (! (meanAbs(risen) > held * 1.8))
        return fail("mono did not restart the envelope toward full level");

    setBool(boolParameter(mono, "part/1/mono"), false);
    setBool(boolParameter(mono, "part/1/legato"), true);
    juce::MidiBuffer back;
    back.addEvent(juce::MidiMessage::noteOff(1, 67), 0);
    if (const int result = render(mono, level, back))
        return result;
    juce::AudioBuffer<float> gone(2, 12000);
    if (const int result = render(mono, gone, settle))
        return result;
    juce::MidiBuffer legatoNote;
    legatoNote.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(mono, level, legatoNote))
        return result;
    juce::AudioBuffer<float> reachSustain(2, 4096);
    if (const int result = render(mono, reachSustain, settle))
        return result;
    if (const int result = render(mono, level, settle))
        return result;
    const double legatoHeld = meanAbs(level);
    juce::MidiBuffer legatoNext;
    legatoNext.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(mono, level, legatoNext))
        return result;
    if (const int result = render(mono, risen, settle))
        return result;
    if (mono.getActiveVoiceCount() != 1 || ! (meanAbs(risen) < legatoHeld * 1.5))
        return fail("legato restarted the envelope or kept two voices");

    setBool(boolParameter(mono, "part/1/mono"), true);
    juce::MidiBuffer bothFlags;
    bothFlags.addEvent(juce::MidiMessage::noteOn(1, 76, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(mono, level, bothFlags))
        return result;
    if (mono.getActiveVoiceCount() != 1 || mono.isNoteActive(1, 1, 72))
        return fail("mono plus legato kept the previous note");

    mono.releaseResources();

    XenMileniumAudioProcessor pedal;
    setPlain(floatParameter(pedal, "part/1/env/amp/attack"), 0.001f);
    setPlain(floatParameter(pedal, "part/1/env/amp/decay"), 0.001f);
    setPlain(floatParameter(pedal, "part/1/env/amp/sustain"), 1.0f);
    setPlain(floatParameter(pedal, "part/1/env/amp/release"), 0.05f);
    pedal.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> pedalBuffer(2, 128);
    juce::MidiBuffer pedalOn;
    pedalOn.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(120)), 0);
    pedalOn.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 10);
    pedalOn.addEvent(juce::MidiMessage::noteOff(1, 69), 20);
    if (const int result = render(pedal, pedalBuffer, pedalOn))
        return result;
    juce::AudioBuffer<float> still(2, 4800);
    if (const int result = render(pedal, still, settle))
        return result;
    if (! pedal.isNoteActive(1, 1, 69) || ! bufferHasEnergy(still))
        return fail("sustain pedal did not hold the note");
    juce::MidiBuffer pedalOff;
    pedalOff.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
    if (const int result = render(pedal, pedalBuffer, pedalOff))
        return result;
    juce::AudioBuffer<float> released(2, 4800);
    if (const int result = render(pedal, released, settle))
        return result;
    if (pedal.isNoteActive(1, 1, 69))
        return fail("releasing the sustain pedal left the note held");

    pedal.releaseResources();
    return 0;
}

int checkEnvelopeBlockRateAndState()
{
    XenMileniumAudioProcessor processor;
    setPlain(floatParameter(processor, "part/1/env/amp/attack"), 0.001f);
    setPlain(floatParameter(processor, "part/1/env/amp/decay"), 0.1f);
    setPlain(floatParameter(processor, "part/1/env/amp/sustain"), 0.2f);
    setPlain(floatParameter(processor, "part/1/env/amp/release"), 0.05f);
    setPlain(floatParameter(processor, "master/volume"), 1.0f);
    processor.prepareToPlay(48000.0, 480);
    juce::AudioBuffer<float> early(2, 480);
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(processor, early, on))
        return result;
    const double duringDecay = meanAbs(early);
    juce::AudioBuffer<float> later(2, 4800);
    juce::MidiBuffer empty;
    if (const int result = render(processor, later, empty))
        return result;
    const double atSustain = meanAbs(later);
    if (! (atSustain < duringDecay * 0.75))
        return fail("decay did not move toward sustain");

    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
    if (const int result = render(processor, early, off))
        return result;
    juce::AudioBuffer<float> done(2, 4800);
    if (const int result = render(processor, done, empty))
        return result;
    if (processor.getActiveVoiceCount() != 0)
        return fail("release did not free the voice");
    if (const int result = render(processor, done, empty))
        return result;
    if (! bufferIsSilent(done))
        return fail("a freed voice left sound in the next block");

    for (int part = 1; part <= 8; ++part)
        setBool(boolParameter(processor, ("part/" + juce::String(part) + "/enabled").toRawUTF8()), true);
    for (int block = 0; block < 3; ++block)
        if (const int result = render(processor, early, empty))
            return result;
        else if (! bufferIsSilent(early) || processor.getActiveVoiceCount() != 0)
            return fail("enabled parts sounded without MIDI");

    setBool(boolParameter(processor, "part/1/enabled"), true);
    for (int part = 2; part <= 8; ++part)
        setBool(boolParameter(processor, ("part/" + juce::String(part) + "/enabled").toRawUTF8()), false);
    setPlain(floatParameter(processor, "part/1/env/amp/attack"), 0.001f);
    setPlain(floatParameter(processor, "part/1/env/amp/sustain"), 1.0f);
    setPlain(floatParameter(processor, "part/1/env/amp/release"), 0.4f);
    juce::MidiBuffer held;
    held.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(processor, early, held))
        return result;
    float loud = 0.0f;
    for (int guard = 0; guard < 30 && std::abs(loud) < 0.05f; ++guard)
    {
        if (const int result = render(processor, early, empty))
            return result;
        loud = early.getSample(0, early.getNumSamples() - 1);
    }
    if (std::abs(loud) < 0.05f)
        return fail("could not reach a loud sample before the rate change");
    processor.prepareToPlay(96000.0, 128);
    juce::AudioBuffer<float> after(2, 128);
    if (const int result = render(processor, after, empty))
        return result;
    if (! processor.isNoteActive(1, 1, 69) || std::abs(after.getSample(0, 0) - loud) > 0.05f)
        return fail("sample rate change reset the voice or its phase");

    const int sizes[] = { 1, 64, 1024 };
    for (const int size : sizes)
    {
        XenMileniumAudioProcessor sized;
        setBool(boolParameter(sized, "part/8/enabled"), true);
        setBool(boolParameter(sized, "part/1/enabled"), false);
        sized.prepareToPlay(48000.0, size);
        juce::AudioBuffer<float> block(2, size);
        bool heard = false;
        const int blocks = std::max(2, 128 / size);
        for (int index = 0; index < blocks; ++index)
        {
            juce::MidiBuffer midi;
            if (index == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 70, static_cast<juce::uint8>(100)), 0);
            if (const int result = render(sized, block, midi))
                return result;
            heard = heard || bufferHasEnergy(block);
        }
        if (! heard || sized.getActiveVoiceCountForPart(8) != 1)
            return fail("part 8 was silent at block size " + juce::String(size));
        sized.releaseResources();
    }

    XenMileniumAudioProcessor layout;
    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    if (! layout.setBusesLayout(mono))
        return fail("mono layout was rejected");
    layout.prepareToPlay(44100.0, 64);
    juce::AudioBuffer<float> monoBuffer(1, 64);
    juce::MidiBuffer monoNote;
    monoNote.addEvent(juce::MidiMessage::noteOn(3, 62, static_cast<juce::uint8>(100)), 0);
    if (const int result = render(layout, monoBuffer, monoNote))
        return result;
    if (monoBuffer.getNumChannels() != 1 || ! bufferHasEnergy(monoBuffer))
        return fail("mono output stayed silent");
    layout.releaseResources();

    setPlain(floatParameter(processor, "part/3/env/amp/attack"), 1.5f);
    setPlain(choiceParameter(processor, "part/5/osc/1/waveSource"), 1.0f);
    setBool(boolParameter(processor, "part/4/enabled"), true);
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    XenMileniumAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (! juce::approximatelyEqual(floatParameter(restored, "part/3/env/amp/attack")->get(), 1.5f)
        || choiceParameter(restored, "part/5/osc/1/waveSource")->getCurrentChoiceName() != "saw"
        || ! juce::approximatelyEqual(floatParameter(restored, "part/1/env/amp/attack")->get(), 0.001f)
        || ! boolParameter(restored, "part/4/enabled")->get()
        || boolParameter(restored, "part/8/enabled")->get())
        return fail("part settings were not restored independently");
    if (restored.getName() != "Xen Milenium")
        return fail("plugin name changed");

    processor.releaseResources();
    restored.releaseResources();
    return 0;
}
} // namespace

int runEightPartTests()
{
    if (const int parts = checkIndependentParts())
        return parts;
    if (const int channels = checkChannelAndOffsets())
        return channels;
    if (const int stealing = checkStealingAndRelease())
        return stealing;
    if (const int modes = checkGlobalStealMonoAndPedal())
        return modes;
    if (const int envelope = checkEnvelopeBlockRateAndState())
        return envelope;

    std::cout << "parts ok\n";
    return 0;
}
