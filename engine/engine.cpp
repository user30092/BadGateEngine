#include "engine.hpp"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <thread>

using namespace mc;

namespace {

Rect centeredButton(int screenW, int screenH, int index, int total,
                    float btnW = 420.0f, float btnH = 90.0f, float gap = 24.0f) {
    float totalH = total * btnH + (total - 1) * gap;
    float startY = (screenH - totalH) * 0.5f;
    float x = (screenW - btnW) * 0.5f;
    float y = startY + index * (btnH + gap);
    return Rect{x, y, btnW, btnH};
}

} // namespace

// ============================================================================
// lifecycle
// ============================================================================

void Engine::init(IHost& host) {
    host_ = &host;
    savesRoot = host.dataPath() + "/saves";
    ensureDir(host.dataPath());
    ensureDir(savesRoot);
    settings = Settings::load(host.dataPath() + "/settings.cfg");

    // Стабильное имя игрока: генерируем один раз, потом читаем из settings.
    // От этого зависит UUID и, следовательно, per-player save.
    if (settings.playerName.empty()) {
        Random r(std::random_device{}());
        settings.playerName = "Player" + std::to_string(r.nextInt(100, 999));
        settings.save(host.dataPath() + "/settings.cfg");
    }
    playerName = settings.playerName;
}

void Engine::tick(float dt) {
    if (!running_.load()) return;

    // ---- FPS / frame time (EMA по 0.5 сек) ----
    fpsAccum_  += dt;
    fpsFrames_ += 1;
    if (fpsAccum_ >= 0.5f) {
        cachedFps_      = fpsFrames_ / fpsAccum_;
        cachedFrameMs_  = 1000.0f * fpsAccum_ / fpsFrames_;
        fpsAccum_       = 0.0f;
        fpsFrames_      = 0;
    }

    // ---- autosave player state (singleplayer) ----
    if (screen == Screen::InGame && !multiplayer && world && player) {
        metaAutosaveTimer_ += dt;
        if (metaAutosaveTimer_ >= META_AUTOSAVE_INTERVAL) {
            metaAutosaveTimer_ = 0.0f;
            saveCurrentPlayerState();
        }
    }

    // ---- GL init, как только платформа дала окно ----
    if (!gl.valid()) {
        if (void* nw = host_->nativeWindow()) {
            if (gl.init(nw)) {
                if (!worldRenderer.init())  LOGE("WorldRenderer init failed");
                if (!entityRenderer.init()) LOGE("EntityRenderer init failed");
                if (!uiRenderer.init())     LOGE("UIRenderer init failed");
            }
        }
    }
    if (!gl.valid()) return;

    pollNetwork();

    if (screen == Screen::InGame && world && player) {
        renderGameFrame(dt);
    } else if (screen == Screen::Paused && world) {
        renderPausedFrame();
    } else {
        renderMenuScreen(/*clearScreen=*/true);
    }
}

void Engine::shutdown() {
    saveCurrentPlayerState();           // сначала player (пока world жив)
    if (world) world->shutdown();       // потом чанки
    worldRenderer.clear();
    if (netClient.isRunning()) netClient.stop();
    gl.destroy();
}

// ============================================================================
// pause-кнопка (вызывается платформенным бэкендом)
// ============================================================================

bool Engine::handlePauseTap(float x, float y, bool isDown) {
    if (screen != Screen::InGame) return false;
    if (isDown && pauseBtn.contains(x, y)) { pauseTapActive = true;  return true; }
    if (!isDown && pauseTapActive) {
        pauseTapActive = false;
        if (pauseBtn.contains(x, y)) screen = Screen::Paused;
        return true;
    }
    return false;
}

// ============================================================================
// переходы
// ============================================================================

void Engine::refreshWorldList() {
    worldList = listSavedWorlds(savesRoot);
    pendingCreate.worldIndex = static_cast<uint32_t>(worldList.size()) + 1;
    pendingCreate.name = "World " + std::to_string(pendingCreate.worldIndex);
    pendingCreate.salt.clear();
}

