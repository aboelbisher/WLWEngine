#pragma once

#include <vector>
#include "core/mesh.h"
#include "core/vertex_3d.h"

using namespace wlw;

core::Mesh<core::Vertex3D> createPyramidFrustumWithNormals(float baseSize = 2.0f, float topSize = 1.0f, float height = 1.5f) {

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
  std::vector<uint32_t> indices; // We still return indices, but they will be sequential 0, 1, 2, ...

  // --- 1. Bottom Face (Normal: 0, 0, -1) ---
  core::Vector3 N_Bottom = { 0.0f, 0.0f, -1.0f }; // Points straight down
  vertices.push_back({ P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Bottom }); // 0
  vertices.push_back({ P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Bottom }); // 1
  vertices.push_back({ P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Bottom }); // 2

  vertices.push_back({ P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Bottom }); // 3
  vertices.push_back({ P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Bottom }); // 4
  vertices.push_back({ P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Bottom }); // 5

  // --- 2. Top Face (Normal: 0, 0, 1) ---
  core::Vector3 N_Top = { 0.0f, 0.0f, 1.0f }; // Points straight up
  vertices.push_back({ P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Top }); // 6
  vertices.push_back({ P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Top }); // 7
  vertices.push_back({ P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Top }); // 8

  vertices.push_back({ P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Top }); // 9
  vertices.push_back({ P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Top }); // 10
  vertices.push_back({ P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Top }); // 11

  // --- 3. Side 1 (Face 0-1-5-4) ---
  core::Vector3 V1_0 = P[1] - P[0]; // Edge 0->1
  core::Vector3 V4_0 = P[4] - P[0]; // Edge 0->4 (This is a simplified tangent/bitangent approach)
  core::Vector3 N_Side1 = V1_0.Cross(V4_0).Normalize();

  // Triangle 1: 0, 1, 5
  vertices.push_back({ P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Side1 }); // 12
  vertices.push_back({ P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Side1 }); // 13
  vertices.push_back({ P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Side1 }); // 14
  // Triangle 2: 5, 4, 0
  vertices.push_back({ P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Side1 }); // 15
  vertices.push_back({ P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Side1 }); // 16
  vertices.push_back({ P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Side1 }); // 17

  // --- 4. Side 2 (Face 1-2-6-5) ---
  core::Vector3 V2_1 = P[2] - P[1];
  core::Vector3 V5_1 = P[5] - P[1];
  core::Vector3 N_Side2 = V2_1.Cross(V5_1).Normalize();

  // Triangle 1: 1, 2, 6
  vertices.push_back({ P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Side2 }); // 18
  vertices.push_back({ P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Side2 }); // 19
  vertices.push_back({ P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Side2 }); // 20
  // Triangle 2: 6, 5, 1
  vertices.push_back({ P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Side2 }); // 21
  vertices.push_back({ P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Side2 }); // 22
  vertices.push_back({ P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Side2 }); // 23

  // --- 5. Side 3 (Face 2-3-7-6) ---
  core::Vector3 V3_2 = P[3] - P[2];
  core::Vector3 V6_2 = P[6] - P[2];
  core::Vector3 N_Side3 = V3_2.Cross(V6_2).Normalize();

  // ... (Add 6 vertices for Side 3, all using N_Side3) ...
  // Using simple vector math:
  // P[2], P[3], P[7],  and P[7], P[6], P[2]

  vertices.push_back({ P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Side3 }); // 24
  vertices.push_back({ P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Side3 }); // 25
  vertices.push_back({ P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Side3 }); // 26

  vertices.push_back({ P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Side3 }); // 27
  vertices.push_back({ P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Side3 }); // 28
  vertices.push_back({ P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Side3 }); // 29


  // --- 6. Side 4 (Face 3-0-4-7) ---
  core::Vector3 V0_3 = P[0] - P[3];
  core::Vector3 V7_3 = P[7] - P[3];
  core::Vector3 N_Side4 = V0_3.Cross(V7_3).Normalize();

  // ... (Add 6 vertices for Side 4, all using N_Side4) ...
  // Using simple vector math:
  // P[3], P[0], P[4], and P[4], P[7], P[3]

  vertices.push_back({ P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Side4 }); // 30
  vertices.push_back({ P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Side4 }); // 31
  vertices.push_back({ P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Side4 }); // 32

  vertices.push_back({ P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Side4 }); // 33
  vertices.push_back({ P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Side4 }); // 34
  vertices.push_back({ P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Side4 }); // 35


  // Finally, since we de-indexed, we create sequential indices 0, 1, 2, ..., 35
  indices.resize(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) {
    indices[i] = static_cast<uint32_t>(i);
  }


  core::Mesh<core::Vertex3D> mesh_;;
  mesh_.SetVertices(vertices);
  mesh_.SetIndices(indices);
  return mesh_;
}

