#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "IrisOSCManager.h"
#include <queue>
#include <optional>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool getIntersectionPoint(float ax, float ay, float bx, float by,
                                  float cx, float cy, float dx, float dy,
                                  float& ix, float& iy, float& tWall)
{
    float d = (dy - cy) * (bx - ax) - (dx - cx) * (by - ay);
    if (std::abs(d) < 1e-10f) return false;

    float ua = ((dx - cx) * (ay - cy) - (dy - cy) * (ax - cx)) / d;
    float ub = ((bx - ax) * (ay - cy) - (by - ay) * (ax - cx)) / d;

    if (ua >= 0.0f && ua <= 1.0f && ub >= 0.0f && ub <= 1.0f)
    {
        ix    = ax + ua * (bx - ax);
        iy    = ay + ua * (by - ay);
        tWall = ub;
        return true;
    }
    return false;
}

static float distSq(float x1, float y1, float x2, float y2)
{
    return (x1-x2)*(x1-x2) + (y1-y2)*(y1-y2);
}

static void closestPointOnSegment(float px, float py,
                                   float x1, float y1, float x2, float y2,
                                   float& outX, float& outY)
{
    float l2 = distSq(x1, y1, x2, y2);
    if (l2 < 1e-10f) { outX = x1; outY = y1; return; }

    float t = ((px - x1) * (x2 - x1) + (py - y1) * (y2 - y1)) / l2;
    t = juce::jlimit(0.0f, 1.0f, t);
    outX = x1 + t * (x2 - x1);
    outY = y1 + t * (y2 - y1);
}

// Finite value clamped to [lo, hi], or the fallback for NaN/Inf.
static float sanitise (double v, float fallback, float lo, float hi)
{
    if (! std::isfinite (v)) return fallback;
    return juce::jlimit (lo, hi, static_cast<float> (v));
}

static float unitCoord (double v, float fallback = 0.5f) { return sanitise (v, fallback, 0.0f, 1.0f); }

// Parameters that may be broadcast to other instances (bit index = position).
static const char* const kBroadcastParamIds[] = { "inertia", "freeze", "spread", "mix", "wallOpacity", "normalize", "align" };

// ---------------------------------------------------------------------------
// Convolution worker threads
// ---------------------------------------------------------------------------
// Persistent realtime threads that help the audio thread run convolution jobs.
// No allocation, no locks: jobs are handed out through one atomic word that
// packs (jobCount << 32 | nextIndex), so a worker that is late from the previous
// block can only ever pick up a valid job of the current block.

class IrisAudioProcessor::ConvolutionWorkers
{
public:
    ConvolutionWorkers (IrisAudioProcessor& o, int numThreads) : owner (o)
    {
        for (int i = 0; i < numThreads; ++i)
            threads.push_back (std::make_unique<Worker> (*this, i));

        for (auto& t : threads)
            if (! t->startRealtimeThread (juce::Thread::RealtimeOptions{}.withPriority (8)))
                t->startThread (juce::Thread::Priority::highest);
    }

    ~ConvolutionWorkers()
    {
        for (auto& t : threads) t->signalThreadShouldExit();
        for (auto& t : threads) t->wake.signal();
        for (auto& t : threads) t->stopThread (2000);
    }

    // Audio thread. Returns once every job has finished.
    void run (int numJobs) noexcept
    {
        done.store (0, std::memory_order_relaxed);
        work.store (static_cast<uint64_t> (numJobs) << 32, std::memory_order_release);

        const int helpers = std::min (static_cast<int> (threads.size()), numJobs - 1);
        for (int i = 0; i < helpers; ++i)
            threads[static_cast<size_t> (i)]->wake.signal();

        drain();

        while (done.load (std::memory_order_acquire) < numJobs)
            finished.wait (1);
    }

    void setWorkgroup (const juce::AudioWorkgroup& wg)
    {
        const juce::SpinLock::ScopedLockType sl (workgroupLock);
        workgroup = wg;
        workgroupGeneration.fetch_add (1);
    }

private:
    void drain() noexcept
    {
        for (;;)
        {
            const auto v     = work.fetch_add (1, std::memory_order_acq_rel);
            const auto total = static_cast<int> (v >> 32);
            const auto index = static_cast<int> (v & 0xffffffffu);
            if (index >= total) return;

            owner.runConvJob (index);

            if (done.fetch_add (1, std::memory_order_acq_rel) + 1 == total)
                finished.signal();
        }
    }

    struct Worker : public juce::Thread
    {
        Worker (ConvolutionWorkers& p, int i) : juce::Thread ("IRIS conv " + juce::String (i)), pool (p) {}

        void run() override
        {
            juce::WorkgroupToken token;
            int joinedGeneration = -1;

            while (! threadShouldExit())
            {
                if (! wake.wait (100)) continue;
                if (threadShouldExit()) break;

                const int g = pool.workgroupGeneration.load();
                if (g != joinedGeneration)
                {
                    juce::AudioWorkgroup wg;
                    {
                        const juce::SpinLock::ScopedLockType sl (pool.workgroupLock);
                        wg = pool.workgroup;
                    }
                    token.reset();
                    if (wg) wg.join (token);
                    joinedGeneration = g;
                }

                pool.drain();
            }
        }

        ConvolutionWorkers& pool;
        juce::WaitableEvent wake;
    };

    IrisAudioProcessor& owner;
    std::vector<std::unique_ptr<Worker>> threads;
    std::atomic<uint64_t> work { 0 };
    std::atomic<int>      done { 0 };
    juce::WaitableEvent   finished;

    juce::SpinLock        workgroupLock;
    juce::AudioWorkgroup  workgroup;
    std::atomic<int>      workgroupGeneration { 0 };
};

// ---------------------------------------------------------------------------
// Parameter layout
// ---------------------------------------------------------------------------

juce::AudioProcessorValueTreeState::ParameterLayout IrisAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // w1..w3 are kept so existing sessions and automation lanes still load. They are
    // no longer written by the plugin and are hidden from automation.
    auto hidden = juce::AudioParameterFloatAttributes().withAutomatable (false);
    layout.add(std::make_unique<juce::AudioParameterFloat>("w1", "Weight 1", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, hidden));
    layout.add(std::make_unique<juce::AudioParameterFloat>("w2", "Weight 2", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, hidden));
    layout.add(std::make_unique<juce::AudioParameterFloat>("w3", "Weight 3", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, hidden));

    layout.add(std::make_unique<juce::AudioParameterFloat>("inertia",     "Inertia",       0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool> ("freeze",      "Freeze",        false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("spread",      "Spread",        0.0f, 1.0f, 0.3f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("mix",         "Mix",           0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("wallOpacity", "Wall Opacity",  0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterBool> ("normalize",   "Normalize",     true));
    layout.add(std::make_unique<juce::AudioParameterBool> ("align",       "Align",         true));

    // IRs are normalised to unity energy, so 0 dB keeps the dry signal at unity
    // and the wet signal at roughly the same loudness.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain",
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f),
        0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("listenerX", "Listener X", 0.0f, 1.0f, 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("listenerY", "Listener Y", 0.0f, 1.0f, 0.5f));

    return layout;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

IrisAudioProcessor::IrisAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                     .withInput  ("Input",  juce::AudioChannelSet::mono(),   true)
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout()),
      oscManager(IrisOSCManager::getInstance())
#endif
{
    weight1 = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("w1"));
    weight2 = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("w2"));
    weight3 = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("w3"));

    inertiaParam     = parameters.getRawParameterValue("inertia");
    freezeParam      = parameters.getRawParameterValue("freeze");
    spreadParam      = parameters.getRawParameterValue("spread");
    mixParam         = parameters.getRawParameterValue("mix");
    wallOpacityParam = parameters.getRawParameterValue("wallOpacity");
    normalizeParam   = parameters.getRawParameterValue("normalize");
    alignParam       = parameters.getRawParameterValue("align");
    outputGainParam  = parameters.getRawParameterValue("outputGain");
    listenerXParam   = parameters.getRawParameterValue("listenerX");
    listenerYParam   = parameters.getRawParameterValue("listenerY");

    formatManager.registerBasicFormats();
    renderState = std::make_shared<RenderState>();

    localAudioListener.id     = juce::Uuid();
    localAudioListener.name   = "Local Listener";
    localAudioListener.isLocal = true;
    localAudioListener.x      = 0.5f;
    localAudioListener.y      = 0.5f;
    selectedListenerId = localAudioListener.id;

    for (auto* id : kBroadcastParamIds)
        parameters.addParameterListener(id, this);
    parameters.addParameterListener("listenerX",   this);
    parameters.addParameterListener("listenerY",   this);

    startTimerHz(60);
    oscManager.addProcessor(this);
}

IrisAudioProcessor::~IrisAudioProcessor()
{
    stopTimer();
    oscManager.removeProcessor(this);
    workers.reset();
}

// ---------------------------------------------------------------------------
// AudioProcessor overrides
// ---------------------------------------------------------------------------

const juce::String IrisAudioProcessor::getName() const { return JucePlugin_Name; }

bool IrisAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool IrisAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool IrisAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double IrisAudioProcessor::getTailLengthSeconds() const { return cachedTailSeconds.load(); }

int  IrisAudioProcessor::getNumPrograms()                               { return 1; }
int  IrisAudioProcessor::getCurrentProgram()                            { return 0; }
void IrisAudioProcessor::setCurrentProgram (int)                        {}
const juce::String IrisAudioProcessor::getProgramName (int)            { return {}; }
void IrisAudioProcessor::changeProgramName (int, const juce::String&)  {}

bool IrisAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* IrisAudioProcessor::createEditor()
{
    return new IrisAudioProcessorEditor(*this);
}

// ---------------------------------------------------------------------------
// Bus layout
// ---------------------------------------------------------------------------

#ifndef JucePlugin_PreferredChannelConfigurations
bool IrisAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
  #else
    auto inputSet  = layouts.getMainInputChannelSet();
    auto outputSet = layouts.getMainOutputChannelSet();

    if (inputSet.isDisabled() || outputSet.isDisabled())
        return false;

    // Mono input can drive any output channel count.
    // This is the primary ambisonics use case: one dry source convolved
    // through each channel of an N-channel IR.
    if (inputSet == juce::AudioChannelSet::mono())
        return outputSet.size() > 0 && outputSet.size() <= kMaxIRChannels;

    // For multi-channel inputs (stereo, FOA, SOA, …) the output must match
    // the input exactly. This ensures the IR channel count is meaningful and
    // prevents REAPER from silently padding input channels with zeros.
    return inputSet == outputSet;
  #endif
}
#endif

// ---------------------------------------------------------------------------
// Prepare / Release
// ---------------------------------------------------------------------------

void IrisAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (workers == nullptr)
        workers = std::make_unique<ConvolutionWorkers>(*this, std::clamp (juce::SystemStats::getNumCpus() - 2, 1, 7));

    // Some hosts call prepareToPlay while the audio callback is still running
    // (e.g. when DSP is switched on or the block size changes). Resizing the
    // buffers below would then race with processBlock and can leave it writing
    // through a null/dangling channel pointer. JUCE wrappers call processBlock
    // under getCallbackLock(), so holding it here serialises the two.
    const juce::ScopedLock cbLock (getCallbackLock());

    // Guard against hosts that report 0 (or nonsense) as the maximum block size.
    if (samplesPerBlock <= 0) samplesPerBlock = 512;

    const int numOut = std::max(getTotalNumOutputChannels(), 1);
    const int numIn  = std::max(getTotalNumInputChannels(),  1);

    // Rebuilding every convolver is expensive and cuts reverb tails, so only do it
    // when something the convolvers depend on actually changed.
    const bool specChanged = sampleRate != lastPreparedRate || samplesPerBlock != lastPreparedBlock
                          || numIn != lastPreparedIn || numOut != lastPreparedOut;

    processSpec.sampleRate       = sampleRate;
    processSpec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    processSpec.numChannels      = 1;

    // Pre-allocate all audio-thread buffers.
    // avoidReallocating=true means these are no-ops if the size hasn't changed.
    inputBuffer.setSize(numIn, samplesPerBlock, false, false, true);
    for (auto& buf : irScratchBuffers)
        buf.setSize(numOut, samplesPerBlock, false, false, true);

    {
        juce::ScopedLock sl(stateLock);

        if (specChanged)
        {
            for (auto& p : points)
                updateConvolver(p);

            // Fades still reference convolvers prepared for the old spec.
            pendingFadeOuts.clear();
        }
        else
        {
            for (auto& p : points)
                for (auto& c : p.convolvers)
                    if (c) c->reset();
        }

        rebuildRenderState (true);
    }

    numGainTracks = 0;
    numFadeTracks = 0;
    lastWet = lastDry = lastOutGain = -1.0f;

    lastPreparedRate  = sampleRate;
    lastPreparedBlock = samplesPerBlock;
    lastPreparedIn    = numIn;
    lastPreparedOut   = numOut;

    preparedBlockSize.store(samplesPerBlock);
}


void IrisAudioProcessor::releaseResources() {}

void IrisAudioProcessor::audioWorkgroupContextChanged (const juce::AudioWorkgroup& workgroup)
{
    if (workers != nullptr)
        workers->setWorkgroup (workgroup);
}

// ---------------------------------------------------------------------------
// Process block
// ---------------------------------------------------------------------------

void IrisAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int maxBlock   = preparedBlockSize.load();
    const int numSamples = buffer.getNumSamples();

    // Not prepared yet (host called process before prepareToPlay): output silence
    // rather than touching unallocated buffers.
    if (maxBlock <= 0 || inputBuffer.getNumSamples() < maxBlock || numSamples <= 0)
    {
        buffer.clear();
        return;
    }

    if (numSamples <= maxBlock)
    {
        processSubBlock(buffer);
        return;
    }

    // Host sent a larger block than it announced in prepareToPlay.
    // Process it in prepared-size slices instead of overrunning our buffers.
    const int numCh = buffer.getNumChannels();
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int len = std::min(maxBlock, numSamples - start);
        juce::AudioBuffer<float> slice(buffer.getArrayOfWritePointers(), numCh, start, len);
        processSubBlock(slice);
    }
}

void IrisAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // The default implementation clears outputs above the input count, which
    // silences the right channel in the mono -> stereo layout. Pass the dry
    // signal to every output instead, as processing does.
    const int numIn = getTotalNumInputChannels();
    if (numIn == 1)
        for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
            buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());

    numGainTracks = 0;
    numFadeTracks = 0;
}

void IrisAudioProcessor::runConvJob (int index) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    auto& job     = jobs[static_cast<size_t>(index)];
    auto& ir      = *job.ir;
    auto& scratch = irScratchBuffers[static_cast<size_t>(index)];
    const int n   = jobNumSamples;

    auto convolve = [&] (juce::dsp::Convolution& conv, int inCh, int outCh)
    {
        juce::dsp::AudioBlock<float> inBlock (const_cast<float* const*> (jobInputPtrs) + inCh, 1, static_cast<size_t>(n));
        juce::dsp::AudioBlock<float> outBlock (scratch.getArrayOfWritePointers() + outCh, 1, static_cast<size_t>(n));
        conv.process (juce::dsp::ProcessContextNonReplacing<float> (inBlock, outBlock));
    };

    if (job.broadcast)
    {
        convolve (*ir.convolvers[0], 0, 0);
        return;
    }

    for (int ch = 0; ch < job.numCh; ++ch)
    {
        // Mono input feeds every IR channel; multichannel input is routed 1:1.
        const int inCh = (jobNumInputCh == 1) ? 0 : ch;
        auto& conv = ir.convolvers[static_cast<size_t>(ch)];

        if (inCh >= jobNumInputCh || conv == nullptr)
            scratch.clear (ch, 0, n);
        else
            convolve (*conv, inCh, ch);
    }
}

void IrisAudioProcessor::processSubBlock (juce::AudioBuffer<float>& buffer)
{
    std::shared_ptr<RenderState> state = std::atomic_load(&renderState);

    const int numSamples  = buffer.getNumSamples();
    const int numOutputCh = buffer.getNumChannels();

    // Never index past what was allocated in prepareToPlay, whatever the host passes.
    const int numInputCh  = std::min({ getTotalNumInputChannels(),
                                       inputBuffer.getNumChannels(),
                                       buffer.getNumChannels() });

    if (numInputCh <= 0)
    {
        buffer.clear();
        return;
    }

    // Copy input into pre-allocated buffer — no heap allocation on the audio thread.
    for (int ch = 0; ch < numInputCh; ++ch)
        inputBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    buffer.clear();

    // --- Build the job list ---
    // Each job owns one scratch buffer, so jobs never write to shared memory.
    int numJobs = 0, numNextTracks = 0;
    const int fadeLen = std::max(1, static_cast<int>(0.03 * processSpec.sampleRate));

    auto addJob = [&] (const ActiveIR& ir, float g0, float g1)
    {
        auto& job     = jobs[static_cast<size_t>(numJobs)];
        job.ir        = &ir;
        job.gainStart = g0;
        job.gainEnd   = g1;
        job.broadcast = (ir.sourceChannels == 1 && ir.convolvers.size() == 1);
        job.numCh     = job.broadcast ? 1 : std::min({ numOutputCh,
                                                       static_cast<int>(ir.convolvers.size()),
                                                       irScratchBuffers[static_cast<size_t>(numJobs)].getNumChannels() });
        ++numJobs;
    };

    if (state != nullptr)
    {
        for (const auto& ir : state->activeIRs)
        {
            if (numJobs >= kMaxJobs) break;
            if (ir.convolvers.empty() || ir.convolvers[0] == nullptr) continue;

            const auto* key = ir.convolvers[0].get();
            float start = 0.0f;
            bool  known = false;
            for (int t = 0; t < numGainTracks; ++t)
                if (gainTracks[static_cast<size_t>(t)].conv == key)
                    { start = gainTracks[static_cast<size_t>(t)].gain; known = true; break; }

            // Re-activated after being silent: drop the stale tail from long ago.
            if (! known)
                for (auto& c : ir.convolvers)
                    if (c) c->reset();

            const float target = ir.weight;
            if (start < 1.0e-6f && target < 1.0e-6f) continue;

            gainTracksNext[static_cast<size_t>(numNextTracks++)] = { ir.id, key, target };
            addJob (ir, start, target);
        }

        // Replaced or removed IRs: keep convolving for a short fade so their
        // output doesn't stop abruptly (no click, no dry gap).
        for (int f = 0; f < numFadeTracks; ++f)
            fadeTracks[static_cast<size_t>(f)].seen = false;

        for (const auto& ir : state->fadingOut)
        {
            if (numJobs >= kMaxJobs) break;
            if (ir.convolvers.empty() || ir.convolvers[0] == nullptr) continue;

            const auto* key = ir.convolvers[0].get();
            FadeTrack* track = nullptr;
            for (int f = 0; f < numFadeTracks; ++f)
                if (fadeTracks[static_cast<size_t>(f)].conv == key)
                    { track = &fadeTracks[static_cast<size_t>(f)]; break; }

            if (track == nullptr)
            {
                if (numFadeTracks >= kMaxJobs) continue;

                // Start from the gain it was actually playing at; never seen = silent.
                float g = 0.0f;
                for (int t = 0; t < numGainTracks; ++t)
                    if (gainTracks[static_cast<size_t>(t)].conv == key)
                        { g = gainTracks[static_cast<size_t>(t)].gain; break; }

                track = &fadeTracks[static_cast<size_t>(numFadeTracks++)];
                *track = { key, g, 0, false };
            }

            track->seen = true;
            if (track->done >= fadeLen || track->gain < 1.0e-6f) continue;

            const float a0 = 1.0f - static_cast<float>(track->done) / static_cast<float>(fadeLen);
            track->done = std::min(fadeLen, track->done + numSamples);
            const float a1 = 1.0f - static_cast<float>(track->done) / static_cast<float>(fadeLen);
            addJob (ir, track->gain * a0, track->gain * a1);
        }

        int kept = 0;
        for (int f = 0; f < numFadeTracks; ++f)
            if (fadeTracks[static_cast<size_t>(f)].seen)
                fadeTracks[static_cast<size_t>(kept++)] = fadeTracks[static_cast<size_t>(f)];
        numFadeTracks = kept;
    }

    std::swap(gainTracks, gainTracksNext);
    numGainTracks = numNextTracks;

    // --- Run the jobs: inline when there is one, otherwise spread over the workers ---
    jobNumSamples  = numSamples;
    jobInputPtrs   = inputBuffer.getArrayOfReadPointers();   // fetched once: jobs run in parallel
    jobNumInputCh  = numInputCh;
    jobNumOutputCh = numOutputCh;

    if (numJobs == 1 || (numJobs > 1 && workers == nullptr))
        for (int j = 0; j < numJobs; ++j) runConvJob (j);
    else if (numJobs > 1)
        workers->run (numJobs);

    // --- Mix, with per-block gain ramps (no zipper noise) ---
    for (int j = 0; j < numJobs; ++j)
    {
        const auto& job     = jobs[static_cast<size_t>(j)];
        const auto& scratch = irScratchBuffers[static_cast<size_t>(j)];

        if (job.broadcast)
            for (int ch = 0; ch < numOutputCh; ++ch)
                buffer.addFromWithRamp(ch, 0, scratch.getReadPointer(0), numSamples, job.gainStart, job.gainEnd);
        else
            for (int ch = 0; ch < job.numCh; ++ch)
                buffer.addFromWithRamp(ch, 0, scratch.getReadPointer(ch), numSamples, job.gainStart, job.gainEnd);
    }

    // --- Dry / wet blend ---
    // Wet amount is the Mix control alone: IR gains are already normalised, so the
    // listener's distance to the IRs never turns the reverb into dry signal.
    float userMix = mixParam->load();
    userMix = std::isfinite(userMix) ? juce::jlimit(0.0f, 1.0f, userMix) : 1.0f;

    const float wet = (state != nullptr && state->hasIRs) ? userMix : 0.0f;
    const float dry = 1.0f - wet;
    if (lastWet < 0.0f) { lastWet = wet; lastDry = dry; }

    for (int ch = 0; ch < numOutputCh; ++ch)
    {
        const int inputCh = (numInputCh == 1) ? 0 : ch;
        buffer.applyGainRamp(ch, 0, numSamples, lastWet, wet);
        if (inputCh < numInputCh)
            buffer.addFromWithRamp(ch, 0, inputBuffer.getReadPointer(inputCh), numSamples, lastDry, dry);
    }
    lastWet = wet;
    lastDry = dry;

    // --- Output gain (dB) ---
    float gainDb = outputGainParam->load();
    if (! std::isfinite(gainDb)) gainDb = 0.0f;
    const float gainLin = juce::Decibels::decibelsToGain(gainDb);
    if (lastOutGain < 0.0f) lastOutGain = gainLin;
    buffer.applyGainRamp(0, numSamples, lastOutGain, gainLin);
    lastOutGain = gainLin;
}

