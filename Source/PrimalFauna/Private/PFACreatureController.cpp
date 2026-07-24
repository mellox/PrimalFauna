#include "PFACreatureController.h"
#include "PrimalFauna.h"
#include "PFACheats.h"
#include "PFACreatureBase.h"
#include "PFASubsystem.h"
#include "Species/PFASicklejaw.h"

#include "Creature/FGCreature.h"
#include "Creature/FGCreatureSpawner.h"
#include "FGCreatureSubsystem.h"
#include "FGCharacterPlayer.h"
#include "PFAFactionConfig.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BrainComponent.h"
#include "EngineUtils.h"

// NOTE: the W1.8 perception wiring (WireCreaturePerception / LogCreatureState
// Heartbeat, called from OnPossess below) is defined in the sibling TU
// PFACreatureControllerPerception.cpp -- split out purely to keep both files
// under the 500-line workspace limit (this class owns both). See that file for
// the full root-cause + symbol/export analysis.

void APFACreatureController::LogRuleVerdict(const AFGCreature* SelfCreature, const AActor* Target, int32 RuleNumber, bool bHostile) const
{
	if (!PFADiag::IsEnabled())
	{
		return;
	}

	// w18-3 verdict-change throttle: w18-2 logged EVERY evaluation tick --
	// in-game evidence 2026-07-24: ~400 verdict lines/s with 3 live
	// Sicklejaws, 46% of the entire session log (30454/66210 lines), all
	// game-thread log I/O. A (target -> rule+verdict) pair now logs only when
	// its result CHANGES; steady-state re-evaluation is silent. Map lifetime =
	// controller lifetime; FObjectKey stays unique across target death/reuse.
	const uint8 Encoded = static_cast<uint8>((RuleNumber << 1) | (bHostile ? 1 : 0));
	const FObjectKey TargetKey(Target);
	if (const uint8* Prev = mLastLoggedRuleVerdict.Find(TargetKey))
	{
		if (*Prev == Encoded)
		{
			return;
		}
	}
	mLastLoggedRuleVerdict.Add(TargetKey, Encoded);

	// Contract format per docs/02-hostility-and-factions.md §2 -- do not
	// reformat (this is NOT the [PFA][TEST] self-test grammar).
	UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s vs %s: rule%d -> %s"),
		SelfCreature ? *SelfCreature->GetName() : TEXT("<null>"),
		Target ? *Target->GetName() : TEXT("<null>"),
		RuleNumber,
		bHostile ? TEXT("HOSTILE") : TEXT("NOT_HOSTILE"));
}

