#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "IrisOSCManager.h"
#include <queue>

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

// ---------------------------------------------------------------------------
// Parameter layout
// ---------------------------------------------------------------------------

juce::AudioProcessorValueTreeState::ParameterLayout IrisAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>("w1", "Weight 1", 0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("w2", "Weight 2", 0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("w3", "Weight 3", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("inertia",     "Inertia",       0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool> ("freeze",      "Freeze",        false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("spread",      "Spread",        0.0f, 1.0f, 0.3f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("mix",         "Mix",           0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("wallOpacity", "Wall Opacity",  0.0f, 1.0f, 0.8f));
    layout.add(std::make_unique<juce::AudioParameterBool> ("normalize",   "Normalize",     true));
    layout.add(std::make_unique<juce::AudioParameterBool> ("align",       "Align",         true));

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

    formatManager.registerBasicFormats();
    renderState = std::make_shared<RenderState>();

    localAudioListener.id     = juce::Uuid();
    localAudioListener.name   = "Local Listener";
    localAudioListener.isLocal = true;
    localAudioListener.x      = 0.5f;
    localAudioListener.y      = 0.5f;
    selectedListenerId = localAudioListener.id;

    parameters.addParameterListener("inertia",     this);
    parameters.addParameterListener("freeze",      this);
    parameters.addParameterListener("spread",      this);
    parameters.addParameterListener("mix",         this);
    parameters.addParameterListener("wallOpacity", this);
    parameters.addParameterListener("normalize",   this);
    parameters.addParameterListener("align",       this);

    startTimerHz(60);
    oscManager.addProcessor(this);
}

IrisAudioProcessor::~IrisAudioProcessor()
{
    oscManager.removeProcessor(this);
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
        return outputSet.size() > 0 && outputSet.size() <= 64;

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
    processSpec.sampleRate       = sampleRate;
    processSpec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    processSpec.numChannels      = 1;

    const int numOut = std::max(getTotalNumOutputChannels(), 1);
    const int numIn  = std::max(getTotalNumInputChannels(),  1);

    // Pre-allocate all audio-thread buffers.
    // avoidReallocating=true means these are no-ops if the size hasn't changed.
    inputBuffer.setSize(numIn,  samplesPerBlock, false, false, true);
    mixBuffer  .setSize(numOut, samplesPerBlock, false, false, true);

    for (auto& buf : irScratchBuffers)
        buf.setSize(numOut, samplesPerBlock, false, false, true);

    for (auto& p : points)
        updateConvolver(p);
}


void IrisAudioProcessor::releaseResources() {}

// ---------------------------------------------------------------------------
// Process block
// ---------------------------------------------------------------------------

void IrisAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    std::shared_ptr<RenderState> state = std::atomic_load(&renderState);

    const int numOutputCh = buffer.getNumChannels();
    const int numInputCh  = getTotalNumInputChannels();
    const int numSamples  = buffer.getNumSamples();

    // Copy input into pre-allocated buffer — no heap allocation on the audio thread.
    for (int ch = 0; ch < std::min(numInputCh, inputBuffer.getNumChannels()); ++ch)
        inputBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    buffer.clear();

    if (state && !state->activeIRs.empty())
    {
        // --- Parallel convolution across active IRs ---
        // Each IR writes into its own pre-allocated scratch buffer, so there are
        // no data races between jobs. We use an atomic counter as a lightweight
        // barrier: the last job to finish signals the WaitableEvent.
        const int numActive = static_cast<int>(state->activeIRs.size());

        // Count jobs that actually need processing.
        int jobCount = 0;
        for (int i = 0; i < numActive; ++i)
        {
            auto& ir = state->activeIRs[static_cast<size_t>(i)];
            if (ir.weight > 0.0001f && !ir.convolvers.empty())
                ++jobCount;
        }

        if (jobCount > 0)
        {
            convJobsDone.reset();
            convJobsRemaining.store(jobCount);

            for (int irIdx = 0; irIdx < numActive; ++irIdx)
            {
                auto& ir = state->activeIRs[static_cast<size_t>(irIdx)];
                if (ir.weight <= 0.0001f || ir.convolvers.empty()) continue;

                auto& scratch = irScratchBuffers[static_cast<size_t>(irIdx)];

                // Lambda captures pointers — all data is stable for the duration of processBlock.
                convolutionPool.addJob([this, &ir, &scratch, numSamples, numOutputCh, numInputCh]
                {
                    if (ir.sourceChannels == 1)
                    {
                        scratch.clear(0, 0, numSamples);

                        juce::dsp::AudioBlock<float> inBlock(
                            inputBuffer.getArrayOfWritePointers(), 1,
                            static_cast<size_t>(numSamples));
                        juce::dsp::AudioBlock<float> outBlock(
                            scratch.getArrayOfWritePointers(), 1,
                            static_cast<size_t>(numSamples));

                        juce::dsp::ProcessContextNonReplacing<float> ctx(inBlock, outBlock);
                        ir.convolvers[0]->process(ctx);
                    }
                    else
                    {
                        const int numCh = std::min({ numOutputCh,
                                                    static_cast<int>(ir.convolvers.size()),
                                                    numInputCh });

                        for (int ch = 0; ch < numCh; ++ch)
                        {
                            if (!ir.convolvers[static_cast<size_t>(ch)]) continue;

                            scratch.clear(ch, 0, numSamples);

                            juce::dsp::AudioBlock<float> inBlock(
                                inputBuffer.getArrayOfWritePointers() + ch, 1,
                                static_cast<size_t>(numSamples));
                            juce::dsp::AudioBlock<float> outBlock(
                                scratch.getArrayOfWritePointers() + ch, 1,
                                static_cast<size_t>(numSamples));

                            juce::dsp::ProcessContextNonReplacing<float> ctx(inBlock, outBlock);
                            ir.convolvers[static_cast<size_t>(ch)]->process(ctx);
                        }
                    }

                    // Decrement counter; last job signals the barrier.
                    if (convJobsRemaining.fetch_sub(1) == 1)
                        convJobsDone.signal();
                });
            }

            // Wait for all convolution jobs to finish before mixing.
            convJobsDone.wait();

            // --- Mix results from scratch buffers into the output ---
            for (int irIdx = 0; irIdx < numActive; ++irIdx)
            {
                auto& ir = state->activeIRs[static_cast<size_t>(irIdx)];
                if (ir.weight <= 0.0001f || ir.convolvers.empty()) continue;

                auto& scratch = irScratchBuffers[static_cast<size_t>(irIdx)];

                if (ir.sourceChannels == 1)
                {
                    for (int ch = 0; ch < numOutputCh; ++ch)
                        buffer.addFrom(ch, 0, scratch, 0, 0, numSamples, ir.weight);
                }
                else
                {
                    const int numCh = std::min({ numOutputCh,
                                                static_cast<int>(ir.convolvers.size()),
                                                numInputCh });
                    for (int ch = 0; ch < numCh; ++ch)
                        buffer.addFrom(ch, 0, scratch, ch, 0, numSamples, ir.weight);
                }
            }
        }
    }

    // Dry / Wet blend.
    const float userMix      = mixParam->load();
    const float totalW       = state ? state->totalWeight : 0.0f;
    const float effectiveWet = userMix * juce::jlimit(0.0f, 1.0f, totalW);

    for (int ch = 0; ch < numOutputCh; ++ch)
    {
        const int inputCh = (numInputCh == 1) ? 0 : ch;
        buffer.applyGain(ch, 0, numSamples, effectiveWet);
        buffer.addFrom(ch, 0, inputBuffer, inputCh, 0, numSamples, 1.0f - effectiveWet);
    }
}

// ---------------------------------------------------------------------------
// IR loading
// ---------------------------------------------------------------------------



static void applyAlignAndNormalize(IRPoint& p, bool enableAlign, bool enableNorm)
{
    int numSamples  = p.sourceBuffer->getNumSamples();
    int numChannels = p.sourceBuffer->getNumChannels();

    float storedGain  = 1.0f;
    int   storedOnset = 0;

    if (enableAlign)
    {
        float globalPeak = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            globalPeak = juce::jmax(globalPeak, p.sourceBuffer->getMagnitude(c, 0, numSamples));

        float threshold = globalPeak * 0.1f;
        int   onset     = -1;

        for (int i = 0; i < numSamples && onset == -1; ++i)
            for (int c = 0; c < numChannels; ++c)
                if (std::abs(p.sourceBuffer->getSample(c, i)) > threshold)
                    { onset = i; break; }

        if (onset < 0) onset = 0;
        storedOnset = onset;

        if (onset > 0)
        {
            int newLen = numSamples - onset;
            for (int c = 0; c < numChannels; ++c)
            {
                auto* dest = p.sourceBuffer->getWritePointer(c);
                std::memmove(dest, dest + onset, static_cast<size_t>(newLen) * sizeof(float));
            }
            p.sourceBuffer->setSize(numChannels, newLen, true);

            int fadeLen = static_cast<int>(0.005 * p.sampleRate);
            if (fadeLen > 0 && fadeLen < newLen)
                p.sourceBuffer->applyGainRamp(0, 0, fadeLen, 0.0f, 1.0f);

            numSamples = newLen;
        }
    }

    if (enableNorm)
    {
        float rms = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            rms += p.sourceBuffer->getRMSLevel(c, 0, numSamples);
        rms /= static_cast<float>(numChannels);

        const float targetRMS = 0.1f;
        if (rms > 0.00001f)
        {
            storedGain = targetRMS / rms;
            p.sourceBuffer->applyGain(storedGain);
        }
    }

    p.normGain    = storedGain;
    p.onsetOffset = storedOnset;
}

juce::Uuid IrisAudioProcessor::addIRFromFile(const juce::File& file)
{
    if (!file.existsAsFile()) return juce::Uuid::null();

    if (formatManager.getNumKnownFormats() == 0)
        formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader) return juce::Uuid::null();

    int numSamples  = static_cast<int>(reader->lengthInSamples);
    int numChannels = static_cast<int>(reader->numChannels);
    if (numSamples <= 0 || numChannels <= 0) return juce::Uuid::null();

    IRPoint p;
    p.id            = juce::Uuid();
    p.name          = file.getFileName();
    p.sourceFile    = file;
    p.sampleRate    = reader->sampleRate;
    p.sourceChannels = numChannels;

    p.rawBuffer    = std::make_shared<juce::AudioBuffer<float>>(numChannels, numSamples);
    reader->read(p.rawBuffer.get(), 0, numSamples, 0, true, true);
    p.sourceBuffer = std::make_shared<juce::AudioBuffer<float>>(*p.rawBuffer);

    applyAlignAndNormalize(p, alignParam->load() > 0.5f, normalizeParam->load() > 0.5f);

    int   n   = static_cast<int>(points.size());
    float col = static_cast<float>(n % 3);
    float row = std::floor(static_cast<float>(n) / 3.0f);
    p.x = 0.2f + col * 0.2f;
    p.y = 0.2f + row * 0.2f;

    juce::Random rng(p.id.toString().hashCode());
    p.color = juce::Colour::fromHSV(rng.nextFloat() * 0.15f + 0.55f, 0.8f, 0.9f, 1.0f);

    updateConvolver(p);
    points.push_back(p);
    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();

    oscManager.syncAddIR(p.id, p.name, file, this);
    recalcTailLength();
    return p.id;
}

void IrisAudioProcessor::addIRFromFileWithID(const juce::File& file, juce::Uuid id)
{
    if (!file.existsAsFile()) return;

    {
        juce::ScopedLock sl(stateLock);
        for (const auto& p : points)
            if (p.id == id) return;
    }

    if (formatManager.getNumKnownFormats() == 0)
        formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader) return;

    int numSamples  = static_cast<int>(reader->lengthInSamples);
    int numChannels = static_cast<int>(reader->numChannels);

    IRPoint p;
    p.id             = id;
    p.name           = file.getFileName();
    p.sourceFile     = file;
    p.sampleRate     = reader->sampleRate;
    p.sourceChannels = numChannels;

    p.rawBuffer    = std::make_shared<juce::AudioBuffer<float>>(numChannels, numSamples);
    reader->read(p.rawBuffer.get(), 0, numSamples, 0, true, true);
    p.sourceBuffer = std::make_shared<juce::AudioBuffer<float>>(*p.rawBuffer);

    applyAlignAndNormalize(p, alignParam->load() > 0.5f, normalizeParam->load() > 0.5f);

    int   n   = static_cast<int>(points.size());
    float col = static_cast<float>(n % 3);
    float row = std::floor(static_cast<float>(n) / 3.0f);
    p.x = 0.2f + col * 0.2f;
    p.y = 0.2f + row * 0.2f;

    juce::Random rng(p.id.toString().hashCode());
    p.color = juce::Colour::fromHSV(rng.nextFloat() * 0.15f + 0.55f, 0.8f, 0.9f, 1.0f);

    updateConvolver(p);

    {
        juce::ScopedLock sl(stateLock);
        points.push_back(p);
    }
    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();
    recalcTailLength();
}

void IrisAudioProcessor::addIRPoint(const juce::String& name)
{
    IRPoint p;
    p.id   = juce::Uuid();
    p.name = name;

    int n = static_cast<int>(points.size());
    p.x = 0.2f + (n % 3) * 0.2f;
    p.y = 0.2f + std::floor(n / 3.0f) * 0.2f;

    juce::Random rng(p.id.toString().hashCode());
    p.color = juce::Colour::fromHSV(rng.nextFloat() * 0.15f + 0.55f, 0.8f, 0.9f, 1.0f);

    {
        juce::ScopedLock sl(stateLock);
        points.push_back(p);
    }
    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();
}

// ---------------------------------------------------------------------------
// Convolver management
// ---------------------------------------------------------------------------

void IrisAudioProcessor::updateConvolver(IRPoint& p)
{
    if (!p.sourceBuffer || p.sourceChannels == 0) return;

    p.convolvers.clear();

    int numOutputChannels = getTotalNumOutputChannels();
    if (numOutputChannels <= 0) numOutputChannels = 1;

    // Mono IR: create one convolver; processBlock will broadcast it to all output channels.
    // Multi-channel IR: create one convolver per matching channel, capped at the smaller
    // of output count and IR channel count. Excess output channels stay silent.
    const int numConvolvers = (p.sourceChannels == 1)
                            ? 1
                            : std::min(numOutputChannels, p.sourceChannels);

    for (int ch = 0; ch < numConvolvers; ++ch)
    {
        auto conv = std::make_shared<juce::dsp::Convolution>();

        const int len = p.sourceBuffer->getNumSamples();
        juce::AudioBuffer<float> mono(1, len);
        mono.copyFrom(0, 0, *p.sourceBuffer, ch, 0, len);

        if (processSpec.sampleRate > 0)
            conv->prepare(processSpec);

        conv->loadImpulseResponse(std::move(mono),
                                  p.sampleRate,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::no,
                                  juce::dsp::Convolution::Normalise::no);

        p.convolvers.push_back(conv);
    }
}


void IrisAudioProcessor::reprocessIRPoints()
{
    suspendProcessing(true);
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
    suspendProcessing(false);
    if (onStateChanged) onStateChanged();
    recalcTailLength();
}

// ---------------------------------------------------------------------------
// Point management
// ---------------------------------------------------------------------------

void IrisAudioProcessor::removePoint(juce::Uuid id, bool broadcast)
{
    if (broadcast && broadcastIRs)
        oscManager.syncRemoveIR(id, this);

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
    if (onStateChanged) onStateChanged();
    recalcTailLength();
}

void IrisAudioProcessor::updatePointPosition(juce::Uuid id, float x, float y, bool broadcast)
{
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
}

void IrisAudioProcessor::setPointLocked(juce::Uuid id, bool locked, bool broadcast)
{
    if (broadcast && broadcastIRs)
        oscManager.syncLocked(id, locked, this);

    juce::ScopedLock sl(stateLock);
    for (auto& p : points)
        if (p.id == id) { p.locked = locked; break; }

    for (auto& w : walls)
        if (w.id == id) { w.locked = locked; break; }

    if (onStateChanged) onStateChanged();
}

void IrisAudioProcessor::setPointName(juce::Uuid id, const juce::String& name, bool broadcast)
{
    if (broadcast && broadcastIRs)
        oscManager.syncIRName(id, name, this);

    juce::ScopedLock sl(stateLock);
    for (auto& p : points)
        if (p.id == id) { p.name = name; break; }

    for (auto& w : walls)
        if (w.id == id) { w.name = name; break; }

    if (onStateChanged) onStateChanged();
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

    {
        juce::ScopedLock sl(stateLock);
        w.name = "Wall " + juce::String(walls.size() + 1);
        w.x1 = juce::jlimit(0.0f, 1.0f, x1);
        w.y1 = juce::jlimit(0.0f, 1.0f, y1);
        w.x2 = juce::jlimit(0.0f, 1.0f, x2);
        w.y2 = juce::jlimit(0.0f, 1.0f, y2);
        walls.push_back(w);
    }

    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();

    if (broadcast && broadcastWalls)
        oscManager.syncAddWall(w.id, x1, y1, x2, y2, this);

    return w.id;
}

void IrisAudioProcessor::addWallWithID(juce::Uuid id, float x1, float y1, float x2, float y2)
{
    juce::ScopedLock sl(stateLock);
    for (const auto& existing : walls)
        if (existing.id == id) return;

    OcclusionWall w;
    w.id          = id;
    w.name        = "Wall " + juce::String(walls.size() + 1);
    w.x1          = juce::jlimit(0.0f, 1.0f, x1);
    w.y1          = juce::jlimit(0.0f, 1.0f, y1);
    w.x2          = juce::jlimit(0.0f, 1.0f, x2);
    w.y2          = juce::jlimit(0.0f, 1.0f, y2);
    w.attenuation = 0.05f;
    w.color       = juce::Colours::cyan;
    walls.push_back(w);

    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();
}

void IrisAudioProcessor::removeWall(juce::Uuid id, bool broadcast)
{
    if (broadcast && broadcastWalls)
        oscManager.syncRemoveWall(id, this);

    juce::ScopedLock sl(stateLock);
    for (auto it = walls.begin(); it != walls.end(); ++it)
    {
        if (it->id == id) { walls.erase(it); break; }
    }
    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();
}

void IrisAudioProcessor::updateWall(juce::Uuid id, float x1, float y1, float x2, float y2, bool broadcast)
{
    if (broadcast && broadcastWalls)
        oscManager.syncWallPosition(id, x1, y1, x2, y2, this);

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
            }
            break;
        }
    }
    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();
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

