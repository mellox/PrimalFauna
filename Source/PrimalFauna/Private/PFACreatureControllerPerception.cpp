// ===========================================================================
// PFACreatureControllerPerception.cpp -- APFACreatureController's W1.8
// perception-wiring methods, split out of PFACreatureController.cpp purely to
// keep both TUs under the 500-line workspace limit (same class, two files --
// the engine itself splits large classes this way). Declared in
// PFACreatureController.h; called from OnPossess (PFACreatureController.cpp).
//
// W1.8 (docs/08 packet): THE PERCEPTION GAP. Micro-test w17-1 evidence
// (_team/test-logs/2026-07-23-microtest-w17/pfa-log-excerpt.txt): 7/7 creatures
// run the borrowed BT with a blackboard, yet with pfa.Diag on and 7 hostiles
// 2-5m away for minutes, ZERO CanBeHostileTowards / rule-verdict / target lines
// fired -- the tree runs but nothing ever ENTERS its perception/targeting
// evaluation. Root cause, established by reading the REAL headers (not memory):
//
//   The creature's sight/hearing sense configs live on the CREATURE
//   (AFGCreature::mPerceptionSettings, FGCreature.h:560 -- authored per species
//   ctor, e.g. Species/PFASicklejaw.cpp). The perception COMPONENT lives on the
//   CONTROLLER (AAIController::PerceptionComponent, AIController.h:142).
//   Something must copy the creature's configs into the controller's component
//   (ConfigureSense) or the perception system generates ZERO stimuli for this
//   listener -> OnTargetPerceptionUpdated never fires -> no aggro -> no state
//   transition -> the BT's fight branch is never reached. We call
//   Super::OnPossess FIRST, so if AFGCreatureController::OnPossess did that
//   copy, OUR creatures would be configured too -- they are not. Therefore the
//   copy lived in the vanilla BP_CreatureController_C event graph (the SAME BP
//   layer that also did RunBehaviorTree, W1.6) which our C++ controller
//   replaced wholesale (docs/07 §1). This file reproduces that missing copy.
//
// WHAT FGAIPerceptionComponent.h REVEALED (the packet's key unknown): it is a
// THIN UAIPerceptionComponent subclass overriding ONLY OnRegister() and
// HandleExpiredStimulus(), with a protected AFGCreatureController*
// mCreatureController back-ref (AI/FGAIPerceptionComponent.h) and NO public
// "configure-from-creature" entry point. So there is no native self-configuring
// path to prefer -- hand-wiring through the engine's own ConfigureSense API is
// the correct and only reachable mechanism. The back-ref + OnRegister override
// strongly imply the base ctor CreateDefaultSubobject's this component (so we
// INHERIT a live one) AND that OnRegister binds the controller's
// OnTargetPerceptionUpdated handler to the component's delegate (so the
// RECEIVING half is already wired in C++, leaving only sense-config as the gap).
// Both implications are confirmed at runtime by PFA.DumpController
// (PFAPerceptionDebug.cpp) on ours vs a vanilla creature; the step logging here
// + the ControllerPerceptionConfigured self-test surface the component-exists
// assumption at world load, so one launch is conclusive either way.
//
// SYMBOL/EXPORT STATUS (SHIPPING-EXPORT TRAP, docs/07 §1) -- this file adds ZERO
// novel FactoryGame direct-import symbols:
//   AAIController::GetAIPerceptionComponent()      AIController.h:441  INLINE (engine)
//   UAIPerceptionComponent::ConfigureSense                   :319  AIMODULE_API
//   UAIPerceptionComponent::GetSenseConfig<T>()              :306  inline template
//   UAIPerceptionComponent::SetDominantSense                 :300  AIMODULE_API
//   UAIPerceptionComponent::GetSensesConfigIterator          :322  AIMODULE_API
//   UAIPerceptionComponent::RequestStimuliListenerUpdate     :326  AIMODULE_API
//   UAIPerceptionComponent::GetListenerId                    :281  inline
//   UAIPerceptionComponent::OnTargetPerceptionUpdated (delegate ops)  inline
//   AFGCreature::GetPerceptionSettings()           FGCreature.h:370  FORCEINLINE
//       (FactoryGame, but INLINE -> compiled into THIS TU, no DLL import)
//   AFGCreatureController::OnTargetPerceptionUpdated  FGCreatureController.h:370
//       (FactoryGame protected UFUNCTION virtual; bound by NAME + vtable
//        dispatch via AddUniqueDynamic, never a direct symbol import -- the same
//        export-safety argument OnPossess makes for RunBehaviorTree. Reachable
//        from this derived member via the derived-qualified name.)
// ===========================================================================

#include "PFACreatureController.h"
#include "PrimalFauna.h"
#include "PFACheats.h"
// w47-1: the heartbeat reads the speed-trait watch's state off our own creature.
#include "PFACreatureBase.h"

#include "Creature/FGCreature.h"

#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Sight.h"

#include "Engine/World.h"
#include "TimerManager.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
// w45-3: TickAggroRangeDecay calls UCurveFloat::GetFloatValue, so the pointer's
// forward declaration on AFGCreature's getter is not enough here.
#include "Curves/CurveFloat.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "GameFramework/Pawn.h"

void APFACreatureController::WireCreaturePerception(AFGCreature* PossessedCreature)
{
	// Step 0: the inherited perception component. Do NOT CreateDefaultSubobject
	// our own -- a second component would orphan the base's OnRegister/back-ref
	// wiring. If this is ever null (the dump/self-test will scream it), the
	// documented fallback is ctor-creation, deliberately NOT done pre-emptively.
	UAIPerceptionComponent* Perception = GetAIPerceptionComponent();
	if (!Perception)
	{
		UE_LOG(LogPrimalFauna, Error,
			TEXT("[PFA] %s: WireCreaturePerception ABORT -> GetAIPerceptionComponent() is NULL (base ctor did not create one). Creature cannot perceive. Fallback: CreateDefaultSubobject<UFGAIPerceptionComponent> in APFACreatureController ctor (next build)."),
			*GetName());
		return;
	}

	const FFGCreaturePerceptionSettings& Settings = PossessedCreature->GetPerceptionSettings();

	// "Before" sense count for the success-chain diff (we only COUNT -- no deref).
	int32 SensesBefore = 0;
	for (auto It = Perception->GetSensesConfigIterator(); It; ++It) { ++SensesBefore; }

	int32 SensesConfiguredThisPass = 0;

	// Step 1: sight (idempotent -- skip if this component already has a sight
	// config, e.g. a controller reused across respawn).
	if (Settings.SightConfig)
	{
		if (Perception->GetSenseConfig<UAISenseConfig_Sight>() == nullptr)
		{
			Perception->ConfigureSense(*Settings.SightConfig);
			++SensesConfiguredThisPass;
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: WireCreaturePerception step1 ConfigureSense(Sight) OK (SightRadius=%.0f LoseSight=%.0f)"),
				*GetName(), Settings.SightConfig->SightRadius, Settings.SightConfig->LoseSightRadius);
		}
		else
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: WireCreaturePerception step1 Sight already configured on this component -- skipped (idempotent)."),
				*GetName());
		}
	}
	else
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: WireCreaturePerception step1 creature has NO SightConfig in mPerceptionSettings (species ctor built none?) -- sight will not work."),
			*GetName());
	}

	// Step 2: hearing (idempotent).
	if (Settings.HearingConfig)
	{
		if (Perception->GetSenseConfig<UAISenseConfig_Hearing>() == nullptr)
		{
			Perception->ConfigureSense(*Settings.HearingConfig);
			++SensesConfiguredThisPass;
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: WireCreaturePerception step2 ConfigureSense(Hearing) OK (HearingRange=%.0f)"),
				*GetName(), Settings.HearingConfig->HearingRange);
		}
		else
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: WireCreaturePerception step2 Hearing already configured on this component -- skipped (idempotent)."),
				*GetName());
		}
	}
	else
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: WireCreaturePerception step2 creature has NO HearingConfig in mPerceptionSettings."),
			*GetName());
	}

	// Step 3: dominant sense = Sight (docs/02 §5 sight-led design; the perception
	// system uses it to pick the authoritative stimulus location).
	if (Settings.SightConfig)
	{
		Perception->SetDominantSense(UAISense_Sight::StaticClass());
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: WireCreaturePerception step3 SetDominantSense(Sight)."), *GetName());
	}

	// Step 4: ensure the OnTargetPerceptionUpdated handler is bound. Bind ONLY
	// if nothing is bound yet -- if the base's UFGAIPerceptionComponent::
	// OnRegister already bound the controller's handler (likely, given its
	// mCreatureController back-ref), IsBound() is true and we leave it; if the
	// replaced vanilla BP was the binder, IsBound() is false and we supply it.
	// AddUniqueDynamic is itself dup-safe; the IsBound() guard additionally
	// avoids adding a SECOND routing path if the base ever bound a different
	// handler. Idempotent in every scenario.
	if (!Perception->OnTargetPerceptionUpdated.IsBound())
	{
		Perception->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &APFACreatureController::OnTargetPerceptionUpdated);
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: WireCreaturePerception step4 bound OnTargetPerceptionUpdated (was UNBOUND -- the replaced BP layer was vanilla's binder)."),
			*GetName());
	}
	else
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: WireCreaturePerception step4 OnTargetPerceptionUpdated already bound (base C++ OnRegister wired it) -- left as-is."),
			*GetName());
	}

	// Step 5: (re)register this listener so the newly-added senses take effect
	// now instead of on the next natural update.
	Perception->RequestStimuliListenerUpdate();

	int32 SensesAfter = 0;
	for (auto It = Perception->GetSensesConfigIterator(); It; ++It) { ++SensesAfter; }

	// Success-chain SUMMARY -- the ONE line the micro-test greps to confirm W1.8
	// landed. A healthy result: senses 0->2, listenerRegistered=true,
	// delegateBound=true (matching a vanilla creature's PFA.DumpController).
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: WireCreaturePerception DONE component=%s senses %d->%d (configured %d this pass) listenerRegistered=%s delegateBound=%s"),
		*GetName(), *Perception->GetClass()->GetName(), SensesBefore, SensesAfter, SensesConfiguredThisPass,
		Perception->GetListenerId().IsValid() ? TEXT("true") : TEXT("false"),
		Perception->OnTargetPerceptionUpdated.IsBound() ? TEXT("true") : TEXT("false"));

	// Step 6: the gated 30s state heartbeat (see header). Set once per controller.
	if (!StateHeartbeatTimerHandle.IsValid())
	{
		GetWorldTimerManager().SetTimer(StateHeartbeatTimerHandle, this,
			&APFACreatureController::LogCreatureStateHeartbeat, 30.0f, /*bLoop=*/true);
	}

	// NOTE (w45-3, review D1): the first blackboard publish deliberately does NOT
	// live here. WireCreaturePerception runs from OnPossess BEFORE
	// RunBehaviorTree(), and RunBehaviorTree -> UseBlackboard() is what creates the
	// blackboard component at all -- so calling the writers here would either
	// silently no-op on a null blackboard (fixing nothing) or, worse, take the
	// readback-failure path and latch bSpawnerKeyWriteFailed permanently on what is
	// only a timing problem. They are called from OnPossess's bStarted branch
	// instead, where GetBlackboardComponent() is provably non-null.

	// Step 7 (w26-1): keep the AI focus on whatever we are hunting, so the
	// pawn's rotation policy has something to aim at while closing. Another
	// job the vanilla BP controller layer did for its creatures.
	if (!FocusSyncTimerHandle.IsValid())
	{
		GetWorldTimerManager().SetTimer(FocusSyncTimerHandle, this,
			&APFACreatureController::TickFocusSync, 0.25f, /*bLoop=*/true);
	}
}

