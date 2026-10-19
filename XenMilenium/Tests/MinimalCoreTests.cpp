#include "Parameters/ParameterRegistry.h"
#include "PluginProcessor.h"

#include <algorithm>
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
    {
        const float* samples = buffer.getReadPointer(channel);
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            if (! std::isfinite(samples[index]))
                return false;
    }
    return true;
}

bool bufferHasEnergy(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const float* samples = buffer.getReadPointer(channel);
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            if (! isZero(samples[index]))
                return true;
    }
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
    {
        const float* samples = buffer.getReadPointer(channel);
        for (int index = 0; index < buffer.getNumSamples(); ++index)
        {
            sum += std::abs(samples[index]);
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

float peakAbs(const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const float* samples = buffer.getReadPointer(channel);
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            peak = std::max(peak, std::abs(samples[index]));
    }
    return peak;
}

juce::AudioParameterFloat* floatParameter(XenMileniumAudioProcessor& processor, const char* statePath)
{
    const auto* spec = xenmilenium::ParameterRegistry::findByStatePath(statePath);
    if (spec == nullptr)
        return nullptr;
    return dynamic_cast<juce::AudioParameterFloat*>(processor.getValueTreeState().getParameter(spec->apvtsId));
}

juce::AudioParameterBool* boolParameter(XenMileniumAudioProcessor& processor, const char* statePath)
{
    const auto* spec = xenmilenium::ParameterRegistry::findByStatePath(statePath);
    if (spec == nullptr)
        return nullptr;
    return dynamic_cast<juce::AudioParameterBool*>(processor.getValueTreeState().getParameter(spec->apvtsId));
}

juce::AudioParameterChoice* choiceParameter(XenMileniumAudioProcessor& processor, const char* statePath)
{
    const auto* spec = xenmilenium::ParameterRegistry::findByStatePath(statePath);
    if (spec == nullptr)
        return nullptr;
    return dynamic_cast<juce::AudioParameterChoice*>(processor.getValueTreeState().getParameter(spec->apvtsId));
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
        return fail("processBlock produced a non-finite sample");
    return 0;
}

int renderNote(XenMileniumAudioProcessor& processor, juce::AudioBuffer<float>& buffer, int note, int offset = 0)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(127)), offset);
    return render(processor, buffer, midi);
}

void fastEnvelope(XenMileniumAudioProcessor& processor, float releaseSeconds)
{
    setPlain(floatParameter(processor, "part/1/env/amp/attack"), 0.001f);
    setPlain(floatParameter(processor, "part/1/env/amp/decay"), 0.001f);
    setPlain(floatParameter(processor, "part/1/env/amp/sustain"), 1.0f);
    setPlain(floatParameter(processor, "part/1/env/amp/release"), releaseSeconds);
    setPlain(floatParameter(processor, "master/volume"), 1.0f);
}

int checkSilenceWithoutMidi()
{
    XenMileniumAudioProcessor processor;
    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);
    for (int block = 0; block < 4; ++block)
    {
        juce::MidiBuffer midi;
        if (const int result = render(processor, buffer, midi))
            return result;
        if (! bufferIsSilent(buffer))
            return fail("idle core produced sound");
    }
    if (processor.getActiveVoiceCount() != 0)
        return fail("idle core allocated a voice");
    processor.releaseResources();
    return 0;
}

int checkNoteOn()
{
    XenMileniumAudioProcessor processor;
    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);
    if (const int result = renderNote(processor, buffer, 69))
        return result;
    if (! bufferHasEnergy(buffer))
        return fail("note on produced silence");
    if (processor.getActiveVoiceCount() != 1)
        return fail("note on did not start one voice");
    if (std::memcmp(buffer.getReadPointer(0), buffer.getReadPointer(1), sizeof(float) * 128) != 0)
        return fail("stereo channels diverged");
    if (std::abs(buffer.getSample(0, 0)) > 1.0e-6f)
        return fail("voice did not start from silence");

    juce::MidiBuffer velocityZero;
    XenMileniumAudioProcessor other;
    other.prepareToPlay(48000.0, 32);
    juce::AudioBuffer<float> quiet(2, 32);
    velocityZero.addEvent(juce::MidiMessage::noteOn(1, 70, static_cast<juce::uint8>(0)), 0);
    if (const int result = render(other, quiet, velocityZero))
        return result;
    if (! bufferIsSilent(quiet) || other.getActiveVoiceCount() != 0)
        return fail("velocity 0 started a voice");
    processor.releaseResources();
    other.releaseResources();
    return 0;
}

