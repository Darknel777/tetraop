#include "Engine/MinimalVoiceCore.h"
#include "Parameters/ParameterRegistry.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdint>
#include <cstring>
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

float meanAdjacent(const juce::AudioBuffer<float>& buffer, int start = 1)
{
    double sum = 0.0;
    int count = 0;
    const float* samples = buffer.getReadPointer(0);
    for (int index = std::max(1, start); index < buffer.getNumSamples(); ++index)
    {
        sum += std::abs(samples[index] - samples[index - 1]);
        ++count;
    }
    return count > 0 ? static_cast<float>(sum / count) : 0.0f;
}

float maxAdjacent(const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    const float* samples = buffer.getReadPointer(0);
    for (int index = 1; index < buffer.getNumSamples(); ++index)
        peak = std::max(peak, std::abs(samples[index] - samples[index - 1]));
    return peak;
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

void prepareVoice(XenMileniumAudioProcessor& processor)
{
    setPath(processor, "part/1/env/amp/attack", 0.001f);
    setPath(processor, "part/1/env/amp/decay", 0.2f);
    setPath(processor, "part/1/env/amp/sustain", 1.0f);
    setPath(processor, "part/1/env/amp/release", 0.4f);
    setPath(processor, "master/volume", 1.0f);
}

int renderNote(XenMileniumAudioProcessor& processor, juce::AudioBuffer<float>& buffer, int note, int channel = 1)
{
    for (int index = 0; index < buffer.getNumChannels(); ++index)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample(index, sample, std::nanf(""));
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(110)), 0);
    processor.processBlock(buffer, midi);
    if (! bufferIsFinite(buffer))
        return fail("non-finite sample");
    return 0;
}

int renderEmpty(XenMileniumAudioProcessor& processor, juce::AudioBuffer<float>& buffer)
{
    for (int index = 0; index < buffer.getNumChannels(); ++index)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample(index, sample, std::nanf(""));
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    if (! bufferIsFinite(buffer))
        return fail("non-finite sample");
    return 0;
}

void hold(XenMileniumAudioProcessor& processor, int samples, int block)
{
    juce::AudioBuffer<float> buffer(2, block);
    int left = samples;
    while (left > 0)
    {
        buffer.setSize(2, std::min(block, left), false, false, true);
        juce::MidiBuffer midi;
        processor.processBlock(buffer, midi);
        left -= buffer.getNumSamples();
    }
}

class FixedPlayHead : public juce::AudioPlayHead
{
public:
    explicit FixedPlayHead(juce::Optional<double> bpmIn)
        : bpm(bpmIn)
    {
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm);
        return info;
    }

    juce::Optional<double> bpm;
};

int checkGenerators()
{
    using xenmilenium::MinimalVoiceCore;
    if (std::abs(MinimalVoiceCore::lfoBipolar(0, 0.0, 0.0f)) > 1.0e-5f)
        return fail("sine at phase 0 is not zero");
    if (MinimalVoiceCore::lfoBipolar(0, 0.25, 0.0f) < 0.99f)
        return fail("sine at phase 0.25 is not +1");
    if (MinimalVoiceCore::lfoBipolar(1, 0.0, 0.0f) > -0.99f || MinimalVoiceCore::lfoBipolar(1, 0.5, 0.0f) < 0.99f)
        return fail("triangle is outside -1..+1 at the peaks");
    if (MinimalVoiceCore::lfoBipolar(2, 0.0, 0.0f) > -0.99f || MinimalVoiceCore::lfoBipolar(2, 0.999, 0.0f) < 0.99f)
        return fail("saw up is not a rising ramp");
    if (MinimalVoiceCore::lfoBipolar(3, 0.0, 0.0f) < 0.99f || MinimalVoiceCore::lfoBipolar(3, 0.999, 0.0f) > -0.99f)
        return fail("saw down is not a falling ramp");
    if (MinimalVoiceCore::lfoBipolar(4, 0.25, 0.0f) < 0.99f || MinimalVoiceCore::lfoBipolar(4, 0.75, 0.0f) > -0.99f)
        return fail("square is not +1 then -1");
    if (std::abs(MinimalVoiceCore::lfoBipolar(5, 0.2, 0.35f) - 0.35f) > 1.0e-6f)
        return fail("random hold did not keep its stored value");
    if (MinimalVoiceCore::lfoBipolar(5, 0.2, 4.0f) > 1.0f || MinimalVoiceCore::lfoBipolar(4, 0.1, 0.0f) > 1.0f)
        return fail("a waveform exceeded +1");
    if (MinimalVoiceCore::lfoPolarity(-1.0f, true) > 1.0e-6f || MinimalVoiceCore::lfoPolarity(1.0f, true) < 0.99f)
        return fail("unipolar map is not 0..1");
    if (std::abs(MinimalVoiceCore::lfoPolarity(-1.0f, false) + 1.0f) > 1.0e-6f)
        return fail("bipolar map changed the value");

    std::uint32_t state = 0x12345678u;
    const float first = MinimalVoiceCore::randomHold(state);
    const auto after = state;
    const float second = MinimalVoiceCore::randomHold(state);
    std::uint32_t again = 0x12345678u;
    const float repeat = MinimalVoiceCore::randomHold(again);
    if (! std::isfinite(first) || ! std::isfinite(second) || first < -1.0f || first > 1.0f || second < -1.0f || second > 1.0f)
        return fail("random hold is outside -1..+1");
    if (std::abs(first - second) < 1.0e-6f || std::abs(first - repeat) > 1.0e-6f || after == 0x12345678u)
        return fail("random hold is not deterministic");

    double previous = 0.0;
    for (int index = 0; index < MinimalVoiceCore::syncDivisionCount; ++index)
    {
        const double hz = MinimalVoiceCore::syncFrequencyHz(index, 120.0);
        if (! std::isfinite(hz) || hz <= previous)
            return fail("sync divisions are not strictly ordered");
        previous = hz;
    }
    if (std::abs(MinimalVoiceCore::syncFrequencyHz(6, 120.0) - 2.0) > 1.0e-6)
        return fail("1/4 at 120 BPM is not 2 Hz");
    if (std::abs(MinimalVoiceCore::syncFrequencyHz(6, 0.0) - 2.0) > 1.0e-6
        || std::abs(MinimalVoiceCore::syncFrequencyHz(6, -30.0) - 2.0) > 1.0e-6
        || std::abs(MinimalVoiceCore::syncFrequencyHz(6, std::numeric_limits<double>::quiet_NaN()) - 2.0) > 1.0e-6)
        return fail("invalid tempo did not fall back to 120 BPM");
    if (! (MinimalVoiceCore::syncFrequencyHz(0, 120.0) < MinimalVoiceCore::syncFrequencyHz(10, 120.0)))
        return fail("16/1 is not slower than 1/64");
    return 0;
}

