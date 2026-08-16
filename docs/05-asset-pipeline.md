# 05 — Asset Pipeline

**Executor: every stage of this doc is owned by the shared `mod-artist` agent**
(`C:\Claude\Projects\.claude\agents\mod-artist.md`, opus-tier) — art is never done inline by
implementation-tier agents (doc 08 §1 model table).

Locked D1: **AI pipeline first, marketplace packs as the per-creature fallback.** Toolchain facts
from `Creature-VFX-Pipeline-Tools-Summary.md` + per-tool guides; Tripo capabilities verified
against the official v3 docs 2026-07-11. Nobody has shipped a custom Satisfactory creature before
(doc 08 §risk R1) — the asset chain below is where that battle is won.

## 1. Art direction & generation briefs

Dinosaur silhouettes, alien-planet skin: muted stylized base tones, clean shapes (Coffee Stain
look, NOT photoreal/noisy), bioluminescent accent markings (emissive mask — doubles as the aggro
telegraph, §4). Each species doc-01 entry carries the fantasy; the packet author writes the actual
Tripo/Hunyuan prompt from it (e.g. Sicklejaw: "stylized alien raptor, bipedal theropod, sleek
scaled hide, oversized curved lower jaw, bioluminescent stripes along flanks, game-ready, clean
topology, neutral A-pose, closed mouth" — neutral pose + closed mouth matter for rigging).

Generators: **Tripo** primary (API-automatable, better remesh, integrated rig path), **Hunyuan3D
2.1 local** for free iteration on looks (needs ~29 GB VRAM → `free_gpu` first; never during
builds). Iterate silhouettes cheaply BEFORE spending credits on rig-quality generations.

## 2. Per-creature pipeline (stage gates in order)

| Stage | Tool | Key rules |
|---|---|---|
| Generate | Tripo text/image-to-model (or Hunyuan → Blender) | Multiple candidates; pick by silhouette in a flat-lit turntable render. |
| Clean | Blender via MCP (bpy) | Decimate to working size, UV unwrap (non-overlapping — hard prerequisite for both texture tools), scale to real size in METERS before rig, apply transforms. Batch hygiene: `orphans_purge` per iteration [[blender-ue-batch-memory-hygiene]]. |
| **Retopo (STANDARD as of 2026-07-23)** | AutoRemesher 1.0.0 (`C:\Blender\AutoRemesher\autoremesher.exe`, Blender 5.2 bridge) | Organic creature meshes ONLY (hard-surface shatters): voxel remesh first if messy/multi-shell (5.2 preserves color attrs) → `--target-quads` ≈ **5× the desired final count** (undershoots on smooth shapes; LOWER adaptivity ⇒ fewer faces) → minor pinch cleanup. **ALWAYS before rigging — retopo invalidates any existing rig/anims.** ≥4M-tri fused input crashes it. Gotchas: [[blender-5-2-install]]. |
| Rig-check | `POST /v3/animations/rig-check` (free) | Returns riggable + recommended rig type. **Design intent: Sicklejaw/Duskstalker should classify `biped`** (two legs + tail) — biped rigs unlock Tripo's full preset library (§3). Crestback is honestly `quadruped`. |
| Rig | `POST /v3/animations/rig` (25 credits) | Rig model `v2.5-20260210` supports quadruped/serpentine/avian/etc.; `out_format: fbx`. **Use `spec: tripo`, NOT mixamo** — verified at runtime 2026-07-23: preset retargets SILENTLY FAIL (progress 100, no output, uncharged) on `spec=mixamo` rigs and succeed only on `spec=tripo`. Only rig with mixamo spec if the target workflow is external humanoid retargeting, never for Tripo presets. |
| Animate | §3 decision ladder | The pipeline's weak link — see §3. |
| Texture | StableProjectorz (AI base color) → ArmorPaint (PBR set + emissive mask) | §4. GPU tools: `free_gpu` first. |
| **Review gate** | Blender turntable renders, fully textured | **Mandatory before any UE work** [[feedback-render-textured-for-review]] — never ask the user to judge clay. |
| Import | UE headless python | §6 gotchas — the skeletal-import crash rules are load-bearing. |
| Wire | C++ ctor refs + montages + AnimBP-or-code | §6. |

## 3. Animation — the honest plan (verified Tripo facts)

**What Tripo retarget (`/v3/animations/retarget`, 10 credits/anim) actually provides (rig v2.5):**
- **biped:** idle / walk / run / dive / climb / jump / slash / shoot / hurt / fall / turn (rich).
- **quadruped:** `preset:quadruped:walk` **only**. hexapod/octopod: walk. serpentine: march.
- **No attack, idle, hit, or death presets for any non-biped rig.**

**Decision ladder per species** (work down; stop at first success; every rung ends in FBX anims on
OUR skeleton):

1. **Tripo presets** — covers Sicklejaw/Duskstalker locomotion + `slash`/`hurt` (bite-ish attack,
   flinch) IF rig-check says biped. Best case: the slice creature is ~fully Tripo-animated.
2. **Blender hand-author on the Tripo rig** (bpy-scriptable keyframing for simple moves: bite =
   neck lunge + jaw open/close; death = collapse + settle; charge loop = gallop cycle retimed from
   walk). Moderate quality, $0, fully headless. This is the default for the missing
   attack/idle/death pieces and all Crestback specials.
3. **Retarget from a purchased/free anim source via UE IK Retargeter** (editor-manual, one recipe
   per skeleton pair): free **Animal Variety Pack** (Epic-sponsored, wolf 26 anims) is the
   zero-cost quadruped source for Crestback locomotion/attacks.
4. **Buy the creature outright (mesh+rig+anims)** — the approved fallbacks, license-clean for a
   free cooked mod (§5): **Kubold "Raptor Dinosaur Animated"** $59.99 (150+ anims — the exact
   Sicklejaw archetype), **HATOGAME "Animated Dinosaur Pack"** $169.99 (9 species, full hostile-AI
   anim sets), **StockLancer "Sandworm, Lord of the Dunes"** $59.99 (43 anims incl. underground
   reveals). **Purchases need explicit user approval** — surface the decision at the review gate,
   with rung-2 renders in hand so the user judges the actual quality gap.

**Sicklejaw resolution (user, 2026-07-24): QUALITY-FIRST REBUILD before W2.2** — regenerate the
adopted candidate D in neutral A-pose + closed mouth (image-to-model from the approved
review-board renders; `mod-artist` judges look-likeness against the board — user re-approves the
rebuild board before UE import), retopo per the §2 standard, re-upload → Tripo rig (biped,
`spec=tripo` — mixamo rigs fail preset retargets) → re-retarget the 5 presets. Kubold stays the
pocketed fallback for locomotion quality.

**Minimum viable anim set** (land species): idle, walk, run, attack-primary, death.
Polish set: roar/warning, hit-react, leap start/land (Sicklejaw), charge loop + hit (Crestback),
crouch-walk (Duskstalker). The slice ships on the minimum set.

**Montage-less bring-up:** doc 07 §5's assumption — actions may run with a null montage (damage
without anim) — means combat logic testing NEVER blocks on animation. Anim quality gates the
*content* review, not the *systems* review.

