#include "PluginProcessorV3.h"
#include "PluginEditorV3.h"

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    // Legacy weights (internal, not exposed as params effectively for automation usually, but here we go)
    // Actually, we can keep them for structure if needed, or remove. 
    // The previous code had them.
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("w1", "Weight 1", 0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("w2", "Weight 2", 0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("w3", "Weight 3", 0.0f, 1.0f, 0.0f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>("inertia", "Inertia", 0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>("freeze", "Freeze", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("spread", "Spread", 0.0f, 1.0f, 0.3f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("wallOpacity", "Wall Opacity", 0.0f, 1.0f, 0.8f));
    
    return layout;
}

IrisVSTV3AudioProcessor::IrisVSTV3AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    // Parameters
    weight1 = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("w1"));
    weight2 = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("w2"));
    weight3 = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("w3"));
    
    inertiaParam = parameters.getRawParameterValue("inertia");
    freezeParam = parameters.getRawParameterValue("freeze");
    spreadParam = parameters.getRawParameterValue("spread");
    mixParam = parameters.getRawParameterValue("mix");
    wallOpacityParam = parameters.getRawParameterValue("wallOpacity");
    
    // Audio Format
    formatManager.registerBasicFormats();
    
    // Init Render State
    renderState = std::make_shared<RenderState>();

    // Initialize with a listener
    IRPoint listener;
    listener.id = juce::Uuid();
    listener.name = "Listener";
    listener.color = juce::Colours::red;
    listener.x = 0.5f;
    listener.y = 0.5f;
    listener.isListener = true;
    points.push_back(listener);
    
    // OSC
    oscReceiver.connect(9001);
    oscReceiver.addListener(this);
    
    oscSender.connect("127.0.0.1", 9002);
    
    startTimerHz(60); // 60 FPS update
}

IrisVSTV3AudioProcessor::~IrisVSTV3AudioProcessor()
{
    oscReceiver.disconnect();
}

const juce::String IrisVSTV3AudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool IrisVSTV3AudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool IrisVSTV3AudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool IrisVSTV3AudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double IrisVSTV3AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int IrisVSTV3AudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int IrisVSTV3AudioProcessor::getCurrentProgram()
{
    return 0;
}

void IrisVSTV3AudioProcessor::setCurrentProgram (int index)
{
}

const juce::String IrisVSTV3AudioProcessor::getProgramName (int index)
{
    return {};
}

void IrisVSTV3AudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void IrisVSTV3AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    processSpec.sampleRate = sampleRate;
    processSpec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    processSpec.numChannels = 1; // MONO

    // Ensure mixing buffer is large enough
    mixBuffer.setSize(1, samplesPerBlock);

    // Initialize/Reset all convolvers
    for (auto& p : points)
    {
        if (p.convolver)
        {
            p.convolver->prepare(processSpec);
            p.convolver->reset();
        }
    }
}

void IrisVSTV3AudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool IrisVSTV3AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // STRICT MONO
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void IrisVSTV3AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    // Check if we have active IRs
    std::shared_ptr<RenderState> state = std::atomic_load(&renderState);
    
    // PASSTHROUGH LOGIC
    // If no active IRs, we do NOTHING to the buffer.
    // Audio behaves as if plugin is bypassed or wire.
    if (!state || state->activeIRs.empty()) 
    {
        // NO-OP. Buffer passes through untouched.
        return; 
    }
    
    // Mono Processing
    // Buffer is Mono.
    
    // Copy Input
    juce::AudioBuffer<float> inputBuffer;
    inputBuffer.makeCopyOf(buffer, true); 
    
    // Clear Output for accumulation
    buffer.clear(); 
    
    // Ensure mix buffer
    if (mixBuffer.getNumSamples() != buffer.getNumSamples())
        mixBuffer.setSize(1, buffer.getNumSamples());
    
    for (auto& ir : state->activeIRs)
    {
        if (ir.convolver && ir.weight > 0.0001f)
        {
            mixBuffer.clear();
            
            // Convolve Mono Input -> Mono MixBuffer
            juce::dsp::AudioBlock<float> inputBlock(inputBuffer);
            juce::dsp::AudioBlock<float> outputBlock(mixBuffer);
            
            // Create Context
            juce::dsp::ProcessContextNonReplacing<float> context(inputBlock, outputBlock);
            ir.convolver->process(context);
            
            // Accumulate MixBuffer * Weight -> Main Buffer
            // Mono: ch 0 to ch 0
            // We accumulate to mixBuffer first effectively (well, here strictly accumulating to OUTPUT 'buffer'
            // BUT, wait. 'buffer' was cleared at line 179.
            // So 'buffer' IS the wet signal accumulator.
            buffer.addFrom(0, 0, mixBuffer, 0, 0, mixBuffer.getNumSamples(), ir.weight);
        }
    }
    
    // DRY / WET MIX
    float mix = mixParam->load();
    
    // buffer now contains Pure Wet.
    // inputBuffer contains Dry.
    
    // y = (1-mix)*x + mix*wet
    // We can do this in place on 'buffer' (which is wet).
    // buffer = buffer * mix + inputBuffer * (1-mix)
    
    buffer.applyGain(mix);
    buffer.addFrom(0, 0, inputBuffer, 0, 0, buffer.getNumSamples(), 1.0f - mix);
}

