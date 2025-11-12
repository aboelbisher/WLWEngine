#pragma once

#pragma once


// --- Global Setup (Minimal Shaders for Modern GL) ---
// Shaders are required for modern OpenGL (Core Profile 3.3+)
const char* vertex2DShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vertexColor;
void main()
{
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
    vertexColor = aColor;
}
)";

const char* fragment2DShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec3 vertexColor;
void main()
{
    FragColor = vec4(vertexColor, 1.0f);
}
)";


const char* vertex3DShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 UV;

out vec3 vertexColor;
out vec3 vertexNormal;
out vec3 vertexNormal1;
out vec3 vertexPos;
out vec2 vertUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    vertexNormal = normalize(mat3(transpose(inverse(model))) * aNormal);
    gl_Position = projection * view * model * vec4(aPos, 1.0);

    vertexPos = worldPos.xyz;
    vertexColor = aColor.xyz;
    vertUV = UV;
    vertexNormal1 = aNormal;
}

)";


const char* fragment3DShaderSource = R"(
#version 330 core

in vec3 vertexColor;
in vec3 vertexNormal;
in vec3 vertexPos;
in vec2 vertUV;
in vec3 vertexNormal1;

uniform vec3 viewPos;
uniform vec3 lightColor;
uniform vec3 lightPos;
uniform bool useLighting;
uniform float ambientStrength;
uniform float shininess;
uniform bool use_texture;
uniform sampler2D model_texture; 

out vec4 FragColor;

void main()
{
    vec3 final_vertex_color = use_texture ?  texture(model_texture, vertUV).xyz : vertexColor;

    //vec3 final_vertex_color = use_texture ?  vec3(0.0, 1.0, 0.0) : vertexColor;

    if (!useLighting) {
      FragColor = vec4(final_vertex_color, 1.0);
      return;
    }

    vec3 ambient = ambientStrength * final_vertex_color;

    vec3 norm = normalize(vertexNormal);
    vec3 lightDir = normalize(lightPos - vertexPos); 

    // Calculate the angle (cosine) and ensure it's not negative (light hitting from behind)
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * final_vertex_color;

    // --- 3. Specular Lighting ---
    // Bright spot reflecting directly at the viewer.

    // Direction from fragment to the viewer/camera
    vec3 viewDir = normalize(viewPos - vertexPos);

    // Reflection vector (R = 2 * (N . L) * N - L)
    vec3 reflectDir = reflect(-lightDir, norm); 

    // Calculate specular factor (cosine of angle between view and reflection vector, raised to shininess)
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    vec3 specular = spec * lightColor;

    // --- 4. Final Color Calculation ---
    // Combine all lighting components and multiply by the object's base color.
    vec3 result = (ambient + diffuse + specular);

    FragColor = vec4(result, 1.0);
}
)";
