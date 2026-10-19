#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParameterRegistry.h"

#include <cmath>

namespace
{
juce::AudioParameterFloat* floatSlot(juce::AudioProcessorValueTreeState& state, const char* id)
{
    return dynamic_cast<juce::AudioParameterFloat*>(state.getParameter(id));
}

float finiteOr(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}
} // namespace

XenMileniumAudioProcessor::XenMileniumAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, juce::Identifier("XenMileniumParameters"), xenmilenium::ParameterRegistry::createLayout()),
      document(std::make_unique<xenmilenium::StoredDocument>(xenmilenium::makeInitialDocument()))
{
    // get() / getIndex() читают атом, который обновляет и setValue, и автоматизация.
    // getRawParameterValue в этом JUCE от setValue не обновляется, поэтому пресет его не использует.
    masterVolume = floatSlot(apvts, "xm.master.volume");
    waveSource = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("xm.p1.o1.waveSource"));
    ampAttack = floatSlot(apvts, "xm.p1.amp.attack");
    ampDecay = floatSlot(apvts, "xm.p1.amp.decay");
    ampSustain = floatSlot(apvts, "xm.p1.amp.sustain");
    ampRelease = floatSlot(apvts, "xm.p1.amp.release");
    partOneEnabled = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("xm.p1.enabled"));
}

XenMileniumAudioProcessor::~XenMileniumAudioProcessor() = default;

void XenMileniumAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 0.0;
    voiceCore.prepare(currentSampleRate);
}

void XenMileniumAudioProcessor::releaseResources()
{
    voiceCore.reset();
}

bool XenMileniumAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    const auto input = layouts.getMainInputChannelSet();
    return input.isDisabled()
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void XenMileniumAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // Входного аудио нет: входящий мусор стирается до расчёта. После генерации буфер не очищается.
    buffer.clear();
    handleMidi(midi);

    const int numSamples = buffer.getNumSamples();
    if (numSamples > 0 && currentSampleRate > 0.0)
    {
        const bool enabled = partOneEnabled == nullptr || partOneEnabled->get();
        voiceCore.setPartEnabled(enabled);

        int rendered = 0;
        for (const auto metadata : midi)
        {
            const int eventAt = juce::jlimit(0, numSamples, metadata.samplePosition);
            renderRange(buffer, rendered, eventAt);
            rendered = eventAt;
            applyMidiToCore(metadata.getMessage());
        }

        renderRange(buffer, rendered, numSamples);
    }

    midi.clear();
}

void XenMileniumAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    midi.clear();
}

void XenMileniumAudioProcessor::handleMidi(const juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isNoteOn())
            continue;

        const int note = message.getNoteNumber();
        if (note < 0 || note > 127)
            continue;

        lastNoteNumber.store(note, std::memory_order_relaxed);
        noteOnCount.fetch_add(1, std::memory_order_relaxed);
    }
}

void XenMileniumAudioProcessor::applyMidiToCore(const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
        voiceCore.noteOn(message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff())
        voiceCore.noteOff(message.getNoteNumber());
}

void XenMileniumAudioProcessor::renderRange(juce::AudioBuffer<float>& buffer, int startSample, int endSample) noexcept
{
    const int channels = buffer.getNumChannels();
    for (int index = startSample; index < endSample; ++index)
    {
        const float sample = voiceCore.renderSample(readControls());
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample(channel, index, sample);
    }
}

xenmilenium::AmplitudeControls XenMileniumAudioProcessor::readControls() const noexcept
{
    xenmilenium::AmplitudeControls controls;
    controls.attackSeconds = finiteOr(ampAttack != nullptr ? ampAttack->get() : 0.01f, 0.01f);
    controls.decaySeconds = finiteOr(ampDecay != nullptr ? ampDecay->get() : 0.2f, 0.2f);
    controls.sustain = finiteOr(ampSustain != nullptr ? ampSustain->get() : 0.8f, 0.8f);
    controls.releaseSeconds = finiteOr(ampRelease != nullptr ? ampRelease->get() : 0.4f, 0.4f);
    controls.masterGain = finiteOr(masterVolume != nullptr ? masterVolume->get() : 0.8f, 0.8f);
    controls.wave = juce::jlimit(0, 3, waveSource != nullptr ? waveSource->getIndex() : 0);
    return controls;
}

juce::AudioProcessorEditor* XenMileniumAudioProcessor::createEditor()
{
    return new XenMileniumAudioProcessorEditor(*this);
}

bool XenMileniumAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String XenMileniumAudioProcessor::getName() const
{
    return "Xen Milenium";
}

bool XenMileniumAudioProcessor::acceptsMidi() const
{
    return true;
}

bool XenMileniumAudioProcessor::producesMidi() const
{
    return false;
}

bool XenMileniumAudioProcessor::isMidiEffect() const
{
    return false;
}

double XenMileniumAudioProcessor::getTailLengthSeconds() const
{
    return 5.0;
}

int XenMileniumAudioProcessor::getNumPrograms()
{
    return 1;
}

int XenMileniumAudioProcessor::getCurrentProgram()
{
    return 0;
}

void XenMileniumAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String XenMileniumAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return "Default";
}

void XenMileniumAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void XenMileniumAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto documentText = saveStateDocument();
    destData.replaceAll(documentText.toRawUTF8(), documentText.getNumBytesAsUTF8());
}

void XenMileniumAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    restoreStateDocument(juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes));
}

juce::AudioProcessorValueTreeState& XenMileniumAudioProcessor::getValueTreeState() noexcept
{
    return apvts;
}

const juce::AudioProcessorValueTreeState& XenMileniumAudioProcessor::getValueTreeState() const noexcept
{
    return apvts;
}

juce::String XenMileniumAudioProcessor::saveStateDocument() const
{
    return xenmilenium::serialiseDocument(apvts, *document);
}

bool XenMileniumAudioProcessor::restoreStateDocument(const juce::String& documentText)
{
    return xenmilenium::deserialiseDocument(documentText, apvts, *document);
}

int XenMileniumAudioProcessor::getLastNoteNumber() const noexcept
{
    return lastNoteNumber.load(std::memory_order_relaxed);
}

int XenMileniumAudioProcessor::getNoteOnCount() const noexcept
{
    return noteOnCount.load(std::memory_order_relaxed);
}

int XenMileniumAudioProcessor::getActiveVoiceCount() const noexcept
{
    return voiceCore.activeVoiceCount();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new XenMileniumAudioProcessor();
}
