#include "IrisLookAndFeel.h"

IrisLookAndFeel::IrisLookAndFeel()
{
    // Define some default JUCE colors if needed, but we will mostly override paint methods
    setColour(juce::TextButton::buttonColourId, Theme::controlBase);
    setColour(juce::TextButton::buttonOnColourId, Theme::accentCyan);
    setColour(juce::TextButton::textColourOnId, Theme::textPrimary);
    setColour(juce::TextButton::textColourOffId, Theme::textPrimary);
    
    setColour(juce::ComboBox::backgroundColourId, Theme::controlBase);
    setColour(juce::ComboBox::textColourId, Theme::textPrimary);
    setColour(juce::ComboBox::arrowColourId, Theme::textSecondary);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentWhite);
    
    setColour(juce::PopupMenu::backgroundColourId, Theme::cardElevated);
    setColour(juce::PopupMenu::textColourId, Theme::textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::controlHover);
    setColour(juce::PopupMenu::highlightedTextColourId, Theme::textPrimary);

    setColour(juce::Slider::thumbColourId, Theme::accentCyan);
    setColour(juce::Slider::trackColourId, Theme::controlBase);
    setColour(juce::Slider::backgroundColourId, Theme::controlBase.darker());
    
    setColour(juce::Label::textColourId, Theme::textPrimary);
}

juce::Font IrisLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return Theme::getBaseFont(13.0f, juce::Font::bold);
}

juce::Font IrisLookAndFeel::getLabelFont(juce::Label&)
{
    return Theme::getBaseFont(14.0f, juce::Font::plain);
}

juce::Font IrisLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return Theme::getBaseFont(13.0f, juce::Font::plain);
}

juce::Font IrisLookAndFeel::getPopupMenuFont()
{
    return Theme::getBaseFont(13.0f, juce::Font::plain);
}

void IrisLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                           const juce::Colour& backgroundColour,
                                           bool isMouseOverButton,
                                           bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    bounds.reduce(0.5f, 0.5f);
    
    juce::Colour baseColour = backgroundColour;
    if (isButtonDown || button.getToggleState())
        baseColour = Theme::controlActive;
    else if (isMouseOverButton)
        baseColour = Theme::controlHover;
    
    g.setColour(baseColour);
    g.fillRoundedRectangle(bounds, Theme::cornerRadius);
    
    g.setColour(Theme::borderMinimal);
    g.drawRoundedRectangle(bounds, Theme::cornerRadius, Theme::borderThickness);
}

void IrisLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                     bool isMouseOverButton, bool isButtonDown)
{
    juce::Font font(getTextButtonFont(button, button.getHeight()));
    g.setFont(font);
    
    juce::Colour textColour = button.findColour(button.getToggleState() 
                                              ? juce::TextButton::textColourOnId 
                                              : juce::TextButton::textColourOffId)
                                    .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);
                                    
    g.setColour(textColour);
    
    const int yIndent = std::min(1, button.proportionOfHeight(0.1f));
    const int cornerSize = juce::jmin(button.getHeight(), button.getWidth()) / 2;
    const int leftIndent = juce::jmin((int)font.getHeight(), cornerSize) / (button.isConnectedOnLeft() ? 4 : 2);
    const int rightIndent = juce::jmin((int)font.getHeight(), cornerSize) / (button.isConnectedOnRight() ? 4 : 2);

    juce::Rectangle<int> textBounds(leftIndent, yIndent,
                                  button.getWidth() - leftIndent - rightIndent,
                                  button.getHeight() - yIndent * 2);

    g.drawFittedText(button.getButtonText(), textBounds,
                     juce::Justification::centred, 2);
}

void IrisLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                       bool isMouseOverButton, bool isButtonDown)
{
    auto fontSize = 14.0f;
    auto tickWidth = fontSize * 1.1f;
    
    auto bounds = button.getLocalBounds().toFloat();
    auto tickBounds = juce::Rectangle<float>(bounds.getX(), 
                                           bounds.getCentreY() - tickWidth * 0.5f, 
                                           tickWidth, tickWidth);
                                           
    g.setColour(Theme::controlBase);
    g.fillRoundedRectangle(tickBounds, 2.0f);
    
    if (isMouseOverButton)
    {
        g.setColour(Theme::controlHover);
        g.drawRoundedRectangle(tickBounds, 2.0f, 1.0f);
    }
    else
    {
        g.setColour(Theme::borderMinimal);
        g.drawRoundedRectangle(tickBounds, 2.0f, 1.0f);
    }
    
    if (button.getToggleState())
    {
        g.setColour(Theme::accentCyan);
        auto check = tickBounds.reduced(3.0f);
        
        juce::Path p;
        p.startNewSubPath(check.getX(), check.getCentreY());
        p.lineTo(check.getCentreX() - 1.0f, check.getBottom() - 1.0f);
        p.lineTo(check.getRight(), check.getY() + 1.0f);
        g.strokePath(p, juce::PathStrokeType(2.0f));
    }
    
    g.setColour(Theme::textPrimary.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.setFont(Theme::getBaseFont(fontSize));
    g.drawFittedText(button.getButtonText(),
                     button.getLocalBounds().withTrimmedLeft(tickBounds.getRight() + 6).toNearestInt(),
                     juce::Justification::centredLeft, 10);
}

void IrisLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                   int buttonX, int buttonY, int buttonW, int buttonH,
                                   juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    
    g.setColour(Theme::controlBase);
    g.fillRoundedRectangle(bounds, Theme::cornerRadius);
    
    if (isButtonDown)
        g.setColour(Theme::accentCyan);
    else
        g.setColour(Theme::borderMinimal);
        
    g.drawRoundedRectangle(bounds.reduced(0.5f), Theme::cornerRadius, Theme::borderThickness);
    
    // Draw dropdown arrow
    juce::Path p;
    float arrowW = 8.0f;
    float arrowH = 5.0f;
    float ax = width - 16.0f;
    float ay = height * 0.5f - arrowH * 0.5f;
    
    p.addTriangle(ax, ay, ax + arrowW, ay, ax + arrowW * 0.5f, ay + arrowH);
    g.setColour(Theme::textSecondary);
    g.fillPath(p);
}

void IrisLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(1, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

void IrisLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(Theme::cardElevated);
    g.fillRoundedRectangle(bounds, Theme::cornerRadius);
    g.setColour(Theme::borderMinimal);
    g.drawRoundedRectangle(bounds.reduced(0.5f), Theme::cornerRadius, Theme::borderThickness);
}

void IrisLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive, bool isHighlighted,
                                        bool isTicked, bool hasSubMenu,
                                        const juce::String& text, const juce::String& shortcutKeyText,
                                        const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        auto r = area.reduced(5, 0);
        r.removeFromTop(juce::roundToInt(r.getHeight() * 0.5f) - 1);
        g.setColour(Theme::borderMinimal);
        g.fillRect(r.removeFromTop(1));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour(Theme::controlHover);
        g.fillRoundedRectangle(area.reduced(2).toFloat(), Theme::cornerRadius);
    }
    
    g.setColour(textColour != nullptr ? *textColour : Theme::textPrimary);
    g.setFont(getPopupMenuFont());
    
    auto r = area.reduced(10, 0);
    if (isTicked)
    {
        // Draw tick
        juce::Path p;
        auto tickArea = r.removeFromLeft(15).reduced(2).toFloat();
        p.startNewSubPath(tickArea.getX(), tickArea.getCentreY());
        p.lineTo(tickArea.getCentreX() - 1.0f, tickArea.getBottom() - 1.0f);
        p.lineTo(tickArea.getRight(), tickArea.getY() + 1.0f);
        g.strokePath(p, juce::PathStrokeType(2.0f));
    }
    
    g.drawFittedText(text, r, juce::Justification::centredLeft, 1);
}

void IrisLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float minSliderPos, float maxSliderPos,
                                       const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (slider.isBar())
    {
        g.setColour(Theme::controlBase);
        g.fillRoundedRectangle(x, y, width, height, Theme::cornerRadius);
        
        if (sliderPos > minSliderPos)
        {
            g.setColour(Theme::accentCyan.withAlpha(0.3f));
            g.fillRoundedRectangle(x, y, sliderPos - minSliderPos, height, Theme::cornerRadius);
        }
    }
    else
    {
        auto isHorizontal = slider.isHorizontal();
        auto trackW = isHorizontal ? width : 4.0f;
        auto trackH = isHorizontal ? 4.0f : height;
        auto trackX = isHorizontal ? x : x + width * 0.5f - 2.0f;
        auto trackY = isHorizontal ? y + height * 0.5f - 2.0f : y;

        juce::Rectangle<float> trackBounds(trackX, trackY, trackW, trackH);
        
        g.setColour(Theme::controlBase);
        g.fillRoundedRectangle(trackBounds, 2.0f);
        
        juce::Rectangle<float> fillBounds;
        if (isHorizontal)
             fillBounds = juce::Rectangle<float>(x, trackY, std::max(0.0f, sliderPos - x), trackH);
        else
             fillBounds = juce::Rectangle<float>(trackX, sliderPos, trackW, std::max(0.0f, (float)(height + y) - sliderPos));
             
        g.setColour(Theme::accentCyan);
        g.fillRoundedRectangle(fillBounds, 2.0f);
        
        // Thumb
        float thumbRadius = 6.0f;
        float thumbX = isHorizontal ? sliderPos - thumbRadius : x + width * 0.5f - thumbRadius;
        float thumbY = isHorizontal ? y + height * 0.5f - thumbRadius : sliderPos - thumbRadius;
        
        g.setColour(Theme::accentCyan);
        g.fillEllipse(thumbX, thumbY, thumbRadius * 2, thumbRadius * 2);
    }
}

void IrisLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider& slider)
{
    auto radius = (float) juce::jmin(width / 2, height / 2) - 2.0f;
    auto centreX = (float) x + (float) width * 0.5f;
    auto centreY = (float) y + (float) height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;

    // outline
    g.setColour(Theme::controlBase);
    g.drawEllipse(rx, ry, rw, rw, 2.0f);
    
    // arc
    auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    juce::Path arc;
    arc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour(Theme::accentCyan);
    g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    
    // pointer
    juce::Path p;
    p.addRectangle(-1.0f, -radius + 2.0f, 2.0f, radius * 0.4f);
    p.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(Theme::textPrimary);
    g.fillPath(p);
}
