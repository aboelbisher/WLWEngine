#pragma once

#include <vector>
#include <concepts>
#include <type_traits>

#include "core/vertex_2d.h"
#include "core/vertex_3d.h"
#include "core/mesh.h"

namespace wlw::scene {

template <core::OnlyVerticesTypes T>
class Node {
public:
	const core::Mesh<T>& GetMesh() const {
		return mesh_;
	}

	void SetMesh(const core::Mesh<T>& mesh) {
		mesh_ = mesh;
	}


private:
	core::Mesh<T> mesh_;
};

using Node3D = Node<core::Vertex3D>;
using Node2D = Node<core::Vertex2D>;

} // namespace wlw::scene