bool IrisVSTV3AudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* IrisVSTV3AudioProcessor::createEditor()
{
    return new IrisVSTV3AudioProcessorEditor (*this);
}


void IrisVSTV3AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml("IRIS_STATE");
    
    // Save Points
    juce::XmlElement* pointsXml = xml.createNewChildElement("POINTS");
    for (const auto& p : points)
    {
        juce::XmlElement* pXml = pointsXml->createNewChildElement("POINT");
        pXml->setAttribute("id", p.id.toString());
        pXml->setAttribute("name", p.name);
        pXml->setAttribute("x", p.x);
        pXml->setAttribute("y", p.y);
        pXml->setAttribute("isListener", p.isListener);
        pXml->setAttribute("locked", p.locked);
        pXml->setAttribute("channel", p.selectedChannel); // Save Channel
        pXml->setAttribute("filePath", p.sourceFile.getFullPathName());
        pXml->setAttribute("normGain", p.normGain);
        pXml->setAttribute("onsetOffset", p.onsetOffset);
    }

    // Save Walls
    juce::XmlElement* wallsXml = xml.createNewChildElement("WALLS");
    for (const auto& w : walls)
    {
        juce::XmlElement* wXml = wallsXml->createNewChildElement("WALL");
        wXml->setAttribute("id", w.id.toString());
        wXml->setAttribute("name", w.name);
        wXml->setAttribute("x1", w.x1);
        wXml->setAttribute("y1", w.y1);
        wXml->setAttribute("x2", w.x2);
        wXml->setAttribute("y2", w.y2);
        wXml->setAttribute("locked", w.locked);
        wXml->setAttribute("attenuation", w.attenuation);
    }

    copyXmlToBinary(xml, destData);
}

