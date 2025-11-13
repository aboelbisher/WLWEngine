#pragma once

#include <vector>

#include "core/vertex_2d.h"

namespace wlw::scene {

class Node2D {
public:
	virtual ~Node2D() = default;

	const std::vector<core::Vertex2D>& GetVertices() const;
	const std::vector<uint32_t>& GetIndices() const;

	void SetVertices(const std::vector<core::Vertex2D>& vertices);
	void SetIndices(const std::vector<uint32_t>& indices);

protected:
	std::vector<core::Vertex2D> vertices_;
	std::vector<uint32_t> indices_;

};
}