core::Mesh<core::Vertex3D> createCubeWithNormals(float size = 1.0f) {

  // Half-size for easy centering around the origin
  const float HS = size / 2.0f;

  // The cube will have 24 vertices (4 per face x 6 faces)
  std::vector<core::Vertex3D> vertices;

  // Standard color for the whole cube
   core::Color red = { 1.0f, 0.0f, 0.0f, 1.0f };
   core::Color green = { 0.0f, 1.0f, 0.0f, 1.0f };
   core::Color blue = { 0.0f, 0.0f, 1.0f, 1.0f };
	 blue = green = red;
  // --- 1. FRONT FACE (+Z) ---
  // Vertices defined in counter-clockwise order (standard for front-facing)
  const core::Vector3 N_Front = { 0.0f, 0.0f, 1.0f };

  // Triangle 1: Bottom-left, Bottom-right, Top-right
  vertices.push_back({ { -HS, -HS,  HS }, red, N_Front }); // 0
  vertices.push_back({ {  HS, -HS,  HS }, red, N_Front }); // 1
  vertices.push_back({ {  HS,  HS,  HS }, red, N_Front }); // 2

  // Triangle 2: Top-right, Top-left, Bottom-left
  vertices.push_back({ {  HS,  HS,  HS }, blue, N_Front }); // 3
  vertices.push_back({ { -HS,  HS,  HS }, blue, N_Front }); // 4
  vertices.push_back({ { -HS, -HS,  HS }, blue, N_Front }); // 5

  // --- 2. BACK FACE (-Z) ---
  const core::Vector3 N_Back = { 0.0f, 0.0f, -1.0f };

  // Triangle 1: Bottom-right, Bottom-left, Top-left (Winding is opposite to maintain front-facing)
  vertices.push_back({ {  HS, -HS, -HS }, green, N_Back }); // 6
  vertices.push_back({ { -HS, -HS, -HS }, green, N_Back }); // 7
  vertices.push_back({ { -HS,  HS, -HS }, green, N_Back }); // 8

  // Triangle 2: Top-left, Top-right, Bottom-right
  vertices.push_back({ { -HS,  HS, -HS }, red, N_Back }); // 9
  vertices.push_back({ {  HS,  HS, -HS }, red, N_Back }); // 10
  vertices.push_back({ {  HS, -HS, -HS }, red, N_Back }); // 11

  // --- 3. TOP FACE (+Y) ---
  const core::Vector3 N_Top = { 0.0f, 1.0f, 0.0f };

  // Triangle 1: Top-left-front, Top-right-front, Top-right-back
  vertices.push_back({ { -HS,  HS,  HS }, blue, N_Top }); // 12
  vertices.push_back({ {  HS,  HS,  HS }, blue, N_Top }); // 13
  vertices.push_back({ {  HS,  HS, -HS }, blue, N_Top }); // 14

  // Triangle 2: Top-right-back, Top-left-back, Top-left-front
  vertices.push_back({ {  HS,  HS, -HS }, red, N_Top }); // 15
  vertices.push_back({ { -HS,  HS, -HS }, red, N_Top }); // 16
  vertices.push_back({ { -HS,  HS,  HS }, red, N_Top }); // 17

  // --- 4. BOTTOM FACE (-Y) ---
  const core::Vector3 N_Bottom = { 0.0f, -1.0f, 0.0f };

  // Triangle 1: Bottom-left-front, Bottom-left-back, Bottom-right-back
  vertices.push_back({ { -HS, -HS,  HS }, green, N_Bottom }); // 18
  vertices.push_back({ { -HS, -HS, -HS }, green, N_Bottom }); // 19
  vertices.push_back({ {  HS, -HS, -HS }, green, N_Bottom }); // 20

  // Triangle 2: Bottom-right-back, Bottom-right-front, Bottom-left-front
  vertices.push_back({ {  HS, -HS, -HS }, red, N_Bottom }); // 21
  vertices.push_back({ {  HS, -HS,  HS }, red, N_Bottom }); // 22
  vertices.push_back({ { -HS, -HS,  HS }, red, N_Bottom }); // 23

  // --- 5. RIGHT FACE (+X) ---
  const core::Vector3 N_Right = { 1.0f, 0.0f, 0.0f };

  // Triangle 1: Bottom-right-front, Bottom-right-back, Top-right-back
  vertices.push_back({ {  HS, -HS,  HS }, blue, N_Right }); // 24
  vertices.push_back({ {  HS, -HS, -HS }, blue, N_Right }); // 25
  vertices.push_back({ {  HS,  HS, -HS }, blue, N_Right }); // 26

  // Triangle 2: Top-right-back, Top-right-front, Bottom-right-front
  vertices.push_back({ {  HS,  HS, -HS }, green, N_Right }); // 27
  vertices.push_back({ {  HS,  HS,  HS }, green, N_Right }); // 28
  vertices.push_back({ {  HS, -HS,  HS }, green, N_Right }); // 29

  // --- 6. LEFT FACE (-X) ---
  const core::Vector3 N_Left = { -1.0f, 0.0f, 0.0f };

  // Triangle 1: Bottom-left-back, Bottom-left-front, Top-left-front
  vertices.push_back({ { -HS, -HS, -HS }, red, N_Left }); // 30
  vertices.push_back({ { -HS, -HS,  HS }, red, N_Left }); // 31
  vertices.push_back({ { -HS,  HS,  HS }, red, N_Left }); // 32

  // Triangle 2: Top-left-front, Top-left-back, Bottom-left-back
  vertices.push_back({ { -HS,  HS,  HS }, blue, N_Left }); // 33
  vertices.push_back({ { -HS,  HS, -HS }, blue, N_Left }); // 34
  vertices.push_back({ { -HS, -HS, -HS }, blue, N_Left }); // 35

  // The indices are now simply sequential since every 3 vertices form a unique triangle
  std::vector<uint32_t> indices(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) {
    indices[i] = static_cast<uint32_t>(i);
  }

  core::Mesh<core::Vertex3D> mesh_;;
  mesh_.SetVertices(vertices);
  mesh_.SetIndices(indices);
  return mesh_;
}