#include "IrisOSCManager.h"
#include "PluginProcessor.h"

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

IrisOSCManager& IrisOSCManager::getInstance()
{
    static IrisOSCManager instance;
    return instance;
}

IrisOSCManager::IrisOSCManager()
{
    if (oscReceiver.connect(9001))
    {
        oscReceiver.addListener(this);
        isConnected = true;
        DBG("IrisOSCManager: Connected to port 9001");
    }
    else
    {
        DBG("IrisOSCManager: Failed to connect to port 9001 (port already bound?)");
    }

    oscSender.connect("127.0.0.1", 9002);
}

IrisOSCManager::~IrisOSCManager()
{
    oscReceiver.removeListener(this);
    oscReceiver.disconnect();
}

// ---------------------------------------------------------------------------
// Processor registry
// ---------------------------------------------------------------------------

void IrisOSCManager::addProcessor(IrisAudioProcessor* processor)
{
    IrisAudioProcessor* syncSource = nullptr;
    {
        juce::ScopedLock sl(listLock);
        if (processors.contains(processor)) return;
        if (processors.size() > 0) syncSource = processors[0];
        processors.add(processor);
    }

    if (syncSource != nullptr)
    {
        juce::ScopedLock slState(processor->stateLock);
        juce::ScopedLock slSrc(syncSource->stateLock);

        for (const auto& pair : syncSource->remoteListeners)
            if (pair.first != processor->localAudioListener.id)
                processor->remoteListeners[pair.first] = pair.second;

        IrisAudioProcessor::NetworkListener srcRemote;
        srcRemote.id      = syncSource->localAudioListener.id;
        srcRemote.name    = syncSource->localAudioListener.name;
        srcRemote.x       = syncSource->localAudioListener.x;
        srcRemote.y       = syncSource->localAudioListener.y;
        srcRemote.isLocal = false;
        processor->remoteListeners[srcRemote.id] = srcRemote;
    }

    // Assign the next available single-letter name if this is a fresh instance.
    if (processor->localAudioListener.name == "Local Listener")
    {
        for (char letter = 'A'; letter <= 'Z'; ++letter)
        {
            juce::String letterStr = juce::String::charToString(static_cast<juce_wchar>(letter));
            bool taken = false;
            for (const auto& pair : processor->remoteListeners)
                if (pair.second.name == letterStr) { taken = true; break; }

            if (!taken)
            {
                processor->localAudioListener.name = letterStr;
                break;
            }
        }
    }

    setListenerState(processor->localAudioListener.id,
                     processor->localAudioListener.name,
                     processor->localAudioListener.x,
                     processor->localAudioListener.y,
                     false,
                     processor->localAudioListener.locked,
                     processor);

    if (processor->onStateChanged) processor->onStateChanged();
}

void IrisOSCManager::removeProcessor(IrisAudioProcessor* processor)
{
    juce::Uuid ghostId = processor->localAudioListener.id;
    {
        juce::ScopedLock sl(listLock);
        processors.removeFirstMatchingValue(processor);
    }

    juce::OSCMessage m("/iris/listener/remove");
    m.addString(ghostId.toString());
    sendOSC(m);

    removeGhostId(ghostId);
}

void IrisOSCManager::notifyProcessors(const std::function<void(IrisAudioProcessor*)>& callback,
                                       IrisAudioProcessor* exclude)
{
    juce::ScopedLock sl(listLock);
    for (auto* p : processors)
        if (p != nullptr && p != exclude)
            callback(p);
}

void IrisOSCManager::sendOSC(const juce::OSCMessage& message)
{
    oscSender.send(message);
}

// ---------------------------------------------------------------------------
// Incoming OSC dispatch
// ---------------------------------------------------------------------------

