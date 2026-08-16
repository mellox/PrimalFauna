# PrimalFauna — Asset Ledger

Provenance + license record for every art asset. Created by the `mod-artist` agent.
Purchased/licensed sources are **never** committed to this repo — they live in a local
`_assets-licensed/` folder outside git (see `docs/05-asset-pipeline.md` §5). This file records
where each asset came from, its license, and any attribution owed.

Working sources live under `art/wip/`; review renders under `art/review/`.

| Date | Asset | Type | Source / Tool | License | Credits | Notes |
|---|---|---|---|---|---|---|
| 2026-07-23 | `SK_PFA_Placeholder.fbx` | Skeletal mesh (BLOCKOUT) | Scripted in Blender 5.2 (bpy) by mod-artist | Original (ours) | 0 | **Labeled placeholder — never ships.** Capsule body + head + snout, root/spine/head bone chain, ~1.8m tall. For creature-systems import/montage/locomotion testing until real art lands. |
| 2026-07-23 | `A_PFA_Placeholder_Bite.fbx` | Animation (BLOCKOUT) | Scripted in Blender 5.2 (bpy) | Original (ours) | 0 | Short forward head-snap + body lunge (frames 1–5). Placeholder attack take. |
| 2026-07-23 | `A_PFA_Placeholder_WalkBob.fbx` | Animation (BLOCKOUT) | Scripted in Blender 5.2 (bpy) | Original (ours) | 0 | 24-frame looping locomotion bob (root bob + spine/head counter-motion). Placeholder walk take. |
| 2026-07-23 | Sicklejaw candidate D ("alien") mesh + PBR | Skeletal-mesh base (KEEPER) | Tripo text-to-model `v3.1-20260211`, task `7174a7d7` | Tripo-generated (our account, commercial ok) | 100 (4 candidates) | `art/wip/sicklejaw/glb/7174a7d7…_pbr.fbx`. ~38k tris, quad, single UV, Color/Normal/Rough/Metal PBR incl. teal bio-markings. Candidates A/B/C also generated; A kept as fallback. |
| 2026-07-23 | Sicklejaw D rig (tripo-spec) | Rig FBX (KEEPER) | Tripo `animate_rig` spec=tripo, task `377f815f` | Tripo-generated | 25 (+25 unused mixamo rig) | 54-bone biped skeleton (Root/Hip/Pelvis/limbs/tail). `art/wip/sicklejaw/rigs/377f815f…_model.fbx`. Mixamo-spec rig `46371dc2` also exists but presets fail on it — kept only as A's fallback shell. |
| 2026-07-23 | Sicklejaw D anims ×5 | Animation FBX (placeholder-grade) | Tripo `animate_retarget` presets on `377f815f` | Tripo-generated | 50 (idle/walk/run/slash/hurt @10) | `art/wip/sicklejaw/anims/<name>/…_model.fbx`. Humanoid-styled — usable for bring-up, need raptor-motion refinement (rung 2/4). |
| 2026-07-23 | Sicklejaw review renders | Turntable + telegraph + silhouette + anim proofs | Blender EEVEE, emissive mask Blender-derived | Ours | 0 | `art/review/2026-07-11-sicklejaw/` (+ `review-board.html`). Textured base = Tripo native; ArmorPaint/StableProjectorz stripe pass deferred (GPU-contention + GUI-only). |

## W2.1b — Sicklejaw QUALITY REBUILD (2026-07-24)

Adopted candidate D was rebuilt clean (neutral A-pose + closed mouth → AutoRemesher retopo per
doc 05 §2 → Tripo v3 re-rig biped/`spec=tripo` → 5-preset re-retarget → look re-baked/graded).
Working sources in `art/wip/sicklejaw-rebuild/`; review board `art/review/2026-07-24-sicklejaw-rebuild/`.

