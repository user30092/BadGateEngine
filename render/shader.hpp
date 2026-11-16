#pragma once
#include "gl_headers.hpp"
#include "../core/math.hpp"

namespace mc {

class Shader {
public:
    ~Shader();
    bool build(const char* vs, const char* fs);
    void use() const { glUseProgram(program); }
    void setMat4 (const char* name, const Mat4& m) const;
    void setVec3 (const char* name, const Vec3& v) const;
    void setFloat(const char* name, float v) const;
    void setInt  (const char* name, int v) const;
    GLuint id() const { return program; }

private:
    GLuint program = 0;
};

} // namespace mc
