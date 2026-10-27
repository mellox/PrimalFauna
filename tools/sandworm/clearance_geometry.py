"""Exact surface clearance of a straight cylindrical region, not a passage/fit test.

Supply ALL evaluated mesh vertices in a common, orthonormal HEAD frame, with X
the bore axis and Y/Z radial, using the same units as radius/depth. Transform,
deformation, triangle selection, export identity and units are caller obligations.
This module does not inspect Blender or infer whether its inputs were evaluated.

Triangles are clipped to the CLOSED axial slab [axial_start, axial_start+depth].
The radial minimum includes triangle interiors, edges and slab intersections.
Fraction arithmetic makes predicates exact for the supplied binary floats; only
reported distances/locations are rounded. No sampling or geometric epsilon is
used in clipping. Contact, including at slab end planes, fails conservatively.

An explicit tolerance adds a NON-PASSING contact band around the requested radius:
intrusion below radius-tolerance, contact through radius+tolerance, clear above.
Even a clear result proves only absence of supplied surfaces inside this region;
it does NOT prove a connected hollow passage, correct surface population, miner
fit, bent/swept clearance, animation continuity, or in-game collision behavior.
"""

from dataclasses import dataclass
from fractions import Fraction
import math
import numbers
import operator


@dataclass(frozen=True)
class ClearanceResult:
    status: str
    reason: str
    surface_clear: bool = False
    supplied_geometry_evaluated: bool = False
    passage_topology_proven: bool = False
    target_fit_proven: bool = False
    tested_triangles: int = 0
    slab_intersections: int = 0
    intrusion_triangles: int = 0
    contact_triangles: int = 0
    min_radius: float | None = None
    closest_head_point: tuple | None = None
    closest_triangle_index: int | None = None
    required_radius: float | None = None
    axial_start: float | None = None
    depth: float | None = None
    tolerance: float | None = None


def _number(value):
    if isinstance(value, bool) or not isinstance(value, numbers.Real):
        raise ValueError("coordinates and dimensions must be real numbers")
    value = float(value)
    if not math.isfinite(value):
        raise ValueError("coordinates and dimensions must be finite")
    return Fraction(value)


def _lerp(a, b, t):
    return tuple(x + t * (y - x) for x, y in zip(a, b))


def _clip(poly, limit, keep_above):
    """Sutherland-Hodgman half-space clipping; preserves convex vertex order."""
    if not poly:
        return []
    out = []
    previous = poly[-1]
    previous_in = previous[0] >= limit if keep_above else previous[0] <= limit
    for current in poly:
        current_in = current[0] >= limit if keep_above else current[0] <= limit
        if current_in != previous_in:
            t = (limit - previous[0]) / (current[0] - previous[0])
            out.append(_lerp(previous, current, t))
        if current_in:
            out.append(current)
        previous, previous_in = current, current_in
    return out


def _radial_minimum(poly):
    """Minimize y*y+z*z on a convex polygon, returning a point ON the polygon."""
    # If the origin lies in a projected fan triangle, its radial minimum is 0.
    # The barycentric weights also recover an axial coordinate on the surface.
    a = poly[0]
    for i in range(1, len(poly) - 1):
        b, c = poly[i], poly[i + 1]
        uy, uz = b[1] - a[1], b[2] - a[2]
        vy, vz = c[1] - a[1], c[2] - a[2]
        det = uy * vz - uz * vy
        if det:
            s = (-a[1] * vz + a[2] * vy) / det
            t = (-uy * a[2] + uz * a[1]) / det
            if s >= 0 and t >= 0 and s + t <= 1:
                return Fraction(0), tuple(a[j] + s * (b[j] - a[j])
                                          + t * (c[j] - a[j]) for j in range(3))
    # Otherwise the projected minimum is on an edge. This also covers projections
    # degenerating to a line/point, and slab contact producing a segment/point.
    best = None
    closest = None
    for i, a in enumerate(poly):
        b = poly[(i + 1) % len(poly)]
        dy, dz = b[1] - a[1], b[2] - a[2]
        denom = dy * dy + dz * dz
        t = max(Fraction(0), min(Fraction(1), -(a[1] * dy + a[2] * dz) / denom)) if denom else 0
        point = _lerp(a, b, t)
        radius_squared = point[1] ** 2 + point[2] ** 2
        if best is None or radius_squared < best:
            best, closest = radius_squared, point
    return best, closest