void Engine::enterSingleplayer(const std::string& name, uint32_t seed,
                                bool isNew, const std::string& salt) {
    currentWorldName_  = name;
    metaAutosaveTimer_ = 0.0f;
    localUuid_         = uuidFromName(playerName);

    std::string dir = savesRoot + "/" + name;
    ensureDir(dir);
    ensurePlayerDir(dir);

    // --- world.meta (v3: без player fields) ---
    if (isNew) {
        WorldMeta meta;
        meta.name = name;
        meta.seed = seed;
        meta.salt = salt;
        meta.createdUnix    = static_cast<int64_t>(std::time(nullptr));
        meta.lastPlayedUnix = meta.createdUnix;
        meta.save(dir);
    } else {
        WorldMeta meta;
        if (WorldMeta::load(dir, meta)) {
            meta.lastPlayedUnix = static_cast<int64_t>(std::time(nullptr));
            meta.save(dir);
        }
    }

    // --- player.dat: владелец = текущий игрок (в singleplayer это localUuid_) ---
    ownerUuid_ = localUuid_;
    WorldOwner owner; owner.uuid = ownerUuid_;
    saveWorldOwner(dir, owner);

    // --- создаём мир и игрока ---
    world  = std::make_unique<World>(seed, /*workerThreads=*/0, dir);
    player = std::make_unique<Player>(world.get());
    multiplayer = false;

    // --- загрузка позиции из players/<uuid>.dat ---
    bool resumed = false;
    if (!isNew) {
        SavedPlayer saved;
        if (loadPlayer(dir, localUuid_, saved)) {
            player->position = {saved.x, saved.y, saved.z};
            player->yaw      = saved.yaw;
            player->pitch    = saved.pitch;
            input.setInventoryHotbar(saved.hotbarSlot);
            resumed = true;
        }
    }
    if (!resumed) {
        int h = world->groundHeight(0, 0);
        player->position = {0.5f, static_cast<float>(h + 2), 0.5f};
    }

    world->updateStreaming(player->position.x, player->position.z, 7);
    screen = Screen::InGame;
}

void Engine::enterMultiplayer() {
    localUuid_ = uuidFromName(playerName);   // для debug/UI
    world = std::make_unique<World>(/*seed=*/0, /*workerThreads=*/0, /*saveDir=*/"");
    world->setNetworkOnly(true);
    player = std::make_unique<Player>(world.get());
    player->position = {0.5f, 80.0f, 0.5f};
    multiplayer    = true;
    handshakeSent  = false;
    handshakeAcked = false;
    remotePlayers.clear();
}

void Engine::exitToMenu() {
    saveCurrentPlayerState();
    if (world) {
        worldRenderer.clear();
        if (!multiplayer) world->shutdown();
    }
    world.reset();
    player.reset();
    currentWorldName_.clear();
    localUuid_ = 0;
    ownerUuid_ = 0;
    if (netClient.isRunning()) netClient.stop();
    multiplayer = false;
    remotePlayers.clear();
    screen = Screen::MainMenu;
}

// ============================================================================
// block edits
// ============================================================================

void Engine::sendBlockEdit(int32_t x, int32_t y, int32_t z, BlockId id) {
    world->setBlock(x, y, z, id);
    // WorldRenderer::invalidate() НЕ вызываем — World::setBlock уже помечает
    // нужные чанки dirty (центр + соседи на границе), scheduleMesh перестроит
    // их, сохраняя старый меш до готовности нового (без дырок).
    if (multiplayer && netClient.isRunning()) {
        BlockUpdateMsg msg{x, y, z, id};
        netClient.sendPacket(PacketType::BlockUpdate, writeBlockUpdate(msg));
    }
}

// ============================================================================
// player persistence
// ============================================================================

void Engine::saveCurrentPlayerState() {
    if (multiplayer || !world || !player || currentWorldName_.empty()) return;
    if (localUuid_ == 0) return;

    std::string dir = savesRoot + "/" + currentWorldName_;

    SavedPlayer s;
    s.uuid    = localUuid_;
    s.name    = playerName;
    s.x       = player->position.x;
    s.y       = player->position.y;
    s.z       = player->position.z;
    s.yaw     = player->yaw;
    s.pitch   = player->pitch;
    s.lastSeenUnix = static_cast<int64_t>(std::time(nullptr));
    s.hotbarSlot = input.hotbarSlot();

    savePlayer(dir, s);
}

// ============================================================================
// virtual keyboard
// ============================================================================

