<!-- Generated from schema/mesh.primitive.TM_brep_faces.schema.json by scripts/gen_schema.py. Do not edit. -->
# TM_brep_faces

Which BREP face each triangle came from, the analytic surface of each face, and the materials of the part.

All positions and lengths are in meters and all directions are unit vectors, in the same coordinate frame as the primitive's POSITION accessor (before any node transform). All angles are in radians and turn counter-clockwise when seen from the tip of the axis they turn about (the right-hand rule).

## Example

```json
{
  "faceIndices": 3,
  "faces": [
    {"type": "cylinder", "face_index": 0, "origin": [0, 0, 0], "axis": [0, 0, 1], "x_dir": [1, 0, 0], "radius": 0.005, "extent_angle": [0, 6.2832], "extent_height": [0, 0.01]},
    null
  ],
  "materials": [{"name": "Steel", "density": 7.85}]
}
```

## Properties

All of these are required, and no other properties are allowed, here or inside a face or a material.

| Field | Type | Meaning |
|---|---|---|
| `faceIndices` | integer | Index of an accessor with one value per triangle: the position in `faces` of the BREP face that triangle came from. The accessor is SCALAR with an unsigned integer component type, and its count equals the number of triangles in the primitive. |
| `faces` | array | One entry per BREP face, in face index order. An entry is null when the face is not one of the analytic surface types, so positions still match face indices. |
| `materials` | list of Materials | The materials of the part this primitive belongs to. Empty if the part has none. A writer that cannot tell which part a material belongs to may list every material of the source model here instead. |

## Faces

One BREP face whose surface is a plane, cylinder, cone, sphere or torus. The surface is infinite or closed; the `extent_` fields say which part of it the face covers. A face can be smaller than its extents if its outline is not rectangular in those two directions.

The value types used below:

- point: A position [x, y, z] in meters, in the frame of the primitive's vertices
- direction: A direction [x, y, z] of length 1, in the frame of the primitive's vertices
- range: [min, max]: the stretch of a surface the face covers along one of the surface's two directions, in meters or radians as the field says

Every face has:

| Field | Type | Meaning |
|---|---|---|
| `type` | string | The kind of surface. It decides which other fields are present. |
| `face_index` | integer | Position of this face in the `faces` array, which is the value `faceIndices` holds for its triangles |

and, depending on `type`:

### plane

| Field | Type | Meaning |
|---|---|---|
| `origin` | point | A point on the plane, from which the extents are measured |
| `normal` | direction | Unit direction perpendicular to the plane |
| `x_dir` | direction | Unit direction in the plane: the plane's own X axis. Its Y axis is normal cross x_dir. |
| `extent_x` | range | How far the face reaches along x_dir from origin, in meters |
| `extent_y` | range | How far the face reaches along the plane's Y axis from origin, in meters |

### cylinder

| Field | Type | Meaning |
|---|---|---|
| `origin` | point | A point on the axis, from which heights are measured |
| `axis` | direction | Unit direction of the axis |
| `x_dir` | direction | Unit direction perpendicular to the axis from which angles are measured |
| `radius` | number | Distance from the axis to the surface, in meters |
| `extent_angle` | range | The angles the face covers around the axis, measured from x_dir, in radians. A full cylinder covers 2 pi. |
| `extent_height` | range | How far the face reaches along the axis from origin, in meters |

### cone

| Field | Type | Meaning |
|---|---|---|
| `apex` | point | The point where the cone comes to a tip. The face itself usually does not reach it. |
| `axis` | direction | Unit direction of the axis |
| `x_dir` | direction | Unit direction perpendicular to the axis from which angles are measured |
| `semi_angle` | number | Angle between the axis and the cone's side, in radians. Positive if the cone widens along the axis direction and negative if it narrows. |
| `ref_radius` | number | Radius of the cone's reference circle, in meters. Its center is on the axis at apex + axis * ref_radius / tan(semi_angle). |
| `extent_angle` | range | The angles the face covers around the axis, measured from x_dir, in radians |
| `extent_distance` | range | How far the face reaches along the cone's slanted side, measured from the reference circle in the direction of the axis, in meters |

### sphere

| Field | Type | Meaning |
|---|---|---|
| `center` | point | The center of the sphere |
| `axis` | direction | Unit direction from the center to the sphere's north pole |
| `x_dir` | direction | Unit direction perpendicular to the axis from which longitude is measured |
| `radius` | number | Distance from the center to the surface, in meters |
| `extent_longitude` | range | The angles the face covers around the axis, measured from x_dir, in radians |
| `extent_latitude` | range | The angles the face covers above the equator, toward the axis, in radians: from -pi/2 at the south pole to pi/2 at the north pole |

### torus

| Field | Type | Meaning |
|---|---|---|
| `center` | point | The center of the torus, on its axis |
| `axis` | direction | Unit direction of the axis the tube circles around |
| `x_dir` | direction | Unit direction perpendicular to the axis from which angles are measured |
| `major_radius` | number | Distance from the axis to the middle of the tube, in meters |
| `minor_radius` | number | Radius of the tube, in meters |
| `extent_major_angle` | range | The angles the face covers around the axis, measured from x_dir, in radians |
| `extent_minor_angle` | range | The angles the face covers around the tube, in radians: 0 is the outer edge of the torus and the angle grows toward the axis direction |


## Materials

Either a physical material (name, density) or a visual one (colors), as the source file defined it. Every field is optional.

| Field | Type | Meaning |
|---|---|---|
| `name` | string | The material's name as written in the source file |
| `description` | string | Free text about the material from the source file |
| `density` | number | Mass per volume as OpenCASCADE reports it. It means grams per cubic centimeter, but ignores a kilo prefix in the source file: a file written in kilograms per cubic meter gives a value 1000 times too small (steel 0.00785 instead of 7.85). |
| `density_name` | string | The label the source file gave the density value, for example 'density measure'. It is not a unit. |
| `density_value_type` | string | The STEP measure type of the density value, for example 'POSITIVE_RATIO_MEASURE' |
| `base_color` | color with alpha | The material's overall color |
| `alpha_cutoff` | number | Alpha below this is drawn as fully transparent, as in glTF's alphaCutoff |
| `pbr` | object | Metallic-roughness parameters, as in glTF |
| `common` | object | Classic ambient/diffuse/specular parameters |

`pbr`: Metallic-roughness parameters, as in glTF

| Field | Type | Meaning |
|---|---|---|
| `base_color` | color with alpha | Base color, as in glTF's baseColorFactor |
| `metallic` | number | 0 for a non-metal to 1 for a metal |
| `roughness` | number | 0 for mirror-smooth to 1 for fully rough |
| `refraction_index` | number | Index of refraction; 1.5 is typical glass or plastic |
| `emissive_factor` | color | Color of light the material gives off, [red, green, blue] |

`common`: Classic ambient/diffuse/specular parameters

| Field | Type | Meaning |
|---|---|---|
| `ambient_color` | color | Color in shadow |
| `diffuse_color` | color | Color under direct light |
| `specular_color` | color | Color of highlights |
| `emissive_color` | color | Color of light the material gives off |
| `shininess` | number | 0 for dull to 1 for a tight, bright highlight |
| `transparency` | number | 0 for opaque to 1 for invisible |

## Schema

[`schema/mesh.primitive.TM_brep_faces.schema.json`](schema/mesh.primitive.TM_brep_faces.schema.json) is the single source of truth. This page, the code in cascadio that writes the extension, and its Python dataclasses are all generated from it.
