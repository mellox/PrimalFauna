"""Import the Sicklejaw-specific static remains mesh, PBR textures, and icons.

Run only through tools/ue_headless.ps1. The destination is delete-fresh so a
stale static-mesh import cannot survive a changed FBX. The script asserts the
Blender manifest's dimensions, material-slot contract, asset classes, vanilla
Hog-remains material route, conveyor instancing usage flag, and on-disk
persistence before printing PFA_REMAINS_OK.
"""
import json
import math
import os
import unreal

SRC = r"C:\Claude\Projects\PrimalFauna\art\wip\sicklejaw-remains"
FBX = os.path.join(SRC, "SM_PFA_SicklejawRemains.fbx")
MANIFEST = os.path.join(SRC, "asset_manifest.json")
ROUGHNESS_SOURCE = os.path.join(SRC, "T_PFA_SicklejawRemains_Roughness.png")
DEST = "/PrimalFauna/SicklejawRemains"
DISK_DEST = r"C:\Claude\Projects\SatisfactoryModLoader\Mods\PrimalFauna\Content\SicklejawRemains"
MESH_NAME = "SM_PFA_SicklejawRemains"
MI_HIDE_NAME = "MI_PFA_SicklejawRemains_Hide"
MI_CUT_NAME = "MI_PFA_SicklejawRemains_Cut"
VANILLA_REMAINS_PARENT_PATH = "/Game/FactoryGame/World/-Shared/Material/Asset_Master"
EXPECTED_SLOTS = ["M_PFA_SicklejawRemains_Hide", "M_PFA_SicklejawRemains_Cut"]
VANILLA_REMAINS_STATIC_SWITCHES = {
    "Emissive": False,
    "Have_ReflMap": True,
    "NightEmissive": False,
    "NormalMap is Packed": False,
    "UsePaniniProjection": False,
    "Use_DetailNormal": False,
    "bRandomEmissive": True,
    "bUseSimpleAlbedo": False,
}

TEXTURES = {
    "BaseColor": ("T_PFA_SicklejawRemains_BaseColor.png", True, unreal.TextureCompressionSettings.TC_DEFAULT),
    "Normal": ("T_PFA_SicklejawRemains_Normal.png", False, unreal.TextureCompressionSettings.TC_NORMALMAP),
    "ReflectionMap": ("T_PFA_SicklejawRemains_ReflectionMap.png", False, unreal.TextureCompressionSettings.TC_MASKS),
    "CutBaseColor": ("T_PFA_SicklejawRemains_Cut_BaseColor.png", True, unreal.TextureCompressionSettings.TC_DEFAULT),
}
ICONS = {
    "T_PFA_SicklejawRemains_Icon_Small": "T_PFA_SicklejawRemains_Icon_Small.png",
    "T_PFA_SicklejawRemains_Icon_Large": "T_PFA_SicklejawRemains_Icon_Large.png",
}
EXPECTED_ICON_SIZES = {
    "T_PFA_SicklejawRemains_Icon_Small": (256, 256),
    "T_PFA_SicklejawRemains_Icon_Large": (1024, 1024),
}

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary


def log(message):
    unreal.log("PFA_REMAINS " + str(message))


def fail(message):
    unreal.log_error("PFA_REMAINS ASSERT-FAIL " + str(message))
    raise SystemExit(1)


def require_file(path):
    if not os.path.isfile(path):
        fail("source file missing: %s" % path)


def import_texture(source, asset_name):
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = DEST
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])
    asset = EAL.load_asset("%s/%s" % (DEST, asset_name))
    if not isinstance(asset, unreal.Texture2D):
        fail("%s imported as %s, not Texture2D" % (asset_name, type(asset).__name__))
    return asset


for required in [FBX, MANIFEST, ROUGHNESS_SOURCE]:
    require_file(required)
for filename, _, _ in TEXTURES.values():
    require_file(os.path.join(SRC, filename))
for filename in ICONS.values():
    require_file(os.path.join(SRC, filename))

with open(MANIFEST, "r", encoding="utf-8") as handle:
    manifest = json.load(handle)
roughness_entries = [entry for entry in manifest.get("textures", [])
                     if entry.get("role") == "Roughness"]
if len(roughness_entries) != 1:
    fail("asset_manifest.json needs exactly one Roughness texture contract")