void Engine::openTextEntry(TextField field) {
    textEntry.active       = true;
    textEntry.field        = field;
    textEntry.returnScreen = screen;
    switch (field) {
        case TextField::PlayerName:
            textEntry.buffer    = playerName;
            textEntry.maxLength = 16;
            break;
        case TextField::WorldName:
            textEntry.buffer    = pendingCreate.name;
            textEntry.maxLength = 24;
            break;
        case TextField::ServerHost:
            textEntry.buffer    = settings.lastHost + ":" + std::to_string(settings.lastPort);
            textEntry.maxLength = 32;
            break;
        case TextField::TeleportCoords: {
            char buf[64];
            if (player) {
                std::snprintf(buf, sizeof(buf), "%.2f %.2f %.2f",
                              player->position.x, player->position.y, player->position.z);
            } else {
                std::snprintf(buf, sizeof(buf), "0 80 0");
            }
            textEntry.buffer    = buf;
            textEntry.maxLength = 40;
            break;
        }
    }
    screen = Screen::TextEntry;
}

void Engine::commitTextEntry() {
    switch (textEntry.field) {
        case TextField::PlayerName:
            if (!textEntry.buffer.empty()) {
                playerName = textEntry.buffer;
                settings.playerName = playerName;
                settings.save(host_->dataPath() + "/settings.cfg");
            }
            break;

        case TextField::WorldName:
            if (!textEntry.buffer.empty()) pendingCreate.name = textEntry.buffer;
            break;

        case TextField::ServerHost: {
            auto pos = textEntry.buffer.find(':');
            if (pos != std::string::npos && pos + 1 < textEntry.buffer.size()) {
                settings.lastHost = textEntry.buffer.substr(0, pos);
                const char* first = textEntry.buffer.data() + pos + 1;
                const char* last  = textEntry.buffer.data() + textEntry.buffer.size();
                uint16_t port = 0;
                auto [ptr, ec] = std::from_chars(first, last, port);
                if (ec == std::errc{} && ptr == last) settings.lastPort = port;
            } else if (!textEntry.buffer.empty()) {
                settings.lastHost = textEntry.buffer;
            }
            settings.save(host_->dataPath() + "/settings.cfg");
            break;
        }

        case TextField::TeleportCoords: {
            float vals[3]; int n = 0;
            const char* p = textEntry.buffer.c_str();
            while (*p && n < 3) {
                while (*p == ' ' || *p == ',') ++p;
                if (!*p) break;
                char* end = nullptr;
                float v = std::strtof(p, &end);
                if (end == p) { n = 0; break; }
                vals[n++] = v;
                p = end;
            }
            if (player && n == 3) {
                player->position.x = vals[0];
                player->position.y = clampf(vals[1], 1.0f,
                                            static_cast<float>(CHUNK_HEIGHT - 3));
                player->position.z = vals[2];
                player->velocity = {0, 0, 0};
            } else if (player && n == 1) {
                player->position.y = clampf(vals[0], 1.0f,
                                            static_cast<float>(CHUNK_HEIGHT - 3));
                player->velocity = {0, 0, 0};
            }
            break;
        }
    }
    screen = textEntry.returnScreen;
    textEntry.active = false;
}

// ============================================================================
// text entry helpers (для desktop keyboard)
// ============================================================================

void Engine::textEntryChar(char c) {
    if (!textEntry.active) return;
    if (textEntry.buffer.size() < textEntry.maxLength)
        textEntry.buffer.push_back(c);
}

void Engine::textEntryBackspace() {
    if (!textEntry.active) return;
    if (!textEntry.buffer.empty()) textEntry.buffer.pop_back();
}

void Engine::textEntryDone() {
    if (!textEntry.active) return;
    commitTextEntry();
}

void Engine::textEntryCancel() {
    if (!textEntry.active) return;
    screen = textEntry.returnScreen;
    textEntry.active = false;
}

// ============================================================================
// network
// ============================================================================

