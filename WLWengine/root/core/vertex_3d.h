#pragma once

#include "color.h"
#include "vector3.h"

namespace wlw::core {

	struct Vertex3D {
		Vector3 position;
		Color color;
		Vector3 normal;
 };

} // namespace wlw::core