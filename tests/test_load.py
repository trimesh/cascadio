import os
import cascadio
import trimesh
import tempfile

cwd = os.path.abspath(os.path.dirname(__file__))


def test_convert_step():
    infile = os.path.join(cwd, "models", "featuretype.STEP")

    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "outfile.glb")
        # do the conversion
        cascadio.to_glb(infile, outfile, tol_linear=0.1, tol_angular=0.5)
        scene = trimesh.load(outfile, merge_primitives=True)
    assert len(scene.geometry) == 1

def test_convert_igs(    infile = os.path.join(cwd, "models", "tilt.IGS")):
    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "outfile.glb")
        # do the conversion
        cascadio.to_glb(infile, outfile, file_type=cascadio.iges, tol_linear=0.1, tol_angular=0.5)
        scene = trimesh.load(outfile, merge_primitives=True)
        assert len(scene.geometry) > 0

if __name__ == "__main__":
    test_convert()
