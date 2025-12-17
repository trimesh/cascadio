#pragma once

#include "iges.hpp"
#include "materials.hpp"
#include "primitives.hpp"
#include "step.hpp"
#include "tempfile.hpp"

#include <Message_ProgressRange.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <fstream>
#include <sstream>

// Meshing
#include <BRepMesh_IncrementalMesh.hxx>
#include <RWMesh_CoordinateSystem.hxx>
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
                           Standard_Real tol_linear, Standard_Real tol_angle,
                           Standard_Boolean tol_relative,
                           Standard_Boolean use_parallel,
                           Standard_Boolean use_colors = Standard_True) {
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
static LoadResult loadBytes(const std::string &data, FileType file_type,
                            Standard_Real tol_linear, Standard_Real tol_angle,
                            Standard_Boolean tol_relative,
                            Standard_Boolean use_parallel,
                            Standard_Boolean use_colors = Standard_True) {
  LoadResult result;

  if (file_type == FileType::STEP) {
    StepLoadResult stepResult = loadStepBytes(
        data, tol_linear, tol_angle, tol_relative, use_parallel, use_colors);
    result.doc = stepResult.doc;
    result.shapes = stepResult.shapes;
    result.success = stepResult.success;
  } else if (file_type == FileType::IGES) {
    IgesLoadResult igesResult = loadIgesBytes(
        data, tol_linear, tol_angle, tol_relative, use_parallel, use_colors);
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
static Standard_Real detectLengthUnit(Handle(TDocStd_Document) doc,
                                      const std::vector<TopoDS_Shape> &shapes) {
  Standard_Real lengthUnit = 1.0;

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

  Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
  bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  Standard_Real maxExtent = std::max({xmax - xmin, ymax - ymin, zmax - zmin});

  // If max extent > 1.0, likely in mm (typical CAD parts are 10-1000mm)
  // glTF expects meters, so if extent suggests mm, use 0.001
  if (maxExtent > 1.0) {
    lengthUnit = 0.001; // Assume millimeters -> meters
  }

  return lengthUnit;
}

/// Export document to GLB file
static bool exportToGlbFile(Handle(TDocStd_Document) doc,
                            const char *output_path,
                            Standard_Boolean merge_primitives,
                            Standard_Boolean use_parallel) {
  RWGltf_CafWriter cafWriter(output_path, Standard_True);
  cafWriter.SetMergeFaces(merge_primitives);
  cafWriter.SetParallel(use_parallel);
  cafWriter.SetTransformationFormat(RWGltf_WriterTrsfFormat_Mat4);

  Message_ProgressRange progress;
  TColStd_IndexedDataMapOfStringString fileInfo;
  return cafWriter.Perform(doc, fileInfo, progress);
}

/// Export document to GLB in memory
static std::vector<char> exportToGlbBytes(Handle(TDocStd_Document) doc,
                                          Standard_Boolean merge_primitives,
                                          Standard_Boolean use_parallel) {
  // OCCT's RWGltf_CafWriter requires a file path, so we use a temp file
  // approach but encapsulate it here. In the future, could patch OCCT to
  // support streams.

  // Create a unique temp file
  TempFile tempFile(".glb");
  if (!tempFile.valid()) {
    std::cerr << "Error: Failed to create temp file for GLB export"
              << std::endl;
    return {};
  }
  // Close fd so exportToGlbFile can write to it
  tempFile.close_fd();

  // Export to temp file
  if (!exportToGlbFile(doc, tempFile.path(), merge_primitives, use_parallel)) {
    return {}; // TempFile destructor handles cleanup
  }

  // Read temp file into memory
  std::ifstream file(tempFile.path(), std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }

  std::streampos pos = file.tellg();
  if (pos == std::streampos(-1) || pos < 0) {
    return {};
  }
  std::streamsize size = pos;
  file.seekg(0, std::ios::beg);

  std::vector<char> result(static_cast<size_t>(size));
  if (!file.read(result.data(), size)) {
    return {};
  }

  return result;
  // TempFile destructor automatically cleans up
}

// ============================================================================
// Public API
// ============================================================================

/// Transcode BREP file (STEP or IGES) to GLB file
static int to_glb(char *input_path, char *output_path, FileType file_type,
                  Standard_Real tol_linear, Standard_Real tol_angle,
                  Standard_Boolean tol_relative,
                  Standard_Boolean merge_primitives,
                  Standard_Boolean use_parallel,
                  Standard_Boolean include_brep = Standard_False,
                  std::set<std::string> brep_types = {},
                  Standard_Boolean include_materials = Standard_False) {

  LoadResult loaded = loadFile(input_path, file_type, tol_linear, tol_angle,
                               tol_relative, use_parallel);
  if (!loaded.success) {
    return 1;
  }

  // Get length unit (scale factor to meters for glTF output)
  Standard_Real lengthUnit = detectLengthUnit(loaded.doc, loaded.shapes);

  // Extract materials before exporting (need access to document)
  rapidjson::Document matDoc;
  matDoc.SetArray();
  rapidjson::Value *materialsPtr = nullptr;
  if (include_materials) {
    rapidjson::Value materials =
        extractMaterials(loaded.doc, matDoc.GetAllocator());
    // Move materials into the document as root
    matDoc.Swap(materials);
    materialsPtr = &matDoc;
  }

  if (!exportToGlbFile(loaded.doc, output_path, merge_primitives,
                       use_parallel)) {
    std::cerr << "Error: Failed to write GLB to file" << std::endl;
    loaded.shapes.clear();
    closeDocument(loaded.doc);
    return 1;
  }

  closeDocument(loaded.doc);

  // Metadata injection only works reliably with merge_primitives=true
  // (single merged mesh). With multiple meshes, shape-to-mesh indexing
  // is not guaranteed to be correct.
  if (!merge_primitives && (include_brep || include_materials)) {
    std::cerr << "Warning: include_brep and include_materials require "
                 "merge_primitives=true. Skipping metadata injection."
              << std::endl;
    return 0;
  }

  if ((include_brep && !loaded.shapes.empty()) ||
      (include_materials && materialsPtr != nullptr)) {
    if (!injectExtrasIntoGlb(output_path, loaded.shapes, brep_types,
                             materialsPtr, lengthUnit)) {
      std::cerr << "Warning: Failed to inject extras into GLB" << std::endl;
    }
  }

  // Clear shapes to release geometry memory
  loaded.shapes.clear();

  return 0;
}

/// Transcode BREP bytes (STEP or IGES) to GLB bytes (no temp files)
static std::string
to_glb_bytes(const std::string &data, FileType file_type,
             Standard_Real tol_linear, Standard_Real tol_angle,
             Standard_Boolean tol_relative, Standard_Boolean merge_primitives,
             Standard_Boolean use_parallel,
             Standard_Boolean include_brep = Standard_False,
             std::set<std::string> brep_types = {},
             Standard_Boolean include_materials = Standard_False) {

  LoadResult loaded = loadBytes(data, file_type, tol_linear, tol_angle,
                                tol_relative, use_parallel);
  if (!loaded.success) {
    return "";
  }

  // Get length unit (scale factor to meters for glTF output)
  Standard_Real lengthUnit = detectLengthUnit(loaded.doc, loaded.shapes);

  // Extract materials before closing document
  rapidjson::Document matDoc;
  matDoc.SetArray();
  rapidjson::Value *materialsPtr = nullptr;
  if (include_materials) {
    rapidjson::Value materials =
        extractMaterials(loaded.doc, matDoc.GetAllocator());
    matDoc.Swap(materials);
    materialsPtr = &matDoc;
  }

  std::vector<char> glbData =
      exportToGlbBytes(loaded.doc, merge_primitives, use_parallel);
  closeDocument(loaded.doc);

  if (glbData.empty()) {
    std::cerr << "Error: Failed to export GLB" << std::endl;
    return "";
  }

  // Metadata injection only works reliably with merge_primitives=true
  if (!merge_primitives && (include_brep || include_materials)) {
    std::cerr << "Warning: include_brep and include_materials require "
                 "merge_primitives=true. Skipping metadata injection."
              << std::endl;
    return std::string(glbData.begin(), glbData.end());
  }

  if ((include_brep && !loaded.shapes.empty()) ||
      (include_materials && materialsPtr != nullptr)) {
    glbData = injectExtrasIntoGlbData(glbData, loaded.shapes, brep_types,
                                      materialsPtr, lengthUnit);
    if (glbData.empty()) {
      std::cerr << "Warning: Failed to inject extras into GLB" << std::endl;
      loaded.shapes.clear();
      return "";
    }
  }

  // Clear shapes to release geometry memory
  loaded.shapes.clear();

  return std::string(glbData.begin(), glbData.end());
}

/// Transcode STEP file to GLB file (backward compatibility wrapper)
static int step_to_glb(char *input_path, char *output_path,
                       Standard_Real tol_linear, Standard_Real tol_angle,
                       Standard_Boolean tol_relative,
                       Standard_Boolean merge_primitives,
                       Standard_Boolean use_parallel,
                       Standard_Boolean include_brep = Standard_False,
                       std::set<std::string> brep_types = {},
                       Standard_Boolean include_materials = Standard_False) {
  return to_glb(input_path, output_path, FileType::STEP, tol_linear, tol_angle,
                tol_relative, merge_primitives, use_parallel, include_brep,
                brep_types, include_materials);
}

/// Transcode STEP bytes to GLB bytes (backward compatibility wrapper)
static std::string
step_to_glb_bytes(const std::string &step_data, Standard_Real tol_linear,
                  Standard_Real tol_angle, Standard_Boolean tol_relative,
                  Standard_Boolean merge_primitives,
                  Standard_Boolean use_parallel,
                  Standard_Boolean include_brep = Standard_False,
                  std::set<std::string> brep_types = {},
                  Standard_Boolean include_materials = Standard_False) {
  return to_glb_bytes(step_data, FileType::STEP, tol_linear, tol_angle,
                      tol_relative, merge_primitives, use_parallel,
                      include_brep, brep_types, include_materials);
}

/// Transcode STEP file to OBJ file
static int step_to_obj(char *input_path, char *output_path,
                       Standard_Real tol_linear, Standard_Real tol_angle,
                       Standard_Boolean tol_relative,
                       Standard_Boolean use_parallel,
                       Standard_Boolean use_colors) {

  StepLoadResult loaded = loadStepFile(input_path, tol_linear, tol_angle,
                                       tol_relative, use_parallel, use_colors);
  if (!loaded.success) {
    return 1;
  }

  RWObj_CafWriter cafWriter(output_path);

  Message_ProgressRange progress;
  TColStd_IndexedDataMapOfStringString fileInfo;
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
