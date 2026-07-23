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
