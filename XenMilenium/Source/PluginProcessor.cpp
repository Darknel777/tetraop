#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParameterRegistry.h"

XenMileniumAudioProcessor::XenMileniumAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, juce::Identifier("XenMileniumParameters"), xenmilenium::ParameterRegistry::createLayout()),
      document(std::make_unique<xenmilenium::StoredDocument>(xenmilenium::makeInitialDocument()))
{
}

XenMileniumAudioProcessor::~XenMileniumAudioProcessor() = default;

void XenMileniumAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;
}

void XenMileniumAudioProcessor::releaseResources()
{
}

bool XenMileniumAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    const auto input = layouts.getMainInputChannelSet();
    return input.isDisabled() && output == juce::AudioChannelSet::stereo();
}

void XenMileniumAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    handleMidi(midi);
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
    return 0.0;
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new XenMileniumAudioProcessor();
}
