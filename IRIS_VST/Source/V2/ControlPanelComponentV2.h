#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV2.h"

// Simple table model


class ControlPanelComponent  : public juce::Component,
                               public juce::Button::Listener
{
public:
    ControlPanelComponent(IrisVSTV2AudioProcessor&);
    ~ControlPanelComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    void buttonClicked (juce::Button* button) override; // Added override
    


private:
    IrisVSTV2AudioProcessor& audioProcessor;
    juce::TextButton addIRButton { "+ IR" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    juce::Label inertiaLabel;
    juce::Slider inertiaSlider;
    juce::ToggleButton freezeButton;
    
    juce::TextButton loadLayoutButton { "Load Layout" };
    
    juce::Label spreadLabel;
    juce::Slider spreadSlider;
    
    juce::Label mixLabel;
    juce::Slider mixSlider;
    
    // We don't have APVTS so no smart attachments easily.
    // Basic interaction via callbacks in constructor.

    // Layout Labels
    juce::Label dryLabel;
    juce::Label wetLabel;
    
    juce::Label freezeLabel;
    
    // inertiaLabel exists above
    juce::Label inertiaPlusLabel;
    juce::Label inertiaMinusLabel;
    
    // spreadLabel exists above
    juce::Label spreadPlusLabel;
    juce::Label spreadMinusLabel;

    // List removed
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanelComponent)
};
