#include "convert.hpp"

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <Standard_Failure.hxx>
#include <nanobind/nanobind.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>

#include <fstream>
#include <iterator>
#include <mutex>
#include <new>
#include <type_traits>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace nb = nanobind;
using namespace nb::literals;

/// The text of an OpenCASCADE error. It became a std::exception in OCCT 8;
/// the template keeps the branch for the other version from being compiled.
template <typename Failure> static std::string failureText(const Failure &f) {
  if constexpr (std::is_base_of_v<std::exception, Failure>) {
    return f.what();
  } else {
    return f.GetMessageString();
  }
}

/// Run a conversion with the GIL released so other Python threads keep
/// running, one conversion at a time, and turn every failure into a Python
/// exception:
///   std::invalid_argument -> ValueError    (the input is bad)
///   std::bad_alloc        -> MemoryError
///   anything else         -> RuntimeError  (OpenCASCADE failed)
template <typename Function> static auto guarded(Function &&convert) {
  nb::gil_scoped_release release;
  // OpenCASCADE's application and document registry are unsynchronized
  // globals, so two conversions must not overlap inside one process.
  // (Each conversion still uses many threads; `cascadio.load` runs them
  // in separate processes, which do run side by side.)
  static std::mutex one_at_a_time;
  const std::lock_guard<std::mutex> lock(one_at_a_time);
  try {
    return convert();
  } catch (const std::invalid_argument &) {
    throw;
  } catch (const std::bad_alloc &) {
    throw;
  } catch (const Standard_Failure &failure) {
    throw std::runtime_error("OpenCASCADE error: " + failureText(failure));
  } catch (const std::exception &error) {
    throw std::runtime_error(error.what());
  }
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

NB_MODULE(_core, m) {
  m.doc() = "C++ core of cascadio: converts STEP and IGES to GLB and OBJ.\n\n"
            "These functions run in the calling process with no time or\n"
            "memory limit. Prefer `cascadio.load`.";

  // A library should not write to the terminal: drop OCCT's default
  // console printer. Failures are raised as exceptions instead.
  Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_Printer));

  nb::enum_<FileType>(m, "FileType")
      .value("UNSPECIFIED", FileType::UNSPECIFIED)
      .value("STEP", FileType::STEP)
      .value("IGES", FileType::IGES)
      .export_values();

  nb::enum_<RWMesh_NameFormat>(m, "NodeNameFormat")
      .value("PRODUCT", RWMesh_NameFormat_Product)
      .value("INSTANCE", RWMesh_NameFormat_Instance)
      .value("INSTANCE_OR_PRODUCT", RWMesh_NameFormat_InstanceOrProduct)
      .value("PRODUCT_OR_INSTANCE", RWMesh_NameFormat_ProductOrInstance)
      .value("PRODUCT_AND_INSTANCE", RWMesh_NameFormat_ProductAndInstance)
      .export_values();

  m.def(
      "to_glb_bytes",
      [](nb::bytes data, FileType file_type, double tol_linear,
         double tol_angular, bool tol_relative, bool merge_primitives,
         bool use_parallel, bool include_brep,
         std::set<std::string> brep_types, bool include_materials,
         RWMesh_NameFormat node_name_format, bool y_up) {
        const std::vector<char> glb = guarded([&] {
          return toGlb(data.c_str(), data.size(), file_type,
                       {{tol_linear, tol_angular, tol_relative, use_parallel},
                        merge_primitives, include_brep, std::move(brep_types),
                        include_materials, node_name_format, y_up});
        });
        return nb::bytes(glb.data(), glb.size());
      },
      R"doc(
Convert STEP or IGES bytes to GLB bytes, in memory.

See `cascadio.load` for what the parameters mean.

Raises ValueError if the data cannot be parsed and RuntimeError if
OpenCASCADE fails to convert it.
)doc",
      "data"_a, "file_type"_a = FileType::STEP, "tol_linear"_a = 0.01,
      "tol_angular"_a = 0.5, "tol_relative"_a = false,
      "merge_primitives"_a = true, "use_parallel"_a = true,
      "include_brep"_a = false, "brep_types"_a = std::set<std::string>(),
      "include_materials"_a = false,
      "node_name_format"_a = RWMesh_NameFormat_InstanceOrProduct,
      "y_up"_a = false);

  m.def(
      "step_to_glb",
      [](const std::string &input_path, const std::string &output_path,
         double tol_linear, double tol_angular, bool tol_relative,
         bool merge_primitives, bool use_parallel, bool include_brep,
         std::set<std::string> brep_types, bool include_materials,
         RWMesh_NameFormat node_name_format) {
        deprecated("step_to_glb");
        guarded([&] {
          const std::vector<char> step = readFile(input_path);
          const std::vector<char> glb = toGlb(
              step.data(), step.size(), FileType::STEP,
              {{tol_linear, tol_angular, tol_relative, use_parallel},
               merge_primitives, include_brep, std::move(brep_types),
               include_materials, node_name_format});
          std::ofstream file(output_path, std::ios::binary);
          file.write(glb.data(), static_cast<std::streamsize>(glb.size()));
          if (!file) {
            throw std::runtime_error("cannot write output file: " +
                                     output_path);
          }
          return 0;
        });
        return 0;
      },
      R"doc(
Deprecated: convert a STEP file on disk to a GLB file on disk.

Use `cascadio.load`, which takes and returns bytes, never touches the
filesystem, and enforces a time and memory limit. This function runs in
the calling process with no limits.

Returns 0. Failures raise instead of returning 1.
)doc",
      "input_path"_a, "output_path"_a, "tol_linear"_a = 0.01,
      "tol_angular"_a = 0.5, "tol_relative"_a = false,
      "merge_primitives"_a = true, "use_parallel"_a = true,
      "include_brep"_a = false, "brep_types"_a = std::set<std::string>(),
      "include_materials"_a = false,
      "node_name_format"_a = RWMesh_NameFormat_InstanceOrProduct);

  m.def(
      "step_to_obj",
      [](const std::string &input_path, const std::string &output_path,
         double tol_linear, double tol_angular, bool tol_relative,
         bool use_parallel, bool use_colors) {
        deprecated("step_to_obj");
        guarded([&] {
          const std::vector<char> step = readFile(input_path);
          stepToObj(step.data(), step.size(), output_path,
                    {tol_linear, tol_angular, tol_relative, use_parallel},
                    use_colors);
          return 0;
        });
        return 0;
      },
      R"doc(
Deprecated: convert a STEP file on disk to an OBJ file on disk.

A MTL file is written next to the OBJ when the STEP has colors and
`use_colors` is set. Use `cascadio.load` and export the GLB with a mesh
library instead. This function runs in the calling process with no limits.

Returns 0. Failures raise instead of returning 1.
)doc",
      "input_path"_a, "output_path"_a, "tol_linear"_a = 0.01,
      "tol_angular"_a = 0.5, "tol_relative"_a = false,
      "use_parallel"_a = true, "use_colors"_a = true);

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
