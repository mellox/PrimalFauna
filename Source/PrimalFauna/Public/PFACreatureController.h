#pragma once

#include "CoreMinimal.h"
#include "AI/FGCreatureController.h"
#include "UObject/ObjectKey.h"
#include "PFACreatureController.generated.h"

// ---------------------------------------------------------------------------
// APFACreatureController -- implements docs/02-hostility-and-factions.md §2's
// 8-rule target policy table for every PrimalFauna creature. Doc 02 §1: "We
// own our creatures' controller class, so the entire faction policy is plain
// C++ virtual overrides ... no SML hooks needed for our own creatures."
//
// Rule table -> code map (see PFACreatureController.cpp for the full ladder):
//   CanBeHostileTowards() runs the full 8-rule ladder; rules 1-4 (the
//   unconditional "never" rules) live in the Super-free helper
//   PassesAbsoluteNeverRules() it shares with IsValidTarget().
//   IsValidTarget() composes Super's own validity gate with rules 1-4 ONLY --
//   it must NEVER call CanBeHostileTowards: the base class's own
//   CanBeHostileTowards calls this virtual as a sub-check, so that
//   composition recursed unboundedly (w18-1 stack-overflow crash).
//   CanBeAlertedByTarget_Implementation() delegates to CanBeHostileTowards
//   directly rather than trusting the closed-source default's documented
//   "runs CanBeHostileTowards by default" behavior (doc 07 §1: own every
//   class we change, don't assume binary defaults).
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// w58-5 ENGAGEMENT OBSERVATION. Everything the engagement census needs about
// what the melee action actually DID, recorded by the action itself at the
// moment it ran.
//
// Why recorded rather than queried: the census must not CALL engine action
// code to find out whether the action could fire. This project already paid
// for that lesson at the nest seam -- AFGCreatureSpawner::IsReadyToSpawn reads
// as a nominal query and populates slots and advances native timing. A
// diagnostic that mutates the thing it measures is worse than no diagnostic.
//
// Every timestamp is world-seconds and every consumer prints its AGE, so a
// sample taken minutes ago can never read as a fresh measurement.
// ---------------------------------------------------------------------------
struct FPFAEngagementObservation
{
	// --- sampled inside UPFACreatureActionMelee::CanPerformAction ---------
	bool bHasCanPerformSample = false;
	bool bLastCanPerform = false;
	float LastSampledAttackRangeCm = -1.0f;
	float LastSampledMinRangeCm = -1.0f;
	// Sampled from UFGAction::GetRemainingCooldown() (FORCEINLINE header
	// getter) AT THE SAMPLE, not now. The name carries "AtSample" because the
	// value is only true as of LastCanPerformSampleTimeSeconds.
	float LastSampledCooldownRemainSeconds = -1.0f;
	int32 LastSampledActionState = -1;
	double LastCanPerformSampleTimeSeconds = -1.0;

	// --- recorded inside UPFACreatureActionMelee::PerformAction ----------
	double LastPerformStartTimeSeconds = -1.0;
	int32 PerformStartCount = 0;

	// --- target-hold tracking, updated on the 0.25s beat -----------------
	// FLICKER-TOLERANT BY CONSTRUCTION: the native current target flickers
	// null 1-2x/second during a live engagement (w26-3, session 18), so a null
	// reading NEVER re-stamps the hold. Only a change to a DIFFERENT non-null
	// actor starts a new hold. Without that, heldSecs would reset twice a
	// second and could never report the 46-50s holds this field exists for.
	TWeakObjectPtr<AActor> HeldTarget;
	double HeldTargetSinceSeconds = -1.0;
	int32 PerformStartCountAtHoldStart = 0;
};

