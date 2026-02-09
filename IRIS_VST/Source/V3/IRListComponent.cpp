#include "IRListComponentV3.h"

//==============================================================================
IRListItemV3::IRListItemV3(IrisVSTV3AudioProcessor& p, juce::Uuid id)
    : processor(p), pointId(id)
{
    addAndMakeVisible(nameLabel);
    nameLabel.setJustificationType(juce::Justification::centredLeft);
    
    addAndMakeVisible(xEditor);
    xEditor.setJustification(juce::Justification::centred);
    xEditor.addListener(this);
    
    addAndMakeVisible(yEditor);
    yEditor.setJustification(juce::Justification::centred);
    yEditor.addListener(this);
    
    addAndMakeVisible(lockButton);
    lockButton.setClickingTogglesState(true); // Lock toggles
    lockButton.setTooltip("Lock Position");
    lockButton.addListener(this);
    
    addAndMakeVisible(deleteButton);
    deleteButton.setTooltip("Remove IR");
    deleteButton.addListener(this);
    
    addAndMakeVisible(channelSelector);
    channelSelector.setJustificationType(juce::Justification::centred);
    channelSelector.addListener(this);
    channelSelector.setVisible(false);
    
    updateFromModel();
}

IRListItemV3::~IRListItemV3()
{
}

void IRListItemV3::resized()
{
    auto area = getLocalBounds().reduced(2); // Global padding
    
    // Left: Icons
    // Lock
    lockButton.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(4);
    
    // Delete
    deleteButton.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(8);
    
    // Right: Controls
    // X (Rightmost)
    xEditor.setBounds(area.removeFromRight(60).reduced(2));
    area.removeFromRight(4);
    
    // Y
    yEditor.setBounds(area.removeFromRight(60).reduced(2));
    area.removeFromRight(4);
    
    // Channel Selector (if visible)
    if (channelSelector.isVisible())
    {
        channelSelector.setBounds(area.removeFromRight(60).reduced(2));
        area.removeFromRight(4);
    }
    
    // Remainder: Name
    nameLabel.setBounds(area);
}

void IRListItemV3::paint(juce::Graphics& g)
{
    // Flat background
    g.fillAll(juce::Colours::black); 
    
    // Selection Border
    if (processor.selectedIRId == pointId)
    {
        g.setColour(juce::Colours::cyan.withAlpha(0.6f));
        g.drawRect(getLocalBounds(), 1.0f);
    }
    
    // Separator line
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillRect(0, getHeight()-1, getWidth(), 1);
}

void IRListItemV3::updateFromModel()
{
    // Find point
    bool found = false;
    for (auto& p : processor.points)
    {
        if (p.id == pointId)
        {
            found = true;
            // Name: Emphasize
            nameLabel.setText(p.name, juce::dontSendNotification);
            nameLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
            nameLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            
            // Only update text if not editing to avoid fighting user
            if (!xEditor.hasKeyboardFocus(true))
                xEditor.setText(juce::String(p.x, 3), juce::dontSendNotification);
                
            if (!yEditor.hasKeyboardFocus(true))
                yEditor.setText(juce::String(p.y, 3), juce::dontSendNotification);
            
            lockButton.setToggleState(p.locked, juce::dontSendNotification);
            
            // Visual feedback for lock
            if (p.locked) nameLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
            else nameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
            
            if (p.isListener)
            {
                 deleteButton.setVisible(false);
                 channelSelector.setVisible(false);
            }
            else
            {
                 deleteButton.setVisible(true);
                 if (p.sourceChannels > 2)
                 {
                     channelSelector.setVisible(true);
                     
                     // Populate if count differs
                     if (channelSelector.getNumItems() != p.sourceChannels)
                     {
                         channelSelector.clear();
                         for (int i=0; i<p.sourceChannels; ++i)
                         {
                             channelSelector.addItem(juce::String(i+1), i+1);
                         }
                     }
                     channelSelector.setSelectedId(p.selectedChannel + 1, juce::dontSendNotification);
                 }
                 else
                 {
                     channelSelector.setVisible(false);
                 }
                 resized(); // Re-layout
            }
            break;
        }
    }
}

void IRListItemV3::buttonClicked(juce::Button* b)
{
    if (b == &lockButton)
    {
        processor.setPointLocked(pointId, lockButton.getToggleState());
    }
    else if (b == &deleteButton)
    {
        processor.removePoint(pointId);
    }
}

void IRListItemV3::comboBoxChanged(juce::ComboBox* cb)
{
    if (cb == &channelSelector)
    {
        int ch = channelSelector.getSelectedId() - 1;
        if (ch >= 0)
        {
            processor.reloadIRChannel(pointId, ch);
        }
    }
}

void IRListItemV3::textEditorReturnKeyPressed(juce::TextEditor& ed)
{
    textEditorFocusLost(ed);
}

void IRListItemV3::textEditorFocusLost(juce::TextEditor& ed)
{
    float val = ed.getText().getFloatValue();
    val = juce::jlimit(0.0f, 1.0f, val);
    
    // Find current to get other coord
    float cx = 0.5f, cy = 0.5f;
    bool found = false;
    for (auto& p : processor.points) {
        if (p.id == pointId) {
            cx = p.x; cy = p.y; found = true; break;
        }
    }
    
    if (found) {
        if (&ed == &xEditor) cx = val;
        if (&ed == &yEditor) cy = val;
        processor.updatePointPosition(pointId, cx, cy);
    }
}