// ---------------------------------------------------------------------------
// IR loading
// ---------------------------------------------------------------------------

// Reads an IR file into p (rawBuffer, sourceBuffer, sampleRate, channels).
// Rejects absurd headers instead of allocating gigabytes, checks the read and
// replaces NaN/Inf samples with silence.
static bool readIRFile (juce::AudioFormatManager& formatManager, const juce::File& file, IRPoint& p)
{
    if (! file.existsAsFile()) return false;

    if (formatManager.getNumKnownFormats() == 0)
        formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || ! (reader->sampleRate > 0.0)) return false;

    const auto numChannels = static_cast<int> (reader->numChannels);
    if (reader->lengthInSamples <= 0 || numChannels <= 0 || numChannels > IrisAudioProcessor::kMaxIRChannels)
        return false;

    const auto maxLength  = static_cast<juce::int64> (IrisAudioProcessor::kMaxIRSeconds * reader->sampleRate);
    const auto numSamples = static_cast<int> (std::min (reader->lengthInSamples, maxLength));

    try
    {
        auto raw = std::make_shared<juce::AudioBuffer<float>> (numChannels, numSamples);
        if (! reader->read (raw.get(), 0, numSamples, 0, true, true))
            return false;

        for (int c = 0; c < numChannels; ++c)
        {
            auto* d = raw->getWritePointer (c);
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (d[i])) d[i] = 0.0f;
        }

        p.sourceFile     = file;
        p.sampleRate     = reader->sampleRate;
        p.sourceChannels = numChannels;
        p.rawBuffer      = raw;
        p.sourceBuffer   = std::make_shared<juce::AudioBuffer<float>> (*raw);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }

    return true;
}

static void applyAlignAndNormalize(IRPoint& p, bool enableAlign, bool enableNorm)
{
    auto& buf       = *p.sourceBuffer;
    int numSamples  = buf.getNumSamples();
    int numChannels = buf.getNumChannels();

    float storedGain  = 1.0f;
    int   storedOnset = 0;

    if (enableAlign && numSamples > 0)
    {
        float globalPeak = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            globalPeak = juce::jmax(globalPeak, buf.getMagnitude(c, 0, numSamples));

        float threshold = globalPeak * 0.1f;
        int   onset     = -1;

        for (int i = 0; i < numSamples && onset == -1; ++i)
            for (int c = 0; c < numChannels; ++c)
                if (std::abs(buf.getSample(c, i)) > threshold)
                    { onset = i; break; }

        if (onset < 0) onset = 0;

        // Keep ~1 ms before the onset and fade that pre-roll in on every channel,
        // so the direct sound itself is never softened.
        const int preRoll = static_cast<int>(0.001 * p.sampleRate);
        const int start   = std::max(0, onset - preRoll);
        storedOnset = start;

        if (start > 0)
        {
            int newLen = numSamples - start;
            for (int c = 0; c < numChannels; ++c)
            {
                auto* dest = buf.getWritePointer(c);
                std::memmove(dest, dest + start, static_cast<size_t>(newLen) * sizeof(float));
            }
            buf.setSize(numChannels, newLen, true);
            numSamples = newLen;
        }

        const int rampLen = onset - start;
        if (rampLen > 1)
            for (int c = 0; c < numChannels; ++c)
                buf.applyGainRamp(c, 0, rampLen, 0.0f, 1.0f);
    }

    if (enableNorm && numSamples > 0)
    {
        // Normalise to unity energy (0 dB gain for white noise), so IRs of
        // different lengths play at comparable loudness.
        double energy = 0.0;
        for (int c = 0; c < numChannels; ++c)
        {
            auto* d = buf.getReadPointer(c);
            for (int i = 0; i < numSamples; ++i)
                energy += static_cast<double>(d[i]) * d[i];
        }
        energy /= static_cast<double>(numChannels);

        if (energy > 1.0e-12)
        {
            storedGain = static_cast<float>(1.0 / std::sqrt(energy));
            buf.applyGain(storedGain);
        }
    }

    p.normGain    = storedGain;
    p.onsetOffset = storedOnset;
}

static juce::Colour colourForId (const juce::Uuid& id)
{
    juce::Random rng(id.toString().hashCode());
    return juce::Colour::fromHSV(rng.nextFloat() * 0.15f + 0.55f, 0.8f, 0.9f, 1.0f);
}

juce::Uuid IrisAudioProcessor::addIRFromFile(const juce::File& file)
{
    IRPoint p;
    if (! readIRFile(formatManager, file, p)) return juce::Uuid::null();

    p.id    = juce::Uuid();
    p.name  = file.getFileName();
    p.color = colourForId(p.id);
    applyAlignAndNormalize(p, alignParam->load() > 0.5f, normalizeParam->load() > 0.5f);
    updateConvolver(p);

    {
        juce::ScopedLock sl(stateLock);
        const int n = static_cast<int>(points.size());
        p.x = 0.2f + static_cast<float>(n % 3) * 0.2f;
        p.y = juce::jmin(0.9f, 0.2f + std::floor(static_cast<float>(n) / 3.0f) * 0.2f);
        points.push_back(p);
        updateWeightsGaussian();
    }

    notifyStructuralChange();
    recalcTailLength();

    if (broadcastIRs)
        oscManager.syncAddIR(p.id, p.name, file, this);

    return p.id;
}

void IrisAudioProcessor::addIRFromFileWithID(const juce::File& file, juce::Uuid id, const juce::String& name)
{
    {
        juce::ScopedLock sl(stateLock);
        for (const auto& p : points)
            if (p.id == id) return;
    }

    IRPoint p;
    if (! readIRFile(formatManager, file, p)) return;

    p.id    = id;
    p.name  = name.isNotEmpty() ? name : file.getFileName();
    p.color = colourForId(p.id);
    applyAlignAndNormalize(p, alignParam->load() > 0.5f, normalizeParam->load() > 0.5f);
    updateConvolver(p);

    {
        juce::ScopedLock sl(stateLock);
        for (const auto& existing : points)
            if (existing.id == id) return;

        const int n = static_cast<int>(points.size());
        p.x = 0.2f + static_cast<float>(n % 3) * 0.2f;
        p.y = juce::jmin(0.9f, 0.2f + std::floor(static_cast<float>(n) / 3.0f) * 0.2f);
        points.push_back(p);
        updateWeightsGaussian();
    }

    notifyStructuralChange();
    recalcTailLength();
}

void IrisAudioProcessor::addIRPoint(const juce::String& name)
{
    IRPoint p;
    p.id    = juce::Uuid();
    p.name  = name;
    p.color = colourForId(p.id);

    {
        juce::ScopedLock sl(stateLock);
        const int n = static_cast<int>(points.size());
        p.x = 0.2f + static_cast<float>(n % 3) * 0.2f;
        p.y = juce::jmin(0.9f, 0.2f + std::floor(static_cast<float>(n) / 3.0f) * 0.2f);
        points.push_back(p);
        updateWeightsGaussian();
    }
    notifyStructuralChange();
}

// ---------------------------------------------------------------------------
// Convolver management
// ---------------------------------------------------------------------------

void IrisAudioProcessor::updateConvolver(IRPoint& p)
{
    if (!p.sourceBuffer || p.sourceChannels == 0) return;

    p.convolvers.clear();

    const int numOut = std::max(1, getTotalNumOutputChannels());
    const int numIn  = std::max(1, getTotalNumInputChannels());

    // Mono IR, mono input:     one convolver, broadcast to every output.
    // Mono IR, N-ch input:     the same IR on each input channel (stereo stays stereo).
    // N-ch IR:                 one convolver per IR channel up to the output count;
    //                          a mono input feeds all of them (mono -> ambisonics).
    const int numConvolvers = (p.sourceChannels == 1)
                            ? (numIn == 1 ? 1 : std::min(numIn, numOut))
                            : std::min(numOut, p.sourceChannels);

    const int len = p.sourceBuffer->getNumSamples();

    for (int ch = 0; ch < numConvolvers; ++ch)
    {
        // Non-uniform partitioning: zero latency, but the long tail is processed in
        // large partitions instead of one partition per host block.
        auto conv = std::make_shared<juce::dsp::Convolution>(
            juce::dsp::Convolution::NonUniform { kConvolutionHeadSize }, convQueue);

        juce::AudioBuffer<float> mono(1, len);
        mono.copyFrom(0, 0, *p.sourceBuffer, p.sourceChannels == 1 ? 0 : ch, 0, len);

        // Load BEFORE prepare: prepare() runs the queued load synchronously, so the
        // convolver holds the real IR from its first block instead of a unit
        // impulse (which would pass the dry signal until the background load ends).
        conv->loadImpulseResponse(std::move(mono),
                                  p.sampleRate,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::no,
                                  juce::dsp::Convolution::Normalise::no);

        if (processSpec.sampleRate > 0)
        {
            conv->prepare(processSpec);

            // Rare: the shared background thread picked the load up first. Wait for
            // it and prepare again so the engine is built with the IR.
            for (int tries = 0; tries < 200 && len > 1 && conv->getCurrentIRSize() <= 1; ++tries)
            {
                juce::Thread::sleep(2);
                conv->prepare(processSpec);
            }
        }

        p.convolvers.push_back(conv);
    }
}


