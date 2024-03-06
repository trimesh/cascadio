#include <Message_ProgressRange.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <iostream>
// STEP Read methods
#include <STEPCAFControl_Reader.hxx>
// Meshing
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopoDS_Shape.hxx>
// GLTF Write methods
#include <RWGltf_CafWriter.hxx>

static const char *errorInvalidOutExtension =
    "output filename shall have .glTF or .glb extension.";

/// Transcode STEP to glTF
static int step_to_glb(char *in, char *out, Standard_Real tol_linear,
                       Standard_Real tol_angle) {
  Standard_Boolean gltfIsBinary = Standard_False;

  // glTF format depends on output file extension
  const char *out_ext = strrchr(out, '.');
  if (!out_ext) {
    std::cerr << "Error: " << errorInvalidOutExtension << std::endl;
    return 1;
  } else if (strcasecmp(out_ext, ".gltf") == 0) {
    gltfIsBinary = Standard_False;
  } else if (strcasecmp(out_ext, ".glb") == 0) {
    gltfIsBinary = Standard_True;
  } else {
    std::cerr << "Error: " << errorInvalidOutExtension << std::endl;
    return 1;
  }

  // Creating XCAF document
  Handle(TDocStd_Document) doc;
  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  app->NewDocument("MDTV-XCAF", doc);

  // Loading STEP file
  STEPCAFControl_Reader stepReader;

  // TODO : do this in-memory instead of using a tempfile
  // std::istringstream ss;
  // ss.rdbuf()->pubsetbuf(buf,len);
  // stepReader.ReadStream()

  if (IFSelect_RetDone != stepReader.ReadFile((Standard_CString)in)) {
    std::cerr << "Error: Failed to read STEP file \"" << in << "\" !"
              << std::endl;
    doc->Close();
    return 1;
  }
  stepReader.SetColorMode(true);
  stepReader.SetNameMode(true);
  stepReader.SetLayerMode(true);
  }

  // Transferring to XCAF
  if (!stepReader.Transfer(doc)) {
    std::cerr << "Error: Failed to read STEP file \"" << in << "\" !"
              << std::endl;
    doc->Close();
    return 1;
  }

  std::cout << "Meshing shapes (linear " << tol_linear << ", angular "
            << tol_angle << ") ..." << std::endl;

  XSControl_Reader reader = stepReader.Reader();

  for (int shape_id = 1; shape_id <= reader.NbShapes(); shape_id++) {
    TopoDS_Shape shape = reader.Shape(shape_id);

    if (shape.IsNull()) {
      continue;
    }

    BRepMesh_IncrementalMesh Mesh(shape, tol_linear, Standard_False, tol_angle,
                                  Standard_True);
    Mesh.Perform();
  }

  TColStd_IndexedDataMapOfStringString theFileInfo;

  RWGltf_CafWriter cafWriter(out, gltfIsBinary);
  // TODO : set parameters here like "merge faces into one primitive"
  // and "use MAT4 transforms, always

  Message_ProgressRange progress;

  if (!cafWriter.Perform(doc, theFileInfo, progress)) {
    std::cerr << "Error: Failed to write " << (gltfIsBinary ? "binary " : "")
              << "glTF to file !" << std::endl;
    return 1;
  }

  return 0;
}
