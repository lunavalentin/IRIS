#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV4.h"
#include "RoomMapComponentV4.h"
#include "ControlPanelComponentV4.h"
#include "IRListComponentV4.h"
#include "WallListComponentV4.h"
#include "WallListComponentV4.h"
#include "ListenerListComponentV4.h"
#include "IrisLookAndFeel.h"

class IrisVSTV4AudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    IrisVSTV4AudioProcessorEditor (IrisVSTV4AudioProcessor&);
    ~IrisVSTV4AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    
    // Callback from processor
    void updateUI();

private:
    IrisVSTV4AudioProcessor& audioProcessor;
    IrisLookAndFeel irisLookAndFeel;
    
    RoomMapComponentV4 roomMap;
    ControlPanelComponentV4 controlPanel;
    ListenerListComponentV4 listenerList;
    IRListComponentV4 irList;
    WallListComponentV4 wallList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrisVSTV4AudioProcessorEditor)
};
