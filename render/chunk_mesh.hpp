#pragma once
#include "mesh.hpp"
#include "mesh_data.hpp"

namespace mc {

class ChunkMesh {
public:
    // Вызывается ТОЛЬКО из главного потока (есть GL-контекст).
    void upload(const MeshData& data) { mesh.upload(data.vertices); }
    void draw() const { mesh.draw(); }
    bool empty() const { return mesh.empty(); }
    size_t vertexCount() const { return mesh.vertexCount(); }

private:
    Mesh mesh;
};

} // namespace mc
