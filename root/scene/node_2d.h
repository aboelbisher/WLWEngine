#pragma once

#include <vector>

#include "node.h"
#include "core/vertex_2d.h"

namespace wlw::scene {

class Node2D {
public:
	virtual ~Node2D() = default;

	virtual const std::vector<core::Vertex2D>& GetVertices() const = 0;
	virtual const std::vector<uint32_t>& GetIndices() const = 0;
};
}

