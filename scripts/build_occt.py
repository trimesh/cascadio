#!/usr/bin/env python3
"""
Build the vendored OpenCASCADE (upstream/OCCT) as static libraries.

Only the toolkits cascadio needs are built, and they are installed to
`occt_install/` (override with CASCADIO_OCCT_PREFIX) where CMakeLists.txt
finds them. The same command is used locally and by cibuildwheel:

    python scripts/build_occt.py

The build is skipped when the install is already up to date. Extra arguments
are passed to CMake, e.g. `-DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG"`.
See cmake/toolchain.cmake for the compiler and optimization settings.
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "upstream" / "OCCT"
RAPIDJSON = ROOT / "upstream" / "rapidjson" / "include"
TOOLCHAIN = ROOT / "cmake" / "toolchain.cmake"
PREFIX = Path(os.environ.get("CASCADIO_OCCT_PREFIX", ROOT / "occt_install"))
BUILD = Path(os.environ.get("CASCADIO_OCCT_BUILD", ROOT / "build" / "occt"))

# The toolkits cascadio links. OCCT adds the ones these depend on.
TOOLKITS = ["TKDESTEP", "TKDEIGES", "TKDEGLTF", "TKXCAF", "TKMesh"]

# Whole modules are switched off so nothing else is built.
MODULES = [
    "FoundationClasses",
    "ModelingData",
    "ModelingAlgorithms",
    "Visualization",
    "ApplicationFramework",
    "DataExchange",
    "Draw",
]

# Optional third-party integrations, none of which cascadio uses.
UNUSED = ["OPENGL", "GLES2", "TK", "FREETYPE", "FREEIMAGE", "VTK", "XLIB",
          "OPENVR", "FFMPEG", "TBB", "DRACO", "EIGEN"]  # fmt: skip


def stamp() -> str:
    """A fingerprint of everything that should trigger a rebuild."""
    digest = hashlib.sha256()
    for path in (Path(__file__), TOOLCHAIN, SOURCE / "adm/cmake/version.cmake"):
        digest.update(path.read_bytes())
    for name in ("CASCADIO_LTO", "CASCADIO_CLANG"):
        digest.update(os.environ.get(name, "").encode())
    commit = subprocess.run(
        ["git", "-C", str(SOURCE), "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
    )
    digest.update(commit.stdout.encode())
    return digest.hexdigest()


def run(*command: object) -> None:
    command = [str(c) for c in command]
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument(
        "--clean", action="store_true", help="delete the build and install first"
    )
    # anything unrecognized is passed on to the CMake configure step
    args, cmake_args = parser.parse_known_args()

    if not (SOURCE / "CMakeLists.txt").exists():
        print(f"{SOURCE} is empty: run `git submodule update --init`")
        return 1

    stamp_file = PREFIX / "cascadio-stamp.txt"
    if args.clean:
        shutil.rmtree(BUILD, ignore_errors=True)
        shutil.rmtree(PREFIX, ignore_errors=True)
    elif stamp_file.exists() and stamp_file.read_text() == stamp():
        print(f"OpenCASCADE in {PREFIX} is up to date")
        return 0

    run(
        "cmake",
        "-S", SOURCE,
        "-B", BUILD,
        "-G", "Ninja",
        f"--toolchain={TOOLCHAIN}",
        "-DCMAKE_BUILD_TYPE=Release",
        # lets cmake/toolchain.cmake pick the Windows C runtime; OpenCASCADE
        # asks for a CMake old enough that this is otherwise ignored
        "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
        f"-DINSTALL_DIR={PREFIX}",
        "-DINSTALL_DIR_LAYOUT=Unix",
        "-DBUILD_LIBRARY_TYPE=Static",
        # Keep OpenCASCADE's own null and range checks in the release
        # build. Off, a damaged file reads out of bounds; on, it throws
        # Standard_Failure, which the worker reports as an error.
        "-DBUILD_RELEASE_DISABLE_EXCEPTIONS=OFF",
        "-DBUILD_ADDITIONAL_TOOLKITS=" + ";".join(TOOLKITS),
        *(f"-DBUILD_MODULE_{name}=OFF" for name in MODULES),
        *(f"-DUSE_{name}=OFF" for name in UNUSED),
        "-DUSE_RAPIDJSON=ON",
        f"-D3RDPARTY_RAPIDJSON_INCLUDE_DIR={RAPIDJSON}",
        "-DBUILD_DOC_Overview=OFF",
        *cmake_args,
    )  # fmt: skip
    run("cmake", "--build", BUILD, "--target", "install")

    stamp_file.write_text(stamp())
    print(f"OpenCASCADE installed to {PREFIX}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
