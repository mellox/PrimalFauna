# 07 — Technical Architecture

All header cites verified against 5.6.1-CSS (`Source/FactoryGame/Public/…`) on 2026-07-11. The
FactoryGame `.cpp` files are SML link stubs — behavior claims about binary internals are marked
**[assumption]** and appear in doc 08 test checklists.

## 1. Architecture principle: own every class you change

PrimalFauna's entire gameplay surface lives in **our own subclasses** of the creature framework.
Consequences (verify at every review):

- **Zero SML hooks in v1.** Target policy = virtual overrides in our controller; loot = BNE
  override; worm movement = our movement component. (HostileScaling needs hooks because it changes
  *vanilla* creatures; we don't.) If a packet proposes a hook, that's an architecture deviation →
  escalate to the orchestrator.
- **Minimal AccessTransformers.** Protected members of `AFGCreature`/`AFGCreatureSpawner`/
  `AFGCreatureController` are reached from *inside our subclasses* (C++ protected inheritance
  access). The census/anchor reader (§9) uses reflection (`FindFProperty`) for read-only dumps.
  **Correction (W1.3, 2026-07-23, review-adjudicated):** `AFGCreature::mIsPassiveCreature`/
  `mSpawnDistance`/`mDespawnDistance` are **private**, not protected — self-access is impossible
  for them. Settled mechanism: **one Friend grant** (`Friend=(Class="AFGCreature",
  FriendClass="APFACreatureBase")` in `Config/AccessTransformers.ini`) so a game-update field
  rename breaks the BUILD, not runtime behavior (decisive for `mIsPassiveCreature`, which feeds
  the HostileScaling skip-check). `PFA.SpawnTest` asserts the writes on a live instance. Any
  further transformer entry must be documented in its packet handoff and stays scoped to OUR
  classes' needs, never to altering vanilla instances.
- Vanilla content is referenced (borrowed BT asset §4, Wwise events, remains recipes as templates)
  — never edited. No CDO writes to vanilla classes in v1 except the possible scanner registration
  (§8, quarantined + experimental-flagged).
- **SHIPPING-EXPORT TRAP (found at the 2026-07-23 Phase-1 gate):** a FactoryGame method can be
  declared in the SML headers and link against the stub .cpps yet be ABSENT from the shipping
  DLL's export table (plain non-UFUNCTION methods are the risk class; e.g.
  `AFGCreature::ClearKillOrphanTimer`) → the entire module fails to load at boot (error 127).
  Compile+cook cannot catch it. Rules: prefer UFUNCTION/inline/getter paths for novel calls;
  after EVERY package that adds a new game-API call, run `tools/check_imports.ps1` (verifies every
  FactoryGame import against the real export table) before any game session.

## 2. Class map

This is a design map, not a census of implemented classes. As of 2026-10-14 the Worm classes
and director below do not exist; detailed modeling via Claude is active and gameplay is pending.
The worm director is one global saved record, not one actor/director per region (doc 03 §7).

```
Source/PrimalFauna/
├─ PrimalFauna.{h,cpp}                 FPrimalFaunaModule — log category, build marker; NO hooks
├─ PFAConfig.{h,cpp}                    UPFAConfig : UModConfiguration + FPFAConfigStruct mirror (doc 04)
├─ PFARootInstance.{h,cpp}              UGameInstanceModule: ModConfigurations += UPFAConfig
├─ PFARootWorld.{h,cpp}                 UGameWorldModule: ModSubsystems += APFASubsystem;
│                                        mSchematics += loot schematic (doc 01 §5)
├─ PFASubsystem.{h,cpp}                 APFASubsystem : AModSubsystem, IFGSaveInterface
│                                        nest registry+injection (doc 03), config push, census,
│                                        proposed global worm record, refresh tick "RefreshTick" (never `Tick` —
│                                        clang name-hiding hazard, world-modification.md §7)
├─ PFANestSpawner.{h,cpp}               APFANestSpawner : AFGCreatureSpawner (NestId/RegionId SaveGame)
├─ PFACreatureBase.{h,cpp}              APFACreatureBase : AFGEnemy — shared species base (§3)
├─ PFACreatureController.{h,cpp}        APFACreatureController : AFGCreatureController (doc 02 policy)
├─ Species/PFASicklejaw.{h,cpp}         + PFACrestback, PFADuskstalker (per-species ctor data, §3)
├─ Worm/PFASandwyrm.{h,cpp}             APFASandwyrm : APFACreatureBase (custom movement §7)
├─ Worm/PFAWormMovement.{h,cpp}         UPFAWormMovement : UFGCreatureMovementComponent
├─ Worm/PFAWormActions.{h,cpp}          emergence/lunge as UFGCreatureAction subclasses (§5)
├─ PFARegions.{h,cpp}                   static territory table + FPFARegion structs (doc 03 §2)
├─ PFALoot.{h,cpp}                      UFGItemDescriptor subclasses (remains/trophies) + recipes (§6)
└─ PFACheats.{h,cpp}                    dev console commands (§9)
Content/PrimalFauna/                    meshes, materials, anims, montages, icons, (maybe) 1 BT + AnimBPs
```

