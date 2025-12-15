#pragma once

#include <Quantity_Color.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <set>
#include <string>
#include <vector>

// GLB chunk types
constexpr uint32_t GLB_MAGIC = 0x46546C67; // "glTF"
constexpr uint32_t GLB_VERSION = 2;
constexpr uint32_t GLB_JSON_CHUNK = 0x4E4F534A; // "JSON"
constexpr uint32_t GLB_BIN_CHUNK = 0x004E4942;  // "BIN\0"

// ============================================================================
// JSON Helper Functions
// ============================================================================

/// Helper to add a vec3 array to a JSON object
static void addVec3(rapidjson::Value &obj, const char *name, double x, double y,
                    double z, rapidjson::Document::AllocatorType &alloc) {
  rapidjson::Value arr(rapidjson::kArrayType);
  arr.PushBack(x, alloc).PushBack(y, alloc).PushBack(z, alloc);
  obj.AddMember(rapidjson::StringRef(name), arr, alloc);
}

/// Helper to add an RGBA color array to a JSON object (values 0-1)
static void addColorRGBA(rapidjson::Value &obj, const char *name,
                         const Quantity_ColorRGBA &color,
                         rapidjson::Document::AllocatorType &alloc) {
  rapidjson::Value arr(rapidjson::kArrayType);
  arr.PushBack(color.GetRGB().Red(), alloc)
      .PushBack(color.GetRGB().Green(), alloc)
      .PushBack(color.GetRGB().Blue(), alloc)
      .PushBack(color.Alpha(), alloc);
  obj.AddMember(rapidjson::StringRef(name), arr, alloc);
}

/// Helper to add an RGB color array to a JSON object (values 0-1)
static void addColorRGB(rapidjson::Value &obj, const char *name,
                        const Quantity_Color &color,
                        rapidjson::Document::AllocatorType &alloc) {
  rapidjson::Value arr(rapidjson::kArrayType);
  arr.PushBack(color.Red(), alloc)
      .PushBack(color.Green(), alloc)
      .PushBack(color.Blue(), alloc);
  obj.AddMember(rapidjson::StringRef(name), arr, alloc);
}

/// Helper to add bounds array [min, max] to a JSON object
static void addBounds(rapidjson::Value &obj, const char *name, double min,
                      double max, rapidjson::Document::AllocatorType &alloc) {
  rapidjson::Value arr(rapidjson::kArrayType);
  arr.PushBack(min, alloc).PushBack(max, alloc);
  obj.AddMember(rapidjson::StringRef(name), arr, alloc);
}

// Forward declaration - implemented in primitives.hpp
static rapidjson::Value
extractAllPrimitives(const TopoDS_Shape &shape,
                     rapidjson::Document::AllocatorType &alloc,
                     const std::set<std::string> &allowedTypes,
                     Standard_Real lengthUnit);

// ============================================================================
// GLB Processing (in-memory)
// ============================================================================

