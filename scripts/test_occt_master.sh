#!/bin/bash
# Build cascadio against the newest OpenCASCADE commit and run the tests:
# an early warning before bumping the upstream/OCCT submodule. Run by
# `make occt-master` inside the manylinux container CI uses, with the
# repository mounted read-only at /src.
set -euo pipefail

yum install -y -q clang lld llvm git
export PATH=/opt/python/cp312-cp312/bin:$PATH
pip install -q ninja cmake

# work on a copy so the checkout and its submodule are left alone
mkdir /work
tar -C /src --exclude=./occt_install --exclude=./build -cf - . | tar -C /work -xf -
cd /work
git config --global --add safe.directory "*"
git -C upstream/OCCT fetch -q --depth 1 origin master
git -C upstream/OCCT checkout -q FETCH_HEAD
git -C upstream/OCCT log -1 --oneline

python scripts/build_occt.py
pip install ".[tests]"

# run from another directory so the installed package is what gets tested
cd /tmp
pytest /work/tests -p no:cacheprovider
