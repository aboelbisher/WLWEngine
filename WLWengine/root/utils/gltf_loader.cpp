#include <iostream>
#include <memory>

#include "gltf_loader.h"
#include "core/logger.h"
#include "core/mesh.h"
#include "core/model.h"
#include "rendering/material.h"

#include "tiny_gltf.h"
#include "image_loader.h"

namespace wlw::utils {




  std::vector<std::shared_ptr<utils::Texture>> LoadTextures(const tinygltf::Model & inputModel) {
    std::vector<std::shared_ptr<utils::Texture>> textures;
    for (const auto& gltfTex : inputModel.textures) {
      tinygltf::Image gltfImage = inputModel.images[gltfTex.source];
      core::Vector2 size = { .x = static_cast<float>(gltfImage.width), .y = static_cast<float>(gltfImage.height) };
      auto texture = std::make_shared<utils::Texture>(size, gltfImage.component, gltfImage.image);
      textures.push_back(texture);
    }
    return textures;
  }

  std::vector<std::shared_ptr<rendering::Material>> LoadMaterials(const tinygltf::Model& inputModel, const std::vector<std::shared_ptr<utils::Texture>>& textures) {
    std::vector<std::shared_ptr<rendering::Material>> materials;

    for (const auto& gltfMat : inputModel.materials) {
      std::shared_ptr<rendering::Material> new_material = rendering::Material::Create();
      new_material->name = gltfMat.name;

      // PBR Data
      // Resolve Index -> Pointer immediately
      int texture_index = gltfMat.pbrMetallicRoughness.baseColorTexture.index;
      if (texture_index > -1 && texture_index < textures.size()) {
        auto texture = textures[texture_index];
        new_material->SetTexture(texture);
      }

      new_material->roughness = (float)gltfMat.pbrMetallicRoughness.roughnessFactor;
      new_material->metallic = (float)gltfMat.pbrMetallicRoughness.metallicFactor;

      // Optional: Convert PBR -> Phong
      rendering::Lighting light_data{
        .ambient_strength = 1.0,
        .shininess = (1.0f - new_material->roughness) * 128.0f
      };

      //lightData.color = { 1.0f, 1.0f, 1.0f, 1.0f };
      //lightData.shininess = (1.0f - new_material->roughness) * 128.0f;
      //new_material->lighting_ = lightData;

      new_material->SetLighting(light_data);
      materials.push_back(new_material);
    }
    return materials;
  }

  // Helper to safely get a pointer to the data at index 'i'
  const void* GetAttributePointer(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const std::string& attributeName, size_t& count, int& stride, int& type) {
    if (primitive.attributes.find(attributeName) == primitive.attributes.end()) return nullptr;

    const int accessorIdx = primitive.attributes.at(attributeName);
    const tinygltf::Accessor& accessor = model.accessors[accessorIdx];
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];

    // Validates that the user's Vertex struct matches the data type (Float vs Byte)
    type = accessor.componentType;
    count = accessor.count;

    // CRITICAL FIX: Get the stride in BYTES directly from GLTF. 
    // If ByteStride is 0, the spec says the data is tightly packed.
    stride = accessor.ByteStride(view);
    if (stride == 0) {
      // Calculate tight stride based on type
      // (Simplified for standard attributes: VEC3/float = 12, VEC2/float = 8)
      if (accessor.type == TINYGLTF_TYPE_VEC3) stride = 12;
      else if (accessor.type == TINYGLTF_TYPE_VEC2) stride = 8;
    }

