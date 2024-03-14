import os
import cascadio
import trimesh
import tempfile

cwd = os.path.abspath(os.path.dirname(__file__))


def test_convert_stp(model="featuretype.STEP"):
    infile = os.path.join(cwd, "models", model)
    assert os.path.exists(infile)

    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "outfile.glb")
        # do the conversion
        cascadio.to_glb(infile, outfile, tol_linear=0.1, tol_angular=0.5)
        scene = trimesh.load(outfile, merge_primitives=True)
    assert len(scene.geometry) == 1


def test_convert_igs(model="microstrip.igs"):
    infile = os.path.join(cwd, "models", model)
    assert os.path.exists(infile)

    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "outfile.glb")
        # do the conversion
        cascadio.to_glb(
            infile, outfile, file_type=cascadio.IGES, tol_linear=0.1, tol_angular=0.5
        )
        scene = trimesh.load(outfile, merge_primitives=True)
        assert len(scene.geometry) > 0


if __name__ == "__main__":
    test_convert_igs()
    test_convert_stp()
