#include "IRListComponentV2.h"

//==============================================================================
IRListItem::IRListItem(IrisVSTV2AudioProcessor& p, juce::Uuid id)
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

IRListItem::~IRListItem()
{
}

void IRListItem::resized()
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

void IRListItem::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey.withAlpha(0.2f));
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRect(getLocalBounds());
}

void IRListItem::updateFromModel()
{
    // Find point
    bool found = false;
    for (auto& p : processor.points)
    {
        if (p.id == pointId)
        {
            found = true;
            nameLabel.setText(p.name, juce::dontSendNotification);
            
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

void IRListItem::buttonClicked(juce::Button* b)
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

void IRListItem::comboBoxChanged(juce::ComboBox* cb)
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

void IRListItem::textEditorReturnKeyPressed(juce::TextEditor& ed)
{
    textEditorFocusLost(ed);
}

void IRListItem::textEditorFocusLost(juce::TextEditor& ed)
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
IRListComponent::IRListComponent(IrisVSTV2AudioProcessor& p)
    : processor(p)
{
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentContainer, false);
    
    // addAndMakeVisible(addIRButton);
    // addIRButton.onClick = [this] { addIRClicked(); };
    
    updateContent();
}

IRListComponent::~IRListComponent()
{
}

void IRListComponent::resized()
{
    auto area = getLocalBounds();
    viewport.setBounds(area);
    contentContainer.setBounds(area); // Initial size, will be adjusted in updateContent
}

void IRListComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.3f));
}

void IRListComponent::updateContent()
{
    // Check if sync needed
    bool structureChanged = false;
    if (items.size() != processor.points.size()) 
    {
        structureChanged = true;
    }
    else
    {
        for (size_t i=0; i<items.size(); ++i) {
            if (items[i]->pointId != processor.points[i].id) {
                structureChanged = true; break;
            }
        }
    }
    
    if (structureChanged)
    {
        items.clear();
        contentContainer.removeAllChildren();
        
        int y = 0;
        for (auto& p : processor.points)
        {
            auto item = std::make_unique<IRListItem>(processor, p.id);
            item->setBounds(0, y, getWidth(), 30); // Width will be fixed by resize logic if needed
            contentContainer.addAndMakeVisible(item.get());
            items.push_back(std::move(item));
            y += 30;
        }
        
        contentContainer.setBounds(0, 0, getWidth(), std::max(getHeight(), y));
    }
    else
    {
        // Just update data
        for (auto& item : items) {
            item->updateFromModel();
        }
    }
    
    // Ensure container width matches viewport logic
    if (contentContainer.getWidth() != viewport.getWidth())
        contentContainer.setSize(viewport.getMaximumVisibleWidth(), contentContainer.getHeight());
        
    // Relayout items if container width changed
     int y = 0;
     for (auto& item : items) {
         item->setBounds(0, y, contentContainer.getWidth(), 30);
         y += 30;
     }
}

void IRListComponent::addIRClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select IR File",
                                                      juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                      "*.wav;*.aiff");
                                                      
    auto folderChooserFlags = juce::FileBrowserComponent::openMode | 
                              juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile())
        {
            processor.addIRFromFile(file);
        }
    });
}

//==============================================================================
bool IRListComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".mp3"))
            return true;
    }
    return false;
}

void IRListComponent::filesDropped (const juce::StringArray& files, int x, int y)
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

