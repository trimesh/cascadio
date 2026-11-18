#!/bin/bash
# Build the source distribution against a system OpenCASCADE and run the
# tests, the way a Linux or BSD packager would. Run by `make system-occt`
# inside a micromamba container with the repository mounted at /src.
#
#   test_system_occt.sh OCCT_VERSION
set -euo pipefail

# conda-forge carries old OpenCASCADE releases, which distributions do not
micromamba create -y -q -n occt -c conda-forge \
  python=3.12 "occt=$1" rapidjson cxx-compiler binutils cmake ninja pip

# `micromamba run` activates the environment, which puts the compiler on PATH
micromamba run -n occt env \
  CMAKE_PREFIX_PATH=/opt/conda/envs/occt CASCADIO_CLANG=OFF \
  pip install "$(ls /src/dist/*.tar.gz | tail -1)[tests]"

# run from another directory so the installed package is what gets tested
cd /tmp
micromamba run -n occt pytest /src/tests -p no:cacheprovider
