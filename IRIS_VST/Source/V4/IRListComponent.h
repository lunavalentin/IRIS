#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include "IconButton.h"

// A single row in the list
class IRListItem : public juce::Component, public juce::Button::Listener, public juce::TextEditor::Listener, public juce::Label::Listener
{
public:
    IRListItem(IrisAudioProcessor& p, juce::Uuid id);
    ~IRListItem() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void buttonClicked(juce::Button* b) override;
    void textEditorReturnKeyPressed(juce::TextEditor& ed) override;
    void textEditorFocusLost(juce::TextEditor& ed) override;
    void labelTextChanged(juce::Label* labelThatHasChanged) override;

    void updateFromModel();

    juce::Uuid pointId;

private:
    IrisAudioProcessor& processor;

    // Cached compatibility state — set in updateFromModel(), read in paint().
    // Avoids an O(N) scan of processor.points on every paint call.
    bool isIncompatible = false;

    juce::Label      nameLabel;
    juce::TextEditor xEditor;
    juce::TextEditor yEditor;
    IconButton lockButton   { "Lock",   IconButton::Lock   };
    IconButton deleteButton { "Delete", IconButton::Delete };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRListItem)
};

// The main list container
class IRListComponent : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    IRListComponent(IrisAudioProcessor& p);
    ~IRListComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    
    void updateContent(); // Rebuilds list if needed

    // Drag & Drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    IrisAudioProcessor& processor;
    
    juce::Viewport viewport;
    juce::Component contentContainer;
    
    std::unique_ptr<IRListItem> listenerItem; // (Maybe unused if listener is manual)
    std::vector<std::unique_ptr<IRListItem>> items;
    
    // Header Components
    juce::Label titleLabel; 
    juce::TextEditor listenerXEditor;
    juce::TextEditor listenerYEditor;
    
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRListComponent)
};
