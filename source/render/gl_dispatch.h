#pragma once

#include <QOpenGLContext>

// Qt can select desktop OpenGL or ANGLE at process start. The Windows import
// library still exposes desktop OpenGL symbols, so calling ::gl* directly while
// an ANGLE context is current dispatches into opengl32 with no matching current
// context. Keep all renderer-owned core calls behind this small gateway: desktop
// retains the native path, while GLES resolves the entry point from Qt's current
// context. Fixed-function entry points are intentionally desktop-only.
namespace ei {
namespace gl {

inline bool usesGles()
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    return context && context->isOpenGLES();
}

template <typename Proc>
inline Proc glesProc(const char* name)
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    return context ? reinterpret_cast<Proc>(context->getProcAddress(name)) : nullptr;
}

#define EI_GL_DISPATCH_VOID(name, params, args) \
    inline void name params \
    { \
        if(!usesGles()) { ::gl##name args; return; } \
        using Proc = void (APIENTRY*) params; \
        static Proc proc = nullptr; \
        if(!proc) proc = glesProc<Proc>("gl" #name); \
        if(proc) proc args; \
    }

EI_GL_DISPATCH_VOID(Viewport, (GLint x, GLint y, GLsizei width, GLsizei height), (x, y, width, height))
EI_GL_DISPATCH_VOID(Clear, (GLbitfield mask), (mask))
EI_GL_DISPATCH_VOID(ClearColor, (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha), (red, green, blue, alpha))
EI_GL_DISPATCH_VOID(ColorMask, (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha), (red, green, blue, alpha))
EI_GL_DISPATCH_VOID(DepthFunc, (GLenum func), (func))
EI_GL_DISPATCH_VOID(DepthMask, (GLboolean flag), (flag))
EI_GL_DISPATCH_VOID(BlendFunc, (GLenum source, GLenum destination), (source, destination))
EI_GL_DISPATCH_VOID(LineWidth, (GLfloat width), (width))
EI_GL_DISPATCH_VOID(BindTexture, (GLenum target, GLuint texture), (target, texture))
EI_GL_DISPATCH_VOID(DeleteTextures, (GLsizei count, const GLuint* textures), (count, textures))
EI_GL_DISPATCH_VOID(TexParameteri, (GLenum target, GLenum name, GLint value), (target, name, value))
EI_GL_DISPATCH_VOID(PixelStorei, (GLenum name, GLint value), (name, value))
EI_GL_DISPATCH_VOID(DrawArrays, (GLenum mode, GLint first, GLsizei count), (mode, first, count))
EI_GL_DISPATCH_VOID(DrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices), (mode, count, type, indices))
EI_GL_DISPATCH_VOID(ReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels), (x, y, width, height, format, type, pixels))
EI_GL_DISPATCH_VOID(GetBooleanv, (GLenum name, GLboolean* value), (name, value))
EI_GL_DISPATCH_VOID(GetFloatv, (GLenum name, GLfloat* value), (name, value))
EI_GL_DISPATCH_VOID(GetIntegerv, (GLenum name, GLint* value), (name, value))

#undef EI_GL_DISPATCH_VOID

inline bool unsupportedFixedFunctionCapability(GLenum cap)
{
    if(!usesGles())
        return false;
    // OpenGL ES 2 implements alpha rejection in the fragment shader and has
    // no fixed-function framebuffer-sRGB switch. Passing either desktop-only
    // enum to glEnable/glDisable poisons the ANGLE error state.
    return cap == GL_ALPHA_TEST || cap == GL_FRAMEBUFFER_SRGB;
}

inline void Enable(GLenum cap)
{
    if(unsupportedFixedFunctionCapability(cap))
        return;
    if(!usesGles()) {
        ::glEnable(cap);
        return;
    }
    using Proc = void (APIENTRY*)(GLenum);
    static Proc proc = nullptr;
    if(!proc) proc = glesProc<Proc>("glEnable");
    if(proc) proc(cap);
}

inline void Disable(GLenum cap)
{
    if(unsupportedFixedFunctionCapability(cap))
        return;
    if(!usesGles()) {
        ::glDisable(cap);
        return;
    }
    using Proc = void (APIENTRY*)(GLenum);
    static Proc proc = nullptr;
    if(!proc) proc = glesProc<Proc>("glDisable");
    if(proc) proc(cap);
}

inline GLenum GetError()
{
    if(!usesGles())
        return ::glGetError();
    using Proc = GLenum (APIENTRY*)();
    static Proc proc = nullptr;
    if(!proc) proc = glesProc<Proc>("glGetError");
    return proc ? proc() : GL_NO_ERROR;
}

inline const GLubyte* GetString(GLenum name)
{
    if(!usesGles())
        return ::glGetString(name);
    using Proc = const GLubyte* (APIENTRY*)(GLenum);
    static Proc proc = nullptr;
    if(!proc) proc = glesProc<Proc>("glGetString");
    return proc ? proc(name) : nullptr;
}

inline GLboolean IsEnabled(GLenum cap)
{
    if(!usesGles())
        return ::glIsEnabled(cap);
    using Proc = GLboolean (APIENTRY*)(GLenum);
    static Proc proc = nullptr;
    if(!proc) proc = glesProc<Proc>("glIsEnabled");
    return proc ? proc(cap) : GL_FALSE;
}

// Compatibility-profile operations have no GLES equivalent. All call sites
// are guarded, but these no-op GLES branches make that boundary explicit.
inline void AlphaFunc(GLenum func, GLclampf reference)
{
    if(!usesGles()) ::glAlphaFunc(func, reference);
}
inline void Frustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                    GLdouble nearPlane, GLdouble farPlane)
{
    if(!usesGles()) ::glFrustum(left, right, bottom, top, nearPlane, farPlane);
}
inline void Ortho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
                  GLdouble nearPlane, GLdouble farPlane)
{
    if(!usesGles()) ::glOrtho(left, right, bottom, top, nearPlane, farPlane);
}
inline void LineStipple(GLint factor, GLushort pattern)
{
    if(!usesGles()) ::glLineStipple(factor, pattern);
}

} // namespace gl
} // namespace ei