void IrisAudioProcessor::updateListenerPosition(juce::Uuid id, float x, float y, bool broadcast)
{
    juce::ScopedLock sl(stateLock);

    NetworkListener* moved = nullptr;
    if (id == localAudioListener.id)
        moved = &localAudioListener;
    else if (remoteListeners.count(id))
        moved = &remoteListeners[id];

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
            else if (remoteListeners.count(uId)) follower = &remoteListeners[uId];

            if (follower && !follower->locked)
            {
                follower->x = moved->x;
                follower->y = moved->y;
                oscManager.setListenerState(follower->id, follower->name,
                                            follower->x, follower->y,
                                            false, follower->locked, this);
            }
        }

        oscManager.setListenerState(moved->id, moved->name,
                                    moved->x, moved->y,
                                    false, moved->locked, this);
    }

    if (onStateChanged) onStateChanged();
}

void IrisAudioProcessor::setListenerLocked(juce::Uuid id, bool locked, bool broadcast)
{
    juce::ScopedLock sl(stateLock);

    if (id == localAudioListener.id)
        localAudioListener.locked = locked;
    else if (remoteListeners.count(id))
        remoteListeners[id].locked = locked;

    if (onStateChanged) onStateChanged();

    if (broadcast)
    {
        if (id == localAudioListener.id)
            oscManager.setListenerState(id, localAudioListener.name,
                                        localAudioListener.x, localAudioListener.y,
                                        false, locked, this);
        else if (remoteListeners.count(id))
        {
            const auto& remote = remoteListeners[id];
            oscManager.setListenerState(id, remote.name, remote.x, remote.y, false, locked, this);
        }
    }
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

    if (onStateChanged) onStateChanged();
    if (broadcast) oscManager.syncLinkMatrix(this);
}

