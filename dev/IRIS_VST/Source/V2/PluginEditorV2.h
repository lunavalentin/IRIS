#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV2.h"
#include "RoomMapComponentV2.h"
#include "ControlPanelComponentV2.h"
#include "IRListComponentV2.h"

class IrisVSTV2AudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    IrisVSTV2AudioProcessorEditor (IrisVSTV2AudioProcessor&);
    ~IrisVSTV2AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    
    // Callback from processor
    void updateUI();

private:
    IrisVSTV2AudioProcessor& audioProcessor;
    
    RoomMapComponent roomMap;
    ControlPanelComponent controlPanel;
    IRListComponent irList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrisVSTV2AudioProcessorEditor)
};
