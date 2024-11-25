from pathlib import Path
from unittest import TestCase

import cascadio
import numpy
from numpy.testing import assert_almost_equal
import trimesh
import tempfile

MODELS_DIR = Path(__file__).parent / "models"
FEATURE_TYPE_STEP_PATH = MODELS_DIR / "featuretype.STEP"
COLORED_STEP_PATH = MODELS_DIR / "colored.step"
TOL_LINEAR = 0.1
TOL_ANGULAR = 0.5


class ConvertTest(TestCase):

    def test_convert_step_to_glb(self):
        infile = FEATURE_TYPE_STEP_PATH

        with tempfile.TemporaryDirectory() as temp_dir:
            outfile = Path(temp_dir) / infile.with_suffix(".glb").name
            cascadio.step_to_glb(
                infile.as_posix(),
                outfile.as_posix(),
                tol_linear=TOL_LINEAR,
                tol_angular=TOL_ANGULAR,
            )
            scene = trimesh.load(outfile, merge_primitives=True)

        self.assertEqual(len(scene.geometry), 1)

    def test_convert_step_to_obj_original_axis(self):
        infile = FEATURE_TYPE_STEP_PATH

        with tempfile.TemporaryDirectory() as temp_dir:
            outfile = Path(temp_dir) / infile.with_suffix(".obj").name
            cascadio.step_to_obj(
                infile.as_posix(),
                outfile.as_posix(),
                tol_linear=TOL_LINEAR,
                tol_angular=TOL_ANGULAR,
                swap_z_and_y_axis=False,
            )
            mesh = trimesh.load(outfile, merge_primitives=True)

        self.assertGreater(mesh.mass, 0)
        self.assertGreater(mesh.volume, 0)
        assert_almost_equal(mesh.extents, numpy.array([127.0, 63.5, 34.924999]))

    def test_convert_step_to_obj_swapped_axis(self):
        infile = FEATURE_TYPE_STEP_PATH

        with tempfile.TemporaryDirectory() as temp_dir:
            outfile = Path(temp_dir) / infile.with_suffix(".obj").name
            cascadio.step_to_obj(
                infile.as_posix(),
                outfile.as_posix(),
                tol_linear=TOL_LINEAR,
                tol_angular=TOL_ANGULAR,
                swap_z_and_y_axis=True,
            )
            mesh = trimesh.load(outfile, merge_primitives=True)

        self.assertGreater(mesh.mass, 0)
        self.assertGreater(mesh.volume, 0)
        assert_almost_equal(mesh.extents, numpy.array([127.0, 34.924999, 63.5]))

    def test_convert_step_to_obj_with_colors(self):
        infile = COLORED_STEP_PATH

        with tempfile.TemporaryDirectory() as temp_dir:
            objfile = Path(temp_dir) / infile.with_suffix(".obj").name
            mtlfile = Path(temp_dir) / infile.with_suffix(".mtl").name
            cascadio.step_to_obj(
                infile.as_posix(),
                objfile.as_posix(),
                tol_linear=TOL_LINEAR,
                tol_angular=TOL_ANGULAR,
                use_colors=True,
            )
            self.assertTrue(mtlfile.exists())
            mesh = trimesh.load(objfile, merge_primitives=True)

        self.assertGreater(mesh.mass, 0)
        self.assertGreater(mesh.volume, 0)
        assert_almost_equal(
            mesh.visual.material.main_color, numpy.array([202, 225, 255, 255])
        )

    def test_convert_step_to_obj_without_colors(self):
        infile = COLORED_STEP_PATH

        with tempfile.TemporaryDirectory() as temp_dir:
            objfile = Path(temp_dir) / infile.with_suffix(".obj").name
            mtlfile = Path(temp_dir) / infile.with_suffix(".mtl").name
            cascadio.step_to_obj(
                infile.as_posix(),
                objfile.as_posix(),
                tol_linear=TOL_LINEAR,
                tol_angular=TOL_ANGULAR,
                use_colors=False,
            )
            self.assertFalse(mtlfile.exists())
            mesh = trimesh.load(objfile, merge_primitives=True)

        self.assertGreater(mesh.mass, 0)
        self.assertGreater(mesh.volume, 0)
        assert_almost_equal(
            mesh.visual.material.main_color, numpy.array([102, 102, 102, 255])
        )