APFACreatureController::APFACreatureController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// ===========================================================================
// W1.6 (docs/08 packet, Phase-1 gate finding): the gate proved creatures were
// possessed-but-inert -- CDO mBehaviorTree borrowed correctly (docs/07 §4),
// controller class correct, but the live creature's BehaviorTreeComponent
// never started (DumpCreature: "BehaviorTree=none" / zero ScoreAction/
// PerformAction lines). Root cause: vanilla creatures get "Run Behavior Tree"
// from BP_CreatureController_C's own Blueprint EventGraph on possess; our C++
// controller replaces that whole layer (docs/07 §1: own every class we
// change) and never reproduced the launch step. This override adds exactly
// that step, verified against the REAL engine source (not memory):
//
//   AIController.h:346  AIMODULE_API virtual void OnPossess(APawn*) override;
//     -- protected on AAIController, but AFGCreatureController (FGCreature
//     Controller.h:156) RE-DECLARES it public as part of its own "Begin/End
//     AIController Interface" block -- confirming AFGCreatureController DOES
//     override OnPossess already (its .cpp is a closed-source SML stub, so we
//     cannot read what it does, but the gate's own evidence -- creatures
//     possess fine, controller class is correct, BT still never starts --
//     proves Super's OnPossess does NOT already call RunBehaviorTree). We
//     match its public access level here.
//   AIController.h:261  AIMODULE_API virtual bool RunBehaviorTree(UBehaviorTree*);
//     -- VIRTUAL and NOT overridden anywhere in the AFGCreatureController
//     chain (its own "Begin/End AIController Interface" list is OnPossess/
//     OnUnPossess/MoveTo/UpdateControlRotation only -- RunBehaviorTree is
//     absent). A plain `RunBehaviorTree(...)` call from this class is
//     therefore vtable-indirect dispatch to the ENGINE's own compiled
//     AAIController::RunBehaviorTree -- no FactoryGame-exe export needed at
//     all (contrast the ClearKillOrphanTimer shipping-export trap, docs/07
//     §1: that was a PLAIN NON-VIRTUAL method requiring a direct symbol
//     import; a virtual call resolves through the object's own vtable,
//     populated by the engine's own code that constructed it). RunBehaviorTree
//     is also the exact function every vanilla BP creature's "Run Behavior
//     Tree" Blueprint node calls under the hood -- near-zero risk by
//     construction, used by literally every AI pawn in the game.
//   AIController.cpp:982 (engine source, AIModule/Private/AIController.cpp)
//     -- read directly to answer "does RunBehaviorTree init the blackboard
//     internally": YES, conditionally --
//       if (BTAsset->BlackboardAsset && (Blackboard == nullptr || !Blackboard
//           ->IsCompatibleWith(BTAsset->BlackboardAsset))) { UseBlackboard(...); }
//     -- so we must NOT call UseBlackboard ourselves; RunBehaviorTree already
//     does it exactly when the tree needs it.
//   FGCreatureController.h:349  virtual bool InitializeBlackboard(...) override;
//     (protected). DECISION: do NOT override this ourselves. It is plain
//     C++-only -- NOT a BlueprintNativeEvent/BlueprintImplementableEvent --
//     so BP_CreatureController_C (vanilla's Blueprint controller) structurally
//     CANNOT have its own version either; every vanilla creature that runs a
//     BT (proven: Char_SpaceGiraffe running this SAME borrowed BT_Creature
//     Behavior) is ALREADY going through AFGCreatureController's C++
//     InitializeBlackboard. Since APFACreatureController does not override
//     it, UseBlackboard()'s internal virtual call reaches that exact same,
//     already-proven-working implementation automatically. Reimplementing it
//     ourselves would be strictly worse: closed-source .cpp (can't verify a
//     reimplementation matches), and the one thing vanilla creatures on this
//     tree already do correctly.
//   BrainComponent.h:167  UFUNCTION(BlueprintPure) AIMODULE_API virtual bool
//     IsRunning() const; -- public, virtual (same vtable-safety argument as
//     RunBehaviorTree). Used to make this override idempotent against
//     re-possession (never double-start a tree that is already running).
//
// AAIController's own class comment (AIController.h:84): "In networked games,
// [AIControllers] only exist on the server" -- OnPossess for an AI controller
// is server-only by engine construction, so no HasAuthority()/NM_Client guard
// is needed here (contrast our console commands and subsystem logic, which
// run in contexts reachable from a client and DO need explicit guards).
// ===========================================================================
void APFACreatureController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// GetControlledCreature() (FGCreatureController.h) is itself a const
	// method but returns a plain (non-const) AFGCreature* -- const applies to
	// `this`, not the pointee -- so no cast is needed here.
	AFGCreature* PossessedCreature = GetControlledCreature();
	if (!PossessedCreature)
	{
		// Not necessarily an error -- Super::OnPossess may have rejected InPawn
		// for reasons internal to the closed-source implementation -- but it
		// IS the "nothing to launch a BT on" terminal state, so log loudly
		// rather than silently doing nothing (workspace diagnostics-first rule).
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: OnPossess %s -> GetControlledCreature() is null after Super::OnPossess; no BehaviorTree to launch."),
			*GetName(), InPawn ? *InPawn->GetName() : TEXT("<null>"));
		return;
	}

	// W1.8: wire perception FIRST -- independent of the BT launch (perception
	// feeds aggro; the BT consumes aggro), and must run even if the BT branch
	// below early-returns (a creature with no BT should still be able to
	// perceive). Self-contained + heavily instrumented; see the method.
	WireCreaturePerception(PossessedCreature);

	UBehaviorTree* BTAsset = PossessedCreature->GetBehaviorTreeAsset();
	const TCHAR* BTSource = TEXT("instance");

	// W1.7 (Section-B re-test, 2026-07-23): CDO->instance propagation of the
	// borrowed tree provably does NOT happen through the closed-source spawn
	// path -- EVERY instance read null here, including ones spawned ~5 min
	// after a verified CDO write. Root cause is in unreadable spawn internals;
	// the robust fix is to stop depending on propagation entirely: fall back
	// to the subsystem's GC-rooted borrowed tree and write it onto THIS
	// INSTANCE (so any downstream native read of mBehaviorTree also sees it).
	if (!BTAsset)
	{
		for (TActorIterator<APFASubsystem> It(GetWorld()); It; ++It)
		{
			if (*It)
			{
				BTAsset = It->GetBorrowedBehaviorTree();
				break;
			}
		}
		if (BTAsset)
		{
			BTSource = TEXT("subsystem-fallback");
			if (APFACreatureBase* PFACreature = Cast<APFACreatureBase>(PossessedCreature))
			{
				PFACreature->SetBorrowedBehaviorTree(BTAsset);
			}
		}
	}

	if (!BTAsset)
	{
		UE_LOG(LogPrimalFauna, Error,
			TEXT("[PFA] %s: OnPossess %s -> BT <none> RunBehaviorTree=SKIPPED blackboard=n/a (instance null AND subsystem fallback empty -- check the [PFA] BT borrow/census lines from world load; discovery may have found nothing)."),
			*GetName(), *PossessedCreature->GetName());
		return;
	}

	UBrainComponent* ExistingBrain = GetBrainComponent();
	if (ExistingBrain && ExistingBrain->IsRunning())
	{
		// Idempotent re-possession guard (e.g. a controller reused across a
		// respawn). Never double-start -- StartTree is already safe to call
		// again (RunBehaviorTree would just restart the SAME tree), but a
		// clean "already running, no-op" log line is a clearer signal than a
		// second identical "RunBehaviorTree=true" line for the same creature.
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: OnPossess %s -> BT %s already running (BrainComponent::IsRunning()=true), not restarting."),
			*GetName(), *PossessedCreature->GetName(), *BTAsset->GetName());
		return;
	}

	const bool bStarted = RunBehaviorTree(BTAsset);
	const TCHAR* BlackboardState = GetBlackboardComponent() ? TEXT("present") : TEXT("none");
	const FString BTBlackboardAssetName = BTAsset->BlackboardAsset ? BTAsset->BlackboardAsset->GetName() : TEXT("none");

	if (bStarted)
	{
		// Success chain -- Display, always on (not diagnostics-gated): this is
		// the ONE line the W1.6 re-test greps for to confirm the fix landed.
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: OnPossess %s -> BT %s (source=%s) RunBehaviorTree=true blackboard=%s (btBlackboardAsset=%s)"),
			*GetName(), *PossessedCreature->GetName(), *BTAsset->GetName(), BTSource, BlackboardState, *BTBlackboardAssetName);
	}
	else
	{
		// RunBehaviorTree(...) itself only returns false when a required
		// UseBlackboard() call failed (BTAsset->BlackboardAsset was non-null
		// and incompatible/couldn't be created) -- verified by reading its
		// engine source directly (AIModule/Private/AIController.cpp:982). The
		// engine's own LogBehaviorTree channel (UE_VLOG) may carry more detail.
		UE_LOG(LogPrimalFauna, Error,
			TEXT("[PFA] %s: OnPossess %s -> BT %s RunBehaviorTree=false blackboard=%s (btBlackboardAsset=%s) -- UseBlackboard() likely failed inside RunBehaviorTree (see LogBehaviorTree above for the engine's own reason)."),
			*GetName(), *PossessedCreature->GetName(), *BTAsset->GetName(), BlackboardState, *BTBlackboardAssetName);
	}
}

