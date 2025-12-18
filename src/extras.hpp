#pragma once

#include <Quantity_Color.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <Standard_Integer.hxx>
#include <TopoDS_Face.hxx>
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
// Face Data for BREP Extension
// ============================================================================

/// Data collected for each face during GLB export via callback
struct FaceTriangleData {
  Standard_Integer faceIndex;
  Standard_Integer triStart;
  Standard_Integer triCount;
  TopoDS_Face face;
};

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
static rapidjson::Value extractAllPrimitives(
    const TopoDS_Shape &shape, rapidjson::Document::AllocatorType &alloc,
    const std::set<std::string> &allowedTypes, Standard_Real lengthUnit);

// ============================================================================
// GLB Parsing Helper
// ============================================================================

/// Parse GLB binary format and extract JSON document and binary data
/// Returns true on success, false on error
static bool parseGlb(const std::vector<char> &glbData, rapidjson::Document &doc,
                     const char *&binData, uint32_t &binLength) {
  if (glbData.size() < 12) {
    std::cerr << "Error: GLB data too small for header" << std::endl;
    return false;
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
    return false;
  }

  // Read JSON chunk header
  if (ptr + 8 > end) {
    std::cerr << "Error: GLB data too small for JSON chunk header" << std::endl;
    return false;
  }

  uint32_t jsonChunkLength, jsonChunkType;
  std::memcpy(&jsonChunkLength, ptr, 4);
  ptr += 4;
  std::memcpy(&jsonChunkType, ptr, 4);
  ptr += 4;

  if (jsonChunkType != GLB_JSON_CHUNK) {
    std::cerr << "Error: First chunk is not JSON" << std::endl;
    return false;
  }

  // Read JSON data
  if (ptr + jsonChunkLength > end) {
    std::cerr << "Error: GLB data too small for JSON data" << std::endl;
    return false;
  }

  const char *jsonStart = ptr;
  ptr += jsonChunkLength;

  // Read BIN chunk header (optional)
  binData = nullptr;
  binLength = 0;

  if (ptr + 8 <= end) {
    uint32_t binChunkType;
    std::memcpy(&binLength, ptr, 4);
    ptr += 4;
    std::memcpy(&binChunkType, ptr, 4);
    ptr += 4;

    if (binChunkType == GLB_BIN_CHUNK) {
      binData = ptr;
    } else {
      binLength = 0;
    }
  }

  // Parse JSON
  doc.Parse(jsonStart, jsonChunkLength);
  if (doc.HasParseError()) {
    std::cerr << "Error: Failed to parse GLB JSON" << std::endl;
    return false;
  }

  return true;
}

/// Serialize GLB with modified JSON and binary data
/// Returns serialized GLB data
static std::vector<char> serializeGlb(const rapidjson::Document &doc,
                                      const std::vector<char> &binData) {
  // Serialize JSON
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
  if (!binData.empty()) {
    newTotalLength += 8 + static_cast<uint32_t>(binData.size());
  }

  // Build output GLB
  std::vector<char> result;
  result.reserve(newTotalLength);

  auto append = [&result](const void *data, size_t size) {
    const char *p = static_cast<const char *>(data);
    result.insert(result.end(), p, p + size);
  };

  // Write GLB header
  uint32_t magic = GLB_MAGIC;
  uint32_t version = GLB_VERSION;
  append(&magic, 4);
  append(&version, 4);
  append(&newTotalLength, 4);

  // Write JSON chunk
  uint32_t jsonType = GLB_JSON_CHUNK;
  append(&newJsonLength, 4);
  append(&jsonType, 4);
  result.insert(result.end(), newJson.begin(), newJson.end());

  // Write BIN chunk if present
  if (!binData.empty()) {
    uint32_t binType = GLB_BIN_CHUNK;
    uint32_t binLength = static_cast<uint32_t>(binData.size());
    append(&binLength, 4);
    append(&binType, 4);
    result.insert(result.end(), binData.begin(), binData.end());
  }

  return result;
}

// ============================================================================
// GLB Processing (in-memory)
// ============================================================================

