r"""
import_placeholder_ue.py -- import the W1.3 placeholder creature rig (a
skeletal mesh + 2 animations) into the PrimalFauna plugin's content, so
APFASicklejaw's BeginPlay LoadObject<USkeletalMesh> (PFASicklejaw.cpp) has a
real asset to find at /PrimalFauna/Placeholder/SK_PFA_Placeholder.

NOT run by the authoring agent -- the orchestrator runs this (game closed, no
build running; see BUILD-COORDINATION.md). Mirrors the proven pattern from
[[ue-headless-python-asset-authoring]] (SatisfactoryLaserRifleMod/Scripts/ue/
import_rigged.py) and applies the CRITICAL fix from
[[sf-skeletal-mesh-import-crash]]: AssetImportTask.save=True only persists the
PRIMARY asset -- the auto-created _Skeleton (and _PhysicsAsset) stay
in-memory-only unless the whole destination directory is explicitly saved.
Skipping that step ships a skeletal mesh with a dangling skeleton reference,
which hard-crashes the game the instant the component registers:
    Assertion failed: AssetSkeletonObj  BoneContainer.cpp:165  <SK> missing skeleton

Invoke (forward slashes in -ExecutePythonScript -- UE eats backslash escapes
like the "\u" in "...\ue\..." otherwise):

    "C:\Program Files\Unreal Engine - CSS\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
      "C:\Claude\Projects\SatisfactoryModLoader\FactoryGame.uproject" ^
      -ExecutePythonScript="C:/Claude/Projects/PrimalFauna/tools/import_placeholder_ue.py" ^
      -EnablePlugins=PythonScriptPlugin -unattended -nopause -nosplash -nullrhi ^
      -stdout -FullStdOutLogOutput -NoLogTimes -utf8output

Or adapt SatisfactoryLaserRifleMod/Scripts/ue/run_ue_python.ps1 (its $Uproject
is already the shared SML project -- just point $Script at this file).

After running, grep the log for "PFA_ " and confirm:
  - PFA_ imported SK=SK_PFA_Placeholder skeleton=<not <none>>
  - both PFA_ imported ANIM= lines show a non-none skeleton match
  - PFA_ DISK-VERIFY lines all say exists=True (this is the actual proof the
    save_directory step worked -- the in-editor log lines alone are not
    enough, see the memory note above)
  - PFA_ DONE

DEST below is "/PrimalFauna/Placeholder" (the plugin's own content mount,
matching PFACreatureBase.h consumers exactly) -- note this differs from the
"/Game/PrimalFauna/Placeholder/" phrasing in the W1.3 packet brief. That
phrasing conflicts with PFASicklejaw.cpp's own
LoadObject<USkeletalMesh>(nullptr, "/PrimalFauna/Placeholder/SK_PFA_Placeholder.SK_PFA_Placeholder"),
which is the authoritative path (a CanContainContent plugin mounts its
Content/ folder as /<PluginName>/, the same way SML's own content resolves as
/SML/...). This script targets /PrimalFauna/Placeholder to match the C++ --
flagged here explicitly in case that reading is wrong.
"""
import os
import unreal

SK_FBX = r"C:\Claude\Projects\PrimalFauna\art\wip\placeholder\SK_PFA_Placeholder.fbx"
ANIM_FBXS = [
    ("A_PFA_Placeholder_WalkBob", r"C:\Claude\Projects\PrimalFauna\art\wip\placeholder\A_PFA_Placeholder_WalkBob.fbx"),
    ("A_PFA_Placeholder_Bite", r"C:\Claude\Projects\PrimalFauna\art\wip\placeholder\A_PFA_Placeholder_Bite.fbx"),
]

DEST = "/PrimalFauna/Placeholder"
SK_NAME = "SK_PFA_Placeholder"

# On-disk mirror of DEST for the post-save existence check (see the memory
# note above): CanContainContent plugins mounted by the editor save their
# /<PluginName>/... virtual paths under Mods/<Plugin>/Content/... on disk.
DISK_DEST = r"C:\Claude\Projects\SatisfactoryModLoader\Mods\PrimalFauna\Content\Placeholder"

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("PFA_ " + msg)


def safe_set(obj, prop, value):
    """set_editor_property that logs-and-continues instead of aborting the
    whole script if a property name doesn't exist on this engine build --
    only used for import-option fields this script has NOT already proven
    against a real build (unlike import_mesh/import_animations/skeleton/etc,
    which mirror import_rigged.py's already-proven usage 1:1)."""
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        log("WARN: could not set %s.%s = %r (%s)" % (obj.__class__.__name__, prop, value, e))
        return False


