#pragma once

#include <cmath>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

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

		Vector3 operator+=(const Vector3& other) const {
			return  { x + other.x, y + other.y, z + other.z };
		}

		void operator-=(const Vector3& other) {
			x -= other.x;
			y -= other.y;
			z -= other.z;
		}

		void operator+=(const Vector3& other) {
			x += other.x;
			y += other.y;
			z += other.z;
		}

		Vector3 operator-() const {
			return { -x, -y, -z };
		}

		operator glm::vec3() const {
			return glm::vec3(x, y, z);
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

		Vector3 Rotate(float angle_degrees, const Vector3& rotate_vector) {
			// 2. Create an identity matrix
			glm::vec3 glm_v = { x, y,z };
			glm::mat4 rotation_matrix = glm::mat4(1.0f);

			// 3. Apply the rotation transformation to the matrix
			// glm::rotate handles the conversion from degrees (via glm::radians)
			rotation_matrix = glm::rotate(rotation_matrix, glm::radians(angle_degrees), {rotate_vector.x, rotate_vector.y, rotate_vector.z});

			// 4. Transform the vector
			// We multiply the 4x4 matrix by a 4D vector (using w=1.0 for position/direction)
			glm::vec4 rotated_vec4 = rotation_matrix * glm::vec4(glm_v, 1.0f);

			// 5. Convert the result back to core::Vector3
			glm::vec3 rotated_vec3(rotated_vec4);

			return core::Vector3(rotated_vec3.x, rotated_vec3.y, rotated_vec3.z);

		}
 };

} // namespace wlw::core