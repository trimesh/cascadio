#!/usr/bin/env python3
"""
Show a STEP model with each face colored by its surface type.

    python scripts/color_by_primitive.py [model.step]
"""

import sys
from pathlib import Path

import cascadio
import trimesh
from cascadio.primitives import Cone, Cylinder, Plane, Sphere, Torus

COLORS = {
    Plane: [220, 220, 230, 255],
    Cylinder: [100, 149, 237, 255],
    Cone: [255, 165, 0, 255],
    Sphere: [50, 205, 50, 255],
    Torus: [218, 112, 214, 255],
    type(None): [128, 128, 128, 255],  # any other kind of surface
}

default = Path(__file__).parent.parent / "tests" / "models" / "featuretype.STEP"
path = Path(sys.argv[1]) if len(sys.argv) > 1 else default

glb = cascadio.load(path.read_bytes(), include_brep=True)
scene = trimesh.load_scene(trimesh.util.wrap_as_stream(glb), file_type="glb")
for mesh in scene.geometry.values():
    primitives = mesh.metadata["cascadio"]["brep_primitives"]
    colors = [COLORS[type(primitives[i])] for i in mesh.face_attributes["brep_index"]]
    mesh.visual = trimesh.visual.ColorVisuals(mesh, face_colors=colors)
scene.show()
