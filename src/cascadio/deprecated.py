"""
Functions that worked on file paths, kept so old callers (like released
trimesh) get either a working call or a clear error until they are gone.
"""

import warnings
from collections.abc import Set
from typing import NoReturn

from ._core import DEFAULT_MEMORY_LIMIT, DEFAULT_TIMEOUT, NodeNameFormat, load


def step_to_glb(
    input_path: str,
    output_path: str,
    tol_linear: float = 0.01,
    tol_angular: float = 0.5,
    tol_relative: bool = False,
    merge_primitives: bool = True,
    use_parallel: bool = True,
    include_brep: bool = False,
    brep_types: Set[str] | None = None,
    include_materials: bool = False,
    node_name_format: NodeNameFormat = NodeNameFormat.INSTANCE_OR_PRODUCT,
    timeout: float | None = DEFAULT_TIMEOUT,
    memory_limit: int | None = DEFAULT_MEMORY_LIMIT,
) -> int:
    """
    Deprecated: convert a STEP file on disk to a GLB file on disk.

    Use `load`, which takes and returns bytes. This function will be
    removed after 2027-05-22. Calling it warns, and the warning shows
    the `load` code that replaces the call.

    The files are read and written here, in the caller's own process.
    The sandboxed worker still only sees bytes.

    Parameters
    ----------
    input_path
      The STEP file to read.
    output_path
      Where to write the GLB file.
    tol_linear
      Passed to `load`.
    tol_angular
      Passed to `load`.
    tol_relative
      Passed to `load`.
    merge_primitives
      Passed to `load`.
    use_parallel
      Passed to `load`.
    include_brep
      Passed to `load`.
    brep_types
      Passed to `load`.
    include_materials
      Passed to `load`.
    node_name_format
      Passed to `load`.
    timeout
      Passed to `load`.
    memory_limit
      Passed to `load`.

    Returns
    -------
    status
      Always 0. Failures raise.

    Raises
    ------
    OSError
      If the input can't be read or the output can't be written.
    RuntimeError
      If the conversion fails, as `load` raises it.
    """
    warnings.warn(
        "cascadio.step_to_glb is deprecated and will be removed after "
        "2027-05-22. For security, cascadio now converts each file in a "
        "separate process, locked down with seccomp on Linux, so a bug "
        "that a hostile file triggers in OpenCASCADE can't do much. That "
        "process can't read or write files or reach the network, so "
        "cascadio takes and returns bytes. Replace this call with:\n\n"
        f"    with open({str(input_path)!r}, 'rb') as f:\n"
        f"        glb = cascadio.load(f.read(), tol_linear={tol_linear!r}, "
        f"tol_angular={tol_angular!r}, tol_relative={tol_relative!r}, "
        f"merge_primitives={merge_primitives!r}, use_parallel={use_parallel!r}, "
        f"include_brep={include_brep!r}, brep_types={brep_types!r}, "
        f"include_materials={include_materials!r}, "
        f"node_name_format=cascadio.NodeNameFormat.{node_name_format.name}, "
        f"timeout={timeout!r}, memory_limit={memory_limit!r})\n"
        f"    with open({str(output_path)!r}, 'wb') as f:\n"
        f"        f.write(glb)\n",
        DeprecationWarning,
        stacklevel=2,
    )

    with open(input_path, "rb") as f:
        data = f.read()
    glb = load(
        data,
        "step",
        tol_linear=tol_linear,
        tol_angular=tol_angular,
        tol_relative=tol_relative,
        merge_primitives=merge_primitives,
        use_parallel=use_parallel,
        include_brep=include_brep,
        brep_types=brep_types,
        include_materials=include_materials,
        node_name_format=node_name_format,
        timeout=timeout,
        memory_limit=memory_limit,
    )
    with open(output_path, "wb") as f:
        f.write(glb)
    return 0


def step_to_obj(
    input_path: str,
    output_path: str,
    tol_linear: float = 0.01,
    tol_angular: float = 0.5,
    tol_relative: bool = False,
    use_parallel: bool = True,
    use_colors: bool = True,
    timeout: float | None = DEFAULT_TIMEOUT,
    memory_limit: int | None = DEFAULT_MEMORY_LIMIT,
) -> NoReturn:
    """
    Removed: this used to convert a STEP file on disk to an OBJ file.

    It now always raises, and the error shows the code that replaces
    the call: `load` to get a GLB, then trimesh to write the OBJ.

    Parameters
    ----------
    input_path
      The STEP file it would have read.
    output_path
      The OBJ file it would have written.
    tol_linear
      Put in the suggested `load` call.
    tol_angular
      Put in the suggested `load` call.
    tol_relative
      Put in the suggested `load` call.
    use_parallel
      Put in the suggested `load` call.
    use_colors
      Ignored: the GLB from `load` always has colors.
    timeout
      Put in the suggested `load` call.
    memory_limit
      Put in the suggested `load` call.

    Returns
    -------
    never
      It always raises.

    Raises
    ------
    NotImplementedError
      Always.
    """
    raise NotImplementedError(
        "cascadio.step_to_obj was removed in cascadio 0.2 for security. "
        "cascadio now converts each file in a separate process, locked "
        "down with seccomp on Linux, so a bug that a hostile file triggers "
        "in OpenCASCADE can't do much. That process can't read or write "
        "files or reach the network, and OpenCASCADE's OBJ writer needs to "
        "write files. Replace this call with:\n\n"
        "    import io, trimesh\n"
        f"    with open({str(input_path)!r}, 'rb') as f:\n"
        f"        glb = cascadio.load(f.read(), tol_linear={tol_linear!r}, "
        f"tol_angular={tol_angular!r}, tol_relative={tol_relative!r}, "
        f"use_parallel={use_parallel!r}, timeout={timeout!r}, "
        f"memory_limit={memory_limit!r})\n"
        "    trimesh.load_scene(io.BytesIO(glb), file_type='glb')"
        f".export({str(output_path)!r})\n"
    )
