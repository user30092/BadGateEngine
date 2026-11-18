#include "desktop_platform.hpp"
#include "../engine.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdlib>

namespace mc::desktop {

DesktopPlatform::DesktopPlatform() = default;
DesktopPlatform::~DesktopPlatform() { shutdown(); }

bool DesktopPlatform::init() {
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window_ = glfwCreateWindow(1280, 720, "Badgate", nullptr, nullptr);
    if (!window_) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window_);

    glfwSwapInterval(1);   // VSync

    glfwSetWindowSizeLimits(window_, 640, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);

    // Главный фикс: viewport переставляется автоматически при ресайзе.
    // Без этого картинка остаётся в исходном 1280×720 в углу окна.
    glfwSetFramebufferSizeCallback(window_, +[](GLFWwindow* /*w*/, int fbW, int fbH) {
        if (fbW > 0 && fbH > 0) glViewport(0, 0, fbW, fbH);
    });

    glfwSetWindowUserPointer(window_, this);

    glfwSetMouseButtonCallback(window_, +[](GLFWwindow* w, int b, int a, int m) {
        static_cast<DesktopPlatform*>(glfwGetWindowUserPointer(w))->onMouseButton(b, a, m);
    });
    glfwSetCursorPosCallback(window_, +[](GLFWwindow* w, double x, double y) {
        static_cast<DesktopPlatform*>(glfwGetWindowUserPointer(w))->onCursorPos(x, y);
    });
    glfwSetKeyCallback(window_, +[](GLFWwindow* w, int k, int s, int a, int m) {
        static_cast<DesktopPlatform*>(glfwGetWindowUserPointer(w))->onKey(k, s, a, m);
    });
    glfwSetScrollCallback(window_, +[](GLFWwindow* w, double xo, double yo) {
        static_cast<DesktopPlatform*>(glfwGetWindowUserPointer(w))->onScroll(xo, yo);
    });
    glfwSetCharCallback(window_, +[](GLFWwindow* w, unsigned int cp) {
        static_cast<DesktopPlatform*>(glfwGetWindowUserPointer(w))->onChar(cp);
    });

    return true;
}

void DesktopPlatform::pollEvents(Engine& engine) {
    engine_ = &engine;
    syncCursorMode(engine.screen == Screen::InGame);
    glfwPollEvents();
    if (glfwWindowShouldClose(window_)) shouldClose_ = true;
}

bool DesktopPlatform::shouldClose() const { return shouldClose_; }
void DesktopPlatform::requestQuit()       { shouldClose_ = true; }
void* DesktopPlatform::nativeWindow()     { return window_; }

std::string DesktopPlatform::dataPath() const {
    if (const char* x = std::getenv("XDG_DATA_HOME")) return std::string(x) + "/badgate";
    if (const char* h = std::getenv("HOME"))          return std::string(h) + "/.local/share/badgate";
    if (const char* a = std::getenv("APPDATA"))       return std::string(a) + "/badgate";
    return "./data";
}

void DesktopPlatform::shutdown() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
    }
}

void DesktopPlatform::syncCursorMode(bool captured) {
    const int current = glfwGetInputMode(window_, GLFW_CURSOR);
    const int desired = captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
    if (current == desired) return;
    glfwSetInputMode(window_, GLFW_CURSOR, desired);
    if (captured) firstMouse_ = true;
}

void DesktopPlatform::recomputeKeyboardMove() {
    if (!engine_) return;
    const float x = (dHeld_ ? 1.0f : 0.0f) - (aHeld_ ? 1.0f : 0.0f);
    const float y = (sHeld_ ? 1.0f : 0.0f) - (wHeld_ ? 1.0f : 0.0f);
    engine_->input.setKeyboardMove(x, y);
}

// ---------------------------------------------------------------------------
// mouse
// ---------------------------------------------------------------------------

void DesktopPlatform::onMouseButton(int button, int action, int /*mods*/) {
    if (!engine_ || !engine_->gl.valid()) return;

    const bool captured =
        glfwGetInputMode(window_, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    const bool pressed = (action == GLFW_PRESS);

    if (!captured) {
        // Меню: клики идут в MenuInput.
        double dx, dy;
        glfwGetCursorPos(window_, &dx, &dy);
        const float fx = static_cast<float>(dx);
        const float fy = static_cast<float>(dy);
        const int   sw = engine_->gl.width();
        const int   sh = engine_->gl.height();

        if (pressed) {
            engine_->input.onPointerDown(0, fx, fy, sw, sh);
            engine_->menuInput.onPointerDown(fx, fy);
        } else {
            engine_->input.onPointerUp(0, fx, fy);
            if (auto* hit = engine_->menuInput.onPointerUp(fx, fy, engine_->buttons))
                engine_->handleAction(*hit);
        }
        return;
    }

    // Игра: LMB=break, RMB=place (hold).
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        engine_->input.setBreakHold(pressed);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        engine_->input.setPlaceHold(pressed);
    }
}

