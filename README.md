# PrimalFauna — hostile megafauna for Satisfactory

**Status: PLAN ONLY — no source yet.** This repo is the researched design + implementation contract
(like Satismagictory), written to be executed by an orchestrating agent handing packets to
lower-cost implementation agents.

New hostile creatures for Satisfactory (SML 3.12 / UE 5.6.1-CSS): dinosaur-class predators with
real native AI and learnable territories, culminating in a Dune-style **Sandwyrm** under the Dune
Desert. Creatures are first-class `AFGCreature` citizens, so save/load, multiplayer, radar, and —
by explicit design — **HostileScaling's stat scaling all work with zero cross-mod dependency**.

## The plan

| Doc | Contents |
|---|---|
| [00 — Vision & pillars](docs/00-vision-and-pillars.md) | Locked decisions, pillars, v1.0 gate, non-goals |
| [01 — Creature roster](docs/01-creature-roster.md) | Sicklejaw · Crestback · Duskstalker · Sandwyrm; stats-by-anchor; loot |
| [02 — Hostility & factions](docs/02-hostility-and-factions.md) | Target policy table, aggression modes, pack assist, worm disturbance targeting |
| [03 — Spawning & territories](docs/03-spawning-and-territories.md) | Real `AFGCreatureSpawner` nests, seeded curated regions, persistence, worm event loop |
| [04 — Config & UI](docs/04-config-and-ui.md) | Full config tree + the ModConfiguration-vs-SessionSettings decision |
| [05 — Asset pipeline](docs/05-asset-pipeline.md) | Per-creature recipe: generate → rig → animate → texture → import; worm procedural rig |
| [06 — Integration & compat](docs/06-integration-and-compat.md) | The HostileScaling zero-dependency contract (verified from its source) + verification matrix |
| [07 — Technical architecture](docs/07-technical-architecture.md) | Class map, zero-hook/zero-transformer principle, borrowed-BT strategy, diagnostics contract |
| [08 — Implementation plan](docs/08-implementation-plan.md) | Packets, phases, in-game test checklists, risk register |

Reading order for implementers: 00 → 07 → 08, then the domain doc for your packet.

## Headline design facts (why this plan is buildable)

- Satisfactory ships a **native creature-action library** (`UFGCreatureActionMeleeAttack`,
  `…Charge`, `…LeapAttack`, `…Roar`, …) — our attacks are configured, not coded.
- `AFGCreatureSpawner` is a **runtime-placeable navigation invoker** with SaveGame kill-state —
  our nests are real spawners, indistinguishable from vanilla to other mods.
- Targeting is controller-virtual (`CanBeHostileTowards` et al.) — the whole faction policy lives
  in **our own controller subclass: zero SML hooks, zero AccessTransformers planned**.
- HostileScaling is generic over `AFGCreature`/`AFGCreatureSpawner` (verified in its source) —
  compatibility is structural, not coded.

Workspace law applies: build lock, cold review before user tests, differential review on
path-adjacent changes, diagnostics everywhere (`LogPrimalFauna`, `[PFA]`).