void Engine::pollNetwork() {
    if (!multiplayer || !netClient.isRunning()) return;

    PacketType type;
    std::vector<uint8_t> payload;
    while (netClient.pollPacket(type, payload)) {
        switch (type) {
            case PacketType::Handshake: {
                HandshakeAck ack = readHandshakeAck(payload);
                myPlayerId = ack.playerId;
                player->position = {ack.x, ack.y, ack.z};
                handshakeAcked = true;
                statusText.clear();
                screen = Screen::InGame;
                break;
            }
            case PacketType::ChunkData: {
                ChunkPtr chunk = deserializeChunk(payload);
                if (chunk) world->injectChunk(std::move(chunk));
                break;
            }
            case PacketType::BlockUpdate: {
                BlockUpdateMsg msg = readBlockUpdate(payload);
                world->setBlock(msg.x, msg.y, msg.z, msg.blockId);
                // invalidate() не зовём — setBlock уже пометил dirty
                break;
            }
            case PacketType::PlayerMove: {
                PlayerState ps = readPlayerState(payload);
                if (ps.playerId != myPlayerId) {
                    remotePlayers[ps.playerId] = {ps.x, ps.y, ps.z, ps.yaw, ps.pitch};
                }
                break;
            }
            case PacketType::PlayerLeave: {
                remotePlayers.erase(readPlayerLeave(payload));
                break;
            }
            case PacketType::ChatMessage: {
                statusText = readChatMessage(payload);
                break;
            }
            default: break;
        }
    }

    if (netClient.connectionWasLost()) {
        statusText = "Connection to server lost";
        screen = Screen::ConnectionLost;
        multiplayer = false;
    }
}

// ============================================================================
// actions
// ============================================================================

void Engine::handleAction(const MenuButton& btn) {
    switch (btn.action) {
        case Action::GoTo:
            if (btn.targetScreen == Screen::WorldList)   refreshWorldList();
            if (btn.targetScreen == Screen::CreateWorld) refreshWorldList();
            screen = btn.targetScreen;
            break;

        case Action::PlayWorld: {
            if (btn.payload < 0 || static_cast<size_t>(btn.payload) >= worldList.size()) break;
            const WorldMeta& meta = worldList[btn.payload];
            enterSingleplayer(meta.name, meta.seed, /*isNew=*/false, meta.salt);
            break;
        }

        case Action::DeleteWorld: {
            if (btn.payload < 0 || static_cast<size_t>(btn.payload) >= worldList.size()) break;
            deleteWorld(savesRoot, worldList[btn.payload].name);
            refreshWorldList();
            break;
        }

        case Action::RerollSeed:
            pendingCreate.rerollSalt();
            break;

        case Action::CreateWorldConfirm: {
            uint32_t seed = pendingCreate.salt.empty()
                ? seedFromDateAndSalt("")
                : seedFromDateAndSalt(pendingCreate.salt);
            enterSingleplayer(pendingCreate.name, seed, /*isNew=*/true, pendingCreate.salt);
            break;
        }

        case Action::RenderDistanceDelta:
            settings.renderDistance = static_cast<int>(
                clampf(static_cast<float>(settings.renderDistance + btn.payload), 2.0f, 16.0f));
            settings.save(host_->dataPath() + "/settings.cfg");
            break;

        case Action::SensitivityDelta:
            settings.lookSensitivity =
                clampf(settings.lookSensitivity + btn.payload * 0.02f, 0.02f, 1.0f);
            settings.save(host_->dataPath() + "/settings.cfg");
            break;

        case Action::ConnectToServer:
        case Action::ReconnectLast: {
            statusText = "Connecting...";
            if (playerName.empty()) playerName = "Player";
            bool ok = netClient.start(settings.lastHost, settings.lastPort);
            if (ok) {
                settings.save(host_->dataPath() + "/settings.cfg");
                enterMultiplayer();
                netClient.sendPacket(PacketType::Handshake, writeHandshakeRequest(playerName));
                handshakeSent = true;
                statusText = "Connecting... (waiting for handshake)";
            } else {
                statusText = "Connection failed";
                screen = Screen::ConnectionLost;
            }
            break;
        }

        case Action::ExitToMenu:
            exitToMenu();
            break;

        case Action::QuitApp:
            running_ = false;
            break;

        case Action::ToggleDebug:
            toggleDebug();
            break;

        case Action::ToggleFlying:
            toggleFlying();
            break;

        case Action::OpenTextEntry:
            openTextEntry(static_cast<TextField>(btn.payload));
            break;

        case Action::KeyChar:
            textEntryChar(static_cast<char>(btn.payload));
            break;

        case Action::KeyBackspace:
            textEntryBackspace();
            break;

        case Action::KeySpace:
            textEntryChar(' ');
            break;

        case Action::KeyDone:
            textEntryDone();
            break;

        case Action::KeyCancel:
            textEntryCancel();
            break;

        case Action::None:
        default: break;
    }
}

// ============================================================================
// buildButtonsForScreen
// ============================================================================

