#pragma once
#include <concepts>
#include <type_traits>
#include "core/vertex_2d.h"
#include "core/vertex_3d.h"

namespace wlw::core {

template <typename T>
concept OnlyVerticesTypes =
	std::is_same_v<T, core::Vertex2D> ||
	std::is_same_v<T, core::Vertex3D>;

template <OnlyVerticesTypes T>
class Mesh {
public:
	virtual ~Mesh() = default;

	const std::vector<T>& GetVertices() const {
		return vertices_;
	}

	const std::vector<uint32_t>& GetIndices() const {
		return indices_;
	}

	void SetVertices(const std::vector<T>& vertices) {
		vertices_ = vertices;
	}
	void SetIndices(const std::vector<uint32_t>& indices) {
		indices_ = indices;
	}

protected:
	std::vector<T> vertices_;
	std::vector<uint32_t> indices_;
};
} // namespace wlw::core