void APFACreatureController::TickAggroRangeDecay(float DeltaSeconds)
{
	// w45-2 -- DE-ESCALATION THAT DOES NOT DEPEND ON AN UNKNOWN.
	// The w45-1 curve reshape assumed that zeroing visibility gain past 3,000cm
	// would make an already-seen target stop being visible, letting the native
	// mAggroReductionRate (5/s, "when a target is not visible") shed aggro. Cold
	// review showed that is probably backwards: the curve is a RATE OF INCREASE on
	// an accumulator, so zero gain likely LATCHES visibility at 1.0 instead of
	// clearing it, leaving aggro frozen above mAggroFightThreshold forever. It
	// cannot be settled statically -- the native visibility update is closed source
	// and AFGCreature owns no visibility-loss rate.
	//
	// So rather than bet the behaviour on which of three readings is true, drive
	// the decay ourselves in exactly the region where the NATIVE GAIN IS PROVABLY
	// ZERO (beyond the visibility curve's last key). Two systems never fight over
	// the value: inside the curve's range this does nothing at all, and outside it
	// the native contribution is 0 by construction, so the sum is unambiguous.
	// ModifyAggroForTarget is an already-imported BlueprintCallable this codebase
	// uses elsewhere, so no new export surface.
	//
	// Kill-switch, same pattern as pfa.BiteImpactDelay / pfa.FootPlantAuto /
	// pfa.LocomotionAnims: `pfa.AggroDecayAtRange 0` reverts to relying purely on
	// the native path, which is the A/B that tells us which one is really working.
	static TAutoConsoleVariable<int32> CVarPFAAggroDecayAtRange(
		TEXT("pfa.AggroDecayAtRange"), 1,
		TEXT("1 = we explicitly shed aggro for targets beyond the visibility curve's range (where native gain is 0), so creatures de-escalate and return to roaming. 0 = rely only on the native not-visible decay (which may never run if visibility latches)."),
		ECVF_Default);

	if (CVarPFAAggroDecayAtRange.GetValueOnGameThread() == 0)
	{
		return;
	}

	const APawn* DecayPawn = GetPawn();
	const AFGCreature* DecayCreature = Cast<AFGCreature>(DecayPawn);
	if (!DecayCreature)
	{
		return;
	}

	const UCurveFloat* VisibilityCurve = DecayCreature->GetVisibilityGainCurve();
	if (!VisibilityCurve)
	{
		return;
	}

	// The rate is the creature's OWN authored reduction rate, not a new magic
	// number -- so this path decays at exactly the speed the species already
	// declares for losing interest, and tuning stays in one place.
	const float ReductionRate = DecayCreature->GetAggroReductionRate();
	if (ReductionRate <= 0.0f)
	{
		return;
	}

	// w45-3 (review D6): ACCUMULATE TO 0.25s instead of running every frame. Decay
	// is a rate x dt integration, so batching is behaviourally IDENTICAL and ~15x
	// cheaper -- it removes a per-creature TArray heap allocation per frame (20
	// creatures at 60fps was ~1200 allocations/s to usually do nothing). The log
	// comment below already claimed a 0.25s cadence that did not exist; now it does.
	constexpr float DecayInterval = 0.25f;
	mRangeDecayAccumulator += DeltaSeconds;
	if (mRangeDecayAccumulator < DecayInterval)
	{
		return;
	}
	const float AccumulatedDelta = mRangeDecayAccumulator;
	mRangeDecayAccumulator = 0.0f;

	// Cheap early-out against the const ref before the snapshot copy below.
	if (GetAggroTargets().Num() == 0)
	{
		return;
	}

	const float FightThreshold = DecayCreature->GetAggroFightThreshold();
	const FVector SelfLocation = DecayPawn->GetActorLocation();

	// Copy: ModifyAggroForTarget mutates the native array we would be iterating.
	const TArray<FFGCreatureAggroData> Snapshot = GetAggroTargets();

	// w46-1 -- THE w45-3 TRIGGER WAS TOO NARROW, and the user found it in one
	// session: "the one that aggroed on me, then lost interest, just stood at where
	// it lost interest."
	// w45-3 only disengaged on a THRESHOLD CROSSING (aggro falling from >=50 through
	// 50), because it assumed de-escalation always follows a committed fight. In
	// game both creatures entered range-decay at aggro 40.5 and 3.5 -- ALERTED but
	// never committed -- so `Before >= FightThreshold` was never true, the crossing
	// never happened, ResetTargetingState never ran, and each creature drained to
	// ~0 aggro while keeping its stale target and standing exactly where it stopped.
	// A creature with no aggro left has nothing to pursue whether or not it ever
	// crossed a fight threshold, so FULL DRAIN is equally a disengage event.
	bool bAnyAggroRemains = false;
	bool bDisengageEvent = false;
	const TCHAR* DisengageReason = TEXT("aggro decayed at range");
	// w48-1: WHICH target triggered the disengage event, kept alongside the reason
	// (both last-write-wins, so they always describe the same entry). Used only by
	// the veto line below, to state whether the vetoer is that same actor -- a
	// SELF-VETO, which is never legitimate and is the exact shape of the w46-1 bug
	// this packet fixes. Naming the relationship is what makes that class of bug
	// impossible to hide behind a line that merely says "something still holds aggro".
	const AActor* DisengageTarget = nullptr;
	// w46-1: entries that are ALREADY at zero when we look. See the stale-target
	// diagnostic after the loop -- this is the one residual hole in this path and it
	// is reported rather than acted on, deliberately.
	int32 StaleZeroInRegion = 0;
	// w47-1 (w46-1 cold review F4): the two shapes the w46-1 diagnostic was BLIND to,
	// tallied so one line distinguishes all four.
	//  * NullTargetEntries -- the aggro entry survived but its Target is gone. This
	//    is what "the creature killed the hog and then stood there" looks like from
	//    here, i.e. the natural end of a multi-target fight and precisely the
	//    reported symptom. The loop `continue`s past these, so before this tally
	//    they were invisible.
	//  * ZeroAggroExemptEntries -- zero-aggro entries that never reach the
	//    zero-check because they are inside the curve's range or are recent
	//    aggressors (a recent aggressor sitting at zero is the second blind shape).
	int32 NullTargetEntries = 0;
	int32 ZeroAggroExemptEntries = 0;
	// w47-1 (review F1): WHY a disengage was vetoed. The w46-1 veto widened from
	// "some target is still above the fight threshold" to "some target holds any
	// aggro at all", which makes disengaging strictly HARDER than w45-3 -- a hog at
	// 5 aggro 12m away can now veto the reset for a player who drained to zero at
	// 35m, reproducing the exact "stood where it lost interest" symptom this path
	// exists to fix. The behaviour is kept (forgetting the nearby hog would be
	// worse); what changes is that it can no longer happen SILENTLY, which
	// previously had to be inferred from the ABSENCE of a ResetTargetingState line
	// while the stale-target line was suppressed too (it requires !bDisengageEvent).
	const AActor* VetoTarget = nullptr;
	float VetoAggro = 0.0f;
	float VetoDistanceCm = -1.0f;
	bool bVetoInDecayRegion = false;

	// w47-2 (w47-1 cold review W1): ONE place that records "aggro remains", so the
	// veto tracker cannot be updated at one site and forgotten at the other -- which
	// is exactly what w47-1 did. `bAnyAggroRemains` is set in TWO places (the
	// not-draining/exempt branch below, and a target that still holds aggro after
	// this beat's shed), but only the first recorded who did it. The ordinary
	// two-targets-at-range case therefore printed
	//   `DISENGAGE VETOED -- ... but <an entry we could not name> still holds 0.0 aggro`
	// -- a line asserting a veto while reporting no vetoer and no aggro, which
	// defeats the entire purpose of the F1 line (naming WHO vetoed).
	// Parameter names deliberately differ from the loop locals: shadowing a captured
	// variable is a warning this engine build treats as an error.
	auto NoteAggroRemains = [&VetoTarget, &VetoAggro, &VetoDistanceCm, &bVetoInDecayRegion, &bAnyAggroRemains]
		(const AActor* InTarget, float InAggro, float InDistanceCm, bool bInRegion)
	{
		bAnyAggroRemains = true;
		if (InAggro > VetoAggro)
		{
			VetoAggro = InAggro;
			VetoTarget = InTarget;
			VetoDistanceCm = InDistanceCm;
			bVetoInDecayRegion = bInRegion;
		}
	};

	for (const FFGCreatureAggroData& AggroData : Snapshot)
	{
		AActor* AggroTarget = AggroData.Target;
		if (!AggroTarget)
		{
			++NullTargetEntries;
			continue;
		}

		const float DistanceCm = FVector::Dist(SelfLocation, AggroTarget->GetActorLocation());
		// Only where the native gain is provably nothing. FRichCurve extrapolates
		// PostInfinity as Constant, so this is 0.0 for every distance past the last
		// key -- the exact complement of the region the native path drives.
		const bool bInDecayRegion = (VisibilityCurve->GetFloatValue(DistanceCm) <= 0.0f);

		// w45-3 (review D3): NEVER DRAIN SOMEONE WHO JUST SHOT US. Damage-aggro does
		// not run through either curve (it is the w18-11 path), so a player sniping
		// from 31m would otherwise land hits into a target we are actively draining
		// at 5/s and be ignored -- "I shot it and it didn't care" would be a real
		// regression introduced by this very function. WasRecentlyDamagedBy is the
		// mod's own existing predicate over the inherited protected damage state.
		const bool bRecentAggressor = WasRecentlyDamagedBy(AggroTarget);

		if (!bInDecayRegion || bRecentAggressor)
		{
			// Anything we are NOT draining still counts as live interest, so it must
			// veto the disengage below -- but only if it actually HOLDS aggro. A
			// zero-aggro entry is not interest, and letting one veto would mean a
			// creature that has genuinely forgotten everything could be pinned by a
			// merely-perceived actor standing next to it (perception registers a seen
			// actor at zero aggro). w45-3 skipped zero-aggro entries before reaching
			// this branch, so this preserves that reviewed semantic explicitly.
			if (AggroData.Aggro > KINDA_SMALL_NUMBER)
			{
				// w47-1 (review F1) / w47-2 (review W1): remember the STRONGEST vetoing
				// entry, so the veto line below can name who did it. One float + one
				// pointer, no allocation, and only inside a branch that already runs.
				NoteAggroRemains(AggroTarget, AggroData.Aggro, DistanceCm, bInDecayRegion);
			}
			else
			{
				// w47-1 (review F4): a zero-aggro entry that is exempt from draining --
				// invisible to StaleZeroInRegion, and one of the two shapes that made
				// the w46-1 stale-target diagnostic print nothing.
				++ZeroAggroExemptEntries;
			}
			// Region/exemption exit: forget the log state so a later re-entry
			// reports its first shed instead of being suppressed as "unchanged".
			mRangeDecayLoggedTargets.Remove(FObjectKey(AggroTarget));
			continue;
		}

		// Nothing left to shed. w45-3 skipped these at the top of the loop, before
		// distance was even measured; measuring first is what lets the diagnostic
		// after the loop distinguish "forgotten, in the decay region, still targeted"
		// from "out of range of this mechanism entirely".
		// Clearing the log key here is also the fix for a second-de-escalation blind
		// spot: without it, a target drained once would never print another
		// `range-decay ENTERED` line for the rest of the creature's life, so the
		// SECOND time a player aggroed and walked away the forensics went silent.
		if (AggroData.Aggro <= KINDA_SMALL_NUMBER)
		{
			++StaleZeroInRegion;
			mRangeDecayLoggedTargets.Remove(FObjectKey(AggroTarget));
			continue;
		}

		const float Before = AggroData.Aggro;
		// w46-1: CLAMP THE DELTA WE ACTUALLY APPLY, not just the number we reason
		// about. w45-3's FMath::Max(0.0f, ...) clamped the local `After` while the
		// full unclamped delta still went to ModifyAggroForTarget -- so on the last
		// beat of a drain the log said "-> 0.0" while the native entry could hold a
		// small negative value, and the log has to be true before anything else is.
		// (It also removes the reliance on the native side clamping at zero, which
		// is closed source and never verified.)
		const float AppliedDelta = FMath::Min(ReductionRate * AccumulatedDelta, Before);
		const float After = Before - AppliedDelta;
		const bool bThisCrossed = (Before >= FightThreshold) && (After < FightThreshold);
		// `Before > 0` is already guaranteed by the zero-aggro branch above, so this
		// fires only on a REAL transition to zero driven by this function -- never on
		// a target that arrived already drained.
		const bool bThisDrained = (After <= KINDA_SMALL_NUMBER);

		if (bThisCrossed || bThisDrained)
		{
			bDisengageEvent = true;
			DisengageTarget = AggroTarget;
			// Name which one actually happened -- they are different stories: a
			// crossing means it gave up a committed fight, a drain means it never
			// committed and has now forgotten the target entirely.
			DisengageReason = bThisCrossed
				? TEXT("aggro decayed below fight threshold at range")
				: TEXT("aggro fully drained at range (never reached fight threshold)");
		}

		// w45-3 (review D2): pass bImmediateStateUpdate on the CROSSING FRAME ONLY.
		// Forcing a native state update every tick per target would thrash; forcing
		// it never risks the state machine not noticing at all, since
		// mAggroFightThreshold is documented as a transition-IN condition and
		// nothing states that falling below it transitions OUT.
		ModifyAggroForTarget(AggroTarget, -AppliedDelta,
			/*bImmediateStateUpdate=*/bThisCrossed || bThisDrained);

		// The SECOND site, and the common one -- a target that is draining but still
		// holds aggro this beat.
		//
		// w48-1: THE THRESHOLD IS PART OF THE CONDITION, and it must stay that way.
		// A DRAINING target below the fight threshold is a target we are ABANDONING,
		// so it is not remaining interest and must not veto its own disengage.
		// w46-1 wrote `After > KINDA_SMALL_NUMBER` here, which meant the very target
		// whose crossing set bDisengageEvent immediately registered itself as the
		// vetoer at (say) 49.5 aggro -- a SELF-VETO. The consequence was total: the
		// crossing rule could only ever reset when EVERY draining target hit exactly
		// zero on the same beat, i.e. the crossing rule never fired once in three
		// user sessions, silently collapsing the two-rule design (cross 50 OR drain
		// to 0) that was chosen deliberately, down to drain-to-0 only.
		// So do NOT "helpfully" widen this back to `> 0`: that reintroduces the bug.
		// A draining target at or above the threshold DOES still veto -- we remain
		// committed to it -- and the not-draining/exempt branch above (a recent
		// aggressor, or anything inside the curve's range) vetoes on ANY aggro, which
		// is the live-nearby-interest case and is deliberately left untouched.
		//
		// w48-2 HARDENING (the w48-1 review's recommendation A, taken as its own
		// clause): `&& After > KINDA_SMALL_NUMBER`. At FightThreshold == 0 the
		// w48-1 condition alone kills BOTH disengage rules -- the crossing needs
		// `After < 0`, which `AppliedDelta = FMath::Min(Rate*dt, Before)` makes
		// impossible by clamping After to >= 0, and `After >= 0` is universally
		// true, so a fully-drained target self-vetoes and the species could NEVER
		// disengage. That is strictly worse than the w46-1 bug w48-1 fixed.
		// Not reachable by omission today (vanilla defaults mAggroFightThreshold =
		// 75 at FGCreature.cpp:41; only PFASicklejaw.cpp authors it, at 50), but the
		// roster is expanding -- and the clause is a PROVABLE NO-OP at any positive
		// threshold: After >= FightThreshold > 0 already implies After > 0.
		// Complement when species #2 lands: assert mAggroFightThreshold > 0 at the
		// authoring seam (species ctor or PFA.SelfTest), per check-at-the-seam.
		if (After >= FightThreshold && After > KINDA_SMALL_NUMBER)
		{
			NoteAggroRemains(AggroTarget, After, DistanceCm, bInDecayRegion);
		}

		// w45-3 (review D4): log EVENTS, not samples. The old whole-unit bucket fired
		// ~5 lines/s/target for a full ~20s de-escalation; this project already lost
		// 46% of one session's log to exactly that. Entry and threshold-crossing carry
		// the entire forensic content in two lines.
		if (PFADiag::IsEnabled())
		{
			const FObjectKey TargetKey(AggroTarget);
			if (!mRangeDecayLoggedTargets.Contains(TargetKey))
			{
				mRangeDecayLoggedTargets.Add(TargetKey);
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] %s: range-decay ENTERED on %s -- dist=%.0f (visibility gain 0 beyond the curve), aggro=%.1f falling at %.1f/s toward fightThreshold=%.0f. Slope tells you the mechanism: ~%.0f/s = native visibility LATCHED (we are the only driver); ~%.0f/s = it cleared and native decay runs too."),
					*GetName(), *AggroTarget->GetName(), DistanceCm, Before,
					ReductionRate, FightThreshold, ReductionRate, ReductionRate * 2.0f);
			}
			if (bThisCrossed || bThisDrained)
			{
				// w47-1 (review F6): PROVE the applied delta at the native boundary.
				// `After` is our own arithmetic, which is exactly the class of claim the
				// w46-1 clamp fix existed to stop making -- the log has to be true
				// before anything else is. GetAggroForTarget is already an import of
				// this module (PFACreatureController.cpp:463, ForceAggro's readback), so
				// this adds no export surface, and it is read ONLY on the transition
				// frame inside the diag gate. If readback ever disagrees with After, the
				// native side is transforming our delta and the whole decay model needs
				// revisiting.
				const float Readback = GetAggroForTarget(AggroTarget);
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] %s: range-decay %s on %s -- aggro %.1f -> %.1f readback=%.2f%s (threshold %.0f), state update forced. If the creature does not resume roaming after this, the native machine is holding state on something other than aggro."),
					*GetName(),
					bThisCrossed ? TEXT("CROSSED below fightThreshold") : TEXT("FULLY DRAINED"),
					*AggroTarget->GetName(), Before, After, Readback,
					FMath::IsNearlyEqual(Readback, After, 0.01f)
						? TEXT("")
						: TEXT(" <-- READBACK DISAGREES WITH OUR ARITHMETIC: the native side is transforming the delta"),
					FightThreshold);
			}
		}
	}

	// w45-3 (review D2): reuse the mechanism that already has in-game evidence.
	// Driving aggro under the threshold is NOT provably enough to leave CS_Fight --
	// mCurrentTarget is independent of aggro, so the creature could sit in Fight on
	// a stale target with zero aggro. ResetTargetingState is the w40-1 give-up path
	// (NullifyAllAggro + SetCurrentTarget(nullptr) + TriggerStateUpdate) and is the
	// one disengage route this mod has actually watched work in game. Fires at most
	// once per de-escalation, and only when nothing is left worth fighting.
	// w46-1: fires on a threshold crossing OR a full drain (see above), and only
	// when NO target holds any meaningful aggro -- so a creature that has genuinely
	// forgotten everything returns to roaming instead of standing where it stopped.
	if (bDisengageEvent && !bAnyAggroRemains)
	{
		// The aggro array is about to be emptied, so the per-target log state has to
		// go with it or the NEXT de-escalation of the same target prints no
		// `range-decay ENTERED` line and the forensics silently thin out over a
		// session (the entries themselves are gone, so the in-loop removal above
		// never gets another chance to run for them).
		mRangeDecayLoggedTargets.Reset();
		bLoggedStaleZeroAggroTarget = false;
		bLoggedStaleZeroLongHold = false;
		mStaleZeroConsecutiveBeats = 0;
		ResetTargetingState(DisengageReason);
		// w48-2: ...and then MAKE IT ROAM. Proven in the w48-1 session: the reset
		// above ran (its always-on line fired with the crossing reason, which had
		// never appeared in any log before), aggro was 0 and currentTarget none --
		// and the native machine still sat in CS_Alert on every subsequent
		// heartbeat. The reset is necessary and was not sufficient.
		//
		// BOUNDED TO ONE CALL PER DISENGAGE EVENT, which is what keeps this from
		// fighting the state machine in a loop: ResetTargetingState empties
		// mAggroTargets, so the next beat hits the `GetAggroTargets().Num() == 0`
		// early-out at the top of this function and cannot reach here again until
		// perception registers fresh aggro.
		ForceRoamAfterDisengage(DisengageReason);
		return;
	}

	// w47-1 (review F1): THE VETO, SPOKEN OUT LOUD. A disengage event happened and
	// was refused because something else still holds aggro. Without this line the
	// only evidence is a missing ResetTargetingState, and the stale-target
	// diagnostic below cannot cover it (it requires !bDisengageEvent). Fires at most
	// once per disengage event -- a crossing happens once per drain and a full drain
	// once per target -- so no throttle is needed beyond the diag gate.
	if (bDisengageEvent && bAnyAggroRemains && PFADiag::IsEnabled())
	{
		// w48-1: STATE THE RELATIONSHIP, not just the two names. The w46-1 self-veto
		// sat in this log for three sessions reading as a legitimate multi-target
		// veto, because the line printed the same actor twice without ever saying
		// they were the same actor. A veto by the very target that just disengaged is
		// a contradiction and can only be a bug; say so in the line itself.
		//
		// ⚠ w48-2 -- DO NOT DELETE THIS AS DEAD CODE. With the w48-2 hardening clause
		// in place (`After >= FightThreshold && After > KINDA_SMALL_NUMBER`, ~85 lines
		// up), a SELF-VETO is structurally unreachable at any POSITIVE fight
		// threshold -- so a future reader will find this tripwire never firing and be
		// tempted to remove it.
		//
		// w48-3 (review W3) CORRECTS THE REASON THIS BANNER USED TO GIVE. It said the
		// tripwire "is now precisely the DETECTOR for the threshold-0 hole". That is
		// FALSE, and the same clause proves it: at FightThreshold == 0 the only
		// possible disengage event is bThisDrained (After == 0), and the veto gate
		// `After >= FightThreshold && After > KINDA_SMALL_NUMBER` is false for it --
		// so the drained target cannot register as a vetoer at all. (The crossing rule
		// would need After < 0, which `AppliedDelta = FMath::Min(Rate*dt, Before)`
		// makes impossible.) The clause that closes the hole ALSO closes the detector:
		// bSelfVeto is unreachable at threshold 0 too. A false rationale inside a DO
		// NOT DELETE banner is exactly how the w46-1 self-veto hid in plain sight for
		// three sessions, and this project's standard is that the log has to be true
		// before anything else is -- which applies to load-bearing comments as well.
		//
		// THE HONEST REASON TO KEEP IT: this is a STANDING INVARIANT ASSERT. The veto
		// gate (`After >= FightThreshold`) and the crossing test (`Before >=
		// FightThreshold && After < FightThreshold`) are complementary by
		// construction, and that complementarity is precisely what makes a target that
		// is being abandoned structurally unable to count as remaining interest in
		// itself. If this line ever fires, someone has broken that relationship --
		// widened the veto back toward `> 0`, changed the crossing test, or let the
		// applied delta go unclamped -- and it will say so by name instead of
		// resurfacing as a fresh mystery. That is worth asserting forever, and it
		// costs one pointer comparison inside a branch that already runs.
		//
		// (The threshold-0 hole itself is still real and still unguarded here; its
		// complement is the authoring-seam assert `mAggroFightThreshold > 0` noted at
		// the hardening clause, which is where it actually belongs.)
		const bool bSelfVeto = (VetoTarget != nullptr) && (VetoTarget == DisengageTarget);
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: range-decay DISENGAGE VETOED -- a target %s but %s still holds %.1f aggro (dist=%.0f, inDecayRegion=%d); not resetting.%s The veto is deliberate (forgetting a nearby creature would be worse), but if the creature now stands still, THIS is why."),
			*GetName(), DisengageReason,
			VetoTarget ? *VetoTarget->GetName() : TEXT("<an entry we could not name>"),
			VetoAggro, VetoDistanceCm, bVetoInDecayRegion ? 1 : 0,
			bSelfVeto
				? TEXT(" <-- the vetoer IS the target that just crossed/drained: this is a SELF-VETO and almost certainly a bug (a target being abandoned must not count as remaining interest).")
				: TEXT(""));
	}

	// ---- w46-1 DIAGNOSTIC ONLY: the one residual hole, named out loud. ----------
	// This function can only disengage on a transition it OBSERVES. If aggro reaches
	// zero without us taking the last step -- the native not-visible decay got there
	// first between two of our 0.25s beats, or the entry was registered at zero and
	// never gained any -- then no crossing and no drain is seen, nothing resets, and
	// the creature can sit on a stale mCurrentTarget with nothing left to pursue.
	// That is precisely the shape the user reported ("just stood at where it lost
	// interest"), so if it is still happening the log must say so instead of leaving
	// us to re-derive it from an absence of lines.
	//
	// Deliberately NOT wired to ResetTargetingState. Perception re-registers a
	// merely-SEEN actor at zero aggro, and the sight radius (4,000cm) extends past
	// the curve's last key (3,000cm), so a player simply STANDING at 35m produces
	// exactly this condition continuously -- acting on it would wipe targeting and
	// emit an always-on behaviour-event line four times a second. If this line shows
	// up in a session where a creature stood still, the correct fix is a debounced
	// stale-target timeout (dwell-based, like the w40-1 give-up clock), not a reset
	// from here. Throttled to once per episode, re-armed by any live aggro.
	// w47-1 (review F3/F4): TWO fixes to this diagnostic.
	//
	// F3 -- the boolean latch could SPAM. It was cleared on any beat failing the
	// condition, and the condition includes GetCurrentTarget() != nullptr, which
	// this codebase's own TickFocusSync comment documents as flickering to null
	// 1-2x/second during a live engagement (w26-3, session 18). Every flicker
	// re-armed the latch: ~2 lines/s, indefinitely. Replaced with a CONSECUTIVE-BEAT
	// DEBOUNCE -- the condition must hold for 1.5s (6 beats at 0.25s, raised from 4
	// by the w47-1 review: one beat of margin over a 0.75 Hz flicker was not margin)
	// before anything prints, which a 1-2Hz flicker can never satisfy. The debounce
	// is also deliberately the front half of the dwell timer that is the likely
	// follow-up fix, and w47-2 makes that literal: the printed hold is now the
	// MEASURED one and a second line reports a 5s continuous hold.
	//
	// F4 -- the trigger was blind to its most likely real cause. It counted only
	// non-null, in-region, non-aggressor, zero-aggro entries, so the two ordinary
	// shapes printed NOTHING: the target DIED (entry survives with Target == nullptr
	// -- the natural end of a fight, and exactly the reported symptom) and a recent
	// aggressor sitting at zero. Both are now tallied, the null case joins the
	// trigger, and every tally plus the entry count and state is printed, so ONE
	// line distinguishes all four shapes.
	const AActor* StaleTarget = GetCurrentTarget();
	const bool bStaleZeroTargetHeld =
		(!bDisengageEvent && !bAnyAggroRemains && (StaleZeroInRegion + NullTargetEntries) > 0 && StaleTarget != nullptr);
	if (!bStaleZeroTargetHeld)
	{
		// Cleared on ANY beat that does not meet the condition, not only on live
		// aggro -- a latch that outlives the situation it describes reports the first
		// episode and hides every later one.
		bLoggedStaleZeroAggroTarget = false;
		bLoggedStaleZeroLongHold = false;
		mStaleZeroConsecutiveBeats = 0;
		return;
	}

	++mStaleZeroConsecutiveBeats;
	// w47-2 (w47-1 cold review I3): 6 beats x 0.25s = 1.5s, up from 4 (1.0s). The
	// debounce exists to reject the 1-2 Hz current-target flicker (w26-3), and at 4
	// beats the margin was exactly ONE beat -- a 0.75 Hz flicker satisfies it. Six
	// costs nothing (this is a diagnostic that fires once per episode) and buys real
	// margin.
	constexpr int32 StaleZeroDebounceBeats = 6;
	// w47-2 (I2): a SECOND report at a long hold, so the dwell-based stale-target
	// timeout that is the likely follow-up fix can be sized from a measured number
	// instead of a guess. 20 beats = 5.0s: long enough that no flicker or transient
	// reaches it, short enough that a session with one motionless creature produces
	// the sample.
	constexpr int32 StaleZeroLongHoldBeats = 20;
	if (mStaleZeroConsecutiveBeats < StaleZeroDebounceBeats)
	{
		return;
	}

	// w47-2 (w47-1 cold review I2): print the MEASURED hold, not the constant.
	// w47-1 printed `StaleZeroDebounceBeats * DecayInterval`, and since the latch
	// fires on exactly the beat the debounce is met, that could only ever say
	// "1.00" -- while the comment claimed the number existed so the dwell-timer work
	// "starts from a measured number". It never produced one. This does.
	const float StaleHeldSeconds = static_cast<float>(mStaleZeroConsecutiveBeats) * DecayInterval;

	// The latch is set only when the line is actually PRINTED, so enabling pfa.Diag
	// after the fact still yields the report (the w18-3 verdict throttle has the
	// opposite property and it cost a session).
	if (!bLoggedStaleZeroAggroTarget && PFADiag::IsEnabled())
	{
		bLoggedStaleZeroAggroTarget = true;
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: STALE TARGET (held %.2fs) -- currentTarget=%s state=%s, nothing holds aggro. entries=%d of which staleZeroInRegion=%d nullTarget(target destroyed/GCd)=%d zeroAggroExempt(in-curve-range or recent aggressor)=%d. This mechanism never saw the drop, so it has no disengage event to fire on; the creature may stand where it lost interest. nullTarget>0 means it most likely KILLED what it was fighting. Fix if this correlates with a motionless creature: a dwell-based stale-target timeout (this debounce is its front half)."),
			*GetName(),
			StaleHeldSeconds,
			*StaleTarget->GetName(),
			*CreatureStateEnumToString(GetCurrentCreatureState()),
			Snapshot.Num(), StaleZeroInRegion, NullTargetEntries, ZeroAggroExemptEntries);
	}

	// The long-hold sample. Same episode, same latch discipline -- one line, and
	// only if the condition really held continuously for the whole window.
	if (mStaleZeroConsecutiveBeats >= StaleZeroLongHoldBeats && !bLoggedStaleZeroLongHold && PFADiag::IsEnabled())
	{
		bLoggedStaleZeroLongHold = true;
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: STALE TARGET STILL HELD (%.2fs continuous) -- currentTarget=%s state=%s, still nothing holding aggro. entries=%d staleZeroInRegion=%d nullTarget=%d zeroAggroExempt=%d. THIS is the number to size a dwell-based stale-target timeout from: the creature has held a dead target this long without disengaging on its own."),
			*GetName(),
			StaleHeldSeconds,
			*StaleTarget->GetName(),
			*CreatureStateEnumToString(GetCurrentCreatureState()),
			Snapshot.Num(), StaleZeroInRegion, NullTargetEntries, ZeroAggroExemptEntries);
	}
}

