import io
import os
import tempfile
import timeit
from pathlib import Path
from typing import List, Optional

import cascadio
import cascadio.extension  # Register extension handlers
import cascadio.primitives
import numpy as np
import pytest
import trimesh

# Check if trimesh has extension registry support
try:
    from trimesh.exchange.gltf.extensions import register_handler  # noqa

    _HAS_EXTENSION_REGISTRY = True
except ImportError:
    _HAS_EXTENSION_REGISTRY = False

MODELS_DIR = Path(__file__).parent / "models"
FEATURE_TYPE_STEP_PATH = MODELS_DIR / "featuretype.STEP"
COLORED_STEP_PATH = MODELS_DIR / "colored.step"
MATERIAL_STEP_PATH = MODELS_DIR / "material.stp"
TOL_LINEAR = 0.1
TOL_ANGULAR = 0.5


def check_brep(
    triangles: np.ndarray,
    brep_index: np.ndarray,
    primitives: List[Optional[cascadio.primitives.BrepPrimitive]],
    tolerance: float = 1e-8,
) -> None:
    """
    Validate that triangle vertices lie on their corresponding BREP surfaces.

    For each primitive, checks that all vertices of triangles assigned to that
    primitive (via brep_index) are within tolerance of the analytical surface.

    Parameters
    ----------
    triangles : (n, 3, 3) float64
        Triangle vertices, where triangles[i] is a (3, 3) array of 3 vertices.
    brep_index : (n,) int
        Index into primitives for each triangle.
    primitives : list of BrepPrimitive or None
        Parsed primitives from brep_faces metadata.
    tolerance : float
        Maximum allowed distance from surface in meters.

    Raises
    ------
    AssertionError
        If any vertex is further than tolerance from its assigned surface.
    """
    brep_index = np.asarray(brep_index).flatten()

    for idx, prim in enumerate(primitives):
        if prim is None:
            continue

        tri_mask = brep_index == idx
        if not tri_mask.any():
            continue

        # Get all vertices of triangles with this brep_index
        verts = triangles[tri_mask].reshape(-1, 3)

        if isinstance(prim, cascadio.primitives.Plane):
            # Distance from plane: |dot(v - origin, normal)|
            origin = np.array(prim.origin)
            normal = np.array(prim.normal)
            dist = np.abs(np.dot(verts - origin, normal))
            max_err = dist.max()
            assert max_err <= tolerance, (
                f"Plane {idx}: vertices {max_err * 1000:.6f}mm off surface"
            )

        elif isinstance(prim, cascadio.primitives.Cylinder):
            # Distance from cylinder axis should equal radius
            origin = np.array(prim.origin)
            axis = np.array(prim.axis)
            to_verts = verts - origin
            # Project out the axis component
            perp = to_verts - np.outer(np.dot(to_verts, axis), axis)
            dist = np.linalg.norm(perp, axis=1)
            max_err = np.abs(dist - prim.radius).max()
            assert max_err <= tolerance, (
                f"Cylinder {idx}: vertices {max_err * 1000:.6f}mm off surface"
            )

        elif isinstance(prim, cascadio.primitives.Sphere):
            # Distance from center should equal radius
            center = np.array(prim.center)
            dist = np.linalg.norm(verts - center, axis=1)
            max_err = np.abs(dist - prim.radius).max()
            assert max_err <= tolerance, (
                f"Sphere {idx}: vertices {max_err * 1000:.6f}mm off surface"
            )

        elif isinstance(prim, cascadio.primitives.Cone):
            # For cone: distance from axis at height h should be h * tan(semi_angle)
            apex = np.array(prim.apex)
            axis = np.array(prim.axis)
            to_verts = verts - apex
            # Height along axis from apex
            h = np.dot(to_verts, axis)
            # Perpendicular distance from axis
            perp = to_verts - np.outer(h, axis)
            r_actual = np.linalg.norm(perp, axis=1)
            # Expected radius at this height
            r_expected = np.abs(h) * np.tan(prim.semi_angle)
            max_err = np.abs(r_actual - r_expected).max()
            assert max_err <= tolerance, (
                f"Cone {idx}: vertices {max_err * 1000:.6f}mm off surface"
            )

        elif isinstance(prim, cascadio.primitives.Torus):
            # For torus: project to plane, find distance to major circle,
            # then that distance from minor circle should equal minor_radius
            center = np.array(prim.center)
            axis = np.array(prim.axis)
            to_verts = verts - center
            # Project onto the torus plane (perpendicular to axis)
            h = np.dot(to_verts, axis)
            in_plane = to_verts - np.outer(h, axis)
            # Distance from center in plane
            r_plane = np.linalg.norm(in_plane, axis=1)
            # Distance from the major circle
            dist_to_major = np.sqrt((r_plane - prim.major_radius) ** 2 + h ** 2)
            max_err = np.abs(dist_to_major - prim.minor_radius).max()
            assert max_err <= tolerance, (
                f"Torus {idx}: vertices {max_err * 1000:.6f}mm off surface"
            )


