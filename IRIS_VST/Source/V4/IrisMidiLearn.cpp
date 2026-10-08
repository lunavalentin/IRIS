#include "IrisMidiLearn.h"

static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

static const char* modeToString (IrisMidiLearn::Mode m)
{
    switch (m)
    {
        case IrisMidiLearn::Mode::toggle:   return "toggle";
        case IrisMidiLearn::Mode::absolute: return "absolute";
        case IrisMidiLearn::Mode::continuous:
        default:                            return "continuous";
    }
}

static IrisMidiLearn::Mode modeFromString (const juce::String& s)
{
    if (s == "toggle")   return IrisMidiLearn::Mode::toggle;
    if (s == "absolute") return IrisMidiLearn::Mode::absolute;
    return IrisMidiLearn::Mode::continuous;
}

IrisMidiLearn::IrisMidiLearn (juce::AudioProcessorValueTreeState& state) : parameters (state) {}

IrisMidiLearn::~IrisMidiLearn()
{
    // Stop the device callbacks before anything they touch goes away.
    for (auto& in : openInputs) in->stop();
    openInputs.clear();
}

bool IrisMidiLearn::isToggleParameter (const juce::RangedAudioParameter* p)
{
    return dynamic_cast<const juce::AudioParameterBool*> (p) != nullptr;
}

juce::String IrisMidiLearn::describe (const Mapping& m)
{
    return "CC " + juce::String (m.cc) + (m.channel == 0 ? " any ch" : " ch " + juce::String (m.channel));
}

// ---------------------------------------------------------------------------
// Producers
// ---------------------------------------------------------------------------

void IrisMidiLearn::handleHostMidi (const juce::MidiBuffer& midi) noexcept
{
    if (! hostMidiEnabled.load()) return;

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (! msg.isController()) continue;

        hostFifo.push ({ (juce::uint8) msg.getChannel(), (juce::uint8) msg.getControllerNumber(),
                         (juce::uint8) msg.getControllerValue(), 1, nowMs() });
    }
}

void IrisMidiLearn::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& msg)
{
    if (! msg.isController()) return;

    const juce::SpinLock::ScopedLockType sl (deviceLock);
    deviceFifo.push ({ (juce::uint8) msg.getChannel(), (juce::uint8) msg.getControllerNumber(),
                       (juce::uint8) msg.getControllerValue(), 0, nowMs() });
}

// ---------------------------------------------------------------------------
// Message thread
// ---------------------------------------------------------------------------

void IrisMidiLearn::processPending()
{
    refreshDevices (false);

    std::vector<Event> events;
    hostFifo.popAll   ([&] (const Event& e) { events.push_back (e); });
    deviceFifo.popAll ([&] (const Event& e) { events.push_back (e); });
    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.timeMs < b.timeMs; });

    for (const auto& e : events)
    {
        // The same controller can arrive twice (host routing + direct device):
        // drop an identical value from the other source within 30 ms.
        const int key = (e.channel << 8) | e.cc;
        if (auto it = lastEvent.find (key); it != lastEvent.end())
            if (it->second.fromHost != e.fromHost && it->second.value == e.value && e.timeMs - it->second.timeMs < 30.0)
                continue;
        lastEvent[key] = e;

        if (learningParam.isNotEmpty())
        {
            auto* p = parameters.getParameter (learningParam);
            Mapping m;
            m.cc      = e.cc;
            m.channel = e.channel;
            m.mode    = isToggleParameter (p) ? Mode::toggle : Mode::continuous;

            {
                const juce::ScopedLock sl (lock);
                // One controller drives one parameter.
                for (auto it = mappings.begin(); it != mappings.end();)
                    it = (it->second.cc == m.cc && (it->second.channel == m.channel || it->second.channel == 0))
                         ? mappings.erase (it) : std::next (it);
                mappings[learningParam] = m;
            }

            aboveHalf[learningParam] = e.value >= 64;
            setStatus ("MIDI learn: " + describe (m) + " -> " + (p != nullptr ? p->getName (32) : learningParam));
            learningParam.clear();
            continue;   // the learning move itself doesn't change the value
        }

        std::vector<std::pair<juce::String, Mapping>> targets;
        {
            const juce::ScopedLock sl (lock);
            for (const auto& [id, m] : mappings)
                if (m.cc == e.cc && (m.channel == 0 || m.channel == e.channel))
                    targets.push_back ({ id, m });
        }

        for (const auto& [id, m] : targets)
            apply (id, m, e.value);
    }

    if (learningParam.isNotEmpty() && nowMs() - learnStartMs > learnTimeoutMs)
    {
        learningParam.clear();
        setStatus ("MIDI learn timed out");
    }

    // Close change gestures once a controller has been idle for a moment, so hosts
    // record MIDI moves as one automation pass (Touch/Latch).
    const double t = nowMs();
    for (auto it = gestureLastMs.begin(); it != gestureLastMs.end();)
    {
        if (t - it->second > gestureIdleMs)
        {
            if (auto* p = parameters.getParameter (it->first)) p->endChangeGesture();
            it = gestureLastMs.erase (it);
        }
        else ++it;
    }
}

