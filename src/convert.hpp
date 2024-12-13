#include <Message_ProgressRange.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <iostream>
// STEP Read methods
#include <STEPCAFControl_Reader.hxx>
// Meshing
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopoDS_Shape.hxx>
#include <RWMesh_CoordinateSystem.hxx>
// GLTF Write methods
#include <RWGltf_CafWriter.hxx>
// OBJ Write methods
#include <RWObj_CafWriter.hxx>

static int step_to_glb(char *input_path, char *output_path,
                       Standard_Real tol_linear,
                       Standard_Real tol_angle,
                       Standard_Boolean tol_relative,
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

  if (IFSelect_RetDone != stepReader.ReadFile((Standard_CString)input_path)) {
    std::cerr << "Error: Failed to read STEP file \"" << input_path << "\" !"
              << std::endl;
    doc->Close();
    return 1;
  }
  stepReader.SetColorMode(true);
  stepReader.SetNameMode(true);
  stepReader.SetLayerMode(true);

  // Transferring to XCAF
  if (!stepReader.Transfer(doc)) {
    std::cerr << "Error: Failed to read STEP file \"" << input_path << "\" !"
              << std::endl;
    doc->Close();
    return 1;
  }

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
  RWGltf_CafWriter cafWriter(output_path, Standard_True);

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
    std::cerr << "Error: Failed to write glB to file !" << std::endl;
    doc->Close();
    return 1;
  }

  doc->Close();
  return 0;
}

/// Transcode STEP to OBJ ( and if applicable a MTL sidecar file )
static int step_to_obj(char *input_path, char *output_path,
                       Standard_Real tol_linear,
                       Standard_Real tol_angle,
                       Standard_Boolean tol_relative,
                       Standard_Boolean use_parallel,
                       Standard_Boolean use_colors,
                       Standard_Boolean swap_z_and_y_axis
                       ) {

  // Creating XCAF document
  Handle(TDocStd_Document) doc;
  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  app->NewDocument("BinXCAF", doc);

  // Loading STEP file
  STEPCAFControl_Reader stepReader;

  // TODO : do this in-memory instead of using a tempfile
  // std::istringstream ss;
  // ss.rdbuf()->pubsetbuf(buf,len);
  // stepReader.ReadStream()

  if (IFSelect_RetDone != stepReader.ReadFile((Standard_CString)input_path)) {
    std::cerr << "Error: Failed to read STEP file \"" << input_path << "\" !"
              << std::endl;
    doc->Close();
    return 1;
  }

  // Disabling color-mode will result in no sidecar MTL file being exported.
  stepReader.SetColorMode(use_colors);
  stepReader.SetNameMode(true);
  stepReader.SetLayerMode(true);

  // Transferring to XCAF
  if (!stepReader.Transfer(doc)) {
    std::cerr << "Error: Failed to read STEP file \"" << input_path << "\" !"
              << std::endl;
    doc->Close();
    return 1;
  }

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

  // Export as OBJ ( and if applicable MTL file for materials too )
  RWObj_CafWriter cafWriter(output_path);
  if( swap_z_and_y_axis ) {
    cafWriter.ChangeCoordinateSystemConverter().SetInputCoordinateSystem(
        RWMesh_CoordinateSystem::RWMesh_CoordinateSystem_Zup
    );
    cafWriter.ChangeCoordinateSystemConverter().SetOutputCoordinateSystem(
        RWMesh_CoordinateSystem::RWMesh_CoordinateSystem_Yup
    );
  }

  Message_ProgressRange progress;
  TColStd_IndexedDataMapOfStringString theFileInfo;
  if (!cafWriter.Perform(doc, theFileInfo, progress)) {
    std::cerr << "Error: Failed to write OBJ to file !" << std::endl;
    return 1;
  }

  doc->Close();
  return 0;
}