UCLASS()
class PRIMALFAUNA_API APFACreatureController : public AFGCreatureController
{
	GENERATED_BODY()

public:
	APFACreatureController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// W1.6 (docs/08 packet, Phase-1 gate finding): launches the borrowed
	// BehaviorTree (docs/07 §4) the possessed creature's CDO carries.
	// AFGCreatureController itself declares OnPossess public (AI/
	// FGCreatureController.h:156, overriding AAIController's protected
	// AIController.h:346) -- matched here at the same access level. Vanilla
	// creatures get this from BP_CreatureController_C's own Blueprint graph;
	// our C++ controller replaces that layer and must reproduce it explicitly
	// (docs/07 §1: own every class we change). See the .cpp for the full
	// success-chain logging and the InitializeBlackboard non-decision (we
	// deliberately do NOT override it -- AFGCreatureController already does,
	// and RunBehaviorTree's internal UseBlackboard() call reaches that
	// existing override automatically).
	virtual void OnPossess(APawn* InPawn) override;
    virtual FPathFollowingRequestResult MoveTo(const FAIMoveRequest& MoveRequest,
        FNavPathSharedPtr* OutPath = nullptr) override;

	// Saved creatures can be possessed before APFASubsystem finishes its
	// post-mount BehaviorTree discovery. OnPossess calls this immediately;
	// the subsystem calls it again after discovery succeeds. Idempotent: an
	// already-running brain is never restarted.
	bool EnsureBehaviorTreeStarted(const TCHAR* Trigger);

	// w27-1: per-frame body yaw toward the target (pfa.RotationMode 2, the new
	// default). Two attempts at using UE's built-in mechanisms both failed
	// in-game: orient-to-movement only turns while accelerating (and these
	// creatures move in bursts), and controller-desired rotation was found to
	// be ALREADY ENABLED before we set it (log: "controllerDesired true->true")
	// yet the body still never turned -- so the closed-source
	// AFGCreatureController::UpdateControlRotation override is not consuming
	// our AI focus. Rather than keep guessing at engine plumbing, mode 2 sets
	// the pawn's yaw itself at a fixed rate; modes 0/1 are kept for A/B.
	virtual void Tick(float DeltaTime) override;

	// Doc 02 §2 rules 1-8, evaluated in order, first match wins. See the .cpp
	// for the full ladder and per-rule [PFA] verdict logging.
	virtual bool CanBeHostileTowards(const AActor* actor) const override;

	// Super's validity gate ANDed with rules 1-4 only (never the full ladder
	// -- see the class comment: the base's CanBeHostileTowards calls this
	// virtual, so running CanBeHostileTowards from here recursed; w18-1).
	virtual bool IsValidTarget(const AActor* actor) const override;

	// W2.5: dev/diagnostic force-aggro that actually REGISTERS the target
	// first -- the public SetAggroForTarget silently no-ops on an
	// unregistered actor (GetAggroForTarget's -1.0 sentinel; session-15:
	// PFA.AggroNearest Readback=-1.0, no Fight lock, both modes). Uses the
	// native private GetOrRegisterAggroTarget under the AccessTransformers
	// Friend grant so every FFGCreatureAggroData field is initialized by the
	// game's own code, then explicitly binds the target through the public
	// native setter before one state-machine update. Returns true when native
	// registration + the requested aggro write were accepted; callers measure
	// Fight state and exact current-target binding as a separate postcondition.
	bool ForceAggro(AActor* Target, float NewAggro);

	// W2.5: clear ALL per-target state (aggro entries, visibility entries,
	// current target) so re-perception re-evaluates every rule fresh.
	// Session-15: the first Passive->Hostile config round-trip left creatures
	// with aggroTargets>0 / currentTarget=none and rule5 NOT_HOSTILE vs the
	// player -- stale per-target data captured under the old mode is the
	// prime suspect. Called on EVERY aggression-mode change (idempotent
	// transitions). mAggroTargets/mVisibleTargets are protected
	// (FGCreatureController.h:479/486) -- plain subclass access.
	void ResetTargetingState(const TCHAR* Reason);

