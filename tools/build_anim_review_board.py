r"""
build_anim_review_board.py -- assemble every authored clip's contact sheet,
silhouette and validator numbers into ONE self-contained HTML page, so the
animation can be judged without building the mod or launching the game.

WHY: the user's judging loop has been build -> launch -> load -> respawn ->
spawn -> squint at a creature in tall grass at night. A gait reads or fails to
read instantly in a contact strip. This turns an hour into a minute.

INPUT  (a directory, scanned non-recursively then one level deep):
  <clip>_sheet.png        contact sheet: 6-8 evenly spaced frames, side view
  <clip>_silhouette.png   optional: black-fill key pose
  <clip>_pose*.png        optional: any extra stills
  anim_<clip>.json        optional: validate_anim_fbx.py --report output
  rig_manifest.json       optional: validate_rig_fbx.py --manifest output
OUTPUT: <dir>/review-board.html  (images referenced relatively)

USAGE:
  python build_anim_review_board.py <dir> [--title "Sicklejaw animation v2"]
"""
import html
import json
import os
import sys


CATEGORY_ORDER = [
    ("Locomotion", ("idle", "walk", "run", "prowl", "limp", "turn")),
    ("Attacks", ("bite", "lunge", "attack", "claw", "tail", "charge")),
    ("Reactions", ("hurt", "stagger", "death", "hit")),
    ("Display / emotion", ("alert", "aggressive", "threat", "roar", "fear")),
    ("Behaviour", ("sniff", "investigate", "feed", "rest", "sleep", "shake")),
    ("Additive layers", ("add_", "breathe", "headlook", "tailidle")),
]


def categorise(clip_name):
    lowered = clip_name.lower()
    for label, patterns in CATEGORY_ORDER:
        if any(p in lowered for p in patterns):
            return label
    return "Other"


def collect(directory):
    """clip -> {sheet, silhouette, stills[], report}"""
    clips = {}

    def touch(name):
        return clips.setdefault(name, {"sheet": None, "silhouette": None, "stills": [], "report": None})

    for root, _dirs, files in os.walk(directory):
        # one level deep only
        depth = os.path.relpath(root, directory).count(os.sep)
        if os.path.relpath(root, directory) != "." and depth >= 1:
            continue
        for filename in sorted(files):
            path = os.path.relpath(os.path.join(root, filename), directory).replace("\\", "/")
            lowered = filename.lower()
            # Two naming conventions are accepted: <clip>_sheet.png and the
            # authoring-side contactsheet_<clip>.png / silhouette_<clip>.png.
            if lowered.startswith("contactsheet_") and lowered.endswith(".png"):
                touch(filename[len("contactsheet_"):-len(".png")])["sheet"] = path
            elif lowered.startswith("silhouette_") and lowered.endswith(".png"):
                touch(filename[len("silhouette_"):-len(".png")])["silhouette"] = path
            elif lowered.startswith("footlock_") and lowered.endswith(".png"):
                touch(filename[len("footlock_"):-len(".png")])["stills"].append(path)
            elif lowered.endswith("_sheet.png"):
                touch(filename[: -len("_sheet.png")])["sheet"] = path
            elif lowered.endswith("_silhouette.png"):
                touch(filename[: -len("_silhouette.png")])["silhouette"] = path
            elif lowered.endswith(".png") and "_pose" in lowered:
                touch(filename.split("_pose")[0])["stills"].append(path)
            elif lowered.startswith("anim_") and lowered.endswith(".json"):
                clip = filename[len("anim_"):-len(".json")]
                try:
                    with open(os.path.join(root, filename), "r", encoding="utf-8") as handle:
                        touch(clip)["report"] = json.load(handle)
                except Exception as error:
                    print("WARN: could not read %s (%s)" % (filename, error))
    return clips


def metric_row(report):
    if not report:
        return '<p class="nometrics">No validator report for this clip &mdash; it was not gated.</p>'
    failures = report.get("failures") or []
    warnings = report.get("warnings") or []
    verdict = "REJECT" if failures else ("PASS" if not warnings else "PASS (warnings)")
    verdict_class = "reject" if failures else ("warn" if warnings else "pass")
    bits = [
        ("frames", report.get("frames")),
        ("fps", report.get("fps")),
        ("duration", "%.2fs" % report["duration_s"] if report.get("duration_s") is not None else None),
        ("foot travel L/R", "%.3f / %.3f m" % (
            report.get("foot_travel_m", {}).get("L", 0.0),
            report.get("foot_travel_m", {}).get("R", 0.0))
            if report.get("foot_travel_m") else None),
        ("antiphase", report.get("phase_offset_half_cycles")),
        ("lowest foot Z", "%.3f m" % report["lowest_foot_z_m"] if report.get("lowest_foot_z_m") is not None else None),
        ("implied ground speed", "%.0f cm/s" % report["implied_ground_speed_cms"]
            if report.get("implied_ground_speed_cms") else None),
    ]
    cells = "".join(
        '<span class="metric"><b>%s</b> %s</span>' % (html.escape(str(k)), html.escape(str(v)))
        for k, v in bits if v not in (None, "")
    )
    notes = ""
    if failures:
        notes += "".join('<li class="f">%s</li>' % html.escape(str(f)) for f in failures)
    if warnings:
        notes += "".join('<li class="w">%s</li>' % html.escape(str(w)) for w in warnings)
    notes_html = '<ul class="notes">%s</ul>' % notes if notes else ""
    return ('<div class="metrics"><span class="verdict %s">%s</span>%s</div>%s'
            % (verdict_class, verdict, cells, notes_html))


