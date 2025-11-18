#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

#ifdef WLW_USE_GLFW
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#endif

#include "core/color.h"
#include "scene/node.h"
#include "core/vector2.h"
#include "core/vertex_2d.h"
#include "core/vertex_3d.h"
#include "scene/camera_3d.h"
#include "scene/fps_camera.h"

namespace wlw::scene {



class Window {
public:

	virtual ~Window() = default;
#ifdef WLW_USE_GLFW
	virtual void Initialize(GLFWwindow* master_window = nullptr) = 0;
#endif
	virtual void MakeContextCurrent() = 0;
	virtual void ProcessEvents() = 0;
	virtual void SetClearColor(const core::Color& color) = 0;

	virtual void ProcessMouseMovement(double xpos, double ypos) = 0;

	void SetIsActive(bool is_active) {
		is_active_ = is_active;
	}

	int AddNode(const std::shared_ptr<Node2D>& node_2d) {
		nodes_2d_[nodes_2d_next_id_] = node_2d;
		return nodes_2d_next_id_++;
	}

	int AddNode(const std::shared_ptr<Node3D>& node_3d)  {
		nodes_3d_[nodes_3d_next_id_] = node_3d;
		return nodes_3d_next_id_++;
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

	//TODO: use callback cookies here
	void IterateOver3DNodes(const std::function<void(const std::shared_ptr<Node3D>)> callback) {

		//use vector rather than nodes; 
		//u might be using the same ids when copying children here !!!!
		auto current_nodes = nodes_3d_;

		while (!current_nodes.empty()) {
			Nodes3DMap children = {};
			for (const auto& [_,node] : current_nodes) {
				callback(node);

				auto node_children = node->GetChildren();
				for (const auto& [_, child] : node_children) {
					children.insert({ node_children.size() + 1 ,child });
				}
			}
			current_nodes = children;
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

public:

	static std::unique_ptr<Window> Create(const core::Vector2& size, const std::string& title);
};

} // namespace wlw::core