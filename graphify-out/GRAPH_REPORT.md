# Graph Report - PrimalFauna  (2026-07-23)

## Corpus Check
- 10 files · ~11,850 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 20 nodes · 18 edges · 3 communities (2 shown, 1 thin omitted)
- Extraction: 100% EXTRACTED · 0% INFERRED · 0% AMBIGUOUS
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `0372ec8a`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 2|Community 2]]

## God Nodes (most connected - your core abstractions)
1. `05 — Asset Pipeline` - 9 edges
2. `08 — Implementation Plan (team execution)` - 8 edges
3. `1. Orchestration model` - 2 edges
4. `1. Art direction & generation briefs` - 1 edges
5. `2. Per-creature pipeline (stage gates in order)` - 1 edges
6. `3. Animation — the honest plan (verified Tripo facts)` - 1 edges
7. `4. Texturing & the aggro-glow telegraph` - 1 edges
8. `5. Fab/Sketchfab licensing rules (for rungs 3–4 and the worm fallback)` - 1 edges
9. `6. UE import & wiring (headless-first)` - 1 edges
10. `7. Sandwyrm assets — procedural by design` - 1 edges

## Surprising Connections (you probably didn't know these)
- None detected - all connections are within the same source files.

## Import Cycles
- None detected.

## Communities (3 total, 1 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.25
Nodes (7): 08 — Implementation Plan (team execution), 2. Packet families, 3. Phases, 4. In-game test checklists (owner: user; orchestrator writes per-build), 5. Packet-family inputs (load ONLY these + doc 07), 6. Risk register, 7. Definition of v1.0

### Community 2 - "Community 2"
Cohesion: 0.20
Nodes (9): 05 — Asset Pipeline, 1. Art direction & generation briefs, 2. Per-creature pipeline (stage gates in order), 3. Animation — the honest plan (verified Tripo facts), 4. Texturing & the aggro-glow telegraph, 5. Fab/Sketchfab licensing rules (for rungs 3–4 and the worm fallback), 6. UE import & wiring (headless-first), 7. Sandwyrm assets — procedural by design (+1 more)

## Knowledge Gaps
- **15 isolated node(s):** `1. Art direction & generation briefs`, `2. Per-creature pipeline (stage gates in order)`, `3. Animation — the honest plan (verified Tripo facts)`, `4. Texturing & the aggro-glow telegraph`, `5. Fab/Sketchfab licensing rules (for rungs 3–4 and the worm fallback)` (+10 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `08 — Implementation Plan (team execution)` connect `Community 0` to `Community 1`?**
  _High betweenness centrality (0.205) - this node is a cross-community bridge._
- **Why does `1. Orchestration model` connect `Community 1` to `Community 0`?**
  _High betweenness centrality (0.047) - this node is a cross-community bridge._
- **What connects `1. Art direction & generation briefs`, `2. Per-creature pipeline (stage gates in order)`, `3. Animation — the honest plan (verified Tripo facts)` to the rest of the system?**
  _15 weakly-connected nodes found - possible documentation gaps or missing edges._