#pragma once
#include <memory>

#include "core/window.h"

namespace wlw {

	class WLWEngine {
	public:
		virtual ~WLWEngine() = default;

		virtual void Start(std::unique_ptr<wlw::core::Window> window) = 0;
		virtual void Iterate() = 0;

		virtual void AttachWindow(std::unique_ptr<wlw::core::Window> window) = 0;


		static std::unique_ptr<WLWEngine> Create();
	};
}