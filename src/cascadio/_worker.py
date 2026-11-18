"""
Run one conversion in a child process that can be killed.

OpenCASCADE can hang or use unbounded memory on unusual files, and it does
not check for cancellation in the places where that happens. The only
reliable limit is to do the work in another process and kill that process.

This file is both sides of that:

- `run()` is called by the parent. It starts a child, sends the input,
  and enforces the time limit.
- `main()` is the child. The parent runs this file as a script, so the
  child starts in a few milliseconds without importing `cascadio`, numpy
  or anything else outside the standard library. It loads the compiled
  `_core` module from the path the parent sends.

Protocol, parent to child on stdin:
    4 bytes   length of the header, little-endian
    N bytes   header: JSON `{"core", "function", "options", "memory_limit"}`
              where "core" is the path of the compiled module to load
    the rest  the input file
Child to parent on stdout:
    1 byte    0 for success, 1 for an error
    the rest  the output file, or JSON `{"type", "message"}` for an error
"""

import json
import os
import signal
import struct
import subprocess
import sys
import threading
import time

# Exit code the child uses to say "I stopped because of the memory limit".
EXIT_MEMORY = 97
# How often the child checks its memory use, in seconds.
POLL_SECONDS = 0.02
# Errors the child may ask the parent to raise.
ERRORS = {"ValueError": ValueError, "MemoryError": MemoryError}


def convert(core, function: str, data: bytes, options: dict) -> bytes:
    """
    Call the compiled module with plain-data options.

    Used directly for in-process conversion and by the child process, so
    both paths behave the same. Enum options arrive as their names.
    """
    options = dict(options)
    if function == "obj":
        core.step_to_obj_file(data, **options)
        return b""
    options["file_type"] = getattr(core.FileType, options["file_type"])
    options["node_name_format"] = getattr(
        core.NodeNameFormat, options["node_name_format"]
    )
    options["brep_types"] = set(options["brep_types"])
    return core.to_glb_bytes(data, **options)


def run(
    core_path: str,
    function: str,
    data: bytes,
    options: dict,
    timeout: float | None,
    memory_limit: int | None,
) -> bytes:
    """
    Parent side: convert in a child process and enforce the limits.

    Raises
    ------
    TimeoutError
      The conversion took longer than `timeout` seconds. The child is killed.
    MemoryError
      The child used more than `memory_limit` bytes and stopped itself.
    ValueError
      The input could not be parsed.
    RuntimeError
      OpenCASCADE failed, or the child crashed.
    """
    header = json.dumps(
        {
            "core": core_path,
            "function": function,
            "options": options,
            "memory_limit": memory_limit,
        }
    ).encode()
    child = subprocess.Popen(
        # -I and -S: ignore the environment and site-packages, the child
        # only needs the standard library and starts faster without them
        [sys.executable, "-I", "-S", __file__],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        # no console window flashing up on Windows
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    try:
        output, errors = child.communicate(
            struct.pack("<I", len(header)) + header + data, timeout=timeout
        )
    except subprocess.TimeoutExpired:
        child.kill()
        child.communicate()
        raise TimeoutError(
            f"conversion did not finish within {timeout} seconds; "
            "pass a larger `timeout`, or `timeout=None` for no limit"
        ) from None
    except BaseException:
        # e.g. KeyboardInterrupt: never leave the child running
        child.kill()
        child.communicate()
        raise

    code = child.returncode
    if code == EXIT_MEMORY:
        raise MemoryError(
            f"conversion used more than {memory_limit} bytes of memory; "
            "pass a larger `memory_limit`, or `memory_limit=None` for no limit"
        )
    if code == 0 and output[:1] == b"\x00":
        return output[1:]
    if code == 0 and output[:1] == b"\x01":
        error = json.loads(output[1:])
        raise ERRORS.get(error["type"], RuntimeError)(error["message"])

    # anything else is a crash: say how it died and what it printed last
    if code < 0:
        try:
            reason = f"signal {signal.Signals(-code).name}"
        except ValueError:
            reason = f"signal {-code}"
    else:
        reason = f"exit code {code}"
    detail = errors.decode(errors="replace").strip()[-500:]
    raise RuntimeError(f"converter process crashed ({reason}) {detail}".strip())


def _peak_memory() -> int:
    """The most physical memory this process has used so far, in bytes."""
    if sys.platform == "win32":
        import ctypes
        from ctypes import wintypes

        class Counters(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
                (name, ctypes.c_size_t)
                for name in (
                    "PeakWorkingSetSize",
                    "WorkingSetSize",
                    "QuotaPeakPagedPoolUsage",
                    "QuotaPagedPoolUsage",
                    "QuotaPeakNonPagedPoolUsage",
                    "QuotaNonPagedPoolUsage",
                    "PagefileUsage",
                    "PeakPagefileUsage",
                )
            ]

        kernel32 = ctypes.WinDLL("kernel32")
        kernel32.GetCurrentProcess.restype = wintypes.HANDLE
        kernel32.K32GetProcessMemoryInfo.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(Counters),
            wintypes.DWORD,
        ]
        counters = Counters(cb=ctypes.sizeof(Counters))
        kernel32.K32GetProcessMemoryInfo(
            kernel32.GetCurrentProcess(), ctypes.byref(counters), counters.cb
        )
        return counters.PeakWorkingSetSize

    import resource

    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    # Linux reports kilobytes, macOS reports bytes
    return peak if sys.platform == "darwin" else peak * 1024


def _watch_memory(limit: int) -> None:
    """Exit the whole process as soon as it has used more than `limit`."""
    while True:
        if _peak_memory() > limit:
            os._exit(EXIT_MEMORY)
        time.sleep(POLL_SECONDS)


def main() -> None:
    """Child side: read one request from stdin, write one reply to stdout."""
    # Keep the real stdout for the reply and point file descriptor 1 at
    # stderr, so nothing a C++ library prints can corrupt the reply.
    reply = os.fdopen(os.dup(1), "wb")
    os.dup2(2, 1)

    stdin = sys.stdin.buffer
    (header_length,) = struct.unpack("<I", stdin.read(4))
    header = json.loads(stdin.read(header_length))
    data = stdin.read()

    if header["memory_limit"] is not None:
        threading.Thread(
            target=_watch_memory, args=(header["memory_limit"],), daemon=True
        ).start()

    try:
        # load the compiled module by path, without importing the package
        from importlib.machinery import ExtensionFileLoader
        from importlib.util import module_from_spec, spec_from_loader

        loader = ExtensionFileLoader("_core", header["core"])
        core = module_from_spec(spec_from_loader("_core", loader))
        loader.exec_module(core)

        result = b"\x00" + convert(core, header["function"], data, header["options"])
    except BaseException as error:
        result = (
            b"\x01"
            + json.dumps({"type": type(error).__name__, "message": str(error)}).encode()
        )

    reply.write(result)
    reply.flush()
    # skip interpreter teardown: the work is done and the reply is sent
    os._exit(0)


if __name__ == "__main__":
    main()