roughness_entry = roughness_entries[0]
if roughness_entry.get("file") != os.path.basename(ROUGHNESS_SOURCE):
    fail("Roughness texture contract file must be %s" %
         os.path.basename(ROUGHNESS_SOURCE))
try:
    hide_roughness = float(roughness_entry["scalar"])
except Exception as exc:
    fail("Roughness texture contract needs a scalar (%s)" % exc)
if not math.isfinite(hide_roughness) or not 0.0 <= hide_roughness <= 1.0:
    fail("Roughness scalar must be finite and within 0..1")
try:
    expected_m = manifest["dimensions_m"]
    if isinstance(expected_m, dict):
        expected_axes_m = [expected_m[key] for key in ("x", "y", "z")]
    elif isinstance(expected_m, list) and len(expected_m) == 3:
        expected_axes_m = expected_m
    else:
        raise ValueError("expected an x/y/z object or a three-element [x,y,z] list")
    expected_axes_m = [float(value) for value in expected_axes_m]
    if any(not math.isfinite(value) or value <= 0.0 for value in expected_axes_m):
        raise ValueError("dimensions must be finite positive metres")
    expected_cm = [value * 100.0 for value in expected_axes_m]
except Exception as exc:
    fail("asset_manifest.json needs three x/y/z dimensions (%s)" % exc)
if manifest.get("forward_axis") != "+X" or manifest.get("up_axis") != "+Z":
    fail("manifest orientation must be +X forward and +Z up")
if list(manifest.get("material_slots", [])) != EXPECTED_SLOTS:
    fail("manifest material slots do not match the fixed two-slot contract")

vanilla_remains_parent = EAL.load_asset(VANILLA_REMAINS_PARENT_PATH)
if not isinstance(vanilla_remains_parent, unreal.Material):
    fail("vanilla Hog-remains parent missing: %s" % VANILLA_REMAINS_PARENT_PATH)
if not vanilla_remains_parent.get_editor_property("used_with_instanced_static_meshes"):
    fail("vanilla Hog-remains parent lacks instanced-static-mesh usage")
parent_texture_parameters = set(str(name) for name in MEL.get_texture_parameter_names(vanilla_remains_parent))
parent_scalar_parameters = set(str(name) for name in MEL.get_scalar_parameter_names(vanilla_remains_parent))
parent_static_parameters = set(str(name) for name in MEL.get_static_switch_parameter_names(vanilla_remains_parent))
available_parent_parameters = parent_texture_parameters | parent_scalar_parameters | parent_static_parameters
for required_parameter in ("Albedo", "Normal", "ReflectionMap", "Metallic", "Roughness", "RougnessMultiply"):
    if required_parameter not in available_parent_parameters:
        fail("vanilla Hog-remains parent parameter missing: %s" % required_parameter)

if EAL.does_directory_exist(DEST):
    if not EAL.delete_directory(DEST):
        fail("could not delete stale destination %s" % DEST)
EAL.make_directory(DEST)
log("delete-fresh destination ready")

texture_assets = {}
for parameter, (filename, want_srgb, want_compression) in TEXTURES.items():
    asset_name = os.path.splitext(filename)[0]
    texture = import_texture(os.path.join(SRC, filename), asset_name)
    texture.set_editor_property("srgb", want_srgb)
    texture.set_editor_property("compression_settings", want_compression)
    texture_assets[parameter] = texture
    log("texture=%s class=Texture2D srgb=%s" % (asset_name, want_srgb))

icon_assets = {}
for asset_name, filename in ICONS.items():
    icon = import_texture(os.path.join(SRC, filename), asset_name)
    icon.set_editor_property("srgb", True)
    try:
        icon.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    except Exception as exc:
        log("icon=%s UI lod-group unavailable (%s)" % (asset_name, exc))
    icon_assets[asset_name] = icon
    actual_size = (icon.blueprint_get_size_x(), icon.blueprint_get_size_y())
    expected_size = EXPECTED_ICON_SIZES[asset_name]
    if actual_size != expected_size:
        fail("icon=%s size=%dx%d expected=%dx%d" %
             (asset_name, actual_size[0], actual_size[1], expected_size[0], expected_size[1]))
    log("icon=%s size=%dx%d expected=match" % (asset_name, actual_size[0], actual_size[1]))

