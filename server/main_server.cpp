#include "game_server.hpp"
#include "../core/seed.hpp"
#include "../core/log.hpp"
#include <csignal>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>

using namespace mc;

namespace {
std::atomic<bool> g_shouldStop{false};
void onSignal(int) { g_shouldStop = true; }
}

int main(int argc, char** argv) {
    uint16_t    port          = 25565;
    std::string salt;
    std::string saveDir       = "./server_world";
    int         renderDistance = 8;
    bool        haveExplicitSeed = false;
    uint32_t    explicitSeed     = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto nextArg = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                LOGE("Missing value for %s", name);
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "--port") {
            port = static_cast<uint16_t>(std::stoi(nextArg("--port")));
        } else if (arg == "--salt") {
            salt = nextArg("--salt");
        } else if (arg == "--seed") {
            explicitSeed = static_cast<uint32_t>(std::stoul(nextArg("--seed")));
            haveExplicitSeed = true;
        } else if (arg == "--save-dir") {
            saveDir = nextArg("--save-dir");
        } else if (arg == "--render-distance") {
            renderDistance = std::stoi(nextArg("--render-distance"));
            if (renderDistance < 2)  renderDistance = 2;
            if (renderDistance > 16) renderDistance = 16;
        } else if (arg == "--help" || arg == "-h") {
            LOGI("Usage: badgate_server [options]");
            LOGI("  --port N               listen port (default 25565)");
            LOGI("  --salt TEXT            salt for date-based seed (mutually exclusive with --seed)");
            LOGI("  --seed N               explicit numeric seed");
            LOGI("  --save-dir PATH        world directory (default ./server_world)");
            LOGI("  --render-distance N    chunk radius streamed to clients (2..16, default 8)");
            LOGI("  --help                 show this message");
            return 0;
        } else {
            LOGE("Unknown argument: %s (see --help)", arg.c_str());
            return 1;
        }
    }

    if (saveDir.empty()) saveDir = "./server_world";

    uint32_t seed = haveExplicitSeed ? explicitSeed : seedFromDateAndSalt(salt);

    LOGI("=== BadGate server ===");
    LOGI("  port           : %u", static_cast<unsigned>(port));
    LOGI("  seed           : %u", seed);
    LOGI("  save dir       : %s", saveDir.c_str());
    LOGI("  render distance: %d chunks", renderDistance);
    LOGI("======================");

    ensureDir(saveDir);

    NetworkServer server(seed, saveDir, renderDistance);
    if (!server.start(port)) {
        LOGE("Failed to start server on port %u", static_cast<unsigned>(port));
        return 1;
    }

    LOGI("Server running on port %u. Clients connect via:");
    LOGI("  LAN      : <your-lan-ip>:%u", static_cast<unsigned>(port));
    LOGI("  Internet : <your-public-ip>:%u  (requires port-forward)", static_cast<unsigned>(port));
    LOGI("Press Ctrl+C to stop.");

    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    while (!g_shouldStop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    LOGI("Shutting down...");
    server.stop();   // сохраняет чанки + состояния всех игроков
    LOGI("Bye.");
    return 0;
}