void IrisAudioProcessor::reprocessIRPoints()
{
    {
        juce::ScopedLock sl(stateLock);

        bool enableNorm  = normalizeParam->load() > 0.5f;
        bool enableAlign = alignParam->load() > 0.5f;

        for (auto& p : points)
        {
            if (!p.rawBuffer || p.sourceChannels == 0) continue;

            p.sourceBuffer = std::make_shared<juce::AudioBuffer<float>>(*p.rawBuffer);
            applyAlignAndNormalize(p, enableAlign, enableNorm);
            updateConvolver(p);
        }
    }
    notifyStructuralChange();
    recalcTailLength();
}

// ---------------------------------------------------------------------------
// Point management
// ---------------------------------------------------------------------------

void IrisAudioProcessor::removePoint(juce::Uuid id, bool broadcast)
{
    if (broadcast && broadcastIRs)
        oscManager.syncRemoveIR(id, this);

    {
        juce::ScopedLock sl(stateLock);
        for (auto it = points.begin(); it != points.end(); ++it)
        {
            if (it->id == id)
            {
                points.erase(it);
                targetWeights.erase(id);
                smoothedWeights.erase(id);
                activeIDs.erase(id);
                break;
            }
        }
        updateWeightsGaussian();
    }
    notifyStructuralChange();
    recalcTailLength();
}

void IrisAudioProcessor::updatePointPosition(juce::Uuid id, float x, float y, bool broadcast)
{
    if (! std::isfinite(x) || ! std::isfinite(y)) return;

    if (broadcast && broadcastIRs)
        oscManager.syncIRPosition(id, x, y, this);

    juce::ScopedLock sl(stateLock);
    for (auto& p : points)
    {
        if (p.id == id)
        {
            if (!p.locked)
            {
                p.x = juce::jlimit(0.0f, 1.0f, x);
                p.y = juce::jlimit(0.0f, 1.0f, y);
            }
            break;
        }
    }
    pendingUIRepaint.store(true);
}

void IrisAudioProcessor::setPointLocked(juce::Uuid id, bool locked, bool broadcast)
{
    if (broadcast && broadcastIRs)
        oscManager.syncLocked(id, locked, this);

    {
        juce::ScopedLock sl(stateLock);
        for (auto& p : points)
            if (p.id == id) { p.locked = locked; break; }

        for (auto& w : walls)
            if (w.id == id) { w.locked = locked; break; }
    }
    notifyStructuralChange();
}

void IrisAudioProcessor::setPointName(juce::Uuid id, const juce::String& name, bool broadcast)
{
    if (broadcast && broadcastIRs)
        oscManager.syncIRName(id, name, this);

    {
        juce::ScopedLock sl(stateLock);
        for (auto& p : points)
            if (p.id == id) { p.name = name; break; }

        for (auto& w : walls)
            if (w.id == id) { w.name = name; break; }
    }
    notifyStructuralChange();
}

// ---------------------------------------------------------------------------
// Wall management
// ---------------------------------------------------------------------------

juce::Uuid IrisAudioProcessor::addWall(float x1, float y1, float x2, float y2, bool broadcast)
{
    OcclusionWall w;
    w.id          = juce::Uuid();
    w.attenuation = 0.05f;
    w.color       = juce::Colours::cyan;
    w.x1 = unitCoord(x1); w.y1 = unitCoord(y1);
    w.x2 = unitCoord(x2); w.y2 = unitCoord(y2);

    {
        juce::ScopedLock sl(stateLock);
        w.name = "Wall " + juce::String(walls.size() + 1);
        walls.push_back(w);
        updateWeightsGaussian();
    }

    notifyStructuralChange();

    if (broadcast && broadcastWalls)
        oscManager.syncAddWall(w.id, w.x1, w.y1, w.x2, w.y2, this);

    return w.id;
}

void IrisAudioProcessor::addWallWithID(juce::Uuid id, float x1, float y1, float x2, float y2)
{
    {
        juce::ScopedLock sl(stateLock);
        for (const auto& existing : walls)
            if (existing.id == id) return;

        OcclusionWall w;
        w.id          = id;
        w.name        = "Wall " + juce::String(walls.size() + 1);
        w.x1          = unitCoord(x1);
        w.y1          = unitCoord(y1);
        w.x2          = unitCoord(x2);
        w.y2          = unitCoord(y2);
        w.attenuation = 0.05f;
        w.color       = juce::Colours::cyan;
        walls.push_back(w);

        updateWeightsGaussian();
    }
    notifyStructuralChange();
}

void IrisAudioProcessor::removeWall(juce::Uuid id, bool broadcast)
{
    if (broadcast && broadcastWalls)
        oscManager.syncRemoveWall(id, this);

    {
        juce::ScopedLock sl(stateLock);
        for (auto it = walls.begin(); it != walls.end(); ++it)
        {
            if (it->id == id) { walls.erase(it); break; }
        }
        if (selectedWallId == id) selectedWallId = juce::Uuid::null();
        updateWeightsGaussian();
    }
    notifyStructuralChange();
}

void IrisAudioProcessor::updateWall(juce::Uuid id, float x1, float y1, float x2, float y2, bool broadcast)
{
    if (! std::isfinite(x1) || ! std::isfinite(y1) || ! std::isfinite(x2) || ! std::isfinite(y2)) return;

    bool changed = false;
    {
        juce::ScopedLock sl(stateLock);
        for (auto& w : walls)
        {
            if (w.id == id)
            {
                if (!w.locked)
                {
                    w.x1 = juce::jlimit(0.0f, 1.0f, x1);
                    w.y1 = juce::jlimit(0.0f, 1.0f, y1);
                    w.x2 = juce::jlimit(0.0f, 1.0f, x2);
                    w.y2 = juce::jlimit(0.0f, 1.0f, y2);
                    changed = true;
                }
                break;
            }
        }
        updateWeightsGaussian();
    }

    if (changed && broadcast && broadcastWalls)
        oscManager.syncWallPosition(id, x1, y1, x2, y2, this);

    pendingUIRepaint.store(true);
}

void IrisAudioProcessor::setWallLocked(juce::Uuid id, bool locked)
{
    {
        juce::ScopedLock sl(stateLock);
        for (auto& w : walls)
            if (w.id == id) { w.locked = locked; break; }
    }
    notifyStructuralChange();
}

void IrisAudioProcessor::setWallName(juce::Uuid id, const juce::String& name)
{
    {
        juce::ScopedLock sl(stateLock);
        for (auto& w : walls)
            if (w.id == id) { w.name = name; break; }
    }
    notifyStructuralChange();
}

void IrisAudioProcessor::constrainPointToWalls(float& x, float& y)
{
    juce::ScopedLock sl(stateLock);
    const float minClearance = 0.02f;

    for (const auto& w : walls)
    {
        float cx, cy;
        closestPointOnSegment(x, y, w.x1, w.y1, w.x2, w.y2, cx, cy);

        float d2 = distSq(x, y, cx, cy);
        if (d2 < minClearance * minClearance)
        {
            float dx  = x - cx;
            float dy  = y - cy;
            float len = std::sqrt(d2);

            if (len < 1e-10f)
            {
                dx  = -(w.y2 - w.y1);
                dy  =  (w.x2 - w.x1);
                len = std::sqrt(dx*dx + dy*dy);
            }

            if (len > 0.0f)
            {
                float scale = minClearance / len;
                x = cx + dx * scale;
                y = cy + dy * scale;
            }
        }
    }

    x = juce::jlimit(0.0f, 1.0f, x);
    y = juce::jlimit(0.0f, 1.0f, y);
}

// ---------------------------------------------------------------------------
// Listener management
// ---------------------------------------------------------------------------

namespace
{
    struct ListenerUpdate { juce::Uuid id; juce::String name; float x, y; bool locked; };
}

void IrisAudioProcessor::updateListenerPosition(juce::Uuid id, float x, float y, bool broadcast)
{
    if (! std::isfinite(x) || ! std::isfinite(y)) return;

    // Collected under the lock, sent after releasing it (lock order: listLock -> stateLock).
    std::vector<ListenerUpdate> toSend;

    {
        juce::ScopedLock sl(stateLock);

        NetworkListener* moved = nullptr;
        if (id == localAudioListener.id)
            moved = &localAudioListener;
        else if (auto it = remoteListeners.find(id); it != remoteListeners.end())
            moved = &it->second;

        if (!moved) return;
        if (moved->locked && broadcast) return;

        moved->x = juce::jlimit(0.0f, 1.0f, x);
        moved->y = juce::jlimit(0.0f, 1.0f, y);

        if (broadcast)
        {
            // BFS to find all nodes coupled via the link matrix, then co-move them.
            std::set<juce::String>   visited;
            std::queue<juce::String> bfsQueue;
            visited.insert(moved->id.toString());
            bfsQueue.push(moved->id.toString());

            while (!bfsQueue.empty())
            {
                auto current = bfsQueue.front();
                bfsQueue.pop();

                for (const auto& edge : linkMatrix)
                {
                    if (edge.first == current && visited.count(edge.second) == 0)
                        { visited.insert(edge.second); bfsQueue.push(edge.second); }
                    else if (edge.second == current && visited.count(edge.first) == 0)
                        { visited.insert(edge.first);  bfsQueue.push(edge.first);  }
                }
            }

            for (const auto& uIdStr : visited)
            {
                juce::Uuid uId(uIdStr);
                if (uId == id) continue;

                NetworkListener* follower = nullptr;
                if (uId == localAudioListener.id) follower = &localAudioListener;
                else if (auto it = remoteListeners.find(uId); it != remoteListeners.end()) follower = &it->second;

                if (follower && !follower->locked)
                {
                    follower->x = moved->x;
                    follower->y = moved->y;
                    toSend.push_back({ follower->id, follower->name, follower->x, follower->y, follower->locked });
                }
            }

            toSend.push_back({ moved->id, moved->name, moved->x, moved->y, moved->locked });
        }
    }

    if (broadcastListener)
        for (const auto& u : toSend)
            oscManager.setListenerState(u.id, u.name, u.x, u.y, false, u.locked, this);

    pendingUIRepaint.store(true);
}

void IrisAudioProcessor::setListenerLocked(juce::Uuid id, bool locked, bool broadcast)
{
    std::optional<ListenerUpdate> toSend;
    {
        juce::ScopedLock sl(stateLock);

        NetworkListener* l = nullptr;
        if (id == localAudioListener.id) l = &localAudioListener;
        else if (auto it = remoteListeners.find(id); it != remoteListeners.end()) l = &it->second;
        if (l == nullptr) return;

        l->locked = locked;
        if (broadcast) toSend = ListenerUpdate { l->id, l->name, l->x, l->y, l->locked };
    }

    notifyStructuralChange();

    if (toSend)
        oscManager.setListenerState(toSend->id, toSend->name, toSend->x, toSend->y, false, toSend->locked, this);
}