void IrisOSCManager::oscMessageReceived(const juce::OSCMessage& message)
{
    const auto addr = message.getAddressPattern();

    if (addr == "/iris/listener/sync" && message.size() >= 6
        && message[0].isString() && message[1].isString()
        && message[2].isFloat32() && message[3].isFloat32()
        && message[4].isInt32()   && message[5].isInt32())
    {
        juce::Uuid   id     (message[0].getString());
        juce::String name  = message[1].getString();
        float        x     = message[2].getFloat32();
        float        y     = message[3].getFloat32();
        bool         locked = message[5].getInt32() != 0;

        notifyProcessors([id, name, x, y, locked](IrisAudioProcessor* p)
        {
            juce::ScopedLock sl(p->stateLock);
            if (id != p->localAudioListener.id)
            {
                if (p->remoteListeners.find(id) == p->remoteListeners.end())
                {
                    IrisAudioProcessor::NetworkListener remote;
                    remote.id     = id;
                    remote.name   = name;
                    remote.x      = juce::jlimit(0.0f, 1.0f, x);
                    remote.y      = juce::jlimit(0.0f, 1.0f, y);
                    remote.isLocal = false;
                    remote.locked = locked;
                    p->remoteListeners[id] = remote;
                }
                else
                {
                    p->remoteListeners[id].name   = name;
                    p->remoteListeners[id].locked = locked;
                    p->updateListenerPosition(id, x, y, false);
                }
            }
            else
            {
                p->localAudioListener.name   = name;
                p->localAudioListener.locked = locked;
                p->updateListenerPosition(id, x, y, false);
            }
            if (p->onStateChanged) p->onStateChanged();
        });
    }
    else if (addr == "/iris/listener/matrix" && message.size() == 2
             && message[0].isString() && message[1].isString())
    {
        juce::String source    = message[0].getString();
        juce::String matrixStr = message[1].getString();

        std::vector<std::pair<juce::String, juce::String>> newEdges;
        if (matrixStr.isNotEmpty())
        {
            juce::StringArray pairs;
            pairs.addTokens(matrixStr, ",", "");
            for (const auto& pStr : pairs)
            {
                juce::StringArray parts;
                parts.addTokens(pStr, ":", "");
                if (parts.size() == 2)
                    newEdges.push_back({ parts[0], parts[1] });
            }
        }

        juce::MessageManager::callAsync([this, newEdges, source]()
        {
            juce::ScopedLock sl(listLock);
            for (auto* p : processors)
                if (p != nullptr && p->localAudioListener.id.toString() != source)
                    p->setLinkMatrixConnections(newEdges);
        });
    }
    else if (addr == "/iris/listener/remove" && message.size() == 1 && message[0].isString())
    {
        removeGhostId(juce::Uuid(message[0].getString()));
    }
    else if (addr == "/iris/param/mix" && message.size() == 1 && message[0].isFloat32())
    {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisAudioProcessor* p)
        {
            if (p->mixParam) p->mixParam->store(val);
            p->updateParameterNotifiers("mix", val);
        });
    }
    else if (addr == "/iris/param/spread" && message.size() == 1 && message[0].isFloat32())
    {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisAudioProcessor* p)
        {
            if (p->spreadParam) p->spreadParam->store(val);
            p->updateParameterNotifiers("spread", val);
        });
    }
    else if (addr == "/iris/param/inertia" && message.size() == 1 && message[0].isFloat32())
    {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisAudioProcessor* p)
        {
            if (p->inertiaParam) p->inertiaParam->store(val);
            p->updateParameterNotifiers("inertia", val);
        });
    }
    else if (addr == "/iris/param/freeze" && message.size() == 1 && message[0].isFloat32())
    {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisAudioProcessor* p)
        {
            if (p->freezeParam) p->freezeParam->store(val);
            p->updateParameterNotifiers("freeze", val);
        });
    }
    else if (addr == "/iris/param/wallOpacity" && message.size() == 1 && message[0].isFloat32())
    {
        float val = message[0].getFloat32();
        notifyProcessors([val](IrisAudioProcessor* p)
        {
            if (p->wallOpacityParam) p->wallOpacityParam->store(val);
            p->updateParameterNotifiers("wallOpacity", val);
        });
    }
    else if (addr == "/iris/ir/name" && message.size() == 2
             && message[0].isString() && message[1].isString())
    {
        juce::Uuid   id  (message[0].getString());
        juce::String name = message[1].getString();
        notifyProcessors([id, name](IrisAudioProcessor* p) { p->setPointName(id, name, false); });
    }
}

