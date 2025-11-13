#pragma once

#include "node_2d.h"

namespace wlw::scene {

class Triangle2D : public Node2D {
public:

	virtual const std::vector<core::Vertex2D>& GetVertices() const override;
	virtual const std::vector<uint32_t>& GetIndices() const override;

private:
	std::vector<core::Vertex2D> vertices_ = {
		// Define vertices with position and color
		{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}}, // Bottom Left - Red
		{{ 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f, 1.0f}}, // Bottom Right - Green
		{{ 0.0f,  0.5f}, {0.0f, 0.0f, 1.0f, 1.0f}}  // Top - Blue
	};

	std::vector<uint32_t> indices_ = {
		0, 1, 2 // Single triangle
	};

};

}



