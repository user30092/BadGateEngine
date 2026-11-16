#pragma once
#include "gl_headers.hpp"
#include <vector>
#include <cstddef>

namespace mc {

struct Vertex {
    float x, y, z;    // позиция (локальная для чанка)
    float u, v;       // UV
    float nx, ny, nz; // нормаль
};

class Mesh {
public:
    ~Mesh();
    void upload(const std::vector<Vertex>& verts);
    void draw() const;
    bool empty() const { return count == 0; }
    GLsizei vertexCount() const { return count; }

private:
    GLuint  vao = 0, vbo = 0;
    GLsizei count = 0;
};

} // namespace mc
