#include "convert.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

PYBIND11_MODULE(_core, m) {
  m.doc() = R"pbdoc(
        cascadio._core
        --------------
        C++ core module for converting BREP files into GLB and OBJ.
    )pbdoc";

  py::enum_<FileType>(m, "FileType")
      .value("UNSPECIFIED", FileType::UNSPECIFIED)
      .value("STEP", FileType::STEP)
      .value("IGES", FileType::IGES)
      .export_values();

  m.def("to_glb", &to_glb,
        R"pbdoc(
Convert a BREP file (STEP or IGES) to a GLB file.

Parameters
----------
input_path
  The input BREP file to load.
output_path
  The path to save the GLB file.
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

)pbdoc",
        py::arg("input_path"), py::arg("output_path"),
        py::arg("file_type") = FileType::STEP, py::arg("tol_linear") = 0.01,
        py::arg("tol_angular") = 0.5, py::arg("tol_relative") = false,
        py::arg("merge_primitives") = true, py::arg("use_parallel") = true,
        py::arg("include_brep") = false,
        py::arg("brep_types") = std::set<std::string>(),
        py::arg("include_materials") = false);

  m.def(
      "to_glb_bytes",
      [](py::bytes data, FileType file_type, Standard_Real tol_linear,
         Standard_Real tol_angle, Standard_Boolean tol_relative,
         Standard_Boolean merge_primitives, Standard_Boolean use_parallel,
         Standard_Boolean include_brep, std::set<std::string> brep_types,
         Standard_Boolean include_materials) -> py::bytes {
        std::string input_data = data;
        std::string result =
            to_glb_bytes(input_data, file_type, tol_linear, tol_angle,
                         tol_relative, merge_primitives, use_parallel,
                         include_brep, brep_types, include_materials);
        return py::bytes(result);
      },
      R"pbdoc(
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

)pbdoc",
      py::arg("data"), py::arg("file_type") = FileType::STEP,
      py::arg("tol_linear") = 0.01, py::arg("tol_angular") = 0.5,
      py::arg("tol_relative") = false, py::arg("merge_primitives") = true,
      py::arg("use_parallel") = true, py::arg("include_brep") = false,
      py::arg("brep_types") = std::set<std::string>(),
      py::arg("include_materials") = false);

  // Backward compatibility wrappers
  m.def("step_to_glb", &step_to_glb,
        R"pbdoc(
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

)pbdoc",
        py::arg("input_path"), py::arg("output_path"),
        py::arg("tol_linear") = 0.01, py::arg("tol_angular") = 0.5,
        py::arg("tol_relative") = false, py::arg("merge_primitives") = true,
        py::arg("use_parallel") = true, py::arg("include_brep") = false,
        py::arg("brep_types") = std::set<std::string>(),
        py::arg("include_materials") = false);

  m.def(
      "step_to_glb_bytes",
      [](py::bytes step_data, Standard_Real tol_linear, Standard_Real tol_angle,
         Standard_Boolean tol_relative, Standard_Boolean merge_primitives,
         Standard_Boolean use_parallel, Standard_Boolean include_brep,
         std::set<std::string> brep_types,
         Standard_Boolean include_materials) -> py::bytes {
        std::string data = step_data;
        std::string result = step_to_glb_bytes(
            data, tol_linear, tol_angle, tol_relative, merge_primitives,
            use_parallel, include_brep, brep_types, include_materials);
        return py::bytes(result);
      },
      R"pbdoc(
Convert STEP data (bytes) to GLB data (bytes) without temp files.

Parameters
----------
step_data
  The STEP file content as bytes.
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

)pbdoc",
      py::arg("step_data"), py::arg("tol_linear") = 0.01,
      py::arg("tol_angular") = 0.5, py::arg("tol_relative") = false,
      py::arg("merge_primitives") = true, py::arg("use_parallel") = true,
      py::arg("include_brep") = false,
      py::arg("brep_types") = std::set<std::string>(),
      py::arg("include_materials") = false);

  m.def("step_to_obj", &step_to_obj,
        R"pbdoc(
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

)pbdoc",
        py::arg("input_path"), py::arg("output_path"),
        py::arg("tol_linear") = 0.01, py::arg("tol_angular") = 0.5,
        py::arg("tol_relative") = false, py::arg("use_parallel") = true,
        py::arg("use_colors") = true);

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
