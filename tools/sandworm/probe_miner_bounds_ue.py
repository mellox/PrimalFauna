"""Read-only vanilla miner envelope probe. Run through tools/ue_headless.ps1.

Reads Blueprint component templates and composes their hierarchy transforms.
Never spawns actors, saves a package, or edits source game assets. Construction
script changes and animated bounds require a separate runtime measurement.
"""
import hashlib
import json
import math
import itertools
from pathlib import Path

import unreal

ROOT = Path("C:/Claude/Projects/SatisfactoryModLoader/Content/FactoryGame/Buildable/Factory")
OUT = Path("C:/Claude/Projects/_team/PFA-sandworm-2026-10-14/miner-bounds-editor.json")


def xyz(v):
    return [float(v.x), float(v.y), float(v.z)]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    files = sorted(p for p in ROOT.glob("Miner*/*.uasset")
                   if p.stem.lower() in {"build_minermk1", "build_minermk2", "build_minermk3"})
    if len(files) != 3:
        raise RuntimeError("Expected observed Mk1/2/3 build assets, found %s" % files)
    before = {str(p): sha(p) for p in files}
    report = {
        "status": "incomplete",
        "sizing_accepted": False,
        "units": "cm",
        "scope": "Local editor Blueprint component-template mesh bounds with hierarchy transforms; NOT constructed actor, cooked gameplay or animation swept envelope",
        "limitations": ["Editor assets may differ from installed game or contain placeholder geometry", "Construction scripts NOT executed; additional components or transforms may be absent", "No animation playback envelope sampled", "Attached belts, cables and foundations are not part of this target"],
        "miners": [],
    }
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    library = unreal.SubobjectDataBlueprintFunctionLibrary
    def template(data, blueprint):
        obj = library.get_object_for_blueprint(data, blueprint)
        return obj if obj else library.get_object(data)
    for path in files:
        asset_path = "/Game/FactoryGame/Buildable/Factory/" + path.relative_to(ROOT).with_suffix("").as_posix()
        row = {"source": str(path), "source_sha256": before[str(path)], "source_modified_unix": path.stat().st_mtime, "asset": asset_path}
        try:
            blueprint = unreal.load_asset(asset_path)
            unreal.log("PFA_MINER LOADED %s" % asset_path)
            handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
            unreal.log("PFA_MINER HANDLES %s count=%d" % (path.stem, len(handles)))
            row["components"] = []
            visual_min, visual_max = [math.inf] * 3, [-math.inf] * 3
            for handle in handles:
                data = library.get_data(handle)
                comp = template(data, blueprint)
                if not isinstance(comp, unreal.SceneComponent):
                    continue
                entry = {"name": comp.get_name(), "class": comp.get_class().get_name()}
                chain = []
                cursor = handle
                for _ in range(64):
                    if not library.is_handle_valid(cursor):
                        break
                    cd = library.get_data(cursor)
                    obj = template(cd, blueprint)
                    if isinstance(obj, unreal.SceneComponent):
                        chain.append(obj.get_relative_transform())
                    cursor = library.get_parent_handle(cd)
                else:
                    raise RuntimeError("Component parent traversal exceeds 64")
                entry["transform_chain_child_to_parent"] = [str(t) for t in chain]
                mesh = None
                if isinstance(comp, unreal.StaticMeshComponent):
                    mesh = comp.get_editor_property("static_mesh")
                elif isinstance(comp, unreal.SkinnedMeshComponent):
                    mesh = comp.get_skinned_asset()
                if mesh:
                    bounds = mesh.get_bounds()
                    origin, extent = xyz(bounds.origin), xyz(bounds.box_extent)
                    points = []
                    for signs in itertools.product((-1, 1), repeat=3):
                        point = unreal.Vector(*[o+s*e for o, s, e in zip(origin, signs, extent)])
                        for transform in chain:
                            point = transform.transform_location(point)
                        points.append(xyz(point))
                    entry["mesh"] = mesh.get_path_name()
                    entry["mesh_local_origin_cm"] = origin
                    entry["mesh_local_half_extent_cm"] = extent
                    entry["placeholder_cube_signature"] = all(abs(e-128.0) < 0.01 for e in extent)
                    entry["transformed_box_corners_cm"] = points
                    for i in range(3):
                        visual_min[i] = min(visual_min[i], min(p[i] for p in points))
                        visual_max[i] = max(visual_max[i], max(p[i] for p in points))
                row["components"].append(entry)
            if not all(math.isfinite(v) for v in visual_min + visual_max):
                raise RuntimeError("No nonzero visual mesh component bounds")
            dims = [b-a for a, b in zip(visual_min, visual_max)]
            row["visual_union_min_cm"] = visual_min
            row["visual_union_max_cm"] = visual_max
            row["visual_union_dimensions_cm"] = dims
            row["vertical_swallow_clear_circle_min_cm"] = math.hypot(dims[0], dims[1])
            row["status"] = "measured_editor_component_templates"
            row["placeholder_mesh_signature_detected"] = any(c.get("placeholder_cube_signature", False) for c in row["components"])
            unreal.log("PFA_MINER %s dims_cm=%s opening_cm=%.2f" % (path.stem, dims, row["vertical_swallow_clear_circle_min_cm"]))
        except Exception as exc:
            row["status"] = "failed"
            row["error"] = str(exc)
            unreal.log_error("PFA_MINER ASSERT-FAIL %s: %s" % (path.stem, exc))
        report["miners"].append(row)
    report["asset_bytes_unchanged"] = all(sha(p) == before[str(p)] for p in files)
    report["status"] = "measured_editor_component_templates" if report["asset_bytes_unchanged"] and all(r["status"] == "measured_editor_component_templates" for r in report["miners"]) else "failed"
    report["sizing_rejection_reason"] = "Local editor component templates do not prove a constructed cooked miner envelope; placeholder cube signatures are reported per component. Require real game/cooked geometry and animation validation."
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, indent=2, allow_nan=False), encoding="utf-8")
    unreal.log("PFA_MINER REPORT %s status=%s" % (OUT, report["status"]))
    if report["status"] == "failed":
        raise RuntimeError("PFA_MINER ASSERT-FAIL: incomplete measurement; read report")


main()
