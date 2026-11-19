// The Python module. It holds no OpenCASCADE: every conversion is done by
// the `cascadio-worker` program next to this file, which this module starts,
// feeds, waits for with a time limit, and reports on. See request.hpp for
// the contract between the two and process.hpp for the process handling.

#include "process.hpp"
#include "request.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace nb = nanobind;
using namespace nb::literals;

/// Ordinary files convert in well under a second using about 100 MB,
/// so these defaults only stop runaway inputs.
constexpr double DEFAULT_TIMEOUT = 30.0;
constexpr uint64_t DEFAULT_MEMORY_LIMIT = 8ull << 30;

using Timeout = std::optional<double>;
using MemoryLimit = std::optional<uint64_t>;
using BrepTypes = std::optional<std::set<std::string>>;

/// Where the worker program is. It is installed next to this module;
/// CASCADIO_WORKER overrides that for packagers who put it elsewhere.
static std::string workerPath() {
  if (const char *path = std::getenv("CASCADIO_WORKER")) {
    return path;
  }
#ifdef _WIN32
  return thisBinaryFolder() + "\\cascadio-worker.exe";
#else
  return thisBinaryFolder() + "/cascadio-worker";
#endif
}

/// Raise a Python exception of a built-in type that nanobind has no
/// C++ class for.
[[noreturn]] static void raisePython(PyObject *type, const std::string &message) {
  PyErr_SetString(type, message.c_str());
  throw nb::python_error();
}

/// Run one request through the worker and return what it produced,
/// or raise the Python exception that matches how it ended.
static std::vector<char> convert(Request request, const char *data, size_t size,
                                 Timeout timeout, MemoryLimit memory_limit) {
  if (timeout && !(*timeout > 0)) {
    throw std::invalid_argument("timeout must be positive, or None");
  }
  request.memory_limit = memory_limit.value_or(0);
  const std::string head = request.serialize();
  const std::string program = workerPath();

  ProcessResult result;
  {
    // other Python threads keep running while the worker does
    nb::gil_scoped_release release;
    result = runProcess(program, head, data, size, timeout.value_or(-1.0));
  }

  if (result.timed_out) {
    raisePython(PyExc_TimeoutError,
          "conversion did not finish within " + std::to_string(*timeout) +
              " seconds; pass a larger `timeout`, or None for no limit");
  }
  // one line of what the worker said, for the error message
  std::string said = result.errors;
  while (!said.empty() && (said.back() == '\n' || said.back() == '\r')) {
    said.pop_back();
  }
  switch (result.exit_code) {
  case worker_exit::OK:
    if (std::getenv("CASCADIO_TRACE") != nullptr) {
      // pass the worker's stage timings on to the terminal
      std::fwrite(result.errors.data(), 1, result.errors.size(), stderr);
    }
    return std::move(result.output);
  case worker_exit::INVALID_INPUT:
    throw std::invalid_argument(said);
  case worker_exit::OUT_OF_MEMORY:
    raisePython(PyExc_MemoryError,
          "conversion used more than " +
              std::to_string(memory_limit.value_or(0)) +
              " bytes of memory; pass a larger `memory_limit`, "
              "or None for no limit");
  case worker_exit::FAILED:
    throw std::runtime_error(said.empty() ? "conversion failed" : said);
  default:
    // not one of our exit codes: the worker crashed
    throw std::runtime_error(
        "converter process crashed (" +
        (result.signal != 0 ? "signal " + std::to_string(result.signal)
                            : "exit code " + std::to_string(result.exit_code)) +
        ")" + (said.empty() ? "" : ": " + said));
  }
}

/// "step", "stp", "iges" or "igs", in any letter case.
static FileType parseFileType(std::string name) {
  for (char &c : name) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (name == "step" || name == "stp") {
    return FileType::STEP;
  }
  if (name == "iges" || name == "igs") {
    return FileType::IGES;
  }
  throw std::invalid_argument("Unsupported file_type: " + name +
                              ". Use 'step' or 'iges'.");
}