void APFACreatureController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	TickAggroRangeDecay(DeltaTime);

	// w46-1: BEFORE every early return below. This tracks movement start/stop
	// continuously (the logging inside it is diag-gated, the tracking is not), so
	// putting it after the `!ControlledPawn` return or the disengage `return` would
	// leave legs half-recorded exactly when the creature is doing something
	// interesting. Same lesson as w44-1, applied one layer up: a measurement that
	// only runs on the happy path measures the happy path.
	TickRoamLegTelemetry(DeltaTime);

	// The mode CVar lives in PFACreatureBase.cpp's anonymous namespace; look
	// it up by name so neither file has to export it.
	// w40-1 disengage knobs. Live-tunable because "how long before it loses
	// interest" is a feel judgement that can only be settled in game.
	static TAutoConsoleVariable<float> CVarPFAGiveUpDistance(
		TEXT("pfa.GiveUpDistance"), 4500.0f,
		TEXT("Distance (cm) beyond which a Sicklejaw starts losing interest in its target. 0 disables disengaging (it will watch you forever)."),
		ECVF_Default);
	static TAutoConsoleVariable<float> CVarPFAGiveUpSeconds(
		TEXT("pfa.GiveUpSeconds"), 8.0f,
		TEXT("How long (s) the target must stay beyond pfa.GiveUpDistance before the creature disengages and goes back to roaming."),
		ECVF_Default);

	static IConsoleVariable* RotationModeVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("pfa.RotationMode"));
	const int32 Mode = RotationModeVar ? RotationModeVar->GetInt() : 2;

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	// Focus target with the same grace window the focus sync uses, so a
	// flickering native target can't make the body swing away and back.
	AActor* Target = GetCurrentTarget();
	if (!Target)
	{
		Target = LastValidFocusTarget.Get();
	}

	// ---- w40-1 DISENGAGE: lose interest in a target that has walked away. ----
	// The user went far away and the creatures kept standing and turning to
	// watch. That is not a roaming bug -- a creature that never stops being
	// alert can never enter the roam branch at all, so the w32-1 roam-band fix
	// was downstream of the real cause.
	//
	// Give-up distance and dwell are deliberately generous: a predator that
	// forgets you the instant you step back is as wrong as one that stares
	// forever. Research puts real hunts at 85-90% abandoned, and abandonment is
	// about the payoff no longer being worth the effort -- so the trigger is
	// "you are far away AND have stayed far away", not a flicker of distance.
	static IConsoleVariable* GiveUpDistVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("pfa.GiveUpDistance"));
	static IConsoleVariable* GiveUpTimeVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("pfa.GiveUpSeconds"));
	const float GiveUpDistCm = GiveUpDistVar ? GiveUpDistVar->GetFloat() : 4500.0f;
	const float GiveUpSeconds = GiveUpTimeVar ? GiveUpTimeVar->GetFloat() : 8.0f;

	if (Target && GiveUpDistCm > 0.0f)
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const float Dist = FVector::Dist(Target->GetActorLocation(), ControlledPawn->GetActorLocation());
		if (Dist > GiveUpDistCm)
		{
			if (OutOfRangeSinceSeconds <= 0.0)
			{
				OutOfRangeSinceSeconds = Now;
			}
			else if ((Now - OutOfRangeSinceSeconds) >= GiveUpSeconds)
			{
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] %s: DISENGAGING -- %s has been %.0fm away for %.0fs. Going back to its own business."),
					*GetName(), *Target->GetName(), Dist / 100.0f, GiveUpSeconds);
				OutOfRangeSinceSeconds = 0.0;
				LastValidFocusTarget = nullptr;
				ResetTargetingState(TEXT("target out of range too long -- disengaged"));
				// w48-2: the SAME stuck-state applies here. This path also ends in
				// ResetTargetingState, so it inherits the proven "reset ran, machine
				// stayed in CS_Alert" behaviour verbatim; fixing only the range-decay
				// path would leave the w40-1 give-up half-fixed for no reason.
				// Also bounded to one call per disengage: GetCurrentTarget() is now
				// null and LastValidFocusTarget was just cleared, so `Target` is null
				// on the next tick and this block cannot re-enter until the creature
				// re-acquires something.
				ForceRoamAfterDisengage(TEXT("target out of range too long -- disengaged"));
				return;   // no yaw this tick; the creature is done watching
			}
		}
		else
		{
			// Came back into range: the clock restarts, so a player who leaves
			// and returns does not get a half-expired grace period.
			OutOfRangeSinceSeconds = 0.0;
		}
	}

	// ---- Diagnostic (throttled to ~1Hz, diag-gated): the ONE readout that
	// explains why modes 0/1 are inert. If desiredYaw and controlYaw agree but
	// pawnYaw never converges, the movement component is ignoring the
	// controller; if controlYaw itself tracks pawnYaw, the base's
	// UpdateControlRotation is slaving control rotation TO the pawn (the
	// bSetControlRotationFromPawnOrientation pattern) and no focus we set can
	// ever steer it.
	RotationDiagAccumulator += DeltaTime;
	if (PFADiag::IsEnabled() && Target && RotationDiagAccumulator >= 1.0f)
	{
		RotationDiagAccumulator = 0.0f;
		FVector ToTarget = Target->GetActorLocation() - ControlledPawn->GetActorLocation();
		ToTarget.Z = 0.0f;
		const float DesiredYaw = ToTarget.Rotation().Yaw;
		const float PawnYaw = ControlledPawn->GetActorRotation().Yaw;
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: yaw-diag mode=%d desired=%.0f pawn=%.0f control=%.0f delta=%.0f target=%s"),
			*GetName(), Mode, DesiredYaw, PawnYaw, GetControlRotation().Yaw,
			FMath::FindDeltaAngleDegrees(PawnYaw, DesiredYaw), *Target->GetName());
	}

	if (Mode != 2 || !Target)
	{
		return;
	}

	// ---- Mode 2: drive the body yaw ourselves, at the same rate the other
	// modes were configured for. Yaw only -- pitch/roll are left to the game
	// (mAlignRotationToFloor territory).
	// w35-1 (user, in game): "sometimes they will be pointed at the other
	// creature but slide backwards to get to me."
	//
	// Exactly what this code did. It always aimed the body at GetCurrentTarget(),
	// while the behaviour tree independently decided where to WALK. When those
	// two disagree -- a creature fighting another creature while the player is
	// its aggro target, or mid-switch between targets -- the body locks onto one
	// and travels toward the other, so it moonwalks.
	//
	// An animal turns to face where it is GOING while it is going there, and
	// only turns to face a target when it stops. So: above a walking pace, aim
	// down the velocity; below it, aim at the target. This also means the gait
	// clips finally point the way the creature travels, which is what they were
	// authored for.
	const FVector Velocity = ControlledPawn->GetVelocity();
	const float Speed2D = Velocity.Size2D();
	// Below the roam/prowl band -- i.e. genuinely not travelling anywhere.
	constexpr float FaceTravelSpeedCms = 60.0f;

	FVector ToTarget;
	const bool bFaceTravel = (Speed2D >= FaceTravelSpeedCms);
	if (bFaceTravel)
	{
		ToTarget = Velocity;
	}
	else
	{
		ToTarget = Target->GetActorLocation() - ControlledPawn->GetActorLocation();
	}
	ToTarget.Z = 0.0f;
	if (ToTarget.SizeSquared() < 1.0f)
	{
		return;
	}

	if (PFADiag::IsEnabled() && bFaceTravel)
	{
		const FVector ToTargetDbg = (Target->GetActorLocation() - ControlledPawn->GetActorLocation()).GetSafeNormal2D();
		const float TargetVsTravelDeg = FMath::RadiansToDegrees(FMath::Acos(
			FMath::Clamp(FVector::DotProduct(ToTargetDbg, Velocity.GetSafeNormal2D()), -1.0f, 1.0f)));
		// A large angle here IS the moonwalk: it says the creature is travelling
		// somewhere other than at the thing it has targeted.
		if (TargetVsTravelDeg > 60.0f)
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: facing TRAVEL (speed=%.0f) -- target lies %.0f deg off the direction of travel (target=%s)"),
				*GetName(), Speed2D, TargetVsTravelDeg, *Target->GetName());
		}
	}

	const FRotator CurrentRotation = ControlledPawn->GetActorRotation();
	const FRotator DesiredRotation(CurrentRotation.Pitch, ToTarget.Rotation().Yaw, CurrentRotation.Roll);
	if (FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, DesiredRotation.Yaw)) < 1.0f)
	{
		return;   // already facing; don't fight other systems for sub-degree noise
	}
	ControlledPawn->SetActorRotation(
		FMath::RInterpConstantTo(CurrentRotation, DesiredRotation, DeltaTime, ManualYawRateDegPerSec));
}

