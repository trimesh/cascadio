#pragma once

#include "inject.hpp"
#include "load.hpp"
#include "sandbox.hpp"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Message_ProgressRange.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <OSD_FileSystem.hxx>
#include <RWMesh_CoordinateSystem.hxx>
#include <RWMesh_NameFormat.hxx>
#include <TCollection_AsciiString.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <gp.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// the numbers in request.hpp are passed straight to OpenCASCADE
static_assert(static_cast<int>(NodeNameFormat::PRODUCT) ==
                  RWMesh_NameFormat_Product &&
              static_cast<int>(NodeNameFormat::INSTANCE) ==
                  RWMesh_NameFormat_Instance &&
              static_cast<int>(NodeNameFormat::INSTANCE_OR_PRODUCT) ==
                  RWMesh_NameFormat_InstanceOrProduct &&
              static_cast<int>(NodeNameFormat::PRODUCT_OR_INSTANCE) ==
                  RWMesh_NameFormat_ProductOrInstance &&
              static_cast<int>(NodeNameFormat::PRODUCT_AND_INSTANCE) ==
                  RWMesh_NameFormat_ProductAndInstance,
              "NodeNameFormat must match RWMesh_NameFormat");

/// Metadata the OCCT writers accept for the output file; we add none.
using FileInfo =
    NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString>;

/// Detect length unit from document or shapes
/// Returns scale factor to convert to meters (e.g., 0.001 for millimeters)
static double detectLengthUnit(const Handle(TDocStd_Document) & doc,
                               const std::vector<TopoDS_Shape> &shapes) {
  double lengthUnit = 1.0;

  // Try to get length unit from document first
  if (XCAFDoc_DocumentTool::GetLengthUnit(doc, lengthUnit)) {
    return lengthUnit;
  }

  // Length unit not stored in document - try to detect from shapes
  // Many STEP files don't populate this attribute correctly.
  if (shapes.empty()) {
    return lengthUnit;
  }

  Bnd_Box bbox;
  for (const auto &shape : shapes) {
    BRepBndLib::Add(shape, bbox);
  }

  if (bbox.IsVoid()) {
    return lengthUnit;
  }

  double xmin, ymin, zmin, xmax, ymax, zmax;
  bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  double maxExtent = std::max({xmax - xmin, ymax - ymin, zmax - zmin});

  // If max extent > 1.0, likely in mm (typical CAD parts are 10-1000mm)
  // glTF expects meters, so if extent suggests mm, use 0.001
  if (maxExtent > 1.0) {
    lengthUnit = 0.001; // Assume millimeters -> meters
  }

  return lengthUnit;
}

/// Where OpenCASCADE's readers and writers find files: in memory, never on
/// the disk. Its writers only take file names, and the worker may not touch
/// the filesystem, so this claims every name OCCT opens through
/// OSD_FileSystem. A name nothing has written is not found.
class MemoryFiles : public OSD_FileSystem {
public:
  bool IsSupportedPath(const TCollection_AsciiString &) const override {
    return true;
  }
  bool IsOpenIStream(const std::shared_ptr<std::istream> &stream) const override {
    return stream != nullptr;
  }
  bool IsOpenOStream(const std::shared_ptr<std::ostream> &stream) const override {
    return stream != nullptr;
  }

  std::shared_ptr<std::streambuf>
  OpenStreamBuffer(const TCollection_AsciiString &url,
                   const std::ios_base::openmode mode, const int64_t offset,
                   int64_t *size) override {
    const std::string name = url.ToCString();
    if ((mode & std::ios_base::out) != 0) {
      // writing always starts a new, empty file
      return files_[name] = std::make_shared<std::stringbuf>(
                 std::ios_base::in | std::ios_base::out |
                 std::ios_base::binary);
    }
    const auto found = files_.find(name);
    if (found == files_.end()) {
      return nullptr;
    }
    std::stringbuf &file = *found->second;
    if (size != nullptr) {
      *size = file.pubseekoff(0, std::ios_base::end, std::ios_base::in);
    }
    file.pubseekpos(offset, std::ios_base::in);
    return found->second;
  }

