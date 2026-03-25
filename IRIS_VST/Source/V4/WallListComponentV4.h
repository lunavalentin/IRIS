#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV4.h"
#include "IconButtonV4.h"

class WallListItemV4 : public juce::Component, public juce::Button::Listener, public juce::TextEditor::Listener
{
public:
    WallListItemV4(IrisVSTV4AudioProcessor& p, juce::Uuid id);
    ~WallListItemV4() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void buttonClicked(juce::Button* b) override;
    void textEditorReturnKeyPressed(juce::TextEditor& ed) override;
    void textEditorFocusLost(juce::TextEditor& ed) override;

    void updateFromModel();

    juce::Uuid wallId;

private:
    IrisVSTV4AudioProcessor& processor;
    
    juce::TextEditor nameEditor;
    IconButtonV4 lockButton { "Lock", IconButtonV4::Lock };
    IconButtonV4 deleteButton { "Remove", IconButtonV4::Delete }; // "Remove" icon per user request (was Delete) or just tooltip? User said "remove icon". Kept class generic.
    
    // Boxes for Size/Rotation
    juce::TextEditor lengthEditor;
    juce::TextEditor angleEditor;
    
    // Labels just for context? Or tooltips.
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WallListItemV4)
};

class WallListComponentV4 : public juce::Component
{
public:
    WallListComponentV4(IrisVSTV4AudioProcessor& p);
    ~WallListComponentV4() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    
    void updateContent(); 

private:
    IrisVSTV4AudioProcessor& processor;
    
    juce::Viewport viewport;
    juce::Component contentContainer;
    
    std::vector<std::unique_ptr<WallListItemV4>> items;
    
    juce::Label titleLabel;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WallListComponentV4)
};
