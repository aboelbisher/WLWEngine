#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

#ifdef WLW_USE_GLFW
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#endif

#include "wlw/core/color.h"
#include "wlw/scene/node.h"
#include "wlw/core/vector2.h"
#include "wlw/core/vertex_2d.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/fps_camera.h"
#include "wlw/rendering/texture.h"

namespace wlw::scene {

class Window {
public:

	virtual ~Window() = default;
#ifdef WLW_USE_GLFW
	virtual void Initialize(GLFWwindow* master_window = nullptr) = 0;
#endif
	virtual void MakeContextCurrent() = 0;
	virtual void ProcessEvents() = 0;   // poll input/OS events
	virtual void SwapBuffers() = 0;     // present the drawn frame
	virtual void SetClearColor(const core::Color& color) = 0;

	virtual void ProcessMouseMovement(double xpos, double ypos) = 0;

	void SetIsActive(bool is_active) {
		is_active_ = is_active;
	}

	void SetSkybox(std::shared_ptr<rendering::WCubemap> skybox) {
		skybox_ = skybox;
	}

	std::shared_ptr<rendering::WCubemap> GetSkybox() const {
		return skybox_;
	}

	int AddNode(const std::shared_ptr<Node2D>& node_2d) {
		nodes_2d_[nodes_2d_next_id_] = node_2d;
		return nodes_2d_next_id_++;
	}

	int AddNode(const std::shared_ptr<Node3D>& node_3d)  {
		nodes_3d_[nodes_3d_next_id_] = node_3d;
		return nodes_3d_next_id_++;
	}

	void RemoveNode3D(int id) {
		nodes_3d_.erase(id);
	}

	void ClearScene3D() {
		nodes_3d_.clear();
		nodes_3d_next_id_ = 0;
	}

	// Drops every node and the skybox (frees their GPU resources if nothing else holds them).
	void ClearScene() {
		nodes_2d_.clear();
		nodes_2d_next_id_ = 0;
		ClearScene3D();
		skybox_ = nullptr;
	}

	const Nodes2DMap& GetNodes2D() const  {
		return nodes_2d_;
	}

	const Nodes3DMap& GetNodes3D() const  {
		return nodes_3d_;
	}

	void SetCamera(std::shared_ptr<Camera3D> camera)  {
		camera_ = camera;
	}

	const std::shared_ptr<Camera3D> GetUpdatedCamera() const {
		camera_->UpdateSize(size_);
		return camera_;
	}

	core::Vector2 GetSize() const  {
		return size_;
	}

	// Visits every 3D node: top-level nodes first, then their children, level by level.
	void IterateOver3DNodes(const std::function<void(const std::shared_ptr<Node3D>&)>& callback) const {
		std::vector<std::shared_ptr<Node3D>> current;
		current.reserve(nodes_3d_.size());
		for (const auto& [_, node] : nodes_3d_) {
			current.push_back(node);
		}

		std::vector<std::shared_ptr<Node3D>> next;
		while (!current.empty()) {
			next.clear();
			for (const auto& node : current) {
				callback(node);
				for (const auto& [_, child] : node->GetChildren()) {
					next.push_back(child);
				}
			}
			current.swap(next);
		}
	}


#ifdef WLW_USE_GLFW
	virtual GLFWwindow* GetGLFWwindow() = 0 ;
#endif


protected:
	core::Vector2 size_;

	bool is_active_ = true;

	wlw::scene::Nodes2DMap nodes_2d_{};
	int nodes_2d_next_id_ = 0;

	wlw::scene::Nodes3DMap nodes_3d_ = {};
	int nodes_3d_next_id_ = 0;

	std::shared_ptr<scene::Camera3D> camera_ = scene::FPSCamera::Create();
	std::shared_ptr<rendering::WCubemap> skybox_ = nullptr;

public:

	static std::unique_ptr<Window> Create(const core::Vector2& size, const std::string& title);
};

} // namespace wlw::core