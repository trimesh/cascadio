#pragma once

#include "primitives.hpp"
#include "materials.hpp"

#include <Message_ProgressRange.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <sstream>
#include <unistd.h>

// STEP Read methods
#include <STEPCAFControl_Reader.hxx>
// Meshing
#include <BRepMesh_IncrementalMesh.hxx>
#include <RWMesh_CoordinateSystem.hxx>
// GLTF Write methods
#include <RWGltf_CafWriter.hxx>
// OBJ Write methods
#include <RWObj_CafWriter.hxx>

// ============================================================================
// STEP Loading
// ============================================================================

/// Result of loading a STEP file
struct StepLoadResult {
    Handle(TDocStd_Document) doc;
    std::vector<TopoDS_Shape> shapes;
    bool success;
    
    StepLoadResult() : success(false) {}
};

/// Load a STEP file from disk and mesh the shapes
static StepLoadResult loadStepFile(const char* input_path,
                                    Standard_Real tol_linear,
                                    Standard_Real tol_angle,
                                    Standard_Boolean tol_relative,
                                    Standard_Boolean use_parallel,
                                    Standard_Boolean use_colors = Standard_True) {
    StepLoadResult result;
    
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    app->NewDocument("BinXCAF", result.doc);
    
    STEPCAFControl_Reader stepReader;
    
    if (IFSelect_RetDone != stepReader.ReadFile((Standard_CString)input_path)) {
        std::cerr << "Error: Failed to read STEP file \"" << input_path << "\"" << std::endl;
        result.doc->Close();
        return result;
    }
    
    stepReader.SetColorMode(use_colors);
    stepReader.SetNameMode(true);
    stepReader.SetLayerMode(true);
    
    if (!stepReader.Transfer(result.doc)) {
        std::cerr << "Error: Failed to transfer STEP file \"" << input_path << "\"" << std::endl;
        result.doc->Close();
        return result;
    }
    
    XSControl_Reader reader = stepReader.Reader();
    for (int shape_id = 1; shape_id <= reader.NbShapes(); shape_id++) {
        TopoDS_Shape shape = reader.Shape(shape_id);
        if (shape.IsNull()) {
            continue;
        }
        result.shapes.push_back(shape);
        BRepMesh_IncrementalMesh mesh(shape, tol_linear, tol_relative, tol_angle, use_parallel);
        mesh.Perform();
    }
    
    result.success = true;
    return result;
}

/// Load a STEP file from memory (bytes) and mesh the shapes
static StepLoadResult loadStepBytes(const std::string& stepData,
                                     Standard_Real tol_linear,
                                     Standard_Real tol_angle,
                                     Standard_Boolean tol_relative,
                                     Standard_Boolean use_parallel,
                                     Standard_Boolean use_colors = Standard_True) {
    StepLoadResult result;
    
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    app->NewDocument("BinXCAF", result.doc);
    
    STEPCAFControl_Reader stepReader;
    
    // Create an input stream from the string data
    std::istringstream stepStream(stepData);
    
    if (IFSelect_RetDone != stepReader.ReadStream("step_data.step", stepStream)) {
        std::cerr << "Error: Failed to read STEP data from memory" << std::endl;
        result.doc->Close();
        return result;
    }
    
    stepReader.SetColorMode(use_colors);
    stepReader.SetNameMode(true);
    stepReader.SetLayerMode(true);
    
    if (!stepReader.Transfer(result.doc)) {
        std::cerr << "Error: Failed to transfer STEP data" << std::endl;
        result.doc->Close();
        return result;
    }
    
    XSControl_Reader reader = stepReader.Reader();
    for (int shape_id = 1; shape_id <= reader.NbShapes(); shape_id++) {
        TopoDS_Shape shape = reader.Shape(shape_id);
        if (shape.IsNull()) {
            continue;
        }
        result.shapes.push_back(shape);
        BRepMesh_IncrementalMesh mesh(shape, tol_linear, tol_relative, tol_angle, use_parallel);
        mesh.Perform();
    }
    
    result.success = true;
    return result;
}

// ============================================================================
// GLB Export
// ============================================================================

