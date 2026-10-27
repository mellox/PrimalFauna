r"""
parity_fbx_import_ue.py -- import N skeletal FBX files into a SCRATCH content folder,
measure each the same way, print one PFA_PARITY line per measurement, then DELETE the
scratch folder and verify nothing is left on disk. Nothing under /PrimalFauna/Sicklejaw
is touched. Nothing is saved.

Purpose (2026-10-14): measure whether an FBX exported by Blender 5.3 beta imports into UE
identically to one exported by Blender 5.2 LTS (bounds, bone count/names, ref-pose bone
positions = yaw, material slots, textures). Pass the files via PFA_PARITY_FBXS
(';'-separated) -- the editor swallows script argv.

Run via tools/ue_headless.ps1 (build lock, game-closed gate):
    $env:PFA_PARITY_FBXS = 'C:/scratch/b52_spec.fbx;C:/scratch/b53_spec.fbx'
    powershell -File tools\ue_headless.ps1 -Script tools\parity_fbx_import_ue.py -Grep 'PFA_PARITY'
"""
import os
import unreal

FBXS = [p for p in os.environ.get("PFA_PARITY_FBXS", "").split(";") if p.strip()]
SCRATCH = "/PrimalFauna/_ParityScratch"
DISK_SCRATCH = r"C:\Claude\Projects\SatisfactoryModLoader\Mods\PrimalFauna\Content\_ParityScratch"

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("PFA_PARITY " + msg)


def safe_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
    except Exception as e:
        log("WARN could not set %s.%s=%r (%s)" % (obj.__class__.__name__, prop, value, e))


