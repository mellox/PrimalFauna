#include "PFACreatureController.h"
#include "PrimalFauna.h"
#include "PFACheats.h"
#include "PFACreatureBase.h"
#include "PFAConfig.h"
#include "PFASubsystem.h"
#include "Species/PFASicklejaw.h"

#include "Creature/FGCreature.h"
#include "Creature/FGCreatureSpawner.h"
#include "FGHealthComponent.h"
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

namespace
{
	// ⚠ TRAP, NAMED SO THE NEXT PERSON DOES NOT COPY IT AGAIN (w48-3, review W2):
	// THIS CVar MUST BE DECLARED AT FILE SCOPE, NOT INSIDE ITS FUNCTION.
	//
	// A function-local `static TAutoConsoleVariable` is constructed -- and therefore
	// REGISTERED with the console -- only when control first reaches its declaration.
	// Every other CVar in this mod is a function-local static (PFACreatureBase.cpp,
	// PFACreatureControllerPerception.cpp, the anim ones) and that is fine, because
	// each of those functions runs on BeginPlay or every tick, so the variable exists
	// by the time anyone could type its name. ForceRoamAfterDisengage is different:
	// it is EVENT-DRIVEN, running only on a disengage. Declared inside it, the name
	// `pfa.ForceRoamOnDisengage` did not exist at session start -- typing
	// `pfa.ForceRoamOnDisengage 0` was an UNKNOWN COMMAND that silently did nothing,
	// then the first disengage registered the variable at its default (1) and the
	// kick fired in that same call. The "off" arm of the A/B this packet exists to
	// run was unreachable, and the session would have produced a confident wrong
	// answer.
	//
	// RULE: a CVar whose host function is not guaranteed to run early (BeginPlay /
	// Tick / module startup) must be declared at file scope so module load registers
	// it. Behaviour is otherwise unchanged -- it is read exactly as before, via
	// .GetValueOnGameThread() at the top of ForceRoamAfterDisengage.
	//
	// Kill-switch, same pattern as pfa.AggroDecayAtRange / pfa.LocomotionAnims. This
	// touches the NATIVE state machine, so it must be A/B-able in one session without
	// a rebuild: `pfa.ForceRoamOnDisengage 0` reverts to w48-1 behaviour
	// (ResetTargetingState + TriggerStateUpdate only) while STILL printing the
	// current/desired pair, so turning it off keeps the diagnosis and loses only the
	// fix.
	TAutoConsoleVariable<int32> CVarPFAForceRoamOnDisengage(
		TEXT("pfa.ForceRoamOnDisengage"), 1,
		TEXT("1 = after a disengage, explicitly put the creature back into CS_Roam if the native state machine is still holding CS_Alert/CS_Fight (proven stuck in the w48-1 session). 0 = only observe and log the current/desired state pair."),
		ECVF_Default);
}

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
	// w27-1: mode-2 body yaw is driven from Tick, so it must actually run.
	// Set explicitly rather than trusting an inherited default.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

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

	// w45-3 (review D8): clear per-pawn blackboard/decay state on every possession.
	// A controller reused across a respawn would otherwise inherit a
	// bSpawnerKeyWriteFailed latch earned against a DIFFERENT pawn and never write
	// the key for the new one. Done here rather than in an OnUnPossess override,
	// which this class deliberately does not have (overriding it would force a
	// Super:: call into a closed-source FactoryGame method -- the shipping-export
	// trap that killed module load in w15-2).
	bSpawnerKeyWriteFailed = false;
	bLoggedNoSpawnerForBlackboard = false;
	mRangeDecayLoggedTargets.Reset();
	mRangeDecayAccumulator = 0.0f;
	bLoggedStaleZeroAggroTarget = false;
	// w46-1: the roam-leg accumulators are per-PAWN, so a reused controller must not
	// close a leg that a previous pawn started somewhere else in the world -- that
	// would print a kilometre-long "roam leg" and poison the one measurement this
	// build exists to take.
	bRoamLegActive = false;
	mRoamLegAccumulator = 0.0f;
	mRoamLegEndedTimeSeconds = 0.0;
	mRoamLegIndex = 0;
	bRoamWaitReported = false;

	// W1.8: wire perception FIRST -- independent of the BT launch (perception
	// feeds aggro; the BT consumes aggro), and must run even if the BT branch
	// below early-returns (a creature with no BT should still be able to
	// perceive). Self-contained + heavily instrumented; see the method.
	WireCreaturePerception(PossessedCreature);
	EnsureBehaviorTreeStarted(TEXT("OnPossess"));
}

