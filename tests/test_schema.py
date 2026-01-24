"""
Test that cascadio output validates against the TM_brep_faces JSON schema.
"""

import json
import os
import struct
import tempfile
from pathlib import Path

import cascadio
import jsonschema
import pytest
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012


# Path to schema files
SCHEMA_DIR = Path(__file__).parent.parent / "extensions" / "TM_brep_faces" / "schema"
MODELS_DIR = Path(__file__).parent / "models"


def load_schemas():
    """Load all schema files and create a registry for $ref resolution."""
    schemas = {}
    for schema_file in SCHEMA_DIR.glob("*.json"):
        with open(schema_file) as f:
            schema = json.load(f)
            schemas[schema_file.name] = schema
    return schemas


def create_registry(schemas):
    """Create a jsonschema registry for resolving $ref references."""
    resources = []
    for name, schema in schemas.items():
        resource = Resource.from_contents(schema, default_specification=DRAFT202012)
        resources.append((name, resource))
    return Registry().with_resources(resources)


def extract_glb_json(glb_path):
    """Extract JSON tree from a GLB file."""
    with open(glb_path, "rb") as f:
        data = f.read()

    magic = data[0:4]
    if magic != b"glTF":
        raise ValueError("Not a valid GLB file")

    chunk_length = struct.unpack("<I", data[12:16])[0]
    json_data = data[20 : 20 + chunk_length].decode("utf-8")
    return json.loads(json_data)


# Load schemas once at module level
SCHEMAS = load_schemas()
REGISTRY = create_registry(SCHEMAS)
FACE_SCHEMA = SCHEMAS["face.schema.json"]
EXTENSION_SCHEMA = SCHEMAS["mesh.primitive.TM_brep_faces.schema.json"]


def get_validator(schema):
    """Create a validator with the registry for $ref resolution."""
    return jsonschema.Draft202012Validator(schema, registry=REGISTRY)


# --- Face schema validation tests ---


def test_plane_face_validates():
    """Test that a plane face validates against schema."""
    plane = {
        "type": "plane",
        "face_index": 0,
        "extent_x": [-0.5, 0.5],
        "extent_y": [-0.5, 0.5],
        "origin": [0.0, 0.0, 0.0],
        "normal": [0.0, 0.0, 1.0],
        "x_dir": [1.0, 0.0, 0.0],
    }
    get_validator(FACE_SCHEMA).validate(plane)


def test_cylinder_face_validates():
    """Test that a cylinder face validates against schema."""
    cylinder = {
        "type": "cylinder",
        "face_index": 1,
        "extent_angle": [0.0, 6.283],
        "extent_height": [-0.5, 0.5],
        "origin": [0.0, 0.0, 0.0],
        "axis": [0.0, 0.0, 1.0],
        "radius": 0.5,
    }
    get_validator(FACE_SCHEMA).validate(cylinder)


def test_cone_face_validates():
    """Test that a cone face validates against schema."""
    cone = {
        "type": "cone",
        "face_index": 2,
        "extent_angle": [0.0, 6.283],
        "extent_distance": [0.1, 1.0],
        "apex": [0.0, 0.0, 0.0],
        "axis": [0.0, 0.0, 1.0],
        "semi_angle": 0.785,
        "ref_radius": 0.5,
    }
    get_validator(FACE_SCHEMA).validate(cone)


def test_sphere_face_validates():
    """Test that a sphere face validates against schema."""
    sphere = {
        "type": "sphere",
        "face_index": 3,
        "extent_longitude": [0.0, 6.283],
        "extent_latitude": [-1.57, 1.57],
        "center": [0.0, 0.0, 0.0],
        "radius": 0.5,
    }
    get_validator(FACE_SCHEMA).validate(sphere)


def test_torus_face_validates():
    """Test that a torus face validates against schema."""
    torus = {
        "type": "torus",
        "face_index": 4,
        "extent_major_angle": [0.0, 6.283],
        "extent_minor_angle": [0.0, 6.283],
        "center": [0.0, 0.0, 0.0],
        "axis": [0.0, 0.0, 1.0],
        "major_radius": 1.0,
        "minor_radius": 0.25,
    }
    get_validator(FACE_SCHEMA).validate(torus)


def test_extension_validates():
    """Test that a full TM_brep_faces extension validates."""
    extension = {
        "faceIndices": 3,
        "faces": [
            {
                "type": "cylinder",
                "face_index": 0,
                "extent_angle": [0.0, 6.283],
                "extent_height": [0.0, 0.01],
                "origin": [0.0, 0.0, 0.0],
                "axis": [0.0, 0.0, 1.0],
                "radius": 0.005,
            },
            {
                "type": "plane",
                "face_index": 1,
                "extent_x": [-0.005, 0.005],
                "extent_y": [-0.005, 0.005],
                "origin": [0.0, 0.0, 0.0],
                "normal": [0.0, 0.0, -1.0],
                "x_dir": [1.0, 0.0, 0.0],
            },
        ],
    }
    get_validator(EXTENSION_SCHEMA).validate(extension)