// ---------------------------------------------------------------------------
// Outbound sync
// ---------------------------------------------------------------------------

void IrisOSCManager::setListenerState(const juce::Uuid& id, const juce::String& name,
                                       float x, float y, bool linked, bool locked,
                                       IrisAudioProcessor* source)
{
    notifyProcessors([id, name, x, y, locked](IrisAudioProcessor* p)
    {
        juce::ScopedLock sl(p->stateLock);
        if (id != p->localAudioListener.id)
        {
            if (p->remoteListeners.find(id) == p->remoteListeners.end())
            {
                IrisAudioProcessor::NetworkListener remote;
                remote.id      = id;
                remote.name    = name;
                remote.x       = juce::jlimit(0.0f, 1.0f, x);
                remote.y       = juce::jlimit(0.0f, 1.0f, y);
                remote.isLocal = false;
                remote.locked  = locked;
                p->remoteListeners[id] = remote;
            }
            else
            {
                p->remoteListeners[id].name   = name;
                p->remoteListeners[id].locked = locked;
                p->updateListenerPosition(id, x, y, false);
            }
        }
        else
        {
            p->localAudioListener.name   = name;
            p->localAudioListener.locked = locked;
            p->updateListenerPosition(id, x, y, false);
        }
        if (p->onStateChanged) p->onStateChanged();
    }, source);

    juce::OSCMessage m("/iris/listener/sync");
    m.addString(id.toString());
    m.addString(name);
    m.addFloat32(x);
    m.addFloat32(y);
    m.addInt32(linked ? 1 : 0);
    m.addInt32(locked ? 1 : 0);
    sendOSC(m);
}

void IrisOSCManager::syncLinkMatrix(IrisAudioProcessor* caller)
{
    if (!isConnected) return;

    juce::StringArray edges;
    for (const auto& edge : caller->linkMatrix)
        edges.add(edge.first + ":" + edge.second);

    juce::OSCMessage m("/iris/listener/matrix",
                       caller->localAudioListener.id.toString(),
                       edges.joinIntoString(","));
    sendOSC(m);
}

void IrisOSCManager::setGlobalParam(const juce::String& paramId, float value, IrisAudioProcessor* source)
{
    notifyProcessors([paramId, value](IrisAudioProcessor* p)
    {
        if      (paramId == "mix")         { if (p->mixParam)         p->mixParam->store(value); }
        else if (paramId == "spread")      { if (p->spreadParam)      p->spreadParam->store(value); }
        else if (paramId == "inertia")     { if (p->inertiaParam)     p->inertiaParam->store(value); }
        else if (paramId == "freeze")      { if (p->freezeParam)      p->freezeParam->store(value); }
        else if (paramId == "wallOpacity") { if (p->wallOpacityParam) p->wallOpacityParam->store(value); }

        p->updateParameterNotifiers(paramId, value);
    }, source);

    juce::OSCMessage m("/iris/param/" + paramId);
    m.addFloat32(value);
    sendOSC(m);
}

void IrisOSCManager::syncAddIR(const juce::Uuid& id, const juce::String& name,
                                const juce::File& file, IrisAudioProcessor* source)
{
    notifyProcessors([id, file](IrisAudioProcessor* p) { p->addIRFromFileWithID(file, id); }, source);

    juce::OSCMessage m("/iris/ir/add");
    m.addString(id.toString());
    m.addString(name);
    m.addString(file.getFullPathName());
    sendOSC(m);
}

void IrisOSCManager::syncRemoveIR(const juce::Uuid& id, IrisAudioProcessor* source)
{
    notifyProcessors([id](IrisAudioProcessor* p) { p->removePoint(id, false); }, source);

    juce::OSCMessage m("/iris/ir/remove");
    m.addString(id.toString());
    sendOSC(m);
}

void IrisOSCManager::syncIRPosition(const juce::Uuid& id, float x, float y, IrisAudioProcessor* source)
{
    notifyProcessors([id, x, y](IrisAudioProcessor* p) { p->updatePointPosition(id, x, y, false); }, source);

    juce::OSCMessage m("/iris/ir/pos");
    m.addString(id.toString());
    m.addFloat32(x);
    m.addFloat32(y);
    sendOSC(m);
}

