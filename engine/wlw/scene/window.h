#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

#include "wlw/core/color.h"
#include "wlw/scene/node.h"
#include "wlw/core/vector2.h"
#include "wlw/core/vertex_2d.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/fps_camera.h"
#include "wlw/rendering/texture.h"
#include "wlw/input/input.h"

namespace wlw::scene {

// A drawable surface + its scene (nodes, camera, skybox) + its input.
// The platform layer implements the OS-specific part (platform/<os>/<os>_platform.cpp);
// nothing in this header depends on GLFW, EGL or any OS API.
class Window {
public:

	virtual ~Window() = default;

	// ---- Platform-specific (implemented per OS) ----
	// Creates the OS window/surface and a GL context. `share_with`: share GPU resources with
	// another window's context (desktop multi-window; unsupported on mobile).
	virtual bool Initialize(Window* share_with = nullptr) = 0;
	virtual void MakeContextCurrent() = 0;
	virtual void ProcessEvents() = 0;   // poll OS events and feed GetInput()
	virtual void SwapBuffers() = 0;     // present the drawn frame
	virtual bool ShouldClose() const = 0;          // user closed the window/app (or RequestClose was called)
	virtual void RequestClose() = 0;
	virtual void SetTitle(const std::string& title) = 0;  // no-op where there is no title bar
	// Returns true once after the app comes back from the background (mobile), so the game
	// loop can restart its clock instead of seeing one huge frame.
	virtual bool ConsumeResumed() { return false; }

	input::Input& GetInput() { return input_; }
	const input::Input& GetInput() const { return input_; }

	// Feeds mouse-look to the current camera (FPS camera); called by platforms that capture the cursor.
	void ProcessMouseMovement(double xpos, double ypos) {
		if (first_mouse_) {
			last_mouse_ = {static_cast<float>(xpos), static_cast<float>(ypos)};
			first_mouse_ = false;
		}
		const float xoffset = static_cast<float>(xpos) - last_mouse_.x;
		const float yoffset = last_mouse_.y - static_cast<float>(ypos);
		last_mouse_ = {static_cast<float>(xpos), static_cast<float>(ypos)};
		if (camera_) camera_->ProcessMouseMovement(xoffset, yoffset);
	}

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

	size_t NodeCount3D() const { return nodes_3d_.size(); }

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
		if (size_.x > 0 && size_.y > 0) {  // minimized windows report 0x0
			camera_->UpdateSize(size_);
		}
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




protected:
	core::Vector2 size_;  // drawable size in pixels (updated by the platform on resize)
	input::Input input_;
	bool first_mouse_ = true;
	core::Vector2 last_mouse_{0, 0};

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