int checkReleaseAndRestart()
{
    XenMileniumAudioProcessor processor;
    fastEnvelope(processor, 0.2f);
    processor.prepareToPlay(48000.0, 480);
    juce::AudioBuffer<float> buffer(2, 480);

    if (const int result = renderNote(processor, buffer, 69))
        return result;
    juce::MidiBuffer sustainMidi;
    if (const int result = render(processor, buffer, sustainMidi))
        return result;
    const double held = meanAbs(buffer);
    if (held < 0.01 || processor.getActiveVoiceCount() != 1)
        return fail("held note did not reach a sustained level");

    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
    if (const int result = render(processor, buffer, noteOff))
        return result;
    if (processor.getActiveVoiceCount() != 1 || ! bufferHasEnergy(buffer))
        return fail("note off cut the voice instead of releasing it");

    juce::AudioBuffer<float> mid(2, 4800);
    juce::MidiBuffer empty;
    if (const int result = render(processor, mid, empty))
        return result;
    const double midway = meanAbs(mid);
    if (! (midway < held * 0.85 && midway > held * 0.2))
        return fail("release did not decay, held=" + juce::String(held) + " midway=" + juce::String(midway));

    juce::AudioBuffer<float> finishing(2, 6000);
    if (const int result = render(processor, finishing, empty))
        return result;
    juce::AudioBuffer<float> done(2, 480);
    if (const int result = render(processor, done, empty))
        return result;
    if (! bufferIsSilent(done) || processor.getActiveVoiceCount() != 0)
        return fail("release did not free the voice");

    if (const int result = renderNote(processor, buffer, 69))
        return result;
    if (! bufferHasEnergy(buffer) || processor.getActiveVoiceCount() != 1)
        return fail("note did not restart after release");

    juce::MidiBuffer again;
    again.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(processor, buffer, again))
        return result;
    if (processor.getActiveVoiceCount() != 1)
        return fail("same pitch started a second voice");

    processor.releaseResources();
    return 0;
}