void IrisVSTV3AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr && xmlState->hasTagName("IRIS_STATE"))
    {
        points.clear();
        
        // Restore Points
        if (auto* pointsXml = xmlState->getChildByName("POINTS"))
        {
            for (auto* pXml : pointsXml->getChildIterator())
            {
                IRPoint p;
                p.id = juce::Uuid(pXml->getStringAttribute("id"));
                p.name = pXml->getStringAttribute("name");
                p.x = (float)pXml->getDoubleAttribute("x");
                p.y = (float)pXml->getDoubleAttribute("y");
                p.isListener = pXml->getBoolAttribute("isListener");
                p.locked = pXml->getBoolAttribute("locked");
                p.selectedChannel = pXml->getIntAttribute("channel", 0);
                
                juce::File f = pXml->getStringAttribute("filePath");
                p.sourceFile = f;
                
                if (p.isListener)
                {
                    p.color = juce::Colours::red;
                }
                else
                {
                    // Generate color (deterministic based on ID or random?)
                    // Let's keep the random logic or save color. 
                    // For now, regen random color or user generic
                    juce::Random r(p.id.toString().hashCode());
                    float hue = r.nextFloat() * 0.15f + 0.55f; 
                    p.color = juce::Colour::fromHSV(hue, 0.8f, 0.9f, 1.0f);
                    
                    // Reload Audio if file exists
                    if (f.existsAsFile())
                    {
                        // Note: setStateInformation is often called on message thread, but check logic.
                        // We will try to reload synchronously here as it's state restoration.
                        // Ideally we check sample rate, but state restore usually assumes valid setup.
                        
                        // NOTE: In a real plugin, async loading is better, but for now we do direct.
                        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(f));
                        if (reader)
                        {
                            p.sampleRate = reader->sampleRate;
                            // LOAD INTO SOURCE BUFFER to support channel switching later
                            p.sourceBuffer = std::make_shared<juce::AudioBuffer<float>>((int)reader->numChannels, (int)reader->lengthInSamples);
                            p.sourceChannels = p.sourceBuffer->getNumChannels(); // SET CHEANNEL COUNT
                            reader->read(p.sourceBuffer.get(), 0, (int)reader->lengthInSamples, 0, true, true);
                            
                            // Load V2 Data
                            p.normGain = (float)pXml->getDoubleAttribute("normGain", 1.0);
                            p.onsetOffset = pXml->getIntAttribute("onsetOffset", 0);
                            
                            // Re-Apply specific processing (Trim & Gain)
                            if (p.sourceBuffer)
                            {
                                // 1. Trim (Shift)
                                int onset = p.onsetOffset;
                                int numSamps = p.sourceBuffer->getNumSamples();
                                int numCh = p.sourceBuffer->getNumChannels();
                                
                                if (onset > 0 && onset < numSamps)
                                {
                                    int newNumSamps = numSamps - onset;
                                    for (int c = 0; c < numCh; ++c)
                                    {
                                        auto* dest = p.sourceBuffer->getWritePointer(c);
                                        std::memmove(dest, dest + onset, (size_t)newNumSamps * sizeof(float));
                                    }
                                    p.sourceBuffer->setSize(numCh, newNumSamps, true);
                                    
                                    // Fade In (5ms)
                                    int fadeLen = (int)(0.005 * p.sampleRate);
                                    if (fadeLen > 0 && fadeLen < newNumSamps)
                                        p.sourceBuffer->applyGainRamp(0, 0, fadeLen, 0.0f, 1.0f);
                                }
                                
                                // 2. Apply Gain
                                p.sourceBuffer->applyGain(p.normGain);
                            }
                            // Initialize Convolver
                            p.convolver = std::make_shared<juce::dsp::Convolution>();
                            updateConvolver(p);
                        }
                    }
                }
                points.push_back(p);
            }
        }
        
        // Restore Walls
        walls.clear();
        if (auto* wallsXml = xmlState->getChildByName("WALLS"))
        {
            for (auto* wXml : wallsXml->getChildIterator())
            {
                OcclusionWall w;
                w.id = juce::Uuid(wXml->getStringAttribute("id"));
                w.name = wXml->getStringAttribute("name");
                w.x1 = (float)wXml->getDoubleAttribute("x1");
                w.y1 = (float)wXml->getDoubleAttribute("y1");
                w.x2 = (float)wXml->getDoubleAttribute("x2");
                w.y2 = (float)wXml->getDoubleAttribute("y2");
                w.locked = wXml->getBoolAttribute("locked");
                w.attenuation = (float)wXml->getDoubleAttribute("attenuation", 0.5);
                
                walls.push_back(w);
            }
        }
        
        updateWeightsGaussian();
        if (onStateChanged) onStateChanged();
    }
}

// Note: Changed signature in header to addIRPoint(const juce::String& name)
void IrisVSTV3AudioProcessor::addIRPoint(const juce::String& name)
{
    juce::ScopedLock sl(stateLock);
    
    // This method creates a "Virtual" IR point (no file) for testing/legacy
    juce::Random r;
    IRPoint p;
    p.id = juce::Uuid();
    
    if (name.isNotEmpty()) p.name = name;
    else p.name = "IR " + juce::String(points.size());
    
    // Blue gradient: hue around 0.5-0.7 (cyan to blue/purple)
    // Saturation high, Brightness high
    float hue = r.nextFloat() * 0.15f + 0.55f; 
    p.color = juce::Colour::fromHSV(hue, 0.8f, 0.9f, 1.0f);
    
    p.x = r.nextFloat();
    p.y = r.nextFloat();
    p.isListener = false;
    
    points.push_back(p);
    
    // Note: Calling updateWeightsGaussian inside lock is fine because stateLock is recursive? 
    // Wait, updateWeightsGaussian is NOT locking inside itself in the original code? 
    // I should check updateWeightsGaussian structure.
    // If updateWeightsGaussian also locks, it might be an issue if lock is not recursive.
    // CriticalSection IS recursive in JUCE.
    updateWeightsGaussian();
    
    if (onStateChanged) onStateChanged();
}

