"""
Dataclasses for BREP analytical surface primitives.

These represent the analytical surfaces extracted from STEP files
when using `include_brep=True` in `step_to_glb()`.
"""

from dataclasses import dataclass
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

__all__ = [
    "Plane",
    "Cylinder",
    "Cone",
    "Sphere",
    "Torus",
    "BrepPrimitive",
    "parse_primitive",
    "parse_brep_faces",
]


def parse_primitive(data: dict, face_index: int = 0) -> Optional[BrepPrimitive]:
    """
    Parse a primitive dict from GLTF extension into a typed dataclass.

    Parameters
    ----------
    data : dict or None
        A single primitive dict from the TM_brep_faces extension,
        or None if the face was filtered out
    face_index : int
        The index of this face in the faces array (used as fallback
        if face_index is not in the data)

    Returns
    -------
    BrepPrimitive or None
        The parsed primitive, or None if type is not recognized or data is None.
    """
    if data is None:
        return None

    ptype = data.get("type")
    if ptype is None:
        return None

    # Use face_index from data if present, otherwise use parameter
    idx = data.get("face_index", face_index)

    if ptype == "plane":
        return Plane(
            face_index=idx,
            extent_x=tuple(data["extent_x"]),
            extent_y=tuple(data["extent_y"]),
            origin=tuple(data["origin"]),
            normal=tuple(data["normal"]),
            x_dir=tuple(data["x_dir"]),
        )
    elif ptype == "cylinder":
        return Cylinder(
            face_index=idx,
            extent_angle=tuple(data["extent_angle"]),
            extent_height=tuple(data["extent_height"]),
            origin=tuple(data["origin"]),
            axis=tuple(data["axis"]),
            radius=data["radius"],
        )
    elif ptype == "cone":
        return Cone(
            face_index=idx,
            extent_angle=tuple(data["extent_angle"]),
            extent_distance=tuple(data["extent_distance"]),
            apex=tuple(data["apex"]),
            axis=tuple(data["axis"]),
            semi_angle=data["semi_angle"],
            ref_radius=data["ref_radius"],
        )
    elif ptype == "sphere":
        return Sphere(
            face_index=idx,
            extent_longitude=tuple(data["extent_longitude"]),
            extent_latitude=tuple(data["extent_latitude"]),
            center=tuple(data["center"]),
            radius=data["radius"],
        )
    elif ptype == "torus":
        return Torus(
            face_index=idx,
            extent_major_angle=tuple(data["extent_major_angle"]),
            extent_minor_angle=tuple(data["extent_minor_angle"]),
            center=tuple(data["center"]),
            axis=tuple(data["axis"]),
            major_radius=data["major_radius"],
            minor_radius=data["minor_radius"],
        )

    return None


def parse_brep_faces(brep_faces: List[dict]) -> List[Optional[BrepPrimitive]]:
    """
    Parse all brep_faces from mesh metadata into typed dataclasses.

    Parameters
    ----------
    brep_faces : list of dict
        The faces list from the TM_brep_faces extension

    Returns
    -------
    list of BrepPrimitive or None
        List of parsed primitives, with None for non-analytical faces.
        Maintains the same indexing as the input list.
    """
    return [parse_primitive(face, face_index=i) for i, face in enumerate(brep_faces)]
