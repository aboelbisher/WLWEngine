#pragma once

#include <vector>
#include "core/mesh.h"
#include "core/vertex_3d.h"

using namespace wlw;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

std::shared_ptr<core::Mesh<core::Vertex3D>> createPyramidFrustumWithNormals(float baseSize = 2.0f, float topSize = 1.0f, float height = 1.5f) {
  // Define half-sizes
  const float HB = baseSize / 2.0f;
  const float HT = topSize / 2.0f;
  const float HH = height / 2.0f;

  // Define the 8 UNIQUE POSITIONS first (for readability)
  const core::Vector3 P[8] = {
      {-HB, -HB, -HH}, {HB, -HB, -HH}, {HB, HB, -HH}, {-HB, HB, -HH}, // Base (0-3)
      {-HT, -HT, HH}, {HT, -HT, HH}, {HT, HT, HH}, {-HT, HT, HH}      // Top (4-7)
  };

  std::vector<core::Vertex3D> vertices;
  std::vector<uint32_t> indices;

  // UV Shorthand 
  const core::Vector2 UV_BL = { 0.0f, 0.0f }; // Bottom-Left
  const core::Vector2 UV_BR = { 1.0f, 0.0f }; // Bottom-Right
  const core::Vector2 UV_TR = { 1.0f, 1.0f }; // Top-Right
  const core::Vector2 UV_TL = { 0.0f, 1.0f }; // Top-Left

  // NOTE: C is a placeholder for the color attribute in the struct { P[i], C, N, UV }

  // --- 1. Bottom Face (Normal: 0, 0, -1) ---
  core::Vector3 N_Bottom = { 0.0f, 0.0f, -1.0f };
  vertices.push_back({ P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Bottom, UV_BL }); // 0
  vertices.push_back({ P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Bottom, UV_BR }); // 1
  vertices.push_back({ P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Bottom, UV_TR }); // 2

  vertices.push_back({ P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Bottom, UV_TR }); // 3
  vertices.push_back({ P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Bottom, UV_TL }); // 4
  vertices.push_back({ P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Bottom, UV_BL }); // 5

  // --- 2. Top Face (Normal: 0, 0, 1) ---
  core::Vector3 N_Top = { 0.0f, 0.0f, 1.0f };
  vertices.push_back({ P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Top, UV_BL }); // 6
  vertices.push_back({ P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Top, UV_TL }); // 7
  vertices.push_back({ P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Top, UV_TR }); // 8

  vertices.push_back({ P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Top, UV_TR }); // 9
  vertices.push_back({ P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Top, UV_BR }); // 10
  vertices.push_back({ P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Top, UV_BL }); // 11

  // --- 3. Side 1 (Face 0-1-5-4) ---
  // P[0] is BL, P[1] is BR, P[5] is TR (top-right), P[4] is TL (top-left)
  core::Vector3 V1_0 = P[1] - P[0];
  core::Vector3 V4_0 = P[4] - P[0];
  core::Vector3 N_Side1 = V1_0.Cross(V4_0).Normalize();

  // Triangle 1: 0, 1, 5
  vertices.push_back({ P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Side1, UV_BL }); // 12
  vertices.push_back({ P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Side1, UV_BR }); // 13
  vertices.push_back({ P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Side1, UV_TR }); // 14
  // Triangle 2: 5, 4, 0
  vertices.push_back({ P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Side1, UV_TR }); // 15
  vertices.push_back({ P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Side1, UV_TL }); // 16
  vertices.push_back({ P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Side1, UV_BL }); // 17

  // --- 4. Side 2 (Face 1-2-6-5) ---
  // P[1] is BL, P[2] is BR, P[6] is TR, P[5] is TL
  core::Vector3 V2_1 = P[2] - P[1];
  core::Vector3 V5_1 = P[5] - P[1];
  core::Vector3 N_Side2 = V2_1.Cross(V5_1).Normalize();

  // Triangle 1: 1, 2, 6
  vertices.push_back({ P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Side2, UV_BL }); // 18
  vertices.push_back({ P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Side2, UV_BR }); // 19
  vertices.push_back({ P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Side2, UV_TR }); // 20
  // Triangle 2: 6, 5, 1
  vertices.push_back({ P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Side2, UV_TR }); // 21
  vertices.push_back({ P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Side2, UV_TL }); // 22
  vertices.push_back({ P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Side2, UV_BL }); // 23

  // --- 5. Side 3 (Face 2-3-7-6) ---
  // P[2] is BL, P[3] is BR, P[7] is TR, P[6] is TL
  core::Vector3 V3_2 = P[3] - P[2];
  core::Vector3 V6_2 = P[6] - P[2];
  core::Vector3 N_Side3 = V3_2.Cross(V6_2).Normalize();

  // Triangle 1: 2, 3, 7
  vertices.push_back({ P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Side3, UV_BL }); // 24
  vertices.push_back({ P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Side3, UV_BR }); // 25
  vertices.push_back({ P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Side3, UV_TR }); // 26

  // Triangle 2: 7, 6, 2
  vertices.push_back({ P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Side3, UV_TR }); // 27
  vertices.push_back({ P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Side3, UV_TL }); // 28
  vertices.push_back({ P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Side3, UV_BL }); // 29


  // --- 6. Side 4 (Face 3-0-4-7) ---
  // P[3] is BL, P[0] is BR, P[4] is TR, P[7] is TL
  core::Vector3 V0_3 = P[0] - P[3];
  core::Vector3 V7_3 = P[7] - P[3];
  core::Vector3 N_Side4 = V0_3.Cross(V7_3).Normalize();

  // Triangle 1: 3, 0, 4
  vertices.push_back({ P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Side4, UV_BL }); // 30
  vertices.push_back({ P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Side4, UV_BR }); // 31
  vertices.push_back({ P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Side4, UV_TR }); // 32

  // Triangle 2: 4, 7, 3
  vertices.push_back({ P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Side4, UV_TR }); // 33
  vertices.push_back({ P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Side4, UV_TL }); // 34
  vertices.push_back({ P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Side4, UV_BL }); // 35


  // ... (indices and mesh_ setup remains the same) ...
  indices.resize(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) {
    indices[i] = static_cast<uint32_t>(i);
  }

  auto mesh_ = std::make_shared<core::Mesh<core::Vertex3D>>();
  mesh_->SetVertices(vertices);
  mesh_->SetIndices(indices);
  return mesh_;
}

