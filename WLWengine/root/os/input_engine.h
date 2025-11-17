#pragma once

#include <memory>
#include "scene/window.h"

namespace wlw::os {

	class InputEngine {
	public:
		virtual ~InputEngine() = default;

		virtual void AttachWindow(std::shared_ptr<scene::Window> window) = 0;

		static std::unique_ptr<InputEngine> Create();
	};
} // namespace wlw::os