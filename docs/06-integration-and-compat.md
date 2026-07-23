# 06 — Integration & Compatibility

Locked D5/D6: PrimalFauna creatures are scaled by HostileScaling **exactly like vanilla creatures**,
with **zero dependency in either direction**. This works structurally, not by coordination: both
mods speak "vanilla creature." HostileScaling's mechanism below was read from its actual source
(`HostileScaling/Source/.../HostileScaling.cpp`, `HostileScalingSubsystem.cpp`) on 2026-07-11.

## 1. How HostileScaling discovers and scales creatures (verified, not assumed)

- **Health:** `SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGCreature, BeginPlay)` + a 10 s
  `TActorIterator<AFGCreature>` sweep call `ApplyCreatureHealthScale(AFGCreature*)`. It **skips**
  `IsPassiveCreature()` / `IsTamed()` instances, reads the **CDO's** default health
  (`GetClass()->GetDefaultObject<AFGCreature>()->GetHealthComponent()->mMaxHealth`), writes
  `instance = vanillaCDO × multiplier`, preserving the current-health fraction. Idempotent.
- **Damage:** hooks the three `UFGHealthComponent` entry points (`TakeDamage` / `TakePointDamage` /
  `TakeRadialDamage`) and multiplies when **(damageCauser OR instigator's pawn) casts to a hostile
  `AFGCreature`** and the damaged actor is a player (or building, config-gated).
- **Spawners:** a `TActorIterator<AFGCreatureSpawner>` sweep, keyed by `GetPathName()`, snapshots
  the first-seen (assumed vanilla) `mRespawnTimeIndays` / `mSpawnData.Num()` into SaveGame maps,
  then idempotently applies: respawn days ÷ multiplier; spawn count grown toward
  `round(vanilla × multiplier)` by cloning existing `FSpawnData` entries.
- **No class lists anywhere.** Everything is generic over `AFGCreature`/`AFGCreatureSpawner`.

## 2. Our obligations — the creature contract

| # | Obligation | Where enforced |
|---|---|---|
| C1 | Every creature derives `AFGCreature` (via `AFGEnemy`), **including the Sandwyrm** | doc 01 §4 locked; doc 07 §2 |
| C2 | Standard `UFGHealthComponent`; base health authored **on the CDO** (`SetMaxHealth` in ctor). **Never multiply instance health at runtime** — HostileScaling's CDO-based reconcile would fight it, and SaveGame health would compound. Stat variants (future "Alphas") = separate classes with their own CDO values. | doc 07 §3 |
| C3 | All outgoing damage flows through the standard pipeline with **`DamageCauser` = the creature (or its projectile) and `Instigator` = its controller** — native `UFGCreatureAction*` attacks do this; the worm's radial damage sets both explicitly. Otherwise HostileScaling's damage multiplier silently skips us. | doc 07 §5, §7 |
| C4 | Our per-species `DamageMultiplier` (config) applies when **defining** attack damage on our action objects — a different layer than HostileScaling's entry-point multiply. Expected in-game math: `final = anchor × PFA.DamageMultiplier × HS.DamageMultiplier`. | doc 04 §1 |
| C5 | Config Passive mode is implemented via the instance's `mIsPassiveCreature` — so HostileScaling *correctly stops scaling* a creature the user made passive, same as doggos. | doc 02 §3 |
| C6 | Creature-vs-creature damage is **not** scaled by HostileScaling (its target gate is players/buildings) — our infighting balance is ours alone. Documented so nobody "fixes" it. | doc 02 §2 |

## 3. Our obligations — the spawner contract

| # | Obligation | Why |
|---|---|---|
| S1 | Nest spawners get **deterministic stable actor names** (`PFANest_<Region>_<idx>`) at spawn | HostileScaling keys its vanilla-baseline snapshots by `GetPathName()`. An unstable name re-baselines from SAVED (already-scaled) `mSpawnData` next session → **compounding spawn counts**. Stable path = stable baseline. |
| S2 | Write `mSpawnData`/`mNumberOfCreatures`/`mRespawnTimeIndays` **once at nest creation, with our true (unscaled) values — then never rewrite counts** | Two mods converging on the same field oscillates (we shrink, it grows, every 10 s). One writer per field: creation values are ours, ongoing scaling is HostileScaling's. |
| S3 | Our `NestDensity` config scales **how many nests exist**, never per-spawner counts | Composes multiplicatively with HostileScaling's per-spawner count scaling, zero interference. |
| S4 | Nest deactivation uses `StopSpawning()`/`DestroyCreatures()` + registry state, not count-zeroing | Count writes are forbidden by S2. |

## 4. Verification matrix (both-ways, in-game — doc 08 gates)

`PFA.DumpCreature` prints class, CDO base health, live current/max health, active multiplier math —
so every check below is one console command + one log read.

| Scenario | Expected |
|---|---|
| HS present+enabled, tier > 0 | Live Sicklejaw max HP = CDO anchor × HS health mult (log shows both numbers). HS damage to player scales (compare hit numbers vs config). Nest respawn days shrink per HS log. |
| HS present, disabled mid-save | Within one HS sweep (~10 s) our creatures reconcile back to CDO values. |
| HS absent | Our creatures at CDO values; zero errors; nothing in our code references HS symbols (grep gate at review). |
| HS added mid-save (existing PrimalFauna nests) | Baselines snapshot correctly (S1/S2), no compounding after 2 save/load cycles (count stays `round(vanilla×mult)`, not growing). |
| PrimalFauna removed, HS stays | Save loads; vanilla behavior intact. |
| Both + NodeShuffle | Coexistence smoke: no scan interference (different actor families), no shared hooks. |

The "2 save/load cycles" compounding check is mandatory — compounding is invisible in a single
session (the same trap class as [[sml-gotchas]] §6).

## 5. Broader compatibility notes

- **Vanilla passive-creatures setting (per-player `EPlayerHostilityMode`):** respected via
  `Super::CanBeHostileTowards` deferral (doc 02 §2 rule 5). Test with the setting flipped.
- **Protected creatures list:** honored (doc 02 rule 2) — future game/mod additions to
  `mProtectedCreatures` protect themselves from us automatically.
- **WeaponUpgrades / combat mods:** player→creature damage arrives via the standard pipeline;
  nothing special. Our creatures' stun thresholds (`mStunDamageThreshold`) tuned in balance pass.
- **Creature-modifying mods (e.g. Arachnophilia-class mixins):** they target vanilla creature BPs;
  our classes are unknown to them — inert by construction. Any mod that iterates `AFGCreature`
  generically (like HostileScaling) will see ours — that's the intended citizenship.
- **Arachnophobia mode:** our species set `mIsArachnid = false`; spawner
  `mCreatureClassArachnidOverride` untouched. QA item only.
- **Dedicated server:** all logic server-side; package includes server targets (multi-target
  `package_release.ps1`). Worm telegraph replication is the one client-visual risk (doc 03 §8).
- **Game updates:** re-verify the header cites in docs 02/03/07 (the skill's update guide);
  the contract itself is update-resilient — if HostileScaling's generic mechanism breaks against
  us, it broke against vanilla creatures too.
