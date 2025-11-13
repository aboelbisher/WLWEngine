#include "triangle_2d.h"


namespace wlw::scene {

  const std::vector<core::Vertex2D>& Triangle2D::GetVertices() const {
    return vertices_;
  }
  
  const std::vector<uint32_t>& Triangle2D::GetIndices() const {
    return indices_;
  }


  //Triangle2D::Triangle2D() {}
  //  // 1. Define the vertices (still the same)
  //// X, Y, Z for each vertex
  //  float vertices[] = {
  //    // Position 
  //    -0.5f, -0.5f, 0.0f, // 0: Bottom Left
  //     0.5f, -0.5f, 0.0f, // 1: Bottom Right
  //     0.0f,  0.5f, 0.0f  // 2: Top
  //  };

  //  // **NEW**: Define the indices to reuse vertices
  //  unsigned int indices[] = {
  //      0, 1, 2  // This tells OpenGL to draw the triangle using vertices at index 0, 1, and 2
  //  };

  //  // --- Shaders (Remain the same for this basic example) ---
  //  // ... (The vertexShaderSource and fragmentShaderSource strings go here) ...
  //  // --------------------------------------------------------


  //  // 2. Setup VAO, VBO, and **EBO** (happens once)
  //  unsigned int VBO, VAO, EBO; // <-- EBO is new!
  //  glGenVertexArrays(1, &VAO);
  //  glGenBuffers(1, &VBO);
  //  glGenBuffers(1, &EBO); // <-- Generate EBO

  //  // Bind the VAO first
  //  glBindVertexArray(VAO);

  //  // Bind and populate the VBO (stores vertex data)
  //  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  //  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  //  // **NEW**: Bind and populate the EBO (stores index data)
  //  // Note the GL_ELEMENT_ARRAY_BUFFER target!
  //  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  //  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

  //  // Configure the Vertex Attributes
  //  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  //  glEnableVertexAttribArray(0);

  //  // Note: The EBO *must* remain bound while the VAO is bound, 
  //  // as the VAO stores the 'glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO)' call.

  //  // Unbind VAO and VBO for safety
  //  glBindVertexArray(0);
  //  glBindBuffer(GL_ARRAY_BUFFER, 0);
  //  // Note: We leave the EBO bound or unbind the VAO before the EBO.

  //  // 3. Compile and Link Shaders (The same function call as before)
  //  unsigned int shaderProgram = compileAndLinkShaders(vertexShaderSource, fragmentShaderSource);


  //  // --- The Main Rendering Loop ---
  //  while (!glfwWindowShouldClose(window))
  //  {
  //    // ... (Clear color and event handling) ...

  //    // 4. Draw the triangle using indices!
  //    glUseProgram(shaderProgram);
  //    glBindVertexArray(VAO);

  //    // **NEW DRAW CALL**: glDrawElements
  //    // We tell OpenGL:
  //    // - GL_TRIANGLES: Draw triangles
  //    // - 3: Draw 3 elements (indices)
  //    // - GL_UNSIGNED_INT: The type of data in the EBO
  //    // - (void*)0: The offset into the EBO buffer
  //    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, 0);

  //    // Swap buffers and poll events...
  //    glfwSwapBuffers(window);
  //    glfwPollEvents();
  //  }



} // namespace wlw::scene