#pragma once

#include <Quantity_Color.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <algorithm>
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
#include <unordered_set>
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

/// Safely get pointer to first primitive of a mesh, or nullptr if unavailable
static const rapidjson::Value* getFirstPrimitive(const rapidjson::Value &mesh) {
  if (!mesh.HasMember("primitives") || !mesh["primitives"].IsArray() ||
      mesh["primitives"].Size() == 0) {
    return nullptr;
  }
  return &mesh["primitives"][0];
}

// Forward declaration - implemented in primitives.hpp
static rapidjson::Value extractAllPrimitives(
    const TopoDS_Shape &shape, rapidjson::Document::AllocatorType &alloc,
    const std::set<std::string> &allowedTypes, double lengthUnit);

// Forward declaration - defined in primitives.hpp
static void extractFacePrimitive(const TopoDS_Face &face, int faceIndex,
                                 rapidjson::Value &facesArray,
                                 rapidjson::Document::AllocatorType &alloc,
                                 const std::set<std::string> &allowedTypes,
                                 double lengthUnit);

/// Modify JSON to add BREP extension metadata (for use with JSON callback)
/// Takes JSON string, face data, and pre-calculated faceIndices binary info
/// Returns modified JSON string
static std::string injectBrepExtensionIntoJson(
    const std::string &jsonString,
    const std::vector<FaceTriangleData> &faceData, uint32_t existingBinLength,
    uint32_t faceIndicesBytes, const std::set<std::string> &allowedTypes,
    const rapidjson::Value *materials, double lengthUnit) {

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

    for (const auto &[meshIdx, faces] : facesByUniqueShape) {
      // Find max triangle index for this mesh
      int maxTriangle = 0;
      for (const auto &fd : faces) {
        maxTriangle = std::max(maxTriangle, fd.triStart + fd.triCount);
      }
      uint32_t byteLen = static_cast<uint32_t>(maxTriangle * sizeof(uint32_t));
      shapeBinaryInfo[meshIdx] = {
        static_cast<uint32_t>(maxTriangle),
        currentOffset,
        byteLen
      };
      currentOffset += byteLen;
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

    // Build mapping from JSON mesh index to callback meshIndex (shape index).
    // Multiple JSON meshes may share the same indices accessor (mesh instancing).
    // Accessor IDs are assigned in binary write order, so lower ID = lower callback meshIndex.
    std::map<size_t, int> jsonMeshToShapeIdx;
    if (doc.HasMember("meshes") && doc["meshes"].IsArray()) {
      // Single pass: collect unique accessor IDs and track which meshes use them
      std::set<int> seenAccessorIds;
      std::vector<std::pair<size_t, int>> meshAccessorPairs; // (meshIdx, accessorId)

      for (size_t i = 0; i < doc["meshes"].Size(); ++i) {
        const auto* prim = getFirstPrimitive(doc["meshes"][i]);
        if (prim && prim->HasMember("indices")) {
          int accId = (*prim)["indices"].GetInt();
          if (accId >= 0) {  // Defensive: skip invalid accessor IDs
            seenAccessorIds.insert(accId);
            meshAccessorPairs.emplace_back(i, accId);
          }
        }
      }

      // Build accessor ID -> shape index mapping (std::set iterates in sorted order)
      std::map<int, int> accessorToShapeIdx;
      int shapeIdx = 0;
      for (int accId : seenAccessorIds) {
        accessorToShapeIdx[accId] = shapeIdx++;
      }

      // Map each mesh to its shape index
      for (const auto& [meshIdx, accId] : meshAccessorPairs) {
        jsonMeshToShapeIdx[meshIdx] = accessorToShapeIdx[accId];
      }
    }

    // Add BREP extension to each mesh
    if (doc.HasMember("meshes") && doc["meshes"].IsArray()) {
      for (size_t meshIdx = 0; meshIdx < doc["meshes"].Size(); ++meshIdx) {
        // Look up the shape index for this JSON mesh using accessor ID ordering
        auto shapeIdxIt = jsonMeshToShapeIdx.find(meshIdx);
        if (shapeIdxIt == jsonMeshToShapeIdx.end()) {
          continue; // No shape index mapping for this mesh
        }
        int shapeIdx = shapeIdxIt->second;

        auto accIt = shapeIdxToBrepAccessorId.find(shapeIdx);
        if (accIt == shapeIdxToBrepAccessorId.end()) {
          continue; // No BREP data for this shape
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

/// Modify JSON to add per-mesh edge lines as LINES primitives
/// Each shape with edges provides vertex bytes, index bytes, and min/max bounds.
/// edgeColor must be [R, G, B, A] in [0,1] range (default gray).
static std::string injectEdgeLinesIntoJson(
    const std::string &jsonString,
    uint32_t existingBinLength,
    const std::vector<uint32_t> &shapeVertBytes,
    const std::vector<uint32_t> &shapeIdxBytes,
    const std::vector<float> &shapeMinX,
    const std::vector<float> &shapeMinY,
    const std::vector<float> &shapeMinZ,
    const std::vector<float> &shapeMaxX,
    const std::vector<float> &shapeMaxY,
    const std::vector<float> &shapeMaxZ,
    const std::vector<float> &edgeColor = {0.25f, 0.25f, 0.25f, 1.0f}) {

  rapidjson::Document doc;
  doc.Parse(jsonString.c_str(), jsonString.size());
  if (doc.HasParseError()) {
    std::cerr << "Error: Failed to parse JSON for edge injection" << std::endl;
    return jsonString;
  }

  // Validate input sizes
  size_t numShapes = shapeVertBytes.size();
  if (numShapes == 0) return jsonString;

  // Build per-shape layout info
  struct ShapeLayout {
    uint32_t vertByteLength;
    uint32_t idxByteLength;
    int vertBufViewId;
    int idxBufViewId;
    int vertAccId;
    int idxAccId;
    float minX, minY, minZ, maxX, maxY, maxZ;
  };
  std::vector<ShapeLayout> layouts(numShapes);
  uint32_t totalVertBytes = 0;
  uint32_t totalIdxBytes = 0;
  for (size_t i = 0; i < numShapes; i++) {
    layouts[i].vertByteLength = shapeVertBytes[i];
    layouts[i].idxByteLength = shapeIdxBytes[i];
    layouts[i].vertBufViewId = layouts[i].idxBufViewId = 0;
    layouts[i].vertAccId = layouts[i].idxAccId = 0;
    layouts[i].minX = shapeMinX[i]; layouts[i].minY = shapeMinY[i]; layouts[i].minZ = shapeMinZ[i];
    layouts[i].maxX = shapeMaxX[i]; layouts[i].maxY = shapeMaxY[i]; layouts[i].maxZ = shapeMaxZ[i];
    totalVertBytes += layouts[i].vertByteLength;
    totalIdxBytes += layouts[i].idxByteLength;
  }
  if (totalVertBytes == 0 || totalIdxBytes == 0) {
    return jsonString;
  }

  // Buffer layout: [existing data] [padding] [all verts] [padding] [all indices]
  uint32_t vertSectionBase = alignTo(existingBinLength);
  uint32_t idxSectionBase = alignTo(vertSectionBase + totalVertBytes);
  uint32_t newBinLength = alignTo(idxSectionBase + totalIdxBytes);

  // Update buffers[0].byteLength
  if (doc.HasMember("buffers") && doc["buffers"].IsArray() &&
      doc["buffers"].Size() > 0 &&
      doc["buffers"][0].HasMember("byteLength")) {
    doc["buffers"][0]["byteLength"].SetUint(newBinLength);
  }

  // Compute per-shape buffer offsets within sections
  uint32_t vertOffset = vertSectionBase;
  uint32_t idxOffset = idxSectionBase;
  for (size_t i = 0; i < layouts.size(); i++) {
    auto &l = layouts[i];
    if (l.vertByteLength == 0) continue;

    // BufferView for this shape's edge vertices
    l.vertBufViewId = doc.HasMember("bufferViews") && doc["bufferViews"].IsArray()
        ? doc["bufferViews"].Size() : 0;
    if (doc.HasMember("bufferViews")) {
      rapidjson::Value bv(rapidjson::kObjectType);
      bv.AddMember("buffer", 0, doc.GetAllocator());
      bv.AddMember("byteOffset", vertOffset, doc.GetAllocator());
      bv.AddMember("byteLength", l.vertByteLength, doc.GetAllocator());
      doc["bufferViews"].PushBack(bv, doc.GetAllocator());
    }

    // BufferView for this shape's edge indices
    l.idxBufViewId = doc.HasMember("bufferViews") && doc["bufferViews"].IsArray()
        ? doc["bufferViews"].Size() : 0;
    if (doc.HasMember("bufferViews")) {
      rapidjson::Value bv(rapidjson::kObjectType);
      bv.AddMember("buffer", 0, doc.GetAllocator());
      bv.AddMember("byteOffset", idxOffset, doc.GetAllocator());
      bv.AddMember("byteLength", l.idxByteLength, doc.GetAllocator());
      doc["bufferViews"].PushBack(bv, doc.GetAllocator());
    }

    // Accessor for edge vertices (VEC3 float)
    l.vertAccId = doc.HasMember("accessors") && doc["accessors"].IsArray()
        ? doc["accessors"].Size() : 0;
    if (doc.HasMember("accessors")) {
      rapidjson::Value acc(rapidjson::kObjectType);
      acc.AddMember("bufferView", static_cast<int>(l.vertBufViewId), doc.GetAllocator());
      acc.AddMember("byteOffset", 0, doc.GetAllocator());
      acc.AddMember("componentType", 5126, doc.GetAllocator()); // FLOAT
      acc.AddMember("count", static_cast<int>(l.vertByteLength / (3 * sizeof(float))), doc.GetAllocator());
      acc.AddMember("type", "VEC3", doc.GetAllocator());
      rapidjson::Value minArr(rapidjson::kArrayType);
      minArr.PushBack(l.minX, doc.GetAllocator())
           .PushBack(l.minY, doc.GetAllocator())
           .PushBack(l.minZ, doc.GetAllocator());
      acc.AddMember("min", minArr, doc.GetAllocator());
      rapidjson::Value maxArr(rapidjson::kArrayType);
      maxArr.PushBack(l.maxX, doc.GetAllocator())
           .PushBack(l.maxY, doc.GetAllocator())
           .PushBack(l.maxZ, doc.GetAllocator());
      acc.AddMember("max", maxArr, doc.GetAllocator());
      doc["accessors"].PushBack(acc, doc.GetAllocator());
    }

    // Accessor for edge indices (SCALAR uint32)
    l.idxAccId = doc.HasMember("accessors") && doc["accessors"].IsArray()
        ? doc["accessors"].Size() : 0;
    if (doc.HasMember("accessors")) {
      rapidjson::Value acc(rapidjson::kObjectType);
      acc.AddMember("bufferView", static_cast<int>(l.idxBufViewId), doc.GetAllocator());
      acc.AddMember("byteOffset", 0, doc.GetAllocator());
      acc.AddMember("componentType", 5125, doc.GetAllocator()); // UNSIGNED_INT
      acc.AddMember("count", static_cast<int>(l.idxByteLength / sizeof(uint32_t)), doc.GetAllocator());
      acc.AddMember("type", "SCALAR", doc.GetAllocator());
      doc["accessors"].PushBack(acc, doc.GetAllocator());
    }

    vertOffset += l.vertByteLength;
    idxOffset += l.idxByteLength;
  }

  // Build mapping from JSON mesh index to shape index using indices accessor IDs.
  // Multiple JSON meshes may share the same indices accessor (mesh instancing).
  // Accessor IDs reflect the order RWGltf_CafWriter processed shapes (document traversal order).
  //
  // CRITICAL — Two invariants that MUST hold:
  // 1. Use insertion order (first-occurrence), NOT sorted order. std::set sorts by value,
  //    which would break alignment with perShapeEdges (built from XCAF document explorer).
  // 2. Skip face-less (LINES-only) meshes. RWGltf_CafWriter creates meshes for 1D
  //    topology (edges/curves without faces), but perShapeEdges only contains face-having
  //    shapes (matched by the TopAbs_FACE check in convert.hpp). Including those accessor
  //    IDs shifts the mapping and causes wrong edge attachment for subsequent face meshes.
  std::map<size_t, int> jsonMeshToShapeIdx;
  if (doc.HasMember("meshes") && doc["meshes"].IsArray()) {
    std::vector<int> seenAccessorIds;       // preserves insertion (= traversal) order
    std::unordered_set<int> seenAccSet;      // O(1) duplicate check
    std::vector<std::pair<size_t, int>> meshAccessorPairs;
    for (size_t i = 0; i < doc["meshes"].Size(); ++i) {
      const auto& mesh = doc["meshes"][i];
      // Skip face-less meshes (LINES-only) — RWGltf_CafWriter generates these for
      // 1D topology that has no faces, and perShapeEdges excludes such shapes.
      bool hasTriangles = false;
      if (mesh.HasMember("primitives") && mesh["primitives"].IsArray()) {
        for (const auto& prim : mesh["primitives"].GetArray()) {
          int mode = prim.HasMember("mode") ? prim["mode"].GetInt() : 4;
          if (mode == 4) { hasTriangles = true; break; }
        }
      }
      if (!hasTriangles) continue;

      const auto* prim = getFirstPrimitive(mesh);
      if (prim && prim->HasMember("indices")) {
        int accId = (*prim)["indices"].GetInt();
        if (accId >= 0) {
          if (seenAccSet.find(accId) == seenAccSet.end()) {
            seenAccSet.insert(accId);
            seenAccessorIds.push_back(accId);
          }
          meshAccessorPairs.emplace_back(i, accId);
        }
      }
    }
    std::map<int, int> accessorToShapeIdx;
    int shapeIdx = 0;
    for (int accId : seenAccessorIds) {     // iterate in insertion order
      accessorToShapeIdx[accId] = shapeIdx++;
    }
    for (const auto& [mIdx, accId] : meshAccessorPairs) {
      jsonMeshToShapeIdx[mIdx] = accessorToShapeIdx[accId];
    }
  }

  // Create edge material with the specified color
  int edgeMatId = -1;
  {
    rapidjson::Value mat(rapidjson::kObjectType);
    rapidjson::Value pbr(rapidjson::kObjectType);
    rapidjson::Value color(rapidjson::kArrayType);
    float ecr = edgeColor.size() > 0 ? edgeColor[0] : 0.25f;
    float ecg = edgeColor.size() > 1 ? edgeColor[1] : 0.25f;
    float ecb = edgeColor.size() > 2 ? edgeColor[2] : 0.25f;
    float eca = edgeColor.size() > 3 ? edgeColor[3] : 1.0f;
    color.PushBack(ecr, doc.GetAllocator())
        .PushBack(ecg, doc.GetAllocator())
        .PushBack(ecb, doc.GetAllocator())
        .PushBack(eca, doc.GetAllocator());
    pbr.AddMember("baseColorFactor", color, doc.GetAllocator());
    mat.AddMember("pbrMetallicRoughness", pbr, doc.GetAllocator());
    mat.AddMember("doubleSided", true, doc.GetAllocator());

    if (!doc.HasMember("materials") || !doc["materials"].IsArray()) {
      rapidjson::Value arr(rapidjson::kArrayType);
      arr.PushBack(mat, doc.GetAllocator());
      doc.AddMember("materials", arr, doc.GetAllocator());
      edgeMatId = 0;
    } else {
      edgeMatId = doc["materials"].Size();
      doc["materials"].PushBack(mat, doc.GetAllocator());
    }
  }

  // Add LINES primitive to each mesh using the correct shape mapping
  for (size_t meshIdx = 0; meshIdx < doc["meshes"].Size(); ++meshIdx) {
    auto siIt = jsonMeshToShapeIdx.find(meshIdx);
    if (siIt == jsonMeshToShapeIdx.end()) continue;
    int si = siIt->second;
    if (si < 0 || (size_t)si >= layouts.size()) continue;
    const auto &l = layouts[si];
    if (l.vertByteLength == 0) continue;

    auto &mesh = doc["meshes"][meshIdx];
    if (!mesh.HasMember("primitives") || !mesh["primitives"].IsArray()) continue;

    rapidjson::Value linePrim(rapidjson::kObjectType);
    linePrim.AddMember("mode", 1, doc.GetAllocator()); // LINES
    linePrim.AddMember("material", edgeMatId, doc.GetAllocator());
    rapidjson::Value attrs(rapidjson::kObjectType);
    attrs.AddMember("POSITION", static_cast<int>(l.vertAccId), doc.GetAllocator());
    linePrim.AddMember("attributes", attrs, doc.GetAllocator());
    linePrim.AddMember("indices", static_cast<int>(l.idxAccId), doc.GetAllocator());
    mesh["primitives"].PushBack(linePrim, doc.GetAllocator());
  }

  // Serialize back to string
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  return std::string(buffer.GetString(), buffer.GetSize());
}