## 4. Texturing & the aggro-glow telegraph

- StableProjectorz: albedo base pass (depth/normal ControlNet). Style guard: prompt for "clean
  stylized game texture, flat shading zones, no photo noise" — SD drifts noisy/over-weathered,
  which fights the SF look; ArmorPaint pass corrects.
- ArmorPaint: full PBR set (base/normal/roughness/metallic/AO) via its UE ORM export preset,
  **plus an emissive mask** for the bioluminescent markings.
- In UE: one master creature material (ORM + emissive), per-species Material Instances. The
  emissive intensity is a scalar parameter driven from C++ (`UMaterialInstanceDynamic`) by aggro
  state (doc 01 telegraphs: Sicklejaw brighten, Duskstalker extinguish). Cheap, no extra VFX.
- Icons (item + creature descriptor): scripted Blender render rig (fixed angle/lighting), rendered
  from the FINAL textured model; imported as icon textures (uncheck sRGB only for packed masks per
  the content guide — icons themselves stay sRGB).

## 5. Fab/Sketchfab licensing rules (for rungs 3–4 and the worm fallback)

Verified 2026-07-11 (fab.com/eula + Epic Content EULA):
- Purchased Fab content may ship **incorporated, in cooked/object form, as an inseparable part** of
  a distributed project — an Alpakit .pak/.utoc/.ucas qualifies; free distribution is fine.
- **Never** commit source FBX/uassets of purchased packs to the (public) repo, and never ship them
  in extractable standalone form. Keep purchased sources in a local `_assets-licensed/` folder
  OUTSIDE the git repo; document provenance in `docs/ASSET-LEDGER.md` (create at first asset).
- CC-BY (e.g. free Sketchfab velociraptor): attribution on the mod page + README. CC0
  (Quaternius): no strings. Film-IP fan models (Sketchfab "Dune sandworm"): **prohibited** — the
  StockLancer worm is original IP, safe.

