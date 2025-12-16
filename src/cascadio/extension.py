"""
trimesh_ext.py
--------------

Trimesh extension integration for cascadio.

This module registers handlers for the TM_brep_faces glTF extension,
populating trimesh meshes with:
- mesh.face_attributes['brep_index']: numpy array of per-triangle face indices
- mesh.brep_primitives: list of cascadio.primitives dataclasses

Usage:
    import cascadio.trimesh_ext  # registers the handler on import
    import trimesh
    scene = trimesh.load("model.glb")
"""

from typing import Dict, List, Optional, Any

import numpy as np

try:
    from trimesh.exchange.gltf.extensions import register_handler
except ImportError:
    # Fallback no-op decorator if trimesh doesn't have extension registry yet
    def register_handler(*args, **kwargs):
        def decorator(fn):
            return fn
        return decorator

from .primitives import (
    BrepPrimitive,
    Plane,
    Cylinder,
    Cone,
    Sphere,
    Torus,
)


def _parse_face_to_primitive(face: Optional[Dict], face_index: int) -> Optional[BrepPrimitive]:
    """
    Convert a face definition from TM_brep_faces to a cascadio primitive.

    Parameters
    ----------
    face : dict or None
        Face definition from glTF extension, or None if filtered out
    face_index : int
        Index of this face in the faces array

    Returns
    -------
    BrepPrimitive or None
        The parsed primitive dataclass, or None if type not supported or face is None.
    """
    if face is None:
        return None
        
    face_type = face.get("type")
    if face_type is None:
        return None

    # Default bounds if not specified
    u_bounds = tuple(face.get("uBounds", (0.0, 1.0)))
    v_bounds = tuple(face.get("vBounds", (0.0, 1.0)))

    if face_type == "plane":
        return Plane(
            face_index=face_index,
            origin=tuple(face["origin"]),
            normal=tuple(face["normal"]),
            x_dir=tuple(face.get("xDir", (1.0, 0.0, 0.0))),
            u_bounds=u_bounds,
            v_bounds=v_bounds,
        )
    elif face_type == "cylinder":
        return Cylinder(
            face_index=face_index,
            origin=tuple(face["origin"]),
            axis=tuple(face["axis"]),
            radius=face["radius"],
            u_bounds=u_bounds,
            v_bounds=v_bounds,
        )
    elif face_type == "cone":
        return Cone(
            face_index=face_index,
            apex=tuple(face["apex"]),
            axis=tuple(face["axis"]),
            semi_angle=face["halfAngle"],
            ref_radius=face.get("refRadius", 0.0),
            u_bounds=u_bounds,
            v_bounds=v_bounds,
        )
    elif face_type == "sphere":
        return Sphere(
            face_index=face_index,
            center=tuple(face["center"]),
            radius=face["radius"],
            u_bounds=u_bounds,
            v_bounds=v_bounds,
        )
    elif face_type == "torus":
        return Torus(
            face_index=face_index,
            center=tuple(face["center"]),
            axis=tuple(face["axis"]),
            major_radius=face["majorRadius"],
            minor_radius=face["minorRadius"],
            u_bounds=u_bounds,
            v_bounds=v_bounds,
        )

    # "bspline", "other", or unknown types
    return None


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
    result = {
        "brep_index": np.asarray(face_indices),
        "brep_primitives": [],
    }

    if faces:
        primitives = []
        for i, face in enumerate(faces):
            prim = _parse_face_to_primitive(face, face_index=i)
            primitives.append(prim)
        result["brep_primitives"] = primitives

    return result


@register_handler("TM_brep_faces", "primitive")
def _import_brep_faces_cascadio(ext_data: Dict, **kwargs) -> Optional[Dict]:
    """
    Handle TM_brep_faces extension during import (cascadio version).

    Returns data in standard format for gltf.py to apply:
    - face_attributes: dict of per-face arrays (e.g., brep_index)
    - metadata: dict of additional data (e.g., brep_primitives)
    """
    if ext_data is None:
        return None

    result = {
        "face_attributes": {},
        "metadata": {},
    }

    # Get face indices accessor
    face_indices = None
    if "faceIndices" in ext_data:
        accessor_idx = ext_data["faceIndices"]
        accessors = kwargs.get("accessors")
        if accessors is not None and accessor_idx < len(accessors):
            face_indices = np.asarray(accessors[accessor_idx])
            result["face_attributes"]["brep_index"] = face_indices

    # Get face definitions if present
    faces = ext_data.get("faces")

    # Process into cascadio primitives
    if faces is not None:
        primitives = []
        for i, face in enumerate(faces):
            prim = _parse_face_to_primitive(face, face_index=i)
            primitives.append(prim)
        result["metadata"]["brep_primitives"] = primitives
        # Also store raw faces for direct access
        result["metadata"]["brep_faces"] = faces

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
