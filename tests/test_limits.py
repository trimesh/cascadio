"""
Tests for how conversions fail: bad input, time and memory limits, a
crashing worker, and the pieces that keep a failure from hurting the caller.

Every conversion is done by the `cascadio-worker` program, started by the
compiled module. These tests are about that boundary.
"""

import os
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

import cascadio

MODELS_DIR = Path(__file__).parent / "models"
STEP = (MODELS_DIR / "featuretype.STEP").read_bytes()
NO_LIMITS = {"timeout": None, "memory_limit": None}


def test_limits_do_not_change_output():
    assert cascadio.load(STEP) == cascadio.load(STEP, **NO_LIMITS)
    assert cascadio.load(STEP) == cascadio.to_glb_bytes(STEP)


@pytest.mark.parametrize("file_type", ["step", "iges"])
@pytest.mark.parametrize("data", [b"", b"this is not a CAD file", STEP[:2000]])
def test_bad_data_raises(file_type, data):
    """Unparseable input raises instead of returning empty bytes."""
    with pytest.raises((ValueError, RuntimeError)):
        cascadio.load(data, file_type=file_type)


def test_garbage_is_a_value_error():
    with pytest.raises(ValueError, match="STEP"):
        cascadio.load(b"this is not a CAD file")


def test_bad_arguments():
    with pytest.raises(ValueError, match="file_type"):
        cascadio.load(STEP, file_type="stl")
    with pytest.raises(ValueError, match="timeout"):
        cascadio.load(STEP, timeout=0)
    with pytest.raises(TypeError):
        cascadio.load("not bytes")


def test_timeout():
    """A conversion that runs past `timeout` is killed and reported."""
    started = time.monotonic()
    # far less than a process needs just to start
    with pytest.raises(TimeoutError, match="timeout"):
        cascadio.load(STEP, timeout=0.0001)
    assert time.monotonic() - started < 5
    # the limit is per call: the next one works
    assert len(cascadio.load(STEP)) > 1000


def test_memory_limit():
    """A conversion that uses more than `memory_limit` is stopped."""
    # far less than the conversion needs
    with pytest.raises(MemoryError, match="memory_limit"):
        cascadio.load(STEP, memory_limit=2**20)
    assert len(cascadio.load(STEP)) > 1000


def test_only_one_limit():
    assert len(cascadio.load(STEP, timeout=None)) > 1000
    assert len(cascadio.load(STEP, memory_limit=None)) > 1000


def test_missing_worker(monkeypatch):
    """If the worker program cannot be started, say so."""
    monkeypatch.setenv("CASCADIO_WORKER", str(MODELS_DIR / "no-such-program"))
    with pytest.raises(RuntimeError, match="cannot start"):
        cascadio.load(STEP)


def test_crashing_worker(monkeypatch):
    """A worker that dies without one of our exit codes is a RuntimeError."""
    # the Python interpreter stands in for a crashed worker: handed our
    # request on standard input it fails with an exit code we do not use
    monkeypatch.setenv("CASCADIO_WORKER", sys.executable)
    with pytest.raises(RuntimeError, match="crashed"):
        cascadio.load(STEP)


def test_segfault_is_survived(monkeypatch, tmp_path):
    """A worker killed by a segmentation fault raises; the interpreter lives."""
    if sys.platform == "win32":
        pytest.skip("uses a shell script as the worker")
    worker = tmp_path / "segfault"
    worker.write_text("#!/bin/sh\nkill -SEGV $$\n")
    worker.chmod(0o755)
    monkeypatch.setenv("CASCADIO_WORKER", str(worker))
    with pytest.raises(RuntimeError, match=r"crashed.*signal 11"):
        cascadio.load(STEP)

    monkeypatch.delenv("CASCADIO_WORKER")
    assert len(cascadio.load(STEP)) > 1000


def test_large_input_and_output():
    """More data than a pipe holds flows both ways without deadlock."""
    # padding after the end of a STEP file is ignored by the reader
    padded = STEP + b"\n" * (8 * 2**20)
    assert cascadio.load(padded) == cascadio.load(STEP)


def test_gil_released():
    """Other Python threads keep running while a conversion is under way."""
    data = (MODELS_DIR / "multibody.step").read_bytes()
    done = threading.Event()

    def convert():
        try:
            cascadio.load(data)
        finally:
            done.set()

    thread = threading.Thread(target=convert)
    ticks = 0
    thread.start()
    while not done.is_set():
        time.sleep(0.001)
        ticks += 1
    thread.join()
    # the conversion takes around 100 ms; with the GIL held this would be ~0
    assert ticks > 5


