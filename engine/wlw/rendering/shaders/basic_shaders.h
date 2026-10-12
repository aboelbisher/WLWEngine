#pragma once


// Shader sources without a #version line: the renderer prepends kGLSLHeader (rendering/gl.h),
// so the same source compiles as GLSL 3.30 (desktop) or GLSL ES 3.00 (mobile).
namespace wlw::rendering {

inline constexpr const char* vertex2DShaderSource = R"(
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vertexColor;
void main()
{
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
    vertexColor = aColor;
}
)";

inline constexpr const char* fragment2DShaderSource = R"(
out vec4 FragColor;
in vec3 vertexColor;
void main()
{
    FragColor = vec4(vertexColor, 1.0f);
}
)";


inline constexpr const char* vertex3DShaderSource = R"(
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 UV;
// Instanced drawing only: per-INSTANCE attributes (glVertexAttribDivisor 1), i.e. they advance
// once per copy of the mesh instead of once per vertex. A mat4 takes 4 locations (4..7).
layout (location = 4) in mat4 aInstanceTransform;
layout (location = 8) in vec4 aInstanceTint;

out vec4 vertexColor;  // rgb + alpha (instanced: times the copy's tint)
out vec3 vertexNormal;
out vec3 vertexPos;
out vec2 vertUV;
out vec4 lightSpacePos;  // where this vertex is in the shadow-casting light's view

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform bool instanced;
uniform mat4 lightSpace;            // the shadow light's view-projection
uniform float shadowNormalOffset;   // world units: sample the shadow map a little off the surface

void main()
{
    mat4 world = instanced ? model * aInstanceTransform : model;
    vec4 worldPos = world * vec4(aPos, 1.0);
    vertexNormal = normalize(mat3(transpose(inverse(world))) * aNormal);
    gl_Position = projection * view * worldPos;

    vertexPos = worldPos.xyz;
    vertexColor = instanced ? aColor * aInstanceTint : aColor;
    vertUV = UV;
    // Pushing the lookup point out along the normal keeps a surface from shadowing itself
    // ("shadow acne": the depth it compares against was stored at nearly the same depth).
    lightSpacePos = lightSpace * vec4(worldPos.xyz + vertexNormal * shadowNormalOffset, 1.0);
}

)";


inline constexpr const char* fragment3DShaderSource = R"(
#define MAX_LIGHTS 4

in vec4 vertexColor;
in vec3 vertexNormal;
in vec3 vertexPos;
in vec2 vertUV;
in vec4 lightSpacePos;

uniform vec3 viewPos;
uniform bool viewOrthographic; // orthographic: all view rays are parallel to viewForward
uniform vec3 viewForward;

// The lights (scene light nodes, or the material's light when the scene has none).
uniform bool useLighting;
uniform int lightCount;
uniform int lightTypes[MAX_LIGHTS];          // 0 = Directional, 1 = Point, 2 = Spot
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightDirections[MAX_LIGHTS];    // where the light shines (directional, spot)
uniform vec3 lightColors[MAX_LIGHTS];        // colour * intensity
uniform float lightAmbients[MAX_LIGHTS];
uniform vec3 lightAttenuations[MAX_LIGHTS];  // constant, linear, quadratic falloff with distance
uniform vec2 lightCones[MAX_LIGHTS];         // spot: cos(inner), cos(outer)
uniform float shininess;

// Shadows: from light `shadowLight` (-1 = none), for surfaces that receive them.
uniform int shadowLight;
uniform bool receiveShadow;
uniform highp sampler2DShadow shadowMap;
uniform float shadowStrength;
uniform float shadowSoftness;  // filter radius in texels
uniform float shadowTexel;     // 1 / map size

uniform bool use_texture;
uniform sampler2D model_texture;
uniform bool opaque;  // the material is BlendMode::Opaque: alpha is 1

out vec4 FragColor;

// How much of the shadow light reaches this point, 0..1. Each texture() on a sampler2DShadow
// compares our depth with the stored one (and the GPU blends the 4 nearest results); 9 taps
// around the point average into a soft edge (percentage-closer filtering).
float ShadowVisibility()
{
    vec3 p = lightSpacePos.xyz / lightSpacePos.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;  // outside the map
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            lit += texture(shadowMap, vec3(p.xy + vec2(x, y) * shadowSoftness * shadowTexel, p.z));
        }
    }
    return lit / 9.0;
}

void main()
{
    // Alpha only matters for materials that blend (see blend_mode.h); output is premultiplied.
    vec4 base = use_texture ? texture(model_texture, vertUV) * vec4(1.0, 1.0, 1.0, vertexColor.a) : vertexColor;
    vec3 objectColor = base.rgb;
    float alpha = opaque ? 1.0 : base.a;  // opaque draws never leave see-through pixels behind

    if (!useLighting) {
      FragColor = vec4(objectColor * alpha, alpha);
      return;
    }

    vec3 norm = normalize(vertexNormal);
    // Direction from the surface towards the eye. With perspective it depends on where the
    // fragment is; with an orthographic projection it's the same everywhere.
    vec3 viewDir = viewOrthographic ? -viewForward : normalize(viewPos - vertexPos);
    float shadow = (shadowLight >= 0 && receiveShadow) ? mix(1.0, ShadowVisibility(), shadowStrength) : 1.0;

    vec3 result = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= lightCount) break;
        vec3 toLight;
        float attenuation = 1.0;
        float cone = 1.0;
        if (lightTypes[i] == 0) {  // Directional
            toLight = normalize(-lightDirections[i]);
        } else {                   // Point or Spot
            vec3 offset = lightPositions[i] - vertexPos;
            float distance = length(offset);
            toLight = offset / max(distance, 0.0001);
            vec3 k = lightAttenuations[i];
            attenuation = 1.0 / (k.x + k.y * distance + k.z * distance * distance);
            if (lightTypes[i] == 2) {  // Spot: full inside the inner cone, fading to the outer
                float theta = dot(toLight, normalize(-lightDirections[i]));
                cone = clamp((theta - lightCones[i].y) / max(lightCones[i].x - lightCones[i].y, 0.0001), 0.0, 1.0);
            }
        }
        vec3 halfway = normalize(toLight + viewDir);
        float diff = max(dot(norm, toLight), 0.0);
        float spec = diff > 0.0 ? pow(max(dot(norm, halfway), 0.0), shininess) : 0.0;
        float lit = (i == shadowLight) ? shadow : 1.0;  // shadows take the direct light, not the ambient
        vec3 ambient = lightAmbients[i] * lightColors[i] * attenuation;
        vec3 direct = (diff + spec) * lightColors[i] * attenuation * cone * lit;
        result += ambient + direct;
    }
    FragColor = vec4(result * objectColor * alpha, alpha);
}
)";

// The shadow pass: only depth, seen from the light. Same inputs as the 3D shader (so instanced
// nodes work unchanged); no colour output.
inline constexpr const char* vertexShadowShaderSource = R"(
layout (location = 0) in vec3 aPos;
layout (location = 4) in mat4 aInstanceTransform;

uniform mat4 model;
uniform mat4 lightSpace;
uniform bool instanced;

void main()
{
    mat4 world = instanced ? model * aInstanceTransform : model;
    gl_Position = lightSpace * world * vec4(aPos, 1.0);
}
)";

inline constexpr const char* fragmentShadowShaderSource = R"(
void main()
{
}
)";

} // namespace wlw::rendering
