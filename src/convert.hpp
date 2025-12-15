#include <Message_ProgressRange.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <unistd.h>

// STEP Read methods
#include <STEPCAFControl_Reader.hxx>
// Meshing
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <RWMesh_CoordinateSystem.hxx>
// GLTF Write methods
#include <RWGltf_CafWriter.hxx>
// OBJ Write methods
#include <RWObj_CafWriter.hxx>
// Surface analysis for BREP primitives
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Pln.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>
#include <gp_Ax3.hxx>
#include <BRepTools.hxx>
// JSON
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <set>
#include <string>

// GLB chunk types
constexpr uint32_t GLB_MAGIC = 0x46546C67; // "glTF"
constexpr uint32_t GLB_VERSION = 2;
constexpr uint32_t GLB_JSON_CHUNK = 0x4E4F534A; // "JSON"
constexpr uint32_t GLB_BIN_CHUNK = 0x004E4942; // "BIN\0"

// ============================================================================
// JSON Helper Functions
// ============================================================================

/// Helper to add a vec3 array to a JSON object
static void addVec3(rapidjson::Value& obj, const char* name, 
                    double x, double y, double z,
                    rapidjson::Document::AllocatorType& alloc) {
    rapidjson::Value arr(rapidjson::kArrayType);
    arr.PushBack(x, alloc).PushBack(y, alloc).PushBack(z, alloc);
    obj.AddMember(rapidjson::StringRef(name), arr, alloc);
}

/// Helper to add bounds array [min, max] to a JSON object
static void addBounds(rapidjson::Value& obj, const char* name,
                      double min, double max,
                      rapidjson::Document::AllocatorType& alloc) {
    rapidjson::Value arr(rapidjson::kArrayType);
    arr.PushBack(min, alloc).PushBack(max, alloc);
    obj.AddMember(rapidjson::StringRef(name), arr, alloc);
}

// ============================================================================
// BREP Primitive Extraction
// ============================================================================

/// Get the type name for a surface type, or nullptr if not analytical
static const char* getSurfaceTypeName(GeomAbs_SurfaceType surfType) {
    switch (surfType) {
        case GeomAbs_Plane: return "plane";
        case GeomAbs_Cylinder: return "cylinder";
        case GeomAbs_Cone: return "cone";
        case GeomAbs_Sphere: return "sphere";
        case GeomAbs_Torus: return "torus";
        default: return nullptr;
    }
}

/// Extract BREP primitive info for a face and add to JSON array
/// If allowedTypes is non-empty, only include faces with types in the set
static void extractFacePrimitive(const TopoDS_Face& face, int faceIndex,
                                  rapidjson::Value& facesArray,
                                  rapidjson::Document::AllocatorType& alloc,
                                  const std::set<std::string>& allowedTypes = {}) {
    BRepAdaptor_Surface surf(face, Standard_True);
    GeomAbs_SurfaceType surfType = surf.GetType();
    
    // Determine type name for filtering
    const char* typeName = getSurfaceTypeName(surfType);
    
    // If filtering is enabled, check if this type should be included
    if (!allowedTypes.empty()) {
        if (typeName == nullptr || allowedTypes.find(typeName) == allowedTypes.end()) {
            return; // Skip this face
        }
    }
    
    // Get UV bounds
    Standard_Real uMin, uMax, vMin, vMax;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
    
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("face_index", faceIndex, alloc);
    addBounds(obj, "u_bounds", uMin, uMax, alloc);
    addBounds(obj, "v_bounds", vMin, vMax, alloc);
    
    switch (surfType) {
        case GeomAbs_Plane: {
            gp_Pln pln = surf.Plane();
            gp_Ax3 pos = pln.Position();
            obj.AddMember("type", "plane", alloc);
            addVec3(obj, "origin", pos.Location().X(), pos.Location().Y(), pos.Location().Z(), alloc);
            addVec3(obj, "normal", pos.Direction().X(), pos.Direction().Y(), pos.Direction().Z(), alloc);
            addVec3(obj, "x_dir", pos.XDirection().X(), pos.XDirection().Y(), pos.XDirection().Z(), alloc);
            break;
        }
        case GeomAbs_Cylinder: {
            gp_Cylinder cyl = surf.Cylinder();
            gp_Ax3 pos = cyl.Position();
            obj.AddMember("type", "cylinder", alloc);
            addVec3(obj, "origin", pos.Location().X(), pos.Location().Y(), pos.Location().Z(), alloc);
            addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(), pos.Direction().Z(), alloc);
            obj.AddMember("radius", cyl.Radius(), alloc);
            break;
        }
        case GeomAbs_Cone: {
            gp_Cone cone = surf.Cone();
            gp_Ax3 pos = cone.Position();
            gp_Pnt apex = cone.Apex();
            obj.AddMember("type", "cone", alloc);
            addVec3(obj, "apex", apex.X(), apex.Y(), apex.Z(), alloc);
            addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(), pos.Direction().Z(), alloc);
            obj.AddMember("semi_angle", cone.SemiAngle(), alloc);
            obj.AddMember("ref_radius", cone.RefRadius(), alloc);
            break;
        }
        case GeomAbs_Sphere: {
            gp_Sphere sph = surf.Sphere();
            obj.AddMember("type", "sphere", alloc);
            addVec3(obj, "center", sph.Location().X(), sph.Location().Y(), sph.Location().Z(), alloc);
            obj.AddMember("radius", sph.Radius(), alloc);
            break;
        }
        case GeomAbs_Torus: {
            gp_Torus tor = surf.Torus();
            gp_Ax3 pos = tor.Position();
            obj.AddMember("type", "torus", alloc);
            addVec3(obj, "center", pos.Location().X(), pos.Location().Y(), pos.Location().Z(), alloc);
            addVec3(obj, "axis", pos.Direction().X(), pos.Direction().Y(), pos.Direction().Z(), alloc);
            obj.AddMember("major_radius", tor.MajorRadius(), alloc);
            obj.AddMember("minor_radius", tor.MinorRadius(), alloc);
            break;
        }
        default:
            // Non-analytical surface - still record face_index but no primitive type
            obj.AddMember("type", rapidjson::Value(), alloc); // null
            break;
    }
    
    facesArray.PushBack(obj, alloc);
}

