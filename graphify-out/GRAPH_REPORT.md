# Graph Report - PrimalFauna  (2026-07-24)

## Corpus Check
- 47 files · ~1,998,183 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 303 nodes · 339 edges · 36 communities (32 shown, 4 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 5 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `e70ca09f`
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
- [[_COMMUNITY_Community 28|Community 28]]
- [[_COMMUNITY_Community 29|Community 29]]
- [[_COMMUNITY_Community 30|Community 30]]
- [[_COMMUNITY_Community 31|Community 31]]
- [[_COMMUNITY_Community 32|Community 32]]
- [[_COMMUNITY_Community 33|Community 33]]
- [[_COMMUNITY_Community 35|Community 35]]

## God Nodes (most connected - your core abstractions)
1. `UWorld` - 11 edges
2. `07 — Technical Architecture` - 11 edges
3. `05 — Asset Pipeline` - 9 edges
4. `03 — Spawning & Territories` - 9 edges
5. `AActor` - 8 edges
6. `08 — Implementation Plan (team execution)` - 8 edges
7. `02 — Hostility & Factions` - 8 edges
8. `CanBeHostileTowards()` - 7 edges
9. `FindSequenceInPackages()` - 6 edges
10. `log()` - 6 edges

## Surprising Connections (you probably didn't know these)
- `OnPossess()` --calls--> `WireCreaturePerception()`  [INFERRED]
  Source/PrimalFauna/Private/PFACreatureController.cpp → Source/PrimalFauna/Private/PFACreatureControllerPerception.cpp
- `APFASicklejaw()` --calls--> `SetBaseHealth()`  [INFERRED]
  Source/PrimalFauna/Private/Species/PFASicklejaw.cpp → Source/PrimalFauna/Private/PFACreatureBase.cpp
- `APFASicklejaw()` --calls--> `SetSpawnDespawnDistance()`  [INFERRED]
  Source/PrimalFauna/Private/Species/PFASicklejaw.cpp → Source/PrimalFauna/Private/PFACreatureBase.cpp
- `PickCandidate()` --references--> `FString`  [EXTRACTED]
  Source/PrimalFauna/Private/PFABehaviorBorrower.cpp → Source/PrimalFauna/Private/Species/PFASicklejaw.cpp
- `DumpAllCreaturesCommand()` --references--> `UWorld`  [EXTRACTED]
  Source/PrimalFauna/Private/PFACheats.cpp → Source/PrimalFauna/Private/Species/PFASicklejaw.cpp

## Import Cycles
- None detected.

## Communities (36 total, 4 thin omitted)

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
Cohesion: 0.10
Nodes (22): EPFAAggression, FLifetimeProperty, FObjectInitializer, int32, AggressionModeToString(), APFACreatureBase(), BeginPlay(), GetLifetimeReplicatedProps() (+14 more)

### Community 23 - "Community 23"
Cohesion: 0.19
Nodes (16): AActor, AFGCreature, APawn, APFACreatureController(), CanBeAlertedByTarget_Implementation(), CanBeHostileTowards(), IsValidTarget(), LogRuleVerdict() (+8 more)

### Community 24 - "Community 24"
Cohesion: 0.39
Nodes (7): disk_verify(), log(), make_anim_task(), make_sk_task(), r""" import_placeholder_ue.py -- import the W1.3 placeholder creature rig (a ske, set_editor_property that logs-and-continues instead of aborting the     whole sc, safe_set()

### Community 25 - "Community 25"
Cohesion: 0.50
Nodes (3): 2026-07-23 — Phase-1 gate re-run: THE BT CENSUS (build w15-3, full profile), 2026-07-23 — smoke session (Grass Fields spawn, build w11-2, 113-mod profile incl. SF+), CENSUS — live vanilla creature data (feeds doc 01 anchors)

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
Cohesion: 0.35
Nodes (10): disk_verify(), fail(), import_sk_fresh(), log(), make_anim_task(), make_sk_task(), measure_height_cm(), r""" import_sicklejaw_ue.py -- W2.2: import the APPROVED Sicklejaw rebuild (art/ (+2 more)

### Community 32 - "Community 32"
Cohesion: 0.13
Nodes (13): FName, FString, PFA_RunSelfTests(), ConfigureNest(), CheckNestRegistryConsistency(), PostLoadGame_Implementation(), ReconcileNests(), RefreshTick() (+5 more)

### Community 33 - "Community 33"
Cohesion: 0.25
Nodes (5): UFGCreatureActionMeleeAttack(), PRIMALFAUNA_API, APFACreatureController(), PRIMALFAUNA_API, APFACreatureBase()

### Community 35 - "Community 35"
Cohesion: 0.22
Nodes (8): credits, file, label, model, rendered, status, task_id, type

## Knowledge Gaps
- **115 isolated node(s):** `AFGCreature`, `FObjectInitializer`, `UAnimSequence`, `int32`, `FObjectInitializer` (+110 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **4 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `UWorld` connect `Community 11` to `Community 32`?**
  _High betweenness centrality (0.059) - this node is a cross-community bridge._
- **Why does `SpawnTestCommand()` connect `Community 11` to `Community 22`?**
  _High betweenness centrality (0.037) - this node is a cross-community bridge._
- **Why does `ReadbackAggroFor()` connect `Community 11` to `Community 23`?**
  _High betweenness centrality (0.036) - this node is a cross-community bridge._
- **What connects `AFGCreature`, `FObjectInitializer`, `UAnimSequence` to the rest of the system?**
  _119 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Community 11` be split into smaller, more focused modules?**
  _Cohesion score 0.1168091168091168 - nodes in this community are weakly interconnected._
- **Should `Community 22` be split into smaller, more focused modules?**
  _Cohesion score 0.09686609686609686 - nodes in this community are weakly interconnected._
- **Should `Community 32` be split into smaller, more focused modules?**
  _Cohesion score 0.13071895424836602 - nodes in this community are weakly interconnected._