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
	// game's own code. Returns false if registration was refused.
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

	// W2.3 SHIPPED the Factions keys (flat layout, PFAConfig.cpp) -- these
	// constexprs are no longer the runtime values. Their remaining roles:
	// (1) compile-time doc-04 anchors the SicklejawControllerPolicy self-test
	// pins against drift; (2) the initializers PFAFactionConfig's live
	// statics start from before the first config push. Runtime reads go
	// through PFAFactionConfig::HuntVanillaHostiles()/HuntPassiveFauna().
	static constexpr bool HuntVanillaHostilesDefault = true;
	static constexpr bool HuntPassiveFaunaDefault = false;

protected:
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
