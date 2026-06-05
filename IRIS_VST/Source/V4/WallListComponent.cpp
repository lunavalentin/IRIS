#include "WallListComponent.h"
#include "Theme.h"
#include <cmath>

// ---------------------------------------------------------------------------
// WallListItem
// ---------------------------------------------------------------------------

WallListItem::WallListItem(IrisAudioProcessor& p, juce::Uuid id)
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
    nameEditor.setColour(juce::TextEditor::outlineColourId,    juce::Colours::transparentBlack);
    nameEditor.setText("Wall");

    addAndMakeVisible(lengthEditor);
    lengthEditor.addListener(this);
    lengthEditor.setJustification(juce::Justification::centred);
    lengthEditor.setTooltip("Length");

    addAndMakeVisible(angleEditor);
    angleEditor.addListener(this);
    angleEditor.setJustification(juce::Justification::centred);
    angleEditor.setTooltip("Angle (Deg)");
}

WallListItem::~WallListItem() {}

void WallListItem::resized()
{
    auto area = getLocalBounds().reduced(2);

    lockButton.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(2);

    deleteButton.setBounds(area.removeFromLeft(24).reduced(2));
    area.removeFromLeft(5);

    auto right = area.removeFromRight(100);
    lengthEditor.setBounds(right.removeFromLeft(40).reduced(1));
    right.removeFromLeft(10);
    angleEditor.setBounds(right.removeFromLeft(40).reduced(1));

    nameEditor.setBounds(area.reduced(2));
}

void WallListItem::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panelBackground);

    g.setColour(Theme::borderMinimal);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    if (processor.selectedWallId == wallId)
    {
        g.setColour(Theme::accentCyan.withAlpha(0.6f));
        g.drawRect(getLocalBounds(), 1.0f);
    }

    g.setColour(Theme::textMuted);
    g.setFont(Theme::getBaseFont(10.0f));

    if (lengthEditor.isVisible())
        g.drawText("m", lengthEditor.getRight(), lengthEditor.getY(),
                   10, lengthEditor.getHeight(), juce::Justification::centredLeft);

    if (angleEditor.isVisible())
        g.drawText(juce::CharPointer_UTF8("\xc2\xb0"), angleEditor.getRight(), angleEditor.getY(),
                   10, angleEditor.getHeight(), juce::Justification::centredLeft);
}

void WallListItem::buttonClicked(juce::Button* b)
{
    if (b == &deleteButton)
    {
        processor.removeWall(wallId);
    }
    else if (b == &lockButton)
    {
        juce::ScopedLock sl(processor.stateLock);
        for (auto& w : processor.walls)
            if (w.id == wallId) { w.locked = lockButton.getToggleState(); break; }
        if (processor.onStateChanged) processor.onStateChanged();
    }
}

void WallListItem::textEditorReturnKeyPressed(juce::TextEditor& ed)
{
    textEditorFocusLost(ed);
}

void WallListItem::textEditorFocusLost(juce::TextEditor& ed)
{
    juce::ScopedLock sl(processor.stateLock);
    for (auto& w : processor.walls)
    {
        if (w.id != wallId || w.locked) continue;

        if (&ed == &nameEditor)
        {
            w.name = nameEditor.getText();
        }
        else
        {
            float cx = (w.x1 + w.x2) * 0.5f;
            float cy = (w.y1 + w.y2) * 0.5f;
            float dx = w.x2 - w.x1;
            float dy = w.y2 - w.y1;

            float newLen = std::sqrt(dx*dx + dy*dy);
            float newAng = std::atan2(dy, dx);

            if (&ed == &lengthEditor)
                newLen = std::max(0.01f, ed.getText().getFloatValue());
            else if (&ed == &angleEditor)
                newAng = juce::degreesToRadians(ed.getText().getFloatValue());

            float hx = 0.5f * newLen * std::cos(newAng);
            float hy = 0.5f * newLen * std::sin(newAng);
            w.x1 = cx - hx; w.x2 = cx + hx;
            w.y1 = cy - hy; w.y2 = cy + hy;

            processor.updateWeightsGaussian();
        }

        if (processor.onStateChanged) processor.onStateChanged();
        break;
    }
}

void WallListItem::updateFromModel()
{
    for (const auto& w : processor.walls)
    {
        if (w.id != wallId) continue;

        if (!nameEditor.hasKeyboardFocus(true))
            nameEditor.setText(w.name, juce::dontSendNotification);

        lockButton.setToggleState(w.locked, juce::dontSendNotification);
        nameEditor.setColour(juce::TextEditor::textColourId,
                             w.locked ? juce::Colours::orange : Theme::textPrimary);

        float dx  = w.x2 - w.x1;
        float dy  = w.y2 - w.y1;
        float len = std::sqrt(dx*dx + dy*dy);
        float ang = juce::radiansToDegrees(std::atan2(dy, dx));

        if (!lengthEditor.hasKeyboardFocus(true))
            lengthEditor.setText(juce::String(len, 3), juce::dontSendNotification);

        if (!angleEditor.hasKeyboardFocus(true))
            angleEditor.setText(juce::String(ang, 1), juce::dontSendNotification);

        break;
    }
}

// ---------------------------------------------------------------------------
// WallListComponent
// ---------------------------------------------------------------------------

WallListComponent::WallListComponent(IrisAudioProcessor& p)
    : processor(p)
{
    addAndMakeVisible(titleLabel);
    titleLabel.setText("WALLS", juce::dontSendNotification);
    titleLabel.setFont(Theme::getHeadingFont(12.0f));
    titleLabel.setColour(juce::Label::textColourId, Theme::textSecondary);
    titleLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentContainer, false);

    updateContent();
}

WallListComponent::~WallListComponent() {}

void WallListComponent::resized()
{
    auto area = getLocalBounds();
    titleLabel.setBounds(area.removeFromTop(30).reduced(5));

    if (items.empty())
    {
        viewport.setBounds(area);
        contentContainer.setBounds(0, 0, 0, 0);
        return;
    }

    viewport.setBounds(area);

    int contentHeight = static_cast<int>(items.size()) * 35;
    contentContainer.setBounds(0, 0, viewport.getMaximumVisibleWidth(), contentHeight);

    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        items[static_cast<size_t>(i)]->setBounds(0, i * 35, contentContainer.getWidth(), 30);
}

void WallListComponent::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panelBackground);

    if (items.empty())
    {
        g.setColour(Theme::textMuted);
        g.setFont(Theme::getBaseFont(14.0f));
        g.drawText("No walls active",
                   getLocalBounds().removeFromBottom(getHeight() - 24),
                   juce::Justification::centred);
    }
}

void WallListComponent::updateContent()
{
    bool structureChanged = (items.size() != processor.walls.size());
    if (!structureChanged)
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (items[static_cast<size_t>(i)]->wallId != processor.walls[static_cast<size_t>(i)].id)
                { structureChanged = true; break; }

    if (structureChanged)
    {
        items.clear();
        contentContainer.removeAllChildren();

        for (const auto& w : processor.walls)
        {
            auto item = std::make_unique<WallListItem>(processor, w.id);
            contentContainer.addAndMakeVisible(item.get());
            items.push_back(std::move(item));
        }
        resized();
    }
    else
    {
        for (auto& item : items) item->updateFromModel();
        contentContainer.repaint();
    }
}
