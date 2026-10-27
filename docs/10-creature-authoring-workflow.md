# Creature authoring workflow

This is the repeatable path from a creature idea to a tested Satisfactory species.
It records the Sicklejaw pipeline as inspected on 2026-10-14. A working Sicklejaw
component is precedent, not proof that a different morphology or lifecycle works.
The sandworm requirements below are acceptance criteria, not implementation claims.

## 1. Define the creature and its evidence

Write a species contract before modeling: silhouette, adult dimensions, locomotion,
mouth/attack envelope, intended targets, biome rules, population count, persistence,
death/drop policy, configurable behavior, and multiplayer behavior. Separate artistic
targets, measured values, assumptions, and demonstrated gameplay. Record references
and generated-asset provenance beside the source assets.

For the current sandworm, the user asks for one roaming creature across the desert
biomes; underground and partly exposed travel; eruption beneath a miner; an arching
attack swallowing a miner from above; player/creature swallowing; articulated spine
and mouth; sand effects; and tooth-shard or spice loot. Older documents proposing one
worm per region must not silently replace that global singleton requirement.

Current provisional art targets are **180 m body length, approximately 30 m shaft diameter,
38 m external mouth-lobe span (36–40 m range), and 26 m clear bore**. The wider head supersedes
the earlier 30 m outer-collar cap; the latest proportion rationale lives in
`art/sandworm/design/art-specification.md`. These replace the earlier 100 m art target and old
25–40 m design; they are not certified miner clearance. Detailed modeling via Claude is
active as of 2026-10-14, but the model is not ready and gameplay is not implemented.
The current authoring budget is ≤120k LOD0 triangles and ≤128 deform bones, targeting
48–64 spine joints plus independent radial lip/teeth/throat control and baked exports.
These are review targets, not approval of generated geometry; exact counts must pass
mesh, deformation and export/render acceptance (doc 05 §7).

Required concept set: consistent front/back/left/right straight-body views with open
mouth and teeth; ground emergence; surface/underground travel; both miner attacks;
player and creature swallowing; and loot. Include comparable miner/player scale
references. A dramatic perspective image is not an orthographic modeling reference.

Measure the target building before approving the mouth. Use all Mk1/2/3 component
transforms, including animated protrusions. A vertical swallow's clear circular bore
must exceed the footprint diagonal after teeth project inward; the throat and curved
attack path must also fit the building height. Do not equate outer jaw diameter with
clear bore. Record extra clearance separately from the measured dimensions.

`tools/sandworm/probe_miner_bounds_ue.py` reads observed local vanilla Blueprint
component templates and mesh bounds without saving packages. Its JSON explicitly
excludes construction-script changes, animation swept bounds, and current cooked-game
equivalence. A failed or placeholder-sized result must not be used to certify fit.

The 2026-10-14 probe found 256cm cube bounds in the local static miner/indicator
meshes and 64x192x64cm skeletal bounds. Transformed template unions measured roughly
2.56x10.86x5.38m (Mk1) and 2.56x10.85x5.41m (Mk2/3). These are **placeholder editor
measurements, rejected for sizing**, not vanilla miner dimensions. The evidence JSON
at `_team/PFA-sandworm-2026-10-14/miner-bounds-editor.json` sets
`sizing_accepted=false`. A prior constructed-actor attempt crashed the editor; its log
is retained beside the JSON. No game was launched and no asset package was saved.

**Real-miner acceptance gate:** identify the installed game version and all three
actual Mk1/2/3 classes; record cooked-asset or live-actor geometry provenance; measure
every visible component with its transform and the animation swept envelope. Include
the full maximum footprint and height, not placement clearance or a single mesh.
At the swallow poses, compare those envelopes against the exact exported worm's
minimum tooth-clear passage and curved throat/attack sweep. Record clearance margins
and rendered fit evidence for both underside eruption and top-down arch. Placeholder
signatures, missing components, or untested animation extent fail this gate.

## 2. Establish ownership and preserve the current game

Read `_team/primalfauna-state.md`, `_team/ASSET-COORDINATION.md`,
`_team/BUILD-COORDINATION.md`, and `_team/playbooks/creature-authoring.md`.
The shared playbook contains detailed Sicklejaw failures and remains a companion.
Inspect the live Git state, installed marker, exact asset identities, and build lock.
Keep other agents' edits and rejected candidates; use named file ownership.

