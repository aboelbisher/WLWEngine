#include <iostream>
#include <memory>

#include "gltf_loader.h"
#include "core/logger.h"

#include "tiny_gltf.h"


namespace wlw::utils {

  //bool LoadModel(const std::string& filename, Model& outModel) {
  //  tinygltf::Model model;
  //  tinygltf::TinyGLTF loader;
  //  std::string err;
  //  std::string warn;

  //  bool ret = false;
  //  if (filename.find(".glb") != std::string::npos) {
  //    // Binary GLTF
  //    ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
  //  }
  //  else {
  //    // ASCII JSON GLTF
  //    ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
  //  }

  //  if (!warn.empty()) {
  //    std::cout << "GLTF Warning: " << warn << std::endl;
  //  }

  //  if (!err.empty()) {
  //    std::cerr << "GLTF Error: " << err << std::endl;
  //  }

  //  if (!ret) {
  //    std::cerr << "Failed to parse GLTF: " << filename << std::endl;
  //    return false;
  //  }

  //  // 1. Load Textures
  //  LoadTextures(model, outModel);

  //  // 2. Load Nodes (Recursive traversal is best for scene graphs)
  //  const tinygltf::Scene& scene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
  //  for (size_t i = 0; i < scene.nodes.size(); ++i) {
  //    ProcessNode(model, model.nodes[scene.nodes[i]], outModel);
  //  }

  //  return true;
  //}

  //void LoadTextures(const tinygltf::Model& inputModel, Model& outModel) {
  //  for (const auto& gltfTex : inputModel.textures) {
  //    tinygltf::Image gltfImage = inputModel.images[gltfTex.source];

  //    Texture newTex;
  //    newTex.filePath = gltfImage.uri;
  //    newTex.width = gltfImage.width;
  //    newTex.height = gltfImage.height;
  //    newTex.channels = gltfImage.component;

  //    // In a real engine, you would upload 'gltfImage.image.data()' to GPU here
  //    // and store the ID. For now, we just log it.
  //    std::cout << "Loaded Texture: " << (newTex.filePath.empty() ? "Embedded" : newTex.filePath)
  //      << " (" << newTex.width << "x" << newTex.height << ")" << std::endl;

  //    outModel.textures.push_back(newTex);
  //  }
  //}

  //// Recursively process nodes to handle hierarchy (parent-child transforms)
  //void ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, Model& outModel) {
  //  // If node contains a mesh, load it
  //  if (node.mesh > -1) {
  //    const tinygltf::Mesh& mesh = model.meshes[node.mesh];
  //    ProcessMesh(model, mesh, outModel, node.name);
  //  }

  //  // Process children
  //  for (size_t i = 0; i < node.children.size(); ++i) {
  //    ProcessNode(model, model.nodes[node.children[i]], outModel);
  //  }
  //}

  //void ProcessMesh(const tinygltf::Model& model, const tinygltf::Mesh& inputMesh, Model& outModel, const std::string& nodeName) {
  //  // A Mesh in GLTF can have multiple primitives (sub-meshes with different materials)
  //  for (const auto& primitive : inputMesh.primitives) {
  //    Mesh newMesh;
  //    newMesh.name = nodeName.empty() ? inputMesh.name : nodeName;
  //    newMesh.materialIndex = primitive.material;

  //    // --- 1. EXTRACT INDICES ---
  //    if (primitive.indices > -1) {
  //      const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
  //      const tinygltf::BufferView& bufferView = model.bufferViews[indexAccessor.bufferView];
  //      const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

  //      const unsigned char* dataPtr = buffer.data.data() + bufferView.byteOffset + indexAccessor.byteOffset;

  //      // Indices can be stored as different types (unsigned short, unsigned int, etc.)
  //      // We must normalize them to our engine's format (std::vector<unsigned int>)
  //      if (indexAccessor.componentType == tinygltf::TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
  //        const unsigned short* buf = reinterpret_cast<const unsigned short*>(dataPtr);
  //        for (size_t i = 0; i < indexAccessor.count; ++i) {
  //          newMesh.indices.push_back(buf[i]);
  //        }
  //      }
  //      else if (indexAccessor.componentType == tinygltf::TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
  //        const unsigned int* buf = reinterpret_cast<const unsigned int*>(dataPtr);
  //        for (size_t i = 0; i < indexAccessor.count; ++i) {
  //          newMesh.indices.push_back(buf[i]);
  //        }
  //      }
  //    }

