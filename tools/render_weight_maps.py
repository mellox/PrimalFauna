r"""render_weight_maps.py -- LOOK at the skin weights, don't just measure them.

WHY THIS EXISTS (user, 2026-07-26, after watching a Blender animation comparison)
"I noticed him showing weights in models while doing the animation... sometimes
the weights weren't done correctly. When we do our animations, are we creating
and reviewing stuff like this?"

We were not. validate_rig_fbx.py has good NUMERIC weight gates -- zero-influence
verts, unnormalised sums, influence count, limb overhang, surface-pull under
joint rotation -- and every one of them is a scalar. Three real defects on this
creature say scalars are not enough:

  * THE LEG DISCONNECT (w27, user-reported). 233 leg-majority verts above the hip
    line were owned by the thighs. The artist's FIRST joint-flex heuristic would
    not have caught it; it discarded two heuristics before finding one that did.
    On a weight map it is a colour boundary in obviously the wrong place.
  * THE JAW owning 2,681 verts INCLUDING THE UPPER SKULL (w28) -- the creature's
    whole face moved when it opened its mouth.
  * 3,456 CHEST verts labelled "arm" (w40) inside a measurement tool, which made
    a motionless arm score 86% "silhouette break". Numerically invisible.

A weight map turns all three into something you spot in a second. This is the
same lesson the rest of this pipeline keeps re-learning: the numbers say what you
asked them to; the render says what is actually there.

USAGE
    "C:\Blender\Blender 5.2\blender.exe" --background --factory-startup ^
      --python tools/render_weight_maps.py -- <rig.fbx> [--out DIR] [--bones A,B,C]

Writes one PNG per bone plus a contact sheet, and prints PFAWEIGHT lines.
Read the sheet on EVERY rig or skinning change -- that is the point of it.
"""
import os
import sys

import bpy

# Bones worth looking at every time, because each one has a history on this rig.
# Not exhaustive: pass --bones to inspect others.
# NOTE the naming: this rig is SIDE-PREFIXED (`L_UpperArm`), not suffixed
# (`UpperArm_L`). The first run of this script guessed the suffixed form and
# silently skipped every arm bone -- printing "no vertex group" for exactly the
# chain under investigation. A skip that reads as "nothing to see" is the same
# failure class as check_imports.ps1 skipping 623 imports behind a green tick,
# so the skips are printed loudly and counted in the summary.
DEFAULT_BONES = [
    "Hip", "Pelvis",                    # the leg-disconnect boundary
    "L_Thigh", "R_Thigh",               # what stole the pelvic mass
    "Jaw",                              # the clip that owned the upper skull
    "L_Clavicle", "R_Clavicle",         # the "3,456 chest verts are arm" boundary
    "L_UpperArm", "R_UpperArm", "L_Forearm", "L_Hand",
    "Neck_01", "Neck_02", "Neck_03", "Head",   # the "headless at the shoulders" chain
    "Tail01", "Tail03", "Tail05",
]


def argv_after_dashdash():
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def log(msg):
    print("PFAWEIGHT " + msg)


args = argv_after_dashdash()
if not args:
    log("FAIL: no FBX given")
    sys.exit(1)

fbx = args[0]
out_dir = args[args.index("--out") + 1] if "--out" in args else os.path.join(os.path.dirname(fbx), "renders", "weights")
bones = args[args.index("--bones") + 1].split(",") if "--bones" in args else DEFAULT_BONES
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=fbx)

meshes = [o for o in bpy.data.objects if o.type == "MESH"]
if len(meshes) != 1:
    log("FAIL: expected exactly one skinned mesh, found %d" % len(meshes))
    sys.exit(1)
mesh = meshes[0]

# Vertex-colour layer that the material reads. Painting weights into COLOUR is
# what makes this readable at a glance: blue = this bone owns none of it,
# red = it owns all of it, and the gradient between is where the deformation
# actually blends. A hard blue/red edge in the middle of a limb is the defect.
if not mesh.data.vertex_colors:
    mesh.data.vertex_colors.new(name="WeightVis")
