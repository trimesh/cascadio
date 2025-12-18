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
        
        // Add materials to extension if provided (so both BREP and materials
        // are processed together by the same extension handler)
        if (materials != nullptr) {
          rapidjson::Value matCopy(*materials, doc.GetAllocator());
          ext.AddMember("materials", matCopy, doc.GetAllocator());
        }

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
