#include "RoomMapComponent.h"
#include "Theme.h"

RoomMapComponent::RoomMapComponent(IrisAudioProcessor& p)
    : audioProcessor(p) {}

RoomMapComponent::~RoomMapComponent() {}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void RoomMapComponent::paint (juce::Graphics& g)
{
    g.fillAll(Theme::panelBackground);

    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());

    // Grid
    g.setColour(Theme::gridLineColor);
    for (int i = 1; i < 10; ++i)
    {
        float pos = i / 10.0f;
        g.drawLine(pos * w, 0, pos * w, h);
        g.drawLine(0, pos * h, w, pos * h);
    }
    g.setColour(Theme::borderMinimal);
    g.drawRect(getLocalBounds(), 1);

    // Ambient glow for active IR points
    for (auto& p : audioProcessor.points)
    {
        if (!audioProcessor.smoothedWeights.count(p.id)) continue;
        float weight = audioProcessor.smoothedWeights[p.id];
        if (weight <= 0.01f) continue;

        float radius = 20.0f * weight + 15.0f;
        g.setColour(p.color.withAlpha(weight * 0.15f));
        g.fillEllipse(p.x * w - radius, p.y * h - radius, radius * 2, radius * 2);
    }

    // Walls
    float wallOpacity = audioProcessor.wallOpacityParam
                        ? audioProcessor.wallOpacityParam->load()
                        : 0.8f;

    for (const auto& wall : audioProcessor.walls)
    {
        float x1 = wall.x1 * w, y1 = wall.y1 * h;
        float x2 = wall.x2 * w, y2 = wall.y2 * h;
        float cx = (x1 + x2) * 0.5f, cy = (y1 + y2) * 0.5f;

        g.setColour(Theme::wallLineColor.withAlpha(wallOpacity));
        g.drawLine(x1, y1, x2, y2, 2.0f);

        g.setColour(Theme::wallLineColor.brighter().withAlpha(wallOpacity));
        g.fillEllipse(cx - 3, cy - 3, 6, 6);

        g.setColour(Theme::accentCyan.withAlpha(wallOpacity));
        g.fillEllipse(x1 - 3, y1 - 3, 6, 6);

        g.setColour(Theme::textMuted.withAlpha(wallOpacity));
        g.fillEllipse(x2 - 3, y2 - 3, 6, 6);

        if (wall.locked)
        {
            g.setColour(Theme::accentCyan);
            g.drawRect(cx - 4.0f, cy - 4.0f, 8.0f, 8.0f, 1.0f);
        }
    }

    // Remote listeners
    for (const auto& pair : audioProcessor.remoteListeners)
    {
        const auto& rl = pair.second;

        float outlinePx = rl.x       * w, outlinePy = rl.y       * h;
        float px        = rl.currentX * w, py        = rl.currentY * h;
        float radius    = 10.0f;

        float dist = std::abs(px - outlinePx) + std::abs(py - outlinePy);
        if (dist > 1.0f)
        {
            g.setColour(Theme::borderMinimal);
            g.drawEllipse(outlinePx - radius, outlinePy - radius, radius * 2, radius * 2, 1.0f);
        }

        g.setColour(audioProcessor.selectedListenerId == rl.id
                    ? Theme::listenerRemotePink : Theme::listenerInactiveGrey);
        g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);

        g.setColour(Theme::backgroundDark);
        g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, 2.0f);

        g.setColour(Theme::textPrimary);
        g.setFont(Theme::getBaseFont(10.0f));
        g.drawText(rl.name, px - radius, py - radius, radius * 2, radius * 2, juce::Justification::centred);
    }

    // Local listener
    {
        const auto& local = audioProcessor.localAudioListener;
        float px = local.x * w,       py = local.y * h;
        float lx = local.currentX * w, ly = local.currentY * h;
        float radius = 10.0f;

        float dist = std::abs(px - lx) + std::abs(py - ly);
        if (dist > 1.0f)
        {
            g.setColour(Theme::borderMinimal);
            g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, 1.0f);
        }

        px = lx; py = ly;
        g.setColour(Theme::listenerLocalRed.withAlpha(0.2f));
        g.fillEllipse(px - radius * 1.5f, py - radius * 1.5f, radius * 3.0f, radius * 3.0f);

        g.setColour(Theme::listenerLocalRed);
        g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);

        g.setColour(Theme::textPrimary);
        g.setFont(Theme::getBaseFont(10.0f));
        g.drawText(local.name, px - radius, py - radius, radius * 2, radius * 2, juce::Justification::centred);

        g.setColour(Theme::backgroundDark);
        g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, 2.0f);
    }

    // IR points
    for (const auto& point : audioProcessor.points)
    {
        float px = point.x * w, py = point.y * h;
        float radius = 6.0f;
        float alpha  = 0.5f;
        float strokeW = 1.0f;

        if (audioProcessor.smoothedWeights.count(point.id))
        {
            float weight = audioProcessor.smoothedWeights[point.id];
            if (weight > 0.001f)
            {
                alpha   = 0.5f + 0.5f * weight;
                strokeW = 1.0f + 2.0f * weight;
            }
        }

        g.setColour(point.color.withAlpha(alpha));
        g.fillEllipse(px - radius, py - radius, radius * 2, radius * 2);

        g.setColour(point.color.brighter(0.5f).withAlpha(alpha));
        g.drawEllipse(px - radius, py - radius, radius * 2, radius * 2, strokeW);

        if (point.locked)
        {
            g.setColour(Theme::accentCyan);
            g.fillEllipse(px + radius * 0.5f, py - radius * 1.5f, 4, 4);
        }

        g.setColour(Theme::textSecondary.withAlpha(alpha));
        g.setFont(Theme::getMonospaceFont(10.0f));
        g.drawText(point.name, px + 10, py - 8, 100, 16, juce::Justification::left);
    }
}

