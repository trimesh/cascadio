# TM_brep_faces

Per-triangle BREP face indices and analytic surface definitions for glTF mesh primitives.

## Usage

```json
{
  "extensionsUsed": ["TM_brep_faces"],
  "meshes": [{
    "primitives": [{
      "extensions": {
        "TM_brep_faces": {
          "faceIndices": 3,
          "faces": [{"type": "cylinder", "origin": [0,0,0], "axis": [0,0,1], "radius": 0.005, "extent_angle": [0, 6.28], "extent_height": [0, 0.01]}]
        }
      }
    }]
  }]
}
```

## Properties

| Property | Type | Description |
|----------|------|-------------|
| `faceIndices` | integer | Accessor index for per-triangle face indices (SCALAR, unsigned int) |
| `faces` | array | One entry per face, in face index order: a definition with `type` and geometry parameters, or `null` for a face that is not one of the types below |

## Face Types

### plane
`origin`, `normal`, `x_dir` (vec3), `extent_x`, `extent_y` (bounds in meters)

### cylinder
`origin`, `axis` (vec3), `radius` (meters), `extent_angle` (radians), `extent_height` (meters)

### cone
`apex`, `axis` (vec3), `semi_angle` (radians), `ref_radius` (meters), `extent_angle` (radians), `extent_distance` (meters)

### sphere
`center` (vec3), `radius` (meters), `extent_longitude`, `extent_latitude` (radians)

### torus
`center`, `axis` (vec3), `major_radius`, `minor_radius` (meters), `extent_major_angle`, `extent_minor_angle` (radians)

## Schema

See [`schema/mesh.primitive.TM_brep_faces.schema.json`](schema/mesh.primitive.TM_brep_faces.schema.json) for the JSON Schema. It is the single source of truth: cascadio generates the code that writes this extension from it. All coordinates in meters, same system as mesh vertices.
