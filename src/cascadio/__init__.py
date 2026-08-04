"""
cascadio - Convert STEP and IGES files to GLB using OpenCASCADE.

This module provides functions to convert BREP files (STEP, IGES)
to triangulated mesh formats (GLB, OBJ).
"""

from typing import Literal, Optional, Set, Tuple

# Import the C extension functions and enums
from cascadio._core import (
    FileType,
    to_glb_bytes,
    step_to_glb,
    step_to_obj,
    __version__,
)

# Import primitives submodule
from . import primitives

# Import trimesh extension
from . import extension

# File type constants for convenience
STEP = FileType.STEP
IGES = FileType.IGES


# Valid BREP primitive types
BrepType = Literal["plane", "cylinder", "cone", "sphere", "torus"]


def load(
    data: bytes,
    file_type: Literal["step", "iges"] = "step",
    tol_linear: float = 0.01,
    tol_angular: float = 0.5,
    tol_relative: bool = False,
    merge_primitives: bool = True,
    use_parallel: bool = True,
    include_brep: bool = False,
    brep_types: Optional[Set[BrepType]] = None,
    include_materials: bool = False,
    include_edges: bool = False,
    edge_color: Tuple[float, float, float, float] = (0.25, 0.25, 0.25, 1.0),
) -> bytes:
    """
    Convert BREP data (STEP or IGES) to GLB format.

    This is the primary interface for converting CAD files to mesh format.
    The input and output are both bytes, making it easy to work with in-memory
    data or file I/O.

    Parameters
    ----------
    data : bytes
        The input BREP file content as bytes.
    file_type : str, optional
        The file format: "step" or "iges". Default is "step".
    tol_linear : float, optional
        Linear deflection tolerance for meshing. Default is 0.01.
    tol_angular : float, optional
        Angular deflection tolerance for meshing in radians. Default is 0.5.
    tol_relative : bool, optional
        Whether tol_linear is relative to edge length. Default is False.
    merge_primitives : bool, optional
        Produce a GLB with one mesh primitive per part. Default is True.
    use_parallel : bool, optional
        Use parallel execution for meshing and export. Default is True.
    include_brep : bool, optional
        Include BREP analytical primitive data in GLB extras. Default is False.
    brep_types : set, optional
        Set of primitive types to include if include_brep is True.
        Valid values: "plane", "cylinder", "cone", "sphere", "torus".
    include_materials : bool, optional
        Include material data in GLB asset.extras.materials. Default is False.
    include_edges : bool, optional
        Include edge lines in GLB output. Default is False.
    edge_color : tuple of 4 floats, optional
        RGBA color for edge lines when include_edges=True.
        Each value in [0, 1]. Default is (0.25, 0.25, 0.25, 1.0) (gray).

    Returns
    -------
    bytes
        The GLB file content as bytes, or empty bytes on error.

    Examples
    --------
    >>> import cascadio
    >>> with open("model.step", "rb") as f:
    ...     step_data = f.read()
    >>> glb_data = cascadio.load(step_data, file_type="step")
    >>> with open("model.glb", "wb") as f:
    ...     f.write(glb_data)

    >>> # Load an IGES file
    >>> with open("model.igs", "rb") as f:
    ...     iges_data = f.read()
    >>> glb_data = cascadio.load(iges_data, file_type="iges")

    >>> # Load with edges and custom color
    >>> glb_data = cascadio.load(step_data, include_edges=True, edge_color=(0.25, 0.25, 0.25, 1.0))
    """
    # Convert string file_type to enum
    file_type_lower = file_type.lower()
    if file_type_lower == "step" or file_type_lower == "stp":
        ft_enum = STEP
    elif file_type_lower == "iges" or file_type_lower == "igs":
        ft_enum = IGES
    else:
        raise ValueError(f"Unsupported file_type: {file_type}. Use 'step' or 'iges'.")

    if brep_types is None:
        brep_types = set()

    return to_glb_bytes(
        data=data,
        file_type=ft_enum,
        tol_linear=tol_linear,
        tol_angular=tol_angular,
        tol_relative=tol_relative,
        merge_primitives=merge_primitives,
        use_parallel=use_parallel,
        include_brep=include_brep,
        brep_types=brep_types,
        include_materials=include_materials,
        include_edges=include_edges,
        edge_color=edge_color,
    )


__all__ = [
    "load",
    "FileType",
    "STEP",
    "IGES",
    "to_glb_bytes",
    "step_to_glb",
    "step_to_obj",
    "__version__",
    "primitives",
    "trimesh_ext",
]
