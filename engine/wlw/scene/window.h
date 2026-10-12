#pragma once
#include <memory>
#include <string>
#include <functional>
#include <vector>

#include "wlw/core/color.h"
#include "wlw/core/vector2.h"
#include "wlw/scene/viewport.h"
#include "wlw/input/input.h"

namespace wlw::scene {

// A drawable surface (the OS window / the phone's screen) + its input + what it shows: the
// screen viewport (GetScreen(): a Scene seen through a camera, with the game's UI canvas over
// it) and any offscreen viewports, drawn into their textures before the screen (AddViewport).
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
	// Returns true once after the app was sent to the background (mobile: call, app switch,
	// screen off), so the game can pause itself.
	virtual bool ConsumePaused() { return false; }

	input::Input& GetInput() { return input_; }
	const input::Input& GetInput() const { return input_; }

	// Feeds mouse-look to the screen's camera (FPS camera); called by platforms that capture the cursor.
	void ProcessMouseMovement(double xpos, double ypos) {
		if (first_mouse_) {
			last_mouse_ = {static_cast<float>(xpos), static_cast<float>(ypos)};
			first_mouse_ = false;
		}
		const float xoffset = static_cast<float>(xpos) - last_mouse_.x;
		const float yoffset = last_mouse_.y - static_cast<float>(ypos);
		last_mouse_ = {static_cast<float>(xpos), static_cast<float>(ypos)};
		if (const auto& camera = screen_->GetCamera()) camera->ProcessMouseMovement(xoffset, yoffset);
	}

	void SetIsActive(bool is_active) {
		is_active_ = is_active;
	}

	core::Vector2 GetSize() const  {
		return size_;  // drawable size in pixels
	}

	// ---- What it shows ----
	// The screen: its scene (empty at first), camera (an FPSCamera until SetCamera) and the canvas
	// the game's UI is drawn on (Application::OnRenderUI).
	Viewport& GetScreen() { return *screen_; }
	const Viewport& GetScreen() const { return *screen_; }

	// Offscreen viewports, drawn every frame (as their update mode says) before the screen, in the
	// order added. Returns an id for RemoveViewport.
	int AddViewport(std::shared_ptr<Viewport> viewport) {
		viewports_.push_back({next_viewport_id_, std::move(viewport)});
		return next_viewport_id_++;
	}
	void RemoveViewport(int id) {
		std::erase_if(viewports_, [id](const auto& entry) { return entry.first == id; });
	}
	const std::vector<std::pair<int, std::shared_ptr<Viewport>>>& GetViewports() const { return viewports_; }

	// Drops the scenes and viewports (their GPU resources, if nothing else holds them): the engine
	// calls it at shutdown while the GL context still exists.
	void ReleaseScenes() {
		viewports_.clear();
		screen_->SetScene(nullptr);
	}

	// Layers drawn on top of the screen (after its UI canvas), just before the frame is presented,
	// in the order they were added (e.g. the debug UI). Returns an id for RemoveOverlay.
	int AddOverlay(std::function<void()> draw) {
		overlays_.push_back({next_overlay_id_, std::move(draw)});
		return next_overlay_id_++;
	}
	void RemoveOverlay(int id) {
		std::erase_if(overlays_, [id](const auto& overlay) { return overlay.first == id; });
	}
	void DrawOverlays() const {
		for (const auto& [_, draw] : overlays_) draw();
	}

protected:
	core::Vector2 size_;  // drawable size in pixels (updated by the platform on resize)
	input::Input input_;
	bool first_mouse_ = true;
	core::Vector2 last_mouse_{0, 0};

	bool is_active_ = true;

	std::shared_ptr<Viewport> screen_ = std::make_shared<Viewport>(
		nullptr, ViewportSettings{.name = "Screen", .clear = core::Color{0.1f, 0.1f, 0.15f, 1.0f}});
	std::vector<std::pair<int, std::shared_ptr<Viewport>>> viewports_;
	int next_viewport_id_ = 0;
	std::vector<std::pair<int, std::function<void()>>> overlays_;
	int next_overlay_id_ = 0;

public:

	static std::unique_ptr<Window> Create(const core::Vector2& size, const std::string& title);
};

} // namespace wlw::scene