void RoomMapComponent::resized() {}

// ---------------------------------------------------------------------------
// Mouse interaction
// ---------------------------------------------------------------------------

void RoomMapComponent::mouseDown(const juce::MouseEvent& e)
{
    draggingId = juce::Uuid::null();

    auto hitRadius = [&](float ox, float oy) -> bool
    {
        return e.getPosition().getDistanceFrom(juce::Point<int>(static_cast<int>(ox),
                                                                static_cast<int>(oy))) < 12.0f;
    };

    // Local listener
    float localPx = audioProcessor.localAudioListener.x * getWidth();
    float localPy = audioProcessor.localAudioListener.y * getHeight();
    if (hitRadius(localPx, localPy))
    {
        draggingId             = audioProcessor.localAudioListener.id;
        audioProcessor.selectedListenerId = draggingId;
        isDraggingWall         = false;
        isDraggingListener     = true;
        dragStartMouseX        = static_cast<float>(e.x) / getWidth();
        dragStartMouseY        = static_cast<float>(e.y) / getHeight();
        dragStartObjX          = audioProcessor.localAudioListener.x;
        dragStartObjY          = audioProcessor.localAudioListener.y;
        if (audioProcessor.onStateChanged) audioProcessor.onStateChanged();
        return;
    }

    // Remote listeners
    for (const auto& pair : audioProcessor.remoteListeners)
    {
        float rPx = pair.second.x * getWidth();
        float rPy = pair.second.y * getHeight();
        if (hitRadius(rPx, rPy))
        {
            draggingId             = pair.first;
            audioProcessor.selectedListenerId = draggingId;
            isDraggingWall         = false;
            isDraggingListener     = true;
            dragStartMouseX        = static_cast<float>(e.x) / getWidth();
            dragStartMouseY        = static_cast<float>(e.y) / getHeight();
            dragStartObjX          = pair.second.x;
            dragStartObjY          = pair.second.y;
            if (audioProcessor.onStateChanged) audioProcessor.onStateChanged();
            return;
        }
    }

    // IR points (reverse iteration — top items have priority)
    for (auto it = audioProcessor.points.rbegin(); it != audioProcessor.points.rend(); ++it)
    {
        float px = it->x * getWidth();
        float py = it->y * getHeight();
        if (e.getPosition().getDistanceFrom(juce::Point<int>(static_cast<int>(px),
                                                              static_cast<int>(py))) < 8.0f)
        {
            if (!it->locked)
            {
                draggingId          = it->id;
                isDraggingWall      = false;
                isDraggingListener  = false;
                dragStartMouseX     = static_cast<float>(e.x) / getWidth();
                dragStartMouseY     = static_cast<float>(e.y) / getHeight();
                dragStartObjX       = it->x;
                dragStartObjY       = it->y;
                lastMouseX          = dragStartMouseX;
                lastMouseY          = dragStartMouseY;
            }
            return;
        }
    }

    // Walls
    for (const auto& wall : audioProcessor.walls)
    {
        float x1 = wall.x1 * getWidth(),  y1 = wall.y1 * getHeight();
        float x2 = wall.x2 * getWidth(),  y2 = wall.y2 * getHeight();
        float cx = (x1 + x2) * 0.5f,     cy = (y1 + y2) * 0.5f;

        int hitHandle = -1;
        if      (hitRadius(x1, y1)) hitHandle = 1;
        else if (hitRadius(x2, y2)) hitHandle = 2;
        else if (hitRadius(cx, cy)) hitHandle = 0;

        if (hitHandle != -1)
        {
            audioProcessor.selectedWallId = wall.id;
            if (!wall.locked)
            {
                draggingId      = wall.id;
                isDraggingWall  = true;
                dragHandle      = hitHandle;
                dragStartMouseX = static_cast<float>(e.x) / getWidth();
                dragStartMouseY = static_cast<float>(e.y) / getHeight();
                dragStartWall[0] = wall.x1;
                dragStartWall[1] = wall.y1;
                dragStartWall[2] = wall.x2;
                dragStartWall[3] = wall.y2;
                lastMouseX      = dragStartMouseX;
                lastMouseY      = dragStartMouseY;
            }
            if (audioProcessor.onStateChanged) audioProcessor.onStateChanged();
            return;
        }
    }

    if (audioProcessor.selectedWallId != juce::Uuid::null())
    {
        audioProcessor.selectedWallId = juce::Uuid::null();
        if (audioProcessor.onStateChanged) audioProcessor.onStateChanged();
    }
}

void RoomMapComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingId.isNull()) return;

    float nx = static_cast<float>(e.x) / getWidth();
    float ny = static_cast<float>(e.y) / getHeight();
    float dx = nx - dragStartMouseX;
    float dy = ny - dragStartMouseY;

    if (isDraggingWall)
    {
        float w_x1, w_y1, w_x2, w_y2;

        if (dragHandle == 0)
        {
            w_x1 = juce::jlimit(0.0f, 1.0f, dragStartWall[0] + dx);
            w_y1 = juce::jlimit(0.0f, 1.0f, dragStartWall[1] + dy);
            w_x2 = juce::jlimit(0.0f, 1.0f, dragStartWall[2] + dx);
            w_y2 = juce::jlimit(0.0f, 1.0f, dragStartWall[3] + dy);
        }
        else if (dragHandle == 1)
        {
            float cx = (dragStartWall[0] + dragStartWall[2]) * 0.5f;
            float cy = (dragStartWall[1] + dragStartWall[3]) * 0.5f;
            float halfLen = std::sqrt((nx - cx)*(nx - cx) + (ny - cy)*(ny - cy));

            float wallDx = dragStartWall[2] - dragStartWall[0];
            float wallDy = dragStartWall[3] - dragStartWall[1];
            float angle  = std::atan2(wallDy, wallDx);

            float hx = halfLen * std::cos(angle);
            float hy = halfLen * std::sin(angle);
            w_x1 = cx - hx; w_x2 = cx + hx;
            w_y1 = cy - hy; w_y2 = cy + hy;
        }
        else
        {
            float cx = (dragStartWall[0] + dragStartWall[2]) * 0.5f;
            float cy = (dragStartWall[1] + dragStartWall[3]) * 0.5f;
            float angle  = std::atan2(ny - cy, nx - cx);

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
        float tx = dragStartObjX + dx;
        float ty = dragStartObjY + dy;
        audioProcessor.constrainPointToWalls(tx, ty);

        if (isDraggingListener)
            audioProcessor.updateListenerPosition(draggingId, tx, ty);
        else
            audioProcessor.updatePointPosition(draggingId, tx, ty);
    }

    lastMouseX = nx;
    lastMouseY = ny;
}

void RoomMapComponent::mouseUp(const juce::MouseEvent&)
{
    draggingId = juce::Uuid::null();
}

// ---------------------------------------------------------------------------
// File drag-and-drop
// ---------------------------------------------------------------------------

bool RoomMapComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".aif"))
            return true;
    return false;
}

void RoomMapComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    float nx = juce::jlimit(0.0f, 1.0f, static_cast<float>(x) / getWidth());
    float ny = juce::jlimit(0.0f, 1.0f, static_cast<float>(y) / getHeight());

    for (const auto& f : files)
    {
        juce::File file(f);
        if (file.existsAsFile()
            && (file.hasFileExtension("wav") || file.hasFileExtension("WAV")
                || file.hasFileExtension("aiff") || file.hasFileExtension("aif")))
        {
            juce::Uuid newId = audioProcessor.addIRFromFile(file);
            if (newId != juce::Uuid::null())
            {
                audioProcessor.updatePointPosition(newId, nx, ny);
                nx += 0.02f;
                ny += 0.02f;
            }
        }
    }
}