void IrisAudioProcessor::setLinkMatrixConnections(const std::vector<std::pair<juce::String, juce::String>>& edges)
{
    juce::ScopedLock sl(stateLock);
    linkMatrix.clear();
    for (const auto& edge : edges)
        linkMatrix.insert(edge);
    if (onStateChanged) onStateChanged();
}

// ---------------------------------------------------------------------------
// Parameter sync
// ---------------------------------------------------------------------------

void IrisAudioProcessor::updateParameterNotifiers(juce::String paramId, float value)
{
    isUpdatingFromOSC.store(true);
    if (auto* p = parameters.getParameter(paramId))
        p->setValueNotifyingHost(p->convertTo0to1(value));
    isUpdatingFromOSC.store(false);
}

void IrisAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "normalize" || parameterID == "align")
        reprocessIRPoints();

    if (!isUpdatingFromOSC.load() && broadcastGlobals)
        oscManager.setGlobalParam(parameterID, newValue, this);
}

// ---------------------------------------------------------------------------
// Gaussian weight computation
// ---------------------------------------------------------------------------

void IrisAudioProcessor::updateWeightsGaussian()
{
    if (points.empty())
    {
        targetWeights.clear();
        activeIDs.clear();
        return;
    }

    float spread   = spreadParam->load();
    float sigmaVal = 0.05f + 1.5f * spread * spread;

    float baseOpacity = wallOpacityParam ? wallOpacityParam->load() : 0.8f;

    std::vector<std::pair<float, IRPoint*>> rawWeights;
    float maxWeight = 0.0f;

    for (auto& p : points)
    {
        float dx = p.x - localAudioListener.currentX;
        float dy = p.y - localAudioListener.currentY;
        float w  = std::exp(-(dx*dx + dy*dy) / (2.0f * sigmaVal * sigmaVal));

        float occlusionFactor   = 1.0f;
        int   intersectionCount = 0;

        for (const auto& wall : walls)
        {
            float ix, iy, tWall;
            if (getIntersectionPoint(localAudioListener.currentX, localAudioListener.currentY,
                                     p.x, p.y,
                                     wall.x1, wall.y1, wall.x2, wall.y2,
                                     ix, iy, tWall))
            {
                ++intersectionCount;

                float edgeFade = 1.0f;
                if      (tWall < 0.2f) edgeFade = tWall / 0.2f;
                else if (tWall > 0.8f) edgeFade = (1.0f - tWall) / 0.2f;

                occlusionFactor *= 1.0f - (baseOpacity * edgeFade);
            }
        }

        w *= occlusionFactor;

        p.debug_rawWeight        = std::exp(-(dx*dx + dy*dy) / (2.0f * sigmaVal * sigmaVal));
        p.debug_occlusionFactor  = occlusionFactor;
        p.debug_intersectionCount = intersectionCount;
        p.debug_finalWeight      = w;

        rawWeights.push_back({ w, &p });
        if (w > maxWeight) maxWeight = w;
    }

    // Sort descending by weight
    std::sort(rawWeights.begin(), rawWeights.end(),
              [](const auto& a, const auto& b){ return a.first > b.first; });

    const int kMax = (maxActiveOverride > 0) ? maxActiveOverride : 8;
    const int kMin = (maxActiveOverride > 0) ? std::min(4, maxActiveOverride) : 4;

    std::set<juce::Uuid> nextActiveIDs;

    for (int i = 0; i < std::min(static_cast<int>(rawWeights.size()), kMin); ++i)
        nextActiveIDs.insert(rawWeights[static_cast<size_t>(i)].second->id);

    for (int i = kMin; i < std::min(static_cast<int>(rawWeights.size()), kMax); ++i)
    {
        float      w  = rawWeights[static_cast<size_t>(i)].first;
        juce::Uuid id = rawWeights[static_cast<size_t>(i)].second->id;

        bool wasActive = (activeIDs.find(id) != activeIDs.end());
        if (wasActive ? (w >= tauOut * maxWeight) : (w >= tauIn * maxWeight))
            nextActiveIDs.insert(id);
    }

    activeIDs = nextActiveIDs;

    targetWeights.clear();
    for (auto& pair : rawWeights)
        targetWeights[pair.second->id] = activeIDs.count(pair.second->id) ? pair.first : 0.0f;
}

