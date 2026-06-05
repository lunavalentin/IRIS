#include "PluginProcessor.h"
#include "PluginEditor.h"

IrisAudioProcessorEditor::IrisAudioProcessorEditor (IrisAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
      roomMap(p), controlPanel(p), listenerList(p), irList(p), wallList(p)
{
    setLookAndFeel(&irisLookAndFeel);

    addAndMakeVisible(roomMap);
    addAndMakeVisible(controlPanel);
    addAndMakeVisible(listenerList);
    addAndMakeVisible(irList);
    addAndMakeVisible(wallList);

    // Structural changes (add/remove IR, wall, listener) trigger a full list rebuild.
    audioProcessor.onStateChanged = [this]
    {
        juce::MessageManager::callAsync([this] { updateUI(); });
    };

    // 25Hz lightweight display timer — only repaints the overlay and room map.
    startTimerHz(25);

    setSize(1100, 700);
}

IrisAudioProcessorEditor::~IrisAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
    audioProcessor.onStateChanged = nullptr;
}

void IrisAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(Theme::backgroundDark);
}

void IrisAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    auto& neighbors = audioProcessor.currentNearestNeighbors;

    float sumW = 0.0f;
    for (auto& p : neighbors) sumW += audioProcessor.smoothedWeights[p.id];

    float mix          = audioProcessor.mixParam->load();
    float dynamicFade  = juce::jlimit(0.0f, 1.0f, sumW);
    if (sumW < 0.001f) sumW = 1.0f;

    const int x0 = 20;
    int y = 20;

    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.setColour(juce::Colours::white);
    g.drawText("Active IRs (Gain Factor):", x0, y, 200, 20, juce::Justification::left);
    y += 20;

    for (auto& p : neighbors)
    {
        float normW       = audioProcessor.smoothedWeights[p.id] / sumW;
        float actualFactor = normW * mix * dynamicFade;

        g.setColour(p.color);
        g.drawText(p.name + " (" + juce::String(normW * 100.0f, 1) + "%)",
                   x0, y, 200, 15, juce::Justification::left);

        g.drawText(juce::String(actualFactor, 3), x0, y + 16, 40, 10, juce::Justification::left);

        const float barMaxLen = 100.0f;
        g.fillRect(static_cast<float>(x0) + 45.0f, static_cast<float>(y) + 18.0f,
                   actualFactor * barMaxLen, 6.0f);

        y += 30;
        if (y > 300) break;
    }

    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawText(JucePlugin_VersionString, x0, y, 80, 10, juce::Justification::left);

    // Global compatibility warning — shown when any IR doesn't match the output bus.
    int numOut = audioProcessor.getTotalNumOutputChannels();
    juce::StringArray mismatched;
    for (const auto& p : audioProcessor.points)
        if (p.sourceChannels > 1 && p.sourceChannels != numOut)
            mismatched.add(p.name + " (" + juce::String(p.sourceChannels) + "ch)");

    if (!mismatched.isEmpty())
    {
        juce::String msg = juce::String(juce::CharPointer_UTF8("\xe2\x9a\xa0\xef\xb8\x8f"))
                           + "  Bus mismatch  |  Output: "
                           + juce::String(numOut) + "ch  |  "
                           + mismatched.joinIntoString(", ");

        juce::Font warningFont(juce::FontOptions(12.0f));
        g.setFont(warningFont);
        int textW = static_cast<int>(warningFont.getStringWidthFloat(msg)) + 24;
        int bannerH = 22;
        int bannerY = getHeight() - bannerH - 8;
        int bannerX = x0;

        g.setColour(juce::Colours::orange.withAlpha(0.85f));
        g.fillRoundedRectangle(static_cast<float>(bannerX), static_cast<float>(bannerY),
                               static_cast<float>(textW), static_cast<float>(bannerH), 4.0f);

        g.setColour(juce::Colours::black);
        g.drawText(msg, bannerX + 8, bannerY, textW - 8, bannerH, juce::Justification::centredLeft);
    }

}

void IrisAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    roomMap.setBounds(area.removeFromLeft(static_cast<int>(area.getWidth() * 0.6)));

    controlPanel.setBounds(area.removeFromTop(185));

    auto listArea = area;
    listenerList.setBounds(listArea.removeFromTop(120));
    listArea.removeFromTop(5);
    irList.setBounds(listArea.removeFromTop(static_cast<int>(listArea.getHeight() * 0.5f)));
    listArea.removeFromTop(5);
    wallList.setBounds(listArea);
}

void IrisAudioProcessorEditor::timerCallback()
{
    // Poll the flag set by the processor's 60Hz physics timer.
    // This keeps the overlay and room map in sync without triggering
    // expensive list rebuilds.
    if (audioProcessor.pendingUIRepaint.exchange(false))
    {
        roomMap.repaint();
        repaint();          // redraws paintOverChildren (weight overlay)
    }
}

void IrisAudioProcessorEditor::updateUI()
{
    // Full structural rebuild — called only when IRs/walls/listeners are added or removed.
    roomMap.repaint();
    controlPanel.update();
    irList.updateContent();
    wallList.updateContent();
    repaint();
}
