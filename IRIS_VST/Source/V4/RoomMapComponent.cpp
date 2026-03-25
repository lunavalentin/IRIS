#include "RoomMapComponentV4.h"
#include "Theme.h"

RoomMapComponentV4::RoomMapComponentV4(IrisVSTV4AudioProcessor& p)
    : audioProcessor(p)
{
}

RoomMapComponentV4::~RoomMapComponentV4()
{
}

void RoomMapComponentV4::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panelBackground); 

    // 1. Grid
    g.setColour(Theme::gridLineColor);
    for (int i=1; i<10; ++i) {
        float pos = i / 10.0f;
        g.drawLine(pos*getWidth(), 0, pos*getWidth(), (float)getHeight());
        g.drawLine(0, pos*getHeight(), (float)getWidth(), pos*getHeight());
    }

    g.setColour (Theme::borderMinimal);
    g.drawRect (getLocalBounds(), 1);   // Border

    // Draw interpolation usage (Glows)
    
    float mapW = (float)getWidth();
    float mapH = (float)getHeight();
    
    // Draw active IRs ambient glow (reduced and flat)
    for (auto& p : audioProcessor.points) {
        if (audioProcessor.smoothedWeights.count(p.id)) {
            float weight = audioProcessor.smoothedWeights[p.id];
             
             if (weight > 0.01f) {
                 float px = p.x * mapW;
                 float py = p.y * mapH;
                 
                 // Smaller ambient glow
                 g.setColour(p.color.withAlpha(weight * 0.15f)); 
                 float radius = 20.0f * weight + 15.0f;
                 g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);
             }
        }
    }

    // 2. Walls
    float wallOpacity = 0.8f;
    if (audioProcessor.wallOpacityParam) wallOpacity = *audioProcessor.wallOpacityParam;

    for (const auto& w_wall : audioProcessor.walls)
    {
        float x1 = w_wall.x1 * getWidth();
        float y1 = w_wall.y1 * getHeight();
        float x2 = w_wall.x2 * getWidth();
        float y2 = w_wall.y2 * getHeight();
        
        g.setColour(Theme::wallLineColor.withAlpha(wallOpacity));
        g.drawLine(x1, y1, x2, y2, 2.0f);
        
        // Center Handle (Move)
        float cx = (x1 + x2) * 0.5f;
        float cy = (y1 + y2) * 0.5f;
        g.setColour(Theme::wallLineColor.brighter().withAlpha(wallOpacity));
        g.fillEllipse(cx-3, cy-3, 6, 6);
        
        // Endpoints
        // Left (Size)
        g.setColour(Theme::accentCyan.withAlpha(wallOpacity));
        g.fillEllipse(x1-3, y1-3, 6, 6);
        
        // Right (Rotate)
        g.setColour(Theme::textMuted.withAlpha(wallOpacity));
        g.fillEllipse(x2-3, y2-3, 6, 6);
        
        if (w_wall.locked)
        {
            g.setColour(Theme::accentCyan);
            g.drawRect(cx-4.0f, cy-4.0f, 8.0f, 8.0f, 1.0f);
        }
    }
    // 3. Listeners
    const auto& local = audioProcessor.localAudioListener;
    
    // Remote Listeners
    for (const auto& pair : audioProcessor.remoteListeners)
    {
        const auto& rl = pair.second;
        // Drawing outline at target position
        float outlinePx = rl.x * getWidth();
        float outlinePy = rl.y * getHeight();
        
        // Drawing point at current smoothed position
        float px = rl.currentX * getWidth();
        float py = rl.currentY * getHeight();
        float radius = 10.0f;
        
        // Target outline
        float dist = std::abs(px - outlinePx) + std::abs(py - outlinePy);
        if (dist > 1.0f) {
            g.setColour(Theme::borderMinimal);
            g.drawEllipse(outlinePx - radius, outlinePy - radius, radius * 2, radius * 2, 1.0f);
        }
        
        if (audioProcessor.selectedListenerId == rl.id) {
            g.setColour(Theme::listenerRemotePink);
        } else {
            g.setColour(Theme::listenerInactiveGrey);
        }
        g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);
        
        g.setColour(Theme::backgroundDark);
        g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, 2.0f);
        
        g.setColour(Theme::textPrimary);
        g.setFont(Theme::getBaseFont(10.0f));
        g.drawText(rl.name, px - radius, py - radius, radius * 2, radius * 2, juce::Justification::centred);
    }
    
    // Local Listener
    {
        float px = local.x * getWidth();
        float py = local.y * getHeight();
        float lx = local.currentX * getWidth();
        float ly = local.currentY * getHeight();
        float radius = 10.0f;
        
        float dist = std::abs(px - lx) + std::abs(py - ly);
        if (dist > 1.0f) {
            g.setColour(Theme::borderMinimal);
            g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, 1.0f);
        }
        
        px = lx; py = ly;
        g.setColour(Theme::listenerLocalRed.withAlpha(0.2f));
        g.fillEllipse(px - radius*1.5f, py - radius*1.5f, radius*3.0f, radius*3.0f);
        
        g.setColour(Theme::listenerLocalRed);
        g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);
        
        g.setColour(Theme::textPrimary);
        g.setFont(Theme::getBaseFont(10.0f));
        g.drawText(local.name, px - radius, py - radius, radius * 2, radius * 2, juce::Justification::centred);
        
        g.setColour(Theme::backgroundDark);
        g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, 2.0f);
    }
    
    // 4. Points (IRs)
    for (const auto& point : audioProcessor.points)
    {
        float px = point.x * getWidth();
        float py = point.y * getHeight();
        float radius = 6.0f;

        float alpha = 0.5f;
        float strokeW = 1.0f;
        
        if (audioProcessor.smoothedWeights.count(point.id)) {
            float pointWeight = audioProcessor.smoothedWeights[point.id];
            if (pointWeight > 0.001f) {
                 alpha = 0.5f + 0.5f * pointWeight;
                 strokeW = 1.0f + 2.0f * pointWeight;
            }
        }

        juce::Colour c = point.color;
        g.setColour(c.withAlpha(alpha));
        g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);

        g.setColour(c.brighter(0.5f).withAlpha(alpha));
        g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, strokeW);
        
        if (point.locked)
        {
            g.setColour(Theme::accentCyan);
            g.fillEllipse(px + radius*0.5f, py - radius*1.5f, 4, 4);
        }
        
        g.setColour(Theme::textSecondary.withAlpha(alpha));
        g.setFont(Theme::getMonospaceFont(10.0f));
        g.drawText(point.name, px + 10, py - 8, 100, 16, juce::Justification::left);
    }
}

