# Graph Report - PrimalFauna  (2026-07-25)

## Corpus Check
- 162 files · ~9,025,569 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 654 nodes · 730 edges · 81 communities (59 shown, 22 thin omitted)
- Extraction: 96% EXTRACTED · 4% INFERRED · 0% AMBIGUOUS · INFERRED: 26 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `fdfe4deb`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 2|Community 2]]
- [[_COMMUNITY_Community 3|Community 3]]
- [[_COMMUNITY_Community 4|Community 4]]
- [[_COMMUNITY_Community 5|Community 5]]
- [[_COMMUNITY_Community 6|Community 6]]
- [[_COMMUNITY_Community 7|Community 7]]
- [[_COMMUNITY_Community 8|Community 8]]
- [[_COMMUNITY_Community 9|Community 9]]
- [[_COMMUNITY_Community 10|Community 10]]
- [[_COMMUNITY_Community 11|Community 11]]
- [[_COMMUNITY_Community 12|Community 12]]
- [[_COMMUNITY_Community 13|Community 13]]
- [[_COMMUNITY_Community 14|Community 14]]
- [[_COMMUNITY_Community 15|Community 15]]
- [[_COMMUNITY_Community 16|Community 16]]
- [[_COMMUNITY_Community 21|Community 21]]
- [[_COMMUNITY_Community 22|Community 22]]
- [[_COMMUNITY_Community 23|Community 23]]
- [[_COMMUNITY_Community 24|Community 24]]
- [[_COMMUNITY_Community 25|Community 25]]
- [[_COMMUNITY_Community 26|Community 26]]
- [[_COMMUNITY_Community 27|Community 27]]
- [[_COMMUNITY_Community 28|Community 28]]
- [[_COMMUNITY_Community 29|Community 29]]
- [[_COMMUNITY_Community 30|Community 30]]
- [[_COMMUNITY_Community 31|Community 31]]
- [[_COMMUNITY_Community 32|Community 32]]
- [[_COMMUNITY_Community 33|Community 33]]
- [[_COMMUNITY_Community 35|Community 35]]
- [[_COMMUNITY_Community 37|Community 37]]
- [[_COMMUNITY_Community 38|Community 38]]
- [[_COMMUNITY_Community 41|Community 41]]
- [[_COMMUNITY_Community 42|Community 42]]
- [[_COMMUNITY_Community 43|Community 43]]
- [[_COMMUNITY_Community 44|Community 44]]
- [[_COMMUNITY_Community 45|Community 45]]
- [[_COMMUNITY_Community 46|Community 46]]
- [[_COMMUNITY_Community 47|Community 47]]
- [[_COMMUNITY_Community 48|Community 48]]
- [[_COMMUNITY_Community 49|Community 49]]
- [[_COMMUNITY_Community 50|Community 50]]
- [[_COMMUNITY_Community 51|Community 51]]
- [[_COMMUNITY_Community 52|Community 52]]
- [[_COMMUNITY_Community 53|Community 53]]
- [[_COMMUNITY_Community 54|Community 54]]
- [[_COMMUNITY_Community 55|Community 55]]
- [[_COMMUNITY_Community 56|Community 56]]
- [[_COMMUNITY_Community 57|Community 57]]
- [[_COMMUNITY_Community 58|Community 58]]
- [[_COMMUNITY_Community 59|Community 59]]
- [[_COMMUNITY_Community 60|Community 60]]
- [[_COMMUNITY_Community 61|Community 61]]
- [[_COMMUNITY_Community 62|Community 62]]
- [[_COMMUNITY_Community 63|Community 63]]
- [[_COMMUNITY_Community 64|Community 64]]
- [[_COMMUNITY_Community 65|Community 65]]
- [[_COMMUNITY_Community 66|Community 66]]
- [[_COMMUNITY_Community 67|Community 67]]
- [[_COMMUNITY_Community 68|Community 68]]
- [[_COMMUNITY_Community 69|Community 69]]
- [[_COMMUNITY_Community 70|Community 70]]
- [[_COMMUNITY_Community 71|Community 71]]
- [[_COMMUNITY_Community 72|Community 72]]
- [[_COMMUNITY_Community 73|Community 73]]
- [[_COMMUNITY_Community 74|Community 74]]
- [[_COMMUNITY_Community 75|Community 75]]
- [[_COMMUNITY_Community 76|Community 76]]
- [[_COMMUNITY_Community 77|Community 77]]