juce::Uuid IrisVSTV3AudioProcessor::addIRFromFile(const juce::File& file)
{
    // Basic checks
    if (!file.existsAsFile()) return juce::Uuid::null();

    // Ensure formats
    if (formatManager.getNumKnownFormats() == 0) formatManager.registerBasicFormats();
    
    // Create Reader
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader) return juce::Uuid::null();

    // Load Data
    int numSamples = (int)reader->lengthInSamples;
    int numChannels = (int)reader->numChannels;
    
    // Safety
    if (numSamples <= 0 || numChannels <= 0) return juce::Uuid::null();
    
    // Read
    auto wholeBuffer = std::make_shared<juce::AudioBuffer<float>>(numChannels, numSamples);
    reader->read(wholeBuffer.get(), 0, numSamples, 0, true, true);
    
    // --- V2 Processing: Onset Alignment & Normalization ---
    float storedGain = 1.0f;
    int storedOnset = 0;
    
    {
        // 1. Onset Alignment (Threshold 10% of Peak)
        float globalPeak = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            globalPeak = juce::jmax(globalPeak, wholeBuffer->getMagnitude(c, 0, numSamples));
            
        float threshold = globalPeak * 0.1f;
        int onset = -1;
        
        // Find first sample crossing threshold
        for (int i = 0; i < numSamples; ++i) {
            for (int c = 0; c < numChannels; ++c) {
                if (std::abs(wholeBuffer->getSample(c, i)) > threshold) {
                    onset = i;
                    break;
                }
            }
            if (onset != -1) break;
        }
        
        if (onset == -1) onset = 0;
        storedOnset = onset;
        
        // Trim
        if (onset > 0)
        {
            int newNumSamps = numSamples - onset;
            for (int c = 0; c < numChannels; ++c) {
                auto* dest = wholeBuffer->getWritePointer(c);
                std::memmove(dest, dest + onset, (size_t)newNumSamps * sizeof(float));
            }
            wholeBuffer->setSize(numChannels, newNumSamps, true);
            
            // Fade In (5ms)
            int fadeLen = (int)(0.005 * reader->sampleRate);
            if (fadeLen > 0 && fadeLen < newNumSamps)
                wholeBuffer->applyGainRamp(0, 0, fadeLen, 0.0f, 1.0f);
        }
        
        // 2. Normalization
        // RMS Normalization to Target 0.1
        float rms = 0.0f;
        int currentLen = wholeBuffer->getNumSamples();
        for (int c = 0; c < numChannels; ++c)
           rms += wholeBuffer->getRMSLevel(c, 0, currentLen);
        rms /= (float)numChannels;
        
        float targetRMS = 0.1f; // Target
        if (rms > 0.00001f) {
            storedGain = targetRMS / rms;
            wholeBuffer->applyGain(storedGain);
        }
    }
    // ----------------------------------------------------
    
    // Create Point
    IRPoint p;
    p.normGain = storedGain;
    p.onsetOffset = storedOnset;
    p.id = juce::Uuid();
    p.name = file.getFileName();
    p.sourceFile = file;
    p.sampleRate = reader->sampleRate;
    p.sourceBuffer = wholeBuffer;
    p.sourceChannels = numChannels;
    p.selectedChannel = 0; // Default
    
    // Default Pos
    int n = (int)points.size();
    float row = std::floor(n / 3.0f);
    float col = n % 3;
    p.x = 0.2f + col * 0.2f;
    p.y = 0.2f + row * 0.2f;
    
    // Color
    juce::Random r(p.id.toString().hashCode());
    float hue = r.nextFloat() * 0.15f + 0.55f; 
    p.color = juce::Colour::fromHSV(hue, 0.8f, 0.9f, 1.0f);
    
    // Create Convolver
    p.convolver = std::make_shared<juce::dsp::Convolution>();
    
    // Prepare Audio Data for Convolver (Select Ch 0)
    updateConvolver(p);
    
    points.push_back(p);
    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();
    
    return p.id;
}

void IrisVSTV3AudioProcessor::updateConvolver(IRPoint& p)
{
    if (!p.sourceBuffer || p.sourceChannels == 0) return;
    
    // MONO TARGET
    int len = p.sourceBuffer->getNumSamples();
    auto mono = std::make_shared<juce::AudioBuffer<float>>(1, len);
    
    // Select Channel
    int ch = p.selectedChannel;
    ch = juce::jlimit(0, p.sourceChannels - 1, ch);

    // Copy selected channel to mono buffer
    mono->copyFrom(0, 0, *p.sourceBuffer, ch, 0, len);
    
    p.audioData = mono; // Store it
    
    juce::AudioBuffer<float> copy;
    copy.makeCopyOf(*mono); 
    
    if (processSpec.sampleRate > 0) p.convolver->prepare(processSpec);
    
    p.convolver->loadImpulseResponse(std::move(copy),
                                     p.sampleRate,
                                     juce::dsp::Convolution::Stereo::no, // MONO
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::yes);
}

void IrisVSTV3AudioProcessor::reloadIRChannel(juce::Uuid id, int channel)
{
    for (auto& p : points)
    {
        if (p.id == id)
        {
            p.selectedChannel = channel;
            updateConvolver(p);
            return;
        }
    }
}


void IrisVSTV3AudioProcessor::setPointLocked(juce::Uuid id, bool locked)
{
    juce::ScopedLock sl(stateLock);
    for (auto& p : points)
    {
        if (p.id == id)
        {
            p.locked = locked;
            break;
        }
    }
    if (onStateChanged) onStateChanged();
}

