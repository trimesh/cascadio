"""
Color each face by its kind of surface, and each edge between CAD faces by
the pair of surfaces it joins.

Run from the repository root; writes brep.png. Needs a few extras:
    pip install trimesh scipy pillow "pyglet<2"
On a machine without a screen, set PYGLET_HEADLESS=True.
"""

from pathlib import Path

import trimesh

import cascadio

# faces: one soft color per kind of surface; None is any other surface
FACES = {"Plane": [225, 225, 232], "Cylinder": [170, 195, 240], "Cone": [250, 215, 160]}
FACES |= {
    "Sphere": [180, 235, 180],
    "Torus": [235, 190, 232],
    "NoneType": [150, 150, 150],
}
# edges: one bright color per pair of surfaces, handed out in sorted order
BRIGHT = [[230, 25, 75], [0, 130, 200], [245, 130, 48], [60, 180, 75]]
BRIGHT += [[145, 30, 180], [0, 0, 0], [240, 50, 230], [128, 128, 0]]

step = Path("tests/models/featuretype.STEP").read_bytes()
glb = cascadio.load(step, include_brep=True)
mesh = trimesh.load_mesh(trimesh.util.wrap_as_stream(glb), file_type="glb")
index = mesh.face_attributes["brep_index"]  # per triangle: its CAD face
primitives = mesh.metadata["cascadio"]["brep_primitives"]  # per CAD face: its surface
kind = [type(primitives[i]).__name__ for i in index]  # per triangle: "Plane", ...
mesh.visual = trimesh.visual.ColorVisuals(mesh, face_colors=[FACES[k] for k in kind])
scene = trimesh.Scene(mesh)

welded = mesh.copy()  # CAD faces are meshed separately: weld them to find neighbours
welded.merge_vertices(merge_tex=True, merge_norm=True)
# an edge between two triangles from different CAD faces is a CAD edge
is_edge = index[welded.face_adjacency[:, 0]] != index[welded.face_adjacency[:, 1]]
segments = welded.vertices[welded.face_adjacency_edges[is_edge]]
# name each CAD edge after the two kinds of surface it joins, like "Cylinder-Plane"
joins = [
    "-".join(sorted([kind[a], kind[b]])) for a, b in welded.face_adjacency[is_edge]
]
for color, join in zip(BRIGHT, sorted(set(joins)), strict=False):
    path = trimesh.load_path(segments[[j == join for j in joins]])
    path.colors = [color] * len(path.entities)
    scene.add_geometry(path)
    print(join, color)

scene.set_camera(angles=(1.0, 0.0, 0.5), distance=0.2, center=mesh.centroid)
image = scene.save_image(resolution=(1100, 620), line_settings={"line_width": 3})
Path("brep.png").write_bytes(image)
