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

juce::StringArray filterTypeChoices()
{
    return { "lp12", "lp24", "lp36", "bp12", "bp24", "bp36", "hp12", "hp24", "hp36",
             "notch", "peak", "combPlus", "combMinus", "formant" };
}

juce::StringArray lfoShapeChoices()
{
    return { "sine", "triangle", "sawUp", "sawDown", "pulse", "randomHold" };
}

juce::StringArray lfoPolarityChoices()
{
    return { "unipolar", "bipolar" };
}

juce::StringArray lfoSyncChoices()
{
    return { "16/1", "8/1", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
}

ParameterSpec floatSpec(juce::String apvtsId,
                        juce::String statePath,
                        juce::String displayName,
                        ParameterScope scope,
                        float minimum,
                        float maximum,
                        float defaultValue,
                        juce::String unit,
                        float skewCentre = 0.0f)
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
    spec.skewCentre = skewCentre;
    spec.unit = std::move(unit);
    return spec;
}

std::vector<ParameterSpec> buildSpecs()
{
    std::vector<ParameterSpec> specs;
    specs.reserve(513);

    const auto midiChoices = midiChannelChoices();
    const auto modifiers = modifierChoices();
    const auto positions = positionChoices();
    const auto filterTypes = filterTypeChoices();
    const auto lfoShapes = lfoShapeChoices();
    const auto lfoPolarity = lfoPolarityChoices();
    const auto lfoSync = lfoSyncChoices();

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

        // Формы и уровни микшера — контракт этапа 1.5, не закон инструмента.
        // Уровень 0 глушит источник. Отдельного включения осциллятора в схеме нет.
        const auto waves = basicWaveChoices();
        specs.push_back(choiceSpec(partId(part, "o1.waveSource"),
                                   partPath(part, "osc/1/waveSource"),
                                   partLabel(part, "Osc 1 Wave"),
                                   ParameterScope::oscillator,
                                   waves,
                                   0));
        specs.push_back(choiceSpec(partId(part, "o2.waveSource"),
                                   partPath(part, "osc/2/waveSource"),
                                   partLabel(part, "Osc 2 Wave"),
                                   ParameterScope::oscillator,
                                   waves,
                                   0));
        specs.push_back(choiceSpec(partId(part, "o3.waveSource"),
                                   partPath(part, "osc/3/waveSource"),
                                   partLabel(part, "Osc 3 Wave"),
                                   ParameterScope::oscillator,
                                   waves,
                                   0));
        specs.push_back(floatSpec(partId(part, "mix.osc1"),
                                  partPath(part, "mixer/osc1"),
                                  partLabel(part, "Mixer OSC1"),
                                  ParameterScope::part,
                                  0.0f,
                                  1.0f,
                                  1.0f,
                                  "linear"));
        specs.push_back(floatSpec(partId(part, "mix.osc2"),
                                  partPath(part, "mixer/osc2"),
                                  partLabel(part, "Mixer OSC2"),
                                  ParameterScope::part,
                                  0.0f,
                                  1.0f,
                                  0.0f,
                                  "linear"));
        specs.push_back(floatSpec(partId(part, "mix.ring"),
                                  partPath(part, "mixer/ring"),
                                  partLabel(part, "Mixer RING"),
                                  ParameterScope::part,
                                  0.0f,
                                  1.0f,
                                  0.0f,
                                  "linear"));
        specs.push_back(floatSpec(partId(part, "mix.noise"),
                                  partPath(part, "mixer/noise"),
                                  partLabel(part, "Mixer Noise"),
                                  ParameterScope::part,
                                  0.0f,
                                  1.0f,
                                  0.0f,
                                  "linear"));
        specs.push_back(floatSpec(partId(part, "o3.directOut"),
                                  partPath(part, "osc/3/directOut"),
                                  partLabel(part, "Osc 3 Direct Out"),
                                  ParameterScope::oscillator,
                                  0.0f,
                                  1.0f,
                                  0.0f,
                                  "linear"));
        specs.push_back(floatSpec(partId(part, "amp.attack"),
                                  partPath(part, "env/amp/attack"),
                                  partLabel(part, "Amp Attack"),
                                  ParameterScope::part,
                                  0.001f,
                                  5.0f,
                                  0.01f,
                                  "s"));
        specs.push_back(floatSpec(partId(part, "amp.decay"),
                                  partPath(part, "env/amp/decay"),
                                  partLabel(part, "Amp Decay"),
                                  ParameterScope::part,
                                  0.001f,
                                  5.0f,
                                  0.2f,
                                  "s"));
        specs.push_back(floatSpec(partId(part, "amp.sustain"),
                                  partPath(part, "env/amp/sustain"),
                                  partLabel(part, "Amp Sustain"),
                                  ParameterScope::part,
                                  0.0f,
                                  1.0f,
                                  0.8f,
                                  "linear"));
        specs.push_back(floatSpec(partId(part, "amp.release"),
                                  partPath(part, "env/amp/release"),
                                  partLabel(part, "Amp Release"),
                                  ParameterScope::part,
                                  0.001f,
                                  5.0f,
                                  0.4f,
                                  "s"));

        specs.push_back(choiceSpec(partId(part, "flt.type"),
                                   partPath(part, "filter/type"),
                                   partLabel(part, "Filter Type"),
                                   ParameterScope::part,
                                   filterTypes,
                                   0));
        specs.push_back(floatSpec(partId(part, "flt.cutoff"),
                                  partPath(part, "filter/cutoff"),
                                  partLabel(part, "Filter Cutoff"),
                                  ParameterScope::part,
                                  20.0f,
                                  20000.0f,
                                  2000.0f,
                                  "Hz",
                                  1000.0f));
        specs.push_back(floatSpec(partId(part, "flt.resonance"),
                                  partPath(part, "filter/resonance"),
                                  partLabel(part, "Filter Resonance"),
                                  ParameterScope::part,
                                  0.0f,
                                  1.0f,
                                  0.0f,
                                  "linear"));
        specs.push_back(floatSpec(partId(part, "flt.drive"),
                                  partPath(part, "filter/drive"),
                                  partLabel(part, "Filter Drive"),
                                  ParameterScope::part,
                                  0.0f,
                                  1.0f,
                                  0.0f,
                                  "linear"));
        specs.push_back(floatSpec(partId(part, "flt.keytrack"),
                                  partPath(part, "filter/keytrack"),
                                  partLabel(part, "Filter Keytrack"),
                                  ParameterScope::part,
                                  -1.0f,
                                  1.0f,
                                  0.0f,
                                  "oct/oct"));
        specs.push_back(floatSpec(partId(part, "flt.mix"),
                                  partPath(part, "filter/mix"),
                                  partLabel(part, "Filter Mix"),
                                  ParameterScope::part,
                                  0.0f,
                                  1.0f,
                                  0.0f,
                                  "linear"));
        specs.push_back(floatSpec(partId(part, "fenv.attack"),
                                  partPath(part, "env/filter/attack"),
                                  partLabel(part, "Filter Attack"),
                                  ParameterScope::part,
                                  0.001f,
                                  5.0f,
                                  0.01f,
                                  "s"));
        specs.push_back(floatSpec(partId(part, "fenv.decay"),
                                  partPath(part, "env/filter/decay"),
                                  partLabel(part, "Filter Decay"),
                                  ParameterScope::part,
                                  0.001f,
                                  5.0f,
                                  0.2f,
                                  "s"));
        specs.push_back(floatSpec(partId(part, "fenv.sustain"),
                                  partPath(part, "env/filter/sustain"),
                                  partLabel(part, "Filter Sustain"),
                                  ParameterScope::part,
                                  0.0f,
                                  1.0f,
                                  0.8f,
                                  "linear"));
        specs.push_back(floatSpec(partId(part, "fenv.release"),
                                  partPath(part, "env/filter/release"),
                                  partLabel(part, "Filter Release"),
                                  ParameterScope::part,
                                  0.001f,
                                  5.0f,
                                  0.4f,
                                  "s"));
        specs.push_back(floatSpec(partId(part, "fenv.amount"),
                                  partPath(part, "env/filter/amount"),
                                  partLabel(part, "Filter Amount"),
                                  ParameterScope::part,
                                  -48.0f,
                                  48.0f,
                                  0.0f,
                                  "st"));

        for (int lfo = 1; lfo <= 4; ++lfo)
        {
            const juce::String suffix = "l" + juce::String(lfo) + ".";
            const juce::String path = "lfo/" + juce::String(lfo) + "/";
            const juce::String label = "LFO " + juce::String(lfo) + " ";
            specs.push_back(choiceSpec(partId(part, (suffix + "shape").toRawUTF8()),
                                       partPath(part, (path + "shape").toRawUTF8()),
                                       partLabel(part, (label + "Shape").toRawUTF8()),
                                       ParameterScope::part,
                                       lfoShapes,
                                       0));
            specs.push_back(booleanSpec(partId(part, (suffix + "retrig").toRawUTF8()),
                                        partPath(part, (path + "retrig").toRawUTF8()),
                                        partLabel(part, (label + "Retrig").toRawUTF8()),
                                        ParameterScope::part,
                                        false));
            specs.push_back(choiceSpec(partId(part, (suffix + "polarity").toRawUTF8()),
                                       partPath(part, (path + "polarity").toRawUTF8()),
                                       partLabel(part, (label + "Polarity").toRawUTF8()),
                                       ParameterScope::part,
                                       lfoPolarity,
                                       1));
            specs.push_back(booleanSpec(partId(part, (suffix + "sync").toRawUTF8()),
                                        partPath(part, (path + "sync").toRawUTF8()),
                                        partLabel(part, (label + "Sync").toRawUTF8()),
                                        ParameterScope::part,
                                        false));
            specs.push_back(floatSpec(partId(part, (suffix + "rateHz").toRawUTF8()),
                                     partPath(part, (path + "rateHz").toRawUTF8()),
                                     partLabel(part, (label + "Rate").toRawUTF8()),
                                     ParameterScope::part,
                                     0.01f,
                                     40.0f,
                                     1.0f,
                                     "Hz"));
            specs.push_back(choiceSpec(partId(part, (suffix + "rateSync").toRawUTF8()),
                                       partPath(part, (path + "rateSync").toRawUTF8()),
                                       partLabel(part, (label + "Division").toRawUTF8()),
                                       ParameterScope::part,
                                       lfoSync,
                                       6));
            specs.push_back(floatSpec(partId(part, (suffix + "freeVar").toRawUTF8()),
                                     partPath(part, (path + "freeVariation").toRawUTF8()),
                                     partLabel(part, (label + "Free Variation").toRawUTF8()),
                                     ParameterScope::part,
                                     0.0f,
                                     1.0f,
                                     0.0f,
                                     "linear"));
        }

        specs.push_back(floatSpec(partId(part, "l1.toCutoff"),
                                  partPath(part, "lfo/1/toCutoff"),
                                  partLabel(part, "LFO 1 to Cutoff"),
                                  ParameterScope::part,
                                  -48.0f,
                                  48.0f,
                                  0.0f,
                                  "st"));
        specs.push_back(floatSpec(partId(part, "l2.toPitch"),
                                  partPath(part, "lfo/2/toPitch"),
                                  partLabel(part, "LFO 2 to Pitch"),
                                  ParameterScope::part,
                                  -12.0f,
                                  12.0f,
                                  0.0f,
                                  "st"));
    }

    specs.push_back(floatSpec("xm.master.volume",
                              "master/volume",
                              "Master Volume",
                              ParameterScope::global,
                              0.0f,
                              1.0f,
                              0.8f,
                              "linear"));

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
            juce::NormalisableRange<float> range(spec.minimum, spec.maximum);
            if (spec.skewCentre > spec.minimum && spec.skewCentre < spec.maximum)
                range.setSkewForCentre(spec.skewCentre);
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                id,
                spec.displayName,
                range,
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
