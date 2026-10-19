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
    for (int part = 1; part <= xenmilenium::MinimalVoiceCore::partCount; ++part)
    {
        const auto id = [part](const char* suffix)
        {
            return "xm.p" + juce::String(part) + "." + suffix;
        };
        auto& slot = partSlots[static_cast<std::size_t>(part - 1)];
        slot.enabled = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(id("enabled")));
        slot.mono = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(id("mono")));
        slot.legato = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(id("legato")));
        slot.midiChannel = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id("midiChannel")));
        slot.wave[0] = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id("o1.waveSource")));
        slot.wave[1] = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id("o2.waveSource")));
        slot.wave[2] = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id("o3.waveSource")));
        slot.osc1Level = floatSlot(apvts, id("mix.osc1").toRawUTF8());
        slot.osc2Level = floatSlot(apvts, id("mix.osc2").toRawUTF8());
        slot.ringLevel = floatSlot(apvts, id("mix.ring").toRawUTF8());
        slot.noiseLevel = floatSlot(apvts, id("mix.noise").toRawUTF8());
        slot.directOut = floatSlot(apvts, id("o3.directOut").toRawUTF8());
        slot.attack = floatSlot(apvts, id("amp.attack").toRawUTF8());
        slot.decay = floatSlot(apvts, id("amp.decay").toRawUTF8());
        slot.sustain = floatSlot(apvts, id("amp.sustain").toRawUTF8());
        slot.release = floatSlot(apvts, id("amp.release").toRawUTF8());
        slot.filterType = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id("flt.type")));
        slot.cutoff = floatSlot(apvts, id("flt.cutoff").toRawUTF8());
        slot.resonance = floatSlot(apvts, id("flt.resonance").toRawUTF8());
        slot.drive = floatSlot(apvts, id("flt.drive").toRawUTF8());
        slot.keytrack = floatSlot(apvts, id("flt.keytrack").toRawUTF8());
        slot.filterMix = floatSlot(apvts, id("flt.mix").toRawUTF8());
        slot.filterAttack = floatSlot(apvts, id("fenv.attack").toRawUTF8());
        slot.filterDecay = floatSlot(apvts, id("fenv.decay").toRawUTF8());
        slot.filterSustain = floatSlot(apvts, id("fenv.sustain").toRawUTF8());
        slot.filterRelease = floatSlot(apvts, id("fenv.release").toRawUTF8());
        slot.filterAmount = floatSlot(apvts, id("fenv.amount").toRawUTF8());
        slot.lfo1ToCutoff = floatSlot(apvts, id("l1.toCutoff").toRawUTF8());
        slot.lfo2ToPitch = floatSlot(apvts, id("l2.toPitch").toRawUTF8());
        for (int lfo = 1; lfo <= xenmilenium::MinimalVoiceCore::lfoCount; ++lfo)
        {
            const auto lfoId = [part, lfo](const char* suffix)
            {
                return "xm.p" + juce::String(part) + ".l" + juce::String(lfo) + "." + suffix;
            };
            auto& destination = slot.lfo[static_cast<std::size_t>(lfo - 1)];
            destination.shape = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(lfoId("shape")));
            destination.retrig = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(lfoId("retrig")));
            destination.polarity = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(lfoId("polarity")));
            destination.sync = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(lfoId("sync")));
            destination.rateHz = floatSlot(apvts, lfoId("rateHz").toRawUTF8());
            destination.rateSync = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(lfoId("rateSync")));
            destination.freeVariation = floatSlot(apvts, lfoId("freeVar").toRawUTF8());
        }

        for (int mseg = 1; mseg <= xenmilenium::msegCount; ++mseg)
        {
            const int index = (part - 1) * xenmilenium::msegCount + (mseg - 1);
            const auto modeId = "xm.p" + juce::String(part) + ".m" + juce::String(mseg) + ".mode";
            msegModeParams[static_cast<std::size_t>(index)] = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(modeId));
        }

        for (int slotIndex = 1; slotIndex <= xenmilenium::matrixSlotsPerPart; ++slotIndex)
        {
            const int index = (part - 1) * xenmilenium::matrixSlotsPerPart + (slotIndex - 1);
            const auto slotId = [part, slotIndex](const char* suffix)
            {
                return "xm.p" + juce::String(part) + ".s" + juce::String(slotIndex) + "." + suffix;
            };
            auto& matrix = matrixParams[static_cast<std::size_t>(index)];
            matrix.source = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(slotId("src")));
            matrix.destination = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(slotId("dst")));
            matrix.amount = floatSlot(apvts, slotId("amt").toRawUTF8());
            matrix.enabled = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(slotId("on")));
        }
    }

    voiceCore.publishMsegPoints(document->unknown);
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
        cacheMatrixAndModes();
        syncPartEnables();
        updateTempo();

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