	// --- w58-5 DEAD-TARGET RETENTION ------------------------------------------
	// Runs on its OWN 0.25s beat, immediately before TickAggroRangeDecay: if the
	// current target grades Dead or Destroyed, drop it and reset targeting.
	//
	// WHY ITS OWN BEAT AND ITS OWN KILL-SWITCH rather than a block inside
	// TickAggroRangeDecay (which is where the diagnosis proposed it): that
	// function returns early on `pfa.AggroDecayAtRange 0`, on a missing
	// visibility curve, on a zero reduction rate, and on an EMPTY aggro array --
	// all of them BEFORE its 0.25s accumulator. A dead target held with no aggro
	// entries left is exactly one of the shapes this exists to catch, so folding
	// the check in behind those returns would have made it unreachable in part of
	// its own domain, and would have tied it to an unrelated A/B switch.
	// Kill-switch: pfa.DropDeadTargets.
	void TickDeadTargetDrop(float DeltaSeconds);

	// --- w58-5 ENGAGEMENT OBSERVATION (see FPFAEngagementObservation) ----------
	// Written by UPFACreatureActionMelee, read by PFAEngagementCensus. Both are
	// cheap unconditional recorders -- NOT diag-gated, because a diagnostic that
	// only records while someone is watching is the w44-1 defect wearing a hat.
	void NoteMeleeCanPerformSample(bool bCanPerform, float AttackRangeCm, float MinRangeCm,
		float CooldownRemainSeconds, int32 ActionState);
	void NoteMeleePerformActionStarted();
	const FPFAEngagementObservation& GetEngagementObservation() const { return mEngagementObservation; }

	// --- w48-2 STATE KICK -----------------------------------------------------
	// PROVEN IN GAME (w48-1 session, 2026-07-26): after a successful disengage --
	// `ResetTargetingState (aggro decayed below fight threshold at range)`, aggro
	// wiped, currentTarget null, TriggerStateUpdate(true) already called -- the
	// native machine STAYS IN CS_Alert indefinitely (repeating heartbeats:
	// `state=CS_Alert aggroTargets=0 currentTarget=none`). Both prior cold reviews
	// flagged this as statically unprovable ("mAggroFightThreshold is documented as
	// a transition-IN condition; nothing says falling below it transitions OUT") --
	// it is now measured, not assumed.
	//
	// It matters twice over: the roam branch is gated by BTD_CheckState_C
	// 'Check For Roaming State' State=1 (CS_Roam = 1 << 0), so a creature parked in
	// CS_Alert can never reach it; and CS_Alert maps to DesiredState=3 in the anim
	// driver, so the seven idle-break clips are unreachable too. One stuck state,
	// both "it doesn't wander" and "it just stands there".
	//
	// Called immediately AFTER ResetTargetingState on the two disengage paths (the
	// w45-2 range-decay reset and the w40-1 out-of-range give-up), never before --
	// ResetTargetingState ends with TriggerStateUpdate(true), so the native machine
	// has already had its say by the time we look. Deliberately NOT called on the
	// aggression-mode-change reset (PFACreatureBase.cpp:494). w48-3 states the REAL
	// reason, replacing the w48-2 one ("a Passive flip is not a disengage", which is
	// a rationalization): it is BLAST-RADIUS CONTROL. The kick's effect on the native
	// state machine is still unproven, that reset path is user-verified working
	// today, and a third call site would muddy the very A/B this packet exists to
	// run. Revisit once the kick has in-game evidence.
	//
	// SCOPE (w48-3, review W1): kicks ONLY the stuck-alert shape -- CS_Alert,
	// CS_Fight, CS_Default. CS_Flee (stress-driven) and CS_Investigate (noise) are
	// legitimate non-aggro states on an axis independent of aggro, and forcing Roam
	// would CANCEL them. This narrowing is a differential requirement, not a
	// nicety: the old path (ResetTargetingState alone) could not change the
	// behaviour state at all, so it could never cancel a flee; this one can.
	//
	// Always-on logging (this is a behaviour change; w44-1's lesson is that
	// behaviour which only runs while you are watching is the worst kind of bug),
	// live kill-switch `pfa.ForceRoamOnDisengage` (default 1) so the whole thing is
	// A/B-able in one session without a rebuild, exactly like pfa.AggroDecayAtRange.
	// That CVar is declared at FILE SCOPE in the .cpp, NOT inside this function: a
	// function-local static CVar only registers when its host function first runs,
	// and this one is event-driven, so `pfa.ForceRoamOnDisengage 0` was an unknown
	// command until the first disengage had already happened (w48-3, review W2).
	// Bounded to at most one call per disengage EVENT on both paths, so it can never
	// fight the native machine in a loop -- see the .cpp.
	//
	// The .cpp also records a house rule this packet DISPROVED against the built
	// binary: an un-overridden base-class virtual (GetDesiredCreatureState) still
	// lands in our import table, because our own vtable must carry the base's
	// address. "It's virtual" is not a substitute for checking the export table.
	void ForceRoamAfterDisengage(const TCHAR* Reason);

