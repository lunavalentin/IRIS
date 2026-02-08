#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV2.h"

class RoomMapComponent  : public juce::Component,
                          public juce::FileDragAndDropTarget
{
public:
    RoomMapComponent(IrisVSTV2AudioProcessor&);
    ~RoomMapComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Drag and Drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    IrisVSTV2AudioProcessor& audioProcessor;
    juce::Uuid draggingId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomMapComponent)
};
