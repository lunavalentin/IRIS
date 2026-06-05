#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "IconButton.h"

class WallListItem : public juce::Component, public juce::Button::Listener, public juce::TextEditor::Listener
{
public:
    WallListItem(IrisAudioProcessor& p, juce::Uuid id);
    ~WallListItem() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void buttonClicked(juce::Button* b) override;
    void textEditorReturnKeyPressed(juce::TextEditor& ed) override;
    void textEditorFocusLost(juce::TextEditor& ed) override;

    void updateFromModel();

    juce::Uuid wallId;

private:
    IrisAudioProcessor& processor;
    
    juce::TextEditor nameEditor;
    IconButton lockButton { "Lock", IconButton::Lock };
    IconButton deleteButton { "Remove", IconButton::Delete }; // "Remove" icon per user request (was Delete) or just tooltip? User said "remove icon". Kept class generic.
    
    // Boxes for Size/Rotation
    juce::TextEditor lengthEditor;
    juce::TextEditor angleEditor;
    
    // Labels just for context? Or tooltips.
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WallListItem)
};

class WallListComponent : public juce::Component
{
public:
    WallListComponent(IrisAudioProcessor& p);
    ~WallListComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    
    void updateContent(); 

private:
    IrisAudioProcessor& processor;
    
    juce::Viewport viewport;
    juce::Component contentContainer;
    
    std::vector<std::unique_ptr<WallListItem>> items;
    
    juce::Label titleLabel;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WallListComponent)
};
