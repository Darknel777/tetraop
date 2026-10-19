#include "Parameters/ParameterRegistry.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

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

float maxAdjacentDelta(const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    const float* samples = buffer.getReadPointer(0);
    for (int index = 1; index < buffer.getNumSamples(); ++index)
        peak = std::max(peak, std::abs(samples[index] - samples[index - 1]));
    return peak;
}

juce::RangedAudioParameter* rangedParameter(XenMileniumAudioProcessor& processor, const char* path)
{
    const auto* spec = xenmilenium::ParameterRegistry::findByStatePath(path);
    return spec == nullptr ? nullptr : dynamic_cast<juce::RangedAudioParameter*>(processor.getValueTreeState().getParameter(spec->apvtsId));
}

void setPlain(juce::RangedAudioParameter* parameter, float plain)
{
    static_cast<juce::AudioProcessorParameter*>(parameter)->setValue(parameter->convertTo0to1(plain));
}

void setPath(XenMileniumAudioProcessor& processor, const char* path, float plain)
{
    setPlain(rangedParameter(processor, path), plain);
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

void silenceMixer(XenMileniumAudioProcessor& processor)
{
    setPath(processor, "part/1/mixer/osc1", 0.0f);
    setPath(processor, "part/1/mixer/osc2", 0.0f);
    setPath(processor, "part/1/mixer/ring", 0.0f);
    setPath(processor, "part/1/mixer/noise", 0.0f);
    setPath(processor, "part/1/osc/3/directOut", 0.0f);
}

int checkSources()
{
    XenMileniumAudioProcessor silent;
    for (int part = 1; part <= 8; ++part)
        setPath(silent, ("part/" + juce::String(part) + "/enabled").toRawUTF8(), part == 1 ? 1.0f : 0.0f);
    setPath(silent, "part/1/mixer/noise", 1.0f);
    silent.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);
    juce::MidiBuffer empty;
    if (const int result = render(silent, buffer, empty))
        return result;
    if (! bufferIsSilent(buffer) || silent.getActiveVoiceCount() != 0)
        return fail("noise level without a note produced sound");
    silent.releaseResources();

    const char* waves[] = { "sine", "saw", "triangle", "pulse" };
    for (int index = 0; index < 4; ++index)
    {
        XenMileniumAudioProcessor processor;
        silenceMixer(processor);
        setPath(processor, "part/1/mixer/osc2", 1.0f);
        setPath(processor, "part/1/osc/2/waveSource", static_cast<float>(index));
        setPath(processor, "part/1/env/amp/attack", 0.001f);
        setPath(processor, "part/1/env/amp/sustain", 1.0f);
        setPath(processor, "master/volume", 1.0f);
        processor.prepareToPlay(48000.0, 256);
        juce::MidiBuffer on;
        on.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(110)), 0);
        if (const int result = render(processor, buffer, on))
            return result;
        if (! bufferHasEnergy(buffer))
            return fail(juce::String("osc2 ") + waves[index] + " stayed silent");
        processor.releaseResources();
    }

    auto capture = [](float osc1, float osc2, int osc2Wave, std::vector<float>& out) -> int
    {
        XenMileniumAudioProcessor processor;
        silenceMixer(processor);
        setPath(processor, "part/1/mixer/osc1", osc1);
        setPath(processor, "part/1/mixer/osc2", osc2);
        setPath(processor, "part/1/osc/2/waveSource", static_cast<float>(osc2Wave));
        setPath(processor, "part/1/env/amp/attack", 0.001f);
        setPath(processor, "part/1/env/amp/decay", 0.001f);
        setPath(processor, "part/1/env/amp/sustain", 1.0f);
        setPath(processor, "master/volume", 1.0f);
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> block(2, 256);
        juce::MidiBuffer on;
        on.addEvent(juce::MidiMessage::noteOn(1, 57, static_cast<juce::uint8>(100)), 0);
        if (const int result = render(processor, block, on))
            return result;
        juce::MidiBuffer rest;
        if (const int result = render(processor, block, rest))
            return result;
        out.assign(static_cast<std::size_t>(block.getNumSamples()), 0.0f);
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
            out[static_cast<std::size_t>(sample)] = block.getSample(0, sample);
        processor.releaseResources();
        return 0;
    };

    std::vector<float> osc1Sine;
    std::vector<float> osc2Sine;
    std::vector<float> osc2Saw;
    if (const int result = capture(1.0f, 0.0f, 0, osc1Sine))
        return result;
    if (const int result = capture(0.0f, 1.0f, 0, osc2Sine))
        return result;
    if (const int result = capture(0.0f, 1.0f, 1, osc2Saw))
        return result;
    if (osc1Sine == osc2Saw)
        return fail("osc2 saw matched osc1 sine");
    double sineGap = 0.0;
    for (std::size_t index = 0; index < osc1Sine.size(); ++index)
        sineGap += std::abs(osc1Sine[index] - osc2Sine[index]);
    if (! (sineGap < 0.01))
        return fail("osc2 sine did not match osc1 sine");

    XenMileniumAudioProcessor ring;
    silenceMixer(ring);
    setPath(ring, "part/1/osc/1/waveSource", 3.0f);
    setPath(ring, "part/1/osc/2/waveSource", 3.0f);
    setPath(ring, "part/1/mixer/ring", 1.0f);
    setPath(ring, "part/1/env/amp/attack", 0.001f);
    setPath(ring, "part/1/env/amp/decay", 0.001f);
    setPath(ring, "part/1/env/amp/sustain", 1.0f);
    setPath(ring, "master/volume", 1.0f);
    ring.prepareToPlay(48000.0, 256);
    juce::MidiBuffer ringOn;
    ringOn.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(ring, buffer, ringOn))
        return result;
    juce::MidiBuffer ringRest;
    if (const int result = render(ring, buffer, ringRest))
        return result;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        if (buffer.getSample(0, sample) < 0.05f)
            return fail("ring of two equal pulses was not the positive product");
    ring.releaseResources();

    XenMileniumAudioProcessor noiseA;
    silenceMixer(noiseA);
    setPath(noiseA, "part/1/mixer/noise", 1.0f);
    setPath(noiseA, "part/1/env/amp/attack", 0.001f);
    setPath(noiseA, "part/1/env/amp/sustain", 1.0f);
    setPath(noiseA, "master/volume", 1.0f);
    noiseA.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> one(2, 128);
    juce::MidiBuffer oneNote;
    oneNote.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    if (const int result = render(noiseA, one, oneNote))
        return result;
    XenMileniumAudioProcessor noiseB;
    silenceMixer(noiseB);
    setPath(noiseB, "part/1/mixer/noise", 1.0f);
    setPath(noiseB, "part/1/env/amp/attack", 0.001f);
    setPath(noiseB, "part/1/env/amp/sustain", 1.0f);
    setPath(noiseB, "master/volume", 1.0f);
    noiseB.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> again(2, 128);
    juce::MidiBuffer sameNote;
    sameNote.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    if (const int result = render(noiseB, again, sameNote))
        return result;
    if (std::memcmp(one.getReadPointer(0), again.getReadPointer(0), sizeof(float) * 128u) != 0)
        return fail("per-voice noise was not deterministic");
    juce::MidiBuffer second;
    second.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
    if (const int result = render(noiseB, again, second))
        return result;
    if (std::memcmp(one.getReadPointer(0), again.getReadPointer(0), sizeof(float) * 128u) == 0)
        return fail("a second voice reused the first noise sequence");
    noiseA.releaseResources();
    noiseB.releaseResources();

    XenMileniumAudioProcessor direct;
    silenceMixer(direct);
    setPath(direct, "part/1/osc/3/waveSource", 1.0f);
    setPath(direct, "part/1/env/amp/attack", 0.001f);
    setPath(direct, "part/1/env/amp/sustain", 1.0f);
    setPath(direct, "master/volume", 1.0f);
    direct.prepareToPlay(48000.0, 128);
    juce::MidiBuffer directOn;
    directOn.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
    if (const int result = render(direct, buffer, directOn))
        return result;
    if (! bufferIsSilent(buffer))
        return fail("osc3 sounded through the mixer while direct out was zero");
    setPath(direct, "part/1/osc/3/directOut", 1.0f);
    juce::MidiBuffer directRest;
    if (const int result = render(direct, buffer, directRest))
        return result;
    if (! bufferHasEnergy(buffer))
        return fail("osc3 direct out stayed silent");
    std::vector<float> saw(static_cast<std::size_t>(buffer.getNumSamples()));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        saw[static_cast<std::size_t>(sample)] = buffer.getSample(0, sample);
    setPath(direct, "part/1/osc/3/waveSource", 3.0f);
    if (const int result = render(direct, buffer, directRest))
        return result;
    bool changed = false;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        changed = changed || std::abs(buffer.getSample(0, sample) - saw[static_cast<std::size_t>(sample)]) > 1.0e-5f;
    if (! changed)
        return fail("osc3 waveform did not reach direct out");
    direct.releaseResources();
    return 0;
}

