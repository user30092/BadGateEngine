#include "android_main.hpp"
#include "android_platform.hpp"
#include "..engine.hpp"
#include "../main_loop.hpp"

#include <android/looper.h>
#include "glue/android_native_app_glue.h"

void android_main(android_app* app) {
    mc::android::AndroidPlatform platform(app);
    mc::Engine engine;
    engine->ShowTouchControls() = true;
    mc::runMainLoop(platform, engine);
}
