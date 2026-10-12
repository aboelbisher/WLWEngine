// Small helpers shared by the engine's shaders (and available to games' shaders).

float Saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 Saturate(vec3 x) { return clamp(x, 0.0, 1.0); }

// Remaps x from [from_min, from_max] to [0, 1], clamped.
float Remap01(float x, float from_min, float from_max) { return Saturate((x - from_min) / (from_max - from_min)); }