bool APFACreatureController::PassesAbsoluteNeverRules(const AActor* actor) const
{
	// Rules 1-4 of the doc 02 §2 ladder -- the unconditional "never target"
	// rules. RECURSION CONTRACT: this helper must contain NO Super:: calls and
	// no calls into CanBeHostileTowards/IsValidTarget -- it is the only part
	// of the ladder IsValidTarget may run (see IsValidTarget's comment for the
	// w18-1 stack-overflow this prevents).
	const AFGCreature* SelfCreature = GetControlledCreature();
	if (!actor || !SelfCreature)
	{
		// No basis to reject; both callers keep their own Super deferral for
		// this corner (CanBeHostileTowards returns Super's verdict outright,
		// IsValidTarget has already ANDed Super::IsValidTarget).
		return true;
	}

	const AFGCreature* TargetCreature = Cast<AFGCreature>(actor);

	// Rule 1: tamed creature -> never. Hardcoded, not config.
	//
	// IsTamed() is declared WITHOUT const (Creature/FGCreature.h:484 --
	// verified against the header: "bool IsTamed();", a
	// BlueprintImplementableEvent with no const qualifier) despite being a
	// pure query -- vanilla creature BPs implement it by reading tamed-status
	// data, they don't mutate anything. Our `actor`/TargetCreature are const
	// (CanBeHostileTowards's own signature is `const AActor* actor ... const`,
	// mandated by the base class), so calling this non-const-qualified query
	// needs a const_cast -- not a real mutation, just a header wart.
	if (TargetCreature && const_cast<AFGCreature*>(TargetCreature)->IsTamed())
	{
		LogRuleVerdict(SelfCreature, actor, 1, false);
		return false;
	}

	// Rule 2: protected species (the game's own never-target list) -> never.
	if (TargetCreature)
	{
		if (AFGCreatureSubsystem* CreatureSubsystem = AFGCreatureSubsystem::Get(GetWorld()))
		{
			if (CreatureSubsystem->IsProtectedCreature(TargetCreature->GetClass()))
			{
				LogRuleVerdict(SelfCreature, actor, 2, false);
				return false;
			}
		}
	}

	// Rule 3: our own species (same class) -> never (packmates).
	if (actor->GetClass() == SelfCreature->GetClass())
	{
		LogRuleVerdict(SelfCreature, actor, 3, false);
		return false;
	}

	// Rule 4: another PrimalFauna species -> never (v1; post-1.0 flavor
	// option per doc 02 §2 row 4). Rule 3 already excluded same-class, so
	// reaching here with an APFACreatureBase means a DIFFERENT species.
	if (Cast<APFACreatureBase>(actor) != nullptr)
	{
		LogRuleVerdict(SelfCreature, actor, 4, false);
		return false;
	}

	return true;
}