	// W2.3 SHIPPED the Factions keys (flat layout, PFAConfig.cpp) -- these
	// constexprs are no longer the runtime values. Their remaining roles:
	// (1) compile-time doc-04 anchors the SicklejawControllerPolicy self-test
	// pins against drift; (2) the initializers PFAFactionConfig's live
	// statics start from before the first config push. Runtime reads go
	// through PFAFactionConfig::HuntVanillaHostiles()/HuntPassiveFauna().
	static constexpr bool HuntVanillaHostilesDefault = true;
	static constexpr bool HuntPassiveFaunaDefault = false;

protected:
	// Doc 02 §4: when one same-spawner creature enters Fight, idle/roaming
	// packmates inherit its registered target through the same ForceAggro path
	// used by the dev command. Config-gated by PackAssist.
	virtual void OnCreatureStateChanged_Implementation(
		ECreatureState PreviousState, ECreatureState NewState) override;

	// BNE (AI/FGCreatureController.h:387). Base's own doc comment says it
	// "runs CanBeHostileTowards by default" -- see the class comment above
	// for why we make that explicit instead of relying on it.
	virtual bool CanBeAlertedByTarget_Implementation(const AActor* target) const override;

private:
	// Rules 1-4 of the doc 02 §2 ladder -- tamed / protected species / own
	// species / other PrimalFauna species -> never. RECURSION CONTRACT: no
	// Super:: calls, no CanBeHostileTowards/IsValidTarget calls inside (the
	// base class couples those two virtuals), so it is safe from BOTH
	// CanBeHostileTowards and IsValidTarget. Returns true when no never-rule
	// fires (including null actor / no controlled creature -- callers keep
	// their own Super deferral for that corner).
	bool PassesAbsoluteNeverRules(const AActor* actor) const;

	// Doc 02 §3 Territorial-mode gate, applied to rules 5/6/7. Returns true
	// (permissive) for Hostile mode, false for Passive, and for Territorial
	// checks recent-damage-by-target OR distance-to-spawner <= the species'
	// territory radius -- falling through to Hostile (with a one-time gated
	// log) if the creature has no spawner yet (nests arrive W1.5).
	bool PassesAggressionModeGate(const AActor* Target) const;

	// Doc 02 §3 "or after being damaged by the target" -- reads the inherited
	// (protected) mRecentDamageTaken, which the native controller already
	// prunes by mRecentDamageMaxAge, so any entry present here is by
	// definition recent; we don't need our own recency window.
	bool WasRecentlyDamagedBy(const AActor* Target) const;

	// W1.8 (docs/08 packet; docs/02 §5 perception tuning, docs/07 §4 "OPEN
	// (W1.8): the tree runs but perception/targeting never evaluates"):
	// reproduces the perception wiring the vanilla BP_CreatureController_C layer
	// did that our C++ controller replaced (docs/07 §1: own every class we
	// change) -- the same layer that also did the BT launch (W1.6). Pushes the
	// possessed creature's authored sight/hearing sense configs
	// (AFGCreature::GetPerceptionSettings(), FGCreature.h:370 FORCEINLINE public,
	// inline -> no import) into THIS controller's inherited UAIPerceptionComponent
	// (AAIController::GetAIPerceptionComponent(), AIController.h:441 inline),
	// ensures the inherited OnTargetPerceptionUpdated handler is bound, sets the
	// dominant sense, and kicks a stimuli-listener update. Called from OnPossess
	// after the creature is validated, INDEPENDENT of the BT launch (perception
	// feeds aggro; the BT reads aggro -- separate subsystems). Every step emits
	// its own Display success-chain line. See the .cpp for the full symbol/export
	// analysis and the idempotent bind-if-not-already-bound rationale.
	void WireCreaturePerception(class AFGCreature* PossessedCreature);