void DesktopPlatform::onCursorPos(double x, double y) {
    if (!engine_ || !engine_->gl.valid()) return;

    const bool captured =
        glfwGetInputMode(window_, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;

    if (!captured) {
        engine_->input.onPointerMove(0, static_cast<float>(x), static_cast<float>(y));
        return;
    }

    if (firstMouse_) {
        lastMouseX_ = x; lastMouseY_ = y;
        firstMouse_ = false;
        return;
    }

    const float dx = static_cast<float>(x - lastMouseX_);
    const float dy = static_cast<float>(y - lastMouseY_);
    lastMouseX_ = x; lastMouseY_ = y;

    engine_->input.addLookDelta(dx, dy);
}

void DesktopPlatform::onScroll(double /*xoff*/, double yoff) {
    if (!engine_ || engine_->screen != Screen::InGame) return;
    int slot = engine_->input.hotbarSlot();
    if (yoff > 0.0)      slot = (slot + 1) % 5;
    else if (yoff < 0.0) slot = (slot + 4) % 5;
    engine_->input.setInventoryHotbar(slot);
}

// ---------------------------------------------------------------------------
// keyboard
// ---------------------------------------------------------------------------

void DesktopPlatform::onKey(int key, int /*scancode*/, int action, int /*mods*/) {
    if (!engine_) return;

    const bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
    const bool edge    = (action == GLFW_PRESS);

    // ---- 1. Text entry mode ----
    if (engine_->textEntryActive()) {
        if (edge) {
            if (key == GLFW_KEY_BACKSPACE) { engine_->textEntryBackspace(); return; }
            if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) { engine_->textEntryDone(); return; }
            if (key == GLFW_KEY_ESCAPE)    { engine_->textEntryCancel();  return; }
        }
        return;
    }

    // ---- 2. F3: toggle debug overlay (universal, работает на любом экране) ----
    if (key == GLFW_KEY_F3 && edge) {
        engine_->toggleDebug();
        return;
    }

    // ---- 3. T: teleport (только в игре и только с включённым debug) ----
    if (key == GLFW_KEY_T && edge
        && engine_->debugVisible()
        && engine_->screen == Screen::InGame)
    {
        engine_->openTeleportCoords();
        return;
    }

    // ---- 4. F: toggle flight (только в игре) ----
    if (key == GLFW_KEY_F && edge && engine_->screen == Screen::InGame) {
        engine_->toggleFlying();
        return;
    }

    // ---- 5. Esc: pause / unpause ----
    if (key == GLFW_KEY_ESCAPE && edge) {
        if (engine_->screen == Screen::InGame) {
            engine_->screen = Screen::Paused;
            engine_->input.setBreakHold(false);
            engine_->input.setPlaceHold(false);
            // Сбрасываем held-клавиши, чтобы после unpause не было «залипания»
            wHeld_ = aHeld_ = sHeld_ = dHeld_ = false;
            recomputeKeyboardMove();
        } else if (engine_->screen == Screen::Paused) {
            engine_->screen = Screen::InGame;
        }
        return;
    }

    // ---- 6. 1..5: hotbar ----
    if (edge && key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
        int slot = key - GLFW_KEY_1;
        if (slot >= 0 && slot < 5) engine_->input.setInventoryHotbar(slot);
        return;
    }

    // ---- 7. Game-only keys: WASD / Space / Shift ----
    if (engine_->screen == Screen::InGame) {
        if (key == GLFW_KEY_W) { wHeld_ = pressed; recomputeKeyboardMove(); return; }
        if (key == GLFW_KEY_A) { aHeld_ = pressed; recomputeKeyboardMove(); return; }
        if (key == GLFW_KEY_S) { sHeld_ = pressed; recomputeKeyboardMove(); return; }
        if (key == GLFW_KEY_D) { dHeld_ = pressed; recomputeKeyboardMove(); return; }

        if (key == GLFW_KEY_SPACE) {
            engine_->input.setJumpHold(pressed);
            if (edge) engine_->input.triggerJump();   // one-shot для обычного прыжка
            return;
        }

        if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) {
            engine_->input.setCrouchHold(pressed);
            return;
        }
    }
    // ---- F11: fullscreen toggle (сохраняет/восстанавливает позицию/размер) ----
    if (key == GLFW_KEY_F11 && edge) {
        static int savedX = 0, savedY = 0, savedW = 1280, savedH = 720;
        GLFWmonitor* monitor = glfwGetWindowMonitor(window_);
        if (monitor) {
            // Выходим из fullscreen.
            glfwSetWindowMonitor(window_, nullptr, savedX, savedY, savedW, savedH, 0);
        } else {
            // Входим в fullscreen.
            glfwGetWindowPos(window_, &savedX, &savedY);
            glfwGetWindowSize(window_, &savedW, &savedH);
            GLFWmonitor* primary = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = primary ? glfwGetVideoMode(primary) : nullptr;
            if (mode) {
                glfwSetWindowMonitor(window_, primary, 0, 0,
                                     mode->width, mode->height, mode->refreshRate);
            }
        }
        return;
    }
    // ---- F10: TouchControl тумблер (в целях очисти от димона)
    if (key == GLFW_KEY_F10 && edge) {
        engine_->toggleTC();
        LOGI("Touch controls: %s", engine_->ShowTouchControls() ? "ON" : "OFF");
        return;
    }
}

void DesktopPlatform::onChar(unsigned int cp) {
    if (!engine_ || !engine_->textEntryActive()) return;
    if (cp > 0x7E) return;   // шрифт 5x7 — только ASCII

    char c = static_cast<char>(cp);
    const bool ok =
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
        c == ' ' || c == '.' || c == '-' || c == ':' || c == '/' || c == '+';
    if (ok) engine_->textEntryChar(c);
}

} // namespace mc::desktop
