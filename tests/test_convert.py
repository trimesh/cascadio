import tempfile
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
