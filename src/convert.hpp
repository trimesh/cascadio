#pragma once

#include "filehandle.hpp"
#include "iges.hpp"
#include "materials.hpp"
#include "primitives.hpp"
#include "step.hpp"

#include <Message_ProgressRange.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <TCollection_AsciiString.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <cstddef>

// Meshing
#include <BRepMesh_IncrementalMesh.hxx>
// Bounding box for unit detection
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
// GLTF Write methods
#include <RWGltf_CafWriter.hxx>
// OBJ Write methods
#include <RWObj_CafWriter.hxx>

// ============================================================================
// File Type Enum
// ============================================================================

enum class FileType { UNSPECIFIED = 0, STEP = 1, IGES = 2 };

// ============================================================================
// Generic Loading (dispatches to STEP or IGES)
// ============================================================================

/// Generic load result structure
struct LoadResult {
  Handle(TDocStd_Document) doc;
  std::vector<TopoDS_Shape> shapes;
  bool success;

  LoadResult() : success(false) {}
};

/// Load a BREP file (STEP or IGES) from disk
static LoadResult loadFile(const char *input_path, FileType file_type,
                           double tol_linear, double tol_angle,
                           bool tol_relative, bool use_parallel,
                           bool use_colors = true) {
  LoadResult result;

  if (file_type == FileType::STEP) {
    StepLoadResult stepResult =
        loadStepFile(input_path, tol_linear, tol_angle, tol_relative,
                     use_parallel, use_colors);
    result.doc = stepResult.doc;
    result.shapes = stepResult.shapes;
    result.success = stepResult.success;
  } else if (file_type == FileType::IGES) {
    IgesLoadResult igesResult =
        loadIgesFile(input_path, tol_linear, tol_angle, tol_relative,
                     use_parallel, use_colors);
    result.doc = igesResult.doc;
    result.shapes = igesResult.shapes;
    result.success = igesResult.success;
  } else {
    std::cerr << "Error: Unsupported file type" << std::endl;
  }

  return result;
}

/// Load a BREP file (STEP or IGES) from memory
static LoadResult loadBytes(const char *data, size_t data_len,
                            FileType file_type,
                            double tol_linear, double tol_angle,
                            bool tol_relative, bool use_parallel,
                            bool use_colors = true) {
  LoadResult result;

  if (file_type == FileType::STEP) {
    StepLoadResult stepResult = loadStepBytes(
        data, data_len, tol_linear, tol_angle, tol_relative, use_parallel, use_colors);
    result.doc = stepResult.doc;
    result.shapes = stepResult.shapes;
    result.success = stepResult.success;
  } else if (file_type == FileType::IGES) {
    IgesLoadResult igesResult = loadIgesBytes(
        data, data_len, tol_linear, tol_angle, tol_relative, use_parallel, use_colors);
    result.doc = igesResult.doc;
    result.shapes = igesResult.shapes;
    result.success = igesResult.success;
  } else {
    std::cerr << "Error: Unsupported file type" << std::endl;
  }

  return result;
}

// ============================================================================
// GLB Export
// ============================================================================

