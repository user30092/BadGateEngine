#include "desktop_platform.hpp"       // рядом с файлом — ок
#include "../engine.hpp"              // ← было "engine.hpp"
#include "../main_loop.hpp"           // ← было "main_loop.hpp"

int main() {
    mc::desktop::DesktopPlatform platform;
    if (!platform.init()) return 1;
    mc::Engine engine;
    mc::runMainLoop(platform, engine);
    return 0;
}
