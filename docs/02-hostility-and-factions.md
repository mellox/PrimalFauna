# 02 — Hostility & Factions

The user's core ask: creatures hostile to players **and possibly other creatures**, with systems to
facilitate it. This doc is the target policy. All class/line references were verified against the
real 5.6.1-CSS headers on 2026-07-11 (`Source/FactoryGame/Public/…`); re-verify after game updates.

## 1. The machinery we build on (verified)

- Targeting is **not** interface-based and **not** class-based: `AFGEnemy` is an empty marker
  (`Creature/Enemy/FGEnemy.h:12` — ctor only). The controller `AFGCreatureController : AAIController`
  (`AI/FGCreatureController.h:148`) tracks plain `AActor*` targets in `mAggroTargets`
  (`FFGCreatureAggroData{Target, Aggro, IsVisible}` :48) and exposes **virtual gates**:
  - `virtual bool CanBeHostileTowards(const AActor*) const` :196 — the master hostility decision
  - `IsActorConsideredThreat(const AActor*)` :199 (BlueprintNativeEvent)
  - `virtual bool IsValidTarget(const AActor*) const` :204
  - `CanBeAlertedByTarget(const AActor*)` :387 (BNE, "runs CanBeHostileTowards by default")
- **We own our creatures' controller class**, so the entire faction policy is plain C++ virtual
  overrides in `APFACreatureController` — **no SML hooks needed for our own creatures.** Hooks would
  only enter if we wanted to change *vanilla* creatures' targeting (we don't, v1).
- Native adjacent systems we must respect:
  - Per-player passivity (the vanilla "creature hostility" advanced setting):
    `EPlayerHostilityMode {Default, Passive, Retaliate}` on `AFGPlayerState::mCreatureHostilityMode`
    (`FGPlayerState.h:958`, `FGCreatureSubsystem.h:30`). Enforced somewhere in the binary via
    `CanBeHostileAgainstPlayer` (`FGCreatureController.h:438`, private).
  - Per-creature passivity: `AFGCreature::mIsPassiveCreature` (`FGCreature.h:659`,
    `IsPassiveCreature()` :303 — "can't aggro on anything"). **HostileScaling reads this per
    instance** to decide whether to scale — our Aggression=Passive mode must set it (doc 06 §2).
  - Creature-vs-creature master switch: `AFGCreatureSubsystem::CanCreaturesAttackEachother()`
    (`FGCreatureSubsystem.h:125`, field `mCreaturesCanAttackEachother` :229, "uses family system"
    with `UFGCreatureFamily`, `FGCreature.h:623`).
  - Protected species: `AFGCreatureSubsystem::IsProtectedCreature(TSubclassOf<AFGCreature>)` :132
    (`mProtectedCreatures` :236) — the game's own never-target list (doggo-class fauna).

## 2. The target policy (decision table)

`APFACreatureController::CanBeHostileTowards(Target)` evaluates in order; first match wins.
Every evaluation logs at Verbose: `[PFA] <creature> vs <target>: <rule> -> <verdict>`.

| # | Target is… | Verdict | Notes |
|---|---|---|---|
| 1 | A tamed creature (`IsTamed()`) | **never** | Hardcoded. Not config. |
| 2 | A protected class (`IsProtectedCreature(...)`) | **never** | Respect the game's list — covers doggos regardless of our config. |
| 3 | Our own species (same class) | **never** | Packmates. |
| 4 | Another PrimalFauna species | **never (v1)** | Cross-species PrimalFauna conflict is a post-1.0 flavor option; keep the matrix trivial first. |
| 5 | A player (`AFGCharacterPlayer`) | **defer to `Super::CanBeHostileTowards`**, then apply our species gate (§3) | Super preserves the vanilla per-player Passive/Retaliate setting and any binary-side rules we can't see. We only ever make the result MORE restrictive, never less. |
| 6 | A vanilla creature, `IsPassiveCreature()` false | config `HuntVanillaHostiles` (default true) | The infighting spectacle: dinos vs hogs/stingers. |
| 7 | A vanilla creature, passive | config `HuntPassiveFauna` (default false) | Off by default; rule 2 still shields protected species even when on. |
| 8 | Anything else (vehicles, buildings, unknown) | **defer to Super** | Don't invent new target categories; vanilla behavior is the safe default. |

`IsValidTarget` and `CanBeAlertedByTarget` overrides apply the same table (alerting additionally
gated by the species aggression mode below). **Never override `CanBeHostileAgainstPlayer`
semantics** — it's private and binary-enforced; our player gate composes on top via rule 5.

## 3. Species aggression modes (config `Aggression`, doc 04)