bool APFACreatureController::EnsureBehaviorTreeStarted(const TCHAR* Trigger)
{
	AFGCreature* PossessedCreature = GetControlledCreature();
	if (!PossessedCreature)
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: EnsureBehaviorTreeStarted trigger=%s creature=none decision=skip outcome=no-controlled-creature"),
			*GetName(), Trigger ? Trigger : TEXT("<null>"));
		return false;
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
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: EnsureBehaviorTreeStarted trigger=%s creature=%s BT=none decision=defer outcome=borrow-not-ready"),
			*GetName(), Trigger ? Trigger : TEXT("<null>"), *PossessedCreature->GetName());
		return false;
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
			TEXT("[PFA] %s: EnsureBehaviorTreeStarted trigger=%s creature=%s BT=%s decision=no-op outcome=already-running"),
			*GetName(), Trigger ? Trigger : TEXT("<null>"), *PossessedCreature->GetName(), *BTAsset->GetName());
		return true;
	}

	const bool bStarted = RunBehaviorTree(BTAsset);
	const TCHAR* BlackboardState = GetBlackboardComponent() ? TEXT("present") : TEXT("none");
	const FString BTBlackboardAssetName = BTAsset->BlackboardAsset ? BTAsset->BlackboardAsset->GetName() : TEXT("none");

	if (bStarted)
	{
		// Success chain -- Display, always on (not diagnostics-gated): this is
		// the ONE line the W1.6 re-test greps for to confirm the fix landed.
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: EnsureBehaviorTreeStarted trigger=%s creature=%s BT=%s source=%s decision=start outcome=success blackboard=%s btBlackboardAsset=%s"),
			*GetName(), Trigger ? Trigger : TEXT("<null>"), *PossessedCreature->GetName(), *BTAsset->GetName(), BTSource, BlackboardState, *BTBlackboardAssetName);

		// w45-3 (review D1): FIRST blackboard publish, here and nowhere earlier.
		// The keys the vanilla BP controller used to write -- the roam band and the
		// roam anchor -- must land before the tree's first roam evaluation, not 30s
		// later on the first heartbeat beat (SetTimer does not fire immediately, so
		// a nest creature spent its whole first half-minute with no anchor while the
		// player who just spawned it stood there watching it do nothing).
		//
		// This is the earliest point where it is SAFE: RunBehaviorTree() above is
		// what calls UseBlackboard(), which is what assigns the blackboard component.
		// w45-2 put these in WireCreaturePerception, which runs before that call --
		// so they no-opped on a null blackboard, and could have latched the
		// write-failure flag permanently on a pure timing artefact.
		// The heartbeat keeps calling both: that is the self-healing path if
		// anything re-initialises the blackboard later in the creature's life.
		ApplyRoamBandToBlackboard();
		ApplySpawnerToBlackboard();
	}
	else
	{
		// RunBehaviorTree(...) itself only returns false when a required
		// UseBlackboard() call failed (BTAsset->BlackboardAsset was non-null
		// and incompatible/couldn't be created) -- verified by reading its
		// engine source directly (AIModule/Private/AIController.cpp:982). The
		// engine's own LogBehaviorTree channel (UE_VLOG) may carry more detail.
		UE_LOG(LogPrimalFauna, Error,
			TEXT("[PFA] %s: EnsureBehaviorTreeStarted trigger=%s creature=%s BT=%s decision=start outcome=failed blackboard=%s btBlackboardAsset=%s"),
			*GetName(), Trigger ? Trigger : TEXT("<null>"), *PossessedCreature->GetName(), *BTAsset->GetName(), BlackboardState, *BTBlackboardAssetName);
	}
	return bStarted;
}

