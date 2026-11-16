// main_loop.cpp
#include "main_loop.hpp"
#include "engine.hpp"
#include <chrono>

namespace mc {

void runMainLoop(IHost& host, Engine& engine) {
    engine.init(host);
    auto last = std::chrono::steady_clock::now();

    while (!host.shouldClose() && engine.isRunning()) {
        host.pollEvents(engine);

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        engine.tick(dt);
    }

    engine.shutdown();
    host.shutdown();
}

} // namespace mc
