"""
Color each face by its kind of surface and outline the CAD faces.

Run from the repository root; writes brep.png. Needs a few extras:
    pip install trimesh scipy pillow "pyglet<2"
On a machine without a screen, set PYGLET_HEADLESS=True.
"""

from pathlib import Path

import trimesh

import cascadio
from cascadio.primitives import Cone, Cylinder, Plane, Sphere, Torus

# light gray, blue, orange, green, purple; dark gray for any other surface
COLORS = {Plane: [220, 220, 230], Cylinder: [100, 149, 237], Cone: [255, 165, 0]}
COLORS |= {Sphere: [50, 205, 50], Torus: [218, 112, 214], type(None): [96, 96, 96]}

step = Path("tests/models/featuretype.STEP").read_bytes()
glb = cascadio.load(step, include_brep=True)
mesh = trimesh.load_mesh(trimesh.util.wrap_as_stream(glb), file_type="glb")
index = mesh.face_attributes["brep_index"]  # per triangle: its CAD face
kinds = mesh.metadata["cascadio"]["brep_primitives"]  # per CAD face: its surface
mesh.visual = trimesh.visual.ColorVisuals(
    mesh, face_colors=[COLORS[type(kinds[i])] for i in index]
)

welded = mesh.copy()  # CAD faces are meshed separately: weld them to find neighbours
welded.merge_vertices(merge_tex=True, merge_norm=True)
# an edge between two triangles from different CAD faces is a CAD edge
is_edge = index[welded.face_adjacency[:, 0]] != index[welded.face_adjacency[:, 1]]
edges = trimesh.load_path(welded.vertices[welded.face_adjacency_edges[is_edge]])

scene = trimesh.Scene([mesh, edges])
scene.set_camera(angles=(1.0, 0.0, 0.5), distance=0.2, center=mesh.centroid)
Path("brep.png").write_bytes(scene.save_image(resolution=(1100, 620)))
