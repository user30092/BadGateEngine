// android_platform.cpp
#include "android_platform.hpp"
#include "engine.hpp"

#include <android/input.h>
#include <android/looper.h>
#include <android/native_window.h>
#include "glue/android_native_app_glue.h"

#include <cstring>

namespace mc::android {

AndroidPlatform::AndroidPlatform(android_app* app) : app_(app) {
    app_->userData     = this;
    app_->onAppCmd     = &AndroidPlatform::onCmd;
    app_->onInputEvent = &AndroidPlatform::onInput;
}

AndroidPlatform::~AndroidPlatform() = default;

void AndroidPlatform::pollEvents(Engine& engine) {
    engine_ = &engine;
    int events;
    android_poll_source* source;
    // 0-таймаут, если окно уже есть; -1 — ждём первое окно.
    int timeout = windowReady_.load() ? 0 : -1;
    while (ALooper_pollOnce(timeout, nullptr, &events,
                            reinterpret_cast<void**>(&source)) >= 0) {
        if (source) source->process(app_, source);
        if (app_->destroyRequested) { running_ = false; break; }
        timeout = 0;   // после первого poll — не блокируемся
    }
}

void* AndroidPlatform::nativeWindow() {
    return windowReady_.load() ? static_cast<void*>(pendingWindow_) : nullptr;
}

std::string AndroidPlatform::dataPath() const {
    return app_->activity->internalDataPath;
}

void AndroidPlatform::shutdown() {}

// ---- glue callbacks ----

void AndroidPlatform::onCmd(android_app* app, int32_t cmd) {
    auto* self = static_cast<AndroidPlatform*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            self->pendingWindow_ = app->window;
            self->windowReady_   = true;
            break;
        case APP_CMD_TERM_WINDOW:
            self->windowReady_   = false;
            // NB: EGL surface нужно пересоздать при следующем INIT_WINDOW.
            break;
        case APP_CMD_DESTROY:
            self->running_ = false;
            break;
        default: break;
    }
}

int32_t AndroidPlatform::onInput(android_app* app, AInputEvent* event) {
    auto* self   = static_cast<AndroidPlatform*>(app->userData);
    auto* engine = self->engine_;
    if (!engine || !engine->gl.valid()) return 1;

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        int32_t mask   = action & AMOTION_EVENT_ACTION_MASK;
        int32_t idx    = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                          >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        if (mask == AMOTION_EVENT_ACTION_MOVE) {
            size_t n = AMotionEvent_getPointerCount(event);
            for (size_t i = 0; i < n; ++i)
                engine->input.onPointerMove(AMotionEvent_getPointerId(event, i),
                                            AMotionEvent_getX(event, i),
                                            AMotionEvent_getY(event, i));
            return 1;
        }

        if (mask == AMOTION_EVENT_ACTION_CANCEL) {
            engine->input.onPointerCancel();
            engine->menuInput.onPointerCancel();
            return 1;
        }
        if (idx >= (int32_t)AMotionEvent_getPointerCount(event)) return 1;

        int32_t id = AMotionEvent_getPointerId(event, idx);
        float   x  = AMotionEvent_getX(event, idx);
        float   y  = AMotionEvent_getY(event, idx);
        int     sw = engine->gl.width();
        int     sh = engine->gl.height();

        if (mask == AMOTION_EVENT_ACTION_DOWN ||
            mask == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            if (engine->handlePauseTap(x, y, /*isDown=*/true)) return 1;
            engine->input.onPointerDown(id, x, y, sw, sh);
            engine->menuInput.onPointerDown(x, y);
            return 1;
        }

        if (mask == AMOTION_EVENT_ACTION_UP ||
            mask == AMOTION_EVENT_ACTION_POINTER_UP) {
            if (engine->handlePauseTap(x, y, /*isDown=*/false)) return 1;
            engine->input.onPointerUp(id, x, y);
            if (auto* hit = engine->menuInput.onPointerUp(x, y, engine->buttons))
                engine->handleAction(*hit);
            return 1;
        }
    }
    return 1;
}

} // namespace mc::android
