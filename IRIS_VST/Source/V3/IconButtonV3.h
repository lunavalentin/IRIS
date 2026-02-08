#pragma once
#include <JuceHeader.h>

// Custom small icon button
class IconButtonV3 : public juce::Button
{
public:
    enum IconType { Lock, Delete };
    
    IconButtonV3(const juce::String& name, IconType t) : juce::Button(name), type(t) {}
    
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsMouseOver, bool shouldDrawButtonAsDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        
        // Background
        if (shouldDrawButtonAsDown) g.fillAll(juce::Colours::white.withAlpha(0.2f));
        else if (shouldDrawButtonAsMouseOver) g.fillAll(juce::Colours::white.withAlpha(0.1f));
        
        g.setColour(juce::Colours::white);
        if (type == Delete) g.setColour(juce::Colours::red.withSaturation(0.6f));
        if (type == Lock && getToggleState()) g.setColour(juce::Colours::orange);
        
        auto iconArea = bounds.reduced(4.0f);
        
        if (type == Lock)
        {
            juce::Path p;
            float w = iconArea.getWidth();
            float h = iconArea.getHeight();
            bool locked = getToggleState();
            
            // Body
            g.fillRect(iconArea.getX(), iconArea.getY() + h*0.35f, w, h*0.65f);
            
            // Shackle
            juce::Path shackle;
            float shackleW = w * 0.6f;
            float shackleH = h * 0.45f;
            float shackleX = iconArea.getX() + (w - shackleW) * 0.5f;
            
            if (locked)
                shackle.addRoundedRectangle(shackleX, iconArea.getY(), shackleW, shackleH*2.0f, shackleW*0.5f);
            else {
                shackle.addRoundedRectangle(shackleX + shackleW*0.7f, iconArea.getY() - h*0.1f, shackleW, shackleH*2.0f, shackleW*0.5f);
            }
            
            g.strokePath(shackle, juce::PathStrokeType(2.0f));
        }
        else if (type == Delete)
        {
            float w = iconArea.getWidth();
            float h = iconArea.getHeight();
            
            // Lid
            g.drawRect(iconArea.getX(), iconArea.getY(), w, h*0.15f, 1.5f);
            g.drawRect(iconArea.getX() + w*0.3f, iconArea.getY()-2, w*0.4f, 2.0f, 1.5f); // Handle
            
            // Bin
            g.drawRect(iconArea.getX() + w*0.15f, iconArea.getY() + h*0.25f, w*0.7f, h*0.75f, 1.5f);
            
            // Stripes
            g.drawLine(iconArea.getX() + w*0.35f, iconArea.getY() + h*0.4f, iconArea.getX() + w*0.35f, iconArea.getY() + h*0.8f, 1.5f);
            g.drawLine(iconArea.getX() + w*0.65f, iconArea.getY() + h*0.4f, iconArea.getX() + w*0.65f, iconArea.getY() + h*0.8f, 1.5f);
        }
    }
    
private:
    IconType type;
};
