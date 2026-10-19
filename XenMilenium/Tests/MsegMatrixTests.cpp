#include "Engine/Modulation.h"
#include "Parameters/ParameterRegistry.h"
#include "PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
int fail(const juce::String& message)
{
    std::cerr << message << '\n';
    return 1;
}

bool bufferIsFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            if (! std::isfinite(buffer.getSample(channel, index)))
                return false;
    return true;
}

float meanAbs(const juce::AudioBuffer<float>& buffer, int start = 0)
{
    double sum = 0.0;
    int count = 0;
    const float* samples = buffer.getReadPointer(0);
    for (int index = start; index < buffer.getNumSamples(); ++index)
    {
        sum += std::abs(samples[index]);
        ++count;
    }
    return count > 0 ? static_cast<float>(sum / count) : 0.0f;
}

float maxDifference(const juce::AudioBuffer<float>& left, const juce::AudioBuffer<float>& right)
{
    float peak = 0.0f;
    const int count = std::min(left.getNumSamples(), right.getNumSamples());
    for (int index = 0; index < count; ++index)
        peak = std::max(peak, std::abs(left.getSample(0, index) - right.getSample(0, index)));
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

void setChoice(XenMileniumAudioProcessor& processor, const char* path, const char* name)
{
    const auto* spec = xenmilenium::ParameterRegistry::findByStatePath(path);
    auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(processor.getValueTreeState().getParameter(spec->apvtsId));
    setPlain(parameter, static_cast<float>(xenmilenium::ParameterRegistry::indexOfChoice(*spec, name)));
}

void setBool(XenMileniumAudioProcessor& processor, const char* path, bool value)
{
    const auto* spec = xenmilenium::ParameterRegistry::findByStatePath(path);
    auto* parameter = processor.getValueTreeState().getParameter(spec->apvtsId);
    parameter->setValue(value ? 1.0f : 0.0f);
}

void silenceCarrier(XenMileniumAudioProcessor& processor, int part = 1)
{
    const auto prefix = "part/" + juce::String(part) + "/";
    setPath(processor, (prefix + "mixer/osc1").toRawUTF8(), 0.0f);
    setPath(processor, (prefix + "mixer/osc2").toRawUTF8(), 0.0f);
    setPath(processor, (prefix + "mixer/ring").toRawUTF8(), 0.0f);
    setPath(processor, (prefix + "mixer/noise").toRawUTF8(), 0.0f);
    setPath(processor, (prefix + "osc/3/directOut").toRawUTF8(), 0.0f);
}

void prepareFastAmp(XenMileniumAudioProcessor& processor)
{
    setPath(processor, "part/1/env/amp/attack", 0.001f);
    setPath(processor, "part/1/env/amp/decay", 0.2f);
    setPath(processor, "part/1/env/amp/sustain", 1.0f);
    setPath(processor, "part/1/env/amp/release", 0.5f);
    setPath(processor, "master/volume", 1.0f);
}

void hold(XenMileniumAudioProcessor& processor, int samples, int block = 256)
{
    int left = samples;
    while (left > 0)
    {
        juce::AudioBuffer<float> buffer(2, std::min(block, left));
        juce::MidiBuffer midi;
        processor.processBlock(buffer, midi);
        left -= buffer.getNumSamples();
    }
}

int renderNote(XenMileniumAudioProcessor& processor, juce::AudioBuffer<float>& buffer, int note, int channel, float velocity)
{
    for (int index = 0; index < buffer.getNumChannels(); ++index)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample(index, sample, std::nanf(""));
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(channel, note, velocity), 0);
    processor.processBlock(buffer, midi);
    if (! bufferIsFinite(buffer))
        return fail("non-finite sample");
    return 0;
}

juce::AudioBuffer<float> capture(XenMileniumAudioProcessor& processor, int samples, int note, int channel = 1, float velocity = 1.0f)
{
    processor.releaseResources();
    processor.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> buffer(2, samples);
    renderNote(processor, buffer, note, channel, velocity);
    return buffer;
}