def test_threads():
    """Conversions started from several threads at once all succeed."""
    expected = cascadio.load(STEP)
    results = []

    def convert():
        results.append(cascadio.load(STEP))

    threads = [threading.Thread(target=convert) for _ in range(8)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert results == [expected] * 8


def test_no_processes_left_behind():
    """Workers are reaped, including the ones that were killed."""
    if not sys.platform.startswith("linux"):
        pytest.skip("reads /proc")

    def children():
        me = str(os.getpid())
        found = []
        for entry in os.listdir("/proc"):
            if entry.isdigit():
                try:
                    with open(f"/proc/{entry}/stat") as f:
                        # pid (name) state ppid ...
                        fields = f.read().rsplit(")", 1)[1].split()
                except OSError:
                    continue
                if fields[1] == me:
                    found.append(entry)
        return found

    before = children()
    cascadio.load(STEP)
    with pytest.raises(TimeoutError):
        cascadio.load(STEP, timeout=0.0001)
    with pytest.raises(MemoryError):
        cascadio.load(STEP, memory_limit=2**20)
    assert children() == before


def test_import_without_numpy():
    """cascadio has no required dependencies: it must work without numpy."""
    script = """
import sys
sys.modules["numpy"] = None  # makes `import numpy` raise ImportError
sys.modules["trimesh"] = None
import cascadio
assert len(cascadio.load(open(sys.argv[1], "rb").read())) > 1000
"""
    subprocess.run(
        [sys.executable, "-c", script, str(MODELS_DIR / "featuretype.STEP")],
        check=True,
    )


def test_quiet():
    """A library should not print: conversions write nothing to the terminal."""
    script = """
import sys
import cascadio
for name, kind in (("featuretype.STEP", "step"), ("microstrip.igs", "iges")):
    cascadio.load(open(sys.argv[1] + "/" + name, "rb").read(), file_type=kind)
"""
    result = subprocess.run(
        [sys.executable, "-c", script, str(MODELS_DIR)],
        check=True,
        capture_output=True,
    )
    assert result.stdout == b""
    assert result.stderr == b""


def test_docstrings():
    """Every public function has a numpy-style docstring and a signature."""
    for name in ("load", "to_glb_bytes", "step_to_glb", "step_to_obj"):
        doc = getattr(cascadio, name).__doc__
        signature, body = doc.split("\n", 1)
        assert signature.startswith(name + "(") and "->" in signature
        for section in ("Parameters\n----------", "Returns\n-------", "Raises\n------"):
            assert section in body, (name, section)
        # every argument in the signature is described
        arguments = signature.split("(", 1)[1].rsplit(")", 1)[0].split(", ")
        for argument in arguments:
            assert "\n" + argument.split(":")[0].strip() + "\n" in body, (
                name,
                argument,
            )


def test_trace():
    """CASCADIO_TRACE prints how long each stage took, on standard error."""
    script = "import sys, cascadio; cascadio.load(open(sys.argv[1], 'rb').read())"
    result = subprocess.run(
        [sys.executable, "-c", script, str(MODELS_DIR / "featuretype.STEP")],
        check=True,
        capture_output=True,
        text=True,
        env={**os.environ, "CASCADIO_TRACE": "1"},
    )
    assert result.stdout == ""
    # "cascadio: mesh    412.7 ms"; Windows can add blank lines
    stages = [line.split()[1] for line in result.stderr.splitlines() if line.strip()]
    for stage in ("parse", "mesh", "total"):
        assert stage in stages


@pytest.mark.parametrize(
    "damage,reason",
    [
        (lambda d: b"\x00\x01\x02" + d, "ISO-10303-21"),
        (lambda d: d.replace(b"\n", b"\x00\n", 1), "control characters"),
        (lambda d: d[:400] + bytes([7]) + d[400:], "control characters"),
    ],
)
def test_damaged_step_is_refused_early(damage, reason):
    """Binary junk never reaches OpenCASCADE's STEP parser."""
    with pytest.raises(ValueError, match=reason):
        cascadio.load(damage(STEP))


def test_step_may_hold_non_ascii_text():
    """Names in UTF-8 or Latin-1 are fine; only control characters are not."""
    material = (MODELS_DIR / "material.stp").read_bytes()
    assert len(cascadio.load(material.replace(b"Steel", "Stähl".encode()))) > 1000
    # a byte order mark and leading blank lines are fine too
    assert len(cascadio.load(b"\xef\xbb\xbf\n\n" + STEP)) > 1000


def test_damaged_iges_is_refused_early():
    """IGES is fixed-width; a line that is not is refused before parsing."""
    iges = (MODELS_DIR / "microstrip.igs").read_bytes()
    lines = iges.split(b"\n")
    lines[5] = lines[5][:40]
    with pytest.raises(ValueError, match="line 6"):
        cascadio.load(b"\n".join(lines), file_type="iges")
    # Windows line endings are fine
    windows = iges.replace(b"\n", b"\r\n")
    assert len(cascadio.load(windows, file_type="iges")) > 100


def test_absurd_size_is_refused_before_meshing():
    """A model that could never be meshed is refused, not left to time out."""
    cuboid = (MODELS_DIR / "cuboid_zup.step").read_bytes()
    assert cuboid.count(b"(1.,") > 0
    started = time.monotonic()
    with pytest.raises(ValueError, match="sensible size"):
        cascadio.load(cuboid.replace(b"(1.,", b"(1.E300,"))
    # a tolerance a billion times finer than the model
    with pytest.raises(ValueError, match="tol_linear is too fine"):
        cascadio.load(STEP, tol_linear=1e-9)
    assert time.monotonic() - started < 5
    # a fine tolerance is fine when it is relative to each edge
    assert len(cascadio.load(STEP, tol_linear=1e-3, tol_relative=True)) > 1000
