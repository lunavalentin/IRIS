/*
  ==============================================================================

    IrisOSCManager.cpp
    Created: 9 Feb 2026
    Author:  IRIS

  ==============================================================================
*/

#include "IrisOSCManager.h"
#include "PluginProcessorV4.h"

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

void IrisOSCManager::addProcessor(IrisVSTV4AudioProcessor* processor)
{
    IrisVSTV4AudioProcessor* syncSource = nullptr;
    {
        juce::ScopedLock sl(listLock);
        if (!processors.contains(processor)) {
            if (processors.size() > 0)
                syncSource = processors[0];
            processors.add(processor);
        } else {
            return;
        }
    }
    
    if (syncSource != nullptr)
    {
        juce::ScopedLock slState(processor->stateLock);
        juce::ScopedLock slSrc(syncSource->stateLock);
        
        // 1. Copy over all remote listeners the syncSource knows about
        for (const auto& pair : syncSource->remoteListeners) {
            if (pair.first != processor->localAudioListener.id) {
                processor->remoteListeners[pair.first] = pair.second;
            }
        }
        
        // 2. Add the syncSource's local listener into our remote list
        IrisVSTV4AudioProcessor::NetworkListener srcRemote;
        srcRemote.id = syncSource->localAudioListener.id;
        srcRemote.name = syncSource->localAudioListener.name;
        srcRemote.x = syncSource->localAudioListener.x;
        srcRemote.y = syncSource->localAudioListener.y;
        srcRemote.isLocal = false;
        processor->remoteListeners[srcRemote.id] = srcRemote;
    }
    
    // Assign Letter ID
    if (processor->localAudioListener.name == "Local Listener")
    {
        char letter = 'A';
        while (letter <= 'Z') {
            juce::String letterStr = juce::String::charToString((juce_wchar)letter);
            bool found = false;
            for (const auto& pair : processor->remoteListeners) {
                if (pair.second.name == letterStr) { found = true; break; }
            }
            if (!found) {
                processor->localAudioListener.name = letterStr;
                break;
            }
            letter++;
        }
    }
    
    // 3. Announce this new processor to everyone else
    setListenerState(processor->localAudioListener.id, 
                     processor->localAudioListener.name,
                     processor->localAudioListener.x,
                     processor->localAudioListener.y,
                     false,
                     processor->localAudioListener.locked,
                     processor);
                     
    if (processor->onStateChanged) processor->onStateChanged();
}

void IrisOSCManager::removeProcessor(IrisVSTV4AudioProcessor* processor)
{
    juce::Uuid ghostId = processor->localAudioListener.id;
    {
        juce::ScopedLock sl(listLock);
        processors.removeFirstMatchingValue(processor);
    }
    
    // Broadcast removal to other processes
    juce::OSCMessage m("/iris/listener/remove");
    m.addString(ghostId.toString());
    sendOSC(m);
    
    // Remove from other local instances
    removeGhostId(ghostId);
}