void RoomMapComponentV4::resized()
{
}

void RoomMapComponentV4::mouseDown(const juce::MouseEvent& e)
{
    draggingId = juce::Uuid::null(); // Reset
    
    // Check Local Listener
    float localPx = audioProcessor.localAudioListener.x * getWidth();
    float localPy = audioProcessor.localAudioListener.y * getHeight();
    if (e.getPosition().getDistanceFrom(juce::Point<int>((int)localPx, (int)localPy)) < 12.0f) {
        draggingId = audioProcessor.localAudioListener.id;
        audioProcessor.selectedListenerId = draggingId;
        isDraggingWall = false;
        isDraggingListener = true;
        dragStartMouseX = (float)e.x / getWidth();
        dragStartMouseY = (float)e.y / getHeight();
        dragStartObjX = audioProcessor.localAudioListener.x;
        dragStartObjY = audioProcessor.localAudioListener.y;
        if (audioProcessor.onStateChanged) audioProcessor.onStateChanged();
        return;
    }
    
    // Check Remote Listeners
    for (const auto& pair : audioProcessor.remoteListeners) {
        float rPx = pair.second.x * getWidth();
        float rPy = pair.second.y * getHeight();
        if (e.getPosition().getDistanceFrom(juce::Point<int>((int)rPx, (int)rPy)) < 12.0f) {
            draggingId = pair.first;
            audioProcessor.selectedListenerId = draggingId;
            isDraggingWall = false;
            isDraggingListener = true;
            dragStartMouseX = (float)e.x / getWidth();
            dragStartMouseY = (float)e.y / getHeight();
            dragStartObjX = pair.second.x;
            dragStartObjY = pair.second.y;
            if (audioProcessor.onStateChanged) audioProcessor.onStateChanged();
            return;
        }
    }
    
    // Find clicked point (IRs) - iterate backwards to catch top items first
    for (auto it = audioProcessor.points.rbegin(); it != audioProcessor.points.rend(); ++it)
    {
        float px = it->x * getWidth();
        float py = it->y * getHeight();
        float radius = 8.0f; 

        if (e.getPosition().getDistanceFrom(juce::Point<int>((int)px, (int)py)) < radius)
        {
            if (!it->locked) 
            {
                draggingId = it->id;
                isDraggingWall = false;
                isDraggingListener = false;
                
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
        float cx = (x1 + x2) * 0.5f;
        float cy = (y1 + y2) * 0.5f;
        
        int hitHandle = -1;
        juce::Point<int> mousePos = e.getPosition();
        
        if (mousePos.getDistanceFrom(juce::Point<int>((int)x1, (int)y1)) < 12.0f) hitHandle = 1; // Left/Size
        else if (mousePos.getDistanceFrom(juce::Point<int>((int)x2, (int)y2)) < 12.0f) hitHandle = 2; // Right/Rotate
        else if (mousePos.getDistanceFrom(juce::Point<int>((int)cx, (int)cy)) < 12.0f) hitHandle = 0; // Center/Move

        if (hitHandle != -1)
        {
            // Select Wall
            audioProcessor.selectedWallId = w.id;
            
            if (!w.locked)
            {
                draggingId = w.id;
                isDraggingWall = true;
                dragHandle = hitHandle;
                
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

void RoomMapComponentV4::mouseDrag(const juce::MouseEvent& e)
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
            float w_x1, w_y1, w_x2, w_y2;
            
            if (dragHandle == 0) // Move
            {
                w_x1 = juce::jlimit(0.0f, 1.0f, dragStartWall[0] + dx);
                w_y1 = juce::jlimit(0.0f, 1.0f, dragStartWall[1] + dy);
                w_x2 = juce::jlimit(0.0f, 1.0f, dragStartWall[2] + dx);
                w_y2 = juce::jlimit(0.0f, 1.0f, dragStartWall[3] + dy);
            }
            else if (dragHandle == 1) // Size (Left) - Keep Center Fixed
            {
                float cx = (dragStartWall[0] + dragStartWall[2]) * 0.5f;
                float cy = (dragStartWall[1] + dragStartWall[3]) * 0.5f;
                
                // Dist from mouse to center
                float distSq = (nx - cx)*(nx - cx) + (ny - cy)*(ny - cy);
                float halfLen = std::sqrt(distSq);
                
                // Current Angle
                float wallDx = dragStartWall[2] - dragStartWall[0];
                float wallDy = dragStartWall[3] - dragStartWall[1];
                float angle = std::atan2(wallDy, wallDx);
                
                float hx = halfLen * std::cos(angle);
                float hy = halfLen * std::sin(angle);
                
                w_x1 = cx - hx; w_x2 = cx + hx;
                w_y1 = cy - hy; w_y2 = cy + hy;
            }
            else // Handle 2: Rotate (Right) - Keep Center Fixed, Length Fixed?
            {
                // User said "Right dot controls angle".
                float cx = (dragStartWall[0] + dragStartWall[2]) * 0.5f;
                float cy = (dragStartWall[1] + dragStartWall[3]) * 0.5f;
                
                // New Angle from Center to Mouse
                float angle = std::atan2(ny - cy, nx - cx);
                
                // Original Length
                float wallDx = dragStartWall[2] - dragStartWall[0];
                float wallDy = dragStartWall[3] - dragStartWall[1];
                float halfLen = std::sqrt(wallDx*wallDx + wallDy*wallDy) * 0.5f;
                
                float hx = halfLen * std::cos(angle);
                float hy = halfLen * std::sin(angle);
                
                w_x1 = cx - hx; w_x2 = cx + hx;
                w_y1 = cy - hy; w_y2 = cy + hy;
            }
            
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
            if (isDraggingListener) {
                audioProcessor.updateListenerPosition(draggingId, tx, ty);
            } else {
                audioProcessor.updatePointPosition(draggingId, tx, ty);
            }
        }
        
        lastMouseX = nx;
        lastMouseY = ny;
    }
}

void RoomMapComponentV4::mouseUp(const juce::MouseEvent&)
{
    draggingId = juce::Uuid::null();
}

bool RoomMapComponentV4::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".aif"))
            return true;
    }
    return false;
}

void RoomMapComponentV4::filesDropped(const juce::StringArray& files, int x, int y)
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
