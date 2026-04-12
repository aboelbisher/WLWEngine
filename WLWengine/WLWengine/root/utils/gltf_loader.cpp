#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>

#include "gltf_loader.h"
#include "core/logger.h"
#include "core/mesh.h"
#include "core/model.h"
#include "rendering/material.h"
#include "rendering/texture.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
#include "image_loader.h"

namespace wlw::utils {

  std::vector<RawImage> LoadRawImages(const tinygltf::Model& inputModel) {
    std::vector<RawImage> raw_images;
    for (const auto& gltfTex : inputModel.textures) {
      const tinygltf::Image& gltfImage = inputModel.images[gltfTex.source];
      RawImage raw;
      raw.size = { static_cast<float>(gltfImage.width), static_cast<float>(gltfImage.height) };
      raw.channels = gltfImage.component;
      raw.pixels = gltfImage.image;
      raw_images.push_back(std::move(raw));
    }
    return raw_images;
  }

  // Helper to extract GLM Matrix from GLTF Node
  glm::mat4 GetLocalTransform(const tinygltf::Node& node) {
    glm::mat4 matrix(1.0f);
    if (node.matrix.size() == 16) {
      float floats[16];
      for (int i = 0; i < 16; i++) floats[i] = (float)node.matrix[i];
      matrix = glm::make_mat4(floats);
    }
    else {
      if (node.translation.size() == 3) {
        matrix = glm::translate(matrix, glm::vec3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]));
      }
      if (node.rotation.size() == 4) {
        glm::quat rot((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]);
        matrix *= glm::mat4_cast(rot);
      }
      if (node.scale.size() == 3) {
        matrix = glm::scale(matrix, glm::vec3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]));
      }
    }
    return matrix;
  }

  const void* GetAttributePointer(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const std::string& attributeName, size_t& count, int& stride, int& type) {
    if (primitive.attributes.find(attributeName) == primitive.attributes.end()) return nullptr;
    const int accessorIdx = primitive.attributes.at(attributeName);
    const tinygltf::Accessor& accessor = model.accessors[accessorIdx];
    if (accessor.bufferView < 0) return nullptr;
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    type = accessor.componentType;
    count = accessor.count;
    stride = accessor.ByteStride(view);
    if (stride == 0) {
      stride = tinygltf::GetComponentSizeInBytes(accessor.componentType) * tinygltf::GetNumComponentsInType(accessor.type);
    }
    return &buffer.data[view.byteOffset + accessor.byteOffset];
  }

  void ProcessMesh(const tinygltf::Model& model, const tinygltf::Mesh& inputMesh, const std::string& nodeName, const glm::mat4& transform, const std::vector<std::shared_ptr<rendering::Material>>& materials, std::vector<core::Mesh3DShared>& out_meshes) {
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    for (const auto& primitive : inputMesh.primitives) {
      if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;
      auto new_mesh = std::make_shared<core::Mesh<core::Vertex3D>>();
      new_mesh->name = nodeName;
      if (primitive.material > -1 && primitive.material < (int)materials.size()) {
        new_mesh->SetMaterial(materials[primitive.material]);
      }
      std::vector<uint32_t> indices;
      if (primitive.indices > -1) {
        const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
        const tinygltf::BufferView& bufferView = model.bufferViews[indexAccessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
        const unsigned char* dataPtr = buffer.data.data() + bufferView.byteOffset + indexAccessor.byteOffset;
        if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
          for (size_t i = 0; i < indexAccessor.count; ++i) indices.push_back(dataPtr[i]);
        }
        else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
          const uint16_t* buf = reinterpret_cast<const uint16_t*>(dataPtr);
          for (size_t i = 0; i < indexAccessor.count; ++i) indices.push_back(buf[i]);
        }
        else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
          const uint32_t* buf = reinterpret_cast<const uint32_t*>(dataPtr);
          for (size_t i = 0; i < indexAccessor.count; ++i) indices.push_back(buf[i]);
        }
      }
      new_mesh->SetIndices(indices);
      size_t vertexCount = 0;
      int posStride, normStride, texStride;
      int posType, normType, texType;
      const unsigned char* posPtr = (const unsigned char*)GetAttributePointer(model, primitive, "POSITION", vertexCount, posStride, posType);
      const unsigned char* normPtr = (const unsigned char*)GetAttributePointer(model, primitive, "NORMAL", vertexCount, normStride, normType);
      const unsigned char* texPtr = (const unsigned char*)GetAttributePointer(model, primitive, "TEXCOORD_0", vertexCount, texStride, texType);
      std::vector<core::Vertex3D> vertices;
      vertices.reserve(vertexCount);
      for (size_t i = 0; i < vertexCount; ++i) {
        core::Vertex3D v;
        v.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (posPtr) {
          float p[3];
          memcpy(p, posPtr + (i * posStride), 3 * sizeof(float));
          glm::vec4 pos = transform * glm::vec4(p[0], p[1], p[2], 1.0f);
          v.position = { pos.x, pos.y, pos.z };
        }
        if (normPtr) {
          float n[3];
          memcpy(n, normPtr + (i * normStride), 3 * sizeof(float));
          glm::vec3 norm = glm::normalize(normalMatrix * glm::vec3(n[0], n[1], n[2]));
          v.normal = { norm.x, norm.y, norm.z };
        }
        if (texPtr) {
          float t[2];
          memcpy(t, texPtr + (i * texStride), 2 * sizeof(float));
          v.texCoords = { t[0], t[1] };
        }
        vertices.push_back(v);
      }
      new_mesh->SetVertices(vertices);
      out_meshes.push_back(new_mesh);
    }
  }

  void ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, const glm::mat4& parentMatrix, const std::vector<std::shared_ptr<rendering::Material>>& materials, std::vector<core::Mesh3DShared>& meshes) {
    glm::mat4 globalMatrix = parentMatrix * GetLocalTransform(node);
    if (node.mesh > -1) {
      ProcessMesh(model, model.meshes[node.mesh], node.name, globalMatrix, materials, meshes);
    }
    for (int childIdx : node.children) {
      ProcessNode(model, model.nodes[childIdx], globalMatrix, materials, meshes);
    }
  }

  std::shared_ptr<core::Model<core::Vertex3D>> GLTFLoader::LoadModel(const std::string& filename, rendering::RenderDevice* device, bool normalize) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    bool ret = (filename.find(".glb") != std::string::npos) ? loader.LoadBinaryFromFile(&model, &err, &warn, filename) : loader.LoadASCIIFromFile(&model, &err, &warn, filename);
    if (!ret) return nullptr;

    auto raw_images = LoadRawImages(model);
    std::vector<std::shared_ptr<rendering::WTexture>> gpu_textures;
    for (const auto& raw : raw_images) {
        gpu_textures.push_back(device->CreateTexture2D(raw));
    }

    std::vector<std::shared_ptr<rendering::Material>> materials;
    for (const auto& gltfMat : model.materials) {
      std::shared_ptr<rendering::Material> material = rendering::Material::Create();
      material->name = gltfMat.name;
      rendering::Lighting light_data{ .ambient_strength = 0.5f, .shininess = 32.0f };
      if (gltfMat.pbrMetallicRoughness.baseColorFactor.size() == 4) {
          light_data.color = { (float)gltfMat.pbrMetallicRoughness.baseColorFactor[0], (float)gltfMat.pbrMetallicRoughness.baseColorFactor[1], (float)gltfMat.pbrMetallicRoughness.baseColorFactor[2], (float)gltfMat.pbrMetallicRoughness.baseColorFactor[3] };
      }
      material->SetLighting(light_data);
      int texIdx = gltfMat.pbrMetallicRoughness.baseColorTexture.index;
      if (texIdx >= 0 && texIdx < (int)gpu_textures.size()) {
          material->SetTexture(gpu_textures[texIdx]);
      }
      materials.push_back(material);
    }

    std::vector<core::Mesh3DShared> meshes;
    const tinygltf::Scene& scene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
    for (int nodeIdx : scene.nodes) {
      ProcessNode(model, model.nodes[nodeIdx], glm::mat4(1.0f), materials, meshes);
    }

    if (normalize && !meshes.empty()) {
      glm::vec3 minB(std::numeric_limits<float>::max()), maxB(std::numeric_limits<float>::lowest());
      for (const auto& mesh : meshes) {
        for (const auto& v : mesh->GetVertices()) {
          minB = glm::min(minB, glm::vec3(v.position.x, v.position.y, v.position.z));
          maxB = glm::max(maxB, glm::vec3(v.position.x, v.position.y, v.position.z));
        }
      }
      glm::vec3 center = (minB + maxB) * 0.5f;
      float scale = 1.0f / std::max({maxB.x - minB.x, maxB.y - minB.y, maxB.z - minB.z});
      for (auto& mesh : meshes) {
        auto vertices = mesh->GetVertices();
        for (auto& v : vertices) {
          v.position.x = (v.position.x - center.x) * scale;
          v.position.y = (v.position.y - center.y) * scale;
          v.position.z = (v.position.z - center.z) * scale;
        }
        mesh->SetVertices(vertices);
      }
    }

    auto out = std::make_shared<core::Model<core::Vertex3D>>();
    out->textures = gpu_textures;
    out->materials = materials;
    out->meshes = meshes;
    return out;
  }

} // namespace wlw::utils