| Mode | Meaning | Implementation |
|---|---|---|
| 0 Passive | Exists, never initiates, never retaliates | Set the instance's `mIsPassiveCreature = true` (AccessTransformer `Friend`; per-instance write — the CDO stays authored-hostile). This makes the game's own systems AND HostileScaling treat it as passive consistently. Verify in-game that flipping it live cleanly de-aggros (call `NullifyAllAggro()` :280 on transition). |
| 1 Territorial | Initiates only inside its territory, or after being damaged by the target | In the rule-5/6/7 gate: `if !InTerritory(Target) && !WasRecentlyDamagedBy(Target) -> false`. Territory = distance to `GetSpawner()` (`AFGCreature::mOwningSpawner`, `FGCreature.h:699`) ≤ territory radius (per-species constant, ~1.5× spawner `mSpawnRadius`). Recent damage from the controller's `mAggressors`/`FFGRecentDamageTaken` (:483, :115). Worm region logic is separate (§6). |
| 2 Hostile | Initiates on detection | Gate passes; native perception/aggro drives the rest. |

Retaliation in modes 1–2 is native (damage feeds aggro/`mAggressors`); mode 0 suppresses it by
design (that's what "passive" means to the game — document in the tooltip).

Live config changes: the subsystem refresh tick pushes mode changes to live creatures
(set/clear `mIsPassiveCreature`, `NullifyAllAggro()` when moving to a stricter mode).

## 4. Pack behavior (Sicklejaw showcase; config `PackAssist`)

When a pack member enters Fight state, `APFACreatureController::OnCreatureStateChanged_Implementation`
notifies living same-spawner packmates. Idle/default or roaming PFA controllers receive the leader's
current (or highest-aggro fallback) target through `ForceAggro`, which first uses the native target
registration path, sets aggro just above the packmate's fight threshold, explicitly binds that actor
as the native current target and focus, and then requests one immediate state
update. The aggregate separates native registration/aggro-write acceptance from `joined`, which
requires Fight state, the exact current target, and the requested aggro readback. Already alert,
fighting, investigating, or fleeing packmates are left to the native state machine. One Display line
records leader, spawner, target, candidate, accepted, and measured joined counts,
plus decision and outcome. Per world, `PFA.SelfTest` reports `PackAssistCoverage` as a failing
`NO-COVERAGE` until an assist wave with eligible candidates occurs. It then consumes the immediate
wave evidence and fails if any candidate did not enter Fight with the exact assisted target and
sufficient aggro; already-active fighters are deliberately excluded because native combat may keep
them on another valid target. No hooks — all
classes are ours and the shared spawner relationship is native.

## 5. Perception tuning

Per species via `FFGCreaturePerceptionSettings` (`FGCreature.h:74-97`: instanced
`UAISenseConfig_Sight/Hearing` + `ImmediateVisibilityRadius`) constructed in the species C++ ctor.
Sicklejaw: wide sight, strong hearing (sprint noise attracts). Crestback: short sight, big
immediate radius (you're fine until you're IN it). Duskstalker: long night sight (dial
sight config's lighting affinity only if the sense config exposes it — otherwise same sight,
activity gated by time instead; the flashlight-flee flavor rule is EXPERIMENTAL-flag material,
cut-eligible). Aggro pacing via the creature's native `mAggroGainRate`/curves (`FGCreature.h:595-619`).

## 6. Sandwyrm targeting — disturbance, not perception

The worm doesn't use controller perception at range (it's underground; sight is meaningless).
`APFAWormDirector` (per-region manager inside our subsystem, doc 03 §7) scores **disturbance** in
the region: players sprinting/driving (weight high), operating buildings (weight per building,
capped), explosions (spike). Highest-scoring cluster wins; the emergence point is the target's
predicted position. Config `EventFrequency` scales score accumulation. During the surface window
the worm's controller uses the normal table (§2) with a huge `ImmediateVisibilityRadius` — everyone
near it is a valid target. Building damage on emergence is radial damage with
`DamageCauser = worm actor` and `InstigatedBy = worm controller` so both the vanilla pipeline and
HostileScaling's damage hook classify it correctly (doc 06 §2), gated by config `DamageBuildings`.

## 7. Statically unverifiable items (differential-review carryover → in-game checklist)

The `.cpp` sources are SML stubs — these behaviors are binary-side and MUST be runtime-verified in
the slice phase (doc 08 test checklists):

1. **Does the native `mCreaturesCanAttackEachother` gate veto our creature-vs-creature targets**
   even when our `CanBeHostileTowards` says yes? Test: dino vs hog with the toggle untouched.
   Fallback if vetoed: call `SetCreaturesCanAttackEachother(true)` while `HuntVanillaHostiles` is
   on (side effect: vanilla infighting also enabled — acceptable, document in tooltip) — or
   drive aggro directly via `SetAggroForTarget` which may bypass the gate. Instrument BOTH paths
   from the first build ([PFA] logs on gate evaluation + target acquisition).
2. **Do vanilla creatures retaliate against ours** (their aggro-on-damage picking our creature as
   target)? Expected yes (plain-AActor aggro data). If not: acceptable v1 degradation (one-sided
   hunts), log it, revisit.
3. **Does per-instance `mIsPassiveCreature = true` fully suppress initiation AND retaliation** on a
   live creature (mode 0)? Includes HostileScaling skipping it (its log line disappears for that
   creature).
4. **Does `Super::CanBeHostileTowards` on a player correctly honor the vanilla per-player
   Passive/Retaliate setting** for OUR species? Test with the advanced game setting flipped.
