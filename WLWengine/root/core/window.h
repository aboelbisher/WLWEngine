#pragma once
#include <memory>
#include <string>

#include "color.h"
#include "scene/node_2d.h"
#include "core/vector2.h"

#ifdef WLW_USE_GLFW
#include <GLFW/glfw3.h>
#endif
#include <unordered_map>



namespace wlw::core {

class Window {
public:
	using Nodes2DMap = std::unordered_map<int, std::shared_ptr<scene::Node2D>>;

	virtual ~Window() = default;
#ifdef WLW_USE_GLFW
	virtual void Initialize(GLFWwindow* master_window = nullptr) = 0;
#endif
	virtual void MakeContextCurrent() = 0;
	virtual void ProcessEvents() = 0;
	virtual void SetClearColor(const Color& color) = 0;
	virtual void SetIsActive(bool is_active) = 0;
	virtual int AddNode(const std::shared_ptr<scene::Node2D>& node_2d) = 0;
	virtual Vector2 GetSize() const = 0;

	virtual const Nodes2DMap& GetNodes2D() const = 0;

#ifdef WLW_USE_GLFW
	virtual GLFWwindow* GetGLFWwindow() = 0 ;
#endif


	//virtual void SwapBuffers() = 0;
	//virtual bool ShouldClose() = 0;
	//virtual void PollEvents() = 0;
	//virtual void SetFramebufferSizeCallback(void (*callback)(int, int)) = 0;
	//virtual int GetWidth() const = 0;
	//virtual int GetHeight() const = 0;


	static std::unique_ptr<Window> Create(int width, int height, const std::string& title);
};

} // namespace wlw::core