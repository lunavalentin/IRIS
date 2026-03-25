#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV4.h"

class RoomMapComponentV4  : public juce::Component,
                          public juce::FileDragAndDropTarget
{
public:
    RoomMapComponentV4(IrisVSTV4AudioProcessor&);
    ~RoomMapComponentV4() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Drag and Drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    IrisVSTV4AudioProcessor& audioProcessor;
    juce::Uuid draggingId;
    bool isDraggingWall = false;
    bool isDraggingListener = false;
    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;
    
    // Drag State
    float dragStartMouseX = 0.0f;
    float dragStartMouseY = 0.0f;
    float dragStartObjX = 0.0f;
    float dragStartObjY = 0.0f;
    // For Walls (x1,y1, x2,y2)
    float dragStartWall[4];
    int dragHandle = 0; // 0=Center(Move), 1=Left(Size/P1), 2=Right(Rotate/P2)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomMapComponentV4)
};
