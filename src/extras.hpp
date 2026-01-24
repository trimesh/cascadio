#pragma once

#include <Quantity_Color.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <set>
#include <string>
#include <vector>

// ============================================================================
// Constants
// ============================================================================

/// glTF spec requires 4-byte alignment for buffer views
constexpr uint32_t GLTF_ALIGNMENT = 4;

/// Align value up to specified alignment (default: 4 bytes for glTF)
inline uint32_t alignTo(uint32_t value, uint32_t alignment = GLTF_ALIGNMENT) {
  return (value + (alignment - 1)) & ~(alignment - 1);
}

// ============================================================================
// Face Data for BREP Extension
// ============================================================================

/// Data collected for each BREP face during GLB export via callback.
/// One entry per TopoDS_Face processed.
struct FaceTriangleData {
  int meshIndex;    ///< Unique shape index (0-based), maps to underlying geometry
  int faceIndex;    ///< Face index within this shape (0-based, resets per shape)
  int triStart;     ///< First triangle index in shape's triangle list (0-based)
  int triCount;     ///< Number of triangles generated for this face (>0)
  TopoDS_Face face; ///< Original BREP face for primitive extraction
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

/// Check if a JSON array contains a specific extension name
static bool hasExtension(const rapidjson::Value &arr, const char *name) {
  if (!arr.IsArray()) return false;
  for (const auto &v : arr.GetArray()) {
    if (v.IsString() && std::strcmp(v.GetString(), name) == 0) return true;
  }
  return false;
}

// Forward declaration - implemented in primitives.hpp
static rapidjson::Value extractAllPrimitives(
    const TopoDS_Shape &shape, rapidjson::Document::AllocatorType &alloc,
    const std::set<std::string> &allowedTypes, Standard_Real lengthUnit);

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
    // Group face data by meshIndex (which corresponds to unique shapes)
    std::map<int, std::vector<FaceTriangleData>> facesByUniqueShape;
    for (const auto &fd : faceData) {
      facesByUniqueShape[fd.meshIndex].push_back(fd);
    }

    // Calculate per-unique-shape triangle counts and binary offsets
    // Binary layout: shape0 faceIndices | shape1 faceIndices | ...
    struct ShapeBinaryInfo {
      uint32_t triangleCount;
      uint32_t byteOffset;  // relative to start of faceIndices binary
      uint32_t byteLength;
    };
    std::map<int, ShapeBinaryInfo> shapeBinaryInfo;
    uint32_t currentOffset = 0;

    for (const auto &[shapeIdx, faces] : facesByUniqueShape) {
      // Find max triangle index for this shape
      int maxTriangle = 0;
      for (const auto &fd : faces) {
        maxTriangle = std::max(maxTriangle, fd.triStart + fd.triCount);
      }
      uint32_t byteLen = static_cast<uint32_t>(maxTriangle * sizeof(uint32_t));
      shapeBinaryInfo[shapeIdx] = {
        static_cast<uint32_t>(maxTriangle),
        currentOffset,
        byteLen
      };
      currentOffset += byteLen;
    }

    // Map JSON meshes to BREP shapes via triangle count.
    // Multiple JSON meshes may share one underlying shape (same indices accessor).
    // We match by triangle count since shapes have unique triangle counts in practice.
    // Note: This assumption holds for typical CAD models but could fail for pathological cases.
    std::map<int, int> triCountToShapeIdx;
    for (const auto &[shapeIdx, info] : shapeBinaryInfo) {
      triCountToShapeIdx[info.triangleCount] = shapeIdx;
    }

    // Build mapping from JSON mesh -> unique shape index via indices accessor
    std::map<size_t, int> meshToShapeIdx;
    if (doc.HasMember("meshes") && doc["meshes"].IsArray() &&
        doc.HasMember("accessors") && doc["accessors"].IsArray()) {
      for (size_t meshIdx = 0; meshIdx < doc["meshes"].Size(); ++meshIdx) {
        auto &mesh = doc["meshes"][meshIdx];
        if (!mesh.HasMember("primitives") || !mesh["primitives"].IsArray() ||
            mesh["primitives"].Size() == 0) {
          continue;
        }
        auto &prim = mesh["primitives"][0];
        if (!prim.HasMember("indices")) {
          continue;
        }
        int indicesAccId = prim["indices"].GetInt();
        if (indicesAccId >= 0 && static_cast<size_t>(indicesAccId) < doc["accessors"].Size()) {
          auto &indicesAcc = doc["accessors"][indicesAccId];
          if (indicesAcc.HasMember("count")) {
            int triCount = indicesAcc["count"].GetInt() / 3;
            auto it = triCountToShapeIdx.find(triCount);
            if (it != triCountToShapeIdx.end()) {
              meshToShapeIdx[meshIdx] = it->second;
            }
          }
        }
      }
    }

    // Calculate new binary data layout (4-byte aligned per glTF spec)
    uint32_t alignedBinLength = alignTo(existingBinLength);
    uint32_t faceIndicesBaseOffset = alignedBinLength;
    uint32_t alignedFaceIndicesBytes = alignTo(faceIndicesBytes);
    uint32_t newBinLength = faceIndicesBaseOffset + alignedFaceIndicesBytes;