| Date | Asset | Type | Source / Tool | License | Credits | Notes |
|---|---|---|---|---|---|---|
| 2026-07-24 | Sicklejaw rebuild candidates (C1–C3, C3b) | Base-mesh candidates | Tripo v3 `image-to-model` ×2 + `text-to-model` ×2 (`v3.1-20260211`) | Tripo-generated (our account, commercial ok) | 140 (40+40+30+30) | `art/wip/sicklejaw-rebuild/candidates/`. C1/C2 image-to-model copied D's rearing/open-mouth pose (cut); **C3b text D-tuned = KEEPER** (muted grey-blue, subtle teal, low scutes, lean, closed mouth). C3 kept as runner-up. |
| 2026-07-24 | `keeper_retopo.glb` / `SK_Sicklejaw` mesh | Skeletal-mesh base (KEEPER) | AutoRemesher 1.0.0 retopo of C3b | Ours (derived) | 0 | 1,424,686 tris → **20,520 quads + 590 tris (97.2% quad) = 42.6k tris**. Single non-overlapping UV (smart-project). Real-meters 1.70 m, transforms applied. |
| 2026-07-24 | Sicklejaw rebuild rig | Rig FBX (KEEPER) | Tripo v3 `/models/import` → `/animations/rig` (`v2.5-20260210`, `rig_type=biped`, `spec=tripo`) | Tripo-generated | 25 (+0 rig-check, +0 import) | `art/wip/sicklejaw-rebuild/rigs/139fa2ac…_rig.fbx`. **48-bone creature rig incl. 6-bone tail chain** (the original mixamo rig had none) + 8-bone head. rig-check: riggable=true, biped. |
| 2026-07-24 | Sicklejaw rebuild anims ×5 | Animation FBX (placeholder-grade) | Tripo v3 `/animations/retarget` presets | Tripo-generated | 50 (idle/walk/run/slash/hurt @10) | `art/wip/sicklejaw-rebuild/anims/<name>/`. Humanoid-styled — bring-up only, need raptor-motion refinement (Blender hand-author on the tail rig, or Kubold pack). |
| 2026-07-24 | `SK_Sicklejaw` textures | BaseColor + Normal + Emissive (2048) | Blender bake/grade from C3b + numpy teal emissive mask | Ours | 0 | `art/wip/sicklejaw-rebuild/tex/`. Look preserved on the retopo UVs; emissive mask = teal markings for the aggro telegraph (scalar `MID` in UE). |
| 2026-07-24 | `SK_Sicklejaw.fbx` / `.glb` | Final textured+rigged skeletal mesh (DELIVERABLE) | Blender assembly | Ours (Tripo-derived, commercial ok) | 0 | `art/wip/sicklejaw-rebuild/final/`. Mesh + 48-bone rig + BaseColor/Normal/Emissive. **W2.2 import target once the rebuild board is user-approved.** |
| 2026-07-24 | Sicklejaw rebuild review board | Turntable ×8 + likeness-vs-D + silhouette + aggro + anim proofs | Blender EEVEE, fixed rig; numpy bloom for aggro | Ours | 0 | `art/review/2026-07-24-sicklejaw-rebuild/review-board.html`. **Rebuild total: 215 API credits (pool 8375→8160), $0 purchases.** |
| 2026-07-25 | `T_Sicklejaw_BaseColor.png` | Texture 2048 (DELIVERABLE) | Authored in Blender/numpy from anatomical masks baked off the mesh (`art/wip/sicklejaw-d-v2/scripts/310`+`320`) | Ours (Tripo-derived, commercial ok) | 0 | Countershading down the real body axis + limb/claw/tooth/mouth zones + dark inlay under the glow marks. Generator's high-frequency painted luminance kept, its chroma confetti discarded. **sRGB ON, TC_Default.** Existing UVs — no mesh re-import. |
| 2026-07-25 | `T_Sicklejaw_Roughness.png` | Texture 2048 (DELIVERABLE) | Same pipeline | Ours | 0 | Nine anatomical zones, 0.235 (teeth) to 0.870 (creases). Replaces the generator map, which was a recolour of the albedo (Pearson r 0.627 vs albedo luminance). **sRGB OFF, TC_Masks.** |
| 2026-07-25 | `T_Sicklejaw_Normal.png` | Texture 2048 (DELIVERABLE) | Same pipeline | Ours | 0 | Original bake + world-space scale/scute/pebble detail (continuous across UV seams by construction). XY std 0.034 → 0.045. A/B-verified: gain at 1.6 m, no cost at 10 m. **sRGB OFF, TC_Normalmap.** |
| 2026-07-25 | `T_Sicklejaw_Emissive.png` | Texture 2048 (DELIVERABLE, NEW) | Same pipeline | Ours | 0 | **The aggro telegraph.** R = core marks (lit when calm), G = flare marks (recruited by arousal), B = along-body position so the glow ignites at the head and runs to the tail. Marks placed on the measured peaks of the generator's own teal. **sRGB OFF, TC_Masks.** |
| 2026-07-25 | `M_Sicklejaw` / `MI_Sicklejaw` | UE material + instance | `tools/author_sicklejaw_material_ue.py` (headless) | Ours | 0 | Replaces the auto-generated `tripo_mat_7174a7d7`, which had no named parameters and therefore could not be driven from C++. Runtime parameter: **`AggroGlow` scalar 0–1**. Metallic is a constant 0. |
| 2026-07-25 | Sicklejaw surface review board | BEFORE/AFTER at 5/10/20 m, four lighting rigs, close-ups, aggro ladder day/night/cave, normal A/B | Blender 5.2 EEVEE Next, AgX, fixed game-like rigs | Ours | 0 | `art/wip/sicklejaw-d-v2/renders/w32-surface-board.html`. **w32 surface pass total: 0 credits (API pool untouched), $0 purchases.** |
| 2026-08-15 | `SM_PFA_SicklejawRemains` + icons | Static pickup/conveyor trophy mesh, textures, icons, Blender source, and FBX | Blender extraction/retopo from the approved Sicklejaw D v2 mesh; exact-file render + independent art review; `tools/import_sicklejaw_remains_ue.py` | Ours (Tripo-derived from our commercial-account generation; no new source/attribution) | 0 | `art/wip/sicklejaw-remains/`. Compact head/jaw trophy: 4,006 tris, 30.13 × 34.21 × 26.41 cm, 18-segment posterior rim, closed/manifold FBX roundtrip. UE uses two `MM_FactoryBaked` instances (hide + authored tissue cut), transparent 1024/256 icons, and a build-time cooked-asset gate. |