int checkShapes()
{
    using namespace xenmilenium;
    MsegShape ramp;
    ramp.count = 1;
    ramp.segments[0] = { 1.0f, 1.0f };
    const auto start = evaluateMseg(ramp, 0.0);
    const auto middle = evaluateMseg(ramp, 0.5);
    const auto end = evaluateMseg(ramp, 1.0);
    if (start.finished || std::abs(start.value) > 1.0e-6f)
        return fail("linear MSEG does not start at zero");
    if (middle.finished || std::abs(middle.value - 0.5f) > 1.0e-5f)
        return fail("linear MSEG is not halfway at half duration");
    if (! end.finished || std::abs(end.value - 1.0f) > 1.0e-5f)
        return fail("one-shot MSEG does not hold the end level at the segment end");
    const auto past = evaluateMseg(ramp, 3.0);
    if (! past.finished || std::abs(past.value - 1.0f) > 1.0e-5f)
        return fail("one-shot MSEG did not keep the end level");

    MsegShape negative;
    negative.count = 2;
    negative.segments[0] = { 0.0f, -0.25f };
    negative.segments[1] = { 1.0f, -1.0f };
    const auto jumped = evaluateMseg(negative, 0.0);
    const auto fallen = evaluateMseg(negative, 0.5);
    if (jumped.finished || std::abs(jumped.value + 0.25f) > 1.0e-5f)
        return fail("zero-duration MSEG segment did not jump");
    if (std::abs(fallen.value + 0.625f) > 1.0e-4f)
        return fail("negative MSEG segment is not linear");
    if (std::abs(msegDuration(negative) - 1.0) > 1.0e-6)
        return fail("MSEG duration counted a zero-length segment");

    MsegShape empty;
    if (! evaluateMseg(empty, 0.2).finished || std::abs(evaluateMseg(empty, 0.2).value) > 1.0e-6f)
        return fail("empty MSEG is not silent");

    if (std::abs(noteKeyValue(60)) > 1.0e-6f || std::abs(noteKeyValue(0) + 1.0f) > 1.0e-6f || std::abs(noteKeyValue(120) - 1.0f) > 1.0e-6f
        || std::abs(noteKeyValue(127) - 1.0f) > 1.0e-6f)
        return fail("note key normalisation is not -1..+1 around MIDI 60");

    ModSourceValues sources;
    sources.velocity = 1.0f;
    MatrixSlotControl slots[3] {};
    slots[0].enabled = true;
    slots[0].source = static_cast<int>(ModSource::noteVelocity);
    slots[0].destination = static_cast<int>(ModDest::mixerOsc2);
    slots[0].amount = 0.2f;
    slots[1] = slots[0];
    slots[1].amount = 0.3f;
    const auto summed = sumMatrix(slots, 2, sources);
    slots[0].amount = 0.3f;
    slots[1].amount = 0.2f;
    const auto swapped = sumMatrix(slots, 2, sources);
    if (std::abs(summed.osc2 - 0.5f) > 1.0e-5f || std::abs(swapped.osc2 - summed.osc2) > 1.0e-5f)
        return fail("matrix slots did not sum independently of order");

    slots[0].amount = 0.0f;
    slots[1].enabled = false;
    slots[1].amount = 1.0f;
    const auto silent = sumMatrix(slots, 2, sources);
    if (std::abs(silent.osc2) > 1.0e-6f || std::abs(silent.pitchSemitones) > 1.0e-6f)
        return fail("zero amount or a disabled slot changed the matrix sum");

    MatrixSlotControl cutoffSlots[3] {};
    for (auto& slot : cutoffSlots)
    {
        slot.enabled = true;
        slot.source = static_cast<int>(ModSource::noteVelocity);
        slot.destination = static_cast<int>(ModDest::filterCutoff);
        slot.amount = 1.0f;
    }
    const auto clampedCutoff = sumMatrix(cutoffSlots, 3, sources);
    if (std::abs(clampedCutoff.cutoffSemitones - 96.0f) > 1.0e-4f)
        return fail("cutoff matrix sum was not clamped to 96 semitones");

    MatrixSlotControl pitchSlots[5] {};
    for (auto& slot : pitchSlots)
    {
        slot.enabled = true;
        slot.source = static_cast<int>(ModSource::modWheel);
        slot.destination = static_cast<int>(ModDest::pitch);
        slot.amount = 1.0f;
    }
    sources.modWheel = 1.0f;
    const auto clampedPitch = sumMatrix(pitchSlots, 5, sources);
    if (std::abs(clampedPitch.pitchSemitones - 48.0f) > 1.0e-4f)
        return fail("pitch matrix sum was not clamped to 48 semitones");

    MatrixSlotControl broken = slots[0];
    broken.enabled = true;
    broken.amount = std::numeric_limits<float>::quiet_NaN();
    broken.source = static_cast<int>(ModSource::noteVelocity);
    broken.destination = static_cast<int>(ModDest::pitch);
    const auto skipped = sumMatrix(&broken, 1, sources);
    if (std::abs(skipped.pitchSemitones) > 1.0e-6f)
        return fail("non-finite matrix amount was applied");

    const auto parsed = parseMsegBank(juce::JSON::parse(juce::String(R"({"parameters":{
        "part/1/mseg/2/points":[{"duration":"bad","level":1},{"duration":1,"level":-0.5,"curve":"keep"},{"duration":0,"level":4}],
        "part/2/mseg/1/points":[{"duration":0.25,"level":0.5}],
        "part/8/mseg/4/points":"not-an-array"
    }})")));
    if (parsed.shapes[1].count != 2 || std::abs(parsed.shapes[1].segments[0].level + 0.5f) > 1.0e-5f || parsed.shapes[1].segments[1].level > 1.0f)
        return fail("invalid MSEG points were not skipped or clamped");
    if (parsed.shapes[msegCount].count != 1 || parsed.shapes[msegModuleCount - 1].count != 0 || parsed.shapes[0].count != 0)
        return fail("MSEG points crossed into another part or module");

    return 0;
}

int checkRegistryContract()
{
    using namespace xenmilenium;
    const auto* source = ParameterRegistry::findByStatePath("part/1/matrix/slot/1/source");
    const auto* destination = ParameterRegistry::findByStatePath("part/1/matrix/slot/1/destination");
    if (source == nullptr || destination == nullptr)
        return fail("matrix source or destination is missing");
    if (ParameterRegistry::indexOfChoice(*source, "ampEnv") != static_cast<int>(ModSource::ampEnv)
        || ParameterRegistry::indexOfChoice(*source, "modWheel") != static_cast<int>(ModSource::modWheel)
        || ParameterRegistry::indexOfChoice(*source, "mseg4") != static_cast<int>(ModSource::mseg4)
        || ParameterRegistry::indexOfChoice(*destination, "pitch") != static_cast<int>(ModDest::pitch)
        || ParameterRegistry::indexOfChoice(*destination, "osc/3/waveSource") != static_cast<int>(ModDest::osc3Wave))
        return fail("matrix choice order does not match the DSP enums");
    if (ParameterRegistry::indexOfChoice(*source, "pitchWheel") >= 0 || ParameterRegistry::indexOfChoice(*source, "constant") >= 0)
        return fail("an unimplemented matrix source was registered");

    int modes = 0;
    int slots = 0;
    for (int part = 1; part <= 8; ++part)
    {
        for (int mseg = 1; mseg <= 4; ++mseg)
        {
            const auto path = "part/" + juce::String(part) + "/mseg/" + juce::String(mseg) + "/mode";
            const auto* spec = ParameterRegistry::findByStatePath(path);
            if (spec == nullptr || spec->apvtsId != "xm.p" + juce::String(part) + ".m" + juce::String(mseg) + ".mode")
                return fail("MSEG mode id is missing: " + path);
            ++modes;
        }
        for (int slot = 1; slot <= 32; ++slot)
        {
            const auto base = "part/" + juce::String(part) + "/matrix/slot/" + juce::String(slot) + "/";
            const auto id = "xm.p" + juce::String(part) + ".s" + juce::String(slot) + ".";
            if (ParameterRegistry::findByStatePath(base + "source") == nullptr
                || ParameterRegistry::findByStatePath(base + "destination") == nullptr
                || ParameterRegistry::findByStatePath(base + "amount") == nullptr
                || ParameterRegistry::findByStatePath(base + "enable") == nullptr)
                return fail("matrix slot is missing: " + base);
            if (ParameterRegistry::findByApvtsId(id + "src") == nullptr || ParameterRegistry::findByApvtsId(id + "on") == nullptr)
                return fail("matrix slot id is missing: " + id);
            ++slots;
        }
    }
    if (modes != 32 || slots != 256)
        return fail("expected 32 MSEG modes and 256 matrix slots");

    const auto* enable = ParameterRegistry::findByStatePath("part/8/matrix/slot/32/enable");
    if (enable == nullptr || enable->defaultBool)
        return fail("matrix enable does not default off");
    return 0;
}

