#pragma once

#include <JuceHeader.h>

namespace xenmilenium
{

struct StoredDocument
{
    juce::var ui;
    juce::var embeddedWavetables;
    juce::var unknown;
};

StoredDocument makeInitialDocument();

juce::String serialiseDocument(const juce::AudioProcessorValueTreeState& state, const StoredDocument& document);

bool deserialiseDocument(const juce::String& json,
                         juce::AudioProcessorValueTreeState& state,
                         StoredDocument& document);

} // namespace xenmilenium
