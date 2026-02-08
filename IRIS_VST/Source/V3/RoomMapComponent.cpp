#include "RoomMapComponentV3.h"

RoomMapComponentV3::RoomMapComponentV3(IrisVSTV3AudioProcessor& p)
    : audioProcessor(p)
{
}

RoomMapComponentV3::~RoomMapComponentV3()
{
}

void RoomMapComponentV3::paint (juce::Graphics& g)
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

    // 2. Walls
    float wallOpacity = 0.8f;
    if (audioProcessor.wallOpacityParam) wallOpacity = *audioProcessor.wallOpacityParam;

    for (const auto& w : audioProcessor.walls)
    {
        float x1 = w.x1 * getWidth();
        float y1 = w.y1 * getHeight();
        float x2 = w.x2 * getWidth();
        float y2 = w.y2 * getHeight();
        
        g.setColour(w.color.withAlpha(wallOpacity));
        g.drawLine(x1, y1, x2, y2, 3.0f);
        
        // Endpoints
        g.fillEllipse(x1-3, y1-3, 6, 6);
        g.fillEllipse(x2-3, y2-3, 6, 6);
        
        // Center Handle
        float cx = (x1 + x2) * 0.5f;
        float cy = (y1 + y2) * 0.5f;
        g.setColour(w.color.brighter().withAlpha(wallOpacity));
        g.fillEllipse(cx-4, cy-4, 8, 8);
        
        if (w.locked)
        {
            g.setColour(juce::Colours::orange);
            g.drawRect(cx-5.0f, cy-5.0f, 10.0f, 10.0f, 1.0f);
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

void RoomMapComponentV3::resized()
{
}

void RoomMapComponentV3::mouseDown(const juce::MouseEvent& e)
{
    draggingId = juce::Uuid::null(); // Reset
    
// ...
    // Find clicked point - iterate backwards to catch top items first
    for (auto it = audioProcessor.points.rbegin(); it != audioProcessor.points.rend(); ++it)
    {
        float px = it->x * getWidth();
        float py = it->y * getHeight();
        float radius = it->isListener ? 12.0f : 8.0f; 

        if (e.getPosition().getDistanceFrom(juce::Point<int>((int)px, (int)py)) < radius)
        {
            if (!it->locked) 
            {
                draggingId = it->id;
                isDraggingWall = false;
                
                // Init Drag State
                dragStartMouseX = (float)e.x / getWidth();
                dragStartMouseY = (float)e.y / getHeight();
                dragStartObjX = it->x;
                dragStartObjY = it->y;
                
                lastMouseX = dragStartMouseX;
                lastMouseY = dragStartMouseY;
            }
            return; 
        }
    }
    
    // Check Walls
    for (const auto& w : audioProcessor.walls)
    {
        float x1 = w.x1 * getWidth();
        float y1 = w.y1 * getHeight();
        float x2 = w.x2 * getWidth();
        float y2 = w.y2 * getHeight();
        
        // Check distance to center primarily for "dragging"
        float cx = (x1 + x2) * 0.5f;
        float cy = (y1 + y2) * 0.5f;
        
        if (e.getPosition().getDistanceFrom(juce::Point<int>((int)cx, (int)cy)) < 10.0f)
        {
            // Select Wall
            audioProcessor.selectedWallId = w.id;
            
            if (!w.locked)
            {
                draggingId = w.id;
                isDraggingWall = true;
                
                // Init Drag State
                dragStartMouseX = (float)e.x / getWidth();
                dragStartMouseY = (float)e.y / getHeight();
                
                dragStartWall[0] = w.x1;
                dragStartWall[1] = w.y1;
                dragStartWall[2] = w.x2;
                dragStartWall[3] = w.y2;
                
                lastMouseX = dragStartMouseX;
                lastMouseY = dragStartMouseY;
            }
            if (audioProcessor.onStateChanged) audioProcessor.onStateChanged(); 
            return;
        }
    }
    
    // Deselect if background clicked
    if (audioProcessor.selectedWallId != juce::Uuid::null()) {
        audioProcessor.selectedWallId = juce::Uuid::null();
        if (audioProcessor.onStateChanged) audioProcessor.onStateChanged();
    }
}

void RoomMapComponentV3::mouseDrag(const juce::MouseEvent& e)
{
    if (!draggingId.isNull())
    {
        // Current Mouse (Normalized)
        float nx = (float)e.x / getWidth();
        float ny = (float)e.y / getHeight();
        
        // Delta from Start (Normalized)
        float dx = nx - dragStartMouseX;
        float dy = ny - dragStartMouseY;
        
        if (isDraggingWall)
        {
            // Update Wall Relative to Start
            float w_x1 = juce::jlimit(0.0f, 1.0f, dragStartWall[0] + dx);
            float w_y1 = juce::jlimit(0.0f, 1.0f, dragStartWall[1] + dy);
            float w_x2 = juce::jlimit(0.0f, 1.0f, dragStartWall[2] + dx);
            float w_y2 = juce::jlimit(0.0f, 1.0f, dragStartWall[3] + dy);
            
            audioProcessor.updateWall(draggingId, w_x1, w_y1, w_x2, w_y2);
        }
        else
        {
            // Update Point Relative to Start
            float tx = dragStartObjX + dx; 
            float ty = dragStartObjY + dy;
            
            // Constrain
            audioProcessor.constrainPointToWalls(tx, ty);
            
            // Apply
            audioProcessor.updatePointPosition(draggingId, tx, ty);
        }
        
        lastMouseX = nx;
        lastMouseY = ny;
    }
}

void RoomMapComponentV3::mouseUp(const juce::MouseEvent&)
{
    draggingId = juce::Uuid::null();
}

bool RoomMapComponentV3::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".aif"))
            return true;
    }
    return false;
}

void RoomMapComponentV3::filesDropped(const juce::StringArray& files, int x, int y)
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