std::shared_ptr<core::Mesh<core::Vertex3D>> createCubeWithNormals(float size = 1.0f) {
  const float HS = size / 2.0f;
  std::vector<core::Vertex3D> vertices;

  // UV Shorthand (used for clarity)
  const core::Vector2 UV_BL = { 0.0f, 0.0f }; // Bottom-Left
  const core::Vector2 UV_BR = { 1.0f, 0.0f }; // Bottom-Right
  const core::Vector2 UV_TR = { 1.0f, 1.0f }; // Top-Right
  const core::Vector2 UV_TL = { 0.0f, 1.0f }; // Top-Left

  // Standard color for the whole cube
  core::Color C = { 1.0f, 0.0f, 0.0f, 1.0f }; // Using a single color for simplicity

  // --- 1. FRONT FACE (+Z) ---
  const core::Vector3 N_Front = { 0.0f, 0.0f, 1.0f };
  // Triangle 1: Bottom-left, Bottom-right, Top-right
  vertices.push_back({ { -HS, -HS, HS }, C, N_Front, UV_BL }); // 0
  vertices.push_back({ { HS, -HS, HS }, C, N_Front, UV_BR }); // 1
  vertices.push_back({ { HS, HS, HS }, C, N_Front, UV_TR }); // 2

  // Triangle 2: Top-right, Top-left, Bottom-left
  vertices.push_back({ { HS, HS, HS }, C, N_Front, UV_TR }); // 3
  vertices.push_back({ { -HS, HS, HS }, C, N_Front, UV_TL }); // 4
  vertices.push_back({ { -HS, -HS, HS }, C, N_Front, UV_BL }); // 5

  // --- 2. BACK FACE (-Z) ---
  const core::Vector3 N_Back = { 0.0f, 0.0f, -1.0f };
  // Triangle 1: Bottom-right, Bottom-left, Top-left
  vertices.push_back({ { HS, -HS, -HS }, C, N_Back, UV_BR }); // 6
  vertices.push_back({ { -HS, -HS, -HS }, C, N_Back, UV_BL }); // 7
  vertices.push_back({ { -HS, HS, -HS }, C, N_Back, UV_TL }); // 8

  // Triangle 2: Top-left, Top-right, Bottom-right
  vertices.push_back({ { -HS, HS, -HS }, C, N_Back, UV_TL }); // 9
  vertices.push_back({ { HS, HS, -HS }, C, N_Back, UV_TR }); // 10
  vertices.push_back({ { HS, -HS, -HS }, C, N_Back, UV_BR }); // 11

  // [ ... continue for TOP, BOTTOM, RIGHT, LEFT faces using the UV_... variables ... ]

  // --- 3. TOP FACE (+Y) ---
  const core::Vector3 N_Top = { 0.0f, 1.0f, 0.0f };
  vertices.push_back({ { -HS, HS, HS }, C, N_Top, UV_BL }); // 12
  vertices.push_back({ { HS, HS, HS }, C, N_Top, UV_BR }); // 13
  vertices.push_back({ { HS, HS, -HS }, C, N_Top, UV_TR }); // 14

  vertices.push_back({ { HS, HS, -HS }, C, N_Top, UV_TR }); // 15
  vertices.push_back({ { -HS, HS, -HS }, C, N_Top, UV_TL }); // 16
  vertices.push_back({ { -HS, HS, HS }, C, N_Top, UV_BL }); // 17

  // --- 4. BOTTOM FACE (-Y) ---
  const core::Vector3 N_Bottom = { 0.0f, -1.0f, 0.0f };
  vertices.push_back({ { -HS, -HS, HS }, C, N_Bottom, UV_TL }); // 18 (Mapping inverted to look correct)
  vertices.push_back({ { -HS, -HS, -HS }, C, N_Bottom, UV_BL }); // 19
  vertices.push_back({ { HS, -HS, -HS }, C, N_Bottom, UV_BR }); // 20

  vertices.push_back({ { HS, -HS, -HS }, C, N_Bottom, UV_BR }); // 21
  vertices.push_back({ { HS, -HS, HS }, C, N_Bottom, UV_TR }); // 22
  vertices.push_back({ { -HS, -HS, HS }, C, N_Bottom, UV_TL }); // 23

  // --- 5. RIGHT FACE (+X) ---
  const core::Vector3 N_Right = { 1.0f, 0.0f, 0.0f };
  vertices.push_back({ { HS, -HS, HS }, C, N_Right, UV_BL }); // 24
  vertices.push_back({ { HS, -HS, -HS }, C, N_Right, UV_BR }); // 25
  vertices.push_back({ { HS, HS, -HS }, C, N_Right, UV_TR }); // 26

  vertices.push_back({ { HS, HS, -HS }, C, N_Right, UV_TR }); // 27
  vertices.push_back({ { HS, HS, HS }, C, N_Right, UV_TL }); // 28
  vertices.push_back({ { HS, -HS, HS }, C, N_Right, UV_BL }); // 29

  // --- 6. LEFT FACE (-X) ---
  const core::Vector3 N_Left = { -1.0f, 0.0f, 0.0f };
  vertices.push_back({ { -HS, -HS, -HS }, C, N_Left, UV_BR }); // 30
  vertices.push_back({ { -HS, -HS, HS }, C, N_Left, UV_BL }); // 31
  vertices.push_back({ { -HS, HS, HS }, C, N_Left, UV_TL }); // 32

  vertices.push_back({ { -HS, HS, HS }, C, N_Left, UV_TL }); // 33
  vertices.push_back({ { -HS, HS, -HS }, C, N_Left, UV_TR }); // 34
  vertices.push_back({ { -HS, -HS, -HS }, C, N_Left, UV_BR }); // 35

  // ... (indices and mesh_ setup remains the same) ...
  std::vector<uint32_t> indices(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) {
    indices[i] = static_cast<uint32_t>(i);
  }

  auto mesh_ = std::make_shared<core::Mesh<core::Vertex3D>>();
  mesh_->SetVertices(vertices);
  mesh_->SetIndices(indices);
  return mesh_;
}