	// W1.8 gated diagnostic: logs this creature's current behavior state,
	// aggro-target count, and current target every 30s, but ONLY when
	// pfa.Diag/EnableDiagnostics is on (PFADiag::IsEnabled()). Lets the micro-test
	// distinguish "perceiving but stuck in CS_Default" from "transitioning
	// Roam->Alert->Fight". Deliberately NO forced SetCurrentCreatureState: header
	// evidence (perception -> aggro -> native TriggerStateUpdate/UpdateCreatureState)
	// does not require a manual kick, and forcing state could fight the native
	// state machine (docs/08 packet guidance: implement a kick ONLY if header
	// evidence supports it, else leave this gated logger). Plain member fn
	// (FTimerManager's templated SetTimer binds raw member fns -- no UFUNCTION
	// needed and none wanted, keeping it off the reflection/export surface).
	// w44-1: writes the roam band to the blackboard. Deliberately SEPARATE from
	// the heartbeat's diagnostics and called unconditionally -- putting this
	// behaviour inside a diag-gated function made wandering work only while
	// diagnostics were on, which looked like a working feature.
	void ApplyRoamBandToBlackboard();

	// w45-1: publishes FGBBKeys::Spawner. Same class of gap as the roam band and
	// called from the same unconditional slot. Self-healing (re-checked each beat)
	// because the key can only be written once the native spawner has coupled
	// itself to the creature, which is not guaranteed to have happened by the
	// time we first possess.
	void ApplySpawnerToBlackboard();

	// w45-2: sheds aggro for targets beyond the visibility curve's range, where the
	// native gain is provably 0. Makes de-escalation independent of the closed
	// native visibility semantics (see the .cpp). Kill-switch pfa.AggroDecayAtRange.
	void TickAggroRangeDecay(float DeltaSeconds);
	// Not mutable -- that path is non-const (w45-3 review D7: the w45-2 version was
	// declared mutable while the flag added 50 lines below correctly was not, so the
	// commit stated the convention and broke it on the same screen).
	// Set membership, not a value map: we log region ENTRY and the threshold
	// CROSSING rather than sampling aggro, so there is no per-target value to keep.
	TSet<FObjectKey> mRangeDecayLoggedTargets;
	// Batches the per-frame Tick down to a 0.25s cadence (review D6).
	float mRangeDecayAccumulator = 0.0f;
	// w46-1: set once the "aggro already at zero, target still held" condition has
	// been reported for this engagement; re-armed as soon as any target holds aggro
	// again. Diagnostic-only state -- see the end of TickAggroRangeDecay.
	bool bLoggedStaleZeroAggroTarget = false;
	// w47-2 (w47-1 review I2): second latch, for the LONG-hold sample (20 beats =
	// 5.0s) that gives the dwell-timeout work an actual measured number. Cleared
	// alongside the one above on every beat the condition fails.
	bool bLoggedStaleZeroLongHold = false;
	// w47-1 (w46-1 review F3): consecutive 0.25s beats the stale-zero condition has
	// held. The boolean above alone could SPAM ~2 lines/s forever, because the
	// condition tests GetCurrentTarget() != nullptr and the native current-target
	// flickers null 1-2x/second during an engagement (w26-3) -- each flicker
	// re-armed the latch. w47-2 (review I3): 6 consecutive beats (1.5s), raised from
	// 4 (1.0s) -- at 4 the margin over the flicker was exactly one beat, so a 0.75 Hz
	// flicker would have satisfied it. It is also deliberately the front half of the
	// dwell-based stale-target timeout that is the likely follow-up fix.
	int32 mStaleZeroConsecutiveBeats = 0;

