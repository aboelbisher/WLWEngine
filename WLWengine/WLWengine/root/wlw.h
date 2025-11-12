#pragma once
#include <memory>

#pragma once

#include "scene/window.h"

namespace wlw {

	class WLWEngine {
	public:
		virtual ~WLWEngine() = default;

		virtual void Start(std::shared_ptr<wlw::scene::Window> window) = 0;
		virtual void Iterate() = 0;
		virtual int AttachWindow(std::shared_ptr<wlw::scene::Window> window) = 0;

		static std::unique_ptr<WLWEngine> Create();

	};
}