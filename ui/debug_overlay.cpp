#include "debug_overlay.hpp"
#include <cstdio>

namespace mc {

namespace {
Vec4 fpsColor(float fps) {
    if (fps >= 55.0f) return {0.35f, 1.0f, 0.35f, 1.0f};
    if (fps >= 30.0f) return {1.0f, 1.0f, 0.35f, 1.0f};
    return {1.0f, 0.35f, 0.35f, 1.0f};
}
} // namespace

void DebugOverlay::render(UIRenderer& ui, int sw, int sh, const DebugInfo& info) {
    (void)sw; (void)sh;  // панель в фиксированной позиции

    constexpr float px = 20.0f;
    constexpr float py = 20.0f;
    constexpr float panelW = 560.0f;
    constexpr float panelH = 260.0f;
    constexpr float pad    = 14.0f;
    constexpr float lineH  = 24.0f;
    constexpr float scale  = 2.0f;

    ui.drawRect(px, py, panelW, panelH, Vec4{0.0f, 0.0f, 0.0f, 0.65f});

    char buf[256];
    float x = px + pad;
    float y = py + pad;

    std::snprintf(buf, sizeof(buf), "FPS: %.1f   FRAME: %.2f MS",
                  info.fps, info.frameTimeMs);
    ui.drawText(buf, x, y, scale, fpsColor(info.fps)); y += lineH;

    std::snprintf(buf, sizeof(buf), "POS: %.2f  %.2f  %.2f",
                  info.playerPos.x, info.playerPos.y, info.playerPos.z);
    ui.drawText(buf, x, y, scale, Vec4{1,1,1,1}); y += lineH;

    std::snprintf(buf, sizeof(buf), "YAW: %.1f  PITCH: %.1f  GROUND: %s",
                  info.yaw, info.pitch, info.onGround ? "YES" : "NO");
    ui.drawText(buf, x, y, scale, Vec4{1,1,1,1}); y += lineH;

    y += 6;
    std::snprintf(buf, sizeof(buf), "CHUNKS LOADED: %u  PEND: %u  INFL: %u",
                  info.loadedChunks, info.pendingChunks, info.inFlightChunks);
    ui.drawText(buf, x, y, scale, Vec4{0.75f, 0.9f, 1.0f, 1.0f}); y += lineH;

    std::snprintf(buf, sizeof(buf), "MESHES TOTAL: %u  DRAWN: %u  CULL: %u",
                  info.meshes, info.drawn, info.culled);
    ui.drawText(buf, x, y, scale, Vec4{0.75f, 0.9f, 1.0f, 1.0f}); y += lineH;

    std::snprintf(buf, sizeof(buf), "RDIST: %d  MP: %s  OTHERS: %d",
                  info.renderDistance,
                  info.multiplayer ? "ON" : "OFF",
                  info.remotePlayerCount);
    ui.drawText(buf, x, y, scale, Vec4{0.75f, 0.9f, 1.0f, 1.0f}); y += lineH;

    y += 8;
    ui.drawText("F3: HIDE   T: TELEPORT", x, y, scale, Vec4{1.0f, 0.7f, 0.4f, 1.0f});
}

} // namespace mc
