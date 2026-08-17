"""Read-only inspection of the currently imported Sicklejaw Remains assets."""

import unreal


EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
DEST = "/PrimalFauna/SicklejawRemains"
FAILURES = []

TEXTURE_SETTINGS = {
    "T_PFA_SicklejawRemains_BaseColor": (True, unreal.TextureCompressionSettings.TC_DEFAULT),
    "T_PFA_SicklejawRemains_Cut_BaseColor": (True, unreal.TextureCompressionSettings.TC_DEFAULT),
    "T_PFA_SicklejawRemains_Normal": (False, unreal.TextureCompressionSettings.TC_NORMALMAP),
    "T_PFA_SicklejawRemains_ReflectionMap": (False, unreal.TextureCompressionSettings.TC_MASKS),
}
PARENT_PATH = "/Game/FactoryGame/World/-Shared/Material/Asset_Master.Asset_Master"
MATERIAL_SCALARS = {"Metallic": 0.0, "Roughness": 0.78, "RougnessMultiply": 1.0}
MATERIAL_SWITCHES = {
    "Have_ReflMap": True,
    "Emissive": False,
    "NightEmissive": False,
    "NormalMap is Packed": False,
    "UsePaniniProjection": False,
    "Use_DetailNormal": False,
    "bRandomEmissive": True,
    "bUseSimpleAlbedo": False,
}
MATERIAL_TEXTURES = {
    "MI_PFA_SicklejawRemains_Hide": {
        "Albedo": "T_PFA_SicklejawRemains_BaseColor",
        "Normal": "T_PFA_SicklejawRemains_Normal",
        "ReflectionMap": "T_PFA_SicklejawRemains_ReflectionMap",
    },
    "MI_PFA_SicklejawRemains_Cut": {
        "Albedo": "T_PFA_SicklejawRemains_Cut_BaseColor",
        # No Cut override is authored. MaterialEditingLibrary returns the
        # parent-resolved neutral normal, so assert that exact harmless value
        # instead of pretending the resolved parameter is null.
        "Normal": "/Game/FactoryGame/MasterMaterials/Resources/TX_DefaultNormal_01.TX_DefaultNormal_01",
        "ReflectionMap": "T_PFA_SicklejawRemains_ReflectionMap",
    },
}


def emit(message):
    unreal.log("PFA_REMAINS_INSPECT " + str(message))


def fail(message):
    FAILURES.append(str(message))
    unreal.log_error("PFA_REMAINS_INSPECT_FAIL " + str(message))


mesh = EAL.load_asset(DEST + "/SM_PFA_SicklejawRemains")
if not isinstance(mesh, unreal.StaticMesh):
    raise RuntimeError("Sicklejaw remains static mesh is missing")

box = mesh.get_bounding_box()
emit("mesh=%s bounds_min=%s bounds_max=%s materials=%d" % (
    mesh.get_path_name(), box.min, box.max,
    len(mesh.get_editor_property("static_materials"))))

body_setup = mesh.get_editor_property("body_setup")
if body_setup is None:
    emit("collision body_setup=none")
else:
    agg = body_setup.get_editor_property("agg_geom")
    counts = {}
    for name in ("box_elems", "sphere_elems", "sphyl_elems", "convex_elems", "tapered_capsule_elems"):
        try:
            counts[name] = len(agg.get_editor_property(name))
        except Exception as exc:
            counts[name] = "unavailable:%s" % exc
    emit("collision trace=%s primitives=%s" % (
        body_setup.get_editor_property("collision_trace_flag"), counts))

for name in ("MI_PFA_SicklejawRemains_Hide", "MI_PFA_SicklejawRemains_Cut"):
    mi = EAL.load_asset(DEST + "/" + name)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        fail("material name=%s missing" % name)
        continue
    parent = mi.get_editor_property("parent")
    emit("material name=%s parent=%s" % (
        name, parent.get_path_name() if parent else "none"))
    if not parent or parent.get_path_name() != PARENT_PATH:
        fail("material name=%s parent=%s expected=%s" %
             (name, parent.get_path_name() if parent else "none", PARENT_PATH))
    for param, expected in MATERIAL_SCALARS.items():
        actual = MEL.get_material_instance_scalar_parameter_value(mi, param)
        emit("material name=%s scalar %s=%s" % (name, param, actual))
        if abs(float(actual) - expected) > 0.0001:
            fail("material name=%s scalar %s=%s expected=%s" %
                 (name, param, actual, expected))
    for param, expected_name in MATERIAL_TEXTURES[name].items():
        value = MEL.get_material_instance_texture_parameter_value(mi, param)
        emit("material name=%s texture %s=%s" % (
            name, param, value.get_path_name() if value else "none"))
        expected_path = (expected_name if expected_name.startswith("/") else
                         "%s/%s.%s" % (DEST, expected_name, expected_name))
        actual_path = value.get_path_name() if value else None
        if actual_path != expected_path:
            fail("material name=%s texture %s=%s expected=%s" %
                 (name, param, actual_path or "none", expected_path or "none"))
    for param, expected in MATERIAL_SWITCHES.items():
        actual = MEL.get_material_instance_static_switch_parameter_value(mi, param)
        emit("material name=%s switch %s=%s" % (name, param, actual))
        if actual != expected:
            fail("material name=%s switch %s=%s expected=%s" %
                 (name, param, actual, expected))

for name, (expected_srgb, expected_compression) in TEXTURE_SETTINGS.items():
    texture = EAL.load_asset(DEST + "/" + name)
    if not isinstance(texture, unreal.Texture2D):
        fail("texture name=%s missing-or-wrong-class" % name)
        continue
    actual_srgb = bool(texture.get_editor_property("srgb"))
    actual_compression = texture.get_editor_property("compression_settings")
    emit("texture name=%s srgb=%s compression=%s" %
         (name, actual_srgb, actual_compression))
    if actual_srgb != expected_srgb or actual_compression != expected_compression:
        fail("texture name=%s srgb=%s compression=%s expected-srgb=%s expected-compression=%s" %
             (name, actual_srgb, actual_compression,
              expected_srgb, expected_compression))

if FAILURES:
    raise RuntimeError("Sicklejaw remains inspection failed: %s" % "; ".join(FAILURES))

print("PFA_REMAINS_INSPECT_OK failures=0")
