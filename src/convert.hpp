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

/// Export document to GLB file with callbacks for face data, JSON, and binary
/// appending
static bool exportToGlbFile(
    Handle(TDocStd_Document) doc, const char *output_path,
    Standard_Boolean merge_primitives, Standard_Boolean use_parallel,
    std::vector<FaceTriangleData> *faceData = nullptr,
    RWGltf_CafWriter::JsonPostProcessCallback jsonCallback = nullptr,
    RWGltf_CafWriter::BinaryAppendCallback binaryCallback = nullptr) {
  RWGltf_CafWriter cafWriter(output_path, Standard_True);
  cafWriter.SetMergeFaces(merge_primitives);
  cafWriter.SetParallel(use_parallel);
  cafWriter.SetTransformationFormat(RWGltf_WriterTrsfFormat_Mat4);

  // Set callback to collect face data if requested
  if (faceData != nullptr) {
    faceData->clear();
    cafWriter.SetFaceDataCallback(
        [faceData](Standard_Integer faceIndex, Standard_Integer triStart,
                   Standard_Integer triCount, const TopoDS_Face &face) {
          faceData->push_back({faceIndex, triStart, triCount, face});
        });
  }

  // Set JSON post-process callback if provided
  if (jsonCallback) {
    cafWriter.SetJsonPostProcessCallback(jsonCallback);
  }

  // Set binary append callback if provided
  if (binaryCallback) {
    cafWriter.SetBinaryAppendCallback(binaryCallback);
  }

  Message_ProgressRange progress;
  TColStd_IndexedDataMapOfStringString fileInfo;
  return cafWriter.Perform(doc, fileInfo, progress);
}

