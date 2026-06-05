#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "RoomMapComponent.h"
#include "ControlPanelComponent.h"
#include "IRListComponent.h"
#include "WallListComponent.h"
#include "ListenerListComponent.h"
#include "IrisLookAndFeel.h"

class IrisAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    IrisAudioProcessorEditor (IrisAudioProcessor&);
    ~IrisAudioProcessorEditor() override;

    void paint          (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized        () override;

    // Triggered by structural changes (add/remove IR, wall, listener).
    void updateUI();

private:
    // 25Hz poll: repaints overlay and room map if weights/positions changed.
    void timerCallback() override;

    IrisAudioProcessor& audioProcessor;
    IrisLookAndFeel irisLookAndFeel;

    RoomMapComponent      roomMap;
    ControlPanelComponent controlPanel;
    ListenerListComponent listenerList;
    IRListComponent       irList;
    WallListComponent     wallList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrisAudioProcessorEditor)
};
