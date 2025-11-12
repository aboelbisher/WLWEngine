#pragma once

#include "camera_3d.h"

namespace wlw::scene {
	class FPSCamera : public Camera3D {
	public:
	
		static std::unique_ptr<FPSCamera> Create();

	};
} // namespace wlw::scene