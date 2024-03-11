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
                       Standard_Real tol_angle, Standard_Boolean tol_relative,
                       Standard_Boolean merge_primitives,
                       Standard_Boolean use_parallel) {

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
    // Mesh the shape.
    BRepMesh_IncrementalMesh Mesh(shape, tol_linear, tol_relative, tol_angle,
                                  use_parallel);
    Mesh.Perform();
  }

  // Always export as binary GLB
  // Otherwise this is a multi-file affair.
  RWGltf_CafWriter cafWriter(out, Standard_True);

  // Set flag to merge faces within a single part.
  // May reduce JSON size thanks to smaller number of primitive arrays.
  cafWriter.SetMergeFaces(merge_primitives);

  // Set multithreaded execution.
  cafWriter.SetParallel(use_parallel);

  // Always export as matrices rather than a decomposed
  // rotation-translation-scale
  cafWriter.SetTransformationFormat(RWGltf_WriterTrsfFormat_Mat4);

  Message_ProgressRange progress;
  TColStd_IndexedDataMapOfStringString theFileInfo;
  if (!cafWriter.Perform(doc, theFileInfo, progress)) {
    std::cerr << "Error: Failed to write glTF to file !" << std::endl;
    return 1;
  }

  return 0;
}