Source is maintained in `C:/Claude/Projects/PrimalFauna`. The build wrapper mirrors
Source/Config/plugin metadata into `SatisfactoryModLoader/Mods/PrimalFauna`.
**Content has the opposite flow:** UE import writes the build-tree Content; copy
approved new assets back to repository Content afterward. Never mirror stale repo
Content over newly imported build-tree assets.

Keep each species' mesh/animation, surface materials, and loot packages in separate
owned destinations. Existing `import_sicklejaw_ue.py` deletes its whole Sicklejaw
destination; `author_sicklejaw_material_ue.py` owns SicklejawSurface; the remains
importer owns SicklejawRemains. A new creature importer must never delete these.
Before any delete-fresh import, resolve the exact destination and preserve a rollback.

## 3. Model and rig

Deliver the editable Blender file, textured mesh, stable skeleton, FBX export,
manifest, and rendered turntable/pose evidence. Record units, forward/up axes,
dimensions, material slots, mesh/bone counts, hierarchy, rest pose, and file hashes.
Keep one root; deliberate bone names and stable parenting; normalized nonzero weights;
no accidentally unweighted vertices or cross-body weighting. Test deformations on
the actual bound mesh, not just armature lines.

**Approved-source/exact-export gate:** record hashes for the reviewed concept sheets,
editable Blender source, generation/rig scripts, textures, manifest and exported mesh/
animation files. Export from that named source, then independently reopen the exact
export and check hierarchy, weights, dimensions, materials, mouth clearance and all
required poses. Review renders must come from that export, not an earlier source
scene. Carry these hashes through UE import, saved packages, cooked manifest and
installed artifact readback. A regenerated file invalidates downstream approval until
its differences and gates are reviewed; matching filenames or bone count alone do
not establish asset identity.

The existing `tools/validate_rig_fbx.py` is tailored to a legged creature: it requires
hips, thighs, shins, and feet. Do not fabricate leg bones to make a worm pass. Add a
morphology-specific validator that preserves useful common gates and checks a worm's
continuous spine, smooth weight falloff, mouth/jaw controls, tooth anchoring, clear
bore, bend limits, self-intersection, and usable throat. Include known-bad cases.

For a giant worm, test straight travel, partial burial, full eruption, tightest planned
arch, mouth open/closed, death collapse, and swallowing envelopes. Measure minimum
cross-section at every relevant mouth pose. Too few spine joints can kink an arch;
too much bending can invert the body even with many joints. Review silhouettes and
close views of the mouth, inner throat, and underside as well as numeric output.

Validate the measurement tool before using its result to approve an asset.
`tools/sandworm/collect_evaluated_surface.py` collects evaluated triangles for an
explicit mesh population, action/slot and frame; `clearance_geometry.py` tests triangle
interiors, not only vertices. Use clear, intrusive and tangent controls, deformed and
shape-key geometry, instances, and hidden/disabled evaluation cases. Unsupported
evaluation must reject the request, never silently omit geometry and certify clearance.
Expand both the axial test slab and radial nonpassing band by the reported numerical
bound. The float32 storage-neighborhood bound does not prove modifier arithmetic or
fidelity to the authored shape. Contact/uncertainty is not a clearance pass.

The controlled Blender fixture campaign passed **24/24 cases**, and the in-process
state-restoration diagnostics passed **5/5**, including intentional failure. Receipts:
`_team/PFA-sandworm-2026-10-14/blender-fixtures-v02/run-v02/summary.json` and
`_team/PFA-sandworm-2026-10-14/blender-fixtures-v02/state-checks-v01/summary.json`.
Keep restoration checks for action/slot, NLA, frame/subframe and evaluated pose/shape
keys; unchanged mesh counts alone do not prove temporary-mesh cleanup. These results
validate controlled fixtures, not real worm/miner fit, complete animation coverage or
the identity of a loaded source. Hash checks still need an independently verified
immutable source/launch binding before claiming loaded-source identity.

## 4. Animate with explicit gameplay timing

