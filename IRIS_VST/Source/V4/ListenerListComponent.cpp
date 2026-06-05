#include "ListenerListComponent.h"
#include "Theme.h"
#include "IrisOSCManager.h"

// ---------------------------------------------------------------------------
// ListenerListItem
// ---------------------------------------------------------------------------

ListenerListItem::ListenerListItem(IrisAudioProcessor& p, juce::Uuid id, bool local)
    : listenerId(id), isLocalList(local), processor(p)
{
    addAndMakeVisible(nameLabel);
    nameLabel.setJustificationType(juce::Justification::centredLeft);
    nameLabel.setEditable(false, true, false);
    nameLabel.addListener(this);

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
    if (!isLocalList) linkToggle.setVisible(false);

    addAndMakeVisible(lockToggle);
    lockToggle.setTooltip("Lock Listener");
    lockToggle.setClickingTogglesState(true);
    lockToggle.addListener(this);

    updateFromModel();
}

ListenerListItem::~ListenerListItem() {}

void ListenerListItem::resized()
{
    auto area = getLocalBounds().reduced(2);

    linkToggle.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(4);

    lockToggle.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(4);

    xEditor.setBounds(area.removeFromRight(45).reduced(2));
    area.removeFromRight(4);
    yEditor.setBounds(area.removeFromRight(45).reduced(2));
    area.removeFromRight(4);

    nameLabel.setBounds(area);
}

void ListenerListItem::paint(juce::Graphics& g)
{
    g.fillAll(isLocalList ? Theme::cardElevated : Theme::panelBackground);

    if (processor.selectedListenerId == listenerId)
    {
        g.setColour(isLocalList ? Theme::listenerLocalRed : Theme::listenerRemotePink);
        g.drawRect(getLocalBounds(), 2);
    }

    g.setColour(Theme::borderMinimal);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void ListenerListItem::mouseDown(const juce::MouseEvent&)
{
    processor.selectedListenerId = listenerId;
    if (processor.onStateChanged) processor.onStateChanged();
}

void ListenerListItem::updateFromModel()
{
    juce::ScopedLock sl(processor.stateLock);

    float        x      = 0.0f, y = 0.0f;
    juce::String name;
    bool         locked = false;

    if (isLocalList)
    {
        x      = processor.localAudioListener.x;
        y      = processor.localAudioListener.y;
        name   = processor.localAudioListener.name;
        locked = processor.localAudioListener.locked;
    }
    else if (processor.remoteListeners.count(listenerId))
    {
        const auto& rl = processor.remoteListeners[listenerId];
        x = rl.x; y = rl.y; name = rl.name; locked = rl.locked;
    }
    else
    {
        return;
    }

    nameLabel.setText(name + (isLocalList ? " (Local)" : ""), juce::dontSendNotification);

    if (!xEditor.hasKeyboardFocus(true))
        xEditor.setText(juce::String(x, 2), juce::dontSendNotification);
    if (!yEditor.hasKeyboardFocus(true))
        yEditor.setText(juce::String(y, 2), juce::dontSendNotification);

    lockToggle.setToggleState(locked, juce::dontSendNotification);
}

void ListenerListItem::buttonClicked(juce::Button* b)
{
    if (b == &linkToggle && isLocalList)
    {
        auto* matrixComp = new ListenerLinkMatrixComponent(processor);
        juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(matrixComp),
                                               linkToggle.getScreenBounds(), nullptr);
    }
    else if (b == &lockToggle)
    {
        processor.setListenerLocked(listenerId, lockToggle.getToggleState(), true);
    }
}

void ListenerListItem::textEditorReturnKeyPressed(juce::TextEditor& ed)
{
    textEditorFocusLost(ed);
}

