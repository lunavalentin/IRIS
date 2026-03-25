/*
  ==============================================================================

    IrisOSCManager.h
    Created: 9 Feb 2026
    Author:  IRIS

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessorV3.h"

class IrisVSTV3AudioProcessor;

class IrisOSCManager : public juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    static IrisOSCManager& getInstance();

    // Connection Management
    void addProcessor(IrisVSTV3AudioProcessor* processor);
    void removeProcessor(IrisVSTV3AudioProcessor* processor);

    // OSC Methods
    void oscMessageReceived(const juce::OSCMessage& message) override;

    // Broadcasting / Sync Methods
    // These methods update local processors AND send OSC out
    void setListenerPosition(float x, float y, IrisVSTV3AudioProcessor* source);
    void setGlobalParam(const juce::String& paramId, float value, IrisVSTV3AudioProcessor* source);
    
    // IR / Wall Sync
    void syncAddIR(const juce::Uuid& id, const juce::String& name, const juce::File& file, IrisVSTV3AudioProcessor* source);
    void syncRemoveIR(const juce::Uuid& id, IrisVSTV3AudioProcessor* source);
    void syncIRPosition(const juce::Uuid& id, float x, float y, IrisVSTV3AudioProcessor* source);
    void syncIRName(const juce::Uuid& id, const juce::String& name, IrisVSTV3AudioProcessor* source);
    void syncIRChannel(const juce::Uuid& id, int channel, IrisVSTV3AudioProcessor* source);
    void syncLocked(const juce::Uuid& id, bool locked, IrisVSTV3AudioProcessor* source); // Works for both IR and Wall if IDs unique enough, or separate

    void syncAddWall(const juce::Uuid& id, float x1, float y1, float x2, float y2, IrisVSTV3AudioProcessor* source);
    void syncRemoveWall(const juce::Uuid& id, IrisVSTV3AudioProcessor* source);
    void syncWallPosition(const juce::Uuid& id, float x1, float y1, float x2, float y2, IrisVSTV3AudioProcessor* source);

    // Force Resync (e.g. when a new plugin loads, ask an existing one for state)
    void requestFullSync(IrisVSTV3AudioProcessor* requester);

    void sendOSC(const juce::OSCMessage& message);

private:
    IrisOSCManager();
    ~IrisOSCManager();

    // Helpers
    void notifyProcessors(const std::function<void(IrisVSTV3AudioProcessor*)>& callback, IrisVSTV3AudioProcessor* exclude = nullptr);

    juce::OSCReceiver oscReceiver;
    juce::OSCSender oscSender;
    
    juce::Array<IrisVSTV3AudioProcessor*> processors;
    juce::CriticalSection listLock;
    
    bool isConnected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrisOSCManager)
};