Author on the actual skeleton; do not retarget a humanoid preset onto a worm.
Deliver clip names, durations, source frame rates, loop rules, locomotion speed,
impact/engulf frames, additive status, reference pose, and authored curves.
Keep animation-only export and skeleton identities consistent. Disable FBX leaf bones;
preserve source sampling/frame boundaries. Compare additive layers against the real
rig, not frame1 of an animation-only import.

Sicklejaw uses `Species/PFAAnimInstance` and a native `FAnimInstanceProxy`, avoiding
headless AnimBlueprint graph-authoring limitations. Its game-thread `PreUpdate`
snapshots are a useful thread-safety pattern. A worm may use authored clips,
procedural spine transforms, or both, but one explicit driver must own the final pose.
Calling `PlayAnimation` on a component managed by an AnimInstance replaces that mode.
Do not mix competing pose drivers accidentally.

Adapt `tools/validate_anim_fbx.py` by morphology. Leg cadence/ground-contact rules are
not worm rules. A worm needs continuous body motion, phase continuity, credible sand
intersection, no jaw/teeth inversion, and geometric coverage at the exact swallow
event. Underground travel must not be flagged as an accidental legged-body defect.
Render the complete required moves; numeric gates cannot prove their visual quality.

## 5. Import and verify the saved UE assets

Use a dedicated importer based on `tools/import_sicklejaw_ue.py`; use
`tools/import_sicklejaw_remains_ue.py` for the loot presentation pattern. Import only
the owned new destinations. Discover authored clips rather than maintaining a stale
handwritten list. Verify asset classes, dimensions, bone names/count/hierarchy,
skeleton equality, animation duration/rate, material slots and additive readback.
If scale is wrong, measure the factor; never blindly apply100x.

Save every dependent skeleton/material/animation package, then verify files on disk
and reload important assets. A mesh that points at an unsaved skeleton can crash when
registered. Check PBR texture channels and sRGB/compression settings, materials under
game-like lighting, small/large item icons, pickup and conveyor mesh scale.

Run headless work only through the committed wrapper:

```powershell
pwsh -File tools/ue_headless.ps1 -Script tools/<species-importer>.py
```

Before launch, separately inspect game/editor/build processes, including dotnet-hosted
UnrealBuildTool, and `_team/BUILD-LOCK.txt`. Wait if occupied; never terminate the game
or another build. The wrapper's process-name check alone misses dotnet-hosted UBT.
Do not treat an editor exit code alone as success: require the script's complete
assertion/report result. Preserve failed attempts for diagnosis.

## 6. Add the species without importing Sicklejaw assumptions

The existing base is `APFACreatureBase : AFGEnemy`. Useful seams include native
health/damage identity, configurable aggression, death observation, and a species loot
descriptor. Audit every inherited policy before reuse: at this inspection the base
automatically rescues buried spawns, applies SicklejawAggression, installs the shared
controller, and applies ground-creature rotation policy. The subsystem also pushes
SicklejawAggression to every base-class instance. Add explicit species policy seams
so underground creatures are not teleported above ground or controlled by another
species' settings. Preserve Sicklejaw behavior with differential review.

The current melee action casts to Sicklejaw and uses Sicklejaw timing/damage config;
it is not a generic swallow action. Create species-specific actions with authoritative
target validation, cancellation/death checks, impact timing, and damage attribution.
Pass the worm actor as DamageCauser and its controller as Instigator through the
native damage pipeline so HostileScaling can observe it. Do not restore the removed
unsafe `CanSpawnCreature` hook or call hooked eligibility functions for diagnostics.

Read actual FactoryGame headers for every symbol. Compilation is not proof that a
declared API is exported in Shipping: run the shipping import checker on the artifact.

## 7. Choose the population and movement lifecycle

Ground packs can use `FPFANestRecord`, `APFANestSpawner`, native slot/kill history,
and the existing population reconciliation. `PFAWorldPopulation.cpp` currently
hardcodes Sicklejaw allocation/navigation; adding a class does not populate a new
species automatically. Preserve safe relocation, native-call quiescence, navigation
registration, player/combat holds, and partial-pack kill/respawn state.

