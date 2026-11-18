"""
Test that cascadio output validates against the TM_brep_faces JSON schema.
"""

import dataclasses
import json
import os
import struct
import tempfile
from pathlib import Path

import cascadio
import jsonschema
import pytest


# The one schema file. cascadio generates its C++ writer from it.
SCHEMA_PATH = (
    Path(__file__).parent.parent
    / "extensions"
    / "TM_brep_faces"
    / "schema"
    / "mesh.primitive.TM_brep_faces.schema.json"
)
MODELS_DIR = Path(__file__).parent / "models"

EXTENSION_SCHEMA = json.loads(SCHEMA_PATH.read_text())
# a single face, with the shared definitions it refers to
FACE_SCHEMA = {"$ref": "#/$defs/face", "$defs": EXTENSION_SCHEMA["$defs"]}


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


def get_validator(schema):
    return jsonschema.Draft202012Validator(schema)


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
        Path(output_path).write_bytes(
            cascadio.load(step_file.read_bytes(), include_brep=True)
        )

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


# --- The Python side matches the schema ---


def test_dataclasses_match_schema():
    """Each primitive dataclass has exactly the fields the schema defines."""
    face = EXTENSION_SCHEMA["$defs"]["face"]
    cases = {
        case["if"]["properties"]["type"]["const"]: case["then"]["required"]
        for case in face["allOf"]
    }
    assert set(cases) == set(face["properties"]["type"]["enum"])
    assert set(cases) == set(cascadio.primitives.PRIMITIVES)
    assert set(cases) == set(cascadio.BrepType.__args__)

    for name, required in cases.items():
        kind = cascadio.primitives.PRIMITIVES[name]
        names = {f.name for f in dataclasses.fields(kind)}
        assert names == set(required) | {"face_index"}, name


def test_packaged_schema_is_the_source():
    """The schema shipped in the wheel is the same file, not a copy that can drift."""
    packaged = Path(cascadio.__file__).parent / "schema.json"
    if not packaged.exists():
        pytest.skip("editable install: the schema is only copied into wheels")
    assert json.loads(packaged.read_text()) == EXTENSION_SCHEMA


def test_unknown_brep_type_rejected():
    """A typo in `brep_types` is an error instead of silently matching nothing."""
    data = (MODELS_DIR / "featuretype.STEP").read_bytes()
    with pytest.raises(ValueError, match="brep_types"):
        cascadio.load(data, include_brep=True, brep_types={"plane", "cilinder"})
