# cascadio

A Python library which uses [OpenCASCADE](https://github.com/Open-Cascade-SAS/OCCT) to convert STEP files to an in-memory GLB file which can quickly be loaded by [trimesh](https://github.com/mikedh/trimesh) and other libraries.

The primary effort here is build and packaging using the wonderful work done recently on [scikit-build-core](https://github.com/scikit-build/scikit-build-core) and [cibuildwheel](https://github.com/pypa/cibuildwheel). The goal is to produce wheels for every reasonable platform.

This is *not* intended to be a full binding of OpenCASCADE like [OCP](https://github.com/CadQuery/OCP) or [PythonOCC](https://github.com/tpaviot/pythonocc-core). Rather it is intended to be an easy minimal way to load boundary representation files into a triangulated scene in Python. There are a few options for loading STEP geometry in the open-source ecosystem: GMSH, FreeCAD, etc. However nearly all of them use OpenCASCADE under the hood, as it is pretty much the only open-source BREP kernel.


### Local Build

Developed on Linux which should build locally with docker:

```
# keeping the containers means the OCCT build is cached
# which makes debugging a lot easier
# only building one target 
CIBW_DEBUG_KEEP_CONTAINER=1 CIBW_BUILD="cp312-manylinux-x86_64" cibuildwheel --platform linux
```