## God Nodes (most connected - your core abstractions)
1. `W2.6b — the EXPRESSIVE set (2026-07-25)` - 13 edges
2. `Sicklejaw candidate D — rig + authored animation (W2.6)` - 11 edges
3. `ClipCtx` - 11 edges
4. `Sicklejaw rig-v2 + authored animation — report` - 11 edges
5. `UWorld` - 11 edges
6. `07 — Technical Architecture` - 11 edges
7. `Sicklejaw animation rebind report` - 9 edges
8. `05 — Asset Pipeline` - 9 edges
9. `03 — Spawning & Territories` - 9 edges
10. `Rig` - 8 edges

## Surprising Connections (you probably didn't know these)
- `build_bite()` --calls--> `env()`  [INFERRED]
  art/wip/sicklejaw-d-v2/scripts/60_animate_d.py → art/wip/sicklejaw-d-v2/scripts/exprlib.py
- `build_hurt()` --calls--> `env()`  [INFERRED]
  art/wip/sicklejaw-d-v2/scripts/60_animate_d.py → art/wip/sicklejaw-d-v2/scripts/exprlib.py
- `build_bite()` --calls--> `env()`  [INFERRED]
  art/wip/sicklejaw-rig-v2/scripts/20_animate.py → art/wip/sicklejaw-d-v2/scripts/exprlib.py
- `build_hurt()` --calls--> `env()`  [INFERRED]
  art/wip/sicklejaw-rig-v2/scripts/20_animate.py → art/wip/sicklejaw-d-v2/scripts/exprlib.py
- `APFASicklejaw()` --calls--> `SetBaseHealth()`  [INFERRED]
  Source/PrimalFauna/Private/Species/PFASicklejaw.cpp → Source/PrimalFauna/Private/PFACreatureBase.cpp

## Import Cycles
- None detected.

## Communities (81 total, 22 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.20
Nodes (9): 08 — Implementation Plan (team execution), 1. Orchestration model, 2. Packet families, 3. Phases, 4. In-game test checklists (owner: user; orchestrator writes per-build), 5. Packet-family inputs (load ONLY these + doc 07), 6. Risk register, 7. Definition of v1.0 (+1 more)

### Community 1 - "Community 1"
Cohesion: 0.17
Nodes (11): 07 — Technical Architecture, 10. Save-plan versioning, 1. Architecture principle: own every class you change, 2. Class map, 3. Species authoring — everything on the CDO, in C++, 4. Behavior: borrow the vanilla Behavior Tree, own the actions, 5. Attacks = native `UFGCreatureAction` classes (no custom combat code), 6. Loot & death (+3 more)

### Community 2 - "Community 2"
Cohesion: 0.20
Nodes (9): 05 — Asset Pipeline, 1. Art direction & generation briefs, 2. Per-creature pipeline (stage gates in order), 3. Animation — the honest plan (verified Tripo facts), 4. Texturing & the aggro-glow telegraph, 5. Fab/Sketchfab licensing rules (for rungs 3–4 and the worm fallback), 6. UE import & wiring (headless-first), 7. Sandwyrm assets — procedural by design (+1 more)

### Community 3 - "Community 3"
Cohesion: 0.18
Nodes (10): 04 — Config & UI, 1. The config tree (v1), 2. Multiplayer semantics (must be logged + tested, not assumed), 3. Config ↔ world-reload interactions, 4. Why ModConfiguration (and the alternative considered), 5. UI copy standards, Section: Factions (doc 02), Section: General (+2 more)

