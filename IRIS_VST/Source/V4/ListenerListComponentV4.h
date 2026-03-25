#pragma once
#include <JuceHeader.h>
#include "PluginProcessorV4.h"
#include "IconButtonV4.h"

// Individual list item for a listener
class ListenerListItemV4 : public juce::Component,
                           public juce::Button::Listener,
                           public juce::TextEditor::Listener
{
public:
    ListenerListItemV4(IrisVSTV4AudioProcessor& p, juce::Uuid id, bool local);
    ~ListenerListItemV4() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void updateFromModel();
    void mouseDown(const juce::MouseEvent& e) override;
    
    void buttonClicked(juce::Button* b) override;
    void textEditorReturnKeyPressed(juce::TextEditor& ed) override;
    void textEditorFocusLost(juce::TextEditor& ed) override;

    juce::Uuid listenerId;
    bool isLocalList;

private:
    IrisVSTV4AudioProcessor& processor;
    
    juce::Label nameLabel;
    juce::TextEditor xEditor;
    juce::TextEditor yEditor;
    IconButtonV4 linkToggle { "Link", IconButtonV4::Link };
    IconButtonV4 lockToggle { "Lock", IconButtonV4::Lock };
};


class ListenerLinkMatrixComponentV4 : public juce::Component, public juce::Timer
{
public:
    ListenerLinkMatrixComponentV4(IrisVSTV4AudioProcessor& p);
    ~ListenerLinkMatrixComponentV4() override;
    
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    IrisVSTV4AudioProcessor& processor;
    
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
class ListenerListComponentV4 : public juce::Component,
                                public juce::Timer
{
public:
    ListenerListComponentV4(IrisVSTV4AudioProcessor& p);
    ~ListenerListComponentV4() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    IrisVSTV4AudioProcessor& processor;
    
    juce::Viewport viewport;
    juce::Component contentContainer;
    std::vector<std::unique_ptr<ListenerListItemV4>> items;
    
    void updateContent();
};