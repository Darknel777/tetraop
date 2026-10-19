#include "PluginEditor.h"

XenMileniumAudioProcessorEditor::XenMileniumAudioProcessorEditor(XenMileniumAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p)
{
    titleLabel.setText("Xen Milenium", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::Font(juce::FontOptions(22.0f)));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    scaffoldLabel.setText(juce::String::fromUTF8(
                              "Восемь партий, фильтр, MSEG и матрица модуляции.\nГрафического интерфейса синтезатора и эффектов нет."),
                          juce::dontSendNotification);
    scaffoldLabel.setJustificationType(juce::Justification::centred);
    scaffoldLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(scaffoldLabel);

    midiLabel.setJustificationType(juce::Justification::centred);
    midiLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd0d4dc));
    addAndMakeVisible(midiLabel);
    refreshMidiLabel();

    setSize(560, 180);
    startTimerHz(15);
}

XenMileniumAudioProcessorEditor::~XenMileniumAudioProcessorEditor()
{
    stopTimer();
}

void XenMileniumAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1f24));
}

void XenMileniumAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    titleLabel.setBounds(area.removeFromTop(40));
    area.removeFromTop(8);
    scaffoldLabel.setBounds(area.removeFromTop(48));
    area.removeFromTop(8);
    midiLabel.setBounds(area.removeFromTop(32));
}

void XenMileniumAudioProcessorEditor::timerCallback()
{
    refreshMidiLabel();
}

void XenMileniumAudioProcessorEditor::refreshMidiLabel()
{
    const int note = processor.getLastNoteNumber();
    const int count = processor.getNoteOnCount();

    if (note < 0)
    {
        midiLabel.setText(juce::String::fromUTF8("MIDI: note-on ещё не было"),
                          juce::dontSendNotification);
        return;
    }

    midiLabel.setText(juce::String::fromUTF8("MIDI: последняя нота ")
                          + juce::String(note)
                          + juce::String::fromUTF8(", note-on: ")
                          + juce::String(count),
                      juce::dontSendNotification);
}
