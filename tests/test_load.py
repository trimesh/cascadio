import os
import cascadio
import trimesh
import tempfile

cwd = os.path.abspath(os.path.dirname(__file__))


def test_convert():
    infile = os.path.join(cwd, "models", "featuretype.STEP")

    with tempfile.TemporaryDirectory() as D:
        outfile = os.path.join(D, "outfile.glb")
        # do the conversion
        cascadio.step_to_glb(infile, outfile, 0.1, 0.5)
        scene = trimesh.load(outfile, merge_primitives=True)

    assert len(scene.geometry) == 1


if __name__ == "__main__":
    test_convert()