    // Update buffers[0].byteLength
    if (doc.HasMember("buffers") && doc["buffers"].IsArray() &&
        doc["buffers"].Size() > 0 &&
        doc["buffers"][0].HasMember("byteLength")) {
      doc["buffers"][0]["byteLength"].SetUint(newBinLength);
    }

    // Ensure extensionsUsed contains our extension
    if (!doc.HasMember("extensionsUsed")) {
      doc.AddMember("extensionsUsed", rapidjson::Value(rapidjson::kArrayType),
                    doc.GetAllocator());
    }
    if (!hasExtension(doc["extensionsUsed"], EXTENSION_NAME)) {
      rapidjson::Value extName;
      extName.SetString(EXTENSION_NAME, doc.GetAllocator());
      doc["extensionsUsed"].PushBack(extName, doc.GetAllocator());
    }

    // Create BREP accessor for each unique shape (once per shape, reused by meshes)
    std::map<int, int> shapeIdxToBrepAccessorId;
    for (const auto &[shapeIdx, binInfo] : shapeBinaryInfo) {
      // Add bufferView for this shape's faceIndices
      int bufferViewId = doc.HasMember("bufferViews") && doc["bufferViews"].IsArray()
          ? doc["bufferViews"].Size() : 0;
      if (doc.HasMember("bufferViews")) {
        rapidjson::Value bv(rapidjson::kObjectType);
        bv.AddMember("buffer", 0, doc.GetAllocator());
        bv.AddMember("byteOffset", faceIndicesBaseOffset + binInfo.byteOffset,
                     doc.GetAllocator());
        bv.AddMember("byteLength", binInfo.byteLength, doc.GetAllocator());
        doc["bufferViews"].PushBack(bv, doc.GetAllocator());
      }

      // Add accessor for this shape's faceIndices
      int accessorId = doc.HasMember("accessors") && doc["accessors"].IsArray()
          ? doc["accessors"].Size() : 0;
      if (doc.HasMember("accessors")) {
        rapidjson::Value acc(rapidjson::kObjectType);
        acc.AddMember("bufferView", bufferViewId, doc.GetAllocator());
        acc.AddMember("byteOffset", 0, doc.GetAllocator());
        acc.AddMember("componentType", 5125, doc.GetAllocator()); // UNSIGNED_INT
        acc.AddMember("count", binInfo.triangleCount, doc.GetAllocator());
        acc.AddMember("type", "SCALAR", doc.GetAllocator());
        doc["accessors"].PushBack(acc, doc.GetAllocator());
      }

      shapeIdxToBrepAccessorId[shapeIdx] = accessorId;
    }

    // Pre-build faces arrays for each unique shape
    std::map<int, rapidjson::Value> shapeFacesArrays;
    for (const auto &[shapeIdx, faces] : facesByUniqueShape) {
      rapidjson::Value facesArray(rapidjson::kArrayType);
      for (const auto &fd : faces) {
        extractFacePrimitive(fd.face, fd.faceIndex, facesArray,
                             doc.GetAllocator(), allowedTypes, lengthUnit);
      }
      shapeFacesArrays.emplace(shapeIdx, std::move(facesArray));
    }

    // Add BREP extension to each mesh
    if (doc.HasMember("meshes") && doc["meshes"].IsArray()) {
      for (size_t meshIdx = 0; meshIdx < doc["meshes"].Size(); ++meshIdx) {
        auto shapeIt = meshToShapeIdx.find(meshIdx);
        if (shapeIt == meshToShapeIdx.end()) {
          continue; // No BREP data for this mesh
        }
        int shapeIdx = shapeIt->second;

        auto accIt = shapeIdxToBrepAccessorId.find(shapeIdx);
        if (accIt == shapeIdxToBrepAccessorId.end()) {
          continue;
        }
        int brepAccessorId = accIt->second;

        auto facesIt = shapeFacesArrays.find(shapeIdx);
        if (facesIt == shapeFacesArrays.end()) {
          continue;
        }

        // Add extension to this mesh's primitive
        auto &mesh = doc["meshes"][meshIdx];
        if (mesh.HasMember("primitives") && mesh["primitives"].IsArray() &&
            mesh["primitives"].Size() > 0) {
          auto &prim = mesh["primitives"][0];

          if (!prim.HasMember("extensions")) {
            prim.AddMember("extensions", rapidjson::Value(rapidjson::kObjectType),
                           doc.GetAllocator());
          }

          rapidjson::Value ext(rapidjson::kObjectType);
          ext.AddMember("faceIndices", brepAccessorId, doc.GetAllocator());

          // Copy faces array (need to copy since it may be used by multiple meshes)
          rapidjson::Value facesCopy(facesIt->second, doc.GetAllocator());
          ext.AddMember("faces", facesCopy, doc.GetAllocator());

          // Add materials to extension if provided (only for first mesh)
          if (meshIdx == 0 && materials != nullptr) {
            rapidjson::Value matCopy(*materials, doc.GetAllocator());
            ext.AddMember("materials", matCopy, doc.GetAllocator());
          }

          prim["extensions"].AddMember(rapidjson::StringRef(EXTENSION_NAME), ext,
                                       doc.GetAllocator());
        }
      }
    }
  }

  // Add materials to mesh.extras.cascadio if provided (only for first mesh)
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