/// Detect length unit from document or shapes
/// Returns scale factor to convert to meters (e.g., 0.001 for millimeters)
static double detectLengthUnit(Handle(TDocStd_Document) doc,
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
static std::vector<char> exportToGlbBytes(
    Handle(TDocStd_Document) doc, bool merge_primitives, bool use_parallel,
    FacesByAccessor *faces = nullptr,
    RWMesh_NameFormat node_name_format = RWMesh_NameFormat_InstanceOrProduct) {
  FileHandle handle(".glb");
  if (!handle.valid()) {
    std::cerr << "Error: Failed to create file handle for GLB export"
              << std::endl;
    return {};
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
                    faces);
  writer.SetMergeFaces(merge_primitives);
  writer.SetParallel(use_parallel);
  writer.SetTransformationFormat(RWGltf_WriterTrsfFormat_Mat4);
  writer.SetNodeNameFormat(node_name_format);

  NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString>
      fileInfo;
  if (!writer.Perform(doc, fileInfo, Message_ProgressRange())) {
    return {};
  }

  handle.prepare_for_read();
  return handle.read_all();
}

// ============================================================================
// Public API
// ============================================================================

/// Transcode BREP bytes (STEP or IGES) to GLB bytes
/// This is the main API - one-shot conversion with no disk round-trips
std::vector<char> to_glb_bytes(const char *data, size_t data_len,
                               FileType file_type,
                               double tol_linear,
                               double tol_angle, bool tol_relative,
                               bool merge_primitives, bool use_parallel,
                               bool include_brep = false,
                               std::set<std::string> brep_types = {},
                               bool include_materials = false,
                               RWMesh_NameFormat node_name_format = RWMesh_NameFormat_InstanceOrProduct) {

  LoadResult loaded = loadBytes(data, data_len, file_type, tol_linear, tol_angle,
                                tol_relative, use_parallel);
  if (!loaded.success) {
    return {};
  }

  // Scale factor to meters, the unit of the glTF output
  double lengthUnit = detectLengthUnit(loaded.doc, loaded.shapes);

  rapidjson::Document materials;
  materials.SetArray();
  if (include_materials) {
    rapidjson::Value extracted =
        extractMaterials(loaded.doc, materials.GetAllocator());
    materials.Swap(extracted);
  }

  FacesByAccessor faces;
  std::vector<char> glbData =
      exportToGlbBytes(loaded.doc, merge_primitives, use_parallel,
                       include_brep ? &faces : nullptr, node_name_format);
  closeDocument(loaded.doc);

  if (glbData.empty()) {
    std::cerr << "Error: Failed to export GLB" << std::endl;
    return {};
  }

  if (include_brep || include_materials) {
    glbData = injectIntoGlb(glbData, faces, brep_types,
                            include_materials ? &materials : nullptr,
                            lengthUnit);
  }
  return glbData;
}

// ============================================================================
// Legacy File-based API (prefer to_glb_bytes for efficiency)
// ============================================================================
// NOTE: These file-based routes are LEGACY. All new code should use
// to_glb_bytes(bytes) -> bytes for optimal performance (no disk I/O).
// File-based routes exist only for backward compatibility.
// ============================================================================

/// Transcode BREP file (STEP or IGES) to GLB file
/// LEGACY: Use to_glb_bytes() instead for better performance
static int to_glb(const char *input_path, const char *output_path, FileType file_type,
                  double tol_linear, double tol_angle,
                  bool tol_relative, bool merge_primitives, bool use_parallel,
                  bool include_brep = false, std::set<std::string> brep_types = {},
                  bool include_materials = false, RWMesh_NameFormat node_name_format = RWMesh_NameFormat_InstanceOrProduct) {

  // Read input file
  std::ifstream inFile(input_path, std::ios::binary | std::ios::ate);
  if (!inFile) {
    std::cerr << "Error: Cannot open input file\n";
    return 1;
  }

  // Determine file size
  std::streamsize fileSize = inFile.tellg();
  inFile.seekg(0, std::ios::beg);

  std::vector<char> inputData(fileSize);
  if (!inFile.read(inputData.data(), fileSize)) {
    std::cerr << "Error: Failed to read file\n";
    return 1;
  }
  inFile.close();

  // Use bytes-based API (no disk round-trips)
  std::vector<char> glbData =
      to_glb_bytes(inputData.data(), inputData.size(), file_type, tol_linear,
                   tol_angle, tol_relative,
                   merge_primitives, use_parallel, include_brep, brep_types,
                   include_materials, node_name_format);

  if (glbData.empty()) {
    return 1;
  }

  // Write output file
  std::ofstream outFile(output_path, std::ios::binary);
  if (!outFile) {
    std::cerr << "Error: Cannot open output file" << std::endl;
    return 1;
  }
  outFile.write(glbData.data(), glbData.size());
  outFile.close();

  return 0;
}

/// Transcode STEP file to OBJ file
/// LEGACY: File-based conversion for backward compatibility only
static int step_to_obj(const std::string &input_path, const std::string &output_path,
                       double tol_linear, double tol_angle,
                       bool tol_relative, bool use_parallel, bool use_colors) {

    StepLoadResult loaded = loadStepFile(input_path.c_str(),
                                         tol_linear, tol_angle,
                                         tol_relative, use_parallel, use_colors);
  if (!loaded.success) {
    return 1;
  }

  RWObj_CafWriter cafWriter(output_path.c_str());

  Message_ProgressRange progress;
  NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString>
      fileInfo;
  if (!cafWriter.Perform(loaded.doc, fileInfo, progress)) {
    std::cerr << "Error: Failed to write OBJ to file" << std::endl;
    loaded.shapes.clear();
    closeDocument(loaded.doc);
    return 1;
  }

  loaded.shapes.clear();
  closeDocument(loaded.doc);
  return 0;
}