std::shared_ptr<core::Mesh<core::Vertex3D>> createSphereWithNormals(
  float radius = 1.0f,
  int stacks = 32,
  int sectors = 64
) {
  std::vector<core::Vertex3D> vertices;
  std::vector<uint32_t> indices;

  // --- 1. Generate Vertices ---

  // Constants for stack/sector sizing
  const float stack_step = M_PI / stacks;
  const float sector_step = 2 * M_PI / sectors;

  // Random color for the sphere (simplifying the color assignment)
  const core::Color sphere_color = { 0.8f, 0.8f, 0.8f, 1.0f };

  for (int i = 0; i <= stacks; ++i) {
    // Current angle for the stack (latitude) - ranges from -PI/2 to PI/2
    float stack_angle = M_PI / 2 - i * stack_step;

    // Radius of the circle at this latitude (r * cos(phi))
    float xy_radius = radius * std::cos(stack_angle);

    // Z-coordinate (r * sin(phi))
    float z = radius * std::sin(stack_angle);

    for (int j = 0; j <= sectors; ++j) {
      // Current angle for the sector (longitude) - ranges from 0 to 2*PI
      float sector_angle = j * sector_step;

      // Calculate X and Y coordinates
      float x = xy_radius * std::cos(sector_angle);
      float y = xy_radius * std::sin(sector_angle);

      // 1. Position
      core::Vector3 position = { x, y, z };

      // 2. Normal (Vector pointing from center to vertex, normalized)
      core::Vector3 normal = position.Normalize();

      // 3. UV Coordinates (0.0 to 1.0)
      core::Vector2 uv = {
          (float)j / sectors, // U: Horizontal texture coordinate
          (float)i / stacks   // V: Vertical texture coordinate
      };

      // Create and store the vertex
      vertices.push_back({
          position,
          sphere_color, // Simple grey color
          normal,
          uv
        });
    }
  }

  // --- 2. Generate Indices ---

  // A quad is formed by two triangles (triangle strip)
  for (int i = 0; i < stacks; ++i) {
    int k1 = i * (sectors + 1);     // Index of the first vertex in the current stack
    int k2 = (i + 1) * (sectors + 1); // Index of the first vertex in the next stack

    for (int j = 0; j < sectors; ++j, ++k1, ++k2) {
      // The pole caps are special cases, but the UV method handles them gracefully.
      // Check if not at the poles (first and last stacks)

      // Triangle 1: (k1, k2, k1+1)
      // This connects vertices on the current stack (k1) to the next stack (k2).
      if (i != 0) {
        indices.push_back(k1);
        indices.push_back(k2);
        indices.push_back(k1 + 1);
      }

      // Triangle 2: (k1+1, k2, k2+1)
      // This completes the quad connecting to the next sector column.
      if (i != (stacks - 1)) {
        indices.push_back(k1 + 1);
        indices.push_back(k2);
        indices.push_back(k2 + 1);
      }
    }
  }

  // --- 3. Package and Return ---
  auto mesh_ = std::make_shared<core::Mesh<core::Vertex3D>>();
  mesh_->SetVertices(vertices);
  mesh_->SetIndices(indices);
  return mesh_;
}