#pragma once
#include "gl_headers.hpp"

namespace mc {

class GLContext {
public:
    bool init(void* nativeWindow);   // ANativeWindow* | GLFWwindow*
    void destroy();
    bool valid() const { return valid_; }

    void swapBuffers();
    int  width()  const;
    int  height() const;

private:
#ifdef __ANDROID__
    // EGLDisplay/EGLSurface/EGLContext — это все void*, поэтому держим
    // их как void* и не тянем <EGL/egl.h> в заголовок. В .cpp кастуем.
    void* display_ = nullptr;
    void* surface_ = nullptr;
    void* context_ = nullptr;
    int   w_ = 0;
    int   h_ = 0;
#else
    void* window_ = nullptr;   // GLFWwindow*
#endif
    bool valid_ = false;
};

} // namespace mc