### Community 4 - "Community 4"
Cohesion: 0.18
Nodes (10): 03 — Spawning & Territories, 1. Why real `AFGCreatureSpawner`s (not a custom spawn loop), 2. Territory data (authored, seeded, curated), 3. Injection & idempotence (the subsystem's job), 4. Persistence model — ✅ RESOLVED: OUTCOME A (native persistence), gate re-run 2026-07-23, 5. Density semantics (compose with HostileScaling, don't fight it), 6. Live creature lifecycle rules, 7. Sandwyrm event system (its own phase — no nests) (+2 more)

### Community 5 - "Community 5"
Cohesion: 0.22
Nodes (8): 02 — Hostility & Factions, 1. The machinery we build on (verified), 2. The target policy (decision table), 3. Species aggression modes (config `Aggression`, doc 04), 4. Pack behavior (Sicklejaw showcase; config `PackAssist`), 5. Perception tuning, 6. Sandwyrm targeting — disturbance, not perception, 7. Statically unverifiable items (differential-review carryover → in-game checklist)

### Community 6 - "Community 6"
Cohesion: 0.29
Nodes (6): 00 — Vision & Pillars, 1. Locked decisions (user-approved 2026-07-11), 2. Pillars (tie-breakers for every design argument), 3. V1.0 definition (ship gate), 4. Non-goals (v1) — the parking lot, 5. Reading order

### Community 7 - "Community 7"
Cohesion: 0.29
Nodes (6): 06 — Integration & Compatibility, 1. How HostileScaling discovers and scales creatures (verified, not assumed), 2. Our obligations — the creature contract, 3. Our obligations — the spawner contract, 4. Verification matrix (both-ways, in-game — doc 08 gates), 5. Broader compatibility notes

### Community 8 - "Community 8"
Cohesion: 0.50
Nodes (3): Headline design facts (why this plan is buildable), PrimalFauna — hostile megafauna for Satisfactory, The plan

### Community 9 - "Community 9"
Cohesion: 0.36
Nodes (8): GetBorrowedBehaviorTree(), NeedTransform_Implementation(), PostSaveGame_Implementation(), PreLoadGame_Implementation(), PreSaveGame_Implementation(), int32, UBehaviorTree, virtual

### Community 10 - "Community 10"
Cohesion: 0.33
Nodes (3): GetActiveConfig(), FPFAConfigStruct, UObject

### Community 11 - "Community 11"
Cohesion: 0.12
Nodes (23): AFGCreatureController, APFACreatureBase, FPFASelfTest, FVector, DumpAllCreaturesCommand(), DumpCreatureCommand(), FindLookedAtOrNearestCreature(), IsEnabled() (+15 more)

### Community 12 - "Community 12"
Cohesion: 0.40
Nodes (3): class, EPFAAggression(), FDefaultGameModuleImpl()

### Community 13 - "Community 13"
Cohesion: 0.50
Nodes (3): Placeholder rig — technical notes (for the systems lane), PrimalFauna — Asset Ledger, W2.1b — Sicklejaw QUALITY REBUILD (2026-07-24)

### Community 21 - "Community 21"
Cohesion: 0.25
Nodes (4): namespace, PFABehaviorBorrower(), PFADiag(), PFAFactionConfig()

### Community 22 - "Community 22"
Cohesion: 0.12
Nodes (17): EPFAAggression, FLifetimeProperty, FObjectInitializer, AggressionModeToString(), APFACreatureBase(), ApplyRotationPolicy(), BeginPlay(), GetLifetimeReplicatedProps() (+9 more)

### Community 23 - "Community 23"
Cohesion: 0.17
Nodes (17): AFGCreature, APawn, APFACreatureController(), CanBeAlertedByTarget_Implementation(), CanBeHostileTowards(), ForceAggro(), IsValidTarget(), LogRuleVerdict() (+9 more)