def make_task(fbx, dest, name):
    # Same options as import_sicklejaw_ue.py pass 1 (uniform scale 1.0, convert_scene).
    o = unreal.FbxImportUI()
    o.import_mesh = True
    o.import_as_skeletal = True
    o.import_materials = True
    o.import_textures = True
    o.import_animations = False
    o.create_physics_asset = False
    safe_set(o, "mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    sk = o.skeletal_mesh_import_data
    safe_set(sk, "import_morph_targets", False)
    safe_set(sk, "use_t0_as_ref_pose", True)
    safe_set(sk, "import_uniform_scale", 1.0)
    safe_set(sk, "convert_scene", True)
    safe_set(sk, "normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    t = unreal.AssetImportTask()
    t.filename = fbx
    t.destination_path = dest
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = False
    t.options = o
    return t


def measure(tag, sk, dest):
    b = sk.get_bounds()
    log("%s bounds origin=(%.3f,%.3f,%.3f) extent=(%.3f,%.3f,%.3f) height_cm=%.3f" % (
        tag, b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z, b.box_extent.z * 2))
    try:
        ib = sk.get_editor_property("imported_bounds")
        log("%s imported_bounds origin=(%.3f,%.3f,%.3f) extent=(%.3f,%.3f,%.3f)" % (
            tag, ib.origin.x, ib.origin.y, ib.origin.z, ib.box_extent.x, ib.box_extent.y, ib.box_extent.z))
    except Exception as e:
        log("%s imported_bounds unavailable (%s)" % (tag, e))

    skel = sk.get_editor_property("skeleton")
    log("%s skeleton=%s" % (tag, skel.get_name() if skel else "<none>"))
    try:
        log("%s bone_tree_count=%d" % (tag, len(skel.get_editor_property("bone_tree"))))
    except Exception as e:
        log("%s bone_tree_count unavailable (%s)" % (tag, e))

    # Bone names + component-space ref-pose positions via a transient component.
    try:
        comp = unreal.new_object(unreal.SkeletalMeshComponent)
        try:
            comp.set_skeletal_mesh_asset(sk)
        except Exception:
            comp.set_skeletal_mesh(sk)
        n = comp.get_num_bones()
        names = [str(comp.get_bone_name(i)) for i in range(n)]
        log("%s num_bones=%d names=%s" % (tag, n, ",".join(names)))
        parents = ["%s>%s" % (str(comp.get_parent_bone(unreal.Name(nm))), nm) for nm in names]
        log("%s parents=%s" % (tag, ",".join(parents)))
        for nm in ("Root", "Hip", "Head", "Jaw", "Tail05", "L_Hand", "R_Hand", "L_Foot", "R_Foot"):
            if nm in names:
                try:
                    p = comp.get_ref_pose_position(names.index(nm))
                    log("%s refpose %-8s x=%+9.3f y=%+9.3f z=%+9.3f" % (tag, nm, p.x, p.y, p.z))
                except Exception as e:
                    log("%s refpose %s unavailable (%s)" % (tag, nm, e))
    except Exception as e:
        log("%s bone probe failed (%s)" % (tag, e))

    mats = sk.get_editor_property("materials") or []
    log("%s material_slots=%d" % (tag, len(mats)))
    for i, m in enumerate(mats):
        mi = m.material_interface
        texs = []
        try:
            if mi:
                texs = [t.get_name() for t in unreal.MaterialEditingLibrary.get_used_textures(mi)]
        except Exception as e:
            texs = ["<get_used_textures failed: %s>" % e]
        log("%s slot[%d] name=%s material=%s textures=%s" % (
            tag, i, str(m.material_slot_name), mi.get_name() if mi else "<none>", ",".join(sorted(texs))))

    # Everything the import created under this file's dest, by class.
    for a in sorted(eal.list_assets(dest, recursive=True)):
        ad = eal.find_asset_data(a)
        cls = str(ad.asset_class_path.asset_name) if hasattr(ad, "asset_class_path") else str(ad.asset_class)
        extra = ""
        if cls == "Texture2D":
            try:
                t = eal.load_asset(a)
                extra = " size=%dx%d srgb=%s" % (t.blueprint_get_size_x(), t.blueprint_get_size_y(), t.get_editor_property("srgb"))
            except Exception as e:
                extra = " <tex probe failed: %s>" % e
        log("%s asset class=%s path=%s%s" % (tag, cls, a.replace(dest, ""), extra))

    # LOD0 geometry counts
    try:
        log("%s lod0 verts=%d tris=%d" % (tag, sk.get_num_verts(0), sk.get_num_triangles(0)) if hasattr(sk, "get_num_verts") else "%s lod0 counts unavailable" % tag)
    except Exception as e:
        log("%s lod0 counts failed (%s)" % (tag, e))
    try:
        srm = sk.get_editor_property("asset_import_data")
        log("%s import_data=%s" % (tag, srm.get_class().get_name() if srm else "<none>"))
        for prop in ("import_uniform_scale", "import_rotation", "import_translation", "convert_scene", "force_front_x_axis", "convert_scene_unit"):
            try:
                log("%s import_data.%s=%s" % (tag, prop, srm.get_editor_property(prop)))
            except Exception:
                pass
    except Exception as e:
        log("%s import_data failed (%s)" % (tag, e))


if not FBXS:
    log("ASSERT-FAIL no FBX list in PFA_PARITY_FBXS")
    raise SystemExit(1)
for p in FBXS:
    if not os.path.isfile(p):
        log("ASSERT-FAIL missing %s" % p)
        raise SystemExit(1)

if eal.does_directory_exist(SCRATCH):
    eal.delete_directory(SCRATCH)
    log("pre-clean: deleted stale %s" % SCRATCH)

for fbx in FBXS:
    tag = os.path.splitext(os.path.basename(fbx))[0]
    dest = SCRATCH + "/" + tag
    log("---- %s <- %s (%d bytes)" % (tag, fbx, os.path.getsize(fbx)))
    tools.import_asset_tasks([make_task(fbx, dest, "SK_" + tag)])
    sk = eal.load_asset(dest + "/SK_" + tag)
    if not sk:
        log("%s ASSERT-FAIL did not import" % tag)
        continue
    measure(tag, sk, dest)

# Clean up: nothing from this probe may remain in the plugin content.
deleted = eal.delete_directory(SCRATCH) if eal.does_directory_exist(SCRATCH) else True
log("cleanup delete_directory(%s)=%s" % (SCRATCH, deleted))
left = os.path.isdir(DISK_SCRATCH)
if left:
    try:
        import shutil
        shutil.rmtree(DISK_SCRATCH)
        left = os.path.isdir(DISK_SCRATCH)
        log("cleanup removed leftover disk dir, still_present=%s" % left)
    except Exception as e:
        log("cleanup WARN could not remove %s (%s)" % (DISK_SCRATCH, e))
log("SCRATCH-ON-DISK=%s" % left)
log("DONE")
