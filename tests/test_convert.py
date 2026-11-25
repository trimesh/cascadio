import io

import numpy as np
import pytest
import trimesh
from conftest import MODELS_DIR, glb_json

# Require trimesh extension registry support
from trimesh.exchange.gltf.extensions import register_handler  # noqa

import cascadio
import cascadio.extension  # Register extension handlers
import cascadio.primitives


def check_brep(
    triangles: np.ndarray,
    brep_index: np.ndarray,
    primitives: list[cascadio.primitives.BrepPrimitive | None],
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
            dist_to_major = np.sqrt((r_plane - prim.major_radius) ** 2 + h**2)
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


def load_scene(name: str, **kwargs) -> trimesh.Scene:
    """Convert a test model with cascadio and load the GLB with trimesh."""
    glb = cascadio.load((MODELS_DIR / name).read_bytes(), **kwargs)
    return trimesh.load(io.BytesIO(glb), file_type="glb", merge_primitives=True)


def test_convert_step_to_glb():
    scene = load_scene("featuretype.STEP", tol_linear=0.1, tol_angular=0.5)

    assert len(scene.geometry) == 1

    mesh = next(iter(scene.geometry.values()))
    assert len(mesh.vertices) > 100, "Mesh should have substantial vertices"
    assert len(mesh.faces) > 100, "Mesh should have substantial faces"
    assert mesh.is_winding_consistent, "Mesh should have consistent winding"
    assert np.allclose(scene.extents, np.array([0.127, 0.0635, 0.034925]))


def test_convert_step_to_glb_with_brep():
    """Test that include_brep=True embeds BREP primitive data in mesh extras."""
    scene = load_scene(
        "featuretype.STEP", tol_linear=0.1, tol_angular=0.5, include_brep=True
    )

    # Should have one mesh
    assert len(scene.geometry) == 1

    # Get the mesh
    mesh = next(iter(scene.geometry.values()))

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
    glb_data = cascadio.load(
        (MODELS_DIR / "featuretype.STEP").read_bytes(),
        file_type="step",
        tol_linear=0.1,
        tol_angular=0.5,
        include_brep=True,
        brep_types=brep_types,
    )

    scene = trimesh.load(io.BytesIO(glb_data), file_type="glb", merge_primitives=True)
    mesh = next(iter(scene.geometry.values()))
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


def test_multibody():
    """Test multibody STEP file with BREP data for each body."""
    glb_data = cascadio.load(
        (MODELS_DIR / "multibody.step").read_bytes(),
        file_type="step",
        tol_linear=0.1,
        tol_angular=0.5,
        include_brep=True,
    )

    assert len(glb_data) > 10000, "GLB output suspiciously small for multibody"

    scene = trimesh.load(io.BytesIO(glb_data), file_type="glb")
    assert len(scene.geometry) == 10

    for name, mesh in scene.geometry.items():
        # Verify mesh is valid
        assert len(mesh.vertices) > 10, f"{name}: should have vertices"
        assert len(mesh.faces) > 10, f"{name}: should have faces"
        assert mesh.is_winding_consistent, f"{name}: should have consistent winding"

        # Verify BREP metadata
        assert "cascadio" in mesh.metadata, f"{name}: missing cascadio metadata"
        brep_faces = mesh.metadata["cascadio"]["brep_faces"]
        brep_index = mesh.face_attributes["brep_index"]

        # Size checks
        assert len(brep_index) == len(mesh.faces), f"{name}: brep_index/faces mismatch"
        assert brep_index.max() < len(brep_faces), f"{name}: brep_index out of range"
        assert len(brep_faces) > 0, f"{name}: should have BREP faces"

        # Validate cylinder faces actually lie on cylinder surfaces
        assert_brep_surfaces(mesh)


def test_brep_forces_merge_primitives():
    """Test that include_brep=True forces merge_primitives=True.

    When include_brep is requested with merge_primitives=False, the binding
    automatically enables merge_primitives since BREP metadata requires merged faces.
    """
    # Request merge_primitives=False with include_brep=True
    # Should auto-enable merge_primitives and produce valid BREP data
    glb_data = cascadio.load(
        (MODELS_DIR / "featuretype.STEP").read_bytes(),
        file_type="step",
        tol_linear=0.1,
        tol_angular=0.5,
        merge_primitives=False,  # Will be auto-enabled due to include_brep
        include_brep=True,
    )

    scene = trimesh.load(io.BytesIO(glb_data), file_type="glb")

    # Should have single merged mesh (merge_primitives was auto-enabled)
    assert len(scene.geometry) == 1, (
        "Expected single mesh (merge_primitives auto-enabled)"
    )

    mesh = next(iter(scene.geometry.values()))

    # BREP data should be present
    assert "cascadio" in mesh.metadata, "missing cascadio metadata"
    assert "brep_faces" in mesh.metadata["cascadio"], "missing brep_faces"

    brep_faces = mesh.metadata["cascadio"]["brep_faces"]
    brep_index = mesh.face_attributes.get("brep_index")

    assert brep_index is not None, "missing brep_index"
    assert len(brep_index) == len(mesh.faces), "brep_index/faces mismatch"
    assert brep_index.max() < len(brep_faces), "brep_index out of range"

    # Validate vertices lie on their BREP surfaces
    assert_brep_surfaces(mesh)


def test_convert_step_to_glb_with_substance():
    """`include_brep` gives each part's substance, with its density in kg/m^3."""
    step_data = (MODELS_DIR / "material.stp").read_bytes()

    glb_data = cascadio.load(
        step_data,
        file_type="step",
        tol_linear=0.1,
        tol_angular=0.5,
        include_brep=True,
    )

    assert len(glb_data) > 0

    # Load with trimesh and check mesh metadata
    scene = trimesh.load(io.BytesIO(glb_data), file_type="glb", merge_primitives=True)
    mesh = next(iter(scene.geometry.values()))

    # the file says 7850 kg/m^3; OpenCASCADE alone reports 0.00785
    substance = mesh.metadata["cascadio"]["substance"]
    assert substance["name"] == "Steel"
    assert substance["density"] == pytest.approx(7850)
    assert set(substance) == {"name", "density"}

    # filtering the face types keeps the substance
    glb_data_with_brep = cascadio.load(
        step_data,
        file_type="step",
        tol_linear=0.1,
        tol_angular=0.5,
        include_brep=True,
        brep_types={"plane"},
    )

    assert len(glb_data_with_brep) > 0

    scene_brep = trimesh.load(
        io.BytesIO(glb_data_with_brep), file_type="glb", merge_primitives=True
    )
    mesh_brep = next(iter(scene_brep.geometry.values()))

    # the substance should still be present
    assert mesh_brep.metadata["cascadio"]["substance"]["name"] == "Steel"

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
        (MODELS_DIR / "featuretype.STEP").read_bytes(),
        file_type="step",
        tol_linear=0.1,
        tol_angular=0.5,
        include_brep=True,
    )
    scene = trimesh.load_scene(
        io.BytesIO(glb_data), file_type="glb", merge_primitives=True
    )
    return next(iter(scene.geometry.values()))


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


