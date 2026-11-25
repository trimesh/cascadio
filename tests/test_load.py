import subprocess
import sys
import textwrap
from io import BytesIO

import pytest
import trimesh
from conftest import MODELS_DIR, needs_iges

import cascadio


def test_load_step_bytes():
    """Test loading a STEP file using the new load() function."""
    step_data = (MODELS_DIR / "featuretype.STEP").read_bytes()

    # Convert using the new load() interface
    glb_data = cascadio.load(step_data, file_type="step")
    assert len(glb_data) > 1000, "GLB output suspiciously small"

    # Load with trimesh to verify it's valid
    scene = trimesh.load_scene(file_obj=BytesIO(glb_data), file_type="glb")
    assert len(scene.geometry) == 1

    # Verify mesh is valid
    mesh = next(iter(scene.geometry.values()))
    assert len(mesh.vertices) > 100, "Mesh should have substantial vertices"
    assert len(mesh.faces) > 100, "Mesh should have substantial faces"
    assert mesh.is_winding_consistent, "Mesh should have consistent winding"


@needs_iges
def test_load_iges_bytes():
    """Test loading an IGES file using the new load() function."""
    iges_data = (MODELS_DIR / "microstrip.igs").read_bytes()

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
    mesh = next(iter(scene.geometry.values()))
    assert len(mesh.vertices) > 10, "Mesh should have vertices"
    assert len(mesh.faces) > 10, "Mesh should have faces"


@needs_iges
@pytest.mark.parametrize("file_type", ["igs", "iges", "IGES"])
def test_load_iges_file_extensions(file_type):
    """Test that 'igs', 'iges', and 'IGES' file type strings all work."""
    iges_data = (MODELS_DIR / "microstrip.igs").read_bytes()

    glb_data = cascadio.load(iges_data, file_type=file_type)
    assert len(glb_data) > 1000, (
        f"GLB output for file_type={file_type} suspiciously small"
    )

    # Verify it loads as valid GLB
    scene = trimesh.load_scene(file_obj=BytesIO(glb_data), file_type="glb")
    assert len(scene.geometry) >= 1, f"No geometry for file_type={file_type}"


@pytest.mark.parametrize(
    "name,file_type",
    [
        ("colored.step", "step"),
        pytest.param("microstrip.igs", "iges", marks=needs_iges),
    ],
)
def test_load_zstd(name, file_type):
    """Zstandard-compressed input is detected and gives the same GLB."""
    plain = (MODELS_DIR / name).read_bytes()
    packed = (MODELS_DIR / (name + ".zst")).read_bytes()
    # colored.step.zst was compressed from a pipe, so its frame does not
    # record the unpacked size; microstrip.igs.zst does
    assert packed[:4] == b"\x28\xb5\x2f\xfd"
    assert len(packed) < len(plain)

    expected = cascadio.load(plain, file_type=file_type)
    assert cascadio.load(packed, file_type=file_type) == expected

    # two frames back to back are one valid stream; here the second frame
    # is empty, as written by some tools when they flush
    empty_frame = b"\x28\xb5\x2f\xfd\x24\x00\x01\x00\x00\x99\xe9\xd8\x51"
    assert cascadio.load(packed + empty_frame, file_type=file_type) == expected


def test_load_zstd_damaged():
    """Cut-short or corrupt Zstandard data raises ValueError."""
    packed = (MODELS_DIR / "colored.step.zst").read_bytes()

    with pytest.raises(ValueError, match="Zstandard"):
        cascadio.load(packed[: len(packed) // 2])

    corrupt = bytearray(packed)
    corrupt[len(corrupt) // 2] ^= 0xFF
    with pytest.raises(ValueError):
        cascadio.load(bytes(corrupt))

    # only the magic number
    with pytest.raises(ValueError, match="Zstandard"):
        cascadio.load(packed[:4])


def test_step_to_glb(tmp_path):
    """The deprecated file-based stub still works, and warns."""
    step_file = str(MODELS_DIR / "featuretype.STEP")
    outfile = tmp_path / "output.glb"
    with pytest.warns(DeprecationWarning, match="2027-05-22") as record:
        result = cascadio.step_to_glb(
            step_file, str(outfile), tol_linear=0.1, tol_angular=0.5
        )
    assert result == 0
    # the warning points at the caller, not at cascadio
    assert record[0].filename == __file__

    scene = trimesh.load(str(outfile))
    assert len(scene.geometry) == 1
    mesh = next(iter(scene.geometry.values()))
    assert len(mesh.vertices) > 100
    assert len(mesh.faces) > 100
    assert mesh.is_winding_consistent


def test_step_to_glb_replacement(tmp_path):
    """The code in the warning runs, and writes the same GLB as the stub."""
    step_file = str(MODELS_DIR / "multibody.step")
    stub = tmp_path / "stub.glb"
    with pytest.warns(DeprecationWarning) as record:
        cascadio.step_to_glb(
            step_file,
            str(stub),
            tol_linear=0.1,
            node_name_format=cascadio.NodeNameFormat.PRODUCT,
        )
    message = str(record[0].message)
    assert "tol_linear=0.1" in message
    assert "node_name_format=cascadio.NodeNameFormat.PRODUCT" in message

    code = message.split("Replace this call with:\n\n", 1)[1]
    replaced = tmp_path / "replaced.glb"
    code = textwrap.dedent(code).replace(repr(str(stub)), repr(str(replaced)))
    exec(code, {"cascadio": cascadio})
    assert replaced.read_bytes() == stub.read_bytes()


def test_step_to_obj_raises(tmp_path):
    """The removed OBJ stub explains why it is gone, and writes nothing."""
    step_file = str(MODELS_DIR / "colored.step")
    outfile = tmp_path / "out.obj"
    with pytest.raises(NotImplementedError, match=r"removed in cascadio 0\.2") as error:
        cascadio.step_to_obj(step_file, str(outfile), tol_linear=0.1)
    assert "seccomp" in str(error.value)
    assert "tol_linear=0.1" in str(error.value)
    assert list(tmp_path.iterdir()) == []


def test_step_to_obj_replacement(tmp_path):
    """The code in the error runs and writes an OBJ that trimesh loads."""
    step_file = str(MODELS_DIR / "colored.step")
    outfile = tmp_path / "out.obj"
    with pytest.raises(NotImplementedError) as error:
        cascadio.step_to_obj(step_file, str(outfile))
    code = str(error.value).split("Replace this call with:\n\n", 1)[1]
    exec(textwrap.dedent(code), {"cascadio": cascadio})

    # trimesh writes the colors to a .mtl file next to the OBJ
    assert (tmp_path / "material.mtl").exists()
    scene = trimesh.load_scene(str(outfile))
    assert len(scene.geometry) >= 1


def test_step_to_glb_import_is_quiet():
    """Importing cascadio never warns, even with warnings as errors."""
    subprocess.run([sys.executable, "-W", "error", "-c", "import cascadio"], check=True)
