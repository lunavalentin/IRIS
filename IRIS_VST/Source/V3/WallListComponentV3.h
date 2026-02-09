#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV3.h"
#include "IconButtonV3.h"

class WallListItemV3 : public juce::Component, public juce::Button::Listener, public juce::TextEditor::Listener
{
public:
    WallListItemV3(IrisVSTV3AudioProcessor& p, juce::Uuid id);
    ~WallListItemV3() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void buttonClicked(juce::Button* b) override;
    void textEditorReturnKeyPressed(juce::TextEditor& ed) override;
    void textEditorFocusLost(juce::TextEditor& ed) override;

    void updateFromModel();

    juce::Uuid wallId;

private:
    IrisVSTV3AudioProcessor& processor;
    
    juce::TextEditor nameEditor;
    IconButtonV3 lockButton { "Lock", IconButtonV3::Lock };
    IconButtonV3 deleteButton { "Remove", IconButtonV3::Delete }; // "Remove" icon per user request (was Delete) or just tooltip? User said "remove icon". Kept class generic.
    
    // Boxes for Size/Rotation
    juce::TextEditor lengthEditor;
    juce::TextEditor angleEditor;
    
    // Labels just for context? Or tooltips.
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WallListItemV3)
};

class WallListComponentV3 : public juce::Component
{
public:
    WallListComponentV3(IrisVSTV3AudioProcessor& p);
    ~WallListComponentV3() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    
    void updateContent(); 

private:
    IrisVSTV3AudioProcessor& processor;
    
    juce::Viewport viewport;
    juce::Component contentContainer;
    
    std::vector<std::unique_ptr<WallListItemV3>> items;
    
    juce::Label titleLabel;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WallListComponentV3)
};
