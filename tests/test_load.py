import os
import tempfile
from io import BytesIO

import cascadio
import pytest
import trimesh

cwd = os.path.abspath(os.path.dirname(__file__))


def test_load_step_bytes():
    """Test loading a STEP file using the new load() function."""
    infile = os.path.join(cwd, "models", "featuretype.STEP")
    assert os.path.exists(infile)

    # Read STEP file as bytes
    with open(infile, "rb") as f:
        step_data = f.read()

    # Convert using the new load() interface
    glb_data = cascadio.load(step_data, file_type="step")
    assert len(glb_data) > 1000, "GLB output suspiciously small"

    # Load with trimesh to verify it's valid
    scene = trimesh.load_scene(file_obj=BytesIO(glb_data), file_type="glb")
    assert len(scene.geometry) == 1

    # Verify mesh is valid
    mesh = list(scene.geometry.values())[0]
    assert len(mesh.vertices) > 100, "Mesh should have substantial vertices"
    assert len(mesh.faces) > 100, "Mesh should have substantial faces"
    assert mesh.is_winding_consistent, "Mesh should have consistent winding"


def test_load_iges_bytes():
    """Test loading an IGES file using the new load() function."""
    infile = os.path.join(cwd, "models", "microstrip.igs")
    assert os.path.exists(infile)

    # Read IGES file as bytes
    with open(infile, "rb") as f:
        iges_data = f.read()

    # Convert using the new load() interface with merge_primitives=True
    # This should stitch the surfaces together into one mesh
    glb_data = cascadio.load(
        iges_data,
        file_type="iges",
        tol_linear=0.01,
        tol_angular=0.5,
        merge_primitives=True,
    )
    assert len(glb_data) > 1000, "GLB output suspiciously small"

    # Load with trimesh to verify it's valid and is one mesh
    scene = trimesh.load_scene(file_obj=BytesIO(glb_data), file_type="glb")

    # With stitching and merge_primitives, we should get a single unified mesh
    assert len(scene.geometry) == 1

    # Verify mesh has content
    mesh = list(scene.geometry.values())[0]
    assert len(mesh.vertices) > 10, "Mesh should have vertices"
    assert len(mesh.faces) > 10, "Mesh should have faces"


@pytest.mark.parametrize("file_type", ["igs", "iges", "IGES"])
def test_load_iges_file_extensions(file_type):
    """Test that 'igs', 'iges', and 'IGES' file type strings all work."""
    infile = os.path.join(cwd, "models", "microstrip.igs")
    assert os.path.exists(infile)

    with open(infile, "rb") as f:
        iges_data = f.read()

    glb_data = cascadio.load(iges_data, file_type=file_type)
    assert len(glb_data) > 1000, (
        f"GLB output for file_type={file_type} suspiciously small"
    )

    # Verify it loads as valid GLB
    scene = trimesh.load_scene(file_obj=BytesIO(glb_data), file_type="glb")
    assert len(scene.geometry) >= 1, f"No geometry for file_type={file_type}"


def test_step_to_glb():
    """Test the step_to_glb function with file paths."""
    step_file = os.path.join(cwd, "models", "featuretype.STEP")
    assert os.path.exists(step_file)

    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "output.glb")
        cascadio.step_to_glb(step_file, outfile, tol_linear=0.1, tol_angular=0.5)
        assert os.path.exists(outfile)
        assert os.path.getsize(outfile) > 1000, "Output file suspiciously small"

        scene = trimesh.load(outfile)
        assert len(scene.geometry) == 1

        # Verify mesh is valid
        mesh = list(scene.geometry.values())[0]
        assert len(mesh.vertices) > 100, "Mesh should have substantial vertices"
        assert len(mesh.faces) > 100, "Mesh should have substantial faces"
        assert mesh.is_winding_consistent, "Mesh should have consistent winding"


def test_step_to_obj():
    """Test the step_to_obj function with file paths."""
    step_file = os.path.join(cwd, "models", "featuretype.STEP")
    assert os.path.exists(step_file)

    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "output.obj")
        cascadio.step_to_obj(step_file, outfile, tol_linear=0.1, tol_angular=0.5)
        assert os.path.exists(outfile)
        assert os.path.getsize(outfile) > 1000, "Output file suspiciously small"

        mesh = trimesh.load(outfile)
        assert len(mesh.vertices) > 100, "Mesh should have substantial vertices"
        assert len(mesh.faces) > 100, "Mesh should have substantial faces"
        assert mesh.is_winding_consistent, "Mesh should have consistent winding"
        assert mesh.area > 0, "Mesh should have positive surface area"


if __name__ == "__main__":
    test_load_step_bytes()
    test_load_iges_bytes()
    test_load_iges_file_extensions()
    test_step_to_glb()
    test_step_to_obj()
    print("All tests passed!")
