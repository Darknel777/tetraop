#include "ParameterRegistry.h"

#include <unordered_map>

namespace xenmilenium
{
namespace
{
juce::String partPath(int part, const char* suffix)
{
    return "part/" + juce::String(part) + "/" + suffix;
}

juce::String partId(int part, const char* suffix)
{
    return "xm.p" + juce::String(part) + "." + suffix;
}

juce::String partLabel(int part, const char* label)
{
    return "Part " + juce::String(part) + " " + label;
}

ParameterSpec booleanSpec(juce::String apvtsId,
                          juce::String statePath,
                          juce::String displayName,
                          ParameterScope scope,
                          bool defaultValue)
{
    ParameterSpec spec;
    spec.apvtsId = std::move(apvtsId);
    spec.statePath = std::move(statePath);
    spec.displayName = std::move(displayName);
    spec.kind = ParameterKind::boolean;
    spec.scope = scope;
    spec.defaultBool = defaultValue;
    return spec;
}

ParameterSpec choiceSpec(juce::String apvtsId,
                         juce::String statePath,
                         juce::String displayName,
                         ParameterScope scope,
                         juce::StringArray choices,
                         int defaultChoice)
{
    ParameterSpec spec;
    spec.apvtsId = std::move(apvtsId);
    spec.statePath = std::move(statePath);
    spec.displayName = std::move(displayName);
    spec.kind = ParameterKind::choice;
    spec.scope = scope;
    spec.choices = std::move(choices);
    spec.defaultChoice = defaultChoice;
    return spec;
}

juce::StringArray midiChannelChoices()
{
    juce::StringArray choices;
    choices.add("omni");
    for (int channel = 1; channel <= 16; ++channel)
        choices.add(juce::String(channel));
    return choices;
}

juce::StringArray modifierChoices()
{
    return { "off", "sync", "fm", "bendPlus", "bendMinus", "noise", "pw" };
}

juce::StringArray positionChoices()
{
    return { "pre", "post" };
}

juce::StringArray basicWaveChoices()
{
    return { "sine", "saw", "triangle", "pulse" };
}

ParameterSpec floatSpec(juce::String apvtsId,
                        juce::String statePath,
                        juce::String displayName,
                        ParameterScope scope,
                        float minimum,
                        float maximum,
                        float defaultValue,
                        juce::String unit)
{
    ParameterSpec spec;
    spec.apvtsId = std::move(apvtsId);
    spec.statePath = std::move(statePath);
    spec.displayName = std::move(displayName);
    spec.kind = ParameterKind::floatValue;
    spec.scope = scope;
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.defaultFloat = defaultValue;
    spec.unit = std::move(unit);
    return spec;
}

std::vector<ParameterSpec> buildSpecs()
{
    std::vector<ParameterSpec> specs;
    specs.reserve(94);

    const auto midiChoices = midiChannelChoices();
    const auto modifiers = modifierChoices();
    const auto positions = positionChoices();

    for (int part = 1; part <= 8; ++part)
    {
        specs.push_back(booleanSpec(partId(part, "enabled"),
                                    partPath(part, "enabled"),
                                    partLabel(part, "Enabled"),
                                    ParameterScope::part,
                                    part == 1));
        specs.push_back(booleanSpec(partId(part, "mono"),
                                    partPath(part, "mono"),
                                    partLabel(part, "Mono"),
                                    ParameterScope::part,
                                    false));
        specs.push_back(booleanSpec(partId(part, "legato"),
                                    partPath(part, "legato"),
                                    partLabel(part, "Legato"),
                                    ParameterScope::part,
                                    false));
        specs.push_back(choiceSpec(partId(part, "midiChannel"),
                                   partPath(part, "midiChannel"),
                                   partLabel(part, "MIDI Channel"),
                                   ParameterScope::part,
                                   midiChoices,
                                   0));

        for (int oscillator = 1; oscillator <= 3; ++oscillator)
        {
            const auto oscSuffix = "osc/" + juce::String(oscillator) + "/modifier";
            const auto oscId = "o" + juce::String(oscillator) + ".modifier";
            specs.push_back(choiceSpec(partId(part, oscId.toRawUTF8()),
                                       partPath(part, oscSuffix.toRawUTF8()),
                                       partLabel(part, ("Osc " + juce::String(oscillator) + " Modifier").toRawUTF8()),
                                       ParameterScope::oscillator,
                                       modifiers,
                                       0));
        }

        for (int block = 1; block <= 2; ++block)
        {
            const auto blockSuffix = "fx/" + juce::String(block) + "/";
            const auto blockId = "fx" + juce::String(block) + ".";
            specs.push_back(booleanSpec(partId(part, (blockId + "bypass").toRawUTF8()),
                                        partPath(part, (blockSuffix + "bypass").toRawUTF8()),
                                        partLabel(part, ("FX" + juce::String(block) + " Bypass").toRawUTF8()),
                                        ParameterScope::effect,
                                        false));
            specs.push_back(choiceSpec(partId(part, (blockId + "position").toRawUTF8()),
                                       partPath(part, (blockSuffix + "position").toRawUTF8()),
                                       partLabel(part, ("FX" + juce::String(block) + " Position").toRawUTF8()),
                                       ParameterScope::effect,
                                       positions,
                                       1));
        }
    }

    // Контракт минимального ядра этапа 1.3, не закон всего инструмента.
    // Громкость партии, env/amp/level и копии на партии 2–8 намеренно отсутствуют.
    specs.push_back(floatSpec("xm.master.volume",
                              "master/volume",
                              "Master Volume",
                              ParameterScope::global,
                              0.0f,
                              1.0f,
                              0.8f,
                              "linear"));
    specs.push_back(choiceSpec("xm.p1.o1.waveSource",
                               "part/1/osc/1/waveSource",
                               "Part 1 Osc 1 Wave",
                               ParameterScope::oscillator,
                               basicWaveChoices(),
                               0));
    specs.push_back(floatSpec("xm.p1.amp.attack",
                              "part/1/env/amp/attack",
                              "Part 1 Amp Attack",
                              ParameterScope::part,
                              0.001f,
                              5.0f,
                              0.01f,
                              "s"));
    specs.push_back(floatSpec("xm.p1.amp.decay",
                              "part/1/env/amp/decay",
                              "Part 1 Amp Decay",
                              ParameterScope::part,
                              0.001f,
                              5.0f,
                              0.2f,
                              "s"));
    specs.push_back(floatSpec("xm.p1.amp.sustain",
                              "part/1/env/amp/sustain",
                              "Part 1 Amp Sustain",
                              ParameterScope::part,
                              0.0f,
                              1.0f,
                              0.8f,
                              "linear"));
    specs.push_back(floatSpec("xm.p1.amp.release",
                              "part/1/env/amp/release",
                              "Part 1 Amp Release",
                              ParameterScope::part,
                              0.001f,
                              5.0f,
                              0.4f,
                              "s"));

    return specs;
}

struct Index
{
    std::vector<ParameterSpec> specs;
    std::unordered_map<std::string, std::size_t> byPath;
    std::unordered_map<std::string, std::size_t> byId;

