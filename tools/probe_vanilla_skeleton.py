r"""
probe_vanilla_skeleton.py -- settles UNKNOWN U1 of
_team/primalfauna-animation-path.md: is a vanilla creature SKELETON actually
BINDABLE in our local editor project (does it carry a real bone hierarchy +
reference pose), or is it an empty SML placeholder that we could reference at
runtime but could NOT rig against at author time?

That single answer decides whether path A (skin our creatures to a vanilla
skeleton and inherit Coffee Stain's own animations by reference) is walkable
locally, or whether it first needs an expensive AssetGenerator run.

READ-ONLY. Loads assets and logs; writes nothing, imports nothing.

Run (game closed, no build running -- headless UnrealEditor-Cmd holds ~28GB):
    "C:\Program Files\Unreal Engine - CSS\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
      "C:\Claude\Projects\SatisfactoryModLoader\FactoryGame.uproject" ^
      -ExecutePythonScript="C:/Claude/Projects/PrimalFauna/tools/probe_vanilla_skeleton.py" ^
      -EnablePlugins=PythonScriptPlugin -unattended -nopause -nosplash -nullrhi ^
      -stdout -FullStdOutLogOutput -NoLogTimes -utf8output

Grep the output for "PFAPROBE".
"""
import unreal

# Creature content roots worth inspecting. NonFlyingBird is the primary
# candidate donor (theropod body plan: three-toed feet, beak/jaw, 4-bone
# tail, vestigial arms); the others are the fallback donors for future
# species (quadruped predator / arachnid / grazer / critter).
FOLDERS = [
    "/Game/FactoryGame/Character/Creature/Wildlife/NonFlyingBird",
    "/Game/FactoryGame/Character/Creature/Wildlife/SpaceRabbit",
    "/Game/FactoryGame/Character/Creature/Enemy/Hog",
    "/Game/FactoryGame/Character/Creature/Enemy/Stinger",
    "/Game/FactoryGame/Character/Creature",
]


def log(msg):
    unreal.log("PFAPROBE " + msg)


registry = unreal.AssetRegistryHelpers.get_asset_registry()


def describe_skeleton(asset_path):
    """Try several APIs to read the bone hierarchy -- the point is to learn
    whether ANY real bone data exists locally, so every probe is defensive
    and reports its own failure rather than aborting the script."""
    skeleton = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not skeleton:
        log("  SKELETON %s -> FAILED TO LOAD" % asset_path)
        return
    log("  SKELETON %s class=%s" % (asset_path, type(skeleton).__name__))

    # Route 1: the modern SkeletonModifier (UE5.3+).
    try:
        modifier = unreal.SkeletonModifier()
        modifier.set_skeleton(skeleton)
        bone_names = modifier.get_all_bone_names()
        log("    route=SkeletonModifier boneCount=%d" % len(bone_names))
        if bone_names:
            preview = [str(b) for b in bone_names[:40]]
            log("    bones[0:40]=%s" % ", ".join(preview))
            # Reference-pose transforms are what actually make it bindable.
            try:
                root_xf = modifier.get_bone_transform(bone_names[0], True)
                log("    refPose(root)=%s" % str(root_xf))
            except Exception as e:
                log("    refPose read FAILED (%s)" % e)
        return
    except Exception as e:
        log("    route=SkeletonModifier FAILED (%s)" % e)

    # Route 2: reflection on the bone tree UPROPERTY.
    try:
        bone_tree = skeleton.get_editor_property("bone_tree")
        log("    route=bone_tree boneCount=%d" % len(bone_tree))
        return
    except Exception as e:
        log("    route=bone_tree FAILED (%s)" % e)

    log("    NO ROUTE READ BONES -- treat as placeholder/empty until proven otherwise")


for folder in FOLDERS:
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        log("FOLDER %s -> DOES NOT EXIST locally" % folder)
        continue

    assets = registry.get_assets_by_path(folder, recursive=True)
    log("FOLDER %s -> %d asset(s)" % (folder, len(assets)))

    skeletons = []
    anims = 0
    meshes = 0
    for asset_data in assets:
        class_name = str(asset_data.asset_class_path.asset_name) if hasattr(asset_data, "asset_class_path") else str(asset_data.asset_class)
        object_path = str(asset_data.package_name) + "." + str(asset_data.asset_name)
        if class_name == "Skeleton":
            skeletons.append(str(asset_data.package_name))
        elif class_name == "AnimSequence":
            anims += 1
            if anims <= 25:
                log("  ANIM %s" % object_path)
        elif class_name in ("SkeletalMesh", "BlendSpace", "BlendSpace1D", "AnimBlueprint", "AnimMontage"):
            meshes += 1
            log("  %s %s" % (class_name.upper(), object_path))

    log("FOLDER %s summary: skeletons=%d animSequences=%d otherAnimAssets=%d"
        % (folder, len(skeletons), anims, meshes))

    for skeleton_package in skeletons:
        describe_skeleton(skeleton_package)

log("DONE")