`AFGCreature` is `UCLASS(Abstract)` (`Creature/FGCreature.h:227`); `AFGEnemy` is an empty marker
(`Creature/Enemy/FGEnemy.h:12`) — we derive through it purely for ecosystem convention.

## 3. Species authoring — everything on the CDO, in C++

Per-species constructors set (all fields verified in `FGCreature.h` / base classes):

- **Identity/stats:** health via `GetHealthComponent()->SetMaxHealth(anchorValue)`
  (`FGHealthComponent.h:79`, public) — the CDO value is what HostileScaling snapshots (doc 06 §2:
  stats live on the CLASS, never runtime-multiplied). Movement speeds via `mMoveSpeedData`
  (`FGCreature.h:643`, `FMoveSpeedPair` walk/run/sprint).
- **Perception:** `mPerceptionSettings` (`FFGCreaturePerceptionSettings` :560 — instanced
  `UAISenseConfig_Sight/Hearing`, `ImmediateVisibilityRadius`) built with `CreateDefaultSubobject`.
  Aggro pacing: `mMaximumAggroLevel`/`mAggroFightThreshold`/`mAggroGainRate` (:595-615).
- **Controller:** `AIControllerClass = APFACreatureController::StaticClass()`.
- **Actions (§5)** into `mCreatureActionsSetup` (:584, `FCreatureAction{RequiredStates, Action,
  StartsWithCooldown}` :148).
- **Spawn/despawn:** `mSpawnDistance`/`mDespawnDistance` (:691-695) per species.
- **Mesh/anim:** skeletal mesh + AnimInstance class refs to OUR plugin content. Import per
  [[sf-skeletal-mesh-import-crash]]: headless import MUST `save_directory` (not just save_asset) or
  the cooked mesh crashes on equip-class bugs; assign mesh defensively (a broken skeleton degrades,
  never crashes — same guard philosophy).
- **Flags:** `mIsPassiveCreature` stays FALSE on the CDO (authored-hostile; per-instance writes
  implement config mode 0, doc 02 §3 — self-access from `APFACreatureBase::SetAggressionMode`).
  `mIsArachnid` false. `mCreatureFamily` (:623): create one `UFGCreatureFamily` asset/class for
  PrimalFauna if the family system needs it for creature-vs-creature — resolve during W1
  ([assumption]: family's exact role is binary-side; the census dump logs vanilla families).

**BP wrappers are allowed but not default.** If a species needs editor-side tweaking (anim class
assignment quirks), a thin BP subclass of the C++ species is acceptable; all *data* stays in C++
defaults so the CDO remains the single source of truth.

## 4. Behavior: borrow the vanilla Behavior Tree, own the actions

**STATUS AFTER RUNTIME SAVE/LOAD TESTING (updated 2026-08-16):** the borrow WORKS with three corrections:
(1) CDO→instance propagation does NOT survive the closed-source spawn path — the controller's
OnPossess writes the subsystem's GC-rooted tree onto the INSTANCE (`source=subsystem-fallback`,
W1.7); (2) launching is OUR job — vanilla launches the tree from its BP controller graph
(`RunBehaviorTree` in OnPossess, W1.6); (3) saved creatures can be possessed before the subsystem's
post-mount discovery finishes. `EnsureBehaviorTreeStarted` therefore defers honestly when the tree
is unavailable, and the subsystem retries every existing PFA controller immediately after discovery
succeeds. The helper is idempotent and never restarts an already-running brain. Automatic tests
run after the capped discovery retry; a successful late recovery re-arms the live-creature
one-shot so its corrected evidence is not lost. `PFA.SelfTest` requires at least one live PFA
controller, then checks every one has a creature, an instance tree, and a running brain; zero
controllers is an explicit failing `NO-COVERAGE`, not a vacuous PASS.
The PackAssist self-test is explicitly coverage-aware: per world it records only bounded assist
waves that had eligible candidates, including candidate, accepted, joined, and failed-wave counts.
It fails with `NO-COVERAGE` until such a wave occurs, then fails if any wave did not bind every
candidate to the exact assisted target in Fight with sufficient aggro. Already-active packmates are
not scanned after the fact because native combat may legitimately keep them on another target.

