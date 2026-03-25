#pragma once
#include <JuceHeader.h>

namespace Theme
{
    // Background and Panel Colors
    extern const juce::Colour backgroundDark;
    extern const juce::Colour panelBackground;
    extern const juce::Colour cardElevated;
    
    // UI Elements
    extern const juce::Colour accentCyan;
    extern const juce::Colour controlBase;
    extern const juce::Colour controlHover;
    extern const juce::Colour controlActive;
    extern const juce::Colour borderMinimal;

    // Listeners and Semantic
    extern const juce::Colour listenerLocalRed;
    extern const juce::Colour listenerRemotePink;
    extern const juce::Colour listenerInactiveGrey;

    // Typography
    extern const juce::Colour textPrimary;
    extern const juce::Colour textSecondary;
    extern const juce::Colour textMuted;

    // Spatial Map
    extern const juce::Colour gridLineColor;
    extern const juce::Colour wallLineColor;
    
    // Dimensions
    constexpr float cornerRadius = 4.0f;
    constexpr float cardPadding = 12.0f;
    constexpr float borderThickness = 1.0f;
    constexpr float rowHeight = 32.0f;
    
    // Fonts
    juce::Font getBaseFont(float size = 14.0f, int style = juce::Font::plain);
    juce::Font getHeadingFont(float size = 16.0f);
    juce::Font getMonospaceFont(float size = 13.0f);
}
