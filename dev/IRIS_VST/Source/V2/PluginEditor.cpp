#include "PluginProcessorV2.h"
#include "PluginEditorV2.h"

IrisVSTV2AudioProcessorEditor::IrisVSTV2AudioProcessorEditor (IrisVSTV2AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), roomMap(p), controlPanel(p), irList(p)
{
    addAndMakeVisible(roomMap);
    addAndMakeVisible(controlPanel);
    addAndMakeVisible(irList);
    
    // Register callback
    audioProcessor.onStateChanged = [this] {
        juce::MessageManager::callAsync([this] {
            updateUI();
        });
    };

    setSize (1100, 700);
}

IrisVSTV2AudioProcessorEditor::~IrisVSTV2AudioProcessorEditor()
{
    audioProcessor.onStateChanged = nullptr;
}

void IrisVSTV2AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void IrisVSTV2AudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    // Draw Debug Overlay
    g.setFont(12.0f);
    g.setColour(juce::Colours::white);
    
    auto& neighbors = audioProcessor.currentNearestNeighbors;
    int y = 20;
    int x = 20;
    
    g.drawText("Active IRs:", x, y, 200, 20, juce::Justification::left); y += 20;
    
    float sumW = 0.0f;
    for (auto& p : neighbors) sumW += audioProcessor.smoothedWeights[p.id];
    if (sumW < 0.001f) sumW = 1.0f; // Avoid div/0
    
    for (auto& p : neighbors)
    {
        float w = audioProcessor.smoothedWeights[p.id];
        float normW = w / sumW;
        g.drawText(p.name + ": " + juce::String(normW, 2), x, y, 200, 20, juce::Justification::left);
        
        // Bar
        g.setColour(p.color);
        g.fillRect((float)x + 150, (float)y + 4, normW * 50.0f, 12.0f);
        g.setColour(juce::Colours::white);
        
        y += 20;
        if (y > 300) break;
    }
    
    y += 10;
    bool freeze = audioProcessor.freezeParam->get();
    g.drawText("Freeze: " + juce::String(freeze ? "ON" : "OFF"), x, y, 200, 20, juce::Justification::left); y += 20;
    
    float spread = audioProcessor.spreadParam->get();
    float sigma = 0.05f + 1.5f * spread * spread;
    g.drawText("Spread(Sigma): " + juce::String(sigma, 3), x, y, 200, 20, juce::Justification::left); y += 20;
}

void IrisVSTV2AudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    
    // Split 60% Map, 40% Side Panel
    roomMap.setBounds(area.removeFromLeft(area.getWidth() * 0.6));
    
    // Side Panel: Control Panel (Fixed Height), IR List (Rest)
    controlPanel.setBounds(area.removeFromTop(120)); // Fixed height for controls
    irList.setBounds(area); // Takes remaining space
}

void IrisVSTV2AudioProcessorEditor::updateUI()
{
    roomMap.repaint();
    // controlPanel.updateList(); // Removed
    irList.updateContent();
    repaint(); // For Overlay
}
