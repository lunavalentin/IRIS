#pragma once

#include <JuceHeader.h>

class IrisOSCManager;

// ---------------------------------------------------------------------------
// Data Structures
// ---------------------------------------------------------------------------

struct ActiveIR
{
    std::vector<std::shared_ptr<juce::dsp::Convolution>> convolvers;
    float weight        = 0.0f;
    int   sourceChannels = 0;   // original IR channel count (1 = mono, N = multi-ch)
    juce::Uuid id;
};

struct RenderState
{
    std::vector<ActiveIR> activeIRs;
    float totalWeight = 0.0f;
};

struct IRPoint
{
    juce::Uuid   id;
    juce::String name;
    juce::Colour color;
    float x = 0.5f;
    float y = 0.5f;

    juce::File   sourceFile;
    bool         locked      = false;
    double       sampleRate  = 0.0;
    int          sourceChannels = 0;

    std::shared_ptr<juce::AudioBuffer<float>> rawBuffer;    // Unmodified audio as read from disk
    std::shared_ptr<juce::AudioBuffer<float>> sourceBuffer; // Processed (aligned + normalized) buffer

    std::vector<std::shared_ptr<juce::dsp::Convolution>> convolvers;

    float normGain    = 1.0f;
    int   onsetOffset = 0;

    // Debug data exposed to the UI overlay
    float debug_rawWeight        = 0.0f;
    float debug_occlusionFactor  = 1.0f;
    float debug_finalWeight      = 0.0f;
    int   debug_intersectionCount = 0;
};

struct OcclusionWall
{
    juce::Uuid   id;
    juce::String name;
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 1.0f, y2 = 0.0f;
    bool  locked      = false;
    float attenuation = 0.5f;   // 0 = fully blocking, 1 = transparent

    juce::Colour color = juce::Colours::cyan;

    float getDistanceToPoint(float px, float py) const
    {
        float l2 = (x1-x2)*(x1-x2) + (y1-y2)*(y1-y2);
        if (l2 < 1e-10f)
            return std::sqrt((px-x1)*(px-x1) + (py-y1)*(py-y1));

        float t = ((px-x1)*(x2-x1) + (py-y1)*(y2-y1)) / l2;
        t = juce::jlimit(0.0f, 1.0f, t);

        float projX = x1 + t * (x2-x1);
        float projY = y1 + t * (y2-y1);

        return std::sqrt((px-projX)*(px-projX) + (py-projY)*(py-projY));
    }
};

// ---------------------------------------------------------------------------
// Processor
// ---------------------------------------------------------------------------