/// Export document to GLB in memory with callbacks
static std::vector<char> exportToGlbBytes(
    Handle(TDocStd_Document) doc, Standard_Boolean merge_primitives,
    Standard_Boolean use_parallel,
    std::vector<FaceTriangleData> *faceData = nullptr,
    RWGltf_CafWriter::JsonPostProcessCallback jsonCallback = nullptr,
    RWGltf_CafWriter::BinaryAppendCallback binaryCallback = nullptr) {
  // OCCT's RWGltf_CafWriter requires a file path, so we use a temp file
  // approach but encapsulate it here.

  // Create a unique temp file
  TempFile tempFile(".glb");
  if (!tempFile.valid()) {
    std::cerr << "Error: Failed to create temp file for GLB export"
              << std::endl;
    return {};
  }
  // Close fd so exportToGlbFile can write to it
  tempFile.close_fd();

  // Export to temp file with callbacks
  if (!exportToGlbFile(doc, tempFile.path(), merge_primitives, use_parallel,
                       faceData, jsonCallback, binaryCallback)) {
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

/// Transcode BREP bytes (STEP or IGES) to GLB bytes
/// This is the main API - one-shot conversion with no disk round-trips
static std::string
to_glb_bytes(const std::string &data, FileType file_type,
             Standard_Real tol_linear, Standard_Real tol_angle,
             Standard_Boolean tol_relative, Standard_Boolean merge_primitives,
             Standard_Boolean use_parallel,
             Standard_Boolean include_brep = Standard_False,
             std::set<std::string> brep_types = {},
             Standard_Boolean include_materials = Standard_False) {

  // Warn early if metadata requested without merge_primitives
  if (!merge_primitives && (include_brep || include_materials)) {
    std::cerr << "Warning: include_brep and include_materials require "
                 "merge_primitives=true. Skipping metadata injection."
              << std::endl;
  }

  LoadResult loaded = loadBytes(data, file_type, tol_linear, tol_angle,
                                tol_relative, use_parallel);
  if (!loaded.success) {
    return "";
  }

  // Get length unit (scale factor to meters for glTF output)
  Standard_Real lengthUnit = detectLengthUnit(loaded.doc, loaded.shapes);

  // Extract materials only if needed and conditions are met
  rapidjson::Document matDoc;
  matDoc.SetArray();
  rapidjson::Value *materialsPtr = nullptr;
  if (include_materials && merge_primitives) {
    rapidjson::Value materials =
        extractMaterials(loaded.doc, matDoc.GetAllocator());
    matDoc.Swap(materials);
    materialsPtr = &matDoc;
  }

  // Collect face data via callback if include_brep is requested
  std::vector<FaceTriangleData> faceData;
  std::vector<FaceTriangleData> *faceDataPtr =
      (include_brep && merge_primitives) ? &faceData : nullptr;

  // Create faceIndices binary data if needed (shared between callbacks)
  std::vector<uint32_t> faceIndices;
  auto buildFaceIndices = [&faceData, &faceIndices]() {
    // Early exit if already built or no data
    if (!faceIndices.empty() || faceData.empty()) {
      return;
    }

    // Find max triangle index to determine array size
    Standard_Integer totalTriangles = 0;
    for (const auto &fd : faceData) {
      totalTriangles = std::max(totalTriangles, fd.triStart + fd.triCount);
    }

    if (totalTriangles <= 0) {
      return; // No triangles to map
    }

    // Build mapping array: triangle index -> face index
    faceIndices.resize(totalTriangles, 0);
    for (const auto &fd : faceData) {
      for (Standard_Integer t = 0; t < fd.triCount; ++t) {
        Standard_Integer idx = fd.triStart + t;
        if (idx >= 0 && idx < totalTriangles) {
          faceIndices[idx] = static_cast<uint32_t>(fd.faceIndex);
        }
      }
    }
  };

  // Setup callbacks for direct injection (avoids GLB roundtrip)
  RWGltf_CafWriter::JsonPostProcessCallback jsonCallback = nullptr;
  RWGltf_CafWriter::BinaryAppendCallback binaryCallback = nullptr;

  if (merge_primitives && (include_brep || include_materials)) {
    if (include_brep && faceDataPtr) {
      // BREP extension: inject face data and optional materials
      jsonCallback = [&](const std::string &jsonStr) -> std::string {
        // Early exit if no face data collected
        if (faceData.empty()) {
          // Still inject materials if requested
          if (materialsPtr) {
            return injectBrepExtensionIntoJson(jsonStr, {}, 0, 0, brep_types,
                                               materialsPtr, lengthUnit);
          }
          return jsonStr;
        }

        buildFaceIndices(); // Build face->triangle mapping only if we have data

        // Parse JSON to get existing binary chunk size
        rapidjson::Document doc;
        doc.Parse(jsonStr.c_str(), jsonStr.size());
        if (doc.HasParseError()) {
          std::cerr << "Warning: Failed to parse JSON in callback" << std::endl;
          return jsonStr;
        }

        uint32_t existingBinLength = 0;
        if (doc.HasMember("buffers") && doc["buffers"].IsArray() &&
            doc["buffers"].Size() > 0 &&
            doc["buffers"][0].HasMember("byteLength")) {
          existingBinLength = doc["buffers"][0]["byteLength"].GetUint();
        }

        uint32_t faceIndicesBytes =
            static_cast<uint32_t>(faceIndices.size() * sizeof(uint32_t));
        return injectBrepExtensionIntoJson(jsonStr, faceData, existingBinLength,
                                           faceIndicesBytes, brep_types,
                                           materialsPtr, lengthUnit);
      };

      // Binary callback: append faceIndices array to GLB binary chunk
      binaryCallback = [&](std::ostream &stream,
                           uint32_t currentBinLength) -> uint32_t {
        uint32_t bytesToWrite =
            static_cast<uint32_t>(faceIndices.size() * sizeof(uint32_t));
        stream.write(reinterpret_cast<const char *>(faceIndices.data()),
                     bytesToWrite);

        // Verify write succeeded
        if (!stream.good()) {
          std::cerr << "Error: Failed to write faceIndices to binary chunk"
                    << std::endl;
          return 0;
        }

        return bytesToWrite;
      };
    } else if (include_materials && materialsPtr) {
      // Materials-only: inject into mesh.extras.cascadio without BREP extension
      jsonCallback = [&](const std::string &jsonStr) -> std::string {
        return injectBrepExtensionIntoJson(jsonStr, {}, 0, 0, {}, materialsPtr,
                                           lengthUnit);
      };
    }
  }

  std::vector<char> glbData =
      exportToGlbBytes(loaded.doc, merge_primitives, use_parallel, faceDataPtr,
                       jsonCallback, binaryCallback);
  closeDocument(loaded.doc);

  if (glbData.empty()) {
    std::cerr << "Error: Failed to export GLB" << std::endl;
    return "";
  }

  return std::string(glbData.begin(), glbData.end());
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
static int to_glb(char *input_path, char *output_path, FileType file_type,
                  Standard_Real tol_linear, Standard_Real tol_angle,
                  Standard_Boolean tol_relative,
                  Standard_Boolean merge_primitives,
                  Standard_Boolean use_parallel,
                  Standard_Boolean include_brep = Standard_False,
                  std::set<std::string> brep_types = {},
                  Standard_Boolean include_materials = Standard_False) {

  // Read input file
  std::ifstream inFile(input_path, std::ios::binary);
  if (!inFile) {
    std::cerr << "Error: Cannot open input file" << std::endl;
    return 1;
  }
  std::string inputData((std::istreambuf_iterator<char>(inFile)),
                        std::istreambuf_iterator<char>());
  inFile.close();

  // Use bytes-based API (no disk round-trips)
  std::string glbData =
      to_glb_bytes(inputData, file_type, tol_linear, tol_angle, tol_relative,
                   merge_primitives, use_parallel, include_brep, brep_types,
                   include_materials);

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

/// Transcode STEP file to GLB file (backward compatibility wrapper)
/// LEGACY: Use to_glb_bytes() instead for better performance
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
/// LEGACY: Use to_glb_bytes() directly instead
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
/// LEGACY: File-based conversion for backward compatibility only
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
