#pragma once

const char* skyboxVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 view;
uniform mat4 projection;

void main()
{
    TexCoords = aPos;
    // We remove the translation part of the view matrix to keep the skybox centered
    mat4 staticView = mat4(mat3(view)); 
    vec4 pos = projection * staticView * vec4(aPos, 1.0);
    gl_Position = pos.xyww; // Force depth to 1.0 (maximum depth)
}
)";

const char* skyboxFragmentShaderSource = R"(
#version 330 core 
out vec4 FragColor;

in vec3 TexCoords;

uniform samplerCube skybox;

void main()
{    
    FragColor = texture(skybox, TexCoords);
}
)";
