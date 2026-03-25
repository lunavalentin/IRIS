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
    // Draw Active IR Overlay (Top Left)
    g.setFont(12.0f);
    
    auto& neighbors = audioProcessor.currentNearestNeighbors;
    int y = 20;
    int x = 20;

    g.setColour(juce::Colours::white);
    g.drawText("Active IRs (Gain Factor):", x, y, 200, 20, juce::Justification::left); y += 20;
    
    float sumW = 0.0f;
    for (auto& p : neighbors) sumW += audioProcessor.smoothedWeights[p.id];
    
    // Dynamic Mix Factor
    float mix = audioProcessor.mixParam->load();
    float totalW = sumW; 
    float dynamicFade = juce::jlimit(0.0f, 1.0f, totalW);
    
    if (sumW < 0.001f) sumW = 1.0f; // Avoid div/0 for normalization
    
    for (auto& p : neighbors)
    {
        float w = audioProcessor.smoothedWeights[p.id];
        float normW = w / sumW; // Normalized contribution (0..1)
        
        // Layout: Stacked v3.1.1
        // Line 1: Name
        // Line 2: Value | Bar
        
        // Actual Multiplying Factor = NormW * Mix * DynamicFade
        float actualFactor = normW * mix * dynamicFade;
        
        // Color code based on IR color
        g.setColour(p.color); 
        
        // Line 1: Name
        g.drawText(p.name, x, y, 200, 15, juce::Justification::left);
        
        // Line 2: Value + Bar
        // Value width ~ 40px
        g.drawText(juce::String(actualFactor, 3), x, y + 16, 40, 10, juce::Justification::left);
        
        // Bar starts after value (x + 45)
        float barMaxLen = 100.0f;
        g.fillRect((float)x + 45, (float)y + 18, actualFactor * barMaxLen, 6.0f);
        
        y += 30; // Spacing
        if (y > 300) break;
    }
    
    // Version Check
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawText("v3.1.1", x, y, 50, 10, juce::Justification::left);
}

void IrisVSTV3AudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    
    // Split 60% Map, 40% Side Panel
    roomMap.setBounds(area.removeFromLeft(area.getWidth() * 0.6));
    
    // Side Panel: 
    // Top: Control Panel (3 Rows -> ~150px)
    controlPanel.setBounds(area.removeFromTop(150));
    
    // List Area
    auto listArea = area;
    
    // Top part: IR List (Listener + List)
    // Bottom part: Wall List (Header + List)
    // User asked "Walls... At 2/3".
    // 2/3 down the list area? Or IRs take 2/3?
    // "List 2 Walls... At 2/3" implies IRs take first 2/3.
    
    irList.setBounds(listArea.removeFromTop(listArea.getHeight() * 0.66f)); 
    listArea.removeFromTop(5);
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
