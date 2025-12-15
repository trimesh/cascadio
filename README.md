# cascadio

A Python library which uses [OpenCASCADE](https://github.com/Open-Cascade-SAS/OCCT) to convert STEP files to a GLB file which can quickly be loaded by [trimesh](https://github.com/mikedh/trimesh) and other libraries.

The primary effort here is build and packaging using the wonderful work done recently on [scikit-build-core](https://github.com/scikit-build/scikit-build-core) and [cibuildwheel](https://github.com/pypa/cibuildwheel). The goal is to produce wheels that don't require users to build OpenCASCADE themselves.

This is *not* intended to be a full binding of OpenCASCADE like [OCP](https://github.com/CadQuery/OCP) or [PythonOCC](https://github.com/tpaviot/pythonocc-core). Rather it is intended to be an easy minimal way to load boundary representation files into a triangulated scene in Python. There are a few options for loading STEP geometry in the open-source ecosystem: GMSH, FreeCAD, etc. However nearly all of them use OpenCASCADE under the hood as it is pretty much the only open-source BREP kernel.



### Install

The primary goal of this project is building wheels so vanilla `pip` can be used:

```
pip install cascadio
```

Currently this works on non-MUSL flavors of Linux, Windows x64, and MacOS x64+ARM. You can check [PyPi](https://pypi.org/project/cascadio/#files) for current platforms.

### :warning: :warning: :warning: :warning:
PyPI has a size limit, and each release of this is large! We will *not* be keeping every release on PyPi (i.e. if we run out of space we delete versions) so be very careful pinning the version!

We'll keep the following versions as "LTS" style releases on PyPi:
```
pip install cascadio==0.0.13
```


### Motivation

A lot of analysis can be done on triangulated surface meshes that doesn't need the analytical surfaces from a STEP or BREP file. 

### Contributing

Developed on Linux which should build wheels locally with docker:
```bash
# Build wheels using cibuildwheel (builds OCCT inside container)
CIBW_BUILD="cp312-manylinux_x86_64" cibuildwheel --platform linux
```

For local development without docker:
```bash
# build OCCT to occt_cache/
python scripts/build_occt.py

# source the env vars it writes
source occt_cache/env.sh

# install and test
pip install -e .
pytest tests/
```


### Future Work

Pull requests welcome! 

- Add passable parameters for options included in the RWGLTF writer.
- use in-memory data for input and output, i.e. `stepReader.ReadStream()` instead of a file name. Ideally the Python function signature would be:
  - `convert_to_glb(data: bytes, file_type: str, **parameters) -> bytes`
  - Currently using file names because it's easier. 
- Support IGES 
  - Investigate using OpenCASCADE "Advanced Data Exchange" for Parasolid `.x_b`/`.x_t` and JT `.jt` support.