void Engine::buildButtonsForScreen(int sw, int sh) {
    buttons.clear();

    switch (screen) {
        case Screen::MainMenu: {
            buttons.push_back({centeredButton(sw, sh, 0, 4), "PLAY",        Action::GoTo, 0, Screen::WorldList});
            buttons.push_back({centeredButton(sw, sh, 1, 4), "MULTIPLAYER", Action::GoTo, 0, Screen::MultiplayerConnect});
            buttons.push_back({centeredButton(sw, sh, 2, 4), "SETTINGS",    Action::GoTo, 0, Screen::Settings});
            buttons.push_back({centeredButton(sw, sh, 3, 4), "QUIT",        Action::QuitApp});
            break;
        }

        case Screen::WorldList: {
            int total = static_cast<int>(worldList.size()) + 2;
            for (size_t i = 0; i < worldList.size(); ++i) {
                Rect r = centeredButton(sw, sh, static_cast<int>(i), total);
                buttons.push_back({r, worldList[i].name, Action::PlayWorld, static_cast<int>(i)});
                Rect delR{r.x + r.w + 12.0f, r.y, 80.0f, r.h};
                buttons.push_back({delR, "DEL", Action::DeleteWorld, static_cast<int>(i)});
            }
            buttons.push_back({centeredButton(sw, sh, static_cast<int>(worldList.size()), total),
                                "NEW WORLD", Action::GoTo, 0, Screen::CreateWorld});
            buttons.push_back({centeredButton(sw, sh, static_cast<int>(worldList.size()) + 1, total),
                                "BACK", Action::GoTo, 0, Screen::MainMenu});
            break;
        }

        case Screen::CreateWorld: {
            buttons.push_back({centeredButton(sw, sh, 0, 4), "EDIT NAME",   Action::OpenTextEntry,
                                static_cast<int>(TextField::WorldName)});
            buttons.push_back({centeredButton(sw, sh, 1, 4), "REROLL SEED", Action::RerollSeed});
            buttons.push_back({centeredButton(sw, sh, 2, 4), "CREATE",      Action::CreateWorldConfirm});
            buttons.push_back({centeredButton(sw, sh, 3, 4), "BACK",        Action::GoTo, 0, Screen::MainMenu});
            break;
        }

        case Screen::Settings: {
            float y0 = sh * 0.25f;
            buttons.push_back({Rect{sw * 0.5f - 240, y0,       90, 70}, "-", Action::RenderDistanceDelta, -1});
            buttons.push_back({Rect{sw * 0.5f + 150, y0,       90, 70}, "+", Action::RenderDistanceDelta,  1});
            buttons.push_back({Rect{sw * 0.5f - 240, y0 + 110, 90, 70}, "-", Action::SensitivityDelta,    -1});
            buttons.push_back({Rect{sw * 0.5f + 150, y0 + 110, 90, 70}, "+", Action::SensitivityDelta,     1});
            buttons.push_back({Rect{sw * 0.5f - 210, y0 + 220, 420, 80}, "NAME: " + playerName,
                                Action::OpenTextEntry, static_cast<int>(TextField::PlayerName)});
            buttons.push_back({centeredButton(sw, sh, 4, 5), "BACK", Action::GoTo, 0, Screen::MainMenu});
            break;
        }

        case Screen::MultiplayerConnect: {
            buttons.push_back({centeredButton(sw, sh, 0, 4), "EDIT SERVER", Action::OpenTextEntry,
                                static_cast<int>(TextField::ServerHost)});
            buttons.push_back({centeredButton(sw, sh, 1, 4), "CONNECT",    Action::ConnectToServer});
            buttons.push_back({centeredButton(sw, sh, 2, 4), "DISCONNECT", Action::ExitToMenu});
            buttons.push_back({centeredButton(sw, sh, 3, 4), "BACK",       Action::GoTo, 0, Screen::MainMenu});
            break;
        }

        case Screen::ConnectionLost: {
            buttons.push_back({centeredButton(sw, sh, 0, 2), "RECONNECT", Action::ReconnectLast});
            buttons.push_back({centeredButton(sw, sh, 1, 2), "MAIN MENU", Action::GoTo, 0, Screen::MainMenu});
            break;
        }

        case Screen::Paused: {
            std::string debugLabel = showDebug_ ? "DEBUG: ON" : "DEBUG: OFF";
            buttons.push_back({centeredButton(sw, sh, 0, 4), "RESUME",       Action::GoTo, 0, Screen::InGame});
            buttons.push_back({centeredButton(sw, sh, 1, 4), "TELEPORT",     Action::OpenTextEntry,
                                static_cast<int>(TextField::TeleportCoords)});
            buttons.push_back({centeredButton(sw, sh, 2, 4), debugLabel,     Action::ToggleDebug});
            buttons.push_back({centeredButton(sw, sh, 3, 4), "EXIT TO MENU", Action::ExitToMenu});
            break;
        }

        case Screen::InGame:
            break;

        case Screen::TextEntry: {
            static const char KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:";
            constexpr int KEY_COUNT = sizeof(KEYS) - 1;
            constexpr int cols = 8;
            const int rows = (KEY_COUNT + cols - 1) / cols;

            float areaTop = sh * 0.38f;
            float areaW   = sw * 0.92f;
            float startX  = sw * 0.04f;
            float cellW   = areaW / cols;
            float cellH   = sh * 0.46f / rows;

            for (int i = 0; i < KEY_COUNT; ++i) {
                int col = i % cols, row = i / cols;
                Rect r{startX + col * cellW + 4.0f, areaTop + row * cellH + 4.0f,
                       cellW - 8.0f, cellH - 8.0f};
                buttons.push_back({r, std::string(1, KEYS[i]), Action::KeyChar,
                                   static_cast<int>(KEYS[i])});
            }

            float bottomY = areaTop + rows * cellH + 20.0f;
            float btnW = (areaW - 16.0f) / 3.0f;
            buttons.push_back({Rect{startX,                     bottomY, btnW, 80.0f}, "SPACE",  Action::KeySpace});
            buttons.push_back({Rect{startX + btnW + 8.0f,       bottomY, btnW, 80.0f}, "BACK",   Action::KeyBackspace});
            buttons.push_back({Rect{startX + 2 * (btnW + 8.0f), bottomY, btnW, 80.0f}, "DONE",   Action::KeyDone});
            buttons.push_back({Rect{startX, bottomY + 96.0f, areaW, 70.0f}, "CANCEL", Action::KeyCancel});
            break;
        }
    }
}

