#ifdef WLW_USE_GLFW

#include <vector>
#include <GLFW/glfw3.h>

#include "core/logger.h"

#include "input_engine.h"

namespace wlw::os {
	class InputEngineImpl : public InputEngine {
		public:
		InputEngineImpl() {}

		void AttachWindow(std::shared_ptr<scene::Window> window) override {
			glfwSetWindowUserPointer(window->GetGLFWwindow(), window.get());
			glfwSetCursorPosCallback(window->GetGLFWwindow(), InputEngineImpl::MouseCallback);
				//TODO and what happen when the window is removed XDXDXD ? 
			attached_windows_.push_back(window);
		}

		static void MouseCallback(GLFWwindow* window, double xpos, double ypos) {
			scene::Window* window_ = static_cast<scene::Window*>(glfwGetWindowUserPointer(window));
			if (FAIL_IF(window_ == nullptr, "window instance is null in MouseCallback")) {
				return;
			}
			window_->ProcessMouseMovement(static_cast<float>(xpos), static_cast<float>(ypos));
		}
	private:
		std::vector<std::shared_ptr<scene::Window>> attached_windows_;

	};

	std::unique_ptr<InputEngine> InputEngine::Create() {
		return std::make_unique<InputEngineImpl>();
	}

} // namespace wlw::os

#endif // WLW_USE_GLFW