def make_instance(name, albedo, normal, reflection_map, roughness):
    instance = TOOLS.create_asset(name, DEST, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    if not isinstance(instance, unreal.MaterialInstanceConstant):
        fail("could not create %s" % name)
    MEL.set_material_instance_parent(instance, vanilla_remains_parent)
    MEL.set_material_instance_texture_parameter_value(instance, "Albedo", albedo)
    if normal is not None:
        MEL.set_material_instance_texture_parameter_value(instance, "Normal", normal)
    MEL.set_material_instance_texture_parameter_value(instance, "ReflectionMap", reflection_map)
    # Exact vanilla Hog Remains scalar route. ReflectionMap is a dedicated
    # uniform-white non-sRGB mask, not the FactoryBaked power-pole default
    # which produced the user-observed mirror-black runtime result.
    MEL.set_material_instance_scalar_parameter_value(instance, "Metallic", 1.0)
    MEL.set_material_instance_scalar_parameter_value(instance, "Roughness", roughness)
    MEL.set_material_instance_scalar_parameter_value(instance, "RougnessMultiply", 1.0)
    # Mirror the actual MI_HogPart switches. Have_ReflMap is load-bearing:
    # binding the texture without enabling this compiled branch is a silent
    # no-op. The remaining values make the organic route deterministic.
    for parameter, value in VANILLA_REMAINS_STATIC_SWITCHES.items():
        if parameter not in parent_static_parameters:
            fail("vanilla Hog-remains static parameter missing: %s" % parameter)
        MEL.set_material_instance_static_switch_parameter_value(instance, parameter, value)
    MEL.update_material_instance(instance)
    return instance


mi_hide = make_instance(
    MI_HIDE_NAME, texture_assets["BaseColor"], texture_assets["Normal"],
    texture_assets["ReflectionMap"], hide_roughness)
mi_cut = make_instance(
    MI_CUT_NAME, texture_assets["CutBaseColor"], None,
    texture_assets["ReflectionMap"], hide_roughness)
log("material-route=vanilla-Hog Asset_Master instanced-static-mesh=yes hide=albedo+normal+reflection roughness=%.2f cut=tissue-albedo+reflection" %
    hide_roughness)

options = unreal.FbxImportUI()
options.import_mesh = True
options.import_as_skeletal = False
options.import_materials = False
options.import_textures = False
options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
static_data = options.static_mesh_import_data
static_data.set_editor_property("combine_meshes", True)
static_data.set_editor_property("auto_generate_collision", True)
static_data.set_editor_property("generate_lightmap_u_vs", False)
# Blender authors the source in metres, while this CSS Unreal toolchain reads
# the FBX numeric coordinates as centimetres. The first measured import was
# exactly 1/100 of the manifest, so apply the explicit unit conversion here and
# keep the post-import bounds assertion as the proof against future exporter drift.
static_data.set_editor_property("import_uniform_scale", 100.0)
static_data.set_editor_property("convert_scene", True)
static_data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)

mesh_task = unreal.AssetImportTask()
mesh_task.filename = FBX
mesh_task.destination_path = DEST
mesh_task.destination_name = MESH_NAME
mesh_task.automated = True
mesh_task.replace_existing = True
mesh_task.save = True
mesh_task.options = options
TOOLS.import_asset_tasks([mesh_task])

mesh_path = "%s/%s" % (DEST, MESH_NAME)
mesh = EAL.load_asset(mesh_path)
if not isinstance(mesh, unreal.StaticMesh):
    fail("%s imported as %s, not StaticMesh" % (MESH_NAME, type(mesh).__name__))

static_materials = list(mesh.get_editor_property("static_materials"))
slot_names = [str(entry.get_editor_property("material_slot_name")) for entry in static_materials]
if slot_names != EXPECTED_SLOTS:
    fail("static-mesh material slots %s do not match %s" % (slot_names, EXPECTED_SLOTS))
mesh.set_material(0, mi_hide)
mesh.set_material(1, mi_cut)

bounds = mesh.get_bounding_box()
extent = bounds.get_editor_property("max") - bounds.get_editor_property("min")
actual_cm = [float(extent.x), float(extent.y), float(extent.z)]
for axis, actual, expected in zip("XYZ", actual_cm, expected_cm):
    tolerance = max(1.0, expected * 0.05)
    if abs(actual - expected) > tolerance:
        fail("%s size %.2fcm differs from manifest %.2fcm (tol %.2fcm)" % (axis, actual, expected, tolerance))