/// Extract all BREP primitives from a shape into a JSON array
static rapidjson::Value extractAllPrimitives(const TopoDS_Shape& shape,
                                              rapidjson::Document::AllocatorType& alloc,
                                              const std::set<std::string>& allowedTypes = {}) {
    rapidjson::Value facesArray(rapidjson::kArrayType);
    int faceIndex = 0;
    
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        TopoDS_Face face = TopoDS::Face(explorer.Current());
        extractFacePrimitive(face, faceIndex, facesArray, alloc, allowedTypes);
        faceIndex++;
    }
    
    return facesArray;
}

// ============================================================================
// GLB Processing (in-memory)
// ============================================================================

/// Inject BREP primitives into GLB data in memory
/// Returns modified GLB data, or empty vector on error
static std::vector<char> injectPrimitivesIntoGlbData(
    const std::vector<char>& glbData,
    const std::vector<TopoDS_Shape>& shapes,
    const std::set<std::string>& allowedTypes = {}) {
    
    if (glbData.size() < 12) {
        std::cerr << "Error: GLB data too small for header" << std::endl;
        return {};
    }
    
    const char* ptr = glbData.data();
    const char* end = glbData.data() + glbData.size();
    
    // Read GLB header (12 bytes)
    uint32_t magic, version, totalLength;
    std::memcpy(&magic, ptr, 4); ptr += 4;
    std::memcpy(&version, ptr, 4); ptr += 4;
    std::memcpy(&totalLength, ptr, 4); ptr += 4;
    
    if (magic != GLB_MAGIC) {
        std::cerr << "Error: Invalid GLB magic number" << std::endl;
        return {};
    }
    
    // Read JSON chunk header
    if (ptr + 8 > end) {
        std::cerr << "Error: GLB data too small for JSON chunk header" << std::endl;
        return {};
    }
    uint32_t jsonChunkLength, jsonChunkType;
    std::memcpy(&jsonChunkLength, ptr, 4); ptr += 4;
    std::memcpy(&jsonChunkType, ptr, 4); ptr += 4;
    
    if (jsonChunkType != GLB_JSON_CHUNK) {
        std::cerr << "Error: First chunk is not JSON" << std::endl;
        return {};
    }
    
    // Read JSON data
    if (ptr + jsonChunkLength > end) {
        std::cerr << "Error: GLB data too small for JSON data" << std::endl;
        return {};
    }
    const char* jsonStart = ptr;
    ptr += jsonChunkLength;
    
    // Read BIN chunk header (may not exist for empty meshes)
    uint32_t binChunkLength = 0;
    const char* binData = nullptr;
    if (ptr + 8 <= end) {
        uint32_t binChunkType;
        std::memcpy(&binChunkLength, ptr, 4); ptr += 4;
        std::memcpy(&binChunkType, ptr, 4); ptr += 4;
        
        if (binChunkType == GLB_BIN_CHUNK) {
            binData = ptr;
        } else {
            // Not a BIN chunk, reset
            binChunkLength = 0;
        }
    }
    
    // Parse JSON
    rapidjson::Document doc;
    doc.Parse(jsonStart, jsonChunkLength);
    
    if (doc.HasParseError()) {
        std::cerr << "Error: Failed to parse GLB JSON" << std::endl;
        return {};
    }
    
    // Add primitives to each mesh's extras
    if (doc.HasMember("meshes") && doc["meshes"].IsArray()) {
        auto& meshes = doc["meshes"];
        size_t numMeshes = meshes.Size();
        size_t numShapes = shapes.size();
        
        for (size_t i = 0; i < numMeshes && i < numShapes; i++) {
            auto& mesh = meshes[i];
            
            if (!mesh.HasMember("extras")) {
                mesh.AddMember("extras", rapidjson::Value(rapidjson::kObjectType), doc.GetAllocator());
            }
            
            rapidjson::Value facesArray = extractAllPrimitives(shapes[i], doc.GetAllocator(), allowedTypes);
            mesh["extras"].AddMember("brep_faces", facesArray, doc.GetAllocator());
        }
    }
    
    // Serialize modified JSON
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    // Pad JSON to 4-byte boundary with spaces
    std::string newJson = buffer.GetString();
    while (newJson.size() % 4 != 0) {
        newJson += ' ';
    }
    uint32_t newJsonLength = static_cast<uint32_t>(newJson.size());
    
    // Calculate new total length
    uint32_t newTotalLength = 12 + 8 + newJsonLength;
    if (binChunkLength > 0) {
        newTotalLength += 8 + binChunkLength;
    }
    
    // Build output GLB
    std::vector<char> result;
    result.reserve(newTotalLength);
    
    // Helper to append data
    auto append = [&result](const void* data, size_t size) {
        const char* p = static_cast<const char*>(data);
        result.insert(result.end(), p, p + size);
    };
    
    // Write GLB header
    append(&magic, 4);
    append(&version, 4);
    append(&newTotalLength, 4);
    
    // Write JSON chunk
    uint32_t jsonType = GLB_JSON_CHUNK;
    append(&newJsonLength, 4);
    append(&jsonType, 4);
    result.insert(result.end(), newJson.begin(), newJson.end());
    
    // Write BIN chunk if present
    if (binChunkLength > 0 && binData != nullptr) {
        uint32_t binType = GLB_BIN_CHUNK;
        append(&binChunkLength, 4);
        append(&binType, 4);
        result.insert(result.end(), binData, binData + binChunkLength);
    }
    
    return result;
}