bool APFACreatureController::CanBeHostileTowards(const AActor* actor) const
{
	const AFGCreature* SelfCreature = GetControlledCreature();
	if (!actor || !SelfCreature)
	{
		return Super::CanBeHostileTowards(actor);
	}

	// Rules 1-4 live in the shared Super-free helper so IsValidTarget can
	// apply them too without re-entering this function.
	if (!PassesAbsoluteNeverRules(actor))
	{
		return false;
	}

	const AFGCreature* TargetCreature = Cast<AFGCreature>(actor);

	// Rule 5: a player -> defer to Super (preserves the vanilla per-player
	// Passive/Retaliate setting, doc 02 §2 row 5), THEN apply our species
	// aggression gate. We only ever make the result MORE restrictive.
	if (Cast<AFGCharacterPlayer>(actor) != nullptr)
	{
		if (!Super::CanBeHostileTowards(actor))
		{
			// W2.5 root-cause probe (session-15: rule5 NOT_HOSTILE vs the
			// player even in Hostile config, after the first Passive round
			// trip): when the BASE vetoes, log the one field we control that
			// feeds it. mIsPassiveCreature=true here after a mode flip back
			// to Hostile = the flip failed to clear it; =false = the veto
			// comes from deeper base state. Throttled per target on VALUE
			// change (w25-4 review fix -- this sits on the per-tick hot path).
			if (PFADiag::IsEnabled() && SelfCreature)
			{
				const bool bPassive = SelfCreature->IsPassiveCreature();
				const FObjectKey ProbeKey(actor);
				const bool* PrevPassive = mLastLoggedSuperVetoPassive.Find(ProbeKey);
				if (!PrevPassive || *PrevPassive != bPassive)
				{
					mLastLoggedSuperVetoPassive.Add(ProbeKey, bPassive);
					UE_LOG(LogPrimalFauna, Display,
						TEXT("[PFA] %s vs %s: rule5 SUPER-VETO probe: mIsPassiveCreature=%s"),
						*SelfCreature->GetName(), *actor->GetName(),
						bPassive ? TEXT("true") : TEXT("false"));
				}
			}
			LogRuleVerdict(SelfCreature, actor, 5, false);
			return false;
		}
		const bool bResult = PassesAggressionModeGate(actor);
		LogRuleVerdict(SelfCreature, actor, 5, bResult);
		return bResult;
	}

	// Rules 6/7: a vanilla creature (not tamed/protected/ours -- already
	// filtered by rules 1-4 above). Split on IsPassiveCreature(); both apply
	// the same species aggression gate as rule 5. W2.3: the gate values are
	// now LIVE config (PFAFactionConfig statics, pushed each subsystem
	// refresh tick); the constexpr *Default anchors remain the doc-04
	// compile-time source the statics initialize from.
	if (TargetCreature)
	{
		if (!TargetCreature->IsPassiveCreature())
		{
			const bool bResult = PFAFactionConfig::HuntVanillaHostiles() && PassesAggressionModeGate(actor);
			LogRuleVerdict(SelfCreature, actor, 6, bResult);
			return bResult;
		}
		else
		{
			const bool bResult = PFAFactionConfig::HuntPassiveFauna() && PassesAggressionModeGate(actor);
			LogRuleVerdict(SelfCreature, actor, 7, bResult);
			return bResult;
		}
	}

	// Rule 8: anything else (vehicles, buildings, unknown) -> defer to Super.
	const bool bResult = Super::CanBeHostileTowards(actor);
	LogRuleVerdict(SelfCreature, actor, 8, bResult);
	return bResult;
}