The gamble that makes creature AI cheap — staged so it degrades gracefully:

1. **Census first (W1.2):** `PFA.DumpCreature` on a live hog/stinger logs
   `GetBehaviorTreeAsset()->GetPathName()` (`FGCreature.h:355`), controller class, and each
   `mCreatureActionsSetup` entry's class + key values (reflection dump). Now we know the exact
   vanilla BT paths and real action tuning as templates.
2. **Borrow the BT:** set our species CDO `mBehaviorTree` to the vanilla tree
   (`LoadObject<UBehaviorTree>` at world-module init — post-mount; keep a `UPROPERTY` ref against
   GC per the CDO-edit rules in the skill's content guide). **[assumption]** the vanilla trees are
   generic executors (roam/alert/fight loops that score and run whatever `mCreatureActionsSetup`
   provides — strongly implied by the action-scoring architecture, `FGAction.h:77`
   `ScoreAction`, and per-state override list `FCreatureBehaviorOverride` `FGCreature.h:208`).
3. **Fallback (one editor-manual step, once, reused by all species):** author `BT_PFAGeneric` in the
   editor mirroring the census-dumped structure. The packet carries a click-path recipe; this is
   the ONLY unavoidable editor step in the whole mod if the borrow fails.
4. **Actions are ALWAYS ours** (borrowed or authored BT): native subclasses configured in C++ —
   see §5. Montages are skeleton-bound, so borrowed vanilla montages can never run on our
   skeletons; every action montage is our asset.

## 5. Attacks = native `UFGCreatureAction` classes (no custom combat code)

Verified classes with the fields we set (all under `Creature/Actions/`):

| Species need | Native class | Key fields |
|---|---|---|
| Sicklejaw bite | `UFGCreatureActionMeleeAttack` (`FGCreatureActionMeleeAttack.h:26`) | `mAttackRange:71 mAttackAngle:79 mKnockbackAmount:87 mDamageTypes:95` |
| Sicklejaw pounce | `UFGCreatureActionLeapAttack` (`FGCreatureActionLeapAttack.h:14`) | leap radius/velocity/montages/slam damage `:88-148` |
| Crestback charge | `UFGCreatureActionCharge` (`FGCreatureActionCharge.h:15`) | `mDamageTypes:87`, start/charge/hit montages `:95-103` |
| Warning display | `UFGCreatureActionRoar` (`FGCreatureActionRoar.h:14`) | `mRoarNoiseTickrate:63` |
| Duskstalker pounce | LeapAttack again (slam radius 0) | |
| Worm attacks | custom `UFGCreatureAction` subclasses (Worm/PFAWormActions) — the action SYSTEM is native, the motion is ours | base lifecycle `FGAction.h:59-105` |

Actions are instanced UObjects — **but the native tuning fields are PROTECTED (W1.4 finding), so
species packets SUBCLASS the native action (`UPFACreatureAction*`) and set fields in the subclass
ctor** (compile-safe, no grants); direct `CreateDefaultSubobject<UFGCreatureAction*>`+set does not
compile. Wrap in `FCreatureAction{RequiredStates = Fight}`. Target selection per
action via `FCreatureActionTargetParams` (`FGCreatureAction.h:33` — Closest/HighestAggro/…).
Damage: `mDamageTypes` arrays take `UFGDamageType` instances — instantiate ours with damage =
anchor × config `DamageMultiplier` (doc 04) pushed on refresh; **the damage numbers therefore live
on OUR action objects (ours to scale), while HostileScaling multiplies at the health-component
entry (theirs)** — no overlap (doc 06 §2).
**[assumption → slice test]:** an action runs correctly with a null/1-frame montage (damage without
polish anim); otherwise the slice ships with the imported bite anim from day one.

## 6. Loot & death

Override `SpawnDeathItem_Implementation` (BNE, `FGCreature.h:344`) in `APFACreatureBase`: spawn
item-drop pickup(s) for `Desc_<Species>Remains` (+ trophy rolls per doc 01). Items =
`UFGItemDescriptor` C++ subclasses (stack size/category/mesh/icon on CDO); recipes mirror the
vanilla remains→protein constructor recipes (census dumps the exact vanilla recipe shape);
registered via the loot schematic on the world module (`content-and-assets.md` model: schematic is
the loading anchor). Each death pickup uses the game's `FindGroundLocationAndRotation` helper before
spawn, retains its interaction trace, ignores Pawn collision, and has navigation relevance disabled
so multiple remains cannot become a pathing wall. Icons: rendered from the textured model (doc 05 §5) — the render-before-review
rule [[feedback-render-textured-for-review]] applies to icons too.

Sicklejaw-specific death exception: `APFASicklejaw` still calls `AFGCreature::Died` for native
loot, cleanup, timers and lifetime, but bypasses `DoRagdoll_Internal` only after death because this
imported PhysicsAsset intermittently stretches the skeleton. It plays the preloaded non-looping
authored `Death` collapse, holds its final corpse pose, clears the dead tail additive and disables
corpse-mesh collision. Live/nonlethal ragdoll calls continue through FactoryGame.

## 7. Sandwyrm movement and lifetime (proposed, not implemented)

The current art targets are **180 m body, 30 m shaft, 38 m external mouth-lobe span (36–40 m) and 26 m clear bore**, provisional until
real Mk1/2/3 geometry and the exact exported rig pass the fit gates (doc 10). Starter-project
miner meshes are placeholders; their bounds cannot validate these targets. Required movement
includes underground/partial travel, eruption, and a top-down arch swallowing a complete miner;
player/creature engulf and mouth articulation are separate required attack variants.

Candidate movement route: `UPFAWormMovement : UFGCreatureMovementComponent`
(`Creature/FGCreatureMovementComponent.h:14`), `MOVE_Custom` and `PhysCustom` along a director's
body path, with `ShouldRemainVertical()` reviewed for full-body rotation. This remains engineering
to validate, not a proof supplied by the FlyingBabyCrab precedent. A coherent spine/mouth driver
may combine authored clips and procedural deformation. It must avoid competing pose writers,
worker-thread UObject access and assuming a single upright capsule encloses an arch.

Before deriving from `APFACreatureBase`, isolate existing Sicklejaw policy: the base currently
rescues buried spawns, applies Sicklejaw aggression and ground rotation, and the subsystem pushes
that aggression to all base instances. A worm must opt into its own reviewed policy without
changing Sicklejaw behavior. Retain native creature/health identity and attributed damage so
HostileScaling compatibility can be tested. Standard radial damage may serve eruption effects;
it cannot substitute for timed swallowing and native miner cleanup/inventory semantics.

One saved logical individual may have a materialized native actor or a safely unloaded state.
Record identity, health, position/route, phase/event serial, death/respawn and loot entitlement;
adopt loaded actors first and restore the same state on re-materialization (doc 03 §7a). Neither
`SetIsPersistent(true)` nor a spawnerless orphan-cleanup call is mandated. Select a shipping-exported,
native-compatible lifecycle after reviewing body extent, streaming, multiplayer relevance,
aggression and active consumption transactions. The 100 m/120 m hostile-base distances are not an
approved giant-worm policy; do not inherit them accidentally or change Sicklejaw's native parity.

Verification must exercise a player near mouth/tail while the actor origin is distant, players
at opposite ends, burial/emergence during streaming, cross-desert travel, attack interruption,
death and two save/load cycles. Re-materialization must not duplicate an individual, heal it,
replay a swallowed target or mint loot. Logs must distinguish logical identity, live actor,
phase and residency decisions; source/build success alone proves none of these runtime behaviors.

## 8. Scanner / radar / bestiary surface (polish tier)

- **Radar towers:** spawner-keyed fauna listing (`FGBuildableRadarTower.h:113`,
  `FGRadarTowerRepresentation.h:75`) — expected to work via our real spawners. Verify, don't build.
- **Object scanner:** needs a `UFGCreatureDescriptor` (`Creature/FGCreatureDescriptor.h:15`,
  `mCreatureClass` :25 + arachnophobia alt icons) and registration into the scannable-details data
  (`FGScannableDetails.h:29-45,101`; live hostiles route via `UFGScannableDetailsHostileCreature`
  :207). Registration point is game settings data → likely ONE vanilla CDO array append.
  Quarantine: experimental-flag gated, own packet, differential-reviewed (it's our only vanilla CDO
  write). If it fights back, ship without scanner support (radar listing still works).

## 9. Diagnostics contract (`LogPrimalFauna`, `[PFA]`)

**Severity convention (settled in W1.1 review):** "Verbose" labels below mean *gated detail* —
logged at **Display severity inside the `pfa.Diag`/config gate** (the sml-gotchas §3 body-gate
pattern). Do NOT use literal `UE_LOG(..., Verbose, ...)` for gated content: the category's default
runtime verbosity (Log) silently drops it and makes the flag look broken. True Verbose severity is
reserved for always-compiled lines users raise via `Log LogPrimalFauna Verbose`.

Per workspace law + the skill's diagnostics-first rules; Verbose per-entity, Display summaries:

- **Build marker** in `StartupModule`: `===== PrimalFauna BUILD <tag> LOADED =====` (bump every build).
- **Config snapshot** at world load + on change (doc 04 §2).
- **Census (Display, every load):** regions/nests/creature counts; per-nest Verbose.
- **Target policy (Verbose):** every `CanBeHostileTowards` verdict with rule number (doc 02 §2).
- **Actions (Verbose):** action start/score/hit lines (`[PFA] Sicklejaw action=Melee target=… dmg=…`).
- **Worm (proposed, Display):** every state/residency transition with individual/event IDs and
  scores (doc 03 §7); no implemented director is implied by this logging contract.
- **Heartbeat:** refresh tick counter every 6th tick (~60 s) — distinguishes "stopped" from "idle"
  (WeaponUpgrades lesson).
- **Anchor dump (Display, once):** the vanilla-stat census + our derived stats:
  `[PFA] Anchors: AlphaHog HP=… -> Sicklejaw HP=…` — this line is how in-game HostileScaling
  verification works (doc 06 §4: expected scaled value visible in ONE log read).
- **Dev commands (`PFACheats`, shipped but cheat-gated):** `PFA.DumpCreature` (census a look-at
  creature), `PFA.DumpCandidates <region>`, `PFA.SpawnNest <region> <species>`, `PFA.WormForceEvent`,
  `PFA.KillAll`. Console commands are the in-game test accelerators — they exist from the FIRST
  slice build.

`PFA.WormForceEvent` specifically remains a stub as of 2026-10-14; its command registration
does not establish worm spawn, movement or attack coverage.

## 10. Save-plan versioning

`APFASubsystem` SaveGame fields: `mNestSeed`, `mPlanVersion` (int, bump when the region table
changes shape), nest registry map, and a proposed single worm state record. Worm identity and
death/drop/transaction state must survive actor retirement and re-materialization; their schema
and migration are not implemented yet. On version mismatch: adopt-what-matches, deactivate
orphaned nests (log at Display), materialize new. Mod-removed saves must load vanilla-clean
(everything we add is our classes + save-interface actors — the game drops unknown classes; test
item, doc 08).
