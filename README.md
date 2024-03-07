# cascadio

A Python library which uses [OpenCASCADE](https://github.com/Open-Cascade-SAS/OCCT) to convert STEP files to a GLB file which can quickly be loaded by [trimesh](https://github.com/mikedh/trimesh) and other libraries.

The primary effort here is build and packaging using the wonderful work done recently on [scikit-build-core](https://github.com/scikit-build/scikit-build-core) and [cibuildwheel](https://github.com/pypa/cibuildwheel). The goal is to produce wheels that don't require users to build OpenCASCADE themselves.

This is *not* intended to be a full binding of OpenCASCADE like [OCP](https://github.com/CadQuery/OCP) or [PythonOCC](https://github.com/tpaviot/pythonocc-core). Rather it is intended to be an easy minimal way to load boundary representation files into a triangulated scene in Python. There are a few options for loading STEP geometry in the open-source ecosystem: GMSH, FreeCAD, etc. However nearly all of them use OpenCASCADE under the hood as it is pretty much the only open-source BREP kernel.



### Install

The primary goal of this project is building wheels so vanilla `pip` can be used:

```
pip install cascadio
```

Currently this is building for non-MUSL flavors of `manylinux`. You can check [PyPi](https://pypi.org/project/cascadio/#files) for current platforms.


### Motivation

A lot of analysis can be done on triangulated surface meshes that doesn't need the analytical surfaces from a STEP or BREP file. 

### Contributing

Developed on Linux which should build wheels locally with docker:
```
# this doesn't cache the OCCT build unfortunately.
# It would be nice if it did! You could do it by building OCCT
# in the manylinux images and then passing the new tag to CIBW
CIBW_BUILD="cp312-manylinux_x86_64" cibuildwheel --platform linux
```

Or, if you want to develop that will *only* work in your local environment for development:
```
# just run the `before-all` from pyproject.toml which is approximatly:
cd upstream/OCCT
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DUSE_RAPIDJSON:BOOL="ON" \
      -D3RDPARTY_RAPIDJSON_INCLUDE_DIR="../rapidjson/include" .
ninja
mv lin64/gcc/lib .
```
Then `pip install .` will build and install locally. Make sure to point `LD_LIBRARY_PATH=upstream/OCCT/lin64/gcc/lib` or wherever you put the libraries.


### Future Work

Pull requests welcome! 

- Add passable parameters for options included in the RWGLTF writer.
- use in-memory data for input and output, i.e. `stepReader.ReadStream()` instead of a file name. Ideally the Python function signature would be:
  - `convert_to_glb(data: bytes, file_type: str, **parameters) -> bytes`
  - Currently using file names because it's easier. 
- Support IGES 
  - Investigate using OpenCASCADE "Advanced Data Exchange" for Parasolid `.x_b`/`.x_t` and JT `.jt` support.
- Build wheels for Windows and MacOS.

