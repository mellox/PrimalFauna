# Graph Report - PrimalFauna  (2026-07-23)

## Corpus Check
- 10 files · ~11,743 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 10 nodes · 9 edges · 2 communities (1 shown, 1 thin omitted)
- Extraction: 100% EXTRACTED · 0% INFERRED · 0% AMBIGUOUS
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `932c8b90`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]

## God Nodes (most connected - your core abstractions)
1. `08 — Implementation Plan (team execution)` - 8 edges
2. `1. Orchestration model` - 2 edges
3. `Packet handoff contract` - 1 edges
4. `2. Packet families` - 1 edges
5. `3. Phases` - 1 edges
6. `4. In-game test checklists (owner: user; orchestrator writes per-build)` - 1 edges
7. `5. Packet-family inputs (load ONLY these + doc 07)` - 1 edges
8. `6. Risk register` - 1 edges
9. `7. Definition of v1.0` - 1 edges

## Surprising Connections (you probably didn't know these)
- None detected - all connections are within the same source files.

## Import Cycles
- None detected.

## Communities (2 total, 1 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.25
Nodes (7): 08 — Implementation Plan (team execution), 2. Packet families, 3. Phases, 4. In-game test checklists (owner: user; orchestrator writes per-build), 5. Packet-family inputs (load ONLY these + doc 07), 6. Risk register, 7. Definition of v1.0

## Knowledge Gaps
- **7 isolated node(s):** `Packet handoff contract`, `2. Packet families`, `3. Phases`, `4. In-game test checklists (owner: user; orchestrator writes per-build)`, `5. Packet-family inputs (load ONLY these + doc 07)` (+2 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `08 — Implementation Plan (team execution)` connect `Community 0` to `Community 1`?**
  _High betweenness centrality (0.972) - this node is a cross-community bridge._
- **Why does `1. Orchestration model` connect `Community 1` to `Community 0`?**
  _High betweenness centrality (0.222) - this node is a cross-community bridge._
- **What connects `Packet handoff contract`, `2. Packet families`, `3. Phases` to the rest of the system?**
  _7 weakly-connected nodes found - possible documentation gaps or missing edges._