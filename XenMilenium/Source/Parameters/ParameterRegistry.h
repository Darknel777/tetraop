#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <vector>

namespace xenmilenium
{

enum class ParameterKind
{
    boolean,
    choice,
    floatValue
};

enum class ParameterScope
{
    global,
    part,
    oscillator,
    effect
};

struct ParameterSpec
{
    juce::String apvtsId;
    juce::String statePath;
    juce::String displayName;
    ParameterKind kind = ParameterKind::boolean;
    ParameterScope scope = ParameterScope::part;
    bool defaultBool = false;
    juce::StringArray choices;
    int defaultChoice = 0;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultFloat = 0.0f;
    float skewCentre = 0.0f;
    juce::String unit;
};

class ParameterRegistry
{
public:
    static const std::vector<ParameterSpec>& all();
    static const ParameterSpec* findByStatePath(const juce::String& statePath);
    static const ParameterSpec* findByApvtsId(const juce::String& apvtsId);
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static juce::String plainChoice(const ParameterSpec& spec);
    static int indexOfChoice(const ParameterSpec& spec, const juce::String& plain);

private:
    ParameterRegistry() = delete;
};

} // namespace xenmilenium
