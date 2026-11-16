#pragma once
#include "shader.hpp"
#include "texture.hpp"
#include "chunk_mesh.hpp"
#include "mesh_worker_pool.hpp"
#include "../world/world.hpp"
#include "../core/math.hpp"
#include "../core/frustum.hpp"
#include <unordered_map>
#include <memory>

namespace mc {

class WorldRenderer {
public:
    bool init();
    void render(World& world, const Mat4& viewProj, const Vec3& camPos,
                int renderDistanceChunks);
    void invalidate(ChunkPos cx, ChunkPos cz);
    void clear();

    size_t meshCount()      const { return meshes.size(); }
    size_t meshPending()    const { return meshPool.pendingCount(); }
    size_t meshInFlight()   const { return meshPool.inFlightCount(); }
    size_t drawnLastFrame() const { return drawnCount; }
    size_t culledLastFrame()const { return culledCount; }

private:
    Shader shader;
    Texture atlas;
    MeshWorkerPool meshPool;

    // Ключ — mc::ChunkKey (единый тип из world/chunk.hpp).
    std::unordered_map<ChunkKey, std::unique_ptr<ChunkMesh>, ChunkKeyHash> meshes;

    size_t drawnCount  = 0;
    size_t culledCount = 0;
    int    cleanupCounter = 0;

    void scheduleMesh(World& world, ChunkPos cx, ChunkPos cz,
                      const Vec3& camPos);
    void applyReadyMeshes(World& world);
    void cleanupOrphanMeshes(World& world);
};

} // namespace mc