int checkPolyphonyAndPool()
{
    XenMileniumAudioProcessor processor;
    fastEnvelope(processor, 0.01f);
    processor.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> buffer(2, 256);

    juce::MidiBuffer chord;
    chord.addEvent(juce::MidiMessage::noteOn(2, 60, static_cast<juce::uint8>(100)), 0);
    chord.addEvent(juce::MidiMessage::noteOn(5, 64, static_cast<juce::uint8>(90)), 10);
    chord.addEvent(juce::MidiMessage::noteOn(16, 67, static_cast<juce::uint8>(80)), 20);
    if (const int result = render(processor, buffer, chord))
        return result;
    if (processor.getActiveVoiceCount() != 3 || ! bufferHasEnergy(buffer))
        return fail("three notes did not sound together");

    processor.releaseResources();

    XenMileniumAudioProcessor pool;
    fastEnvelope(pool, 0.01f);
    pool.prepareToPlay(48000.0, 256);
    juce::MidiBuffer more;
    for (int note = 60; note <= 67; ++note)
        more.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), note - 60);
    if (const int result = render(pool, buffer, more))
        return result;
    if (pool.getActiveVoiceCount() != 8)
        return fail("voice pool did not fill to 8, count=" + juce::String(pool.getActiveVoiceCount()));

    juce::MidiBuffer overflow;
    overflow.addEvent(juce::MidiMessage::noteOn(1, 80, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(pool, buffer, overflow))
        return result;
    if (pool.getActiveVoiceCount() != 8 || pool.isNoteActive(1, 1, 60) || ! pool.isNoteActive(1, 1, 80))
        return fail("the ninth note did not steal the oldest voice");

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        if (std::abs(buffer.getSample(0, sample)) > 1.01f)
            return fail("voice sum exceeded the protected level");

    juce::MidiBuffer releaseOne;
    releaseOne.addEvent(juce::MidiMessage::noteOff(1, 80), 0);
    if (const int result = render(pool, buffer, releaseOne))
        return result;

    juce::AudioBuffer<float> wait(2, 2048);
    juce::MidiBuffer empty;
    if (const int result = render(pool, wait, empty))
        return result;
    if (pool.getActiveVoiceCount() != 7 || pool.isNoteActive(1, 1, 80))
        return fail("released note did not leave the pool");

    juce::MidiBuffer reused;
    reused.addEvent(juce::MidiMessage::noteOn(1, 90, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(pool, buffer, reused))
        return result;
    if (pool.getActiveVoiceCount() != 8 || ! pool.isNoteActive(1, 1, 90) || ! bufferHasEnergy(buffer))
        return fail("freed slot did not accept a new pitch");
    pool.releaseResources();

    processor.releaseResources();
    return 0;
}

int checkBlockSizes()
{
    const int sizes[] = { 1, 17, 32, 64, 128, 256, 512, 1024 };
    for (const int size : sizes)
    {
        XenMileniumAudioProcessor processor;
        processor.prepareToPlay(48000.0, size);
        juce::AudioBuffer<float> buffer(2, size);
        const int blocks = std::max(2, 256 / size);
        bool heard = false;
        for (int block = 0; block < blocks; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(127)), 0);
            if (const int result = render(processor, buffer, midi))
                return result;
            if (buffer.getNumSamples() != size || buffer.getNumChannels() != 2)
                return fail("block size changed the buffer dimensions");
            heard = heard || bufferHasEnergy(buffer);
        }
        if (! heard)
            return fail("block size " + juce::String(size) + " stayed silent");
        processor.releaseResources();
    }
    return 0;
}

int checkSampleRates()
{
    const double rates[] = { 8000.0, 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    double slow = 0.0;
    double fast = 0.0;

    for (const double rate : rates)
    {
        XenMileniumAudioProcessor processor;
        setPlain(floatParameter(processor, "master/volume"), 1.0f);
        processor.prepareToPlay(rate, 256);
        juce::AudioBuffer<float> buffer(2, 256);
        if (const int result = renderNote(processor, buffer, 69))
            return result;
        if (! bufferHasEnergy(buffer))
            return fail("sample rate " + juce::String(rate) + " stayed silent");

        if (juce::approximatelyEqual(rate, 8000.0))
            slow = meanAbs(buffer);
        if (juce::approximatelyEqual(rate, 48000.0))
            fast = meanAbs(buffer);
        processor.releaseResources();
    }

    if (! (slow > fast * 2.0))
        return fail("envelope did not follow the sample rate");
    return 0;
}

int checkNaiveWavesStayFinite()
{
    const char* waves[] = { "sine", "saw", "triangle", "pulse" };
    for (const char* wave : waves)
    {
        XenMileniumAudioProcessor processor;
        auto* choice = choiceParameter(processor, "part/1/osc/1/waveSource");
        const int index = choice->choices.indexOf(wave);
        setPlain(choice, static_cast<float>(index));
        setPlain(floatParameter(processor, "part/1/env/amp/attack"), 0.001f);
        setPlain(floatParameter(processor, "master/volume"), 1.0f);
        processor.prepareToPlay(44100.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 127, static_cast<juce::uint8>(127)), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 40, static_cast<juce::uint8>(127)), 1);
        if (const int result = render(processor, buffer, midi))
            return result;
        for (int block = 0; block < 8; ++block)
        {
            juce::MidiBuffer empty;
            if (const int result = render(processor, buffer, empty))
                return result;
            if (peakAbs(buffer) > 1.01f)
                return fail(juce::String(wave) + " exceeded the protected level");
        }
        if (processor.getActiveVoiceCount() != 2)
            return fail(juce::String(wave) + " dropped a voice");
        processor.releaseResources();
    }
    return 0;
}