void ListenerListItem::textEditorFocusLost(juce::TextEditor& ed)
{
    float val = juce::jlimit(0.0f, 1.0f, ed.getText().getFloatValue());

    float cx = 0.0f, cy = 0.0f;
    if (isLocalList)
    {
        cx = processor.localAudioListener.x;
        cy = processor.localAudioListener.y;
    }
    else if (processor.remoteListeners.count(listenerId))
    {
        cx = processor.remoteListeners[listenerId].x;
        cy = processor.remoteListeners[listenerId].y;
    }
    else return;

    if (&ed == &xEditor) cx = val;
    if (&ed == &yEditor) cy = val;

    processor.updateListenerPosition(listenerId, cx, cy, true);
}

void ListenerListItem::labelTextChanged(juce::Label* labelThatHasChanged)
{
    if (labelThatHasChanged != &nameLabel) return;

    juce::String newName = nameLabel.getText().replace(" (Local)", "");

    if (isLocalList)
    {
        processor.localAudioListener.name = newName;
        IrisOSCManager::getInstance().setListenerState(
            processor.localAudioListener.id, newName,
            processor.localAudioListener.x, processor.localAudioListener.y,
            false, processor.localAudioListener.locked, &processor);
    }
    else
    {
        IrisOSCManager::getInstance().setListenerState(
            listenerId, newName,
            processor.remoteListeners[listenerId].x,
            processor.remoteListeners[listenerId].y,
            false, processor.remoteListeners[listenerId].locked, &processor);
    }

    if (processor.onStateChanged) processor.onStateChanged();
}

// ---------------------------------------------------------------------------
// ListenerLinkMatrixComponent
// ---------------------------------------------------------------------------

ListenerLinkMatrixComponent::ListenerLinkMatrixComponent(IrisAudioProcessor& p)
    : processor(p)
{
    setSize(200, 200);
    rebuildMatrix();
    startTimer(200);
}

ListenerLinkMatrixComponent::~ListenerLinkMatrixComponent() {}

void ListenerLinkMatrixComponent::resized() {}

void ListenerLinkMatrixComponent::timerCallback()
{
    size_t count = 1 + processor.remoteListeners.size();
    if (count != sortedIds.size())
        rebuildMatrix();
    else
        updateButtons();
}

