"""
cascadio - Convert STEP and IGES files to GLB using OpenCASCADE.

    import cascadio
    glb = cascadio.load(open("model.step", "rb").read())

`load` takes bytes and returns bytes. It never reads or writes files.
"""

import sys
import warnings
from typing import Literal, Optional, Set

from . import _core, _worker, extension, primitives
from ._core import (
    FileType,
    NodeNameFormat,
    __version__,
    step_to_glb,
    step_to_obj,
    to_glb_bytes,
)

STEP = FileType.STEP
IGES = FileType.IGES

# Valid BREP primitive types
BrepType = Literal["plane", "cylinder", "cone", "sphere", "torus"]

# Default limits for one conversion. Ordinary files finish in well under
# a second and use around 100 MB, so these only stop runaway inputs.
DEFAULT_TIMEOUT = 30.0
DEFAULT_MEMORY_LIMIT = 8 * 2**30

_FILE_TYPES = {"step": "STEP", "stp": "STEP", "iges": "IGES", "igs": "IGES"}


def _convert(
    data: bytes,
    options: dict,
    timeout: Optional[float],
    memory_limit: Optional[int],
) -> bytes:
    """Convert in a killable child process, or in-process if no limit is set."""
    if timeout is None and memory_limit is None:
        return _worker.convert(_core, data, options)
    if getattr(sys, "frozen", False) or not sys.executable:
        # a frozen application has no Python executable to start a child with
        warnings.warn(
            "cascadio cannot start a worker process from a frozen application: "
            "`timeout` and `memory_limit` are not enforced",
            RuntimeWarning,
            stacklevel=3,
        )
        return _worker.convert(_core, data, options)
    return _worker.run(_core.__file__, data, options, timeout, memory_limit)


def load(
    data: bytes,
    file_type: Literal["step", "stp", "iges", "igs"] = "step",
    tol_linear: float = 0.01,
    tol_angular: float = 0.5,
    tol_relative: bool = False,
    merge_primitives: bool = True,
    use_parallel: bool = True,
    include_brep: bool = False,
    brep_types: Optional[Set[BrepType]] = None,
    include_materials: bool = False,
    node_name_format: NodeNameFormat = NodeNameFormat.INSTANCE_OR_PRODUCT,
    y_up: bool = False,
    timeout: Optional[float] = DEFAULT_TIMEOUT,
    memory_limit: Optional[int] = DEFAULT_MEMORY_LIMIT,
) -> bytes:
    """
    Convert STEP or IGES data to a GLB, bytes in and bytes out.

    The conversion runs in a child process so it can be stopped: some files
    make OpenCASCADE hang or use all available memory, and a crash in it
    would otherwise take the whole interpreter down. The child adds about
    30 milliseconds. Pass `timeout=None, memory_limit=None` to convert
    inside the calling process instead, with no protection.

    Parameters
    ----------
    data
      The STEP or IGES file content. It may be compressed with Zstandard
      (`.step.zst`): that is detected from the first four bytes and
      unpacked in memory.
    file_type
      "step" or "iges" (also "stp" and "igs").
    tol_linear
      The largest distance allowed between the mesh and the true surface,
      in the units of the file.
    tol_angular
      The largest angle allowed between neighbouring mesh segments, in radians.
    tol_relative
      If True, `tol_linear` is a fraction of each edge's length
      instead of an absolute distance.
    merge_primitives
      Produce one mesh primitive per part instead of one per face.
    use_parallel
      Use multiple threads for meshing and export.
    include_brep
      Add the `TM_brep_faces` extension: for every triangle the index of
      the CAD face it came from, and the exact plane, cylinder, cone,
      sphere or torus of that face. Turns `merge_primitives` on.
    brep_types
      If given, only these surface types are described, for example
      `{"plane", "cylinder"}`.
    include_materials
      Add material names, densities and colors from the file.
      Turns `merge_primitives` on.
    node_name_format
      Which name each GLB node gets: the instance label, the product
      name, or a mix.
    y_up
      CAD files are almost always Z-up and by default the coordinates are
      kept as they are. glTF says +Y is up, so viewers such as Blender
      rotate what they import. Set this to rotate the output into glTF's
      frame, (x, y, z) -> (x, z, -y), so those viewers show the model the
      way the CAD system did.
    timeout
      Seconds the conversion may take, or None for no limit.
    memory_limit
      Bytes of memory the conversion may use, or None for no limit.

    Returns
    -------
    glb
      The GLB file content.

    Raises
    ------
    ValueError
      The data could not be parsed, or an argument is invalid.
    TimeoutError
      The conversion took longer than `timeout`.
    MemoryError
      The conversion used more than `memory_limit`.
    RuntimeError
      OpenCASCADE failed to convert the file, or crashed.
    """
    kind = _FILE_TYPES.get(str(file_type).lower())
    if kind is None:
        raise ValueError(f"Unsupported file_type: {file_type}. Use 'step' or 'iges'.")
    return _convert(
        data,
        {
            "file_type": kind,
            "tol_linear": tol_linear,
            "tol_angular": tol_angular,
            "tol_relative": tol_relative,
            "merge_primitives": merge_primitives,
            "use_parallel": use_parallel,
            "include_brep": include_brep,
            "brep_types": sorted(brep_types or ()),
            "include_materials": include_materials,
            "node_name_format": node_name_format.name,
            "y_up": y_up,
        },
        timeout,
        memory_limit,
    )


__all__ = [
    "load",
    "FileType",
    "NodeNameFormat",
    "STEP",
    "IGES",
    "DEFAULT_TIMEOUT",
    "DEFAULT_MEMORY_LIMIT",
    "to_glb_bytes",
    "step_to_glb",
    "step_to_obj",
    "__version__",
    "primitives",
    "extension",
]
