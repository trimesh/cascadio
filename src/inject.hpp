#pragma once

#include "glb.hpp"
#include "gltf_writer.hpp"
#include "primitives.hpp"
#include "substance.hpp"

#include <gp_Trsf.hxx>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

/// Add face and substance data to a finished GLB.
///
/// Every primitive the writer recorded gets the TM_brep_faces extension:
/// a uint32 array appended to the binary buffer (one face index per
/// triangle), the surface of each face, and the substance of the part it
/// belongs to. The extension's name and keys come from the schema
/// (tm_brep_faces.hpp).
///
/// @param densityScale turns stored densities into kg/m^3 (see substance.hpp)
/// @param frame moves the face geometry into the coordinate frame of the mesh
static void injectIntoGlb(Glb &glb, const Recorded &recorded,
                          const std::set<std::string> &allowedTypes,
                          double lengthUnit, std::optional<double> densityScale,
                          const gp_Trsf &frame) {
  rapidjson::Document doc;
  doc.Parse(glb.json.data(), glb.json.size());
  if (doc.HasParseError() || !doc.IsObject()) {
    throw std::runtime_error("GLB contains invalid JSON");
  }
  auto &alloc = doc.GetAllocator();
  auto isArray = [&doc](const char *key) {
    return doc.HasMember(key) && doc[key].IsArray();
  };
  if (!isArray("meshes") || !isArray("accessors") || !isArray("bufferViews") ||
      !isArray("buffers") || doc["buffers"].Size() == 0) {
    return; // an empty scene: nothing to attach anything to
  }

  // The substance of a primitive is that of the part it was made from.
  // Instances of a part share its primitive, so they share this too.
  auto substanceFor = [&](int accessor) {
    const auto found = recorded.parts.find(accessor);
    return found == recorded.parts.end()
               ? rapidjson::Value()
               : partSubstance(found->second, densityScale, alloc);
  };

  // indices accessor id -> {faceIndices accessor id, faces JSON}
  std::map<int, std::pair<int, rapidjson::Value>> extensions;
  // glTF wants every buffer view to start on a 4-byte boundary
  glb.bin.resize((glb.bin.size() + 3) & ~size_t(3));
  for (const auto &[indicesId, list] : recorded.faces) {
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
      throw std::runtime_error("the GLB would be over 4 GB, the format's "
                               "limit; use a larger tol_linear");
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
    if (!mesh.IsObject() || !mesh.HasMember("primitives") ||
        !mesh["primitives"].IsArray()) {
      continue;
    }
    for (auto &prim : mesh["primitives"].GetArray()) {
      if (!prim.HasMember("indices") || !prim["indices"].IsInt()) {
        continue;
      }
      const int indicesId = prim["indices"].GetInt();

      auto found = extensions.find(indicesId);
      if (found == extensions.end()) {
        continue;
      }
      // the faces are copied: instanced parts share one accessor
      rapidjson::Value faces(found->second.second, alloc);
      rapidjson::Value substance = substanceFor(indicesId);
      if (!prim.HasMember("extensions")) {
        prim.AddMember("extensions", rapidjson::Value(rapidjson::kObjectType),
                       alloc);
      }
      prim["extensions"].AddMember(
          rapidjson::StringRef(brep::EXTENSION),
          brep::extension(found->second.first, faces, substance, alloc), alloc);
      attached = true;
    }
  }

  if (attached) {
    if (!isArray("extensionsUsed")) {
      doc.RemoveMember("extensionsUsed");
      doc.AddMember("extensionsUsed", rapidjson::Value(rapidjson::kArrayType),
                    alloc);
    }
    doc["extensionsUsed"].PushBack(rapidjson::StringRef(brep::EXTENSION),
                                   alloc);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  glb.json.assign(buffer.GetString(), buffer.GetSize());
}