void IrisAudioProcessor::setListenerName(juce::Uuid id, const juce::String& name)
{
    std::optional<ListenerUpdate> toSend;
    {
        juce::ScopedLock sl(stateLock);

        NetworkListener* l = nullptr;
        if (id == localAudioListener.id) l = &localAudioListener;
        else if (auto it = remoteListeners.find(id); it != remoteListeners.end()) l = &it->second;
        if (l == nullptr) return;

        l->name = name;
        toSend = ListenerUpdate { l->id, l->name, l->x, l->y, l->locked };
    }

    notifyStructuralChange();
    oscManager.setListenerState(toSend->id, toSend->name, toSend->x, toSend->y, false, toSend->locked, this);
}

void IrisAudioProcessor::requestFullOSCSync()
{
    oscManager.requestFullSync(this);
}

// ---------------------------------------------------------------------------
// Link matrix
// ---------------------------------------------------------------------------

void IrisAudioProcessor::toggleLinkMatrix(juce::Uuid id1, juce::Uuid id2, bool broadcast)
{
    std::vector<std::pair<juce::String, juce::String>> edges;

    {
        juce::ScopedLock sl(stateLock);

        juce::String s1   = id1.toString();
        juce::String s2   = id2.toString();
        auto         edge = std::make_pair(std::min(s1, s2), std::max(s1, s2));

        bool isLinking = (linkMatrix.count(edge) == 0);

        if (isLinking)
        {
            linkMatrix.insert(edge);

            // Rebuild each connected component as a clique to enforce full transitivity.
            std::map<juce::String, std::set<juce::String>> adj;
            for (const auto& e : linkMatrix)
            {
                adj[e.first].insert(e.second);
                adj[e.second].insert(e.first);
            }

            std::set<juce::String>                visited;
            std::vector<std::vector<juce::String>> components;

            for (const auto& pair : adj)
            {
                if (visited.count(pair.first)) continue;

                std::vector<juce::String> comp;
                std::queue<juce::String>  q;
                q.push(pair.first);
                visited.insert(pair.first);

                while (!q.empty())
                {
                    auto curr = q.front(); q.pop();
                    comp.push_back(curr);
                    for (const auto& nbr : adj[curr])
                        if (!visited.count(nbr)) { visited.insert(nbr); q.push(nbr); }
                }
                components.push_back(comp);
            }

            linkMatrix.clear();
            for (const auto& comp : components)
                for (size_t i = 0; i < comp.size(); ++i)
                    for (size_t j = i + 1; j < comp.size(); ++j)
                        linkMatrix.insert({ std::min(comp[i], comp[j]), std::max(comp[i], comp[j]) });
        }
        else
        {
            // Severing: remove all edges between s1 and s2's connected component.
            std::map<juce::String, std::set<juce::String>> adj;
            for (const auto& e : linkMatrix)
            {
                adj[e.first].insert(e.second);
                adj[e.second].insert(e.first);
            }

            std::set<juce::String>  s2Group;
            std::queue<juce::String> q;
            q.push(s2);
            s2Group.insert(s2);

            while (!q.empty())
            {
                auto curr = q.front(); q.pop();
                for (const auto& nbr : adj[curr])
                    if (nbr != s1 && !s2Group.count(nbr)) { s2Group.insert(nbr); q.push(nbr); }
            }

            for (const auto& node : s2Group)
                linkMatrix.erase({ std::min(s1, node), std::max(s1, node) });
        }

        edges.assign(linkMatrix.begin(), linkMatrix.end());
    }

    notifyStructuralChange();

    // Every instance shares one link graph: instances in this process get it
    // directly, other processes over OSC.
    if (broadcast) oscManager.syncLinkMatrix(this, edges);
}

void IrisAudioProcessor::setLinkMatrixConnections(const std::vector<std::pair<juce::String, juce::String>>& edges)
{
    {
        juce::ScopedLock sl(stateLock);
        linkMatrix.clear();
        for (const auto& edge : edges)
            linkMatrix.insert({ std::min(edge.first, edge.second), std::max(edge.first, edge.second) });
    }
    notifyStructuralChange();
}

// ---------------------------------------------------------------------------
// Parameter sync
// ---------------------------------------------------------------------------

void IrisAudioProcessor::updateParameterNotifiers(juce::String paramId, float value)
{
    if (! std::isfinite(value)) return;

    if (auto* p = parameters.getParameter(paramId))
    {
        oscUpdateThread.store(juce::Thread::getCurrentThreadId());
        p->setValueNotifyingHost(p->convertTo0to1(value));
        oscUpdateThread.store(nullptr);
    }
}

void IrisAudioProcessor::parameterChanged(const juce::String& parameterID, float)
{
    // May run on the audio thread (host automation): only set flags here.
    // timerCallback() does the actual work on the message thread.
    const auto self = juce::Thread::getCurrentThreadId();
    const bool restoring = isRestoringState.load();

    if ((parameterID == "normalize" || parameterID == "align") && ! restoring)
        reprocessPending.store(true);

    if ((parameterID == "listenerX" || parameterID == "listenerY")
        && listenerWritebackThread.load() != self && ! restoring)
        listenerParamDirty.store(true);

    // Never broadcast while restoring a saved session — otherwise each instance
    // being restored overwrites the values of the instances before it.
    if (oscUpdateThread.load() != self && ! restoring)
        for (uint32_t i = 0; i < std::size(kBroadcastParamIds); ++i)
            if (parameterID == kBroadcastParamIds[i])
                pendingBroadcastMask.fetch_or(1u << i);
}

// ---------------------------------------------------------------------------
// Gaussian weight computation (caller holds stateLock or is the message thread)
// ---------------------------------------------------------------------------

void IrisAudioProcessor::updateWeightsGaussian()
{
    if (points.empty())
    {
        targetWeights.clear();
        activeIDs.clear();
        return;
    }

    float spread   = sanitise(spreadParam ? spreadParam->load() : 0.3f, 0.3f, 0.0f, 1.0f);
    // Dynamic sigma allowing pinpoint accuracy (0.001) down to spread=0
    // Quadratic curve gives fine control at low spread values and broad diffusion at high spread
    float sigmaVal = 0.001f + 1.5f * (spread * spread);

    float baseOpacity = sanitise(wallOpacityParam ? wallOpacityParam->load() : 1.0f, 1.0f, 0.0f, 1.0f);

    const float lx = unitCoord(localAudioListener.currentX);
    const float ly = unitCoord(localAudioListener.currentY);

    // A wall end that touches another wall (corner or T-junction) gets no edge
    // fade, otherwise sound leaks through closed corners.
    constexpr float joinTolerance = 0.01f;
    constexpr float edgeFadeDist  = 0.03f;   // fade distance from a free wall end (room units)

    std::vector<std::pair<bool, bool>> joined(walls.size(), { false, false });
    for (size_t i = 0; i < walls.size(); ++i)
        for (size_t j = 0; j < walls.size(); ++j)
        {
            if (i == j) continue;
            if (walls[j].getDistanceToPoint(walls[i].x1, walls[i].y1) < joinTolerance) joined[i].first  = true;
            if (walls[j].getDistanceToPoint(walls[i].x2, walls[i].y2) < joinTolerance) joined[i].second = true;
        }

    std::vector<std::pair<float, IRPoint*>> rawWeights;
    float maxWeight = 0.0f;

    for (auto& p : points)
    {
        float dx = p.x - lx;
        float dy = p.y - ly;
        float gauss = std::exp(-(dx*dx + dy*dy) / (2.0f * sigmaVal * sigmaVal));

        float occlusionFactor   = 1.0f;
        int   intersectionCount = 0;

        for (size_t wi = 0; wi < walls.size(); ++wi)
        {
            const auto& wall = walls[wi];
            float ix, iy, tWall;
            if (getIntersectionPoint(lx, ly, p.x, p.y,
                                     wall.x1, wall.y1, wall.x2, wall.y2,
                                     ix, iy, tWall))
            {
                ++intersectionCount;

                const float len = std::sqrt(distSq(wall.x1, wall.y1, wall.x2, wall.y2));
                float edgeFade = 1.0f;
                if (! joined[wi].first)  edgeFade = std::min(edgeFade, tWall * len / edgeFadeDist);
                if (! joined[wi].second) edgeFade = std::min(edgeFade, (1.0f - tWall) * len / edgeFadeDist);
                edgeFade = juce::jlimit(0.0f, 1.0f, edgeFade);

                const float transparency = sanitise(wall.attenuation, 0.05f, 0.0f, 1.0f);
                occlusionFactor *= 1.0f - baseOpacity * (1.0f - transparency) * edgeFade;
            }
        }

        // An IR whose file is missing has nothing to play: give it no weight so the
        // others are renormalised instead of leaving a hole in the mix.
        float w = p.convolvers.empty() ? 0.0f : gauss * occlusionFactor;
        if (! std::isfinite(w)) w = 0.0f;

        p.debug_rawWeight        = gauss;
        p.debug_occlusionFactor  = occlusionFactor;
        p.debug_intersectionCount = intersectionCount;
        p.debug_finalWeight      = w;

        rawWeights.push_back({ w, &p });
        if (w > maxWeight) maxWeight = w;
    }

    // Sort descending by weight
    std::sort(rawWeights.begin(), rawWeights.end(),
              [](const auto& a, const auto& b){ return a.first > b.first; });

    // Dynamic kMin based on spread:
    // At near-zero spread (spread < 0.03), allow single nearest neighbor (kMin = 1, zero bleed).
    // As spread widens, scale kMin smoothly up to 4.
    int kMin = 4;
    if (spread < 0.03f)
        kMin = 1;
    else if (spread < 0.10f)
        kMin = 2;
    else if (spread < 0.20f)
        kMin = 3;

    if (maxActiveOverride > 0)
        kMin = std::min(kMin, maxActiveOverride);
    const int kMax = (maxActiveOverride > 0) ? maxActiveOverride : 8;

    std::set<juce::Uuid> nextActiveIDs;

    for (int i = 0; i < std::min(static_cast<int>(rawWeights.size()), kMin); ++i)
        if (rawWeights[static_cast<size_t>(i)].first > 0.0f)
            nextActiveIDs.insert(rawWeights[static_cast<size_t>(i)].second->id);

    for (int i = kMin; i < std::min(static_cast<int>(rawWeights.size()), kMax); ++i)
    {
        float      w  = rawWeights[static_cast<size_t>(i)].first;
        juce::Uuid id = rawWeights[static_cast<size_t>(i)].second->id;

        bool wasActive = (activeIDs.find(id) != activeIDs.end());
        if (w > 0.0f && (wasActive ? (w >= tauOut * maxWeight) : (w >= tauIn * maxWeight)))
            nextActiveIDs.insert(id);
    }

    activeIDs = nextActiveIDs;

    targetWeights.clear();
    for (auto& pair : rawWeights)
        targetWeights[pair.second->id] = activeIDs.count(pair.second->id) ? pair.first : 0.0f;
}

// ---------------------------------------------------------------------------
// Render state
// ---------------------------------------------------------------------------

