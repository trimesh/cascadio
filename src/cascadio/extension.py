"""
extension.py
------------

Trimesh extension integration for cascadio.

This module registers handlers for the TM_brep_faces glTF extension,
populating trimesh meshes with:
- mesh.face_attributes['brep_index']: numpy array of per-triangle face indices
- mesh.metadata['cascadio']['brep_primitives']: list of cascadio.primitives dataclasses

Usage:
    import cascadio  # registers the handler on import
    import trimesh
    scene = trimesh.load("model.glb")
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Any, Dict, List, Optional

if TYPE_CHECKING:
    # numpy is only needed once trimesh hands us arrays, so cascadio
    # itself imports and converts without it
    import numpy as np

try:
    from trimesh.exchange.gltf.extensions import register_handler
except ImportError:
    # Fallback no-op decorator if trimesh doesn't have extension registry yet
    def register_handler(*args, **kwargs):
        def decorator(fn):
            return fn

        return decorator


from .primitives import parse_brep_faces


def process_brep_faces(
    face_indices: np.ndarray,
    faces: Optional[List[Dict]] = None,
) -> Dict[str, Any]:
    """
    Process TM_brep_faces data into mesh-ready format.

    Parameters
    ----------
    face_indices : np.ndarray
        Per-triangle face indices from the extension
    faces : list of dict, optional
        Face definitions with surface parameters

    Returns
    -------
    result : dict
        - 'brep_index': numpy array for face_attributes
        - 'brep_primitives': list of primitive dataclasses
    """
    import numpy as np

    result = {
        "brep_index": np.asarray(face_indices).flatten(),
        "brep_primitives": [],
    }

    if faces:
        result["brep_primitives"] = parse_brep_faces(faces)

    return result


@register_handler("TM_brep_faces", "primitive")
def _import_brep_faces_cascadio(ctx: Dict) -> Optional[Dict]:
    """
    Handle TM_brep_faces extension during import (cascadio version).

    Returns data in standard format for gltf.py to apply:
    - face_attributes: dict of per-face arrays (e.g., brep_index)
    - metadata: dict of additional data (e.g., brep_primitives)
    """
    data = ctx.get("data")
    if data is None:
        return None

    result = {
        "face_attributes": {},
        "metadata": {},
    }

    # Get face indices accessor
    face_indices = None
    if "faceIndices" in data:
        accessor_idx = data["faceIndices"]
        accessors = ctx.get("accessors")
        if accessors is not None and accessor_idx < len(accessors):
            import numpy as np

            face_indices = np.asarray(accessors[accessor_idx]).flatten()
            result["face_attributes"]["brep_index"] = face_indices

    # Get face definitions if present
    faces = data.get("faces")

    # Get materials if present (may be included alongside BREP data)
    materials = data.get("materials")

    # Process into cascadio primitives
    if faces is not None:
        # Put everything under cascadio namespace for consistency
        cascadio_meta = result["metadata"].setdefault("cascadio", {})
        cascadio_meta["brep_primitives"] = parse_brep_faces(faces)
        cascadio_meta["brep_faces"] = faces

    # Add materials if present
    if materials is not None:
        cascadio_meta = result["metadata"].setdefault("cascadio", {})
        cascadio_meta["materials"] = materials

    # Only return if we have something
    if result["face_attributes"] or result["metadata"]:
        return result
    return None


def apply_brep_to_mesh(mesh: Any, brep_data: Dict) -> None:
    """
    Apply BREP extension data to a trimesh mesh object.

    This should be called after loading to populate the mesh attributes.

    Parameters
    ----------
    mesh : trimesh.Trimesh
        The mesh to populate
    brep_data : dict
        Data from the TM_brep_faces handler result
    """
    if "brep_index" in brep_data:
        # Ensure face_attributes exists
        if not hasattr(mesh, "face_attributes") or mesh.face_attributes is None:
            mesh.face_attributes = {}
        mesh.face_attributes["brep_index"] = brep_data["brep_index"]

    if "brep_primitives" in brep_data:
        mesh.brep_primitives = brep_data["brep_primitives"]


__all__ = [
    "process_brep_faces",
    "apply_brep_to_mesh",
]
