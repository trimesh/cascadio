#pragma once

#include "glb.hpp"
#include "gltf_writer.hpp"
#include "materials.hpp"
#include "primitives.hpp"

#include <TDocStd_Document.hxx>
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

/// Add face and material data to a finished GLB.
///
/// With `withFaces`, every primitive the writer recorded gets the
/// TM_brep_faces extension: a uint32 array appended to the binary buffer
/// (one face index per triangle), the surface of each face, and the
/// materials of the part it belongs to. The extension's name and keys come
/// from the schema (tm_brep_faces.hpp).
///
/// With `withExtras`, each mesh's materials are also written to its
/// `extras.cascadio.materials`. That is the only place for them without the
/// extension, and it is where earlier versions put them.
///
/// @param frame moves the face geometry into the coordinate frame of the mesh
static std::vector<char> injectIntoGlb(const std::vector<char> &glbData,
                                       const Handle(TDocStd_Document) & document,
                                       const Recorded &recorded, bool withFaces,
                                       bool withExtras,
                                       const std::set<std::string> &allowedTypes,
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
  if (!isArray("meshes") || !isArray("accessors") || !isArray("bufferViews") ||
      !isArray("buffers") || doc["buffers"].Size() == 0) {
    return glbData; // an empty scene: nothing to attach anything to
  }

  // The materials of each part, by the accessor id of its primitive.
  // If the file assigns no material to any part, every part gets the list
  // of all materials the file defines, which is the best that can be said.
  std::map<int, rapidjson::Value> assigned;
  bool anyAssigned = false;
  for (const auto &[accessor, part] : recorded.parts) {
    rapidjson::Value materials = partMaterials(part, alloc);
    anyAssigned = anyAssigned || !materials.Empty();
    assigned.emplace(accessor, std::move(materials));
  }
  const rapidjson::Value everything =
      anyAssigned ? rapidjson::Value(rapidjson::kArrayType)
                  : allMaterials(document, alloc);
  auto materialsFor = [&](int accessor) {
    const auto found = assigned.find(accessor);
    const rapidjson::Value &source =
        anyAssigned && found != assigned.end() ? found->second : everything;
    return rapidjson::Value(source, alloc); // a copy
  };

  // indices accessor id -> {faceIndices accessor id, faces JSON}
  std::map<int, std::pair<int, rapidjson::Value>> extensions;
  if (withFaces) {
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
  }

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

      if (withExtras && &prim == mesh["primitives"].Begin()) {
        // one list per mesh, taken from its first primitive
        if (!mesh.HasMember("extras") || !mesh["extras"].IsObject()) {
          mesh.RemoveMember("extras");
          mesh.AddMember("extras", rapidjson::Value(rapidjson::kObjectType),
                         alloc);
        }
        rapidjson::Value cascadio(rapidjson::kObjectType);
        cascadio.AddMember("materials", materialsFor(indicesId), alloc);
        mesh["extras"].RemoveMember("cascadio");
        mesh["extras"].AddMember("cascadio", cascadio, alloc);
      }
      if (!withFaces) {
        break;
      }

      auto found = extensions.find(indicesId);
      if (found == extensions.end()) {
        continue;
      }
      // the faces are copied: instanced parts share one accessor
      rapidjson::Value faces(found->second.second, alloc);
      rapidjson::Value materials = materialsFor(indicesId);
      if (!prim.HasMember("extensions")) {
        prim.AddMember("extensions", rapidjson::Value(rapidjson::kObjectType),
                       alloc);
      }
      prim["extensions"].AddMember(
          rapidjson::StringRef(brep::EXTENSION),
          brep::extension(found->second.first, faces, materials, alloc), alloc);
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
  return glb.serialize();
}
