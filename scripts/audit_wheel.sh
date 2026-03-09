#!/bin/bash -eo pipefail
set -x

# skip < Python 3.11 (nanobind provides stable ABI support for Python3.12 and above)
PY_MINOR=$(python -c "import sys; print(sys.version_info.minor)")
if [ "$PY_MINOR" -lt 12 ]; then
  echo "Skipping abi3audit as Python $PY_MINOR < 3.12"
  exit 0
fi

# Skip free threaded python (there currently is no stable ABI for free-threaded
# Python as of 9 Mar 2026)
ABIFLAGS=$(python -c "import sys; print(getattr(sys, 'abiflags', ''))")
if [[ "$ABIFLAGS" == *t* ]]; then
    echo "Skipping abi3audit for free-threaded Python"
    exit 0
fi

abi3audit --strict --report --verbose "$1"