/// Read a whole file, or throw.
static std::vector<char> readFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot open input file: " + path);
  }
  return std::vector<char>(std::istreambuf_iterator<char>(file), {});
}

/// Warn that a file-path function is going away. Raises if the caller has
/// turned warnings into errors.
static void deprecated(const char *name) {
  const std::string message =
      std::string("cascadio.") + name +
      " reads and writes files and is deprecated: it will be removed in the "
      "first release after January 2028. Read the file yourself and call "
      "cascadio.load(data), which takes and returns bytes.";
  if (PyErr_WarnEx(PyExc_DeprecationWarning, message.c_str(), 1) != 0) {
    throw nb::python_error();
  }
}

// ============================================================================
// Docstrings, numpy style. The shared parts are written once.
// ============================================================================

static const std::string DOC_MESH = R"doc(tol_linear
    The largest distance allowed between the mesh and the true surface,
    in the units of the file.
tol_angular
    The largest angle allowed between neighbouring mesh segments,
    in radians.
tol_relative
    If True, `tol_linear` is a fraction of each edge's length instead
    of an absolute distance.
)doc";

static const std::string DOC_GLB = R"doc(merge_primitives
    Produce one mesh primitive per part instead of one per face.
use_parallel
    Use multiple threads for meshing and export.
include_brep
    Add the `TM_brep_faces` extension: for every triangle the index of
    the CAD face it came from, the exact plane, cylinder, cone, sphere
    or torus of that face, and the materials of the part.
    Turns `merge_primitives` on.
brep_types
    If given, only these surface types are described. Any of "plane",
    "cylinder", "cone", "sphere" and "torus".
include_materials
    Add each part's material names, densities and colors without the
    rest of the extension. `include_brep` already includes them.
    Turns `merge_primitives` on.
node_name_format
    Which name each GLB node gets: the instance label, the product
    name, or a mix.
)doc";

static const std::string DOC_Y_UP = R"doc(y_up
    CAD files are almost always Z-up and by default the coordinates are
    kept as they are. glTF says +Y is up, so viewers such as Blender
    rotate what they import. Set this to rotate the output into glTF's
    frame, (x, y, z) -> (x, z, -y), so those viewers show the model the
    way the CAD system did.
)doc";

static const std::string DOC_LIMITS = R"doc(timeout
    Seconds the conversion may take, or None for no limit.
memory_limit
    Bytes of memory the conversion may use, or None for no limit.
)doc";

static const std::string DOC_RAISES = R"doc(
Raises
------
ValueError
    The data could not be parsed, or an argument is invalid.
TimeoutError
    The conversion took longer than `timeout`.
MemoryError
    The conversion used more than `memory_limit`.
RuntimeError
    OpenCASCADE failed to convert the file, or crashed.
)doc";

static const std::string DOC_LOAD =
    R"doc(Convert STEP or IGES data to a GLB, bytes in and bytes out.

The conversion runs in a separate short-lived process so that it can be
stopped: some files make OpenCASCADE hang or use all available memory,
and a crash in it would otherwise take the interpreter down. Nothing is
read from or written to disk.

Parameters
----------
data
    The STEP or IGES file content. It may be compressed with Zstandard
    (`.step.zst`): that is detected from the first four bytes and
    unpacked in memory.
)doc";

static const std::string DOC_RETURNS_GLB = R"doc(
Returns
-------
glb
    The GLB file content.
)doc";

static const std::string DOC_RETURNS_ZERO = R"doc(
Returns
-------
status
    Always 0. Failures raise.
)doc";

