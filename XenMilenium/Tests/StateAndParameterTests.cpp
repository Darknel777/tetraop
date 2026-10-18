#include "Parameters/ParameterRegistry.h"
#include "PluginProcessor.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_set>

namespace
{
int fail(const juce::String& message)
{
    std::cerr << message << '\n';
    return 1;
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

bool bufferIsSilent(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const float* samples = buffer.getReadPointer(channel);
        for (int index = 0; index < buffer.getNumSamples(); ++index)
        {
            std::uint32_t bits = 1;
            std::memcpy(&bits, samples + index, sizeof(bits));
            if (bits != 0u)
                return false;
        }
    }
    return true;
}

const juce::var* property(const juce::var& object, const char* name)
{
    if (auto* dynamicObject = object.getDynamicObject())
        return dynamicObject->getProperties().getVarPointer(juce::Identifier(name));
    return nullptr;
}

int checkRegistry()
{
    const auto& specs = xenmilenium::ParameterRegistry::all();
    if (specs.size() != 88)
        return fail("expected 88 base parameters, got " + juce::String(static_cast<int>(specs.size())));

    std::unordered_set<std::uint32_t> studioOneHashes;
    std::unordered_set<std::uint32_t> rawHashes;

    for (const auto& spec : specs)
    {
        if (spec.apvtsId.isEmpty() || spec.statePath.isEmpty() || spec.displayName.isEmpty())
            return fail("parameter is missing an id, path, or display name");
        if (spec.apvtsId == spec.statePath || spec.apvtsId == spec.displayName)
            return fail("APVTS id is not separate from the state path or display name: " + spec.apvtsId);
        if (spec.apvtsId.length() > 31)
            return fail("APVTS id is longer than 31 characters: " + spec.apvtsId);

        const auto studioOneHash = juce::VST3ClientExtensions::convertJuceParameterId(spec.apvtsId, true);
        const auto rawHash = juce::VST3ClientExtensions::convertJuceParameterId(spec.apvtsId, false);
        if (studioOneHash > 0x7fffffffu || rawHash == 0xffffffffu)
            return fail("VST3 parameter hash is outside the allowed range: " + spec.apvtsId);
        if (! studioOneHashes.insert(studioOneHash).second || ! rawHashes.insert(rawHash).second)
            return fail("VST3 parameter hash collision: " + spec.apvtsId);

        if (spec.kind == xenmilenium::ParameterKind::boolean && ! spec.choices.isEmpty())
            return fail("boolean parameter has choices: " + spec.statePath);
        if (spec.kind == xenmilenium::ParameterKind::choice
            && (spec.choices.size() < 2 || ! juce::isPositiveAndBelow(spec.defaultChoice, spec.choices.size())))
            return fail("choice parameter is incomplete: " + spec.statePath);
    }

    const auto* enabled = xenmilenium::ParameterRegistry::findByStatePath("part/1/enabled");
    const auto* modifier = xenmilenium::ParameterRegistry::findByStatePath("part/8/osc/3/modifier");
    const auto* position = xenmilenium::ParameterRegistry::findByStatePath("part/2/fx/1/position");
    if (enabled == nullptr || enabled->scope != xenmilenium::ParameterScope::part || enabled->apvtsId != "xm.p1.enabled")
        return fail("part enabled mapping is wrong");
    if (modifier == nullptr || modifier->scope != xenmilenium::ParameterScope::oscillator || modifier->apvtsId != "xm.p8.o3.modifier")
        return fail("oscillator modifier mapping is wrong");
    if (position == nullptr || position->scope != xenmilenium::ParameterScope::effect || position->apvtsId != "xm.p2.fx1.position")
        return fail("fx position mapping is wrong");
    if (xenmilenium::ParameterRegistry::findByStatePath("part/1/filter/cutoff") != nullptr)
        return fail("cutoff was registered without a confirmed range");
    if (xenmilenium::ParameterRegistry::findByStatePath("part/1/matrix/slot/1/amount") != nullptr)
        return fail("matrix amount was registered as a fake parameter");

    return 0;
}

int checkDefaultsAndAutomation()
{
    XenMileniumAudioProcessor processor;
    if (processor.getValueTreeState().state.getNumChildren() != static_cast<int>(xenmilenium::ParameterRegistry::all().size()))
        return fail("APVTS did not register every base parameter");

    auto* partOne = boolParameter(processor, "part/1/enabled");
    auto* partTwo = boolParameter(processor, "part/2/enabled");
    auto* channel = choiceParameter(processor, "part/1/midiChannel");
    auto* modifier = choiceParameter(processor, "part/4/osc/2/modifier");
    auto* position = choiceParameter(processor, "part/3/fx/2/position");
    auto* bypass = boolParameter(processor, "part/3/fx/2/bypass");
    if (partOne == nullptr || partTwo == nullptr || channel == nullptr || modifier == nullptr || position == nullptr || bypass == nullptr)
        return fail("a base parameter is missing from APVTS");

    if (! partOne->get() || partTwo->get() || bypass->get())
        return fail("boolean defaults do not match the empty-state schema");
    if (channel->getCurrentChoiceName() != "omni" || modifier->getCurrentChoiceName() != "off" || position->getCurrentChoiceName() != "post")
        return fail("choice defaults do not match the empty-state schema");

    partTwo->setValueNotifyingHost(1.0f);
    if (! partTwo->get())
        return fail("host notification did not enable part 2");
    static_cast<juce::AudioProcessorParameter*>(partTwo)->setValue(0.0f);
    if (partTwo->get())
        return fail("normalised 0 did not disable part 2");

    static_cast<juce::AudioProcessorParameter*>(channel)->setValue(0.5f);
    if (channel->getCurrentChoiceName() != "8")
        return fail("midi channel 0.5 did not snap to choice 8, got " + channel->getCurrentChoiceName());
    static_cast<juce::AudioProcessorParameter*>(channel)->setValue(0.0f);
    if (channel->getCurrentChoiceName() != "omni")
        return fail("midi channel did not return to omni");

    if (processor.getValueTreeState().getRawParameterValue("xm.p1.enabled") == nullptr)
        return fail("raw parameter value is unavailable");

    processor.prepareToPlay(48000.0, 32);
    juce::AudioBuffer<float> buffer(2, 32);
    for (int channelIndex = 0; channelIndex < 2; ++channelIndex)
        for (int sample = 0; sample < 32; ++sample)
            buffer.setSample(channelIndex, sample, 0.25f);
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    if (! bufferIsSilent(buffer) || ! partOne->get())
        return fail("processBlock changed audio or a parameter");
    processor.releaseResources();
    return 0;
}

int checkRoundTrip()
{
    XenMileniumAudioProcessor processor;
    auto* partEight = boolParameter(processor, "part/8/enabled");
    auto* modifier = choiceParameter(processor, "part/1/osc/1/modifier");
    static_cast<juce::AudioProcessorParameter*>(partEight)->setValue(1.0f);
    static_cast<juce::AudioProcessorParameter*>(modifier)->setValue(modifier->convertTo0to1(3.0f));

    juce::MemoryBlock block;
    processor.getStateInformation(block);
    const auto saved = juce::String::fromUTF8(static_cast<const char*>(block.getData()), static_cast<int>(block.getSize()));
    if (! saved.contains("\"part/1/enabled\"") || saved.contains("\"xm.p1.enabled\""))
        return fail("saved document does not use state paths");
    if (saved.contains("\"notes\""))
        return fail("fresh document wrote documentary notes");

    XenMileniumAudioProcessor restored;
    restored.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    auto* restoredEnabled = boolParameter(restored, "part/8/enabled");
    auto* restoredModifier = choiceParameter(restored, "part/1/osc/1/modifier");
    auto* restoredPartOne = boolParameter(restored, "part/1/enabled");
    if (restoredEnabled == nullptr || ! restoredEnabled->get() || restoredPartOne == nullptr || ! restoredPartOne->get())
        return fail("round trip lost part enables");
    if (restoredModifier == nullptr || restoredModifier->getCurrentChoiceName() != "bendPlus")
        return fail("round trip lost modifier choice");

    const auto before = restored.saveStateDocument();
    if (restored.restoreStateDocument("{\"formatVersion\":2}"))
        return fail("future format version was accepted");
    if (restored.saveStateDocument() != before)
        return fail("rejected document changed the state");
    if (restored.restoreStateDocument("") || restored.restoreStateDocument("{") || restored.restoreStateDocument("null")
        || restored.restoreStateDocument("[]") || restored.restoreStateDocument("{\"parameters\":{}}"))
        return fail("corrupt document was accepted");
    if (restored.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"part/1/enabled\":\"yes\"}}"))
        return fail("wrong boolean type was accepted");
    if (restored.restoreStateDocument("{\"formatVersion\":1,\"parameters\":{\"part/1/osc/1/modifier\":\"stack\"}}"))
        return fail("unknown modifier value was accepted");
    if (! restoredEnabled->get() || restoredModifier->getCurrentChoiceName() != "bendPlus")
        return fail("rejected documents reset parameter values");