int checkStealFadeAndOwnership()
{
    XenMileniumAudioProcessor processor;
    setPath(processor, "part/1/mono", 1.0f);
    setPath(processor, "part/1/env/amp/attack", 0.001f);
    setPath(processor, "part/1/env/amp/decay", 0.001f);
    setPath(processor, "part/1/env/amp/sustain", 1.0f);
    setPath(processor, "part/1/env/amp/release", 0.4f);
    setPath(processor, "master/volume", 1.0f);
    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(processor, buffer, on))
        return result;
    juce::MidiBuffer empty;
    float loud = 0.0f;
    for (int guard = 0; guard < 20 && std::abs(loud) < 0.05f; ++guard)
    {
        if (const int result = render(processor, buffer, empty))
            return result;
        loud = buffer.getSample(0, buffer.getNumSamples() - 1);
    }
    if (std::abs(loud) < 0.05f)
        return fail("could not reach a loud note before the steal");

    juce::MidiBuffer next;
    next.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(127)), 32);
    if (const int result = render(processor, buffer, next))
        return result;
    if (maxAdjacentDelta(buffer) > 0.02f)
        return fail("steal created a sample-to-sample jump above 0.02");
    if (processor.getActiveVoiceCount() != 1 || processor.getActiveTailCount() < 1)
        return fail("steal did not keep one voice and a separate tail");
    if (processor.isNoteActive(1, 1, 36) || ! processor.isNoteActive(1, 1, 48))
        return fail("steal kept the old note as the active voice");

    juce::MidiBuffer stale;
    stale.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
    if (const int result = render(processor, buffer, stale))
        return result;
    if (! processor.isNoteActive(1, 1, 48) || processor.getActiveVoiceCount() != 1)
        return fail("stale note off released the replacement note");

    for (int guard = 0; guard < 8; ++guard)
        if (const int result = render(processor, buffer, empty))
            return result;
    if (processor.getActiveTailCount() != 0)
        return fail("steal tail outlived the 4 ms fade");
    processor.releaseResources();
    if (processor.getActiveVoiceCount() != 0 || processor.getActiveTailCount() != 0)
        return fail("reset left voices or tails");

    XenMileniumAudioProcessor burst;
    setPath(burst, "part/1/env/amp/attack", 0.001f);
    setPath(burst, "part/1/env/amp/decay", 0.001f);
    setPath(burst, "part/1/env/amp/sustain", 1.0f);
    setPath(burst, "part/1/env/amp/release", 0.2f);
    setPath(burst, "part/2/enabled", 1.0f);
    setPath(burst, "part/2/midiChannel", 2.0f);
    setPath(burst, "part/1/midiChannel", 1.0f);
    burst.prepareToPlay(48000.0, 64);
    juce::AudioBuffer<float> block(2, 64);
    juce::MidiBuffer held;
    for (int note = 0; note < 8; ++note)
    {
        held.addEvent(juce::MidiMessage::noteOn(2, 40 + note, static_cast<juce::uint8>(90)), note);
        held.addEvent(juce::MidiMessage::noteOn(1, 70 + note, static_cast<juce::uint8>(90)), 8 + note);
    }
    if (const int result = render(burst, block, held))
        return result;
    const int partTwo = burst.getActiveVoiceCountForPart(2);
    juce::MidiBuffer flood;
    for (int note = 0; note < 9; ++note)
        flood.addEvent(juce::MidiMessage::noteOn(1, 20 + note, static_cast<juce::uint8>(100)), note);
    if (const int result = render(burst, block, flood))
        return result;
    if (burst.getActiveVoiceCountForPart(1) != 8 || burst.getActiveVoiceCountForPart(2) != partTwo)
        return fail("a steal burst changed another part or the part voice count");
    if (burst.getActiveTailCount() > 8)
        return fail("tail count exceeded the preallocated part limit");
    for (int note = 0; note < 8; ++note)
        if (! burst.isNoteActive(2, 2, 40 + note))
            return fail("part 1 steal removed a part 2 note");
    juce::MidiBuffer staleFlood;
    staleFlood.addEvent(juce::MidiMessage::noteOff(1, 70), 0);
    if (const int result = render(burst, block, staleFlood))
        return result;
    if (burst.getActiveVoiceCountForPart(1) != 8)
        return fail("stale note off after a burst changed the voice count");

    const int sizes[] = { 1, 16, 64, 128, 256, 512, 1024 };
    for (const int size : sizes)
    {
        XenMileniumAudioProcessor sized;
        silenceMixer(sized);
        setPath(sized, "part/1/mixer/osc1", 1.0f);
        setPath(sized, "part/1/mixer/noise", 0.3f);
        sized.prepareToPlay(48000.0, size);
        juce::AudioBuffer<float> sizedBuffer(2, size);
        bool heard = false;
        const int blocks = std::max(2, 128 / size);
        for (int index = 0; index < blocks; ++index)
        {
            juce::MidiBuffer midi;
            if (index == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(100)), 0);
            if (const int result = render(sized, sizedBuffer, midi))
                return result;
            heard = heard || bufferHasEnergy(sizedBuffer);
        }
        if (! heard)
            return fail("mixer was silent at block size " + juce::String(size));
        sized.releaseResources();
    }

    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    for (const double rate : rates)
    {
        XenMileniumAudioProcessor rated;
        setPath(rated, "part/1/env/amp/attack", 0.001f);
        setPath(rated, "master/volume", 1.0f);
        rated.prepareToPlay(rate, 128);
        juce::AudioBuffer<float> ratedBuffer(2, 128);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        if (const int result = render(rated, ratedBuffer, midi))
            return result;
        if (! bufferHasEnergy(ratedBuffer))
            return fail("sample rate " + juce::String(rate) + " stayed silent");
        rated.prepareToPlay(rate * 2.0, 128);
        juce::MidiBuffer rest;
        if (const int result = render(rated, ratedBuffer, rest))
            return result;
        if (rated.getActiveVoiceCount() != 1)
            return fail("sample rate change dropped the voice");
        rated.releaseResources();
    }

    XenMileniumAudioProcessor oldPreset;
    if (! oldPreset.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"part/1/osc/1/waveSource\":\"triangle\",\"part/4/enabled\":true}}"))
        return fail("old preset was rejected");
    auto* osc1 = dynamic_cast<juce::AudioParameterChoice*>(rangedParameter(oldPreset, "part/1/osc/1/waveSource"));
    auto* osc2 = dynamic_cast<juce::AudioParameterChoice*>(rangedParameter(oldPreset, "part/1/osc/2/waveSource"));
    auto* osc1Level = dynamic_cast<juce::AudioParameterFloat*>(rangedParameter(oldPreset, "part/1/mixer/osc1"));
    auto* osc2Level = dynamic_cast<juce::AudioParameterFloat*>(rangedParameter(oldPreset, "part/1/mixer/osc2"));
    auto* direct = dynamic_cast<juce::AudioParameterFloat*>(rangedParameter(oldPreset, "part/1/osc/3/directOut"));
    auto* part4 = dynamic_cast<juce::AudioParameterBool*>(oldPreset.getValueTreeState().getParameter("xm.p4.enabled"));
    if (osc1 == nullptr || osc1->getCurrentChoiceName() != "triangle"
        || osc2 == nullptr || osc2->getCurrentChoiceName() != "sine"
        || osc1Level == nullptr || ! juce::approximatelyEqual(osc1Level->get(), 1.0f)
        || osc2Level == nullptr || ! juce::approximatelyEqual(osc2Level->get(), 0.0f)
        || direct == nullptr || ! juce::approximatelyEqual(direct->get(), 0.0f)
        || part4 == nullptr || ! part4->get())
        return fail("old preset did not keep defaults for the new mixer");

    setPath(oldPreset, "part/2/osc/3/waveSource", 1.0f);
    setPath(oldPreset, "part/2/osc/3/directOut", 0.4f);
    setPath(oldPreset, "part/2/mixer/ring", 0.25f);
    setPath(oldPreset, "part/2/mixer/noise", 0.1f);
    juce::MemoryBlock state;
    oldPreset.getStateInformation(state);
    XenMileniumAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (dynamic_cast<juce::AudioParameterChoice*>(rangedParameter(restored, "part/2/osc/3/waveSource"))->getCurrentChoiceName() != "saw"
        || ! juce::approximatelyEqual(dynamic_cast<juce::AudioParameterFloat*>(rangedParameter(restored, "part/2/osc/3/directOut"))->get(), 0.4f)
        || ! juce::approximatelyEqual(dynamic_cast<juce::AudioParameterFloat*>(rangedParameter(restored, "part/2/mixer/ring"))->get(), 0.25f)
        || ! juce::approximatelyEqual(dynamic_cast<juce::AudioParameterFloat*>(rangedParameter(restored, "part/2/mixer/noise"))->get(), 0.1f)
        || dynamic_cast<juce::AudioParameterChoice*>(rangedParameter(restored, "part/1/osc/1/waveSource"))->getCurrentChoiceName() != "triangle")
        return fail("mixer parameters did not round-trip");
    if (restored.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"part/1/osc/2/waveSource\":\"wt/analog\"}}"))
        return fail("a table id was accepted as an oscillator waveform");

    oldPreset.releaseResources();
    restored.releaseResources();
    return 0;
}
} // namespace

int runMixerAndPoolTests()
{
    if (const int sources = checkSources())
        return sources;
    if (const int steal = checkStealFadeAndOwnership())
        return steal;
    std::cout << "mixer ok\n";
    return 0;
}