void APFACreatureController::TickFocusSync()
{
	// w26-3: cheap live re-check so pfa.RotationMode can be A/B'd in-game
	// without a respawn (no-op when the mode hasn't changed).
	if (APFACreatureBase* ControlledCreature = Cast<APFACreatureBase>(GetPawn()))
	{
		ControlledCreature->ApplyRotationPolicy();
	}

	AActor* Target = GetCurrentTarget();

	// w26-3 FOCUS GRACE: in-game (session-18) the current target flickers to
	// null and back 1-2x/SECOND while a creature is actively engaging. With
	// focus-driven rotation that would swing the body away and back every
	// flicker, so hold the last valid target briefly and let genuine target
	// loss settle out over the grace window instead.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Target)
	{
		LastValidFocusTarget = Target;
		LastValidFocusTimeSeconds = Now;
	}
	else if (LastValidFocusTarget.IsValid() && (Now - LastValidFocusTimeSeconds) < FocusGraceSeconds)
	{
		Target = LastValidFocusTarget.Get();
	}

	AActor* Previous = LastFocusTarget.Get();
	if (Target == Previous)
	{
		return;
	}
	LastFocusTarget = Target;

	if (Target)
	{
		SetFocus(Target, EAIFocusPriority::Gameplay);
	}
	else
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: focus -> %s (was %s)"),
			*GetName(),
			Target ? *Target->GetName() : TEXT("cleared"),
			Previous ? *Previous->GetName() : TEXT("none"));
	}
}

