r"""
import_sicklejaw_ue.py -- W2.2: import the APPROVED Sicklejaw rebuild
(art/wip/sicklejaw-rebuild/final/SK_Sicklejaw.fbx, 48-bone tail-chain rig,
embedded 2048 textures) + its 5 retargeted preset anims into the plugin's
content at /PrimalFauna/Sicklejaw/, replacing the placeholder era.

Template: import_placeholder_ue.py (proven), plus the 2026-07-24 import
STANDARD from the invisible-Sicklejaw saga (cookbook §22 / doc 05 rider):

  1. ASSERTS, not sanity logs. The placeholder shipped ~1.65cm tall with
     zero-radius bounds and nobody noticed for 15 builds. This script FAILS
     (PFA_ ASSERT-FAIL + SystemExit(1), no "PFA_ DONE" line) instead of
     shipping a degenerate asset.
  2. SELF-CORRECTING SCALE: Blender FBX in meter units + import_uniform_scale
     1.0 = a 100x shrink (laser rifle 0.53cm; placeholder 1.65cm). Import at
     1.0, MEASURE, and if the height reads 0.5..10cm re-import everything at
     uniform scale 100 so the ASSET is right and the C++ auto-scale
     self-neutralizes (PFASicklejaw assign log must then read scaleFactor=1.0).
  3. save_directory ALWAYS (sf-skeletal-mesh-import-crash: task.save only
     persists primaries; a dangling _Skeleton crashes on first spawn).
  4. DISK-VERIFY every load-bearing asset before declaring success.

Run by the orchestrator only (game closed, no build running, nothing else
memory-heavy -- headless UnrealEditor-Cmd holds ~28GB while alive):

    "C:\Program Files\Unreal Engine - CSS\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
      "C:\Claude\Projects\SatisfactoryModLoader\FactoryGame.uproject" ^
      -ExecutePythonScript="C:/Claude/Projects/PrimalFauna/tools/import_sicklejaw_ue.py" ^
      -EnablePlugins=PythonScriptPlugin -unattended -nopause -nosplash -nullrhi ^
      -stdout -FullStdOutLogOutput -NoLogTimes -utf8output

Success = grep the output for:  PFA_ ALL-ASSETS-ON-DISK=True  and  PFA_ DONE
Failure = any  PFA_ ASSERT-FAIL  line (and no DONE).
"""
import os
import unreal

# 2026-07-25: the CANDIDATE-D line. The user rejected the W2.1b rebuild mesh
# ("goofy... not something to be afraid of") and the research separately found
# its rig anatomically broken, so both were abandoned; sicklejaw-rebuild/ and
# sicklejaw-rig-v2/ are documented dead ends kept only for their reports.
# These assets carry a rig that PASSES tools/validate_rig_fbx.py and clips that
# pass tools/validate_anim_fbx.py -- authored, not retargeted from presets.
ART = r"C:\Claude\Projects\PrimalFauna\art\wip\sicklejaw-d-v2"
SK_FBX = os.path.join(ART, "SK_Sicklejaw.fbx")
RIG_MANIFEST = os.path.join(ART, "rig_manifest.json")

# Clips are DISCOVERED, not listed: the animation set grows packet by packet
# (P0 locomotion+attack, then turns/alert/aggressive/threaten/lunge/prowl...),
# and a hardcoded list silently drops whatever was authored last.
def discover_anim_fbxs():
    found = []
    for filename in sorted(os.listdir(ART)):
        if filename.startswith("A_Sicklejaw_") and filename.lower().endswith(".fbx"):
            found.append((os.path.splitext(filename)[0], os.path.join(ART, filename)))
    return found

ANIM_FBXS = discover_anim_fbxs()

DEST = "/PrimalFauna/Sicklejaw"
SK_NAME = "SK_Sicklejaw"
DISK_DEST = r"C:\Claude\Projects\SatisfactoryModLoader\Mods\PrimalFauna\Content\Sicklejaw"

# Designed standing height (doc 01 §1; the artist authored 1.70m).
TARGET_CM = 165.0
EXPECT_MIN_CM = 50.0
EXPECT_MAX_CM = 1000.0

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("PFA_ " + msg)


def fail(msg):
    log("ASSERT-FAIL: " + msg)
    # Persist whatever exists so the wreckage is inspectable, then abort
    # WITHOUT printing DONE (the orchestrator greps for that contract).
    try:
        eal.save_directory(DEST, only_if_is_dirty=False, recursive=True)
    except Exception:
        pass
    raise SystemExit(1)


def safe_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        log("WARN: could not set %s.%s = %r (%s)" % (obj.__class__.__name__, prop, value, e))
        return False


