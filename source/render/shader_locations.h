#pragma once

#include <QHash>
#include <QOpenGLShaderProgram>
#include <QThread>

namespace ei {
// glGetUniformLocation/glGetAttribLocation are driver queries, even when Qt's
// name-based setter looks like a simple assignment. Keep locations for one
// linked program, including -1 for optimized-out inputs. The cache belongs to
// the program's thread and is discarded on relink or QObject destruction.
struct SShaderLocations
{
    GLuint id{0};
    QHash<QByteArray, int> uniforms;
    QHash<QByteArray, int> attributes;
};
inline QHash<QOpenGLShaderProgram*, SShaderLocations>& shaderLocationStore()
{
    // Destruction callbacks may run during late Qt cleanup. Keep the empty
    // registry alive; each program's entries are removed with its QObject.
    static thread_local auto* store = new QHash<QOpenGLShaderProgram*, SShaderLocations>;
    return *store;
}
inline void invalidateShaderLocations(QOpenGLShaderProgram* program)
{
    auto& store = shaderLocationStore();
    auto found = store.find(program);
    if(found != store.end()) found.value() = SShaderLocations();
}
inline bool linkShaderProgram(QOpenGLShaderProgram& program)
{
    invalidateShaderLocations(&program);
    return program.link();
}
inline int shaderLocation(QOpenGLShaderProgram* program, const char* name, bool attribute)
{
    if(!program) return -1;
    if(!program->isLinked() || program->thread() != QThread::currentThread())
        return attribute ? program->attributeLocation(name) : program->uniformLocation(name);
    auto& store = shaderLocationStore();
    auto found = store.find(program);
    if(found == store.end())
    {
        found = store.insert(program, SShaderLocations());
        auto* owner = &store;
        QObject::connect(program, &QObject::destroyed,
                         [owner, program]() { owner->remove(program); });
    }
    auto& locations = found.value();
    if(locations.id != program->programId())
    {
        locations = SShaderLocations();
        locations.id = program->programId();
    }
    auto& names = attribute ? locations.attributes : locations.uniforms;
    const QByteArray key(name);
    const auto cached = names.constFind(key);
    if(cached != names.cend()) return cached.value();
    const int location = attribute ? program->attributeLocation(name) : program->uniformLocation(name);
    names.insert(key, location);
    return location;
}
inline int uniformLocation(QOpenGLShaderProgram* program, const char* name)
{ return shaderLocation(program, name, false); }
inline int attributeLocation(QOpenGLShaderProgram* program, const char* name)
{ return shaderLocation(program, name, true); }
}
