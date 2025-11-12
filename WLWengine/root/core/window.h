#pragma once
#include <memory>
#include <string>

#include "color.h"

#ifdef WLW_USE_GLFW
#include <GLFW/glfw3.h>
#endif



namespace wlw::core {

class Window {
public:
	virtual ~Window() = default;
	virtual void Initialize() = 0;
	virtual void MakeContextCurrent() = 0;
	virtual void ProcessEvents() = 0;
	virtual void SetClearColor(const Color& color) = 0;


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