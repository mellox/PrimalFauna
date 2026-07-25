"""List the ACTUAL bone names on the imported skeleton, and re-measure motion
using those names.

Written because probe_imported_motion.py returned 0.0 for Thigh_L as well as for
every arm bone -- and the legs visibly animate in game. A measurement that
reports "no motion" for a bone that is definitely moving is measuring the wrong
thing: get_bone_pose_for_time returns identity for an unknown bone rather than
raising, so a typo'd name is indistinguishable from a flat track. Confirm the
names FIRST; the previous numbers are worthless until this prints.
"""
import unreal

ANIM = "/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_Walk"
anim = unreal.EditorAssetLibrary.load_asset(ANIM)
if not anim:
    unreal.log("PFABONES ANIM NOT FOUND")
    raise SystemExit(1)

names = []
try:
    names = [str(n) for n in unreal.AnimationLibrary.get_animation_track_names(anim)]
    unreal.log("PFABONES source=get_animation_track_names count=%d" % len(names))
except Exception as e:
    unreal.log("PFABONES get_animation_track_names failed: %s" % e)

if not names:
    sk = unreal.EditorAssetLibrary.load_asset("/PrimalFauna/Sicklejaw/SK_Sicklejaw")
    skel = sk.get_editor_property("skeleton")
    try:
        tree = skel.get_editor_property("bone_tree")
        unreal.log("PFABONES bone_tree entries=%d (names not directly exposed)" % len(tree))
    except Exception as e:
        unreal.log("PFABONES bone_tree failed: %s" % e)

for n in names:
    unreal.log("PFABONES name %s" % n)

# Re-measure with the REAL names, for anything that looks like a limb.
def peak_range(a, bone):
    n = min(a.get_editor_property("number_of_sampled_keys") or 0, 60)
    if n <= 1:
        return None
    L = a.get_play_length()
    prev = None
    total = 0.0
    for i in range(n):
        t = (L * i) / (n - 1)
        xf = unreal.AnimationLibrary.get_bone_pose_for_time(a, bone, t, False)
        q = xf.rotation
        if prev is not None:
            # Angular distance between successive quats -- wrap-safe, unlike
            # per-euler-axis min/max which reads 359 deg on a tiny wobble.
            d = abs(q.x*prev.x + q.y*prev.y + q.z*prev.z + q.w*prev.w)
            d = min(1.0, max(-1.0, d))
            import math
            total += math.degrees(2.0 * math.acos(d))
        prev = q
    return total

for n in names:
    low = n.lower()
    if any(k in low for k in ("arm", "hand", "thigh", "calf", "foot", "tail", "clav", "finger")):
        r = peak_range(anim, n)
        unreal.log("PFABONES motion %-22s total-rotation=%s deg" % (n, "n/a" if r is None else "%.1f" % r))

unreal.log("PFABONES DONE")
