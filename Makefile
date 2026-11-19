# The front door for working on cascadio. Needs `uv`; the Docker targets
# also need Docker. Run `make` to see this list.
#
# Everyday, on your own machine. Each step builds on the one before:
#   make occt          build static OpenCASCADE into occt_install/ (slow, once)
#   make install       build cascadio against it into .venv/, with test tools
#   make test          run the tests
#   make wheel         build a wheel for this machine into dist/
#   make audit WHEEL=x check a wheel's size, contents and stable ABI use
#
# Release checks, in Docker so they do not depend on your machine:
#   make manylinux     build and test a Linux wheel exactly like CI does
#   make system-occt   build the sdist against a system OpenCASCADE and test
#   make occt-master   build and test against the newest OpenCASCADE commit
#
# For maintainers, before a release (needs clang; takes a few minutes):
#   make bughunt       lint, then the tests and a fuzzer against a worker
#                      built with sanitizers; see the target for details
#
# Housekeeping:
#   make sdist         build the source distribution into dist/
#   make schema        regenerate the README and dataclasses of the extension
#   make lock          update uv.lock to the newest build tools
#   make clean         delete everything the targets above created
#
# CI uses the same targets: cibuildwheel calls `make occt` before building
# and `make audit` on each wheel (see [tool.cibuildwheel] in pyproject.toml).
#
# Options, for example `make occt CPUS=4`:
#   CPUS    cores a build may use (default: all of them)
#   PYTHON  CPython tag for `make manylinux` (default: cp312)
#   OCCT    OpenCASCADE version for `make system-occt` (default: 7.9.0,
#           the oldest one cascadio supports)
#   SECONDS how long the fuzzer in `make bughunt` runs (default: 120)

CPUS ?= $(shell getconf _NPROCESSORS_ONLN)
PYTHON ?= cp312
OCCT ?= 7.9.0
SECONDS ?= 120

MANYLINUX := quay.io/pypa/manylinux_2_28_x86_64
MICROMAMBA := mambaorg/micromamba:latest

# Every tool comes from uv.lock, checked against its recorded hash. That
# includes the ones uv fetches to build cascadio itself, through the
# constraints file written from the lock below.
TOOLS := uv run --frozen --only-group build
export UV_BUILD_CONSTRAINT := build-constraints.txt
export CMAKE_BUILD_PARALLEL_LEVEL := $(CPUS)
# compiler warnings in our own code are errors (see CMakeLists.txt)
export CASCADIO_STRICT := ON

.PHONY: help occt install test wheel audit bughunt manylinux system-occt occt-master sdist schema lock clean

help:
	@sed -n '/^#/!q;s/^# \{0,1\}//p' $(MAKEFILE_LIST)

upstream/OCCT/CMakeLists.txt:
	git submodule update --init --depth 1

# build_occt.py does nothing when occt_install/ is already up to date
occt: upstream/OCCT/CMakeLists.txt build-constraints.txt
	$(TOOLS) python scripts/build_occt.py

build-constraints.txt: uv.lock
	UV_BUILD_CONSTRAINT= uv export --quiet --frozen --only-group build --no-emit-project -o $@

# always rebuilds cascadio, so C++ edits are never silently left out
install: occt
	uv sync --frozen --extra tests --reinstall-package cascadio

test: install
	uv run --frozen --extra tests pytest tests

wheel: occt
	uv build --wheel --out-dir dist

audit:
	$(TOOLS) python scripts/audit_wheel.py $(WHEEL)

sdist: build-constraints.txt
	uv build --sdist --out-dir dist

# Everything that looks for bugs nobody has reported yet, in one go:
#
#   1. ruff: Python style and likely mistakes.
#   2. A second copy of the worker is compiled with the address and
#      undefined-behavior sanitizers, so a memory error in our code stops
#      it with a report instead of going unnoticed. (Every build already
#      treats compiler warnings in our code as errors.)
#   3. The test suite runs against that worker.
#   4. scripts/fuzz.py converts randomly damaged copies of the test models
#      with that worker for SECONDS seconds, and saves every input that
#      crashes, hangs or runs out of memory to fuzz-findings/.
#   5. The same again with the release worker, whose system call filter
#      the sanitizers leave out: a call the filter does not allow is
#      saved as a "sandbox" finding.
#
# Steps 4 and 5 do not fail the target: OpenCASCADE has known crashes on
# damaged files, which cascadio survives by design. Read the summaries, and
# look at anything whose stack is in our code rather than OpenCASCADE's.
SANITIZED := build/sanitize/cascadio-worker
SANITIZER_ENV := CASCADIO_WORKER=$(CURDIR)/$(SANITIZED) \
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1

bughunt: install
	uvx ruff check .
	uvx ruff format --check .
	$(TOOLS) cmake -S . -B build/sanitize -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
	  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer" \
	  "-Dnanobind_DIR=$$($(TOOLS) python -m nanobind --cmake_dir)"
	$(TOOLS) cmake --build build/sanitize --target cascadio-worker
	$(SANITIZER_ENV) uv run --frozen --extra tests pytest tests
	$(SANITIZER_ENV) uv run --frozen --extra tests python scripts/fuzz.py \
	  --seconds $(SECONDS) --jobs $(CPUS)
	uv run --frozen --extra tests python scripts/fuzz.py --seconds $(SECONDS) --jobs $(CPUS)

# OpenCASCADE has to be built again inside the container, for its older
# system libraries. It is installed under build/ on the host (which the
# container sees at /host) so that only the first run is slow.
manylinux: upstream/OCCT/CMakeLists.txt
	CIBW_BUILD="$(PYTHON)-manylinux_x86_64" \
	CIBW_CONTAINER_ENGINE="docker; create_args: --privileged --cpus=$(CPUS)" \
	CIBW_ENVIRONMENT_LINUX="CASCADIO_OCCT_PREFIX=/host$(CURDIR)/build/occt_manylinux CASCADIO_OCCT_BUILD=/tmp/occt_build UV_CONSTRAINT=build-constraints.txt CASCADIO_STRICT=ON CMAKE_BUILD_PARALLEL_LEVEL=$(CPUS)" \
	  uvx cibuildwheel --platform linux

# What a Linux or BSD packager does: no vendored code, the OpenCASCADE,
# RapidJSON and zstd come from the system (here from conda-forge, which
# still carries old OpenCASCADE versions).
system-occt: sdist
	docker run --rm --cpus $(CPUS) -v "$(CURDIR)":/src:ro $(MICROMAMBA) \
	  bash /src/scripts/test_system_occt.sh $(OCCT)

# Early warning before bumping the upstream/OCCT submodule.
# Works on a copy, so your checkout and submodule are left alone.
occt-master: upstream/OCCT/CMakeLists.txt
	docker run --rm --cpus $(CPUS) -v "$(CURDIR)":/src:ro $(MANYLINUX) \
	  bash /src/scripts/test_occt_master.sh

# everything that repeats the TM_brep_faces schema is generated from it
schema:
	uv run --no-project python scripts/gen_schema.py

lock:
	uv lock --upgrade

# build/occt_manylinux is written by a container as root, hence Docker
clean:
	-docker run --rm -v "$(CURDIR)":/src $(MANYLINUX) rm -rf /src/build/occt_manylinux
	rm -rf build dist wheelhouse occt_install .venv build-constraints.txt fuzz-findings