void IrisOSCManager::notifyProcessors(const std::function<void(IrisVSTV4AudioProcessor*)>& callback, IrisVSTV4AudioProcessor* exclude)
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
    if (message.getAddressPattern() == "/iris/listener/sync" && message.size() >= 6)
    {
        if (message[0].isString() && message[1].isString() && message[2].isFloat32() && message[3].isFloat32() && message[4].isInt32() && message[5].isInt32())
        {
            juce::Uuid id(message[0].getString());
            juce::String name = message[1].getString();
            float x = message[2].getFloat32();
            float y = message[3].getFloat32();
            bool linked = message[4].getInt32() != 0;
            bool locked = message[5].getInt32() != 0;
            
            // Notify ALL processors
            notifyProcessors([id, name, x, y, locked](IrisVSTV4AudioProcessor* p) {
                juce::ScopedLock sl(p->stateLock);
                if (id != p->localAudioListener.id) {
                    if (p->remoteListeners.find(id) == p->remoteListeners.end()) {
                        IrisVSTV4AudioProcessor::NetworkListener remote;
                        remote.id = id;
                        remote.name = name;
                        remote.x = juce::jlimit(0.0f, 1.0f, x);
                        remote.y = juce::jlimit(0.0f, 1.0f, y);
                        remote.isLocal = false;
                        remote.locked = locked;
                        p->remoteListeners[id] = remote;
                    } else {
                        p->remoteListeners[id].name = name;
                        p->remoteListeners[id].locked = locked;
                        p->updateListenerPosition(id, x, y, false);
                    }
                } else {
                    p->localAudioListener.name = name;
                    p->localAudioListener.locked = locked;
                    p->updateListenerPosition(id, x, y, false);
                }
                if (p->onStateChanged) p->onStateChanged();
            });
        }
    }
    else if (message.getAddressPattern() == "/iris/listener/matrix" && message.size() == 2)
    {
        if (message[0].isString() && message[1].isString())
        {
            juce::String source = message[0].getString();
            juce::String matrixStr = message[1].getString();
            
            std::vector<std::pair<juce::String, juce::String>> newEdges;
            if (matrixStr.isNotEmpty()) {
                juce::StringArray pairs;
                pairs.addTokens(matrixStr, ",", "");
                for (const auto& pStr : pairs) {
                    juce::StringArray parts;
                    parts.addTokens(pStr, ":", "");
                    if (parts.size() == 2) {
                        newEdges.push_back({parts[0], parts[1]});
                    }
                }
            }
            
            juce::MessageManager::callAsync([this, newEdges, source]() {
                juce::ScopedLock sl(listLock);
                for (auto* p : processors) {
                    if (p != nullptr && p->localAudioListener.id.toString() != source) {
                        p->setLinkMatrixConnections(newEdges);
                    }
                }
            });
        }
    }
    else if (message.getAddressPattern() == "/iris/listener/remove" && message.size() == 1)
    {
        if (message[0].isString())
        {
            juce::Uuid id(message[0].getString());
            removeGhostId(id);
        }
    }
    else if (message.getAddressPattern() == "/iris/param/mix" && message.size() == 1)
    {
        if (message[0].isFloat32())
        {
            float val = message[0].getFloat32();
            notifyProcessors([val](IrisVSTV4AudioProcessor* p) {
                if(auto* par = p->mixParam) par->store(val);
                p->updateParameterNotifiers("mix", val); // Helper to update Host/UI
            });
        }
    }
    // Add other OSC parsers here...
    else if (message.getAddressPattern() == "/iris/param/spread" && message.size() == 1) {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisVSTV4AudioProcessor* p) { if(auto* par = p->spreadParam) par->store(val); p->updateParameterNotifiers("spread", val); });
    }
    else if (message.getAddressPattern() == "/iris/param/inertia" && message.size() == 1) {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisVSTV4AudioProcessor* p) { if(auto* par = p->inertiaParam) par->store(val); p->updateParameterNotifiers("inertia", val); });
    }
    else if (message.getAddressPattern() == "/iris/param/freeze" && message.size() == 1) {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisVSTV4AudioProcessor* p) { if(auto* par = p->freezeParam) par->store(val); p->updateParameterNotifiers("freeze", val); });
    }
     else if (message.getAddressPattern() == "/iris/param/wallOpacity" && message.size() == 1) {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisVSTV4AudioProcessor* p) { if(auto* par = p->wallOpacityParam) par->store(val); p->updateParameterNotifiers("wallOpacity", val); });
    }
}


// ==============================================================================
// BROADCASTING
// ==============================================================================

void IrisOSCManager::syncLinkMatrix(IrisVSTV4AudioProcessor* caller)
{
    if (isConnected) {
        juce::StringArray edges;
        for (const auto& edge : caller->linkMatrix) {
            edges.add(edge.first + ":" + edge.second);
        }
        juce::String matrixData = edges.joinIntoString(",");
        
        juce::OSCMessage m("/iris/listener/matrix", caller->localAudioListener.id.toString(), matrixData);
        sendOSC(m);
    }
}

void IrisOSCManager::setListenerState(const juce::Uuid& id, const juce::String& name, float x, float y, bool linked, bool locked, IrisVSTV4AudioProcessor* source)
{
    // 1. Notify other local processors
    notifyProcessors([id, name, x, y, locked](IrisVSTV4AudioProcessor* p) {
        juce::ScopedLock sl(p->stateLock);
        if (id != p->localAudioListener.id) {
            if (p->remoteListeners.find(id) == p->remoteListeners.end()) {
                 IrisVSTV4AudioProcessor::NetworkListener remote;
                 remote.id = id;
                 remote.name = name;
                 remote.x = juce::jlimit(0.0f, 1.0f, x);
                 remote.y = juce::jlimit(0.0f, 1.0f, y);
                 remote.isLocal = false;
                 remote.locked = locked;
                 p->remoteListeners[id] = remote;
            } else {
                 p->remoteListeners[id].name = name;
                 p->remoteListeners[id].locked = locked;
                 p->updateListenerPosition(id, x, y, false);
            }
        } else {
            // It IS this specific instance's local listener!
            // Another instance is broadcasting changes to our local listener!
            p->localAudioListener.name = name;
            p->localAudioListener.locked = locked;
            p->updateListenerPosition(id, x, y, false);
        }
        if (p->onStateChanged) p->onStateChanged();
    }, source);
    
    // 2. Send OSC
    juce::OSCMessage m("/iris/listener/sync");
    m.addString(id.toString());
    m.addString(name);
    m.addFloat32(x);
    m.addFloat32(y);
    m.addInt32(linked ? 1 : 0);
    m.addInt32(locked ? 1 : 0);
    sendOSC(m);
}