def assert_brep_surfaces(mesh, tolerance: float = 1e-8) -> None:
    """
    Assert that all mesh faces lie on their assigned BREP surfaces.

    Convenience wrapper around check_brep that extracts data from a trimesh mesh.
    """
    brep_index = mesh.face_attributes.get("brep_index")

    # MUST correspond to mesh faces
    assert len(brep_index) == len(mesh.faces)
    
    primitives = cascadio.primitives.parse_brep_faces(
        mesh.metadata["cascadio"]["brep_faces"]
    )

    # Get triangles as (n, 3, 3) array
    triangles = mesh.vertices[mesh.faces]

    check_brep(triangles, brep_index, primitives, tolerance)


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


@pytest.mark.parametrize(
    "use_colors,expected_color",
    [
        (True, np.array([202, 225, 255, 255])),  # With colors from STEP
        (False, np.array([102, 102, 102, 255])),  # Default gray
    ],
)
def test_convert_step_to_obj_colors(use_colors, expected_color):
    """Test OBJ export with and without colors."""
    step_path = COLORED_STEP_PATH

    with tempfile.TemporaryDirectory() as temp_dir:
        obj_path = Path(temp_dir) / step_path.with_suffix(".obj").name
        mtl_path = obj_path.with_suffix(".mtl")
        cascadio.step_to_obj(
            step_path.as_posix(),
            obj_path.as_posix(),
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
            use_colors=use_colors,
        )

        # MTL file should exist only when colors are enabled
        assert mtl_path.exists() == use_colors
        mesh = trimesh.load(obj_path, merge_primitives=True)

    assert mesh.mass > 0.0
    assert mesh.volume > 0.0
    assert np.allclose(mesh.visual.material.main_color, expected_color)


@pytest.mark.skipif(
    not _HAS_EXTENSION_REGISTRY, reason="trimesh extension registry not available"
)
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

    # Should have brep_faces in mesh.metadata.cascadio from TM_brep_faces extension
    assert "cascadio" in mesh.metadata
    assert "brep_faces" in mesh.metadata["cascadio"]
    brep_faces = mesh.metadata["cascadio"]["brep_faces"]

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
    assert len(cyl.extent_angle) == 2
    assert len(cyl.extent_height) == 2

    # Verify plane dataclass structure
    plane = planes[0]
    assert isinstance(plane, cascadio.primitives.Plane)
    assert len(plane.origin) == 3
    assert len(plane.normal) == 3
    assert len(plane.x_dir) == 3

    # Validate cylinder faces lie on cylinder surfaces
    assert_brep_surfaces(mesh)


@pytest.mark.skipif(
    not _HAS_EXTENSION_REGISTRY, reason="trimesh extension registry not available"
)
@pytest.mark.parametrize(
    "brep_types,expected_count,expected_types",
    [
        ({"cylinder"}, 46, {"cylinder"}),
        ({"plane"}, 49, {"plane"}),
        ({"cylinder", "plane"}, 95, {"cylinder", "plane"}),
    ],
)
def test_convert_step_to_glb_brep_types_filter(
    brep_types, expected_count, expected_types
):
    """Test that brep_types parameter filters which primitives are included.

    When filtering, the primitives array preserves index alignment with brep_index
    by including null entries for filtered-out faces.
    """
    step_path = FEATURE_TYPE_STEP_PATH
    step_data = step_path.read_bytes()

    # Convert with type filter
    glb_data = cascadio.load(
        step_data,
        file_type="step",
        tol_linear=TOL_LINEAR,
        tol_angular=TOL_ANGULAR,
        include_brep=True,
        brep_types=brep_types,
    )

    scene = trimesh.load(io.BytesIO(glb_data), file_type="glb", merge_primitives=True)
    mesh = list(scene.geometry.values())[0]
    brep_faces = mesh.metadata["cascadio"]["brep_faces"]
    brep_index = mesh.face_attributes["brep_index"]

    # Total primitives should match total face count (preserves alignment)
    assert len(brep_faces) == 96

    # Non-null entries should match filter
    non_null = [f for f in brep_faces if f is not None]
    assert len(non_null) == expected_count
    assert {f["type"] for f in non_null} == expected_types

    # brep_index should correctly map to primitives
    assert brep_index.max() == len(brep_faces) - 1

    # Validate cylinder faces when cylinder type is included
    if "cylinder" in brep_types:
        assert_brep_surfaces(mesh)


