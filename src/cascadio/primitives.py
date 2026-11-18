"""
Dataclasses for BREP analytical surface primitives.

These represent the analytical surfaces extracted from CAD files
when using `include_brep=True` in `cascadio.load()`.
"""

from dataclasses import dataclass, fields
from typing import List, Tuple, Union, Optional


@dataclass(frozen=True)
class Plane:
    """A planar surface."""

    # Index of this face in the BREP shape
    face_index: int
    # Extent in local X direction (meters)
    extent_x: Tuple[float, float]
    # Extent in local Y direction (meters)
    extent_y: Tuple[float, float]
    # A point on the plane
    origin: Tuple[float, float, float]
    # Unit normal vector
    normal: Tuple[float, float, float]
    # Unit X direction in the plane
    x_dir: Tuple[float, float, float]


@dataclass(frozen=True)
class Cylinder:
    """A cylindrical surface."""

    # Index of this face in the BREP shape
    face_index: int
    # Angular extent around axis (radians)
    extent_angle: Tuple[float, float]
    # Height extent along axis (meters)
    extent_height: Tuple[float, float]
    # Point on axis
    origin: Tuple[float, float, float]
    # Unit axis direction
    axis: Tuple[float, float, float]
    # Cylinder radius in meters
    radius: float


@dataclass(frozen=True)
class Cone:
    """A conical surface."""

    # Index of this face in the BREP shape
    face_index: int
    # Angular extent around axis (radians)
    extent_angle: Tuple[float, float]
    # Distance extent from apex (meters)
    extent_distance: Tuple[float, float]
    # Apex point of the cone
    apex: Tuple[float, float, float]
    # Unit axis direction from apex
    axis: Tuple[float, float, float]
    # Semi-angle at apex in radians
    semi_angle: float
    # Reference radius at the origin plane in meters
    ref_radius: float


@dataclass(frozen=True)
class Sphere:
    """A spherical surface."""

    # Index of this face in the BREP shape
    face_index: int
    # Longitude extent (radians)
    extent_longitude: Tuple[float, float]
    # Latitude extent (radians)
    extent_latitude: Tuple[float, float]
    # Center point
    center: Tuple[float, float, float]
    # Sphere radius in meters
    radius: float


@dataclass(frozen=True)
class Torus:
    """A toroidal surface."""

    # Index of this face in the BREP shape
    face_index: int
    # Angle extent around main axis (radians)
    extent_major_angle: Tuple[float, float]
    # Angle extent around tube (radians)
    extent_minor_angle: Tuple[float, float]
    # Center point
    center: Tuple[float, float, float]
    # Unit axis direction (normal to torus plane)
    axis: Tuple[float, float, float]
    # Distance from center to tube center in meters
    major_radius: float
    # Tube radius in meters
    minor_radius: float


# Union type for any primitive
BrepPrimitive = Union[Plane, Cylinder, Cone, Sphere, Torus]

# The `type` string of a face in the TM_brep_faces extension -> its dataclass.
# The dataclass fields are the schema's fields for that type; a test checks
# they match extensions/TM_brep_faces/schema.
PRIMITIVES = {
    "plane": Plane,
    "cylinder": Cylinder,
    "cone": Cone,
    "sphere": Sphere,
    "torus": Torus,
}

__all__ = [
    "Plane",
    "Cylinder",
    "Cone",
    "Sphere",
    "Torus",
    "BrepPrimitive",
    "PRIMITIVES",
    "parse_primitive",
    "parse_brep_faces",
]


def parse_primitive(
    data: Optional[dict], face_index: int = 0
) -> Optional[BrepPrimitive]:
    """
    Parse one face of the TM_brep_faces extension into a typed dataclass.

    Parameters
    ----------
    data
      A face dict from the extension, or None for a face that is not an
      analytic surface or was filtered out.
    face_index
      Used when the dict carries no `face_index` of its own.

    Returns
    -------
    primitive
      The parsed primitive, or None if `data` is None or of an unknown type.
    """
    kind = PRIMITIVES.get((data or {}).get("type"))
    if kind is None:
        return None
    values = {"face_index": face_index, **data}
    return kind(
        **{
            # JSON arrays become tuples so the frozen dataclass is hashable
            f.name: tuple(v) if isinstance(v := values[f.name], list) else v
            for f in fields(kind)
        }
    )


def parse_brep_faces(brep_faces: List[Optional[dict]]) -> List[Optional[BrepPrimitive]]:
    """
    Parse the `faces` list of the TM_brep_faces extension.

    Returns one entry per face, in the same order, with None for faces
    that are not analytic surfaces.
    """
    return [parse_primitive(face, face_index=i) for i, face in enumerate(brep_faces)]