def test_cylinder_vertices_on_surface(brep_mesh):
    """Verify mesh vertices mapped to cylinders lie on the cylinder surface."""
    assert_brep_surfaces(brep_mesh)


if __name__ == "__main__":
    test_multibody()


def _node_names(glb: bytes) -> list:
    """Read node names straight from the GLB JSON chunk."""
    header = glb_json(glb)
    return [node.get("name") for node in header["nodes"]]


@pytest.mark.parametrize(
    "name_format,expected",
    [
        # instance labels from the STEP assembly structure
        ("INSTANCE", ["box_sides", "NAUO1", "NAUO2", "NAUO3"]),
        ("INSTANCE_OR_PRODUCT", ["box_sides", "NAUO1", "NAUO2", "NAUO3"]),
        # human-readable product names
        ("PRODUCT", ["box_sides", "box_base", "side_long", "side_long"]),
        ("PRODUCT_OR_INSTANCE", ["box_sides", "box_base", "side_long", "side_long"]),
        (
            "PRODUCT_AND_INSTANCE",
            ["box_sides", "box_base [NAUO1]", "side_long [NAUO2]", "side_long [NAUO3]"],
        ),
    ],
)
def test_node_name_format(name_format, expected):
    """`node_name_format` picks between instance and product names for nodes."""
    step_data = (MODELS_DIR / "multibody.step").read_bytes()
    name_format = getattr(cascadio.NodeNameFormat, name_format)

    names = _node_names(cascadio.load(step_data, node_name_format=name_format))
    assert names[:4] == expected

    # the low-level binding takes the same argument
    assert (
        _node_names(cascadio.to_glb_bytes(step_data, node_name_format=name_format))
        == names
    )


def test_node_name_format_default():
    """The default keeps the behavior from before the option existed."""
    step_data = (MODELS_DIR / "multibody.step").read_bytes()
    assert _node_names(cascadio.load(step_data)) == _node_names(
        cascadio.load(
            step_data, node_name_format=cascadio.NodeNameFormat.INSTANCE_OR_PRODUCT
        )
    )


