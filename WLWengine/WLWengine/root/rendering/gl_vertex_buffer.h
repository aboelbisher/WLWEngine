#ifdef WLW_USE_GLFW

#pragma once
#include <vector>
#include <glad/glad.h>


#include "rendering/vertex_buffer.h"
#include "core/vertex_2d.h"
#include "core/vertex_3d.h"

namespace wlw::core {
class GLVertexBuffer : public WVertexBuffer {
public:
  GLVertexBuffer(const std::vector<Vertex3D>& vertices) {
    // 1. Generate and Bind VAO
    glGenVertexArrays(1, &m_VAO_);
    glBindVertexArray(m_VAO_);

    // 2. Generate and Bind VBO
    glGenBuffers(1, &m_VBO_);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO_);

    // 3. Upload Data
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex3D), vertices.data(), GL_STATIC_DRAW);

    // 4. Set Vertex Attributes (Layout matching the Vertex2D struct)
    // Attribute 0: Position (3 floats)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)0);
    glEnableVertexAttribArray(0);

    // Attribute 1: Color (4 floats)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

		// Attribute 2: Normal (3 floats)
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)(7 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // Attribute 3: UV (2 floats)
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)(10 * sizeof(float)));
    glEnableVertexAttribArray(3);

    // NOTE on VAO: The VAO is created, and the VBO layout is set here. 
    // The EBO (Index Buffer) will be bound separately during the draw call, 
    // as the VAO does NOT store EBO state in this implementation pattern.

    // 5. Unbind
    Unbind();
  } 


	GLVertexBuffer(const std::vector<Vertex2D>& vertices) {
    // 1. Generate and Bind VAO
    glGenVertexArrays(1, &m_VAO_);
    glBindVertexArray(m_VAO_);

    // 2. Generate and Bind VBO
    glGenBuffers(1, &m_VBO_);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO_);

    // 3. Upload Data
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex2D), vertices.data(), GL_STATIC_DRAW);

    // 4. Set Vertex Attributes (Layout matching the Vertex2D struct)
    // Attribute 0: Position (2 floats)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)0);
    glEnableVertexAttribArray(0);

    // Attribute 1: Color (4 floats)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // NOTE on VAO: The VAO is created, and the VBO layout is set here. 
    // The EBO (Index Buffer) will be bound separately during the draw call, 
    // as the VAO does NOT store EBO state in this implementation pattern.

    // 5. Unbind
    Unbind();
  }

  ~GLVertexBuffer() override {
    glDeleteBuffers(1, &m_VBO_);
    glDeleteVertexArrays(1, &m_VAO_);
  }

  void Bind() const override {
    glBindVertexArray(m_VAO_);
  }

  void Unbind() const override {
    glBindVertexArray(0);
  }

private:
	GLuint m_VBO_ = 0; // Vertex Buffer Object ID
	GLuint m_VAO_ = 0; // Vertex Array Object ID

};

} // wlw:core

#endif // WLW_USE_GLFW