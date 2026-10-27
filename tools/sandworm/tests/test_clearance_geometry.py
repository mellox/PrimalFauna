"""Analytic controls for surface clearance; no Blender, external packages or assets."""

import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from clearance_geometry import check_surface_clearance


def one_triangle(vertices, **kwargs):
    return check_surface_clearance(vertices, [(0, 1, 2)], **kwargs)


class ClearanceTests(unittest.TestCase):
    def test_edge_intrusion_missed_by_vertices_midpoints_and_centroid(self):
        # y=12.9 segment runs z=-2..6; closest point is at t=1/4, NOT midpoint.
        # All old samples stay outside radius 13. Vertex minimum sqrt(170.41)>13.
        verts = [(0.2, 12.9, -2), (0.8, 12.9, 6), (0.5, 20, 6)]
        samples = verts + [tuple((verts[i][k] + verts[j][k]) / 2 for k in range(3))
                           for i, j in ((0, 1), (1, 2), (2, 0))]
        samples.append(tuple(sum(v[k] for v in verts) / 3 for k in range(3)))
        self.assertGreater(min(math.hypot(p[1], p[2]) for p in samples), 13)
        result = one_triangle(verts, radius=13, depth=1)
        self.assertEqual(result.status, "intrusion")
        self.assertAlmostEqual(result.min_radius, 12.9)
        self.assertAlmostEqual(result.closest_head_point[0], 0.35)
        self.assertEqual(result.closest_head_point[2], 0)
        self.assertEqual(result.intrusion_triangles, 1)

    def test_axis_pierces_triangle_interior(self):
        verts = [(0.5, -3, -2), (0.5, 3, -2), (0.5, 0, 4)]
        result = one_triangle(verts, radius=0.1, depth=1)
        self.assertEqual(result.status, "intrusion")
        self.assertEqual(result.min_radius, 0)
        self.assertEqual(result.closest_head_point, (0.5, 0, 0))

    def test_thin_slab_crossing_with_no_old_samples_in_slab(self):
        # Slab [0.25,0.26] crosses the long triangle although every old x sample
        # belongs to {-1, 0, 1/3, 1}. A tiny slab is not permission to omit it.
        result = one_triangle([(-1, 0.5, 0), (1, 0.5, 0), (1, 0.5, 1)],
                              radius=1, axial_start=0.25, depth=0.01)
        self.assertEqual(result.status, "intrusion")
        self.assertEqual(result.slab_intersections, 1)
        self.assertEqual(result.min_radius, 0.5)
        self.assertGreaterEqual(result.closest_head_point[0], 0.25)
        self.assertLessEqual(result.closest_head_point[0], 0.26)

    def test_clipping_removes_intrusion_outside_slab(self):
        # Radius reaches zero only at x=-1. In x=[0,1], y >= 2.
        result = one_triangle([(-1, 0, 0), (1, 4, 0), (1, 4, 2)], radius=1, depth=1)
        self.assertEqual(result.status, "clear")
        self.assertEqual(result.min_radius, 2)

    def test_square_tube_is_clear_but_topology_and_fit_unproven(self):
        # Eight triangulated walls at y/z=+/-2, extending past the requested slab.
        ring = [(-2, -2), (2, -2), (2, 2), (-2, 2)]
        verts = [(x, y, z) for x in (-1, 3) for y, z in ring]
        tris = []
        for i in range(4):
            j = (i + 1) % 4
            tris.extend([(i, j, j + 4), (i, j + 4, i + 4)])
        result = check_surface_clearance(verts, tris, radius=1, depth=2)
        self.assertTrue(result.surface_clear)
        self.assertTrue(result.supplied_geometry_evaluated)
        self.assertEqual(result.min_radius, 2)
        self.assertEqual(result.tested_triangles, 8)
        self.assertEqual(result.slab_intersections, 8)
        self.assertFalse(result.passage_topology_proven)
        self.assertFalse(result.target_fit_proven)

    def test_tangency_and_tolerance_are_nonpassing(self):
        for y, expected in ((0.98, "intrusion"), (0.995, "contact"),
                            (1, "contact"), (1.005, "contact"), (1.02, "clear")):
            with self.subTest(y=y):
                result = one_triangle([(0, y, -1), (1, y, -1), (0, y, 1)],
                                      radius=1, depth=1, tolerance=0.01)
                self.assertEqual(result.status, expected)
                self.assertEqual(result.surface_clear, expected == "clear")
        exact = one_triangle([(0, 1, -1), (1, 1, -1), (0, 1, 1)], radius=1, depth=1)
        self.assertEqual(exact.status, "contact")

    def test_one_float_step_each_side_of_radius_is_not_rounded_to_contact(self):
        for y, status in ((math.nextafter(1.0, 0.0), "intrusion"),
                          (math.nextafter(1.0, math.inf), "clear")):
            result = one_triangle([(0, y, -1), (1, y, -1), (0, y, 1)], radius=1, depth=1)
            self.assertEqual(result.status, status)

    def test_closed_solid_can_have_clear_surfaces_without_proving_passage(self):
        # Region is inside a CLOSED solid box: no boundary crosses its bore, but
        # surface clearance alone cannot certify inside/outside or empty volume.
        verts = [(x, y, z) for x in (-2, 2) for y in (-2, 2) for z in (-2, 2)]
        quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1),
                 (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
        tris = [tri for a, b, c, d in quads for tri in ((a, b, c), (a, c, d))]
        result = check_surface_clearance(verts, tris, radius=1, depth=1)
        self.assertEqual(result.status, "clear")
        self.assertFalse(result.passage_topology_proven)
        self.assertFalse(result.target_fit_proven)

    def test_sliver_slab_has_no_epsilon_that_drops_coverage(self):
        result = one_triangle([(-1, 0.5, 0), (1, 0.5, 0), (1, 0.5, 1)],
                              radius=1, depth=1e-300)
        self.assertEqual(result.status, "intrusion")
        self.assertEqual(result.min_radius, 0.5)

    def test_triangle_touches_only_slab_end_point(self):
        result = one_triangle([(-1, 2, 1), (-1, 2, -1), (0, 0.5, 0)], radius=1, depth=1)
        self.assertEqual(result.status, "intrusion")
        self.assertEqual(result.closest_head_point, (0, 0.5, 0))

    def test_triangle_touches_only_slab_end_segment(self):
        result = one_triangle([(-1, 2, 0), (0, 0.5, -1), (0, 0.5, 1)], radius=1, depth=1)
        self.assertEqual(result.status, "intrusion")
        self.assertEqual(result.min_radius, 0.5)

    def test_orientation_and_triangle_order_do_not_change_clearance(self):
        verts = [(0.2, 12.9, -2), (0.8, 12.9, 6), (0.5, 20, 6)]
        for indices in ((0, 1, 2), (1, 2, 0), (2, 1, 0)):
            with self.subTest(indices=indices):
                result = check_surface_clearance(verts, [indices], radius=13, depth=1)
                self.assertEqual(result.status, "intrusion")
                self.assertAlmostEqual(result.min_radius, 12.9)

    def test_empty_and_out_of_slab_fail_closed(self):
        for verts, tris in (([], []), ([(0, 0, 0)], []),
                            ([(2, 2, 0), (3, 2, 0), (3, 2, 1)], [(0, 1, 2)])):
            result = check_surface_clearance(verts, tris, radius=1, depth=1)
            self.assertEqual(result.status, "no_coverage")
            self.assertFalse(result.surface_clear)
            self.assertIsNone(result.min_radius)

    def test_invalid_inputs_fail_closed_even_after_good_triangle(self):
        good = [(0, 2, 0), (1, 2, 0), (1, 2, 1)]
        cases = [(good + [(math.nan, 0, 0)], [(0, 1, 2)]),
                 (good + [(math.inf, 0, 0)], [(0, 1, 2)]),
                 (good, [(0, 1, 2), (-1, 1, 2)]),
                 (good, [(0, 1, 2), (0, 1, 3)]),
                 (good, [(0, 1, 2), (0, 0, 1)]),
                 (good, [(True, 1, 2)]), (good, [(0.0, 1, 2)]),
                 (good, [(0, 1)]), ([(0, 2), *good[1:]], [(0, 1, 2)]),
                 (None, []), (good, None)]
        for verts, tris in cases:
            with self.subTest(vertices=verts, triangles=tris):
                result = check_surface_clearance(verts, tris, radius=1, depth=1)
                self.assertEqual(result.status, "invalid")
                self.assertFalse(result.surface_clear)
                self.assertFalse(result.supplied_geometry_evaluated)
                self.assertIsNone(result.min_radius)

    def test_invalid_dimensions_fail_closed(self):
        for settings in (dict(radius=0, depth=1), dict(radius=1, depth=0),
                         dict(radius=-1, depth=1), dict(radius=1, depth=math.nan),
                         dict(radius=math.inf, depth=1), dict(radius=True, depth=1),
                         dict(radius=1, depth=1, tolerance=-0.01),
                         dict(radius=1, depth=1, tolerance=1),
                         dict(radius=1, depth=1, axial_start=math.inf)):
            with self.subTest(settings=settings):
                result = check_surface_clearance([], [], **settings)
                self.assertEqual(result.status, "invalid")
                self.assertFalse(result.surface_clear)


if __name__ == "__main__":
    unittest.main()