void programSlot(XenMileniumAudioProcessor& processor, int part, int slot, const char* source, const char* destination, float amount, bool enabled)
{
    const auto base = "part/" + juce::String(part) + "/matrix/slot/" + juce::String(slot) + "/";
    setChoice(processor, (base + "source").toRawUTF8(), source);
    setChoice(processor, (base + "destination").toRawUTF8(), destination);
    setPath(processor, (base + "amount").toRawUTF8(), amount);
    setBool(processor, (base + "enable").toRawUTF8(), enabled);
}

int checkZeroAndSum()
{
    XenMileniumAudioProcessor dry;
    prepareFastAmp(dry);
    const auto plain = capture(dry, 2048, 60);

    XenMileniumAudioProcessor zeroAmount;
    prepareFastAmp(zeroAmount);
    programSlot(zeroAmount, 1, 1, "modWheel", "pitch", 0.0f, true);
    const auto zeroed = capture(zeroAmount, 2048, 60);
    if (maxDifference(plain, zeroed) > 1.0e-5f)
        return fail("zero matrix amount changed the dry note");

    XenMileniumAudioProcessor disabled;
    prepareFastAmp(disabled);
    programSlot(disabled, 1, 4, "modWheel", "pitch", 1.0f, false);
    const auto ignored = capture(disabled, 2048, 60);
    if (maxDifference(plain, ignored) > 1.0e-5f)
        return fail("a disabled matrix slot changed the dry note");

    auto renderVelocity = [](float first, float second)
    {
        XenMileniumAudioProcessor processor;
        prepareFastAmp(processor);
        silenceCarrier(processor);
        programSlot(processor, 1, 1, "noteVelocity", "mixer/osc2", first, true);
        programSlot(processor, 1, 2, "noteVelocity", "mixer/osc2", second, true);
        return capture(processor, 2048, 60, 1, 1.0f);
    };
    const auto split = renderVelocity(0.2f, 0.3f);
    const auto together = renderVelocity(0.5f, 0.0f);
    const auto swapped = renderVelocity(0.3f, 0.2f);
    if (meanAbs(split) < 0.01f || std::abs(meanAbs(split) - meanAbs(together)) > 0.01f || maxDifference(split, swapped) > 1.0e-3f)
        return fail("two matrix slots did not sum to one slot of the same depth");

    XenMileniumAudioProcessor saturated;
    prepareFastAmp(saturated);
    silenceCarrier(saturated);
    for (int slot = 1; slot <= 4; ++slot)
        programSlot(saturated, 1, slot, "noteVelocity", "mixer/osc2", 1.0f, true);
    const auto limited = capture(saturated, 1024, 60, 1, 1.0f);
    XenMileniumAudioProcessor one;
    prepareFastAmp(one);
    silenceCarrier(one);
    programSlot(one, 1, 1, "noteVelocity", "mixer/osc2", 1.0f, true);
    const auto single = capture(one, 1024, 60, 1, 1.0f);
    if (! bufferIsFinite(limited) || std::abs(meanAbs(limited) - meanAbs(single)) > 0.02f)
        return fail("stacked unit destinations were not clamped");
    return 0;
}

int checkRoutesStaySingle()
{
    auto renderPitch = [](float hardWired, float matrixAmount, bool enabled)
    {
        XenMileniumAudioProcessor processor;
        prepareFastAmp(processor);
        setPath(processor, "part/1/lfo/2/toPitch", hardWired);
        setPath(processor, "part/1/lfo/2/rateHz", 5.0f);
        programSlot(processor, 1, 1, "lfo2", "pitch", matrixAmount, enabled);
        return capture(processor, 4096, 60);
    };
    const auto hardOnly = renderPitch(8.0f, 0.0f, false);
    const auto hardAndZero = renderPitch(8.0f, 0.0f, true);
    const auto dry = renderPitch(0.0f, 0.0f, false);
    const auto both = renderPitch(8.0f, 1.0f, true);
    if (maxDifference(hardOnly, hardAndZero) > 1.0e-4f)
        return fail("an empty matrix slot doubled LFO 2 pitch");
    if (maxDifference(hardOnly, dry) < 1.0e-3f || maxDifference(both, hardOnly) < 1.0e-3f)
        return fail("LFO 2 pitch route or its matrix addition did not change audio");

    auto renderCutoff = [](float hardWired, float matrixAmount)
    {
        XenMileniumAudioProcessor processor;
        prepareFastAmp(processor);
        setChoice(processor, "part/1/osc/1/waveSource", "saw");
        setPath(processor, "part/1/filter/mix", 1.0f);
        setPath(processor, "part/1/filter/cutoff", 500.0f);
        setChoice(processor, "part/1/filter/type", "lp12");
        setPath(processor, "part/1/lfo/1/toCutoff", hardWired);
        setPath(processor, "part/1/lfo/1/rateHz", 6.0f);
        programSlot(processor, 1, 2, "lfo1", "filter/cutoff", matrixAmount, true);
        return capture(processor, 4096, 60);
    };
    const auto cutoffOnly = renderCutoff(36.0f, 0.0f);
    const auto cutoffDry = renderCutoff(0.0f, 0.0f);
    const auto cutoffBoth = renderCutoff(36.0f, 1.0f);
    if (maxDifference(cutoffOnly, cutoffDry) < 1.0e-3f || maxDifference(cutoffBoth, cutoffOnly) < 1.0e-3f)
        return fail("LFO 1 cutoff route was doubled or did not add the matrix");

    auto renderEnvelope = [](float amount, float matrixAmount)
    {
        XenMileniumAudioProcessor processor;
        prepareFastAmp(processor);
        setChoice(processor, "part/1/osc/1/waveSource", "saw");
        setPath(processor, "part/1/filter/mix", 1.0f);
        setPath(processor, "part/1/filter/cutoff", 400.0f);
        setChoice(processor, "part/1/filter/type", "lp12");
        setPath(processor, "part/1/env/filter/amount", amount);
        setPath(processor, "part/1/env/filter/attack", 0.08f);
        programSlot(processor, 1, 3, "filterEnv", "filter/cutoff", matrixAmount, true);
        return capture(processor, 4096, 60);
    };
    const auto envelopeOnly = renderEnvelope(24.0f, 0.0f);
    const auto envelopeDry = renderEnvelope(0.0f, 0.0f);
    const auto envelopeBoth = renderEnvelope(24.0f, 1.0f);
    if (maxDifference(envelopeOnly, envelopeDry) < 1.0e-3f || maxDifference(envelopeBoth, envelopeOnly) < 1.0e-3f)
        return fail("filter envelope cutoff route was doubled or did not add the matrix");
    return 0;
}

