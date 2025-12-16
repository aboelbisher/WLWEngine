#include <iostream>
#include <memory>

#include "gltf_loader.h"
#include "core/logger.h"
#include "core/mesh.h"
#include "core/model.h"
#include "rendering/material.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>


#include "tiny_gltf.h"
#include "image_loader.h"


namespace wlw::utils {


  std::vector<std::shared_ptr<utils::Texture>> LoadTextures(const tinygltf::Model & inputModel) {
    std::vector<std::shared_ptr<utils::Texture>> textures;
    for (const auto& gltfTex : inputModel.textures) {

      //std::shared_ptr<utils::Texture> texture = utils::ImageLoader::LoadImage("cube.jpg");

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

      
      // Lighting setup...
      rendering::Lighting light_data{
          .ambient_strength = 1.0f, // High ambient to ensure visibility for debugging
          .shininess = (1.0f - new_material->roughness) * 128.0f
      };

      // 1. Load PBR Base Color Factor (RGBA)
      // glTF defines this as a vector of 4 floats (default is 1,1,1,1)
      if (gltfMat.pbrMetallicRoughness.baseColorFactor.size() == 4) {
        light_data.color = {
            (float)gltfMat.pbrMetallicRoughness.baseColorFactor[0],
            (float)gltfMat.pbrMetallicRoughness.baseColorFactor[1],
            (float)gltfMat.pbrMetallicRoughness.baseColorFactor[2],
            (float)gltfMat.pbrMetallicRoughness.baseColorFactor[3]
        };
      }
      else {
        light_data.color = { 1.0f, 1.0f, 1.0f, 1.0f };
      }

      new_material->SetLighting(light_data);


      // 2. Load Texture
      int texture_index = gltfMat.pbrMetallicRoughness.baseColorTexture.index;
      if (texture_index > -1 && texture_index < textures.size()) {
        auto texture = textures[texture_index];
        new_material->SetTexture(texture);
      }
   
      new_material->roughness = (float)gltfMat.pbrMetallicRoughness.roughnessFactor;
      new_material->metallic = (float)gltfMat.pbrMetallicRoughness.metallicFactor;


      materials.push_back(new_material);
    }
    return materials;
  }

  const void* GetAttributePointer(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const std::string& attributeName, size_t& count, int& stride, int& type) {
    if (primitive.attributes.find(attributeName) == primitive.attributes.end()) return nullptr;

    const int accessorIdx = primitive.attributes.at(attributeName);
    const tinygltf::Accessor& accessor = model.accessors[accessorIdx];

    // FIX 1: Check for Draco Compression / Sparse Accessors
    // If bufferView is undefined (-1), the data is not in a standard buffer.
    if (accessor.bufferView < 0) {
      std::cerr << "Error: Attribute '" << attributeName << "' has no bufferView (likely Draco compressed). This loader does not support Draco." << std::endl;
      return nullptr;
    }

    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];

    type = accessor.componentType;
    count = accessor.count;

    // FIX 2: Correct Stride Calculation
    // If ByteStride is 0 (tightly packed), we must calculate it based on the ACTUAL component type, not just assuming Float.
    stride = accessor.ByteStride(view);
    if (stride == 0) {
      int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
      int numComponents = tinygltf::GetNumComponentsInType(accessor.type);
      stride = componentSize * numComponents;
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

  void ProcessMesh(const tinygltf::Model& model, const tinygltf::Mesh& inputMesh, const std::string& nodeName, const glm::mat4& transform, const std::vector<std::shared_ptr<rendering::Material>>& materials, std::vector<core::Mesh3DShared>& out_meshes) {

    // Pre-calculate Normal Matrix
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));

    for (const auto& primitive : inputMesh.primitives) {

      // FIX 1: Check Primitive Mode
      // 4 = TRIANGLES. 5 = TRIANGLE_STRIP, 6 = TRIANGLE_FAN.
      // If your engine expects GL_TRIANGLES, loading strips will look "corrupted".
      if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
        std::cerr << "Warning: Primitive mode " << primitive.mode << " not supported (Only TRIANGLES). Mesh: " << nodeName << std::endl;
        continue;
      }

      auto new_mesh = std::make_shared<core::Mesh<core::Vertex3D>>();
      new_mesh->name = nodeName;

      if (primitive.material > -1 && primitive.material < materials.size()) {
        new_mesh->SetMaterial(materials[primitive.material]);
      }

