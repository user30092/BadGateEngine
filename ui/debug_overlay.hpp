#pragma once
#include "ui.hpp"
#include "../core/math.hpp"

namespace mc {

struct DebugInfo {
    float fps = 0.0f;
    float frameTimeMs = 0.0f;
    Vec3  playerPos;
    float yaw = 0.0f, pitch = 0.0f;
    bool  onGround = false;

    unsigned loadedChunks   = 0;
    unsigned pendingChunks  = 0;
    unsigned inFlightChunks = 0;

    unsigned meshes = 0;
    unsigned drawn  = 0;
    unsigned culled = 0;

    int  renderDistance    = 0;
    bool multiplayer       = false;
    int  remotePlayerCount = 0;
};

class DebugOverlay {
public:
    void render(UIRenderer& ui, int sw, int sh, const DebugInfo& info);
};

} // namespace mc
