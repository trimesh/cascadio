#include "convert.hpp"
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/set.h>

#include <cstddef>
#include <vector>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace nb = nanobind;

NB_MODULE(_core, m) {
  m.doc() = R"doc(
        cascadio._core
        --------------
        C++ core module for converting BREP files into GLB and OBJ.
    )doc";

  nb::enum_<FileType>(m, "FileType")
      .value("UNSPECIFIED", FileType::UNSPECIFIED)
      .value("STEP", FileType::STEP)
      .value("IGES", FileType::IGES)
      .export_values();

  m.def(
      "to_glb_bytes",
      [](nb::bytes data, FileType file_type, double tol_linear,
         double tol_angle, bool tol_relative, bool merge_primitives,
         bool use_parallel, bool include_brep, std::set<std::string> brep_types,
         bool include_materials) -> nb::bytes {
        // Force merge_primitives when BREP/materials requested (metadata requires merged faces)
        if ((include_brep || include_materials) && !merge_primitives) {
          std::cerr << "Warning: include_brep/include_materials require merge_primitives=true, enabling automatically" << std::endl;
          merge_primitives = true;
        }

        const char* input_data = static_cast<const char*>(data.data());
        size_t input_data_len = data.size();

        std::vector<char> result =
            to_glb_bytes(input_data, input_data_len, file_type, tol_linear, tol_angle,
                         tol_relative, merge_primitives, use_parallel,
                         include_brep, brep_types, include_materials);
        return nb::bytes(result.data(), result.size());
      },
      R"doc(
Convert BREP data (STEP or IGES bytes) to GLB data (bytes) without temp files.

Parameters
----------
data
  The BREP file content as bytes.
file_type
  The file type: FileType.STEP or FileType.IGES
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
  Include BREP analytical primitive data in GLB extras.
brep_types
  If non-empty, only include these primitive types in brep_faces.
include_materials
  Include material data in GLB asset.extras.materials.

Returns
-------
bytes
  The GLB file content as bytes, or empty bytes on error.

)doc",
      nb::arg("data"), nb::arg("file_type") = FileType::STEP,
      nb::arg("tol_linear") = 0.01, nb::arg("tol_angular") = 0.5,
      nb::arg("tol_relative") = false, nb::arg("merge_primitives") = true,
      nb::arg("use_parallel") = true, nb::arg("include_brep") = false,
      nb::arg("brep_types") = std::set<std::string>(),
      nb::arg("include_materials") = false);

  // Backward compatibility wrappers
  m.def(
      "step_to_glb",
      [](const std::string &input_path, const std::string &output_path, double tol_linear,
         double tol_angle, bool tol_relative, bool merge_primitives,
         bool use_parallel, bool include_brep, std::set<std::string> brep_types,
         bool include_materials) -> int {
        // Force merge_primitives when BREP/materials requested (metadata requires merged faces)
        if ((include_brep || include_materials) && !merge_primitives) {
          std::cerr << "Warning: include_brep/include_materials require merge_primitives=true, enabling automatically" << std::endl;
          merge_primitives = true;
        }
        return to_glb(input_path.c_str(), output_path.c_str(), FileType::STEP, tol_linear,
                      tol_angle, tol_relative, merge_primitives, use_parallel,
                      include_brep, brep_types, include_materials);
      },
      R"doc(
Convert a step file to a GLB file.

Parameters
----------
input_path
  The input STEP file to load.
output_path
  The path to save the GLB file.
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
  Include BREP analytical primitive data in GLB extras.
  Primitives (plane, cylinder, cone, sphere, torus) are stored
  in extras.brep_faces with face_index, type, and geometry params.
brep_types
  If non-empty, only include these primitive types in brep_faces.
  Valid values: "plane", "cylinder", "cone", "sphere", "torus".
  If empty (default), all primitive types are included.
include_materials
  Include material data in GLB asset.extras.materials.
  Materials include physical properties (name, density) and
  visual properties (colors, PBR metallic/roughness).

)doc",
      nb::arg("input_path"), nb::arg("output_path"),
      nb::arg("tol_linear") = 0.01, nb::arg("tol_angular") = 0.5,
      nb::arg("tol_relative") = false, nb::arg("merge_primitives") = true,
      nb::arg("use_parallel") = true, nb::arg("include_brep") = false,
      nb::arg("brep_types") = std::set<std::string>(),
      nb::arg("include_materials") = false);

  m.def("step_to_obj", &step_to_obj,
        R"doc(
Convert a step file to a OBJ ( and if applicable MTL ) file.

Parameters
----------
input_path
  The input STEP file to load.
output_path
  The path to save the OBJ ( and if applicable MTL ) file.
tol_linear
  How large should linear deflection be allowed.
tol_angular
  How large should angular deflection be allowed.
tol_relative
  Is tol_linear relative to edge length, or an absolute distance?
use_parallel
  Use parallel execution to produce meshes and exports.
use_colors
  Whether to export/use colors/materials from the STEP input.
  Disabling colors will skip exporting a MTL sidecar file.
  If input STEP doesn't use color/material then no MTL will be exported,
  regardless of 'use_colors'.

)doc",
        nb::arg("input_path"), nb::arg("output_path"),
        nb::arg("tol_linear") = 0.01, nb::arg("tol_angular") = 0.5,
        nb::arg("tol_relative") = false, nb::arg("use_parallel") = true,
        nb::arg("use_colors") = true);

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