## Placeholder rig — technical notes (for the systems lane)

- **Files:** `art/wip/placeholder/SK_PFA_Placeholder.fbx` (mesh + skeleton, bind pose) plus two
  armature-only anim takes in the same folder.
- **Skeleton:** 3-bone chain `root` → `spine` → `head` (connected). `root` sits at the mesh origin
  (0,0,0) = ground/feet contact; mesh extends z 0.15–1.80 m. Vertex groups exist for all 3 bones
  (Blender automatic weights).
- **Scale:** verified by FBX round-trip — re-imported mesh measures 0.64 × 0.56 × 1.65 m
  (top of head at 1.80 m). **Confirm ~180 cm on UE import**; a sub-cm arrival = the
  `bake_space_transform` shrink bug (it is OFF here, so this should be clean).
- **Export settings used:** `apply_scale_options='FBX_SCALE_ALL'`, `bake_space_transform=False`,
  `axis_forward='X'`, `axis_up='Z'`, `add_leaf_bones=False`, `use_armature_deform_only=True`.
  Creature faces **+X (forward)**; snout points +X.
- **Anim takes** are armature-only FBX (one take each) targeting the same bone hierarchy — import
  "as animation" against the SK's skeleton; bone names match by hierarchy.
- Previews: `preview_BLOCKOUT_bindpose.png`, `preview_BLOCKOUT_bite_peak.png` (blockout, not art review).
