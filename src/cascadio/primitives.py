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
    # A point on the plane
    origin: Tuple[float, float, float]
    # Unit normal vector
    normal: Tuple[float, float, float]
    # Unit X direction in the plane
    x_dir: Tuple[float, float, float]
    # Parametric U bounds
    u_bounds: Tuple[float, float]
    # Parametric V bounds
    v_bounds: Tuple[float, float]


@dataclass(frozen=True)
class Cylinder:
    """A cylindrical surface."""

    # Index of this face in the BREP shape
    face_index: int
    # Point on axis at V=0
    origin: Tuple[float, float, float]
    # Unit axis direction
    axis: Tuple[float, float, float]
    # Cylinder radius
    radius: float
    # Angular bounds in radians
    u_bounds: Tuple[float, float]
    # Height extent along axis
    v_bounds: Tuple[float, float]


@dataclass(frozen=True)
class Cone:
    """A conical surface."""

    # Index of this face in the BREP shape
    face_index: int
    # Apex point of the cone
    apex: Tuple[float, float, float]
    # Unit axis direction
    axis: Tuple[float, float, float]
    # Half-angle at apex in radians
    semi_angle: float
    # Radius at the reference plane
    ref_radius: float
    # Angular bounds in radians
    u_bounds: Tuple[float, float]
    # Distance along axis from apex
    v_bounds: Tuple[float, float]


@dataclass(frozen=True)
class Sphere:
    """A spherical surface."""

    # Index of this face in the BREP shape
    face_index: int
    # Center point
    center: Tuple[float, float, float]
    # Sphere radius
    radius: float
    # Longitude bounds in radians
    u_bounds: Tuple[float, float]
    # Latitude bounds in radians [-π/2, π/2]
    v_bounds: Tuple[float, float]


@dataclass(frozen=True)
class Torus:
    """A toroidal surface."""

    # Index of this face in the BREP shape
    face_index: int
    # Center point
    center: Tuple[float, float, float]
    # Unit axis direction
    axis: Tuple[float, float, float]
    # Distance from center to tube center
    major_radius: float
    # Tube radius
    minor_radius: float
    # Angular bounds around axis in radians
    u_bounds: Tuple[float, float]
    # Angular bounds around tube in radians
    v_bounds: Tuple[float, float]


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


def parse_primitive(data: dict) -> Optional[BrepPrimitive]:
    """
    Parse a primitive dict from GLTF extras into a typed dataclass.

    Parameters
    ----------
    data : dict or None
        A single primitive dict from mesh.metadata['brep_faces'],
        or None if the face was filtered out

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

    face_index = data["face_index"]
    u_bounds = tuple(data["u_bounds"])
    v_bounds = tuple(data["v_bounds"])

    if ptype == "plane":
        return Plane(
            face_index=face_index,
            origin=tuple(data["origin"]),
            normal=tuple(data["normal"]),
            x_dir=tuple(data["x_dir"]),
            u_bounds=u_bounds,
            v_bounds=v_bounds,
        )
    elif ptype == "cylinder":
        return Cylinder(
            face_index=face_index,
            origin=tuple(data["origin"]),
            axis=tuple(data["axis"]),
            radius=data["radius"],
            u_bounds=u_bounds,
            v_bounds=v_bounds,
        )
    elif ptype == "cone":
        return Cone(
            face_index=face_index,
            apex=tuple(data["apex"]),
            axis=tuple(data["axis"]),
            semi_angle=data["semi_angle"],
            ref_radius=data["ref_radius"],
            u_bounds=u_bounds,
            v_bounds=v_bounds,
        )
    elif ptype == "sphere":
        return Sphere(
            face_index=face_index,
            center=tuple(data["center"]),
            radius=data["radius"],
            u_bounds=u_bounds,
            v_bounds=v_bounds,
        )
    elif ptype == "torus":
        return Torus(
            face_index=face_index,
            center=tuple(data["center"]),
            axis=tuple(data["axis"]),
            major_radius=data["major_radius"],
            minor_radius=data["minor_radius"],
            u_bounds=u_bounds,
            v_bounds=v_bounds,
        )

    return None


def parse_brep_faces(brep_faces: List[dict]) -> List[Optional[BrepPrimitive]]:
    """
    Parse all brep_faces from mesh metadata into typed dataclasses.

    Parameters
    ----------
    brep_faces : list of dict
        The brep_faces list from mesh.metadata['brep_faces']

    Returns
    -------
    list of BrepPrimitive or None
        List of parsed primitives, with None for non-analytical faces.
        Maintains the same indexing as the input list.
    """
    return [parse_primitive(face) for face in brep_faces]
