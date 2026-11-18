"""
Test that cascadio output validates against the TM_brep_faces JSON schema.
"""

import dataclasses
import json
import struct
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


def extract_glb_json(glb: bytes) -> dict:
    """The JSON chunk of a GLB."""
    assert glb[:4] == b"glTF"
    length = struct.unpack("<I", glb[12:16])[0]
    return json.loads(glb[20 : 20 + length])


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
    """Every face cascadio writes validates against the schema."""
    validator = get_validator(FACE_SCHEMA)
    found = 0
    for model in sorted(MODELS_DIR.glob("*.st*p")):
        tree = extract_glb_json(cascadio.load(model.read_bytes(), include_brep=True))
        for mesh in tree["meshes"]:
            for primitive in mesh["primitives"]:
                extension = primitive.get("extensions", {}).get("TM_brep_faces")
                if extension is None:
                    continue
                get_validator(EXTENSION_SCHEMA).validate(extension)
                for face in extension["faces"]:
                    if face is not None:
                        validator.validate(face)
                        found += 1
    assert found > 100


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
