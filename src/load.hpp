#pragma once

#include "filehandle.hpp"

#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <IGESControl_Reader.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <Standard_Version.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <cstddef>
#include <istream>
#include <stdexcept>
#include <streambuf>
#include <vector>

enum class FileType { UNSPECIFIED = 0, STEP = 1, IGES = 2 };

/// Tessellation settings shared by every loader.
struct MeshOptions {
  double tol_linear;
  double tol_angle;
  bool tol_relative;
  bool use_parallel;
};

/// A read-only std::istream over a block of memory, so readers can parse
/// input bytes in place without copying them or touching the filesystem.
class imemstream : public std::istream {
public:
  imemstream(const char *data, std::size_t size)
      : std::istream(&buffer_), buffer_(data, size) {}

private:
  struct membuf : std::streambuf {
    membuf(const char *data, std::size_t size) {
      char *p = const_cast<char *>(data); // never written through
      setg(p, p, p + size);
    }
  } buffer_;
};

/// A meshed CAD document. Closes the document when it goes out of scope,
/// including when an exception is thrown part way through a conversion.
struct Loaded {
  Handle(TDocStd_Document) doc;
  std::vector<TopoDS_Shape> shapes;

  Loaded() {
    XCAFApp_Application::GetApplication()->NewDocument("BinXCAF", doc);
  }
  ~Loaded() {
    if (!doc.IsNull()) {
      // closing through the application also removes the document
      // from the application's list of open documents
      XCAFApp_Application::GetApplication()->Close(doc);
    }
  }
  Loaded(const Loaded &) = delete;
  Loaded &operator=(const Loaded &) = delete;

  /// Keep a shape and triangulate it in place.
  void addMeshed(const TopoDS_Shape &shape, const MeshOptions &mesh) {
    if (shape.IsNull()) {
      return;
    }
    shapes.push_back(shape);
    // the constructor runs the meshing
    BRepMesh_IncrementalMesh(shape, mesh.tol_linear, mesh.tol_relative,
                             mesh.tol_angle, mesh.use_parallel);
  }
};

/// Read STEP bytes into `loaded`, keeping names, layers and optionally colors.
static void loadStep(Loaded &loaded, const char *data, size_t size,
                     const MeshOptions &mesh, bool use_colors) {
  STEPCAFControl_Reader reader;
  imemstream stream(data, size);
  if (reader.ReadStream("data.step", stream) != IFSelect_RetDone) {
    throw std::invalid_argument("could not parse the data as a STEP file");
  }
  reader.SetColorMode(use_colors);
  reader.SetNameMode(true);
  reader.SetLayerMode(true);
  if (!reader.Transfer(loaded.doc)) {
    throw std::runtime_error("could not convert the STEP file to shapes");
  }
  const XSControl_Reader &shapes = reader.Reader();
  for (int i = 1; i <= shapes.NbShapes(); i++) {
    loaded.addMeshed(shapes.Shape(i), mesh);
  }
}

/// Read IGES bytes into `loaded`.
/// IGES files are usually loose surfaces, so they are sewn into one shape.
static void loadIges(Loaded &loaded, const char *data, size_t size,
                     const MeshOptions &mesh) {
  IGESControl_Reader reader;
#if OCC_VERSION_HEX >= 0x080100
  imemstream stream(data, size);
  const IFSelect_ReturnStatus status = reader.ReadStream("data.igs", stream);
#else
  // older OpenCASCADE can only read IGES from a path
  FileHandle handle(".igs");
  if (!handle.valid() || !handle.write_data(data, size)) {
    throw std::runtime_error("could not create a temporary file for IGES");
  }
  handle.prepare_for_read();
  const IFSelect_ReturnStatus status = reader.ReadFile(handle.path());
#endif
  if (status != IFSelect_RetDone) {
    throw std::invalid_argument("could not parse the data as an IGES file");
  }

  reader.TransferRoots();
  // a small tolerance for joining neighbouring surfaces,
  // unrelated to the meshing tolerance
  const double tolerance = 1e-6;
  BRepBuilderAPI_Sewing sewing(tolerance);
  sewing.SetMaxTolerance(tolerance * 10);
  sewing.SetMinTolerance(tolerance * 0.1);
  int count = 0;
  TopoDS_Shape shape;
  for (int i = 1; i <= reader.NbShapes(); i++) {
    shape = reader.Shape(i);
    if (!shape.IsNull()) {
      sewing.Add(shape);
      count++;
    }
  }
  if (count == 0) {
    throw std::invalid_argument("the IGES file contains no shapes");
  }
  if (count > 1) {
    sewing.Perform();
    shape = sewing.SewedShape();
  }
  if (shape.IsNull()) {
    throw std::runtime_error("could not join the IGES surfaces");
  }

  loaded.addMeshed(shape, mesh);
  XCAFDoc_DocumentTool::ShapeTool(loaded.doc->Main())->AddShape(shape, false);
}

/// Read and mesh a STEP or IGES file held in memory.
static void load(Loaded &loaded, const char *data, size_t size,
                 FileType file_type, const MeshOptions &mesh,
                 bool use_colors = true) {
  switch (file_type) {
  case FileType::STEP:
    return loadStep(loaded, data, size, mesh, use_colors);
  case FileType::IGES:
    return loadIges(loaded, data, size, mesh);
  default:
    throw std::invalid_argument("unsupported file type");
  }
}
