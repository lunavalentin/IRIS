#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV3.h"

#include "IconButtonV3.h"

// A single row in the list
class IRListItemV3 : public juce::Component, public juce::Button::Listener, public juce::TextEditor::Listener, public juce::ComboBox::Listener
{
public:
    IRListItemV3(IrisVSTV3AudioProcessor& p, juce::Uuid id);
    ~IRListItemV3() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // GUI callbacks
    void buttonClicked(juce::Button* b) override;
    void textEditorReturnKeyPressed(juce::TextEditor& ed) override;
    void textEditorFocusLost(juce::TextEditor& ed) override;

    void updateFromModel(); // Pulls data from processor

    juce::Uuid pointId;

private:
    IrisVSTV3AudioProcessor& processor;
    
    juce::Label nameLabel;
    juce::TextEditor xEditor;
    juce::TextEditor yEditor;
    IconButtonV3 lockButton { "Lock", IconButtonV3::Lock };
    IconButtonV3 deleteButton { "Delete", IconButtonV3::Delete };
    juce::ComboBox channelSelector;
    void comboBoxChanged(juce::ComboBox*) override;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRListItemV3)
};

// The main list container
class IRListComponentV3 : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    IRListComponentV3(IrisVSTV3AudioProcessor& p);
    ~IRListComponentV3() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    
    void updateContent(); // Rebuilds list if needed

    // Drag & Drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    IrisVSTV3AudioProcessor& processor;
    
    juce::Viewport viewport;
    juce::Component contentContainer;
    
    std::vector<std::unique_ptr<IRListItemV3>> items;
    
    juce::TextButton addIRButton { "+ IR" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    void addIRClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRListComponentV3)
};