void IrisMidiLearn::apply (const juce::String& paramId, const Mapping& m, int value)
{
    auto* p = parameters.getParameter (paramId);
    if (p == nullptr) return;

    const bool isToggle = isToggleParameter (p);
    float newValue = p->getValue();

    if (isToggle && m.mode == Mode::toggle)
    {
        // Momentary pad: flip on each press (rising edge through 64), ignore releases.
        const bool above = value >= 64;
        const bool wasAbove = aboveHalf[paramId];
        aboveHalf[paramId] = above;
        if (! above || wasAbove) return;
        newValue = newValue >= 0.5f ? 0.0f : 1.0f;
    }
    else if (isToggle)
    {
        newValue = value >= 64 ? 1.0f : 0.0f;   // absolute / latching
    }
    else
    {
        newValue = (float) value / 127.0f;      // continuous over the parameter's full range
    }

    if (gestureLastMs.find (paramId) == gestureLastMs.end())
        p->beginChangeGesture();
    gestureLastMs[paramId] = nowMs();

    p->setValueNotifyingHost (newValue);
}

void IrisMidiLearn::startLearning (const juce::String& paramId)
{
    if (parameters.getParameter (paramId) == nullptr) return;
    learningParam = paramId;
    learnStartMs  = nowMs();
    setStatus ("LEARNING... move a control");
}

void IrisMidiLearn::cancelLearning()
{
    if (learningParam.isEmpty()) return;
    learningParam.clear();
    setStatus ("MIDI learn cancelled");
}

void IrisMidiLearn::clearMapping (const juce::String& paramId)
{
    {
        const juce::ScopedLock sl (lock);
        mappings.erase (paramId);
    }
    auto* p = parameters.getParameter (paramId);
    setStatus ("MIDI mapping cleared: " + (p != nullptr ? p->getName (32) : paramId));
}

void IrisMidiLearn::clearAll()
{
    {
        const juce::ScopedLock sl (lock);
        mappings.clear();
    }
    learningParam.clear();
    setStatus ("All MIDI mappings cleared");
}

std::optional<IrisMidiLearn::Mapping> IrisMidiLearn::getMapping (const juce::String& paramId) const
{
    const juce::ScopedLock sl (lock);
    if (auto it = mappings.find (paramId); it != mappings.end()) return it->second;
    return std::nullopt;
}

std::vector<std::pair<juce::String, IrisMidiLearn::Mapping>> IrisMidiLearn::getAllMappings() const
{
    const juce::ScopedLock sl (lock);
    return { mappings.begin(), mappings.end() };
}

void IrisMidiLearn::setMode (const juce::String& paramId, Mode mode)
{
    {
        const juce::ScopedLock sl (lock);
        if (auto it = mappings.find (paramId); it != mappings.end()) it->second.mode = mode;
    }
    uiDirty.store (true);
}

