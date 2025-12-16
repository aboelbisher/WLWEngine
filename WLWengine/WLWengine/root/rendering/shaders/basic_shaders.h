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
uniform vec3 lightPos;
uniform vec3 lightColor;
uniform bool useLighting;
uniform float ambientStrength;
uniform float shininess;
uniform bool use_texture;
uniform sampler2D model_texture; 

out vec4 FragColor;

void main()
{

    vec3 objectColor = use_texture ?  texture(model_texture, vertUV).xyz : vertexColor;

    if (!useLighting) {
      FragColor = vec4(objectColor, 1.0);
      return;
    }

    vec3 norm = normalize(vertexNormal);
    vec3 lightDir = normalize(lightPos - vertexPos); 
    vec3 viewDir = normalize(viewPos - vertexPos);

    vec3 halfwayDir = normalize(lightDir + viewDir);

    vec3 ambient = ambientStrength * lightColor;

    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    vec3 specular = (diff > 0.0) ? spec * lightColor  : vec3(0.0);

    vec3 result = (ambient + diffuse) * objectColor + specular;
    FragColor = vec4(result, 1.0);
}
)";
