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
    processor.notifyStructuralChange();
}

void ListenerListItem::updateFromModel()
{
    juce::ScopedLock sl(processor.stateLock);

    float        x      = 0.0f, y = 0.0f;
    juce::String name;
    bool         locked = false;

    if (isLocalList)
    {
        // The local id changes when a session is restored: follow it.
        listenerId = processor.localAudioListener.id;
        x      = processor.localAudioListener.x;
        y      = processor.localAudioListener.y;
        name   = processor.localAudioListener.name;
        locked = processor.localAudioListener.locked;
    }
    else if (auto it = processor.remoteListeners.find(listenerId); it != processor.remoteListeners.end())
    {
        const auto& rl = it->second;
        x = rl.x; y = rl.y; name = rl.name; locked = rl.locked;
    }
    else
    {
        return;
    }

    // Label::setText closes an open editor, so never touch it during a rename.
    if (! nameLabel.isBeingEdited())
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
        if (onOpenLinkMatrix) onOpenLinkMatrix(linkToggle);
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
    {
        juce::ScopedLock sl(processor.stateLock);
        if (isLocalList)
        {
            cx = processor.localAudioListener.x;
            cy = processor.localAudioListener.y;
        }
        else if (auto it = processor.remoteListeners.find(listenerId); it != processor.remoteListeners.end())
        {
            cx = it->second.x;
            cy = it->second.y;
        }
        else return;
    }

    if (&ed == &xEditor) cx = val;
    if (&ed == &yEditor) cy = val;

    processor.updateListenerPosition(listenerId, cx, cy, true);
}

void ListenerListItem::labelTextChanged(juce::Label* labelThatHasChanged)
{
    if (labelThatHasChanged != &nameLabel) return;

    juce::String newName = nameLabel.getText().replace(" (Local)", "").trim().substring(0, 64);
    if (newName.isNotEmpty())
        processor.setListenerName(listenerId, newName);
}

// ---------------------------------------------------------------------------
// ListenerLinkMatrixComponent
// ---------------------------------------------------------------------------

ListenerLinkMatrixComponent::ListenerLinkMatrixComponent(IrisAudioProcessor& p)
    : processor(&p)
{
    setSize(200, 200);
    rebuildMatrix();
    startTimer(200);
}

ListenerLinkMatrixComponent::~ListenerLinkMatrixComponent() {}

void ListenerLinkMatrixComponent::resized() {}

juce::String ListenerLinkMatrixComponent::nameFor(const juce::Uuid& id) const
{
    if (processor == nullptr) return {};
    if (id == processor->localAudioListener.id) return processor->localAudioListener.name;
    if (auto it = processor->remoteListeners.find(id); it != processor->remoteListeners.end())
        return it->second.name;
    return "?";
}

void ListenerLinkMatrixComponent::timerCallback()
{
    if (processor == nullptr) return;

    // Rebuild when the set of listeners changes (not just the count).
    std::vector<juce::Uuid> ids;
    {
        juce::ScopedLock sl(processor->stateLock);
        ids.push_back(processor->localAudioListener.id);
        for (const auto& pair : processor->remoteListeners)
            ids.push_back(pair.first);
    }

    if (ids != sortedIds)
        rebuildMatrix();
    else
        updateButtons();
}

void ListenerLinkMatrixComponent::rebuildMatrix()
{
    if (processor == nullptr) return;
    juce::ScopedLock sl(processor->stateLock);

    cells.clear();
    sortedIds.clear();

    sortedIds.push_back(processor->localAudioListener.id);
    for (const auto& pair : processor->remoteListeners)
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

            juce::String rName = nameFor(rId);
            juce::String cName = nameFor(cId);

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
                if (processor == nullptr) return;
                processor->toggleLinkMatrix(rId, cId, true);
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
    if (processor == nullptr) return;
    juce::ScopedLock sl(processor->stateLock);
    for (auto& cell : cells)
    {
        if (cell.rId == cell.cId) continue;

        juce::String s1 = cell.rId.toString();
        juce::String s2 = cell.cId.toString();
        auto edge = std::make_pair(std::min(s1, s2), std::max(s1, s2));
        cell.btn->setToggleState(processor->linkMatrix.count(edge) > 0, juce::dontSendNotification);
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

    if (processor == nullptr) return;
    juce::ScopedLock sl(processor->stateLock);
    for (int i = 0; i < n; ++i)
    {
        juce::Uuid   id   = sortedIds[static_cast<size_t>(i)];
        juce::String name = nameFor(id);
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

ListenerListComponent::~ListenerListComponent()
{
    // The call-out box is a child of the editor and may be deleted after us;
    // make sure it no longer touches the processor.
    if (matrixContent != nullptr) matrixContent->detach();
    if (matrixBox != nullptr)     matrixBox->dismiss();
}

void ListenerListComponent::openLinkMatrix(juce::Component& anchor)
{
    if (matrixBox != nullptr) { matrixBox->dismiss(); return; }

    auto* parent  = getTopLevelComponent();
    auto  content = std::make_unique<ListenerLinkMatrixComponent>(processor);
    matrixContent = content.get();

    const auto area = parent->getLocalArea(&anchor, anchor.getLocalBounds());
    matrixBox = &juce::CallOutBox::launchAsynchronously(std::move(content), area, parent);
}

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
    refresh();
}

void ListenerListComponent::refresh()
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
    localItem->onOpenLinkMatrix = [this](juce::Component& anchor) { openLinkMatrix(anchor); };
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