void APFACreatureController::ApplyRoamBandToBlackboard()
{
	// The vanilla BehaviorTree's roam branch reads MinRoamingDistance /
	// MaxRoamingDistance from the BLACKBOARD. Vanilla's BP controller writes
	// them; we have no BP, so a C++-only creature gets [0..0] and dutifully
	// roams zero centimetres. Third instance of that same vanilla-parity gap
	// (after the un-launched BehaviorTree and the null aggro curves).
	//
	// Called from the heartbeat -- so it is SELF-HEALING. If anything
	// re-initialises the blackboard later (a BT restart, a state reset) the keys
	// come back on the next beat instead of staying zero for the rest of the
	// creature's life. It must never be gated behind diagnostics: see w44-1.
	UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	const AFGCreature* RoamSetter = Cast<AFGCreature>(GetPawn());
	if (!BlackboardComp || !RoamSetter)
	{
		return;
	}

	const FFloatInterval& Band = RoamSetter->GetRoamingDistance();
	if (Band.Max <= 0.0f)
	{
		return;
	}

	const float BBMax = BlackboardComp->GetValueAsFloat(FName(TEXT("MaxRoamingDistance")));
	if (FMath::IsNearlyEqual(BBMax, Band.Max))
	{
		return;
	}

	BlackboardComp->SetValueAsFloat(FName(TEXT("MinRoamingDistance")), Band.Min);
	BlackboardComp->SetValueAsFloat(FName(TEXT("MaxRoamingDistance")), Band.Max);

	// w45-3 (review D5): READ IT BACK BEFORE CLAIMING IT. This line used to assert
	// the write unconditionally -- the exact defect class F2 was created to remove
	// from the Spawner writer, left in place on its sibling because only one of the
	// two got looked at. SetValueAsFloat is equally void-returning and equally
	// silent when the key is absent or type-mismatched.
	const float MinReadback = BlackboardComp->GetValueAsFloat(FName(TEXT("MinRoamingDistance")));
	const float MaxReadback = BlackboardComp->GetValueAsFloat(FName(TEXT("MaxRoamingDistance")));
	const bool bBandTook =
		FMath::IsNearlyEqual(MinReadback, Band.Min) && FMath::IsNearlyEqual(MaxReadback, Band.Max);

	// Always-on, not diag-gated: this is a behaviour change taking effect, and
	// the whole reason the bug survived was that its evidence was invisible.
	// (UE_LOG needs its verbosity as a literal token, so this is two calls.)
	if (bBandTook)
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: roam band write TOOK -- wrote [%.0f..%.0f] (was %.0f max), readback=[%.0f..%.0f]. Vanilla's BP controller normally does this."),
			*GetName(), Band.Min, Band.Max, BBMax, MinReadback, MaxReadback);
	}
	else
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: roam band write FAILED -- wrote [%.0f..%.0f], readback=[%.0f..%.0f]. The borrowed blackboard asset has no usable roaming-distance keys; roaming cannot work until this is resolved."),
			*GetName(), Band.Min, Band.Max, MinReadback, MaxReadback);
	}
}

