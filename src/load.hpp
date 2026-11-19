#pragma once

#include "filehandle.hpp"
#include "request.hpp"
#include "trace.hpp"
#include "zstd.hpp"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Bnd_Box.hxx>
#include <IGESControl_Reader.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <Standard_Version.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <istream>
#include <stdexcept>
#include <string>
#include <streambuf>
#include <vector>

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

/// Refuse a shape that cannot be meshed in any reasonable time or memory,
/// before trying. A corrupt file can hold coordinates like 1e300; meshing
/// that to an absolute tolerance would run until it is killed.
static void checkMeshable(const TopoDS_Shape &shape, const MeshOptions &mesh) {
  Bnd_Box box;
  BRepBndLib::Add(shape, box, false);
  if (box.IsVoid()) {
    return; // nothing to mesh
  }
  const double size = box.IsOpen() ? HUGE_VAL : std::sqrt(box.SquareExtent());
  // 1e12 is a million kilometers if the file is in millimeters
  if (!std::isfinite(size) || size > 1e12) {
    throw std::invalid_argument(
        "the model has no sensible size: its coordinates are infinite or "
        "absurdly large");
  }
  // Curved surfaces need about sqrt(size / tolerance) segments per arc.
  // Past this ratio that is tens of thousands, squared, per surface.
  if (!mesh.tol_relative && mesh.tol_linear > 0 &&
      size / mesh.tol_linear > 1e9) {
    throw std::invalid_argument(
        "tol_linear is too fine for a model this large (the model is more "
        "than a billion times the tolerance across); use a larger "
        "tol_linear, or tol_relative=True");
  }
}

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
    checkMeshable(shape, mesh);
    shapes.push_back(shape);
    Span span("mesh");
    // the constructor runs the meshing
    BRepMesh_IncrementalMesh(shape, mesh.tol_linear, mesh.tol_relative,
                             mesh.tol_angle, mesh.use_parallel);
  }
};

/// A STEP file is text that starts with "ISO-10303-21". Anything else is
/// refused here, so that OpenCASCADE's parser never sees binary junk.
/// Returns where the file really starts: a byte order mark and blank lines
/// before the header are harmless, but OpenCASCADE refuses them.
static size_t checkLooksLikeStep(const char *data, size_t size) {
  static const char magic[] = "ISO-10303-21";
  size_t start = 0;
  if (size >= 3 && std::memcmp(data, "\xEF\xBB\xBF", 3) == 0) {
    start = 3; // a UTF-8 byte order mark
  }
  while (start < size && std::isspace(static_cast<unsigned char>(data[start]))) {
    start++;
  }
  if (size - start < sizeof(magic) - 1 ||
      std::memcmp(data + start, magic, sizeof(magic) - 1) != 0) {
    throw std::invalid_argument(
        "could not parse the data as a STEP file: it does not start with "
        "ISO-10303-21");
  }
  for (size_t i = start; i < size; i++) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    // bytes above 127 are allowed: names are often UTF-8 or Latin-1
    if ((c < 32 && c != '\t' && c != '\n' && c != '\r' && c != '\f') ||
        c == 127) {
      throw std::invalid_argument(
          "could not parse the data as a STEP file: it contains control "
          "characters, so it is binary or damaged");
    }
  }
  return start;
}

/// An IGES file is fixed-width text: every line carries a section letter
/// in column 73. Anything else is refused before OpenCASCADE parses it.
static void checkLooksLikeIges(const char *data, size_t size) {
  size_t lines = 0;
  for (size_t begin = 0; begin < size;) {
    const char *newline =
        static_cast<const char *>(std::memchr(data + begin, '\n', size - begin));
    size_t end = newline == nullptr ? size : static_cast<size_t>(newline - data);
    const size_t next = end + 1;
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(data[end - 1]))) {
      end--; // trailing spaces and the carriage return
    }
    if (end > begin) {
      // B and C mark the rare binary and compressed forms
      if (end - begin < 73 || end - begin > 80 ||
          std::strchr("SGDPTBC", data[begin + 72]) == nullptr ||
          data[begin + 72] == '\0') {
        throw std::invalid_argument(
            "could not parse the data as an IGES file: line " +
            std::to_string(lines + 1) +
            " is not 80 columns with a section letter in column 73");
      }
      lines++;
    }
    begin = next;
  }
  if (lines == 0) {
    throw std::invalid_argument(
        "could not parse the data as an IGES file: it is empty");
  }
}

/// Read STEP bytes into `loaded`, keeping names, layers and optionally colors.
static void loadStep(Loaded &loaded, const char *data, size_t size,
                     const MeshOptions &mesh, bool use_colors) {
  const size_t start = checkLooksLikeStep(data, size);
  STEPCAFControl_Reader reader;
  imemstream stream(data + start, size - start);
  {
    Span span("parse");
    if (reader.ReadStream("data.step", stream) != IFSelect_RetDone) {
      throw std::invalid_argument("could not parse the data as a STEP file");
    }
  }
  reader.SetColorMode(use_colors);
  reader.SetNameMode(true);
  reader.SetLayerMode(true);
  {
    Span span("build shapes");
    if (!reader.Transfer(loaded.doc)) {
      throw std::runtime_error("could not convert the STEP file to shapes");
    }
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
  checkLooksLikeIges(data, size);
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
  const IFSelect_ReturnStatus status = reader.ReadFile(handle.path());
#endif
  if (status != IFSelect_RetDone) {
    throw std::invalid_argument("could not parse the data as an IGES file");
  }

  Span span("build shapes");
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
/// Zstandard-compressed input is recognized and unpacked first.
static void load(Loaded &loaded, const char *data, size_t size,
                 FileType file_type, const MeshOptions &mesh,
                 bool use_colors = true) {
  std::vector<char> unpacked;
  if (isZstd(data, size)) {
    Span span("decompress");
    unpacked = zstdDecompress(data, size);
    data = unpacked.data();
    size = unpacked.size();
  }
  switch (file_type) {
  case FileType::STEP:
    return loadStep(loaded, data, size, mesh, use_colors);
  case FileType::IGES:
    return loadIges(loaded, data, size, mesh);
  default:
    throw std::invalid_argument("unsupported file type");
  }
}