/// Inject BREP primitives into GLB file on disk
static bool injectPrimitivesIntoGlb(const char* glbPath, 
                                     const std::vector<TopoDS_Shape>& shapes,
                                     const std::set<std::string>& allowedTypes = {}) {
    // Read entire file
    std::ifstream inFile(glbPath, std::ios::binary | std::ios::ate);
    if (!inFile) {
        std::cerr << "Error: Cannot open GLB file for reading" << std::endl;
        return false;
    }
    
    std::streampos pos = inFile.tellg();
    if (pos == std::streampos(-1) || pos < 0) {
        std::cerr << "Error: Failed to get GLB file size" << std::endl;
        return false;
    }
    std::streamsize size = pos;
    inFile.seekg(0, std::ios::beg);
    
    std::vector<char> glbData(static_cast<size_t>(size));
    if (!inFile.read(glbData.data(), size)) {
        std::cerr << "Error: Failed to read GLB file" << std::endl;
        return false;
    }
    inFile.close();
    
    // Process in memory
    std::vector<char> result = injectPrimitivesIntoGlbData(glbData, shapes, allowedTypes);
    if (result.empty()) {
        return false;
    }
    
    // Write back to file
    std::ofstream outFile(glbPath, std::ios::binary | std::ios::trunc);
    if (!outFile) {
        std::cerr << "Error: Cannot open GLB file for writing" << std::endl;
        return false;
    }
    outFile.write(result.data(), result.size());
    outFile.close();
    
    return true;
}

// ============================================================================
// STEP Loading (shared logic)
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
                       std::set<std::string> brep_types = {}) {
    
    StepLoadResult loaded = loadStepFile(input_path, tol_linear, tol_angle, 
                                          tol_relative, use_parallel);
    if (!loaded.success) {
        return 1;
    }
    
    if (!exportToGlbFile(loaded.doc, output_path, merge_primitives, use_parallel)) {
        std::cerr << "Error: Failed to write GLB to file" << std::endl;
        loaded.doc->Close();
        return 1;
    }
    
    loaded.doc->Close();
    
    if (include_brep && !loaded.shapes.empty()) {
        if (!injectPrimitivesIntoGlb(output_path, loaded.shapes, brep_types)) {
            std::cerr << "Warning: Failed to inject BREP primitives into GLB" << std::endl;
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
                                      std::set<std::string> brep_types = {}) {
    
    StepLoadResult loaded = loadStepBytes(step_data, tol_linear, tol_angle,
                                           tol_relative, use_parallel);
    if (!loaded.success) {
        return "";
    }
    
    std::vector<char> glbData = exportToGlbBytes(loaded.doc, merge_primitives, use_parallel);
    loaded.doc->Close();
    
    if (glbData.empty()) {
        std::cerr << "Error: Failed to export GLB" << std::endl;
        return "";
    }
    
    if (include_brep && !loaded.shapes.empty()) {
        glbData = injectPrimitivesIntoGlbData(glbData, loaded.shapes, brep_types);
        if (glbData.empty()) {
            std::cerr << "Warning: Failed to inject BREP primitives into GLB" << std::endl;
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
