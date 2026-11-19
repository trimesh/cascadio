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
          "faces": [{"type": "cylinder", "face_index": 0, "origin": [0,0,0], "axis": [0,0,1], "radius": 0.005, "extent_angle": [0, 6.28], "extent_height": [0, 0.01]}],
          "materials": [{"name": "Steel", "density": 7850.0}]
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
| `materials` | array | Every material the source model defines; empty if it defines none. The list describes the whole model, so it is the same on each primitive |

All three are required, and no other fields are allowed, here or inside a face or a material. A writer that adds a field the schema does not describe produces an invalid file.

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

## Materials

Each entry is either a physical material or a visual one, as the source file defined it. Every field is optional.

- Physical: `name`, `description`, `density`, `density_name` (what the value measures and its unit), `density_value_type`
- Visual: `name`, `base_color` (RGBA), `alpha_cutoff`, `pbr` (`base_color`, `metallic`, `roughness`, `refraction_index`, `emissive_factor`), `common` (`ambient_color`, `diffuse_color`, `specular_color`, `emissive_color`, `shininess`, `transparency`)

## Schema

See [`schema/mesh.primitive.TM_brep_faces.schema.json`](schema/mesh.primitive.TM_brep_faces.schema.json) for the JSON Schema. It is the single source of truth: cascadio generates the code that writes this extension from it. All coordinates in meters, same system as mesh vertices.
