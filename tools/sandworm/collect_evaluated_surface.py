"""Single-pose Blender adapter for the straight-cylinder SURFACE test only.

Run in a fresh, background Blender process with the source .blend already loaded
and auto-execution disabled. Arguments follow Blender's ``--`` separator. Every
scene MESH must be explicitly named; stage meshes are not silently excluded.
The supplied head-to-world JSON is a 4x4 proper rigid matrix in Blender units.
Its columns are the head X/Y/Z axes; translation is the head origin. All radius,
depth, start and tolerance arguments are METERS; units-per-meter is explicit.
The adapter bounds single IEEE binary32 nearest-storage rounding neighborhoods
of evaluated positions/object matrices, not accumulated modifier/authoring error.

Example (supply the actual names, action, dimensions and matrix, never this text):
  blender --background --disable-autoexec SOURCE.blend --python THIS.py --
    --source-blend SOURCE.blend --source-sha256 PRELAUNCH_SHA256 --asset-root ASSET_DIR --armature RIG
    --action ACTION --frame 1 --expected-mesh-count 2 --mesh-name BODY
    --mesh-name TEETH --head-frame-json FRAME.json --units-per-meter 100
    --radius 13 --depth 25 --output OUTSIDE_ASSET_TREE/fresh-receipt.json

No save/promote operation. Action/NLA/frame changes are temporary and restored.
Unsupported instancing, hidden/disabled pose dependencies or empty component
surfaces fail the WHOLE batch. No hidden surface is silently omitted.
An extraction receipt does not prove mesh suitability, passage topology, target
fit, animation continuity, export equivalence, or game behavior. CPU tests cover
contracts/transforms only; Blender extraction requires separate fixture evidence.
"""

import argparse
from dataclasses import asdict, dataclass
from fractions import Fraction
import hashlib
import json
import math
import numbers
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import clearance_geometry

if Path(clearance_geometry.__file__).resolve() != Path(__file__).resolve().with_name("clearance_geometry.py"):
    raise RuntimeError("a different clearance_geometry module is already loaded")
check_surface_clearance = clearance_geometry.check_surface_clearance


RIGID_TOLERANCE = 1e-6  # Float matrix roundoff only; matrices are never normalized.


def _finite(value):
    if isinstance(value, bool) or not isinstance(value, numbers.Real):
        raise ValueError("expected a finite real number")
    value = float(value)
    if not math.isfinite(value):
        raise ValueError("expected a finite real number")
    return value


def _matrix(matrix):
    rows = tuple(tuple(_finite(v) for v in row) for row in matrix)
    if len(rows) != 4 or any(len(row) != 4 for row in rows):
        raise ValueError("matrix must be 4x4")
    if rows[3] != (0, 0, 0, 1):
        raise ValueError("matrix must be affine, with last row [0,0,0,1]")
    return rows


