#include "Theme.h"

namespace Theme
{
    // A soft dark grey, not pure black
    const juce::Colour backgroundDark       = juce::Colour(0xff1A1A1A);
    const juce::Colour panelBackground      = juce::Colour(0xff222222);
    const juce::Colour cardElevated         = juce::Colour(0xff2A2A2A);

    // Muted teal/cyan accent
    const juce::Colour accentCyan           = juce::Colour(0xff00D8D8);
    const juce::Colour controlBase          = juce::Colour(0xff333333);
    const juce::Colour controlHover         = juce::Colour(0xff444444);
    const juce::Colour controlActive        = juce::Colour(0xff555555);
    const juce::Colour borderMinimal        = juce::Colour(0xff3A3A3A);

    const juce::Colour listenerLocalRed     = juce::Colour(0xffFF4040);
    const juce::Colour listenerRemotePink   = juce::Colour(0xffFF80C0);
    const juce::Colour listenerInactiveGrey = juce::Colour(0xff888888);

    const juce::Colour textPrimary          = juce::Colour(0xffE0E0E0);
    const juce::Colour textSecondary        = juce::Colour(0xffA0A0A0);
    const juce::Colour textMuted            = juce::Colour(0xff606060);

    const juce::Colour gridLineColor        = juce::Colour(0xff2A2A2A);
    const juce::Colour wallLineColor        = juce::Colour(0xff666666);

    juce::Font getBaseFont(float size, int style)
    {
        juce::Font f;
        f.setTypefaceName(juce::Font::getDefaultSansSerifFontName());
        f.setHeight(size);
        f.setStyleFlags(style);
        return f;
    }

    juce::Font getHeadingFont(float size)
    {
        return getBaseFont(size, juce::Font::bold);
    }

    juce::Font getMonospaceFont(float size)
    {
        juce::Font f;
        f.setTypefaceName(juce::Font::getDefaultMonospacedFontName());
        f.setHeight(size);
        return f;
    }
}
