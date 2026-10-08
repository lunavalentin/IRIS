#include "IrisOSCManager.h"
#include "PluginProcessor.h"

// ---------------------------------------------------------------------------
// Input validation helpers
// ---------------------------------------------------------------------------

static bool isFiniteFloatArg(const juce::OSCArgument& a)
{
    return a.isFloat32() && std::isfinite(a.getFloat32());
}

// Accepts only strings that round-trip as a UUID (rejects garbage and the null id).
static bool parseUuid(const juce::OSCArgument& a, juce::Uuid& out)
{
    if (! a.isString()) return false;
    const auto s = a.getString().trim();
    juce::Uuid id(s);
    if (id.isNull() || ! id.toString().equalsIgnoreCase(s.removeCharacters("-{}"))) return false;
    out = id;
    return true;
}

static bool isUuidString(const juce::String& s)
{
    juce::Uuid id(s);
    return ! id.isNull() && id.toString().equalsIgnoreCase(s.trim().removeCharacters("-{}"));
}

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
    {
        juce::ScopedLock sl(listLock);
        if (processors.contains(processor)) return;

        // Seed the new instance with the listeners every other instance knows about.
        if (processors.size() > 0)
        {
            auto* syncSource = processors[0];
            juce::ScopedLock slState(processor->stateLock);
            juce::ScopedLock slSrc(syncSource->stateLock);

            for (const auto& pair : syncSource->remoteListeners)
                if (pair.first != processor->localAudioListener.id)
                    processor->remoteListeners[pair.first] = pair.second;

            IrisAudioProcessor::NetworkListener srcRemote;
            srcRemote.id      = syncSource->localAudioListener.id;
            srcRemote.name    = syncSource->localAudioListener.name;
            srcRemote.x       = srcRemote.currentX = syncSource->localAudioListener.x;
            srcRemote.y       = srcRemote.currentY = syncSource->localAudioListener.y;
            srcRemote.locked  = syncSource->localAudioListener.locked;
            srcRemote.isLocal = false;
            processor->remoteListeners[srcRemote.id] = srcRemote;

            processor->linkMatrix = syncSource->linkMatrix;
        }

        processors.add(processor);
    }

    // Assign the next available single-letter name if this is a fresh instance.
    {
        juce::ScopedLock slState(processor->stateLock);
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
    }

    setListenerState(processor->localAudioListener.id,
                     processor->localAudioListener.name,
                     processor->localAudioListener.x,
                     processor->localAudioListener.y,
                     false,
                     processor->localAudioListener.locked,
                     processor);

    processor->notifyStructuralChange();
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

void IrisOSCManager::applyListenerState(IrisAudioProcessor* p, const juce::Uuid& id, const juce::String& name,
                                         float x, float y, bool locked)
{
    {
        juce::ScopedLock sl(p->stateLock);

        if (id == p->localAudioListener.id)
        {
            const bool wasLocked = p->localAudioListener.locked;
            p->localAudioListener.name   = name;
            p->localAudioListener.locked = locked;

            // A locked local listener never moves because of a remote message.
            if (! wasLocked || ! locked)
            {
                p->localAudioListener.x = juce::jlimit(0.0f, 1.0f, x);
                p->localAudioListener.y = juce::jlimit(0.0f, 1.0f, y);
            }
        }
        else if (auto it = p->remoteListeners.find(id); it != p->remoteListeners.end())
        {
            it->second.name   = name;
            it->second.locked = locked;
            it->second.x      = juce::jlimit(0.0f, 1.0f, x);
            it->second.y      = juce::jlimit(0.0f, 1.0f, y);
        }
        else
        {
            if (p->remoteListeners.size() >= IrisAudioProcessor::kMaxRemoteListeners)
                return;

            IrisAudioProcessor::NetworkListener remote;
            remote.id      = id;
            remote.name    = name;
            remote.x       = remote.currentX = juce::jlimit(0.0f, 1.0f, x);
            remote.y       = remote.currentY = juce::jlimit(0.0f, 1.0f, y);
            remote.isLocal = false;
            remote.locked  = locked;
            p->remoteListeners[id] = remote;
        }
    }

    p->notifyStructuralChange();
}