int checkMonoLayout()
{
    XenMileniumAudioProcessor processor;
    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    if (! processor.setBusesLayout(mono))
        return fail("processor rejected a mono layout");

    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(1, 128);
    if (const int result = renderNote(processor, buffer, 69))
        return result;
    if (buffer.getNumChannels() != 1 || ! bufferHasEnergy(buffer))
        return fail("mono render was silent or changed channel count");
    processor.releaseResources();
    return 0;
}

int checkClicks()
{
    XenMileniumAudioProcessor processor;
    setPlain(floatParameter(processor, "part/1/env/amp/attack"), 0.05f);
    setPlain(floatParameter(processor, "part/1/env/amp/decay"), 0.001f);
    setPlain(floatParameter(processor, "part/1/env/amp/sustain"), 1.0f);
    setPlain(floatParameter(processor, "part/1/env/amp/release"), 0.05f);
    setPlain(floatParameter(processor, "master/volume"), 1.0f);
    processor.prepareToPlay(48000.0, 64);

    juce::AudioBuffer<float> buffer(2, 64);
    if (const int result = renderNote(processor, buffer, 69))
        return result;

    float previous = buffer.getSample(0, 0);
    if (std::abs(previous) > 1.0e-6f)
        return fail("attack did not begin at silence");
    for (int index = 1; index < buffer.getNumSamples(); ++index)
    {
        const float sample = buffer.getSample(0, index);
        if (std::abs(sample - previous) > 0.05f)
            return fail("attack stepped");
        previous = sample;
    }

    juce::AudioBuffer<float> held(2, 3000);
    juce::MidiBuffer empty;
    if (const int result = render(processor, held, empty))
        return result;
    previous = held.getSample(0, held.getNumSamples() - 1);

    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
    if (const int result = render(processor, buffer, noteOff))
        return result;
    if (std::abs(buffer.getSample(0, 0) - previous) > 0.05f)
        return fail("note off clicked");
    for (int index = 1; index < buffer.getNumSamples(); ++index)
    {
        const float sample = buffer.getSample(0, index);
        if (std::abs(sample - previous) > 0.05f)
            return fail("release stepped");
        previous = sample;
    }

    processor.releaseResources();
    return 0;
}

int checkPartEnableAndBypass()
{
    XenMileniumAudioProcessor processor;
    fastEnvelope(processor, 0.05f);
    auto* partOne = boolParameter(processor, "part/1/enabled");
    auto* partTwo = boolParameter(processor, "part/2/enabled");
    static_cast<juce::AudioProcessorParameter*>(partOne)->setValue(0.0f);
    static_cast<juce::AudioProcessorParameter*>(partTwo)->setValue(0.0f);
    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);
    if (const int result = renderNote(processor, buffer, 69))
        return result;
    if (! bufferIsSilent(buffer) || processor.getActiveVoiceCount() != 0)
        return fail("disabled part 1 sounded");

    static_cast<juce::AudioProcessorParameter*>(partOne)->setValue(1.0f);
    if (const int result = renderNote(processor, buffer, 69))
        return result;
    if (processor.getActiveVoiceCount() != 1)
        return fail("re-enabled part did not accept a note");

    static_cast<juce::AudioProcessorParameter*>(partOne)->setValue(0.0f);
    juce::MidiBuffer empty;
    if (const int result = render(processor, buffer, empty))
        return result;
    if (processor.getActiveVoiceCount() != 1 || ! bufferHasEnergy(buffer))
        return fail("disabling the part cut the voice");

    juce::MidiBuffer ignored;
    ignored.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(127)), 0);
    if (const int result = render(processor, buffer, ignored))
        return result;
    if (processor.getActiveVoiceCount() != 1)
        return fail("disabled part accepted a new note");

    juce::AudioBuffer<float> finishing(2, 4800);
    if (const int result = render(processor, finishing, empty))
        return result;
    juce::AudioBuffer<float> done(2, 128);
    if (const int result = render(processor, done, empty))
        return result;
    if (! bufferIsSilent(done) || processor.getActiveVoiceCount() != 0)
        return fail("disabled part did not finish the release");

    static_cast<juce::AudioProcessorParameter*>(partOne)->setValue(1.0f);
    if (const int result = renderNote(processor, buffer, 69))
        return result;
    const int voicesBeforeBypass = processor.getActiveVoiceCount();
    juce::MidiBuffer bypassMidi;
    bypassMidi.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(127)), 0);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            buffer.setSample(channel, index, 0.5f);
    processor.processBlockBypassed(buffer, bypassMidi);
    if (! bufferIsSilent(buffer) || bypassMidi.getNumEvents() != 0)
        return fail("bypass did not clear audio and midi");
    if (processor.getActiveVoiceCount() != voicesBeforeBypass)
        return fail("bypass advanced or dropped voices");
    if (const int result = render(processor, buffer, empty))
        return result;
    if (! bufferHasEnergy(buffer))
        return fail("voice did not resume after bypass");

    setPlain(floatParameter(processor, "master/volume"), 0.0f);
    if (const int result = render(processor, buffer, empty))
        return result;
    if (! bufferIsSilent(buffer) || processor.getActiveVoiceCount() == 0)
        return fail("zero master volume freed the voice or left sound");

    processor.releaseResources();
    return 0;
}

