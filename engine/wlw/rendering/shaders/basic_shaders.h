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

out vec3 vertexColor;
out vec3 vertexNormal;
out vec3 vertexNormal1;
out vec3 vertexPos;
out vec2 vertUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform bool instanced;

void main()
{
    mat4 world = instanced ? model * aInstanceTransform : model;
    vec4 worldPos = world * vec4(aPos, 1.0);
    vertexNormal = normalize(mat3(transpose(inverse(world))) * aNormal);
    gl_Position = projection * view * worldPos;

    vertexPos = worldPos.xyz;
    vertexColor = instanced ? aColor.xyz * aInstanceTint.xyz : aColor.xyz;
    vertUV = UV;
    vertexNormal1 = aNormal;
}

)";


inline constexpr const char* fragment3DShaderSource = R"(

in vec3 vertexColor;
in vec3 vertexNormal;
in vec3 vertexPos;
in vec2 vertUV;
in vec3 vertexNormal1;

uniform vec3 viewPos;
uniform bool viewOrthographic; // orthographic: all view rays are parallel to viewForward
uniform vec3 viewForward;
uniform int lightType; // 0 = Directional, 1 = Point, 2 = Spot
uniform vec3 lightPos;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform bool useLighting;
uniform float ambientStrength;
uniform float shininess;

uniform float lightConstant;
uniform float lightLinear;
uniform float lightQuadratic;
uniform float lightCutOff;
uniform float lightOuterCutOff;

uniform bool use_texture;
uniform sampler2D model_texture; 

out vec4 FragColor;

void main()
{
    vec3 objectColor = use_texture ? texture(model_texture, vertUV).xyz : vertexColor;

    if (!useLighting) {
      FragColor = vec4(objectColor, 1.0);
      return;
    }

    vec3 norm = normalize(vertexNormal);
    // Direction from the surface towards the eye. With perspective it depends on where the
    // fragment is; with an orthographic projection it's the same everywhere.
    vec3 viewDir = viewOrthographic ? -viewForward : normalize(viewPos - vertexPos);
    
    vec3 lightDirCalc;
    float attenuation = 1.0;
    float intensity = 1.0;

    if (lightType == 0) { // Directional
        lightDirCalc = normalize(-lightDir);
    } else { // Point or Spot
        lightDirCalc = normalize(lightPos - vertexPos);
        float distance = length(lightPos - vertexPos);
        attenuation = 1.0 / (lightConstant + lightLinear * distance + lightQuadratic * (distance * distance));
        
        if (lightType == 2) { // Spot
            float theta = dot(lightDirCalc, normalize(-lightDir));
            float epsilon = lightCutOff - lightOuterCutOff;
            intensity = clamp((theta - lightOuterCutOff) / epsilon, 0.0, 1.0);
        }
    }

    vec3 halfwayDir = normalize(lightDirCalc + viewDir);

    vec3 ambient = ambientStrength * lightColor;

    float diff = max(dot(norm, lightDirCalc), 0.0);
    vec3 diffuse = diff * lightColor;

    float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    vec3 specular = (diff > 0.0) ? spec * lightColor : vec3(0.0);

    ambient *= attenuation;
    diffuse *= attenuation * intensity;
    specular *= attenuation * intensity;

    vec3 result = (ambient + diffuse + specular) * objectColor;
    FragColor = vec4(result, 1.0);
}
)";

} // namespace wlw::rendering
