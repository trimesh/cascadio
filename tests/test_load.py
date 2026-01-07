import os
import sys
import tempfile
from io import BytesIO

import cascadio
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
    assert len(glb_data) > 0

    # Load with trimesh to verify it's valid
    scene = trimesh.load_scene(file_obj=BytesIO(glb_data), file_type="glb")
    assert len(scene.geometry) == 1


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
    assert len(glb_data) > 0

    # Load with trimesh to verify it's valid and is one mesh
    scene = trimesh.load_scene(file_obj=BytesIO(glb_data), file_type="glb")

    # With stitching and merge_primitives, we should get a single unified mesh
    assert len(scene.geometry) == 1
    print(f"IGES loaded successfully as {len(scene.geometry)} mesh(es)")


def test_load_iges_file_extensions():
    """Test that both 'igs' and 'iges' file type strings work."""
    infile = os.path.join(cwd, "models", "microstrip.igs")
    assert os.path.exists(infile)

    with open(infile, "rb") as f:
        iges_data = f.read()

    # Test with 'igs' extension
    glb_data1 = cascadio.load(iges_data, file_type="igs")
    assert len(glb_data1) > 0

    # Test with 'iges' extension
    glb_data2 = cascadio.load(iges_data, file_type="iges")
    assert len(glb_data2) > 0

    # Test with 'IGES' (uppercase)
    glb_data3 = cascadio.load(iges_data, file_type="IGES")
    assert len(glb_data3) > 0


def test_step_to_glb():
    """Test the step_to_glb function with file paths."""
    step_file = os.path.join(cwd, "models", "featuretype.STEP")
    assert os.path.exists(step_file)

    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "output.glb")
        cascadio.step_to_glb(step_file, outfile, tol_linear=0.1, tol_angular=0.5)
        assert os.path.exists(outfile)

        scene = trimesh.load(outfile)
        assert len(scene.geometry) == 1


def test_step_to_obj():
    """Test the step_to_obj function with file paths."""
    step_file = os.path.join(cwd, "models", "featuretype.STEP")
    assert os.path.exists(step_file)

    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "output.obj")
        cascadio.step_to_obj(step_file, outfile, tol_linear=0.1, tol_angular=0.5)
        assert os.path.exists(outfile)

        mesh = trimesh.load(outfile)
        assert mesh.vertices is not None
        assert len(mesh.vertices) > 0


if __name__ == "__main__":
    test_load_step_bytes()
    test_load_iges_bytes()
    test_load_iges_file_extensions()
    test_step_to_glb()
    test_step_to_obj()
    print("All tests passed!")