/// Inject BREP primitives and materials into GLB data in memory
/// Returns modified GLB data, or empty vector on error
/// lengthUnit is the scale factor to convert to meters (from XCAFDoc_DocumentTool::GetLengthUnit)
static std::vector<char>
injectExtrasIntoGlbData(const std::vector<char> &glbData,
                        const std::vector<TopoDS_Shape> &shapes,
                        const std::set<std::string> &allowedTypes = {},
                        const rapidjson::Value *materials = nullptr,
                        Standard_Real lengthUnit = 1.0) {

  if (glbData.size() < 12) {
    std::cerr << "Error: GLB data too small for header" << std::endl;
    return {};
  }

  const char *ptr = glbData.data();
  const char *end = glbData.data() + glbData.size();

  // Read GLB header (12 bytes)
  uint32_t magic, version, totalLength;
  std::memcpy(&magic, ptr, 4);
  ptr += 4;
  std::memcpy(&version, ptr, 4);
  ptr += 4;
  std::memcpy(&totalLength, ptr, 4);
  ptr += 4;

  if (magic != GLB_MAGIC) {
    std::cerr << "Error: Invalid GLB magic number" << std::endl;
    return {};
  }

  // Read JSON chunk header
  if (ptr + 8 > end) {
    std::cerr << "Error: GLB data too small for JSON chunk header" << std::endl;
    return {};
  }
  uint32_t jsonChunkLength, jsonChunkType;
  std::memcpy(&jsonChunkLength, ptr, 4);
  ptr += 4;
  std::memcpy(&jsonChunkType, ptr, 4);
  ptr += 4;

  if (jsonChunkType != GLB_JSON_CHUNK) {
    std::cerr << "Error: First chunk is not JSON" << std::endl;
    return {};
  }

  // Read JSON data
  if (ptr + jsonChunkLength > end) {
    std::cerr << "Error: GLB data too small for JSON data" << std::endl;
    return {};
  }
  const char *jsonStart = ptr;
  ptr += jsonChunkLength;

  // Read BIN chunk header (may not exist for empty meshes)
  uint32_t binChunkLength = 0;
  const char *binData = nullptr;
  if (ptr + 8 <= end) {
    uint32_t binChunkType;
    std::memcpy(&binChunkLength, ptr, 4);
    ptr += 4;
    std::memcpy(&binChunkType, ptr, 4);
    ptr += 4;

    if (binChunkType == GLB_BIN_CHUNK) {
      binData = ptr;
    } else {
      // Not a BIN chunk, reset
      binChunkLength = 0;
    }
  }

  // Parse JSON
  rapidjson::Document doc;
  doc.Parse(jsonStart, jsonChunkLength);

  if (doc.HasParseError()) {
    std::cerr << "Error: Failed to parse GLB JSON" << std::endl;
    return {};
  }

  // Helper to ensure mesh.extras.cascadio exists
  auto ensureMeshCascadio = [&doc](rapidjson::Value &mesh) {
    if (!mesh.HasMember("extras")) {
      mesh.AddMember("extras", rapidjson::Value(rapidjson::kObjectType),
                     doc.GetAllocator());
    }
    if (!mesh["extras"].HasMember("cascadio")) {
      mesh["extras"].AddMember("cascadio",
                               rapidjson::Value(rapidjson::kObjectType),
                               doc.GetAllocator());
    }
  };

  // Add primitives and materials to each mesh's extras.cascadio
  if (doc.HasMember("meshes") && doc["meshes"].IsArray()) {
    auto &meshes = doc["meshes"];
    size_t numMeshes = meshes.Size();
    size_t numShapes = shapes.size();

    for (size_t i = 0; i < numMeshes; i++) {
      auto &mesh = meshes[i];

      // Add primitives if shapes provided
      if (!shapes.empty() && i < numShapes) {
        ensureMeshCascadio(mesh);
        rapidjson::Value facesArray =
            extractAllPrimitives(shapes[i], doc.GetAllocator(), allowedTypes, lengthUnit);
        mesh["extras"]["cascadio"].AddMember("primitives", facesArray,
                                             doc.GetAllocator());
      }

      // Add materials to each mesh (document-level materials apply to all)
      if (materials != nullptr && materials->IsArray() &&
          materials->Size() > 0) {
        ensureMeshCascadio(mesh);
        rapidjson::Value materialsCopy;
        materialsCopy.CopyFrom(*materials, doc.GetAllocator());
        mesh["extras"]["cascadio"].AddMember("materials", materialsCopy,
                                             doc.GetAllocator());
      }
    }
  }

  // Serialize modified JSON
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  // Pad JSON to 4-byte boundary with spaces
  std::string newJson = buffer.GetString();
  while (newJson.size() % 4 != 0) {
    newJson += ' ';
  }
  uint32_t newJsonLength = static_cast<uint32_t>(newJson.size());

  // Calculate new total length
  uint32_t newTotalLength = 12 + 8 + newJsonLength;
  if (binChunkLength > 0) {
    newTotalLength += 8 + binChunkLength;
  }

  // Build output GLB
  std::vector<char> result;
  result.reserve(newTotalLength);

  // Helper to append data
  auto append = [&result](const void *data, size_t size) {
    const char *p = static_cast<const char *>(data);
    result.insert(result.end(), p, p + size);
  };

  // Write GLB header
  append(&magic, 4);
  append(&version, 4);
  append(&newTotalLength, 4);

  // Write JSON chunk
  uint32_t jsonType = GLB_JSON_CHUNK;
  append(&newJsonLength, 4);
  append(&jsonType, 4);
  result.insert(result.end(), newJson.begin(), newJson.end());

  // Write BIN chunk if present
  if (binChunkLength > 0 && binData != nullptr) {
    uint32_t binType = GLB_BIN_CHUNK;
    append(&binChunkLength, 4);
    append(&binType, 4);
    result.insert(result.end(), binData, binData + binChunkLength);
  }

  return result;
}

/// Inject BREP primitives and materials into GLB file on disk
/// lengthUnit is the scale factor to convert to meters (from XCAFDoc_DocumentTool::GetLengthUnit)
static bool injectExtrasIntoGlb(const char *glbPath,
                                const std::vector<TopoDS_Shape> &shapes,
                                const std::set<std::string> &allowedTypes = {},
                                const rapidjson::Value *materials = nullptr,
                                Standard_Real lengthUnit = 1.0) {
  // Read entire file
  std::ifstream inFile(glbPath, std::ios::binary | std::ios::ate);
  if (!inFile) {
    std::cerr << "Error: Cannot open GLB file for reading" << std::endl;
    return false;
  }

  std::streampos pos = inFile.tellg();
  if (pos == std::streampos(-1) || pos < 0) {
    std::cerr << "Error: Failed to get GLB file size" << std::endl;
    return false;
  }
  std::streamsize size = pos;
  inFile.seekg(0, std::ios::beg);

  std::vector<char> glbData(static_cast<size_t>(size));
  if (!inFile.read(glbData.data(), size)) {
    std::cerr << "Error: Failed to read GLB file" << std::endl;
    return false;
  }
  inFile.close();

  // Process in memory
  std::vector<char> result =
      injectExtrasIntoGlbData(glbData, shapes, allowedTypes, materials, lengthUnit);
  if (result.empty()) {
    return false;
  }

  // Write back to file
  std::ofstream outFile(glbPath, std::ios::binary | std::ios::trunc);
  if (!outFile) {
    std::cerr << "Error: Cannot open GLB file for writing" << std::endl;
    return false;
  }
  outFile.write(result.data(), result.size());
  if (!outFile) {
    std::cerr << "Error: Failed to write GLB file" << std::endl;
    return false;
  }
  outFile.close();

  return true;
}
