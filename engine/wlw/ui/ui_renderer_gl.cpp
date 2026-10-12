#ifdef WLW_RENDERER_GL

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/gl.h"
#include "wlw/rendering/gl_blend.h"
#include "wlw/rendering/gl_shader.h"
#include "wlw/ui/ui_renderer.h"

namespace wlw::ui {

namespace {

constexpr const char* kVertexShader = R"(
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;
layout (location = 3) in vec4 aParams;
uniform mat4 uProjection;
out vec2 vUV;
out vec4 vColor;
out vec4 vParams;
void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
    vParams = aParams;
}
)";

// params.x picks the kind of quad:
//   1 = SDF text: the atlas stores distance to the glyph edge (0.5 = edge); params.y is where to
//       cut (lower = thicker, for outlines). fwidth() = how much the value changes per screen
//       pixel, so the edge is anti-aliased over ~1 pixel at any text size.
//   2 = rectangle with rounded corners: uv = position from the centre, params.yz = half size,
//       params.w = radius. The signed distance to a rounded box is computed per pixel.
//   4 = image: the bound texture (straight alpha, like image files) times the vertex colour.
// Output is premultiplied alpha (rgb already multiplied by alpha): blends correctly, also when
// layers of semi-transparent UI overlap.
constexpr const char* kFragmentShader = R"(
in vec2 vUV;
in vec4 vColor;
in vec4 vParams;
uniform sampler2D uAtlas;
out vec4 FragColor;
void main() {
    if (vParams.x > 3.5) {
        vec4 texel = texture(uAtlas, vUV) * vColor;
        FragColor = vec4(texel.rgb * texel.a, texel.a);
        return;
    }
    float coverage;
    if (vParams.x < 1.5) {
        float d = texture(uAtlas, vUV).r;
        float w = max(fwidth(d) * 0.7, 1e-4);
        coverage = smoothstep(vParams.y - w, vParams.y + w, d);
    } else {
        vec2 q = abs(vUV) - vParams.yz + vParams.w;
        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vParams.w;
        coverage = clamp(0.5 - d / max(fwidth(d), 1e-4), 0.0, 1.0);
    }
    float a = vColor.a * coverage;
    FragColor = vec4(vColor.rgb * a, a);
}
)";

