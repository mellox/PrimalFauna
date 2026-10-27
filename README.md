# PrimalFauna — hostile megafauna for Satisfactory

**Status (2026-10-13): in active development, not yet released on ficsit.app.** One creature,
the **Sicklejaw** pack predator, spawns and fights in play, with authored animations, nests, loot
and a world-population layer that is on by default (toggle `EnableWorldPopulation`); the
population layer's in-game acceptance is still pending. The other three creatures in the design
docs (Crestback, Duskstalker, Sandwyrm) have no source or content yet. The installed build marker
is `2026-09-28-w60-1`.

Built against SML `^3.12.0`; the plugin manifest declares `GameVersion >=491125`. Creatures are
first-class `AFGCreature` citizens with real `AFGCreatureSpawner` nests and **no cross-mod
dependency on HostileScaling in either direction**. Save/load persistence and HostileScaling's
handling of our nests have been exercised in play. Multiplayer is untested and has a known gap
(the bite strike is server-only and not replicated); radar listing is untested.

## What exists today

- **Sicklejaw** (`Source/PrimalFauna/Private/Species/`, `Content/Sicklejaw*`): a native-AI pack
  predator built on the shared vanilla hostile behaviour tree, with authored locomotion, attack
  and death clips, a tail additive, a non-ragdoll authored death collapse, remains and loot drops,
  and a dedicated surface material.
- **Controller, factions and census** (`PFACreatureController*`, `PFAFactionConfig`,
  `PFAEngagementCensus`): targeting policy, aggro decay, dead-target drop and pack assist in our
  own controller subclass, with the faction table and the engagement census beside it.
- **Nests** (`PFANestSpawner`): runtime-placed `AFGCreatureSpawner` nests with native
  spawn/despawn distances, kill-state persistence and population budgets.
- **World population** (`PFAWorldPopulation`, `PFAPopulationPolicy`, `PFAPopulationSpawner`,
  on by default, toggle `EnableWorldPopulation`): places packs per biome by classifying sampled
  points against the game's native map-area texture, with resource-site guard packs, wandering
  packs and pack spacing. The policy is engine-independent; the offline tests that exercised it
  during the w59 packets are not yet checked into this repo.
- **Config** (`PFAConfig.h`, 22 settings): master `Enabled`, `NestDensity`, Sicklejaw aggression
  and damage multiplier, `HuntVanillaHostiles`, `HuntPassiveFauna`, `PackAssist`, the world
  population knobs, and `EnableDiagnostics` for the verbose `[PFA]` log.
- **Dev console commands**: 16 `PFA.*` commands registered across the source, among them
  `PFA.SelfTest`, `PFA.SpawnNest`, `PFA.DumpCreature`, `PFA.DumpAllCreatures`,
  `PFA.DumpController`, `PFA.EngagementCensus`, `PFA.CorpseCensus` and `PFA.KillAll`.
  `PFA.WormForceEvent` is registered but is a stub; there is no worm.

## Engine access, measured from the source

The original plan called for zero SML hooks and minimal AccessTransformers. The shipped source
has:

- **Three SML hooks**, all in `PFAPopulationSpawner.cpp`, on the non-virtual
  `AFGCreatureSpawner::IsReadyToSpawn`, `SpawnCreatures` and `SpawnSingleCreature`. They
  short-circuit only nests this mod manages; vanilla spawners and HostileScaling's count and
  respawn ownership are untouched. `CanSpawnCreature` is deliberately not hooked (its shipping
  prologue relocates incorrectly under funchook; see the comment at the hook site).
- **Two `Friend` AccessTransformers** (`Config/AccessTransformers.ini`): `AFGCreature` →
  `APFACreatureBase`, for writes to private spawn/despawn and passive fields on this mod's own
  creatures, and `AFGCreatureController` → `APFACreatureController`, to call the private
  `GetOrRegisterAggroTarget` from our own controller. Neither is applied to vanilla actors. The
  rationale for both is in the ini comments; doc 07 documents only the first grant.

## Building

```powershell
# from the repo root, game CLOSED, no other build running (PowerShell 7; 5.1 refuses the script)
pwsh .\tools\build.ps1
```

`tools/build.ps1` is the only supported build path. It mirrors this repo's `Source/`, `Config/`
and `PrimalFauna.uplugin` into the UBT tree at `SatisfactoryModLoader\Mods\PrimalFauna` (a copy,
not a link), honours the cross-session build lock in `_team/BUILD-LOCK.txt`, disables
WeaponUpgrades for the duration, verifies the build marker inside the deployed DLL, and runs
`tools/check_imports.ps1` against the shipping export table. `Content/` is authored in the
mirror by the headless-editor import scripts and copied back into this repo by hand; the build
only warns when the two copies differ. When the gitignored `art/wip/sicklejaw-d-v2` clip source
is present it also asserts every authored animation clip made it into the cook; otherwise it
prints a NOTE and skips that check. Asset import and inspection scripts live beside it.

## Design docs

| Doc | Contents |
|---|---|
| [00 — Vision & pillars](docs/00-vision-and-pillars.md) | Locked decisions, pillars, v1.0 gate, non-goals |
| [01 — Creature roster](docs/01-creature-roster.md) | Sicklejaw · Crestback · Duskstalker · Sandwyrm; stats-by-anchor; loot |
| [02 — Hostility & factions](docs/02-hostility-and-factions.md) | Target policy table, aggression modes, pack assist, worm disturbance targeting |
| [03 — Spawning & territories](docs/03-spawning-and-territories.md) | Nests, seeded curated regions, persistence, worm event loop |
| [04 — Config & UI](docs/04-config-and-ui.md) | Config tree and the ModConfiguration-vs-SessionSettings decision |
| [05 — Asset pipeline](docs/05-asset-pipeline.md) | Per-creature recipe: generate → rig → animate → texture → import |
| [06 — Integration & compat](docs/06-integration-and-compat.md) | The HostileScaling zero-dependency contract and verification matrix |
| [07 — Technical architecture](docs/07-technical-architecture.md) | Class map, borrowed-BT strategy, diagnostics contract |
| [08 — Implementation plan](docs/08-implementation-plan.md) | Packets, phases, in-game test checklists, risk register |
| [10 — Creature authoring workflow](docs/10-creature-authoring-workflow.md) | Repeatable concept-to-gameplay process, morphology-specific validation, exact-export identity and acceptance evidence |
| [CENSUS](docs/CENSUS.md) · [ASSET-LEDGER](docs/ASSET-LEDGER.md) · [BT-FALLBACK-RECIPE](docs/BT-FALLBACK-RECIPE.md) | Live vanilla creature data, asset provenance, behaviour-tree fallback |

The docs are the design contract and were written before implementation; where this README and
a doc disagree, the source is authoritative and the doc is the intent. Working state and the
per-build handoff log live outside this repo in `_team/primalfauna-state.md`.

Workspace rules apply: build lock, cold review before user tests, differential review on
path-replacing changes, diagnostics everywhere (`LogPrimalFauna`, `[PFA]`).
