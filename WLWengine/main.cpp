#include <iostream>

#include "root/wlw.h"
#include "root/core/window.h"
#include <chrono>
#include <thread>
#include <scene/triangle_2d.h>
#include <core/vertex_2d.h>
#include <array>
#include <core/color.h>
#include <core/mesh.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace wlw;


core::Mesh<core::Vertex3D> createPyramidFrustum(float baseSize = 2.0f, float topSize = 1.0f, float height = 1.5f) {

  // Define half-sizes for easier positioning
  const float halfBase = baseSize / 2.0f;
  const float halfTop = topSize / 2.0f;
  const float halfHeight = height / 2.0f;

  // --- 1. Define the 8 Unique Vertices ---
  // Note: We use indexed drawing, so we only need 8 unique vertices, 
  // even though the shape has 6 faces and 24 corners.

  std::vector<core::Vertex3D> vertices = {
    // Base Vertices (at Z = -halfHeight)
    //       (Index 0)
    {{-halfBase, -halfBase, -halfHeight}, {1.0f, 0.0f, 0.0f, 1.0f}}, // Red corner
    //       (Index 1)
    {{ halfBase, -halfBase, -halfHeight}, {1.0f, 0.5f, 0.0f, 1.0f}}, // Orange corner
    //       (Index 2)
    {{ halfBase,  halfBase, -halfHeight}, {0.0f, 1.0f, 0.0f, 1.0f}}, // Green corner
    //       (Index 3)
    {{-halfBase,  halfBase, -halfHeight}, {0.0f, 0.0f, 1.0f, 1.0f}}, // Blue corner

    // Top Vertices (at Z = +halfHeight)
    //       (Index 4)
    {{-halfTop, -halfTop,  halfHeight}, {1.0f, 1.0f, 1.0f, 1.0f}}, // White corner
    //       (Index 5)
    {{ halfTop, -halfTop,  halfHeight}, {0.5f, 0.5f, 0.5f, 1.0f}}, // Gray corner
    //       (Index 6)
    {{ halfTop,  halfTop,  halfHeight}, {1.0f, 1.0f, 0.0f, 1.0f}}, // Yellow corner
    //       (Index 7)
    {{-halfTop,  halfTop,  halfHeight}, {0.0f, 1.0f, 1.0f, 1.0f}}, // Cyan corner
  };

  // --- 2. Define the Indices (How to connect the 8 vertices with triangles) ---
  // A frustum has 6 faces (Base, Top, and 4 sides). Each face is made of 2 triangles.

  std::vector<unsigned int> indices = {
    // --- Bottom Face (2 triangles) ---
    // Indices 0, 1, 2 and 2, 3, 0 (Winding order matters for face culling!)
    0, 1, 2,
    2, 3, 0,

    // --- Top Face (2 triangles) ---
    // Indices 4, 7, 6 and 6, 5, 4
    4, 7, 6,
    6, 5, 4,

    // --- Side 1 (0, 1, 5, 4) (2 triangles) ---
    0, 1, 5,
    5, 4, 0,

    // --- Side 2 (1, 2, 6, 5) (2 triangles) ---
    1, 2, 6,
    6, 5, 1,

    // --- Side 3 (2, 3, 7, 6) (2 triangles) ---
    2, 3, 7,
    7, 6, 2,

    // --- Side 4 (3, 0, 4, 7) (2 triangles) ---
    3, 0, 4,
    4, 7, 3
  };

  core::Mesh<core::Vertex3D> mesh_;;
	mesh_.SetVertices(vertices);
	mesh_.SetIndices(indices);
  return mesh_;
}

//void rotateMeshData(core::Mesh<core::Vertex3D>& mesh, float angle_degrees, const core::Vector3& axis) {
//  std::cout << "\n--- Applying " << angle_degrees << " degree rotation around the Y-axis... ---" << std::endl;
//
//  // We only implement the specific 90-degree Y-axis rotation logic here for demonstration.
//  // In a real engine, you'd use matrix math (like GLM) for any angle/axis.
//  if (angle_degrees == 90.0f && axis.y == 1.0f && axis.x == 0.0f && axis.z == 0.0f) {
//    for (auto& vertex : mesh.GetVertices()) {
//      float old_x = vertex.position.x;
//      float old_z = vertex.position.z;
//
//      // Simplified 90 degree Y-axis rotation:
//      vertex.position.x = old_z;
//      // vertex.position.y remains the same (y' = y)
//      vertex.position.z = -old_x;
//    }
//  }
//  else {
//    std::cerr << "Error: Only the 90 degree Y-axis rotation is implemented in this simple example." << std::endl;
//  }
//}

int main() {

	auto engine = wlw::WLWEngine::Create();
	std::shared_ptr< wlw::core::Window> window = wlw::core::Window::Create(800, 600, "MainWindow");


  auto frustum = createPyramidFrustum();
  // 2. Rotate the shape 90 degrees around the Y-axis
  //rotateMeshData(frustum, 90.0f, { 0.0f, 1.0f, 0.0f });


	auto shape = std::make_shared<wlw::scene::Node<wlw::core::Vertex3D>>();
  shape->SetMesh(frustum);
	//shape->SetVertices(frustum.vertices);
	//shape->SetIndices(frustum.indices);
  window->AddNode(shape);

	core::Color red(1.0f, 0.0f, 0.0f, 1.0f);
	std::array<wlw::core::Vertex2D, 3> vertices = { {
			// --- Triangle 1: RED ---
			// V0: Top Left of first triangle
			{{-0.9f,  0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}},
			// V1: Top Right of first triangle
			{{-0.1f,  0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}},
			// V2: Bottom Center of first triangle
			{{-0.5f, -0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}
		} };
	auto triangle = std::make_shared<wlw::scene::Triangle2D>(vertices);
	window->AddNode(triangle);


	//window->SetClearColor(wlw::core::Color(0.0f, 0.0f, 1.0f, 1.0f));
	engine->Start(window);


//	std::shared_ptr< wlw::core::Window> window1 = wlw::core::Window::Create(1024, 1024, "SecondaryWIdow");
//
//	std::array<wlw::core::Vertex2D, 3> vertices1 = { {
//
//{ { 0.1f, 0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
//// V4: Top Right of second triangle
//{ { 0.9f,  0.5f}, {0.0f, 0.0f, 1.0f, 1.0f} },
//// V5: Bottom Center of second triangle
//	{ { 0.5f, -0.0f}, {0.0f, 0.0f, 1.0f, 1.0f} }
//} };
//	auto shape1 = std::make_shared<wlw::scene::Triangle2D>(vertices1);
//	window1->AddNode(shape1);
//	engine->AttachWindow(window1);

	while(true) {

		engine->Iterate();
	}


}