def make_sk_task():
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
    safe_set(sk, "import_uniform_scale", 1.0)
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


def make_anim_task(dest_name, fbx_path, target_skeleton):
    o = unreal.FbxImportUI()
    o.import_mesh = False
    o.import_as_skeletal = True
    o.import_animations = True
    o.import_materials = False
    o.import_textures = False
    o.create_physics_asset = False
    # The property that makes this an "import animation data driven by an
    # EXISTING skeleton" pass instead of creating a new mesh+skeleton.
    o.skeleton = target_skeleton
    safe_set(o, "mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)

    anim = o.anim_sequence_import_data
    safe_set(anim, "import_bone_tracks", True)
    safe_set(anim, "use_default_sample_rate", True)
    safe_set(anim, "animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    safe_set(anim, "convert_scene", True)

    t = unreal.AssetImportTask()
    t.filename = fbx_path
    t.destination_path = DEST
    t.destination_name = dest_name
    t.automated = True
    t.replace_existing = True
    t.save = True
    t.options = o
    return t


def disk_verify(asset_name):
    path = os.path.join(DISK_DEST, asset_name + ".uasset")
    exists = os.path.isfile(path)
    log("DISK-VERIFY %s exists=%s (%s)" % (asset_name, exists, path))
    return exists


# --- pass 1: skeletal mesh + its auto-created Skeleton/PhysicsAsset ---
log("importing %s (skeletal mesh)" % SK_NAME)
tools.import_asset_tasks([make_sk_task()])

sk_asset_path = DEST + "/" + SK_NAME
sk = eal.load_asset(sk_asset_path)
if not sk:
    log("FAIL: %s did not import (aborting -- animations need its skeleton)" % SK_NAME)
    log("DONE")
    raise SystemExit(1)

skeleton = sk.get_editor_property("skeleton")
log("imported SK=%s skeleton=%s" % (sk.get_name(), skeleton.get_name() if skeleton else "<none>"))

try:
    bounds = sk.get_bounds()
    ext = bounds.box_extent
    log("SK bounds (half-extents cm): x=%.1f y=%.1f z=%.1f -- sanity-check this looks like a creature, not a speck or a building" % (ext.x, ext.y, ext.z))
except Exception as e:
    log("WARN: could not read bounds (%s)" % e)

if not skeleton:
    log("FAIL: %s imported with no Skeleton -- cannot import animations against it. Fix the SK import first." % SK_NAME)
    saved = eal.save_directory(DEST, only_if_is_dirty=False, recursive=True)
    log("save_directory(%s) ok=%s" % (DEST, saved))
    log("DONE")
    raise SystemExit(1)

# --- pass 2: the 2 animations, targeting the SK's own skeleton ---
for dest_name, fbx_path in ANIM_FBXS:
    log("importing %s (animation)" % dest_name)
    tools.import_asset_tasks([make_anim_task(dest_name, fbx_path, skeleton)])

    anim_asset_path = DEST + "/" + dest_name
    anim_asset = eal.load_asset(anim_asset_path)
    if not anim_asset:
        log("FAIL: %s did not import" % dest_name)
        continue

    anim_skeleton = anim_asset.get_editor_property("skeleton")
    skeleton_matches = anim_skeleton and skeleton and anim_skeleton.get_name() == skeleton.get_name()
    log("imported ANIM=%s skeleton=%s (matches SK skeleton=%s)" %
        (anim_asset.get_name(), anim_skeleton.get_name() if anim_skeleton else "<none>", skeleton_matches))

# --- CRITICAL: save the whole directory, not just the primary assets. This
# is what actually persists the auto-created _Skeleton/_PhysicsAsset to disk
# -- t.save=True on each task above only saved the SK/anim primaries.
# See [[sf-skeletal-mesh-import-crash]]. Skipping this ships a dangling
# skeleton reference that crashes the game on the creature's first spawn. ---
saved = eal.save_directory(DEST, only_if_is_dirty=False, recursive=True)
log("save_directory(%s) ok=%s" % (DEST, saved))

try:
    for asset in eal.list_assets(DEST, recursive=False):
        log("  asset: " + asset)
except Exception as e:
    log("  list err %s" % e)

# --- on-disk proof (not just in-editor state) that the load-bearing assets
# actually hit disk before anyone tries to build/cook against them. ---
all_present = True
all_present &= disk_verify(SK_NAME)
if skeleton:
    all_present &= disk_verify(skeleton.get_name())
for dest_name, _ in ANIM_FBXS:
    all_present &= disk_verify(dest_name)

log("ALL-ASSETS-ON-DISK=%s" % all_present)
log("DONE")