	// --- w58-5 dead-target drop state -----------------------------------------
	// Separate accumulator from mRangeDecayAccumulator on purpose: sharing one
	// would couple the drop's cadence to the decay's early returns (see
	// TickDeadTargetDrop's comment).
	float mDeadTargetDropAccumulator = 0.0f;
	// Set while the current target has already been reported as confirmed-dead
	// for this engagement, so a drop that the native machine refuses cannot
	// produce a line every 0.25s. Cleared as soon as the target changes or
	// grades alive again.
	TWeakObjectPtr<AActor> mLastDeadTargetReported;
	// w58-5 (review M7): consecutive 0.25s beats on which the SAME actor has been
	// found dead and still bound. The latch above throttles the LOG; this throttles
	// the ACTION, so a drop the native machine refuses cannot fire the always-on
	// ResetTargetingState + state-kick lines eight times a second.
	int32 mDeadTargetDropAttempts = 0;

	// w58-5: what the melee action measured, when it measured it. Public read
	// accessor above; written only by NoteMelee* .
	FPFAEngagementObservation mEngagementObservation;

	// --- w46-1 ROAM-LEG TELEMETRY (PFACreatureControllerRoam.cpp) --------------
	// The roam band reaches the blackboard as [600..2500]cm and the VANILLA BT picks
	// the destination. It is a closed BP asset, so whether it randomises inside the
	// band or always takes MaxRoamingDistance CANNOT be established statically -- and
	// the user's report ("when it wandered it seem to do for the same length") is
	// exactly what always-taking-the-max looks like. So measure it: detect movement
	// start/stop and log one line per completed leg with the distance actually
	// covered, so ONE session answers "do leg lengths vary within the band".
	//
	// THE TRACKING IS UNCONDITIONAL; ONLY THE LOG LINES ARE DIAG-GATED. w44-1 exists
	// because a load-bearing blackboard write sat inside a diag-gated function, so
	// wandering only worked while somebody was watching. State that must be
	// continuous across a session cannot be sampled only when diagnostics are on --
	// legs would start unrecorded and complete with garbage.
	void TickRoamLegTelemetry(float DeltaSeconds);
	float mRoamLegAccumulator = 0.0f;
	bool bRoamLegActive = false;
	FVector mRoamLegStartLocation = FVector::ZeroVector;
	FVector mRoamLegLastLocation = FVector::ZeroVector;
	// Position/time of the last sample that was genuinely MOVING, so the stop
	// debounce below does not inflate every leg's duration by its own window.
	FVector mRoamLegLastMovingLocation = FVector::ZeroVector;
	double mRoamLegStartTimeSeconds = 0.0;
	double mRoamLegLastMovingTimeSeconds = 0.0;
	// World time the previous leg ended (0 = none yet), which is what makes the
	// between-leg wait measurable at all.
	double mRoamLegEndedTimeSeconds = 0.0;
	double mRoamLegWaitBeforeSeconds = -1.0;
	float mRoamLegPathCm = 0.0f;
	float mRoamLegPeakSpeedCms = 0.0f;
	int32 mRoamLegSamples = 0;
	int32 mRoamLegProwlBandSamples = 0;
	// Dips below the movement threshold that did NOT reach the debounce count --
	// reported so a leg that looks short can be told from a leg that was stalled.
	int32 mRoamLegStalls = 0;
	int32 mRoamLegBelowThresholdSamples = 0;
	// True if a target OR any aggro entry existed at any point during the leg.
	// w47-1 (review F5.2): aggro is included because the native current-target
	// flickers null 1-2x/second while engaged (w26-3), so target-only sampling let
	// pursuit legs into the roam histogram through the flicker window.
	bool bRoamLegHadTarget = false;
	// w47-1 (review F5.2): the leg ran wholly or partly outside CS_Roam -- a
	// reaction (knockback, flee, the frame after a ResetTargetingState), not a roam
	// sample. Tracked SEPARATELY from the target flag so the log names which
	// disqualifier fired instead of claiming "had a target" about a creature that
	// had none.
	bool bRoamLegNonRoamState = false;
	ECreatureState mRoamLegStateAtStart = ECreatureState::CS_Default;
	int32 mRoamLegIndex = 0;
	bool bRoamWaitReported = false;

	void LogCreatureStateHeartbeat();