void ListenerLinkMatrixComponent::rebuildMatrix()
{
    juce::ScopedLock sl(processor.stateLock);

    cells.clear();
    sortedIds.clear();

    sortedIds.push_back(processor.localAudioListener.id);
    for (const auto& pair : processor.remoteListeners)
        sortedIds.push_back(pair.first);

    const int n        = static_cast<int>(sortedIds.size());
    const int cellSize = 25;
    const int margin   = 30;
    setSize(margin + n * cellSize + 10, margin + n * cellSize + 10);

    for (int r = 0; r < n; ++r)
    {
        for (int c = 0; c < n; ++c)
        {
            juce::Uuid rId = sortedIds[static_cast<size_t>(r)];
            juce::Uuid cId = sortedIds[static_cast<size_t>(c)];

            juce::String rName = (rId == processor.localAudioListener.id)
                                 ? processor.localAudioListener.name
                                 : processor.remoteListeners[rId].name;
            juce::String cName = (cId == processor.localAudioListener.id)
                                 ? processor.localAudioListener.name
                                 : processor.remoteListeners[cId].name;

            Cell cell;
            cell.rId = rId;
            cell.cId = cId;
            cell.btn = std::make_unique<juce::ToggleButton>();
            cell.btn->setTooltip(rName + " <-> " + cName);

            if (rId == cId)
            {
                cell.btn->setToggleState(true, juce::dontSendNotification);
                cell.btn->setEnabled(false);
            }

            cell.btn->onClick = [this, rId, cId]()
            {
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

void ListenerLinkMatrixComponent::updateButtons()
{
    juce::ScopedLock sl(processor.stateLock);
    for (auto& cell : cells)
    {
        if (cell.rId == cell.cId) continue;

        juce::String s1 = cell.rId.toString();
        juce::String s2 = cell.cId.toString();
        auto edge = std::make_pair(std::min(s1, s2), std::max(s1, s2));
        cell.btn->setToggleState(processor.linkMatrix.count(edge) > 0, juce::dontSendNotification);
    }
}

void ListenerLinkMatrixComponent::paint(juce::Graphics& g)
{
    g.fillAll(Theme::cardElevated);
    g.setColour(Theme::textPrimary);
    g.setFont(Theme::getBaseFont(12.0f));

    const int n        = static_cast<int>(sortedIds.size());
    const int cellSize = 25;
    const int margin   = 30;

    juce::ScopedLock sl(processor.stateLock);
    for (int i = 0; i < n; ++i)
    {
        juce::Uuid   id   = sortedIds[static_cast<size_t>(i)];
        juce::String name = (id == processor.localAudioListener.id)
                            ? processor.localAudioListener.name
                            : processor.remoteListeners[id].name;
        if (name.length() > 2) name = name.substring(0, 2);

        g.drawText(name, margin + i * cellSize,  5,      cellSize, 20,       juce::Justification::centred);
        g.drawText(name, 5,                       margin + i * cellSize, 25, cellSize, juce::Justification::centredRight);
    }
}

// ---------------------------------------------------------------------------
// ListenerListComponent
// ---------------------------------------------------------------------------

ListenerListComponent::ListenerListComponent(IrisAudioProcessor& p)
    : processor(p)
{
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentContainer, false);

    startTimer(100);
    updateContent();
}

ListenerListComponent::~ListenerListComponent() {}

void ListenerListComponent::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panelBackground);
    g.setColour(Theme::textSecondary);
    g.setFont(Theme::getHeadingFont(12.0f));
    g.drawText("LISTENERS", 5, 0, 100, 20, juce::Justification::centredLeft);
}

void ListenerListComponent::resized()
{
    auto area = getLocalBounds().reduced(2);
    area.removeFromTop(20);
    viewport.setBounds(area);

    int contentHeight = static_cast<int>(items.size()) * 30;
    contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), contentHeight);

    for (size_t i = 0; i < items.size(); ++i)
        items[i]->setBounds(0, static_cast<int>(i) * 30, contentContainer.getWidth(), 30);
}

void ListenerListComponent::timerCallback()
{
    std::vector<juce::Uuid> currentRemoteIds;
    {
        juce::ScopedLock sl(processor.stateLock);
        for (const auto& pair : processor.remoteListeners)
            currentRemoteIds.push_back(pair.first);
    }

    bool structureChanged = (items.empty() || items.size() - 1 != currentRemoteIds.size());
    if (!structureChanged)
        for (size_t i = 0; i < currentRemoteIds.size(); ++i)
            if (items[i + 1]->listenerId != currentRemoteIds[i])
                { structureChanged = true; break; }

    if (structureChanged)
        updateContent();
    else
        for (auto& item : items) item->updateFromModel();
}

void ListenerListComponent::updateContent()
{
    juce::ScopedLock sl(processor.stateLock);
    items.clear();
    contentContainer.removeAllChildren();

    const int rowH = 30;
    int y = 0;

    auto localItem = std::make_unique<ListenerListItem>(processor, processor.localAudioListener.id, true);
    localItem->setBounds(0, y, contentContainer.getWidth(), rowH);
    contentContainer.addAndMakeVisible(localItem.get());
    items.push_back(std::move(localItem));
    y += rowH;

    for (const auto& pair : processor.remoteListeners)
    {
        auto remoteItem = std::make_unique<ListenerListItem>(processor, pair.first, false);
        remoteItem->setBounds(0, y, contentContainer.getWidth(), rowH);
        contentContainer.addAndMakeVisible(remoteItem.get());
        items.push_back(std::move(remoteItem));
        y += rowH;
    }

    contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), std::max(10, y));
    resized();
}