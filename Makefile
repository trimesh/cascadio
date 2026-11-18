# Checks that are too slow or too niche for CI. They run in Docker so the
# result does not depend on what is installed on your machine.
#
#   make wheel          build and test one Linux wheel exactly like CI does
#   make sdist          build the source distribution into dist/
#   make system-occt    build the sdist against a system OpenCASCADE and test
#   make occt-master    build and test against the newest OpenCASCADE commit

# CPython tag for `make wheel`, and OpenCASCADE version for `make system-occt`.
# 7.9.0 is the oldest OpenCASCADE cascadio supports.
PYTHON ?= cp312
OCCT ?= 7.9.0

# Cores the Docker builds may use.
CPUS ?= 4

MANYLINUX := quay.io/pypa/manylinux_2_28_x86_64
MICROMAMBA := mambaorg/micromamba:latest

.PHONY: wheel sdist system-occt occt-master

wheel:
	CIBW_BUILD="$(PYTHON)-manylinux_x86_64" uvx cibuildwheel --platform linux

sdist:
	uvx --from build pyproject-build --sdist

# What a Linux or BSD packager does: no vendored code, the OpenCASCADE and
# RapidJSON come from the system (here from conda-forge, which has old versions).
system-occt: sdist
	docker run --rm --cpus $(CPUS) -v "$(CURDIR)":/src:ro $(MICROMAMBA) \
	  bash /src/scripts/test_system_occt.sh $(OCCT)

# Early warning before bumping the upstream/OCCT submodule.
# Works on a copy, so your checkout and submodule are left alone.
occt-master:
	docker run --rm --cpus $(CPUS) -v "$(CURDIR)":/src:ro $(MANYLINUX) \
	  bash /src/scripts/test_occt_master.sh
