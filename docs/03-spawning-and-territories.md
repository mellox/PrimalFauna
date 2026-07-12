# 03 — Spawning & Territories

Locked decision D4: biome nests/territories — persistent, seeded, vanilla-spawner-like. The worm is
event-driven (§7). Header references verified 2026-07-11.

## 1. Why real `AFGCreatureSpawner`s (not a custom spawn loop)

`AFGCreatureSpawner` (`Creature/FGCreatureSpawner.h:52`, `AActor + IFGSaveInterface`) natively
provides everything pillar 1 wants:

- **Per-slot persistence:** `mSpawnData : TArray<FSpawnData>` is SaveGame (:225) with per-creature
  `WasKilled`, `NumTimesKilled`, `KilledOnDayNr` (:31-43) — kill-state and respawn bookkeeping for free.
- **Respawn cadence:** `mRespawnTimeIndays` (:263, int32, -1 = never) — same field HostileScaling
  already scales (doc 06 §3).
- **Activation by proximity:** spawners idle until players near; creatures despawn far
  (`mSpawnDistanceOverride`/`mDespawnDistanceOverride` :241-245, subsystem `ManageCreatures`).
- **Navmesh on demand:** spawners are UE **navigation invokers** (`RegisterAsNavigationInvoker(bool)`
  :182) — navmesh generates around them, and spawn placement waits for it
  (`FSpawnData::WaitingForSpawnLocation` :46). Runtime placement is therefore first-class, not a hack.
- **Day/night gating:** `mCanSpawnDuringDay`/`mCanSpawnDuringNight` (:209-213) — Duskstalker's
  night bias at the spawn level, no custom clock code.
- **Ecosystem integration:** spawners register with `AFGCreatureSubsystem` (delegates
  `mOnSpawnerRegistered` `FGCreatureSubsystem.h:213`) and radar towers list fauna via spawners
  (`AFGBuildableRadarTower::TryAddCreatureSpawner` `FGBuildableRadarTower.h:113`).
- **Anti-grief hint:** `mCachedIsNearBase` (SaveGame :237) — the game already tracks
  near-player-base state for spawners (expected: suppresses respawn near factories; binary-side,
  verify in slice testing).

**Our subclass `APFNestSpawner : AFGCreatureSpawner`** exists for: identification
(`IsA<APFNestSpawner>` — cleanly separates ours from vanilla for diagnostics/adoption), region/nest
id fields (SaveGame), and species-config despawn behavior. It must NOT change spawn mechanics —
that keeps HostileScaling's generic `TActorIterator<AFGCreatureSpawner>` treatment (doc 06 §3) and
radar/scanner behavior identical to vanilla. ⚠ This is a path-adjacent subclass: the cold review
for its packet runs the differential checklist (vanilla-spawner invariants vs our subclass).

## 2. Territory data (authored, seeded, curated)

A **territory region** = named area + species mix. Baked as a C++ static table (v1; a DataTable
asset is an acceptable alternative if the editor is open anyway — decide in the packet):

```
FPFRegion { FName RegionId; FString Label;            // "DuneDesert_Fringe"
            TArray<FPFDiskArea> Areas;                 // {FVector Center(with GROUND Z), float Radius}
            TArray<FPFNestSpec> Nests; }               // per species: class, nest count min..max,
                                                       //   creatures/nest (FInt32Interval), spawn radius,
                                                       //   day/night flags, respawn days
```

- **Ground-accurate Z is baked at authoring time** so injection needs no runtime terrain raycast —
  this dodges the entire NodeShuffle unstreamed-terrain problem (spawners at valid ground Z are
  inert until a player streams them in; the navmesh invoker handles the rest locally).
