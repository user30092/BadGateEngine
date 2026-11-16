#include "shader.hpp"
#include "../core/log.hpp"

namespace mc {

namespace {

// GLES3 vs GL 3.3 core — разный синтаксис версии в первой строке.
// precision mediump float; в фрагментниках оставляем как есть: на desktop
// GLSL 1.30+ принимает precision-квалификаторы и молча их игнорирует
// (см. GLSL 4.60 §4.5.4). Так шейдер-сорсы остаются одни и те же.
const char* glslVersionHeader() {
#ifdef __ANDROID__
    return "#version 300 es\n";
#else
    return "#version 330 core\n";
#endif
}

GLuint compile(GLenum type, const char* body) {
    GLuint s = glCreateShader(type);
    const char* sources[2] = { glslVersionHeader(), body };
    glShaderSource(s, 2, sources, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("Shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace

Shader::~Shader() {
    if (program) glDeleteProgram(program);
}

bool Shader::build(const char* vs, const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return false;

    program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);

    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        LOGE("Program link error: %s", log);
        return false;
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return true;
}

void Shader::setMat4(const char* name, const Mat4& m) const {
    glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE, m.m);
}
void Shader::setVec3(const char* name, const Vec3& v) const {
    glUniform3f(glGetUniformLocation(program, name), v.x, v.y, v.z);
}
void Shader::setFloat(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(program, name), v);
}
void Shader::setInt(const char* name, int v) const {
    glUniform1i(glGetUniformLocation(program, name), v);
}

} // namespace mc
