#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <vector>

namespace xenmilenium
{

enum class ParameterKind
{
    boolean,
    choice
};

enum class ParameterScope
{
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
