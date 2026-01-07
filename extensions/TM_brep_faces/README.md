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
| `faces` | array | Face definitions with `type` and geometry parameters |

## Face Types

### plane
`origin`, `normal`, `x_dir` (vec3), `extent_x`, `extent_y` (bounds in meters)

### cylinder
`origin`, `axis` (vec3), `radius` (meters), `extent_angle` (radians), `extent_height` (meters)

### cone
`apex`, `axis` (vec3), `half_angle` (radians), `ref_radius` (meters), `extent_angle` (radians), `extent_distance` (meters)

### sphere
`center` (vec3), `radius` (meters), `extent_longitude`, `extent_latitude` (radians)

### torus
`center`, `axis` (vec3), `major_radius`, `minor_radius` (meters), `extent_major_angle`, `extent_minor_angle` (radians)

## Schema

See `schema/` for JSON Schema definitions. All coordinates in meters, same system as mesh vertices.
