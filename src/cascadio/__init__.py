"""
cascadio - Convert STEP and IGES files to GLB using OpenCASCADE.

    import cascadio
    glb = cascadio.load(open("model.step", "rb").read())

`load` takes bytes and returns bytes. It never reads or writes files.
Everything here is implemented in the compiled `_core` module, except
the deprecated `step_to_glb`, which reads and writes files in Python,
and `step_to_obj`, which only raises an error that shows its replacement.
"""

from . import extension, primitives
from ._core import (
    DEFAULT_MEMORY_LIMIT,
    DEFAULT_TIMEOUT,
    MAX_TIMEOUT,
    FileType,
    NodeNameFormat,
    __version__,
    load,
    to_glb_bytes,
)
from .deprecated import step_to_glb, step_to_obj
from .primitives import BrepType

STEP = FileType.STEP
IGES = FileType.IGES


__all__ = [
    "DEFAULT_MEMORY_LIMIT",
    "DEFAULT_TIMEOUT",
    "IGES",
    "MAX_TIMEOUT",
    "STEP",
    "BrepType",
    "FileType",
    "NodeNameFormat",
    "__version__",
    "extension",
    "load",
    "primitives",
    "step_to_glb",
    "step_to_obj",
    "to_glb_bytes",
]
