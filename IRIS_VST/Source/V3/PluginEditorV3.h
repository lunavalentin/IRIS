#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV3.h"
#include "RoomMapComponentV3.h"
#include "ControlPanelComponentV3.h"
#include "IRListComponentV3.h"
#include "WallListComponentV3.h"
#include "WallListComponentV3.h"

class IrisVSTV3AudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    IrisVSTV3AudioProcessorEditor (IrisVSTV3AudioProcessor&);
    ~IrisVSTV3AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    
    // Callback from processor
    void updateUI();

private:
    IrisVSTV3AudioProcessor& audioProcessor;
    
    RoomMapComponentV3 roomMap;
    ControlPanelComponentV3 controlPanel;
    IRListComponentV3 irList;
    WallListComponentV3 wallList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrisVSTV3AudioProcessorEditor)
};
