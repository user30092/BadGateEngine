// android_platform.hpp
#pragma once
#include "../platform.hpp"
#include <atomic>

struct android_app;
struct ANativeWindow;
struct AInputEvent;

namespace mc::android {

class AndroidPlatform : public IHost {
public:
    explicit AndroidPlatform(android_app* app);
    ~AndroidPlatform() override;

    void pollEvents(Engine& engine) override;
    bool shouldClose() const override { return !running_.load(); }
    void requestQuit() override        { running_ = false; }
    void* nativeWindow() override;
    std::string dataPath() const override;
    void shutdown() override;

    // Публичные, чтобы glue callbacks имели к ним доступ.
    static int32_t onInput(android_app* app, AInputEvent* event);
    static void    onCmd  (android_app* app, int32_t cmd);

private:
    android_app*      app_ = nullptr;
    Engine*           engine_ = nullptr;
    std::atomic<bool> running_{true};
    std::atomic<bool> windowReady_{false};
    ANativeWindow*    pendingWindow_ = nullptr;
};

} // namespace mc::android
