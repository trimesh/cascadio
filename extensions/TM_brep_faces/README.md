# TM_brep_faces

## Contributors

- Mike Dawson-Haggerty, Trimesh
- [Your name here]

## Status

Draft

## Dependencies

Written against glTF 2.0 specification.

## Overview

This extension adds per-triangle BREP (Boundary Representation) face indices and optional analytic surface definitions to mesh primitives. It enables consumers to map tessellated triangles back to their source CAD faces and access the underlying analytic geometry.

## Motivation

CAD models are defined using BREP with analytic surfaces (planes, cylinders, cones, spheres, tori, NURBS). When exported to glTF, this information is lost during tessellation. This extension preserves:

1. **Face-triangle mapping**: Which BREP face each triangle came from
2. **Surface geometry**: The analytic definition of each face (optional)

Use cases:
- Feature recognition on tessellated CAD models
- Fitting analytic primitives to mesh regions
- Quality inspection against nominal geometry
- Selective refinement of specific faces
- CAD-aware mesh processing

## Extension Declaration

```json
{
  "extensionsUsed": ["TM_brep_faces"]
}
```

## Mesh Primitive Extension

The extension is added to a mesh primitive:

```json
{
  "meshes": [{
    "primitives": [{
      "attributes": {
        "POSITION": 0,
        "NORMAL": 1
      },
      "indices": 2,
      "extensions": {
        "TM_brep_faces": {
          "faceIndices": 3,
          "faces": [
            {
              "type": "plane",
              "origin": [0.0, 0.0, 0.0],
              "normal": [0.0, 0.0, 1.0]
            },
            {
              "type": "cylinder",
              "origin": [0.0, 0.0, 0.0],
              "axis": [0.0, 0.0, 1.0],
              "radius": 0.005
            }
          ]
        }
      }
    }]
  }]
}
```

## Properties

### faceIndices

**Type**: `integer` (accessor index)  
**Required**: Yes

Index of an accessor containing per-triangle face indices. The accessor:
- **MUST** have `type` of `"SCALAR"`
- **MUST** have `componentType` of `5121` (UNSIGNED_BYTE), `5123` (UNSIGNED_SHORT), or `5125` (UNSIGNED_INT)
- **MUST** have `count` equal to the number of triangles (i.e., `primitive.indices.count / 3` for indexed geometry, or `POSITION.count / 3` for non-indexed)
- Values **MUST** be valid indices into the `faces` array, or the reserved value indicating an unmapped triangle

The reserved "unmapped" value is the maximum value for the component type (255, 65535, or 4294967295).

### faces

**Type**: `array` of face objects  
**Required**: No (if omitted, only face indices are provided without surface definitions)

Array of face definitions. Each face object has a required `type` property and type-specific parameters.

## Face Types

### plane

An infinite plane defined by a point and normal.

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `type` | `string` | Yes | Must be `"plane"` |
| `origin` | `number[3]` | Yes | A point on the plane |
| `normal` | `number[3]` | Yes | Unit normal vector |

### cylinder

An infinite cylinder defined by axis and radius.

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `type` | `string` | Yes | Must be `"cylinder"` |
| `origin` | `number[3]` | Yes | A point on the axis |
| `axis` | `number[3]` | Yes | Unit direction vector of axis |
| `radius` | `number` | Yes | Radius (must be > 0) |

### cone

A cone defined by apex, axis, and half-angle.

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `type` | `string` | Yes | Must be `"cone"` |
| `apex` | `number[3]` | Yes | The apex point |
| `axis` | `number[3]` | Yes | Unit direction vector (from apex) |
| `halfAngle` | `number` | Yes | Half-angle in radians (0 < angle < π/2) |

### sphere

A sphere defined by center and radius.

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `type` | `string` | Yes | Must be `"sphere"` |
| `center` | `number[3]` | Yes | Center point |
| `radius` | `number` | Yes | Radius (must be > 0) |

### torus

A torus defined by center, axis, major and minor radii.

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `type` | `string` | Yes | Must be `"torus"` |
| `center` | `number[3]` | Yes | Center point |
| `axis` | `number[3]` | Yes | Unit normal to the torus plane |
| `majorRadius` | `number` | Yes | Distance from center to tube center |
| `minorRadius` | `number` | Yes | Radius of the tube |

### bspline

A B-spline surface (trimmed or untrimmed). Control points and knots stored in accessors.

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `type` | `string` | Yes | Must be `"bspline"` |
| `controlPoints` | `integer` | Yes | Accessor index for control point grid (VEC3 or VEC4 for rational) |
| `controlPointsU` | `integer` | Yes | Number of control points in U direction |
| `controlPointsV` | `integer` | Yes | Number of control points in V direction |
| `degreeU` | `integer` | Yes | Degree in U direction |
| `degreeV` | `integer` | Yes | Degree in V direction |
| `knotsU` | `integer` | Yes | Accessor index for U knot vector (SCALAR) |
| `knotsV` | `integer` | Yes | Accessor index for V knot vector (SCALAR) |

### other

A face with unknown or unsupported surface type.

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `type` | `string` | Yes | Must be `"other"` |
| `surfaceType` | `string` | No | Original surface type name if known |

