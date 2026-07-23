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
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BrainComponent.h"
#include "EngineUtils.h"

namespace
{
	void LogRuleVerdict(const AFGCreature* SelfCreature, const AActor* Target, int32 RuleNumber, bool bHostile)
	{
		if (!PFADiag::IsEnabled())
		{
			return;
		}
		// Contract format per docs/02-hostility-and-factions.md §2 -- do not
		// reformat (this is NOT the [PFA][TEST] self-test grammar).
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s vs %s: rule%d -> %s"),
			SelfCreature ? *SelfCreature->GetName() : TEXT("<null>"),
			Target ? *Target->GetName() : TEXT("<null>"),
			RuleNumber,
			bHostile ? TEXT("HOSTILE") : TEXT("NOT_HOSTILE"));
	}
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

bool APFACreatureController::CanBeHostileTowards(const AActor* actor) const
{
	const AFGCreature* SelfCreature = GetControlledCreature();
	if (!actor || !SelfCreature)
	{
		return Super::CanBeHostileTowards(actor);
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

	// Rule 5: a player -> defer to Super (preserves the vanilla per-player
	// Passive/Retaliate setting, doc 02 §2 row 5), THEN apply our species
	// aggression gate. We only ever make the result MORE restrictive.
	if (Cast<AFGCharacterPlayer>(actor) != nullptr)
	{
		if (!Super::CanBeHostileTowards(actor))
		{
			LogRuleVerdict(SelfCreature, actor, 5, false);
			return false;
		}
		const bool bResult = PassesAggressionModeGate(actor);
		LogRuleVerdict(SelfCreature, actor, 5, bResult);
		return bResult;
	}

	// Rules 6/7: a vanilla creature (not tamed/protected/ours -- already
	// filtered by rules 1-4 above). Split on IsPassiveCreature(); both apply
	// the same species aggression gate as rule 5.
	if (TargetCreature)
	{
		if (!TargetCreature->IsPassiveCreature())
		{
			const bool bResult = HuntVanillaHostilesDefault && PassesAggressionModeGate(actor);
			LogRuleVerdict(SelfCreature, actor, 6, bResult);
			return bResult;
		}
		else
		{
			const bool bResult = HuntPassiveFaunaDefault && PassesAggressionModeGate(actor);
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
	return Super::IsValidTarget(actor) && CanBeHostileTowards(actor);
}

bool APFACreatureController::CanBeAlertedByTarget_Implementation(const AActor* target) const
{
	return CanBeHostileTowards(target);
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

				OutDetail = FString::Printf(TEXT("AIControllerClass=%s HuntVanillaHostiles(default)=%s HuntPassiveFauna(default)=%s"),
					ControllerClass ? *ControllerClass->GetName() : TEXT("none"),
					APFACreatureController::HuntVanillaHostilesDefault ? TEXT("true") : TEXT("false"),
					APFACreatureController::HuntPassiveFaunaDefault ? TEXT("true") : TEXT("false"));
				return bControllerWired && bDefaultsMatchDoc04;
			}});
		return true;
	}();
}
