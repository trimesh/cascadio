import io
import tempfile
import timeit
from pathlib import Path

import cascadio
import numpy as np
import trimesh

MODELS_DIR = Path(__file__).parent / "models"
FEATURE_TYPE_STEP_PATH = MODELS_DIR / "featuretype.STEP"
COLORED_STEP_PATH = MODELS_DIR / "colored.step"
TOL_LINEAR = 0.1
TOL_ANGULAR = 0.5


def test_convert_step_to_glb():
    step_path = FEATURE_TYPE_STEP_PATH

    with tempfile.TemporaryDirectory() as temp_dir:
        glb_path = Path(temp_dir) / step_path.with_suffix(".glb").name
        cascadio.step_to_glb(
            step_path.as_posix(),
            glb_path.as_posix(),
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
        )
        scene = trimesh.load(glb_path, merge_primitives=True)

    assert len(scene.geometry) == 1
    assert np.allclose(scene.extents, np.array([0.127, 0.0635, 0.034925]))


def test_convert_step_to_obj():
    step_path = FEATURE_TYPE_STEP_PATH

    with tempfile.TemporaryDirectory() as temp_dir:
        obj_path = Path(temp_dir) / step_path.with_suffix(".obj").name
        cascadio.step_to_obj(
            step_path.as_posix(),
            obj_path.as_posix(),
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
        )
        mesh = trimesh.load(obj_path, merge_primitives=True)

    assert mesh.mass > 0.0
    assert mesh.volume > 0.0
    assert np.allclose(mesh.extents, np.array([127.0, 63.5, 34.924999]))


def test_convert_step_to_obj_with_colors():
    step_path = COLORED_STEP_PATH

    with tempfile.TemporaryDirectory() as temp_dir:
        obj_path = Path(temp_dir) / step_path.with_suffix(".obj").name
        mtl_path = obj_path.with_suffix(".mtl")
        cascadio.step_to_obj(
            step_path.as_posix(),
            obj_path.as_posix(),
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
            use_colors=True,
        )
        assert mtl_path.exists()
        mesh = trimesh.load(obj_path, merge_primitives=True)

    assert mesh.mass > 0.0
    assert mesh.volume > 0.0
    assert np.allclose(mesh.visual.material.main_color, np.array([202, 225, 255, 255]))


def test_convert_step_to_obj_without_colors():
    step_path = COLORED_STEP_PATH

    with tempfile.TemporaryDirectory() as temp_dir:
        obj_path = Path(temp_dir) / step_path.with_suffix(".obj").name
        mtl_path = obj_path.with_suffix(".mtl")
        cascadio.step_to_obj(
            step_path.as_posix(),
            obj_path.as_posix(),
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
            use_colors=False,
        )
        assert not mtl_path.exists()
        mesh = trimesh.load(obj_path, merge_primitives=True)

    assert mesh.mass > 0.0
    assert mesh.volume > 0.0
    assert np.allclose(mesh.visual.material.main_color, np.array([102, 102, 102, 255]))


def test_convert_step_to_glb_with_brep():
    """Test that include_brep=True embeds BREP primitive data in mesh extras."""
    step_path = FEATURE_TYPE_STEP_PATH

    with tempfile.TemporaryDirectory() as temp_dir:
        glb_path = Path(temp_dir) / step_path.with_suffix(".glb").name
        cascadio.step_to_glb(
            step_path.as_posix(),
            glb_path.as_posix(),
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
            include_brep=True,
        )

        # Load with trimesh
        scene = trimesh.load(glb_path, merge_primitives=True)

    # Should have one mesh
    assert len(scene.geometry) == 1

    # Get the mesh
    mesh = list(scene.geometry.values())[0]

    # Should have brep_faces in mesh metadata
    assert "brep_faces" in mesh.metadata
    brep_faces = mesh.metadata["brep_faces"]

    # Check brep_faces content
    assert len(brep_faces) == 96  # featuretype.STEP has 96 faces

    # Parse into dataclasses (maintains indexing, None for non-analytical)
    primitives = cascadio.primitives.parse_brep_faces(brep_faces)
    assert len(primitives) == 96  # Same length, preserves face indices

    # Count non-None primitives (analytical surfaces)
    analytical_count = sum(1 for p in primitives if p is not None)
    assert analytical_count == 95  # 96 faces - 1 non-analytical

    # Get typed primitives using list comprehension
    cylinders = [p for p in primitives if isinstance(p, cascadio.primitives.Cylinder)]
    planes = [p for p in primitives if isinstance(p, cascadio.primitives.Plane)]

    assert len(cylinders) == 46
    assert len(planes) == 49

    # Verify cylinder dataclass structure
    cyl = cylinders[0]
    assert isinstance(cyl, cascadio.primitives.Cylinder)
    assert len(cyl.origin) == 3
    assert len(cyl.axis) == 3
    assert isinstance(cyl.radius, float)
    assert len(cyl.u_bounds) == 2
    assert len(cyl.v_bounds) == 2

    # Verify plane dataclass structure
    plane = planes[0]
    assert isinstance(plane, cascadio.primitives.Plane)
    assert len(plane.origin) == 3
    assert len(plane.normal) == 3
    assert len(plane.x_dir) == 3