void IrisAudioProcessor::publishRenderState (std::shared_ptr<RenderState> next)
{
    if (next == nullptr)
        next = std::make_shared<RenderState>();

    // The old state (and the convolvers it references) must never be freed on the
    // audio thread: keep it here until nothing else holds it.
    retiredStates.push_back(std::atomic_load(&renderState));
    std::atomic_store(&renderState, next);

    retiredStates.erase(std::remove_if(retiredStates.begin(), retiredStates.end(),
                                       [&](const std::shared_ptr<RenderState>& s)
                                       { return s == nullptr || (s.use_count() == 1 && s != next); }),
                        retiredStates.end());
}

bool IrisAudioProcessor::rebuildRenderState (bool snapWeights)
{
    updateWeightsGaussian();

    const float smoothAlpha = 0.25f;
    bool  changed    = false;
    float sumForNorm = 0.0f;
    bool  hasIRs     = false;

    for (auto& p : points)
    {
        if (! p.convolvers.empty()) hasIRs = true;

        auto it = targetWeights.find(p.id);
        const float target = (it != targetWeights.end()) ? it->second : 0.0f;

        float& current = smoothedWeights[p.id];
        if (snapWeights)
        {
            changed = changed || current != target;
            current = target;
        }
        else if (std::abs(target - current) > 0.0001f)
        {
            current += (target - current) * smoothAlpha;
            if (current < 0.001f && target < 1e-6f) current = 0.0f;
            changed = true;
        }

        if (current > 0.0f) sumForNorm += current;
    }

    auto next = std::make_shared<RenderState>();
    next->hasIRs = hasIRs;
    currentNearestNeighbors.clear();

    for (auto& p : points)
    {
        const float w = smoothedWeights[p.id];
        if (w <= 0.0001f) continue;

        if (! p.convolvers.empty() && sumForNorm > 1.0e-9f)
        {
            // Equal-power interpolation: the IR tails are decorrelated, so their
            // gains must have unit energy (sum of squares = 1), not unit sum.
            ActiveIR air;
            air.convolvers     = p.convolvers;
            air.weight         = std::sqrt(w / sumForNorm);
            air.sourceChannels = p.sourceChannels;
            air.id             = p.id;
            next->activeIRs.push_back(air);
        }

        currentNearestNeighbors.push_back(p);
    }

    std::sort(currentNearestNeighbors.begin(), currentNearestNeighbors.end(),
              [&](const IRPoint& a, const IRPoint& b)
              { return smoothedWeights[a.id] > smoothedWeights[b.id]; });

    if (! snapWeights)
    {
        // Anything that was audible and is no longer playing through the same
        // convolver (removed, or rebuilt by a reprocess) fades out on the audio thread.
        if (auto prev = std::atomic_load(&renderState))
            for (const auto& old : prev->activeIRs)
            {
                if (old.weight < 0.01f || old.convolvers.empty()) continue;

                bool stillPlaying = false;
                for (const auto& n : next->activeIRs)
                    if (n.id == old.id && ! n.convolvers.empty() && n.convolvers[0] == old.convolvers[0])
                        { stillPlaying = true; break; }

                if (! stillPlaying)
                    pendingFadeOuts.push_back({ old, 6 });   // ~100 ms at 60 Hz, fade itself is 30 ms
            }

        for (auto& f : pendingFadeOuts)
        {
            next->fadingOut.push_back(f.ir);
            --f.ticksLeft;
        }
        pendingFadeOuts.erase(std::remove_if(pendingFadeOuts.begin(), pendingFadeOuts.end(),
                                             [](const PendingFade& f) { return f.ticksLeft <= 0; }),
                              pendingFadeOuts.end());
    }

    publishRenderState(next);

    if (changed) pendingUIRepaint.store(true);
    return changed;
}

// ---------------------------------------------------------------------------
// Timer callback — applies pending parameter work, physics, render state swap
// ---------------------------------------------------------------------------

void IrisAudioProcessor::timerCallback()
{
    // --- 1. Work requested by parameterChanged() (no locks held here) ---
    if (reprocessPending.exchange(false))
        reprocessIRPoints();

    if (listenerParamDirty.exchange(false))
        updateListenerPosition(localAudioListener.id,
                               unitCoord(listenerXParam->load()),
                               unitCoord(listenerYParam->load()), true);

    if (const auto mask = pendingBroadcastMask.exchange(0))
    {
        const bool flags[] = { broadcastInertia, broadcastFreeze, broadcastSpread, broadcastMix,
                               broadcastWallOpacity, broadcastNormalize, broadcastAlign };

        for (uint32_t i = 0; i < std::size(kBroadcastParamIds); ++i)
            if ((mask & (1u << i)) != 0 && flags[i])
                if (auto* v = parameters.getRawParameterValue(kBroadcastParamIds[i]))
                    oscManager.setGlobalParam(kBroadcastParamIds[i], v->load(), this);
    }

    // --- 2. Physics, weights and the render state ---
    const bool  frozen       = freezeParam->load() > 0.5f;
    const float inertia      = sanitise(inertiaParam->load(), 0.0f, 0.0f, 1.0f);
    const float physicsAlpha = 1.0f - 0.98f * inertia;

    float localX = 0.5f, localY = 0.5f;
    bool  weightsChanged = false;
    {
        juce::ScopedLock sl(stateLock);

        auto applyPhysics = [&](NetworkListener& listener)
        {
            if (frozen) return;

            if (std::abs(listener.x - listener.currentX) > 0.0001f)
                listener.currentX += (listener.x - listener.currentX) * physicsAlpha;

            if (std::abs(listener.y - listener.currentY) > 0.0001f)
                listener.currentY += (listener.y - listener.currentY) * physicsAlpha;
        };

        applyPhysics(localAudioListener);
        for (auto& pair : remoteListeners)
            applyPhysics(pair.second);

        weightsChanged = rebuildRenderState(false);

        localX = localAudioListener.x;
        localY = localAudioListener.y;
    }

    // --- 3. Mirror the listener position into the host parameters, only on change ---
    // (Writing every tick floods the host with edits and fights automation.)
    {
        listenerWritebackThread.store(juce::Thread::getCurrentThreadId());
        if (auto* px = parameters.getParameter("listenerX"))
            if (std::abs(px->convertFrom0to1(px->getValue()) - localX) > 1.0e-4f)
                px->setValueNotifyingHost(px->convertTo0to1(localX));
        if (auto* py = parameters.getParameter("listenerY"))
            if (std::abs(py->convertFrom0to1(py->getValue()) - localY) > 1.0e-4f)
                py->setValueNotifyingHost(py->convertTo0to1(localY));
        listenerWritebackThread.store(nullptr);
    }

    // --- 4. Weight output ---
    if (!isBenchmarking && (weightsChanged || frozen))
        sendWeightsOSC();
}

// ---------------------------------------------------------------------------
// OSC weight output
// ---------------------------------------------------------------------------

void IrisAudioProcessor::sendWeightsOSC()
{
    if (currentNearestNeighbors.empty()) return;

    float sum = 0.0f;
    for (auto& p : currentNearestNeighbors) sum += smoothedWeights[p.id];
    if (sum <= 0.0f) sum = 1.0f;

    juce::OSCMessage m("/iris/weights");
    for (auto& p : currentNearestNeighbors)
    {
        m.addString(p.name);
        m.addFloat32(smoothedWeights[p.id] / sum);
    }
    oscManager.sendOSC(m);
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------

// v2: unity-energy IR normalisation and 0 dB default output gain (was RMS 0.1 / -30 dB).
static constexpr int kStateVersion = 2;

void IrisAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml("IRIS_STATE");
    xml.setAttribute("stateVersion", kStateVersion);

    // 1. Full AudioProcessorValueTreeState serialization
    auto state = parameters.copyState();
    if (std::unique_ptr<juce::XmlElement> paramsTreeXml = state.createXml())
        xml.addChildElement(paramsTreeXml.release());

    // 2. Explicit PARAMETERS element for absolute safety / compatibility across versions
    auto* paramsXml = xml.createNewChildElement("PARAMETERS_EXPLICIT");
    if (spreadParam)      paramsXml->setAttribute("spread",      spreadParam->load());
    if (wallOpacityParam) paramsXml->setAttribute("wallOpacity", wallOpacityParam->load());
    if (normalizeParam)   paramsXml->setAttribute("normalize",   normalizeParam->load() > 0.5f);
    if (alignParam)       paramsXml->setAttribute("align",       alignParam->load() > 0.5f);
    if (inertiaParam)     paramsXml->setAttribute("inertia",     inertiaParam->load());
    if (freezeParam)      paramsXml->setAttribute("freeze",      freezeParam->load() > 0.5f);
    if (mixParam)         paramsXml->setAttribute("mix",         mixParam->load());
    if (outputGainParam)  paramsXml->setAttribute("outputGain",  outputGainParam->load());
    if (listenerXParam)   paramsXml->setAttribute("listenerX",   listenerXParam->load());
    if (listenerYParam)   paramsXml->setAttribute("listenerY",   listenerYParam->load());

    // Also set on root element for quick inspection and backwards compatibility
    if (spreadParam)      xml.setAttribute("spread",      spreadParam->load());
    if (wallOpacityParam) xml.setAttribute("wallOpacity", wallOpacityParam->load());

    juce::ScopedLock sl(stateLock);

    auto* pointsXml = xml.createNewChildElement("POINTS");
    for (const auto& p : points)
    {
        auto* pXml = pointsXml->createNewChildElement("POINT");
        pXml->setAttribute("id",          p.id.toString());
        pXml->setAttribute("name",        p.name);
        pXml->setAttribute("x",           p.x);
        pXml->setAttribute("y",           p.y);
        pXml->setAttribute("locked",      p.locked);
        pXml->setAttribute("filePath",    p.sourceFile.getFullPathName());
    }

    auto* wallsXml = xml.createNewChildElement("WALLS");
    for (const auto& w : walls)
    {
        auto* wXml = wallsXml->createNewChildElement("WALL");
        wXml->setAttribute("id",          w.id.toString());
        wXml->setAttribute("name",        w.name);
        wXml->setAttribute("x1",          w.x1);
        wXml->setAttribute("y1",          w.y1);
        wXml->setAttribute("x2",          w.x2);
        wXml->setAttribute("y2",          w.y2);
        wXml->setAttribute("locked",      w.locked);
        wXml->setAttribute("attenuation", w.attenuation);
    }

    auto* netXml = xml.createNewChildElement("NETWORK_STATE");
    netXml->setAttribute("localListenerId",   localAudioListener.id.toString());
    netXml->setAttribute("localListenerName", localAudioListener.name);
    netXml->setAttribute("selectedListenerId", selectedListenerId.toString());
    netXml->setAttribute("localLocked",       localAudioListener.locked);
    netXml->setAttribute("localX",            localAudioListener.x);
    netXml->setAttribute("localY",            localAudioListener.y);

    auto* linksXml = xml.createNewChildElement("LINKS");
    for (const auto& e : linkMatrix)
    {
        auto* eXml = linksXml->createNewChildElement("EDGE");
        eXml->setAttribute("a", e.first);
        eXml->setAttribute("b", e.second);
    }

    auto* bcXml = xml.createNewChildElement("BROADCAST_FLAGS");
    bcXml->setAttribute("listener",    broadcastListener);
    bcXml->setAttribute("irs",         broadcastIRs);
    bcXml->setAttribute("walls",       broadcastWalls);
    bcXml->setAttribute("inertia",     broadcastInertia);
    bcXml->setAttribute("freeze",      broadcastFreeze);
    bcXml->setAttribute("spread",      broadcastSpread);
    bcXml->setAttribute("mix",         broadcastMix);
    bcXml->setAttribute("wallOpacity", broadcastWallOpacity);
    bcXml->setAttribute("normalize",   broadcastNormalize);
    bcXml->setAttribute("align",       broadcastAlign);

    copyXmlToBinary(xml, destData);
}

void IrisAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (!xmlState || !xmlState->hasTagName("IRIS_STATE")) return;

    isRestoringState.store(true);
    struct RestoreGuard { std::atomic<bool>& f; ~RestoreGuard() { f.store(false); } } restoreGuard { isRestoringState };

    const int version = xmlState->getIntAttribute("stateVersion", 1);
    bool savedOutputGain = false;

    // --- Phase 1: Restore parameters FIRST so align/normalize/spread/wallOpacity are set ---
    if (auto* paramsTreeXml = xmlState->getChildByName(parameters.state.getType()))
    {
        savedOutputGain = paramsTreeXml->getChildByAttribute("id", "outputGain") != nullptr;
        parameters.replaceState(juce::ValueTree::fromXml(*paramsTreeXml));
    }

    auto restoreParam = [this](const juce::String& paramID, double val)
    {
        if (auto* p = parameters.getParameter(paramID))
            if (std::isfinite(val))
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(val)));
    };

    if (auto* paramsXml = xmlState->getChildByName("PARAMETERS_EXPLICIT"))
    {
        savedOutputGain = savedOutputGain || paramsXml->hasAttribute("outputGain");

        for (auto* id : { "spread", "wallOpacity", "inertia", "mix", "outputGain", "listenerX", "listenerY" })
            if (paramsXml->hasAttribute(id))
                restoreParam(id, paramsXml->getDoubleAttribute(id));

        for (auto* id : { "normalize", "align", "freeze" })
            if (paramsXml->hasAttribute(id))
                restoreParam(id, paramsXml->getBoolAttribute(id) ? 1.0 : 0.0);
    }
    else
    {
        if (xmlState->hasAttribute("spread"))
            restoreParam("spread", xmlState->getDoubleAttribute("spread", 0.3));
        if (xmlState->hasAttribute("wallOpacity"))
            restoreParam("wallOpacity", xmlState->getDoubleAttribute("wallOpacity", 1.0));
    }

    // Any NaN/Inf that came in through the parameter tree goes back to its default.
    for (auto* param : getParameters())
        if (! std::isfinite(param->getValue()))
            param->setValueNotifyingHost(param->getDefaultValue());

    // v1 sessions: IRs were normalised to RMS 0.1 (≈ +25..+42 dB) and the output
    // gain defaulted to -30 dB. Shift the saved gain by +30 dB so the wet level stays
    // close to what was saved and the dry signal is no longer 30 dB down.
    // (Sessions from before Output Gain existed keep the new 0 dB default.)
    if (version < 2 && savedOutputGain)
        restoreParam("outputGain", juce::jlimit(-60.0, 12.0, static_cast<double>(outputGainParam->load()) + 30.0));

    // --- Phase 2: Load IR files (heavy, outside the lock) ---
    const bool enableAlign = alignParam->load() > 0.5f;
    const bool enableNorm  = normalizeParam->load() > 0.5f;
    std::vector<IRPoint> newPoints;

    if (auto* pointsXml = xmlState->getChildByName("POINTS"))
    {
        for (auto* pXml : pointsXml->getChildIterator())
        {
            IRPoint p;
            p.id     = juce::Uuid(pXml->getStringAttribute("id"));
            if (p.id.isNull()) p.id = juce::Uuid();
            p.name   = pXml->getStringAttribute("name");
            p.x      = unitCoord(pXml->getDoubleAttribute("x", 0.5));
            p.y      = unitCoord(pXml->getDoubleAttribute("y", 0.5));
            p.locked = pXml->getBoolAttribute("locked", false);
            p.color  = colourForId(p.id);

            const auto path = pXml->getStringAttribute("filePath");
            if (juce::File::isAbsolutePath(path))
            {
                p.sourceFile = juce::File(path);
                if (readIRFile(formatManager, p.sourceFile, p))
                {
                    applyAlignAndNormalize(p, enableAlign, enableNorm);
                    updateConvolver(p);
                }
            }

            newPoints.push_back(std::move(p));
        }
    }

    std::vector<OcclusionWall> newWalls;
    if (auto* wallsXml = xmlState->getChildByName("WALLS"))
    {
        for (auto* wXml : wallsXml->getChildIterator())
        {
            OcclusionWall w;
            w.id          = juce::Uuid(wXml->getStringAttribute("id"));
            if (w.id.isNull()) w.id = juce::Uuid();
            w.name        = wXml->getStringAttribute("name");
            w.x1          = unitCoord(wXml->getDoubleAttribute("x1"), 0.0f);
            w.y1          = unitCoord(wXml->getDoubleAttribute("y1"), 0.0f);
            w.x2          = unitCoord(wXml->getDoubleAttribute("x2"), 1.0f);
            w.y2          = unitCoord(wXml->getDoubleAttribute("y2"), 0.0f);
            w.locked      = wXml->getBoolAttribute("locked", false);
            w.attenuation = sanitise(wXml->getDoubleAttribute("attenuation", 0.05), 0.05f, 0.0f, 1.0f);
            newWalls.push_back(w);
        }
    }

    // --- Phase 3: Swap the model in under the lock ---
    juce::Uuid ghostToRemove;
    bool announceListener = false;
    {
        juce::ScopedLock sl(stateLock);

        points = std::move(newPoints);
        walls  = std::move(newWalls);
        targetWeights.clear();
        smoothedWeights.clear();
        activeIDs.clear();
        selectedIRId   = juce::Uuid::null();
        selectedWallId = juce::Uuid::null();

        if (auto* netXml = xmlState->getChildByName("NETWORK_STATE"))
        {
            juce::Uuid savedId(netXml->getStringAttribute("localListenerId"));

            // Two instances restored from a duplicated track must not share an id.
            bool duplicate = savedId.isNull() || remoteListeners.count(savedId) > 0;
            if (!duplicate)
            {
                ghostToRemove = localAudioListener.id;
                localAudioListener.id   = savedId;
                localAudioListener.name = netXml->getStringAttribute("localListenerName", "Local Listener");
            }

            juce::Uuid savedSelected(netXml->getStringAttribute("selectedListenerId"));
            selectedListenerId = (savedSelected == savedId && duplicate)
                                 ? localAudioListener.id
                                 : savedSelected;

            localAudioListener.locked = netXml->getBoolAttribute("localLocked", false);
            localAudioListener.x = localAudioListener.currentX = unitCoord(netXml->getDoubleAttribute("localX", localAudioListener.x));
            localAudioListener.y = localAudioListener.currentY = unitCoord(netXml->getDoubleAttribute("localY", localAudioListener.y));
            announceListener = true;
        }

        if (auto* linksXml = xmlState->getChildByName("LINKS"))
        {
            linkMatrix.clear();
            for (auto* eXml : linksXml->getChildIterator())
            {
                const auto a = eXml->getStringAttribute("a"), b = eXml->getStringAttribute("b");
                if (! juce::Uuid(a).isNull() && ! juce::Uuid(b).isNull() && a != b)
                    linkMatrix.insert({ std::min(a, b), std::max(a, b) });
            }
        }

        // --- Phase 4: Restore per-parameter broadcast flags ---
        if (auto* bcXml = xmlState->getChildByName("BROADCAST_FLAGS"))
        {
            broadcastListener    = bcXml->getBoolAttribute("listener",    broadcastListener);
            broadcastIRs         = bcXml->getBoolAttribute("irs",         broadcastIRs);
            broadcastWalls       = bcXml->getBoolAttribute("walls",       broadcastWalls);
            broadcastInertia     = bcXml->getBoolAttribute("inertia",     broadcastInertia);
            broadcastFreeze      = bcXml->getBoolAttribute("freeze",      broadcastFreeze);
            broadcastSpread      = bcXml->getBoolAttribute("spread",      broadcastSpread);
            broadcastMix         = bcXml->getBoolAttribute("mix",         broadcastMix);
            broadcastWallOpacity = bcXml->getBoolAttribute("wallOpacity", broadcastWallOpacity);
            broadcastNormalize   = bcXml->getBoolAttribute("normalize",   broadcastNormalize);
            broadcastAlign       = bcXml->getBoolAttribute("align",       broadcastAlign);
        }

        // Publish immediately with settled weights: offline renders and hosts
        // without a running message loop get the reverb from the first block.
        pendingFadeOuts.clear();
        rebuildRenderState(true);
    }

    // The IRs were just processed with the restored flags; nothing left to do.
    reprocessPending.store(false);
    listenerParamDirty.store(false);
    pendingBroadcastMask.store(0);

    // --- Phase 5: Tell the other instances (outside stateLock) ---
    if (! ghostToRemove.isNull())
        oscManager.removeGhostId(ghostToRemove);

    if (announceListener)
        oscManager.setListenerState(localAudioListener.id, localAudioListener.name,
                                    localAudioListener.x, localAudioListener.y,
                                    false, localAudioListener.locked, this);

    recalcTailLength();
    notifyStructuralChange();
}

// ---------------------------------------------------------------------------
// JSON layout import / export
// ---------------------------------------------------------------------------

