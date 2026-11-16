#include "entity_renderer.hpp"
#include "../core/log.hpp"
#include <cmath>

namespace mc {

namespace {

// Добавляет коробку (6 граней, по 2 треугольника) в локальных координатах.
// UV не используются (шейдер болванчика не семплит текстуру), но Vertex
// требует эти поля — заполняем нулями, чтобы не трогать Mesh::upload().
void addBox(std::vector<Vertex>& out, float x0, float y0, float z0,
            float x1, float y1, float z1) {
    struct Face { float nx, ny, nz; float v[4][3]; };
    const Face faces[6] = {
        { 1, 0, 0, {{x1,y0,z1},{x1,y0,z0},{x1,y1,z0},{x1,y1,z1}} },
        {-1, 0, 0, {{x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}} },
        { 0, 1, 0, {{x0,y1,z1},{x1,y1,z1},{x1,y1,z0},{x0,y1,z0}} },
        { 0,-1, 0, {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}} },
        { 0, 0, 1, {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}} },
        { 0, 0,-1, {{x1,y0,z0},{x0,y0,z0},{x0,y1,z0},{x1,y1,z0}} },
    };
    for (auto& f : faces) {
        Vertex v[4];
        for (int i = 0; i < 4; ++i) {
            v[i] = {f.v[i][0], f.v[i][1], f.v[i][2], 0.0f, 0.0f, f.nx, f.ny, f.nz};
        }
        out.push_back(v[0]); out.push_back(v[1]); out.push_back(v[2]);
        out.push_back(v[0]); out.push_back(v[2]); out.push_back(v[3]);
    }
}

} // namespace

static const char* ENTITY_VS = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec3 aNormal;

uniform mat4 uMVP;

out vec3 vNormal;

void main() {
    vNormal = aNormal;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* ENTITY_FS = R"(
precision mediump float;

in vec3 vNormal;
uniform vec3 uColor;
uniform vec3 uLightDir;

out vec4 FragColor;

void main() {
    float diff = max(dot(normalize(vNormal), normalize(-uLightDir)), 0.0);
    float light = 0.4 + 0.6 * diff;
    FragColor = vec4(uColor * light, 1.0);
}
)";

bool EntityRenderer::init() {
    if (!shader.build(ENTITY_VS, ENTITY_FS)) {
        LOGE("EntityRenderer shader failed");
        return false;
    }

    // Локальные координаты: таз игрока в (0,0,0), ноги начинаются в y=0 —
    // так Model-матрица = translate(позиция игрока) без доп. смещения.
    // Размеры примерно как у Player (collision box 0.6 x 1.8, см. player.hpp).
    std::vector<Vertex> verts;
    addBox(verts, -0.3f, 0.0f, -0.15f,  0.3f, 1.2f,  0.15f); // тело + ноги
    addBox(verts, -0.25f, 1.2f, -0.25f, 0.25f, 1.7f, 0.25f); // голова
    bodyMesh.upload(verts);
    return true;
}

void EntityRenderer::render(const Mat4& viewProj, const std::vector<RemoteEntityView>& others) {
    if (others.empty() || bodyMesh.empty()) return;

    shader.use();
    shader.setVec3("uLightDir", Vec3{-0.4f, -1.0f, -0.3f}.normalized());

    // Нет настоящих скинов — различаем игроков цветом, выбранным по id.
    static const Vec3 PALETTE[6] = {
        {0.85f, 0.25f, 0.25f}, {0.25f, 0.55f, 0.90f}, {0.30f, 0.80f, 0.30f},
        {0.90f, 0.70f, 0.20f}, {0.70f, 0.30f, 0.85f}, {0.30f, 0.85f, 0.80f},
    };

    for (auto& e : others) {
        Mat4 model = Mat4::translate({e.x, e.y, e.z}) * Mat4::rotateY(e.yaw * DEG2RAD);
        shader.setMat4("uMVP", viewProj * model);
        shader.setVec3("uColor", PALETTE[e.id % 6]);
        bodyMesh.draw();
    }
}

} // namespace mc
