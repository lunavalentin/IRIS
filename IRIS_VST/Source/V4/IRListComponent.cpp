#include "IRListComponent.h"
#include "Theme.h"

// ---------------------------------------------------------------------------
// IRListItem
// ---------------------------------------------------------------------------

IRListItem::IRListItem(IrisAudioProcessor& p, juce::Uuid id)
    : processor(p), pointId(id)
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

    addAndMakeVisible(lockButton);
    lockButton.setClickingTogglesState(true);
    lockButton.setTooltip("Lock Position");
    lockButton.addListener(this);

    addAndMakeVisible(deleteButton);
    deleteButton.setTooltip("Remove IR");
    deleteButton.addListener(this);

    updateFromModel();
}

IRListItem::~IRListItem() {}

void IRListItem::resized()
{
    auto area = getLocalBounds().reduced(2);

    lockButton.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(4);

    deleteButton.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(8);

    xEditor.setBounds(area.removeFromRight(60).reduced(2));
    area.removeFromRight(4);

    yEditor.setBounds(area.removeFromRight(60).reduced(2));
    area.removeFromRight(4);

    nameLabel.setBounds(area);
}

void IRListItem::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panelBackground);

    if (isIncompatible)
    {
        g.setColour(juce::Colours::orange.withAlpha(0.12f));
        g.fillRect(getLocalBounds());
        g.setColour(juce::Colours::orange.withAlpha(0.6f));
        g.fillRect(0, 0, 3, getHeight());
    }

    if (processor.selectedIRId == pointId)
    {
        g.setColour(isIncompatible ? juce::Colours::orange.withAlpha(0.8f)
                                   : Theme::accentCyan.withAlpha(0.6f));
        g.drawRect(getLocalBounds(), 1.0f);
    }

    g.setColour(Theme::borderMinimal);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void IRListItem::updateFromModel()
{
    for (auto& p : processor.points)
    {
        if (p.id != pointId) continue;

        int  numOut    = processor.getTotalNumOutputChannels();
        bool newIncompat = (p.sourceChannels > 1 && p.sourceChannels != numOut);

        // Only repaint the row if the visual state actually changed.
        if (newIncompat != isIncompatible)
        {
            isIncompatible = newIncompat;
            repaint();
        }

        juce::String displayName = p.name;
        if (isIncompatible)
            displayName += "  \u26a0 " + juce::String(p.sourceChannels)
                         + "ch IR / " + juce::String(numOut) + "ch out";

        nameLabel.setText(displayName, juce::dontSendNotification);
        nameLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        nameLabel.setColour(juce::Label::textColourId,
                            isIncompatible ? juce::Colours::orange
                                           : (p.locked ? juce::Colours::orange.brighter()
                                                        : Theme::textPrimary));
        nameLabel.setTooltip(isIncompatible
                             ? "IR has " + juce::String(p.sourceChannels) + " channels but output has "
                               + juce::String(numOut) + ". Use a mono IR or match track channel count."
                             : "");

        if (!xEditor.hasKeyboardFocus(true))
            xEditor.setText(juce::String(p.x, 3), juce::dontSendNotification);

        if (!yEditor.hasKeyboardFocus(true))
            yEditor.setText(juce::String(p.y, 3), juce::dontSendNotification);

        lockButton.setToggleState(p.locked, juce::dontSendNotification);
        deleteButton.setVisible(true);
        break;
    }
}

void IRListItem::buttonClicked(juce::Button* b)
{
    if (b == &lockButton)
        processor.setPointLocked(pointId, lockButton.getToggleState());
    else if (b == &deleteButton)
        processor.removePoint(pointId);
}

void IRListItem::textEditorReturnKeyPressed(juce::TextEditor& ed)
{
    textEditorFocusLost(ed);
}

void IRListItem::textEditorFocusLost(juce::TextEditor& ed)
{
    float val = juce::jlimit(0.0f, 1.0f, ed.getText().getFloatValue());

    float cx = 0.5f, cy = 0.5f;
    for (auto& p : processor.points)
    {
        if (p.id == pointId) { cx = p.x; cy = p.y; break; }
    }

    if (&ed == &xEditor) cx = val;
    if (&ed == &yEditor) cy = val;

    processor.updatePointPosition(pointId, cx, cy);
}

void IRListItem::labelTextChanged(juce::Label* labelThatHasChanged)
{
    if (labelThatHasChanged == &nameLabel)
        processor.setPointName(pointId, nameLabel.getText(), true);
}

// ---------------------------------------------------------------------------
// IRListComponent
// ---------------------------------------------------------------------------

IRListComponent::IRListComponent(IrisAudioProcessor& p)
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

IRListComponent::~IRListComponent() {}

void IRListComponent::resized()
{
    auto area = getLocalBounds().reduced(2);

    titleLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(5);
    viewport.setBounds(area);

    const int rowH          = 30;
    int       contentHeight = static_cast<int>(items.size()) * rowH;
    contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), contentHeight);

    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        items[static_cast<size_t>(i)]->setBounds(0, i * rowH, contentContainer.getWidth(), rowH);
}

void IRListComponent::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panelBackground);
}

void IRListComponent::updateContent()
{
    std::vector<juce::Uuid> currentIds;
    for (auto& p : processor.points)
        currentIds.push_back(p.id);

    bool structureChanged = (items.size() != currentIds.size());
    if (!structureChanged)
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i]->pointId != currentIds[i]) { structureChanged = true; break; }

    if (structureChanged)
    {
        items.clear();
        contentContainer.removeAllChildren();

        const int rowH = 30;
        int y = 0;
        for (auto id : currentIds)
        {
            auto item = std::make_unique<IRListItem>(processor, id);
            item->setBounds(0, y, contentContainer.getWidth(), rowH);
            contentContainer.addAndMakeVisible(item.get());
            items.push_back(std::move(item));
            y += rowH;
        }

        contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), std::max(10, y));
    }
    else
    {
        for (auto& item : items)
            item->updateFromModel();
    }

    if (contentContainer.getWidth() != viewport.getWidth())
    {
        contentContainer.setSize(viewport.getMaximumVisibleWidth(), contentContainer.getHeight());
        int y = 0;
        for (auto& item : items)
        {
            item->setBounds(0, y, contentContainer.getWidth(), 30);
            y += 30;
        }
    }
}

bool IRListComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif")
            || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".mp3"))
            return true;
    return false;
}

void IRListComponent::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        juce::File file(f);
        if (file.existsAsFile())
            processor.addIRFromFile(file);
    }
}
