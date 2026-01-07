#!/usr/bin/env python3
"""Test that cascadio works with a fully read-only filesystem.

This script is run by cibuildwheel on Linux with / remounted read-only.
It verifies:
1. Filesystem is actually read-only (sanity check on multiple locations)
2. cascadio.load() works despite this (via memfd)
"""

import os
import sys

def main():
    # Verify multiple locations are read-only
    test_paths = [
        "/tmp/test_write",
        "/var/tmp/test_write",
        os.path.join(os.getcwd(), "test_write"),
    ]

    all_readonly = True
    for path in test_paths:
        try:
            with open(path, "w") as f:
                f.write("test")
            os.unlink(path)  # Clean up if write succeeded
            print(f"ERROR: {os.path.dirname(path)} is writeable")
            all_readonly = False
        except OSError as e:
            print(f"Confirmed read-only: {path} ({e.strerror})")

    if not all_readonly:
        print("ERROR: Filesystem is not fully read-only")
        return 1

    # Now test cascadio
    import cascadio

    # Find test models
    script_dir = os.path.dirname(os.path.abspath(__file__))
    models_dir = os.path.join(script_dir, "..", "tests", "models")

    # Test STEP
    step_file = os.path.join(models_dir, "featuretype.STEP")
    with open(step_file, "rb") as f:
        data = f.read()
    result = cascadio.load(data, "step")
    assert len(result) > 0, "STEP conversion failed"
    print(f"STEP conversion OK: {len(result)} bytes")

    # Test IGES
    iges_file = os.path.join(models_dir, "microstrip.igs")
    with open(iges_file, "rb") as f:
        data = f.read()
    result = cascadio.load(data, "iges")
    assert len(result) > 0, "IGES conversion failed"
    print(f"IGES conversion OK: {len(result)} bytes")

    print("All read-only filesystem tests passed!")
    return 0

if __name__ == "__main__":
    sys.exit(main())
