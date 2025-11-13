#include "node_2d.h"

namespace wlw::scene {
	const std::vector<core::Vertex2D>& Node2D::GetVertices() const {
		return vertices_;
	}
	const std::vector<uint32_t>& Node2D::GetIndices() const {
		return indices_;
	}
	void Node2D::SetVertices(const std::vector<core::Vertex2D>& vertices) {
		vertices_ = vertices;
	}
	void Node2D::SetIndices(const std::vector<uint32_t>& indices) {
		indices_ = indices;
	}
} // wlw::scene