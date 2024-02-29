# cascadio

A Python library which uses [OpenCASCADE](https://github.com/Open-Cascade-SAS/OCCT) to convert STEP and IGES files to an in-memory GLB file which can quickly be loaded by [trimesh](https://github.com/mikedh/trimesh) and other libraries.

The intention is to use `scikit-build` and `cibuildwheel` to produce wheels for every reasonable platform.

This is *not* intended to be a full binding of OpenCASCADE like [OCP](https://github.com/CadQuery/OCP) or [PythonOCC](https://github.com/tpaviot/pythonocc-core). Rather it is intended to be an easy minimal way to load STEP and IGES files in Python. There are many ways to load STEP in the open-source ecosystem: GMSH, FreeCAD, etc. However all of them with very few exceptions use OpenCASCADE under the hood as it i
