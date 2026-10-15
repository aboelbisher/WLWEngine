// Small helpers shared by the engine's shaders (and available to games' shaders).

float Saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 Saturate(vec3 x) { return clamp(x, 0.0, 1.0); }

// Remaps x from [from_min, from_max] to [0, 1], clamped.
float Remap01(float x, float from_min, float from_max) { return Saturate((x - from_min) / (from_max - from_min)); }

// The matrix that carries normals through a transform m (its 3x3 part): the cofactor matrix, flipped
// when m mirrors (det < 0). The same directions as transpose(inverse(m)) without dividing by the
// determinant, so a scale of 0 stays finite. Not unit length: SafeNormalize the result.
// C++ twin, with the reasons: core::NormalMatrix (core/normal_matrix.h).
highp mat3 NormalMatrix(highp mat3 m) {
  highp vec3 x = cross(m[1], m[2]);
  highp float flip = dot(m[0], x) < 0.0 ? -1.0 : 1.0;  // not sign(): det 0 must keep the normal
  return mat3(x, cross(m[2], m[0]), cross(m[0], m[1])) * flip;
}

// normalize() that gives 0 instead of NaN for a zero vector.
highp vec3 SafeNormalize(highp vec3 v) { return v * inversesqrt(max(dot(v, v), 1e-30)); }
