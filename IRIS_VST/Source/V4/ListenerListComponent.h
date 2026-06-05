#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "IconButton.h"

// Individual list item for a listener
class ListenerListItem : public juce::Component,
                           public juce::Button::Listener,
                           public juce::TextEditor::Listener,
                           public juce::Label::Listener
{
public:
    ListenerListItem(IrisAudioProcessor& p, juce::Uuid id, bool local);
    ~ListenerListItem() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void updateFromModel();
    void mouseDown(const juce::MouseEvent& e) override;
    
    void buttonClicked(juce::Button* b) override;
    void textEditorReturnKeyPressed(juce::TextEditor& ed) override;
    void textEditorFocusLost(juce::TextEditor& ed) override;
    void labelTextChanged(juce::Label* labelThatHasChanged) override;

    juce::Uuid listenerId;
    bool isLocalList;

private:
    IrisAudioProcessor& processor;
    
    juce::Label nameLabel;
    juce::TextEditor xEditor;
    juce::TextEditor yEditor;
    IconButton linkToggle { "Link", IconButton::Link };
    IconButton lockToggle { "Lock", IconButton::Lock };
};


class ListenerLinkMatrixComponent : public juce::Component, public juce::Timer
{
public:
    ListenerLinkMatrixComponent(IrisAudioProcessor& p);
    ~ListenerLinkMatrixComponent() override;
    
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    IrisAudioProcessor& processor;
    
    struct Cell {
        juce::Uuid rId;
        juce::Uuid cId;
        std::unique_ptr<juce::ToggleButton> btn;
    };
    std::vector<Cell> cells;
    std::vector<juce::Uuid> sortedIds;
    
    void rebuildMatrix();
    void updateButtons();
    
    juce::TooltipWindow tooltipWindow { this, 700 };
};

// Containment list component
class ListenerListComponent : public juce::Component,
                                public juce::Timer
{
public:
    ListenerListComponent(IrisAudioProcessor& p);
    ~ListenerListComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    IrisAudioProcessor& processor;
    
    juce::Viewport viewport;
    juce::Component contentContainer;
    std::vector<std::unique_ptr<ListenerListItem>> items;
    
    void updateContent();
};