/// Export document to GLB file
static bool exportToGlbFile(Handle(TDocStd_Document) doc,
                            const char* output_path,
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
    // OCCT's RWGltf_CafWriter requires a file path, so we use a temp file approach
    // but encapsulate it here. In the future, could patch OCCT to support streams.
    
    // Create a unique temp filename
    char tempPath[] = "/tmp/cascadio_XXXXXX.glb";
    int fd = mkstemps(tempPath, 4);  // 4 = length of ".glb"
    if (fd == -1) {
        std::cerr << "Error: Failed to create temp file for GLB export" << std::endl;
        return {};
    }
    close(fd);
    
    // Export to temp file
    if (!exportToGlbFile(doc, tempPath, merge_primitives, use_parallel)) {
        std::remove(tempPath);
        return {};
    }
    
    // Read temp file into memory
    std::ifstream file(tempPath, std::ios::binary | std::ios::ate);
    if (!file) {
        std::remove(tempPath);
        return {};
    }
    
    std::streampos pos = file.tellg();
    if (pos == std::streampos(-1) || pos < 0) {
        file.close();
        std::remove(tempPath);
        return {};
    }
    std::streamsize size = pos;
    file.seekg(0, std::ios::beg);
    
    std::vector<char> result(static_cast<size_t>(size));
    if (!file.read(result.data(), size)) {
        file.close();
        std::remove(tempPath);
        return {};
    }
    file.close();
    std::remove(tempPath);
    
    return result;
}

// ============================================================================
// Public API
// ============================================================================

/// Transcode STEP file to GLB file
static int step_to_glb(char *input_path, char *output_path,
                       Standard_Real tol_linear,
                       Standard_Real tol_angle,
                       Standard_Boolean tol_relative,
                       Standard_Boolean merge_primitives,
                       Standard_Boolean use_parallel,
                       Standard_Boolean include_brep = Standard_False,
                       std::set<std::string> brep_types = {},
                       Standard_Boolean include_materials = Standard_False) {
    
    StepLoadResult loaded = loadStepFile(input_path, tol_linear, tol_angle, 
                                          tol_relative, use_parallel);
    if (!loaded.success) {
        return 1;
    }
    
    // Extract materials before exporting (need access to document)
    rapidjson::Document matDoc;
    matDoc.SetArray();
    rapidjson::Value* materialsPtr = nullptr;
    if (include_materials) {
        rapidjson::Value materials = extractMaterials(loaded.doc, matDoc.GetAllocator());
        // Move materials into the document as root
        matDoc.Swap(materials);
        materialsPtr = &matDoc;
    }
    
    if (!exportToGlbFile(loaded.doc, output_path, merge_primitives, use_parallel)) {
        std::cerr << "Error: Failed to write GLB to file" << std::endl;
        loaded.doc->Close();
        return 1;
    }
    
    loaded.doc->Close();
    
    if ((include_brep && !loaded.shapes.empty()) || (include_materials && materialsPtr != nullptr)) {
        if (!injectExtrasIntoGlb(output_path, loaded.shapes, brep_types, materialsPtr)) {
            std::cerr << "Warning: Failed to inject extras into GLB" << std::endl;
        }
    }
    
    return 0;
}

/// Transcode STEP bytes to GLB bytes (no temp files exposed to Python)
static std::string step_to_glb_bytes(const std::string& step_data,
                                      Standard_Real tol_linear,
                                      Standard_Real tol_angle,
                                      Standard_Boolean tol_relative,
                                      Standard_Boolean merge_primitives,
                                      Standard_Boolean use_parallel,
                                      Standard_Boolean include_brep = Standard_False,
                                      std::set<std::string> brep_types = {},
                                      Standard_Boolean include_materials = Standard_False) {
    
    StepLoadResult loaded = loadStepBytes(step_data, tol_linear, tol_angle,
                                           tol_relative, use_parallel);
    if (!loaded.success) {
        return "";
    }
    
    // Extract materials before closing document
    rapidjson::Document matDoc;
    matDoc.SetArray();
    rapidjson::Value* materialsPtr = nullptr;
    if (include_materials) {
        rapidjson::Value materials = extractMaterials(loaded.doc, matDoc.GetAllocator());
        matDoc.Swap(materials);
        materialsPtr = &matDoc;
    }
    
    std::vector<char> glbData = exportToGlbBytes(loaded.doc, merge_primitives, use_parallel);
    loaded.doc->Close();
    
    if (glbData.empty()) {
        std::cerr << "Error: Failed to export GLB" << std::endl;
        return "";
    }
    
    if ((include_brep && !loaded.shapes.empty()) || (include_materials && materialsPtr != nullptr)) {
        glbData = injectExtrasIntoGlbData(glbData, loaded.shapes, brep_types, materialsPtr);
        if (glbData.empty()) {
            std::cerr << "Warning: Failed to inject extras into GLB" << std::endl;
            return "";
        }
    }
    
    return std::string(glbData.begin(), glbData.end());
}

/// Transcode STEP file to OBJ file
static int step_to_obj(char *input_path, char *output_path,
                       Standard_Real tol_linear,
                       Standard_Real tol_angle,
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
        loaded.doc->Close();
        return 1;
    }
    
    loaded.doc->Close();
    return 0;
}