int capture(XenMileniumAudioProcessor& processor, int note, int wave, int type, float cutoff, float resonance, float samplesToSkip, juce::AudioBuffer<float>& out)
{
    prepareVoice(processor);
    setPath(processor, "part/1/osc/1/waveSource", static_cast<float>(wave));
    setPath(processor, "part/1/filter/type", static_cast<float>(type));
    setPath(processor, "part/1/filter/cutoff", cutoff);
    setPath(processor, "part/1/filter/resonance", resonance);
    setPath(processor, "part/1/filter/mix", 1.0f);
    processor.prepareToPlay(48000.0, 256);
    const int skip = static_cast<int>(samplesToSkip);
    if (skip > 0)
    {
        juce::AudioBuffer<float> lead(2, std::min(skip, 256));
        if (const int result = renderNote(processor, lead, note))
            return result;
        hold(processor, std::max(0, skip - lead.getNumSamples()), 256);
    }
    if (skip == 0)
    {
        if (const int result = renderNote(processor, out, note))
            return result;
    }
    else if (const int result = renderEmpty(processor, out))
    {
        return result;
    }
    return 0;
}

int checkResponses()
{
    juce::AudioBuffer<float> low(2, 1024);
    juce::AudioBuffer<float> high(2, 1024);
    XenMileniumAudioProcessor lowPass;
    XenMileniumAudioProcessor openPass;
    if (const int result = capture(lowPass, 69, 1, 0, 180.0f, 0.15f, 1200.0f, low))
        return result;
    if (const int result = capture(openPass, 69, 1, 0, 8000.0f, 0.15f, 1200.0f, high))
        return result;
    if (! (meanAdjacent(low) < meanAdjacent(high) * 0.55f))
        return fail("LP12 did not darken a saw");

    juce::AudioBuffer<float> hpOpen(2, 1024);
    juce::AudioBuffer<float> hpClosed(2, 1024);
    XenMileniumAudioProcessor hpLowCut;
    XenMileniumAudioProcessor hpHighCut;
    if (const int result = capture(hpLowCut, 36, 0, 6, 80.0f, 0.0f, 1500.0f, hpOpen))
        return result;
    if (const int result = capture(hpHighCut, 36, 0, 6, 6000.0f, 0.0f, 1500.0f, hpClosed))
        return result;
    if (! (meanAbs(hpClosed) < meanAbs(hpOpen) * 0.35f))
        return fail("HP12 did not remove a low sine");

    juce::AudioBuffer<float> centred(2, 1024);
    juce::AudioBuffer<float> offset(2, 1024);
    XenMileniumAudioProcessor bandCentre;
    XenMileniumAudioProcessor bandOffset;
    if (const int result = capture(bandCentre, 69, 0, 3, 440.0f, 0.85f, 1500.0f, centred))
        return result;
    if (const int result = capture(bandOffset, 69, 0, 3, 5000.0f, 0.85f, 1500.0f, offset))
        return result;
    if (! (meanAbs(centred) > meanAbs(offset) * 2.0f))
        return fail("BP12 did not favour its centre");

    for (int slope = 0; slope < 3; ++slope)
    {
        juce::AudioBuffer<float> darker(2, 512);
        juce::AudioBuffer<float> brighter(2, 512);
        XenMileniumAudioProcessor dark;
        XenMileniumAudioProcessor bright;
        if (const int result = capture(dark, 69, 1, slope, 220.0f, 0.1f, 800.0f, darker))
            return result;
        if (const int result = capture(bright, 69, 1, slope, 7000.0f, 0.1f, 800.0f, brighter))
            return result;
        if (! (meanAdjacent(darker) < meanAdjacent(brighter)))
            return fail("an LP slope did not respond to cutoff");
        XenMileniumAudioProcessor hpDark;
        XenMileniumAudioProcessor hpBright;
        juce::AudioBuffer<float> hpA(2, 512);
        juce::AudioBuffer<float> hpB(2, 512);
        if (const int result = capture(hpDark, 36, 0, 6 + slope, 120.0f, 0.0f, 800.0f, hpA))
            return result;
        if (const int result = capture(hpBright, 36, 0, 6 + slope, 7000.0f, 0.0f, 800.0f, hpB))
            return result;
        if (! (meanAbs(hpB) < meanAbs(hpA)))
            return fail("an HP slope did not respond to cutoff");
    }

    juce::AudioBuffer<float> onNotch(2, 1024);
    juce::AudioBuffer<float> offNotch(2, 1024);
    XenMileniumAudioProcessor notchOn;
    XenMileniumAudioProcessor notchOff;
    if (const int result = capture(notchOn, 69, 0, 9, 440.0f, 0.9f, 1800.0f, onNotch))
        return result;
    if (const int result = capture(notchOff, 69, 0, 9, 4000.0f, 0.9f, 1800.0f, offNotch))
        return result;
    if (! (meanAbs(onNotch) < meanAbs(offNotch) * 0.7f) || meanAbs(offNotch) < 0.01f)
        return fail("notch did not attenuate the centre while keeping a far sine");

    juce::AudioBuffer<float> peakLow(2, 1024);
    juce::AudioBuffer<float> peakHigh(2, 1024);
    juce::AudioBuffer<float> peakFar(2, 1024);
    XenMileniumAudioProcessor peakPlain;
    XenMileniumAudioProcessor peakRes;
    XenMileniumAudioProcessor peakOffset;
    if (const int result = capture(peakPlain, 69, 0, 10, 440.0f, 0.0f, 1500.0f, peakLow))
        return result;
    if (const int result = capture(peakRes, 69, 0, 10, 440.0f, 1.0f, 1500.0f, peakHigh))
        return result;
    if (const int result = capture(peakOffset, 69, 0, 10, 4000.0f, 1.0f, 1500.0f, peakFar))
        return result;
    if (! (meanAbs(peakHigh) > meanAbs(peakLow) * 1.15f) || ! (meanAbs(peakHigh) > meanAbs(peakFar)))
        return fail("peak resonance or centre did not raise the sine");

    juce::AudioBuffer<float> plus(2, 1024);
    juce::AudioBuffer<float> minus(2, 1024);
    XenMileniumAudioProcessor combPlus;
    XenMileniumAudioProcessor combMinus;
    if (const int result = capture(combPlus, 57, 1, 11, 300.0f, 0.85f, 2000.0f, plus))
        return result;
    if (const int result = capture(combMinus, 57, 1, 12, 300.0f, 0.85f, 2000.0f, minus))
        return result;
    if (maxDifference(plus, minus) < 1.0e-3f || meanAbs(plus) < 1.0e-4f || meanAbs(minus) < 1.0e-4f)
        return fail("Comb+ and Comb- are not distinct");

    juce::AudioBuffer<float> formant(2, 1024);
    juce::AudioBuffer<float> lowpass(2, 1024);
    XenMileniumAudioProcessor formantVoice;
    XenMileniumAudioProcessor lowpassVoice;
    if (const int result = capture(formantVoice, 60, 1, 13, 500.0f, 0.6f, 1500.0f, formant))
        return result;
    if (const int result = capture(lowpassVoice, 60, 1, 0, 500.0f, 0.6f, 1500.0f, lowpass))
        return result;
    if (meanAbs(formant) < 1.0e-4f || maxDifference(formant, lowpass) < 1.0e-3f)
        return fail("formant produced no energy or matched LP12");

    for (int type = 0; type < 14; ++type)
    {
        XenMileniumAudioProcessor processor;
        juce::AudioBuffer<float> buffer(2, 256);
        if (const int result = capture(processor, 64, 1, type, type == 11 || type == 12 ? 250.0f : 900.0f, 1.0f, 0.0f, buffer))
            return result;
        if (meanAbs(buffer) < 1.0e-6f)
            return fail("filter type " + juce::String(type) + " was silent");
        processor.releaseResources();
    }
    return 0;
}

