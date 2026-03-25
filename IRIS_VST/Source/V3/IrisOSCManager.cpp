/*
  ==============================================================================

    IrisOSCManager.cpp
    Created: 9 Feb 2026
    Author:  IRIS

  ==============================================================================
*/

#include "IrisOSCManager.h"
#include "PluginProcessorV3.h"

IrisOSCManager& IrisOSCManager::getInstance()
{
    static IrisOSCManager instance;
    return instance;
}

IrisOSCManager::IrisOSCManager()
{
    // Try to connect to port 9001
    // Since this is a singleton, it runs once.
    if (oscReceiver.connect(9001))
    {
        oscReceiver.addListener(this);
        isConnected = true;
        DBG("IrisOSCManager: Connected to port 9001");
    }
    else
    {
        DBG("IrisOSCManager: Failed to connect to port 9001 (already bound?)");
    }

    // Sender to port 9002 (e.g. Max/MSP or other tools)
    oscSender.connect("127.0.0.1", 9002);
}

IrisOSCManager::~IrisOSCManager()
{
    oscReceiver.removeListener(this);
    oscReceiver.disconnect();
}

void IrisOSCManager::addProcessor(IrisVSTV3AudioProcessor* processor)
{
    juce::ScopedLock sl(listLock);
    
    // If it's the first processor and we aren't connected, maybe retry? 
    // Usually connect logic is in constructor which is fine.
    
    if (!processors.contains(processor))
    {
        // If there are already processors, maybe sync state FROM the first one TO the new one?
        if (processors.size() > 0)
        {
            // Reset new processor to match existing one? 
            // Ideally the Host manages state (save/load), but for live sync we might want to pull.
            // For now, let's just add it.
        }
        processors.add(processor);
    }
}

void IrisOSCManager::removeProcessor(IrisVSTV3AudioProcessor* processor)
{
    juce::ScopedLock sl(listLock);
    processors.removeFirstMatchingValue(processor);
}

void IrisOSCManager::notifyProcessors(const std::function<void(IrisVSTV3AudioProcessor*)>& callback, IrisVSTV3AudioProcessor* exclude)
{
    juce::ScopedLock sl(listLock);
    for (auto* p : processors)
    {
        if (p != exclude && p != nullptr)
        {
             // We are on the message thread usually for OSC callbacks if using MessageLoopCallback
             // But calls from UI are on Message Thread too.
             // Calls from Processor might be audio thread? No, usually UI triggers parameters.
             // Precaution: use MessageManager::callAsync if we are not on message thread?
             // For now, assume these are lightweight logic updates.
             callback(p);
        }
    }
}

void IrisOSCManager::sendOSC(const juce::OSCMessage& message)
{
    if (!oscSender.send(message)) {
        // DBG("OSC Send Failed");
    }
}

// ==============================================================================
// OSC PROCESSING
// ==============================================================================

