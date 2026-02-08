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
};

class IrisVSTV2AudioProcessor  : public juce::AudioProcessor,
                               public juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                               public juce::Timer
{
public:
    IrisVSTV2AudioProcessor();
    ~IrisVSTV2AudioProcessor() override;

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

    // Custom Data
    std::vector<IRPoint> points;
    void addIRPoint(const juce::String& name = ""); 
    void removePoint(juce::Uuid id);
    void updatePointPosition(juce::Uuid id, float x, float y);
    void setPointName(juce::Uuid id, const juce::String& name);
    
    // New methods
    juce::Uuid addIRFromFile(const juce::File& file);
    void reloadIRChannel(juce::Uuid id, int channelIndex);
    void updateConvolver(IRPoint& p);
    void setPointLocked(juce::Uuid id, bool locked);
    void loadLayoutFromJSON(const juce::File& file);

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
    
    // New Parameters
    juce::AudioParameterFloat* inertiaParam = nullptr; // 0.0 - 1.0
    juce::AudioParameterBool* freezeParam = nullptr;
    juce::AudioParameterFloat* spreadParam = nullptr; // Controls Sigma
    juce::AudioParameterFloat* mixParam = nullptr; // Dry/Wet
    
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
    // Render State (Thread safe)
    std::shared_ptr<RenderState> renderState; // Accessed by Audio Thread
    
    juce::OSCReceiver oscReceiver;
    juce::OSCSender oscSender;
    
    // Mixing Buffer
    juce::AudioBuffer<float> mixBuffer;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrisVSTV2AudioProcessor)
};
