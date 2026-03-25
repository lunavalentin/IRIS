#include "IRListComponentV4.h"
#include "Theme.h"

//==============================================================================
IRListItemV4::IRListItemV4(IrisVSTV4AudioProcessor& p, juce::Uuid id)
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

IRListItemV4::~IRListItemV4()
{
}

void IRListItemV4::resized()
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

void IRListItemV4::paint(juce::Graphics& g)
{
    // Flat background
    g.fillAll(Theme::panelBackground); 
    
    // Selection Border
    if (processor.selectedIRId == pointId)
    {
        g.setColour(Theme::accentCyan.withAlpha(0.6f));
        g.drawRect(getLocalBounds(), 1.0f);
    }
    
    // Separator line
    g.setColour(Theme::borderMinimal);
    g.fillRect(0, getHeight()-1, getWidth(), 1);
}

void IRListItemV4::updateFromModel()
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
            nameLabel.setColour(juce::Label::textColourId, Theme::textPrimary);
            nameLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            
            // Only update text if not editing to avoid fighting user
            if (!xEditor.hasKeyboardFocus(true))
                xEditor.setText(juce::String(p.x, 3), juce::dontSendNotification);
                
            if (!yEditor.hasKeyboardFocus(true))
                yEditor.setText(juce::String(p.y, 3), juce::dontSendNotification);
            
            lockButton.setToggleState(p.locked, juce::dontSendNotification);
            
            // Visual feedback for lock
            if (p.locked) nameLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
            else nameLabel.setColour(juce::Label::textColourId, Theme::textPrimary);
            
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
            break;
        }
    }
}

void IRListItemV4::buttonClicked(juce::Button* b)
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

void IRListItemV4::comboBoxChanged(juce::ComboBox* cb)
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

void IRListItemV4::textEditorReturnKeyPressed(juce::TextEditor& ed)
{
    textEditorFocusLost(ed);
}

void IRListItemV4::textEditorFocusLost(juce::TextEditor& ed)
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
IRListComponentV4::IRListComponentV4(IrisVSTV4AudioProcessor& p)
    : processor(p)
{
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentContainer, false);
    
    addAndMakeVisible(titleLabel);
    titleLabel.setText("IRS", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(Theme::getHeadingFont(12.0f));
    titleLabel.setColour(juce::Label::textColourId, Theme::textSecondary);

    updateContent();
}

IRListComponentV4::~IRListComponentV4()
{
}

void IRListComponentV4::resized()
{
    auto area = getLocalBounds().reduced(2);
    
    // Header (Title)
    titleLabel.setBounds(area.removeFromTop(20));
    
    // Spacer
    area.removeFromTop(5);
    
    // Viewport
    viewport.setBounds(area);
    int contentHeight = items.size() * 30; // 30px row
    contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), contentHeight);
    
    for (int i=0; i<items.size(); ++i)
         items[i]->setBounds(0, i*30, contentContainer.getWidth(), 30);
}

void IRListComponentV4::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panelBackground); 
    
    if (items.empty()) { /* ... */ }
}

void IRListComponentV4::updateContent()
{
    // 2. Handle IRs
    std::vector<juce::Uuid> currentIRIds;
    for (auto& p : processor.points) {
        currentIRIds.push_back(p.id);
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
            auto item = std::make_unique<IRListItemV4>(processor, id);
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
bool IRListComponentV4::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".mp3"))
            return true;
    }
    return false;
}

void IRListComponentV4::filesDropped (const juce::StringArray& files, int x, int y)
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

