#pragma once
#include "../platform.hpp"
#include <string>

struct GLFWwindow;

namespace mc::desktop {

class DesktopPlatform : public IHost {
public:
    DesktopPlatform();
    ~DesktopPlatform() override;

    bool init();

    void pollEvents(Engine& engine) override;
    bool shouldClose() const override;
    void requestQuit() override;
    void* nativeWindow() override;
    std::string dataPath() const override;
    void shutdown() override;

private:
    void onMouseButton(int button, int action, int mods);
    void onCursorPos  (double x, double y);
    void onKey        (int key, int scancode, int action, int mods);
    void onScroll     (double xoff, double yoff);
    void onChar       (unsigned int codepoint);

    GLFWwindow* window_      = nullptr;
    bool        shouldClose_ = false;
    Engine*     engine_      = nullptr;

    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    bool   firstMouse_ = true;

    bool wHeld_ = false, aHeld_ = false, sHeld_ = false, dHeld_ = false;

    void syncCursorMode(bool captured);
    void recomputeKeyboardMove();
};

} // namespace mc::desktop