int checkControls()
{
    juce::AudioBuffer<float> dry(2, 512);
    juce::AudioBuffer<float> shaped(2, 512);
    XenMileniumAudioProcessor plain;
    XenMileniumAudioProcessor driven;
    setPath(driven, "part/1/filter/drive", 1.0f);
    if (const int result = capture(plain, 69, 1, 0, 1800.0f, 0.2f, 1000.0f, dry))
        return result;
    if (const int result = capture(driven, 69, 1, 0, 1800.0f, 0.2f, 1000.0f, shaped))
        return result;
    if (maxDifference(dry, shaped) < 1.0e-3f)
        return fail("drive did not change the filtered saw");

    juce::AudioBuffer<float> tracked(2, 1024);
    juce::AudioBuffer<float> fixed(2, 1024);
    XenMileniumAudioProcessor withTrack;
    XenMileniumAudioProcessor withoutTrack;
    setPath(withTrack, "part/1/filter/keytrack", 1.0f);
    if (const int result = capture(withoutTrack, 96, 1, 0, 280.0f, 0.05f, 1000.0f, fixed))
        return result;
    if (const int result = capture(withTrack, 96, 1, 0, 280.0f, 0.05f, 1000.0f, tracked))
        return result;
    if (! (meanAdjacent(tracked) > meanAdjacent(fixed) * 1.2f))
        return fail("keytrack +1 did not brighten MIDI note 96");

    XenMileniumAudioProcessor jumping;
    prepareVoice(jumping);
    setPath(jumping, "part/1/osc/1/waveSource", 0.0f);
    setPath(jumping, "part/1/filter/cutoff", 160.0f);
    setPath(jumping, "part/1/filter/mix", 1.0f);
    jumping.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> first(2, 256);
    if (const int result = renderNote(jumping, first, 69))
        return result;
    setPath(jumping, "part/1/filter/cutoff", 12000.0f);
    juce::AudioBuffer<float> second(2, 256);
    if (const int result = renderEmpty(jumping, second))
        return result;
    if (maxAdjacent(second) > 0.35f || ! (meanAbs(second) > meanAbs(first) * 1.5f))
        return fail("cutoff smoothing clicked or did not open");

    XenMileniumAudioProcessor isolated;
    XenMileniumAudioProcessor other;
    prepareVoice(isolated);
    prepareVoice(other);
    setPath(other, "part/2/filter/type", 8.0f);
    setPath(other, "part/2/filter/cutoff", 80.0f);
    setPath(other, "part/2/filter/mix", 1.0f);
    setPath(other, "part/2/filter/resonance", 1.0f);
    isolated.prepareToPlay(48000.0, 128);
    other.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> a(2, 128);
    juce::AudioBuffer<float> b(2, 128);
    if (const int result = renderNote(isolated, a, 60))
        return result;
    if (const int result = renderNote(other, b, 60))
        return result;
    if (maxDifference(a, b) > 0.0f)
        return fail("part 2 filter changed part 1");

    XenMileniumAudioProcessor rates;
    prepareVoice(rates);
    setPath(rates, "part/1/filter/mix", 1.0f);
    setPath(rates, "part/1/filter/resonance", 0.95f);
    rates.prepareToPlay(44100.0, 128);
    juce::AudioBuffer<float> block(2, 128);
    if (const int result = renderNote(rates, block, 62))
        return result;
    rates.prepareToPlay(96000.0, 64);
    juce::AudioBuffer<float> faster(2, 64);
    if (const int result = renderEmpty(rates, faster))
        return result;
    if (rates.getActiveVoiceCount() != 1)
        return fail("sample-rate change dropped the voice");
    rates.releaseResources();
    if (rates.getActiveVoiceCount() != 0 || rates.getActiveTailCount() != 0)
        return fail("reset left a voice or a tail");
    return 0;
}

