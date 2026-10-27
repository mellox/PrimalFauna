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

**Our subclass `APFANestSpawner : AFGCreatureSpawner`** exists for: identification
(`IsA<APFANestSpawner>` — cleanly separates ours from vanilla for diagnostics/adoption), region/nest
id fields (SaveGame), and species-config despawn behavior. It must NOT change spawn mechanics —
that keeps HostileScaling's generic `TActorIterator<AFGCreatureSpawner>` treatment (doc 06 §3) and
radar/scanner behavior identical to vanilla. ⚠ This is a path-adjacent subclass: the cold review
for its packet runs the differential checklist (vanilla-spawner invariants vs our subclass).

## 2. Territory data (authored, seeded, curated)

A **territory region** = named area + species mix. Baked as a C++ static table (v1; a DataTable
asset is an acceptable alternative if the editor is open anyway — decide in the packet):

```
FPFARegion { FName RegionId; FString Label;            // "DuneDesert_Fringe"
            TArray<FPFADiskArea> Areas;                 // {FVector Center(with GROUND Z), float Radius}
            TArray<FPFANestSpec> Nests; }               // per species: class, nest count min..max,
                                                       //   creatures/nest (FInt32Interval), spawn radius,
                                                       //   day/night flags, respawn days
```

- **Ground-accurate Z is baked at authoring time** so injection needs no runtime terrain raycast —
  this dodges the entire NodeShuffle unstreamed-terrain problem (spawners at valid ground Z are
  inert until a player streams them in; the navmesh invoker handles the rest locally).
- **Authoring workflow (packet W2.1, mirrors Satismagictory's site-authoring):** a dev console
  command (`PFA.DumpCandidates <RegionId>`) samples the region areas, line-traces ground Z, probes
  water (`AFGWaterVolume::EncompassesPoint` — reject), logs candidates + teleports the player
  through them for eyeballing. Curation session with the user picks final areas; results baked into
  the table. Starter-plain exclusion zones (Grass Fields spawn shores) are part of the table, not code.
- **Seeded selection:** per save, `UPROPERTY(SaveGame) int32 mNestSeed` (rolled once on first
  activation) drives which candidate points inside each area become nests and the per-nest count
  roll — every save gets a stable but distinct layout. `NestDensity` config scales the nest count
  roll (0.25–3.0), clamped to the area's candidate capacity.

## 3. Injection & idempotence (the subsystem's job)

`APFASubsystem` (server-only, `AModSubsystem`, 10 s refresh tick — HostileScaling cadence):

1. **Adopt:** scan world for existing `APFANestSpawner`s (they persist in the save — §4), match to
   the registry by `NestId`, adopt state.
2. **Materialize missing:** for each registry nest with no live spawner: `SpawnActor<APFANestSpawner>`
   with **deterministic stable name** (`FActorSpawnParameters::Name = "PFANest_<RegionId>_<idx>"`) —
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
   `[PFA] Nest <id> suppressed: player base within <d>m`. Re-check on refresh so building near a
   nest eventually (next session) retires it — the world yields to the factory, like vanilla.
5. **Log census every load (Display):** `[PFA] Regions: N, nests live: X (by species: …), suppressed: Y`.
   Per-nest lines at Verbose.

## 4. Persistence model — native actor persistence + explicit runtime-config mirror (corrected 2026-08-16)

**What the Phase-1 gate proved (build w15-3):** dynamically spawned `APFANestSpawner` *actors* and
their `mSpawnData` records persist natively (`records=4 preexisting-live=4`, census `adopted=4
materialized=0`), with no duplicate actor/name/count compounding.

**What it failed to prove, and the user falsified on 2026-08-16:** living creatures are ordinary
distance-managed actors and do not necessarily serialize with the spawner. More importantly, the
base spawner's authoring fields (`mCreatureClass`, `mNumberOfCreatures`, day/night flags,
`mSpawnRadius`, `mRespawnTimeIndays`) are *not* SaveGame; level-placed vanilla spawners recover them
from their archetype, but a runtime C++ nest reloads with CDO defaults. The saved spawn slots then
have no usable creature class and cannot repopulate. Actor adoption alone was therefore a false
positive for functional persistence.

