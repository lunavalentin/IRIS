#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV2.h"

// Custom small icon button
class IconButton : public juce::Button
{
public:
    enum IconType { Lock, Delete };
    
    IconButton(const juce::String& name, IconType t) : juce::Button(name), type(t) {}
    
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsMouseOver, bool shouldDrawButtonAsDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        
        // Background
        if (shouldDrawButtonAsDown) g.fillAll(juce::Colours::white.withAlpha(0.2f));
        else if (shouldDrawButtonAsMouseOver) g.fillAll(juce::Colours::white.withAlpha(0.1f));
        
        g.setColour(juce::Colours::white);
        if (type == Delete) g.setColour(juce::Colours::red.withSaturation(0.6f));
        if (type == Lock && getToggleState()) g.setColour(juce::Colours::orange);
        
        auto iconArea = bounds.reduced(4.0f);
        
        if (type == Lock)
        {
            juce::Path p;
            float w = iconArea.getWidth();
            float h = iconArea.getHeight();
            bool locked = getToggleState();
            
            // Body
            p.addRectangle(iconArea.getX(), iconArea.getY() + h*0.35f, w, h*0.65f);
            
            // Shackle
            juce::Path shackle;
            float shackleW = w * 0.6f;
            float shackleH = h * 0.45f;
            float shackleX = iconArea.getX() + (w - shackleW) * 0.5f;
            
            if (locked)
                shackle.addRoundedRectangle(shackleX, iconArea.getY(), shackleW, shackleH*2.0f, shackleW*0.5f);
            else {
                shackle.addRoundedRectangle(shackleX + shackleW*0.7f, iconArea.getY() - h*0.1f, shackleW, shackleH*2.0f, shackleW*0.5f);
            }
            
            // Subtract inner
            // Just draw stroke?
            g.strokePath(shackle, juce::PathStrokeType(2.0f));
            g.fillRect(iconArea.getX(), iconArea.getY() + h*0.35f, w, h*0.65f);
        }
        else if (type == Delete)
        {
            // Trash can
            juce::Path p;
            float w = iconArea.getWidth();
            float h = iconArea.getHeight();
            
            // Lid
            g.drawRect(iconArea.getX(), iconArea.getY(), w, h*0.15f, 1.5f);
            g.drawRect(iconArea.getX() + w*0.3f, iconArea.getY()-2, w*0.4f, 2.0f, 1.5f); // Handle
            
            // Bin
            g.drawRect(iconArea.getX() + w*0.15f, iconArea.getY() + h*0.25f, w*0.7f, h*0.75f, 1.5f);
            
            // Stripes
            g.drawLine(iconArea.getX() + w*0.35f, iconArea.getY() + h*0.4f, iconArea.getX() + w*0.35f, iconArea.getY() + h*0.8f, 1.5f);
            g.drawLine(iconArea.getX() + w*0.65f, iconArea.getY() + h*0.4f, iconArea.getX() + w*0.65f, iconArea.getY() + h*0.8f, 1.5f);
        }
    }
    
private:
    IconType type;
};

// A single row in the list
class IRListItem : public juce::Component, public juce::Button::Listener, public juce::TextEditor::Listener, public juce::ComboBox::Listener
{
public:
    IRListItem(IrisVSTV2AudioProcessor& p, juce::Uuid id);
    ~IRListItem() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // GUI callbacks
    void buttonClicked(juce::Button* b) override;
    void textEditorReturnKeyPressed(juce::TextEditor& ed) override;
    void textEditorFocusLost(juce::TextEditor& ed) override;

    void updateFromModel(); // Pulls data from processor

    juce::Uuid pointId;

private:
    IrisVSTV2AudioProcessor& processor;
    
    juce::Label nameLabel;
    juce::TextEditor xEditor;
    juce::TextEditor yEditor;
    IconButton lockButton { "Lock", IconButton::Lock };
    IconButton deleteButton { "Delete", IconButton::Delete };
    juce::ComboBox channelSelector;
    void comboBoxChanged(juce::ComboBox*) override;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRListItem)
};

// The main list container
class IRListComponent : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    IRListComponent(IrisVSTV2AudioProcessor& p);
    ~IRListComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    
    void updateContent(); // Rebuilds list if needed

    // Drag & Drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    IrisVSTV2AudioProcessor& processor;
    
    juce::Viewport viewport;
    juce::Component contentContainer;
    
    std::vector<std::unique_ptr<IRListItem>> items;
    
    juce::TextButton addIRButton { "+ IR" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    void addIRClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRListComponent)
};