def test_y_up_cuboid():
    """
    `y_up` rotates CAD's Z-up into glTF's Y-up: (x, y, z) -> (x, z, -y).

    The models are a 1 x 2 x 3 meter cuboid exported from Onshape twice,
    once as-is and once with Onshape's own "Y-up" export option.
    """
    z_up = load_scene("cuboid_zup.step")
    assert np.allclose(z_up.bounds, [[0, 0, 0], [1, 2, 3]])

    rotated = load_scene("cuboid_zup.step", y_up=True)
    assert np.allclose(rotated.bounds, [[0, 0, -2], [1, 3, 0]])

    # the same thing Onshape produces when asked for Y-up
    onshape = load_scene("cuboid_yup.step")
    assert np.allclose(rotated.bounds, onshape.bounds)


def test_y_up_brep():
    """With `y_up` the BREP surfaces are rotated along with the mesh."""
    for name in ("featuretype.STEP", "cuboid_zup.step"):
        scene = load_scene(name, y_up=True, include_brep=True)
        for mesh in scene.geometry.values():
            assert_brep_surfaces(mesh)

    # the top face of the cuboid points along +Z in CAD and +Y in glTF
    for y_up, up in ((False, [0, 0, 1]), (True, [0, 1, 0])):
        scene = load_scene("cuboid_zup.step", y_up=y_up, include_brep=True)
        mesh = next(iter(scene.geometry.values()))
        normals = [p.normal for p in mesh.metadata["cascadio"]["brep_primitives"]]
        assert any(np.allclose(n, up) for n in normals)


def test_brep_includes_substance():
    """`include_brep` alone brings the substance: it is part of the extension."""
    scene = load_scene("material.stp", include_brep=True)
    mesh = next(iter(scene.geometry.values()))
    assert mesh.metadata["cascadio"]["substance"]["name"] == "Steel"
    assert len(mesh.metadata["cascadio"]["brep_primitives"]) > 0


@pytest.mark.parametrize("option", ["include_brep", "include_materials"])
def test_substance_per_part(option):
    """Each mesh carries the substance of its own part, not the whole model's."""
    # two parts made from material.stp: "Part 1" is Steel, "Part 2" is Aluminum
    scene = load_scene("two_materials.step", **{option: True})
    assert len(scene.geometry) == 2
    found = {
        name: mesh.metadata["cascadio"]["substance"]
        for name, mesh in scene.geometry.items()
    }
    assert sorted(s["name"] for s in found.values()) == ["Aluminum", "Steel"]
    assert found["Part 1"]["name"] == "Steel"
    # the file gives both the same density, in kilograms per cubic meter
    assert all(s["density"] == pytest.approx(7850) for s in found.values())


def test_appearance_is_a_gltf_material():
    """
    How a part looks is in the GLB's own materials, and nothing of it is in
    the extension: the substance is what the part is made of.
    """
    glb = cascadio.load((MODELS_DIR / "colored.step").read_bytes(), include_brep=True)
    header = glb_json(glb)
    for mesh in header["meshes"]:
        for primitive in mesh["primitives"]:
            assert primitive["extensions"]["TM_brep_faces"]["substance"] is None
            material = header["materials"][primitive["material"]]
            assert len(material["pbrMetallicRoughness"]["baseColorFactor"]) == 4


def test_instances_share_a_substance():
    """
    A substance belongs to a part, so every node that shows the part
    reaches the same one, through its mesh's primitive.
    """
    # multibody.step places the "gusset" part (#8805) four times. Give it a
    # substance, with its density written as kg * m^-3 this time.
    gusset = b"""
#90001=PROPERTY_DEFINITION('material property','material name',#8805);
#90002=DESCRIPTIVE_REPRESENTATION_ITEM('Brass','Brass');
#90003=REPRESENTATION('material name',(#90002),#2455);
#90004=PROPERTY_DEFINITION_REPRESENTATION(#90001,#90003);
#90005=(MASS_UNIT() NAMED_UNIT(*) SI_UNIT(.KILO.,.GRAM.));
#90006=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.));
#90007=DERIVED_UNIT_ELEMENT(#90005,1.);
#90008=DERIVED_UNIT_ELEMENT(#90006,-3.);
#90009=DERIVED_UNIT((#90007,#90008));
#90010=MEASURE_REPRESENTATION_ITEM('density measure',
POSITIVE_RATIO_MEASURE(8500.),#90009);
#90011=REPRESENTATION('density',(#90010),#2455);
#90012=PROPERTY_DEFINITION('material property','density of part',#8805);
#90013=PROPERTY_DEFINITION_REPRESENTATION(#90012,#90011);
ENDSEC;
END-ISO-10303-21;"""
    data = (MODELS_DIR / "multibody.step").read_bytes()
    data = data[: data.rindex(b"ENDSEC;")] + gusset
    glb = cascadio.load(data, include_brep=True)
    header = glb_json(glb)

    found = {}
    for node in header["nodes"]:
        if "mesh" not in node:
            continue
        mesh = header["meshes"][node["mesh"]]
        for primitive in mesh["primitives"]:
            substance = primitive["extensions"]["TM_brep_faces"]["substance"]
            found.setdefault(mesh["name"], []).append(substance)
    brass = {"name": "Brass", "density": pytest.approx(8500)}
    assert found["gusset"] == [brass] * 4
    # the other parts are made of nothing the file names
    others = [s for name, found_ in found.items() if name != "gusset" for s in found_]
    assert others and all(s is None for s in others)


