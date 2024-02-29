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

/// @name Defines
/// @{
const Standard_Real DefaultLinDeflection = 0.1;
const Standard_Real DefaultAngDeflection = 0.5;
/// @}

/// @name BRepMesh_IncrementalMesh parameters
/// https://www.opencascade.com/doc/occt-7.1.0/overview/html/occt_user_guides__modeling_algos.html#occt_modalg_11_2
/// @{
static Standard_Real g_theLinDeflection = DefaultLinDeflection;
static Standard_Real g_theAngDeflection = DefaultAngDeflection;
/// @}

/// @name Other parameters
/// @{
static int g_verbose_level = 0;
/// @}

/// @name Command line arguments
/// @{
static const char *kHelp = "-h";
static const char *kHelpLong = "--help";
static const char *kLinearDeflection = "--linear";
static const char *kAngularDeflection = "--angular";
static const char *kVerbose = "-v";
/// @}

/// @name Error messages
/// @{
static const char *errorInvalidOutExtension =
    "output filename shall have .glTF or .glb extension.";
/// @}

/// Transcode STEP to glTF
static int step_to_glb(char *in, char *out) {
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

  if (g_verbose_level >= 1) {
    std::cout << "Loading \"" << in << "\" ..." << std::endl;
  }

  // Loading STEP file
  STEPCAFControl_Reader stepReader;
  if (IFSelect_RetDone != stepReader.ReadFile((Standard_CString)in)) {
    std::cerr << "Error: Failed to read STEP file \"" << in << "\" !"
              << std::endl;
    doc->Close();
    return 1;
  }
  stepReader.SetColorMode(true);
  stepReader.SetNameMode(true);
  stepReader.SetLayerMode(true);

  if (g_verbose_level >= 1) {
    std::cout << "Parsing STEP ..." << std::endl;
  }

  // Transferring to XCAF
  if (!stepReader.Transfer(doc)) {
    std::cerr << "Error: Failed to read STEP file \"" << in << "\" !"
              << std::endl;
    doc->Close();
    return 1;
  }

  std::cout << "Meshing shapes (linear " << g_theLinDeflection << ", angular "
            << g_theAngDeflection << ") ..." << std::endl;

  XSControl_Reader reader = stepReader.Reader();

  for (int shape_id = 1; shape_id <= reader.NbShapes(); shape_id++) {
    TopoDS_Shape shape = reader.Shape(shape_id);

    if (shape.IsNull()) {
      continue;
    }

    BRepMesh_IncrementalMesh Mesh(shape, g_theLinDeflection, Standard_False,
                                  g_theAngDeflection, Standard_True);
    Mesh.Perform();
  }

  TColStd_IndexedDataMapOfStringString theFileInfo;

  if (g_verbose_level >= 1) {
    std::cout << "Saving to " << (gltfIsBinary ? "binary " : "") << "glTF ..."
              << std::endl;
  }

  RWGltf_CafWriter cafWriter(out, gltfIsBinary);
  // SetTransformationFormat (RWGltf_WriterTrsfFormat theFormat)
  // https://dev.opencascade.org/doc/refman/html/_r_w_gltf___writer_trsf_format_8hxx.html#a24e114d176d2b2254deac8f1b3e95bf7

  Message_ProgressRange progress;

  if (!cafWriter.Perform(doc, theFileInfo, progress)) {
    std::cerr << "Error: Failed to write " << (gltfIsBinary ? "binary " : "")
              << "glTF to file !" << std::endl;
    return 1;
  }

  return 0;
}