- **Authoring workflow (packet W2.1, mirrors Satismagictory's site-authoring):** a dev console
  command (`PF.DumpCandidates <RegionId>`) samples the region areas, line-traces ground Z, probes
  water (`AFGWaterVolume::EncompassesPoint` — reject), logs candidates + teleports the player
  through them for eyeballing. Curation session with the user picks final areas; results baked into
  the table. Starter-plain exclusion zones (Grass Fields spawn shores) are part of the table, not code.
- **Seeded selection:** per save, `UPROPERTY(SaveGame) int32 mNestSeed` (rolled once on first
  activation) drives which candidate points inside each area become nests and the per-nest count
  roll — every save gets a stable but distinct layout. `NestDensity` config scales the nest count
  roll (0.25–3.0), clamped to the area's candidate capacity.

## 3. Injection & idempotence (the subsystem's job)

`APFSubsystem` (server-only, `AModSubsystem`, 10 s refresh tick — HostileScaling cadence):

1. **Adopt:** scan world for existing `APFNestSpawner`s (they persist in the save — §4), match to
   the registry by `NestId`, adopt state.
2. **Materialize missing:** for each registry nest with no live spawner: `SpawnActor<APFNestSpawner>`
   with **deterministic stable name** (`FActorSpawnParameters::Name = "PFNest_<RegionId>_<idx>"`) —
   stable names keep `GetPathName()` stable across sessions, which HostileScaling uses as its
   baseline key (doc 06 §3). Configure from the nest spec (`mCreatureClass`, `mNumberOfCreatures`,
   `mSpawnRadius`, day/night, `mRespawnTimeIndays`), then `RegisterAsNavigationInvoker(true)` and
   `PopulateSpawnData()` (:160). **Set spawn data ONCE at creation; never rewrite counts afterward**
   (doc 06 §3 — count scaling belongs to HostileScaling).
3. **Deactivate surplus/disabled:** species disabled in config or density lowered → mark nest
   dormant: `StopSpawning()` (:138) + `DestroyCreatures()` (:144) + skip in respawn; registry keeps
   it for re-enable. Never `Destroy()` the spawner actor mid-session gratuitously (churn); orphaned
   spawners from a *removed region* (plan version bump) are destroyed explicitly and logged.
4. **Anti-grief check at materialization:** skip/relocate a nest if player buildables exist within
   `NestClearRadius` (~150 m) of it (query via the buildable subsystem); log
   `[PF] Nest <id> suppressed: player base within <d>m`. Re-check on refresh so building near a
   nest eventually (next session) retires it — the world yields to the factory, like vanilla.
5. **Log census every load (Display):** `[PF] Regions: N, nests live: X (by species: …), suppressed: Y`.
   Per-nest lines at Verbose.

## 4. Persistence model ⚠ [assumption → slice test]

**Intended:** runtime-spawned `APFNestSpawner`s are SAVED by the game (the class implements
`IFGSaveInterface`; dynamically spawned save-interface actors persist like buildings do), so
`mSpawnData` kill-state/respawn-days survive reload natively, and creatures re-link via
`TryRecoupleCreatureAndSpawner()` (:175) / orphan-kill otherwise (`FGCreature.h:663`).

This is exactly the class of "new path through closed-source machinery" the differential review
exists for. Slice-phase test items (doc 08): spawner survives save/load with kill-state intact;
no duplicate spawner after reload (adoption works); creatures re-couple; mod-removed → save loads
clean (spawner class missing = actor dropped by the save system — verify no crash, creatures
orphan-despawn); mod re-added → registry re-materializes.

**Fallback if dynamic spawners do NOT persist correctly:** flip to NodeShuffle's transient model —
spawners `RF_Transient`, re-materialized every load, with kill-state/respawn-days mirrored into our
own `UPROPERTY(SaveGame)` registry and pushed into `mSpawnData` after `PopulateSpawnData()`. More
code, fully under our control. The registry structure in §3 is designed so this swap changes the
materialize step only.

## 5. Density semantics (compose with HostileScaling, don't fight it)

- **Our `NestDensity` = how many nests exist.** Applied at materialization; never touches a live
  spawner's `mSpawnData` count.
- **HostileScaling's spawn multiplier = creatures per nest + respawn speed.** It snapshots vanilla
  counts per spawner path and rewrites them idempotently — our stable names + write-once counts
  make our nests indistinguishable from map spawners to it. Both knobs compose multiplicatively
  with zero coordination. (Full contract: doc 06 §3.)

## 6. Live creature lifecycle rules

- Creatures spawn owned by their nest (`mOwningSpawner` set by the spawner path) → despawn-when-far,
  respawn-per-days, orphan-cleanup all native.
- Config species-disable despawns via the nest (§3.3), not by iterating creatures — one owner.
- `Aggression` changes retarget live creatures (doc 02 §3) but never despawn them.

## 7. Sandwyrm event system (its own phase — no nests)

One `APFWormDirector` per worm region (Dune Desert v1; Rocky Desert optional), state machine
driven from the subsystem tick, ALL state SaveGame:

```
DORMANT (cooldown/respawn wait, config RespawnDays after a kill)
  → STALKING  patrol point wanders region; disturbance scoring (doc 02 §6) accumulates per actor cluster
  → TELEGRAPH target locked: rumble audio at distance → moving dust-trail VFX + decal toward target
              (5–10 s, speed readable); abort → STALKING if target leaves region
  → EMERGE    at target point: radial damage (players + config-gated buildings) + physics launch
              + eruption VFX/audio; the worm ACTOR surfaces (§7a) — attackable window 8–15 s
  → SUBMERGE  burrow VFX, cooldown scoring reset → STALKING (or DORMANT if killed)
```

**§7a — one persistent worm actor per region** (not spawned per event): `APFSandwyrm` is an
`AFGCreature` subclass (identity/health/damage standard — locked, doc 01 §4) with
`SetIsPersistent(true)` (`FGCreature.h:477` — exempts it from the despawn budget; verify) and
orphan-timer cleared (`ClearKillOrphanTimer()` :438) since it has no spawner. Between events it
sits buried (collision off, hidden, invulnerable-by-position — NOT god-mode: being unreachable is
the protection; health persists so a wounded worm stays wounded across events — deliberate design).
Movement is a custom movement component in custom mode driven kinematically by the director
(doc 07 §7); it never touches navmesh. Direct spawn path if ever needed:
`AFGCreatureSubsystem::BeginSpawningCreature(class, transform)` (`FGCreatureSubsystem.h:162`).

**Worm testability rule (diagnostics-first):** every state transition logs at Display with score
snapshot: `[PF] Wyrm: STALKING->TELEGRAPH target=<actor> score=… dist=…`. A dev console command
`PF.WormForceEvent` triggers a hunt at the player for testing without waiting on scores.

## 8. Multiplayer

All of the above is server-authoritative (subsystem + directors run under `HasAuthority()`).
Creatures/spawners replicate natively (vanilla machinery). Worm telegraph VFX/audio must be
multicast or replicated-state-driven so clients see the dust trail — packet-level detail, flagged
for the MP test matrix.
