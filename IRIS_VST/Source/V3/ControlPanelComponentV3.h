#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV3.h"

// Simple table model


class ControlPanelComponentV3  : public juce::Component,
                               public juce::Button::Listener
{
public:
    ControlPanelComponentV3(IrisVSTV3AudioProcessor&);
    ~ControlPanelComponentV3() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    void buttonClicked (juce::Button* button) override; // Added override
    
    void update();
    

private:
    IrisVSTV3AudioProcessor& audioProcessor;
    juce::TextButton addIRButton { "+ IR" };
    juce::TextButton addWallButton { "+ Wall" };
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
    
    // Wall Opacity
    juce::Label wallOpacityLabel;
    juce::Slider wallOpacitySlider;
    
    // Wall Props - REMOVED (Moved to WallListComponent)
    // juce::GroupComponent wallPropsGroup ...
    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanelComponentV3)
};
