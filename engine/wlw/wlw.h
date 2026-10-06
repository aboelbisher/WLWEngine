#pragma once
#include <memory>

#pragma once

#include "wlw/scene/window.h"
#include "wlw/rendering/rendering_driver.h"
#include "wlw/rendering/render_device.h"

namespace wlw {

	class WLWEngine {
	public:
		virtual ~WLWEngine() = default;

		virtual bool Start(std::shared_ptr<wlw::scene::Window> window) = 0;  // creates the GL context; false on failure
		virtual void Iterate() = 0;
		virtual int AttachWindow(std::shared_ptr<wlw::scene::Window> window) = 0;

		virtual rendering::RenderingDriver* GetRenderingDriver() = 0;
		virtual rendering::RenderDevice* GetRenderDevice() = 0;

		static std::unique_ptr<WLWEngine> Create();

	};
}