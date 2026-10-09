#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "RoomMapComponent.h"
#include "ControlPanelComponent.h"
#include "IRListComponent.h"
#include "WallListComponent.h"
#include "ListenerListComponent.h"
#include "IrisLookAndFeel.h"

// Draws the active-IR weights and warnings over the room map. A separate,
// click-through layer so the 25 Hz refresh repaints only this area instead of
// the whole editor (sliders, lists).
class WeightOverlayComponent : public juce::Component
{
public:
    explicit WeightOverlayComponent (IrisAudioProcessor& p) : audioProcessor (p)
    {
        setInterceptsMouseClicks (false, false);
    }
    void paint (juce::Graphics&) override;

private:
    IrisAudioProcessor& audioProcessor;
};

class IrisAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    IrisAudioProcessorEditor (IrisAudioProcessor&);
    ~IrisAudioProcessorEditor() override;

    void paint          (juce::Graphics&) override;
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
    WeightOverlayComponent overlay;

    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrisAudioProcessorEditor)
};
