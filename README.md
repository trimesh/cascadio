# cascadio

A Python library which uses [OpenCASCADE](https://github.com/Open-Cascade-SAS/OCCT) to convert STEP files to a GLB file which can quickly be loaded by [trimesh](https://github.com/mikedh/trimesh) and other libraries.

The primary effort here is build and packaging using the wonderful work done recently on [scikit-build-core](https://github.com/scikit-build/scikit-build-core) and [cibuildwheel](https://github.com/pypa/cibuildwheel). The goal is to produce wheels that don't require users to build OpenCASCADE themselves.

This is *not* intended to be a full binding of OpenCASCADE like [OCP](https://github.com/CadQuery/OCP) or [PythonOCC](https://github.com/tpaviot/pythonocc-core). Rather it is intended to be an easy minimal way to load boundary representation files into a triangulated scene in Python. There are a few options for loading STEP geometry in the open-source ecosystem: GMSH, FreeCAD, etc. However nearly all of them use OpenCASCADE under the hood as it is pretty much the only open-source BREP kernel.



### Install

The primary goal of this project is building wheels so vanilla `pip` can be used:

```
pip install cascadio
```

Version 0.2 is out as a release candidate. It is much smaller and safer but changes how errors are reported (see below), so `pip` only picks it when asked:

```
pip install --pre cascadio
```

Wheels are built for Python 3.12 and newer on Linux (x86_64 and ARM, not musl), Windows x64, and macOS (Intel and ARM). Each wheel is about 9 MB and has no dependencies. You can check [PyPI](https://pypi.org/project/cascadio/#files) for current platforms.

### Usage

```python
import cascadio

with open("model.step", "rb") as f:
    glb = cascadio.load(f.read())  # bytes in, bytes out

# Zstandard-compressed files are detected and unpacked in memory
glb = cascadio.load(open("model.step.zst", "rb").read())

# IGES, with a finer mesh
glb = cascadio.load(data, file_type="iges", tol_linear=0.001)

# load the result with trimesh
import io, trimesh
scene = trimesh.load(io.BytesIO(glb), file_type="glb")
```

`load` never reads or writes files. Useful options (see the docstring for all of them):

- `include_brep=True` adds, for every triangle, which CAD face it came from and the exact plane, cylinder, cone, sphere or torus of that face. This is the [`TM_brep_faces`](extensions/TM_brep_faces/README.md) glTF extension.
- `include_materials=True` adds material names, densities and colors.
- `y_up=True` rotates the model into glTF's Y-up frame. CAD files are almost always Z-up, and by default the coordinates are left as they are. Viewers that follow the glTF convention, such as Blender, rotate on import, so use this if the model shows up lying on its side.

#### Limits and errors

OpenCASCADE can hang, run out of memory or crash on unusual files. So every conversion is done by a small separate program, `cascadio-worker`, which ships in the wheel next to the Python module. The module starts it, hands it the bytes, and stops it if it breaks a limit. This costs a few milliseconds per call.

```python
cascadio.load(data, timeout=30.0, memory_limit=8 * 2**30)  # the defaults
```

| What happened | Exception |
|---|---|
| The data is not a valid STEP or IGES file | `ValueError` |
| It took longer than `timeout` seconds | `TimeoutError` |
| It used more than `memory_limit` bytes | `MemoryError` |
| OpenCASCADE failed or crashed | `RuntimeError` |

Either limit can be `None` to switch it off.

`step_to_glb` and `step_to_obj` work on file paths. They are deprecated and will be removed in the first release after January 2028.

### Motivation

A lot of analysis can be done on triangulated surface meshes that doesn't need the analytical surfaces from a STEP or BREP file. 

### Contributing

Everything goes through `make`, which needs [uv](https://docs.astral.sh/uv/). Run `make` on its own to list the targets.

```bash
make occt      # build static OpenCASCADE into occt_install/ (several minutes, once)
make test      # build cascadio against it and run the tests
make wheel     # build a wheel for this machine into dist/
```

`make occt` fetches the OpenCASCADE, RapidJSON and zstd submodules if needed and uses clang and lld when they are installed. Add `CPUS=4` to any target to limit how many cores it uses.

OpenCASCADE is built as static libraries with only the pieces cascadio uses, then linked into the `cascadio-worker` program. The linker removes every function that is not reachable, which is what keeps the wheels small. The Python module itself is a small nanobind extension that contains no OpenCASCADE: it starts the worker and enforces the limits. `src/request.hpp` is the whole contract between the two.

#### Building against a system OpenCASCADE

The source distribution on PyPI contains no vendored code. It builds against the OpenCASCADE your system provides, which is what Linux and BSD packagers want:

```bash
# needs the development files of OpenCASCADE 7.9 or newer, RapidJSON and zstd
pip install --no-binary cascadio cascadio
```

No patches to OpenCASCADE are needed. If CMake does not find it, point `CMAKE_PREFIX_PATH` at the install.

#### Build tools are locked

`uv.lock` records the exact version and checksum of every tool that takes part in building a wheel (the `build` group in `pyproject.toml`). Local and CI builds refuse anything that does not match. `make lock` takes updates.

#### Release checks

These run in Docker and are kept out of CI on purpose:

```bash
make manylinux     # build and test a Linux wheel exactly like CI
make system-occt   # build the sdist against OpenCASCADE 7.9.0 and test
make occt-master   # build and test against the newest OpenCASCADE commit
```

### Future Work

Pull requests welcome!

- Add passable parameters for options included in the RWGLTF writer.
- Investigate using OpenCASCADE "Advanced Data Exchange" for Parasolid `.x_b`/`.x_t` and JT `.jt` support.
