#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV4.h"

// Simple table model


class ControlPanelComponentV4  : public juce::Component,
                               public juce::Button::Listener
{
public:
    ControlPanelComponentV4(IrisVSTV4AudioProcessor&);
    ~ControlPanelComponentV4() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    void buttonClicked (juce::Button* button) override;
    
    void update(); // Manual update for non-parameter things if needed
    

private:
    IrisVSTV4AudioProcessor& audioProcessor;
    
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
    
    // --- Row 3 ---
    juce::ToggleButton freezeButton { "Freeze" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    
    juce::Label inertiaLabel;
    juce::Slider inertiaSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inertiaAttachment;
    
    juce::Label spreadLabel;
    juce::Slider spreadSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spreadAttachment;
    
    // Helpers
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanelComponentV4)
};
