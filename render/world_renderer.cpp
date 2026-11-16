#include "world_renderer.hpp"
#include "../core/log.hpp"
#include <vector>
#include <cmath>

namespace mc {

static const char* WORLD_VS = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec3 aNormal;

uniform mat4 uMVP;
uniform vec3 uChunkOffset;

out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec3 worldPos = aPos + uChunkOffset;
    vUV = aUV;
    vNormal = aNormal;
    vWorldPos = worldPos;
    gl_Position = uMVP * vec4(worldPos, 1.0);
}
)";

static const char* WORLD_FS = R"(
precision mediump float;

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uAtlas;
uniform vec3  uLightDir;
uniform vec3  uFogColor;
uniform float uFogNear;
uniform float uFogFar;
uniform vec3  uCamPos;

out vec4 FragColor;

void main() {
    vec4 tex = texture(uAtlas, vUV);
    if (tex.a < 0.1) discard;

    float diff = max(dot(normalize(vNormal), normalize(-uLightDir)), 0.0);
    float light = 0.35 + 0.65 * diff;
    if (vNormal.y > 0.5) light *= 1.1;

    vec3 color = tex.rgb * light;

    float dist = length(vWorldPos - uCamPos);
    float fog = clamp((dist - uFogNear) / (uFogFar - uFogNear), 0.0, 1.0);
    color = mix(color, uFogColor, fog);

    FragColor = vec4(color, tex.a);
}
)";

bool WorldRenderer::init() {
    if (!shader.build(WORLD_VS, WORLD_FS)) {
        LOGE("WorldRenderer shader failed");
        return false;
    }

    constexpr int ATLAS_SIZE = 256;
    std::vector<uint8_t> pixels(ATLAS_SIZE * ATLAS_SIZE * 4);
    buildDefaultAtlas(pixels.data(), ATLAS_SIZE);
    atlas.createFromPixels(pixels.data(), ATLAS_SIZE, ATLAS_SIZE);

    return true;
}

void WorldRenderer::invalidate(ChunkPos cx, ChunkPos cz) {
    meshes.erase(ChunkKey{cx, cz});
    meshes.erase(ChunkKey{cx - 1, cz});
    meshes.erase(ChunkKey{cx + 1, cz});
    meshes.erase(ChunkKey{cx, cz - 1});
    meshes.erase(ChunkKey{cx, cz + 1});
}

void WorldRenderer::clear() {
    meshes.clear();
}

void WorldRenderer::scheduleMesh(World& world, ChunkPos cx, ChunkPos cz,
                                 const Vec3& camPos) {
    const auto& loaded = world.loaded();
    auto it = loaded.find(ChunkKey{cx, cz});
    if (it == loaded.end()) return;

    ChunkPtr center = it->second;
    if (!center->isDirty()) return;

    auto findChunk = [&](ChunkPos x, ChunkPos z) -> ChunkPtr {
        auto f = loaded.find(ChunkKey{x, z});
        return (f == loaded.end()) ? nullptr : f->second;
    };
    ChunkPtr north = findChunk(cx, cz - 1);
    ChunkPtr south = findChunk(cx, cz + 1);
    ChunkPtr west  = findChunk(cx - 1, cz);
    ChunkPtr east  = findChunk(cx + 1, cz);

    if (!north || !south || !west || !east) return;

    MeshTask task;
    task.center = std::move(center);
    task.north  = std::move(north);
    task.south  = std::move(south);
    task.west   = std::move(west);
    task.east   = std::move(east);
    task.centerRevision = task.center->revision();

    float wx = static_cast<float>(cx) * CHUNK_SIZE + CHUNK_SIZE * 0.5f;
    float wz = static_cast<float>(cz) * CHUNK_SIZE + CHUNK_SIZE * 0.5f;
    float dx = wx - camPos.x;
    float dz = wz - camPos.z;
    task.priority = dx * dx + dz * dz;

    meshPool.request(std::move(task));
}

