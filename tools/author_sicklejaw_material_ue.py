"""Author M_Sicklejaw (+ MI_Sicklejaw) headlessly, with the runtime aggro-glow parameters.

WHY A REAL MATERIAL AT ALL
The shipped surface came in as `tripo_mat_7174a7d7`, the material UE auto-generates from
the FBX's embedded textures.  An auto-generated material has no named parameters, so
nothing in it can be driven from C++.  The aggro glow needs exactly that, so the creature
gets a proper master material with a Material Instance per species.

THE PARAMETER CONTRACT (this is what C++ talks to)
  scalar  AggroGlow       0..1   <- THE ONLY ONE THE GAME SETS AT RUNTIME
  scalar  GlowIntensity   0..40  design knob, default 14
  vector  GlowColor              calm tint    (linear)
  vector  GlowColorHot           committed tint (linear); the material lerps by AggroGlow,
                                 so C++ gets the colour shift for free from one scalar
  texture BaseColor / Roughness / Normal / Emissive

THE EMISSIVE MAP'S CHANNELS
  R = core marks   -- head, neck and shoulder; lit even when calm
  G = flare marks  -- ribcage, haunch, spine vent, tail; recruited as arousal rises
  B = along-body position, 0 at the snout and 1 at the tail tip.  The material lights the
      flare marks in order of B, so the pattern IGNITES FROM THE HEAD AND RUNS BACKWARDS.
      That is what makes alert distinguishable from calm in daylight, where a
      brightness-only ramp is invisible.

RUN
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<this file>"
Sequence it like a build (headless UE holds ~28 GB).  Idempotent: deletes and recreates
both assets, then asserts every parameter is present before saving.
"""
import unreal

PKG = '/PrimalFauna/Sicklejaw'          # matches tools/import_sicklejaw_ue.py DEST
TEXDIR = PKG + '/Textures'
SRC_TEX = 'C:/Claude/Projects/PrimalFauna/art/wip/sicklejaw-d-v2/textures'
MAT_PATH = PKG + '/M_Sicklejaw'
MI_PATH = PKG + '/MI_Sicklejaw'

# linear RGB -- matches matlib.GLOW_COLOR / GLOW_COLOR_HOT in the art pipeline
GLOW_COLOR = (0.0137, 0.5209, 0.6229, 1.0)
GLOW_COLOR_HOT = (0.3250, 1.0000, 0.7414, 1.0)
GLOW_INTENSITY = 14.0
CORE_FLOOR = 0.18
BOOST_MIN = 0.20
RECRUIT_SOFT = 0.06
RECRUIT_SPAN = 1.10

TEXTURES = {                       # parameter name -> texture asset name
    'BaseColor': 'T_Sicklejaw_BaseColor',
    'Roughness': 'T_Sicklejaw_Roughness',
    'Normal': 'T_Sicklejaw_Normal',
    'Emissive': 'T_Sicklejaw_Emissive',
}

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log('[PFA-MAT] ' + msg)


def fail(msg):
    unreal.log_error('[PFA-MAT] FAIL: ' + msg)
    raise SystemExit(1)


# ------------------------------------------------------------------ textures
# Import the four PNGs if they are not in the project yet.  These live on the EXISTING
# UVs, so the skeletal mesh does NOT need re-importing and no clip needs re-validating.
import os

_tasks = []
for pname, aname in TEXTURES.items():
    if EAL.does_asset_exist('%s/%s' % (TEXDIR, aname)):
        continue
    src = os.path.join(SRC_TEX, aname + '.png')
    if not os.path.isfile(src):
        fail('source texture not on disk: %s' % src)
    t = unreal.AssetImportTask()
    t.filename = src
    t.destination_path = TEXDIR
    t.destination_name = aname
    t.automated = True
    t.replace_existing = True
    t.save = True
    _tasks.append(t)
if _tasks:
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(_tasks)
    log('imported %d texture(s)' % len(_tasks))

tex = {}
for pname, aname in TEXTURES.items():
    path = '%s/%s' % (TEXDIR, aname)
    if not EAL.does_asset_exist(path):
        fail('texture %s still missing after import' % path)
    t = EAL.load_asset(path)
    if t is None or t.blueprint_get_size_x() != 2048:
        fail('%s did not import as a 2048 texture' % path)
    tex[pname] = t
    log('texture OK %s (%dx%d)' % (path, t.blueprint_get_size_x(), t.blueprint_get_size_y()))