void IrisOSCManager::oscMessageReceived(const juce::OSCMessage& message)
{
    if (message.getAddressPattern() == "/iris/listener/pos" && message.size() == 2)
    {
        if (message[0].isFloat32() && message[1].isFloat32())
        {
            float x = message[0].getFloat32();
            float y = message[1].getFloat32();
            
            // Notify ALL processors (source is null, so everyone updates)
            notifyProcessors([x, y](IrisVSTV3AudioProcessor* p) {
                p->updatePointPosition(juce::Uuid(), x, y); // Listener has null/empty UUID logic or specific method? 
                // Wait, listener is usually stored in 'points' but identified by isListener.
                // We need a specific method for Listener if UUID is unknown, or we find it.
                
                // Let's use a helper on Processor to "setListener"
                p->setListenerPosition(x, y, false); 
            });
        }
    }
    else if (message.getAddressPattern() == "/iris/param/mix" && message.size() == 1)
    {
        if (message[0].isFloat32())
        {
            float val = message[0].getFloat32();
            notifyProcessors([val](IrisVSTV3AudioProcessor* p) {
                if(auto* par = p->mixParam) par->store(val);
                p->updateParameterNotifiers("mix", val); // Helper to update Host/UI
            });
        }
    }
    // Add other OSC parsers here...
    else if (message.getAddressPattern() == "/iris/param/spread" && message.size() == 1) {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisVSTV3AudioProcessor* p) { if(auto* par = p->spreadParam) par->store(val); p->updateParameterNotifiers("spread", val); });
    }
    else if (message.getAddressPattern() == "/iris/param/inertia" && message.size() == 1) {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisVSTV3AudioProcessor* p) { if(auto* par = p->inertiaParam) par->store(val); p->updateParameterNotifiers("inertia", val); });
    }
    else if (message.getAddressPattern() == "/iris/param/freeze" && message.size() == 1) {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisVSTV3AudioProcessor* p) { if(auto* par = p->freezeParam) par->store(val); p->updateParameterNotifiers("freeze", val); });
    }
     else if (message.getAddressPattern() == "/iris/param/wallOpacity" && message.size() == 1) {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisVSTV3AudioProcessor* p) { if(auto* par = p->wallOpacityParam) par->store(val); p->updateParameterNotifiers("wallOpacity", val); });
    }
}


// ==============================================================================
// BROADCASTING
// ==============================================================================

void IrisOSCManager::setListenerPosition(float x, float y, IrisVSTV3AudioProcessor* source)
{
    // 1. Notify other local processors
    notifyProcessors([x, y](IrisVSTV3AudioProcessor* p) {
        p->setListenerPosition(x, y, false);
    }, source);
    
    // 2. Send OSC
    juce::OSCMessage m("/iris/listener/pos");
    m.addFloat32(x);
    m.addFloat32(y);
    sendOSC(m);
}

void IrisOSCManager::setGlobalParam(const juce::String& paramId, float value, IrisVSTV3AudioProcessor* source)
{
    notifyProcessors([paramId, value](IrisVSTV3AudioProcessor* p) {
        // We need a generic setter or switch
        if (paramId == "mix") { if(p->mixParam) p->mixParam->store(value); }
        else if (paramId == "spread") { if(p->spreadParam) p->spreadParam->store(value); }
        else if (paramId == "inertia") { if(p->inertiaParam) p->inertiaParam->store(value); }
        else if (paramId == "freeze") { if(p->freezeParam) p->freezeParam->store(value); }
        else if (paramId == "wallOpacity") { if(p->wallOpacityParam) p->wallOpacityParam->store(value); }
        
        p->updateParameterNotifiers(paramId, value);
    }, source);
    
    juce::OSCMessage m("/iris/param/" + paramId);
    m.addFloat32(value);
    sendOSC(m);
}

void IrisOSCManager::syncAddIR(const juce::Uuid& id, const juce::String& name, const juce::File& file, IrisVSTV3AudioProcessor* source)
{
    // Sync creation
    notifyProcessors([id, name, file](IrisVSTV3AudioProcessor* p) {
        p->addIRFromFileWithID(file, id); // Need this method
    }, source);
    
    // OSC
    juce::OSCMessage m("/iris/ir/add");
    m.addString(id.toString());
    m.addString(name);
    m.addString(file.getFullPathName());
    sendOSC(m);
}

void IrisOSCManager::syncRemoveIR(const juce::Uuid& id, IrisVSTV3AudioProcessor* source)
{
    notifyProcessors([id](IrisVSTV3AudioProcessor* p) { p->removePoint(id, false); }, source);
    
    juce::OSCMessage m("/iris/ir/remove");
    m.addString(id.toString());
    sendOSC(m);
}

void IrisOSCManager::syncIRPosition(const juce::Uuid& id, float x, float y, IrisVSTV3AudioProcessor* source)
{
    notifyProcessors([id, x, y](IrisVSTV3AudioProcessor* p) { p->updatePointPosition(id, x, y, false); }, source);
    
    juce::OSCMessage m("/iris/ir/pos");
    m.addString(id.toString());
    m.addFloat32(x);
    m.addFloat32(y);
    sendOSC(m);
}