def make_sk_task(uniform_scale):
    o = unreal.FbxImportUI()
    o.import_mesh = True
    o.import_as_skeletal = True
    o.import_materials = True
    o.import_textures = True
    o.import_animations = False
    o.create_physics_asset = True
    safe_set(o, "mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)

    sk = o.skeletal_mesh_import_data
    safe_set(sk, "import_morph_targets", False)
    safe_set(sk, "use_t0_as_ref_pose", True)
    safe_set(sk, "import_uniform_scale", uniform_scale)
    safe_set(sk, "convert_scene", True)
    safe_set(sk, "normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)

    t = unreal.AssetImportTask()
    t.filename = SK_FBX
    t.destination_path = DEST
    t.destination_name = SK_NAME
    t.automated = True
    t.replace_existing = True
    t.save = True
    t.options = o
    return t


def make_anim_task(dest_name, fbx_path, target_skeleton, uniform_scale):
    o = unreal.FbxImportUI()
    o.import_mesh = False
    o.import_as_skeletal = True
    o.import_animations = True
    o.import_materials = False
    o.import_textures = False
    o.create_physics_asset = False
    o.skeleton = target_skeleton
    safe_set(o, "mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)

    anim = o.anim_sequence_import_data
    safe_set(anim, "import_bone_tracks", True)
    # 2026-07-24: keep the SOURCE frame rate (24fps, artist-authored timing)
    # instead of resampling to the 30fps default -- Run (1.25s @ 24fps) lands
    # on a half-frame at 30fps and the importer HARD-FAILS ("animation has to
    # be frame-border aligned"; Slash would too at 197.5 frames). Boundary
    # snap stays on as the belt for any future oddly-cut clip.
    safe_set(anim, "use_default_sample_rate", False)
    safe_set(anim, "snap_to_closest_frame_boundary", True)
    safe_set(anim, "animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    safe_set(anim, "convert_scene", True)
    # Anims must ride the SAME unit correction as the mesh or root motion /
    # bone translations land at 1/100 scale.
    safe_set(anim, "import_uniform_scale", uniform_scale)

    t = unreal.AssetImportTask()
    t.filename = fbx_path
    t.destination_path = DEST + "/Anims"
    t.destination_name = dest_name
    t.automated = True
    t.replace_existing = True
    t.save = True
    t.options = o
    return t


def measure_height_cm(sk):
    bounds = sk.get_bounds()
    ext = bounds.box_extent
    h = float(ext.z) * 2.0
    log("SK measured: half-extents cm x=%.2f y=%.2f z=%.2f -> height=%.2fcm" % (ext.x, ext.y, ext.z, h))
    return h


def disk_verify(rel_dir, asset_name):
    path = os.path.join(rel_dir, asset_name + ".uasset")
    exists = os.path.isfile(path)
    log("DISK-VERIFY %s exists=%s (%s)" % (asset_name, exists, path))
    return exists


# --- pre-flight: every source file must exist before touching the editor ---
for p in [SK_FBX] + [f for _, f in ANIM_FBXS]:
    if not os.path.isfile(p):
        fail("source FBX missing: %s" % p)

def import_sk_fresh(uniform_scale):
    """Delete-then-import so replace-in-place can never hand back stale
    bounds/render state (first run of this script: the x100 re-import over
    the existing asset measured BYTE-IDENTICAL 1.00cm bounds -- the classic
    stale-asset trap). Save + fresh-load before measuring."""
    if eal.does_directory_exist(DEST):
        eal.delete_directory(DEST)
        log("deleted %s for a fresh import" % DEST)
    tools.import_asset_tasks([make_sk_task(uniform_scale)])
    eal.save_directory(DEST, only_if_is_dirty=False, recursive=True)
    fresh = eal.load_asset(DEST + "/" + SK_NAME)
    if not fresh:
        fail("%s did not import (scale %.1f)" % (SK_NAME, uniform_scale))
    return fresh


# --- pass 1: import at 1.0, measure, then ONE measured-factor correction ---
chosen_scale = 1.0
log("importing %s at uniform scale %.1f" % (SK_NAME, chosen_scale))
sk = import_sk_fresh(chosen_scale)
height = measure_height_cm(sk)

if height < EXPECT_MIN_CM:
    # Unit shrink (Blender meter-unit FBX; laser rifle 0.53cm, placeholder
    # 1.65cm, this asset 1.00cm). Correction factor comes from the
    # MEASUREMENT, not a guessed constant.
    if height <= 0.0:
        fail("mesh measured zero height -- geometry-less import, nothing to scale")
    chosen_scale = max(2.0, min(2000.0, TARGET_CM / height))
    log("unit shrink detected (%.2fcm) -- RE-importing at measured factor %.1f" % (height, chosen_scale))
    sk = import_sk_fresh(chosen_scale)
    height = measure_height_cm(sk)

if not (EXPECT_MIN_CM <= height <= EXPECT_MAX_CM):
    fail("mesh height %.2fcm outside sane creature range [%.0f, %.0f] even after measured-factor correction (factor %.1f)" % (height, EXPECT_MIN_CM, EXPECT_MAX_CM, chosen_scale))

skeleton = sk.get_editor_property("skeleton")
if not skeleton:
    fail("%s imported with no Skeleton" % SK_NAME)

# 2026-07-25 BONE-COUNT CONTRACT: Blender's validator wrote a manifest of the
# rig it accepted; assert UE ended up with the SAME skeleton. This is what
# catches a silent rig swap or an importer that quietly drops/adds bones --
# the failure class that shipped a broken skeleton for three sessions.
if os.path.isfile(RIG_MANIFEST):
    import json
    with open(RIG_MANIFEST, "r") as handle:
        manifest = json.load(handle)
    expected_bones = manifest.get("bone_count")
    try:
        actual_bones = len(skeleton.get_editor_property("bone_tree"))
    except Exception as error:
        actual_bones = None
        log("WARN: could not read UE bone count (%s) -- contract not enforced this run" % error)
    if expected_bones and actual_bones is not None:
        if actual_bones != expected_bones:
            fail("BONE-COUNT CONTRACT BROKEN: Blender accepted %d bones, UE imported %d"
                 % (expected_bones, actual_bones))
        log("bone-count contract OK: %d bones match the Blender manifest" % actual_bones)
else:
    log("WARN: no rig_manifest.json beside the mesh -- bone-count contract NOT enforced")
log("imported SK=%s skeleton=%s height=%.1fcm scale=%.0f" % (sk.get_name(), skeleton.get_name(), height, chosen_scale))

mats = sk.get_editor_property("materials")
log("materials on SK: %d slot(s)" % (len(mats) if mats is not None else -1))
if not mats or len(mats) == 0:
    fail("SK imported with ZERO material slots -- embedded-texture import failed; wire the standalone 2048 PNGs instead")

# --- pass 2: the 5 animations against the SK's skeleton, same scale ---
for dest_name, fbx_path in ANIM_FBXS:
    log("importing %s (animation, scale %.0f)" % (dest_name, chosen_scale))
    tools.import_asset_tasks([make_anim_task(dest_name, fbx_path, skeleton, chosen_scale)])
    anim_asset = eal.load_asset(DEST + "/Anims/" + dest_name)
    if not anim_asset:
        fail("%s did not import" % dest_name)
    # 2026-07-24 lesson (w24-1 in-game): the FBX importer can put the MESH
    # into the primary package and the actual take into a suffixed sibling.
    # `skeleton` exists on BOTH USkeletalMesh and UAnimSequence, so a
    # skeleton-match check alone is FOOLED. Assert the CLASS and a nonzero
    # play length -- the two properties only a real animation satisfies.
    if not isinstance(anim_asset, unreal.AnimSequence):
        fail("%s imported as %s, NOT AnimSequence -- the FBX take landed in a sibling asset (check *_Anim_preset_* neighbors / multi-take FBX)" % (dest_name, type(anim_asset).__name__))
    play_len = anim_asset.get_play_length()
    if play_len <= 0.0:
        fail("%s is an AnimSequence with ZERO play length -- empty take" % dest_name)
    anim_skeleton = anim_asset.get_editor_property("skeleton")
    if not anim_skeleton or anim_skeleton.get_name() != skeleton.get_name():
        fail("%s skeleton mismatch (got %s, want %s)" % (dest_name, anim_skeleton.get_name() if anim_skeleton else "<none>", skeleton.get_name()))
    log("imported ANIM=%s class=AnimSequence length=%.2fs skeleton-match=True" % (dest_name, play_len))

# --- CRITICAL: persist EVERYTHING (skeleton/physics/textures/materials) ---
saved = eal.save_directory(DEST, only_if_is_dirty=False, recursive=True)
log("save_directory(%s) ok=%s" % (DEST, saved))

for asset in eal.list_assets(DEST, recursive=True):
    log("  asset: " + asset)

all_present = True
all_present &= disk_verify(DISK_DEST, SK_NAME)
all_present &= disk_verify(DISK_DEST, skeleton.get_name())
for dest_name, _ in ANIM_FBXS:
    all_present &= disk_verify(os.path.join(DISK_DEST, "Anims"), dest_name)
log("ALL-ASSETS-ON-DISK=%s" % all_present)
if not all_present:
    fail("one or more load-bearing assets missing on disk after save_directory")

log("DONE")
