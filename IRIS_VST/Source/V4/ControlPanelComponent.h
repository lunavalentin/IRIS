#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// Simple table model


class ControlPanelComponent  : public juce::Component,
                               public juce::Button::Listener
{
public:
    ControlPanelComponent(IrisAudioProcessor&);
    ~ControlPanelComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    void buttonClicked (juce::Button* button) override;
    
    void update(); // Manual update for non-parameter things if needed
    

private:
    IrisAudioProcessor& audioProcessor;
    
    // --- Row 1 ---
    juce::TextButton addIRButton { "+ IR" };
    
    juce::Label mixLabel;
    juce::Slider mixSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    
    juce::TextButton loadLayoutButton { "Load Layout" };
    juce::TextButton broadcastButton { "Broadcast..." };
    
    // --- Row 2 ---
    juce::TextButton addWallButton { "+ Wall" };
    
    juce::Label wallOpacityLabel;
    juce::Slider wallOpacitySlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wallOpacityAttachment;
    
    juce::TextButton saveLayoutButton { "Save Layout" };
    
    // --- Row 2b: Output Gain ---
    juce::Label outputGainLabel;
    juce::Slider outputGainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;

    // --- Row 3 ---
    juce::ToggleButton freezeButton { "Freeze" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    
    juce::ToggleButton normalizeButton { "Normalize" };
    juce::ToggleButton alignButton { "Align" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> normalizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> alignAttachment;
    
    juce::Label inertiaLabel;
    juce::Slider inertiaSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inertiaAttachment;
    
    juce::Label spreadLabel;
    juce::Slider spreadSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spreadAttachment;
    
    // Helpers
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanelComponent)
};
