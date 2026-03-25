#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV4.h"

#include "IconButtonV4.h"

// A single row in the list
class IRListItemV4 : public juce::Component, public juce::Button::Listener, public juce::TextEditor::Listener, public juce::ComboBox::Listener
{
public:
    IRListItemV4(IrisVSTV4AudioProcessor& p, juce::Uuid id);
    ~IRListItemV4() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // GUI callbacks
    void buttonClicked(juce::Button* b) override;
    void textEditorReturnKeyPressed(juce::TextEditor& ed) override;
    void textEditorFocusLost(juce::TextEditor& ed) override;

    void updateFromModel(); // Pulls data from processor

    juce::Uuid pointId;

private:
    IrisVSTV4AudioProcessor& processor;
    
    juce::Label nameLabel;
    juce::TextEditor xEditor;
    juce::TextEditor yEditor;
    IconButtonV4 lockButton { "Lock", IconButtonV4::Lock };
    IconButtonV4 deleteButton { "Delete", IconButtonV4::Delete };
    juce::ComboBox channelSelector;
    void comboBoxChanged(juce::ComboBox*) override;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRListItemV4)
};

// The main list container
class IRListComponentV4 : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    IRListComponentV4(IrisVSTV4AudioProcessor& p);
    ~IRListComponentV4() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    
    void updateContent(); // Rebuilds list if needed

    // Drag & Drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    IrisVSTV4AudioProcessor& processor;
    
    juce::Viewport viewport;
    juce::Component contentContainer;
    
    std::unique_ptr<IRListItemV4> listenerItem; // (Maybe unused if listener is manual)
    std::vector<std::unique_ptr<IRListItemV4>> items;
    
    // Header Components
    juce::Label titleLabel; 
    juce::TextEditor listenerXEditor;
    juce::TextEditor listenerYEditor;
    
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRListComponentV4)
};
