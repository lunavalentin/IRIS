#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class RoomMapComponent : public juce::Component,
                          public juce::FileDragAndDropTarget
{
public:
    RoomMapComponent(IrisAudioProcessor&);
    ~RoomMapComponent() override;

    void paint   (juce::Graphics&) override;
    void resized ()                override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;

private:
    IrisAudioProcessor& audioProcessor;

    juce::Uuid draggingId;
    bool       isDraggingWall     = false;
    bool       isDraggingListener = false;

    float dragStartMouseX = 0.0f;
    float dragStartMouseY = 0.0f;
    float dragStartObjX   = 0.0f;
    float dragStartObjY   = 0.0f;
    float dragStartWall[4]{};
    int   dragHandle      = 0;   // 0 = move, 1 = resize (P1), 2 = rotate (P2)

    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoomMapComponent)
};
