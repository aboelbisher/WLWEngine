#pragma once

#include "node_2d.h"
#include "core/vertex_2d.h"

#include <array>

namespace wlw::scene {

class Triangle2D : public Node2D {
public:
	Triangle2D(std::array<core::Vertex2D, 3> vertices);
};

}