## 6. UE import & wiring (headless-first)

- **Skeletal import (the crash that already bit us** [[sf-skeletal-mesh-import-crash]]**):**
  headless python import MUST `save_directory` (not just save_asset) or the cooked mesh crashes on
  use; assign meshes defensively behind a `GetSkeleton()` guard so a broken import degrades instead
  of crashing. Import via FBX with our skeleton; one skeleton per species (shared skeleton across
  species only if rigs genuinely match — don't force it).
- **Blender→UE FBX export** [[blender-ue-fbx-export-gotchas]]: NO `bake_space_transform` (size
  distortion), verify real-world scale in UE (a raptor arriving at 0.5 cm is a known failure mode),
  delete stale assets before reimport.
- **Anim assets:** import anim FBXs against the species skeleton; create `UAnimMontage`s for action
  anims (scriptable via python asset tools; if montage creation resists headless authoring, it's a
  documented 2-minute editor recipe per anim).
- **AnimBP:** the game provides `UFGCreatureAnimInstance` (Creature/FGCreatureAnimInstance.h) —
  census (doc 07 §9) dumps what vanilla anim instances read. V1 recipe: minimal editor-authored
  AnimBP per species (locomotion blendspace keyed on speed + montage slot), parented to
  `UFGCreatureAnimInstance` if its variables prove useful, else plain `UAnimInstance`. This is an
  accepted editor-manual step (one per species, ~15 min, click-path recipe in the packet). Code
  alternative for the slice: drive locomotion by `PlayAnimation`/single-node switching from C++ —
  acceptable for systems bring-up only.
- **Materials:** master material + MIs as §4; creature meshes use standard skeletal materials.
  Remains item meshes mirror the matching vanilla remains material route. Sicklejaw Remains uses
  `Asset_Master` with dedicated Albedo, Normal, and uniform-white non-sRGB `TC_MASKS`
  `ReflectionMap` textures plus `Have_ReflMap=true`; it must not use `MM_FactoryBaked` (that route
  inherited factory paint/custom-data and PowerPole reflection defaults on a plain pickup).

## 7. Sandwyrm assets — procedural by design

- **Mesh:** segmented body authored/cleaned in Blender (Tripo-generate a hero head + one body
  segment + tail tip; array/vary segments in Blender). Budget ≤80k tris total (it's the flagship).
- **Skeleton:** simple spine chain (~20–30 bones head→tail), authored by bpy SCRIPT (deterministic,
  no rigging tool needed — serpentine rigs are trivial chains).
- **Animation: NONE imported.** The spine is driven procedurally in C++ (undulation sine train +
  emergence arc + lunge curves via `UPFAWormMovement`, doc 07 §7) — the same procedural-transform
  playbook that already ships in LaserRifle [[laserrifle-procedural-hold]]. Morph/jaw: one jaw bone,
  code-driven. This makes the worm the LEAST animation-risky creature despite being the biggest.
- **Fallback (pre-approved path, needs purchase sign-off):** StockLancer worm pack (§3 rung 4) if
  procedural motion reads badly in the review gate.
- **VFX:** eruption/dust from the free CC0 JangaFX VDB library baked to **flipbooks** (Niagara
  SubUV — the game-modding default; never runtime VDB), dust-trail = Niagara ribbon + ground decal,
  optional VectorayGen swirl field for the eruption vortex. Screen shake + rumble audio reuse
  base-game Wwise events ([[sf-mod-wwise-sound]]: `PostAkEventAtLocation`, reuse existing event
  paths — explosion/quake family; AkAudio module + Wwise plugin refs in `.uplugin`).

## 8. GPU & credit discipline

- `free_gpu` before Hunyuan/StableProjectorz sessions; **never any GPU tool during a game/mod
  build** (workspace law). Sequence headless UE work like a build (it holds ~28 GB).
- Tripo: **rig-check free; rig 25 credits; retarget 10/anim** ($1 = 100 credits; doc examples show
  30/20 — verify billing on first real call). API credits ≠ Studio credits — always label which
  pool a balance report refers to [[tripo-two-credit-pools]]; params from
  `component-farming-prompts.md` conventions, not addon defaults.
- Expected v1 spend if the ladder stays on rungs 1–3: **≈ 4 rigs (100 cr) + ~10 retargets (100 cr)
  ≈ $2–5 API credits + $0 purchases.** Worst case (all fallbacks): +$60 (Kubold) or +$170
  (HATOGAME) + $60 (StockLancer) — user-approved, per §3/§7.