int checkEnvelope()
{
    auto renderFiltered = [](float amount, float attack, float release, int holdSamples, bool noteOff, juce::AudioBuffer<float>& out) -> int
    {
        XenMileniumAudioProcessor processor;
        prepareVoice(processor);
        setPath(processor, "part/1/osc/1/waveSource", 1.0f);
        setPath(processor, "part/1/filter/cutoff", 220.0f);
        setPath(processor, "part/1/filter/mix", 1.0f);
        setPath(processor, "part/1/env/filter/amount", amount);
        setPath(processor, "part/1/env/filter/attack", attack);
        setPath(processor, "part/1/env/filter/decay", 0.5f);
        setPath(processor, "part/1/env/filter/sustain", 1.0f);
        setPath(processor, "part/1/env/filter/release", release);
        setPath(processor, "part/1/env/amp/release", 1.5f);
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> lead(2, 256);
        if (const int result = renderNote(processor, lead, 60))
            return result;
        hold(processor, holdSamples, 256);
        if (noteOff)
        {
            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            juce::AudioBuffer<float> gap(2, 256);
            processor.processBlock(gap, off);
            hold(processor, 6000, 256);
        }
        return renderEmpty(processor, out);
    };

    juce::AudioBuffer<float> zeroA(2, 512);
    juce::AudioBuffer<float> zeroB(2, 512);
    if (const int result = renderFiltered(0.0f, 0.001f, 0.4f, 2000, false, zeroA))
        return result;
    if (const int result = renderFiltered(0.0f, 3.0f, 0.01f, 2000, false, zeroB))
        return result;
    if (maxDifference(zeroA, zeroB) > 1.0e-5f)
        return fail("zero filter amount changed cutoff");

    juce::AudioBuffer<float> opened(2, 512);
    juce::AudioBuffer<float> closed(2, 512);
    if (const int result = renderFiltered(36.0f, 0.001f, 0.4f, 3000, false, opened))
        return result;
    if (const int result = renderFiltered(-36.0f, 0.001f, 0.4f, 3000, false, closed))
        return result;
    if (! (meanAdjacent(opened) > meanAdjacent(zeroA) * 1.3f) || ! (meanAdjacent(closed) < meanAdjacent(zeroA) * 0.85f))
        return fail("filter amount did not open and close cutoff");

    juce::AudioBuffer<float> held(2, 256);
    juce::AudioBuffer<float> released(2, 256);
    if (const int result = renderFiltered(40.0f, 0.001f, 0.02f, 2000, false, held))
        return result;
    if (const int result = renderFiltered(40.0f, 0.001f, 0.02f, 2000, true, released))
        return result;
    const float heldBrightness = meanAdjacent(held) / std::max(meanAbs(held), 1.0e-5f);
    const float releasedBrightness = meanAdjacent(released) / std::max(meanAbs(released), 1.0e-5f);
    if (! (releasedBrightness < heldBrightness * 0.75f))
        return fail("filter envelope did not release toward the base cutoff");

    XenMileniumAudioProcessor pedal;
    prepareVoice(pedal);
    setPath(pedal, "part/1/osc/1/waveSource", 1.0f);
    setPath(pedal, "part/1/filter/cutoff", 200.0f);
    setPath(pedal, "part/1/filter/mix", 1.0f);
    setPath(pedal, "part/1/env/filter/amount", 36.0f);
    setPath(pedal, "part/1/env/filter/attack", 0.001f);
    setPath(pedal, "part/1/env/filter/sustain", 1.0f);
    setPath(pedal, "part/1/env/filter/release", 0.02f);
    setPath(pedal, "part/1/env/amp/release", 1.5f);
    pedal.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> start(2, 256);
    if (const int result = renderNote(pedal, start, 60))
        return result;
    hold(pedal, 2000, 256);
    juce::MidiBuffer down;
    down.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
    juce::AudioBuffer<float> pedalBuffer(2, 64);
    pedal.processBlock(pedalBuffer, down);
    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    pedal.processBlock(pedalBuffer, off);
    hold(pedal, 4000, 256);
    juce::AudioBuffer<float> still(2, 256);
    if (const int result = renderEmpty(pedal, still))
        return result;
    if (! pedal.isNoteActive(1, 1, 60))
        return fail("sustain pedal did not hold the note");
    const float stillBrightness = meanAdjacent(still) / std::max(meanAbs(still), 1.0e-5f);
    if (stillBrightness < heldBrightness * 0.7f)
        return fail("sustain pedal released the filter envelope");

    XenMileniumAudioProcessor solo;
    XenMileniumAudioProcessor duo;
    auto arm = [](XenMileniumAudioProcessor& processor)
    {
        prepareVoice(processor);
        setPath(processor, "part/1/osc/1/waveSource", 1.0f);
        setPath(processor, "part/1/filter/cutoff", 180.0f);
        setPath(processor, "part/1/filter/mix", 1.0f);
        setPath(processor, "part/1/env/filter/amount", 36.0f);
        setPath(processor, "part/1/env/filter/attack", 1.0f);
        setPath(processor, "part/1/env/filter/sustain", 1.0f);
        setPath(processor, "part/1/env/amp/release", 0.001f);
        processor.prepareToPlay(48000.0, 256);
    };
    arm(solo);
    arm(duo);
    juce::AudioBuffer<float> lead(2, 256);
    if (const int result = renderNote(solo, lead, 60))
        return result;
    if (const int result = renderNote(duo, lead, 60))
        return result;
    juce::MidiBuffer second;
    second.addEvent(juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(110)), 0);
    duo.processBlock(lead, second);
    hold(solo, 16000, 256);
    hold(duo, 16000, 256);
    juce::MidiBuffer releaseSecond;
    releaseSecond.addEvent(juce::MidiMessage::noteOff(1, 67), 0);
    duo.processBlock(lead, releaseSecond);
    hold(solo, 4000, 256);
    hold(duo, 4000, 256);
    juce::AudioBuffer<float> soloNow(2, 512);
    juce::AudioBuffer<float> duoNow(2, 512);
    if (const int result = renderEmpty(solo, soloNow))
        return result;
    if (const int result = renderEmpty(duo, duoNow))
        return result;
    if (! duo.isNoteActive(1, 1, 60) || duo.isNoteActive(1, 1, 67))
        return fail("releasing one note changed which voice was held");
    const float soloBrightness = meanAdjacent(soloNow) / std::max(meanAbs(soloNow), 1.0e-5f);
    const float duoBrightness = meanAdjacent(duoNow) / std::max(meanAbs(duoNow), 1.0e-5f);
    if (duoBrightness < soloBrightness * 0.65f)
        return fail("one note restarted the other voice filter envelope");

    XenMileniumAudioProcessor partA;
    XenMileniumAudioProcessor partB;
    arm(partA);
    arm(partB);
    setPath(partB, "part/2/enabled", 1.0f);
    setPath(partB, "part/2/midiChannel", 2.0f);
    setPath(partB, "part/2/env/filter/amount", -40.0f);
    setPath(partB, "part/2/filter/mix", 1.0f);
    partA.prepareToPlay(48000.0, 256);
    partB.prepareToPlay(48000.0, 256);
    if (const int result = renderNote(partA, lead, 60))
        return result;
    if (const int result = renderNote(partB, lead, 60))
        return result;
    juce::MidiBuffer otherPart;
    otherPart.addEvent(juce::MidiMessage::noteOn(2, 72, static_cast<juce::uint8>(110)), 0);
    partB.processBlock(lead, otherPart);
    hold(partA, 8000, 256);
    hold(partB, 8000, 256);
    juce::AudioBuffer<float> only(2, 256);
    juce::AudioBuffer<float> both(2, 256);
    if (const int result = renderEmpty(partA, only))
        return result;
    if (const int result = renderEmpty(partB, both))
        return result;
    if (maxDifference(only, both) < 1.0e-4f)
        return fail("part 2 filter envelope did not change only part 2");
    return 0;
}

