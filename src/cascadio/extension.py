"""
The trimesh plugin for the `TM_brep_faces` glTF extension.

Importing cascadio registers it. When trimesh then loads a GLB made with
`cascadio.load(..., include_brep=True)`, each mesh gets:

- `mesh.face_attributes["brep_index"]`: for every triangle, the index of
  the CAD face it came from
- `mesh.metadata["cascadio"]["brep_primitives"]`: that face's surface as a
  `cascadio.primitives` dataclass, or None if it is not a plane, cylinder,
  cone, sphere or torus
- `mesh.metadata["cascadio"]["brep_faces"]`: the same as plain dicts
- `mesh.metadata["cascadio"]["materials"]`: every material the source
  model defines
"""

from typing import Optional

from .primitives import parse_brep_faces

try:
    from trimesh.exchange.gltf.extensions import register_handler
except ImportError:
    # trimesh is optional, and old versions have no extension registry
    def register_handler(*args, **kwargs):
        return lambda function: function


@register_handler("TM_brep_faces", "primitive")
def _import_brep_faces(ctx: dict) -> Optional[dict]:
    """
    Turn one primitive's `TM_brep_faces` data into what trimesh attaches
    to the mesh: `{"face_attributes": {...}, "metadata": {...}}`.
    """
    data = ctx.get("data")
    if data is None:
        return None

    face_attributes = {}
    metadata = {}
    accessors = ctx.get("accessors")
    index = data.get("faceIndices")
    if index is not None and accessors is not None and index < len(accessors):
        # trimesh hands accessors over as numpy arrays
        face_attributes["brep_index"] = accessors[index].reshape(-1)
    if data.get("faces") is not None:
        metadata["brep_primitives"] = parse_brep_faces(data["faces"])
        metadata["brep_faces"] = data["faces"]
    if data.get("materials") is not None:
        metadata["materials"] = data["materials"]

    if not face_attributes and not metadata:
        return None
    return {
        "face_attributes": face_attributes,
        "metadata": {"cascadio": metadata} if metadata else {},
    }
