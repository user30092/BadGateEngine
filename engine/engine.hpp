#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform.hpp"
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/seed.hpp"
#include "world/world.hpp"
#include "world/world_save.hpp"
#include "world/player_save.hpp"
#include "world/raycast.hpp"
#include "entity/player.hpp"
#include "input/input.hpp"
#include "input/camera.hpp"
#include "render/gl_context.hpp"
#include "render/world_renderer.hpp"
#include "render/entity_renderer.hpp"
#include "ui/ui.hpp"
#include "ui/menu.hpp"
#include "ui/debug_overlay.hpp"
#include "net/client.hpp"
#include "net/protocol.hpp"

namespace mc {

class Engine {
public:
    // ========================================================================
    // lifecycle (вызывается из main_loop)
    // ========================================================================
    void init(IHost& host);
    void tick(float dt);
    void shutdown();
    bool isRunning() const { return running_.load(); }

    // ========================================================================
    // platform-facing API (вызывается платформенным бэкендом)
    // ========================================================================
    // Тап по pause-кнопке: intercept до обычного input dispatch.
    // true = событие съедено (в input/menu не пойдёт).
    bool handlePauseTap(float x, float y, bool isDown);

    // Диспатч нажатой меню-кнопки (MenuInput → Action).
    void handleAction(const MenuButton& btn);

    // ========================================================================
    // публичные подсистемы (видны платформенному бэкенду)
    // ========================================================================
    Input       input;
    MenuInput   menuInput;
    GLContext   gl;

    Screen                  screen = Screen::MainMenu;
    std::vector<MenuButton> buttons;

    void toggleFlying() { if (player) player->toggleFlying(); }
    // ========================================================================
    // debug overlay
    // ========================================================================
    void toggleTC()           { showTouchControls_ = !showTouchControls_; }
    void toggleDebug()        { showDebug_ = !showDebug_; }
    bool debugVisible() const { return showDebug_; }
    bool ShowTouchControls() const { return showTouchControls_; }
    void openTeleportCoords() { openTextEntry(TextField::TeleportCoords); }

    // ========================================================================
    // text entry (для клавиатурного ввода на desktop)
    // ========================================================================
    bool textEntryActive() const { return textEntry.active; }
    void textEntryChar(char c);
    void textEntryBackspace();
    void textEntryDone();
    void textEntryCancel();


private:
    IHost* host_ = nullptr;

    // ---- подсистемы ----
    CameraController  cameraCtrl;
    WorldRenderer     worldRenderer;
    EntityRenderer    entityRenderer;
    UIRenderer        uiRenderer;
    DebugOverlay      debugOverlay;
    NetworkClient     netClient;

    // ---- настройки / пути ----
    Settings              settings;
    std::string           savesRoot;
    std::vector<WorldMeta> worldList;
    PendingWorldCreation  pendingCreate;
    std::string           statusText;

    // ---- мир / игрок ----
    std::unique_ptr<World>  world;
    std::unique_ptr<Player> player;
    bool multiplayer = false;

    // ---- persistence ----
    std::string currentWorldName_;        // пусто = не в мире (меню/мультиплеер)
    uint64_t    localUuid_ = 0;           // uuidFromName(playerName)
    uint64_t    ownerUuid_ = 0;           // из player.dat (singleplayer)
    float       metaAutosaveTimer_ = 0.0f;
    static constexpr float META_AUTOSAVE_INTERVAL = 30.0f;   // сек

    // ---- мультиплеер ----
    std::string playerName;
    uint32_t    myPlayerId    = 0;
    bool        handshakeSent = false;
    bool        handshakeAcked = false;
    float       moveSendTimer = 0.0f;
    static constexpr float MOVE_SEND_INTERVAL = 1.0f / 15.0f;

    struct RemotePlayer { float x, y, z, yaw, pitch; };
    std::unordered_map<uint32_t, RemotePlayer> remotePlayers;

    // ---- виртуальная клавиатура ----
    TextEntryState textEntry;

    // ---- HUD-кнопки ----
    Rect jumpBtn, flyBtn, breakBtn, placeBtn, pauseBtn;
    bool pauseTapActive = false;

    // ---- cooldown для held ЛКМ/ПКМ (desktop) ----
    float breakCooldown = 0.0f;
    float placeCooldown = 0.0f;
    static constexpr float ACTION_COOLDOWN = 0.15f;

    // ---- touch overlay ----
    bool  showTouchControls_   = false;

    // ---- debug overlay ----
    bool  showDebug_     = false;
    float fpsAccum_      = 0.0f;
    int   fpsFrames_     = 0;
    float cachedFps_     = 0.0f;
    float cachedFrameMs_ = 0.0f;

    std::atomic<bool> running_{true};

    // ---- внутренние методы ----
    void buildButtonsForScreen(int sw, int sh);
    void enterSingleplayer(const std::string& name, uint32_t seed,
                           bool isNew, const std::string& salt);
    void enterMultiplayer();
    void exitToMenu();
    void refreshWorldList();
    void renderMenuScreen(bool clearScreen);
    void renderPausedFrame();
    void renderGameFrame(float dt);
    void pollNetwork();
    void sendBlockEdit(int32_t x, int32_t y, int32_t z, BlockId id);
    void openTextEntry(TextField field);
    void commitTextEntry();
    void saveCurrentPlayerState();
    DebugInfo buildDebugInfo() const;
};

} // namespace mc