//==============================================================================
IRListComponentV3::IRListComponentV3(IrisVSTV3AudioProcessor& p)
    : processor(p)
{
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentContainer, false);
    
    addAndMakeVisible(titleLabel);
    titleLabel.setText("IRs", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    // Font set in paint or resized
    
    // Listener Editors
    addAndMakeVisible(listenerXEditor);
    listenerXEditor.setJustification(juce::Justification::centred);
    listenerXEditor.setText(juce::String(processor.currentListenerX, 2));
    listenerXEditor.onReturnKey = [this] { 
        processor.currentListenerX = listenerXEditor.getText().getFloatValue(); 
        processor.updateWeightsGaussian();
        if(processor.onStateChanged) processor.onStateChanged();
    };
    
    addAndMakeVisible(listenerYEditor);
    listenerYEditor.setJustification(juce::Justification::centred);
    listenerYEditor.setText(juce::String(processor.currentListenerY, 2));
    listenerYEditor.onReturnKey = [this] {
        processor.currentListenerY = listenerYEditor.getText().getFloatValue();
        processor.updateWeightsGaussian();
        if(processor.onStateChanged) processor.onStateChanged();
    };

    updateContent();
}

IRListComponentV3::~IRListComponentV3()
{
}

void IRListComponentV3::resized()
{
    auto area = getLocalBounds().reduced(2);
    
    // Header (Title)
    titleLabel.setBounds(area.removeFromTop(20));
    
    // Listener Section
    auto listArea = area.removeFromTop(30);
    
    // Align Listener editors with the list columns
    // List Item Layout: [Lock] [Del] [Name] [Y] [X]
    // X is Rightmost(60), Y is Next(60).
    
    auto lRight = listArea.removeFromRight(60).reduced(2);
    listenerXEditor.setBounds(lRight); // X is last
    
    auto lRight2 = listArea.removeFromRight(60).reduced(2);
    listenerYEditor.setBounds(lRight2); // Y is before X
    
    // The "LISTENER" text is drawn in paint, or we can use a component?
    // We are drawing it in paint.
    
    // Spacer
    area.removeFromTop(5);
    
    // Viewport
    viewport.setBounds(area);
    int contentHeight = items.size() * 30; // 30px row
    contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), contentHeight);
    
    for (int i=0; i<items.size(); ++i)
         items[i]->setBounds(0, i*30, contentContainer.getWidth(), 30);
}

void IRListComponentV3::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.brighter(0.05f)); 
    
    // Listener Background
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    // Listener is at Y=22 roughly
    g.fillRect(0, 22, getWidth(), 30);
    
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font("Inter", 12.0f, juce::Font::bold));
    
    // "Listener" label - Align with Name column roughly
    // Name starts after Lock(24)+Del(24) = 48
    g.drawText("Listener", 55, 22, 100, 30, juce::Justification::left); 
    
    if (items.empty()) { /* ... */ }
}

void IRListComponentV3::updateContent()
{
    // 1. Handle Listener
    juce::Uuid listenerId = juce::Uuid::null();
    for (auto& p : processor.points) {
        if (p.isListener) {
            listenerId = p.id;
            break;
        }
    }
    
    if (listenerId != juce::Uuid::null())
    {
        if (!listenerItem || listenerItem->pointId != listenerId)
        {
            listenerItem = std::make_unique<IRListItemV3>(processor, listenerId);
            addAndMakeVisible(listenerItem.get());
            resized(); // Update layout to include listener
        }
        else
        {
            listenerItem->updateFromModel();
        }
    }
    
    // 2. Handle IRs (Non-Listener)
    std::vector<juce::Uuid> currentIRIds;
    for (auto& p : processor.points) {
        if (!p.isListener) currentIRIds.push_back(p.id);
    }
    
    bool structureChanged = false;
    if (items.size() != currentIRIds.size()) 
    {
        structureChanged = true;
    }
    else
    {
        for (size_t i=0; i<items.size(); ++i) {
            if (items[i]->pointId != currentIRIds[i]) {
                structureChanged = true; break;
            }
        }
    }
    
    if (structureChanged)
    {
        items.clear();
        contentContainer.removeAllChildren();
        
        int y = 0;
        int rowH = 30;
        
        for (auto id : currentIRIds)
        {
            auto item = std::make_unique<IRListItemV3>(processor, id);
            item->setBounds(0, y, contentContainer.getWidth(), rowH);
            contentContainer.addAndMakeVisible(item.get());
            items.push_back(std::move(item));
            y += rowH;
        }
        
        contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), std::max(10, y));
    }
    else
    {
        // Just update data
        for (auto& item : items) {
            item->updateFromModel();
        }
    }
    
    // Ensure container width
    if (contentContainer.getWidth() != viewport.getWidth()) { 
        // Force width update
        contentContainer.setSize(viewport.getMaximumVisibleWidth(), contentContainer.getHeight());
         int y = 0;
         for (auto& item : items) {
             item->setBounds(0, y, contentContainer.getWidth(), 30);
             y += 30;
         }
    }
}


//==============================================================================
bool IRListComponentV3::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".mp3"))
            return true;
    }
    return false;
}

void IRListComponentV3::filesDropped (const juce::StringArray& files, int x, int y)
{
    for (auto& f : files)
    {
        juce::File file(f);
        if (file.existsAsFile())
        {
            // We can add multiple files!
            processor.addIRFromFile(file);
        }
    }
}

