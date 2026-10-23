#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>
#include "wlw/core/mesh.h"
#include "wlw/core/vertex_3d.h"

namespace wlw::utils {

namespace primitives_detail {
// v / |v| (written out, not glm::normalize's inverse square root: the meshes stay bit-identical to 0.2's).
inline Vec3 Unit(const Vec3& v) { return v / glm::length(v); }
// Meshes written with Y up (the cube, cylinder and gem) turned to the engine's Z up (core/math.h FromYUp): their
// +Y becomes +Z and their +Z (the cube's "front") becomes -Y, kModelFront. A rotation, so windings stay.
inline void TurnToZUp(std::vector<core::Vertex3D>& vertices) {
  for (core::Vertex3D& v : vertices) {
    v.position = FromYUp(v.position);
    v.normal = FromYUp(v.normal);
  }
}
}  // namespace primitives_detail

// Every mesh here is centred on the origin, Z up (core/math.h Conventions), front faces counter-clockwise seen
// from outside. The pyramid frustum, cylinder, gem and sphere stand on Z.

inline std::unique_ptr<core::Mesh<core::Vertex3D>> PyramidFrustumMesh(float base_size = 2.0f, float top_size = 1.0f,
                                                                      float height = 1.5f) {
  // Define half-sizes
  const float HB = base_size / 2.0f;
  const float HT = top_size / 2.0f;
  const float HH = height / 2.0f;

  // Define the 8 UNIQUE POSITIONS first (for readability)
  const Vec3 P[8] = {
      {-HB, -HB, -HH}, {HB, -HB, -HH}, {HB, HB, -HH}, {-HB, HB, -HH},  // Base (0-3)
      {-HT, -HT, HH},  {HT, -HT, HH},  {HT, HT, HH},  {-HT, HT, HH}    // Top (4-7)
  };

  std::vector<core::Vertex3D> vertices;
  std::vector<uint32_t> indices;

  // UV Shorthand
  const Vec2 UV_BL = {0.0f, 0.0f};  // Bottom-Left
  const Vec2 UV_BR = {1.0f, 0.0f};  // Bottom-Right
  const Vec2 UV_TR = {1.0f, 1.0f};  // Top-Right
  const Vec2 UV_TL = {0.0f, 1.0f};  // Top-Left

  // NOTE: C is a placeholder for the color attribute in the struct { P[i], C, N, UV }

  // --- 1. Bottom Face (Normal: 0, 0, -1) ---
  Vec3 N_Bottom = {0.0f, 0.0f, -1.0f};
  vertices.push_back({P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Bottom, UV_BL});  // 0
  vertices.push_back({P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Bottom, UV_TR});  // 1 (counter-clockwise seen from below)
  vertices.push_back({P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Bottom, UV_BR});  // 2

  vertices.push_back({P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Bottom, UV_TR});  // 3
  vertices.push_back({P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Bottom, UV_BL});  // 4
  vertices.push_back({P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Bottom, UV_TL});  // 5

  // --- 2. Top Face (Normal: 0, 0, 1) ---
  Vec3 N_Top = {0.0f, 0.0f, 1.0f};
  vertices.push_back({P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Top, UV_BL});  // 6
  vertices.push_back({P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Top, UV_TR});  // 7 (counter-clockwise seen from above)
  vertices.push_back({P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Top, UV_TL});  // 8

  vertices.push_back({P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Top, UV_TR});  // 9
  vertices.push_back({P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Top, UV_BL});  // 10
  vertices.push_back({P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Top, UV_BR});  // 11

  // --- 3. Side 1 (Face 0-1-5-4) ---
  // P[0] is BL, P[1] is BR, P[5] is TR (top-right), P[4] is TL (top-left)
  Vec3 V1_0 = P[1] - P[0];
  Vec3 V4_0 = P[4] - P[0];
  Vec3 N_Side1 = primitives_detail::Unit(glm::cross(V1_0, V4_0));

  // Triangle 1: 0, 1, 5
  vertices.push_back({P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Side1, UV_BL});  // 12
  vertices.push_back({P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Side1, UV_BR});  // 13
  vertices.push_back({P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Side1, UV_TR});  // 14
  // Triangle 2: 5, 4, 0
  vertices.push_back({P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Side1, UV_TR});  // 15
  vertices.push_back({P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Side1, UV_TL});  // 16
  vertices.push_back({P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Side1, UV_BL});  // 17

  // --- 4. Side 2 (Face 1-2-6-5) ---
  // P[1] is BL, P[2] is BR, P[6] is TR, P[5] is TL
  Vec3 V2_1 = P[2] - P[1];
  Vec3 V5_1 = P[5] - P[1];
  Vec3 N_Side2 = primitives_detail::Unit(glm::cross(V2_1, V5_1));

  // Triangle 1: 1, 2, 6
  vertices.push_back({P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Side2, UV_BL});  // 18
  vertices.push_back({P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Side2, UV_BR});  // 19
  vertices.push_back({P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Side2, UV_TR});  // 20
  // Triangle 2: 6, 5, 1
  vertices.push_back({P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Side2, UV_TR});  // 21
  vertices.push_back({P[5], {0.5f, 0.5f, 0.5f, 1.0f}, N_Side2, UV_TL});  // 22
  vertices.push_back({P[1], {1.0f, 0.5f, 0.0f, 1.0f}, N_Side2, UV_BL});  // 23

  // --- 5. Side 3 (Face 2-3-7-6) ---
  // P[2] is BL, P[3] is BR, P[7] is TR, P[6] is TL
  Vec3 V3_2 = P[3] - P[2];
  Vec3 V6_2 = P[6] - P[2];
  Vec3 N_Side3 = primitives_detail::Unit(glm::cross(V3_2, V6_2));

  // Triangle 1: 2, 3, 7
  vertices.push_back({P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Side3, UV_BL});  // 24
  vertices.push_back({P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Side3, UV_BR});  // 25
  vertices.push_back({P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Side3, UV_TR});  // 26

  // Triangle 2: 7, 6, 2
  vertices.push_back({P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Side3, UV_TR});  // 27
  vertices.push_back({P[6], {1.0f, 1.0f, 0.0f, 1.0f}, N_Side3, UV_TL});  // 28
  vertices.push_back({P[2], {0.0f, 1.0f, 0.0f, 1.0f}, N_Side3, UV_BL});  // 29

  // --- 6. Side 4 (Face 3-0-4-7) ---
  // P[3] is BL, P[0] is BR, P[4] is TR, P[7] is TL
  Vec3 V0_3 = P[0] - P[3];
  Vec3 V7_3 = P[7] - P[3];
  Vec3 N_Side4 = primitives_detail::Unit(glm::cross(V0_3, V7_3));

  // Triangle 1: 3, 0, 4
  vertices.push_back({P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Side4, UV_BL});  // 30
  vertices.push_back({P[0], {1.0f, 0.0f, 0.0f, 1.0f}, N_Side4, UV_BR});  // 31
  vertices.push_back({P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Side4, UV_TR});  // 32

  // Triangle 2: 4, 7, 3
  vertices.push_back({P[4], {1.0f, 1.0f, 1.0f, 1.0f}, N_Side4, UV_TR});  // 33
  vertices.push_back({P[7], {0.0f, 1.0f, 1.0f, 1.0f}, N_Side4, UV_TL});  // 34
  vertices.push_back({P[3], {0.0f, 0.0f, 1.0f, 1.0f}, N_Side4, UV_BL});  // 35

  // ... (indices and mesh setup remains the same) ...
  indices.resize(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) {
    indices[i] = static_cast<uint32_t>(i);
  }

  auto mesh = std::make_unique<core::Mesh<core::Vertex3D>>();
  mesh->SetVertices(vertices);
  mesh->SetIndices(indices);
  return mesh;
}

inline std::unique_ptr<core::Mesh<core::Vertex3D>> CubeMesh(float size = 1.0f,
                                                            const Color& color = {1.0f, 1.0f, 1.0f, 1.0f}) {
  const float HS = size / 2.0f;
  std::vector<core::Vertex3D> vertices;

  // UV Shorthand
  const Vec2 UV_BL = {0.0f, 0.0f};
  const Vec2 UV_BR = {1.0f, 0.0f};
  const Vec2 UV_TR = {1.0f, 1.0f};
  const Vec2 UV_TL = {0.0f, 1.0f};

  // --- 1. FRONT FACE (+Z) ---
  const Vec3 N_Front = {0.0f, 0.0f, 1.0f};
  vertices.push_back({{-HS, -HS, HS}, color, N_Front, UV_BL});
  vertices.push_back({{HS, -HS, HS}, color, N_Front, UV_BR});
  vertices.push_back({{HS, HS, HS}, color, N_Front, UV_TR});
  vertices.push_back({{HS, HS, HS}, color, N_Front, UV_TR});
  vertices.push_back({{-HS, HS, HS}, color, N_Front, UV_TL});
  vertices.push_back({{-HS, -HS, HS}, color, N_Front, UV_BL});

  // --- 2. BACK FACE (-Z) ---
  const Vec3 N_Back = {0.0f, 0.0f, -1.0f};
  vertices.push_back({{HS, -HS, -HS}, color, N_Back, UV_BR});
  vertices.push_back({{-HS, -HS, -HS}, color, N_Back, UV_BL});
  vertices.push_back({{-HS, HS, -HS}, color, N_Back, UV_TL});
  vertices.push_back({{-HS, HS, -HS}, color, N_Back, UV_TL});
  vertices.push_back({{HS, HS, -HS}, color, N_Back, UV_TR});
  vertices.push_back({{HS, -HS, -HS}, color, N_Back, UV_BR});

  // --- 3. TOP FACE (+Y) ---
  const Vec3 N_Top = {0.0f, 1.0f, 0.0f};
  vertices.push_back({{-HS, HS, HS}, color, N_Top, UV_BL});
  vertices.push_back({{HS, HS, HS}, color, N_Top, UV_BR});
  vertices.push_back({{HS, HS, -HS}, color, N_Top, UV_TR});
  vertices.push_back({{HS, HS, -HS}, color, N_Top, UV_TR});
  vertices.push_back({{-HS, HS, -HS}, color, N_Top, UV_TL});
  vertices.push_back({{-HS, HS, HS}, color, N_Top, UV_BL});

  // --- 4. BOTTOM FACE (-Y) ---
  const Vec3 N_Bottom = {0.0f, -1.0f, 0.0f};
  vertices.push_back({{-HS, -HS, HS}, color, N_Bottom, UV_TL});
  vertices.push_back({{-HS, -HS, -HS}, color, N_Bottom, UV_BL});
  vertices.push_back({{HS, -HS, -HS}, color, N_Bottom, UV_BR});
  vertices.push_back({{HS, -HS, -HS}, color, N_Bottom, UV_BR});
  vertices.push_back({{HS, -HS, HS}, color, N_Bottom, UV_TR});
  vertices.push_back({{-HS, -HS, HS}, color, N_Bottom, UV_TL});

  // --- 5. RIGHT FACE (+X) ---
  const Vec3 N_Right = {1.0f, 0.0f, 0.0f};
  vertices.push_back({{HS, -HS, HS}, color, N_Right, UV_BL});
  vertices.push_back({{HS, -HS, -HS}, color, N_Right, UV_BR});
  vertices.push_back({{HS, HS, -HS}, color, N_Right, UV_TR});
  vertices.push_back({{HS, HS, -HS}, color, N_Right, UV_TR});
  vertices.push_back({{HS, HS, HS}, color, N_Right, UV_TL});
  vertices.push_back({{HS, -HS, HS}, color, N_Right, UV_BL});

  // --- 6. LEFT FACE (-X) ---
  const Vec3 N_Left = {-1.0f, 0.0f, 0.0f};
  vertices.push_back({{-HS, -HS, -HS}, color, N_Left, UV_BR});
  vertices.push_back({{-HS, -HS, HS}, color, N_Left, UV_BL});
  vertices.push_back({{-HS, HS, HS}, color, N_Left, UV_TL});
  vertices.push_back({{-HS, HS, HS}, color, N_Left, UV_TL});
  vertices.push_back({{-HS, HS, -HS}, color, N_Left, UV_TR});
  vertices.push_back({{-HS, -HS, -HS}, color, N_Left, UV_BR});

  // The faces above are named as written (Y up); turned, front (+Z) faces -Y and top (+Y) faces +Z.
  primitives_detail::TurnToZUp(vertices);
  std::vector<uint32_t> indices(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) {
    indices[i] = static_cast<uint32_t>(i);
  }

  auto mesh = std::make_unique<core::Mesh<core::Vertex3D>>();
  mesh->SetVertices(vertices);
  mesh->SetIndices(indices);
  return mesh;
}

// A flat rectangle in the XY plane, facing up (+Z), centred on the origin: a floor or a ground for shadows to land
// on. `width` along X, `depth` along Y; uv (0, 0) at the -X -Y corner. One quad, two triangles.
inline std::unique_ptr<core::Mesh<core::Vertex3D>> PlaneMesh(float width = 1.0f, float depth = 1.0f,
                                                             const Color& color = {1.0f, 1.0f, 1.0f, 1.0f}) {
  const float hw = width / 2.0f, hd = depth / 2.0f;
  std::vector<core::Vertex3D> vertices = {
      {{-hw, -hd, 0.0f}, color, kUp, {0.0f, 0.0f}},
      {{hw, -hd, 0.0f}, color, kUp, {1.0f, 0.0f}},
      {{hw, hd, 0.0f}, color, kUp, {1.0f, 1.0f}},
      {{-hw, hd, 0.0f}, color, kUp, {0.0f, 1.0f}},
  };
  // Counter-clockwise seen from above, like the cube's top face.
  std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
  auto mesh = std::make_unique<core::Mesh<core::Vertex3D>>();
  mesh->SetVertices(vertices);
  mesh->SetIndices(indices);
  return mesh;
}

// A cube with chamfered (bevelled) edges: 6 faces shrunk inwards by `bevel`, 12 slanted strips
// along the edges and 8 small triangles at the corners. Every facet has its own flat normal, so
// the edges catch the light differently from the faces - that's what makes it read as a solid,
// "physical" block instead of a flat-shaded box. `bevel` is clamped to [0, size / 2).
inline std::unique_ptr<core::Mesh<core::Vertex3D>> BeveledCubeMesh(float size = 1.0f, float bevel = 0.1f,
                                                                   const Color& color = {1.0f, 1.0f, 1.0f, 1.0f}) {
  const float h = size / 2.0f;                              // half size: the outer surface
  const float in = h - std::clamp(bevel, 0.0f, h * 0.99f);  // where the flat faces end
  std::vector<core::Vertex3D> vertices;

  // Adds a flat convex polygon facing `normal`. Points may come in any order: they're sorted
  // counter-clockwise around the normal (seen from outside), which is the front-face winding.
  auto add_polygon = [&](std::vector<glm::vec3> points, const glm::vec3& normal) {
    glm::vec3 center(0.0f);
    for (const glm::vec3& p : points) center += p;
    center /= static_cast<float>(points.size());
    const glm::vec3 u = glm::normalize(points[0] - center);
    const glm::vec3 v = glm::cross(normal, u);
    std::sort(points.begin(), points.end(), [&](const glm::vec3& a, const glm::vec3& b) {
      return std::atan2(glm::dot(a - center, v), glm::dot(a - center, u)) <
             std::atan2(glm::dot(b - center, v), glm::dot(b - center, u));
    });
    const Vec3 n{normal.x, normal.y, normal.z};
    for (size_t i = 1; i + 1 < points.size(); ++i) {  // triangle fan
      for (const glm::vec3& p : {points[0], points[i], points[i + 1]}) {
        vertices.push_back({{p.x, p.y, p.z}, color, n, {0.0f, 0.0f}});
      }
    }
  };
  // The point with value v0 on axis a0, v1 on a1 and v2 on a2.
  auto point = [](int a0, float v0, int a1, float v1, int a2, float v2) {
    glm::vec3 p(0.0f);
    p[a0] = v0;
    p[a1] = v1;
    p[a2] = v2;
    return p;
  };
  const float signs[] = {-1.0f, 1.0f};

  // 6 faces: on axis `a` at +-h, spanning +-in on the other two axes.
  for (int a = 0; a < 3; ++a) {
    const int b = (a + 1) % 3, c = (a + 2) % 3;
    for (float s : signs) {
      glm::vec3 normal(0.0f);
      normal[a] = s;
      add_polygon({point(a, s * h, b, -in, c, -in), point(a, s * h, b, in, c, -in), point(a, s * h, b, in, c, in),
                   point(a, s * h, b, -in, c, in)},
                  normal);
    }
  }
  // 12 edge strips: between the face on axis `a` (sign sa) and the face on axis `b` (sign sb),
  // running along the third axis `c`.
  for (int a = 0; a < 3; ++a) {
    const int b = (a + 1) % 3, c = (a + 2) % 3;
    for (float sa : signs) {
      for (float sb : signs) {
        glm::vec3 normal(0.0f);
        normal[a] = sa;
        normal[b] = sb;
        add_polygon({point(a, sa * h, b, sb * in, c, -in), point(a, sa * h, b, sb * in, c, in),
                     point(a, sa * in, b, sb * h, c, -in), point(a, sa * in, b, sb * h, c, in)},
                    glm::normalize(normal));
      }
    }
  }
  // 8 corner triangles.
  for (float sx : signs) {
    for (float sy : signs) {
      for (float sz : signs) {
        add_polygon({{sx * h, sy * in, sz * in}, {sx * in, sy * h, sz * in}, {sx * in, sy * in, sz * h}},
                    glm::normalize(glm::vec3(sx, sy, sz)));
      }
    }
  }

  std::vector<uint32_t> indices(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) indices[i] = static_cast<uint32_t>(i);

  auto mesh = std::make_unique<core::Mesh<core::Vertex3D>>();
  mesh->SetVertices(vertices);
  mesh->SetIndices(indices);
  return mesh;
}

inline std::unique_ptr<core::Mesh<core::Vertex3D>> SphereMesh(float radius = 1.0f, int stacks = 32, int sectors = 64) {
  std::vector<core::Vertex3D> vertices;
  std::vector<uint32_t> indices;

  // --- 1. Generate Vertices ---

  // Constants for stack/sector sizing
  const float stack_step = static_cast<float>(std::numbers::pi / stacks);
  const float sector_step = static_cast<float>(2 * std::numbers::pi / sectors);

  // Random color for the sphere (simplifying the color assignment)
  const Color sphere_color = {0.8f, 0.8f, 0.8f, 1.0f};

  for (int i = 0; i <= stacks; ++i) {
    // Current angle for the stack (latitude) - ranges from -PI/2 to PI/2
    float stack_angle = static_cast<float>(std::numbers::pi / 2) - i * stack_step;

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
      Vec3 position = {x, y, z};

      // 2. Normal (Vector pointing from center to vertex, normalized)
      Vec3 normal = primitives_detail::Unit(position);

      // 3. UV Coordinates (0.0 to 1.0)
      Vec2 uv = {
          (float)j / sectors,  // U: Horizontal texture coordinate
          (float)i / stacks    // V: Vertical texture coordinate
      };

      // Create and store the vertex
      vertices.push_back({position,
                          sphere_color,  // Simple grey color
                          normal, uv});
    }
  }