def _det(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
            - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def validate_head_frame(matrix):
    """Accept a proper rigid affine head-to-world matrix, never scale/shear/mirror."""
    m = _matrix(matrix)
    for i in range(3):
        for j in range(3):
            dot = math.fsum(m[k][i] * m[k][j] for k in range(3))
            if abs(dot - (1 if i == j else 0)) > RIGID_TOLERANCE:
                raise ValueError("head frame must be orthonormal; scale/shear/singularity rejected")
    if abs(_det(m) - 1) > 4 * RIGID_TOLERANCE:
        raise ValueError("head frame must have determinant +1; reflection rejected")
    return m


def validate_units(units_per_meter):
    units = _finite(units_per_meter)
    if units <= 0:
        raise ValueError("units_per_meter must be positive")
    return units


def validate_surface_contract(inventory, expected_names, expected_count):
    """inventory contains ALL scene (name,type) pairs, including non-mesh objects."""
    expected = tuple(expected_names)
    if (isinstance(expected_count, bool) or not isinstance(expected_count, numbers.Integral)
            or expected_count <= 0 or expected_count != len(expected)):
        raise ValueError("positive expected mesh count must equal the explicit name count")
    if any(not isinstance(name, str) or not name for name in expected):
        raise ValueError("expected mesh names must be nonempty strings")
    if len(set(expected)) != len(expected):
        raise ValueError("duplicate expected mesh names")
    observed = {}
    for name, kind in inventory:
        if name in observed:
            raise ValueError("duplicate observed object name: " + name)
        observed[name] = kind
    missing = [name for name in expected if name not in observed]
    wrong_type = [name for name in expected if name in observed and observed[name] != "MESH"]
    unexpected = sorted(name for name, kind in observed.items()
                        if kind == "MESH" and name not in expected)
    if missing or wrong_type or unexpected:
        raise ValueError(f"surface contract mismatch: missing={missing}, non_mesh={wrong_type}, "
                         f"unexpected_meshes={unexpected}")
    return expected


def validate_evaluation_policy(states, armature_name):
    """Reject known rest/frozen-pose paths; this is not a dependency-completeness proof."""
    for state in states:
        name = state["name"]
        if not state["in_view_layer"] or state["hide_viewport"] or state["hidden_in_view_layer"]:
            raise ValueError("hidden/disabled/excluded pose surface or armature: " + name)
        if state["pose_position"] == "REST":
            raise ValueError("armature is configured to evaluate rest pose: " + name)
        if state["shape_keys_muted"] or state["show_only_shape_key"]:
            raise ValueError("muted/pinned shape-key evaluation is unsupported: " + name)
        if state["type"] == "MESH" and state["mode"] != "OBJECT":
            raise ValueError("mesh edit/sculpt modes are unsupported: " + name)
        for modifier in state["modifiers"]:
            if not modifier["show_viewport"]:
                raise ValueError("viewport-disabled modifier: " + name + "/" + modifier["name"])
            if modifier["type"] == "ARMATURE" and modifier["armature_target"] != armature_name:
                raise ValueError("armature modifier requires the explicit selected rig: " + name)


@dataclass(frozen=True)
class TransformedSurface:
    vertices: list
    error_bound: Fraction


class StoragePrecisionError(ValueError):
    """Observed data cannot satisfy the specified storage-neighborhood contract."""


def float32_storage_radius(value):
    """Maximum half-spacing to adjacent binary32 numbers, exact and conservative."""
    value = _finite(value)
    try:
        packed = struct.pack(">f", abs(value))
    except OverflowError as error:
        raise StoragePrecisionError("input exceeds finite binary32 storage") from error
    if struct.unpack(">f", packed)[0] != abs(value):
        raise StoragePrecisionError("observed value is not exactly representable in binary32")
    bits = struct.unpack(">I", packed)[0]
    if bits == 0x7f7fffff:
        raise StoragePrecisionError("binary32 maximum has no finite upper neighbor")
    previous = struct.unpack(">f", struct.pack(">I", bits - 1))[0] if bits else -2.0 ** -149
    following = struct.unpack(">f", struct.pack(">I", bits + 1))[0]
    center = Fraction(abs(value))
    return max(center - Fraction(previous), Fraction(following) - center) / 2


def transform_vertices(vertices, object_world, head_to_world, units_per_meter, *, source_precision):
    """Return coordinates AND a conservative uniform 3D uncertainty bound.

    All affine arithmetic uses exact rational values of supplied binary floats.
    Let R be the accepted head rotation and Q its nearest proper orthogonal
    matrix. delta=sum(abs(R^T R-I)) bounds the spectral defect. For delta<1,
    ||R-Q||2 <= delta/(1+sqrt(1-delta)) <= delta. Thus using R^T instead of
    Q^T has error <= delta*||world-origin||1/units. Add the exact L1 rounding
    error of converting the final coordinates to floats. The largest bound at
    any vertex also bounds every convex combination on every triangle.
    This defines a near-rigid input's intended frame as Q, without silently
    treating scaled/sheared R as exact. Use check_transformed_surface to apply
    uncertainty to radial AND axial boundaries; raw coordinates do not certify.
    float32_storage additionally bounds the nearest-rounding neighborhoods of
    evaluated positions and object-world coefficients, NOT arbitrary upstream
    modifier arithmetic or authoring fidelity. For each affine product m*p,
    uncertainty is |m|*ep+|p|*em+em*ep; add translation error. Its world L1 bound
    also bounds error after Q's orthogonal rotation. Head matrix and units remain
    exact supplied inputs. exact_supplied is available ONLY to explicit callers.
    """
    if source_precision not in ("float32_storage", "exact_supplied"):
        raise StoragePrecisionError("an explicit supported source_precision contract is required")
    obj = _matrix(object_world)
    if not math.isfinite(_det(obj)) or _det(obj) == 0:
        raise ValueError("object world matrix must be nonsingular")
    head = tuple(tuple(Fraction(v) for v in row) for row in validate_head_frame(head_to_world))
    obj_error = tuple(tuple(float32_storage_radius(v) if source_precision == "float32_storage" else Fraction(0)
                            for v in row) for row in obj)
    obj = tuple(tuple(Fraction(v) for v in row) for row in obj)
    units = Fraction(validate_units(units_per_meter))
    defect = sum(abs(sum(head[k][i] * head[k][j] for k in range(3)) - (i == j))
                 for i in range(3) for j in range(3))
    if defect >= 1:
        raise ValueError("cannot bound the near-rigid frame's orthogonal correction")
    result, error_bound = [], Fraction(0)
    for vertex in vertices:
        p = tuple(Fraction(_finite(v)) for v in vertex)
        if len(p) != 3:
            raise ValueError("vertex must contain three coordinates")
        pe = tuple(float32_storage_radius(v) if source_precision == "float32_storage" else Fraction(0) for v in p)
        world = tuple(sum(obj[i][k] * p[k] for k in range(3)) + obj[i][3]
                      for i in range(3))
        delta = tuple(world[i] - head[i][3] for i in range(3))
        exact = tuple(sum(head[k][i] * delta[k] for k in range(3)) / units
                      for i in range(3))
        point = tuple(float(v) for v in exact)
        if not all(math.isfinite(v) for v in point):
            raise ValueError("transformed coordinates must be finite")
        result.append(point)
        error = defect * sum(abs(v) for v in delta) / units
        error += sum(obj_error[i][3] + sum(abs(obj[i][k])*pe[k] + abs(p[k])*obj_error[i][k]
                                          + obj_error[i][k]*pe[k] for k in range(3))
                     for i in range(3)) / units
        error += sum(abs(Fraction(rounded) - value) for rounded, value in zip(point, exact))
        error_bound = max(error_bound, error)
    return TransformedSurface(result, error_bound)


def _outward(value, upward):
    rounded = float(value)
    if not math.isfinite(rounded):
        raise ValueError("uncertainty expansion is not finite")
    needs_expansion = Fraction(rounded) < value if upward else Fraction(rounded) > value
    if needs_expansion:
        rounded = math.nextafter(rounded, math.inf if upward else -math.inf)
    if not math.isfinite(rounded):
        raise ValueError("uncertainty expansion cannot be represented")
    return rounded


def check_transformed_surface(surface, triangles, *, radius, depth, axial_start=0, tolerance=0):
    """Guard the helper with an enlarged slab and nonpassing radial error band.

    Any true surface point in the requested slab maps within E of a computed
    point in [start-E,end+E]. Its radial distance differs by <=E. Consequently a
    CLEAR result on that expanded slab with radius+tolerance+E implies clearance
    for the requested slab/frame. Bounds are rounded OUTWARD when passed to the
    float-input helper. Excess/unrepresentable uncertainty cannot certify clear.
    """
    guard = dict(frame_interpretation="nearest proper orthogonal head axes", error_bound_m=None)
    try:
        error = surface.error_bound
        if not isinstance(error, Fraction) or error < 0:
            raise ValueError("missing/invalid exact transform error bound")
        r, d, start, tol = (Fraction(_finite(v)) for v in (radius, depth, axial_start, tolerance))
        if r <= 0 or d <= 0 or tol < 0 or tol >= r:
            raise ValueError("invalid requested cylinder dimensions/tolerance")
        guard["error_bound_m"] = _outward(error, True)
        lower = _outward(start - error, False)
        upper = _outward(start + d + error, True)
        expanded_depth = _outward(Fraction(upper) - Fraction(lower), True)
        expanded_tolerance = _outward(tol + error, True)
        if expanded_tolerance >= r:
            raise ValueError("transform uncertainty reaches the requested cylinder radius")
        guard.update(axial_start_m=lower, depth_m=expanded_depth, tolerance_m=expanded_tolerance)
        result = check_surface_clearance(surface.vertices, triangles, radius=radius,
                                         depth=expanded_depth, axial_start=lower, tolerance=expanded_tolerance)
        return result, guard
    except (TypeError, ValueError, ArithmeticError) as error:
        return clearance_geometry.ClearanceResult("uncertain", str(error)), guard


def _sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_prelaunch_hash(expected, observed):
    if (not isinstance(expected, str) or len(expected) != 64
            or any(c not in "0123456789abcdefABCDEF" for c in expected)):
        raise ValueError("source-sha256 must be an externally recorded prelaunch SHA-256")
    if expected.lower() != observed.lower():
        raise ValueError("source .blend differs from the externally supplied prelaunch SHA-256")


def validate_output_path(output, asset_root, source_blend):
    """Reject collisions and any write within the declared/canonical art tree."""
    if Path(output).exists() or Path(output).is_symlink():
        raise ValueError("receipt output already exists, including symlink paths")
    output, root, source = map(lambda p: Path(p).resolve(), (output, asset_root, source_blend))
    canonical_art = Path(__file__).resolve().parents[2] / "art"
    if not source.is_relative_to(root):
        raise ValueError("source .blend must be within the explicitly declared asset root")
    if output.is_relative_to(root) or output.is_relative_to(canonical_art):
        raise ValueError("receipt output must be outside the asset tree")
    if output.exists() or not output.parent.is_dir():
        raise ValueError("receipt requires a fresh path in an existing output directory")
    return output


def collect(args, head_frame):
    """Blender-only extraction. Import bpy lazily; never claim this from CPU tests."""
    import bpy

    scene = bpy.context.scene
    source = Path(args.source_blend).resolve()
    validate_prelaunch_hash(args.source_sha256, _sha(source))
    if not bpy.data.filepath or Path(bpy.data.filepath).resolve() != source:
        raise ValueError("loaded Blender filepath does not match source-blend")
    if bpy.data.is_dirty:
        raise ValueError("loaded scene has unsaved changes; source hash would not identify it")
    if bpy.data.libraries or bpy.utils.blend_paths(absolute=True, packed=False, local=False):
        raise ValueError("linked or unpacked external dependencies are not bound by this receipt")
    names = validate_surface_contract(((o.name, o.type) for o in scene.objects),
                                      args.mesh_name, args.expected_mesh_count)
    rig = scene.objects.get(args.armature)
    if rig is None or rig.type != "ARMATURE" or rig.animation_data is None:
        raise ValueError("named armature with existing animation data is required")
    action = bpy.data.actions.get(args.action)
    if action is None:
        raise ValueError("requested action is missing")
    animation = rig.animation_data
    if animation.use_tweak_mode or animation.action_blend_type != "REPLACE" or animation.action_influence != 1:
        raise ValueError("single-action evaluation requires no tweak mode and full REPLACE influence")
    slots = list(action.slots) if hasattr(action, "slots") else []
    if slots:
        chosen = [s for s in slots if s.identifier == args.action_slot] if args.action_slot else slots
        if len(chosen) != 1 or chosen[0].target_id_type != "OBJECT":
            raise ValueError("action requires one unambiguous OBJECT slot; use --action-slot")
        slot = chosen[0]
    else:
        if args.action_slot:
            raise ValueError("requested action slot is unavailable")
        slot = None
    old = (animation.action, getattr(animation, "action_slot", None), animation.use_nla,
           scene.frame_current, scene.frame_subframe)
    vertices, triangles, objects, transform_error = [], [], [], Fraction(0)
    try:
        animation.action = action
        if slot is not None:
            animation.action_slot = slot
        animation.use_nla = False
        scene.frame_set(args.frame, subframe=args.subframe)
        bpy.context.view_layer.update()
        if (scene.frame_current != args.frame or abs(scene.frame_subframe - args.subframe) > 1e-7
                or animation.action != action
                or (slot is not None and animation.action_slot != slot)):
            raise ValueError("requested action/slot/frame did not remain active after evaluation")
        states = []
        for name in (*names, args.armature):
            obj = scene.objects[name]
            present = bpy.context.view_layer.objects.get(name) is not None
            shape_keys = getattr(obj.data, "shape_keys", None)
            modifiers = [dict(name=m.name, type=m.type, show_viewport=m.show_viewport,
                              show_render=m.show_render,
                              armature_target=getattr(getattr(m, "object", None), "name", None)
                              if m.type == "ARMATURE" else None) for m in obj.modifiers]
            states.append(dict(name=name, type=obj.type, mode=obj.mode, in_view_layer=present,
                               hide_viewport=obj.hide_viewport,
                               hidden_in_view_layer=obj.hide_get(view_layer=bpy.context.view_layer) if present else None,
                               pose_position=getattr(obj.data, "pose_position", None), modifiers=modifiers,
                               show_only_shape_key=getattr(obj, "show_only_shape_key", False),
                               shape_keys_muted=[k.name for k in shape_keys.key_blocks if k.mute] if shape_keys else []))
        validate_evaluation_policy(states, args.armature)
        depsgraph = bpy.context.evaluated_depsgraph_get()
        if any(instance.is_instance for instance in depsgraph.object_instances):
            raise ValueError("instanced geometry requires explicit realization; extraction rejected")
        for name in names:
            original = scene.objects[name]
            if bpy.context.view_layer.objects.get(name) is None:
                raise ValueError("contracted object is absent from active view layer: " + name)
            evaluated = original.evaluated_get(depsgraph)
            if evaluated.type != "MESH":
                raise ValueError("evaluated object is not a mesh: " + name)
            mesh = None
            try:
                mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph)
                if mesh is None:
                    raise ValueError("evaluated mesh is unavailable: " + name)
                mesh.calc_loop_triangles()
                if not mesh.vertices or not mesh.loop_triangles:
                    raise ValueError("contracted object has an empty evaluated surface: " + name)
                v_start, t_start = len(vertices), len(triangles)
                world = _matrix(evaluated.matrix_world)
                surface = transform_vertices((v.co for v in mesh.vertices), world,
                                             head_frame, args.units_per_meter, source_precision="float32_storage")
                vertices.extend(surface.vertices)
                transform_error = max(transform_error, surface.error_bound)
                triangles.extend(tuple(v_start + i for i in tri.vertices) for tri in mesh.loop_triangles)
                objects.append(dict(source_name=name, evaluated_name=evaluated.name,
                                    vertex_start=v_start, vertex_end=len(vertices),
                                    triangle_start=t_start, triangle_end=len(triangles),
                                    vertex_count=len(mesh.vertices), triangle_count=len(mesh.loop_triangles),
                                    transform_error_bound_m=_outward(surface.error_bound, True),
                                    evaluated_matrix_world=world))
            finally:
                evaluated.to_mesh_clear()
        result, guard = check_transformed_surface(TransformedSurface(vertices, transform_error), triangles,
                                                  radius=args.radius, depth=args.depth,
                                                  axial_start=args.axial_start, tolerance=args.tolerance)
        return dict(extraction_completed=True, surface_check=asdict(result), numerical_guard=guard, objects=objects,
                    evaluation_policy="visible enabled pose dependencies; no silent omission; full dependency proof pending",
                    object_evaluation_states=states,
                    vertices=len(vertices), triangles=len(triangles), blender_version=bpy.app.version_string,
                    scene=scene.name, view_layer=bpy.context.view_layer.name,
                    actual_action=animation.action.name,
                    actual_action_slot=getattr(getattr(animation, "action_slot", None), "identifier", None),
                    frame=scene.frame_current, subframe=scene.frame_subframe, nla_enabled=False)
    finally:
        animation.action = old[0]
        if old[1] is not None:
            animation.action_slot = old[1]
        animation.use_nla = old[2]
        scene.frame_set(old[3], subframe=old[4])
        bpy.context.view_layer.update()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("source-blend", "source-sha256", "asset-root", "armature", "action", "head-frame-json", "output"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--mesh-name", action="append", required=True)
    parser.add_argument("--expected-mesh-count", type=int, required=True)
    parser.add_argument("--action-slot")
    parser.add_argument("--frame", type=int, required=True)
    parser.add_argument("--subframe", type=float, default=0)
    for name in ("units-per-meter", "radius", "depth"):
        parser.add_argument("--" + name, type=float, required=True)
    parser.add_argument("--axial-start", type=float, default=0)
    parser.add_argument("--tolerance", type=float, default=0)
    args = parser.parse_args(argv)
    output = validate_output_path(args.output, args.asset_root, args.source_blend)
    receipt = dict(schema="sandworm-evaluated-surface-v1", extraction_completed=False,
                   source_precision_contract="single IEEE binary32 nearest-storage neighborhoods for evaluated positions/object matrix",
                   head_and_units_precision="exact supplied inputs; nearest proper orthogonal head-frame interpretation",
                   upstream_modifier_arithmetic_proven=False, authored_shape_fidelity_proven=False,
                   model_accepted=False, passage_topology_proven=False, target_fit_proven=False,
                   continuous_animation_proven=False, scope="all scene mesh surfaces, one pose, straight cylinder",
                   loaded_source_identity_proven=False,
                   source_identity_scope="observed disk hashes only; immutable snapshot and launch binding unverified",
                   request={k: repr(v) if isinstance(v, float) and not math.isfinite(v) else v
                            for k, v in vars(args).items()}, status="invalid")
    exit_code = 1
    try:
        paths = dict(source_blend=Path(args.source_blend).resolve(), adapter=Path(__file__).resolve(),
                     helper=Path(__file__).resolve().with_name("clearance_geometry.py"),
                     head_frame=Path(args.head_frame_json).resolve())
        receipts = {key: dict(path=str(path), sha256=_sha(path)) for key, path in paths.items()}
        receipt["inputs"] = receipts
        validate_prelaunch_hash(args.source_sha256, receipts["source_blend"]["sha256"])
        head = validate_head_frame(json.loads(paths["head_frame"].read_text(encoding="utf-8-sig")))
        receipt["head_to_world"] = head
        receipt["rigid_matrix_tolerance"] = RIGID_TOLERANCE
        validate_units(args.units_per_meter)
        if not 0 <= _finite(args.subframe) < 1:
            raise ValueError("subframe must be in [0,1)")
        dimensions = check_surface_clearance([], [], radius=args.radius, depth=args.depth,
                                              axial_start=args.axial_start, tolerance=args.tolerance)
        if dimensions.status == "invalid":
            raise ValueError(dimensions.reason)
        collected = collect(args, head)
        if any(_sha(path) != receipts[key]["sha256"] for key, path in paths.items()):
            raise ValueError("an input file changed during extraction; receipt rejected")
        receipt.update(collected)
        receipt["status"] = collected["surface_check"]["status"]
        exit_code = 0 if collected["surface_check"]["surface_clear"] else 2
    except Exception as error:
        receipt["error"] = f"{type(error).__name__}: {error}"
        if isinstance(error, StoragePrecisionError):
            receipt["status"] = "uncertain"
    # Exclusive create is the final collision gate; never overwrite an existing receipt.
    with output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(receipt, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps(dict(receipt=str(output), status=receipt["status"], model_accepted=False)))
    return exit_code


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    raise SystemExit(main(args))
