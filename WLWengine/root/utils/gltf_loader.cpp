#include <iostream>
#include <memory>

#include "gltf_loader.h"
#include "core/logger.h"

#define TINYGLTF_IMPLEMENTATION

#include "tiny_gltf.h"


namespace wlw::utils {

std::unique_ptr<core::Mesh<core::Vertex3D>> GLTFLoader::Load(const std::string& filepath) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool res = loader.LoadASCIIFromFile(&model, &err, &warn, filepath);
    
    FAIL_IF(!warn.empty(), "glTF Loader Warning: " + warn);
    
    if (FAIL_IF(!err.empty(), "glTF Loader Error: " + err)) {
        return nullptr;
    }

    if (FAIL_IF(!res, "Failed to load glTF file: " + filepath)) {
        return nullptr;
    }


    auto mesh = std::make_unique<core::Mesh<core::Vertex3D>>();
    std::vector<core::Vertex3D> vertices;
    std::vector<uint32_t> indices;

    // For simplicity, we load the first mesh and its first primitive.
    if (FAIL_IF(model.meshes.empty(), "No meshes found in glTF file: " + filepath)) {
        return nullptr;
    }

    const auto& gltf_mesh = model.meshes[0];

    if (FAIL_IF(gltf_mesh.primitives.empty(), "No primitives found in mesh: " + gltf_mesh.name)) {
        return nullptr;
    }


    const auto& primitive = gltf_mesh.primitives[0];

    // --- Indices ---
    if (primitive.indices >= 0) {
        const auto& accessor = model.accessors[primitive.indices];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];

        const uint16_t* data = reinterpret_cast<const uint16_t*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
        for (size_t i = 0; i < accessor.count; ++i) {
            indices.push_back(data[i]);
        }
    }

    // --- Vertices ---
    const float* positions = nullptr;
    const float* normals = nullptr;
    const float* texcoords = nullptr;
    size_t vertex_count = 0;

    if (primitive.attributes.count("POSITION")) {
        const auto& accessor = model.accessors[primitive.attributes.at("POSITION")];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        positions = reinterpret_cast<const float*>(&model.buffers[bufferView.buffer].data[bufferView.byteOffset + accessor.byteOffset]);
        vertex_count = accessor.count;
    }

    if (primitive.attributes.count("NORMAL")) {
        const auto& accessor = model.accessors[primitive.attributes.at("NORMAL")];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        normals = reinterpret_cast<const float*>(&model.buffers[bufferView.buffer].data[bufferView.byteOffset + accessor.byteOffset]);
    }

    if (primitive.attributes.count("TEXCOORD_0")) {
        const auto& accessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        texcoords = reinterpret_cast<const float*>(&model.buffers[bufferView.buffer].data[bufferView.byteOffset + accessor.byteOffset]);
    }

    vertices.resize(vertex_count);
    for (size_t i = 0; i < vertex_count; ++i) {
        if (positions) {
            vertices[i].position = {positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]};
        }
        if (normals) {
            vertices[i].normal = {normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]};
        }
        if (texcoords) {
            vertices[i].texCoords = {texcoords[i * 2 + 0], texcoords[i * 2 + 1]};
        }
        // Color is not typically in glTF vertex attributes, so we'll leave it as default.
    }

    mesh->SetVertices(vertices);
    mesh->SetIndices(indices);

    return mesh;
}

} // namespace wlw::utils
