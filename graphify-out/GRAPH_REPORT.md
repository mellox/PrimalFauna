# Graph Report - PrimalFauna  (2026-07-23)

## Corpus Check
- 22 files · ~240,021 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 121 nodes · 107 edges · 21 communities (16 shown, 5 thin omitted)
- Extraction: 100% EXTRACTED · 0% INFERRED · 0% AMBIGUOUS
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `822b029f`
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
- [[_COMMUNITY_Community 12|Community 12]]
- [[_COMMUNITY_Community 13|Community 13]]
- [[_COMMUNITY_Community 14|Community 14]]
- [[_COMMUNITY_Community 15|Community 15]]
- [[_COMMUNITY_Community 16|Community 16]]

## God Nodes (most connected - your core abstractions)
1. `07 — Technical Architecture` - 11 edges
2. `03 — Spawning & Territories` - 9 edges
3. `05 — Asset Pipeline` - 9 edges
4. `08 — Implementation Plan (team execution)` - 8 edges
5. `02 — Hostility & Factions` - 8 edges
6. `00 — Vision & Pillars` - 6 edges
7. `04 — Config & UI` - 6 edges
8. `06 — Integration & Compatibility` - 6 edges
9. `virtual` - 5 edges
10. `1. The config tree (v1)` - 5 edges

## Surprising Connections (you probably didn't know these)
- None detected - all connections are within the same source files.

## Import Cycles
- None detected.

## Communities (21 total, 5 thin omitted)

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
Cohesion: 0.20
Nodes (9): 03 — Spawning & Territories, 1. Why real `AFGCreatureSpawner`s (not a custom spawn loop), 2. Territory data (authored, seeded, curated), 3. Injection & idempotence (the subsystem's job), 4. Persistence model ⚠ [assumption → slice test], 5. Density semantics (compose with HostileScaling, don't fight it), 6. Live creature lifecycle rules, 7. Sandwyrm event system (its own phase — no nests) (+1 more)

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
Cohesion: 0.50
Nodes (7): int32, NeedTransform_Implementation(), PostLoadGame_Implementation(), PostSaveGame_Implementation(), PreLoadGame_Implementation(), PreSaveGame_Implementation(), virtual

### Community 10 - "Community 10"
Cohesion: 0.33
Nodes (3): FPFAConfigStruct, GetActiveConfig(), UObject

## Knowledge Gaps
- **66 isolated node(s):** `FPFAConfigStruct`, `UObject`, `PRIMALFAUNA_API`, `PRIMALFAUNA_API`, `class` (+61 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **5 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **What connects `FPFAConfigStruct`, `UObject`, `PRIMALFAUNA_API` to the rest of the system?**
  _66 weakly-connected nodes found - possible documentation gaps or missing edges._