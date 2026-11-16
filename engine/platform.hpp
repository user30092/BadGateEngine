#pragma once
#include <string>

namespace mc {

class Engine;   // fwd

// Хост = платформенный бэкенд. Единственное место, которое знает про ОС.
class IHost {
public:
    virtual ~IHost() = default;

    // Прокачать ОС-события и раздать их в engine (инпут, lifecycle).
    // Должен быть неблокирующим. shouldClose() выставляется платформой
    // при APP_CMD_DESTROY / закрытии окна.
    virtual void pollEvents(Engine& engine) = 0;

    virtual bool shouldClose() const = 0;
    virtual void requestQuit()       = 0;

    // Непрозрачный хендл окна для GLContext::init(void*).
    // Android вернёт ANativeWindow*, desktop — GLFWwindow*.
    // nullptr, если окно ещё не готово (Android lifecycle).
    virtual void* nativeWindow() = 0;

    // Корень для saves/ и settings.cfg.
    virtual std::string dataPath() const = 0;

    virtual void shutdown() = 0;
};

} // namespace mc