void APFACreatureController::ApplySpawnerToBlackboard()
{
	// w45-1 -- the LAST unwritten key in FGBBKeys. That namespace holds exactly
	// five (FGCreatureController.h:18-26): Spawner, Spline, TargetActor,
	// MinRoamingDistance, MaxRoamingDistance. As of w44-1 the roam band is
	// written, TargetActor tracks combat and Spline is legitimately null (nest
	// spawners author no patrol splines) -- but Spawner logged `null` on 100% of
	// heartbeats even for nest-spawned creatures, whose AFGCreature::GetSpawner()
	// should be valid. Same shape as every parity gap before it: vanilla's BP
	// controller wrote a key our C++-only controller does not.
	//
	// HONEST ABOUT WHAT THIS FIXES: the BT is a closed BP asset, so it CANNOT be
	// proven statically that the roam branch consumes this key -- the roam
	// destination has to be anchored somewhere, and the spawner is the only
	// candidate on the blackboard, but that is inference. The log line below is
	// written so ONE session settles it, and it distinguishes the two failure
	// modes that look identical from the outside: a creature with no spawner at
	// all (nothing to publish -- a deeper spawn-path problem) versus a spawner we
	// simply never told the tree about.
	// A failed write is TERMINAL, not transient: the blackboard asset's key schema
	// is fixed at load, so a key that is absent or type-mismatched now will never
	// become writable. Stop attempting (and stop logging) after the first proven
	// failure, otherwise the change-gate below -- which can only latch on a
	// SUCCESSFUL readback -- would retry and re-log every 30s for the creature's
	// whole life.
	if (bSpawnerKeyWriteFailed)
	{
		return;
	}

	UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	AFGCreature* SpawnerOwner = Cast<AFGCreature>(GetPawn());
	if (!BlackboardComp || !SpawnerOwner)
	{
		return;
	}

	AFGCreatureSpawner* Spawner = SpawnerOwner->GetSpawner();
	if (!Spawner)
	{
		// Not an error on its own: PFA.SpawnTest creatures are deliberately
		// spawnerless (which is why they get ClearNativeOrphanTimer). Says so
		// once per creature, always-on, because "creature has no spawner" is the
		// other half of the diagnosis and must not need diagnostics to appear.
		if (!bLoggedNoSpawnerForBlackboard)
		{
			bLoggedNoSpawnerForBlackboard = true;
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: GetSpawner() is null -- nothing to publish to the FGBBKeys::Spawner key. Expected for PFA.SpawnTest creatures; for a NEST-spawned creature this means the native spawner never coupled itself and roaming has no anchor."),
				*GetName());
		}
		return;
	}

	// Change-gated: the BT may legitimately re-read this, but rewriting an
	// identical object value every beat would spam the key's observers.
	if (BlackboardComp->GetValueAsObject(FGBBKeys::Spawner) == Spawner)
	{
		return;
	}

	// w45-2 (review F2) -- NEVER CLAIM A WRITE YOU DID NOT VERIFY.
	// UBlackboardComponent::SetValueAsObject returns void, and the underlying
	// SetValue SILENTLY returns false when the key is absent from the blackboard
	// asset or its type class doesn't match (BlackboardComponent.cpp:687-691).
	// Since we run on a BORROWED vanilla blackboard we do not own, that is a live
	// possibility. Without a readback the change-gate above would never latch and
	// this always-on line would announce a successful write every 30s per creature
	// forever, while nothing had been written -- a self-inflicted lie in the one
	// build whose entire purpose is log forensics. Same readback discipline as
	// SetBorrowedBehaviorTree (PFACreatureBase.cpp).
	BlackboardComp->SetValueAsObject(FGBBKeys::Spawner, Spawner);
	const UObject* Readback = BlackboardComp->GetValueAsObject(FGBBKeys::Spawner);
	const bool bWriteTook = (Readback == Spawner);

	// w45-3 (review D1): the latch demands POSITIVE PROOF of a schema defect --
	// the asset exists and genuinely has no usable key -- not merely a failed
	// readback. A failed write with no asset yet is a TIMING artefact that heals on
	// the next beat, and latching on it would convert "published late" into "never
	// published", with an always-on log line confidently misdiagnosing a race as a
	// missing blackboard key. GetBlackboardAsset/GetKeyID are AIMODULE_API engine
	// symbols (BlackboardComponent.h), so this adds no FactoryGame export surface.
	const bool bSchemaProvenBad =
		(BlackboardComp->GetBlackboardAsset() != nullptr) &&
		(BlackboardComp->GetKeyID(FGBBKeys::Spawner) == FBlackboard::InvalidKey);
	bSpawnerKeyWriteFailed = !bWriteTook && bSchemaProvenBad;

	if (bWriteTook)
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: FGBBKeys::Spawner write TOOK -- wrote %s, readback=%s. Vanilla's BP controller normally does this; if wandering starts working, THIS was the missing roam anchor."),
			*GetName(), *Spawner->GetName(), Readback ? *Readback->GetName() : TEXT("null"));
	}
	else if (bSchemaProvenBad)
	{
		// Terminal and always-on: this is a real, permanent capability gap in the
		// borrowed asset, and it means the roam branch can never get an anchor
		// from us. Logged once (the latch above stops further attempts).
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: FGBBKeys::Spawner write FAILED PERMANENTLY -- the borrowed blackboard asset has no usable 'Spawner' key (absent or type-mismatched). The roam branch can never read an anchor from us. Not retrying."),
			*GetName());
	}
	else if (PFADiag::IsEnabled())
	{
		// Transient: blackboard not ready yet. Diag-gated because it is expected
		// noise during bring-up and self-heals on the next beat -- an always-on
		// line here would train the reader to ignore the terminal one above.
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: FGBBKeys::Spawner write did not take yet (blackboard asset not ready) -- retrying on the next beat, NOT latched."),
			*GetName());
	}
}