void XenMileniumAudioProcessor::syncPartEnables() noexcept
{
    const auto controls = readControls();
    for (int part = 0; part < xenmilenium::MinimalVoiceCore::partCount; ++part)
        voiceCore.setPartEnabled(part, controls.parts[static_cast<std::size_t>(part)].enabled);
}

bool XenMileniumAudioProcessor::partAcceptsChannel(const xenmilenium::PartControls& part, int channel) noexcept
{
    if (! part.enabled || channel < 1 || channel > 16)
        return false;
    return part.midiChannel == 0 || part.midiChannel == channel;
}

void XenMileniumAudioProcessor::applyMidiToCore(const juce::MidiMessage& message) noexcept
{
    const auto controls = readControls();
    const int channel = message.getChannel();
    const bool sustain = message.isController() && message.getControllerNumber() == 64;
    const bool modWheel = message.isController() && message.getControllerNumber() == 1;
    const bool pedalDown = sustain && message.getControllerValue() >= 64;

    for (int part = 0; part < xenmilenium::MinimalVoiceCore::partCount; ++part)
    {
        const auto& partControls = controls.parts[static_cast<std::size_t>(part)];
        if (! partAcceptsChannel(partControls, channel))
            continue;

        if (sustain)
            voiceCore.setSustain(part, channel, pedalDown);
        else if (modWheel)
            voiceCore.setModWheel(part, channel, static_cast<float>(message.getControllerValue()) / 127.0f);
        else if (message.isNoteOn())
        {
            std::array<bool, xenmilenium::MinimalVoiceCore::lfoCount> retrig {};
            for (int index = 0; index < xenmilenium::MinimalVoiceCore::lfoCount; ++index)
                retrig[static_cast<std::size_t>(index)] = partControls.lfo[static_cast<std::size_t>(index)].retrig;
            voiceCore.noteOn(part, channel, message.getNoteNumber(), message.getFloatVelocity(), partControls.mono, partControls.legato, retrig);
        }
        else if (message.isNoteOff())
            voiceCore.noteOff(part, channel, message.getNoteNumber());
    }
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

void XenMileniumAudioProcessor::updateTempo() noexcept
{
    currentTempoBpm = 120.0f;
    if (auto* head = getPlayHead())
    {
        if (const auto position = head->getPosition())
        {
            if (const auto bpm = position->getBpm())
            {
                if (std::isfinite(*bpm) && *bpm > 0.0)
                    currentTempoBpm = static_cast<float>(*bpm);
            }
        }
    }
}

void XenMileniumAudioProcessor::cacheMatrixAndModes() noexcept
{
    for (int index = 0; index < xenmilenium::msegModuleCount; ++index)
    {
        const auto* mode = msegModeParams[static_cast<std::size_t>(index)];
        modeCache[static_cast<std::size_t>(index)] = mode != nullptr ? mode->getIndex() : 0;
    }

    for (int part = 0; part < xenmilenium::modulationPartCount; ++part)
    {
        bool active = false;
        for (int slot = 0; slot < xenmilenium::matrixSlotsPerPart; ++slot)
        {
            const int index = part * xenmilenium::matrixSlotsPerPart + slot;
            const auto& parameter = matrixParams[static_cast<std::size_t>(index)];
            auto& cached = matrixCache[static_cast<std::size_t>(index)];
            cached.source = parameter.source != nullptr ? parameter.source->getIndex() : 0;
            cached.destination = parameter.destination != nullptr ? parameter.destination->getIndex() : 0;
            cached.amount = finiteOr(parameter.amount != nullptr ? parameter.amount->get() : 0.0f, 0.0f);
            cached.enabled = parameter.enabled != nullptr && parameter.enabled->get();
            if (cached.enabled && std::abs(cached.amount) > 0.0f && cached.source != 0 && cached.destination != 0)
                active = true;
        }
        matrixActiveCache[static_cast<std::size_t>(part)] = active;
    }

    voiceCore.latchModulation(modeCache.data());
}

xenmilenium::EngineControls XenMileniumAudioProcessor::readControls() const noexcept
{
    xenmilenium::EngineControls controls;
    controls.matrix = matrixCache.data();
    controls.matrixActive = matrixActiveCache;
    controls.masterGain = finiteOr(masterVolume != nullptr ? masterVolume->get() : 0.8f, 0.8f);
    controls.tempoBpm = std::isfinite(currentTempoBpm) && currentTempoBpm > 0.0f ? currentTempoBpm : 120.0f;

    for (int part = 0; part < xenmilenium::MinimalVoiceCore::partCount; ++part)
    {
        const auto& slot = partSlots[static_cast<std::size_t>(part)];
        auto& destination = controls.parts[static_cast<std::size_t>(part)];
        destination.enabled = slot.enabled != nullptr ? slot.enabled->get() : part == 0;
        destination.mono = slot.mono != nullptr && slot.mono->get();
        destination.legato = slot.legato != nullptr && slot.legato->get();
        destination.midiChannel = slot.midiChannel != nullptr ? slot.midiChannel->getIndex() : 0;
        for (int oscillator = 0; oscillator < xenmilenium::MinimalVoiceCore::oscillatorCount; ++oscillator)
            destination.wave[static_cast<std::size_t>(oscillator)] = juce::jlimit(0, 3, slot.wave[static_cast<std::size_t>(oscillator)] != nullptr
                                                                                            ? slot.wave[static_cast<std::size_t>(oscillator)]->getIndex()
                                                                                            : 0);
        destination.osc1Level = finiteOr(slot.osc1Level != nullptr ? slot.osc1Level->get() : 1.0f, 1.0f);
        destination.osc2Level = finiteOr(slot.osc2Level != nullptr ? slot.osc2Level->get() : 0.0f, 0.0f);
        destination.ringLevel = finiteOr(slot.ringLevel != nullptr ? slot.ringLevel->get() : 0.0f, 0.0f);
        destination.noiseLevel = finiteOr(slot.noiseLevel != nullptr ? slot.noiseLevel->get() : 0.0f, 0.0f);
        destination.directOut = finiteOr(slot.directOut != nullptr ? slot.directOut->get() : 0.0f, 0.0f);
        destination.attackSeconds = finiteOr(slot.attack != nullptr ? slot.attack->get() : 0.01f, 0.01f);
        destination.decaySeconds = finiteOr(slot.decay != nullptr ? slot.decay->get() : 0.2f, 0.2f);
        destination.sustain = finiteOr(slot.sustain != nullptr ? slot.sustain->get() : 0.8f, 0.8f);
        destination.releaseSeconds = finiteOr(slot.release != nullptr ? slot.release->get() : 0.4f, 0.4f);
        destination.filterType = juce::jlimit(0, xenmilenium::MinimalVoiceCore::filterTypeCount - 1,
                                              slot.filterType != nullptr ? slot.filterType->getIndex() : 0);
        destination.cutoffHz = finiteOr(slot.cutoff != nullptr ? slot.cutoff->get() : 2000.0f, 2000.0f);
        destination.resonance = finiteOr(slot.resonance != nullptr ? slot.resonance->get() : 0.0f, 0.0f);
        destination.drive = finiteOr(slot.drive != nullptr ? slot.drive->get() : 0.0f, 0.0f);
        destination.keytrack = finiteOr(slot.keytrack != nullptr ? slot.keytrack->get() : 0.0f, 0.0f);
        destination.filterMix = finiteOr(slot.filterMix != nullptr ? slot.filterMix->get() : 0.0f, 0.0f);
        destination.filterAttackSeconds = finiteOr(slot.filterAttack != nullptr ? slot.filterAttack->get() : 0.01f, 0.01f);
        destination.filterDecaySeconds = finiteOr(slot.filterDecay != nullptr ? slot.filterDecay->get() : 0.2f, 0.2f);
        destination.filterSustain = finiteOr(slot.filterSustain != nullptr ? slot.filterSustain->get() : 0.8f, 0.8f);
        destination.filterReleaseSeconds = finiteOr(slot.filterRelease != nullptr ? slot.filterRelease->get() : 0.4f, 0.4f);
        destination.filterAmountSemitones = finiteOr(slot.filterAmount != nullptr ? slot.filterAmount->get() : 0.0f, 0.0f);
        destination.lfo1ToCutoffSemitones = finiteOr(slot.lfo1ToCutoff != nullptr ? slot.lfo1ToCutoff->get() : 0.0f, 0.0f);
        destination.lfo2ToPitchSemitones = finiteOr(slot.lfo2ToPitch != nullptr ? slot.lfo2ToPitch->get() : 0.0f, 0.0f);
        for (int index = 0; index < xenmilenium::MinimalVoiceCore::lfoCount; ++index)
        {
            const auto& lfo = slot.lfo[static_cast<std::size_t>(index)];
            auto& destinationLfo = destination.lfo[static_cast<std::size_t>(index)];
            destinationLfo.shape = juce::jlimit(0, xenmilenium::MinimalVoiceCore::lfoShapeCount - 1, lfo.shape != nullptr ? lfo.shape->getIndex() : 0);
            destinationLfo.retrig = lfo.retrig != nullptr && lfo.retrig->get();
            destinationLfo.unipolar = lfo.polarity != nullptr && lfo.polarity->getIndex() == 0;
            destinationLfo.sync = lfo.sync != nullptr && lfo.sync->get();
            destinationLfo.rateHz = finiteOr(lfo.rateHz != nullptr ? lfo.rateHz->get() : 1.0f, 1.0f);
            destinationLfo.rateSync = juce::jlimit(0, xenmilenium::MinimalVoiceCore::syncDivisionCount - 1, lfo.rateSync != nullptr ? lfo.rateSync->getIndex() : 6);
            destinationLfo.freeVariation = finiteOr(lfo.freeVariation != nullptr ? lfo.freeVariation->get() : 0.0f, 0.0f);
        }
    }

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
    if (! xenmilenium::deserialiseDocument(documentText, apvts, *document))
        return false;

    voiceCore.publishMsegPoints(document->unknown);
    return true;
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

int XenMileniumAudioProcessor::getActiveVoiceCountForPart(int partNumber) const noexcept
{
    if (partNumber < 1 || partNumber > xenmilenium::MinimalVoiceCore::partCount)
        return 0;
    return voiceCore.activeVoiceCountForPart(partNumber - 1);
}

int XenMileniumAudioProcessor::getActiveTailCount() const noexcept
{
    return voiceCore.activeTailCount();
}

bool XenMileniumAudioProcessor::isNoteActive(int partNumber, int channel, int noteNumber) const noexcept
{
    if (partNumber < 1 || partNumber > xenmilenium::MinimalVoiceCore::partCount)
        return false;
    return voiceCore.isNoteActive(partNumber - 1, channel, noteNumber);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new XenMileniumAudioProcessor();
}