int checkSourcesAndDestinations()
{
    {
        XenMileniumAudioProcessor processor;
        prepareFastAmp(processor);
        setPath(processor, "part/1/env/amp/attack", 0.2f);
        silenceCarrier(processor);
        programSlot(processor, 1, 1, "ampEnv", "mixer/osc2", 1.0f, true);
        processor.releaseResources();
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> early(2, 256);
        renderNote(processor, early, 60, 1, 1.0f);
        hold(processor, 8000, 256);
        juce::AudioBuffer<float> late(2, 1024);
        juce::MidiBuffer midi;
        processor.processBlock(late, midi);
        if (meanAbs(late) < meanAbs(early) * 4.0f)
            return fail("amp envelope did not raise a matrix level");
    }

    {
        XenMileniumAudioProcessor processor;
        prepareFastAmp(processor);
        setPath(processor, "part/1/env/filter/attack", 0.2f);
        silenceCarrier(processor);
        programSlot(processor, 1, 1, "filterEnv", "mixer/osc2", 1.0f, true);
        processor.releaseResources();
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> early(2, 256);
        renderNote(processor, early, 60, 1, 1.0f);
        hold(processor, 8000, 256);
        juce::AudioBuffer<float> late(2, 1024);
        juce::MidiBuffer midi;
        processor.processBlock(late, midi);
        if (meanAbs(late) < 0.02f || meanAbs(late) < meanAbs(early) * 4.0f)
            return fail("filter envelope did not modulate through the matrix");
    }

    {
        XenMileniumAudioProcessor quiet;
        prepareFastAmp(quiet);
        setPath(quiet, "part/1/lfo/3/rateHz", 7.0f);
        const auto without = capture(quiet, 4800, 60);
        XenMileniumAudioProcessor moving;
        prepareFastAmp(moving);
        setPath(moving, "part/1/lfo/3/rateHz", 7.0f);
        programSlot(moving, 1, 1, "lfo3", "pitch", 1.0f, true);
        const auto with = capture(moving, 4800, 60);
        if (maxDifference(without, with) < 1.0e-2f)
            return fail("LFO 3 did not change pitch through the matrix");
    }

    {
        XenMileniumAudioProcessor processor;
        prepareFastAmp(processor);
        silenceCarrier(processor);
        setChoice(processor, "part/1/lfo/4/polarity", "unipolar");
        setPath(processor, "part/1/lfo/4/rateHz", 5.0f);
        programSlot(processor, 1, 1, "lfo4", "mixer/osc2", 1.0f, true);
        const auto with = capture(processor, 4800, 60);
        programSlot(processor, 1, 1, "lfo4", "mixer/osc2", 0.0f, true);
        const auto without = capture(processor, 4800, 60);
        if (meanAbs(with) < 0.01f || meanAbs(without) > 1.0e-5f)
            return fail("LFO 4 did not change a mixer level through the matrix");
    }

    {
        XenMileniumAudioProcessor low;
        prepareFastAmp(low);
        silenceCarrier(low);
        programSlot(low, 1, 1, "noteVelocity", "mixer/osc2", 1.0f, true);
        const auto soft = capture(low, 2048, 60, 1, 20.0f / 127.0f);
        const auto hard = capture(low, 2048, 72, 1, 1.0f);
        if (meanAbs(hard) < meanAbs(soft) * 3.0f)
            return fail("velocity did not scale the matrix destination");
    }

    {
        XenMileniumAudioProcessor processor;
        prepareFastAmp(processor);
        silenceCarrier(processor);
        programSlot(processor, 1, 1, "noteKey", "mixer/osc2", 1.0f, true);
        const auto centre = capture(processor, 2048, 60);
        const auto upper = capture(processor, 2048, 96);
        if (meanAbs(centre) > 1.0e-5f || meanAbs(upper) < 0.02f)
            return fail("note key did not open the level around MIDI 60");
    }

    {
        XenMileniumAudioProcessor processor;
        prepareFastAmp(processor);
        programSlot(processor, 1, 1, "modWheel", "pitch", 1.0f, true);
        processor.releaseResources();
        processor.prepareToPlay(48000.0, 2048);
        juce::AudioBuffer<float> down(2, 2048);
        juce::MidiBuffer low;
        low.addEvent(juce::MidiMessage::controllerEvent(1, 1, 0), 0);
        low.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(110)), 0);
        processor.processBlock(down, low);
        processor.releaseResources();
        processor.prepareToPlay(48000.0, 2048);
        juce::AudioBuffer<float> up(2, 2048);
        juce::MidiBuffer high;
        high.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);
        high.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(110)), 0);
        processor.processBlock(up, high);
        if (! bufferIsFinite(down) || ! bufferIsFinite(up) || maxDifference(down, up) < 1.0e-2f || ! processor.isNoteActive(1, 1, 60))
            return fail("mod wheel CC1 did not change pitch");
    }

    const struct DestinationCase
    {
        const char* destination;
        const char* wave;
        float osc1;
        float osc2;
        float ring;
        float noise;
        float direct;
        float mix;
        float cutoff;
        const char* type;
    } destinations[] = {
        { "pitch", "sine", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2000.0f, "lp12" },
        { "filter/cutoff", "saw", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 280.0f, "lp12" },
        { "filter/resonance", "saw", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 440.0f, "bp12" },
        { "filter/drive", "sine", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 4000.0f, "lp12" },
        { "filter/mix", "saw", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 180.0f, "lp12" },
        { "mixer/osc1", "sine", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2000.0f, "lp12" },
        { "mixer/osc2", "sine", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2000.0f, "lp12" },
        { "mixer/ring", "sine", 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2000.0f, "lp12" },
        { "mixer/noise", "sine", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2000.0f, "lp12" },
        { "osc/3/directOut", "sine", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2000.0f, "lp12" },
        { "osc/1/waveSource", "sine", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2000.0f, "lp12" },
        { "osc/2/waveSource", "sine", 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2000.0f, "lp12" },
        { "osc/3/waveSource", "sine", 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 2000.0f, "lp12" },
    };

    for (const auto& item : destinations)
    {
        auto renderWheel = [&](int wheel)
        {
            XenMileniumAudioProcessor processor;
            prepareFastAmp(processor);
            setChoice(processor, "part/1/osc/1/waveSource", item.wave);
            setChoice(processor, "part/1/osc/2/waveSource", item.wave);
            setChoice(processor, "part/1/osc/3/waveSource", item.wave);
            setPath(processor, "part/1/mixer/osc1", item.osc1);
            setPath(processor, "part/1/mixer/osc2", item.osc2);
            setPath(processor, "part/1/mixer/ring", item.ring);
            setPath(processor, "part/1/mixer/noise", item.noise);
            setPath(processor, "part/1/osc/3/directOut", item.direct);
            setPath(processor, "part/1/filter/mix", item.mix);
            setPath(processor, "part/1/filter/cutoff", item.cutoff);
            setChoice(processor, "part/1/filter/type", item.type);
            programSlot(processor, 1, 8, "modWheel", item.destination, 1.0f, true);
            processor.releaseResources();
            processor.prepareToPlay(48000.0, 2048);
            juce::AudioBuffer<float> buffer(2, 2048);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, wheel), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(120)), 0);
            processor.processBlock(buffer, midi);
            return buffer;
        };
        const auto low = renderWheel(0);
        const auto high = renderWheel(127);
        if (! bufferIsFinite(low) || ! bufferIsFinite(high) || maxDifference(low, high) < 1.0e-4f)
            return fail(juce::String("destination did not respond to mod wheel: ") + item.destination);
    }

    return 0;
}

