#!/usr/bin/env python3
"""
Build OpenCASCADE for cascadio wheel builds.
Skips build if OCCT libraries already exist.

For local development:
- Builds OCCT to {project}/occt_cache/ to avoid polluting upstream/
- Set CASCADIO_OCCT_LIB env var to point pip install to the cached libs
- Headers in upstream/OCCT are still used (they don't have absolute paths)
- Applies patches from patches/ directory automatically
- Supports incremental builds (use --clean for full rebuild)

For cibuildwheel:
- Detects CIBUILDWHEEL=1 env var
- Builds OCCT in-tree at upstream/OCCT/lin64/gcc/lib etc.
- Cache persists across cibuildwheel runs
"""

import argparse
import glob
import os
import platform
import subprocess
import sys

# Check if running inside cibuildwheel
IN_CIBUILDWHEEL = os.environ.get("CIBUILDWHEEL") == "1"

# Get project root (parent of scripts/)
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)

# Local cache directory for development builds (in project root)
LOCAL_CACHE_DIR = os.path.join(PROJECT_ROOT, "occt_cache")

# Platform-specific library paths
PLATFORM_LIB_SUBDIR = {
    "Linux": "lin64/gcc/lib",
    "Darwin": "mac64/clang/lib",
    "Windows": "win64/vc14/lib",
}

PLATFORM_LIB_NAME = {
    "Linux": "libTKernel.so",
    "Darwin": "libTKernel.dylib",
    "Windows": "TKernel.dll",
}

CMAKE_ARGS = [
    "-G",
    "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DUSE_RAPIDJSON:BOOL=ON",
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
]


def run(cmd, **kwargs):
    """Run a command, exit on failure."""
    print(f"+ {' '.join(cmd)}", flush=True)
    result = subprocess.run(cmd, **kwargs)
    if result.returncode != 0:
        sys.exit(result.returncode)


def build_patch(occt_src, patches_dir):
    """Regenerate patch files from current diff in upstream/OCCT."""
    os.makedirs(patches_dir, exist_ok=True)
    patch_file = os.path.join(patches_dir, "tm_brep_faces.patch")
    with open(patch_file, "w") as f:
        subprocess.run(["git", "diff", "HEAD"], cwd=occt_src, stdout=f, check=True)
    print(f"Generated: {patch_file}")


def apply_patches(occt_src, patches_dir):
    """Apply patches from patches/ directory if they haven't been applied.
    
    Returns:
        bool: True if any patches were applied (source modified), False otherwise
    """
    if not os.path.isdir(patches_dir):
        return False

    patches = sorted(glob.glob(os.path.join(patches_dir, "*.patch")))
    if not patches:
        return False

    print(f"Found {len(patches)} patch(es) to check...")

    patches_applied = False
    for patch_path in patches:
        patch_name = os.path.basename(patch_path)
        abs_patch_path = os.path.abspath(patch_path)

        # Check if patch is already applied by doing a dry-run reverse
        result = subprocess.run(
            [
                "patch",
                "-p1",
                "--reverse",
                "--dry-run",
                "--force",
                "--silent",
                "-i",
                abs_patch_path,
            ],
            cwd=occt_src,
            capture_output=True,
        )

        if result.returncode == 0:
            print(f"  {patch_name}: already applied")
            continue

        # Apply the patch (patch handles line ending conversion automatically)
        print(f"  {patch_name}: applying...")
        subprocess.run(
            ["patch", "-p1", "-i", abs_patch_path],
            cwd=occt_src,
            check=True,
        )
        patches_applied = True

    return patches_applied


def get_lib_marker(base_path, system, in_tree=False):
    """Get the path to the library marker file.

    Args:
        base_path: Base directory for libraries
        system: Platform name (Linux, Darwin, Windows)
        in_tree: If True, use platform-specific subdirs (for cibuildwheel).
                 If False, use flat 'lib' dir (for ninja install).
    """
    lib_name = PLATFORM_LIB_NAME.get(system, "")
    if in_tree:
        subdir = PLATFORM_LIB_SUBDIR.get(system, "")
        return os.path.join(base_path, subdir, lib_name)
    else:
        return os.path.join(base_path, "lib", lib_name)