vcol = mesh.data.vertex_colors["WeightVis"]

mat = bpy.data.materials.new("WeightVis")
mat.use_nodes = True
nt = mat.node_tree
for n in list(nt.nodes):
    nt.nodes.remove(n)
attr = nt.nodes.new("ShaderNodeVertexColor")
attr.layer_name = "WeightVis"
emit = nt.nodes.new("ShaderNodeEmission")          # emission: no lighting to misread
out_node = nt.nodes.new("ShaderNodeOutputMaterial")
nt.links.new(attr.outputs["Color"], emit.inputs["Color"])
nt.links.new(emit.outputs["Emission"], out_node.inputs["Surface"])
mesh.data.materials.clear()
mesh.data.materials.append(mat)

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in \
    [i.identifier for i in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "BLENDER_EEVEE"
scene.render.resolution_x, scene.render.resolution_y = 900, 900
scene.render.film_transparent = False

# One fixed three-quarter camera for every bone, so the sheet is comparable and
# a boundary that moves between bones is obviously a boundary, not a camera move.
# Frame the mesh from its ACTUAL world bounding box. The first version placed the
# camera from mesh.dimensions and a hand-tuned rotation, and put the creature's
# HEAD out of frame -- on a sheet whose whole purpose was diagnosing the neck.
# A diagnostic image that silently crops the region under investigation is worse
# than no image, because it looks like evidence. Compute it, don't eyeball it.
from mathutils import Vector

corners = [mesh.matrix_world @ Vector(c) for c in mesh.bound_box]
centre = sum(corners, Vector((0, 0, 0))) / 8.0
radius = max((c - centre).length for c in corners)

cam_data = bpy.data.cameras.new("WeightCam")
cam = bpy.data.objects.new("WeightCam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam

# Three-quarter view, pulled back far enough that the whole bounding sphere fits
# the narrower FOV axis with margin.
import math

direction = Vector((0.85, -1.0, 0.35)).normalized()
distance = radius / math.tan(0.5 * cam_data.angle) * 1.35
cam.location = centre + direction * distance
# Point the camera at the centre: -Z is the camera's forward axis.
cam.rotation_euler = (centre - cam.location).to_track_quat('-Z', 'Y').to_euler()

log("framing: centre=(%.2f, %.2f, %.2f) radius=%.2f distance=%.2f"
    % (centre.x, centre.y, centre.z, radius, distance))

written = []
for bone in bones:
    vg = mesh.vertex_groups.get(bone)
    if not vg:
        log("skip %-14s (no vertex group)" % bone)
        continue

    weights = [0.0] * len(mesh.data.vertices)
    owned = 0
    for v in mesh.data.vertices:
        for g in v.groups:
            if g.group == vg.index:
                weights[v.index] = g.weight
                if g.weight >= 0.5:
                    owned += 1
                break

    for loop in mesh.data.loops:
        w = weights[loop.vertex_index]
        # blue (none) -> green (shared) -> red (fully owned). The green band IS
        # the blend region; a missing green band means a hard seam that will
        # crease, and green in the wrong place means the wrong bone owns it.
        vcol.data[loop.index].color = (w, 1.0 - abs(w - 0.5) * 2.0, 1.0 - w, 1.0)

    path = os.path.join(out_dir, "WEIGHT_%s.png" % bone)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    written.append(path)
    log("%-14s majority-owned verts=%-6d peak=%.2f -> %s"
        % (bone, owned, max(weights), os.path.basename(path)))

log("wrote %d weight maps to %s" % (len(written), out_dir))
log("DONE -- read these on EVERY rig or skinning change; a hard colour edge "
    "mid-limb, or a colour that crosses an anatomical boundary, is the defect.")
