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
    expect_speed = None
    report = None
    if "--expect-speed-cms" in args:
        expect_speed = float(args[args.index("--expect-speed-cms") + 1])
    if "--report" in args:
        report = args[args.index("--report") + 1]
    return fbx, expect_speed, report


def sample_world_positions(armature, bone_name, frames):
    """World-space head position of a bone per frame."""
    positions = []
    for frame in frames:
        bpy.context.scene.frame_set(frame)
        pose_bone = armature.pose.bones[bone_name]
        positions.append((armature.matrix_world @ pose_bone.head).copy())
    return positions


def main():
    fbx_path, expect_speed_cms, report_path = parse_args()
    if not os.path.isfile(fbx_path):
        fail("FBX not found: %s" % fbx_path)
        sys.exit(1)

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

        bpy.context.scene.frame_set(frames[0])
        worst_bone, worst_dev = None, 0.0
        for pose_bone in armature.pose.bones:
            translation = pose_bone.matrix_basis.to_translation().length
            angle = abs(pose_bone.matrix_basis.to_quaternion().angle)
            deviation = max(translation, angle)
            if deviation > worst_dev:
                worst_bone, worst_dev = pose_bone.name, deviation
        log("frame-1 deviation from reference pose: %.6f (worst bone: %s)"
            % (worst_dev, worst_bone or "none"))
        if worst_dev > 0.02:
            fail("additive does not START at the reference pose (worst %.4f on '%s') -- it will "
                 "POP the instant it blends in" % (worst_dev, worst_bone))

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