@pytest.mark.skipif(
    not _HAS_EXTENSION_REGISTRY, reason="trimesh extension registry not available"
)
def test_multibody():
    """Test multibody STEP file with BREP data for each body."""
    with open(MODELS_DIR / "multibody.step", "rb") as f:
        step_data = f.read()

    glb_data = cascadio.load(
        step_data,
        file_type="step",
        tol_linear=TOL_LINEAR,
        tol_angular=TOL_ANGULAR,
        include_brep=True,
    )

    scene = trimesh.load(io.BytesIO(glb_data), file_type="glb")
    assert len(scene.geometry) == 10

    for name, mesh in scene.geometry.items():
        brep_faces = mesh.metadata["cascadio"]["brep_faces"]
        brep_index = mesh.face_attributes["brep_index"]

        # Size checks
        assert len(brep_index) == len(mesh.faces), f"{name}: brep_index/faces mismatch"
        assert brep_index.max() < len(brep_faces), f"{name}: brep_index out of range"

        # Validate cylinder faces actually lie on cylinder surfaces
        assert_brep_surfaces(mesh)

    
def test_step_to_glb_bytes_performance():
    """Compare performance of file-based vs bytes-based conversion."""
    step_data = FEATURE_TYPE_STEP_PATH.read_bytes()
    n_iterations = 5

    # Time file-based method (no BREP)
    def file_based():
        f = tempfile.NamedTemporaryFile(suffix=".glb", delete=False)
        try:
            f.close()
            cascadio.step_to_glb(
                FEATURE_TYPE_STEP_PATH.as_posix(),
                f.name,
                tol_linear=TOL_LINEAR,
                tol_angular=TOL_ANGULAR,
            )
            with open(f.name, "rb") as glb_file:
                return glb_file.read()
        finally:
            if os.path.exists(f.name):
                os.unlink(f.name)

    # Time file-based method with BREP
    def file_based_brep():
        f = tempfile.NamedTemporaryFile(suffix=".glb", delete=False)
        try:
            f.close()
            cascadio.step_to_glb(
                FEATURE_TYPE_STEP_PATH.as_posix(),
                f.name,
                tol_linear=TOL_LINEAR,
                tol_angular=TOL_ANGULAR,
                include_brep=True,
            )
            with open(f.name, "rb") as glb_file:
                return glb_file.read()
        finally:
            if os.path.exists(f.name):
                os.unlink(f.name)

    # Time bytes-based methods
    def bytes_based():
        return cascadio.load(
            step_data, file_type="step", tol_linear=TOL_LINEAR, tol_angular=TOL_ANGULAR
        )

    def bytes_with_brep():
        return cascadio.load(
            step_data,
            file_type="step",
            tol_linear=TOL_LINEAR,
            tol_angular=TOL_ANGULAR,
            include_brep=True,
        )

    file_time = timeit.timeit(file_based, number=n_iterations)
    file_brep_time = timeit.timeit(file_based_brep, number=n_iterations)
    bytes_time = timeit.timeit(bytes_based, number=n_iterations)
    brep_time = timeit.timeit(bytes_with_brep, number=n_iterations)

    print(f"\nPerformance ({n_iterations} iterations):")
    print(f"  File-based (no BREP):   {file_time / n_iterations * 1000:.1f}ms/call")
    print(
        f"  File-based (with BREP): {file_brep_time / n_iterations * 1000:.1f}ms/call  (+{(file_brep_time / file_time - 1) * 100:.1f}%)"
    )
    print(
        f"  Bytes (no BREP):        {bytes_time / n_iterations * 1000:.1f}ms/call  ({file_time / bytes_time:.2f}x faster)"
    )
    print(
        f"  Bytes (with BREP):      {brep_time / n_iterations * 1000:.1f}ms/call  (+{(brep_time / bytes_time - 1) * 100:.1f}%, {file_brep_time / brep_time:.2f}x faster)"
    )

    # Verify outputs are valid
    assert len(file_based()) > 0
    assert len(bytes_based()) > 0


