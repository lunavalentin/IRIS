#include "WallListComponentV3.h"

// ==============================================================================
// WallListItemV3
// ==============================================================================

WallListItemV3::WallListItemV3(IrisVSTV3AudioProcessor& p, juce::Uuid id)
    : processor(p), wallId(id)
{
    addAndMakeVisible(nameEditor);
    nameEditor.addListener(this);
    nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    nameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    
    addAndMakeVisible(lockButton);
    lockButton.addListener(this);
    lockButton.setClickingTogglesState(true);
    
    addAndMakeVisible(deleteButton);
    deleteButton.addListener(this);
    
    updateFromModel();
}

WallListItemV3::~WallListItemV3()
{
}

void WallListItemV3::resized()
{
    auto area = getLocalBounds().reduced(2);
    
    // Layout: [Lock] [Name .........] [Delete]
    lockButton.setBounds(area.removeFromLeft(20));
    area.removeFromLeft(5);
    deleteButton.setBounds(area.removeFromRight(20));
    area.removeFromRight(5);
    
    nameEditor.setBounds(area);
}

void WallListItemV3::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::white.withAlpha(0.05f));
    
    if (processor.selectedWallId == wallId)
    {
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawRect(getLocalBounds(), 1);
    }
}

void WallListItemV3::buttonClicked(juce::Button* b)
{
    if (b == &deleteButton)
    {
        processor.removeWall(wallId);
    }
    else if (b == &lockButton)
    {
        // Find and toggle
        for (auto& w : processor.walls) {
            if (w.id == wallId) {
                w.locked = lockButton.getToggleState();
                if (processor.onStateChanged) processor.onStateChanged();
                break;
            }
        }
    }
}

void WallListItemV3::textEditorReturnKeyPressed(juce::TextEditor& ed)
{
    ed.giveAwayKeyboardFocus();
    
    // Update Name
    for (auto& w : processor.walls) {
        if (w.id == wallId) {
            w.name = ed.getText();
            break;
        }
    }
}

void WallListItemV3::textEditorFocusLost(juce::TextEditor& ed)
{
    textEditorReturnKeyPressed(ed);
}

void WallListItemV3::updateFromModel()
{
    for (const auto& w : processor.walls)
    {
        if (w.id == wallId)
        {
            if (!nameEditor.hasKeyboardFocus(true))
                nameEditor.setText(w.name, juce::dontSendNotification);
            
            lockButton.setToggleState(w.locked, juce::dontSendNotification);
            break;
        }
    }
}

// ==============================================================================
// WallListComponentV3
// ==============================================================================

WallListComponentV3::WallListComponentV3(IrisVSTV3AudioProcessor& p)
    : processor(p)
{
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Walls", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentContainer, false);
    updateContent();
}

WallListComponentV3::~WallListComponentV3()
{
}

void WallListComponentV3::resized()
{
    auto area = getLocalBounds();
    titleLabel.setBounds(area.removeFromTop(24));
    viewport.setBounds(area);
    
    contentContainer.setBounds(0, 0, viewport.getWidth() - 15, items.size() * 35);
    
    for (int i=0; i<items.size(); ++i)
    {
         items[i]->setBounds(0, i*35, contentContainer.getWidth(), 30);
    }
}

void WallListComponentV3::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.brighter(0.05f)); // Slightly lighter than bg
}

void WallListComponentV3::updateContent()
{
    // Check if walls changed
    bool structureChanged = false;
    if (items.size() != processor.walls.size()) structureChanged = true;
    else {
        // Check IDs
        for (int i=0; i<items.size(); ++i) {
             if (items[i]->wallId != processor.walls[i].id) {
                 structureChanged = true;
                 break;
             }
        }
    }
    
    if (structureChanged)
    {
        items.clear();
        contentContainer.deleteAllChildren();
        
        for (const auto& w : processor.walls)
        {
            auto item = std::make_unique<WallListItemV3>(processor, w.id);
            contentContainer.addAndMakeVisible(item.get());
            items.push_back(std::move(item));
        }
        resized(); // Recalculate layout
    }
    else
    {
        // Just update values
        for (auto& item : items) item->updateFromModel();
        // Repaint for selection highlight
        contentContainer.repaint();
    }
}
