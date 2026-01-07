#!/usr/bin/env python3
"""Color faces of a STEP model by primitive surface type."""

from cascadio.primitives import Cone, Cylinder, Plane, Sphere, Torus

import cascadio
import trimesh

COLORS = {
    Plane: [220, 220, 230, 255],
    Cylinder: [100, 149, 237, 255],
    Cone: [255, 165, 0, 255],
    Sphere: [50, 205, 50, 255],
    Torus: [218, 112, 214, 255],
    type(None): [128, 128, 128, 255],
}

glb = cascadio.load(open("../../models/featuretype.STEP", "rb").read(), include_brep=True)
scene = trimesh.load_scene(trimesh.util.wrap_as_stream(glb), file_type="glb")
for mesh in scene.geometry.values():
    prims = mesh.metadata.get("cascadio", {}).get("brep_primitives", [])
    idx = mesh.face_attributes.get("brep_index", []).flatten()
    colors = [
        COLORS.get(type(prims[i]) if 0 <= i < len(prims) else None, COLORS[type(None)])
        for i in idx
    ]
    mesh.visual = trimesh.visual.ColorVisuals(mesh, face_colors=colors)
scene.show()
