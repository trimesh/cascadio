#include "convert.hpp"
#include <pybind11/pybind11.h>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

int add(int i, int j) { return i + j; }

namespace py = pybind11;

PYBIND11_MODULE(cascadio, m) {
  m.doc() = R"pbdoc(
        cascadio
        ---------
        A module for converting BREP files into GLB.
    )pbdoc";

  m.def("step_to_glb", &step_to_glb, R"pbdoc(
        Convert a step file to a GLB file.
    )pbdoc");

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