void IrisOSCManager::setGlobalParam(const juce::String& paramId, float value, IrisVSTV4AudioProcessor* source)
{
    notifyProcessors([paramId, value](IrisVSTV4AudioProcessor* p) {
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

void IrisOSCManager::syncAddIR(const juce::Uuid& id, const juce::String& name, const juce::File& file, IrisVSTV4AudioProcessor* source)
{
    // Sync creation
    notifyProcessors([id, name, file](IrisVSTV4AudioProcessor* p) {
        p->addIRFromFileWithID(file, id); // Need this method
    }, source);
    
    // OSC
    juce::OSCMessage m("/iris/ir/add");
    m.addString(id.toString());
    m.addString(name);
    m.addString(file.getFullPathName());
    sendOSC(m);
}

void IrisOSCManager::syncRemoveIR(const juce::Uuid& id, IrisVSTV4AudioProcessor* source)
{
    notifyProcessors([id](IrisVSTV4AudioProcessor* p) { p->removePoint(id, false); }, source);
    
    juce::OSCMessage m("/iris/ir/remove");
    m.addString(id.toString());
    sendOSC(m);
}

void IrisOSCManager::syncIRPosition(const juce::Uuid& id, float x, float y, IrisVSTV4AudioProcessor* source)
{
    notifyProcessors([id, x, y](IrisVSTV4AudioProcessor* p) { p->updatePointPosition(id, x, y, false); }, source);
    
    juce::OSCMessage m("/iris/ir/pos");
    m.addString(id.toString());
    m.addFloat32(x);
    m.addFloat32(y);
    sendOSC(m);
}

void IrisOSCManager::syncIRName(const juce::Uuid& id, const juce::String& name, IrisVSTV4AudioProcessor* source)
{
    notifyProcessors([id, name](IrisVSTV4AudioProcessor* p) { p->setPointName(id, name, false); }, source);
     // OSC?
}

void IrisOSCManager::syncIRChannel(const juce::Uuid& id, int channel, IrisVSTV4AudioProcessor* source)
{
    notifyProcessors([id, channel](IrisVSTV4AudioProcessor* p) { p->reloadIRChannel(id, channel, false); }, source);
}

void IrisOSCManager::syncLocked(const juce::Uuid& id, bool locked, IrisVSTV4AudioProcessor* source)
{
    notifyProcessors([id, locked](IrisVSTV4AudioProcessor* p) { p->setPointLocked(id, locked, false); }, source);
}

void IrisOSCManager::syncAddWall(const juce::Uuid& id, float x1, float y1, float x2, float y2, IrisVSTV4AudioProcessor* source)
{
    notifyProcessors([id, x1, y1, x2, y2](IrisVSTV4AudioProcessor* p) { 
        p->addWallWithID(id, x1, y1, x2, y2); // Need this
    }, source);
    
    juce::OSCMessage m("/iris/wall/add");
    m.addString(id.toString());
    m.addFloat32(x1); m.addFloat32(y1); m.addFloat32(x2); m.addFloat32(y2);
    sendOSC(m);
}

void IrisOSCManager::syncRemoveWall(const juce::Uuid& id, IrisVSTV4AudioProcessor* source)
{
     notifyProcessors([id](IrisVSTV4AudioProcessor* p) { p->removeWall(id, false); }, source);
    
    juce::OSCMessage m("/iris/wall/remove");
    m.addString(id.toString());
    sendOSC(m);
}

void IrisOSCManager::syncWallPosition(const juce::Uuid& id, float x1, float y1, float x2, float y2, IrisVSTV4AudioProcessor* source)
{
    notifyProcessors([id, x1, y1, x2, y2](IrisVSTV4AudioProcessor* p) { p->updateWall(id, x1, y1, x2, y2, false); }, source);
    
    juce::OSCMessage m("/iris/wall/pos");
    m.addString(id.toString());
    m.addFloat32(x1); m.addFloat32(y1); m.addFloat32(x2); m.addFloat32(y2);
    sendOSC(m);
}

void IrisOSCManager::requestFullSync(IrisVSTV4AudioProcessor* requester)
{
    if (requester == nullptr) return;

    // 1. Sync Listener
    setListenerState(requester->localAudioListener.id, requester->localAudioListener.name, requester->localAudioListener.x, requester->localAudioListener.y, false, requester->localAudioListener.locked, requester);
    
    // 2. Sync Matrix
    syncLinkMatrix(requester);
    
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

void IrisOSCManager::removeGhostId(const juce::Uuid& ghostId)
{
    juce::ScopedLock sl(listLock);
    for (auto* p : processors) {
        if (p != nullptr) {
            juce::ScopedLock slState(p->stateLock);
            p->remoteListeners.erase(ghostId);
            if (p->selectedListenerId == ghostId) {
                p->selectedListenerId = p->localAudioListener.id;
            }
            if (p->onStateChanged) p->onStateChanged();
        }
    }
}