void WorldRenderer::applyReadyMeshes(World& world) {
    std::vector<MeshDataPtr> ready;
    ready.reserve(8);
    meshPool.drainReady(ready, 8);

    for (auto& data : ready) {
        ChunkKey key{data->cx, data->cz};
        auto it = world.loaded().find(key);
        if (it == world.loaded().end()) {
            meshes.erase(key);
            continue;
        }
        Chunk& chunk = *it->second;

        // Быстрая проверка: устарел ли меш.
        if (chunk.revision() != data->revision) continue;

        // Применяем: обновляем/создаём GL-объект.
        auto mit = meshes.find(key);
        if (mit == meshes.end()) {
            mit = meshes.emplace(key, std::make_unique<ChunkMesh>()).first;
        }
        mit->second->upload(*data);

        // Атомарно сбрасываем dirty, если revision не изменился за upload.
        // Если поменялся — следующий кадр перестроит.
        chunk.markCleanIfRevision(data->revision);
    }
}

void WorldRenderer::cleanupOrphanMeshes(World& world) {
    const auto& loaded = world.loaded();
    for (auto it = meshes.begin(); it != meshes.end(); ) {
        if (loaded.find(it->first) == loaded.end())
            it = meshes.erase(it);
        else
            ++it;
    }
}

void WorldRenderer::render(World& world, const Mat4& viewProj, const Vec3& camPos,
                            int renderDistanceChunks) {
    // 1. Забрать готовые меши.
    applyReadyMeshes(world);

    // 2. Раз в ~60 кадров вычистить меши для чанков, которых больше нет в мире.
    if (++cleanupCounter >= 60) {
        cleanupCounter = 0;
        cleanupOrphanMeshes(world);
    }

    // 3. Запланировать построение для dirty чанков в радиусе.
    int pcx = static_cast<int>(std::floor(camPos.x / CHUNK_SIZE));
    int pcz = static_cast<int>(std::floor(camPos.z / CHUNK_SIZE));
    int r2 = renderDistanceChunks * renderDistanceChunks;

    for (int dz = -renderDistanceChunks; dz <= renderDistanceChunks; ++dz)
    for (int dx = -renderDistanceChunks; dx <= renderDistanceChunks; ++dx) {
        if (dx * dx + dz * dz > r2) continue;
        scheduleMesh(world, pcx + dx, pcz + dz, camPos);
    }

    // 4. Извлечь frustum.
    Frustum frustum;
    frustum.extractFrom(viewProj);

    // 5. Настроить шейдер.
    shader.use();
    shader.setMat4("uMVP", viewProj);
    shader.setVec3("uLightDir", Vec3{-0.4f, -1.0f, -0.3f}.normalized());
    shader.setVec3("uFogColor", Vec3{0.6f, 0.75f, 0.95f});
    shader.setFloat("uFogNear", 30.0f);
    shader.setFloat("uFogFar", static_cast<float>(renderDistanceChunks * CHUNK_SIZE) * 0.95f);
    shader.setVec3("uCamPos", camPos);
    shader.setInt("uAtlas", 0);
    atlas.bind(0);

    drawnCount = 0;
    culledCount = 0;

    // 6. Отрисовка: distance + frustum culling.
    for (auto& [key, mesh] : meshes) {
        if (mesh->empty()) continue;

        int ddx = key.x - pcx;
        int ddz = key.z - pcz;
        if (ddx * ddx + ddz * ddz > r2) continue;

        AABB box = chunkAABB(key.x, key.z);
        if (!frustum.intersects(box)) {
            ++culledCount;
            continue;
        }

        Vec3 offset{
            static_cast<float>(key.x * CHUNK_SIZE),
            0.0f,
            static_cast<float>(key.z * CHUNK_SIZE)
        };
        shader.setVec3("uChunkOffset", offset);
        mesh->draw();
        ++drawnCount;
    }
}

} // namespace mc
