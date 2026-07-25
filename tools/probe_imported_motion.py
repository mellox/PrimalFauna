"""probe_imported_motion.py -- how much does each BONE actually move, in the
UAnimSequence UNREAL HAS, not in the FBX Blender exported?

WHY
The user has reported three times that the forelimbs do not move. The Blender
gate says they do -- wrist travel up 5x, elbow range up 4x, real phase lag. Both
cannot be true, and the honest next step is to measure the asset at the END of
the pipeline instead of the start. Everything between (export settings, importer
options, bone-track compression) is unexamined territory that has already
produced two silent failures on this project: clips that were secretly meshes,
and additive flags that never got set.

If the arm tracks are flat HERE, the motion is being lost in export or import
and no amount of re-authoring will fix it. If they are large here, the motion is
real and the problem is perceptual -- too subtle at gameplay distance, or hidden
by the tucked pose -- which is a completely different fix.
"""
import unreal

CLIPS = ["A_Sicklejaw_Idle", "A_Sicklejaw_Walk", "A_Sicklejaw_Run", "A_Sicklejaw_Threaten"]
# Arm chain vs a control group we KNOW animates (the tail was re-modelled and
# the user can see it move). If arms read flat and tail reads large, the
# pipeline is fine and the arm data is the problem.
BONES = ["UpperArm_L", "Forearm_L", "Hand_L", "UpperArm_R", "Tail01", "Tail03", "Thigh_L"]


def track_range(anim, bone):
    """Peak-to-peak rotation of one bone across the clip, in degrees."""
    n = anim.get_editor_property("number_of_sampled_keys")
    if not n or n <= 1:
        return None
    length = anim.get_play_length()
    lo = [999.0, 999.0, 999.0]
    hi = [-999.0, -999.0, -999.0]
    samples = min(n, 60)
    got = False
    for i in range(samples):
        t = (length * i) / max(1, samples - 1)
        try:
            xf = unreal.AnimationLibrary.get_bone_pose_for_time(anim, bone, t, False)
        except Exception:
            return None
        r = xf.rotation.rotator()
        got = True
        for axis, v in enumerate((r.roll, r.pitch, r.yaw)):
            lo[axis] = min(lo[axis], v)
            hi[axis] = max(hi[axis], v)
    if not got:
        return None
    return max(hi[a] - lo[a] for a in range(3))


for clip in CLIPS:
    path = "/PrimalFauna/Sicklejaw/Anims/%s" % clip
    anim = unreal.EditorAssetLibrary.load_asset(path)
    if not anim:
        unreal.log("PFAMOTION %s: NOT FOUND" % clip)
        continue
    parts = []
    for bone in BONES:
        rng = track_range(anim, bone)
        parts.append("%s=%s" % (bone, "n/a" if rng is None else ("%.1f" % rng)))
    unreal.log("PFAMOTION %-24s keys=%s len=%.2fs | %s" % (
        clip,
        anim.get_editor_property("number_of_sampled_keys"),
        anim.get_play_length(),
        "  ".join(parts)))

unreal.log("PFAMOTION DONE (degrees of peak-to-peak rotation per bone, in the IMPORTED asset)")
