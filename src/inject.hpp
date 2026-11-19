#pragma once

#include "glb.hpp"
#include "gltf_writer.hpp"
#include "primitives.hpp"

#include <gp_Trsf.hxx>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

/// Add the TM_brep_faces extension and/or material metadata to a finished GLB.
///
/// For each index accessor the writer recorded faces for, this appends a
/// uint32 array (one face index per triangle) to the binary buffer and attaches
/// the extension object to every primitive that uses that accessor. The
/// extension's name and keys come from the schema (tm_brep_faces.hpp).
///
/// `faces` is null when BREP data was not asked for. Then only the
/// materials are written, to the first mesh's `extras.cascadio.materials`,
/// because there is no extension object to hold them.
/// `frame` moves the face geometry into the coordinate frame of the mesh.
static std::vector<char> injectIntoGlb(const std::vector<char> &glbData,
                                       const FacesByAccessor *faces,
                                       const std::set<std::string> &allowedTypes,
                                       const rapidjson::Value &materials,
                                       double lengthUnit,
                                       const gp_Trsf &frame) {
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

  if (faces != nullptr && !faces->empty() && hasMeshes &&
      isArray("accessors") &&
      isArray("bufferViews") && isArray("buffers") &&
      doc["buffers"].Size() > 0) {
    // glTF wants every buffer view to start on a 4-byte boundary
    glb.bin.resize((glb.bin.size() + 3) & ~size_t(3));

    // indices accessor id -> {faceIndices accessor id, faces JSON}
    std::map<int, std::pair<int, rapidjson::Value>> extensions;
    for (const auto &[indicesId, list] : *faces) {
      std::vector<uint32_t> faceIndices;
      rapidjson::Value facesJson(rapidjson::kArrayType);
      for (size_t i = 0; i < list.size(); ++i) {
        const FaceTriangles &f = list[i];
        faceIndices.resize(
            std::max(faceIndices.size(), size_t(f.triStart + f.triCount)),
            UINT32_MAX);
        std::fill_n(faceIndices.begin() + f.triStart, f.triCount,
                    static_cast<uint32_t>(i));
        facesJson.PushBack(describeFace(f.face, static_cast<int>(i), alloc,
                                        allowedTypes, lengthUnit, frame),
                           alloc);
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
        // copied: instanced parts share one accessor across meshes, and
        // the material list describes the whole model
        rapidjson::Value facesCopy(found->second.second, alloc);
        rapidjson::Value materialsCopy(materials, alloc);
        rapidjson::Value ext = brep::extension(found->second.first, facesCopy,
                                               materialsCopy, alloc);
        if (!prim.HasMember("extensions")) {
          prim.AddMember("extensions",
                         rapidjson::Value(rapidjson::kObjectType), alloc);
        }
        prim["extensions"].AddMember(rapidjson::StringRef(brep::EXTENSION), ext,
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
      doc["extensionsUsed"].PushBack(rapidjson::StringRef(brep::EXTENSION),
                                     alloc);
    }
  }

  if (faces == nullptr && hasMeshes && doc["meshes"][0].IsObject()) {
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
                                 rapidjson::Value(materials, alloc), alloc);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  glb.json.assign(buffer.GetString(), buffer.GetSize());
  return glb.serialize();
}