log("mesh=%s size-cm=%.2f,%.2f,%.2f slots=%s" %
    (mesh_path, actual_cm[0], actual_cm[1], actual_cm[2], "+".join(slot_names)))

saved = EAL.save_directory(DEST, only_if_is_dirty=False, recursive=True)
if not saved:
    fail("save_directory returned false for %s" % DEST)

required_assets = [MESH_NAME, MI_HIDE_NAME, MI_CUT_NAME]
required_assets += [os.path.splitext(filename)[0] for filename, _, _ in TEXTURES.values()]
required_assets += list(ICONS.keys())
missing_disk = [name for name in required_assets
                if not os.path.isfile(os.path.join(DISK_DEST, name + ".uasset"))]
if missing_disk:
    fail("assets missing from disk after save: %s" % missing_disk)

for parameter, (filename, expected_srgb, expected_compression) in TEXTURES.items():
    asset_name = os.path.splitext(filename)[0]
    persisted_texture = EAL.load_asset("%s/%s" % (DEST, asset_name))
    if not isinstance(persisted_texture, unreal.Texture2D):
        fail("%s did not reload as Texture2D" % asset_name)
    actual_srgb = bool(persisted_texture.get_editor_property("srgb"))
    actual_compression = persisted_texture.get_editor_property("compression_settings")
    if actual_srgb != expected_srgb or actual_compression != expected_compression:
        fail("%s texture settings persisted srgb=%s compression=%s expected srgb=%s compression=%s" %
             (asset_name, actual_srgb, actual_compression,
              expected_srgb, expected_compression))
    log("persisted-texture role=%s name=%s srgb=%s compression=%s outcome=match" %
        (parameter, asset_name, actual_srgb, actual_compression))

reloaded_hide = EAL.load_asset("%s/%s" % (DEST, MI_HIDE_NAME))
reloaded_cut = EAL.load_asset("%s/%s" % (DEST, MI_CUT_NAME))
material_contracts = (
    (MI_HIDE_NAME, reloaded_hide, texture_assets["BaseColor"], texture_assets["Normal"]),
    (MI_CUT_NAME, reloaded_cut, texture_assets["CutBaseColor"], None),
)
for name, instance, expected_albedo, expected_normal in material_contracts:
    parent = instance.get_editor_property("parent") if instance else None
    if not parent or parent.get_path_name() != vanilla_remains_parent.get_path_name():
        fail("%s did not persist its vanilla Hog-remains parent" % name)
    persisted_albedo = MEL.get_material_instance_texture_parameter_value(instance, "Albedo")
    persisted_reflection = MEL.get_material_instance_texture_parameter_value(instance, "ReflectionMap")
    if persisted_albedo != expected_albedo or persisted_reflection != texture_assets["ReflectionMap"]:
        fail("%s did not persist its exact Albedo and ReflectionMap" % name)
    if expected_normal is not None:
        persisted_normal = MEL.get_material_instance_texture_parameter_value(instance, "Normal")
        if persisted_normal != expected_normal:
            fail("%s did not persist its exact Normal" % name)
    for parameter, expected_value in (
        ("Metallic", 1.0),
        ("Roughness", hide_roughness),
        ("RougnessMultiply", 1.0),
    ):
        actual_value = MEL.get_material_instance_scalar_parameter_value(instance, parameter)
        if not math.isclose(actual_value, expected_value, rel_tol=0.0, abs_tol=1e-4):
            fail("%s scalar %s %.4f did not persist expected %.4f" %
                 (name, parameter, actual_value, expected_value))
    for parameter, expected_value in VANILLA_REMAINS_STATIC_SWITCHES.items():
        actual_value = MEL.get_material_instance_static_switch_parameter_value(instance, parameter)
        if actual_value != expected_value:
            fail("%s static switch %s=%s did not persist expected %s" %
                 (name, parameter, actual_value, expected_value))
    log("persisted-material name=%s albedo=%s reflection=%s roughness=%.2f switches=%d outcome=match" %
        (name, persisted_albedo.get_path_name(), persisted_reflection.get_path_name(),
         hide_roughness, len(VANILLA_REMAINS_STATIC_SWITCHES)))
log("all-assets-on-disk=%d outcome=ready" % len(required_assets))
print("PFA_REMAINS_OK")