// ---------------------------------------------------------------------------
// Timer callback — physics, smoothing, render state swap
// ---------------------------------------------------------------------------

void IrisAudioProcessor::timerCallback()
{
    bool  frozen       = freezeParam->load() > 0.5f;
    float inertia      = inertiaParam->load();
    float physicsAlpha = 1.0f - 0.98f * inertia;

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

    updateWeightsGaussian();

    const float smoothAlpha  = 0.25f;
    bool        changed      = false;
    float       sumForNorm   = 0.0f;

    auto nextState = std::make_shared<RenderState>();

    for (auto& p : points)
    {
        float target = targetWeights.count(p.id) ? targetWeights[p.id] : 0.0f;

        if (smoothedWeights.find(p.id) == smoothedWeights.end())
            smoothedWeights[p.id] = 0.0f;

        float& current = smoothedWeights[p.id];
        if (std::abs(target - current) > 0.0001f)
        {
            current += (target - current) * smoothAlpha;
            if (current < 0.001f && target < 1e-6f) current = 0.0f;
            changed = true;
        }

        if (current > 0.0f) sumForNorm += current;
    }

    nextState->totalWeight = sumForNorm;

    currentNearestNeighbors.clear();

    for (auto& p : points)
    {
        float w = smoothedWeights[p.id];
        if (w > 0.0001f)
        {
            float normW = (sumForNorm > 0.001f) ? (w / sumForNorm) : 0.0f;

            if (!p.convolvers.empty())
            {
                ActiveIR air;
                air.convolvers     = p.convolvers;
                air.weight         = normW;
                air.sourceChannels = p.sourceChannels;
                air.id             = p.id;
                nextState->activeIRs.push_back(air);
            }

            currentNearestNeighbors.push_back(p);
        }
    }

    std::sort(currentNearestNeighbors.begin(), currentNearestNeighbors.end(),
              [&](const IRPoint& a, const IRPoint& b)
              { return smoothedWeights[a.id] > smoothedWeights[b.id]; });

    // Hold the previous state on this (message) thread for one more tick.
    // This ensures its refcount never drops to zero on the audio thread,
    // preventing a free() call there. The old state is released here next tick.
    prevRenderState = std::atomic_load(&renderState);
    std::atomic_store(&renderState, nextState);

    if (!isBenchmarking && (changed || frozen))
    {
        if (!currentNearestNeighbors.empty())
        {
            if (currentNearestNeighbors.size() > 0) *weight1 = smoothedWeights[currentNearestNeighbors[0].id] / sumForNorm; else *weight1 = 0;
            if (currentNearestNeighbors.size() > 1) *weight2 = smoothedWeights[currentNearestNeighbors[1].id] / sumForNorm; else *weight2 = 0;
            if (currentNearestNeighbors.size() > 2) *weight3 = smoothedWeights[currentNearestNeighbors[2].id] / sumForNorm; else *weight3 = 0;

            sendWeightsOSC();
        }
        // Signal the editor to repaint at its own rate.
        // Do NOT call onStateChanged here — that triggers full list rebuilds.
        pendingUIRepaint.store(true);
    }
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

void IrisAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml("IRIS_STATE");

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
        pXml->setAttribute("normGain",    p.normGain);
        pXml->setAttribute("onsetOffset", p.onsetOffset);
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

    copyXmlToBinary(xml, destData);
}

void IrisAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (!xmlState || !xmlState->hasTagName("IRIS_STATE")) return;

    points.clear();

    if (auto* pointsXml = xmlState->getChildByName("POINTS"))
    {
        for (auto* pXml : pointsXml->getChildIterator())
        {
            IRPoint p;
            p.id   = juce::Uuid(pXml->getStringAttribute("id"));
            p.name = pXml->getStringAttribute("name");
            p.x    = static_cast<float>(pXml->getDoubleAttribute("x"));
            p.y    = static_cast<float>(pXml->getDoubleAttribute("y"));

            juce::File f(pXml->getStringAttribute("filePath"));
            p.sourceFile = f;

            juce::Random rng(p.id.toString().hashCode());
            p.color = juce::Colour::fromHSV(rng.nextFloat() * 0.15f + 0.55f, 0.8f, 0.9f, 1.0f);

            if (f.existsAsFile())
            {
                std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(f));
                if (reader)
                {
                    p.sampleRate     = reader->sampleRate;
                    p.sourceChannels = static_cast<int>(reader->numChannels);

                    p.rawBuffer = std::make_shared<juce::AudioBuffer<float>>(
                        static_cast<int>(reader->numChannels),
                        static_cast<int>(reader->lengthInSamples));
                    reader->read(p.rawBuffer.get(), 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

                    p.normGain    = static_cast<float>(pXml->getDoubleAttribute("normGain",    1.0));
                    p.onsetOffset = pXml->getIntAttribute("onsetOffset", 0);

                    p.sourceBuffer = std::make_shared<juce::AudioBuffer<float>>(*p.rawBuffer);
                    applyAlignAndNormalize(p, alignParam->load() > 0.5f, normalizeParam->load() > 0.5f);
                    updateConvolver(p);
                }
            }

            points.push_back(p);
        }
    }

    walls.clear();
    if (auto* wallsXml = xmlState->getChildByName("WALLS"))
    {
        for (auto* wXml : wallsXml->getChildIterator())
        {
            OcclusionWall w;
            w.id          = juce::Uuid(wXml->getStringAttribute("id"));
            w.name        = wXml->getStringAttribute("name");
            w.x1          = static_cast<float>(wXml->getDoubleAttribute("x1"));
            w.y1          = static_cast<float>(wXml->getDoubleAttribute("y1"));
            w.x2          = static_cast<float>(wXml->getDoubleAttribute("x2"));
            w.y2          = static_cast<float>(wXml->getDoubleAttribute("y2"));
            w.locked      = wXml->getBoolAttribute("locked");
            w.attenuation = static_cast<float>(wXml->getDoubleAttribute("attenuation", 0.5));
            walls.push_back(w);
        }
    }

    if (auto* netXml = xmlState->getChildByName("NETWORK_STATE"))
    {
        juce::Uuid savedId(netXml->getStringAttribute("localListenerId"));

        bool duplicate = remoteListeners.count(savedId) > 0;
        if (!duplicate)
        {
            juce::Uuid oldId = localAudioListener.id;
            localAudioListener.id   = savedId;
            localAudioListener.name = netXml->getStringAttribute("localListenerName", "Local Listener");
            oscManager.removeGhostId(oldId);
        }

        juce::Uuid savedSelected(netXml->getStringAttribute("selectedListenerId"));
        selectedListenerId = (savedSelected == savedId && duplicate)
                             ? localAudioListener.id
                             : savedSelected;

        localAudioListener.locked = netXml->getBoolAttribute("localLocked", false);

        oscManager.setListenerState(localAudioListener.id, localAudioListener.name,
                                    localAudioListener.x, localAudioListener.y,
                                    false, localAudioListener.locked, this);
    }

    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();
}