void IrisVSTV3AudioProcessor::updatePointPosition(juce::Uuid id, float x, float y)
{
    juce::ScopedLock sl(stateLock);
    for (auto& p : points)
    {
        if (p.id == id)
        {
            if (!p.locked) // Safety check, though UI should prevent this too
            {
                p.x = juce::jlimit(0.0f, 1.0f, x);
                p.y = juce::jlimit(0.0f, 1.0f, y);
                
                if (p.isListener) {
                    currentListenerX = p.x;
                    currentListenerY = p.y;
                }
            }
            break;
        }
    }
    // Optimization: might not need full redraw for every move, but for now yes.
    // updateWeightsGaussian(); // REMOVED to avoid stutter during drag. Timer handles it.
    // onStateChanged called in timerCallback
}

void IrisVSTV3AudioProcessor::removePoint(juce::Uuid id)
{
    juce::ScopedLock sl(stateLock);
    for (auto it = points.begin(); it != points.end(); ++it)
    {
        if (it->id == id && !it->isListener) // Cannot delete listener
        {
            it->audioData.reset(); // Clear memory
            points.erase(it);
            // Clean up weights maps
            targetWeights.erase(id);
            smoothedWeights.erase(id);
            activeIDs.erase(id);
            break;
        }
    }
    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();
}

juce::Uuid IrisVSTV3AudioProcessor::addWall(float x1, float y1, float x2, float y2)
{
    juce::ScopedLock sl(stateLock);
    OcclusionWall w;
    w.id = juce::Uuid();
    w.name = "Wall " + juce::String(walls.size() + 1);
    w.x1 = juce::jlimit(0.0f, 1.0f, x1);
    w.y1 = juce::jlimit(0.0f, 1.0f, y1);
    w.x2 = juce::jlimit(0.0f, 1.0f, x2);
    w.y2 = juce::jlimit(0.0f, 1.0f, y2);
    w.attenuation = 0.05f; // Epsilon
    w.color = juce::Colours::cyan;
    walls.push_back(w);
    
    updateWeightsGaussian(); // Recalculate weights (occlusion might change)
    if (onStateChanged) onStateChanged();
    return w.id;
}

void IrisVSTV3AudioProcessor::removeWall(juce::Uuid id)
{
    juce::ScopedLock sl(stateLock);
    for (auto it = walls.begin(); it != walls.end(); ++it)
    {
        if (it->id == id)
        {
            walls.erase(it);
            break;
        }
    }
    updateWeightsGaussian();
    if (onStateChanged) onStateChanged();
}

void IrisVSTV3AudioProcessor::updateWall(juce::Uuid id, float x1, float y1, float x2, float y2)
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
            }
            break;
        }
    }
    updateWeightsGaussian();
    if (onStateChanged) onStateChanged(); // Explicit update
}

// Geometry Helper: Segment-Segment Intersection Point
// Returns true if intersection found, outputting point to (ix, iy)
static bool getIntersectionPoint(float ax, float ay, float bx, float by, 
                                 float cx, float cy, float dx, float dy,
                                 float& ix, float& iy)
{
    float d = (dy - cy) * (bx - ax) - (dx - cx) * (by - ay);
    if (d == 0) return false; // Parallel

    float ua = ((dx - cx) * (ay - cy) - (dy - cy) * (ax - cx)) / d;
    float ub = ((bx - ax) * (ay - cy) - (by - ay) * (ax - cx)) / d;

    if (ua >= 0.0f && ua <= 1.0f && ub >= 0.0f && ub <= 1.0f) {
        ix = ax + ua * (bx - ax);
        iy = ay + ua * (by - ay);
        return true;
    }
    return false;
}

// Helper: Distance squared
static float distSq(float x1, float y1, float x2, float y2) {
    return (x1-x2)*(x1-x2) + (y1-y2)*(y1-y2);
}

// Helper: Closest point on segment
static void getClosestPointOnSegment(float px, float py, float x1, float y1, float x2, float y2, float& outX, float& outY)
{
    float l2 = distSq(x1, y1, x2, y2);
    if (l2 == 0) { outX = x1; outY = y1; return; }
    
    float t = ((px - x1) * (x2 - x1) + (py - y1) * (y2 - y1)) / l2;
    t = juce::jlimit(0.0f, 1.0f, t);
    
    outX = x1 + t * (x2 - x1);
    outY = y1 + t * (y2 - y1);
}