  /// Everything written to `name`, which is then forgotten.
  std::string take(const std::string &name) {
    const auto found = files_.find(name);
    if (found == files_.end()) {
      return {};
    }
    std::string content = found->second->str();
    files_.erase(found);
    return content;
  }

private:
  std::map<std::string, std::shared_ptr<std::stringbuf>> files_;
};

/// The one MemoryFiles, put in front of OpenCASCADE's own file system on
/// first use. Call it once before any thread starts: OCCT's list of file
/// systems is not safe to change while others read it.
static MemoryFiles &memoryFiles() {
  static const Handle(MemoryFiles) files = [] {
    Handle(MemoryFiles) created = new MemoryFiles();
    OSD_FileSystem::AddDefaultProtocol(created, true);
    return created;
  }();
  return *files;
}

/// Write a meshed document as glTF into memory, optionally recording which
/// triangles came from which BREP face. It is written as .gltf and its
/// buffer, not as .glb: OCCT's GLB writer deletes its temporary buffer with
/// a filesystem call. Glb packs the two into a GLB afterwards.
static Glb writeGlb(const Handle(TDocStd_Document) & doc,
                    const Request &request, Recorded *recorded) {
  GltfWriter writer("cascadio.gltf", recorded);
  writer.SetMergeFaces(request.merge_primitives);
  writer.SetParallel(request.use_parallel);
  writer.SetTransformationFormat(RWGltf_WriterTrsfFormat_Mat4);
  writer.SetNodeNameFormat(
      static_cast<RWMesh_NameFormat>(request.node_name_format));
  if (request.y_up) {
    // with both ends declared the writer rotates Z-up into glTF's Y-up;
    // by default the input frame is undefined and nothing is rotated
    writer.ChangeCoordinateSystemConverter().SetInputCoordinateSystem(
        RWMesh_CoordinateSystem_Zup);
  }
  Span span("write glb");
  MemoryFiles &files = memoryFiles();
  const bool written = writer.Perform(doc, FileInfo(), Message_ProgressRange());
  Glb glb;
  glb.json = files.take("cascadio.gltf");
  glb.bin = files.take("cascadio.bin");
  if (!written || glb.json.empty()) {
    throw std::runtime_error("OpenCASCADE could not write the GLB");
  }
  return glb;
}

/// Convert STEP or IGES bytes to a GLB. The bytes are untrusted, so this
/// needs the lock-down in force.
static Glb toGlb(const Locked &, Request request, const char *data,
                 size_t size) {
  for (const std::string &type : request.brep_types) {
    if (std::find(std::begin(brep::TYPES), std::end(brep::TYPES), type) ==
        std::end(brep::TYPES)) {
      throw std::invalid_argument("unknown brep_types entry: " + type);
    }
  }
  // face and substance data is attached per part,
  // which needs one merged primitive per part
  if (request.include_brep) {
    request.merge_primitives = true;
  }

  Loaded loaded;
  load(loaded, data, size, request);
  Recorded recorded;
  Glb glb =
      writeGlb(loaded.doc, request, request.include_brep ? &recorded : nullptr);
  if (request.include_brep) {
    Span span("face data");
    // scale factor from the file's unit to meters, the unit of glTF
    const double lengthUnit = detectLengthUnit(loaded.doc, loaded.shapes);
    // the face metadata must turn with the mesh: -90 degrees about X
    gp_Trsf frame;
    if (request.y_up) {
      frame.SetRotation(gp::OX(), -M_PI / 2);
    }
    if (!loaded.densityScale) {
      warn("the file gives densities in mass units with different prefixes, "
           "so no density is reported");
    }
    injectIntoGlb(glb, recorded, request.brep_types, lengthUnit,
                  loaded.densityScale, frame);
  }
  return glb;
}
