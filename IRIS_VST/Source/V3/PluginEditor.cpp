#include "PluginProcessorV3.h"
#include "PluginEditorV3.h"

IrisVSTV3AudioProcessorEditor::IrisVSTV3AudioProcessorEditor (IrisVSTV3AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), roomMap(p), controlPanel(p), irList(p), wallList(p)
{
    addAndMakeVisible(roomMap);
    addAndMakeVisible(controlPanel);
    addAndMakeVisible(irList);
    addAndMakeVisible(wallList);
    
    // Register callback
    audioProcessor.onStateChanged = [this] {
        juce::MessageManager::callAsync([this] {
            updateUI();
        });
    };

    setSize (1100, 700);
}

IrisVSTV3AudioProcessorEditor::~IrisVSTV3AudioProcessorEditor()
{
    audioProcessor.onStateChanged = nullptr;
}

void IrisVSTV3AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void IrisVSTV3AudioProcessorEditor::paintOverChildren (juce::Graphics& g)
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
    bool freeze = audioProcessor.freezeParam->load();
    g.drawText("Freeze: " + juce::String(freeze ? "ON" : "OFF"), x, y, 200, 20, juce::Justification::left); y += 20;
    
    float spread = audioProcessor.spreadParam->load();
    float sigma = 0.05f + 1.5f * spread * spread;
    g.drawText("Spread(Sigma): " + juce::String(sigma, 3), x, y, 200, 20, juce::Justification::left); y += 20;
}

void IrisVSTV3AudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    
    // Split 60% Map, 40% Side Panel
    roomMap.setBounds(area.removeFromLeft(area.getWidth() * 0.6));
    
    // Side Panel: 
    // Top: Control Panel (150px)
    // Remaining split 50/50 IR List / Wall List
    controlPanel.setBounds(area.removeFromTop(150));
    
    auto listArea = area;
    irList.setBounds(listArea.removeFromTop(listArea.getHeight() * 0.6)); // Give IRs slightly more space
    wallList.setBounds(listArea);
}

void IrisVSTV3AudioProcessorEditor::updateUI()
{
    roomMap.repaint();
    controlPanel.update();
    irList.updateContent();
    wallList.updateContent();
    repaint(); // For Overlay
}
