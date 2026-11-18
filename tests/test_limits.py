"""
Tests for how conversions fail: bad input, time and memory limits,
crashes, and the pieces that keep a failure from hurting the caller.
"""

import subprocess
import sys
import threading
import time
from pathlib import Path

import cascadio
import pytest
from cascadio import _worker

MODELS_DIR = Path(__file__).parent / "models"
STEP = (MODELS_DIR / "featuretype.STEP").read_bytes()
# no limits means the conversion runs inside this process
IN_PROCESS = {"timeout": None, "memory_limit": None}


def test_in_process_matches_worker():
    """Both ways of running a conversion give the same bytes."""
    assert cascadio.load(STEP) == cascadio.load(STEP, **IN_PROCESS)


@pytest.mark.parametrize("limits", [{}, IN_PROCESS], ids=["worker", "in-process"])
@pytest.mark.parametrize("file_type", ["step", "iges"])
@pytest.mark.parametrize("data", [b"", b"this is not a CAD file", STEP[:2000]])
def test_bad_data_raises(limits, file_type, data):
    """Unparseable input raises instead of returning empty bytes."""
    with pytest.raises((ValueError, RuntimeError)):
        cascadio.load(data, file_type=file_type, **limits)


@pytest.mark.parametrize("limits", [{}, IN_PROCESS], ids=["worker", "in-process"])
def test_garbage_is_a_value_error(limits):
    with pytest.raises(ValueError, match="STEP"):
        cascadio.load(b"this is not a CAD file", **limits)


def test_bad_file_type():
    with pytest.raises(ValueError, match="file_type"):
        cascadio.load(STEP, file_type="stl")


def test_timeout():
    """A conversion that runs past `timeout` is killed and reported."""
    started = time.monotonic()
    # a process cannot even start in a millisecond
    with pytest.raises(TimeoutError, match="timeout"):
        cascadio.load(STEP, timeout=0.001)
    assert time.monotonic() - started < 5
    # the limit is per call: the next one works
    assert len(cascadio.load(STEP)) > 1000


def test_memory_limit():
    """A conversion that uses more than `memory_limit` is stopped."""
    # far less than the Python interpreter itself needs
    with pytest.raises(MemoryError, match="memory_limit"):
        cascadio.load(STEP, memory_limit=2**20)
    assert len(cascadio.load(STEP)) > 1000


def test_only_one_limit():
    """Either limit alone still runs in a worker."""
    assert len(cascadio.load(STEP, timeout=None)) > 1000
    assert len(cascadio.load(STEP, memory_limit=None)) > 1000


def test_worker_crash_is_reported():
    """If the child dies without answering, the parent raises RuntimeError."""
    # a module that cannot be loaded stands in for a crash inside OpenCASCADE
    with pytest.raises(RuntimeError):
        _worker.run("/nonexistent/_core.so", STEP, {}, 30, None)


def test_gil_released():
    """Other Python threads keep running during an in-process conversion."""
    data = (MODELS_DIR / "multibody.step").read_bytes()
    done = threading.Event()

    def convert():
        try:
            cascadio.load(data, **IN_PROCESS)
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


def test_frozen_application(monkeypatch):
    """Without a Python executable there is no worker: warn and convert anyway."""
    monkeypatch.setattr(sys, "frozen", True, raising=False)
    with pytest.warns(RuntimeWarning, match="not enforced"):
        assert len(cascadio.load(STEP)) > 1000


def test_import_without_numpy():
    """cascadio has no required dependencies: it must work without numpy."""
    script = """
import sys
sys.modules["numpy"] = None  # makes `import numpy` raise ImportError
sys.modules["trimesh"] = None
import cascadio
data = open(sys.argv[1], "rb").read()
assert len(cascadio.load(data)) > 1000
assert len(cascadio.load(data, timeout=None, memory_limit=None)) > 1000
"""
    subprocess.run(
        [sys.executable, "-c", script, str(MODELS_DIR / "featuretype.STEP")],
        check=True,
    )


def test_quiet():
    """A library should not print: conversions write nothing to the terminal."""
    script = """
import sys, warnings
import cascadio
for name, kind in (("featuretype.STEP", "step"), ("microstrip.igs", "iges")):
    data = open(sys.argv[1] + "/" + name, "rb").read()
    cascadio.load(data, file_type=kind)
    cascadio.load(data, file_type=kind, timeout=None, memory_limit=None)
"""
    result = subprocess.run(
        [sys.executable, "-c", script, str(MODELS_DIR)],
        check=True,
        capture_output=True,
    )
    assert result.stdout == b""
    assert result.stderr == b""


def test_threads_in_process():
    """Conversions started from several threads at once all succeed."""
    expected = cascadio.load(STEP, **IN_PROCESS)
    results = []

    def convert():
        results.append(cascadio.load(STEP, **IN_PROCESS))

    threads = [threading.Thread(target=convert) for _ in range(8)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert results == [expected] * 8


def test_threads_worker():
    """Worker processes run side by side without interfering."""
    expected = cascadio.load(STEP)
    results = []

    def convert():
        results.append(cascadio.load(STEP))

    threads = [threading.Thread(target=convert) for _ in range(4)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert results == [expected] * 4