  //    // --- 2. EXTRACT ATTRIBUTES (Position, Normal, TexCoord) ---

  //    // Helper variables to store data pointers
  //    const float* positionBuffer = nullptr;
  //    const float* normalBuffer = nullptr;
  //    const float* texCoordBuffer = nullptr;

  //    size_t vertexCount = 0;
  //    int posStride = 0, normStride = 0, texStride = 0;

  //    // Find attributes in the map
  //    if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
  //      const int accessorIdx = primitive.attributes.at("POSITION");
  //      const tinygltf::Accessor& accessor = model.accessors[accessorIdx];
  //      const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
  //      positionBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[view.byteOffset + accessor.byteOffset]));
  //      vertexCount = accessor.count;
  //      // byteStride might be 0 if tightly packed. If so, calculate based on type (Vec3 = 12 bytes)
  //      posStride = accessor.ByteStride(model.bufferViews[accessor.bufferView]) ? (accessor.ByteStride(model.bufferViews[accessor.bufferView]) / sizeof(float)) : 3;
  //    }

  //    if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
  //      const int accessorIdx = primitive.attributes.at("NORMAL");
  //      const tinygltf::Accessor& accessor = model.accessors[accessorIdx];
  //      const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
  //      normalBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[view.byteOffset + accessor.byteOffset]));
  //      normStride = accessor.ByteStride(model.bufferViews[accessor.bufferView]) ? (accessor.ByteStride(model.bufferViews[accessor.bufferView]) / sizeof(float)) : 3;
  //    }

  //    if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
  //      const int accessorIdx = primitive.attributes.at("TEXCOORD_0");
  //      const tinygltf::Accessor& accessor = model.accessors[accessorIdx];
  //      const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
  //      texCoordBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[view.byteOffset + accessor.byteOffset]));
  //      texStride = accessor.ByteStride(model.bufferViews[accessor.bufferView]) ? (accessor.ByteStride(model.bufferViews[accessor.bufferView]) / sizeof(float)) : 2;
  //    }

  //    // --- 3. ASSEMBLE VERTICES ---
  //    for (size_t i = 0; i < vertexCount; ++i) {
  //      Vertex v;

  //      // Get Position
  //      if (positionBuffer) {
  //        v.position.x = positionBuffer[i * posStride + 0];
  //        v.position.y = positionBuffer[i * posStride + 1];
  //        v.position.z = positionBuffer[i * posStride + 2];
  //      }

  //      // Get Normal
  //      if (normalBuffer) {
  //        v.normal.x = normalBuffer[i * normStride + 0];
  //        v.normal.y = normalBuffer[i * normStride + 1];
  //        v.normal.z = normalBuffer[i * normStride + 2];
  //      }
  //      else {
  //        v.normal = { 0, 0, 0 }; // Default if missing
  //      }

  //      // Get UV
  //      if (texCoordBuffer) {
  //        v.texCoord.u = texCoordBuffer[i * texStride + 0];
  //        v.texCoord.v = texCoordBuffer[i * texStride + 1];
  //      }
  //      else {
  //        v.texCoord = { 0, 0 };
  //      }

  //      newMesh.vertices.push_back(v);
  //    }

  //    std::cout << "Processed Mesh: " << newMesh.name
  //      << " | Verts: " << newMesh.vertices.size()
  //      << " | Indices: " << newMesh.indices.size() << std::endl;

  //    outModel.meshes.push_back(newMesh);
  //  }
  //}


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
        vertices[i].color = core::Color(1.0,1.0,1.0, 1.0);
        // Color is not typically in glTF vertex attributes, so we'll leave it as default.
    }

    mesh->SetVertices(vertices);
    mesh->SetIndices(indices);

    return mesh;
}

} // namespace wlw::utils