def test_invalid_plane_missing_fields():
    """Test that a plane missing required fields fails validation."""
    invalid_plane = {
        "type": "plane",
        "face_index": 0,
        "extent_x": [-0.5, 0.5],
        "extent_y": [-0.5, 0.5],
        "origin": [0.0, 0.0, 0.0],
        # missing normal, x_dir
    }
    with pytest.raises(jsonschema.ValidationError):
        get_validator(FACE_SCHEMA).validate(invalid_plane)


def test_invalid_radius_zero():
    """Test that zero radius fails validation."""
    invalid_cylinder = {
        "type": "cylinder",
        "face_index": 0,
        "extent_angle": [0.0, 6.283],
        "extent_height": [-0.5, 0.5],
        "origin": [0.0, 0.0, 0.0],
        "axis": [0.0, 0.0, 1.0],
        "radius": 0,  # Invalid: must be > 0
    }
    with pytest.raises(jsonschema.ValidationError):
        get_validator(FACE_SCHEMA).validate(invalid_cylinder)


def test_invalid_vec3_wrong_length():
    """Test that a vec3 with wrong length fails validation."""
    invalid_plane = {
        "type": "plane",
        "face_index": 0,
        "extent_x": [-0.5, 0.5],
        "extent_y": [-0.5, 0.5],
        "origin": [0.0, 0.0],  # Invalid: should be 3 elements
        "normal": [0.0, 0.0, 1.0],
        "x_dir": [1.0, 0.0, 0.0],
    }
    with pytest.raises(jsonschema.ValidationError):
        get_validator(FACE_SCHEMA).validate(invalid_plane)


# --- Cascadio output validation test ---


def test_cascadio_output_validates():
    """Test that cascadio GLB output validates against schema."""
    step_files = list(MODELS_DIR.glob("*.step")) + list(MODELS_DIR.glob("*.stp"))
    assert step_files, "No STEP files found for testing"

    step_file = step_files[0]

    with tempfile.NamedTemporaryFile(suffix=".glb", delete=False) as f:
        output_path = f.name

    try:
        cascadio.step_to_glb(str(step_file), output_path, include_brep=True)

        tree = extract_glb_json(output_path)

        # Find and validate TM_brep_faces extension
        found_extension = False
        for mesh in tree.get("meshes", []):
            for prim in mesh.get("primitives", []):
                extensions = prim.get("extensions", {})
                if "TM_brep_faces" in extensions:
                    found_extension = True
                    brep_data = extensions["TM_brep_faces"]

                    # Validate each face
                    validator = get_validator(FACE_SCHEMA)
                    for face in brep_data.get("faces", []):
                        if face is not None and face.get("type") is not None:
                            validator.validate(face)

        assert found_extension, "TM_brep_faces extension not found in output"

    finally:
        if os.path.exists(output_path):
            os.unlink(output_path)


# --- Schema resolution helpers ---


def resolve_ref(ref_path, schemas):
    """Resolve a $ref path to its actual schema content."""
    if "#" in ref_path:
        file_name, pointer = ref_path.split("#", 1)
        schema = schemas[file_name]
        parts = pointer.strip("/").split("/")
        result = schema
        for part in parts:
            result = result[part]
        return result
    else:
        return schemas[ref_path]


def resolve_refs(obj, schemas):
    """Recursively resolve all $ref references in a schema object."""
    if isinstance(obj, dict):
        if "$ref" in obj:
            resolved = resolve_ref(obj["$ref"], schemas)
            return resolve_refs(resolved, schemas)
        return {k: resolve_refs(v, schemas) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [resolve_refs(item, schemas) for item in obj]
    return obj


def build_resolved_schema():
    """Build and return the fully resolved schema from source files."""
    schemas = load_schemas()
    root_schema = schemas["mesh.primitive.TM_brep_faces.schema.json"]
    return resolve_refs(root_schema, schemas)


# --- Baked schema match test ---


def test_baked_schema_matches_source():
    """Test that packaged schema.json matches dynamically resolved source."""
    baked_path = Path(cascadio.__file__).parent / "schema.json"

    if not baked_path.exists():
        baked_path = Path(__file__).parent.parent / "src" / "cascadio" / "schema.json"

    assert baked_path.exists(), (
        "schema.json not found. Run: python scripts/build_schema.py"
    )

    with open(baked_path) as f:
        baked = json.load(f)

    resolved = build_resolved_schema()

    assert baked == resolved, (
        "Baked schema.json doesn't match source schemas. "
        "Run: python scripts/build_schema.py"
    )
