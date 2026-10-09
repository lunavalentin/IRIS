#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MidiLearnControls.h"

// Simple table model


class ControlPanelComponent  : public juce::Component,
                               public juce::Button::Listener
{
public:
    ControlPanelComponent(IrisAudioProcessor&);
    ~ControlPanelComponent() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    
    void buttonClicked (juce::Button* button) override;
    
    void update(); // Manual update for non-parameter things if needed
    

private:
    IrisAudioProcessor& audioProcessor;
    
    // --- Row 1 ---
    juce::TextButton addIRButton { "+ IR" };
    
    juce::Label mixLabel;
    MidiLearnable<juce::Slider> mixSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    
    juce::TextButton loadLayoutButton { "Load Layout" };
    juce::TextButton broadcastButton { "Broadcast..." };
    juce::TextButton midiButton { "MIDI" };

    // Controls that can be MIDI-learned (right-click), with their parameter ids.
    std::vector<std::pair<juce::Component*, juce::String>> learnables;
    template <class Control> void makeLearnable (Control& c, const juce::String& paramId);
    void showMidiSetupMenu();
    
    // --- Row 2 ---
    juce::TextButton addWallButton { "+ Wall" };
    
    juce::Label wallOpacityLabel;
    MidiLearnable<juce::Slider> wallOpacitySlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wallOpacityAttachment;
    
    juce::TextButton saveLayoutButton { "Save Layout" };
    
    // --- Row 2b: Output Gain ---
    juce::Label outputGainLabel;
    MidiLearnable<juce::Slider> outputGainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;

    // --- Row 3 ---
    MidiLearnable<juce::ToggleButton> freezeButton { "Freeze" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    
    MidiLearnable<juce::ToggleButton> normalizeButton { "Normalize" };
    MidiLearnable<juce::ToggleButton> alignButton { "Align" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> normalizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> alignAttachment;
    
    juce::Label inertiaLabel;
    MidiLearnable<juce::Slider> inertiaSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inertiaAttachment;
    
    juce::Label spreadLabel;
    MidiLearnable<juce::Slider> spreadSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spreadAttachment;
    
    // Helpers
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanelComponent)
};
