import os
import cascadio
import trimesh
import tempfile

cwd = os.path.abspath(os.path.dirname(__file__))

def test_convert():
    outfile = tempfile.NamedTemporaryFile(suffix='.glb')
    infile = os.path.join(cwd, "wrench.STEP")
    
    # do it
    cascadio.step_to_glb(infile, outfile.name, .1, .5)

    scene = trimesh.load('hi.glb', merge_primitives=True)
    return scene
    
if __name__ == '__main__':

    r = test_convert()