A global worm singleton needs its own durable director record, separate from pack
density. Store unique identity, random seed, desert/position, lifecycle phase,
health/death/respawn timing, loot entitlement, and an event serial. On load, adopt the
matching actor before creating anything. Reconcile duplicate or missing actors;
revalidate transient targets; make attack/drop completion idempotent across reload.
Test global uniqueness while travelling between desert regions.

Logical persistence is separate from native actor residency. Preserve the saved
individual when its actor unloads; re-materialization restores its state, never a new
healthy individual or a replayed swallow/drop. Actor retirement must consider the
whole visible/swept body, all nearby players, combat and active transactions. A player
beside the tail must not lose the worm because its origin is distant. Neither permanent
actor residency nor the hostile-base 100 m/120 m distances are a settled worm policy;
review the extent-aware lifecycle and native compatibility before selecting either.

Native `SetIsPersistent` and `ClearKillOrphanTimer` appear in current headers, but
header presence is insufficient: doc 07 §1 records an actual Shipping-export trap for
`ClearKillOrphanTimer`. Do not prescribe that call or permanent-persistence flag;
verify an exported, native-compatible lifecycle and its runtime behavior. A custom
`UFGCreatureMovementComponent` can use `MOVE_Custom`/`PhysCustom`; deliberate burial
must be distinguished from collision failure. Use bounded terrain/biome checks, a
coherent body path and appropriate collision/damage volumes rather than assuming
one enormous upright character capsule represents an arched worm.

Replicate phase/event time, movement and attack outcomes from the server. Clients
must see telegraphs, sand, jaw movement, and target consumption without independently
applying damage. Persist health across burial and save/load; define a deliberate
death and respawn policy. Keep Sicklejaw native distance parity unchanged.

## 8. Swallowing miners, players and creatures

Use the native building dismantle interface as the candidate cleanup route rather
than raw actor deletion. Check `CanDismantle`; capture the agreed inventory/refund
policy exactly once; complete the engulf animation; invoke native dismantling;
verify power/belt connections and resource-node occupancy afterward. Extractor base
classes also include pumps, so classify actual miners explicitly. Recheck that the
target still exists and has not been upgraded, moved, or dismantled during telegraph.

`GetDismantleRefund` includes inventories/potential contents; adding those contents a
second time risks duplication. Define whether consumed building materials are lost,
dropped, or held for later loot, then test that policy. Do not infer inventory safety
from an available header declaration. Check filled output buffers and power shards,
connected belts/cables, save/reload at attack phases, and NodeShuffle occupancy.

For players and creatures, retain native attributed damage, death and player recovery
behavior. Hiding/deleting a player pawn is not a valid substitute for swallowing.
The visual engulf moment and actual mouth overlap must agree. Interrupted attacks
must release targets and restore any temporary collision/presentation state.

## 9. Loot and effects

Follow `PFALoot` for native item descriptors, `UpdateCachedStackSize`, deferred
content loading with durable references, recipe/schematic registration, and existing
save availability. The base death-drop guard is transient; a persistent boss needs
durable entitlement to prevent duplicate drops after reload. Choose the tooth-shard
or spice distribution deliberately, with a visible pickup mesh and usable icons.

Validate each loot LOD after modifiers and again after export:

1. Check finite geometry and post-modifier mesh validity, including duplicate or
   degenerate faces. Inspect a copy when validation can repair geometry; a repair is
   evidence of a defect, not a silent pass.
2. Check the intended atlas bounds and padding (0–1 for these single-tile loot
   atlases), finite UVs and bake coverage. Inspect actual bitmap pixels for meaningful
   color/variation and expected channel content; a saved PNG or successful bake exit
   does not prove usable BaseColor, roughness or normals.
3. Import every exact FBX/GLB LOD into a fresh scene, checking hashes, dimensions,
   origin, triangles, UVs and material/bitmap links. Convert glTF axes explicitly
   before comparing bounds. Links alone do not establish texture fidelity.
4. Render those imported exports with consistent camera/light/color settings: a
   neutral hero view and a 64px alpha icon per format/LOD. Review appearance and LOD
   silhouette at the delivered scale. Keep source files unchanged and reports/renders
   outside the asset tree; then perform the separate saved-UE-asset and game gates.