int checkMsegPlayback()
{
    const auto points = juce::String(R"({"formatVersion":1,"parameters":{"part/1/mseg/1/points":[{"duration":0.05,"level":1}]}})");
    {
        XenMileniumAudioProcessor processor;
        if (! processor.restoreStateDocument(points))
            return fail("MSEG point document was rejected");
        prepareFastAmp(processor);
        silenceCarrier(processor);
        setChoice(processor, "part/1/mseg/1/mode", "noteOn");
        programSlot(processor, 1, 1, "mseg1", "mixer/osc2", 1.0f, true);
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> early(2, 256);
        if (renderNote(processor, early, 60, 1, 1.0f))
            return 1;
        hold(processor, 3600, 256);
        juce::AudioBuffer<float> late(2, 512);
        juce::MidiBuffer midi;
        processor.processBlock(late, midi);
        if (meanAbs(late) < 0.02f || meanAbs(late) < meanAbs(early) * 5.0f)
            return fail("one-shot MSEG did not rise");
        hold(processor, 8000, 256);
        if (! processor.isNoteActive(1, 1, 60))
            return fail("MSEG end released the MIDI voice");
    }

    {
        XenMileniumAudioProcessor oneShot;
        oneShot.restoreStateDocument(points);
        prepareFastAmp(oneShot);
        silenceCarrier(oneShot);
        setChoice(oneShot, "part/1/mseg/1/mode", "noteOn");
        programSlot(oneShot, 1, 1, "mseg1", "mixer/osc2", 1.0f, true);
        oneShot.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> started(2, 64);
        renderNote(oneShot, started, 60, 1, 1.0f);
        hold(oneShot, 2880, 256);
        juce::AudioBuffer<float> held(2, 512);
        juce::MidiBuffer midi;
        oneShot.processBlock(held, midi);

        XenMileniumAudioProcessor looped;
        looped.restoreStateDocument(points);
        prepareFastAmp(looped);
        silenceCarrier(looped);
        setChoice(looped, "part/1/mseg/1/mode", "loop");
        programSlot(looped, 1, 1, "mseg1", "mixer/osc2", 1.0f, true);
        looped.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> loopStart(2, 64);
        renderNote(looped, loopStart, 60, 1, 1.0f);
        hold(looped, 2880, 256);
        juce::AudioBuffer<float> wrapped(2, 512);
        juce::MidiBuffer loopMidi;
        looped.processBlock(wrapped, loopMidi);
        if (meanAbs(held) < 0.02f || meanAbs(wrapped) > meanAbs(held) * 0.75f)
            return fail("loop did not return below the one-shot hold");
        if (! looped.isNoteActive(1, 1, 60))
            return fail("looping MSEG released the voice");
    }

    {
        const auto falling = juce::String(R"({"formatVersion":1,"parameters":{"part/1/mseg/1/points":[{"duration":0.04,"level":-1}]}})");
        XenMileniumAudioProcessor processor;
        processor.restoreStateDocument(falling);
        prepareFastAmp(processor);
        setChoice(processor, "part/1/mseg/1/mode", "noteOn");
        programSlot(processor, 1, 1, "mseg1", "pitch", 1.0f, true);
        const auto moved = capture(processor, 4096, 60);
        programSlot(processor, 1, 1, "mseg1", "pitch", 0.0f, true);
        const auto still = capture(processor, 4096, 60);
        if (maxDifference(moved, still) < 1.0e-3f)
            return fail("negative MSEG level did not move pitch");
    }

    {
        XenMileniumAudioProcessor processor;
        processor.restoreStateDocument(points);
        prepareFastAmp(processor);
        silenceCarrier(processor);
        setChoice(processor, "part/1/mseg/1/mode", "noteOff");
        setPath(processor, "part/1/env/amp/release", 0.4f);
        programSlot(processor, 1, 1, "mseg1", "mixer/osc2", 1.0f, true);
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> on(2, 256);
        renderNote(processor, on, 60, 1, 1.0f);
        hold(processor, 4000, 256);
        juce::AudioBuffer<float> held(2, 512);
        juce::MidiBuffer empty;
        processor.processBlock(held, empty);
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        juce::AudioBuffer<float> released(2, 256);
        processor.processBlock(released, off);
        hold(processor, 1500, 256);
        juce::AudioBuffer<float> after(2, 512);
        processor.processBlock(after, empty);
        if (meanAbs(held) > 1.0e-5f || meanAbs(after) < 0.02f || ! processor.isNoteActive(1, 1, 60))
            return fail("note-off MSEG did not wait for Note Off");
    }

    {
        XenMileniumAudioProcessor processor;
        processor.restoreStateDocument(points);
        prepareFastAmp(processor);
        silenceCarrier(processor);
        setChoice(processor, "part/1/mseg/1/mode", "noteOff");
        programSlot(processor, 1, 1, "mseg1", "mixer/osc2", 1.0f, true);
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> on(2, 64);
        renderNote(processor, on, 60, 1, 1.0f);
        juce::MidiBuffer pedal;
        pedal.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
        pedal.addEvent(juce::MidiMessage::noteOff(1, 60), 1);
        juce::AudioBuffer<float> held(2, 256);
        processor.processBlock(held, pedal);
        hold(processor, 2000, 256);
        juce::AudioBuffer<float> waiting(2, 256);
        juce::MidiBuffer empty;
        processor.processBlock(waiting, empty);
        juce::MidiBuffer lift;
        lift.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        juce::AudioBuffer<float> lifted(2, 64);
        processor.processBlock(lifted, lift);
        hold(processor, 2000, 256);
        juce::AudioBuffer<float> after(2, 512);
        processor.processBlock(after, empty);
        if (meanAbs(waiting) > 1.0e-5f || meanAbs(after) < 0.02f)
            return fail("CC64 did not delay the note-off MSEG");
    }

    return 0;
}

