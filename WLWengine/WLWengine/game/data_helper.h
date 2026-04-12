#pragma once

#include <vector>
#include "core/mesh.h"
#include "core/vertex_3d.h"

using namespace wlw;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline std::shared_ptr<core::Mesh<core::Vertex3D>> createPyramidFrustumWithNormals(float baseSize = 2.0f, float topSize = 1.0f, float height = 1.5f) {
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

inline std::shared_ptr<core::Mesh<core::Vertex3D>> createCubeWithNormals(float size = 1.0f, const core::Color& color = {1.0f, 1.0f, 1.0f, 1.0f}) {
  const float HS = size / 2.0f;
  std::vector<core::Vertex3D> vertices;

  // UV Shorthand
  const core::Vector2 UV_BL = {0.0f, 0.0f};
  const core::Vector2 UV_BR = {1.0f, 0.0f};
  const core::Vector2 UV_TR = {1.0f, 1.0f};
  const core::Vector2 UV_TL = {0.0f, 1.0f};

  // --- 1. FRONT FACE (+Z) ---
  const core::Vector3 N_Front = {0.0f, 0.0f, 1.0f};
  vertices.push_back({{-HS, -HS, HS}, color, N_Front, UV_BL});
  vertices.push_back({{HS, -HS, HS}, color, N_Front, UV_BR});
  vertices.push_back({{HS, HS, HS}, color, N_Front, UV_TR});
  vertices.push_back({{HS, HS, HS}, color, N_Front, UV_TR});
  vertices.push_back({{-HS, HS, HS}, color, N_Front, UV_TL});
  vertices.push_back({{-HS, -HS, HS}, color, N_Front, UV_BL});

  // --- 2. BACK FACE (-Z) ---
  const core::Vector3 N_Back = {0.0f, 0.0f, -1.0f};
  vertices.push_back({{HS, -HS, -HS}, color, N_Back, UV_BR});
  vertices.push_back({{-HS, -HS, -HS}, color, N_Back, UV_BL});
  vertices.push_back({{-HS, HS, -HS}, color, N_Back, UV_TL});
  vertices.push_back({{-HS, HS, -HS}, color, N_Back, UV_TL});
  vertices.push_back({{HS, HS, -HS}, color, N_Back, UV_TR});
  vertices.push_back({{HS, -HS, -HS}, color, N_Back, UV_BR});

  // --- 3. TOP FACE (+Y) ---
  const core::Vector3 N_Top = {0.0f, 1.0f, 0.0f};
  vertices.push_back({{-HS, HS, HS}, color, N_Top, UV_BL});
  vertices.push_back({{HS, HS, HS}, color, N_Top, UV_BR});
  vertices.push_back({{HS, HS, -HS}, color, N_Top, UV_TR});
  vertices.push_back({{HS, HS, -HS}, color, N_Top, UV_TR});
  vertices.push_back({{-HS, HS, -HS}, color, N_Top, UV_TL});
  vertices.push_back({{-HS, HS, HS}, color, N_Top, UV_BL});

  // --- 4. BOTTOM FACE (-Y) ---
  const core::Vector3 N_Bottom = {0.0f, -1.0f, 0.0f};
  vertices.push_back({{-HS, -HS, HS}, color, N_Bottom, UV_TL});
  vertices.push_back({{-HS, -HS, -HS}, color, N_Bottom, UV_BL});
  vertices.push_back({{HS, -HS, -HS}, color, N_Bottom, UV_BR});
  vertices.push_back({{HS, -HS, -HS}, color, N_Bottom, UV_BR});
  vertices.push_back({{HS, -HS, HS}, color, N_Bottom, UV_TR});
  vertices.push_back({{-HS, -HS, HS}, color, N_Bottom, UV_TL});

  // --- 5. RIGHT FACE (+X) ---
  const core::Vector3 N_Right = {1.0f, 0.0f, 0.0f};
  vertices.push_back({{HS, -HS, HS}, color, N_Right, UV_BL});
  vertices.push_back({{HS, -HS, -HS}, color, N_Right, UV_BR});
  vertices.push_back({{HS, HS, -HS}, color, N_Right, UV_TR});
  vertices.push_back({{HS, HS, -HS}, color, N_Right, UV_TR});
  vertices.push_back({{HS, HS, HS}, color, N_Right, UV_TL});
  vertices.push_back({{HS, -HS, HS}, color, N_Right, UV_BL});

  // --- 6. LEFT FACE (-X) ---
  const core::Vector3 N_Left = {-1.0f, 0.0f, 0.0f};
  vertices.push_back({{-HS, -HS, -HS}, color, N_Left, UV_BR});
  vertices.push_back({{-HS, -HS, HS}, color, N_Left, UV_BL});
  vertices.push_back({{-HS, HS, HS}, color, N_Left, UV_TL});
  vertices.push_back({{-HS, HS, HS}, color, N_Left, UV_TL});
  vertices.push_back({{-HS, HS, -HS}, color, N_Left, UV_TR});
  vertices.push_back({{-HS, -HS, -HS}, color, N_Left, UV_BR});

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


inline std::shared_ptr<core::Mesh<core::Vertex3D>> createSphereWithNormals(
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

  inline std::shared_ptr<core::Mesh<core::Vertex3D>> createCylinderWithNormals(float radius = 0.5f, float height = 1.0f, int segments = 32) {
  std::vector<core::Vertex3D> vertices;
  std::vector<uint32_t> indices;
  const float HH = height / 2.0f;
  const core::Color color = {0.7f, 0.7f, 0.7f, 1.0f};

  // Top and Bottom center points
  core::Vertex3D topCenter = {{0, HH, 0}, color, {0, 1, 0}, {0.5f, 0.5f}};
  core::Vertex3D bottomCenter = {{0, -HH, 0}, color, {0, -1, 0}, {0.5f, 0.5f}};

  for (int i = 0; i <= segments; ++i) {
    float angle = i * 2.0f * (float)M_PI / segments;
    float x = radius * std::cos(angle);
    float z = radius * std::sin(angle);
    float u = (float)i / segments;

    // Side vertices
    core::Vector3 normal = core::Vector3(x, 0, z).Normalize();
    vertices.push_back({{x, HH, z}, color, normal, {u, 1.0f}}); // Top side
    vertices.push_back({{x, -HH, z}, color, normal, {u, 0.0f}}); // Bottom side

    // Cap vertices
    vertices.push_back({{x, HH, z}, color, {0, 1, 0}, {u, 1.0f}}); // Top cap
    vertices.push_back({{x, -HH, z}, color, {0, -1, 0}, {u, 0.0f}}); // Bottom cap
  }

  int stride = 4;
  for (int i = 0; i < segments; ++i) {
    int curr = i * stride;
    int next = (i + 1) * stride;

    // Side faces
    indices.push_back(curr); indices.push_back(next); indices.push_back(curr + 1);
    indices.push_back(curr + 1); indices.push_back(next); indices.push_back(next + 1);

    // Top cap
    int tcIdx = (int)vertices.size();
    vertices.push_back(topCenter);
    indices.push_back(tcIdx); indices.push_back(curr + 2); indices.push_back(next + 2);

    // Bottom cap
    int bcIdx = (int)vertices.size();
    vertices.push_back(bottomCenter);
    indices.push_back(bcIdx); indices.push_back(next + 3); indices.push_back(curr + 3);
  }

  auto mesh_ = std::make_shared<core::Mesh<core::Vertex3D>>();
  mesh_->SetVertices(vertices);
  mesh_->SetIndices(indices);
  return mesh_;
  }

  inline std::shared_ptr<core::Mesh<core::Vertex3D>> createGemWithNormals(float width = 0.5f, float height = 1.0f) {
  std::vector<core::Vertex3D> vertices;
  const float HW = width / 2.0f;
  const float HH = height / 2.0f;
  const core::Color color = {1.0f, 1.0f, 1.0f, 1.0f};

  core::Vector3 top = {0, HH, 0};
  core::Vector3 bottom = {0, -HH, 0};
  core::Vector3 mid[4] = {{HW, 0, HW}, {-HW, 0, HW}, {-HW, 0, -HW}, {HW, 0, -HW}};

  auto addTriangle = [&](const core::Vector3& p1, const core::Vector3& p2, const core::Vector3& p3) {
    core::Vector3 v1 = p2 - p1;
    core::Vector3 v2 = p3 - p1;
    core::Vector3 normal = v1.Cross(v2).Normalize();
    vertices.push_back({p1, color, normal, {0.5f, 1.0f}});
    vertices.push_back({p2, color, normal, {0.0f, 0.0f}});
    vertices.push_back({p3, color, normal, {1.0f, 0.0f}});
  };

  for (int i = 0; i < 4; ++i) {
    addTriangle(top, mid[(i + 1) % 4], mid[i]);
    addTriangle(bottom, mid[i], mid[(i + 1) % 4]);
  }

  std::vector<uint32_t> indices(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) indices[i] = (uint32_t)i;

  auto mesh_ = std::make_shared<core::Mesh<core::Vertex3D>>();
  mesh_->SetVertices(vertices);
  mesh_->SetIndices(indices);
  return mesh_;
  }