void APFACreatureController::LogCreatureStateHeartbeat()
{
	// ---- w44-1 CRITICAL FIX (cold review) ---------------------------------
	// BEHAVIOUR FIRST, THEN THE DIAG GATE. w38-1 put the roam-band blackboard
	// write -- the ONLY thing in the entire repo that makes creatures wander --
	// inside this function, which returns early unless diagnostics are on. So
	// wandering only ever worked in a diagnostics-enabled session. The user
	// confirmed "I see them wander around now" during exactly such a session,
	// which made a permanently-broken feature look fixed.
	//
	// The lesson is bigger than the bug: NEVER put behaviour inside a function
	// whose contract is "diagnostics". This one even said so in its own header
	// comment ("ONLY when pfa.Diag is on") and I added a load-bearing write to
	// it anyway. Behaviour that only runs while you are watching is the worst
	// kind of bug -- it is invisible precisely when nobody is looking.
	ApplyRoamBandToBlackboard();
	ApplySpawnerToBlackboard();

	// Gated: only SPEAKS when diagnostics are on (the timer keeps firing cheaply
	// otherwise). GetCurrentCreatureState/GetAggroTargets/GetCurrentTarget are
	// all inherited public inline getters (FGCreatureController.h:182/216/208);
	// CreatureStateEnumToString is an inline free fn (FGCreature.h:111).
	if (!PFADiag::IsEnabled())
	{
		return;
	}
	const AActor* CurrentTarget = GetCurrentTarget();

	// w45-1: the aggro LEDGER, not just the count. The w44-1 session could prove
	// creatures were stuck in CS_Fight but not WHY, because the heartbeat only
	// ever printed how many aggro targets existed. Both gain curves are keyed on
	// DISTANCE, and decay (mAggroReductionRate, 5/s) only runs while a target is
	// NOT visible -- so aggro, visibility and distance TOGETHER are the whole
	// mechanism, and any one of them alone explains nothing. Reading
	// `aggro=100.0 visible=yes dist=3500` next to a 3,000cm zero-crossing is what
	// distinguishes "the curve is wrong" from "the curve is right and something
	// else pins the state".
	// Both are FORCEINLINE header getters (FGCreatureController.h-adjacent
	// FGCreature.h:396/402), so printing them adds no import surface -- and they
	// turn the ledger into something self-explanatory months from now: the
	// threshold the aggro numbers have to fall under, and the rate they fall at.
	const AFGCreature* HeartbeatCreature = Cast<AFGCreature>(GetPawn());

	FString AggroLedger;
	// w45-2 (review F4): if the pawn is gone there is no origin to measure FROM, so
	// distance must read as the -1 sentinel. Falling back to FVector::ZeroVector
	// would print a plausible-looking distance from WORLD ORIGIN, and in a
	// forensics line a confidently wrong number is worse than a missing one.
	const APawn* LedgerPawn = GetPawn();
	const FVector SelfLocation = LedgerPawn ? LedgerPawn->GetActorLocation() : FVector::ZeroVector;
	for (const FFGCreatureAggroData& AggroData : GetAggroTargets())
	{
		const AActor* AggroTarget = AggroData.Target;
		const bool bCanMeasure = (LedgerPawn != nullptr) && (AggroTarget != nullptr);
		AggroLedger += FString::Printf(TEXT("[%s aggro=%.1f visible=%s dist=%.0f] "),
			AggroTarget ? *AggroTarget->GetName() : TEXT("null"),
			AggroData.Aggro,
			AggroData.IsVisible ? TEXT("yes") : TEXT("no"),
			bCanMeasure ? FVector::Dist(SelfLocation, AggroTarget->GetActorLocation()) : -1.0f);
	}
	if (AggroLedger.IsEmpty())
	{
		AggroLedger = TEXT("<none>");
	}

	// w48-2: DESIRED next to CURRENT. The w48-1 session proved the machine parks in
	// CS_Alert with aggroTargets=0 and currentTarget=none, and the heartbeat could
	// not say whether the machine WANTED alert or merely failed to leave it -- two
	// causes needing opposite fixes. GetDesiredCreatureState (FGCreatureController.h
	// :189) adds NO import surface -- but NOT for the reason this project's house
	// rule would give. It is virtual, and the built DLL shows it in the import table
	// ANYWAY: it is a base virtual we do not override, so our own vtable already had
	// to carry the base implementation's address. It was therefore already imported
	// before this line existed (measured: FactoryGame 174 -> 176 across the packet,
	// with three CreatureState symbols present, so only two are new). It is safe
	// because the shipping DLL EXPORTS it (dumpbin ordinal 18422) -- see the long
	// note at ForceRoamAfterDisengage in PFACreatureController.cpp.
	// READ IT LIKE THIS: `state=CS_Alert desired=CS_Roam` = the transition is not
	// being applied (the pfa.ForceRoamOnDisengage kick is the fix, and its own
	// always-on line will say whether it took). `state=CS_Alert desired=CS_Alert` =
	// something OTHER than aggro is holding the machine alert, and the fix is
	// upstream of the state setter entirely.
	const ECreatureState HeartbeatDesired = GetDesiredCreatureState();

	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: state heartbeat -> state=%s desired=%s aggroTargets=%d currentTarget=%s fightThreshold=%.0f reductionRate=%.1f/s aggro: %s (30s gated diag; state!=desired = a transition is not being applied; state==desired==CS_Roam with no aggro = HEALTHY, nothing to diagnose; state==desired on a NON-roam state with no aggro = something other than aggro holds it)"),
		*GetName(),
		*CreatureStateEnumToString(GetCurrentCreatureState()),
		*CreatureStateEnumToString(HeartbeatDesired),
		GetAggroTargets().Num(),
		CurrentTarget ? *CurrentTarget->GetName() : TEXT("none"),
		HeartbeatCreature ? HeartbeatCreature->GetAggroFightThreshold() : -1.0f,
		HeartbeatCreature ? HeartbeatCreature->GetAggroReductionRate() : -1.0f,
		*AggroLedger);

	// w25-1 (aggro-but-no-path diagnosis; session-13 evidence: creatures that
	// had chased earlier later sat frozen in CS_Fight targeting the player's
	// RESPAWNED pawn): dump the movement layer's actual state -- every
	// Object-typed blackboard key by its REAL name+value (the BT's own view
	// of the target; the vanilla BP controller layer may re-sync these on
	// target change -- our C++ controller never writes them: candidate parity
	// gap), plus path-following status and true velocity. One heartbeat then
	// separates: BB target stale/null (-> write the key on target change) vs
	// moveStatus Idle (BT branch not issuing moves) vs Moving-with-zero
	// velocity (nav/path blocked).
	FString BlackboardDump = TEXT("<no blackboard>");
	// Local named to dodge C4458: AAIController already has a `Blackboard`
	// member (same shadowing-as-error class as the W1.3 `Controller` rename).
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardDump.Reset();
		if (const UBlackboardData* BlackboardAsset = BlackboardComp->GetBlackboardAsset())
		{
			for (const FBlackboardEntry& Entry : BlackboardAsset->Keys)
			{
				if (Entry.KeyType && Entry.KeyType->IsA<UBlackboardKeyType_Object>())
				{
					UObject* Value = BlackboardComp->GetValueAsObject(Entry.EntryName);
					BlackboardDump += FString::Printf(TEXT("%s=%s "),
						*Entry.EntryName.ToString(),
						Value ? *Value->GetName() : TEXT("null"));
				}
			}
		}
		// w38-1: ANSWERED, and it was the predicted answer. The w32-1 heartbeat
		// shipped with `roamBB=[0..0] roamCDO=[600..2500]` -- our CDO values are
		// correct and the BLACKBOARD keys the roam branch actually reads are
		// zero. So the vanilla BP controller was populating them and we must do
		// it ourselves. Third time this exact shape has appeared: the BT that
		// vanilla's BP launched for us, the aggro curves its BP authored, and now
		// the roam keys its BP wrote.
		//
		// The WRITE now lives in ApplyRoamBandToBlackboard(), called unconditionally
		// at the top of this function (w44-1). Only the reporting is left here.

		// Both sides still logged, because the write above only matters if it
		// STICKS -- and a key that reverts is the same bug wearing a hat.
		BlackboardDump += FString::Printf(TEXT("| roamBB=[%.0f..%.0f] "),
			BlackboardComp->GetValueAsFloat(FName(TEXT("MinRoamingDistance"))),
			BlackboardComp->GetValueAsFloat(FName(TEXT("MaxRoamingDistance"))));
		if (const AFGCreature* RoamCreature = Cast<AFGCreature>(GetPawn()))
		{
			BlackboardDump += FString::Printf(TEXT("roamCDO=[%.0f..%.0f]"),
				RoamCreature->GetRoamingDistance().Min, RoamCreature->GetRoamingDistance().Max);
		}

		if (BlackboardDump.IsEmpty())
		{
			BlackboardDump = TEXT("<no Object-typed keys>");
		}
	}
	const APawn* ControlledPawn = GetPawn();
	// w47-1: the SPEED-TRAIT fields. The roaming-design research flagged that
	// nothing ever calls AFGCreature::SetMoveSpeed, so if the BT or native code
	// does, it silently overwrites the per-individual `MaxWalkSpeed *= SpeedTrait`
	// and every individual moves identically ("they all seem to do the same
	// thing"). The change DETECTOR lives on the creature (PFASpeedTraitWatch.cpp);
	// this puts the current state on the line a reader is already looking at, so a
	// heartbeat that shows walkSpeed == an exact mMoveSpeedData value while
	// traitBaseline is a x1.0x product is the whole diagnosis at a glance.
	const APFACreatureBase* SpeedCreature = Cast<APFACreatureBase>(ControlledPawn);
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: heartbeat-move -> moveStatus=%d (0=Idle 1=Waiting 2=Paused 3=Moving) velocity=%.0fcm/s walkSpeed=%.1f (traitBaseline=%.1f trait=x%.3f unexplainedChanges=%d) bbObjectKeys: %s"),
		*GetName(),
		static_cast<int32>(GetMoveStatus()),
		ControlledPawn ? ControlledPawn->GetVelocity().Size2D() : -1.0f,
		SpeedCreature ? SpeedCreature->GetLiveMaxWalkSpeedCms() : -1.0f,
		SpeedCreature ? SpeedCreature->GetTraitWalkSpeedBaselineCms() : -1.0f,
		SpeedCreature ? SpeedCreature->GetSpeedTraitMultiplier() : -1.0f,
		SpeedCreature ? SpeedCreature->GetWalkSpeedChangeCount() : -1,
		*BlackboardDump);
}
