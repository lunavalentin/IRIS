#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

// ---------------------------------------------------------------------------
// MIDI learn UI helpers (menu wording follows Aura)
// ---------------------------------------------------------------------------

// Wraps a Slider/ToggleButton so a right-click opens the MIDI learn menu instead
// of moving or toggling the control.
template <class Base>
class MidiLearnable : public Base
{
public:
    using Base::Base;

    juce::String paramId;
    std::function<void()> onMidiMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onMidiMenu) onMidiMenu(); return; }
        Base::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        Base::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        Base::mouseUp (e);
    }
};

namespace MidiLearnUI
{
    // The learn/clear/mode menu for one parameter.
    inline juce::PopupMenu buildMenu (IrisAudioProcessor& p, const juce::String& paramId)
    {
        auto& ml = p.midiLearn;
        juce::PopupMenu m;

        auto* param = p.parameters.getParameter (paramId);
        const auto mapping = ml.getMapping (paramId);
        const auto name = param != nullptr ? param->getName (32) : paramId;

        m.addSectionHeader ("MIDI: " + name + (mapping ? "  (" + IrisMidiLearn::describe (*mapping) + ")" : juce::String()));

        const auto learning = ml.getLearningParam();
        if (learning == paramId)
            m.addItem ("Cancel MIDI Learn", [&ml] { ml.cancelLearning(); });
        else
            m.addItem (learning.isEmpty() ? "MIDI Learn" : "MIDI Learn (cancels current learn)",
                       [&ml, paramId] { ml.startLearning (paramId); });

        if (mapping)
        {
            m.addItem ("Clear MIDI Mapping", [&ml, paramId] { ml.clearMapping (paramId); });
            m.addItem ("Respond on any MIDI channel", true, mapping->channel == 0,
                       [&ml, paramId, any = mapping->channel == 0] { ml.setAnyChannel (paramId, ! any); });

            if (IrisMidiLearn::isToggleParameter (param))
            {
                m.addSeparator();
                m.addItem ("Toggle on each press (momentary pad)", true, mapping->mode == IrisMidiLearn::Mode::toggle,
                           [&ml, paramId] { ml.setMode (paramId, IrisMidiLearn::Mode::toggle); });
                m.addItem ("Absolute (latching controller: >=64 on)", true, mapping->mode == IrisMidiLearn::Mode::absolute,
                           [&ml, paramId] { ml.setMode (paramId, IrisMidiLearn::Mode::absolute); });
            }
        }

        return m;
    }

    inline void showMenu (IrisAudioProcessor& p, const juce::String& paramId, juce::Component& target)
    {
        buildMenu (p, paramId).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
    }

    // Orange "LEARNING..." frame while this control waits for a controller, or a
    // small CC badge when it is mapped. Call from the parent's paintOverChildren.
    inline void paintState (juce::Graphics& g, IrisAudioProcessor& p, const juce::String& paramId,
                            juce::Rectangle<int> bounds)
    {
        auto& ml = p.midiLearn;

        if (ml.getLearningParam() == paramId)
        {
            const float pulse = 0.55f + 0.45f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f);
            g.setColour (juce::Colours::orange.withAlpha (pulse));
            g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 4.0f, 2.0f);

            g.setFont (Theme::getBaseFont (9.0f));
            auto tag = bounds.removeFromTop (12).removeFromRight (64);
            g.setColour (juce::Colours::orange.withAlpha (0.9f));
            g.fillRoundedRectangle (tag.toFloat(), 3.0f);
            g.setColour (juce::Colours::black);
            g.drawText ("LEARNING...", tag, juce::Justification::centred);
        }
        else if (auto m = ml.getMapping (paramId))
        {
            g.setFont (Theme::getBaseFont (9.0f));
            const juce::String text = "CC" + juce::String (m->cc);
            auto tag = bounds.removeFromTop (11).removeFromRight (34);
            g.setColour (Theme::accentCyan.withAlpha (0.85f));
            g.fillRoundedRectangle (tag.toFloat(), 3.0f);
            g.setColour (juce::Colours::black);
            g.drawText (text, tag, juce::Justification::centred);
        }
    }
}
