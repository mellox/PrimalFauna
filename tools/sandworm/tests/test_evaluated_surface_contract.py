"""Pure CPU contract/transform checks. These do NOT execute or mock Blender extraction."""

import copy
from contextlib import redirect_stdout
from fractions import Fraction
import hashlib
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from collect_evaluated_surface import (transform_vertices, validate_head_frame, validate_units,
                                       validate_surface_contract, validate_output_path, main,
                                       check_transformed_surface, TransformedSurface, validate_prelaunch_hash,
                                       validate_evaluation_policy, float32_storage_radius, StoragePrecisionError)
from clearance_geometry import check_surface_clearance


IDENTITY = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]


class ContractTests(unittest.TestCase):
    def test_complete_mesh_inventory_with_nonmesh_objects(self):
        inventory = [("Body", "MESH"), ("Teeth", "MESH"), ("Rig", "ARMATURE"), ("Camera", "CAMERA")]
        self.assertEqual(validate_surface_contract(inventory, ["Body", "Teeth"], 2), ("Body", "Teeth"))

    def test_missing_unexpected_duplicate_and_nonmesh_surfaces_rejected(self):
        cases = [([("Body", "MESH")], ["Body", "Teeth"], 2),
                 ([("Body", "MESH"), ("HiddenTeeth", "MESH")], ["Body"], 1),
                 ([("Body", "MESH"), ("Body", "MESH")], ["Body"], 1),
                 ([("Body", "ARMATURE")], ["Body"], 1),
                 ([("Body", "MESH")], ["Body", "Body"], 2),
                 ([("Body", "MESH")], ["Body"], 2),
                 ([], [], 0), ([("Body", "MESH")], ["Body"], True)]
        for inventory, expected, count in cases:
            with self.subTest(inventory=inventory, expected=expected, count=count):
                with self.assertRaises(ValueError):
                    validate_surface_contract(inventory, expected, count)

    def test_rigid_transform_and_unit_conversion_preserve_analytic_clearance(self):
        verts = [(0.2, 12.9, -2), (0.8, 12.9, 6), (0.5, 20, 6)]
        # 90-degree world rotation + nonzero translation; model coordinates are cm.
        head = [[0, -1, 0, 500], [1, 0, 0, -200], [0, 0, 1, 700], [0, 0, 0, 1]]
        transformed = transform_vertices([(x*100, y*100, z*100) for x, y, z in verts], head, head, 100,
                                         source_precision="exact_supplied")
        for got, expected in zip(transformed.vertices, verts):
            for a, b in zip(got, expected):
                self.assertAlmostEqual(a, b)
        result, guard = check_transformed_surface(transformed, [(0, 1, 2)], radius=13, depth=1)
        self.assertEqual(result.status, "intrusion")
        self.assertAlmostEqual(result.min_radius, 12.9)

    def test_evaluated_object_scale_is_applied_and_head_translation_is_world_units(self):
        obj = [[2, 0, 0, 100], [0, 3, 0, 200], [0, 0, 4, 300], [0, 0, 0, 1]]
        head = copy.deepcopy(IDENTITY)
        head[0][3], head[1][3], head[2][3] = 50, 150, 250
        result = transform_vertices([(10, 10, 10)], obj, head, 100, source_precision="exact_supplied")
        self.assertEqual(result.vertices, [(0.7, 0.8, 0.9)])

    def test_scaled_sheared_singular_reflected_nonfinite_and_projective_head_rejected(self):
        changes = [(0, 0, 2), (0, 1, 0.01), (0, 0, 0), (0, 0, -1),
                   (1, 1, math.nan), (1, 3, math.inf), (3, 0, 1e-15)]
        for i, j, value in changes:
            matrix = copy.deepcopy(IDENTITY)
            matrix[i][j] = value
            with self.subTest(change=(i, j, value)):
                with self.assertRaises(ValueError):
                    validate_head_frame(matrix)
        with self.assertRaises(ValueError):
            validate_head_frame([[1, 0, 0], [0, 1, 0], [0, 0, 1]])

    def test_rotation_roundoff_is_accepted_without_normalizing_matrix(self):
        angle = 0.37
        c, s = math.cos(angle), math.sin(angle)
        matrix = [[c, -s, 0, 4], [s, c, 0, 5], [0, 0, 1, 6], [0, 0, 0, 1]]
        self.assertEqual(validate_head_frame(matrix), tuple(map(tuple, matrix)))
        # A rotated tangent remains non-passing WITHOUT requesting a manual margin.
        moved = transform_vertices([(0, 1, -1), (1, 1, -1), (0, 1, 1)], matrix, matrix, 1,
                                   source_precision="exact_supplied")
        result, guard = check_transformed_surface(moved, [(0, 1, 2)], radius=1, depth=1, tolerance=0)
        self.assertEqual(result.status, "contact")

    def test_review_reproducer_rotated_tangency_never_certifies_clear(self):
        c, s = math.cos(0.6), math.sin(0.6)
        matrix = [[c, -s, 0, 4], [s, c, 0, 5], [0, 0, 1, 6], [0, 0, 0, 1]]
        surface = transform_vertices([(0, 1, -1), (1, 1, -1), (0, 1, 1)], matrix, matrix, 1,
                                     source_precision="exact_supplied")
        result, guard = check_transformed_surface(surface, [(0, 1, 2)], radius=1, depth=1)
        self.assertFalse(result.surface_clear)
        self.assertEqual(result.status, "contact")
        self.assertGreater(guard["error_bound_m"], 0)

    def test_accepted_near_rigid_scales_have_conservative_radial_band(self):
        for scale in (1.0000004, 0.9999996):
            head = copy.deepcopy(IDENTITY)
            head[1][1] = scale
            surface = transform_vertices([(0, 1, -1), (1, 1, -1), (0, 1, 1)], IDENTITY, head, 1,
                                         source_precision="exact_supplied")
            result, guard = check_transformed_surface(surface, [(0, 1, 2)], radius=1, depth=1)
            self.assertEqual(result.status, "contact")
            self.assertFalse(result.surface_clear)

    def test_error_guard_expands_both_axial_boundaries(self):
        for x in (-1e-10, 1 + 1e-10):
            vertices = [(x, 0.5, -0.1), (x, 0.6, 0.1), (x, 0.5, 0.1),
                        (0.2, 2, -1), (0.8, 2, -1), (0.5, 2, 1)]
            triangles = [(0, 1, 2), (3, 4, 5)]
            raw = check_surface_clearance(vertices, triangles, radius=1, depth=1)
            self.assertTrue(raw.surface_clear)  # Intruding triangle entirely outside raw slab.
            result, guard = check_transformed_surface(TransformedSurface(vertices, Fraction(1e-9)),
                                                       triangles, radius=1, depth=1)
            self.assertEqual(result.status, "intrusion")
            self.assertEqual(result.slab_intersections, 2)

    def test_excessive_uncertainty_is_noncertifying(self):
        surface = TransformedSurface([(0, 2, -1), (1, 2, -1), (0, 2, 1)], Fraction(2))
        result, guard = check_transformed_surface(surface, [(0, 1, 2)], radius=1, depth=1)
        self.assertEqual(result.status, "uncertain")
        self.assertFalse(result.surface_clear)

    def test_receipt_mixed_precision_tangency_in_centimeters_is_nonpassing(self):
        c, s = math.cos(0.6), math.sin(0.6)
        head = [[c, -s, 0, 4], [s, c, 0, 5], [0, 0, 1, 6], [0, 0, 0, 1]]
        stored = [[0.8253356218338013, -0.5646424889564514, 0, 4],
                  [0.5646424889564514, 0.8253356218338013, 0, 5],
                  [0, 0, 1, 6], [0, 0, 0, 1]]
        verts = [(0, 100, -100), (100, 100, -100), (0, 100, 100)]
        represented = transform_vertices(verts, stored, head, 100, source_precision="exact_supplied")
        raw, _ = check_transformed_surface(represented, [(0, 1, 2)], radius=1, depth=1)
        self.assertTrue(raw.surface_clear)  # The represented geometry IS slightly clear.
        neighborhood = transform_vertices(verts, stored, head, 100, source_precision="float32_storage")
        guarded, _ = check_transformed_surface(neighborhood, [(0, 1, 2)], radius=1, depth=1)
        self.assertEqual(guarded.status, "contact")
        self.assertFalse(guarded.surface_clear)
        self.assertGreater(neighborhood.error_bound, Fraction(raw.min_radius - 1))

    def test_storage_bounds_follow_ulp_scale_including_zero_and_subnormal(self):
        self.assertEqual(float32_storage_radius(0), Fraction(1, 2**150))
        self.assertEqual(float32_storage_radius(2.0**-149), Fraction(1, 2**150))
        self.assertEqual(float32_storage_radius(1), Fraction(1, 2**24))
        self.assertEqual(float32_storage_radius(-1), Fraction(1, 2**24))
        self.assertEqual(float32_storage_radius(2**26), 4)
        for invalid in (0.1, 1e100, float.fromhex("0x1.fffffep+127")):
            with self.assertRaises(StoragePrecisionError):
                float32_storage_radius(invalid)

    def test_large_stored_world_translation_increases_uncertainty_without_fixed_epsilon(self):
        matrix = copy.deepcopy(IDENTITY)
        matrix[0][3] = matrix[1][3] = 2**26
        verts = [(0, 100, -100), (100, 100, -100), (0, 100, 100)]
        near = transform_vertices(verts, IDENTITY, IDENTITY, 100, source_precision="float32_storage")
        far = transform_vertices(verts, matrix, matrix, 100, source_precision="float32_storage")
        self.assertGreaterEqual(far.error_bound, Fraction(8, 100))
        self.assertGreater(far.error_bound, near.error_bound * 1000)
        result, _ = check_transformed_surface(far, [(0, 1, 2)], radius=1, depth=1)
        self.assertEqual(result.status, "contact")

    def test_storage_contract_rejects_nonbinary32_mesh_or_object_inputs(self):
        with self.assertRaises(StoragePrecisionError):
            transform_vertices([(0, 0.1, 0)], IDENTITY, IDENTITY, 1, source_precision="float32_storage")
        matrix = copy.deepcopy(IDENTITY)
        matrix[0][3] = 0.1
        with self.assertRaises(StoragePrecisionError):
            transform_vertices([(0, 1, 0)], matrix, IDENTITY, 1, source_precision="float32_storage")

    def test_prelaunch_hash_mismatch_and_malformed_digest_rejected(self):
        observed = hashlib.sha256(b"replacement bytes").hexdigest()
        validate_prelaunch_hash(observed.upper(), observed)
        for expected in (hashlib.sha256(b"original loaded bytes").hexdigest(), "bad", "z" * 64):
            with self.assertRaises(ValueError):
                validate_prelaunch_hash(expected, observed)

    def test_disabled_or_frozen_deformation_policy_rejects_whole_batch(self):
        body = dict(name="Body", type="MESH", mode="OBJECT", in_view_layer=True,
                    hide_viewport=False, hidden_in_view_layer=False, pose_position=None,
                    shape_keys_muted=[], show_only_shape_key=False,
                    modifiers=[dict(name="Deform", type="ARMATURE", show_viewport=True,
                                    show_render=True, armature_target="Rig")])
        rig = dict(name="Rig", type="ARMATURE", mode="POSE", in_view_layer=True,
                   hide_viewport=False, hidden_in_view_layer=False, pose_position="POSE",
                   shape_keys_muted=[], show_only_shape_key=False, modifiers=[])
        validate_evaluation_policy([body, rig], "Rig")
        for index in (0, 1):
            for field, value in (("hide_viewport", True), ("hidden_in_view_layer", True),
                                 ("in_view_layer", False)):
                states = copy.deepcopy([body, rig])
                states[index][field] = value
                with self.subTest(object=index, field=field), self.assertRaises(ValueError):
                    validate_evaluation_policy(states, "Rig")
        for field, value in (("shape_keys_muted", ["Bend"]), ("show_only_shape_key", True),
                             ("mode", "EDIT")):
            states = copy.deepcopy([body, rig])
            states[0][field] = value
            with self.assertRaises(ValueError):
                validate_evaluation_policy(states, "Rig")
        states = copy.deepcopy([body, rig])
        states[1]["pose_position"] = "REST"
        with self.assertRaises(ValueError):
            validate_evaluation_policy(states, "Rig")
        for field, value in (("show_viewport", False), ("armature_target", None),
                             ("armature_target", "UncontractedRig")):
            states = copy.deepcopy([body, rig])
            states[0]["modifiers"][0][field] = value
            with self.assertRaises(ValueError):
                validate_evaluation_policy(states, "Rig")

    def test_invalid_units_and_vertices_rejected(self):
        for units in (0, -1, math.nan, math.inf, True, "100"):
            with self.subTest(units=units):
                with self.assertRaises(ValueError):
                    validate_units(units)
        for vertices in ([(0, 0)], [(0, 0, math.nan)], [(0, 0, math.inf)]):
            with self.assertRaises(ValueError):
                transform_vertices(vertices, IDENTITY, IDENTITY, 1, source_precision="exact_supplied")
        singular = copy.deepcopy(IDENTITY)
        singular[2][2] = 0
        with self.assertRaises(ValueError):
            transform_vertices([(0, 0, 0)], singular, IDENTITY, 1, source_precision="exact_supplied")

    def test_output_collision_and_asset_tree_writes_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            asset = root / "asset"
            asset.mkdir()
            blend = asset / "source.blend"
            blend.write_bytes(b"fixture path only; not a Blender file")
            fresh = root / "receipt.json"
            self.assertEqual(validate_output_path(fresh, asset, blend), fresh.resolve())
            fresh.write_text("prior evidence", encoding="utf-8")
            with self.assertRaises(ValueError):
                validate_output_path(fresh, asset, blend)
            self.assertEqual(fresh.read_text(encoding="utf-8"), "prior evidence")
            with self.assertRaises(ValueError):
                validate_output_path(asset / "bad.json", asset, blend)
            with self.assertRaises(ValueError):
                validate_output_path(root / "missing" / "out.json", asset, blend)
            with self.assertRaises(ValueError):
                validate_output_path(root / "out.json", root / "different", blend)

    def test_invalid_cli_request_writes_nonpassing_receipt_without_loading_blender(self):
        # Invalid dimensions are rejected before collect()/bpy import. No Blender mock.
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            asset = root / "asset"
            asset.mkdir()
            blend = asset / "fixture.blend"
            blend.write_bytes(b"not a Blender file; only input validation is being exercised")
            head = root / "head.json"
            head.write_text(json.dumps(IDENTITY), encoding="utf-8")
            output = root / "result.json"
            arguments = ["--source-blend", str(blend), "--asset-root", str(asset), "--armature", "Rig",
                         "--source-sha256", hashlib.sha256(blend.read_bytes()).hexdigest(),
                         "--action", "Action", "--frame", "1", "--expected-mesh-count", "1",
                         "--mesh-name", "Body", "--head-frame-json", str(head), "--units-per-meter", "100",
                         "--radius", "nan", "--depth", "1", "--output", str(output)]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(main(arguments), 1)
            receipt = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(receipt["status"], "invalid")
            self.assertFalse(receipt["extraction_completed"])
            self.assertFalse(receipt["model_accepted"])
            self.assertFalse(receipt["loaded_source_identity_proven"])
            self.assertEqual(receipt["request"]["radius"], "nan")
            self.assertEqual(len(receipt["inputs"]["source_blend"]["sha256"]), 64)


if __name__ == "__main__":
    unittest.main()
