#pragma once
#include "mesh.hpp"
#include "shader.hpp"
#include "../core/math.hpp"
#include <vector>
#include <cstdint>

namespace mc {

// То, что EntityRenderer нужно знать про одного удалённого игрока, чтобы
// его нарисовать. Engine (main.cpp) собирает вектор таких записей из
// remotePlayers перед вызовом render().
struct RemoteEntityView {
    float x = 0, y = 0, z = 0;
    float yaw = 0;      // градусы — как и Player::yaw
    uint32_t id = 0;    // используется только для выбора цвета из палитры
};

// Рисует других игроков как простых блочных "болванчиков" (тело+голова,
// без анимации) — временная замена нормальной модели/скина, которых в
// проекте пока нет. Как только появится настоящий меш персонажа, этот
// класс можно заменить, не трогая протокол (PlayerMove уже шлёт всё
// необходимое — позицию и yaw/pitch).
class EntityRenderer {
public:
    bool init();
    void render(const Mat4& viewProj, const std::vector<RemoteEntityView>& others);

private:
    Shader shader;
    Mesh bodyMesh; // статичная геометрия, строится один раз в init()
};

} // namespace mc
