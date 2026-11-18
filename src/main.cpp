#include "convert.hpp"

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <Standard_Failure.hxx>
#include <nanobind/nanobind.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>

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
/// running, and turn every failure into a Python exception:
///   std::invalid_argument -> ValueError    (the input is bad)
///   std::bad_alloc        -> MemoryError
///   anything else         -> RuntimeError  (OpenCASCADE failed)
template <typename Function> static auto guarded(Function &&convert) {
  nb::gil_scoped_release release;
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
      "step_to_obj_file",
      [](nb::bytes data, const std::string &output_path, double tol_linear,
         double tol_angular, bool tol_relative, bool use_parallel,
         bool use_colors) {
        guarded([&] {
          stepToObj(data.c_str(), data.size(), output_path,
                    {tol_linear, tol_angular, tol_relative, use_parallel},
                    use_colors);
          return 0;
        });
      },
      R"doc(
Convert STEP bytes to an OBJ file at `output_path`.

A MTL file is written next to it when the STEP has colors and
`use_colors` is set. This is the only function that writes to disk.
)doc",
      "data"_a, "output_path"_a, "tol_linear"_a = 0.01, "tol_angular"_a = 0.5,
      "tol_relative"_a = false, "use_parallel"_a = true,
      "use_colors"_a = true);

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
