#pragma once

#include "inject.hpp"
#include "filehandle.hpp"
#include "load.hpp"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Message_ProgressRange.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <RWMesh_CoordinateSystem.hxx>
#include <RWMesh_NameFormat.hxx>
#include <RWObj_CafWriter.hxx>
#include <TCollection_AsciiString.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <gp.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

/// Everything that controls a GLB conversion.
struct ConvertOptions {
  MeshOptions mesh;
  bool merge_primitives = true;
  bool include_brep = false;
  std::set<std::string> brep_types;
  bool include_materials = false;
  RWMesh_NameFormat node_name_format = RWMesh_NameFormat_InstanceOrProduct;
  /// Rotate from CAD's Z-up into glTF's Y-up: (x, y, z) -> (x, z, -y)
  bool y_up = false;
};

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

/// Write a meshed document to GLB bytes, optionally recording which
/// triangles came from which BREP face.
/// OCCT's writer needs file paths, so both the output and its temporary
/// binary buffer are memfd-backed on Linux (no filesystem writes) and
/// temp files elsewhere.
static std::vector<char> writeGlb(const Handle(TDocStd_Document) & doc,
                                  const ConvertOptions &options,
                                  Recorded *recorded) {
  FileHandle handle(".glb");
  if (!handle.valid()) {
    throw std::runtime_error("could not create a temporary file for the GLB");
  }
  // A memfd output has no directory for OCCT's sibling `.bin.tmp`,
  // so give the binary buffer its own memfd.
  std::optional<FileHandle> binHandle;
  if (handle.is_memfd()) {
    binHandle.emplace(".bin");
  }

  GltfWriter writer(handle.path(),
                    binHandle && binHandle->valid() ? binHandle->path()
                                                    : nullptr,
                    recorded);
  writer.SetMergeFaces(options.merge_primitives);
  writer.SetParallel(options.mesh.use_parallel);
  writer.SetTransformationFormat(RWGltf_WriterTrsfFormat_Mat4);
  writer.SetNodeNameFormat(options.node_name_format);
  if (options.y_up) {
    // with both ends declared the writer rotates Z-up into glTF's Y-up;
    // by default the input frame is undefined and nothing is rotated
    writer.ChangeCoordinateSystemConverter().SetInputCoordinateSystem(
        RWMesh_CoordinateSystem_Zup);
  }
  {
    Span span("write glb");
    if (!writer.Perform(doc, FileInfo(), Message_ProgressRange())) {
      throw std::runtime_error("OpenCASCADE could not write the GLB");
    }
  }

  return handle.read_all();
}

/// Convert STEP or IGES bytes to GLB bytes without touching the disk.
static std::vector<char> toGlb(const char *data, size_t size,
                               FileType file_type, ConvertOptions options) {
  for (const std::string &type : options.brep_types) {
    if (std::find(std::begin(brep::TYPES), std::end(brep::TYPES), type) ==
        std::end(brep::TYPES)) {
      throw std::invalid_argument("unknown brep_types entry: " + type);
    }
  }
  // face and material metadata is attached per part,
  // which needs one merged primitive per part
  if (options.include_brep || options.include_materials) {
    options.merge_primitives = true;
  }

  Loaded loaded;
  load(loaded, data, size, file_type, options.mesh);

  // BREP data always carries the materials: they are part of the extension
  const bool annotate = options.include_brep || options.include_materials;
  Recorded recorded;
  std::vector<char> glb =
      writeGlb(loaded.doc, options, annotate ? &recorded : nullptr);
  if (glb.empty()) {
    throw std::runtime_error("OpenCASCADE wrote an empty GLB");
  }

  if (annotate) {
    Span span("face data");
    // scale factor from the file's unit to meters, the unit of glTF
    const double lengthUnit = detectLengthUnit(loaded.doc, loaded.shapes);
    // the face metadata must turn with the mesh: -90 degrees about X
    gp_Trsf frame;
    if (options.y_up) {
      frame.SetRotation(gp::OX(), -M_PI / 2);
    }
    glb = injectIntoGlb(glb, loaded.doc, recorded, options.include_brep,
                        options.include_materials, options.brep_types,
                        lengthUnit, frame);
  }
  return glb;
}

/// Convert STEP bytes to an OBJ file. A `.mtl` file is written next to it
/// when the STEP has colors and `use_colors` is set.
static void stepToObj(const char *data, size_t size,
                      const std::string &output_path, const MeshOptions &mesh,
                      bool use_colors) {
  Loaded loaded;
  load(loaded, data, size, FileType::STEP, mesh, use_colors);
  RWObj_CafWriter writer(output_path.c_str());
  if (!writer.Perform(loaded.doc, FileInfo(), Message_ProgressRange())) {
    throw std::runtime_error("OpenCASCADE could not write the OBJ");
  }
}
