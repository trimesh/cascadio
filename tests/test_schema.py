"""
Test that cascadio output validates against the TM_brep_faces JSON schema.
"""

import json
import os
import struct
import unittest
from pathlib import Path

try:
    import jsonschema
    from referencing import Registry, Resource
    from referencing.jsonschema import DRAFT202012
    HAS_JSONSCHEMA = True
except ImportError:
    HAS_JSONSCHEMA = False


# Path to schema files
SCHEMA_DIR = Path(__file__).parent.parent / "extensions" / "TM_brep_faces" / "schema"


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

    # Parse GLB header
    magic = data[0:4]
    if magic != b"glTF":
        raise ValueError("Not a valid GLB file")

    chunk_length = struct.unpack("<I", data[12:16])[0]
    json_data = data[20:20 + chunk_length].decode("utf-8")
    return json.loads(json_data)


@unittest.skipIf(not HAS_JSONSCHEMA, "jsonschema not installed")
class TestBrepFacesSchema(unittest.TestCase):
    """Test TM_brep_faces extension output against JSON schema."""

    @classmethod
    def setUpClass(cls):
        """Load schemas and set up validator."""
        cls.schemas = load_schemas()
        cls.registry = create_registry(cls.schemas)
        cls.face_schema = cls.schemas["face.schema.json"]
        cls.extension_schema = cls.schemas["mesh.primitive.TM_brep_faces.schema.json"]

    def get_validator(self, schema):
        """Create a validator with the registry for $ref resolution."""
        return jsonschema.Draft202012Validator(
            schema,
            registry=self.registry,
        )

    def test_plane_face_validates(self):
        """Test that a plane face validates against schema."""
        plane = {
            "type": "plane",
            "origin": [0.0, 0.0, 0.0],
            "normal": [0.0, 0.0, 1.0],
            "x_dir": [1.0, 0.0, 0.0],
            "extent_x": [-0.5, 0.5],
            "extent_y": [-0.5, 0.5],
        }
        validator = self.get_validator(self.face_schema)
        validator.validate(plane)

    def test_cylinder_face_validates(self):
        """Test that a cylinder face validates against schema."""
        cylinder = {
            "type": "cylinder",
            "origin": [0.0, 0.0, 0.0],
            "axis": [0.0, 0.0, 1.0],
            "radius": 0.5,
            "extent_angle": [0.0, 6.283],
            "extent_height": [-0.5, 0.5],
        }
        validator = self.get_validator(self.face_schema)
        validator.validate(cylinder)

    def test_cone_face_validates(self):
        """Test that a cone face validates against schema."""
        cone = {
            "type": "cone",
            "apex": [0.0, 0.0, 0.0],
            "axis": [0.0, 0.0, 1.0],
            "half_angle": 0.785,
            "ref_radius": 0.5,
            "extent_angle": [0.0, 6.283],
            "extent_distance": [0.1, 1.0],
        }
        validator = self.get_validator(self.face_schema)
        validator.validate(cone)

    def test_sphere_face_validates(self):
        """Test that a sphere face validates against schema."""
        sphere = {
            "type": "sphere",
            "center": [0.0, 0.0, 0.0],
            "radius": 0.5,
            "extent_longitude": [0.0, 6.283],
            "extent_latitude": [-1.57, 1.57],
        }
        validator = self.get_validator(self.face_schema)
        validator.validate(sphere)

    def test_torus_face_validates(self):
        """Test that a torus face validates against schema."""
        torus = {
            "type": "torus",
            "center": [0.0, 0.0, 0.0],
            "axis": [0.0, 0.0, 1.0],
            "major_radius": 1.0,
            "minor_radius": 0.25,
            "extent_major_angle": [0.0, 6.283],
            "extent_minor_angle": [0.0, 6.283],
        }
        validator = self.get_validator(self.face_schema)
        validator.validate(torus)

    def test_extension_validates(self):
        """Test that a full TM_brep_faces extension validates."""
        extension = {
            "faceIndices": 3,
            "faces": [
                {
                    "type": "cylinder",
                    "origin": [0.0, 0.0, 0.0],
                    "axis": [0.0, 0.0, 1.0],
                    "radius": 0.005,
                    "extent_angle": [0.0, 6.283],
                    "extent_height": [0.0, 0.01],
                },
                {
                    "type": "plane",
                    "origin": [0.0, 0.0, 0.0],
                    "normal": [0.0, 0.0, -1.0],
                    "x_dir": [1.0, 0.0, 0.0],
                    "extent_x": [-0.005, 0.005],
                    "extent_y": [-0.005, 0.005],
                },
            ],
        }
        validator = self.get_validator(self.extension_schema)
        validator.validate(extension)

    def test_invalid_plane_missing_fields(self):
        """Test that a plane missing required fields fails validation."""
        invalid_plane = {
            "type": "plane",
            "origin": [0.0, 0.0, 0.0],
            # missing normal, x_dir, extent_x, extent_y
        }
        validator = self.get_validator(self.face_schema)
        with self.assertRaises(jsonschema.ValidationError):
            validator.validate(invalid_plane)

    def test_invalid_radius_zero(self):
        """Test that zero radius fails validation."""
        invalid_cylinder = {
            "type": "cylinder",
            "origin": [0.0, 0.0, 0.0],
            "axis": [0.0, 0.0, 1.0],
            "radius": 0,  # Invalid: must be > 0
            "extent_angle": [0.0, 6.283],
            "extent_height": [-0.5, 0.5],
        }
        validator = self.get_validator(self.face_schema)
        with self.assertRaises(jsonschema.ValidationError):
            validator.validate(invalid_cylinder)

    def test_invalid_vec3_wrong_length(self):
        """Test that a vec3 with wrong length fails validation."""
        invalid_plane = {
            "type": "plane",
            "origin": [0.0, 0.0],  # Invalid: should be 3 elements
            "normal": [0.0, 0.0, 1.0],
            "x_dir": [1.0, 0.0, 0.0],
            "extent_x": [-0.5, 0.5],
            "extent_y": [-0.5, 0.5],
        }
        validator = self.get_validator(self.face_schema)
        with self.assertRaises(jsonschema.ValidationError):
            validator.validate(invalid_plane)