bool APFACreatureController::IsValidTarget(const AActor* actor) const
{
	// RECURSION HAZARD (caused the w18-1 EXCEPTION_STACK_OVERFLOW): the BASE
	// class's CanBeHostileTowards (FGCreatureController.cpp:286 per the crash
	// stack) calls this VIRTUAL IsValidTarget as its own sub-check. So the old
	// `&& CanBeHostileTowards(actor)` here looped forever the moment any rule
	// consulted Super::CanBeHostileTowards (rule 5 for players, rule 8):
	// ours -> Super -> base:286 -> IsValidTarget (virtual, ours) -> ours -> ...
	//
	// Therefore only the Super-free rules 1-4 run here. Nothing is lost on
	// hostility-INITIATION paths: the base already conjoins IsValidTarget into
	// CanBeHostileTowards, and our override runs the full 8-rule ladder on
	// top. Retention/validation paths that consult IsValidTarget alone keep
	// vanilla semantics plus the absolute never-rules.
	return Super::IsValidTarget(actor) && PassesAbsoluteNeverRules(actor);
}

bool APFACreatureController::CanBeAlertedByTarget_Implementation(const AActor* target) const
{
	return CanBeHostileTowards(target);
}

bool APFACreatureController::ForceAggro(AActor* Target, float NewAggro)
{
	if (!Target)
	{
		return false;
	}
	// Native private registration via the Friend grant (AccessTransformers
	// .ini) -- the same entry-initialization path perception itself uses.
	FFGCreatureAggroData* Entry = GetOrRegisterAggroTarget(Target);
	if (!Entry)
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: ForceAggro(%s) -- native registration REFUSED (closed-source gate); aggro not set."),
			*GetName(), *Target->GetName());
		return false;
	}
	SetAggroForTarget(Target, NewAggro, /*bImmediateStateUpdate=*/true);
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: ForceAggro(%s, %.1f) registered+set; readback=%.1f state=%s currentTarget=%s"),
		*GetName(), *Target->GetName(), NewAggro,
		GetAggroForTarget(Target),
		*CreatureStateEnumToString(GetCurrentCreatureState()),
		GetCurrentTarget() ? *GetCurrentTarget()->GetName() : TEXT("none"));
	return true;
}

void APFACreatureController::ResetTargetingState(const TCHAR* Reason)
{
	const int32 AggroCount = mAggroTargets.Num();
	const int32 VisibleCount = mVisibleTargets.Num();
	NullifyAllAggro(/*bImmediateStateUpdate=*/false);
	mAggroTargets.Reset();
	mVisibleTargets.Reset();
	SetCurrentTarget(nullptr);
	TriggerStateUpdate(/*bImmediate=*/true);
	// Always-on: a state wipe is a behavioral event the log must explain.
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: ResetTargetingState (%s): cleared %d aggro + %d visibility entries; currentTarget=none."),
		*GetName(), Reason, AggroCount, VisibleCount);
}