// ---------------------------------------------------------------------------
// JSON layout import / export
// ---------------------------------------------------------------------------

void IrisAudioProcessor::loadLayoutFromJSON(const juce::File& file)
{
    if (!file.existsAsFile()) return;

    juce::var json = juce::JSON::parse(file);
    if (!json.isObject()) return;

    float xmin = 0.0f, xmax = 1.0f, ymin = 0.0f, ymax = 1.0f;
    if (auto extent = json["extent"]; extent.isObject())
    {
        xmin = extent.getProperty("xmin", 0.0f);
        xmax = extent.getProperty("xmax", 100.0f);
        ymin = extent.getProperty("ymin", 0.0f);
        ymax = extent.getProperty("ymax", 100.0f);
    }

    float rangeX = (std::abs(xmax - xmin) < 0.001f) ? 1.0f : (xmax - xmin);
    float rangeY = (std::abs(ymax - ymin) < 0.001f) ? 1.0f : (ymax - ymin);

    // --- Phase 1: Collect IDs to remove while holding the lock ---
    std::vector<juce::Uuid> toRemove;
    {
        juce::ScopedLock sl(stateLock);
        for (auto& p : points) toRemove.push_back(p.id);
    }

    // --- Phase 2: Remove old points WITHOUT holding the lock ---
    for (auto id : toRemove)
        removePoint(id);

    // --- Phase 3: Parse IR specs from JSON (no lock needed, just reading JSON) ---
    struct IRSpec { juce::File irFile; float wx, wy; juce::String name; };
    std::vector<IRSpec> irSpecs;

    if (auto irs = json["irs"]; irs.isArray())
    {
        for (int i = 0; i < irs.size(); ++i)
        {
            juce::var    irObj = irs[i];
            juce::String path  = irObj.getProperty("path", "");

            juce::File irFile = file.getSiblingFile(path);
            if (!irFile.existsAsFile())
                irFile = juce::File(path);

            if (irFile.existsAsFile())
            {
                IRSpec spec;
                spec.irFile = irFile;
                spec.wx     = irObj.getProperty("x",    0.0f);
                spec.wy     = irObj.getProperty("y",    0.0f);
                spec.name   = irObj.getProperty("name", "");
                irSpecs.push_back(spec);
            }
        }
    }

    // --- Phase 4: Load IR files WITHOUT the lock (heavy I/O) ---
    for (auto& spec : irSpecs)
    {
        juce::Uuid id = addIRFromFile(spec.irFile);

        if (id != juce::Uuid::null())
        {
            float xn = juce::jlimit(0.0f, 1.0f, (spec.wx - xmin) / rangeX);
            float yn = juce::jlimit(0.0f, 1.0f, (spec.wy - ymin) / rangeY);

            juce::ScopedLock sl(stateLock);
            for (auto& p : points)
            {
                if (p.id == id)
                {
                    p.x = xn; p.y = yn;
                    if (spec.name.isNotEmpty()) p.name = spec.name;
                    break;
                }
            }
        }
    }

    // --- Phase 5: Parse and apply walls under lock ---
    if (auto wallsVar = json["walls"]; wallsVar.isArray())
    {
        juce::ScopedLock sl(stateLock);
        walls.clear();
        for (int i = 0; i < wallsVar.size(); ++i)
        {
            juce::var wObj = wallsVar[i];

            float nx1 = juce::jlimit(0.0f, 1.0f, (static_cast<float>(wObj.getProperty("x1", 0.0f)) - xmin) / rangeX);
            float ny1 = juce::jlimit(0.0f, 1.0f, (static_cast<float>(wObj.getProperty("y1", 0.0f)) - ymin) / rangeY);
            float nx2 = juce::jlimit(0.0f, 1.0f, (static_cast<float>(wObj.getProperty("x2", 0.0f)) - xmin) / rangeX);
            float ny2 = juce::jlimit(0.0f, 1.0f, (static_cast<float>(wObj.getProperty("y2", 0.0f)) - ymin) / rangeY);

            OcclusionWall w;
            w.id          = juce::Uuid();
            w.name        = wObj.getProperty("name", "Wall");
            w.x1          = nx1; w.y1 = ny1;
            w.x2          = nx2; w.y2 = ny2;
            w.locked      = wObj.getProperty("locked",      false);
            w.attenuation = wObj.getProperty("attenuation", 0.05f);
            w.color       = juce::Colours::cyan;
            walls.push_back(w);
        }
    }

    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();
}

void IrisAudioProcessor::saveLayoutToJSON(const juce::File& file)
{
    juce::ScopedLock sl(stateLock);

    juce::DynamicObject* root   = new juce::DynamicObject();
    juce::DynamicObject* extent = new juce::DynamicObject();
    extent->setProperty("xmin", 0.0); extent->setProperty("xmax", 1.0);
    extent->setProperty("ymin", 0.0); extent->setProperty("ymax", 1.0);
    root->setProperty("extent", extent);

    juce::Array<juce::var> irArray;
    for (const auto& p : points)
    {
        juce::DynamicObject* irObj = new juce::DynamicObject();
        irObj->setProperty("name", p.name);
        irObj->setProperty("x",    p.x);
        irObj->setProperty("y",    p.y);

        juce::String path = p.sourceFile.getFullPathName();
        if (path.startsWith(file.getParentDirectory().getFullPathName()))
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
