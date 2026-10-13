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
#include "wlw/rendering/gl_program.h"
#include "wlw/rendering/gl_resources.h"
#include "wlw/rendering/gl_shader.h"
#include "wlw/ui/ui_renderer.h"

namespace wlw::ui {

namespace {

class GLUIRenderer : public UIRenderer {
 public:
  GLUIRenderer() {
    program_ = rendering::BuildEngineProgram("ui.vert", "ui.frag");  // shaders/ui.{vert,frag}
    projection_location_ = glGetUniformLocation(program_, "uProjection");
    atlas_location_ = glGetUniformLocation(program_, "uTexture");

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
      else if (batch.texture) texture = rendering::GLTexture(*batch.texture);
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
    const rendering::GLShader* program = rendering::GLShaderFor(*material.GetShader());  // compiled once
    if (!program) return;
    const rendering::GLShader& shader = *program;
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