## Example

A cylinder with two planar end caps:

```json
{
  "asset": {"version": "2.0"},
  "extensionsUsed": ["TM_brep_faces"],
  "buffers": [{"byteLength": 12345}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 3600},
    {"buffer": 0, "byteOffset": 3600, "byteLength": 3600},
    {"buffer": 0, "byteOffset": 7200, "byteLength": 1800},
    {"buffer": 0, "byteOffset": 9000, "byteLength": 200}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 300, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 300, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5123, "count": 900, "type": "SCALAR"},
    {"bufferView": 3, "componentType": 5121, "count": 300, "type": "SCALAR"}
  ],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1},
      "indices": 2,
      "extensions": {
        "TM_brep_faces": {
          "faceIndices": 3,
          "faces": [
            {
              "type": "cylinder",
              "origin": [0.0, 0.0, 0.0],
              "axis": [0.0, 0.0, 1.0],
              "radius": 0.005
            },
            {
              "type": "plane",
              "origin": [0.0, 0.0, 0.0],
              "normal": [0.0, 0.0, -1.0]
            },
            {
              "type": "plane", 
              "origin": [0.0, 0.0, 0.01],
              "normal": [0.0, 0.0, 1.0]
            }
          ]
        }
      }
    }]
  }],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
}
```

## Coordinate System

All geometric parameters (origins, axes, centers, control points) are in the same coordinate system as the mesh vertex positions. If the glTF uses a coordinate system transform, the face geometry is subject to the same transform.

Units are meters, consistent with glTF convention.

## Implementation Notes

### Exporters (e.g., Open CASCADE)

1. During tessellation, track which BREP face each triangle originates from
2. Build face index array with one entry per triangle
3. Extract analytic surface parameters from each BREP face
4. Transform surface parameters to glTF coordinate system (Y-up, meters)
5. Write accessor for face indices
6. Populate extension JSON

### Importers (e.g., trimesh)

1. Check for `TM_brep_faces` in primitive extensions
2. Load face indices accessor
3. Group triangles by face index
4. Optionally validate mesh vertices against analytic surfaces
5. Use face data for downstream processing

## Schema

### TM_brep_faces

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "TM_brep_faces",
  "type": "object",
  "properties": {
    "faceIndices": {
      "type": "integer",
      "minimum": 0,
      "description": "Index of accessor containing per-triangle face indices"
    },
    "faces": {
      "type": "array",
      "items": {"$ref": "#/$defs/face"},
      "description": "Array of face definitions"
    }
  },
  "required": ["faceIndices"],
  "$defs": {
    "face": {
      "type": "object",
      "required": ["type"],
      "properties": {
        "type": {
          "type": "string",
          "enum": ["plane", "cylinder", "cone", "sphere", "torus", "bspline", "other"]
        }
      },
      "allOf": [
        {
          "if": {"properties": {"type": {"const": "plane"}}},
          "then": {
            "properties": {
              "origin": {"$ref": "#/$defs/vec3"},
              "normal": {"$ref": "#/$defs/vec3"}
            },
            "required": ["origin", "normal"]
          }
        },
        {
          "if": {"properties": {"type": {"const": "cylinder"}}},
          "then": {
            "properties": {
              "origin": {"$ref": "#/$defs/vec3"},
              "axis": {"$ref": "#/$defs/vec3"},
              "radius": {"type": "number", "exclusiveMinimum": 0}
            },
            "required": ["origin", "axis", "radius"]
          }
        },
        {
          "if": {"properties": {"type": {"const": "cone"}}},
          "then": {
            "properties": {
              "apex": {"$ref": "#/$defs/vec3"},
              "axis": {"$ref": "#/$defs/vec3"},
              "halfAngle": {"type": "number", "exclusiveMinimum": 0, "exclusiveMaximum": 1.5707963267948966}
            },
            "required": ["apex", "axis", "halfAngle"]
          }
        },
        {
          "if": {"properties": {"type": {"const": "sphere"}}},
          "then": {
            "properties": {
              "center": {"$ref": "#/$defs/vec3"},
              "radius": {"type": "number", "exclusiveMinimum": 0}
            },
            "required": ["center", "radius"]
          }
        },
        {
          "if": {"properties": {"type": {"const": "torus"}}},
          "then": {
            "properties": {
              "center": {"$ref": "#/$defs/vec3"},
              "axis": {"$ref": "#/$defs/vec3"},
              "majorRadius": {"type": "number", "exclusiveMinimum": 0},
              "minorRadius": {"type": "number", "exclusiveMinimum": 0}
            },
            "required": ["center", "axis", "majorRadius", "minorRadius"]
          }
        }
      ]
    },
    "vec3": {
      "type": "array",
      "items": {"type": "number"},
      "minItems": 3,
      "maxItems": 3
    }
  }
}
```

## Known Implementations

- Open CASCADE Technology (planned)
- trimesh (planned)

## Resources

- [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
- [Open CASCADE BREP documentation](https://dev.opencascade.org/doc/overview/html/occt_user_guides__modeling_data.html)
