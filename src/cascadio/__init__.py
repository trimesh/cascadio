"""
cascadio - Convert STEP files to GLB using OpenCASCADE.

This module provides functions to convert BREP files (STEP, IGES)
to triangulated mesh formats (GLB, OBJ).
"""

# Import the C extension functions
from cascadio._core import step_to_glb, step_to_glb_bytes, step_to_obj, __version__

# Import primitives submodule
from . import primitives

__all__ = [
    # Core conversion functions
    "step_to_glb",
    "step_to_glb_bytes",
    "step_to_obj",
    # Version
    "__version__",
    # Primitives submodule
    "primitives",
]
