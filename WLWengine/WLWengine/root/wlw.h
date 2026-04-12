#pragma once
#include <memory>

#pragma once

#include "scene/window.h"
#include "rendering/rendering_driver.h"
#include "rendering/render_device.h"

namespace wlw {

	class WLWEngine {
	public:
		virtual ~WLWEngine() = default;

		virtual void Start(std::shared_ptr<wlw::scene::Window> window) = 0;
		virtual void Iterate() = 0;
		virtual int AttachWindow(std::shared_ptr<wlw::scene::Window> window) = 0;

		virtual rendering::RenderingDriver* GetRenderingDriver() = 0;
		virtual rendering::RenderDevice* GetRenderDevice() = 0;

		static std::unique_ptr<WLWEngine> Create();

	};
}