void IrisOSCManager::syncIRName(const juce::Uuid& id, const juce::String& name, IrisVSTV3AudioProcessor* source)
{
    notifyProcessors([id, name](IrisVSTV3AudioProcessor* p) { p->setPointName(id, name, false); }, source);
     // OSC?
}

void IrisOSCManager::syncIRChannel(const juce::Uuid& id, int channel, IrisVSTV3AudioProcessor* source)
{
    notifyProcessors([id, channel](IrisVSTV3AudioProcessor* p) { p->reloadIRChannel(id, channel, false); }, source);
}

void IrisOSCManager::syncLocked(const juce::Uuid& id, bool locked, IrisVSTV3AudioProcessor* source)
{
    notifyProcessors([id, locked](IrisVSTV3AudioProcessor* p) { p->setPointLocked(id, locked, false); }, source);
}

void IrisOSCManager::syncAddWall(const juce::Uuid& id, float x1, float y1, float x2, float y2, IrisVSTV3AudioProcessor* source)
{
    notifyProcessors([id, x1, y1, x2, y2](IrisVSTV3AudioProcessor* p) { 
        p->addWallWithID(id, x1, y1, x2, y2); // Need this
    }, source);
    
    juce::OSCMessage m("/iris/wall/add");
    m.addString(id.toString());
    m.addFloat32(x1); m.addFloat32(y1); m.addFloat32(x2); m.addFloat32(y2);
    sendOSC(m);
}

void IrisOSCManager::syncRemoveWall(const juce::Uuid& id, IrisVSTV3AudioProcessor* source)
{
     notifyProcessors([id](IrisVSTV3AudioProcessor* p) { p->removeWall(id, false); }, source);
    
    juce::OSCMessage m("/iris/wall/remove");
    m.addString(id.toString());
    sendOSC(m);
}

void IrisOSCManager::syncWallPosition(const juce::Uuid& id, float x1, float y1, float x2, float y2, IrisVSTV3AudioProcessor* source)
{
    notifyProcessors([id, x1, y1, x2, y2](IrisVSTV3AudioProcessor* p) { p->updateWall(id, x1, y1, x2, y2, false); }, source);
    
    juce::OSCMessage m("/iris/wall/pos");
    m.addString(id.toString());
    m.addFloat32(x1); m.addFloat32(y1); m.addFloat32(x2); m.addFloat32(y2);
    sendOSC(m);
}

void IrisOSCManager::requestFullSync(IrisVSTV3AudioProcessor* requester)
{
    if (requester == nullptr) return;

    // 1. Sync Listener
    setListenerPosition(requester->currentListenerX, requester->currentListenerY, requester);
    
    // 2. Sync Globals
    if (requester->mixParam) setGlobalParam("mix", requester->mixParam->load(), requester);
    if (requester->spreadParam) setGlobalParam("spread", requester->spreadParam->load(), requester);
    if (requester->inertiaParam) setGlobalParam("inertia", requester->inertiaParam->load(), requester);
    if (requester->freezeParam) setGlobalParam("freeze", requester->freezeParam->load(), requester);
    if (requester->wallOpacityParam) setGlobalParam("wallOpacity", requester->wallOpacityParam->load(), requester);
    
    // 3. Sync Walls
    {
        juce::ScopedLock sl(requester->stateLock);
        for (const auto& w : requester->walls)
        {
             syncAddWall(w.id, w.x1, w.y1, w.x2, w.y2, requester);
             // Also properties? locked?
             // syncLocked(w.id, w.locked, requester);
        }
    
    // 4. Sync IRs
        for (const auto& p : requester->points)
        {
            if (p.isListener) continue;
            // Add
            syncAddIR(p.id, p.name, p.sourceFile, requester);
            // Position
            syncIRPosition(p.id, p.x, p.y, requester);
            // Props
            syncLocked(p.id, p.locked, requester);
            syncIRName(p.id, p.name, requester);
            syncIRChannel(p.id, p.selectedChannel, requester);
        }
    }
}
