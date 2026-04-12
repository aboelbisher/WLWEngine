#pragma once
#include <concepts>
#include <type_traits>
#include <memory>
#include <vector>
#include <array>
#include <algorithm>
#include <glm/glm.hpp>

#include "core/vertex_2d.h"
#include "core/vertex_3d.h"
#include "rendering/material.h"
#include "rendering/vertex_buffer.h"
#include "rendering/index_buffer.h"
#include "core/collision.h"

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
		vertex_buffer_ = nullptr;
		index_buffer_ = nullptr;

        if (vertices_.empty()) {
            local_aabb_ = { {0,0,0}, {0,0,0} };
            return;
        }

        glm::vec3 minB(vertices_[0].position.x, vertices_[0].position.y, vertices_[0].position.z);
        glm::vec3 maxB = minB;
        for (const auto& v : vertices_) {
            minB = glm::min(minB, glm::vec3(v.position.x, v.position.y, v.position.z));
            maxB = glm::max(maxB, glm::vec3(v.position.x, v.position.y, v.position.z));
        }
        local_aabb_ = { {minB.x, minB.y, minB.z}, {maxB.x, maxB.y, maxB.z} };
	}

    const scene::AABB& GetLocalAABB() const { return local_aabb_; }

	void SetIndices(const std::vector<uint32_t>& indices) {
		indices_ = indices;
		index_buffer_ = nullptr;
	}

	void SetMaterial(const std::shared_ptr<rendering::Material>& material) {
		material_ = material;
	}

	std::shared_ptr<rendering::Material> GetMaterial() const {
		return material_;
	}

	void SetVertexBuffer(std::unique_ptr<WVertexBuffer> vertex_buffer) {
		vertex_buffer_ = std::move(vertex_buffer);
	}

	const WVertexBuffer* GetVertexBuffer() const {
		return vertex_buffer_.get();
	}

	void SetIndexBuffer(std::unique_ptr<WIndexBuffer> index_buffer) {
		index_buffer_ = std::move(index_buffer);
	}

	const WIndexBuffer* GetIndexBuffer() const {
		return index_buffer_.get();
	}

	std::string name = "";

protected:
	std::vector<T> vertices_;
	std::vector<uint32_t> indices_;

	std::unique_ptr<WVertexBuffer> vertex_buffer_ = nullptr;
	std::unique_ptr<WIndexBuffer> index_buffer_ = nullptr;

	std::shared_ptr<rendering::Material> material_ = nullptr;

    scene::AABB local_aabb_ = { {0,0,0}, {0,0,0} };
};
} // namespace wlw::core
