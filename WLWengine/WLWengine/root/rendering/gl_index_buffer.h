#ifdef WLW_USE_GLFW


#pragma once

#include "rendering/index_buffer.h"
#include <glad/glad.h>

#include <vector>
#include <cstdint>

namespace wlw::core {
  class GLIndexBuffer : public WIndexBuffer {
  private:
    GLuint m_EBO = 0; // Element Buffer Object ID
    uint32_t m_Count = 0;

  public:
    GLIndexBuffer(const std::vector<uint32_t>& indices)
      : m_Count(indices.size()) {

      glGenBuffers(1, &m_EBO);
      // Bind the EBO to GL_ELEMENT_ARRAY_BUFFER to upload data
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_Count * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
      // Unbind
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    ~GLIndexBuffer() override {
      glDeleteBuffers(1, &m_EBO);
    }

    void Bind() const override {
      // Bind the EBO for the upcoming glDrawElements call
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    }

    void Unbind() const override {
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    //uint32_t GetCount() const override {
    //  return m_Count;
    //}
  };
} // wlw:core

#endif // WLW_USE_GLFW