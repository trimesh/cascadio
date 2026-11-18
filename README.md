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
# fetch OpenCASCADE and RapidJSON
git submodule update --init --depth 1

# install build tools (clang and lld are used when installed)
uv pip install cmake ninja

# build static OpenCASCADE into occt_install/ (several minutes, done once)
python scripts/build_occt.py

# install and test
uv pip install -e ".[tests]"
pytest tests/
```

OpenCASCADE is built as static libraries with only the pieces cascadio uses, then linked into a single extension module. The linker removes every function that is not reachable, which is what keeps the wheels small.

#### Building against a system OpenCASCADE

The source distribution on PyPI contains no vendored code. It builds against the OpenCASCADE your system provides, which is what Linux and BSD packagers want:

```bash
# needs the development files of OpenCASCADE 7.9 or newer, and RapidJSON
pip install --no-binary cascadio cascadio
```

No patches to OpenCASCADE are needed. If CMake does not find it, point `CMAKE_PREFIX_PATH` at the install.

#### Slower checks

These run in Docker and are kept out of CI on purpose:

```bash
make wheel         # build and test one Linux wheel exactly like CI
make system-occt   # build the sdist against OpenCASCADE 7.9.0 and test
make occt-master   # build and test against the newest OpenCASCADE commit
```

### Future Work

Pull requests welcome!

- Add passable parameters for options included in the RWGLTF writer.
- Investigate using OpenCASCADE "Advanced Data Exchange" for Parasolid `.x_b`/`.x_t` and JT `.jt` support.
