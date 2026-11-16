#include "gl_context.hpp"
#include "../core/log.hpp"

#ifdef __ANDROID__
#  include <EGL/egl.h>
#  include <android/native_window.h>
#else
#  define GLFW_INCLUDE_NONE
#  include <GLFW/glfw3.h>
#endif

namespace mc {

#ifdef __ANDROID__

bool GLContext::init(void* nw) {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) { LOGE("No EGL display"); return false; }

    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) { LOGE("eglInitialize failed"); return false; }
    LOGI("EGL %d.%d", major, minor);

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };
    EGLConfig config; EGLint numConfigs;
    eglChooseConfig(display, attribs, &config, 1, &numConfigs);
    if (numConfigs == 0) { LOGE("No EGL config"); return false; }

    EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttribs);
    if (context == EGL_NO_CONTEXT) { LOGE("No EGL context"); return false; }

    EGLSurface surface = eglCreateWindowSurface(display, config,
                                                static_cast<ANativeWindow*>(nw), nullptr);
    if (surface == EGL_NO_SURFACE) { LOGE("No EGL surface"); return false; }

    if (!eglMakeCurrent(display, surface, surface, context)) {
        LOGE("eglMakeCurrent failed"); return false;
    }

    display_ = display;
    surface_ = surface;
    context_ = context;

    eglQuerySurface(display, surface, EGL_WIDTH,  &w_);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h_);
    LOGI("Surface %dx%d", w_, h_);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    valid_ = true;
    return true;
}

void GLContext::destroy() {
    if (!valid_) return;
    EGLDisplay display = static_cast<EGLDisplay>(display_);
    EGLContext context = static_cast<EGLContext>(context_);
    EGLSurface surface = static_cast<EGLSurface>(surface_);

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    eglTerminate(display);

    display_ = surface_ = context_ = nullptr;
    valid_ = false;
}

void GLContext::swapBuffers() {
    if (valid_) eglSwapBuffers(static_cast<EGLDisplay>(display_),
                               static_cast<EGLSurface>(surface_));
}

int GLContext::width()  const { return w_; }
int GLContext::height() const { return h_; }

#else // ============ Desktop (GLFW + GL 3.3 core) ============

bool GLContext::init(void* nw) {
    GLFWwindow* window = static_cast<GLFWwindow*>(nw);
    if (!window) return false;

    glfwMakeContextCurrent(window);

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        LOGE("glewInit failed: %s", glewGetErrorString(err));
        return false;
    }
    // GLEW на core-контексте иногда оставляет GL_INVALID_ENUM в error-state —
    // чистим, чтобы первый glGetError() в игре не словил эту «ошибку».
    while (glGetError() != GL_NO_ERROR) {}

    window_ = window;
    LOGI("OpenGL %s", glGetString(GL_VERSION));

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    valid_ = true;
    return true;
}

void GLContext::destroy() {
    // Окно и GL-контекст уничтожает DesktopPlatform через glfwDestroyWindow —
    // здесь только сбрасываем указатель, чтобы Mesh/Texture/Shader
    // деструкторы не дёрнули GL-функции без текущего контекста.
    window_ = nullptr;
    valid_ = false;
}

void GLContext::swapBuffers() {
    if (valid_ && window_) glfwSwapBuffers(static_cast<GLFWwindow*>(window_));
}

int GLContext::width() const {
    if (!window_) return 0;
    int w, h;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(window_), &w, &h);
    return w;
}
int GLContext::height() const {
    if (!window_) return 0;
    int w, h;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(window_), &w, &h);
    return h;
}

#endif

} // namespace mc
