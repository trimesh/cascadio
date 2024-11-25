#include "convert.hpp"
#include <pybind11/pybind11.h>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

PYBIND11_MODULE(cascadio, m) {
  m.doc() = R"pbdoc(
        cascadio
        ---------
        A module for converting BREP files into GLB and OBJ.
    )pbdoc";

  m.def("step_to_glb",
	&step_to_glb,
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

)pbdoc",
	py::arg("input_path"),
	py::arg("output_path"),
	py::arg("tol_linear") = 0.01,
	py::arg("tol_angular") = 0.5,
	py::arg("tol_relative") = false,
	py::arg("merge_primitives") = true,
	py::arg("use_parallel") = true
	);

  m.def("step_to_obj",
	&step_to_obj,
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
swap_z_and_y_axis
  Whether to swap y and z axis in the exported meshes.

)pbdoc",
	py::arg("input_path"),
	py::arg("output_path"),
	py::arg("tol_linear") = 0.01,
	py::arg("tol_angular") = 0.5,
	py::arg("tol_relative") = false,
	py::arg("use_parallel") = true,
	py::arg("use_colors") = true,
	py::arg("swap_z_and_y_axis") = true
	);

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