    return &buffer.data[view.byteOffset + accessor.byteOffset];
  }

  // Helper to extract GLM Matrix from GLTF Node
  glm::mat4 GetLocalTransform(const tinygltf::Node& node) {
    glm::mat4 matrix(1.0f);

    // 1. Matrix property (if defined, it overrides T/R/S)
    if (node.matrix.size() == 16) {
      // GLTF stores column-major, GLM accepts column-major.
      // We convert double[] to float for GLM
      float floats[16];
      for (int i = 0; i < 16; i++) floats[i] = (float)node.matrix[i];
      matrix = glm::make_mat4(floats);
    }
    else {
      // 2. Translation
      if (node.translation.size() == 3) {
        matrix = glm::translate(matrix, glm::vec3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]));
      }
      // 3. Rotation (Quaternion)
      if (node.rotation.size() == 4) {
        // GLTF Quaternions are stored as (x, y, z, w)
        // GLM Quat constructor is (w, x, y, z)
        glm::quat rot((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]);
        matrix *= glm::mat4_cast(rot);
      }
      // 4. Scale
      if (node.scale.size() == 3) {
        matrix = glm::scale(matrix, glm::vec3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]));
      }
    }
    return matrix;
  }


  void ProcessMesh(const tinygltf::Model& model, const tinygltf::Mesh& inputMesh, Model& outModel, const std::string& nodeName, const glm::mat4& transform) {

    // Pre-calculate Normal Matrix (Inverse Transpose) to handle non-uniform scaling correctly
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));

    for (const auto& primitive : inputMesh.primitives) {
      Mesh newMesh;
      newMesh.name = nodeName;
      if (primitive.material > -1 && primitive.material < outModel.materials.size()) {
        newMesh.material = outModel.materials[primitive.material];
      }

      // --- 1. INDICES ---
      if (primitive.indices > -1) {
        const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
        const tinygltf::BufferView& bufferView = model.bufferViews[indexAccessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
        const unsigned char* dataPtr = buffer.data.data() + bufferView.byteOffset + indexAccessor.byteOffset;

        if (indexAccessor.componentType == tinygltf::TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
          const uint8_t* buf = reinterpret_cast<const uint8_t*>(dataPtr);
          for (size_t i = 0; i < indexAccessor.count; ++i) newMesh.indices.push_back(buf[i]);
        }
        else if (indexAccessor.componentType == tinygltf::TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
          const uint16_t* buf = reinterpret_cast<const uint16_t*>(dataPtr);
          for (size_t i = 0; i < indexAccessor.count; ++i) newMesh.indices.push_back(buf[i]);
        }
        else if (indexAccessor.componentType == tinygltf::TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
          const uint32_t* buf = reinterpret_cast<const uint32_t*>(dataPtr);
          for (size_t i = 0; i < indexAccessor.count; ++i) newMesh.indices.push_back(buf[i]);
        }
      }

      // --- 2. ATTRIBUTES ---
      size_t vertexCount = 0;
      int posStride, normStride, texStride;
      int posType, normType, texType;

      const unsigned char* posPtr = (const unsigned char*)GetAttributePointer(model, primitive, "POSITION", vertexCount, posStride, posType);
      const unsigned char* normPtr = (const unsigned char*)GetAttributePointer(model, primitive, "NORMAL", vertexCount, normStride, normType);
      const unsigned char* texPtr = (const unsigned char*)GetAttributePointer(model, primitive, "TEXCOORD_0", vertexCount, texStride, texType);

      for (size_t i = 0; i < vertexCount; ++i) {
        Vertex v;

        // Position: Apply Transform (Baking)
        if (posPtr) {
          const float* p = reinterpret_cast<const float*>(posPtr + (i * posStride));
          glm::vec4 pos = glm::vec4(p[0], p[1], p[2], 1.0f);
          pos = transform * pos; // Bake transform
          v.position = { pos.x, pos.y, pos.z };
        }

        // Normal: Apply Normal Matrix (Baking)
        if (normPtr) {
          const float* n = reinterpret_cast<const float*>(normPtr + (i * normStride));
          glm::vec3 norm = glm::vec3(n[0], n[1], n[2]);
          norm = glm::normalize(normalMatrix * norm); // Bake rotation/scale
          v.normal = { norm.x, norm.y, norm.z };
        }
        else {
          v.normal = { 0, 0, 0 };
        }

        // TexCoord: No transform usually
        if (texPtr) {
          const float* t = reinterpret_cast<const float*>(texPtr + (i * texStride));
          v.texCoord = { t[0], t[1] };
        }
        else {
          v.texCoord = { 0, 0 };
        }
        newMesh.vertices.push_back(v);
      }
      outModel.meshes.push_back(newMesh);
    }
  }
  void ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, Model& outModel, const glm::mat4& parentMatrix) {
    // Calculate Global Transform for this node
    glm::mat4 localMatrix = GetLocalTransform(node);
    glm::mat4 globalMatrix = parentMatrix * localMatrix;

    if (node.mesh > -1) {
      const tinygltf::Mesh& mesh = model.meshes[node.mesh];
      // Pass the CALCULATED Global Matrix to bake into vertices
      ProcessMesh(model, mesh, outModel, node.name, globalMatrix);
    }
    for (size_t i = 0; i < node.children.size(); ++i) {
      ProcessNode(model, model.nodes[node.children[i]], outModel, globalMatrix);
    }
  }


  std::shared_ptr<core::Model<core::Vertex3D>> GLTFLoader::LoadModel(const std::string& filename) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ret = false;
    if (filename.find(".glb") != std::string::npos) {
      // Binary GLTF
      ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
    }
    else {
      // ASCII JSON GLTF
      ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
    }

    if (!warn.empty()) {
      std::cout << "GLTF Warning: " << warn << std::endl;
    }

    if (!err.empty()) {
      std::cerr << "GLTF Error: " << err << std::endl;
    }

    if (!ret) {
      std::cerr << "Failed to parse GLTF: " << filename << std::endl;
      return nullptr;
    }

    // 1. Load Textures
    auto textures = LoadTextures(model);
    auto materials = LoadMaterials(model, textures);

    std::vector<core::Mesh3DShared> meshes;

    // 2. Load Nodes (Recursive traversal is best for scene graphs)
    const tinygltf::Scene& scene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
      ProcessNode(model, model.nodes[scene.nodes[i]], meshes, materials);
    }


    auto out_model = std::make_shared<core::Model<core::Vertex3D>>();
    out_model->textures = textures;
    out_model->materials = materials;
    out_model->meshes = meshes;
    return out_model;
  }



} // namespace wlw::utils
