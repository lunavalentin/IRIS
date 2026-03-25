#include "ListenerListComponentV4.h"
#include "Theme.h"

//==============================================================================
ListenerListItemV4::ListenerListItemV4(IrisVSTV4AudioProcessor& p, juce::Uuid id, bool local)
    : listenerId(id), isLocalList(local), processor(p)
{
    addAndMakeVisible(nameLabel);
    nameLabel.setJustificationType(juce::Justification::centredLeft);
    
    addAndMakeVisible(xEditor);
    xEditor.setJustification(juce::Justification::centred);
    xEditor.addListener(this);
    
    addAndMakeVisible(yEditor);
    yEditor.setJustification(juce::Justification::centred);
    yEditor.addListener(this);
    
    addAndMakeVisible(linkToggle);
    linkToggle.setTooltip("Link Matrix");
    linkToggle.setClickingTogglesState(false);
    linkToggle.addListener(this);

    addAndMakeVisible(lockToggle);
    lockToggle.setTooltip("Lock Listener");
    lockToggle.setClickingTogglesState(true);
    lockToggle.addListener(this);

    if (!isLocalList) {
        linkToggle.setVisible(false);
    }

    updateFromModel();
}

ListenerListItemV4::~ListenerListItemV4()
{
}

void ListenerListItemV4::resized()
{
    auto area = getLocalBounds().reduced(2);
    
    // Link Toggle (Left)
    linkToggle.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(4);
    
    // Lock Toggle (Left)
    lockToggle.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(4);
    
    // X/Y (Right)
    xEditor.setBounds(area.removeFromRight(45).reduced(2));
    area.removeFromRight(4);
    yEditor.setBounds(area.removeFromRight(45).reduced(2));
    area.removeFromRight(4);
    
    // Name (Center)
    nameLabel.setBounds(area);
}

void ListenerListItemV4::paint(juce::Graphics& g)
{
    // Lighter background for Local, darker for remote
    if (isLocalList)
    {
        g.fillAll(Theme::cardElevated);
    }
    else
    {
        g.fillAll(Theme::panelBackground);
    }
    
    // Selection Border
    if (processor.selectedListenerId == listenerId)
    {
        g.setColour(isLocalList ? Theme::listenerLocalRed : Theme::listenerRemotePink);
        g.drawRect(getLocalBounds(), 2);
    }
    
    // Separator
    g.setColour(Theme::borderMinimal);
    g.fillRect(0, getHeight()-1, getWidth(), 1);
}

void ListenerListItemV4::mouseDown(const juce::MouseEvent& e)
{
    processor.selectedListenerId = listenerId;
    if (processor.onStateChanged) processor.onStateChanged();
}

void ListenerListItemV4::updateFromModel()
{
    juce::ScopedLock sl(processor.stateLock);
    
    float x = 0.0f;
    float y = 0.0f;
    juce::String name = "";
    bool locked = false;
    
    if (isLocalList) {
        x = processor.localAudioListener.x;
        y = processor.localAudioListener.y;
        name = processor.localAudioListener.name;
        locked = processor.localAudioListener.locked;
    } else if (processor.remoteListeners.count(listenerId)) {
        const auto& rl = processor.remoteListeners[listenerId];
        x = rl.x;
        y = rl.y;
        name = rl.name;
        locked = rl.locked;
    } else {
        return;
    }
    
    nameLabel.setText(name + (isLocalList ? " (Local)" : ""), juce::dontSendNotification);
    
    if (!xEditor.hasKeyboardFocus(true))
        xEditor.setText(juce::String(x, 2), juce::dontSendNotification);
    if (!yEditor.hasKeyboardFocus(true))
        yEditor.setText(juce::String(y, 2), juce::dontSendNotification);
        
    lockToggle.setToggleState(locked, juce::dontSendNotification);
}

