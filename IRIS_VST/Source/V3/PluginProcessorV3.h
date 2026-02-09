#pragma once

#include <JuceHeader.h>

// Active IR for the Render Graph (Thread-Safe snapshot)
struct ActiveIR {
    std::shared_ptr<juce::dsp::Convolution> convolver;
    float weight;
    juce::Uuid id;
};

// Render State: A snapshot of what to process on the audio thread
struct RenderState {
    std::vector<ActiveIR> activeIRs;
    float totalWeight = 0.0f;
};

struct IRPoint {
    juce::Uuid id;
    juce::String name;
    juce::Colour color;
    float x; // Normalized 0.0 - 1.0
    float y; // Normalized 0.0 - 1.0
    bool isListener;
    
    // New fields for file loading and locking
    juce::File sourceFile;
    bool locked = false;
    double sampleRate = 0.0;
    std::shared_ptr<juce::AudioBuffer<float>> audioData; // Processed (stereo) for convolver
    std::shared_ptr<juce::AudioBuffer<float>> sourceBuffer; // Original raw file
    int sourceChannels = 0;
    int selectedChannel = 0;
    
    // Convolution Engine (Lazy loaded)
    
    // Convolution Engine (Lazy loaded)
    // Convolution Engine (Lazy loaded)
    std::shared_ptr<juce::dsp::Convolution> convolver;
    
    // V2 Data
    float normGain = 1.0f;
    int onsetOffset = 0;
    
    // Debug Data (for UI)
    float debug_rawWeight = 0.0f;
    float debug_occlusionFactor = 1.0f;
    float debug_finalWeight = 0.0f;
    int debug_intersectionCount = 0;
};

struct OcclusionWall
{
    juce::Uuid id;
    juce::String name;
    float x1, y1; // Start point (normalized 0-1)
    float x2, y2; // End point (normalized 0-1)
    bool locked = false;
    float attenuation = 0.5f; // 0.0 = full block, 1.0 = transparent
    
    // UI Helpers
    juce::Colour color = juce::Colours::cyan;
    
    // For hit testing
    float getDistanceToPoint(float px, float py) const
    {
        // Distance from point (px,py) to line segment (x1,y1)-(x2,y2)
        float l2 = (x1-x2)*(x1-x2) + (y1-y2)*(y1-y2);
        if (l2 == 0) return std::sqrt((px-x1)*(px-x1) + (py-y1)*(py-y1));
        
        float t = ((px-x1)*(x2-x1) + (py-y1)*(y2-y1)) / l2;
        t = juce::jlimit(0.0f, 1.0f, t);
        
        float projx = x1 + t * (x2-x1);
        float projy = y1 + t * (y2-y1);
        
        return std::sqrt((px-projx)*(px-projx) + (py-projy)*(py-projy));
    }
};

class IrisVSTV3AudioProcessor  : public juce::AudioProcessor,
                               public juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                               public juce::Timer
{
public:
    IrisVSTV3AudioProcessor();
    ~IrisVSTV3AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters;

    // Custom Data
    std::vector<IRPoint> points;
    std::vector<OcclusionWall> walls;
    juce::Uuid selectedWallId; // Selection state
    juce::Uuid selectedIRId;   // Selection state for IRs
    juce::CriticalSection stateLock; // Protects points, walls, and other UI-modifiable state
    
    void addIRPoint(const juce::String& name);
    void removePoint(juce::Uuid id);
    void updatePointPosition(juce::Uuid id, float x, float y);
    
    // Wall Management
    juce::Uuid addWall(float x1, float y1, float x2, float y2);
    void removeWall(juce::Uuid id);
    void updateWall(juce::Uuid id, float x1, float y1, float x2, float y2);
    
    // Constraint
    void constrainPointToWalls(float& x, float& y);
    
    // New methods
    juce::Uuid addIRFromFile(const juce::File& file);
    void reloadIRChannel(juce::Uuid id, int channelIndex);
    void updateConvolver(IRPoint& p);
    void setPointLocked(juce::Uuid id, bool locked);
    void setPointName(juce::Uuid id, const juce::String& name);
    void loadLayoutFromJSON(const juce::File& file);
    void saveLayoutToJSON(const juce::File& file);

    // NN Logic
    void updateWeightsGaussian();
    void timerCallback() override;

    // Helper to send weights via OSC
    void sendWeightsOSC(); 
    
    std::vector<IRPoint> currentNearestNeighbors; 

    // Gaussian & Hysteresis Params
    // float sigma = 0.25f; // Replaced by parameter
    float tauIn = 0.1f;        
    float tauOut = 0.05f;      
    
    // Smoothing
    std::map<juce::Uuid, float> targetWeights;
    std::map<juce::Uuid, float> smoothedWeights;
    std::set<juce::Uuid> activeIDs;

    // Parameters
    // Legacy params (kept for structure)
    juce::AudioParameterFloat* weight1 = nullptr;
    juce::AudioParameterFloat* weight2 = nullptr;
    juce::AudioParameterFloat* weight3 = nullptr;
    
    // New Parameters (Atomic for APVTS)
    std::atomic<float>* inertiaParam = nullptr; // 0.0 - 1.0
    std::atomic<float>* freezeParam = nullptr;
    std::atomic<float>* spreadParam = nullptr; // Controls Sigma
    std::atomic<float>* mixParam = nullptr; // Dry/Wet
    std::atomic<float>* wallOpacityParam = nullptr; // Visual opacity
    
    // Physics / Smoothing State
    float currentListenerX = 0.5f;
    float currentListenerY = 0.5f;

    // OSC Data
    std::vector<juce::String> availableIRs; 
    
    // OSC callbacks
    void oscMessageReceived (const juce::OSCMessage& message) override;

    std::function<void()> onStateChanged;
    
    // Audio Format Manager
    juce::AudioFormatManager formatManager;
    
    // Process Spec for Convolvers
    juce::dsp::ProcessSpec processSpec;

private:
    // Thread Safety
    // stateLock moved to public

    // Helper to get a stable snapshot for the UI if needed, or just lock
    // For now, UI components should lock `stateLock` when iterating points/walls, 
    // OR use the helper methods which I will make thread-safe.

private:
    // Render State (Thread safe)
    std::shared_ptr<RenderState> renderState; // Accessed by Audio Thread
    
    juce::OSCReceiver oscReceiver;
    juce::OSCSender oscSender;
    
    // Mixing Buffer
    juce::AudioBuffer<float> mixBuffer;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrisVSTV3AudioProcessor)
};