      // --- 1. INDICES ---
      std::vector<uint32_t> indices;
      if (primitive.indices > -1) {
        const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
        const tinygltf::BufferView& bufferView = model.bufferViews[indexAccessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
        const unsigned char* dataPtr = buffer.data.data() + bufferView.byteOffset + indexAccessor.byteOffset;

        // GLTF Indices are always Integer types, simple casting works fine here.
        if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
          const uint8_t* buf = reinterpret_cast<const uint8_t*>(dataPtr);
          for (size_t i = 0; i < indexAccessor.count; ++i) indices.push_back(buf[i]);
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

      // --- 2. ATTRIBUTES ---
      size_t vertexCount = 0;
      int posStride, normStride, texStride;
      int posType = 0, normType = 0, texType = 0; // Initialize types

      // We use 'unsigned char*' for byte-wise pointer arithmetic
      const unsigned char* posPtr = (const unsigned char*)GetAttributePointer(model, primitive, "POSITION", vertexCount, posStride, posType);
      const unsigned char* normPtr = (const unsigned char*)GetAttributePointer(model, primitive, "NORMAL", vertexCount, normStride, normType);
      const unsigned char* texPtr = (const unsigned char*)GetAttributePointer(model, primitive, "TEXCOORD_0", vertexCount, texStride, texType);

      // FIX 2: Type Validation
      // If the GLTF uses Compressed (Short) Positions, blind float casting will explode the mesh.
      if (posPtr && posType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        std::cerr << "Error: Vertex Position is not FLOAT (Type: " << posType << "). Compression not supported." << std::endl;
        return;
      }
      if (normPtr && normType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        std::cerr << "Error: Vertex Normal is not FLOAT. Compression not supported." << std::endl;
        // Can continue, but normals will be wrong.
      }

      std::vector<core::Vertex3D> vertices;
      vertices.reserve(vertexCount);

      for (size_t i = 0; i < vertexCount; ++i) {
        core::Vertex3D v;

        // POSITION
        if (posPtr) {
          // FIX 3: Safe Memory Read (memcpy)
          // Avoids reinterpret_cast alignment issues.
          float p[3];
          memcpy(p, posPtr + (i * posStride), 3 * sizeof(float));

          glm::vec4 pos = glm::vec4(p[0], p[1], p[2], 1.0f);
          pos = transform * pos;
          v.position = { pos.x, pos.y, pos.z };
        }

        // NORMAL
        if (normPtr) {
          float n[3];
          memcpy(n, normPtr + (i * normStride), 3 * sizeof(float));

          glm::vec3 norm = glm::vec3(n[0], n[1], n[2]);
          norm = glm::normalize(normalMatrix * norm);
          v.normal = { norm.x, norm.y, norm.z };
        } else {
          v.normal = { 0, 0, 0 };
        }

        // TEXCOORD
        if (texPtr) {
          // UVs can be FLOAT (5126), UNSIGNED_BYTE (5121), or UNSIGNED_SHORT (5123)
          if (texType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            float t[2];
            memcpy(t, texPtr + (i * texStride), 2 * sizeof(float));
            v.texCoords = { t[0], t[1] };
          } else if (texType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) { // Handle common quantized UVs (normalized unsigned short)
            unsigned short t[2];
            memcpy(t, texPtr + (i * texStride), 2 * sizeof(unsigned short));
            // Normalize 0..65535 to 0.0..1.0
            v.texCoords = { t[0] / 65535.0f, t[1] / 65535.0f };
          }
          else {
            v.texCoords = { 0, 0 }; // Fallback
          }
        }
        else {
          v.texCoords = { 0, 0 };
        }

        vertices.push_back(v);
      }
      new_mesh->SetVertices(vertices);
      out_meshes.push_back(new_mesh);
    }
  }

  void ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, const glm::mat4& parentMatrix, const std::vector<std::shared_ptr<rendering::Material>>& materials,  std::vector<core::Mesh3DShared>& meshes) {
    // Calculate Global Transform for this node
    glm::mat4 localMatrix = GetLocalTransform(node);
    glm::mat4 globalMatrix = parentMatrix * localMatrix;

    if (node.mesh > -1) {
      const tinygltf::Mesh& mesh = model.meshes[node.mesh];
      // Pass the CALCULATED Global Matrix to bake into vertices
      ProcessMesh(model, mesh,  node.name, globalMatrix, materials, meshes);
    }
    for (size_t i = 0; i < node.children.size(); ++i) {
      ProcessNode(model, model.nodes[node.children[i]], globalMatrix, materials, meshes);
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

    glm::mat4 rootTransform = glm::mat4(1.0f);

    // 2. Load Nodes (Recursive traversal is best for scene graphs)
    const tinygltf::Scene& scene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
      ProcessNode(model, model.nodes[scene.nodes[i]], rootTransform, materials, meshes);
    }


    auto out_model = std::make_shared<core::Model<core::Vertex3D>>();
    out_model->textures = textures;
    out_model->materials = materials;
    out_model->meshes = meshes;
    return out_model;
  }



} // namespace wlw::utils