void APFACreatureController::OnCreatureStateChanged_Implementation(
	ECreatureState PreviousState, ECreatureState NewState)
{
	Super::OnCreatureStateChanged_Implementation(PreviousState, NewState);
	// Read the persisted snapshot directly here: this callback can run before the
	// subsystem's first periodic config push, and a cached default must never
	// assist a pack when the saved PackAssist setting is disabled.
	if (!FPFAConfigStruct::GetActiveConfig(this).PackAssist || PreviousState == ECreatureState::CS_Fight ||
		NewState != ECreatureState::CS_Fight)
	{
		return;
	}

	AFGCreature* Leader = GetControlledCreature();
	AFGCreatureSpawner* Spawner = Leader ? Leader->GetSpawner() : nullptr;
	AActor* Target = GetCurrentTarget();
	if (!Target)
	{
		Target = GetHighestAggroTarget(ETargetVisibilityRequirement::TVR_None, 0.0f);
	}
	if (!Leader || !Spawner || !Target)
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] PackAssist: leader=%s spawner=%s target=%s candidates=0 joined=0 decision=skip outcome=missing-context"),
			*GetNameSafe(Leader), *GetNameSafe(Spawner), *GetNameSafe(Target));
		return;
	}
	if (!IsInGameThread())
	{
		UE_LOG(LogPrimalFauna, Error,
			TEXT("[PFA] PackAssist: leader=%s spawner=%s target=%s candidates=0 joined=0 decision=skip outcome=not-game-thread"),
			*Leader->GetName(), *Spawner->GetName(), *Target->GetName());
		return;
	}

	// ForceAggro requests an immediate native state transition. That transition
	// synchronously invokes this callback on the assisted controller, so a large
	// pack would otherwise recurse once per member and rescan the same spawner at
	// every depth. One game-thread guard per spawner keeps the whole assist wave a
	// single bounded O(n) loop while allowing independent spawners to assist.
	static TSet<FObjectKey> ActivePackAssistSpawners;
	const FObjectKey SpawnerKey(Spawner);
	if (ActivePackAssistSpawners.Contains(SpawnerKey))
	{
		UE_LOG(LogPrimalFauna, Verbose,
			TEXT("[PFA] PackAssist: leader=%s spawner=%s target=%s candidates=0 joined=0 decision=skip outcome=reentrant-wave"),
			*Leader->GetName(), *Spawner->GetName(), *Target->GetName());
		return;
	}
	ActivePackAssistSpawners.Add(SpawnerKey);
	struct FScopedPackAssistWave
	{
		TSet<FObjectKey>& ActiveSpawners;
		FObjectKey Key;
		~FScopedPackAssistWave()
		{
			ActiveSpawners.Remove(Key);
		}
	} ScopedWave{ActivePackAssistSpawners, SpawnerKey};

	int32 Candidates = 0;
	int32 Accepted = 0;
	int32 Joined = 0;
	for (AFGCreature* PackCreature : Spawner->GetCreatures())
	{
		if (!PackCreature || PackCreature == Leader || PackCreature->IsActorBeingDestroyed())
		{
			continue;
		}
		const UFGHealthComponent* Health = PackCreature->GetHealthComponent();
		if (!Health || Health->IsDead())
		{
			continue;
		}
		APFACreatureController* PackController = Cast<APFACreatureController>(PackCreature->GetController());
		if (!PackController)
		{
			continue;
		}
		const ECreatureState PackState = PackController->GetCurrentCreatureState();
		if (PackState != ECreatureState::CS_Default && PackState != ECreatureState::CS_Roam)
		{
			continue;
		}
		++Candidates;
		const float RequestedAggro = PackCreature->GetAggroFightThreshold() + 1.0f;
		const bool bAccepted = PackController->ForceAggro(Target, RequestedAggro);
		Accepted += bAccepted ? 1 : 0;
		const bool bJoined = bAccepted &&
			PackController->GetCurrentCreatureState() == ECreatureState::CS_Fight &&
			PackController->GetCurrentTarget() == Target &&
			PackController->GetAggroForTarget(Target) >= RequestedAggro;
		Joined += bJoined ? 1 : 0;
	}

	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] PackAssist: leader=%s spawner=%s target=%s candidates=%d accepted=%d joined=%d decision=share-fight-target outcome=%s"),
		*Leader->GetName(), *Spawner->GetName(), *Target->GetName(), Candidates, Accepted, Joined,
		Joined == Candidates ? TEXT("complete") : TEXT("partial"));
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