def write_env_file(lib_dir):
    """Write environment variables to a sourceable file."""
    env_file = os.path.join(LOCAL_CACHE_DIR, "env.sh")
    with open(env_file, "w") as f:
        f.write(f"export CASCADIO_OCCT_LIB={lib_dir}\n")
        f.write(f"export LD_LIBRARY_PATH={lib_dir}:$LD_LIBRARY_PATH\n")
    print(f"Wrote {env_file}")
    print(f"Run: source {env_file}")


def main():
    parser = argparse.ArgumentParser(description="Build OpenCASCADE for cascadio")
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean build (remove CMake cache and rebuild from scratch)",
    )
    parser.add_argument(
        "--force", action="store_true", help="Force rebuild even if libraries exist"
    )
    parser.add_argument(
        "--build-patch",
        action="store_true",
        help="Regenerate patch files from diff in upstream/OCCT",
    )
    args = parser.parse_args()

    system = platform.system()

    # OCCT source is always in upstream/OCCT relative to project root
    occt_src = os.path.join(PROJECT_ROOT, "upstream", "OCCT")
    patches_dir = os.path.join(PROJECT_ROOT, "patches")

    # Handle --build-patch separately
    if args.build_patch:
        return build_patch(occt_src, patches_dir)

    if IN_CIBUILDWHEEL:
        # In CI: build in-tree
        install_prefix = occt_src
        print("Running in cibuildwheel, building OCCT in-tree")
    else:
        # Local dev: build to cache directory
        install_prefix = LOCAL_CACHE_DIR
        print(f"Local development build, using cache at {LOCAL_CACHE_DIR}")

    print(f"Building OCCT for {system}...")

    if not os.path.isdir(occt_src):
        print(f"Error: {occt_src} not found")
        return 1

    # Create install prefix for local builds
    if not IN_CIBUILDWHEEL:
        os.makedirs(install_prefix, exist_ok=True)

    os.chdir(occt_src)

    # Apply patches (CMake will detect header changes and ninja will rebuild)
    apply_patches(occt_src, patches_dir)

    # Clean build if requested (removes CMake cache but keeps source changes)
    if args.clean:
        print("Cleaning CMake cache...")
        cmake_cache = os.path.join(occt_src, "CMakeCache.txt")
        cmake_files = os.path.join(occt_src, "CMakeFiles")
        build_ninja = os.path.join(occt_src, "build.ninja")
        if os.path.exists(cmake_cache):
            os.remove(cmake_cache)
        if os.path.exists(build_ninja):
            os.remove(build_ninja)
        if os.path.isdir(cmake_files):
            import shutil

            shutil.rmtree(cmake_files)

    # Build cmake args
    cmake_args = CMAKE_ARGS.copy()

    # RapidJSON path
    rapidjson_path = os.path.join(PROJECT_ROOT, "upstream", "rapidjson", "include")
    cmake_args.append(f"-D3RDPARTY_RAPIDJSON_INCLUDE_DIR={rapidjson_path}")

    if not IN_CIBUILDWHEEL:
        # For local builds, set install prefix to cache dir
        cmake_args.append(f"-DCMAKE_INSTALL_PREFIX={install_prefix}")
        cmake_args.append(f"-DINSTALL_DIR={install_prefix}")

    cmake_args.append(".")

    # Always run cmake configure to pick up any source changes (patches, etc.)
    # CMake is fast when nothing changed, and ninja handles incremental builds
    print("Configuring with CMake...")
    run(["cmake"] + cmake_args)

    # Patch build.ninja to remove GL/EGL linking on Linux
    if system == "Linux" and os.path.exists("build.ninja"):
        with open("build.ninja", "r") as f:
            content = f.read()
        content = content.replace(" -lGL ", " ").replace(" -lEGL ", " ")
        with open("build.ninja", "w") as f:
            f.write(content)
        print("Patched build.ninja to remove GL/EGL")

    # Build (ninja handles incremental builds automatically)
    print("Building with ninja...")
    run(["ninja"])

    # For local builds, also install to get libs in the cache dir
    if not IN_CIBUILDWHEEL:
        run(["ninja", "install"])
        lib_dir = os.path.join(install_prefix, "lib")
        print("\nOCCT built successfully!")
        write_env_file(lib_dir)
    else:
        print("OCCT build complete!")

    return 0


if __name__ == "__main__":
    sys.exit(main())
