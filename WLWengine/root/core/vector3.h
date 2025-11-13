#pragma once

#include <cmath>

namespace wlw::core {
	struct Vector3 {
		float x;
		float y;
		float z;

		Vector3 operator+(const Vector3& other) const {
			return { x + other.x, y + other.y, z + other.z };
		}

		Vector3 operator-(const Vector3& other) const {
			return { x - other.x, y - other.y, z - other.z };
		}

		Vector3 Cross(const Vector3& a) {
			return {
				y * a.z - z * a.y,
				z * a.x - x * a.z,
				x * a.y - y * a.x
			};
		}
		
		Vector3 operator*(float scalar) const {
			return { x * scalar, y * scalar, z * scalar };
		}

		Vector3 operator/(float scalar) const {
			return { x / scalar, y / scalar, z / scalar };
		}

		Vector3 Normalize() {
			float length = std::sqrt(x * x + y * y + z * z);
			return { x / length, y / length, z / length };
		}
 };

} // namespace wlw::core