void IrisVSTV3AudioProcessor::constrainPointToWalls(float& x, float& y)
{
    juce::ScopedLock sl(stateLock);
    const float minClearance = 0.02f; // Normalized units
    
    for (const auto& w : walls)
    {
        float cx, cy;
        getClosestPointOnSegment(x, y, w.x1, w.y1, w.x2, w.y2, cx, cy);
        
        float d2 = distSq(x, y, cx, cy);
        
        if (d2 < minClearance * minClearance)
        {
            // Too close! Push away.
            // Vector from wall point to current point
            float dx = x - cx;
            float dy = y - cy;
            float len = std::sqrt(d2);
            
            if (len == 0) {
                // Exact overlap (rare), push perp
                dx = -(w.y2 - w.y1);
                dy = (w.x2 - w.x1);
                len = std::sqrt(dx*dx + dy*dy);
            }
            
            if (len > 0) {
                // Normalize and scale to clearance
                float scale = minClearance / len;
                x = cx + dx * scale;
                y = cy + dy * scale;
            }
        }
    }
    
    // Also clamp to 0-1 bounds (global room walls)
    x = juce::jlimit(0.0f, 1.0f, x);
    y = juce::jlimit(0.0f, 1.0f, y);
}

void IrisVSTV3AudioProcessor::updateWeightsGaussian()
{
    // Find Listener
    IRPoint* listener = nullptr;
    for (auto& p : points) {
        if (p.isListener) {
            listener = &p;
            break;
        }
    }
    
    if (!listener || points.size() <= 1) {
        targetWeights.clear();
        activeIDs.clear();
        return;
    }

    // 1. Calculate Raw Gaussian Weights for all points
    std::vector<std::pair<float, IRPoint*>> rawWeights;
    float maxWeight = 0.0f;
    
    float spread = spreadParam->load();
    float sigmaVal = 0.05f + 1.5f * spread * spread;
    
    for (auto& p : points) {
        if (p.isListener) continue;

        float dx = p.x - currentListenerX;
        float dy = p.y - currentListenerY;
        float d2 = dx*dx + dy*dy;
        
        // Gaussian: w = exp(-d^2 / (2 * sigma^2))
        float w = std::exp(-d2 / (2.0f * sigmaVal * sigmaVal));
        
        // --- OCCLUSION CHECK ---
        // Soft Occlusion: Center-weighted
        float occlusionFactor = 1.0f;
        
        for (const auto& wStruct : walls)
        {
             float ix, iy;
             if (getIntersectionPoint(currentListenerX, currentListenerY, p.x, p.y, 
                                      wStruct.x1, wStruct.y1, wStruct.x2, wStruct.y2, ix, iy))
             {
                 // Calculate Wall Center and Length
                 float cx = (wStruct.x1 + wStruct.x2) * 0.5f;
                 float cy = (wStruct.y1 + wStruct.y2) * 0.5f;
                 float wallLen = std::sqrt(distSq(wStruct.x1, wStruct.y1, wStruct.x2, wStruct.y2));
                 
                 // Distance from Intersection to Center
                 float distToCenter = std::sqrt(distSq(ix, iy, cx, cy));
                 
                 // Normalized distance t (0 at center, 1 at endpoints)
                 float t = 0.0f;
                 if (wallLen > 0) t = distToCenter / (wallLen * 0.5f);
                 t = juce::jlimit(0.0f, 1.0f, t);
                 
                 // Exponential Curve
                 // o = pow(epsilon, pow(1.0 - t, gamma))
                 // constants: epsilon=0.05, gamma=2.0
                 float epsilon = 0.05f;
                 float gamma = 4.0f;
                 
                 float o = std::pow(epsilon, std::pow(1.0f - t, gamma));
                 
                 occlusionFactor *= o;
             }
        }
        
        w *= occlusionFactor;
        // -----------------------
        
        rawWeights.push_back({w, &p});
        if (w > maxWeight) maxWeight = w;
    }

    // 2. Select Active Set
    // Rule A: Always keep top Kmin (4)
    // Rule B: Include others if w >= tau * max (up to Kmax = 8)
    
    // Sort descending by weight
    std::sort(rawWeights.begin(), rawWeights.end(), 
              [](const auto& a, const auto& b){ return a.first > b.first; });

    int Kmin = 4;
    int Kmax = 8;
    
    std::set<juce::Uuid> nextActiveIDs;

    // A. Add top Kmin
    for (int i=0; i < std::min((int)rawWeights.size(), Kmin); ++i) {
        nextActiveIDs.insert(rawWeights[(size_t)i].second->id);
    }
    
    // B. Hysteresis
    for (int i = Kmin; i < std::min((int)rawWeights.size(), Kmax); ++i) {
        float w = rawWeights[(size_t)i].first;
        juce::Uuid id = rawWeights[(size_t)i].second->id;
        
        bool wasActive = activeIDs.find(id) != activeIDs.end();
        
        if (wasActive) {
            if (w >= tauOut * maxWeight) nextActiveIDs.insert(id);
        } else {
            if (w >= tauIn * maxWeight) nextActiveIDs.insert(id);
        }
    }
    
    activeIDs = nextActiveIDs;

// 3. Set Targets for Active Set (RE-CALCULATE WITH OCCLUSION)
// Note: We already calculated w with occlusion above, but didn't store it mapped by ID.
// Re-running loop or storing map above is better.
// Let's re-run loop for clarity or store in map first.
// Storing in map is O(N) which is fine.

    // Fill map from rawWeights list?
    // rawWeights has ALL points.
    targetWeights.clear();
    for (auto& pair : rawWeights) {
        float w = pair.first;
        juce::Uuid id = pair.second->id;
        
        if (activeIDs.count(id)) {
            targetWeights[id] = w;
        } else {
            targetWeights[id] = 0.0f;
        }
    }
}