    if (! restored.restoreStateDocument(
            "{\"formatVersion\":1,\"parameters\":{\"part/9/volume\":0.5,\"part/1/mseg/1/points\":[]},\"embeddedWavetables\":[{\"id\":\"wt/test\"}],\"customFlag\":true}"))
        return fail("document with unregistered paths was rejected");

    juce::var parsed;
    if (juce::JSON::parse(restored.saveStateDocument(), parsed).failed())
        return fail("saved document is not JSON");
    const auto* parameters = property(parsed, "parameters");
    const auto* unknown = property(parsed, "unknown");
    const auto* tables = property(parsed, "embeddedWavetables");
    if (parameters == nullptr || unknown == nullptr || tables == nullptr || ! tables->isArray() || tables->getArray()->size() != 1)
        return fail("saved document lost a top-level section");
    const auto* unknownParameters = property(unknown->getProperty("parameters", {}), "part/9/volume");
    const auto* points = property(unknown->getProperty("parameters", {}), "part/1/mseg/1/points");
    const auto* customFlag = property(unknown->getProperty("document", {}), "customFlag");
    if (unknownParameters == nullptr || ! juce::approximatelyEqual(static_cast<double>(*unknownParameters), 0.5))
        return fail("unregistered numeric path was not preserved");
    if (points == nullptr || ! points->isArray())
        return fail("MSEG points were not preserved as a document value");
    if (customFlag == nullptr || ! static_cast<bool>(*customFlag))
        return fail("unknown top-level key was not preserved");
    if (property(*parameters, "part/1/mseg/1/points") != nullptr)
        return fail("MSEG points were stored as an automatable parameter");