  // --- 2. Generate Indices ---

  // A quad is formed by two triangles (triangle strip)
  for (int i = 0; i < stacks; ++i) {
    int k1 = i * (sectors + 1);        // Index of the first vertex in the current stack
    int k2 = (i + 1) * (sectors + 1);  // Index of the first vertex in the next stack

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
  auto mesh = std::make_unique<core::Mesh<core::Vertex3D>>();
  mesh->SetVertices(vertices);
  mesh->SetIndices(indices);
  return mesh;
}

inline std::unique_ptr<core::Mesh<core::Vertex3D>> CylinderMesh(float radius = 0.5f, float height = 1.0f,
                                                                int segments = 32) {
  std::vector<core::Vertex3D> vertices;
  std::vector<uint32_t> indices;
  const float HH = height / 2.0f;
  const Color color = {0.7f, 0.7f, 0.7f, 1.0f};

  // Top and Bottom center points
  core::Vertex3D topCenter = {{0, HH, 0}, color, {0, 1, 0}, {0.5f, 0.5f}};
  core::Vertex3D bottomCenter = {{0, -HH, 0}, color, {0, -1, 0}, {0.5f, 0.5f}};

  for (int i = 0; i <= segments; ++i) {
    float angle = i * 2.0f * kPi / segments;
    float x = radius * std::cos(angle);
    float z = radius * std::sin(angle);
    float u = (float)i / segments;

    // Side vertices
    Vec3 normal = primitives_detail::Unit(Vec3(x, 0, z));
    vertices.push_back({{x, HH, z}, color, normal, {u, 1.0f}});   // Top side
    vertices.push_back({{x, -HH, z}, color, normal, {u, 0.0f}});  // Bottom side

    // Cap vertices
    vertices.push_back({{x, HH, z}, color, {0, 1, 0}, {u, 1.0f}});    // Top cap
    vertices.push_back({{x, -HH, z}, color, {0, -1, 0}, {u, 0.0f}});  // Bottom cap
  }

  int stride = 4;
  for (int i = 0; i < segments; ++i) {
    int curr = i * stride;
    int next = (i + 1) * stride;

    // Side faces
    indices.push_back(curr);
    indices.push_back(next);
    indices.push_back(curr + 1);
    indices.push_back(curr + 1);
    indices.push_back(next);
    indices.push_back(next + 1);

    // Top cap
    int tcIdx = (int)vertices.size();
    vertices.push_back(topCenter);
    indices.push_back(tcIdx);  // counter-clockwise seen from outside (the angle turns clockwise from above)
    indices.push_back(next + 2);
    indices.push_back(curr + 2);

    // Bottom cap
    int bcIdx = (int)vertices.size();
    vertices.push_back(bottomCenter);
    indices.push_back(bcIdx);
    indices.push_back(curr + 3);
    indices.push_back(next + 3);
  }
  primitives_detail::TurnToZUp(vertices);

  auto mesh = std::make_unique<core::Mesh<core::Vertex3D>>();
  mesh->SetVertices(vertices);
  mesh->SetIndices(indices);
  return mesh;
}

inline std::unique_ptr<core::Mesh<core::Vertex3D>> GemMesh(float width = 0.5f, float height = 1.0f) {
  std::vector<core::Vertex3D> vertices;
  const float HW = width / 2.0f;
  const float HH = height / 2.0f;
  const Color color = {1.0f, 1.0f, 1.0f, 1.0f};

  Vec3 top = {0, HH, 0};
  Vec3 bottom = {0, -HH, 0};
  Vec3 mid[4] = {{HW, 0, HW}, {-HW, 0, HW}, {-HW, 0, -HW}, {HW, 0, -HW}};

  auto add_triangle = [&](const Vec3& p1, const Vec3& p2, const Vec3& p3) {
    Vec3 v1 = p2 - p1;
    Vec3 v2 = p3 - p1;
    Vec3 normal = primitives_detail::Unit(glm::cross(v1, v2));
    vertices.push_back({p1, color, normal, {0.5f, 1.0f}});
    vertices.push_back({p2, color, normal, {0.0f, 0.0f}});
    vertices.push_back({p3, color, normal, {1.0f, 0.0f}});
  };

  for (int i = 0; i < 4; ++i) {
    add_triangle(top, mid[(i + 1) % 4], mid[i]);
    add_triangle(bottom, mid[i], mid[(i + 1) % 4]);
  }
  primitives_detail::TurnToZUp(vertices);

  std::vector<uint32_t> indices(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) indices[i] = (uint32_t)i;

  auto mesh = std::make_unique<core::Mesh<core::Vertex3D>>();
  mesh->SetVertices(vertices);
  mesh->SetIndices(indices);
  return mesh;
}

}  // namespace wlw::utils
