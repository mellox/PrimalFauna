r"""
validate_anim_fbx.py -- headless Blender gate that judges a creature ANIMATION
before it is ever imported into Unreal, let alone launched in the game.

WHY THIS EXISTS (user, 2026-07-25): "Should we be testing the animation in
blender before even going to satisfactory". Yes. Every animation defect this
project has hit -- a humanoid gait on a raptor, one leg driven by arm motion,
a mesh 100x too small, clips that were actually the mesh -- was visible in the
source data and was instead discovered by the user watching the game, minutes
to sessions later. Blender answers in seconds and costs no build, no launch,
no load, no spawn, and none of the user's attention.

WHAT IT MEASURES (a gait cycle is a physical object, so it is checkable):
  1. IN-PLACE          root translation ~ 0 (the game capsule drives movement)
  2. LEGS ALIVE        each leg's foot actually moves; a dead leg is the
                       "one leg kicks up and down" defect
  3. ANTIPHASE         left and right feet contact ~half a cycle apart --
                       the signature that separates a real gait from a
                       humanoid clip stretched over the wrong skeleton
  4. GROUNDED          the lowest foot reaches ~the rig's ground plane
                       (feet neither float above it nor sink through)
  5. IMPLIED SPEED     during contact the stance foot tracks backward at a
                       constant rate; that rate IS the speed the creature
                       must move in-game or the feet visibly slide. Compare
                       it to the species' configured move speeds.
  6. SANITY            no NaN / no absurd bone excursions

USAGE (exit 0 = pass, 1 = reject; findings printed as PFAANIM ...):
    "C:\Blender\Blender 5.2\blender.exe" --background --factory-startup ^
      --python "C:/Claude/Projects/PrimalFauna/tools/validate_anim_fbx.py" ^
      -- "C:/path/A_Creature_Walk.fbx" [--expect-speed-cms 400] [--report out.json]

Speed check is advisory unless --expect-speed-cms is passed.
"""
import json
import math
import os
import sys

import bpy
from mathutils import Vector


FOOT_PATTERNS = ("foot", "ankle", "toebase", "toe_")
ROOT_PATTERNS = ("root",)
# anything matching these is ALLOWED at/below the ground plane -- it is what stands on it
GROUNDED_PATTERNS = ("foot", "ankle", "toe", "finger", "hand", "root")
GROUND_PENETRATION_WARN_M = 0.005     # a bone this far under the floor: report it
GROUND_PENETRATION_FAIL_M = 0.030     # this far under: the mesh is visibly through it

LEFT_TOKENS = ("left", "_l_", ".l", "-l-", "l_")
RIGHT_TOKENS = ("right", "_r_", ".r", "-r-", "r_")

IN_PLACE_DRIFT_TOLERANCE_M = 0.05     # root may wobble, may not travel
DEAD_LEG_MIN_TRAVEL_M = 0.03          # a foot that moves less than this is dead
ANTIPHASE_TOLERANCE = 0.18            # |phase - 0.5| must be under this
GROUND_CONTACT_BAND_M = 0.05          # "in contact" = within this of lowest Z
GROUND_PLANE_TOLERANCE_M = 0.08       # lowest foot should reach ~rig origin Z
SPEED_MISMATCH_TOLERANCE = 0.35       # +/-35% before feet read as sliding

failures = []
warnings = []


def log(msg):
    print("PFAANIM " + msg)


def fail(msg):
    failures.append(msg)
    print("PFAANIM FAIL: " + msg)


def warn(msg):
    warnings.append(msg)
    print("PFAANIM WARN: " + msg)


def side_of(name):
    lowered = name.lower()
    if any(t in lowered for t in LEFT_TOKENS):
        return "L"
    if any(t in lowered for t in RIGHT_TOKENS):
        return "R"
    return None


def parse_args():
    argv = sys.argv
    if "--" not in argv:
        print("PFAANIM FAIL: no FBX path given (pass it after --)")
        sys.exit(1)
    args = argv[argv.index("--") + 1:]
    fbx = args[0]
    opt = {"expect_speed": None, "report": None, "ref_rig": None,
           "sickle": None, "claw_down": False}
    if "--expect-speed-cms" in args:
        opt["expect_speed"] = float(args[args.index("--expect-speed-cms") + 1])
    if "--report" in args:
        opt["report"] = args[args.index("--report") + 1]
    if "--ref-rig" in args:
        opt["ref_rig"] = args[args.index("--ref-rig") + 1]
    if "--sickle-digit" in args:
        opt["sickle"] = args[args.index("--sickle-digit") + 1]
    if "--claw-down" in args:
        opt["claw_down"] = True
    return fbx, opt