# sRGB / compression sanity.  Getting this wrong is silent and looks like "the maps are
# just wrong": a linear map read as sRGB is washed out, an sRGB map read as linear is dark.
EXPECTED = {
    'BaseColor': (True, unreal.TextureCompressionSettings.TC_DEFAULT),
    'Roughness': (False, unreal.TextureCompressionSettings.TC_MASKS),
    'Normal': (False, unreal.TextureCompressionSettings.TC_NORMALMAP),
    'Emissive': (False, unreal.TextureCompressionSettings.TC_MASKS),
}
for pname, (want_srgb, want_comp) in EXPECTED.items():
    t = tex[pname]
    if bool(t.get_editor_property('srgb')) != want_srgb:
        t.set_editor_property('srgb', want_srgb)
        log('corrected sRGB on %s -> %s' % (pname, want_srgb))
    if t.get_editor_property('compression_settings') != want_comp:
        t.set_editor_property('compression_settings', want_comp)
        log('corrected compression on %s' % pname)

# ------------------------------------------------------------------ master material
for p in (MI_PATH, MAT_PATH):
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p)
        log('deleted stale %s' % p)

atools = unreal.AssetToolsHelpers.get_asset_tools()
mat = atools.create_asset('M_Sicklejaw', PKG, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    fail('could not create M_Sicklejaw')


def expr(cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def link(a, out, b, inp):
    if not MEL.connect_material_expressions(a, out, b, inp):
        fail('could not connect %s.%s -> %s.%s' % (a, out, b, inp))


def prop(a, out, p):
    if not MEL.connect_material_property(a, out, p):
        fail('could not connect %s.%s to material property' % (a, out))


def tex_param(name, x, y, sampler=None):
    n = expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    n.set_editor_property('parameter_name', name)
    n.set_editor_property('texture', tex[name])
    n.set_editor_property('group', 'Textures')
    if sampler is not None:
        n.set_editor_property('sampler_type', sampler)
    return n


def scalar(name, val, x, y, group='Glow'):
    n = expr(unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property('parameter_name', name)
    n.set_editor_property('default_value', val)
    n.set_editor_property('group', group)
    return n


def vector(name, val, x, y, group='Glow'):
    n = expr(unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property('parameter_name', name)
    n.set_editor_property('default_value', unreal.LinearColor(*val))
    n.set_editor_property('group', group)
    return n


def mul(x, y, ca=None, cb=None):
    n = expr(unreal.MaterialExpressionMultiply, x, y)
    if ca is not None:
        n.set_editor_property('const_a', ca)
    if cb is not None:
        n.set_editor_property('const_b', cb)
    return n


def add(x, y, ca=None, cb=None):
    n = expr(unreal.MaterialExpressionAdd, x, y)
    if ca is not None:
        n.set_editor_property('const_a', ca)
    if cb is not None:
        n.set_editor_property('const_b', cb)
    return n


def sub(x, y, ca=None, cb=None):
    n = expr(unreal.MaterialExpressionSubtract, x, y)
    if ca is not None:
        n.set_editor_property('const_a', ca)
    if cb is not None:
        n.set_editor_property('const_b', cb)
    return n


# --- the plain PBR half -------------------------------------------------------
t_base = tex_param('BaseColor', -1500, -400)
t_rough = tex_param('Roughness', -1500, -100)
t_norm = tex_param('Normal', -1500, 200, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
t_emis = tex_param('Emissive', -1500, 520)

prop(t_base, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
prop(t_rough, 'R', unreal.MaterialProperty.MP_ROUGHNESS)
prop(t_norm, 'RGB', unreal.MaterialProperty.MP_NORMAL)

# Metallic is a CONSTANT 0, not a texture.  The generator's metallic map was 98% zero
# with 0.09% of texels spiking to 0.84 -- pure noise that showed as dark pinholes.
c_metal = expr(unreal.MaterialExpressionConstant, -700, 340)
c_metal.set_editor_property('r', 0.0)
prop(c_metal, '', unreal.MaterialProperty.MP_METALLIC)

# --- the glow half ------------------------------------------------------------
p_aggro = scalar('AggroGlow', 0.0, -1500, 820)
p_inten = scalar('GlowIntensity', GLOW_INTENSITY, -1500, 940)
p_cold = vector('GlowColor', GLOW_COLOR, -1500, 1060)
p_hot = vector('GlowColorHot', GLOW_COLOR_HOT, -1500, 1240)

# recruit = smoothstep(B - soft, B + soft, AggroGlow * span)
n_span = mul(-1150, 800, cb=RECRUIT_SPAN)
link(p_aggro, '', n_span, 'A')
n_lo = sub(-1150, 560, cb=RECRUIT_SOFT)
link(t_emis, 'B', n_lo, 'A')
n_hi = add(-1150, 660, cb=RECRUIT_SOFT)
link(t_emis, 'B', n_hi, 'A')
n_rec = expr(unreal.MaterialExpressionSmoothStep, -900, 620)
link(n_lo, '', n_rec, 'Min')
link(n_hi, '', n_rec, 'Max')
link(n_span, '', n_rec, 'Value')

# level = saturate(R*(CORE_FLOOR + (1-CORE_FLOOR)*A) + G*recruit)
n_cf = mul(-1150, 1000, cb=1.0 - CORE_FLOOR)
link(p_aggro, '', n_cf, 'A')
n_core_t = add(-950, 1000, cb=CORE_FLOOR)
link(n_cf, '', n_core_t, 'A')
n_core = mul(-700, 900)
link(t_emis, 'R', n_core, 'A')
link(n_core_t, '', n_core, 'B')
n_flare = mul(-700, 640)
link(t_emis, 'G', n_flare, 'A')
link(n_rec, '', n_flare, 'B')
n_sum = add(-500, 760)
link(n_core, '', n_sum, 'A')
link(n_flare, '', n_sum, 'B')
n_level = expr(unreal.MaterialExpressionClamp, -320, 760)
n_level.set_editor_property('min_default', 0.0)
n_level.set_editor_property('max_default', 1.0)
link(n_sum, '', n_level, 'Input')

# boost = BOOST_MIN + (1-BOOST_MIN) * A*A
n_a2 = mul(-1150, 1160)
link(p_aggro, '', n_a2, 'A')
link(p_aggro, '', n_a2, 'B')
n_bs = mul(-950, 1160, cb=1.0 - BOOST_MIN)
link(n_a2, '', n_bs, 'A')
n_boost = add(-760, 1160, cb=BOOST_MIN)
link(n_bs, '', n_boost, 'A')

# tint = lerp(GlowColor, GlowColorHot, A)
n_tint = expr(unreal.MaterialExpressionLinearInterpolate, -900, 1320)
link(p_cold, '', n_tint, 'A')
link(p_hot, '', n_tint, 'B')
link(p_aggro, '', n_tint, 'Alpha')

# emissive = tint * level * boost * GlowIntensity
n_bi = mul(-560, 1120)
link(n_boost, '', n_bi, 'A')
link(p_inten, '', n_bi, 'B')
n_lb = mul(-360, 1000)
link(n_level, '', n_lb, 'A')
link(n_bi, '', n_lb, 'B')
n_emis = mul(-160, 1150)
link(n_tint, '', n_emis, 'A')
link(n_lb, '', n_emis, 'B')
prop(n_emis, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

MEL.layout_material_expressions(mat)
MEL.recompile_material(mat)

# ------------------------------------------------------------------ verify, then instance
params = {}
for getter, kind in ((MEL.get_scalar_parameter_names, 'scalar'),
                     (MEL.get_vector_parameter_names, 'vector'),
                     (MEL.get_texture_parameter_names, 'texture')):
    try:
        for n in getter(mat):
            params[str(n)] = kind
    except Exception as e:                                   # older API surface
        log('note: could not enumerate %s parameters (%s)' % (kind, e))

REQUIRED = ['AggroGlow', 'GlowIntensity', 'GlowColor', 'GlowColorHot',
            'BaseColor', 'Roughness', 'Normal', 'Emissive']
if params:
    missing = [p for p in REQUIRED if p not in params]
    if missing:
        fail('parameters missing from the compiled material: %s' % missing)
    log('parameters OK: %s' % sorted(params.items()))
else:
    log('WARNING: parameter enumeration unavailable -- open M_Sicklejaw and eyeball the '
        'parameter list before wiring C++')

mi = atools.create_asset('MI_Sicklejaw', PKG, unreal.MaterialInstanceConstant,
                         unreal.MaterialInstanceConstantFactoryNew())
MEL.set_material_instance_parent(mi, mat)
MEL.set_material_instance_scalar_parameter_value(mi, 'AggroGlow', 0.0)
MEL.set_material_instance_scalar_parameter_value(mi, 'GlowIntensity', GLOW_INTENSITY)
MEL.update_material_instance(mi)

EAL.save_asset(MAT_PATH)
EAL.save_asset(MI_PATH)
for pname in TEXTURES:
    EAL.save_asset('%s/%s' % (TEXDIR, TEXTURES[pname]))
log('SAVED %s and %s' % (MAT_PATH, MI_PATH))
log('NEXT: assign MI_Sicklejaw to SK_Sicklejaw material slot 0, then drive AggroGlow '
    'from a UMaterialInstanceDynamic created in C++.')
print('PFA_MATERIAL_OK')
