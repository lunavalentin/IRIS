/*
  ==============================================================================

    IrisOSCManager.h
    Created: 9 Feb 2026
    Author:  IRIS

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV4.h"

class IrisVSTV4AudioProcessor;

class IrisOSCManager : public juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    static IrisOSCManager& getInstance();

    // Connection Management
    void addProcessor(IrisVSTV4AudioProcessor* processor);
    void removeProcessor(IrisVSTV4AudioProcessor* processor);

    // OSC Methods
    void oscMessageReceived(const juce::OSCMessage& message) override;

    // Broadcasting / Sync Methods
    // These methods update local processors AND send OSC out
    void setListenerState(const juce::Uuid& id, const juce::String& name, float x, float y, bool linked, bool locked, IrisVSTV4AudioProcessor* source);
    void syncLinkMatrix(IrisVSTV4AudioProcessor* source);
    void setGlobalParam(const juce::String& paramId, float value, IrisVSTV4AudioProcessor* source);
    
    // IR / Wall Sync
    void syncAddIR(const juce::Uuid& id, const juce::String& name, const juce::File& file, IrisVSTV4AudioProcessor* source);
    void syncRemoveIR(const juce::Uuid& id, IrisVSTV4AudioProcessor* source);
    void syncIRPosition(const juce::Uuid& id, float x, float y, IrisVSTV4AudioProcessor* source);
    void syncIRName(const juce::Uuid& id, const juce::String& name, IrisVSTV4AudioProcessor* source);
    void syncIRChannel(const juce::Uuid& id, int channel, IrisVSTV4AudioProcessor* source);
    void syncLocked(const juce::Uuid& id, bool locked, IrisVSTV4AudioProcessor* source); // Works for both IR and Wall if IDs unique enough, or separate

    void syncAddWall(const juce::Uuid& id, float x1, float y1, float x2, float y2, IrisVSTV4AudioProcessor* source);
    void syncRemoveWall(const juce::Uuid& id, IrisVSTV4AudioProcessor* source);
    void syncWallPosition(const juce::Uuid& id, float x1, float y1, float x2, float y2, IrisVSTV4AudioProcessor* source);

    // Force Resync (e.g. when a new plugin loads, ask an existing one for state)
    void requestFullSync(IrisVSTV4AudioProcessor* requester);
    void removeGhostId(const juce::Uuid& ghostId);

    void sendOSC(const juce::OSCMessage& message);

private:
    IrisOSCManager();
    ~IrisOSCManager();

    // Helpers
    void notifyProcessors(const std::function<void(IrisVSTV4AudioProcessor*)>& callback, IrisVSTV4AudioProcessor* exclude = nullptr);

    juce::OSCReceiver oscReceiver;
    juce::OSCSender oscSender;
    
    juce::Array<IrisVSTV4AudioProcessor*> processors;
    juce::CriticalSection listLock;
    
    bool isConnected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrisOSCManager)
};