@pytest.mark.skipif(
    not _HAS_EXTENSION_REGISTRY, reason="trimesh extension registry not available"
)
def test_convert_step_to_glb_with_materials():
    """Test that include_materials=True extracts material data from STEP."""
    step_path = MATERIAL_STEP_PATH

    # Convert with materials
    glb_data = cascadio.load(
        step_path.read_bytes(),
        file_type="step",
        tol_linear=TOL_LINEAR,
        tol_angular=TOL_ANGULAR,
        include_materials=True,
    )

    assert len(glb_data) > 0

    # Load with trimesh and check mesh metadata
    scene = trimesh.load(io.BytesIO(glb_data), file_type="glb", merge_primitives=True)
    mesh = list(scene.geometry.values())[0]

    # Materials should be in mesh.metadata.cascadio.materials
    assert "cascadio" in mesh.metadata
    assert "materials" in mesh.metadata["cascadio"]

    materials = mesh.metadata["cascadio"]["materials"]
    assert len(materials) == 1

    # Materials are arbitrary dicts - check the raw data
    mat = materials[0]
    assert mat["name"] == "Steel"
    assert mat["description"] == "Steel"
    assert "density" in mat
    assert 0.007 < mat["density"] < 0.008  # ~0.00785 g/mm³ for steel

    # Test with both materials and BREP data (plane filter)
    glb_data_with_brep = cascadio.load(
        step_path.read_bytes(),
        file_type="step",
        tol_linear=TOL_LINEAR,
        tol_angular=TOL_ANGULAR,
        include_materials=True,
        include_brep=True,
        brep_types={"plane"},
    )

    assert len(glb_data_with_brep) > 0

    scene_brep = trimesh.load(
        io.BytesIO(glb_data_with_brep), file_type="glb", merge_primitives=True
    )
    mesh_brep = list(scene_brep.geometry.values())[0]

    # Materials should still be present
    assert "cascadio" in mesh_brep.metadata
    assert "materials" in mesh_brep.metadata["cascadio"]
    assert len(mesh_brep.metadata["cascadio"]["materials"]) == 1
    assert mesh_brep.metadata["cascadio"]["materials"][0]["name"] == "Steel"

    # BREP data should be present with only planes
    assert "brep_faces" in mesh_brep.metadata["cascadio"]
    assert "brep_index" in mesh_brep.face_attributes
    brep_faces = mesh_brep.metadata["cascadio"]["brep_faces"]
    primitives = cascadio.primitives.parse_brep_faces(brep_faces)
    planes = [p for p in primitives if isinstance(p, cascadio.primitives.Plane)]
    non_planes = [
        p
        for p in primitives
        if p is not None and not isinstance(p, cascadio.primitives.Plane)
    ]
    assert len(planes) > 0
    assert len(non_planes) == 0  # Only planes should be present


@pytest.fixture
def brep_mesh():
    """Load featuretype.STEP with BREP data for cylinder tests."""
    glb_data = cascadio.load(
        FEATURE_TYPE_STEP_PATH.read_bytes(),
        file_type="step",
        tol_linear=TOL_LINEAR,
        tol_angular=TOL_ANGULAR,
        include_brep=True,
    )
    scene = trimesh.load_scene(
        io.BytesIO(glb_data), file_type="glb", merge_primitives=True
    )
    return list(scene.geometry.values())[0]


@pytest.mark.skipif(
    not _HAS_EXTENSION_REGISTRY, reason="trimesh extension registry not available"
)
def test_cylinder_parameters(brep_mesh):
    """Validate cylinder primitive parameters (unit axis, positive radius, valid bounds)."""
    brep_faces = brep_mesh.metadata["cascadio"]["brep_faces"]
    primitives = cascadio.primitives.parse_brep_faces(brep_faces)
    cylinders = [p for p in primitives if isinstance(p, cascadio.primitives.Cylinder)]

    assert len(cylinders) == 46

    for cyl in cylinders:
        axis = np.array(cyl.axis)
        assert np.isclose(np.linalg.norm(axis), 1.0, atol=1e-6)
        assert cyl.radius > 0
        assert cyl.extent_angle[1] > cyl.extent_angle[0]
        assert np.isfinite(cyl.extent_height[0]) and np.isfinite(cyl.extent_height[1])


@pytest.mark.skipif(
    not _HAS_EXTENSION_REGISTRY, reason="trimesh extension registry not available"
)
def test_cylinder_units_match_mesh(brep_mesh):
    """Verify cylinders and mesh use consistent units (meters)."""
    brep_faces = brep_mesh.metadata["cascadio"]["brep_faces"]
    primitives = cascadio.primitives.parse_brep_faces(brep_faces)
    cylinders = [p for p in primitives if isinstance(p, cascadio.primitives.Cylinder)]

    # Mesh extents ~127mm x 63.5mm x 35mm in meters
    assert np.allclose(
        brep_mesh.bounding_box.extents, [0.127, 0.0635, 0.034925], rtol=0.01
    )

    # Cylinder radii should be in meters (1.5mm to 10mm range)
    radii = [c.radius for c in cylinders]
    assert 0.0001 < min(radii) < 0.1
    assert 0.0001 < max(radii) < 0.1


@pytest.mark.skipif(
    not _HAS_EXTENSION_REGISTRY, reason="trimesh extension registry not available"
)
def test_cylinder_vertices_on_surface(brep_mesh):
    """Verify mesh vertices mapped to cylinders lie on the cylinder surface."""
    assert_brep_surfaces(brep_mesh)

if __name__ == '__main__':
    test_multibody()
    
