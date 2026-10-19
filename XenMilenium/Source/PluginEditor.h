#pragma once

#include "PluginProcessor.h"

class XenMileniumAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit XenMileniumAudioProcessorEditor(XenMileniumAudioProcessor&);
    ~XenMileniumAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshMidiLabel();

    XenMileniumAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Label scaffoldLabel;
    juce::Label midiLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XenMileniumAudioProcessorEditor)
};