GLuint Compile(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  const char* sources[] = {rendering::kGLSLHeader, source};
  glShaderSource(shader, 2, sources, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    WLW_LOG_ERROR("UI shader failed to compile:\n%s", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

class GLUIRenderer : public UIRenderer {
 public:
  GLUIRenderer() {
    const GLuint vs = Compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fs = Compile(GL_FRAGMENT_SHADER, kFragmentShader);
    program_ = glCreateProgram();
    if (vs) glAttachShader(program_, vs);
    if (fs) glAttachShader(program_, fs);
    glLinkProgram(program_);
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    projection_location_ = glGetUniformLocation(program_, "uProjection");
    atlas_location_ = glGetUniformLocation(program_, "uAtlas");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = sizeof(UIVertex);
    auto attribute = [&](GLuint location, GLint components, size_t offset) {
      glEnableVertexAttribArray(location);
      glVertexAttribPointer(location, components, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offset));
    };
    attribute(0, 2, offsetof(UIVertex, position));
    attribute(1, 2, offsetof(UIVertex, uv));
    attribute(2, 4, offsetof(UIVertex, color));
    attribute(3, 4, offsetof(UIVertex, params));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);  // recorded in the VAO
    glBindVertexArray(0);

    // Bound when a batch has no font (only rectangles): the shader never samples it then.
    const uint8_t white = 255;
    glGenTextures(1, &white_texture_);
    UploadR8(white_texture_, 1, 1, &white);
  }

  ~GLUIRenderer() override {
    if (!platform::HasCurrentGLContext()) return;
    glDeleteProgram(program_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
    glDeleteBuffers(1, &ibo_);
    glDeleteTextures(1, &white_texture_);
    for (auto& [_, texture] : atlas_textures_) glDeleteTextures(1, &texture);
  }

  void Draw(const Canvas& canvas, glm::vec2 framebuffer_pixels) override {
    if (canvas.QuadCount() == 0) return;
    EnsureIndices(canvas.QuadCount());

    // Stream this frame's vertices (orphan the old storage first: no waiting on the GPU).
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const auto bytes = static_cast<GLsizeiptr>(canvas.Vertices().size() * sizeof(UIVertex));
    glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, canvas.Vertices().data());

    // UI state: over everything, blended; restored for the 3D pass afterwards.
    glViewport(0, 0, static_cast<GLsizei>(framebuffer_pixels.x), static_cast<GLsizei>(framebuffer_pixels.y));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(program_);
    glUniformMatrix4fv(projection_location_, 1, GL_FALSE, glm::value_ptr(canvas.Projection()));
    glUniform1i(atlas_location_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    for (const UIBatch& batch : canvas.Batches()) {
      rendering::ApplyBlendMode(batch.blend);  // premultiplied output (see blend_mode.h)
      if (batch.material) {
        DrawShaderRect(canvas, batch);
        continue;
      }
      // The run's one texture on unit 0: a font atlas, an image, or (rectangles only) a white texel.
      GLuint texture = white_texture_;
      if (batch.font) texture = AtlasTexture(*batch.font);
      else if (batch.texture) texture = batch.texture->GetID();
      glBindTexture(GL_TEXTURE_2D, texture);
      DrawQuads(batch);
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
  }

 private:
  static void DrawQuads(const UIBatch& batch) {
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(batch.quad_count * 6), GL_UNSIGNED_INT,
                   reinterpret_cast<void*>(batch.first_quad * 6 * sizeof(uint32_t)));
  }

  // A Canvas::ShaderRect: its quad with the material's canvas shader, then back to the UI's.
  void DrawShaderRect(const Canvas& canvas, const UIBatch& batch) {
    const rendering::ShaderMaterial& material = *batch.material;
    if (!material.IsValid() || material.GetShader()->GetTarget() != rendering::Shader::Target::Canvas) return;
    const auto& shader = static_cast<const rendering::GLShader&>(*material.GetShader());
    const UIVertex* corners = &canvas.Vertices()[batch.first_quad * 4];  // top left, ..., bottom right = [2]
    const glm::vec2 size = (corners[2].position - corners[0].position) * canvas.PixelsPerUnit();
    glUseProgram(shader.Program());
    glUniformMatrix4fv(shader.Location("uProjection"), 1, GL_FALSE, glm::value_ptr(canvas.Projection()));
    glUniform2f(shader.Location("uSize"), size.x, size.y);
    shader.ApplyParams(material, 0);
    DrawQuads(batch);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
  }

  static void UploadR8(GLuint texture, int width, int height, const uint8_t* pixels) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // rows of single bytes aren't 4-byte aligned
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    // Linear filtering is what makes SDF text smooth: the distance is interpolated between texels.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  // Each font's atlas goes to the GPU once, the first time it's drawn.
  GLuint AtlasTexture(const Font& font) {
    auto [it, inserted] = atlas_textures_.try_emplace(font.Id(), 0);
    if (inserted) {
      glGenTextures(1, &it->second);
      const Font::Atlas& atlas = font.GetAtlas();
      UploadR8(it->second, atlas.width, atlas.height, atlas.pixels.data());
    }
    return it->second;
  }

  // Every quad is 2 triangles over its 4 vertices: 0-1-2, 0-2-3. The pattern never changes, so
  // the index buffer is only rebuilt when more quads than ever before are drawn.
  void EnsureIndices(size_t quads) {
    if (quads <= index_quads_) return;
    index_quads_ = quads * 2;
    std::vector<uint32_t> indices(index_quads_ * 6);
    for (size_t q = 0; q < index_quads_; ++q) {
      const auto v = static_cast<uint32_t>(q * 4);
      const uint32_t pattern[6] = {v, v + 1, v + 2, v, v + 2, v + 3};
      std::copy(pattern, pattern + 6, indices.begin() + q * 6);
    }
    glBindVertexArray(vao_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(),
                 GL_STATIC_DRAW);
    glBindVertexArray(0);
  }

  GLuint program_ = 0;
  GLint projection_location_ = -1;
  GLint atlas_location_ = -1;
  GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
  GLuint white_texture_ = 0;
  size_t index_quads_ = 0;
  std::unordered_map<uint32_t, GLuint> atlas_textures_;  // by Font::Id()
};

}  // namespace

std::unique_ptr<UIRenderer> UIRenderer::Create() { return std::make_unique<GLUIRenderer>(); }

}  // namespace wlw::ui

#endif  // WLW_RENDERER_GL