@unittest.skipIf(not HAS_JSONSCHEMA, "jsonschema not installed")
class TestCascadioOutputSchema(unittest.TestCase):
    """Test actual cascadio output validates against schema."""

    @classmethod
    def setUpClass(cls):
        """Set up cascadio and schema validation."""
        try:
            import cascadio
            cls.cascadio = cascadio
        except ImportError:
            cls.cascadio = None
            return

        cls.schemas = load_schemas()
        cls.registry = create_registry(cls.schemas)
        cls.face_schema = cls.schemas["face.schema.json"]

        # Find test STEP files
        cls.test_dir = Path(__file__).parent / "models"
        cls.step_files = list(cls.test_dir.glob("*.step")) + list(cls.test_dir.glob("*.stp"))

    def get_validator(self, schema):
        """Create a validator with the registry for $ref resolution."""
        return jsonschema.Draft202012Validator(
            schema,
            registry=self.registry,
        )

    def test_cascadio_output_validates(self):
        """Test that cascadio GLB output validates against schema."""
        if self.cascadio is None:
            self.skipTest("cascadio not installed")

        if not self.step_files:
            self.skipTest("No STEP files found for testing")

        import tempfile

        step_file = self.step_files[0]

        with tempfile.NamedTemporaryFile(suffix=".glb", delete=False) as f:
            output_path = f.name

        try:
            # Convert with BREP data
            self.cascadio.step_to_glb(
                str(step_file),
                output_path,
                include_brep=True,
            )

            # Extract JSON tree
            tree = extract_glb_json(output_path)

            # Find TM_brep_faces extension
            for mesh in tree.get("meshes", []):
                for prim in mesh.get("primitives", []):
                    extensions = prim.get("extensions", {})
                    if "TM_brep_faces" in extensions:
                        brep_data = extensions["TM_brep_faces"]

                        # Validate each face
                        validator = self.get_validator(self.face_schema)
                        for face in brep_data.get("faces", []):
                            # Skip filtered faces (null) and non-analytical surfaces (type: null)
                            if face is not None and face.get("type") is not None:
                                validator.validate(face)

        finally:
            if os.path.exists(output_path):
                os.unlink(output_path)


if __name__ == "__main__":
    unittest.main()