def build(directory, title):
    clips = collect(directory)
    if not clips:
        print("No clip assets found in %s" % directory)
        return None

    grouped = {}
    for name, data in clips.items():
        grouped.setdefault(categorise(name), []).append((name, data))

    rig_manifest = None
    rig_path = os.path.join(directory, "rig_manifest.json")
    if os.path.isfile(rig_path):
        try:
            with open(rig_path, "r", encoding="utf-8") as handle:
                rig_manifest = json.load(handle)
        except Exception:
            pass

    parts = ["""<!doctype html><meta charset="utf-8">
<title>%s</title>
<style>
 :root{color-scheme:dark}
 body{background:#12141a;color:#e7e9ee;font:15px/1.55 system-ui,Segoe UI,sans-serif;margin:0;padding:32px}
 h1{font-size:26px;margin:0 0 4px}
 .sub{color:#98a0b3;margin:0 0 28px}
 h2{font-size:19px;margin:38px 0 14px;padding-bottom:7px;border-bottom:1px solid #2a2f3d;color:#c9d2e8}
 .clip{background:#191c25;border:1px solid #262b38;border-radius:10px;padding:16px 18px;margin:0 0 20px}
 .clip h3{margin:0 0 10px;font-size:17px;color:#fff}
 .clip img{max-width:100%%;display:block;border-radius:6px;background:#0c0e13;margin:0 0 10px}
 .sil{max-width:340px !important}
 .metrics{display:flex;flex-wrap:wrap;gap:8px 14px;align-items:center;margin:6px 0}
 .metric{font-size:13px;color:#aab3c6}
 .metric b{color:#7f8aa3;font-weight:600;margin-right:4px}
 .verdict{font-size:12px;font-weight:700;letter-spacing:.04em;padding:3px 9px;border-radius:20px}
 .pass{background:#12482c;color:#7ff0ad}.warn{background:#4a3c12;color:#f5d888}.reject{background:#4d1a1f;color:#ff9aa5}
 .notes{margin:6px 0 0;padding-left:18px;font-size:13px}
 .notes .f{color:#ff9aa5}.notes .w{color:#f5d888}
 .nometrics{color:#6d7governed;font-size:13px;color:#6d788f;margin:4px 0 0}
 .rig{background:#151b18;border:1px solid #24382c;border-radius:10px;padding:14px 18px;margin:0 0 26px}
 .rig code{color:#8fd9ab}
 .howto{background:#161a24;border-left:3px solid #3d5a9e;padding:12px 16px;margin:0 0 26px;color:#b9c2d6;font-size:14px}
</style>
<h1>%s</h1>
<p class="sub">Judge from the strips &mdash; no build, no launch, no game session needed.</p>
<div class="howto"><b>How to read a contact sheet:</b> the frames run left&nbsp;&rarr;&nbsp;right through one cycle.
A believable gait shows the two legs <i>alternating</i>, the planted foot staying put under a body that carries
forward, the tail counter-swinging, and the head holding steadier than the hips. If every frame looks like the
same pose with a leg at a different height, the cycle is wrong.</div>
""" % (html.escape(title), html.escape(title))]

    if rig_manifest:
        pairs = len(rig_manifest.get("mirrored_pairs", []))
        parts.append(
            '<div class="rig"><b>Rig</b> &mdash; <code>%s</code> &middot; %s bones &middot; root <code>%s</code> '
            '&middot; %d mirrored L/R pairs &middot; height %.2f m</div>'
            % (html.escape(str(rig_manifest.get("armature", "?"))),
               html.escape(str(rig_manifest.get("bone_count", "?"))),
               html.escape(str(rig_manifest.get("root", "?"))),
               pairs, float(rig_manifest.get("height_m") or 0.0)))

    for label, _patterns in CATEGORY_ORDER + [("Other", ())]:
        if label not in grouped:
            continue
        parts.append("<h2>%s</h2>" % html.escape(label))
        for name, data in sorted(grouped[label]):
            parts.append('<div class="clip"><h3>%s</h3>' % html.escape(name))
            if data["sheet"]:
                parts.append('<img src="%s" alt="%s contact sheet">' % (html.escape(data["sheet"]), html.escape(name)))
            for still in data["stills"]:
                parts.append('<img src="%s" alt="">' % html.escape(still))
            if data["silhouette"]:
                parts.append('<img class="sil" src="%s" alt="%s silhouette">'
                             % (html.escape(data["silhouette"]), html.escape(name)))
            parts.append(metric_row(data["report"]))
            parts.append("</div>")

    out_path = os.path.join(directory, "review-board.html")
    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(parts))
    print("Review board written: %s  (%d clips)" % (out_path, len(clips)))
    return out_path


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    target = sys.argv[1]
    board_title = "PrimalFauna animation review"
    if "--title" in sys.argv:
        board_title = sys.argv[sys.argv.index("--title") + 1]
    build(target, board_title)