void IrisAudioProcessor::loadLayoutFromJSON(const juce::File& file)
{
    if (!file.existsAsFile()) return;

    juce::var json = juce::JSON::parse(file);
    if (!json.isObject()) return;

    auto setParam = [this](const char* id, const juce::var& v)
    {
        const double d = static_cast<double>(v);
        if (! std::isfinite(d)) return;
        if (auto* p = parameters.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(d)));
    };

    // --- Global Parameters from layout JSON ---
    if (json.hasProperty("spread"))       setParam("spread", json["spread"]);
    if (json.hasProperty("mix"))          setParam("mix", json["mix"]);
    if (json.hasProperty("inertia"))      setParam("inertia", json["inertia"]);
    if (json.hasProperty("output_gain"))  setParam("outputGain", json["output_gain"]);
    if (json.hasProperty("normalize"))    setParam("normalize", static_cast<bool>(json["normalize"]) ? 1.0 : 0.0);
    if (json.hasProperty("align"))        setParam("align", static_cast<bool>(json["align"]) ? 1.0 : 0.0);

    if (json.hasProperty("wall_opacity"))     setParam("wallOpacity", json["wall_opacity"]);
    else if (json.hasProperty("wallOpacity")) setParam("wallOpacity", json["wallOpacity"]);

    // --- Global Locks Configuration ---
    bool hasGlobalLock   = false;
    bool globalLockIRs   = false;
    bool globalLockWalls = false;
    std::optional<bool> listenerLock;

    if (json.hasProperty("locks"))
    {
        auto locksVar = json["locks"];
        if (locksVar.isBool())
        {
            hasGlobalLock   = true;
            globalLockIRs   = static_cast<bool>(locksVar);
            globalLockWalls = globalLockIRs;
        }
        else if (locksVar.isObject())
        {
            hasGlobalLock = true;
            if (locksVar.hasProperty("irs") || locksVar.hasProperty("points"))
            {
                globalLockIRs = locksVar.hasProperty("irs") ? static_cast<bool>(locksVar["irs"])
                                                            : static_cast<bool>(locksVar["points"]);
            }
            if (locksVar.hasProperty("walls"))
                globalLockWalls = static_cast<bool>(locksVar["walls"]);
            if (locksVar.hasProperty("listener"))
                listenerLock = static_cast<bool>(locksVar["listener"]);
        }
    }
    else if (json.hasProperty("locked"))
    {
        auto lockedVar = json["locked"];
        if (lockedVar.isBool())
        {
            hasGlobalLock   = true;
            globalLockIRs   = static_cast<bool>(lockedVar);
            globalLockWalls = globalLockIRs;
        }
    }

    // Coordinates are mapped from the extent block to 0..1. Missing values default
    // to the 0..1 square (same as having no extent block at all).
    auto num = [](const juce::var& obj, const char* key, double fallback)
    {
        const double d = obj.hasProperty(key) ? static_cast<double>(obj[key]) : fallback;
        return std::isfinite(d) ? d : fallback;
    };

    double xmin = 0.0, xmax = 1.0, ymin = 0.0, ymax = 1.0;
    if (auto extent = json["extent"]; extent.isObject())
    {
        xmin = num(extent, "xmin", 0.0);
        xmax = num(extent, "xmax", 1.0);
        ymin = num(extent, "ymin", 0.0);
        ymax = num(extent, "ymax", 1.0);
    }

    const double rangeX = (std::abs(xmax - xmin) < 1e-6) ? 1.0 : (xmax - xmin);
    const double rangeY = (std::abs(ymax - ymin) < 1e-6) ? 1.0 : (ymax - ymin);
    auto mapX = [&](double v) { return unitCoord((v - xmin) / rangeX, 0.0f); };
    auto mapY = [&](double v) { return unitCoord((v - ymin) / rangeY, 0.0f); };

    // --- Remove old IRs (broadcast, so every instance follows) ---
    std::vector<juce::Uuid> toRemove;
    {
        juce::ScopedLock sl(stateLock);
        for (auto& p : points) toRemove.push_back(p.id);
    }
    for (auto id : toRemove)
        removePoint(id);

    // --- Load IRs. Relative paths resolve next to the JSON file; absolute paths are
    //     used as-is. Never fall back to the host's working directory. ---
    if (auto irs = json["irs"]; irs.isArray())
    {
        for (int i = 0; i < irs.size(); ++i)
        {
            juce::var    irObj = irs[i];
            juce::String path  = irObj.getProperty("path", "").toString();
            if (path.isEmpty()) continue;

            juce::File irFile = juce::File::isAbsolutePath(path) ? juce::File(path)
                                                                 : file.getSiblingFile(path);
            if (!irFile.existsAsFile()) continue;

            juce::Uuid id = addIRFromFile(irFile);
            if (id.isNull()) continue;

            bool irLocked = hasGlobalLock ? globalLockIRs : false;
            if (irObj.hasProperty("locked"))
                irLocked = static_cast<bool>(irObj.getProperty("locked", false));
            else if (irObj.hasProperty("locks"))
                irLocked = static_cast<bool>(irObj.getProperty("locks", false));

            const juce::String name = irObj.getProperty("name", "").toString();
            if (name.isNotEmpty()) setPointName(id, name, true);

            updatePointPosition(id, mapX(num(irObj, "x", 0.0)), mapY(num(irObj, "y", 0.0)), true);
            setPointLocked(id, irLocked, true);
        }
    }

    // --- Walls (broadcast add/remove so other instances match) ---
    if (auto wallsVar = json["walls"]; wallsVar.isArray())
    {
        std::vector<juce::Uuid> oldWalls;
        {
            juce::ScopedLock sl(stateLock);
            for (auto& w : walls) oldWalls.push_back(w.id);
        }
        for (auto id : oldWalls)
            removeWall(id);

        for (int i = 0; i < wallsVar.size(); ++i)
        {
            juce::var wObj = wallsVar[i];
            if (! wObj.isObject()) continue;

            const auto id = addWall(mapX(num(wObj, "x1", 0.0)), mapY(num(wObj, "y1", 0.0)),
                                    mapX(num(wObj, "x2", 0.0)), mapY(num(wObj, "y2", 0.0)), true);

            bool wallLocked = hasGlobalLock ? globalLockWalls : false;
            if (wObj.hasProperty("locked"))
                wallLocked = static_cast<bool>(wObj.getProperty("locked", false));
            else if (wObj.hasProperty("locks"))
                wallLocked = static_cast<bool>(wObj.getProperty("locks", false));

            juce::ScopedLock sl(stateLock);
            for (auto& w : walls)
            {
                if (w.id != id) continue;
                const auto name = wObj.getProperty("name", "").toString();
                if (name.isNotEmpty()) w.name = name;
                w.locked      = wallLocked;
                w.attenuation = sanitise(num(wObj, "attenuation", 0.05), 0.05f, 0.0f, 1.0f);
                break;
            }
        }
    }

    // --- Listener ---
    if (auto l = json["listener"]; l.isObject())
        updateListenerPosition(localAudioListener.id, mapX(num(l, "x", 0.5)), mapY(num(l, "y", 0.5)), true);

    if (listenerLock)
        setListenerLocked(localAudioListener.id, *listenerLock, true);

    {
        juce::ScopedLock sl(stateLock);
        updateWeightsGaussian();
    }

    // Every IR above was loaded after normalize/align were set, so a reprocess
    // requested by those parameter changes would only redo the same work.
    reprocessPending.store(false);
    notifyStructuralChange();
}

void IrisAudioProcessor::saveLayoutToJSON(const juce::File& file)
{
    if (file == juce::File()) return;

    juce::ScopedLock sl(stateLock);

    juce::DynamicObject* root   = new juce::DynamicObject();
    juce::DynamicObject* extent = new juce::DynamicObject();
    extent->setProperty("xmin", 0.0); extent->setProperty("xmax", 1.0);
    extent->setProperty("ymin", 0.0); extent->setProperty("ymax", 1.0);
    root->setProperty("extent", extent);

    if (spreadParam)      root->setProperty("spread",       spreadParam->load());
    if (normalizeParam)   root->setProperty("normalize",    normalizeParam->load() > 0.5f);
    if (alignParam)       root->setProperty("align",        alignParam->load() > 0.5f);
    if (wallOpacityParam) root->setProperty("wall_opacity", wallOpacityParam->load());
    if (mixParam)         root->setProperty("mix",          mixParam->load());
    if (inertiaParam)     root->setProperty("inertia",      inertiaParam->load());
    if (outputGainParam)  root->setProperty("output_gain",  outputGainParam->load());

    juce::DynamicObject* listener = new juce::DynamicObject();
    listener->setProperty("x", localAudioListener.x);
    listener->setProperty("y", localAudioListener.y);
    root->setProperty("listener", listener);

    juce::Array<juce::var> irArray;
    for (const auto& p : points)
    {
        juce::DynamicObject* irObj = new juce::DynamicObject();
        irObj->setProperty("name",   p.name);
        irObj->setProperty("x",      p.x);
        irObj->setProperty("y",      p.y);
        irObj->setProperty("locked", p.locked);

        juce::String path = p.sourceFile.getFullPathName();
        if (p.sourceFile.isAChildOf(file.getParentDirectory()))
            path = p.sourceFile.getRelativePathFrom(file.getParentDirectory());
        irObj->setProperty("path", path);

        irArray.add(irObj);
    }
    root->setProperty("irs", irArray);

    juce::Array<juce::var> wallArray;
    for (const auto& w : walls)
    {
        juce::DynamicObject* wObj = new juce::DynamicObject();
        wObj->setProperty("name",        w.name);
        wObj->setProperty("x1",          w.x1);
        wObj->setProperty("y1",          w.y1);
        wObj->setProperty("x2",          w.x2);
        wObj->setProperty("y2",          w.y2);
        wObj->setProperty("locked",      w.locked);
        wObj->setProperty("attenuation", w.attenuation);
        wallArray.add(wObj);
    }
    root->setProperty("walls", wallArray);

    file.replaceWithText(juce::JSON::toString(juce::var(root)));
}

// ---------------------------------------------------------------------------
// Benchmark helper
// ---------------------------------------------------------------------------

void IrisAudioProcessor::loadDummyIRs(int count, int lengthSamples)
{
    juce::ScopedLock sl(stateLock);
    points.clear();

    for (int i = 0; i < count; ++i)
    {
        IRPoint p;
        p.id             = juce::Uuid();
        p.name           = "Dummy_" + juce::String(i);
        p.sampleRate     = 48000.0;
        p.sourceChannels = 1;
        p.sourceBuffer   = std::make_shared<juce::AudioBuffer<float>>(1, lengthSamples);
        p.rawBuffer      = std::make_shared<juce::AudioBuffer<float>>(1, lengthSamples);

        juce::Random rng(i);
        for (int s = 0; s < lengthSamples; ++s)
        {
            float val = rng.nextFloat() * 2.0f - 1.0f;
            p.sourceBuffer->setSample(0, s, val);
            p.rawBuffer->setSample(0, s, val);
        }

        updateConvolver(p);

        p.x = rng.nextFloat();
        p.y = rng.nextFloat();
        points.push_back(p);
    }

    updateWeightsGaussian();
}

// ---------------------------------------------------------------------------
// Tail length
// ---------------------------------------------------------------------------

void IrisAudioProcessor::recalcTailLength()
{
    double maxSeconds = 0.0;
    juce::ScopedLock sl(stateLock);
    for (const auto& p : points)
    {
        if (p.sourceBuffer && p.sampleRate > 0.0)
        {
            double seconds = static_cast<double>(p.sourceBuffer->getNumSamples()) / p.sampleRate;
            if (seconds > maxSeconds)
                maxSeconds = seconds;
        }
    }
    cachedTailSeconds.store(maxSeconds);
}

// ---------------------------------------------------------------------------
// Plugin entry point
// ---------------------------------------------------------------------------

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IrisAudioProcessor();
}