/// Inject BREP primitives and materials into GLB data in memory
/// Returns modified GLB data, or empty vector on error
/// lengthUnit is the scale factor to convert to meters (from
/// XCAFDoc_DocumentTool::GetLengthUnit)
static std::vector<char>
injectExtrasIntoGlbData(const std::vector<char> &glbData,
                        const std::vector<TopoDS_Shape> &shapes,
                        const std::set<std::string> &allowedTypes = {},
                        const rapidjson::Value *materials = nullptr,
                        Standard_Real lengthUnit = 1.0) {

  // Parse GLB
  rapidjson::Document doc;
  const char *binDataPtr = nullptr;
  uint32_t binChunkLength = 0;

  if (!parseGlb(glbData, doc, binDataPtr, binChunkLength)) {
    return {};
  }

  // Extension name constant
  const char *EXTENSION_NAME = "TM_brep_faces";

  // Helper to ensure extensionsUsed array exists and contains our extension
  auto ensureExtensionUsed = [&doc, EXTENSION_NAME]() {
    if (!doc.HasMember("extensionsUsed")) {
      doc.AddMember("extensionsUsed", rapidjson::Value(rapidjson::kArrayType),
                    doc.GetAllocator());
    }
    auto &extUsed = doc["extensionsUsed"];
    bool found = false;
    for (auto &v : extUsed.GetArray()) {
      if (v.IsString() && std::string(v.GetString()) == EXTENSION_NAME) {
        found = true;
        break;
      }
    }
    if (!found) {
      rapidjson::Value extName;
      extName.SetString(EXTENSION_NAME, doc.GetAllocator());
      extUsed.PushBack(extName, doc.GetAllocator());
    }
  };

  // Helper to ensure primitive.extensions.TM_brep_faces exists
  auto ensurePrimitiveExtension =
      [&doc,
       EXTENSION_NAME](rapidjson::Value &primitive) -> rapidjson::Value & {
    if (!primitive.HasMember("extensions")) {
      primitive.AddMember("extensions",
                          rapidjson::Value(rapidjson::kObjectType),
                          doc.GetAllocator());
    }
    if (!primitive["extensions"].HasMember(EXTENSION_NAME)) {
      primitive["extensions"].AddMember(
          rapidjson::StringRef(EXTENSION_NAME),
          rapidjson::Value(rapidjson::kObjectType), doc.GetAllocator());
    }
    return primitive["extensions"][EXTENSION_NAME];
  };

  // Helper to ensure mesh.extras.cascadio exists (for materials which aren't
  // part of TM_brep_faces)
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

  // Add TM_brep_faces extension to mesh primitives
  if (doc.HasMember("meshes") && doc["meshes"].IsArray()) {
    auto &meshes = doc["meshes"];
    size_t numMeshes = meshes.Size();
    size_t numShapes = shapes.size();

    for (size_t i = 0; i < numMeshes; i++) {
      auto &mesh = meshes[i];

      // Add BREP faces extension to mesh primitives
      if (!shapes.empty() && i < numShapes) {
        rapidjson::Value facesArray = extractAllPrimitives(
            shapes[i], doc.GetAllocator(), allowedTypes, lengthUnit);

        // Add to each primitive in the mesh (typically just one)
        if (mesh.HasMember("primitives") && mesh["primitives"].IsArray()) {
          for (auto &primitive : mesh["primitives"].GetArray()) {
            ensureExtensionUsed();
            auto &ext = ensurePrimitiveExtension(primitive);
            // Move facesArray into "faces" property
            rapidjson::Value facesCopy;
            facesCopy.CopyFrom(facesArray, doc.GetAllocator());
            ext.AddMember("faces", facesCopy, doc.GetAllocator());
          }
        }
      }

      // Add materials to mesh.extras.cascadio (not part of TM_brep_faces)
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

  // Prepare binary data for serialization
  std::vector<char> binData;
  if (binDataPtr != nullptr && binChunkLength > 0) {
    binData.assign(binDataPtr, binDataPtr + binChunkLength);
  }

  return serializeGlb(doc, binData);
}

/// Inject BREP primitives and materials into GLB file on disk
/// lengthUnit is the scale factor to convert to meters (from
/// XCAFDoc_DocumentTool::GetLengthUnit)
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
  std::vector<char> result = injectExtrasIntoGlbData(
      glbData, shapes, allowedTypes, materials, lengthUnit);
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

// Forward declaration - defined in primitives.hpp
static void extractFacePrimitive(const TopoDS_Face &face, int faceIndex,
                                 rapidjson::Value &facesArray,
                                 rapidjson::Document::AllocatorType &alloc,
                                 const std::set<std::string> &allowedTypes,
                                 Standard_Real lengthUnit);

/// Modify JSON to add BREP extension metadata (for use with JSON callback)
/// Takes JSON string, face data, and pre-calculated faceIndices binary info
/// Returns modified JSON string
static std::string injectBrepExtensionIntoJson(
    const std::string &jsonString,
    const std::vector<FaceTriangleData> &faceData, uint32_t existingBinLength,
    uint32_t faceIndicesBytes, const std::set<std::string> &allowedTypes,
    const rapidjson::Value *materials, Standard_Real lengthUnit) {

  // Parse JSON
  rapidjson::Document doc;
  doc.Parse(jsonString.c_str(), jsonString.size());
  if (doc.HasParseError()) {
    std::cerr << "Error: Failed to parse JSON for injection" << std::endl;
    return jsonString;
  }

  const char *EXTENSION_NAME = "TM_brep_faces";
  const bool hasBrepData = !faceData.empty() && faceIndicesBytes > 0;

  // Only modify buffers/accessors/bufferViews if we have BREP data
  if (hasBrepData) {
    // Calculate new binary data layout
    uint32_t alignedBinLength =
        (existingBinLength + 3) & ~3; // 4-byte alignment
    uint32_t faceIndicesOffset = alignedBinLength;
    uint32_t alignedFaceIndicesBytes = (faceIndicesBytes + 3) & ~3;
    uint32_t newBinLength = faceIndicesOffset + alignedFaceIndicesBytes;

    // Update buffers[0].byteLength
    if (doc.HasMember("buffers") && doc["buffers"].IsArray() &&
        doc["buffers"].Size() > 0) {
      doc["buffers"][0]["byteLength"].SetUint(newBinLength);
    }

    // Add bufferView for faceIndices
    int faceIndicesBufferViewId =
        doc.HasMember("bufferViews") && doc["bufferViews"].IsArray()
            ? doc["bufferViews"].Size()
            : 0;
    if (doc.HasMember("bufferViews")) {
      rapidjson::Value bv(rapidjson::kObjectType);
      bv.AddMember("buffer", 0, doc.GetAllocator());
      bv.AddMember("byteOffset", faceIndicesOffset, doc.GetAllocator());
      bv.AddMember("byteLength", faceIndicesBytes, doc.GetAllocator());
      doc["bufferViews"].PushBack(bv, doc.GetAllocator());
    }

    // Add accessor for faceIndices
    int faceIndicesAccessorId =
        doc.HasMember("accessors") && doc["accessors"].IsArray()
            ? doc["accessors"].Size()
            : 0;
    if (doc.HasMember("accessors")) {
      rapidjson::Value acc(rapidjson::kObjectType);
      acc.AddMember("bufferView", faceIndicesBufferViewId, doc.GetAllocator());
      acc.AddMember("byteOffset", 0, doc.GetAllocator());
      acc.AddMember("componentType", 5125, doc.GetAllocator()); // UNSIGNED_INT
      acc.AddMember("count", faceIndicesBytes / 4, doc.GetAllocator());
      acc.AddMember("type", "SCALAR", doc.GetAllocator());
      doc["accessors"].PushBack(acc, doc.GetAllocator());
    }

    // Ensure extensionsUsed contains our extension
    if (!doc.HasMember("extensionsUsed")) {
      doc.AddMember("extensionsUsed", rapidjson::Value(rapidjson::kArrayType),
                    doc.GetAllocator());
    }
    bool found = false;
    for (auto &v : doc["extensionsUsed"].GetArray()) {
      if (v.IsString() && std::strcmp(v.GetString(), EXTENSION_NAME) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      rapidjson::Value extName;
      extName.SetString(EXTENSION_NAME, doc.GetAllocator());
      doc["extensionsUsed"].PushBack(extName, doc.GetAllocator());
    }

    // Create faces array with primitive data
    rapidjson::Value facesArray(rapidjson::kArrayType);
    for (const auto &fd : faceData) {
      extractFacePrimitive(fd.face, fd.faceIndex, facesArray,
                           doc.GetAllocator(), allowedTypes, lengthUnit);
    }

    // Add extension to first mesh primitive
    if (doc.HasMember("meshes") && doc["meshes"].IsArray() &&
        doc["meshes"].Size() > 0) {
      auto &mesh = doc["meshes"][0];
      if (mesh.HasMember("primitives") && mesh["primitives"].IsArray() &&
          mesh["primitives"].Size() > 0) {
        auto &prim = mesh["primitives"][0];

        if (!prim.HasMember("extensions")) {
          prim.AddMember("extensions", rapidjson::Value(rapidjson::kObjectType),
                         doc.GetAllocator());
        }

        rapidjson::Value ext(rapidjson::kObjectType);
        ext.AddMember("faceIndices", faceIndicesAccessorId, doc.GetAllocator());
        ext.AddMember("faces", facesArray, doc.GetAllocator());

        prim["extensions"].AddMember(rapidjson::StringRef(EXTENSION_NAME), ext,
                                     doc.GetAllocator());
      }
    }
  }

  // Add materials to mesh.extras.cascadio if provided (independent of BREP
  // data)
  if (materials != nullptr) {
    if (doc.HasMember("meshes") && doc["meshes"].IsArray() &&
        doc["meshes"].Size() > 0) {
      auto &mesh = doc["meshes"][0];
      if (!mesh.HasMember("extras")) {
        mesh.AddMember("extras", rapidjson::Value(rapidjson::kObjectType),
                       doc.GetAllocator());
      }
      if (!mesh["extras"].HasMember("cascadio")) {
        mesh["extras"].AddMember("cascadio",
                                 rapidjson::Value(rapidjson::kObjectType),
                                 doc.GetAllocator());
      }
      rapidjson::Value matCopy(*materials, doc.GetAllocator());
      mesh["extras"]["cascadio"].AddMember("materials", matCopy,
                                           doc.GetAllocator());
    }
  }

  // Serialize back to string
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  return std::string(buffer.GetString(), buffer.GetSize());
}

/// Inject BREP extension with face data collected via callback (LEGACY - for
/// file-based roundtrip) This version creates a faceIndices accessor for
/// per-triangle face mapping Returns modified GLB data, or empty vector on
/// error
static std::vector<char>
injectBrepExtensionWithFaceData(const std::vector<char> &glbData,
                                const std::vector<FaceTriangleData> &faceData,
                                const std::set<std::string> &allowedTypes = {},
                                const rapidjson::Value *materials = nullptr,
                                Standard_Real lengthUnit = 1.0) {

  // Parse GLB
  rapidjson::Document doc;
  const char *binDataPtr = nullptr;
  uint32_t binChunkLength = 0;

  if (!parseGlb(glbData, doc, binDataPtr, binChunkLength)) {
    return {};
  }

  // Extension name constant
  const char *EXTENSION_NAME = "TM_brep_faces";

  // Calculate total triangles from face data
  Standard_Integer totalTriangles = 0;
  for (const auto &fd : faceData) {
    Standard_Integer endTri = fd.triStart + fd.triCount;
    if (endTri < fd.triStart) {
      // Integer overflow check
      std::cerr << "Error: Triangle index overflow" << std::endl;
      return {};
    }
    totalTriangles = std::max(totalTriangles, endTri);
  }

  if (totalTriangles <= 0) {
    std::cerr << "Error: Invalid triangle count" << std::endl;
    return {};
  }

  // Create faceIndices array (per-triangle face index)
  std::vector<uint32_t> faceIndices(totalTriangles, 0);
  for (const auto &fd : faceData) {
    for (Standard_Integer t = 0; t < fd.triCount; ++t) {
      Standard_Integer idx = fd.triStart + t;
      // Bounds check (should always pass given totalTriangles calculation)
      if (idx >= 0 && idx < totalTriangles) {
        faceIndices[idx] = static_cast<uint32_t>(fd.faceIndex);
      }
    }
  }

  // Create faces array with primitive data
  rapidjson::Value facesArray(rapidjson::kArrayType);
  for (const auto &fd : faceData) {
    extractFacePrimitive(fd.face, fd.faceIndex, facesArray, doc.GetAllocator(),
                         allowedTypes, lengthUnit);
  }

  // Build new binary data: original + faceIndices
  std::vector<char> newBinData;
  if (binDataPtr != nullptr && binChunkLength > 0) {
    // Validate binDataPtr points within glbData bounds
    const char *dataStart = glbData.data();
    const char *dataEnd = dataStart + glbData.size();
    if (binDataPtr < dataStart || binDataPtr + binChunkLength > dataEnd) {
      std::cerr << "Error: Binary data pointer out of bounds" << std::endl;
      return {};
    }
    newBinData.assign(binDataPtr, binDataPtr + binChunkLength);
  }

  // Pad to 4-byte alignment before adding faceIndices
  while (newBinData.size() % 4 != 0) {
    newBinData.push_back(0);
  }

  uint32_t faceIndicesOffset = static_cast<uint32_t>(newBinData.size());
  uint32_t faceIndicesBytes =
      static_cast<uint32_t>(faceIndices.size() * sizeof(uint32_t));

  // Append faceIndices data
  const char *faceIndicesPtr =
      reinterpret_cast<const char *>(faceIndices.data());
  newBinData.insert(newBinData.end(), faceIndicesPtr,
                    faceIndicesPtr + faceIndicesBytes);

  // Pad new binary to 4-byte alignment
  while (newBinData.size() % 4 != 0) {
    newBinData.push_back(0);
  }

  uint32_t newBinLength = static_cast<uint32_t>(newBinData.size());

  // Update buffers[0].byteLength
  if (doc.HasMember("buffers") && doc["buffers"].IsArray() &&
      doc["buffers"].Size() > 0) {
    doc["buffers"][0]["byteLength"].SetUint(newBinLength);
  }

  // Add bufferView for faceIndices
  int faceIndicesBufferViewId = 0;
  if (doc.HasMember("bufferViews") && doc["bufferViews"].IsArray()) {
    faceIndicesBufferViewId = doc["bufferViews"].Size();
    rapidjson::Value bv(rapidjson::kObjectType);
    bv.AddMember("buffer", 0, doc.GetAllocator());
    bv.AddMember("byteOffset", faceIndicesOffset, doc.GetAllocator());
    bv.AddMember("byteLength", faceIndicesBytes, doc.GetAllocator());
    doc["bufferViews"].PushBack(bv, doc.GetAllocator());
  }

  // Add accessor for faceIndices
  int faceIndicesAccessorId = 0;
  if (doc.HasMember("accessors") && doc["accessors"].IsArray()) {
    faceIndicesAccessorId = doc["accessors"].Size();
    rapidjson::Value acc(rapidjson::kObjectType);
    acc.AddMember("bufferView", faceIndicesBufferViewId, doc.GetAllocator());
    acc.AddMember("byteOffset", 0, doc.GetAllocator());
    acc.AddMember("componentType", 5125, doc.GetAllocator()); // UNSIGNED_INT
    acc.AddMember("count", static_cast<uint32_t>(faceIndices.size()),
                  doc.GetAllocator());
    acc.AddMember("type", "SCALAR", doc.GetAllocator());
    doc["accessors"].PushBack(acc, doc.GetAllocator());
  }

  // Ensure extensionsUsed contains our extension
  if (!doc.HasMember("extensionsUsed")) {
    doc.AddMember("extensionsUsed", rapidjson::Value(rapidjson::kArrayType),
                  doc.GetAllocator());
  }
  auto &extUsed = doc["extensionsUsed"];
  bool found = false;
  for (auto &v : extUsed.GetArray()) {
    if (v.IsString() && std::string(v.GetString()) == EXTENSION_NAME) {
      found = true;
      break;
    }
  }
  if (!found) {
    rapidjson::Value extName;
    extName.SetString(EXTENSION_NAME, doc.GetAllocator());
    extUsed.PushBack(extName, doc.GetAllocator());
  }

  // Helper to ensure mesh.extras.cascadio exists (for materials)
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

  // Add extension to mesh primitives
  if (doc.HasMember("meshes") && doc["meshes"].IsArray()) {
    for (auto &mesh : doc["meshes"].GetArray()) {
      if (mesh.HasMember("primitives") && mesh["primitives"].IsArray()) {
        for (auto &primitive : mesh["primitives"].GetArray()) {
          // Add extension
          if (!primitive.HasMember("extensions")) {
            primitive.AddMember("extensions",
                                rapidjson::Value(rapidjson::kObjectType),
                                doc.GetAllocator());
          }
          if (!primitive["extensions"].HasMember(EXTENSION_NAME)) {
            rapidjson::Value ext(rapidjson::kObjectType);
            ext.AddMember("faceIndices", faceIndicesAccessorId,
                          doc.GetAllocator());
            // Copy faces array
            rapidjson::Value facesCopy;
            facesCopy.CopyFrom(facesArray, doc.GetAllocator());
            ext.AddMember("faces", facesCopy, doc.GetAllocator());
            primitive["extensions"].AddMember(
                rapidjson::StringRef(EXTENSION_NAME), ext, doc.GetAllocator());
          }
        }
      }

      // Add materials to mesh.extras.cascadio if provided
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

  return serializeGlb(doc, newBinData);
}