void IrisOSCManager::syncIRName(const juce::Uuid& id, const juce::String& name, IrisAudioProcessor* source)
{
    notifyProcessors([id, name](IrisAudioProcessor* p) { p->setPointName(id, name, false); }, source);

    juce::OSCMessage m("/iris/ir/name");
    m.addString(id.toString());
    m.addString(name);
    sendOSC(m);
}

void IrisOSCManager::syncLocked(const juce::Uuid& id, bool locked, IrisAudioProcessor* source)
{
    notifyProcessors([id, locked](IrisAudioProcessor* p) { p->setPointLocked(id, locked, false); }, source);
}

void IrisOSCManager::syncAddWall(const juce::Uuid& id, float x1, float y1, float x2, float y2,
                                  IrisAudioProcessor* source)
{
    notifyProcessors([id, x1, y1, x2, y2](IrisAudioProcessor* p)
    {
        p->addWallWithID(id, x1, y1, x2, y2);
    }, source);

    juce::OSCMessage m("/iris/wall/add");
    m.addString(id.toString());
    m.addFloat32(x1); m.addFloat32(y1);
    m.addFloat32(x2); m.addFloat32(y2);
    sendOSC(m);
}

void IrisOSCManager::syncRemoveWall(const juce::Uuid& id, IrisAudioProcessor* source)
{
    notifyProcessors([id](IrisAudioProcessor* p) { p->removeWall(id, false); }, source);

    juce::OSCMessage m("/iris/wall/remove");
    m.addString(id.toString());
    sendOSC(m);
}

void IrisOSCManager::syncWallPosition(const juce::Uuid& id, float x1, float y1, float x2, float y2,
                                       IrisAudioProcessor* source)
{
    notifyProcessors([id, x1, y1, x2, y2](IrisAudioProcessor* p)
    {
        p->updateWall(id, x1, y1, x2, y2, false);
    }, source);

    juce::OSCMessage m("/iris/wall/pos");
    m.addString(id.toString());
    m.addFloat32(x1); m.addFloat32(y1);
    m.addFloat32(x2); m.addFloat32(y2);
    sendOSC(m);
}

void IrisOSCManager::requestFullSync(IrisAudioProcessor* requester)
{
    if (requester == nullptr) return;

    setListenerState(requester->localAudioListener.id,
                     requester->localAudioListener.name,
                     requester->localAudioListener.x,
                     requester->localAudioListener.y,
                     false,
                     requester->localAudioListener.locked,
                     requester);

    syncLinkMatrix(requester);

    if (requester->mixParam)         setGlobalParam("mix",         requester->mixParam->load(),         requester);
    if (requester->spreadParam)      setGlobalParam("spread",      requester->spreadParam->load(),      requester);
    if (requester->inertiaParam)     setGlobalParam("inertia",     requester->inertiaParam->load(),     requester);
    if (requester->freezeParam)      setGlobalParam("freeze",      requester->freezeParam->load(),      requester);
    if (requester->wallOpacityParam) setGlobalParam("wallOpacity", requester->wallOpacityParam->load(), requester);

    juce::ScopedLock sl(requester->stateLock);
    for (const auto& w : requester->walls)
        syncAddWall(w.id, w.x1, w.y1, w.x2, w.y2, requester);

    for (const auto& p : requester->points)
    {
        syncAddIR(p.id, p.name, p.sourceFile, requester);
        syncIRPosition(p.id, p.x, p.y, requester);
        syncLocked(p.id, p.locked, requester);
        syncIRName(p.id, p.name, requester);
    }
}

void IrisOSCManager::removeGhostId(const juce::Uuid& ghostId)
{
    juce::ScopedLock sl(listLock);
    for (auto* p : processors)
    {
        if (p != nullptr)
        {
            juce::ScopedLock slState(p->stateLock);
            p->remoteListeners.erase(ghostId);
            if (p->selectedListenerId == ghostId)
                p->selectedListenerId = p->localAudioListener.id;
            if (p->onStateChanged) p->onStateChanged();
        }
    }
}