class IrisAudioProcessor : public juce::AudioProcessor,
                           public juce::Timer,
                           public juce::AudioProcessorValueTreeState::Listener
{
    friend class IrisOSCManager;

public:
    IrisAudioProcessor();
    ~IrisAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool  acceptsMidi() const override;
    bool  producesMidi() const override;
    bool  isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Parameters
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters;

    // IR and Wall state (guarded by stateLock)
    std::vector<IRPoint>      points;
    std::vector<OcclusionWall> walls;
    juce::Uuid selectedWallId;
    juce::Uuid selectedIRId;

    // Network listener model
    struct NetworkListener
    {
        juce::Uuid   id;
        juce::String name;
        float x        = 0.5f;
        float y        = 0.5f;
        float currentX = 0.5f;
        float currentY = 0.5f;
        bool  isLocal  = false;
        bool  locked   = false;
    };

    NetworkListener localAudioListener;
    std::map<juce::Uuid, NetworkListener> remoteListeners;
    juce::Uuid selectedListenerId;

    // Link matrix — symmetric edge set for listener movement coupling
    std::set<std::pair<juce::String, juce::String>> linkMatrix;
    void toggleLinkMatrix(juce::Uuid id1, juce::Uuid id2, bool broadcast = true);
    void setLinkMatrixConnections(const std::vector<std::pair<juce::String, juce::String>>& edges);

    juce::CriticalSection stateLock;

    // IR management
    juce::Uuid addIRFromFile(const juce::File& file);
    void addIRFromFileWithID(const juce::File& file, juce::Uuid id);
    void addIRPoint(const juce::String& name);
    void removePoint(juce::Uuid id, bool broadcast = true);
    void updatePointPosition(juce::Uuid id, float x, float y, bool broadcast = true);
    void setPointLocked(juce::Uuid id, bool locked, bool broadcast = true);
    void setPointName(juce::Uuid id, const juce::String& name, bool broadcast = true);

    // Wall management
    juce::Uuid addWall(float x1, float y1, float x2, float y2, bool broadcast = true);
    void addWallWithID(juce::Uuid id, float x1, float y1, float x2, float y2);
    void removeWall(juce::Uuid id, bool broadcast = true);
    void updateWall(juce::Uuid id, float x1, float y1, float x2, float y2, bool broadcast = true);
    void constrainPointToWalls(float& x, float& y);

    // Listener management
    void updateListenerPosition(juce::Uuid id, float x, float y, bool broadcast = true);
    void setListenerLocked(juce::Uuid id, bool locked, bool broadcast = true);
    void requestFullOSCSync();

    // Layout persistence
    void loadLayoutFromJSON(const juce::File& file);
    void saveLayoutToJSON(const juce::File& file);

    // IR processing
    void updateConvolver(IRPoint& p);
    void reprocessIRPoints();
    void updateWeightsGaussian();

    // Weight smoothing and render pipeline
    void timerCallback() override;
    void sendWeightsOSC();

    // Parameter sync helpers
    void updateParameterNotifiers(juce::String paramId, float value);
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // OSC broadcast flags
    bool broadcastListener = true;
    bool broadcastIRs      = true;
    bool broadcastWalls    = true;

    // Per-parameter broadcast flags
    bool broadcastInertia     = true;
    bool broadcastFreeze      = true;
    bool broadcastSpread      = true;
    bool broadcastMix         = false;   // OFF by default — mix is usually per-instance
    bool broadcastWallOpacity = true;
    bool broadcastNormalize   = true;
    bool broadcastAlign       = true;

    std::atomic<bool> isUpdatingFromOSC { false };

    // Set by the processor timer when display data (weights, positions) has changed.
    // The editor polls this at its own repaint rate rather than being pushed at 60Hz.
    std::atomic<bool> pendingUIRepaint { false };

    // Structural change notification — called on the message thread.
    // Use only for add/remove IR/wall/listener, NOT for every weight update.
    std::function<void()> onStateChanged;

    // Exposed for the UI overlay and OSC weight output
    std::vector<IRPoint> currentNearestNeighbors;

    // Smoothing state
    std::map<juce::Uuid, float> targetWeights;
    std::map<juce::Uuid, float> smoothedWeights;
    std::set<juce::Uuid>        activeIDs;

    // Hysteresis thresholds
    float tauIn  = 0.10f;
    float tauOut = 0.05f;

    // APVTS parameter pointers
    juce::AudioParameterFloat* weight1 = nullptr;
    juce::AudioParameterFloat* weight2 = nullptr;
    juce::AudioParameterFloat* weight3 = nullptr;

    std::atomic<float>* inertiaParam     = nullptr;
    std::atomic<float>* freezeParam      = nullptr;
    std::atomic<float>* spreadParam      = nullptr;
    std::atomic<float>* mixParam         = nullptr;
    std::atomic<float>* wallOpacityParam = nullptr;
    std::atomic<float>* normalizeParam   = nullptr;
    std::atomic<float>* alignParam       = nullptr;
    std::atomic<float>* outputGainParam  = nullptr;
    std::atomic<float>* listenerXParam   = nullptr;
    std::atomic<float>* listenerYParam   = nullptr;

    // Prevents feedback loop when syncing listener position to/from parameter
    std::atomic<bool> isUpdatingListenerFromParam { false };

    // Audio format manager
    juce::AudioFormatManager formatManager;

    // DSP process spec (set in prepareToPlay, used by updateConvolver)
    juce::dsp::ProcessSpec processSpec;

    // Benchmark helpers
    void loadDummyIRs(int count, int lengthSamples = 48000);
    int  maxActiveOverride = -1;
    bool isBenchmarking    = false;

    // Cached tail length — updated whenever IRs are added/removed/reprocessed.
    std::atomic<double> cachedTailSeconds { 0.0 };
    void recalcTailLength();

private:
    // Render pipeline
    std::shared_ptr<RenderState> renderState;
    std::shared_ptr<RenderState> prevRenderState;   // held on message thread to prevent free() on audio thread

    // Audio buffers — pre-allocated in prepareToPlay, never resized on the audio thread.
    juce::AudioBuffer<float> inputBuffer;           // capture of the incoming block
    juce::AudioBuffer<float> mixBuffer;             // per-channel convolution scratch

    // Per-IR scratch buffers for parallel convolution.
    // Index matches the active IR index in RenderState::activeIRs.
    static constexpr int kMaxParallelIRs = 64;
    std::array<juce::AudioBuffer<float>, kMaxParallelIRs> irScratchBuffers;

    // Thread pool for parallel convolution across active IRs.
    // Uses numCPUs-1 threads, capped at 8 to leave headroom for REAPER.
    juce::ThreadPool convolutionPool { std::max(1, std::min(8, juce::SystemStats::getNumCpus() - 1)) };

    // Parallel convolution barrier — pre-allocated atomic counter.
    std::atomic<int> convJobsRemaining { 0 };
    juce::WaitableEvent convJobsDone { true /* manualReset */ };

    IrisOSCManager& oscManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrisAudioProcessor)
};
