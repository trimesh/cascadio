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
    """The deprecated file-based wrapper still works, and warns."""
    step_file = os.path.join(cwd, "models", "featuretype.STEP")
    assert os.path.exists(step_file)

    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "output.glb")
        with pytest.warns(DeprecationWarning, match="cascadio.load"):
            result = cascadio.step_to_glb(
                step_file, outfile, tol_linear=0.1, tol_angular=0.5
            )
        assert result == 0
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
    """The deprecated file-based wrapper still works, and warns."""
    step_file = os.path.join(cwd, "models", "featuretype.STEP")
    assert os.path.exists(step_file)

    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "output.obj")
        with pytest.warns(DeprecationWarning, match="cascadio.load"):
            result = cascadio.step_to_obj(
                step_file, outfile, tol_linear=0.1, tol_angular=0.5
            )
        assert result == 0
        assert os.path.exists(outfile)
        assert os.path.getsize(outfile) > 1000, "Output file suspiciously small"

        mesh = trimesh.load(outfile)
        assert len(mesh.vertices) > 100, "Mesh should have substantial vertices"
        assert len(mesh.faces) > 100, "Mesh should have substantial faces"
        assert mesh.is_winding_consistent, "Mesh should have consistent winding"
        assert mesh.area > 0, "Mesh should have positive surface area"


@pytest.mark.parametrize(
    "name,file_type", [("colored.step", "step"), ("microstrip.igs", "iges")]
)
@pytest.mark.parametrize(
    "limits",
    [{}, {"timeout": None, "memory_limit": None}],
    ids=["worker", "in-process"],
)
def test_load_zstd(name, file_type, limits):
    """Zstandard-compressed input is detected and gives the same GLB."""
    models = os.path.join(cwd, "models")
    with open(os.path.join(models, name), "rb") as f:
        plain = f.read()
    with open(os.path.join(models, name + ".zst"), "rb") as f:
        packed = f.read()
    # colored.step.zst was compressed from a pipe, so its frame does not
    # record the unpacked size; microstrip.igs.zst does
    assert packed[:4] == b"\x28\xb5\x2f\xfd"
    assert len(packed) < len(plain)

    expected = cascadio.load(plain, file_type=file_type, **limits)
    assert cascadio.load(packed, file_type=file_type, **limits) == expected

    # two frames back to back are one valid stream; here the second frame
    # is empty, as written by some tools when they flush
    empty_frame = b"\x28\xb5\x2f\xfd\x24\x00\x01\x00\x00\x99\xe9\xd8\x51"
    assert (
        cascadio.load(packed + empty_frame, file_type=file_type, **limits) == expected
    )


@pytest.mark.parametrize(
    "limits",
    [{}, {"timeout": None, "memory_limit": None}],
    ids=["worker", "in-process"],
)
def test_load_zstd_damaged(limits):
    """Cut-short or corrupt Zstandard data raises ValueError."""
    with open(os.path.join(cwd, "models", "colored.step.zst"), "rb") as f:
        packed = f.read()

    with pytest.raises(ValueError, match="Zstandard"):
        cascadio.load(packed[: len(packed) // 2], **limits)

    corrupt = bytearray(packed)
    corrupt[len(corrupt) // 2] ^= 0xFF
    with pytest.raises(ValueError):
        cascadio.load(bytes(corrupt), **limits)

    # only the magic number
    with pytest.raises(ValueError, match="Zstandard"):
        cascadio.load(packed[:4], **limits)
