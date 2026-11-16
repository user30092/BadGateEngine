#pragma once
#include "../input/input.hpp"        // Rect (уже портативен)
#include "../core/random.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <random>

namespace mc {

enum class Screen {
    MainMenu,
    WorldList,
    CreateWorld,
    Settings,
    MultiplayerConnect,
    InGame,
    Paused,
    ConnectionLost,
    TextEntry,
};

enum class TextField { PlayerName, WorldName, ServerHost, TeleportCoords };

enum class Action {
    None,
    GoTo,
    PlayWorld,
    DeleteWorld,
    CreateWorldConfirm,
    RerollSeed,
    RenderDistanceDelta,
    SensitivityDelta,
    ConnectToServer,
    ReconnectLast,
    ExitToMenu,
    QuitApp,

    OpenTextEntry,
    KeyChar,
    KeyBackspace,
    KeySpace,
    KeyDone,
    KeyCancel,
    ToggleDebug,
};

struct TextEntryState {
    bool active = false;
    TextField field = TextField::PlayerName;
    std::string buffer;
    size_t maxLength = 24;
    Screen returnScreen = Screen::MainMenu;
};

struct MenuButton {
    Rect rect;
    std::string label;
    Action action = Action::None;
    int payload = 0;
    Screen targetScreen = Screen::MainMenu;
};

// Распознаёт тап (DOWN + UP в одной точке без существенного сдвига) по
// прямоугольным кнопкам меню. Платформа (Android / GLFW) декодирует своё
// нативное событие и зовёт эти методы с уже извлечёнными координатами.
class MenuInput {
public:
    void onPointerDown(float x, float y) {
        down = true;
        downX = x;
        downY = y;
    }

    // Возвращает указатель на кнопку, по которой тапнули, либо nullptr.
    // buttons должен жить до конца вызова (указатель не сохраняется).
    const MenuButton* onPointerUp(float x, float y,
                                   const std::vector<MenuButton>& buttons) {
        bool wasDown = down;
        down = false;
        if (!wasDown) return nullptr;
        float dx = x - downX;
        float dy = y - downY;
        if (dx * dx + dy * dy > TAP_SLOP * TAP_SLOP) return nullptr; // свайп, не тап
        for (auto& b : buttons) {
            if (b.rect.contains(x, y)) return &b;
        }
        return nullptr;
    }

    void onPointerCancel() { down = false; }

private:
    bool  down   = false;
    float downX  = 0, downY = 0;
    static constexpr float TAP_SLOP = 28.0f;   // px — терпимость к дрожанию пальца
};

struct Settings {
    int   renderDistance  = 6;
    float lookSensitivity = 0.15f;
    bool  musicEnabled    = true;
    std::string lastHost = "127.0.0.1";
    uint16_t    lastPort = 25565;

    // NEW: stable player name across launches. Engine::init generates a
    // random one on first launch, then persists it so UUIDs stay stable.
    std::string playerName;

    void save(const std::string& path) const;
    static Settings load(const std::string& path);
};

struct PendingWorldCreation {
    std::string name = "World 1";
    std::string salt;
    uint32_t worldIndex = 1;

    void rerollSalt() {
        Random r(std::random_device{}());
        char buf[9];
        std::snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(r.next()));
        salt = buf;
    }
};

} // namespace mc