// ---------------------------------------------------------------------------
// Incoming OSC dispatch (message thread). Every value is validated: wrong types,
// NaN/Inf and malformed ids are dropped instead of reaching the audio path.
// ---------------------------------------------------------------------------

void IrisOSCManager::oscMessageReceived(const juce::OSCMessage& message)
{
    const auto addr = message.getAddressPattern();

    if (addr == "/iris/listener/sync" && message.size() >= 6
        && message[1].isString()
        && isFiniteFloatArg(message[2]) && isFiniteFloatArg(message[3])
        && message[4].isInt32() && message[5].isInt32())
    {
        juce::Uuid id;
        if (! parseUuid(message[0], id)) return;

        const juce::String name   = message[1].getString().substring(0, 64);
        const float        x      = message[2].getFloat32();
        const float        y      = message[3].getFloat32();
        const bool         locked = message[5].getInt32() != 0;

        notifyProcessors([id, name, x, y, locked](IrisAudioProcessor* p)
        {
            applyListenerState(p, id, name, x, y, locked);
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
                if (parts.size() == 2 && parts[0] != parts[1]
                    && isUuidString(parts[0]) && isUuidString(parts[1]))
                    newEdges.push_back({ parts[0], parts[1] });

                if (newEdges.size() > 4096) return;
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
    else if (addr == "/iris/listener/remove" && message.size() == 1)
    {
        juce::Uuid id;
        if (parseUuid(message[0], id))
            removeGhostId(id);
    }
    else if (addr.toString().startsWith("/iris/param/") && message.size() == 1 && isFiniteFloatArg(message[0]))
    {
        const auto  paramId = addr.toString().fromFirstOccurrenceOf("/iris/param/", false, false);
        const float val     = message[0].getFloat32();

        // Only the parameters that are broadcast between instances can be set over OSC,
        // and each instance only accepts the ones it has enabled in the broadcast menu.
        notifyProcessors([paramId, val](IrisAudioProcessor* p)
        {
            bool accept = false;
            if      (paramId == "mix")         accept = p->broadcastMix;
            else if (paramId == "spread")      accept = p->broadcastSpread;
            else if (paramId == "inertia")     accept = p->broadcastInertia;
            else if (paramId == "freeze")      accept = p->broadcastFreeze;
            else if (paramId == "wallOpacity") accept = p->broadcastWallOpacity;
            else if (paramId == "normalize")   accept = p->broadcastNormalize;
            else if (paramId == "align")       accept = p->broadcastAlign;

            if (accept)
                p->updateParameterNotifiers(paramId, val);
        });
    }
    else if (addr == "/iris/ir/name" && message.size() == 2 && message[1].isString())
    {
        juce::Uuid id;
        if (! parseUuid(message[0], id)) return;
        juce::String name = message[1].getString().substring(0, 128);
        notifyProcessors([id, name](IrisAudioProcessor* p) { if (p->broadcastIRs) p->setPointName(id, name, false); });
    }
    else if (addr == "/iris/ir/pos" && message.size() == 3
             && isFiniteFloatArg(message[1]) && isFiniteFloatArg(message[2]))
    {
        juce::Uuid id;
        if (! parseUuid(message[0], id)) return;
        const float x = message[1].getFloat32(), y = message[2].getFloat32();
        notifyProcessors([id, x, y](IrisAudioProcessor* p) { if (p->broadcastIRs) p->updatePointPosition(id, x, y, false); });
    }
    else if (addr == "/iris/wall/pos" && message.size() == 5
             && isFiniteFloatArg(message[1]) && isFiniteFloatArg(message[2])
             && isFiniteFloatArg(message[3]) && isFiniteFloatArg(message[4]))
    {
        juce::Uuid id;
        if (! parseUuid(message[0], id)) return;
        const float x1 = message[1].getFloat32(), y1 = message[2].getFloat32();
        const float x2 = message[3].getFloat32(), y2 = message[4].getFloat32();
        notifyProcessors([id, x1, y1, x2, y2](IrisAudioProcessor* p) { if (p->broadcastWalls) p->updateWall(id, x1, y1, x2, y2, false); });
    }
}

// ---------------------------------------------------------------------------
// Outbound sync. Callers must not hold any processor's stateLock.
// ---------------------------------------------------------------------------

void IrisOSCManager::setListenerState(const juce::Uuid& id, const juce::String& name,
                                       float x, float y, bool linked, bool locked,
                                       IrisAudioProcessor* source)
{
    if (! std::isfinite(x) || ! std::isfinite(y)) return;

    notifyProcessors([id, name, x, y, locked](IrisAudioProcessor* p)
    {
        applyListenerState(p, id, name, x, y, locked);
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

void IrisOSCManager::syncLinkMatrix(IrisAudioProcessor* caller,
                                     const std::vector<std::pair<juce::String, juce::String>>& edges)
{
    // Instances in this process share the same graph.
    notifyProcessors([&edges](IrisAudioProcessor* p) { p->setLinkMatrixConnections(edges); }, caller);

    juce::StringArray edgeStrings;
    for (const auto& edge : edges)
        edgeStrings.add(edge.first + ":" + edge.second);

    juce::OSCMessage m("/iris/listener/matrix",
                       caller->localAudioListener.id.toString(),
                       edgeStrings.joinIntoString(","));
    sendOSC(m);
}

void IrisOSCManager::setGlobalParam(const juce::String& paramId, float value, IrisAudioProcessor* source)
{
    if (! std::isfinite(value)) return;

    notifyProcessors([paramId, value](IrisAudioProcessor* p)
    {
        // Go through the parameter object only (not the raw atomic), so the APVTS
        // state tree, the host and the UI attachments all see the change.
        p->updateParameterNotifiers(paramId, value);
    }, source);

    juce::OSCMessage m("/iris/param/" + paramId);
    m.addFloat32(value);
    sendOSC(m);
}

void IrisOSCManager::syncAddIR(const juce::Uuid& id, const juce::String& name,
                                const juce::File& file, IrisAudioProcessor* source)
{
    notifyProcessors([id, file, name](IrisAudioProcessor* p) { p->addIRFromFileWithID(file, id, name); }, source);

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

    // Snapshot under the requester's lock, then send without holding it.
    IrisAudioProcessor::NetworkListener local;
    std::vector<OcclusionWall> walls;
    std::vector<std::pair<juce::String, juce::String>> edges;
    struct IRSnap { juce::Uuid id; juce::String name; juce::File file; float x, y; bool locked; };
    std::vector<IRSnap> irs;
    {
        juce::ScopedLock sl(requester->stateLock);
        local = requester->localAudioListener;
        walls = requester->walls;
        edges.assign(requester->linkMatrix.begin(), requester->linkMatrix.end());
        for (const auto& p : requester->points)
            irs.push_back({ p.id, p.name, p.sourceFile, p.x, p.y, p.locked });
    }

    setListenerState(local.id, local.name, local.x, local.y, false, local.locked, requester);
    syncLinkMatrix(requester, edges);

    if (requester->mixParam)         setGlobalParam("mix",         requester->mixParam->load(),         requester);
    if (requester->spreadParam)      setGlobalParam("spread",      requester->spreadParam->load(),      requester);
    if (requester->inertiaParam)     setGlobalParam("inertia",     requester->inertiaParam->load(),     requester);
    if (requester->freezeParam)      setGlobalParam("freeze",      requester->freezeParam->load(),      requester);
    if (requester->wallOpacityParam) setGlobalParam("wallOpacity", requester->wallOpacityParam->load(), requester);

    for (const auto& w : walls)
        syncAddWall(w.id, w.x1, w.y1, w.x2, w.y2, requester);

    for (const auto& p : irs)
    {
        syncAddIR(p.id, p.name, p.file, requester);
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
        if (p == nullptr) continue;
        {
            juce::ScopedLock slState(p->stateLock);
            p->remoteListeners.erase(ghostId);
            if (p->selectedListenerId == ghostId)
                p->selectedListenerId = p->localAudioListener.id;
        }
        p->notifyStructuralChange();
    }
}
