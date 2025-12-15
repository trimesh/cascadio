#!/usr/bin/env python3
"""
Build OpenCASCADE for cascadio wheel builds.
Skips build if OCCT libraries already exist.

For cibuildwheel caching:
- The host's upstream/OCCT directory is mounted into the container
- OCCT headers use relative paths, so builds are portable
- Cache persists across cibuildwheel runs if built on host first
"""

import os
import platform
import subprocess
import sys

# OCCT library paths to check for cache hit
CACHE_MARKERS = {
    "Linux": "upstream/OCCT/lin64/gcc/lib/libTKernel.so",
    "Darwin": "upstream/OCCT/mac64/clang/lib/libTKernel.dylib",
    "Windows": "upstream/OCCT/win64/vc14/bin/TKernel.dll",
}

CMAKE_ARGS = [
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DUSE_RAPIDJSON:BOOL=ON",
    "-D3RDPARTY_RAPIDJSON_INCLUDE_DIR=../rapidjson/include",
    "-DUSE_OPENGL:BOOL=OFF",
    "-DUSE_TK:BOOL=OFF",
    "-DUSE_FREETYPE:BOOL=OFF",
    "-DUSE_VTK:BOOL=OFF",
    "-DUSE_XLIB:BOOL=OFF",
    "-DUSE_GLES2:BOOL=OFF",
    "-DUSE_OPENVR:BOOL=OFF",
    "-DBUILD_Inspector:BOOL=OFF",
    "-DUSE_FREEIMAGE:BOOL=OFF",
    "-DBUILD_SAMPLES_QT:BOOL=OFF",
    "-DBUILD_MODULE_Draw:BOOL=OFF",
    "-DBUILD_MODULE_Visualization:BOOL=OFF",
    "-DBUILD_MODULE_ApplicationFramework:BOOL=OFF",
    ".",
]


def run(cmd, **kwargs):
    """Run a command, exit on failure."""
    print(f"+ {' '.join(cmd)}", flush=True)
    result = subprocess.run(cmd, **kwargs)
    if result.returncode != 0:
        sys.exit(result.returncode)


def is_cache_valid(marker):
    """Check if cached build exists."""
    return os.path.exists(marker)


def main():
    system = platform.system()
    marker = CACHE_MARKERS.get(system)
    
    if marker and is_cache_valid(marker):
        print(f"Using cached OCCT ({marker} exists)")
        return 0

    print(f"Building OCCT for {system}...")

    # Change to OCCT directory
    occt_dir = "upstream/OCCT"
    if not os.path.isdir(occt_dir):
        print(f"Error: {occt_dir} not found")
        return 1
    os.chdir(occt_dir)

    # Clean build artifacts
    run(["git", "clean", "-xdf"])

    # Configure with cmake
    run(["cmake"] + CMAKE_ARGS)

    # Patch build.ninja to remove GL/EGL linking on Linux
    if system == "Linux" and os.path.exists("build.ninja"):
        with open("build.ninja", "r") as f:
            content = f.read()
        content = content.replace(" -lGL ", " ").replace(" -lEGL ", " ")
        with open("build.ninja", "w") as f:
            f.write(content)
        print("Patched build.ninja to remove GL/EGL")

    # Build
    run(["ninja"])

    print("OCCT build complete!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
