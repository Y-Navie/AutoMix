#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class GenreAutoMixEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit GenreAutoMixEditor (GenreAutoMixProcessor&);
    ~GenreAutoMixEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshVisibility();

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    Knob& addKnob (std::vector<std::unique_ptr<Knob>>& list, const juce::String& id, const juce::String& name);

    GenreAutoMixProcessor& proc;
    juce::LookAndFeel_V4 lnf;

    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    juce::ComboBox modeBox, roleBox, genreBox;
    std::unique_ptr<ComboAtt> modeAtt, roleAtt, genreAtt;
    juce::Label modeLabel, roleLabel, genreLabel;

    // master
    juce::Label promptLabel, promptInfo, status, meters;
    juce::TextEditor prompt, report;
    juce::TextButton learnBtn { "LEARN" }, mixBtn { "AUTO MIX" }, resetBtn { "RESET" };
    std::vector<std::unique_ptr<Knob>> masterKnobs;
    juce::String lastStatus;

    // track
    juce::Label trackInfo;
    juce::ToggleButton bypassBtn { "Bypass AutoMix" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    std::vector<std::unique_ptr<Knob>> trackKnobs;

    bool lastWasMaster = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenreAutoMixEditor)
};