def test_convert_step_to_glb_brep_types_filter():
    """Test that brep_types parameter filters which primitives are included."""
    step_path = FEATURE_TYPE_STEP_PATH

    with tempfile.TemporaryDirectory() as temp_dir:
        # Test with only cylinders
        glb_cyl = Path(temp_dir) / "cylinders.glb"
        cascadio.step_to_glb(
            step_path.as_posix(),
            glb_cyl.as_posix(),
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
            include_brep=True,
            brep_types={"cylinder"},
        )
        scene_cyl = trimesh.load(glb_cyl, merge_primitives=True)
        mesh_cyl = list(scene_cyl.geometry.values())[0]
        brep_cyl = mesh_cyl.metadata["brep_faces"]

        # Should only have cylinders
        assert len(brep_cyl) == 46
        assert all(f["type"] == "cylinder" for f in brep_cyl)

        # Test with only planes
        glb_plane = Path(temp_dir) / "planes.glb"
        cascadio.step_to_glb(
            step_path.as_posix(),
            glb_plane.as_posix(),
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
            include_brep=True,
            brep_types={"plane"},
        )
        scene_plane = trimesh.load(glb_plane, merge_primitives=True)
        mesh_plane = list(scene_plane.geometry.values())[0]
        brep_plane = mesh_plane.metadata["brep_faces"]

        # Should only have planes
        assert len(brep_plane) == 49
        assert all(f["type"] == "plane" for f in brep_plane)

        # Test with both
        glb_both = Path(temp_dir) / "both.glb"
        cascadio.step_to_glb(
            step_path.as_posix(),
            glb_both.as_posix(),
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
            include_brep=True,
            brep_types={"cylinder", "plane"},
        )
        scene_both = trimesh.load(glb_both, merge_primitives=True)
        mesh_both = list(scene_both.geometry.values())[0]
        brep_both = mesh_both.metadata["brep_faces"]

        # Should have both types (95 analytical faces)
        assert len(brep_both) == 95
        types = {f["type"] for f in brep_both}
        assert types == {"cylinder", "plane"}


def test_step_to_glb_bytes():
    """Test the bytes-based conversion without temp files."""
    step_path = FEATURE_TYPE_STEP_PATH

    # Read STEP file as bytes
    step_data = step_path.read_bytes()

    # Convert to GLB bytes
    glb_data = cascadio.step_to_glb_bytes(
        step_data,
        tol_linear=TOL_LINEAR,
        tol_angular=TOL_ANGULAR,
        include_brep=True,
    )

    assert len(glb_data) > 0

    # Load with trimesh from bytes
    scene = trimesh.load(io.BytesIO(glb_data), file_type="glb", merge_primitives=True)

    assert len(scene.geometry) == 1
    mesh = list(scene.geometry.values())[0]
    assert "brep_faces" in mesh.metadata
    assert len(mesh.metadata["brep_faces"]) == 96


def test_step_to_glb_bytes_performance():
    """Compare performance of file-based vs bytes-based conversion."""

    step_path = FEATURE_TYPE_STEP_PATH
    step_data = step_path.read_bytes()

    n_iterations = 5

    # Time file-based method
    def file_based():
        with tempfile.NamedTemporaryFile(suffix=".glb", delete=True) as f:
            cascadio.step_to_glb(
                step_path.as_posix(),
                f.name,
                tol_linear=TOL_LINEAR,
                tol_angular=TOL_ANGULAR,
            )
            # Read result to make comparison fair
            f.seek(0)
            return f.read()

    # Time bytes-based method
    def bytes_based():
        return cascadio.step_to_glb_bytes(
            step_data,
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
        )

    # Time bytes-based with include_brep
    def bytes_with_brep():
        return cascadio.step_to_glb_bytes(
            step_data,
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
            include_brep=True,
        )

    file_time = timeit.timeit(file_based, number=n_iterations)
    bytes_time = timeit.timeit(bytes_based, number=n_iterations)
    brep_time = timeit.timeit(bytes_with_brep, number=n_iterations)

    print(f"\nPerformance comparison ({n_iterations} iterations):")
    print(
        f"  File-based:    {file_time:.3f}s ({file_time / n_iterations * 1000:.1f}ms per call)"
    )
    print(
        f"  Bytes-based:   {bytes_time:.3f}s ({bytes_time / n_iterations * 1000:.1f}ms per call)"
    )
    print(
        f"  With BREP:     {brep_time:.3f}s ({brep_time / n_iterations * 1000:.1f}ms per call)"
    )
    print(f"  File vs bytes: {file_time / bytes_time:.2f}x")
    print(f"  BREP overhead: {(brep_time / bytes_time - 1) * 100:.1f}%")

    # Both should produce valid output
    file_result = file_based()
    bytes_result = bytes_based()
    assert len(file_result) > 0
    assert len(bytes_result) > 0
    # Sizes should be similar (not exact due to temp file naming differences in GLTF)
    assert abs(len(file_result) - len(bytes_result)) < 1000
