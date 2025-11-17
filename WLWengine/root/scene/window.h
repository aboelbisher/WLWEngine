#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#ifdef WLW_USE_GLFW
#include <GLFW/glfw3.h>
#endif

#include "core/color.h"
#include "scene/node.h"
#include "core/vector2.h"
#include "core/vertex_2d.h"
#include "core/vertex_3d.h"
#include "scene/camera_3d.h"

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
	virtual void SetIsActive(bool is_active) = 0;
	virtual int AddNode(const std::shared_ptr<Node3D>& node_2d) = 0;
	virtual int AddNode(const std::shared_ptr<Node2D>& node_3d) = 0;
	virtual core::Vector2 GetSize() const = 0;

	virtual const Nodes2DMap& GetNodes2D() const = 0;
	virtual const Nodes3DMap& GetNodes3D() const = 0;

	virtual void SetCamera(std::shared_ptr<Camera3D> camera) = 0;
	virtual const std::shared_ptr<Camera3D> GetUpdatedCamera() const = 0;

	virtual void ProcessMouseMovement(double xpos, double ypos) = 0;

#ifdef WLW_USE_GLFW
	virtual GLFWwindow* GetGLFWwindow() = 0 ;
#endif


	//virtual void SwapBuffers() = 0;
	//virtual bool ShouldClose() = 0;
	//virtual void PollEvents() = 0;
	//virtual void SetFramebufferSizeCallback(void (*callback)(int, int)) = 0;
	//virtual int GetWidth() const = 0;
	//virtual int GetHeight() const = 0;


	static std::unique_ptr<Window> Create(const core::Vector2& size, const std::string& title);
};

} // namespace wlw::core