void ListenerListItemV4::buttonClicked(juce::Button* b)
{
    if (b == &linkToggle && isLocalList)
    {
        auto* matrixComp = new ListenerLinkMatrixComponentV4(processor);
        juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(matrixComp), linkToggle.getScreenBounds(), nullptr);
    }
    else if (b == &lockToggle)
    {
        processor.setListenerLocked(listenerId, lockToggle.getToggleState(), true);
    }
}

void ListenerListItemV4::textEditorReturnKeyPressed(juce::TextEditor& ed)
{
    textEditorFocusLost(ed);
}

void ListenerListItemV4::textEditorFocusLost(juce::TextEditor& ed)
{
    float val = ed.getText().getFloatValue();
    val = juce::jlimit(0.0f, 1.0f, val);
    
    float cx = 0.0f, cy = 0.0f;
    if (isLocalList) {
        cx = processor.localAudioListener.x;
        cy = processor.localAudioListener.y;
    } else if (processor.remoteListeners.count(listenerId)) {
        cx = processor.remoteListeners[listenerId].x;
        cy = processor.remoteListeners[listenerId].y;
    } else {
        return;
    }
    
    if (&ed == &xEditor) cx = val;
    if (&ed == &yEditor) cy = val;
    
    processor.updateListenerPosition(listenerId, cx, cy, true);
}


//==============================================================================

ListenerLinkMatrixComponentV4::ListenerLinkMatrixComponentV4(IrisVSTV4AudioProcessor& p)
    : processor(p)
{
    setSize(200, 200);
    rebuildMatrix();
    startTimer(200);
}

ListenerLinkMatrixComponentV4::~ListenerLinkMatrixComponentV4() {}

void ListenerLinkMatrixComponentV4::resized()
{
    // The bounds are determined statically in getLocalBounds() during paint.
}

void ListenerLinkMatrixComponentV4::timerCallback()
{
    // check if node count changed
    size_t count = 1 + processor.remoteListeners.size();
    if (count != sortedIds.size()) {
        rebuildMatrix();
    } else {
        updateButtons();
    }
}

void ListenerLinkMatrixComponentV4::rebuildMatrix()
{
    juce::ScopedLock sl(processor.stateLock);
    
    cells.clear();
    sortedIds.clear();
    
    sortedIds.push_back(processor.localAudioListener.id);
    for (const auto& pair : processor.remoteListeners) {
        sortedIds.push_back(pair.first);
    }
    
    int n = (int)sortedIds.size();
    int cellSize = 25;
    int margin = 30;
    setSize(margin + n * cellSize + 10, margin + n * cellSize + 10);
    
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            juce::Uuid rId = sortedIds[r];
            juce::Uuid cId = sortedIds[c];
            
            Cell cell;
            cell.rId = rId;
            cell.cId = cId;
            cell.btn = std::make_unique<juce::ToggleButton>();
            
            juce::String rName = (rId == processor.localAudioListener.id) ? processor.localAudioListener.name : processor.remoteListeners[rId].name;
            juce::String cName = (cId == processor.localAudioListener.id) ? processor.localAudioListener.name : processor.remoteListeners[cId].name;
            cell.btn->setTooltip(rName + " <-> " + cName);
            
            if (rId == cId) {
                cell.btn->setToggleState(true, juce::dontSendNotification);
                cell.btn->setEnabled(false); // Diagonal always on
            }
            
            cell.btn->onClick = [this, rId, cId]() {
                processor.toggleLinkMatrix(rId, cId, true);
                updateButtons();
            };
            
            addAndMakeVisible(cell.btn.get());
            cell.btn->setBounds(margin + c * cellSize, margin + r * cellSize, cellSize, cellSize);
            cells.push_back(std::move(cell));
        }
    }
    updateButtons();
}

void ListenerLinkMatrixComponentV4::updateButtons()
{
    juce::ScopedLock sl(processor.stateLock);
    for (auto& cell : cells) {
        if (cell.rId == cell.cId) continue;
        
        juce::String s1 = cell.rId.toString();
        juce::String s2 = cell.cId.toString();
        std::pair<juce::String, juce::String> edge(std::min(s1, s2), std::max(s1, s2));
        bool isLinked = processor.linkMatrix.count(edge) > 0;
        
        cell.btn->setToggleState(isLinked, juce::dontSendNotification);
    }
}