**Corrected contract:** `APFANestSpawner` mirrors PFA's authored base configuration into its own
SaveGame fields, restores it **before** the base `PostLoadGame` recouple path, and runs a one-shot
adoption check. The snapshot is deliberately not refreshed from live fields during `PreSaveGame`:
HostileScaling owns session-transient respawn scaling and starvation radius widening, so those must
reset to PFA's authored baseline when it is disabled or absent on the next load. Existing pre-fix
saves are repaired from the durable nest registry without replacing a non-empty saved `mSpawnData`;
their exact effective slot count is preserved for the legacy recovery session, including
HostileScaling. Only a genuinely empty legacy slot array is populated. The
`NestSpawnerRuntimeReady` self-test requires snapshot + creature class + non-empty slots.

### (original assumption text, superseded)

**Original intended assumption:** runtime-spawned `APFANestSpawner`s are SAVED by the game (the class implements
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

## 7. Sandwyrm event system (proposed global singleton — no pack allocation)

**2026-10-14 requirement:** one saved roaming individual across desert biomes. The detailed
model is being authored; worm gameplay/director is not implemented. The earlier one-director/
one-actor-per-region design is superseded. One authoritative director record belongs to the
subsystem; region membership is routing/target context, never an independent spawn budget.
Provisional art targets are 180 m body / 30 m shaft / 38 m external mouth-lobe span (36–40 m) / 26 m clear bore (doc 01 §4), pending
real miner-envelope and deformation measurements; editor placeholder dimensions are rejected.

Proposed behavior states, independent of whether a world actor is currently materialized:

```
DORMANT (death/respawn policy to be decided; retain individual identity)
  → STALKING  underground or partially exposed route through eligible desert terrain
  → TELEGRAPH target locked: rumble audio at distance → moving dust-trail VFX + decal toward target
              (timing to test); abort → STALKING if the target/route becomes invalid
  → ATTACK    emerge beneath a miner OR arch down onto it; player/creature swallow variants
              actual engulf volume, damage/consumption timing, attackable body and sand effects
  → SUBMERGE  burrow VFX, cooldown scoring reset → STALKING (or DORMANT if killed)
```

**§7a — durable identity is not permanent actor residency.** Save the unique identity, route/biome,
position, random state, health, death/respawn timing, phase, event serial and drop entitlement.
On load, adopt the matching live actor before considering materialization. If an actor has been
unloaded, re-materialize the same individual/state only through the reviewed native-compatible
route. Never replace unloading with a fresh healthy worm, duplicate a live body, or replay a
completed swallow/drop. Revalidate targets and incomplete transactions after reload.

Native spawn/despawn, streaming, orphan cleanup and actor persistence need a deliberate worm
policy. `SetIsPersistent(true)` is a candidate mechanism, **not a settled requirement**. Header
presence is insufficient for shipping APIs; `ClearKillOrphanTimer` has a recorded export trap
(doc 07 §1), so the earlier mandatory call is withdrawn. Native creature creation/eligibility
and save integration must be verified, not bypassed by an untracked actor spawn.

Distance/streaming decisions must consider the **whole body and its swept attack extent**, every
nearby player, visible segments, combat and active transactions. A center can be far away while
the mouth or tail is beside a player. Do not impose the native hostile-base 100 m/120 m distances
as a worm policy without that review, or globally change them. Retain native rules wherever
compatible with the verified worm policy; ordinary Sicklejaw spawn/despawn behavior is unchanged.
Compare actor residency and safe re-materialization alternatives with measured runtime evidence.

Custom movement/pose design (doc 07 §7) must support the complete body through terrain rather
than turning a permanently hidden patrol point into unrelated eruption actors. Safe transitions
between desert areas, attack cancellation and save/load behavior remain implementation gates.

**Worm testability rule (diagnostics-first):** every state transition logs at Display with score
snapshot: `[PFA] Wyrm: STALKING->TELEGRAPH target=<actor> score=… dist=…`. A dev console command
`PFA.WormForceEvent` is currently a stub; the proposed implementation should select testable
attack variants without waiting for disturbance scores and log the same individual/event IDs.

## 8. Multiplayer

All of the above is server-authoritative (subsystem + directors run under `HasAuthority()`).
Creatures/spawners replicate natively (vanilla machinery). Worm telegraph VFX/audio must be
multicast or replicated-state-driven so clients see the dust trail — packet-level detail, flagged
for the MP test matrix.