int checkLfoAudio()
{
    XenMileniumAudioProcessor dry;
    XenMileniumAudioProcessor pitched;
    prepareVoice(dry);
    prepareVoice(pitched);
    setPath(pitched, "part/1/lfo/2/toPitch", 9.0f);
    setPath(pitched, "part/1/lfo/2/rateHz", 7.0f);
    dry.prepareToPlay(48000.0, 512);
    pitched.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> dryBuffer(2, 512);
    juce::AudioBuffer<float> pitchBuffer(2, 512);
    if (const int result = renderNote(dry, dryBuffer, 69))
        return result;
    if (const int result = renderNote(pitched, pitchBuffer, 69))
        return result;
    hold(dry, 3000, 512);
    hold(pitched, 3000, 512);
    if (const int result = renderEmpty(dry, dryBuffer))
        return result;
    if (const int result = renderEmpty(pitched, pitchBuffer))
        return result;
    if (maxDifference(dryBuffer, pitchBuffer) < 1.0e-3f || ! pitched.isNoteActive(1, 1, 69))
        return fail("LFO 2 did not move pitch or changed the MIDI note");

    XenMileniumAudioProcessor unmodulated;
    XenMileniumAudioProcessor cutoffMod;
    XenMileniumAudioProcessor ignored;
    prepareVoice(unmodulated);
    prepareVoice(cutoffMod);
    prepareVoice(ignored);
    for (auto* processor : { &unmodulated, &cutoffMod, &ignored })
    {
        setPath(*processor, "part/1/osc/1/waveSource", 1.0f);
        setPath(*processor, "part/1/filter/cutoff", 450.0f);
        setPath(*processor, "part/1/filter/mix", 1.0f);
    }
    setPath(cutoffMod, "part/1/lfo/1/toCutoff", 30.0f);
    setPath(cutoffMod, "part/1/lfo/1/rateHz", 5.0f);
    setPath(ignored, "part/1/lfo/1/toCutoff", 30.0f);
    setPath(ignored, "part/1/lfo/1/rateHz", 5.0f);
    setPath(ignored, "part/1/filter/mix", 0.0f);
    unmodulated.prepareToPlay(48000.0, 512);
    cutoffMod.prepareToPlay(48000.0, 512);
    ignored.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> base(2, 512);
    juce::AudioBuffer<float> moved(2, 512);
    juce::AudioBuffer<float> skipped(2, 512);
    juce::AudioBuffer<float> dryMix(2, 512);
    if (const int result = renderNote(unmodulated, base, 60))
        return result;
    if (const int result = renderNote(cutoffMod, moved, 60))
        return result;
    if (const int result = renderNote(ignored, skipped, 60))
        return result;
    XenMileniumAudioProcessor dryReference;
    prepareVoice(dryReference);
    setPath(dryReference, "part/1/osc/1/waveSource", 1.0f);
    dryReference.prepareToPlay(48000.0, 512);
    if (const int result = renderNote(dryReference, dryMix, 60))
        return result;
    hold(unmodulated, 4000, 512);
    hold(cutoffMod, 4000, 512);
    hold(ignored, 4000, 512);
    hold(dryReference, 4000, 512);
    if (const int result = renderEmpty(unmodulated, base))
        return result;
    if (const int result = renderEmpty(cutoffMod, moved))
        return result;
    if (const int result = renderEmpty(ignored, skipped))
        return result;
    if (const int result = renderEmpty(dryReference, dryMix))
        return result;
    if (maxDifference(base, moved) < 1.0e-3f)
        return fail("LFO 1 did not move cutoff");
    if (maxDifference(skipped, dryMix) > 1.0e-5f)
        return fail("LFO 1 changed dry audio while filter mix was 0");

    XenMileniumAudioProcessor direct;
    XenMileniumAudioProcessor directFiltered;
    prepareVoice(direct);
    prepareVoice(directFiltered);
    setPath(direct, "part/1/mixer/osc1", 0.0f);
    setPath(directFiltered, "part/1/mixer/osc1", 0.0f);
    setPath(direct, "part/1/osc/3/directOut", 1.0f);
    setPath(directFiltered, "part/1/osc/3/directOut", 1.0f);
    setPath(directFiltered, "part/1/filter/type", 8.0f);
    setPath(directFiltered, "part/1/filter/cutoff", 90.0f);
    setPath(directFiltered, "part/1/filter/resonance", 1.0f);
    setPath(directFiltered, "part/1/filter/mix", 1.0f);
    setPath(directFiltered, "part/1/env/filter/amount", 48.0f);
    direct.prepareToPlay(48000.0, 256);
    directFiltered.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> directA(2, 256);
    juce::AudioBuffer<float> directB(2, 256);
    if (const int result = renderNote(direct, directA, 69))
        return result;
    if (const int result = renderNote(directFiltered, directB, 69))
        return result;
    if (maxDifference(directA, directB) > 0.0f || meanAbs(directA) < 1.0e-4f)
        return fail("OSC3 Direct Out changed with the filter");

    XenMileniumAudioProcessor resetA;
    XenMileniumAudioProcessor resetB;
    XenMileniumAudioProcessor freeRun;
    auto armPitch = [](XenMileniumAudioProcessor& processor, bool retrig)
    {
        prepareVoice(processor);
        setPath(processor, "part/1/lfo/2/toPitch", 12.0f);
        setPath(processor, "part/1/lfo/2/rateHz", 3.0f);
        setPath(processor, "part/1/lfo/2/retrig", retrig ? 1.0f : 0.0f);
        processor.prepareToPlay(48000.0, 128);
    };
    armPitch(resetA, true);
    armPitch(resetB, true);
    armPitch(freeRun, false);
    juce::AudioBuffer<float> first(2, 128);
    juce::AudioBuffer<float> second(2, 128);
    juce::AudioBuffer<float> continued(2, 128);
    if (const int result = renderNote(resetA, first, 64))
        return result;
    hold(resetB, 20000, 128);
    hold(freeRun, 20000, 128);
    if (const int result = renderNote(resetB, second, 64))
        return result;
    if (const int result = renderNote(freeRun, continued, 64))
        return result;
    if (maxDifference(first, second) > 1.0e-4f)
        return fail("retrigger did not restart LFO 2");
    if (maxDifference(first, continued) < 1.0e-3f)
        return fail("free-running LFO 2 restarted with the note");

    FixedPlayHead slow(60.0);
    FixedPlayHead fast(240.0);
    FixedPlayHead nominal(120.0);
    FixedPlayHead invalid(0.0);
    XenMileniumAudioProcessor at60;
    XenMileniumAudioProcessor at240;
    XenMileniumAudioProcessor at120;
    XenMileniumAudioProcessor missing;
    XenMileniumAudioProcessor rejected;
    auto armSync = [](XenMileniumAudioProcessor& processor, juce::AudioPlayHead* head)
    {
        prepareVoice(processor);
        setPath(processor, "part/1/lfo/2/sync", 1.0f);
        setPath(processor, "part/1/lfo/2/rateSync", 6.0f);
        setPath(processor, "part/1/lfo/2/toPitch", 12.0f);
        if (head != nullptr)
            processor.setPlayHead(head);
        processor.prepareToPlay(48000.0, 256);
    };
    armSync(at60, &slow);
    armSync(at240, &fast);
    armSync(at120, &nominal);
    armSync(missing, nullptr);
    armSync(rejected, &invalid);
    juce::AudioBuffer<float> slowBuffer(2, 2048);
    juce::AudioBuffer<float> fastBuffer(2, 2048);
    juce::AudioBuffer<float> hostBuffer(2, 2048);
    juce::AudioBuffer<float> fallbackBuffer(2, 2048);
    juce::AudioBuffer<float> invalidBuffer(2, 2048);
    if (const int result = renderNote(at60, slowBuffer, 69))
        return result;
    if (const int result = renderNote(at240, fastBuffer, 69))
        return result;
    if (const int result = renderNote(at120, hostBuffer, 69))
        return result;
    if (const int result = renderNote(missing, fallbackBuffer, 69))
        return result;
    if (const int result = renderNote(rejected, invalidBuffer, 69))
        return result;
    hold(at60, 8000, 256);
    hold(at240, 8000, 256);
    hold(at120, 8000, 256);
    hold(missing, 8000, 256);
    hold(rejected, 8000, 256);
    if (const int result = renderEmpty(at60, slowBuffer))
        return result;
    if (const int result = renderEmpty(at240, fastBuffer))
        return result;
    if (const int result = renderEmpty(at120, hostBuffer))
        return result;
    if (const int result = renderEmpty(missing, fallbackBuffer))
        return result;
    if (const int result = renderEmpty(rejected, invalidBuffer))
        return result;
    if (maxDifference(slowBuffer, fastBuffer) < 1.0e-3f)
        return fail("tempo did not change a synced LFO");
    if (maxDifference(hostBuffer, fallbackBuffer) > 1.0e-4f || maxDifference(fallbackBuffer, invalidBuffer) > 1.0e-4f)
        return fail("missing or invalid tempo did not use 120 BPM");

    XenMileniumAudioProcessor quarter;
    XenMileniumAudioProcessor sixteenth;
    armSync(quarter, &nominal);
    armSync(sixteenth, &nominal);
    setPath(sixteenth, "part/1/lfo/2/rateSync", 8.0f);
    juce::AudioBuffer<float> quarterBuffer(2, 2048);
    juce::AudioBuffer<float> sixteenthBuffer(2, 2048);
    if (const int result = renderNote(quarter, quarterBuffer, 69))
        return result;
    if (const int result = renderNote(sixteenth, sixteenthBuffer, 69))
        return result;
    if (maxDifference(quarterBuffer, sixteenthBuffer) < 1.0e-4f)
        return fail("sync divisions 1/4 and 1/16 sounded the same");

    XenMileniumAudioProcessor lfo1;
    XenMileniumAudioProcessor lfo34;
    prepareVoice(lfo1);
    prepareVoice(lfo34);
    setPath(lfo1, "part/1/osc/1/waveSource", 1.0f);
    setPath(lfo34, "part/1/osc/1/waveSource", 1.0f);
    setPath(lfo1, "part/1/filter/mix", 1.0f);
    setPath(lfo34, "part/1/filter/mix", 1.0f);
    setPath(lfo1, "part/1/filter/cutoff", 600.0f);
    setPath(lfo34, "part/1/filter/cutoff", 600.0f);
    setPath(lfo1, "part/1/lfo/1/toCutoff", 24.0f);
    setPath(lfo34, "part/1/lfo/1/toCutoff", 24.0f);
    setPath(lfo1, "part/1/lfo/1/rateHz", 4.0f);
    setPath(lfo34, "part/1/lfo/1/rateHz", 4.0f);
    setPath(lfo34, "part/1/lfo/3/rateHz", 33.0f);
    setPath(lfo34, "part/1/lfo/3/shape", 4.0f);
    setPath(lfo34, "part/1/lfo/4/rateHz", 0.05f);
    setPath(lfo34, "part/1/lfo/4/shape", 5.0f);
    setPath(lfo34, "part/1/lfo/4/polarity", 0.0f);
    lfo1.prepareToPlay(48000.0, 256);
    lfo34.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> left(2, 256);
    juce::AudioBuffer<float> right(2, 256);
    if (const int result = renderNote(lfo1, left, 58))
        return result;
    if (const int result = renderNote(lfo34, right, 58))
        return result;
    if (maxDifference(left, right) > 1.0e-5f)
        return fail("LFO 3 or LFO 4 changed LFO 1 audio");
    return 0;
}

