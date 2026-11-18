"""
cascadio - Convert STEP and IGES files to GLB using OpenCASCADE.

    import cascadio
    glb = cascadio.load(open("model.step", "rb").read())

`load` takes bytes and returns bytes. It never reads or writes files.
Everything here is implemented in the compiled `_core` module.
"""

from typing import Literal

from . import extension, primitives
from ._core import (
    DEFAULT_MEMORY_LIMIT,
    DEFAULT_TIMEOUT,
    FileType,
    NodeNameFormat,
    __version__,
    load,
    step_to_glb,
    step_to_obj,
    to_glb_bytes,
)

STEP = FileType.STEP
IGES = FileType.IGES

# The surface types `brep_types` accepts
BrepType = Literal["plane", "cylinder", "cone", "sphere", "torus"]

__all__ = [
    "load",
    "FileType",
    "NodeNameFormat",
    "STEP",
    "IGES",
    "BrepType",
    "DEFAULT_TIMEOUT",
    "DEFAULT_MEMORY_LIMIT",
    "to_glb_bytes",
    "step_to_glb",
    "step_to_obj",
    "__version__",
    "primitives",
    "extension",
]