void IrisVSTV3AudioProcessor::timerCallback()
{
    // 0. Update Physics / Inertia
    // Get target from actual listener point
    IRPoint* listener = nullptr;
    for (auto& p : points) { if (p.isListener) { listener = &p; break; } }
    
    if (listener)
    {
        if (freezeParam->load()) 
        {
            // Frozen: Do not move.
            // currentListenerX/Y stay as is.
        }
        else
        {
            float targetX = listener->x;
            float targetY = listener->y;
            
            float inertia = inertiaParam->load();
            // Map inertia 0..1 to smooth coeff.
            // 0 -> 1.0 (instant)
            // 1 -> 0.05 approx (slow)
            // Simple map: coeff = 1.0 - inertia * 0.98 ?
            // dt = 1/60 = 0.016
            // tau: 0 to 1s.
            // alpha = 1 - exp(-dt/tau).
            // Let's use simple linear for alpha directly?
            // alpha = 0.01 (slow) to 1.0 (fast).
            // map: alpha = 1.0 - 0.99 * inertia
            float alpha = 1.0f - 0.98f * inertia;
            
            if (std::abs(targetX - currentListenerX) > 0.0001f)
                currentListenerX += (targetX - currentListenerX) * alpha;
            if (std::abs(targetY - currentListenerY) > 0.0001f)
                currentListenerY += (targetY - currentListenerY) * alpha;
        }
    }

    // 1. Recalculate Weights for current position
    updateWeightsGaussian();

    // 2. Smoothing Logic
    
    // Smoothing (One-pole approx 50ms)
    const float alpha = 0.25f;
    bool changed = false;

    // Update Smoothed Weights & Prepare Active List for Audio
    std::shared_ptr<RenderState> nextState = std::make_shared<RenderState>();
    
    // Sum for normalization
    float sumForNorm = 0.0f;

    // First pass: update smoothed and sum active
    for (auto& p : points) {
        if (p.isListener) continue;
        
        float target = 0.0f;
        if (targetWeights.count(p.id)) target = targetWeights[p.id];
        
        // Init if missing
        if (smoothedWeights.find(p.id) == smoothedWeights.end()) smoothedWeights[p.id] = 0.0f;

        float& current = smoothedWeights[p.id];
        if (std::abs(target - current) > 0.0001f) {
            current += (target - current) * alpha;
            if (current < 0.001f && target == 0.0f) current = 0.0f; // Snap to zero
            changed = true;
        }
        
        if (current > 0.0f) sumForNorm += current;
    }
    
    // 3. Build Safe Render List (Normalized)
    currentNearestNeighbors.clear();
    
    for (auto& p : points) {
         if (p.isListener) continue;
         float w = smoothedWeights[p.id];
         
         if (w > 0.0001f) {
             float normW = (sumForNorm > 0.001f) ? (w / sumForNorm) : 0.0f;
             
             // Check if we have a convolver
             if (p.convolver)
             {
                 ActiveIR air;
                 air.convolver = p.convolver;
                 air.weight = normW;
                 air.id = p.id;
                 nextState->activeIRs.push_back(air);
             }
             
             // For OSC/UI
             currentNearestNeighbors.push_back(p);
         }
    }
    
    // Sort for OSC stability
    std::sort(currentNearestNeighbors.begin(), currentNearestNeighbors.end(), 
             [&](const IRPoint& a, const IRPoint& b){
                 return smoothedWeights[a.id] > smoothedWeights[b.id];
             });
             
    // Swap Render State
    std::atomic_store(&renderState, nextState);
    
    if (changed || freezeParam->load()) { // if frozen we might want to update osc just in case? Or no.
         // Send normalized weights
        if (!currentNearestNeighbors.empty()) {
            if (currentNearestNeighbors.size() > 0) *weight1 = smoothedWeights[currentNearestNeighbors[0].id] / sumForNorm; else *weight1 = 0;
             if (currentNearestNeighbors.size() > 1) *weight2 = smoothedWeights[currentNearestNeighbors[1].id] / sumForNorm; else *weight2 = 0;
             if (currentNearestNeighbors.size() > 2) *weight3 = smoothedWeights[currentNearestNeighbors[2].id] / sumForNorm; else *weight3 = 0;

            sendWeightsOSC();
        }
        if (onStateChanged) onStateChanged();
    }
}

