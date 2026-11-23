"""Shared by the test files."""

import json
import struct
import sys
from pathlib import Path

import pytest

import cascadio

MODELS_DIR = Path(__file__).parent / "models"


def glb_json(glb: bytes) -> dict:
    """The JSON chunk of a GLB."""
    assert glb[:4] == b"glTF"
    length = struct.unpack("<I", glb[12:16])[0]
    return json.loads(glb[20 : 20 + length])


def _reads_iges() -> bool:
    """OpenCASCADE before 8.1 reads IGES only from a file, which the
    locked-down converter may not open, so a build against it refuses IGES."""
    iges = (MODELS_DIR / "microstrip.igs").read_bytes()
    try:
        cascadio.load(iges, file_type="iges")
    except ValueError as error:
        if "OpenCASCADE 8.1" in str(error):
            return False
        raise
    return True


needs_iges = pytest.mark.skipif(
    not _reads_iges(), reason="IGES needs cascadio built with OpenCASCADE 8.1+"
)


@pytest.fixture
def fake_worker(monkeypatch, tmp_path):
    """Have the module start a shell script in place of the worker."""
    if sys.platform == "win32":
        pytest.skip("uses a shell script as the worker")

    def install(script: str) -> None:
        worker = tmp_path / "worker"
        worker.write_text("#!/bin/sh\n" + script + "\n")
        worker.chmod(0o755)
        monkeypatch.setenv("CASCADIO_WORKER", str(worker))

    return install
