"""
The surfaces of the TM_brep_faces extension as dataclasses.

Generated from extensions/TM_brep_faces/schema/mesh.primitive.TM_brep_faces.schema.json
by scripts/gen_schema.py. Do not edit: change the schema and run `make schema`.

All positions and lengths are in meters and all directions are unit vectors, in
the same coordinate frame as the primitive's POSITION accessor (before any node
transform). All angles are in radians and turn counter-clockwise when seen from
the tip of the axis they turn about (the right-hand rule).
"""

from dataclasses import dataclass, fields
from typing import List, Optional, Tuple, Union

# a position [x, y, z] in meters
Point = Tuple[float, float, float]
# a direction [x, y, z] of length 1
Direction = Tuple[float, float, float]
# [min, max] along one of a surface's two directions
Range = Tuple[float, float]


@dataclass(frozen=True)
class Plane:
    """
    A face on a plane.

    Attributes
    ----------
    face_index
        Position of this face in the `faces` array, which is the value
        `faceIndices` holds for its triangles
    origin
        A point on the plane, from which the extents are measured
    normal
        Unit direction perpendicular to the plane
    x_dir
        Unit direction in the plane: the plane's own X axis. Its Y axis is
        normal cross x_dir.
    extent_x
        How far the face reaches along x_dir from origin, in meters
    extent_y
        How far the face reaches along the plane's Y axis from origin, in
        meters
    """

    face_index: int
    origin: Point
    normal: Direction
    x_dir: Direction
    extent_x: Range
    extent_y: Range


@dataclass(frozen=True)
class Cylinder:
    """
    A face on a cylinder.

    Attributes
    ----------
    face_index
        Position of this face in the `faces` array, which is the value
        `faceIndices` holds for its triangles
    origin
        A point on the axis, from which heights are measured
    axis
        Unit direction of the axis
    x_dir
        Unit direction perpendicular to the axis from which angles are measured
    radius
        Distance from the axis to the surface, in meters
    extent_angle
        The angles the face covers around the axis, measured from x_dir, in
        radians. A full cylinder covers 2 pi.
    extent_height
        How far the face reaches along the axis from origin, in meters
    """

    face_index: int
    origin: Point
    axis: Direction
    x_dir: Direction
    radius: float
    extent_angle: Range
    extent_height: Range


@dataclass(frozen=True)
class Cone:
    """
    A face on a cone.

    Attributes
    ----------
    face_index
        Position of this face in the `faces` array, which is the value
        `faceIndices` holds for its triangles
    apex
        The point where the cone comes to a tip. The face itself usually does
        not reach it.
    axis
        Unit direction of the axis
    x_dir
        Unit direction perpendicular to the axis from which angles are measured
    semi_angle
        Angle between the axis and the cone's side, in radians. Positive if the
        cone widens along the axis direction and negative if it narrows.
    ref_radius
        Radius of the cone's reference circle, in meters. Its center is on the
        axis at apex + axis * ref_radius / tan(semi_angle).
    extent_angle
        The angles the face covers around the axis, measured from x_dir, in
        radians
    extent_distance
        How far the face reaches along the cone's slanted side, measured from
        the reference circle in the direction of the axis, in meters
    """

    face_index: int
    apex: Point
    axis: Direction
    x_dir: Direction
    semi_angle: float
    ref_radius: float
    extent_angle: Range
    extent_distance: Range


@dataclass(frozen=True)
class Sphere:
    """
    A face on a sphere.

    Attributes
    ----------
    face_index
        Position of this face in the `faces` array, which is the value
        `faceIndices` holds for its triangles
    center
        The center of the sphere
    axis
        Unit direction from the center to the sphere's north pole
    x_dir
        Unit direction perpendicular to the axis from which longitude is
        measured
    radius
        Distance from the center to the surface, in meters
    extent_longitude
        The angles the face covers around the axis, measured from x_dir, in
        radians
    extent_latitude
        The angles the face covers above the equator, toward the axis, in
        radians: from -pi/2 at the south pole to pi/2 at the north pole
    """

    face_index: int
    center: Point
    axis: Direction
    x_dir: Direction
    radius: float
    extent_longitude: Range
    extent_latitude: Range


@dataclass(frozen=True)
class Torus:
    """
    A face on a torus.

    Attributes
    ----------
    face_index
        Position of this face in the `faces` array, which is the value
        `faceIndices` holds for its triangles
    center
        The center of the torus, on its axis
    axis
        Unit direction of the axis the tube circles around
    x_dir
        Unit direction perpendicular to the axis from which angles are measured
    major_radius
        Distance from the axis to the middle of the tube, in meters
    minor_radius
        Radius of the tube, in meters
    extent_major_angle
        The angles the face covers around the axis, measured from x_dir, in
        radians
    extent_minor_angle
        The angles the face covers around the tube, in radians: 0 is the outer
        edge of the torus and the angle grows toward the axis direction
    """

    face_index: int
    center: Point
    axis: Direction
    x_dir: Direction
    major_radius: float
    minor_radius: float
    extent_major_angle: Range
    extent_minor_angle: Range


BrepPrimitive = Union[Plane, Cylinder, Cone, Sphere, Torus]

# the `type` of a face in the extension -> its dataclass
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
    Parse one face of the TM_brep_faces extension into its dataclass.

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