int checkInvalidSampleRateAndOrder()
{
    XenMileniumAudioProcessor processor;
    processor.prepareToPlay(0.0, 64);
    juce::AudioBuffer<float> buffer(2, 64);
    if (const int result = renderNote(processor, buffer, 69))
        return result;
    if (! bufferIsSilent(buffer))
        return fail("non-positive sample rate produced sound");

    processor.prepareToPlay(-48000.0, 64);
    if (const int result = renderNote(processor, buffer, 69))
        return result;
    if (! bufferIsSilent(buffer))
        return fail("negative sample rate produced sound");

    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> ordered(2, 128);
    juce::MidiBuffer events;
    events.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    events.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0);
    events.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    if (const int result = render(processor, ordered, events))
        return result;
    if (processor.getActiveVoiceCount() != 0)
        return fail("note on and note off at the same sample left a voice");

    juce::MidiBuffer later;
    later.addEvent(juce::MidiMessage::noteOff(1, 64), 10);
    later.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(127)), 40);
    if (const int result = render(processor, ordered, later))
        return result;
    if (processor.getActiveVoiceCount() != 1 || ! bufferHasEnergy(ordered))
        return fail("events in one block were not applied in order");

    juce::AudioBuffer<float> emptyBlock(2, 0);
    juce::MidiBuffer emptyMidi;
    processor.processBlock(emptyBlock, emptyMidi);
    if (emptyBlock.getNumChannels() != 2 || emptyBlock.getNumSamples() != 0 || processor.getActiveVoiceCount() != 1)
        return fail("zero-length block changed the voice or the buffer");

    processor.releaseResources();
    return 0;
}

