#include "RoomMapComponentV2.h"

RoomMapComponent::RoomMapComponent(IrisVSTV2AudioProcessor& p)
    : audioProcessor(p)
{
}

RoomMapComponent::~RoomMapComponent()
{
}

void RoomMapComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black); 

    // 1. Grid
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    for (int i=1; i<10; ++i) {
        float pos = i / 10.0f;
        g.drawLine(pos*getWidth(), 0, pos*getWidth(), (float)getHeight());
        g.drawLine(0, pos*getHeight(), (float)getWidth(), pos*getHeight());
    }

    g.setColour (juce::Colours::white);
    g.drawRect (getLocalBounds(), 1);   // Border

    // Draw interpolation usage (Glows)
    
    // Find Listener first
    const IRPoint* listener = nullptr;
    for (const auto& p : audioProcessor.points) {
        if (p.isListener) {
            listener = &p;
            break;
        }
    }

    if (listener)
    {
        float w = (float)getWidth();
        float h = (float)getHeight();
        
        // Draw active IRs ambient glow
        for (auto& p : audioProcessor.points) {
            if (p.isListener) continue;
            
            if (audioProcessor.smoothedWeights.count(p.id)) {
                float weight = audioProcessor.smoothedWeights[p.id];
                 
                 if (weight > 0.01f) {
                     float px = p.x * w;
                     float py = p.y * h;
                     
                     // Large ambient glow
                     g.setColour(p.color.withAlpha(weight * 0.4f)); 
                     float radius = 40.0f * weight + 20.0f;
                     g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);
                 }
            }
        }
    }

    // 3. Points
    for (const auto& point : audioProcessor.points)
    {
        float px = point.x * getWidth();
        float py = point.y * getHeight();
        float radius = point.isListener ? 12.0f : 8.0f;

        g.setColour(juce::Colours::white);

        if (point.isListener)
        {
            // Use SMOOTHED position for the main glow/indicator
            float lx = audioProcessor.currentListenerX * getWidth();
            float ly = audioProcessor.currentListenerY * getHeight();
            
            // Draw Target (Mouse position) if different
            float dist = std::abs(px - lx) + std::abs(py - ly);
            if (dist > 1.0f) // If noticeable difference
            {
                // Draw Ghost Target
                g.setColour(juce::Colours::white.withAlpha(0.3f));
                g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, 1.0f);
            }
            
            // Update center for main drawing to be the SMOOTHED position
            px = lx; 
            py = ly;

            // Glowing effect for listener (at smoothed pos)
            g.setColour(juce::Colours::red.withAlpha(0.5f));
            g.fillEllipse(px - radius*1.5f, py - radius*1.5f, radius*3.0f, radius*3.0f);
            
            g.setColour(juce::Colours::red);
            g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);
            
            // Halo ring
            g.setColour(juce::Colours::white);
            g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, 2.0f);
        }
        else
        {
            // Logic for IRs: Brightness based on weight
            float alpha = 0.3f; // Base dimness
            float strokeW = 1.0f;
            
            if (audioProcessor.smoothedWeights.count(point.id)) {
                float w = audioProcessor.smoothedWeights[point.id];
                if (w > 0.001f) {
                     alpha = 0.4f + 0.6f * w; // Scale brightness
                     strokeW = 1.0f + 4.0f * w; // Thicker ring
                }
            }

            juce::Colour c = point.color;
            // Modern gradient: bright center to base color
            juce::ColourGradient cg(c.brighter(0.8f).withAlpha(alpha), px - radius*0.3f, py - radius*0.3f, 
                                    c.withAlpha(alpha), px+radius, py+radius, true);
            g.setGradientFill(cg);
            g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);

            // Ring
            g.setColour(c.brighter(1.0f).withAlpha(alpha));
            g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, strokeW);
            
            // Lock Indicator
            if (point.locked)
            {
                g.setColour(juce::Colours::orange);
                // Draw a small 'L' or padlock representation
                // Simple: small circle/dot top right
                g.fillEllipse(px + radius*0.5f, py - radius*1.5f, 6, 6);
            }
            
            // Text
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.drawText(point.name, px + 15, py - 10, 100, 20, juce::Justification::left);
        }
    }
}

void RoomMapComponent::resized()
{
}

void RoomMapComponent::mouseDown(const juce::MouseEvent& e)
{
    draggingId = juce::Uuid::null(); // Reset
    
    // Find clicked point - iterate backwards to catch top items first
    for (auto it = audioProcessor.points.rbegin(); it != audioProcessor.points.rend(); ++it)
    {
        float px = it->x * getWidth();
        float py = it->y * getHeight();
        float radius = it->isListener ? 12.0f : 8.0f; 

        // Simple distance check
        if (e.getPosition().getDistanceFrom(juce::Point<int>((int)px, (int)py)) < radius)
        {
            if (!it->locked) // Only drag if not locked
            {
                draggingId = it->id;
            }
            // If locked, we swallow the click but don't set draggingId, 
            // effectively preventing drag but perhaps acknowledging selection if we had it.
            break;
        }
    }
}

void RoomMapComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (!draggingId.isNull())
    {
        float nx = juce::jlimit(0.0f, 1.0f, (float)e.x / getWidth());
        float ny = juce::jlimit(0.0f, 1.0f, (float)e.y / getHeight());
        
        audioProcessor.updatePointPosition(draggingId, nx, ny);
    }
}

void RoomMapComponent::mouseUp(const juce::MouseEvent&)
{
    draggingId = juce::Uuid::null();
}

bool RoomMapComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".aif"))
            return true;
    }
    return false;
}

void RoomMapComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    float nx = juce::jlimit(0.0f, 1.0f, (float)x / getWidth());
    float ny = juce::jlimit(0.0f, 1.0f, (float)y / getHeight());
    
    for (const auto& f : files)
    {
        juce::File file(f);
        if (file.existsAsFile() && (file.hasFileExtension("wav") || file.hasFileExtension("aiff") || file.hasFileExtension("aif")))
        {
            juce::Uuid newId = audioProcessor.addIRFromFile(file);
            
            if (newId != juce::Uuid::null())
            {
                audioProcessor.updatePointPosition(newId, nx, ny);
                
                // Slight offset for multiple files so they don't stack 100%
                nx += 0.02f;
                ny += 0.02f;
            }
        }
    }
}
