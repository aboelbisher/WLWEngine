#pragma once

#include "color.h"
#include "vector3.h"
#include "vector2.h"

namespace wlw::core {

	struct Vertex3D {
		Vector3 position;
		Color color;
		Vector3 normal;
		Vector2 texCoords; 
 };

} // namespace wlw::core