The packet's `loot-export-audit.py` implements the export inspection/render step.
ToothShard v001 passed all four exact FBX/GLB LOD imports in
`_team/PFA-sandworm-2026-10-14/loot-v001-tooth-audit-v02.json`; the parent reviewer
also inspected all eight reimport hero/icon images and passed shape/icon appearance.
That visual review is separate from the script's deliberately false automatic
`visual_acceptance` flag; neither result proves UE import or gameplay.

DesertSpice v002 remains rejected despite generation exit 0: its BaseColor bake was
black, global decimation left duplicate faces, and UV maxima reached approximately
1.686/1.663. The preserved `loot-v002-authoring-diagnostic.json` in the same packet
records out-of-atlas UVs and validation changing a mesh copy. V003 fixed the bake
and LOD0, but actual LOD1 reimport images still exposed slivers despite numeric
audit success. V004 removed complete small flake components instead of decimating
them, preserving retained faces/UVs, bounds, LOD0 exports and textures. All four
exact FBX/GLB imports passed the audit; the parent separately reviewed all eight
hero/icon images and passed source/export appearance. See
`spice-v004-asset-review.json` and `art/sandworm/loot/selected-assets.json`.
UE import, collision, inventory appearance and gameplay drops remain unproven.

Sand effects should follow the actual surface intersection and movement; buried
segments must not emit above mountains or across unloaded terrain. Keep effect
lifetime/particle count bounded. Verify eruption, moving ridge, partial exposure,
submerge and death separately. Effect creation is client presentation of authoritative
state; visual absence must not suppress gameplay or duplicate damage.

## 10. Build, review and prove gameplay

Extend the wrapper's asset assertions: current animation/remains cook checks cover
Sicklejaw paths only. Require all new mesh/skeleton/material/animation/effect/loot
packages in the cook and verify shipping imports. Use the committed wrapper:

```powershell
pwsh -File tools/build.ps1 -Marker <source-build-marker>
```

This wrapper deploys and hardcodes the canonical repo. For isolated candidates, read
the current approved staging workflow; do not assume a worktree changes those paths.
Preserve the last reviewed installation, configurations, saves, other mods, and
rollback hashes. Require an independent cold review of changed invariants/consumers,
then exact candidate identity and installed-file readback. Restore the reviewed build
if an unreviewed deployment cannot be completed safely.

Acceptance evidence must cover each requirement:

| Boundary | Required evidence |
|---|---|
| Art | Consistent turnarounds and every requested scenario/loot concept |
| Rig/model | Editable source, stable hierarchy, deformations and measured swallow clearance |
| UE assets | Import assertions, saved/reloaded dependencies, rendered materials, cooked package census |
| Population | One saved individual globally; at most one live body; desert travel; no unintended Sicklejaw changes |
| Movement | Visible burial, partial exposure, eruption and top arch without invalid collision/teleport rescue |
| Attacks | Miner swallowed both ways; player/creature engulf; native damage/death/cleanup; cancellation |
| Inventory | Filled miner/shards/connections/node claim follow the documented consumption policy once |
| Persistence | Two save/load cycles; wounded/buried/attacking/dead/respawn states; unchanged identity and entitlements after re-materialization |
| Residency | Players at mouth/tail/opposite ends; full-body streaming/visibility; no center-distance disappearance or duplicate body |
| Loot/effects | Correct item pickups once, presentation/stacking, bounded sand effects at the surface |
| Compatibility | HostileScaling behavior, NodeShuffle occupancy, host/client observations where supported |
| Regression | Existing Sicklejaw spawn/despawn, combat, loot, partial kills and relocation remain healthy |

Log inputs, decisions and outcomes with actor/event IDs and effective configuration.
Self-tests must report no-coverage honestly when no worm or target was exercised.
Use a dedicated test save or preserved copy for destructive miner tests. A successful
build, asset manifest, static assertion, or staged screenshot is never gameplay proof.
Record the installed marker and log/save evidence for the final test session.

## Current status and follow-up discipline

This guide documents the existing pipeline and the required new-species process.
It does not claim the sandworm has been implemented, imported or gameplay-tested.
Update the relevant row with artifact paths/hashes and explicit measured outcomes as
work completes. Keep failed candidates and unresolved runtime assumptions visible;
do not rename a smaller completed subset as the full creature goal.
