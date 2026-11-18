#pragma once

#include "glb.hpp"
#include "gltf_writer.hpp"

#include <Quantity_Color.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <algorithm>
#include <cstdint>
#include <cstring>
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

// Forward declaration - defined in primitives.hpp
static void extractFacePrimitive(const TopoDS_Face &face, int faceIndex,
                                 rapidjson::Value &facesArray,
                                 rapidjson::Document::AllocatorType &alloc,
                                 const std::set<std::string> &allowedTypes,
                                 double lengthUnit);

/// Add the TM_brep_faces extension and/or material metadata to a finished GLB.
///
/// For each index accessor the writer recorded faces for, this appends a
/// uint32 array (one face index per triangle) to the binary buffer and attaches
/// `{faceIndices, faces}` to every primitive that uses that accessor.
static std::vector<char> injectIntoGlb(const std::vector<char> &glbData,
                                       const FacesByAccessor &faces,
                                       const std::set<std::string> &allowedTypes,
                                       const rapidjson::Value *materials,
                                       double lengthUnit) {
  static const char *EXTENSION_NAME = "TM_brep_faces";

  Glb glb = Glb::parse(glbData);
  rapidjson::Document doc;
  doc.Parse(glb.json.data(), glb.json.size());
  if (doc.HasParseError() || !doc.IsObject()) {
    throw std::runtime_error("GLB contains invalid JSON");
  }
  auto &alloc = doc.GetAllocator();
  auto isArray = [&doc](const char *key) {
    return doc.HasMember(key) && doc[key].IsArray();
  };
  const bool hasMeshes = isArray("meshes") && doc["meshes"].Size() > 0;

  if (!faces.empty() && hasMeshes && isArray("accessors") &&
      isArray("bufferViews") && isArray("buffers") &&
      doc["buffers"].Size() > 0) {
    glb.bin.resize(alignTo(static_cast<uint32_t>(glb.bin.size())));

    // indices accessor id -> {faceIndices accessor id, faces JSON}
    std::map<int, std::pair<int, rapidjson::Value>> extensions;
    for (const auto &[indicesId, list] : faces) {
      std::vector<uint32_t> faceIndices;
      rapidjson::Value facesJson(rapidjson::kArrayType);
      for (size_t i = 0; i < list.size(); ++i) {
        const FaceTriangles &f = list[i];
        faceIndices.resize(
            std::max(faceIndices.size(), size_t(f.triStart + f.triCount)),
            UINT32_MAX);
        std::fill_n(faceIndices.begin() + f.triStart, f.triCount,
                    static_cast<uint32_t>(i));
        extractFacePrimitive(f.face, static_cast<int>(i), facesJson, alloc,
                             allowedTypes, lengthUnit);
      }

      const size_t byteOffset = glb.bin.size();
      const size_t byteLength = faceIndices.size() * sizeof(uint32_t);
      if (byteOffset + byteLength > UINT32_MAX) {
        throw std::runtime_error("GLB would exceed the 4 GB format limit");
      }
      const char *raw = reinterpret_cast<const char *>(faceIndices.data());
      glb.bin.insert(glb.bin.end(), raw, raw + byteLength);

      rapidjson::Value view(rapidjson::kObjectType);
      view.AddMember("buffer", 0, alloc);
      view.AddMember("byteOffset", static_cast<uint32_t>(byteOffset), alloc);
      view.AddMember("byteLength", static_cast<uint32_t>(byteLength), alloc);
      doc["bufferViews"].PushBack(view, alloc);

      rapidjson::Value accessor(rapidjson::kObjectType);
      accessor.AddMember("bufferView", doc["bufferViews"].Size() - 1, alloc);
      accessor.AddMember("byteOffset", 0, alloc);
      accessor.AddMember("componentType", 5125, alloc); // UNSIGNED_INT
      accessor.AddMember("count", static_cast<uint32_t>(faceIndices.size()),
                         alloc);
      accessor.AddMember("type", "SCALAR", alloc);
      doc["accessors"].PushBack(accessor, alloc);

      extensions.emplace(
          indicesId, std::make_pair(static_cast<int>(doc["accessors"].Size()) - 1,
                                    std::move(facesJson)));
    }

    doc["buffers"][0].RemoveMember("byteLength");
    doc["buffers"][0].AddMember("byteLength",
                                static_cast<uint32_t>(glb.bin.size()), alloc);

    bool attached = false;
    for (auto &mesh : doc["meshes"].GetArray()) {
      if (!mesh.HasMember("primitives") || !mesh["primitives"].IsArray()) {
        continue;
      }
      for (auto &prim : mesh["primitives"].GetArray()) {
        if (!prim.HasMember("indices") || !prim["indices"].IsInt()) {
          continue;
        }
        auto found = extensions.find(prim["indices"].GetInt());
        if (found == extensions.end()) {
          continue;
        }
        rapidjson::Value ext(rapidjson::kObjectType);
        ext.AddMember("faceIndices", found->second.first, alloc);
        // copied because instanced parts share one accessor across meshes
        ext.AddMember("faces", rapidjson::Value(found->second.second, alloc),
                      alloc);
        if (!attached && materials != nullptr) {
          ext.AddMember("materials", rapidjson::Value(*materials, alloc),
                        alloc);
        }
        if (!prim.HasMember("extensions")) {
          prim.AddMember("extensions",
                         rapidjson::Value(rapidjson::kObjectType), alloc);
        }
        prim["extensions"].AddMember(rapidjson::StringRef(EXTENSION_NAME), ext,
                                     alloc);
        attached = true;
      }
    }

    if (attached) {
      if (!isArray("extensionsUsed")) {
        doc.RemoveMember("extensionsUsed");
        doc.AddMember("extensionsUsed",
                      rapidjson::Value(rapidjson::kArrayType), alloc);
      }
      doc["extensionsUsed"].PushBack(rapidjson::StringRef(EXTENSION_NAME),
                                     alloc);
    }
  }

  // Materials are also stored on the first mesh's extras so they are
  // available without BREP data.
  if (materials != nullptr && hasMeshes && doc["meshes"][0].IsObject()) {
    auto &mesh = doc["meshes"][0];
    if (!mesh.HasMember("extras") || !mesh["extras"].IsObject()) {
      mesh.RemoveMember("extras");
      mesh.AddMember("extras", rapidjson::Value(rapidjson::kObjectType), alloc);
    }
    auto &extras = mesh["extras"];
    if (!extras.HasMember("cascadio") || !extras["cascadio"].IsObject()) {
      extras.RemoveMember("cascadio");
      extras.AddMember("cascadio", rapidjson::Value(rapidjson::kObjectType),
                       alloc);
    }
    extras["cascadio"].AddMember("materials",
                                 rapidjson::Value(*materials, alloc), alloc);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  glb.json.assign(buffer.GetString(), buffer.GetSize());
  return glb.serialize();
}