int checkPresetAndLoad()
{
    const auto* glide = xenmilenium::ParameterRegistry::findByStatePath("part/1/lfo/1/shape");
    if (glide == nullptr || glide->choices.size() != 6 || glide->choices[5] != "randomHold")
        return fail("random shape was not randomHold");
    if (glide->choices.contains("randomGlide"))
        return fail("randomGlide was registered");

    XenMileniumAudioProcessor processor;
    setPath(processor, "part/1/filter/cutoff", 1550.0f);
    setPath(processor, "part/1/filter/type", 9.0f);
    setPath(processor, "part/1/env/filter/amount", -12.0f);
    setPath(processor, "part/1/lfo/2/shape", 4.0f);
    setPath(processor, "part/1/lfo/2/toPitch", 3.5f);
    setPath(processor, "part/1/filter/mix", 0.25f);
    const float cutoff = rangedParameter(processor, "part/1/filter/cutoff")->getNormalisableRange().convertFrom0to1(
        rangedParameter(processor, "part/1/filter/cutoff")->getValue());
    const auto saved = processor.saveStateDocument();
    XenMileniumAudioProcessor restored;
    if (! restored.restoreStateDocument(saved))
        return fail("new filter preset did not round-trip");
    auto* type = dynamic_cast<juce::AudioParameterChoice*>(restored.getValueTreeState().getParameter("xm.p1.flt.type"));
    auto* shape = dynamic_cast<juce::AudioParameterChoice*>(restored.getValueTreeState().getParameter("xm.p2.l2.shape"));
    auto* amount = dynamic_cast<juce::AudioParameterFloat*>(restored.getValueTreeState().getParameter("xm.p1.fenv.amount"));
    auto* pitch = dynamic_cast<juce::AudioParameterFloat*>(restored.getValueTreeState().getParameter("xm.p1.l2.toPitch"));
    auto* mix = dynamic_cast<juce::AudioParameterFloat*>(restored.getValueTreeState().getParameter("xm.p1.flt.mix"));
    auto* savedCutoff = dynamic_cast<juce::AudioParameterFloat*>(restored.getValueTreeState().getParameter("xm.p1.flt.cutoff"));
    if (type == nullptr || type->getCurrentChoiceName() != "notch" || shape == nullptr || shape->getIndex() != 0)
        return fail("filter type or an untouched LFO did not round-trip");
    auto* partOneShape = dynamic_cast<juce::AudioParameterChoice*>(restored.getValueTreeState().getParameter("xm.p1.l2.shape"));
    if (partOneShape == nullptr || partOneShape->getCurrentChoiceName() != "pulse")
        return fail("LFO shape did not round-trip");
    if (amount == nullptr || std::abs(amount->get() + 12.0f) > 0.02f || pitch == nullptr || std::abs(pitch->get() - 3.5f) > 0.02f)
        return fail("modulation amount did not round-trip");
    if (mix == nullptr || std::abs(mix->get() - 0.25f) > 1.0e-4f || savedCutoff == nullptr || std::abs(savedCutoff->get() - cutoff) > 0.05f)
        return fail("filter mix or cutoff did not round-trip");
    juce::var parsed;
    if (juce::JSON::parse(restored.saveStateDocument(), parsed).failed() || static_cast<int>(parsed.getProperty("formatVersion", 0)) != 1)
        return fail("formatVersion changed");

    XenMileniumAudioProcessor oldPreset;
    if (! oldPreset.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"part/1/osc/1/waveSource\":\"saw\",\"part/4/enabled\":true}}"))
        return fail("old preset was rejected");
    auto* wave = dynamic_cast<juce::AudioParameterChoice*>(oldPreset.getValueTreeState().getParameter("xm.p1.o1.waveSource"));
    auto* oldMix = dynamic_cast<juce::AudioParameterFloat*>(oldPreset.getValueTreeState().getParameter("xm.p1.flt.mix"));
    auto* oldAmount = dynamic_cast<juce::AudioParameterFloat*>(oldPreset.getValueTreeState().getParameter("xm.p1.fenv.amount"));
    auto* oldPitch = dynamic_cast<juce::AudioParameterFloat*>(oldPreset.getValueTreeState().getParameter("xm.p1.l2.toPitch"));
    auto* enabled = dynamic_cast<juce::AudioParameterBool*>(oldPreset.getValueTreeState().getParameter("xm.p4.enabled"));
    if (wave == nullptr || wave->getCurrentChoiceName() != "saw" || enabled == nullptr || ! enabled->get())
        return fail("old preset lost an existing value");
    if (oldMix == nullptr || std::abs(oldMix->get()) > 1.0e-6f || oldAmount == nullptr || std::abs(oldAmount->get()) > 1.0e-6f
        || oldPitch == nullptr || std::abs(oldPitch->get()) > 1.0e-6f)
        return fail("missing filter keys did not load silent defaults");

    XenMileniumAudioProcessor dense;
    prepareVoice(dense);
    setPath(dense, "part/1/filter/mix", 1.0f);
    setPath(dense, "part/1/filter/type", 1.0f);
    setPath(dense, "part/1/filter/resonance", 0.8f);
    for (int part = 2; part <= 8; ++part)
    {
        setPath(dense, ("part/" + juce::String(part) + "/enabled").toRawUTF8(), 1.0f);
        setPath(dense, ("part/" + juce::String(part) + "/midiChannel").toRawUTF8(), static_cast<float>(part));
        setPath(dense, ("part/" + juce::String(part) + "/filter/mix").toRawUTF8(), 1.0f);
        setPath(dense, ("part/" + juce::String(part) + "/env/amp/attack").toRawUTF8(), 0.001f);
    }
    dense.prepareToPlay(48000.0, 64);
    juce::AudioBuffer<float> buffer(2, 64);
    juce::MidiBuffer midi;
    for (int part = 1; part <= 8; ++part)
        for (int note = 0; note < 8; ++note)
            midi.addEvent(juce::MidiMessage::noteOn(part, 48 + note, static_cast<juce::uint8>(100)), 0);
    dense.processBlock(buffer, midi);
    if (! bufferIsFinite(buffer) || dense.getActiveVoiceCount() != 64)
        return fail("filtered 64-voice chord was not finite or not full");
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 80, static_cast<juce::uint8>(100)), 0);
    dense.processBlock(buffer, midi);
    if (! bufferIsFinite(buffer) || dense.getActiveVoiceCountForPart(1) != 8 || dense.getActiveTailCount() < 1)
        return fail("filtered steal did not keep the 8-voice pool and a tail");
    if (dense.isNoteActive(1, 1, 48))
        return fail("filtered steal removed the wrong note");
    return 0;
}
} // namespace

int runFilterLfoTests()
{
    if (const int generators = checkGenerators())
        return generators;
    if (const int responses = checkResponses())
        return responses;
    if (const int controls = checkControls())
        return controls;
    if (const int envelope = checkEnvelope())
        return envelope;
    if (const int lfo = checkLfoAudio())
        return lfo;
    if (const int preset = checkPresetAndLoad())
        return preset;
    std::cout << "filter ok\n";
    return 0;
}