def reference_joints(ref_fbx):
    """Rest-pose joint positions from the SKELETAL MESH fbx -- the skeleton UE actually
    binds to. Needed because an animation-only FBX does NOT round-trip a rest pose:
    with no skin deformer in the file, Blender's importer takes the bones' rest from
    the node defaults, which is the pose at export time. Measured on this rig, the anim
    FBXs' apparent rest sits up to 7.3 cm from the real one -- so 'is frame 1 the
    reference pose?' cannot be asked of an animation file on its own, and the check
    that tried (comparing matrix_basis to identity at frame 1) was a TAUTOLOGY: the
    importer defines rest AS frame 1, so identity is exactly what it always found."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=ref_fbx)
    arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if len(arms) != 1:
        return None
    armature = arms[0]
    return {pb.name: (armature.matrix_world @ pb.head).copy()
            for pb in armature.pose.bones}


def sample_world_positions(armature, bone_name, frames):
    """World-space head position of a bone per frame."""
    positions = []
    for frame in frames:
        bpy.context.scene.frame_set(frame)
        pose_bone = armature.pose.bones[bone_name]
        positions.append((armature.matrix_world @ pose_bone.head).copy())
    return positions


def main():
    fbx_path, opt = parse_args()
    expect_speed_cms, report_path = opt["expect_speed"], opt["report"]
    if not os.path.isfile(fbx_path):
        fail("FBX not found: %s" % fbx_path)
        sys.exit(1)

    ref_joints = None
    if opt["ref_rig"]:
        if os.path.isfile(opt["ref_rig"]):
            ref_joints = reference_joints(opt["ref_rig"])
            log("reference skeleton loaded from %s (%d bones)"
                % (os.path.basename(opt["ref_rig"]), len(ref_joints or {})))
        else:
            warn("--ref-rig not found (%s) -- blend-safety cannot be judged against "
                 "the real skeleton" % opt["ref_rig"])

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=fbx_path)
    log("loaded %s" % os.path.basename(fbx_path))

    armatures = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if len(armatures) != 1:
        fail("expected exactly ONE armature, found %d" % len(armatures))
        sys.exit(1)
    armature = armatures[0]

    action = armature.animation_data.action if armature.animation_data else None
    if action is None:
        fail("armature carries NO action -- this FBX has no animation on it "
             "(the mesh-twin defect: the take landed somewhere else)")
        sys.exit(1)

    start, end = (int(action.frame_range[0]), int(action.frame_range[1]))
    frame_count = end - start + 1
    fps = bpy.context.scene.render.fps
    duration = frame_count / float(fps) if fps else 0.0
    log("action '%s' frames %d..%d (%d) @ %dfps = %.2fs"
        % (action.name, start, end, frame_count, fps, duration))
    if frame_count < 2:
        fail("action has %d frame(s) -- not an animation" % frame_count)
        sys.exit(1)

    frames = list(range(start, end + 1))
    bone_names = [b.name for b in armature.pose.bones]

    # --- ADDITIVE LAYERS take a different gate entirely -----------------------
    # An additive layer (breathing, head saccades, tail drift, per-mood tail
    # attitude) is designed to blend OVER a base clip. It deliberately keys no
    # foot bones -- if it did, it would inject that foot motion into every clip
    # it rides on. Judging it by the gait rules (dead-leg, antiphase, ground
    # speed, cadence) is a category error, and mine was failing 12 correct
    # assets for it. The real correctness criteria for an additive are:
    # something moves, feet DON'T, and frame 1 sits at the reference pose so
    # blending in produces no pop.
    if "add_" in os.path.basename(fbx_path).lower():
        log("ADDITIVE layer detected -- gait checks (dead-leg/antiphase/speed/cadence) "
            "do not apply; judging blend-safety instead")

        # BLEND SAFETY, measured against the REAL skeleton (see reference_joints).
        # The old version of this check read matrix_basis at frame 1 and compared it to
        # identity. Because Blender's importer defines an anim-only FBX's rest pose AS
        # frame 1, that comparison could never fail -- it passed 12 layers, one of which
        # (Add_Saccade) was shipping a foot 34.8 cm out of place.
        bpy.context.scene.frame_set(frames[0])
        if ref_joints:
            worst = (0.0, None)
            for pose_bone in armature.pose.bones:
                ref = ref_joints.get(pose_bone.name)
                if ref is None:
                    continue
                dev = ((armature.matrix_world @ pose_bone.head) - ref).length
                if dev > worst[0]:
                    worst = (dev, pose_bone.name)
            log("frame-1 joint deviation from the SKELETON's reference pose: "
                "%.4fm (worst bone: %s)" % (worst[0], worst[1] or "none"))
            if worst[0] > 0.01:
                fail("additive does not START at the skeleton's reference pose (worst "
                     "%.4fm on '%s') -- as an additive layer that is a CONSTANT offset "
                     "added to every clip it rides over, not just a pop on blend-in. "
                     "Usual cause: the export baked a stale pose onto bones this layer "
                     "does not key." % (worst[0], worst[1]))
        else:
            warn("additive blend-safety NOT checked: pass --ref-rig <SK_*.fbx>. "
                 "Checking matrix_basis at frame 1 instead is a tautology on an "
                 "animation-only FBX and tells you nothing.")

        moved_any = False
        foot_movers = []
        for name in bone_names:
            positions = sample_world_positions(armature, name, frames)
            span = max((positions[i] - positions[0]).length for i in range(len(positions)))
            if span > 0.002:
                moved_any = True
                if any(p in name.lower() for p in FOOT_PATTERNS):
                    foot_movers.append("%s(%.3fm)" % (name, span))
        if not moved_any:
            fail("additive layer animates NOTHING -- every bone is static across the clip")
        if foot_movers:
            fail("additive layer moves FOOT bones (%s) -- that motion would be injected into "
                 "every clip this layer rides over" % ", ".join(foot_movers))
        else:
            log("feet correctly untouched by this additive layer")

        # The additive path used to return WITHOUT writing a report, so a report file
        # from an earlier sweep survived on disk and read as current -- including FAILs
        # from the era when additives were (wrongly) judged by the gait rules. A stale
        # report is worse than no report: it is evidence that lies.
        if report_path:
            os.makedirs(os.path.dirname(report_path), exist_ok=True)
            with open(report_path, "w", encoding="utf-8") as handle:
                json.dump({"source_fbx": fbx_path, "action": action.name,
                           "frames": frame_count, "fps": fps, "additive": True,
                           "failures": failures, "warnings": warnings}, handle, indent=2)
            log("report written: %s" % report_path)
        log("SUMMARY failures=%d warnings=%d" % (len(failures), len(warnings)))
        if failures:
            log("VERDICT: REJECT -- do not import this layer")
            sys.exit(1)
        log("VERDICT: ACCEPT (additive)")
        return

    # --- locate feet (prefer the deepest foot-ish bone per side) -------------
    feet = {}
    for side in ("L", "R"):
        candidates = [n for n in bone_names
                      if any(p in n.lower() for p in FOOT_PATTERNS) and side_of(n) == side]
        if candidates:
            # toe/ankle tips sit lowest; pick the one with the lowest rest Z
            candidates.sort(key=lambda n: (armature.matrix_world @ armature.pose.bones[n].head).z)
            feet[side] = candidates[0]
    if len(feet) != 2:
        fail("could not identify a left AND right foot bone (found: %s) -- "
             "rig naming is not role-based/symmetric, so gait cannot be judged"
             % (", ".join("%s=%s" % (s, b) for s, b in feet.items()) or "none"))
        sys.exit(1)
    log("feet: L=%s R=%s" % (feet["L"], feet["R"]))

    # --- 1. in-place check ---------------------------------------------------
    root_name = None
    for name in bone_names:
        bone = armature.pose.bones[name]
        if bone.parent is None:
            root_name = name
            break
    if root_name:
        root_positions = sample_world_positions(armature, root_name, frames)
        xs = [p.x for p in root_positions]
        ys = [p.y for p in root_positions]
        drift = math.hypot(max(xs) - min(xs), max(ys) - min(ys))
        log("root '%s' horizontal drift = %.3fm" % (root_name, drift))
        if drift > IN_PLACE_DRIFT_TOLERANCE_M:
            fail("clip is NOT in-place: root travels %.3fm (tolerance %.3fm). The game capsule "
                 "drives movement, so root motion double-moves the creature."
                 % (drift, IN_PLACE_DRIFT_TOLERANCE_M))

    # --- sample both feet ----------------------------------------------------
    foot_tracks = {side: sample_world_positions(armature, name, frames)
                   for side, name in feet.items()}

    for position_list in foot_tracks.values():
        for position in position_list:
            if any(math.isnan(component) or math.isinf(component) for component in position):
                fail("non-finite bone position in the clip (NaN/Inf keyframes)")
                sys.exit(1)

    # --- 2. dead-leg check ---------------------------------------------------
    travel = {}
    for side, positions in foot_tracks.items():
        span_x = max(p.x for p in positions) - min(p.x for p in positions)
        span_y = max(p.y for p in positions) - min(p.y for p in positions)
        span_z = max(p.z for p in positions) - min(p.z for p in positions)
        travel[side] = max(span_x, span_y, span_z)
        log("foot %s (%s) travel: x=%.3f y=%.3f z=%.3f m" % (side, feet[side], span_x, span_y, span_z))
    for side, distance in travel.items():
        if distance < DEAD_LEG_MIN_TRAVEL_M:
            fail("the %s foot barely moves (%.3fm) -- a dead/mis-driven leg. This is the "
                 "'one leg kicks up and down' defect." % (side, distance))

    # --- 3. antiphase --------------------------------------------------------
    def lowest_frame_index(positions):
        return min(range(len(positions)), key=lambda i: positions[i].z)

    low_l = lowest_frame_index(foot_tracks["L"])
    low_r = lowest_frame_index(foot_tracks["R"])
    phase = abs(low_l - low_r) / float(frame_count)
    phase = min(phase, 1.0 - phase) * 2.0   # normalise so 1.0 == half a cycle
    log("contact phase offset = %.2f of a half-cycle (L deepest @f%d, R @f%d)"
        % (phase, frames[low_l], frames[low_r]))
    if abs(phase - 1.0) > ANTIPHASE_TOLERANCE * 2:
        fail("feet are NOT in antiphase (offset %.2f, want ~1.0) -- both legs move together, "
             "which reads as hopping/pumping rather than walking" % phase)

    # --- 4. grounded ---------------------------------------------------------
    lowest_z = min(min(p.z for p in positions) for positions in foot_tracks.values())
    log("lowest foot Z across the clip = %.3fm (rig origin = 0)" % lowest_z)
    if abs(lowest_z) > GROUND_PLANE_TOLERANCE_M:
        warn("lowest foot sits %.3fm from the rig's ground plane -- the creature will look like "
             "it floats or sinks unless the runtime foot-plant compensates" % lowest_z)

    # --- 4b. GROUND PENETRATION of everything that is NOT a foot -------------
    # Added W2.9. Check 4 above asks whether the feet REACH the ground; nothing asked
    # whether the rest of the animal stays above it. Measured across the shipped set,
    # five clips had the tail tip 4.5-22.7 cm UNDER the floor (Death -22.7, Stagger
    # -20.4, Lunge -18.9, Hurt -11.0, Bite -4.6) and every one of them was ACCEPTed by
    # this validator. The number even existed in the authoring self-check -- but only
    # for the ten clips authored AFTER the measurement was added, which is the whole
    # lesson: a measurement that does not run on everything is not a gate.
    deepest = (1e9, None, None)
    for name in bone_names:
        if any(p in name.lower() for p in GROUNDED_PATTERNS):
            continue
        for frame, position in zip(frames, sample_world_positions(armature, name, frames)):
            if position.z < deepest[0]:
                deepest = (position.z, name, frame)
    log("deepest non-foot bone: %s at %.3fm (frame %s)"
        % (deepest[1], deepest[0], deepest[2]))
    if deepest[0] < -GROUND_PENETRATION_FAIL_M:
        fail("GROUND PENETRATION: '%s' reaches %.3fm BELOW the ground plane at frame %s "
             "(limit %.3fm). Bones sit inside the mesh, so the surface is deeper still -- "
             "this reads as the creature clipping through the floor."
             % (deepest[1], deepest[0], deepest[2], -GROUND_PENETRATION_FAIL_M))
    elif deepest[0] < -GROUND_PENETRATION_WARN_M:
        warn("'%s' dips %.3fm below the ground plane at frame %s -- acceptable for a body "
             "part that is meant to rest on the ground, wrong for anything else"
             % (deepest[1], deepest[0], deepest[2]))

    # --- 4c. SICKLE CLAW (digit II) -----------------------------------------
    # Research 2.3 and 6 [M]: the dromaeosaur sickle claw is held CLEAR of the ground at
    # all times except the pin. Measured relatively rather than absolutely, because a
    # childless bone's tip does not survive the FBX round trip -- but "digit II's last
    # joint is the highest of the three toes" is exactly the research requirement and it
    # is round-trip safe. Skipped unless the caller names the digit: the token is
    # species-specific and a gate that guesses is a gate that cries wolf.
    if opt["sickle"]:
        for side in ("L", "R"):
            distal = [n for n in bone_names
                      if "_toe" in n.lower() and n.endswith("_02") and side_of(n) == side]
            if len(distal) < 2:
                continue
            mins = {}
            for name in distal:
                mins[name] = min(p.z for p in sample_world_positions(armature, name, frames))
            sickle = [n for n in distal if opt["sickle"] in n]
            if not sickle:
                warn("no digit matching '%s' on side %s" % (opt["sickle"], side))
                continue
            sk = sickle[0]
            others = {n: v for n, v in mins.items() if n != sk}
            highest = max(mins, key=lambda n: mins[n])
            log("%s toes' lowest joint Z: %s  (sickle=%s)"
                % (side, ", ".join("%s %.3f" % (n.split("_")[-2], mins[n])
                                   for n in sorted(mins)), sk.split("_")[-2]))
            if opt["claw_down"]:
                log("  claw-down clip: digit II is MEANT to come down here, reported only")
            elif highest != sk:
                fail("digit II (%s) is NOT the highest toe (%s sits %.3fm higher) -- the "
                     "sickle claw must ride clear of the ground at all times except the "
                     "pin (research 2.3/6). A claw ploughing alongside the weight-bearing "
                     "digits is the single cheapest 'this is a raptor' read, thrown away."
                     % (sk, highest, mins[highest] - mins[sk]))
            elif mins[sk] - max(others.values()) < 0.01:
                warn("digit II (%s) is the highest toe but only by %.3fm -- the clearance "
                     "will not read at gameplay distance"
                     % (sk, mins[sk] - max(others.values())))

    # --- 5. implied ground speed --------------------------------------------
    implied_speeds = []
    for side, positions in foot_tracks.items():
        contact_band = lowest_z + GROUND_CONTACT_BAND_M
        contact_frames = [i for i, p in enumerate(positions) if p.z <= contact_band]
        if len(contact_frames) < 2:
            warn("foot %s never holds a contact phase (%d frames in the ground band) -- "
                 "cannot derive ground speed" % (side, len(contact_frames)))
            continue
        steps = []
        for a, b in zip(contact_frames, contact_frames[1:]):
            if b - a != 1:
                continue   # only consecutive contact frames
            delta = positions[b] - positions[a]
            steps.append(math.hypot(delta.x, delta.y))
        if not steps:
            continue
        steps.sort()
        median_step = steps[len(steps) // 2]
        implied = median_step * fps * 100.0   # m/frame -> cm/s
        implied_speeds.append(implied)
        log("foot %s: %d contact frames, median step %.4fm -> implied ground speed %.0f cm/s"
            % (side, len(contact_frames), median_step, implied))

    implied_avg = sum(implied_speeds) / len(implied_speeds) if implied_speeds else 0.0
    if implied_speeds:
        log("IMPLIED GROUND SPEED = %.0f cm/s (match the creature's move speed to this, "
            "or the feet will visibly slide)" % implied_avg)
        if expect_speed_cms:
            ratio = implied_avg / expect_speed_cms if expect_speed_cms else 0.0
            log("configured move speed = %.0f cm/s, ratio = %.2f" % (expect_speed_cms, ratio))
            if abs(ratio - 1.0) > SPEED_MISMATCH_TOLERANCE:
                fail("animation implies %.0f cm/s but the creature is configured to move at %.0f cm/s "
                     "(%.0f%% off) -- feet WILL slide in game. Fix the clip's cadence or the move speed."
                     % (implied_avg, expect_speed_cms, abs(ratio - 1.0) * 100.0))

    # --- 6. CADENCE (Alexander's stride relation) ----------------------------
    # Added after the behaviour research found Walk ~52% and Run ~29% too fast
    # in STEP FREQUENCY while both still passed every check above. They passed
    # because foot-slide only proves stride_length x frequency == speed: a
    # creature taking tiny mincing steps very fast covers exactly the right
    # ground and reads completely wrong. Alexander (1976) relates stride length
    # to hip height and speed across animals of wildly different size:
    #     S / h = 2.3 * (v^2 / (g*h))^0.3
    # so the correct cycle duration for a given speed falls straight out. This
    # is the difference between "moves at the right speed" and "moves like an
    # animal of this size".
    GRAVITY = 9.81
    CADENCE_TOLERANCE = 0.25   # +/-25% before the gait reads as wrong-sized

    if implied_speeds and implied_avg > 1.0:
        # Hip height = the pelvis/hip bone's rest height above the lowest foot,
        # which is the 'h' Alexander's relation is defined against.
        hip_bone = None
        for name in bone_names:
            lowered = name.lower()
            if "pelvis" in lowered or ("hip" in lowered and side_of(name) is None):
                hip_bone = name
                break
        if hip_bone:
            bpy.context.scene.frame_set(frames[0])
            hip_world = armature.matrix_world @ armature.pose.bones[hip_bone].head
            hip_height_m = hip_world.z - lowest_z
            speed_ms = implied_avg / 100.0
            if hip_height_m > 0.05 and speed_ms > 0.05:
                froude = (speed_ms * speed_ms) / (GRAVITY * hip_height_m)
                stride_m = 2.3 * hip_height_m * (froude ** 0.3)
                expected_cycle_s = stride_m / speed_ms
                actual_cycle_s = duration
                ratio = actual_cycle_s / expected_cycle_s
                log("cadence: hip height %.2fm, Froude %.2f -> expected stride %.2fm, "
                    "expected cycle %.2fs vs actual %.2fs (ratio %.2f)"
                    % (hip_height_m, froude, stride_m, expected_cycle_s, actual_cycle_s, ratio))
                # Gaits that are DELIBERATELY off natural cadence: a stalk is
                # slow and careful by intent, a limp is asymmetric by injury.
                # Alexander's relation describes efficient natural locomotion,
                # so judging these against it is a category error -- report the
                # number, don't fail the clip.
                deliberate = any(token in os.path.basename(fbx_path).lower()
                                 for token in ("prowl", "stalk", "sneak", "limp", "creep"))
                if abs(ratio - 1.0) > CADENCE_TOLERANCE and deliberate:
                    warn("cadence is %.0f%% off the natural stride for this speed, but this clip "
                         "reads as a DELIBERATE gait (stalk/limp) -- off-cadence is the intent, "
                         "so this is reported, not failed. Confirm it looks purposeful, not broken."
                         % (abs(ratio - 1.0) * 100.0))
                elif abs(ratio - 1.0) > CADENCE_TOLERANCE:
                    faster_slower = "TOO FAST" if ratio < 1.0 else "TOO SLOW"
                    fail("cadence %s: this clip steps at %.2fs/cycle but an animal with a %.2fm "
                         "hip height moving at %.0f cm/s strides every %.2fs (%.0f%% off). The "
                         "speed is right but the STEP SIZE is wrong -- it will read as mincing "
                         "or lumbering. Re-time to ~%d frames @%dfps."
                         % (faster_slower, actual_cycle_s, hip_height_m, implied_avg,
                            expected_cycle_s, abs(ratio - 1.0) * 100.0,
                            int(round(expected_cycle_s * fps)), fps))
            else:
                warn("cadence check skipped: hip height %.2fm / speed %.2fm/s out of range"
                     % (hip_height_m, speed_ms))
        else:
            warn("cadence check skipped: no pelvis/hip bone found to measure hip height")

    # --- verdict -------------------------------------------------------------
    if report_path:
        report = {
            "source_fbx": fbx_path,
            "action": action.name,
            "frames": frame_count,
            "fps": fps,
            "duration_s": round(duration, 3),
            "feet": feet,
            "foot_travel_m": {k: round(v, 4) for k, v in travel.items()},
            "phase_offset_half_cycles": round(phase, 3),
            "lowest_foot_z_m": round(lowest_z, 4),
            "deepest_nonfoot": [round(deepest[0], 4), deepest[1], deepest[2]],
            "implied_ground_speed_cms": round(implied_avg, 1),
            "failures": failures,
            "warnings": warnings,
        }
        os.makedirs(os.path.dirname(report_path), exist_ok=True)
        with open(report_path, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2)
        log("report written: %s" % report_path)

    log("SUMMARY failures=%d warnings=%d" % (len(failures), len(warnings)))
    if failures:
        log("VERDICT: REJECT -- do not import this clip")
        sys.exit(1)
    log("VERDICT: ACCEPT")


main()