int checkCoreParameters()
{
    struct Expectation
    {
        const char* path;
        const char* id;
        xenmilenium::ParameterKind kind;
        xenmilenium::ParameterScope scope;
        float minimum;
        float maximum;
        float defaultValue;
        const char* unit;
    };

    const Expectation expectations[] = {
        { "master/volume", "xm.master.volume", xenmilenium::ParameterKind::floatValue, xenmilenium::ParameterScope::global, 0.0f, 1.0f, 0.8f, "linear" },
        { "part/1/env/amp/attack", "xm.p1.amp.attack", xenmilenium::ParameterKind::floatValue, xenmilenium::ParameterScope::part, 0.001f, 5.0f, 0.01f, "s" },
        { "part/1/env/amp/decay", "xm.p1.amp.decay", xenmilenium::ParameterKind::floatValue, xenmilenium::ParameterScope::part, 0.001f, 5.0f, 0.2f, "s" },
        { "part/1/env/amp/sustain", "xm.p1.amp.sustain", xenmilenium::ParameterKind::floatValue, xenmilenium::ParameterScope::part, 0.0f, 1.0f, 0.8f, "linear" },
        { "part/1/env/amp/release", "xm.p1.amp.release", xenmilenium::ParameterKind::floatValue, xenmilenium::ParameterScope::part, 0.001f, 5.0f, 0.4f, "s" },
    };

    XenMileniumAudioProcessor processor;
    if (processor.getName() != "Xen Milenium")
        return fail("plugin name changed");
    if (! juce::approximatelyEqual(processor.getTailLengthSeconds(), 5.0))
        return fail("tail length is not the core release maximum");

    for (const auto& expected : expectations)
    {
        const auto* spec = xenmilenium::ParameterRegistry::findByStatePath(expected.path);
        const auto* byId = xenmilenium::ParameterRegistry::findByApvtsId(expected.id);
        if (spec == nullptr || byId != spec)
            return fail(juce::String("missing core parameter ") + expected.path);
        if (spec->apvtsId != expected.id || spec->kind != expected.kind || spec->scope != expected.scope || spec->unit != expected.unit)
            return fail(juce::String("core parameter contract mismatch ") + expected.path);
        if (! juce::approximatelyEqual(spec->minimum, expected.minimum)
            || ! juce::approximatelyEqual(spec->maximum, expected.maximum)
            || ! juce::approximatelyEqual(spec->defaultFloat, expected.defaultValue))
            return fail(juce::String("core parameter range mismatch ") + expected.path);

        auto* parameter = floatParameter(processor, expected.path);
        if (parameter == nullptr || parameter->getLabel() != expected.unit)
            return fail(juce::String("core parameter label mismatch ") + expected.path);
        if (! juce::approximatelyEqual(parameter->get(), expected.defaultValue))
            return fail(juce::String("core parameter default mismatch ") + expected.path);
        if (! juce::approximatelyEqual(parameter->convertTo0to1(expected.minimum), 0.0f)
            || ! juce::approximatelyEqual(parameter->convertTo0to1(expected.maximum), 1.0f))
            return fail(juce::String("core parameter normalisation mismatch ") + expected.path);

        const float normalisedDefault = parameter->convertTo0to1(expected.defaultValue);
        const float expectedNormalised = (expected.defaultValue - expected.minimum) / (expected.maximum - expected.minimum);
        if (! juce::approximatelyEqual(normalisedDefault, expectedNormalised))
            return fail(juce::String("core parameter linear map mismatch ") + expected.path);
    }

    const auto* wave = xenmilenium::ParameterRegistry::findByStatePath("part/1/osc/1/waveSource");
    if (wave == nullptr || wave->apvtsId != "xm.p1.o1.waveSource" || wave->kind != xenmilenium::ParameterKind::choice
        || wave->scope != xenmilenium::ParameterScope::oscillator || wave->defaultChoice != 0
        || wave->choices.size() != 4 || wave->choices[0] != "sine" || wave->choices[1] != "saw"
        || wave->choices[2] != "triangle" || wave->choices[3] != "pulse")
        return fail("wave source contract mismatch");

    auto* waveParameter = choiceParameter(processor, "part/1/osc/1/waveSource");
    if (waveParameter == nullptr || waveParameter->getCurrentChoiceName() != "sine")
        return fail("wave source default is not sine");
    if (! juce::approximatelyEqual(waveParameter->convertTo0to1(3.0f), 1.0f))
        return fail("wave source normalisation is not index / 3");

    if (xenmilenium::ParameterRegistry::findByStatePath("part/1/osc/2/waveSource") != nullptr
        || xenmilenium::ParameterRegistry::findByStatePath("part/1/env/amp/level") != nullptr
        || xenmilenium::ParameterRegistry::findByStatePath("part/1/volume") != nullptr
        || xenmilenium::ParameterRegistry::findByStatePath("part/1/env/filter/attack") != nullptr)
        return fail("engine registered a parameter outside its set");
    const auto* partEightWave = xenmilenium::ParameterRegistry::findByStatePath("part/8/osc/1/waveSource");
    const auto* partEightAttack = xenmilenium::ParameterRegistry::findByStatePath("part/8/env/amp/attack");
    if (partEightWave == nullptr || partEightWave->apvtsId != "xm.p8.o1.waveSource"
        || partEightAttack == nullptr || partEightAttack->apvtsId != "xm.p8.amp.attack"
        || ! juce::approximatelyEqual(partEightAttack->minimum, 0.001f)
        || ! juce::approximatelyEqual(partEightAttack->maximum, 5.0f))
        return fail("part 8 did not receive the core wave and envelope contract");

    setPlain(floatParameter(processor, "master/volume"), 0.25f);
    setPlain(floatParameter(processor, "part/1/env/amp/attack"), 1.5f);
    setPlain(waveParameter, 1.0f);

    juce::MemoryBlock block;
    processor.getStateInformation(block);
    XenMileniumAudioProcessor restored;
    restored.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    if (! juce::approximatelyEqual(floatParameter(restored, "master/volume")->get(), 0.25f)
        || ! juce::approximatelyEqual(floatParameter(restored, "part/1/env/amp/attack")->get(), 1.5f)
        || choiceParameter(restored, "part/1/osc/1/waveSource")->getCurrentChoiceName() != "saw")
        return fail("core parameter round trip failed");
    juce::var roundTrip;
    if (juce::JSON::parse(restored.saveStateDocument(), roundTrip).failed()
        || static_cast<int>(roundTrip.getProperty("formatVersion", 0)) != 1)
        return fail("core state left format version 1");

    const auto before = restored.saveStateDocument();
    if (restored.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"master/volume\":\"loud\"}}")
        || restored.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"master/volume\":true}}")
        || restored.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"part/1/osc/1/waveSource\":\"wt/analog\"}}"))
        return fail("invalid core value was accepted");
    if (restored.saveStateDocument() != before)
        return fail("rejected core document changed the state");

    if (! restored.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"master/volume\":1,\"part/1/env/amp/release\":9}}"))
        return fail("numeric core values were rejected");
    if (! juce::approximatelyEqual(floatParameter(restored, "master/volume")->get(), 1.0f)
        || ! juce::approximatelyEqual(floatParameter(restored, "part/1/env/amp/release")->get(), 5.0f))
        return fail("core float was not clamped on load");
    if (! restored.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"part/1/env/amp/attack\":-4}}"))
        return fail("negative core time was rejected");
    if (! juce::approximatelyEqual(floatParameter(restored, "part/1/env/amp/attack")->get(), 0.001f))
        return fail("core time was not clamped to 1 ms");

    if (! restored.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"part/8/enabled\":true}}"))
        return fail("old document without core keys was rejected");
    if (! juce::approximatelyEqual(floatParameter(restored, "master/volume")->get(), 0.8f)
        || choiceParameter(restored, "part/1/osc/1/waveSource")->getCurrentChoiceName() != "sine"
        || ! boolParameter(restored, "part/8/enabled")->get())
        return fail("missing core keys did not return to defaults");

    if (! restored.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"part/1/osc/2/waveSource\":\"wt/analog\"}}"))
        return fail("unregistered wave source was rejected");
    juce::var parsed;
    juce::JSON::parse(restored.saveStateDocument(), parsed);
    const auto unknown = parsed.getProperty("unknown", {}).getProperty("parameters", {});
    const auto* unknownObject = unknown.getDynamicObject();
    const auto* preserved = unknownObject != nullptr
                                ? unknownObject->getProperties().getVarPointer(juce::Identifier("part/1/osc/2/waveSource"))
                                : nullptr;
    if (preserved == nullptr || preserved->toString() != "wt/analog")
        return fail("table id on an unregistered oscillator was not preserved");

    processor.releaseResources();
    restored.releaseResources();
    return 0;
}
} // namespace

int runMinimalCoreTests()
{
    if (const int silence = checkSilenceWithoutMidi())
        return silence;
    if (const int note = checkNoteOn())
        return note;
    if (const int release = checkReleaseAndRestart())
        return release;
    if (const int polyphony = checkPolyphonyAndPool())
        return polyphony;
    if (const int blocks = checkBlockSizes())
        return blocks;
    if (const int rates = checkSampleRates())
        return rates;
    if (const int waves = checkNaiveWavesStayFinite())
        return waves;
    if (const int mono = checkMonoLayout())
        return mono;
    if (const int clicks = checkClicks())
        return clicks;
    if (const int enable = checkPartEnableAndBypass())
        return enable;
    if (const int order = checkInvalidSampleRateAndOrder())
        return order;
    if (const int parameters = checkCoreParameters())
        return parameters;

    std::cout << "core ok\n";
    return 0;
}