def _within(values, bounds, tolerance, period=None):
    """Are all values inside [min, max]? Angles may be off by whole turns."""
    low, high = bounds[0] - tolerance, bounds[1] + tolerance
    values = np.asarray(values)
    if period is None:
        return bool(((values >= low) & (values <= high)).all())
    return bool(
        np.any(
            [
                (values + k * period >= low) & (values + k * period <= high)
                for k in (-2, -1, 0, 1, 2)
            ],
            axis=0,
        ).all()
    )


def check_extents(prim, verts) -> None:
    """
    Recompute where each vertex sits on its surface, exactly the way the
    schema describes each field, and check it is inside the stated extents.
    """
    P = cascadio.primitives
    turn = 2 * np.pi
    if isinstance(prim, P.Plane):
        d = verts - prim.origin
        y_dir = np.cross(prim.normal, prim.x_dir)
        assert _within(d @ prim.x_dir, prim.extent_x, 1e-7)
        assert _within(d @ y_dir, prim.extent_y, 1e-7)
        return

    axis, x_dir = np.array(prim.axis), np.array(prim.x_dir)
    y_dir = np.cross(axis, x_dir)
    assert abs(np.linalg.norm(axis) - 1) < 1e-9
    assert abs(np.linalg.norm(x_dir) - 1) < 1e-9
    assert abs(axis @ x_dir) < 1e-9
    if isinstance(prim, P.Cylinder):
        d = verts - prim.origin
        assert _within(d @ axis, prim.extent_height, 1e-7)
        assert _within(np.arctan2(d @ y_dir, d @ x_dir), prim.extent_angle, 1e-5, turn)
    elif isinstance(prim, P.Cone):
        # the reference circle sits on the axis, this far from the apex
        reference = prim.apex + axis * prim.ref_radius / np.tan(prim.semi_angle)
        d = verts - reference
        # distance along the slanted side, from distance along the axis
        assert _within((d @ axis) / np.cos(prim.semi_angle), prim.extent_distance, 1e-7)
        off_axis = np.hypot(d @ x_dir, d @ y_dir) > 1e-9  # the apex has no angle
        assert _within(
            np.arctan2(d @ y_dir, d @ x_dir)[off_axis], prim.extent_angle, 1e-5, turn
        )
    elif isinstance(prim, P.Sphere):
        d = verts - prim.center
        latitude = np.arcsin(np.clip(d @ axis / prim.radius, -1, 1))
        assert _within(latitude, prim.extent_latitude, 1e-5)
        off_axis = np.hypot(d @ x_dir, d @ y_dir) > 1e-9  # poles have no longitude
        assert _within(
            np.arctan2(d @ y_dir, d @ x_dir)[off_axis],
            prim.extent_longitude,
            1e-5,
            turn,
        )
    elif isinstance(prim, P.Torus):
        d = verts - prim.center
        assert _within(
            np.arctan2(d @ y_dir, d @ x_dir), prim.extent_major_angle, 1e-5, turn
        )
        # around the tube: 0 at the outer edge, growing toward the axis direction
        outward = np.hypot(d @ x_dir, d @ y_dir) - prim.major_radius
        assert _within(
            np.arctan2(d @ axis, outward), prim.extent_minor_angle, 1e-5, turn
        )


@pytest.mark.parametrize("y_up", [False, True])
def test_extents_mean_what_the_schema_says(y_up):
    """Every face's vertices fall inside its extents, measured as documented."""
    checked = set()
    for model in [*sorted(MODELS_DIR.glob("*.st*p")), MODELS_DIR / "featuretype.STEP"]:
        scene = load_scene(model.name, include_brep=True, y_up=y_up)
        for mesh in scene.geometry.values():
            triangles = mesh.vertices[mesh.faces]
            index = mesh.face_attributes["brep_index"]
            for i, prim in enumerate(mesh.metadata["cascadio"]["brep_primitives"]):
                verts = triangles[index == i].reshape(-1, 3)
                if prim is not None and len(verts):
                    check_extents(prim, verts)
                    checked.add(type(prim).__name__)
    # the test models cover these surface types
    assert {"Plane", "Cylinder"} <= checked
    print("checked:", sorted(checked))


def test_include_materials_is_include_brep():
    """The option from cascadio 0.1 is now the same as `include_brep`."""
    data = (MODELS_DIR / "material.stp").read_bytes()
    assert cascadio.load(data, include_materials=True) == cascadio.load(
        data, include_brep=True
    )
