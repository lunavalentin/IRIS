#include "WallListComponentV3.h"
#include <cmath>

// ==============================================================================
// WallListItemV3
// ==============================================================================

WallListItemV3::WallListItemV3(IrisVSTV3AudioProcessor& p, juce::Uuid id)
    : processor(p), wallId(id)
{
    addAndMakeVisible(lockButton);
    lockButton.addListener(this);
    lockButton.setClickingTogglesState(true);
    
    addAndMakeVisible(deleteButton);
    deleteButton.addListener(this);
    
    addAndMakeVisible(nameEditor);
    nameEditor.addListener(this);
    nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    nameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    nameEditor.setText("Wall");
    
    // Boxes
    addAndMakeVisible(lengthEditor);
    lengthEditor.addListener(this);
    lengthEditor.setJustification(juce::Justification::centred);
    lengthEditor.setTooltip("Length");
    
    addAndMakeVisible(angleEditor);
    angleEditor.addListener(this);
    angleEditor.setJustification(juce::Justification::centred);
    angleEditor.setTooltip("Angle (Deg)");
}

WallListItemV3::~WallListItemV3()
{
}

void WallListItemV3::resized()
{
    auto area = getLocalBounds().reduced(2);
    
    // Layout: Lock(24) | Remove(24) | Name(Flex) | Length(40) | Unit(10) | Angle(40) | Unit(10)
    
    lockButton.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(2);
    
    deleteButton.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(5);
    
    // Right side Boxes (Total 100)
    auto right = area.removeFromRight(100);
    
    // Length
    lengthEditor.setBounds(right.removeFromLeft(40).reduced(1));
    right.removeFromLeft(10); // Space for 'm'
    
    // Angle
    angleEditor.setBounds(right.removeFromLeft(40).reduced(1));
     // Remaining space for 'deg'
    
    // Name
    nameEditor.setBounds(area.reduced(2));
}

void WallListItemV3::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black); 
    
    // Separator
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillRect(0, getHeight()-1, getWidth(), 1);
    
    if (processor.selectedWallId == wallId) {
        g.setColour(juce::Colours::cyan.withAlpha(0.6f));
        g.drawRect(getLocalBounds(), 1.0f);
    }
    
    // Draw Units next to boxes
    g.setColour(juce::Colours::grey);
    g.setFont(juce::Font("Inter", 10.0f, juce::Font::plain));
    
    // Units are drawn in the gaps we left in resized()
    // Length is at lengthEditor.getRight()
    if (lengthEditor.isVisible())
        g.drawText("m", lengthEditor.getRight(), lengthEditor.getY(), 10, lengthEditor.getHeight(), juce::Justification::centredLeft);
        
    if (angleEditor.isVisible())
        g.drawText(juce::CharPointer_UTF8("\xc2\xb0"), angleEditor.getRight(), angleEditor.getY(), 10, angleEditor.getHeight(), juce::Justification::centredLeft);
}

// ... WallListComponent ...

// (Duplicate paint removed)

void WallListItemV3::buttonClicked(juce::Button* b)
{
    if (b == &deleteButton)
    {
        processor.removeWall(wallId);
    }
    else if (b == &lockButton)
    {
        // lock logic
        juce::ScopedLock sl(processor.stateLock);
        for(auto& w : processor.walls) {
            if(w.id == wallId) { w.locked = lockButton.getToggleState(); break; }
        }
        if(processor.onStateChanged) processor.onStateChanged();
    }
}

void WallListItemV3::textEditorReturnKeyPressed(juce::TextEditor& ed)
{
    textEditorFocusLost(ed);
}

void WallListItemV3::textEditorFocusLost(juce::TextEditor& ed)
{
    juce::ScopedLock sl(processor.stateLock);
    for(auto& w : processor.walls) {
        if(w.id == wallId && !w.locked) { // Don't edit if locked
            if (&ed == &nameEditor) {
                w.name = nameEditor.getText();
            }
            else {
                // Geometry Update
                // We pivot around Center for consistency?
                float cx = (w.x1 + w.x2) * 0.5f;
                float cy = (w.y1 + w.y2) * 0.5f;
                float dx = w.x2 - w.x1;
                float dy = w.y2 - w.y1;
                
                float currentLen = std::sqrt(dx*dx + dy*dy);
                float currentAng = std::atan2(dy, dx);
                
                float newLen = currentLen;
                float newAng = currentAng;
                
                if (&ed == &lengthEditor) {
                     float val = ed.getText().getFloatValue();
                     if (val < 0.01f) val = 0.01f;
                     newLen = val;
                }
                else if (&ed == &angleEditor) {
                    float val = ed.getText().getFloatValue();
                     newAng = juce::degreesToRadians(val);
                }
                
                float halfX = 0.5f * newLen * std::cos(newAng);
                float halfY = 0.5f * newLen * std::sin(newAng);
                
                w.x1 = cx - halfX; w.x2 = cx + halfX;
                w.y1 = cy - halfY; w.y2 = cy + halfY;
                
                processor.updateWeightsGaussian();
            }
            if(processor.onStateChanged) processor.onStateChanged();
            break;
        }
    }
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
            
            // Calculate size/angle
            float dx = w.x2 - w.x1;
            float dy = w.y2 - w.y1;
            float len = std::sqrt(dx*dx + dy*dy);
            float ang = juce::radiansToDegrees(std::atan2(dy, dx));
            
            if (!lengthEditor.hasKeyboardFocus(true))
                lengthEditor.setText(juce::String(len, 3), juce::dontSendNotification);
                
            if (!angleEditor.hasKeyboardFocus(true))
                angleEditor.setText(juce::String(ang, 1), juce::dontSendNotification);
            
            // Visual feedback
            if (w.locked) nameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::orange);
            else nameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
            
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
    
    // Button moved back to Control Panel
    
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
    
    // Header
    titleLabel.setBounds(area.removeFromTop(30).reduced(5));
    
    if (items.empty())
    {
        viewport.setBounds(area); // Still set bounds so it exists, but it's empty
        contentContainer.setBounds(0,0,0,0);
        return;
    }
    
    viewport.setBounds(area);
    
    // Fix: Explicitly set height based on items
    int contentHeight = items.size() * 35;
    contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), contentHeight);
    
    for (int i=0; i<items.size(); ++i)
    {
         items[i]->setBounds(0, i*35, contentContainer.getWidth(), 30);
    }
}

void WallListComponentV3::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.brighter(0.05f)); // Slightly lighter than bg
    
    if (items.empty())
    {
        g.setColour(juce::Colours::grey);
        g.setFont(14.0f);
        g.drawText("No walls active", getLocalBounds().removeFromBottom(getHeight() - 24), juce::Justification::centred);
    }
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
        // Fix: Use removeAllChildren, not deleteAllChildren (items vector owns them)
        contentContainer.removeAllChildren();
        
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
