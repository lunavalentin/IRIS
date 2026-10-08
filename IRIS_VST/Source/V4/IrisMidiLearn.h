#pragma once

#include <JuceHeader.h>

// ---------------------------------------------------------------------------
// MIDI learn (modelled on Aura's MidiLearnManager)
//
// Right-click a control -> "MIDI Learn" -> move a controller: the next CC that
// arrives is bound to that parameter. CCs come from MIDI devices opened directly
// (works in any host, including ones that don't route MIDI to plugins) and from
// the host's MIDI input when the plugin format provides one.
//
// Threading: the device callback and the audio thread only push raw CC events
// into lock-free FIFOs. Everything else (learning, mapping, parameter changes,
// opening devices) runs on the message thread in processPending(), which the
// processor calls from its 60 Hz timer.
// ---------------------------------------------------------------------------

class IrisMidiLearn : private juce::MidiInputCallback
{
public:
    enum class Mode { continuous, toggle, absolute };

    struct Mapping
    {
        int  cc      = -1;
        int  channel = 0;              // 1..16, or 0 = any channel
        Mode mode    = Mode::continuous;
    };

    explicit IrisMidiLearn (juce::AudioProcessorValueTreeState& state);
    ~IrisMidiLearn() override;

    // --- Audio thread ---
    void handleHostMidi (const juce::MidiBuffer& midi) noexcept;

    // --- Message thread ---
    void processPending();

    void startLearning (const juce::String& paramId);
    void cancelLearning();
    juce::String getLearningParam() const   { return learningParam; }

    void clearMapping (const juce::String& paramId);
    void clearAll();
    std::optional<Mapping> getMapping (const juce::String& paramId) const;
    std::vector<std::pair<juce::String, Mapping>> getAllMappings() const;
    void setMode (const juce::String& paramId, Mode mode);
    void setAnyChannel (const juce::String& paramId, bool anyChannel);

    // Device input: "*" = all inputs, "" = none, otherwise a device identifier.
    void setDeviceSelection (const juce::String& selection);
    juce::String getDeviceSelection() const;
    juce::StringArray getOpenDeviceNames() const;

    void setHostMidiEnabled (bool shouldBeEnabled) { hostMidiEnabled.store (shouldBeEnabled); }
    bool isHostMidiEnabled() const                 { return hostMidiEnabled.load(); }

    // Status line for the UI ("MIDI learn: CC 30 ch 1 -> Spread", "MIDI learn timed out", ...).
    juce::String getStatus() const;

    // Set whenever learning state, mappings or status change; the editor polls it.
    std::atomic<bool> uiDirty { true };

    // Persistence (safe from any thread).
    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement& xml);

    static bool isToggleParameter (const juce::RangedAudioParameter* p);
    static juce::String describe (const Mapping& m);

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void refreshDevices (bool force);
    void apply (const juce::String& paramId, const Mapping& m, int value);
    void setStatus (const juce::String& s);

    struct Event { juce::uint8 channel = 0, cc = 0, value = 0, fromHost = 0; double timeMs = 0.0; };

    template <size_t N>
    struct Fifo
    {
        juce::AbstractFifo fifo { (int) N };
        std::array<Event, N> events;

        bool push (const Event& e) noexcept
        {
            const auto scope = fifo.write (1);
            if (scope.blockSize1 > 0) { events[(size_t) scope.startIndex1] = e; return true; }
            return false;
        }

        template <typename Fn> void popAll (Fn&& fn)
        {
            const auto scope = fifo.read (fifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i) fn (events[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i) fn (events[(size_t) (scope.startIndex2 + i)]);
        }
    };

    juce::AudioProcessorValueTreeState& parameters;

    Fifo<512> hostFifo;      // single producer: audio thread
    Fifo<512> deviceFifo;    // producers: CoreMIDI callbacks (serialised by deviceLock)
    juce::SpinLock deviceLock;

    std::atomic<bool> hostMidiEnabled { true };

    mutable juce::CriticalSection lock;                 // guards mappings, selection and status
    std::map<juce::String, Mapping> mappings;
    juce::String deviceSelection { "*" };
    juce::String status;

    // Message-thread state
    juce::String learningParam;
    double learnStartMs = 0.0;
    std::vector<std::unique_ptr<juce::MidiInput>> openInputs;
    juce::String openedSelection { "\x01" };            // forces the first refresh
    juce::StringArray openedIds;
    double lastDeviceScanMs = 0.0;
    std::map<juce::String, bool>   aboveHalf;           // toggle mode: edge detection
    std::map<juce::String, double> gestureLastMs;       // open change gestures
    std::map<int, Event>           lastEvent;           // (ch << 8 | cc) -> last event, for host/device de-duplication

    static constexpr double learnTimeoutMs  = 10000.0;
    static constexpr double gestureIdleMs   = 300.0;
    static constexpr double deviceRescanMs  = 2000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrisMidiLearn)
};