// ============================================================================
// rendering
// ============================================================================

void Engine::renderMenuScreen(bool clearScreen) {
    int sw = gl.width(), sh = gl.height();
    buildButtonsForScreen(sw, sh);

    if (clearScreen) {
        glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    uiRenderer.begin(sw, sh);
    if (!clearScreen) {
        uiRenderer.drawRect(0, 0, static_cast<float>(sw), static_cast<float>(sh),
                            Vec4{0, 0, 0, 0.55f});
    }

    const char* title = "BADGATE";
    switch (screen) {
        case Screen::WorldList:          title = "SELECT WORLD"; break;
        case Screen::CreateWorld:        title = "NEW WORLD"; break;
        case Screen::Settings:           title = "SETTINGS"; break;
        case Screen::MultiplayerConnect: title = "MULTIPLAYER"; break;
        case Screen::ConnectionLost:     title = "CONNECTION LOST"; break;
        case Screen::Paused:             title = "PAUSED"; break;
        case Screen::TextEntry:          title = "ENTER TEXT"; break;
        default: break;
    }
    float titleScale = 6.0f;
    float titleW = UIRenderer::textWidth(title, titleScale);
    uiRenderer.drawText(title, (sw - titleW) * 0.5f, sh * 0.08f, titleScale, Vec4{1,1,1,1});

    if (screen == Screen::CreateWorld) {
        std::string line1 = "NAME: " + pendingCreate.name;
        std::string seedShown = pendingCreate.salt.empty() ? "DATE ONLY" : pendingCreate.salt;
        std::string line2 = "SALT: " + seedShown;
        uiRenderer.drawText(line1.c_str(), sw * 0.5f - 200, sh * 0.18f,      3.0f, Vec4{0.8f,0.9f,1,1});
        uiRenderer.drawText(line2.c_str(), sw * 0.5f - 200, sh * 0.18f + 30, 3.0f, Vec4{0.8f,0.9f,1,1});
    } else if (screen == Screen::Settings) {
        std::string l1 = "RENDER DIST: " + std::to_string(settings.renderDistance);
        char sensBuf[32];
        std::snprintf(sensBuf, sizeof(sensBuf), "%.2f", settings.lookSensitivity);
        std::string l2 = std::string("SENSITIVITY: ") + sensBuf;
        uiRenderer.drawText(l1.c_str(), sw * 0.5f - 90, sh * 0.25f - 40, 3.0f, Vec4{1,1,1,1});
        uiRenderer.drawText(l2.c_str(), sw * 0.5f - 90, sh * 0.25f + 70, 3.0f, Vec4{1,1,1,1});
    } else if (screen == Screen::MultiplayerConnect || screen == Screen::ConnectionLost) {
        std::string hostLine = settings.lastHost + ":" + std::to_string(settings.lastPort);
        uiRenderer.drawText(hostLine.c_str(), sw * 0.5f - 150, sh * 0.18f, 3.0f, Vec4{0.8f,0.9f,1,1});
        if (!statusText.empty())
            uiRenderer.drawText(statusText.c_str(), sw * 0.5f - 260, sh * 0.22f, 2.5f, Vec4{1,0.6f,0.6f,1});
    } else if (screen == Screen::TextEntry) {
        std::string shown = textEntry.buffer.empty() ? "_" : textEntry.buffer;
        float tw = UIRenderer::textWidth(shown.c_str(), 4.0f);
        uiRenderer.drawText(shown.c_str(), (sw - tw) * 0.5f, sh * 0.2f, 4.0f, Vec4{1, 1, 0.6f, 1});
    }

    for (auto& b : buttons) uiRenderer.drawButton(b.rect, b.label.c_str(), false);

    if (showDebug_ && screen == Screen::Paused)
        debugOverlay.render(uiRenderer, sw, sh, buildDebugInfo());

    uiRenderer.flush();
    gl.swapBuffers();
}

void Engine::renderPausedFrame() {
    float aspect = static_cast<float>(gl.width()) / static_cast<float>(gl.height());
    Mat4 proj = Mat4::perspective(70.0f * DEG2RAD, aspect, 0.1f, 300.0f);
    Vec3 eyePos = player->eyePos();
    Mat4 view = Mat4::lookAt(eyePos, eyePos + player->forward(), {0, 1, 0});
    glClearColor(0.6f, 0.75f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    worldRenderer.render(*world, proj * view, eyePos, settings.renderDistance);
    if (!remotePlayers.empty()) {
        std::vector<RemoteEntityView> others;
        others.reserve(remotePlayers.size());
        for (auto& [id, rp] : remotePlayers) others.push_back({rp.x, rp.y, rp.z, rp.yaw, id});
        entityRenderer.render(proj * view, others);
    }
    renderMenuScreen(/*clearScreen=*/false);
}

void Engine::renderGameFrame(float dt) {
    float pad = 24.0f, btnSize = 110.0f;
    flyBtn   = Rect{gl.width() - btnSize * 2 - pad * 2,         gl.height() - btnSize * 2 - pad * 2, btnSize, btnSize};
    jumpBtn  = Rect{gl.width() - btnSize - pad,         gl.height() - btnSize * 2 - pad * 2, btnSize, btnSize};
    breakBtn = Rect{gl.width() - btnSize * 2 - pad * 2, gl.height() - btnSize - pad,         btnSize, btnSize};
    placeBtn = Rect{gl.width() - btnSize - pad,         gl.height() - btnSize - pad,         btnSize, btnSize};
    pauseBtn = Rect{pad, pad, 140.0f, 70.0f};
    input.setFlyButton(flyBtn);
    input.setJumpButton(jumpBtn);
    input.setBreakButton(breakBtn);
    input.setPlaceButton(placeBtn);

    world->updateStreaming(player->position.x, player->position.z, settings.renderDistance + 1);

    cameraCtrl.update(input, player->yaw, player->pitch, dt, settings.lookSensitivity);

    const float moveSpeed = player->flying ? 60.0f : 30.0f;

    Vec2 moveAxis = input.moveAxis();
    if (moveAxis.length() > 0.01f) {
        player->moveForward(-moveAxis.y * moveSpeed * dt);
        player->moveRight(moveAxis.x * moveSpeed * dt);
    }
    if (!player->flying && input.jumpPressed()) player->jump();

    // Полёт: прямое управление velocity.y через Space/Shift.
    // Должно идти ДО player->update(dt), чтобы update использовал свежий vel.
    if (player->flying) {
        if      (input.jumpHeld())   player->velocity.y =  15.0f;
        else if (input.crouchHeld()) player->velocity.y = -15.0f;
        else                          player->velocity.y =   0.0f;   // hover
    }

    Vec3 eye = player->eyePos();
    Vec3 dir = player->forward();
    auto hit = raycast(*world, eye, dir, 6.0f);

    breakCooldown -= dt;
    placeCooldown -= dt;

    if (hit.hit) {
        const bool wantBreak = input.breakPressed() || input.breakHeld();
        const bool wantPlace = input.placePressed() || input.placeHeld();

        if (wantBreak && breakCooldown <= 0.0f) {
            sendBlockEdit(hit.bx, hit.by, hit.bz, 0);
            breakCooldown = ACTION_COOLDOWN;
        }
        if (wantPlace && placeCooldown <= 0.0f) {
            sendBlockEdit(hit.px, hit.py, hit.pz, static_cast<BlockId>(input.hotbarSlot()));
            placeCooldown = ACTION_COOLDOWN;
        }
    }

    player->update(dt);

    if (multiplayer && netClient.isRunning()) {
        moveSendTimer += dt;
        if (moveSendTimer >= MOVE_SEND_INTERVAL) {
            moveSendTimer = 0.0f;
            PlayerState ps{myPlayerId,
                           player->position.x, player->position.y, player->position.z,
                           player->yaw, player->pitch};
            netClient.sendPacket(PacketType::PlayerMove, writePlayerState(ps));
        }
    }

    glClearColor(0.6f, 0.75f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = static_cast<float>(gl.width()) / static_cast<float>(gl.height());
    Mat4 proj = Mat4::perspective(70.0f * DEG2RAD, aspect, 0.1f, 300.0f);
    Vec3 eyePos = player->eyePos();
    Mat4 view = Mat4::lookAt(eyePos, eyePos + dir, {0, 1, 0});
    Mat4 vp = proj * view;

    worldRenderer.render(*world, vp, eyePos, settings.renderDistance);

    if (!remotePlayers.empty()) {
        std::vector<RemoteEntityView> others;
        others.reserve(remotePlayers.size());
        for (auto& [id, rp] : remotePlayers) {
            others.push_back({rp.x, rp.y, rp.z, rp.yaw, id});
        }
        entityRenderer.render(vp, others);
    }

    uiRenderer.begin(gl.width(), gl.height());
    uiRenderer.drawCrosshair(gl.width(), gl.height());
    uiRenderer.drawHotbar(gl.width(), gl.height(), input.hotbarSlot(), 5);

    if (ShowTouchControls()) {
        Vec2 joyCenter{gl.width() * 0.2f, gl.height() * 0.7f};
        Vec2 knob = joyCenter + input.moveAxis() * 120.0f;
        uiRenderer.drawJoystick(joyCenter, knob, 150.0f, true);
        uiRenderer.drawButton(flyBtn,  "FLY", false);
        uiRenderer.drawButton(jumpBtn,  "JMP", false);
        uiRenderer.drawButton(breakBtn, "HIT", false);
        uiRenderer.drawButton(placeBtn, "PUT", false);
        uiRenderer.drawButton(pauseBtn, "MENU", false);
    }

    if (showDebug_)
        debugOverlay.render(uiRenderer, gl.width(), gl.height(), buildDebugInfo());

    uiRenderer.flush();
    gl.swapBuffers();
}

// ============================================================================
// debug info
// ============================================================================

DebugInfo Engine::buildDebugInfo() const {
    DebugInfo d;
    d.fps         = cachedFps_;
    d.frameTimeMs = cachedFrameMs_;

    if (player) {
        d.playerPos = player->position;
        d.yaw       = player->yaw;
        d.pitch     = player->pitch;
        d.onGround  = player->onGround;
    }
    if (world) {
        d.loadedChunks   = static_cast<unsigned>(world->loadedCount());
        d.pendingChunks  = static_cast<unsigned>(world->pendingCount());
        d.inFlightChunks = static_cast<unsigned>(world->inFlightCount());
    }
    d.meshes            = static_cast<unsigned>(worldRenderer.meshCount());
    d.drawn             = static_cast<unsigned>(worldRenderer.drawnLastFrame());
    d.culled            = static_cast<unsigned>(worldRenderer.culledLastFrame());
    d.renderDistance    = settings.renderDistance;
    d.multiplayer       = multiplayer;
    d.remotePlayerCount = static_cast<int>(remotePlayers.size());
    return d;
}