    const auto withUi = restored.saveStateDocument();
    if (! restored.restoreStateDocument("{\"formatVersion\":1,\"ui\":{\"focusedPart\":4}}"))
        return fail("ui object was rejected");
    juce::var withFocused;
    juce::JSON::parse(restored.saveStateDocument(), withFocused);
    const auto* focused = property(withFocused.getProperty("ui", {}), "focusedPart");
    if (focused == nullptr || static_cast<int>(*focused) != 4)
        return fail("ui was not replaced");
    if (! restored.restoreStateDocument(withUi))
        return fail("could not restore the previous document");

    return 0;
}

int checkEmptyExample()
{
    const juce::File example(XEN_MILENIUM_EMPTY_PRESET_PATH);
    if (! example.existsAsFile())
        return fail("empty preset example is missing");

    XenMileniumAudioProcessor processor;
    if (! processor.restoreStateDocument(example.loadFileAsString()))
        return fail("empty preset example was rejected");

    auto* enabled = boolParameter(processor, "part/8/enabled");
    auto* channel = choiceParameter(processor, "part/5/midiChannel");
    auto* modifier = choiceParameter(processor, "part/8/osc/3/modifier");
    auto* position = choiceParameter(processor, "part/1/fx/1/position");
    if (enabled == nullptr || enabled->get() || channel == nullptr || channel->getCurrentChoiceName() != "omni")
        return fail("empty example did not apply part defaults");
    if (modifier == nullptr || modifier->getCurrentChoiceName() != "off" || position == nullptr || position->getCurrentChoiceName() != "post")
        return fail("empty example did not apply modifier or fx position");

    juce::var parsed;
    if (juce::JSON::parse(processor.saveStateDocument(), parsed).failed())
        return fail("could not parse the document saved from the empty example");
    if (property(parsed, "notes") != nullptr || property(parsed, "openQuestions") != nullptr)
        return fail("documentary keys were stored as plugin state");

    const auto unknownParameters = parsed.getProperty("unknown", {}).getProperty("parameters", {});
    const auto* unison = property(unknownParameters, "part/1/osc/1/unison");
    const auto* cutoff = property(unknownParameters, "part/1/filter/cutoff");
    const auto* amount = property(unknownParameters, "part/1/matrix/slot/1/amount");
    if (unison == nullptr || static_cast<int>(*unison) != 1)
        return fail("unison from the example was not preserved outside APVTS");
    if (cutoff != nullptr)
        return fail("null cutoff was stored as a value");
    if (amount == nullptr || static_cast<int>(*amount) != 0)
        return fail("matrix amount was not preserved as state");
    if (property(*property(parsed, "parameters"), "part/1/enabled") == nullptr)
        return fail("registered path disappeared after loading the example");

    processor.prepareToPlay(44100.0, 16);
    juce::AudioBuffer<float> buffer(2, 16);
    buffer.clear();
    buffer.setSample(0, 0, -1.0f);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    processor.processBlock(buffer, midi);
    if (! bufferIsSilent(buffer))
        return fail("loaded parameters produced audio");
    processor.releaseResources();
    return 0;
}
} // namespace

int runStateAndParameterTests()
{
    if (const int registry = checkRegistry())
        return registry;
    if (const int defaults = checkDefaultsAndAutomation())
        return defaults;
    if (const int roundTrip = checkRoundTrip())
        return roundTrip;
    if (const int example = checkEmptyExample())
        return example;

    std::cout << "state ok\n";
    return 0;
}