void ListenerLinkMatrixComponentV4::paint(juce::Graphics& g)
{
    g.fillAll(Theme::cardElevated);
    g.setColour(Theme::textPrimary);
    g.setFont(Theme::getBaseFont(12.0f));
    
    int n = (int)sortedIds.size();
    int cellSize = 25;
    int margin = 30;
    
    juce::ScopedLock sl(processor.stateLock);
    
    // Draw row / col labels
    for (int i = 0; i < n; ++i) {
        juce::Uuid id = sortedIds[i];
        juce::String name = (id == processor.localAudioListener.id) ? processor.localAudioListener.name : processor.remoteListeners[id].name;
        if (name.length() > 2) name = name.substring(0, 2); // truncate for neatness
        
        // Col headers
        g.drawText(name, margin + i * cellSize, 5, cellSize, 20, juce::Justification::centred);
        // Row headers
        g.drawText(name, 5, margin + i * cellSize, 25, cellSize, juce::Justification::centredRight);
    }
}

//==============================================================================

ListenerListComponentV4::ListenerListComponentV4(IrisVSTV4AudioProcessor& p)
    : processor(p)
{
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentContainer, false);
    
    startTimer(100);
    updateContent();
}

ListenerListComponentV4::~ListenerListComponentV4()
{
}

void ListenerListComponentV4::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panelBackground); 
    
    g.setColour(Theme::textSecondary);
    g.setFont(Theme::getHeadingFont(12.0f));
    g.drawText("LISTENERS", 5, 0, 100, 20, juce::Justification::centredLeft); 
}

void ListenerListComponentV4::resized()
{
    auto area = getLocalBounds().reduced(2);
    area.removeFromTop(20); // Title
    
    viewport.setBounds(area);
    
    int contentHeight = (int)items.size() * 30;
    contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), contentHeight);
    
    for (size_t i = 0; i < items.size(); ++i)
         items[i]->setBounds(0, (int)i * 30, contentContainer.getWidth(), 30);
}

void ListenerListComponentV4::timerCallback()
{
    // Compare structural IDs
    std::vector<juce::Uuid> currentRemoteIds;
    {
        juce::ScopedLock sl(processor.stateLock);
        for (const auto& pair : processor.remoteListeners) {
            currentRemoteIds.push_back(pair.first);
        }
    }
    
    bool structureChanged = false;
    // Items contains Local + Remotes. So sizes should differ by 1.
    if ((items.size() == 0) || (items.size() - 1 != currentRemoteIds.size())) {
        structureChanged = true;
    } else {
        for (size_t i = 0; i < currentRemoteIds.size(); ++i) {
            if (items[i+1]->listenerId != currentRemoteIds[i]) {
                structureChanged = true;
                break;
            }
        }
    }
    
    if (structureChanged) {
        updateContent();
    } else {
        for (auto& item : items) {
            item->updateFromModel();
        }
    }
}

void ListenerListComponentV4::updateContent()
{
    juce::ScopedLock sl(processor.stateLock);
    items.clear();
    contentContainer.removeAllChildren();
    
    int y = 0;
    int rowH = 30;
    
    // Add Local fixed at top
    auto localItem = std::make_unique<ListenerListItemV4>(processor, processor.localAudioListener.id, true);
    localItem->setBounds(0, y, contentContainer.getWidth(), rowH);
    contentContainer.addAndMakeVisible(localItem.get());
    items.push_back(std::move(localItem));
    y += rowH;
    
    // Add Remotes
    for (const auto& pair : processor.remoteListeners)
    {
        auto remoteItem = std::make_unique<ListenerListItemV4>(processor, pair.first, false);
        remoteItem->setBounds(0, y, contentContainer.getWidth(), rowH);
        contentContainer.addAndMakeVisible(remoteItem.get());
        items.push_back(std::move(remoteItem));
        y += rowH;
    }
    
    contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), std::max(10, y));
    resized();
}