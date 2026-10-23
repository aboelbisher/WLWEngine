#pragma once

#include "wlw/rendering/gl.h"
#include "wlw/rendering/render_target.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/texture.h"

// The GPU side of textures, shaders and render targets on GL. Games make those as plain objects
// (texture.h, shader.h, render_target.h); the renderers call these, which make the GL object the
// first time it's needed, keep it in the resource (Resource::SetGpu) and bring it up to date (a
// texture's Update, a target's new size). Need the GL context.
namespace wlw::rendering {

class GLShader;

// The texture's GL name, uploaded / reallocated as needed.
GLuint GLTexture(const WTexture& texture);
// Binds it to texture unit `unit` (leaves that unit active).
void BindGLTexture(const WTexture& texture, unsigned int unit);

// The shader's program, compiled the first time; null if it doesn't compile (logged once; the
// Shader is then marked invalid).
const GLShader* GLShaderFor(const Shader& shader);

// The target's framebuffer, at the target's current size (reallocated when it changed).
GLuint GLFramebuffer(RenderTarget& target);

// Whether the GPU can draw into this format: the 8-bit ones always; R16F / RGBA16F need
// EXT_color_buffer_half_float (or EXT_color_buffer_float) on GLES 3.0.
bool CanRenderTo(TextureFormat format);

// Whether the GL context has this extension (e.g. "GL_EXT_disjoint_timer_query").
bool HasGLExtension(const char* extension);

// Makes the GPU side of every resource alive now (Resource::InternalAccess::ForEachAlive): compiles shaders and
// uploads textures, so the first frames don't stall on it. Afterwards, a shader compiled when first
// drawn is logged ("compiled during play"). Returns how many shaders it compiled.
int PrepareAliveResources();

}  // namespace wlw::rendering