bool APFACreatureController::PassesAggressionModeGate(const AActor* Target) const
{
	const APFACreatureBase* SelfBase = Cast<APFACreatureBase>(GetControlledCreature());
	if (!SelfBase)
	{
		// Our controller only ever possesses our own creatures (we set
		// AIControllerClass ourselves in APFACreatureBase's ctor); reaching
		// here would mean that wiring is broken elsewhere. Fail open
		// (Hostile-like) rather than silently blocking every evaluation.
		return true;
	}

	switch (SelfBase->GetAggressionMode())
	{
	case EPFAAggression::Passive:
		// Belt-and-suspenders: mIsPassiveCreature (set by SetAggressionMode)
		// should already suppress native aggro before we're even asked, but
		// never initiate here either way.
		return false;

	case EPFAAggression::Territorial:
	{
		if (WasRecentlyDamagedBy(Target))
		{
			return true;
		}
		if (AFGCreatureSpawner* Spawner = SelfBase->GetSpawner())
		{
			const float DistanceCm = FVector::Dist(Spawner->GetActorLocation(), Target->GetActorLocation());
			return DistanceCm <= SelfBase->GetTerritoryRadiusCm();
		}
		// No spawner yet -- nests arrive W1.5 (doc 03 §5). Fall through to
		// Hostile behavior so a Territorial creature isn't dead weight
		// before nests exist; log once so this isn't mistaken for a bug.
		if (!bLoggedNoSpawnerFallback && PFADiag::IsEnabled())
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: Territorial mode with no spawner yet (nests arrive W1.5) -- falling through to Hostile behavior."),
				*SelfBase->GetName());
			bLoggedNoSpawnerFallback = true;
		}
		return true;
	}

	case EPFAAggression::Hostile:
	default:
		return true;
	}
}

bool APFACreatureController::WasRecentlyDamagedBy(const AActor* Target) const
{
	if (!Target)
	{
		return false;
	}
	for (const FFGRecentDamageTaken& Entry : mRecentDamageTaken)
	{
		if (Entry.DamageCauser == Target)
		{
			return true;
		}
		if (Entry.DamageInstigator && Entry.DamageInstigator->GetPawn() == Target)
		{
			return true;
		}
	}
	return false;
}

// ---- W1.3 self-test: entity-free wiring checks (see PFACheats.h for the
// registration idiom). Lives here (not PFASicklejaw.cpp) so it can read the
// HuntVanillaHostilesDefault / HuntPassiveFaunaDefault constants directly. ----
namespace
{
	const bool bRegistered_SicklejawControllerPolicy = []
	{
		RegisterPFASelfTest({TEXT("SicklejawControllerPolicy"),
			[](UWorld* /*World*/, FString& OutDetail) -> bool
			{
				const APFASicklejaw* Cdo = GetDefault<APFASicklejaw>();
				const UClass* ControllerClass = Cdo ? Cdo->AIControllerClass.Get() : nullptr;
				const bool bControllerWired = ControllerClass == APFACreatureController::StaticClass();
				const bool bDefaultsMatchDoc04 =
					(APFACreatureController::HuntVanillaHostilesDefault == true) &&
					(APFACreatureController::HuntPassiveFaunaDefault == false);

				OutDetail = FString::Printf(TEXT("AIControllerClass=%s HuntVanillaHostiles(default)=%s HuntPassiveFauna(default)=%s live=(%s,%s)"),
					ControllerClass ? *ControllerClass->GetName() : TEXT("none"),
					APFACreatureController::HuntVanillaHostilesDefault ? TEXT("true") : TEXT("false"),
					APFACreatureController::HuntPassiveFaunaDefault ? TEXT("true") : TEXT("false"),
					PFAFactionConfig::HuntVanillaHostiles() ? TEXT("true") : TEXT("false"),
					PFAFactionConfig::HuntPassiveFauna() ? TEXT("true") : TEXT("false"));
				return bControllerWired && bDefaultsMatchDoc04;
			}});
		return true;
	}();
}