NB_MODULE(_core, m) {
  m.doc() = "The compiled part of cascadio. Each function starts the "
            "`cascadio-worker` program\nnext to this module to do the "
            "conversion, and enforces the limits.";

  nb::enum_<FileType>(m, "FileType")
      .value("UNSPECIFIED", FileType::UNSPECIFIED)
      .value("STEP", FileType::STEP)
      .value("IGES", FileType::IGES)
      .export_values();

  nb::enum_<NodeNameFormat>(m, "NodeNameFormat")
      .value("PRODUCT", NodeNameFormat::PRODUCT)
      .value("INSTANCE", NodeNameFormat::INSTANCE)
      .value("INSTANCE_OR_PRODUCT", NodeNameFormat::INSTANCE_OR_PRODUCT)
      .value("PRODUCT_OR_INSTANCE", NodeNameFormat::PRODUCT_OR_INSTANCE)
      .value("PRODUCT_AND_INSTANCE", NodeNameFormat::PRODUCT_AND_INSTANCE)
      .export_values();

  m.attr("DEFAULT_TIMEOUT") = DEFAULT_TIMEOUT;
  m.attr("DEFAULT_MEMORY_LIMIT") = DEFAULT_MEMORY_LIMIT;

  // `load` and `to_glb_bytes` differ only in how the file type is given
  auto toGlb = [](const nb::bytes &data, FileType file_type, double tol_linear,
                  double tol_angular, bool tol_relative, bool merge_primitives,
                  bool use_parallel, bool include_brep, BrepTypes brep_types,
                  bool include_materials, NodeNameFormat node_name_format,
                  bool y_up, Timeout timeout, MemoryLimit memory_limit) {
    Request request;
    request.file_type = file_type;
    request.tol_linear = tol_linear;
    request.tol_angular = tol_angular;
    request.tol_relative = tol_relative;
    request.merge_primitives = merge_primitives;
    request.use_parallel = use_parallel;
    request.include_brep = include_brep;
    request.brep_types = brep_types.value_or(std::set<std::string>());
    request.include_materials = include_materials;
    request.node_name_format = node_name_format;
    request.y_up = y_up;
    const std::vector<char> glb =
        convert(request, data.c_str(), data.size(), timeout, memory_limit);
    return nb::bytes(glb.data(), glb.size());
  };

  m.def(
      "load",
      [toGlb](const nb::bytes &data, const std::string &file_type,
              double tol_linear, double tol_angular, bool tol_relative,
              bool merge_primitives, bool use_parallel, bool include_brep,
              BrepTypes brep_types, bool include_materials,
              NodeNameFormat node_name_format, bool y_up, Timeout timeout,
              MemoryLimit memory_limit) {
        return toGlb(data, parseFileType(file_type), tol_linear, tol_angular,
                     tol_relative, merge_primitives, use_parallel, include_brep,
                     std::move(brep_types), include_materials, node_name_format,
                     y_up, timeout, memory_limit);
      },
      (DOC_LOAD + R"doc(file_type
    "step" or "iges" (also "stp" and "igs").
)doc" + DOC_MESH + DOC_GLB + DOC_Y_UP + DOC_LIMITS + DOC_RETURNS_GLB +
       DOC_RAISES + R"doc(
Examples
--------
>>> import cascadio
>>> with open("model.step", "rb") as f:
...     glb = cascadio.load(f.read())
>>> glb = cascadio.load(iges_bytes, file_type="iges", timeout=120)
)doc")
          .c_str(),
      "data"_a, "file_type"_a = "step", "tol_linear"_a = 0.01,
      "tol_angular"_a = 0.5, "tol_relative"_a = false,
      "merge_primitives"_a = true, "use_parallel"_a = true,
      "include_brep"_a = false, "brep_types"_a = nb::none(),
      "include_materials"_a = false,
      "node_name_format"_a = NodeNameFormat::INSTANCE_OR_PRODUCT,
      "y_up"_a = false, "timeout"_a = DEFAULT_TIMEOUT,
      "memory_limit"_a = DEFAULT_MEMORY_LIMIT);

  m.def("to_glb_bytes", toGlb,
        (DOC_LOAD + R"doc(file_type
    `FileType.STEP` or `FileType.IGES`. This is the only difference
    from `load`, which takes the name as a string.
)doc" + DOC_MESH + DOC_GLB + DOC_Y_UP + DOC_LIMITS + DOC_RETURNS_GLB +
         DOC_RAISES)
            .c_str(),
        "data"_a, "file_type"_a = FileType::STEP, "tol_linear"_a = 0.01,
        "tol_angular"_a = 0.5, "tol_relative"_a = false,
        "merge_primitives"_a = true, "use_parallel"_a = true,
        "include_brep"_a = false, "brep_types"_a = nb::none(),
        "include_materials"_a = false,
        "node_name_format"_a = NodeNameFormat::INSTANCE_OR_PRODUCT,
        "y_up"_a = false, "timeout"_a = DEFAULT_TIMEOUT,
        "memory_limit"_a = DEFAULT_MEMORY_LIMIT);

  m.def(
      "step_to_glb",
      [toGlb](const std::string &input_path, const std::string &output_path,
              double tol_linear, double tol_angular, bool tol_relative,
              bool merge_primitives, bool use_parallel, bool include_brep,
              BrepTypes brep_types, bool include_materials,
              NodeNameFormat node_name_format, Timeout timeout,
              MemoryLimit memory_limit) {
        deprecated("step_to_glb");
        const std::vector<char> step = readFile(input_path);
        const nb::bytes glb =
            toGlb(nb::bytes(step.data(), step.size()), FileType::STEP,
                  tol_linear, tol_angular, tol_relative, merge_primitives,
                  use_parallel, include_brep, std::move(brep_types),
                  include_materials, node_name_format, false, timeout,
                  memory_limit);
        std::ofstream file(output_path, std::ios::binary);
        file.write(glb.c_str(), static_cast<std::streamsize>(glb.size()));
        if (!file) {
          throw std::runtime_error("cannot write output file: " + output_path);
        }
        return 0;
      },
      (R"doc(Deprecated: convert a STEP file on disk to a GLB file on disk.

Use `load`, which takes and returns bytes and never touches the
filesystem. This function will be removed in the first release after
January 2028.

Parameters
----------
input_path
    The STEP file to read.
output_path
    Where to write the GLB file.
)doc" + DOC_MESH + DOC_GLB + DOC_LIMITS + DOC_RETURNS_ZERO + DOC_RAISES)
          .c_str(),
      "input_path"_a, "output_path"_a, "tol_linear"_a = 0.01,
      "tol_angular"_a = 0.5, "tol_relative"_a = false,
      "merge_primitives"_a = true, "use_parallel"_a = true,
      "include_brep"_a = false, "brep_types"_a = nb::none(),
      "include_materials"_a = false,
      "node_name_format"_a = NodeNameFormat::INSTANCE_OR_PRODUCT,
      "timeout"_a = DEFAULT_TIMEOUT, "memory_limit"_a = DEFAULT_MEMORY_LIMIT);

  m.def(
      "step_to_obj",
      [](const std::string &input_path, const std::string &output_path,
         double tol_linear, double tol_angular, bool tol_relative,
         bool use_parallel, bool use_colors, Timeout timeout,
         MemoryLimit memory_limit) {
        deprecated("step_to_obj");
        const std::vector<char> step = readFile(input_path);
        Request request;
        request.output = "obj";
        request.output_path = output_path;
        request.tol_linear = tol_linear;
        request.tol_angular = tol_angular;
        request.tol_relative = tol_relative;
        request.use_parallel = use_parallel;
        request.use_colors = use_colors;
        convert(request, step.data(), step.size(), timeout, memory_limit);
        return 0;
      },
      (R"doc(Deprecated: convert a STEP file on disk to an OBJ file on disk.

Use `load` and export the GLB with a mesh library instead. This
function will be removed in the first release after January 2028.

Parameters
----------
input_path
    The STEP file to read.
output_path
    Where to write the OBJ file. A MTL file is written next to it
    when the STEP has colors and `use_colors` is set.
)doc" + DOC_MESH + R"doc(use_parallel
    Use multiple threads for meshing and export.
use_colors
    Export colors and materials from the STEP file.
)doc" + DOC_LIMITS + DOC_RETURNS_ZERO + DOC_RAISES)
          .c_str(),
      "input_path"_a, "output_path"_a, "tol_linear"_a = 0.01,
      "tol_angular"_a = 0.5, "tol_relative"_a = false,
      "use_parallel"_a = true, "use_colors"_a = true,
      "timeout"_a = DEFAULT_TIMEOUT, "memory_limit"_a = DEFAULT_MEMORY_LIMIT);

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