int checkVoiceBoundaries()
{
    const auto points = juce::String(R"({"formatVersion":1,"parameters":{"part/1/mseg/1/points":[{"duration":0.2,"level":1}],"part/2/mseg/1/points":[{"duration":0,"level":1}]}})");
    XenMileniumAudioProcessor processor;
    if (! processor.restoreStateDocument(points))
        return fail("steal preset was rejected");
    prepareFastAmp(processor);
    silenceCarrier(processor);
    setBool(processor, "part/1/mono", true);
    setChoice(processor, "part/1/mseg/1/mode", "noteOn");
    programSlot(processor, 1, 1, "mseg1", "mixer/osc2", 1.0f, true);
    programSlot(processor, 2, 1, "mseg1", "mixer/osc2", 1.0f, true);
    setPath(processor, "part/2/mixer/osc1", 0.0f);
    processor.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> first(2, 256);
    renderNote(processor, first, 60, 1, 1.0f);
    hold(processor, 4800, 256);
    juce::AudioBuffer<float> before(2, 512);
    juce::MidiBuffer empty;
    processor.processBlock(before, empty);
    juce::AudioBuffer<float> stolen(2, 256);
    renderNote(processor, stolen, 67, 1, 1.0f);
    hold(processor, 200, 64);
    juce::AudioBuffer<float> after(2, 512);
    processor.processBlock(after, empty);
    if (meanAbs(before) < 0.03f || meanAbs(after) > meanAbs(before) * 0.45f)
        return fail("a stolen note kept the previous MSEG level");
    if (processor.getActiveVoiceCountForPart(1) != 1 || processor.getActiveVoiceCountForPart(2) != 0 || processor.getActiveTailCount() != 0)
        return fail("steal crossed parts or left a tail");
    if (! processor.isNoteActive(1, 1, 67) || processor.isNoteActive(1, 1, 60))
        return fail("steal did not replace the old note");

    XenMileniumAudioProcessor retrigger;
    retrigger.restoreStateDocument(juce::String(R"({"formatVersion":1,"parameters":{"part/1/mseg/1/points":[{"duration":0.04,"level":1}]}})"));
    prepareFastAmp(retrigger);
    silenceCarrier(retrigger);
    setChoice(retrigger, "part/1/mseg/1/mode", "noteOn");
    programSlot(retrigger, 1, 1, "mseg1", "mixer/osc2", 1.0f, true);
    retrigger.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> start(2, 64);
    renderNote(retrigger, start, 60, 1, 1.0f);
    hold(retrigger, 4000, 256);
    juce::AudioBuffer<float> full(2, 256);
    retrigger.processBlock(full, empty);
    juce::AudioBuffer<float> again(2, 64);
    renderNote(retrigger, again, 60, 1, 1.0f);
    hold(retrigger, 128, 64);
    juce::AudioBuffer<float> restarted(2, 256);
    retrigger.processBlock(restarted, empty);
    if (meanAbs(full) < 0.02f || meanAbs(restarted) > meanAbs(full) * 0.4f)
        return fail("repeated Note On did not restart the MSEG");

    setPath(processor, "part/1/env/amp/release", 0.05f);
    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 67), 0);
    juce::AudioBuffer<float> release(2, 256);
    processor.processBlock(release, off);
    hold(processor, 8000, 256);
    if (processor.getActiveVoiceCount() != 0)
        return fail("amp release did not free a voice after the MSEG");

    const double rates[] = { 44100.0, 96000.0, 192000.0 };
    const int blocks[] = { 1, 64, 1024 };
    for (double rate : rates)
    {
        for (int block : blocks)
        {
            XenMileniumAudioProcessor sized;
            prepareFastAmp(sized);
            programSlot(sized, 1, 1, "modWheel", "pitch", 0.7f, true);
            programSlot(sized, 1, 2, "noteVelocity", "filter/cutoff", 1.0f, true);
            setPath(sized, "part/1/filter/mix", 1.0f);
            sized.prepareToPlay(rate, block);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 100), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
            int seen = 0;
            while (seen < 128)
            {
                const int count = std::min(block, 128 - seen);
                juce::AudioBuffer<float> buffer(2, count);
                buffer.clear();
                sized.processBlock(buffer, midi);
                midi.clear();
                if (! bufferIsFinite(buffer))
                    return fail("matrix render was non-finite");
                seen += count;
            }
            if (! sized.isNoteActive(1, 1, 60))
                return fail("short blocks dropped the note");
            sized.releaseResources();
        }
    }
    return 0;
}

