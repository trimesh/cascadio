// The Python module. It holds no OpenCASCADE: every conversion is done by
// the `cascadio-worker` program next to this file, which this module starts,
// feeds, waits for with a time limit, and reports on. See request.hpp for
// the contract between the two and process.hpp for the process handling.

// Python's headers set feature macros and have to come before any other
#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>

#include "process.hpp"
#include "request.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>

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
  const std::string override = environmentValue("CASCADIO_WORKER");
  if (!override.empty()) {
    return override;
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

/// How a crashed worker ended, for a person: "SIGSEGV, a segmentation
/// fault", or on Windows "0xC0000005, an access violation".
static std::string crashName(const ProcessResult &result) {
#ifdef _WIN32
  const auto code = static_cast<unsigned>(result.exit_code);
  switch (code) {
  case 0xC0000005:
    return "0xC0000005, an access violation";
  case 0xC00000FD:
    return "0xC00000FD, a stack overflow";
  case 0xC0000409:
    return "0xC0000409, a buffer overrun";
  case 3:
    return "exit code 3, an abort";
  default:
    break;
  }
  char hex[16];
  std::snprintf(hex, sizeof(hex), "0x%08X", code);
  return std::string("exit code ") +
         (code >= 0xC0000000 ? hex : std::to_string(code));
#else
  switch (result.signal) {
  case 0:
    return "exit code " + std::to_string(result.exit_code);
  case SIGSEGV:
    return "SIGSEGV, a segmentation fault";
  case SIGBUS:
    return "SIGBUS, a bus error";
  case SIGFPE:
    return "SIGFPE, an arithmetic error";
  case SIGILL:
    return "SIGILL, an illegal instruction";
  case SIGABRT:
    return "SIGABRT, an abort";
  case SIGKILL:
    return "SIGKILL, perhaps the system ran out of memory";
  default:
    return "signal " + std::to_string(result.signal);
  }
#endif
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
  try {
    // other Python threads keep running while the worker does
    nb::gil_scoped_release release;
    result = runProcess(program, head, data, size, timeout.value_or(-1.0), [] {
      // Ctrl-C: Python's handler runs here, and raises KeyboardInterrupt
      nb::gil_scoped_acquire acquire;
      return PyErr_CheckSignals() != 0;
    });
  } catch (const StartError &error) {
    switch (error.kind) {
    case StartError::MISSING:
      throw std::runtime_error("cascadio's converter is missing at " +
                               program +
                               "; reinstall cascadio, or check CASCADIO_WORKER");
    case StartError::NOT_RUNNABLE:
      throw std::runtime_error("cascadio's converter at " + program +
                               " cannot be run (" + error.what() +
                               "); the folder may not allow running programs");
    default:
      throw std::runtime_error("cascadio's converter at " + program +
                               " could not be started (" + error.what() + ")");
    }
  }
  // a signal handler that raised, such as Ctrl-C's, wins over how the
  // worker ended
  if (PyErr_CheckSignals() != 0 || PyErr_Occurred() != nullptr) {
    throw nb::python_error();
  }
  if (result.interrupted) {
    raisePython(PyExc_KeyboardInterrupt, "");
  }
  if (result.timed_out) {
    raisePython(PyExc_TimeoutError,
          "conversion did not finish within " + std::to_string(*timeout) +
              " seconds; pass a larger `timeout`, or None for no limit");
  }
  // What the worker said: warnings to pass on, and a last line that says
  // why it failed (anything before it is trace output).
  std::string said;
  std::istringstream lines(result.errors);
  for (std::string line; std::getline(lines, line);) {
    while (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind(worker_warning, 0) == 0) {
      if (PyErr_WarnEx(PyExc_RuntimeWarning,
                       line.c_str() + sizeof(worker_warning) - 1, 1) != 0) {
        throw nb::python_error(); // warnings are errors here
      }
    } else if (!line.empty()) {
      said = line;
    }
  }
  switch (result.exit_code) {
  case worker_exit::OK:
    if (!environmentValue("CASCADIO_TRACE").empty()) {
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
  case worker_exit::NO_SANDBOX:
    throw std::runtime_error(
        "cascadio could not sandbox its converter, so it did not run it: " +
        said);
  case worker_exit::MISMATCH:
    throw std::runtime_error("the converter at " + program +
                             " does not match this cascadio (" + said +
                             "); reinstall cascadio");
  default:
#ifdef SIGSYS
    if (result.signal == SIGSYS) {
      // the lock-down's answer to a system call it does not allow
      // (sandbox.hpp); run the worker under `strace -f` to see which
      throw std::runtime_error(
          "the converter was stopped by its sandbox: it tried something the "
          "sandbox forbids. This is a bug in cascadio; please report it "
          "with the file");
    }
#endif
    throw std::runtime_error("the converter crashed on this file (" +
                             crashName(result) +
                             "); the file is probably damaged, or "
                             "OpenCASCADE cannot handle it" +
                             (said.empty() ? "" : ": " + said));
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

  // where the module finds the worker, for the tests to run it directly
  m.def("_worker_path", workerPath);
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

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
