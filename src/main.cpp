#include "convert.hpp"

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <nanobind/nanobind.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>

#include <fstream>
#include <iostream>
#include <iterator>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace nb = nanobind;
using namespace nb::literals;

/// Read a whole file, or throw.
static std::vector<char> readFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot open input file: " + path);
  }
  return std::vector<char>(std::istreambuf_iterator<char>(file), {});
}

/// Run a conversion, reporting any failure on stderr.
/// Returns false if it threw.
template <typename Function> static bool report(Function &&convert) {
  try {
    convert();
    return true;
  } catch (const std::exception &error) {
    std::cerr << "cascadio: " << error.what() << std::endl;
    return false;
  }
}

static const char *PARAMETERS = R"doc(
tol_linear
  How large should linear deflection be allowed.
tol_angular
  How large should angular deflection be allowed.
tol_relative
  Is tol_linear relative to edge length, or an absolute distance?
merge_primitives
  Produce a GLB with one mesh primitive per part.
use_parallel
  Use parallel execution to produce meshes and exports.
include_brep
  Include BREP analytical primitive data in the TM_brep_faces extension.
  Turns merge_primitives on.
brep_types
  If non-empty, only include these primitive types in brep_faces.
  Valid values: "plane", "cylinder", "cone", "sphere", "torus".
include_materials
  Include material data (names, density, colors) in the GLB.
  Turns merge_primitives on.
node_name_format
  Name format for GLB nodes and meshes; controls whether instance
  names, product names, or both are used.
)doc";

NB_MODULE(_core, m) {
  m.doc() = "C++ core of cascadio: converts STEP and IGES to GLB and OBJ.";

  // A library should not write to the terminal: drop OCCT's default
  // console printer. Failures are reported through return values.
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
         RWMesh_NameFormat node_name_format) {
        std::vector<char> glb;
        report([&] {
          glb = toGlb(data.c_str(), data.size(), file_type,
                      {{tol_linear, tol_angular, tol_relative, use_parallel},
                       merge_primitives, include_brep, std::move(brep_types),
                       include_materials, node_name_format});
        });
        return nb::bytes(glb.data(), glb.size());
      },
      (std::string(R"doc(
Convert BREP data (STEP or IGES bytes) to GLB bytes without temp files.
Returns empty bytes on error.

data
  The STEP or IGES file content.
file_type
  FileType.STEP or FileType.IGES)doc") +
       PARAMETERS)
          .c_str(),
      "data"_a, "file_type"_a = FileType::STEP, "tol_linear"_a = 0.01,
      "tol_angular"_a = 0.5, "tol_relative"_a = false,
      "merge_primitives"_a = true, "use_parallel"_a = true,
      "include_brep"_a = false, "brep_types"_a = std::set<std::string>(),
      "include_materials"_a = false,
      "node_name_format"_a = RWMesh_NameFormat_InstanceOrProduct);

  m.def(
      "step_to_glb",
      [](const std::string &input_path, const std::string &output_path,
         double tol_linear, double tol_angular, bool tol_relative,
         bool merge_primitives, bool use_parallel, bool include_brep,
         std::set<std::string> brep_types, bool include_materials,
         RWMesh_NameFormat node_name_format) {
        const bool ok = report([&] {
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
        });
        return ok ? 0 : 1;
      },
      (std::string(R"doc(
Convert a STEP file to a GLB file. Returns 0 on success and 1 on error.

input_path
  The STEP file to load.
output_path
  Where to save the GLB file.)doc") +
       PARAMETERS)
          .c_str(),
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
        const bool ok = report([&] {
          const std::vector<char> step = readFile(input_path);
          stepToObj(step.data(), step.size(), output_path,
                    {tol_linear, tol_angular, tol_relative, use_parallel},
                    use_colors);
        });
        return ok ? 0 : 1;
      },
      R"doc(
Convert a STEP file to an OBJ file. Returns 0 on success and 1 on error.

input_path
  The STEP file to load.
output_path
  Where to save the OBJ file. A MTL file is written next to it
  when the STEP has colors and use_colors is set.
tol_linear
  How large should linear deflection be allowed.
tol_angular
  How large should angular deflection be allowed.
tol_relative
  Is tol_linear relative to edge length, or an absolute distance?
use_parallel
  Use parallel execution to produce meshes and exports.
use_colors
  Whether to export colors and materials from the STEP input.
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
