#!/usr/bin/env python3
"""
Checks run on every wheel in CI: `python scripts/audit_wheel.py WHEEL`

1. The wheel is small and holds exactly one native library. OpenCASCADE is
   linked statically, so anything else means the build regressed.
2. Stable-ABI wheels really only use the stable ABI (abi3audit).
"""

import subprocess
import sys
import zipfile
from pathlib import Path

MAX_MB = 15
NATIVE = (".so", ".pyd", ".dylib", ".dll")
# the Windows repair step may put the C++ runtime next to the extension
RUNTIME = ("msvcp", "vcruntime", "concrt", "vcomp")


def main(wheel: Path) -> None:
    size_mb = wheel.stat().st_size / 1e6
    native = [
        name
        for name in zipfile.ZipFile(wheel).namelist()
        if name.endswith(NATIVE) or ".so." in name
    ]
    ours = [n for n in native if not Path(n).name.lower().startswith(RUNTIME)]
    print(f"{wheel.name}: {size_mb:.1f} MB, native libraries: {native}")
    assert size_mb < MAX_MB, f"wheel is {size_mb:.1f} MB, limit is {MAX_MB} MB"
    assert len(ours) == 1, f"expected one native library, found {ours}"

    if "abi3" in wheel.name:
        subprocess.run(
            ["abi3audit", "--strict", "--report", "--verbose", str(wheel)],
            check=True,
        )


if __name__ == "__main__":
    main(Path(sys.argv[1]))