	// w26-1: keeps the AI focus pointed at the current target so the pawn's
	// rotation policy (orient-to-movement, set in APFACreatureBase::BeginPlay)
	// has something to aim at while closing -- user report: creatures
	// approached without ever turning to face, which also silently narrows
	// the melee cone. Vanilla creatures get this from their BP controller.
	// Change-gated (only re-focuses when the target actually changes).
	void TickFocusSync();
	FTimerHandle FocusSyncTimerHandle;
	TWeakObjectPtr<AActor> LastFocusTarget;

	// w26-3 focus grace (see the .cpp): the native current-target flickers
	// null and back ~1-2x/second during a live engagement; without this the
	// focus-driven body yaw would swing away and back on every flicker.
	TWeakObjectPtr<AActor> LastValidFocusTarget;

	// --- w40-1 DISENGAGE ------------------------------------------------------
	// User: "When I went far away, they would still just stand there and watch
	// me, turning to just watch me. So they didn't really wander around."
	//
	// They were not failing to roam -- they never STOPPED being alert, and the
	// roam branch cannot run on a creature that is permanently watching you. So
	// w32-1's roam-band fix was solving something downstream of the real cause.
	// This is the named anti-pattern "unwavering pursuit, never loses interest":
	// a real predator assesses, loses interest, and goes back to its own life.
	// Tracked as world-seconds, not a countdown, so a hitch cannot strand it.
	double OutOfRangeSinceSeconds = 0.0;
	double LastValidFocusTimeSeconds = 0.0;
	static constexpr double FocusGraceSeconds = 3.0;

	// w27-1 manual-yaw mode: turn rate and the ~1Hz throttle for the yaw
	// diagnostic (desired vs pawn vs control rotation -- the readout that
	// identifies which layer is swallowing the rotation).
	static constexpr float ManualYawRateDegPerSec = 360.0f;
	float RotationDiagAccumulator = 0.0f;

	// Repeating 30s timer driving LogCreatureStateHeartbeat; set once in
	// WireCreaturePerception (guarded against re-set on re-possession), and
	// auto-cleared by the world when this controller actor is destroyed on
	// creature despawn -- so no OnUnPossess/EndPlay override (each would force a
	// Super:: direct call to a closed-source FactoryGame method) is needed.
	FTimerHandle StateHeartbeatTimerHandle;

	// Set once PassesAggressionModeGate has logged the "no spawner yet"
	// fallback for this controller, so it logs once instead of once per
	// evaluation. Mutable because CanBeHostileTowards and friends are const.
	mutable bool bLoggedNoSpawnerFallback = false;

	// w45-1: set once ApplySpawnerToBlackboard has reported a spawnerless
	// creature, so the always-on line appears once per creature rather than
	// every 30s beat. Not mutable -- that path is non-const.
	bool bLoggedNoSpawnerForBlackboard = false;

	// w45-2 (review F2): set once a Spawner-key write has been proven to have NOT
	// taken via readback. Terminal -- the blackboard asset's key schema is fixed at
	// load, so retrying can only re-log a failure that cannot change.
	bool bSpawnerKeyWriteFailed = false;

	// w18-3: doc 02 §2 contract logger (format frozen -- see the .cpp),
	// throttled to log a target only when its (rule, verdict) result CHANGES.
	// w18-2 logged every evaluation tick: ~400 lines/s in-game, 46% of the
	// session log. Diag-gated (pfa.Diag). Mutable map: written from the const
	// evaluation path, same convention as bLoggedNoSpawnerFallback above.
	void LogRuleVerdict(const AFGCreature* SelfCreature, const AActor* Target, int32 RuleNumber, bool bHostile) const;
	mutable TMap<FObjectKey, uint8> mLastLoggedRuleVerdict;

	// W2.5 (w25-4 review fix): per-target throttle for the rule5 super-veto
	// probe, mirroring mLastLoggedRuleVerdict -- logs only when
	// mIsPassiveCreature CHANGES for a target, so the per-tick veto path
	// cannot re-flood the log (the w18-2 lesson, applied to its own probe).
	mutable TMap<FObjectKey, bool> mLastLoggedSuperVetoPassive;
};