### Community 24 - "Community 24"
Cohesion: 0.39
Nodes (7): disk_verify(), log(), make_anim_task(), make_sk_task(), r""" import_placeholder_ue.py -- import the W1.3 placeholder creature rig (a ske, set_editor_property that logs-and-continues instead of aborting the     whole sc, safe_set()

### Community 25 - "Community 25"
Cohesion: 0.50
Nodes (3): 2026-07-23 — Phase-1 gate re-run: THE BT CENSUS (build w15-3, full profile), 2026-07-23 — smoke session (Grass Fields spawn, build w11-2, 113-mod profile incl. SF+), CENSUS — live vanilla creature data (feeds doc 01 anchors)

### Community 27 - "Community 27"
Cohesion: 0.20
Nodes (9): Bone-mapping notes, Known compromises / conscious deviations, Method (per clip), Note for the UE import step (orchestrator), Per-clip results, Root cause (what actually broke it in-game), Sicklejaw animation rebind report, The exact next tweak I would make (+1 more)

### Community 28 - "Community 28"
Cohesion: 0.27
Nodes (10): FAssetData, FResult, IAssetRegistry, DiscoverAndBorrow(), PickCandidate(), QueryBehaviorTrees(), int32, TArray (+2 more)

### Community 29 - "Community 29"
Cohesion: 0.21
Nodes (10): CanPerformAction_Implementation(), GetConfiguredBiteDamage(), PerformAction_Implementation(), ProcessHit_Implementation(), ScoreAction_Implementation(), TryDirectMeleeHit(), UPFACreatureActionMelee(), AFGCharacterBase (+2 more)

### Community 30 - "Community 30"
Cohesion: 0.22
Nodes (8): After either path — re-verify, Assigning `BT_PFAGeneric` to our creatures, BT-FALLBACK-RECIPE — authoring `BT_PFAGeneric`, Path A — hardcode a discovered vanilla path (try this before Path B), Path B — author `BT_PFAGeneric` from scratch, Prerequisites, Step 0 — read the census the borrower already logged (do this first), The structure (best-effort — verify against a census BT)

### Community 31 - "Community 31"
Cohesion: 0.30
Nodes (10): disk_verify(), fail(), import_sk_fresh(), log(), make_anim_task(), make_sk_task(), measure_height_cm(), r""" import_sicklejaw_ue.py -- W2.2: import the APPROVED Sicklejaw rebuild (art/ (+2 more)

### Community 32 - "Community 32"
Cohesion: 0.13
Nodes (13): FName, PFA_RunSelfTests(), ConfigureNest(), CheckNestRegistryConsistency(), PostLoadGame_Implementation(), ReconcileNests(), RefreshTick(), SpawnNestAtPlayer() (+5 more)

### Community 35 - "Community 35"
Cohesion: 0.22
Nodes (8): credits, file, label, model, rendered, status, task_id, type

### Community 37 - "Community 37"
Cohesion: 0.24
Nodes (10): int32, FString, int32, TCHAR, BeginPlay(), FindSequenceInPackages(), SpawnTestCommand(), StartLocomotionAnim() (+2 more)

### Community 41 - "Community 41"
Cohesion: 0.07
Nodes (43): build_bite(), build_gait(), build_hurt(), build_idle(), bump_cyc(), Author the five Sicklejaw clips procedurally on rig-v2, self-check them, export., Coil -> committed forward strike -> snap -> follow-through -> recover.     The b, Sharp impact flinch -> brace stagger -> recover. (+35 more)

### Community 42 - "Community 42"
Cohesion: 0.08
Nodes (26): BodySolver, bump(), ccd_ik(), clamp01(), ease_in_out(), Gait, implied_speed_check(), lerp() (+18 more)

### Community 43 - "Community 43"
Cohesion: 0.07
Nodes (21): bump_cyc(), ClipCtx, oneshot_window(), Expressive-clip forge: the shared machinery for the W2.6b Sicklejaw set.  The W2, Keys a clip from a parameter function and measures it in the same pass., Where the head can sit given the trunk lean -- kept inside the neck's reach, Head AND tip of every bone -- a point cloud that is sensitive to both         tr, Idle's mean pose + its own per-bone excursion. A one-shot that starts         in (+13 more)

### Community 44 - "Community 44"
Cohesion: 0.08
Nodes (25): 10. THE ONE NEXT IMPROVEMENT I WOULD MAKE, 11. FILES, 1. THE JAW: A BIND DEFECT, NOT A TUNING PROBLEM, 1. WHICH FILE IS D, AND HOW I ESTABLISHED IT, 2. CLIP TABLE — every number from `validate_anim_fbx.py`, 2. D'S EXISTING RIG — WHY IT COULD NOT BE KEPT, 3. EMOTIONAL REGISTER — the silhouette proof, 3. HOW D'S ANATOMY WAS MEASURED (from the mesh, not the broken rig) (+17 more)

### Community 45 - "Community 45"
Cohesion: 0.14
Nodes (15): axis_projected_hinge(), H(), measure_jaw_hinge(), mid(), mirror(), procedural_weights(), Build the canonical Sicklejaw creature rig (rig-v2).  Steps:   1. Import SK_Sick, Split a polyline into n EVEN segments -> n+1 points.      Tripo's spine and neck (+7 more)

### Community 46 - "Community 46"
Cohesion: 0.13
Nodes (9): arc_point(), centreline(), leg_joints(), mirror(), Build the canonical rig for candidate D (mesh 7174a7d7 / rig 377f815f).  D's gen, Centroid per band along `axis` -> a polyline through the limb's core., Point at arc-length fraction t along a polyline., Split the measured leg centreline at THEROPOD arc-length proportions.      Extre (+1 more)

### Community 47 - "Community 47"
Cohesion: 0.15
Nodes (9): _engine(), Shared textured render rig for the Sicklejaw rig-v2 work. Fixed 3-point light se, Slide the ground backward so an in-place clip's planted foot stays locked     to, Fixed 3-point rig: key (warm, front-left-high), fill (cool, front-right-low), ri, Shadow-catching ground. `checker` (world units per square) adds a grid so     fo, scroll_ground(), setup_ground(), setup_lights() (+1 more)

### Community 48 - "Community 48"
Cohesion: 0.17
Nodes (11): 1. WHAT WAS ACTUALLY WRONG (beyond the packet brief), 2. BONE CANON ADOPTED, 3. ASSERTION RESULTS (Deliverable 1 proof), 4. ANIMATION, 5. EXPORT SETTINGS (verbatim — match the importer to these), 6. STYLED AGAINST, 7. KNOWN COMPROMISES, 8. THE ONE NEXT IMPROVEMENT I WOULD MAKE (+3 more)

### Community 49 - "Community 49"
Cohesion: 0.33
Nodes (9): fail(), log(), main(), parse_args(), r""" validate_anim_fbx.py -- headless Blender gate that judges a creature ANIMAT, World-space head position of a bone per frame., sample_world_positions(), side_of() (+1 more)

### Community 50 - "Community 50"
Cohesion: 0.33
Nodes (9): fail(), log(), main(), mirror_name(), parse_args(), r""" validate_rig_fbx.py -- headless Blender gate that FAILS a creature FBX whos, Best-effort L<->R name flip, tolerant of the common conventions., side_of() (+1 more)

### Community 51 - "Community 51"
Cohesion: 0.25
Nodes (7): build_gait(), build_idle(), build_turn(), Cycle + stance clips: gaits (Walk / Run / Prowl), the three idle registers, and, Stationary re-facing -- the fix for "a stopped creature cannot track a     straf, A biped gait cycle. The stance foot is SOLVED to track backward at exactly     `, One idle machine, three emotional registers (spec 3). The register is carried

### Community 52 - "Community 52"
Cohesion: 0.43
Nodes (6): build(), categorise(), collect(), metric_row(), r""" build_anim_review_board.py -- assemble every authored clip's contact sheet,, clip -> {sheet, silhouette, stills[], report}

### Community 53 - "Community 53"
Cohesion: 0.33
Nodes (3): Per-clip visual proof.  Produces, per clip:   * 0 / 25 / 50 / 75 % textured thre, Render one frame with the ground scrolled to match the clip's ground speed., shot_at()

### Community 54 - "Community 54"
Cohesion: 0.33
Nodes (3): Per-clip visual proof.  Produces, per clip:   * 0 / 25 / 50 / 75 % textured thre, Render one frame with the ground scrolled to match the clip's ground speed., shot_at()

### Community 55 - "Community 55"
Cohesion: 0.33
Nodes (3): Measure D's REAL modelled mouth gape from the MESH, not from the bone directions, Mean of the n most-forward verts (the physical tip of that jaw)., tip_of()

### Community 56 - "Community 56"
Cohesion: 0.33
Nodes (3): jaw_factor(), RIG FIX: give the Sicklejaw a mouth that can actually close.  DEFECT (found 2026, 1 = pure lower jaw, 0 = pure upper skull, smooth across the tooth line     and f

### Community 57 - "Community 57"
Cohesion: 0.33
Nodes (3): export_action(), FBX export settings for the Sicklejaw rig-v2, per the OFFICIAL Satisfactory modd, Armature-only, exactly ONE action per FBX (All Actions on, NLA off).

### Community 59 - "Community 59"
Cohesion: 0.50
Nodes (4): describe_skeleton(), log(), r""" probe_vanilla_skeleton.py -- settles UNKNOWN U1 of _team/primalfauna-animat, Try several APIs to read the bone hierarchy -- the point is to learn     whether

### Community 60 - "Community 60"
Cohesion: 0.50
Nodes (3): Render the SOURCE Sicklejaw mesh (textured) from 4 angles + a symmetry-plane ana, Mirror across the plane through centroid at given yaw; measure residual via, sym_error()

### Community 61 - "Community 61"
Cohesion: 0.67
Nodes (3): build(), Settle the export-scale contradiction empirically.  Three candidate conventions,, roundtrip()

### Community 62 - "Community 62"
Cohesion: 0.50
Nodes (3): W2.6b -- the EXPRESSIVE Sicklejaw set, authored on the jaw-fixed rig (v3).  Fift, Guarantee frame 1 is exactly the reference pose even for lagged curves., zeroed()

## Knowledge Gaps
- **158 isolated node(s):** `VERDICT`, `1. WHICH FILE IS D, AND HOW I ESTABLISHED IT`, `2. D'S EXISTING RIG — WHY IT COULD NOT BE KEPT`, `3. HOW D'S ANATOMY WAS MEASURED (from the mesh, not the broken rig)`, `4. BONE CANON + ASSERTION RESULTS` (+153 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **22 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `UWorld` connect `Community 11` to `Community 32`, `Community 37`?**
  _High betweenness centrality (0.013) - this node is a cross-community bridge._
- **Why does `SpawnTestCommand()` connect `Community 37` to `Community 11`?**
  _High betweenness centrality (0.011) - this node is a cross-community bridge._
- **Why does `Rig` connect `Community 42` to `Community 41`?**
  _High betweenness centrality (0.010) - this node is a cross-community bridge._
- **What connects `VERDICT`, `1. WHICH FILE IS D, AND HOW I ESTABLISHED IT`, `2. D'S EXISTING RIG — WHY IT COULD NOT BE KEPT` to the rest of the system?**
  _263 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Community 11` be split into smaller, more focused modules?**
  _Cohesion score 0.1168091168091168 - nodes in this community are weakly interconnected._
- **Should `Community 22` be split into smaller, more focused modules?**
  _Cohesion score 0.11904761904761904 - nodes in this community are weakly interconnected._
- **Should `Community 32` be split into smaller, more focused modules?**
  _Cohesion score 0.13071895424836602 - nodes in this community are weakly interconnected._