void IrisVSTV3AudioProcessor::sendWeightsOSC()
{
    // Format: /iris/weights s:name f:w s:name f:w ...
    if (currentNearestNeighbors.empty()) return;
    
    juce::OSCMessage m("/iris/weights");
    
    float sum = 0.0f;
    for (auto& p : currentNearestNeighbors) sum += smoothedWeights[p.id];
    if (sum <= 0.0f) sum = 1.0f;

    for (auto& p : currentNearestNeighbors) {
        m.addString(p.name);
        m.addFloat32(smoothedWeights[p.id] / sum);
    }
    oscSender.send(m);
}

void IrisVSTV3AudioProcessor::oscMessageReceived (const juce::OSCMessage& message)
{
    if (message.getAddressPattern() == "/ir_list")
    {
        availableIRs.clear();
        for (const auto& arg : message) {
            if (arg.isString()) availableIRs.push_back(arg.getString());
        }
        
        // Notify Editor? 
        if (onStateChanged) juce::MessageManager::callAsync([this]{ onStateChanged(); });
    }
}

void IrisVSTV3AudioProcessor::setPointName(juce::Uuid id, const juce::String& name)
{
    for (auto& p : points)
    {
        if (p.id == id)
        {
            p.name = name;
            break;
        }
    }
    if (onStateChanged) onStateChanged();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IrisVSTV3AudioProcessor();
}

void IrisVSTV3AudioProcessor::loadLayoutFromJSON(const juce::File& file)
{
    if (!file.existsAsFile()) return;
    
    juce::var json = juce::JSON::parse(file);
    if (!json.isObject()) return;
    
    // Extent
    juce::var extent = json["extent"];
    float xmin = 0.0f, xmax = 1.0f, ymin = 0.0f, ymax = 1.0f;
    
    if (extent.isObject())
    {
        xmin = extent.getProperty("xmin", 0.0f);
        xmax = extent.getProperty("xmax", 100.0f);
        ymin = extent.getProperty("ymin", 0.0f);
        ymax = extent.getProperty("ymax", 100.0f);
    }
    
    // IRs
    juce::var irs = json["irs"];
    if (irs.isArray())
    {
        // CLEAR EXISTING IRs (Keep Listener)
        std::vector<juce::Uuid> toRemove;
        for (auto& p : points) {
            if (!p.isListener) toRemove.push_back(p.id);
        }
        for (auto id : toRemove) removePoint(id);
        
        for (int i = 0; i < irs.size(); ++i)
        {
            juce::var irObj = irs[i];
            juce::String path = irObj.getProperty("path", "");
            float wx = irObj.getProperty("x", 0.0f);
            float wy = irObj.getProperty("y", 0.0f);
            juce::String name = irObj.getProperty("name", "");
            
            // Resolve path (relative to JSON)
            juce::File irFile = file.getSiblingFile(path);
            if (!irFile.existsAsFile())
            {
                 // Try absolute
                 irFile = juce::File(path);
            }
            
            if (irFile.existsAsFile())
            {
                juce::Uuid id = addIRFromFile(irFile);
                if (id != juce::Uuid::null())
                {
                    // Map Coords
                    float rangeX = (xmax - xmin);
                    float rangeY = (ymax - ymin);
                    
                    float xn = 0.5f;
                    float yn = 0.5f;
                    
                    if (std::abs(rangeX) > 0.001f) xn = (wx - xmin) / rangeX;
                    if (std::abs(rangeY) > 0.001f) yn = (wy - ymin) / rangeY;
                    
                    xn = juce::jlimit(0.0f, 1.0f, xn);
                    yn = juce::jlimit(0.0f, 1.0f, yn);
                    
                    updatePointPosition(id, xn, yn);
                    if (name.isNotEmpty()) setPointName(id, name);
                }
            }
        }
    }
    
    // Notify
    if (onStateChanged) onStateChanged();
}
