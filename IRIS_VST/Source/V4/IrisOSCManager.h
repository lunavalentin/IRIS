#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class IrisAudioProcessor;

class IrisOSCManager : public juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    static IrisOSCManager& getInstance();

    void addProcessor(IrisAudioProcessor* processor);
    void removeProcessor(IrisAudioProcessor* processor);

    void oscMessageReceived(const juce::OSCMessage& message) override;

    // Listener sync
    void setListenerState(const juce::Uuid& id, const juce::String& name,
                          float x, float y, bool linked, bool locked,
                          IrisAudioProcessor* source);
    void syncLinkMatrix(IrisAudioProcessor* source);

    // Global parameter sync
    void setGlobalParam(const juce::String& paramId, float value, IrisAudioProcessor* source);

    // IR sync
    void syncAddIR(const juce::Uuid& id, const juce::String& name, const juce::File& file, IrisAudioProcessor* source);
    void syncRemoveIR(const juce::Uuid& id, IrisAudioProcessor* source);
    void syncIRPosition(const juce::Uuid& id, float x, float y, IrisAudioProcessor* source);
    void syncIRName(const juce::Uuid& id, const juce::String& name, IrisAudioProcessor* source);
    void syncLocked(const juce::Uuid& id, bool locked, IrisAudioProcessor* source);

    // Wall sync
    void syncAddWall(const juce::Uuid& id, float x1, float y1, float x2, float y2, IrisAudioProcessor* source);
    void syncRemoveWall(const juce::Uuid& id, IrisAudioProcessor* source);
    void syncWallPosition(const juce::Uuid& id, float x1, float y1, float x2, float y2, IrisAudioProcessor* source);

    // Full resync from a requester instance to all others
    void requestFullSync(IrisAudioProcessor* requester);
    void removeGhostId(const juce::Uuid& ghostId);

    void sendOSC(const juce::OSCMessage& message);

private:
    IrisOSCManager();
    ~IrisOSCManager() override;

    void notifyProcessors(const std::function<void(IrisAudioProcessor*)>& callback,
                          IrisAudioProcessor* exclude = nullptr);

    juce::OSCReceiver oscReceiver;
    juce::OSCSender   oscSender;

    juce::Array<IrisAudioProcessor*> processors;
    juce::CriticalSection             listLock;

    bool isConnected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrisOSCManager)
};