void APFACreatureController::ForceRoamAfterDisengage(const TCHAR* Reason)
{
	// The `pfa.ForceRoamOnDisengage` kill-switch is declared at FILE SCOPE at the top
	// of this TU, not here -- see the trap note there. Declaring it inside this
	// event-driven function meant it did not exist until the first disengage.

	// ---- ITEM 1: THE DIAGNOSIS FIELD -----------------------------------------
	// GetDesiredCreatureState (FGCreatureController.h:189) is what the machine
	// WANTS; GetCurrentCreatureState (:182) is what it IS. Printing them together
	// splits the problem cleanly, and the two halves need different fixes:
	//   desired=CS_Roam while current=CS_Alert -> the machine knows it should roam
	//     and the transition simply is not being applied. The force below IS the
	//     fix, and `took=true` proves it.
	//   desired=CS_Alert                        -> the native "desired" logic still
	//     WANTS alert with zero aggro and no target, so something other than aggro
	//     is holding it (leftover perception/visibility stimuli are the prime
	//     suspect -- ResetTargetingState already clears mAggroTargets and
	//     mVisibleTargets, so the residue would have to be in the perception
	//     component's own stimuli). Then forcing is the WRONG LAYER, and the fix
	//     belongs in a separate packet (UAIPerceptionComponent::ForgetAll/ForgetActor
	//     are engine AIMODULE_API, so no FactoryGame export risk -- but that is a
	//     different change).
	//     TWO OUTCOMES ARE POSSIBLE HERE AND WE DO NOT KNOW WHICH, so read the next
	//     heartbeat rather than assuming (w48-3, review INFO-2): if
	//     UpdateCreatureState re-runs on its own, the forced CS_Roam REVERTS and the
	//     heartbeat says CS_Alert again; if it is trigger-driven instead
	//     (mShouldUpdateCreatureState, FGCreatureController.h:514) the forced state
	//     can LATCH and the heartbeat keeps reading CS_Roam while desired stays
	//     CS_Alert -- a creature that looks fixed in the log while the real cause is
	//     untouched. Either way the upstream perception fix is what is needed; the
	//     heartbeat's state/desired pair is what tells them apart.
	//
	// ---- EXPORT NOTE: A HOUSE RULE OF THIS PROJECT IS WRONG, AND THE BINARY SAID
	// SO. -----------------------------------------------------------------------
	// The packet (and earlier reviews in this repo) reasoned "GetDesiredCreature
	// State is virtual => vtable-dispatched => it can never appear in the import
	// table". I wrote that here, then dumped the built DLL, and it is FALSE:
	//   ?GetDesiredCreatureState@AFGCreatureController@@UEBA?AW4ECreatureState@@XZ
	// IS in this module's import table.
	//
	// The reason is structural, not a compiler whim: it is a base-class virtual we
	// do NOT override, and APFACreatureController's vtable is emitted in OUR
	// module, so our vtable must contain the address of the BASE implementation.
	// That address is a direct cross-DLL symbol reference. So for an un-overridden
	// base virtual, "virtual" buys nothing at all -- the import exists whether or
	// not anyone calls the function.
	//
	// What that also proves is that CALLING it here costs ZERO new import surface,
	// because the symbol was already there. Measured, not argued: this build's
	// FactoryGame imports went 174 -> 176 (total 900 -> 902) while THREE
	// CreatureState symbols are now in the table -- so exactly two are new
	// (SetCurrentCreatureState + IsCreatureStateEnabled) and this one was already
	// present before the packet.
	//
	// The thing that actually makes it safe is the same thing that makes every
	// other FactoryGame call safe: the shipping DLL EXPORTS it (dumpbin ordinal
	// 18422), verified before the call was written and re-proven by
	// check_imports.ps1 on the built binary. Treat "is it exported?" as the only
	// question; "is it virtual?" is not a substitute for asking it.
	const ECreatureState Before = GetCurrentCreatureState();
	const ECreatureState Desired = GetDesiredCreatureState();

	// IsCreatureStateEnabled (FGCreature.h:366, public UFUNCTION; exported --
	// dumpbin ordinal 23664) answers the one question a bare `took=false` would
	// leave open: whether CS_Roam is even in this creature's enabled-state mask. A
	// refusal with roamEnabled=0 is a species-authoring bug, not a state-machine
	// one, and the two need opposite fixes.
	const AFGCreature* KickCreature = Cast<AFGCreature>(GetPawn());
	const int32 RoamEnabled = KickCreature
		? (KickCreature->IsCreatureStateEnabled(ECreatureState::CS_Roam) ? 1 : 0)
		: -1;

	// The desired/current disagreement is the whole diagnosis, so say it in the
	// line rather than leaving it to be re-derived from two enum names.
	const TCHAR* DesiredVerdict = (Desired == ECreatureState::CS_Roam)
		? TEXT(" The machine WANTS CS_Roam, so this is purely a transition that was not applied.")
		: TEXT(" The machine still WANTS this non-roam state with zero aggro and no target, so something other than aggro is holding it (perception stimuli residue is the prime suspect). If the next heartbeat reads CS_Alert again, forcing is the wrong layer and the fix belongs upstream.");

	if (CVarPFAForceRoamOnDisengage.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: disengage state check (%s): current=%s desired=%s roamEnabled=%d -- pfa.ForceRoamOnDisengage=0, NOT forcing.%s"),
			*GetName(), Reason,
			*CreatureStateEnumToString(Before), *CreatureStateEnumToString(Desired),
			RoamEnabled, DesiredVerdict);
		return;
	}

	if (Before == ECreatureState::CS_Roam)
	{
		// Nothing was stuck. Still always-on: the ABSENCE of a kick line would be
		// ambiguous between "already roaming" and "the helper never ran".
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: disengage state check (%s): already CS_Roam (desired=%s) -- no kick needed."),
			*GetName(), Reason, *CreatureStateEnumToString(Desired));
		return;
	}

	// ---- w48-3 (review W1): KICK ONLY THE STUCK-ALERT SHAPE. -------------------
	// The contract this helper was written for is "the machine is still holding
	// CS_Alert/CS_Fight after a disengage" -- but w48-2 implemented `anything that
	// is not CS_Roam`, which is a strictly wider set and includes two states that
	// are legitimately NOT aggro:
	//   CS_Flee       -- driven by mStressLevel (SetStressLevel /
	//                    ModifyStressLevel / GetStressAccumulationFromDamage), an
	//                    axis independent of aggro. A creature fleeing while a
	//                    distant target's aggro drains out would be yanked out of
	//                    Flee into Roam.
	//   CS_Investigate -- a creature checking a noise (ShouldInvestigateStimulus is
	//                    live in our import table), likewise not an aggro hold.
	// This is a DIFFERENTIAL miss, not a hypothetical: the OLD path
	// (ResetTargetingState alone) could not change the behaviour STATE at all, so
	// it could never cancel a flee. The new path can, so the narrowing has to be
	// explicit.
	//
	// CS_Default stays in the allowed set deliberately -- the pre-existing heartbeat
	// text already calls CS_Default a stuck shape.
	if (Before != ECreatureState::CS_Alert
		&& Before != ECreatureState::CS_Fight
		&& Before != ECreatureState::CS_Default)
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: disengage state check (%s): current=%s desired=%s roamEnabled=%d -- NOT kicking: this is not a stuck-alert shape (Flee/Investigate are legitimate non-aggro states and forcing Roam would cancel them)."),
			*GetName(), Reason,
			*CreatureStateEnumToString(Before), *CreatureStateEnumToString(Desired),
			RoamEnabled);
		// DesiredVerdict is deliberately NOT appended here: its text is written for
		// the stuck-alert case ("something other than aggro is holding it, perception
		// residue is the prime suspect") and would be a false explanation for a
		// stress-driven Flee. The log has to be true before anything else is.
		return;
	}

	// ---- ITEM 2: APPLY THE TRANSITION ----------------------------------------
	// SetCurrentCreatureState (FGCreatureController.h:346) is the setter the header
	// itself points at: ":528-530 Current behavior state of the creature. Don't set
	// directly, call SetCurrentCreatureState." It is `protected` (plain subclass
	// access) and NON-virtual, so this IS a direct FactoryGame symbol import -- the
	// exact shipping-export-trap shape. Verified present in the shipping DLL before
	// writing this line (dumpbin ordinal 29575,
	// ?SetCurrentCreatureState@AFGCreatureController@@IEAA_NW4ECreatureState@@@Z),
	// and check_imports.ps1 re-proves it against the built binary every build.
	//
	// This is the machine's OWN mechanism, not a bypass: it is what the native
	// UpdateCreatureState calls, so OnCreatureStateChanged and everything hanging
	// off it still fire.
	//
	// It returns bool. A REFUSED transition must never read as a successful one --
	// that is the whole reason the return value is logged rather than discarded.
	const bool bTook = SetCurrentCreatureState(ECreatureState::CS_Roam);
	const ECreatureState After = GetCurrentCreatureState();

	const TCHAR* Outcome =
		(bTook && After == ECreatureState::CS_Roam)
			? TEXT("TOOK")
			: (!bTook
				? TEXT("REFUSED by the native machine (returned false)")
				: TEXT("RETURNED TRUE BUT THE STATE IS NOT CS_Roam -- the setter's contract is not what we assumed"));

	// ALWAYS-ON. This is a behaviour change on a path the user is testing, and
	// w44-1 is this project's standing lesson that evidence for a behaviour change
	// must not sit behind PFADiag::IsEnabled().
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: disengage state KICK (%s): current=%s desired=%s roamEnabled=%d -> SetCurrentCreatureState(CS_Roam) returned %s, state now %s [%s].%s"),
		*GetName(), Reason,
		*CreatureStateEnumToString(Before), *CreatureStateEnumToString(Desired),
		RoamEnabled,
		bTook ? TEXT("true") : TEXT("false"),
		*CreatureStateEnumToString(After),
		Outcome, DesiredVerdict);
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

	const bool bRegistered_SicklejawBehaviorTreesRunning = []
	{
		RegisterPFASelfTest({TEXT("SicklejawBehaviorTreesRunning"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				int32 Controllers = 0;
				int32 NoCreature = 0;
				int32 NoTree = 0;
				int32 BrainNotRunning = 0;
				for (TActorIterator<APFACreatureController> It(World); It; ++It)
				{
					APFACreatureController* Controller = *It;
					++Controllers;
					AFGCreature* Creature = Controller ? Controller->GetControlledCreature() : nullptr;
					NoCreature += Creature ? 0 : 1;
					NoTree += (Creature && Creature->GetBehaviorTreeAsset()) ? 0 : 1;
					const UBrainComponent* Brain = Controller ? Controller->GetBrainComponent() : nullptr;
					BrainNotRunning += (Brain && Brain->IsRunning()) ? 0 : 1;
				}
				OutDetail = FString::Printf(
					TEXT("controllers=%d noCreature=%d noTree=%d brainNotRunning=%d"),
					Controllers, NoCreature, NoTree, BrainNotRunning);
				if (Controllers == 0)
				{
					OutDetail = TEXT("NO-COVERAGE: no live PFA controllers; spawn a Sicklejaw and re-run");
					return false;
				}
				return Controllers > 0 && NoCreature == 0 && NoTree == 0 && BrainNotRunning == 0;
			}});
		return true;
	}();

	const bool bRegistered_PackAssistCoverage = []
	{
		RegisterPFASelfTest({TEXT("PackAssistCoverage"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				if (!PFAFactionConfig::PackAssist())
				{
					OutDetail = TEXT("NO-COVERAGE: PackAssist is disabled");
					return true;
				}

				int32 FightingPackLeaders = 0;
				int32 ObservedPackmates = 0;
				int32 EligibleLeftBehind = 0;
				for (TActorIterator<APFACreatureController> It(World); It; ++It)
				{
					APFACreatureController* LeaderController = *It;
					AFGCreature* Leader = LeaderController ? LeaderController->GetControlledCreature() : nullptr;
					AFGCreatureSpawner* Spawner = Leader ? Leader->GetSpawner() : nullptr;
					AActor* Target = LeaderController ? LeaderController->GetCurrentTarget() : nullptr;
					if (!Leader || !Spawner || !Target ||
						LeaderController->GetCurrentCreatureState() != ECreatureState::CS_Fight)
					{
						continue;
					}

					int32 ThisPackmates = 0;
					for (AFGCreature* PackCreature : Spawner->GetCreatures())
					{
						if (!PackCreature || PackCreature == Leader || PackCreature->IsActorBeingDestroyed())
						{
							continue;
						}
						const UFGHealthComponent* Health = PackCreature->GetHealthComponent();
						APFACreatureController* PackController =
							Cast<APFACreatureController>(PackCreature->GetController());
						if (!Health || Health->IsDead() || !PackController)
						{
							continue;
						}
						++ThisPackmates;
						const ECreatureState PackState = PackController->GetCurrentCreatureState();
						EligibleLeftBehind +=
							(PackState == ECreatureState::CS_Default || PackState == ECreatureState::CS_Roam) ? 1 : 0;
					}
					if (ThisPackmates > 0)
					{
						++FightingPackLeaders;
						ObservedPackmates += ThisPackmates;
					}
				}

				if (FightingPackLeaders == 0)
				{
					OutDetail = TEXT("NO-COVERAGE: no live fighting same-spawner PFA pack exists; aggro one member and re-run");
					return true;
				}
				OutDetail = FString::Printf(
					TEXT("fightingPackLeaders=%d observedPackmates=%d eligibleLeftBehind=%d"),
					FightingPackLeaders, ObservedPackmates, EligibleLeftBehind);
				return EligibleLeftBehind == 0;
			}});
		return true;
	}();
}