void IrisMidiLearn::setAnyChannel (const juce::String& paramId, bool anyChannel)
{
    {
        const juce::ScopedLock sl (lock);
        if (auto it = mappings.find (paramId); it != mappings.end())
            it->second.channel = anyChannel ? 0 : juce::jmax (1, it->second.channel == 0 ? 1 : it->second.channel);
    }
    uiDirty.store (true);
}

void IrisMidiLearn::setStatus (const juce::String& s)
{
    {
        const juce::ScopedLock sl (lock);
        status = s;
    }
    uiDirty.store (true);
}

juce::String IrisMidiLearn::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

void IrisMidiLearn::setDeviceSelection (const juce::String& selection)
{
    {
        const juce::ScopedLock sl (lock);
        deviceSelection = selection;
    }
    uiDirty.store (true);
}

juce::String IrisMidiLearn::getDeviceSelection() const
{
    const juce::ScopedLock sl (lock);
    return deviceSelection;
}

juce::StringArray IrisMidiLearn::getOpenDeviceNames() const
{
    juce::StringArray names;
    for (auto& in : openInputs) names.add (in->getName());
    return names;
}

void IrisMidiLearn::refreshDevices (bool force)
{
    const auto selection = getDeviceSelection();
    const double t = nowMs();

    if (! force && selection == openedSelection && t - lastDeviceScanMs < deviceRescanMs)
        return;
    lastDeviceScanMs = t;

    juce::StringArray wanted;
    if (selection.isNotEmpty())
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            if (selection == "*" || d.identifier == selection)
                wanted.add (d.identifier);

    if (selection == openedSelection && wanted == openedIds)
        return;   // nothing plugged in or out

    for (auto& in : openInputs) in->stop();
    openInputs.clear();

    juce::StringArray opened, failed;
    for (const auto& id : wanted)
    {
        if (auto in = juce::MidiInput::openDevice (id, this))
        {
            in->start();
            opened.add (in->getName());
            openInputs.push_back (std::move (in));
        }
        else
        {
            failed.add (id);
        }
    }

    const bool firstOpen = openedSelection == "\x01";
    openedSelection = selection;
    openedIds       = wanted;

    if (! failed.isEmpty())
        setStatus ("MIDI input FAILED to open: " + failed.joinIntoString (", "));
    else if (! firstOpen || ! opened.isEmpty())
        setStatus (opened.isEmpty() ? juce::String ("MIDI: no MIDI input devices")
                                    : "MIDI input enabled: " + opened.joinIntoString (", "));
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

std::unique_ptr<juce::XmlElement> IrisMidiLearn::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("MIDI_MAPPINGS");
    const juce::ScopedLock sl (lock);
    xml->setAttribute ("devices", deviceSelection);
    xml->setAttribute ("hostMidi", hostMidiEnabled.load());

    for (const auto& [id, m] : mappings)
    {
        auto* e = xml->createNewChildElement ("MAP");
        e->setAttribute ("param", id);
        e->setAttribute ("cc", m.cc);
        e->setAttribute ("ch", m.channel);
        e->setAttribute ("mode", modeToString (m.mode));
    }
    return xml;
}

void IrisMidiLearn::fromXml (const juce::XmlElement& xml)
{
    std::map<juce::String, Mapping> loaded;
    for (auto* e : xml.getChildWithTagNameIterator ("MAP"))
    {
        const auto id = e->getStringAttribute ("param");
        const int cc  = e->getIntAttribute ("cc", -1);
        const int ch  = e->getIntAttribute ("ch", 0);
        if (parameters.getParameter (id) == nullptr || cc < 0 || cc > 127 || ch < 0 || ch > 16) continue;
        loaded[id] = { cc, ch, modeFromString (e->getStringAttribute ("mode")) };
    }

    {
        const juce::ScopedLock sl (lock);
        mappings = std::move (loaded);
        deviceSelection = xml.getStringAttribute ("devices", "*");
    }
    hostMidiEnabled.store (xml.getBoolAttribute ("hostMidi", true));
    uiDirty.store (true);
}