int checkPartsAndPresets()
{
    XenMileniumAudioProcessor isolated;
    prepareFastAmp(isolated);
    setChoice(isolated, "part/1/midiChannel", "1");
    const auto baseline = capture(isolated, 2048, 60);
    programSlot(isolated, 8, 32, "modWheel", "pitch", 1.0f, true);
    programSlot(isolated, 2, 1, "mseg1", "mixer/osc2", 1.0f, true);
    setPath(isolated, "part/2/mixer/osc2", 1.0f);
    const auto untouched = capture(isolated, 2048, 60);
    if (maxDifference(baseline, untouched) > 1.0e-4f || isolated.getActiveVoiceCountForPart(2) != 0 || isolated.getActiveVoiceCountForPart(8) != 0)
        return fail("another part's matrix changed part 1");

    XenMileniumAudioProcessor partEight;
    prepareFastAmp(partEight);
    setChoice(partEight, "part/1/midiChannel", "1");
    setBool(partEight, "part/8/enabled", true);
    setChoice(partEight, "part/8/midiChannel", "8");
    setPath(partEight, "part/8/env/amp/attack", 0.001f);
    setPath(partEight, "part/8/env/amp/sustain", 1.0f);
    programSlot(partEight, 8, 32, "modWheel", "pitch", 1.0f, true);
    partEight.prepareToPlay(48000.0, 2048);
    juce::AudioBuffer<float> own(2, 2048);
    juce::MidiBuffer channelEight;
    channelEight.addEvent(juce::MidiMessage::controllerEvent(8, 1, 127), 0);
    channelEight.addEvent(juce::MidiMessage::noteOn(8, 60, static_cast<juce::uint8>(110)), 0);
    partEight.processBlock(own, channelEight);
    partEight.releaseResources();
    partEight.prepareToPlay(48000.0, 2048);
    juce::AudioBuffer<float> withoutWheel(2, 2048);
    juce::MidiBuffer noteOnly;
    noteOnly.addEvent(juce::MidiMessage::noteOn(8, 60, static_cast<juce::uint8>(110)), 0);
    partEight.processBlock(withoutWheel, noteOnly);
    if (! bufferIsFinite(own) || maxDifference(own, withoutWheel) < 1.0e-2f || partEight.getActiveVoiceCountForPart(1) != 0)
        return fail("part 8 did not keep its own mod wheel");

    XenMileniumAudioProcessor channelFilter;
    prepareFastAmp(channelFilter);
    setChoice(channelFilter, "part/1/midiChannel", "1");
    setBool(channelFilter, "part/2/enabled", true);
    setChoice(channelFilter, "part/2/midiChannel", "2");
    setPath(channelFilter, "part/2/env/amp/attack", 0.001f);
    setPath(channelFilter, "part/2/env/amp/sustain", 1.0f);
    programSlot(channelFilter, 2, 1, "modWheel", "pitch", 1.0f, true);
    channelFilter.prepareToPlay(48000.0, 2048);
    juce::AudioBuffer<float> foreign(2, 2048);
    juce::MidiBuffer wrongChannel;
    wrongChannel.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);
    wrongChannel.addEvent(juce::MidiMessage::noteOn(2, 64, static_cast<juce::uint8>(110)), 0);
    channelFilter.processBlock(foreign, wrongChannel);
    channelFilter.releaseResources();
    channelFilter.prepareToPlay(48000.0, 2048);
    juce::AudioBuffer<float> matched(2, 2048);
    juce::MidiBuffer rightChannel;
    rightChannel.addEvent(juce::MidiMessage::controllerEvent(2, 1, 127), 0);
    rightChannel.addEvent(juce::MidiMessage::noteOn(2, 64, static_cast<juce::uint8>(110)), 0);
    channelFilter.processBlock(matched, rightChannel);
    if (channelFilter.getActiveVoiceCountForPart(1) != 0 || maxDifference(foreign, matched) < 1.0e-2f)
        return fail("mod wheel crossed MIDI channels");

    const auto oldPreset = juce::String(R"({"formatVersion":1,"parameters":{"part/1/osc/1/waveSource":"saw","part/1/filter/mix":0.3,"part/1/lfo/2/toPitch":2.5}})");
    XenMileniumAudioProcessor legacy;
    if (! legacy.restoreStateDocument(oldPreset))
        return fail("stage 1.6 preset was rejected");
    auto* wave = dynamic_cast<juce::AudioParameterChoice*>(legacy.getValueTreeState().getParameter("xm.p1.o1.waveSource"));
    auto* mix = dynamic_cast<juce::AudioParameterFloat*>(legacy.getValueTreeState().getParameter("xm.p1.flt.mix"));
    auto* pitch = dynamic_cast<juce::AudioParameterFloat*>(legacy.getValueTreeState().getParameter("xm.p1.l2.toPitch"));
    auto* amount = dynamic_cast<juce::AudioParameterFloat*>(legacy.getValueTreeState().getParameter("xm.p1.s1.amt"));
    auto* enable = dynamic_cast<juce::AudioParameterBool*>(legacy.getValueTreeState().getParameter("xm.p1.s1.on"));
    auto* source = dynamic_cast<juce::AudioParameterChoice*>(legacy.getValueTreeState().getParameter("xm.p1.s1.src"));
    auto* mode = dynamic_cast<juce::AudioParameterChoice*>(legacy.getValueTreeState().getParameter("xm.p1.m1.mode"));
    if (wave == nullptr || wave->getCurrentChoiceName() != "saw" || mix == nullptr || std::abs(mix->get() - 0.3f) > 1.0e-4f
        || pitch == nullptr || std::abs(pitch->get() - 2.5f) > 1.0e-4f || amount == nullptr || std::abs(amount->get()) > 1.0e-6f
        || enable == nullptr || enable->get() || source == nullptr || source->getCurrentChoiceName() != "off"
        || mode == nullptr || mode->getCurrentChoiceName() != "noteOn")
        return fail("old preset did not keep its values and matrix defaults");
    if (! legacy.saveStateDocument().contains("\"formatVersion\": 1") && ! legacy.saveStateDocument().contains("\"formatVersion\":1"))
        return fail("formatVersion was not kept at 1");

    XenMileniumAudioProcessor equivalent;
    setChoice(equivalent, "part/1/osc/1/waveSource", "saw");
    setPath(equivalent, "part/1/filter/mix", 0.3f);
    setPath(equivalent, "part/1/lfo/2/toPitch", 2.5f);
    prepareFastAmp(legacy);
    prepareFastAmp(equivalent);
    const auto legacyAudio = capture(legacy, 2048, 60);
    const auto equivalentAudio = capture(equivalent, 2048, 60);
    if (maxDifference(legacyAudio, equivalentAudio) > 1.0e-4f)
        return fail("old preset did not match the same parameters with a zero matrix");

    const auto modern = juce::String(R"({"formatVersion":1,"parameters":{
        "part/3/mseg/2/mode":"loop",
        "part/3/matrix/slot/17/source":"lfo3",
        "part/3/matrix/slot/17/destination":"pitch",
        "part/3/matrix/slot/17/amount":-0.35,
        "part/3/matrix/slot/17/enable":true,
        "part/3/matrix/slot/17/polarity":"unipolar",
        "part/1/mseg/1/points":[{"duration":0.02,"level":1,"bend":true},{"duration":"bad","level":1},{"duration":0.01,"level":0.5}]
    }})");
    XenMileniumAudioProcessor modernProcessor;
    if (! modernProcessor.restoreStateDocument(modern))
        return fail("matrix preset was rejected");
    const auto saved = modernProcessor.saveStateDocument();
    XenMileniumAudioProcessor restored;
    if (! restored.restoreStateDocument(saved))
        return fail("saved matrix preset was rejected");
    auto* restoredMode = dynamic_cast<juce::AudioParameterChoice*>(restored.getValueTreeState().getParameter("xm.p3.m2.mode"));
    auto* restoredSource = dynamic_cast<juce::AudioParameterChoice*>(restored.getValueTreeState().getParameter("xm.p3.s17.src"));
    auto* restoredDestination = dynamic_cast<juce::AudioParameterChoice*>(restored.getValueTreeState().getParameter("xm.p3.s17.dst"));
    auto* restoredAmount = dynamic_cast<juce::AudioParameterFloat*>(restored.getValueTreeState().getParameter("xm.p3.s17.amt"));
    auto* restoredEnable = dynamic_cast<juce::AudioParameterBool*>(restored.getValueTreeState().getParameter("xm.p3.s17.on"));
    if (restoredMode == nullptr || restoredMode->getCurrentChoiceName() != "loop"
        || restoredSource == nullptr || restoredSource->getCurrentChoiceName() != "lfo3"
        || restoredDestination == nullptr || restoredDestination->getCurrentChoiceName() != "pitch"
        || restoredAmount == nullptr || std::abs(restoredAmount->get() + 0.35f) > 1.0e-4f
        || restoredEnable == nullptr || ! restoredEnable->get())
        return fail("matrix slot or MSEG mode was lost on save");
    juce::var parsed;
    juce::JSON::parse(restored.saveStateDocument(), parsed);
    const auto unknownParameters = parsed.getProperty("unknown", {}).getProperty("parameters", {});
    const auto* unknownObject = unknownParameters.getDynamicObject();
    const auto* parameterObject = parsed.getProperty("parameters", {}).getDynamicObject();
    const auto* storedPoints = unknownObject != nullptr ? unknownObject->getProperty("part/1/mseg/1/points").getArray() : nullptr;
    if (storedPoints == nullptr || storedPoints->size() != 3 || parameterObject == nullptr
        || parameterObject->hasProperty("part/1/mseg/1/points"))
        return fail("MSEG points were not preserved beside the parameters");
    if (! restored.saveStateDocument().contains("\"bend\"") || ! restored.saveStateDocument().contains("bad"))
        return fail("unknown MSEG point data was dropped");
    if (xenmilenium::ParameterRegistry::findByStatePath("part/3/matrix/slot/17/polarity") != nullptr)
        return fail("polarity became a parameter during save");

    XenMileniumAudioProcessor polarityA;
    polarityA.restoreStateDocument(juce::String(R"({"formatVersion":1,"parameters":{"part/1/matrix/slot/1/polarity":"unipolar"}})"));
    prepareFastAmp(polarityA);
    silenceCarrier(polarityA);
    programSlot(polarityA, 1, 1, "noteVelocity", "mixer/osc2", 1.0f, true);
    const auto unipolarLabel = capture(polarityA, 1024, 60);
    XenMileniumAudioProcessor polarityB;
    polarityB.restoreStateDocument(juce::String(R"({"formatVersion":1,"parameters":{"part/1/matrix/slot/1/polarity":"bipolar"}})"));
    prepareFastAmp(polarityB);
    silenceCarrier(polarityB);
    programSlot(polarityB, 1, 1, "noteVelocity", "mixer/osc2", 1.0f, true);
    const auto bipolarLabel = capture(polarityB, 1024, 60);
    if (maxDifference(unipolarLabel, bipolarLabel) > 1.0e-5f)
        return fail("unregistered slot polarity changed the audio");

    XenMileniumAudioProcessor omittedEnable;
    omittedEnable.restoreStateDocument(juce::String(R"({"formatVersion":1,"parameters":{"part/1/matrix/slot/1/source":"noteVelocity","part/1/matrix/slot/1/destination":"mixer/osc2","part/1/matrix/slot/1/amount":1}})"));
    prepareFastAmp(omittedEnable);
    silenceCarrier(omittedEnable);
    const auto disabledByDefault = capture(omittedEnable, 1024, 60);
    if (meanAbs(disabledByDefault) > 1.0e-5f)
        return fail("a missing enable flag turned the slot on");

    return 0;
}
} // namespace

int runMsegMatrixTests()
{
    if (const int shapes = checkShapes())
        return shapes;
    if (const int registry = checkRegistryContract())
        return registry;
    if (const int zero = checkZeroAndSum())
        return zero;
    if (const int routes = checkRoutesStaySingle())
        return routes;
    if (const int sources = checkSourcesAndDestinations())
        return sources;
    if (const int playback = checkMsegPlayback())
        return playback;
    if (const int voices = checkVoiceBoundaries())
        return voices;
    if (const int presets = checkPartsAndPresets())
        return presets;

    std::cout << "matrix ok\n";
    return 0;
}