def _nondegenerate(a, b, c):
    u = tuple(y - x for x, y in zip(a, b))
    v = tuple(y - x for x, y in zip(a, c))
    return any(u[i] * v[j] != u[j] * v[i] for i, j in ((0, 1), (0, 2), (1, 2)))


def check_surface_clearance(vertices, triangles, *, radius, depth,
                            axial_start=0.0, tolerance=0.0):
    """Return a fail-closed result for indexed triangles in caller's head frame.

    vertices: iterable of finite XYZ triples; triangles: iterable of index triples.
    radius/depth must be positive. 0 <= tolerance < radius. Degenerate triangles,
    malformed indices (including negative indices), nonfinite/invalid inputs and
    empty slab coverage cannot pass. All triangles are validated, even outside
    the slab or after an intrusion, so an invalid tail never yields a partial pass.
    Counts refer to triangles, not connected defects. Locations are in input units.

    supplied_geometry_evaluated means this calculation completed on ALL supplied
    triangles, not that an external application's modifier stack was evaluated.
    A caller must separately establish complete/appropriate surface coverage.
    """
    tested = intersected = intrusions = contacts = 0
    metadata = {}
    try:
        r, d, start, tol = map(_number, (radius, depth, axial_start, tolerance))
        if r <= 0 or d <= 0 or tol < 0 or tol >= r:
            raise ValueError("require radius/depth > 0 and 0 <= tolerance < radius")
        metadata = dict(required_radius=float(r), depth=float(d),
                        axial_start=float(start), tolerance=float(tol))
        end = start + d
        low_squared, high_squared = (r - tol) ** 2, (r + tol) ** 2
        verts = []
        for vertex in vertices:
            point = tuple(_number(v) for v in vertex)
            if len(point) != 3:
                raise ValueError("each vertex must have exactly three coordinates")
            verts.append(point)
        minimum = closest = closest_index = None
        for index, triangle in enumerate(triangles):
            indices = tuple(triangle)
            if len(indices) != 3 or any(isinstance(i, bool) or not isinstance(i, numbers.Integral)
                                        for i in indices):
                raise ValueError("each triangle must have three integer indices")
            indices = tuple(operator.index(i) for i in indices)
            if any(i < 0 or i >= len(verts) for i in indices):
                raise ValueError("triangle index outside vertex array")
            poly = [verts[i] for i in indices]
            if not _nondegenerate(*poly):
                raise ValueError("degenerate triangle")
            tested += 1
            poly = _clip(_clip(poly, start, True), end, False)
            if not poly:
                continue
            intersected += 1
            value, point = _radial_minimum(poly)
            if minimum is None or value < minimum:
                minimum, closest, closest_index = value, point, index
            if value < low_squared:
                intrusions += 1
            elif value <= high_squared:
                contacts += 1
        counts = dict(tested_triangles=tested, slab_intersections=intersected,
                      intrusion_triangles=intrusions, contact_triangles=contacts)
        if not intersected:
            return ClearanceResult("no_coverage", "no supplied triangle intersects the axial slab",
                                   **counts, **metadata)
        point = tuple(float(v) for v in closest)
        distance = math.hypot(point[1], point[2])
        if not math.isfinite(distance):
            raise ValueError("closest radial distance cannot be represented as a finite float")
        status = "intrusion" if intrusions else "contact" if contacts else "clear"
        reason = {"intrusion": "surface penetrates radius minus tolerance",
                  "contact": "surface touches or lies within the non-passing tolerance band",
                  "clear": "supplied surfaces are outside radius plus tolerance in the slab"}[status]
        return ClearanceResult(status, reason, surface_clear=status == "clear",
                               supplied_geometry_evaluated=True, min_radius=distance,
                               closest_head_point=point, closest_triangle_index=closest_index,
                               **counts, **metadata)
    except (ValueError, TypeError, OverflowError, ArithmeticError) as error:
        return ClearanceResult("invalid", str(error), tested_triangles=tested,
                               slab_intersections=intersected, **metadata)
