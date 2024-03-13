#include <Message_ProgressRange.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <iostream>

// Readers
#include <IGESCAFControl_Reader.hxx>
#include <STEPCAFControl_Reader.hxx>

// Meshing
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopoDS_Shape.hxx>
// GLTF Write methods
#include <RWGltf_CafWriter.hxx>

enum FileType {
  // File type must be specified explicitly
  UNSPECIFIED = 0,
  // File is in the STEP format
  STEP = 1,
  // File is in the IGES format
  IGES = 2,
  // File is in the BREP format.
  BREP = 3
};

// Convert boundary representation files to GLB
static int to_glb(char *in, char *out, FileType file_type,
                  Standard_Real tol_linear, Standard_Real tol_angle,
                  Standard_Boolean tol_relative,
                  Standard_Boolean merge_primitives,
                  Standard_Boolean use_parallel) {

  // Creating XCAF document
  Handle(TDocStd_Document) doc;
  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  app->NewDocument("cascadio", doc);
  XSControl_Reader reader;

  switch (file_type) {
  case STEP: {
    // TODO : do this in-memory instead of using a tempfile
    // however `WriteStream` isn't implemented on GLTF writer
    // so it would be a little pointless to take input as a stream
    // std::istringstream ss;
    // ss.rdbuf()->pubsetbuf(buf,len);
    // stepReader.ReadStream()
    // Loading STEP file from filename
    STEPCAFControl_Reader stepReader;
    if (IFSelect_RetDone != stepReader.ReadFile((Standard_CString)in)) {
      std::cerr << "Error: Failed to read STEP file" << std::endl;
      doc->Close();
      return 1;
    }

    // Transferring to XCAF
    if (!stepReader.Transfer(doc)) {
      std::cerr << "Error: Failed to read STEP file" << std::endl;
      doc->Close();
      return 1;
    }
    reader = stepReader.Reader();
    break;
  }

  case IGES: {
    IGESCAFControl_Reader igsReader;

    // parse the IGES file
    if (IFSelect_RetDone != igsReader.ReadFile((Standard_CString)in)) {
      std::cerr << "Error: Failed to read IGS file" << std::endl;
      doc->Close();
      return 1;
    }
    // Transfer to XCAF
    if (!igsReader.Transfer(doc)) {
      std::cerr << "Error: Failed to convert IGS file" << std::endl;
      doc->Close();
      return 1;
    }
    reader = igsReader;

    // TODO : sew faces here
    // BRepBuilderAPI_Sewing Sew;
    // Sew.Add(Face1);
    // Sew.Add(Face2);
    // Sew.Perform();
    // TopoDS_Shape result= Sew.SewedShape();
    break;
  }
  default: {
    std::cerr << "Error: unknown file type!" << std::endl;
    doc->Close();
    return 1;
  }
  }

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
  TColStd_IndexedDataMapOfStringString fileInfo;
  if (!cafWriter.Perform(doc, fileInfo, progress)) {
    std::cerr << "Error: Failed to write GLB file!" << std::endl;
    return 1;
  }

  return 0;
}