    Index()
        : specs(buildSpecs())
    {
        for (std::size_t i = 0; i < specs.size(); ++i)
        {
            byPath.emplace(specs[i].statePath.toStdString(), i);
            byId.emplace(specs[i].apvtsId.toStdString(), i);
        }

        jassert(byPath.size() == specs.size());
        jassert(byId.size() == specs.size());
    }
};

const Index& index()
{
    static const Index built;
    return built;
}

const ParameterSpec* lookup(const std::unordered_map<std::string, std::size_t>& map, const juce::String& key)
{
    const auto found = map.find(key.toStdString());
    if (found == map.end())
        return nullptr;
    return &index().specs[found->second];
}
} // namespace

const std::vector<ParameterSpec>& ParameterRegistry::all()
{
    return index().specs;
}

const ParameterSpec* ParameterRegistry::findByStatePath(const juce::String& statePath)
{
    return lookup(index().byPath, statePath);
}

const ParameterSpec* ParameterRegistry::findByApvtsId(const juce::String& apvtsId)
{
    return lookup(index().byId, apvtsId);
}

juce::AudioProcessorValueTreeState::ParameterLayout ParameterRegistry::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& spec : all())
    {
        const juce::ParameterID id { spec.apvtsId, 1 };

        if (spec.kind == ParameterKind::boolean)
        {
            layout.add(std::make_unique<juce::AudioParameterBool>(id, spec.displayName, spec.defaultBool));
            continue;
        }

        if (spec.kind == ParameterKind::floatValue)
        {
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                id,
                spec.displayName,
                juce::NormalisableRange<float>(spec.minimum, spec.maximum),
                spec.defaultFloat,
                juce::AudioParameterFloatAttributes().withLabel(spec.unit)));
            continue;
        }

        layout.add(std::make_unique<juce::AudioParameterChoice>(id, spec.displayName, spec.choices, spec.defaultChoice));
    }

    return layout;
}

juce::String ParameterRegistry::plainChoice(const ParameterSpec& spec)
{
    jassert(spec.kind == ParameterKind::choice);
    jassert(juce::isPositiveAndBelow(spec.defaultChoice, spec.choices.size()));
    return spec.choices[spec.defaultChoice];
}

int ParameterRegistry::indexOfChoice(const ParameterSpec& spec, const juce::String& plain)
{
    jassert(spec.kind == ParameterKind::choice);
    return spec.choices.indexOf(plain);
}

} // namespace xenmilenium
