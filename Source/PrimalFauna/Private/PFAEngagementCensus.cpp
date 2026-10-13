// ===========================================================================
// PFAEngagementCensus.cpp -- w58-5. See PFAEngagementCensus.h for why.
//
// LOG GRAMMAR (deliberate, per the workspace log rules):
//   * The legend line contains NO "name value" pair written in the form the
//     fields themselves print -- it holds no equals sign at all -- so counting
//     a field can anchor on the field's own syntax without the legend matching
//     too. Every explanation below is prose.
//   * Every number printed is measured on THIS pass, or is a timestamped
//     sample whose AGE is printed beside it. Nothing is baked in: the stall
//     threshold is read from its console variable every pass and printed.
//   * The census states what it measured. It never states a cause. It does
//     not say a creature is stuck BECAUSE of anything -- it says the target
//     graded dead, or that N seconds passed with zero action starts.
//   * Distribution is recoverable per creature: every engaged controller gets
//     its own line, not just a total.
// ===========================================================================
#include "PFAEngagementCensus.h"

#include "PFACheats.h"
#include "PFACreatureBase.h"
#include "PFACreatureController.h"
#include "PrimalFauna.h"

#include "Creature/FGCreature.h"
#include "FGCharacterBase.h"
#include "FGHealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// Sized from measured combat timing on this species rather than picked:
	// mActionCooldown = 2.5s, mActionCooldownMax = 6.0s and mActionStartDelay
	// tops out at 0.65s (Actions/PFACreatureActionMelee.cpp), so a creature
	// that is attacking perfectly normally can be silent for ~6.65s. 8.0s
	// clears that by ~1.35s and sits far below the 46-50s holds measured on
	// 2026-08-18, so the window between "healthy" and "the bug" is wide.
	//
	// It is a console variable and it is PRINTED on every line that uses it,
	// so the number a reader sees is the number that ran.
	TAutoConsoleVariable<float> CVarPFAEngagementStallSeconds(
		TEXT("pfa.EngagementStallSeconds"), 8.0f,
		TEXT("Seconds a creature may sit in CS_Fight on an unchanged target with ZERO melee action starts before the engagement self-test reports a stall. Sized above this species' own worst-case cooldown plus start delay."),
		ECVF_Default);

	bool& EngagementLegendEmitted()
	{
		static bool bEmitted = false;
		return bEmitted;
	}

	int32& EngagementPassCounter()
	{
		static int32 Passes = 0;
		return Passes;
	}

	// One walk of the world, shared by RunPass and EvaluateEngagementLiveness so
	// the census and the self-test can never disagree about what they saw.
	struct FEngagementRow
	{
		const APFACreatureController* Controller = nullptr;
		const AActor* Target = nullptr;
		PFAEngagement::ETargetLiveness Liveness = PFAEngagement::ETargetLiveness::Destroyed;
		ECreatureState State = ECreatureState::CS_Default;
		float TargetDistCm = -1.0f;
		float Aggro = -1.0f;
		float HeldSeconds = -1.0f;
		int32 PerformStartsThisHold = 0;
		float SecsSinceLastPerform = -1.0f;
		float CanPerformSampleAgeSec = -1.0f;
		bool bStalled = false;
		bool bDeadTargetHeld = false;
	};

	void CollectEngagementRows(UWorld* World, TArray<FEngagementRow>& OutRows,
		int32& OutControllersSeen, int32& OutAggroedNoTarget, float StallSeconds)
	{
		const double NowSeconds = static_cast<double>(World->GetTimeSeconds());

		for (TActorIterator<APFACreatureBase> It(World); It; ++It)
		{
			const APFACreatureBase* Creature = *It;
			if (!Creature || Creature->IsActorBeingDestroyed())
			{
				continue;
			}
			// w58-5 (review M5): grade the OBSERVER too. c9a567f keeps a corpse in
			// the world for 120s; whether it keeps its controller is engine-internal
			// (AFGCreature::Died is closed). A dead creature holding a stale target
			// would otherwise sit in this census as a permanent stall.
			const UFGHealthComponent* SelfHealth = Creature->GetHealthComponent();
			if (SelfHealth && SelfHealth->IsDead())
			{
				continue;
			}
			const APFACreatureController* Controller =
				Cast<APFACreatureController>(Creature->GetController());
			if (!Controller)
			{
				continue;
			}
			++OutControllersSeen;

			const AActor* Target = Controller->GetCurrentTarget();
			if (!Target)
			{
				// w58-7 (r2 review F2): a controller with NO current target used to
				// leave this census silently. After the selective dead-target drop
				// that is the shape of the failure the drop can cause -- ledger
				// retained, nothing bound, nothing attacked -- so it needs a set of
				// its own. Counted here, not asserted here: a creature between a
				// disengage and its next roam leg is also targetless and healthy.
				// What separates them is LIVE AGGRO, which is measured, not inferred.
				int32 LiveEntries = 0;
				for (const FFGCreatureAggroData& AggroData : Controller->GetAggroTargets())
				{
					const AActor* Entry = AggroData.Target;
					if (Entry && AggroData.Aggro > KINDA_SMALL_NUMBER &&
						!PFAEngagement::IsConfirmedNotFightable(PFAEngagement::GradeTargetLiveness(Entry)))
					{
						++LiveEntries;
					}
				}
				if (LiveEntries > 0)
				{
					++OutAggroedNoTarget;
					UE_LOG(LogPrimalFauna, Warning,
						TEXT("[PFA] Engagement controller=%s currentTarget=none liveAggroEntries=%d state=%s. Measured: this controller holds aggro for at least one live actor and has no current target bound. Why it has none was not tested here."),
						*Controller->GetName(), LiveEntries,
						*CreatureStateEnumToString(Controller->GetCurrentCreatureState()));
				}
				continue;
			}

			FEngagementRow Row;
			Row.Controller = Controller;
			Row.Target = Target;
			Row.Liveness = PFAEngagement::GradeTargetLiveness(Target);
			Row.State = Controller->GetCurrentCreatureState();
			Row.Aggro = Controller->GetAggroForTarget(Target);
			Row.TargetDistCm = FVector::Dist(Creature->GetActorLocation(), Target->GetActorLocation());

			const FPFAEngagementObservation& Obs = Controller->GetEngagementObservation();
			if (Obs.HeldTargetSinceSeconds >= 0.0 && Obs.HeldTarget.Get() == Target)
			{
				Row.HeldSeconds = static_cast<float>(NowSeconds - Obs.HeldTargetSinceSeconds);
				Row.PerformStartsThisHold = Obs.PerformStartCount - Obs.PerformStartCountAtHoldStart;
			}
			if (Obs.LastPerformStartTimeSeconds >= 0.0)
			{
				Row.SecsSinceLastPerform = static_cast<float>(NowSeconds - Obs.LastPerformStartTimeSeconds);
			}
			if (Obs.bHasCanPerformSample && Obs.LastCanPerformSampleTimeSeconds >= 0.0)
			{
				Row.CanPerformSampleAgeSec = static_cast<float>(NowSeconds - Obs.LastCanPerformSampleTimeSeconds);
			}

			// THE TWO ASSERTIONS, evaluated from measurements only.
			Row.bDeadTargetHeld = PFAEngagement::IsConfirmedNotFightable(Row.Liveness);
			Row.bStalled = (Row.State == ECreatureState::CS_Fight)
				&& (Row.HeldSeconds >= StallSeconds)
				&& (Row.PerformStartsThisHold == 0);

			OutRows.Add(Row);
		}
	}

	FString FormatEngagementRow(const FEngagementRow& Row)
	{
		const FPFAEngagementObservation& Obs = Row.Controller->GetEngagementObservation();
		const TCHAR* CanPerformField = Obs.bHasCanPerformSample
			? (Obs.bLastCanPerform ? TEXT("1") : TEXT("0"))
			: TEXT("unsampled");

		return FString::Printf(
			TEXT("[PFA] Engagement controller=%s currentTarget=%s targetAlive=%s targetLiveness=%s targetDistCm=%.0f aggro=%.1f state=%s minRange=%.0f attackRange=%.0f canPerform=%s canPerformSampleAgeSec=%.2f cooldownRemainAtSampleSec=%.2f actionStateAtSample=%d performStartsThisHold=%d secsSinceLastPerform=%.1f heldSecs=%.1f stalled=%d deadTargetHeld=%d"),
			*Row.Controller->GetName(),
			*Row.Target->GetName(),
			PFAEngagement::LivenessAliveField(Row.Liveness),
			PFAEngagement::LivenessToken(Row.Liveness),
			Row.TargetDistCm,
			Row.Aggro,
			*CreatureStateEnumToString(Row.State),
			Obs.LastSampledMinRangeCm,
			Obs.LastSampledAttackRangeCm,
			CanPerformField,
			Row.CanPerformSampleAgeSec,
			Obs.LastSampledCooldownRemainSeconds,
			Obs.LastSampledActionState,
			Row.PerformStartsThisHold,
			Row.SecsSinceLastPerform,
			Row.HeldSeconds,
			Row.bStalled ? 1 : 0,
			Row.bDeadTargetHeld ? 1 : 0);
	}
}

namespace PFAEngagement
{
	ETargetLiveness GradeTargetLiveness(const AActor* Target)
	{
		if (!IsValid(Target) || Target->IsActorBeingDestroyed())
		{
			return ETargetLiveness::Destroyed;
		}
		// AFGCharacterBase::GetHealthComponent is FORCEINLINE (FGCharacterBase.h
		// :210) and UFGHealthComponent::IsDead is FORCEINLINE (FGHealthComponent.h
		// :137), so this predicate adds no import surface. It covers creatures and
		// the player character alike -- both derive from AFGCharacterBase, which is
		// the point: liveness is asked of the TARGET, whatever it is.
		const AFGCharacterBase* Character = Cast<AFGCharacterBase>(Target);
		if (!Character)
		{
			return ETargetLiveness::NoHealthComponent;
		}
		const UFGHealthComponent* Health = Character->GetHealthComponent();
		if (!Health)
		{
			return ETargetLiveness::NoHealthComponent;
		}
		return Health->IsDead() ? ETargetLiveness::Dead : ETargetLiveness::Alive;
	}

	const TCHAR* LivenessToken(ETargetLiveness Liveness)
	{
		switch (Liveness)
		{
		case ETargetLiveness::Alive:             return TEXT("alive");
		case ETargetLiveness::Dead:              return TEXT("dead");
		case ETargetLiveness::Destroyed:         return TEXT("destroyed");
		case ETargetLiveness::NoHealthComponent: return TEXT("nohealthcomponent");
		}
		return TEXT("nohealthcomponent");
	}

	const TCHAR* LivenessAliveField(ETargetLiveness Liveness)
	{
		switch (Liveness)
		{
		case ETargetLiveness::Alive:     return TEXT("1");
		case ETargetLiveness::Dead:      return TEXT("0");
		case ETargetLiveness::Destroyed: return TEXT("0");
		// NEVER 0 and never 1: no measurement was taken, and a binary field that
		// invents a value is how a wrong label makes a reader stop looking.
		case ETargetLiveness::NoHealthComponent: return TEXT("unknown");
		}
		return TEXT("unknown");
	}

	bool IsConfirmedNotFightable(ETargetLiveness Liveness)
	{
		return Liveness == ETargetLiveness::Dead || Liveness == ETargetLiveness::Destroyed;
	}

	float GetStallSeconds()
	{
		return FMath::Max(0.0f, CVarPFAEngagementStallSeconds.GetValueOnGameThread());
	}

	void RunPass(UWorld* World, const TCHAR* Trigger, bool bLogEveryCreature)
	{
		if (!World)
		{
			return;
		}

		const float StallSeconds = GetStallSeconds();
		TArray<FEngagementRow> Rows;
		int32 ControllersSeen = 0;
		int32 AggroedNoTarget = 0;
		CollectEngagementRows(World, Rows, ControllersSeen, AggroedNoTarget, StallSeconds);

		int32 Stalled = 0;
		int32 DeadTargetsHeld = 0;
		int32 AliveTargets = 0;
		int32 UnknownLiveness = 0;
		float LongestHeldSeconds = -1.0f;
		for (const FEngagementRow& Row : Rows)
		{
			Stalled += Row.bStalled ? 1 : 0;
			DeadTargetsHeld += Row.bDeadTargetHeld ? 1 : 0;
			AliveTargets += (Row.Liveness == ETargetLiveness::Alive) ? 1 : 0;
			UnknownLiveness += (Row.Liveness == ETargetLiveness::NoHealthComponent) ? 1 : 0;
			LongestHeldSeconds = FMath::Max(LongestHeldSeconds, Row.HeldSeconds);
		}

		const int32 Pass = ++EngagementPassCounter();

		if (!EngagementLegendEmitted())
		{
			EngagementLegendEmitted() = true;
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] Engagement legend: one pass iterates every PrimalFauna creature actor in the world and reports the ones that currently hold a target. ")
				TEXT("A target counts as alive when its own health component reports not dead, as dead when that component reports dead, as destroyed when the actor is gone or being destroyed, ")
				TEXT("and as unknown when the actor is not a character this mod can ask -- unknown is never treated as dead anywhere in this mod. ")
				TEXT("Held seconds is the time since this controller last bound this exact actor, and a momentary null current target does not restart it, because the native current target flickers null once or twice a second during a live fight; a targeting reset does restart it. ")
				TEXT("Perform starts this hold counts melee action starts since that binding. ")
				TEXT("The two range figures, the can-perform verdict, the remaining cooldown and the action state are the values the melee action itself recorded the last time the behaviour tree asked it, and the sample age beside them says how old that recording is. ")
				TEXT("A stall is reported when a creature is in the fight state, has held one target for at least the printed stall threshold, and has started zero melee actions in that whole hold. ")
				TEXT("Every figure here is a measurement or a count. Nothing on these lines states a cause."));
		}

		// ALWAYS-ON summary. The denominator is on the line so a zero can never
		// be read as a finding on its own.
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] EngagementCensus pass=%d trigger=%s controllersSeen=%d withTarget=%d aggroedNoTarget=%d aliveTargets=%d deadTargetsHeld=%d unknownLiveness=%d stalled=%d longestHeldSecs=%.1f stallThresholdSecs=%.1f detail=%d"),
			Pass, Trigger, ControllersSeen, Rows.Num(), AggroedNoTarget, AliveTargets, DeadTargetsHeld,
			UnknownLiveness, Stalled, LongestHeldSeconds, StallSeconds,
			(bLogEveryCreature || PFADiag::IsEnabled()) ? 1 : 0);

		// Per-creature distribution. Detail lines are diag-gated or forced by the
		// console command -- EXCEPT any row that failed, which always prints: a
		// finding that only appears while diagnostics are on is a finding nobody
		// gets. This is the same rule the w44-1 fix established for behaviour.
		for (const FEngagementRow& Row : Rows)
		{
			if (bLogEveryCreature || PFADiag::IsEnabled() || Row.bStalled || Row.bDeadTargetHeld)
			{
				UE_LOG(LogPrimalFauna, Display, TEXT("%s"), *FormatEngagementRow(Row));
			}
		}

		// The always-on assertion. TWO CLASSES, TWO NAMES, deliberately.
		// deadTargetsHeld is the defect this packet exists to prevent and must never
		// be non-zero in a healthy session -- it keeps the FAIL token.
		// stalled counts creatures in the fight state that started no melee action in
		// the printed window. That window is sized from this species' cooldown only,
		// so a creature that is PURSUING, pathing, or out of attack range counts here
		// while behaving correctly. It is reported under its own name so a reader is
		// never trained to ignore a FAIL.
		if (DeadTargetsHeld > 0)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA][TEST] EngagementLiveness: FAIL deadTargetsHeld=%d of withTarget=%d (controllersSeen=%d). Each of these controllers holds a target whose own health component reported dead on this pass. Per-creature lines above carry the distribution."),
				DeadTargetsHeld, Rows.Num(), ControllersSeen);
		}
		if (AggroedNoTarget > 0)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA][TEST] EngagementBinding: FAIL aggroedNoTarget=%d of controllersSeen=%d. Each of these controllers holds aggro for at least one live actor and has no current target bound. Per-controller lines above name them."),
				AggroedNoTarget, ControllersSeen);
		}
		if (Stalled > 0)
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA][TEST] EngagementStall: OBSERVED stalled=%d of withTarget=%d (controllersSeen=%d, stallThresholdSecs=%.1f). Measured: fight state, one unchanged target held at least that long, zero melee action starts in the hold. Whether the creature was in attack range during that hold was NOT tested here -- read targetDistCm against attackRangeCm on the per-creature lines above."),
				Stalled, Rows.Num(), ControllersSeen, StallSeconds);
		}
	}

	bool EvaluateEngagementLiveness(UWorld* World, FString& OutDetail)
	{
		if (!World)
		{
			OutDetail = TEXT("NO-COVERAGE: no world.");
			return true;
		}

		const float StallSeconds = GetStallSeconds();
		TArray<FEngagementRow> Rows;
		int32 ControllersSeen = 0;
		int32 AggroedNoTarget = 0;
		CollectEngagementRows(World, Rows, ControllersSeen, AggroedNoTarget, StallSeconds);

		int32 Stalled = 0;
		int32 DeadTargetsHeld = 0;
		FString Offenders;
		for (const FEngagementRow& Row : Rows)
		{
			Stalled += Row.bStalled ? 1 : 0;
			DeadTargetsHeld += Row.bDeadTargetHeld ? 1 : 0;
			if (Row.bStalled || Row.bDeadTargetHeld)
			{
				Offenders += FString::Printf(TEXT("[%s target %s graded %s held %.1fs starts %d] "),
					*Row.Controller->GetName(), *Row.Target->GetName(),
					LivenessToken(Row.Liveness), Row.HeldSeconds, Row.PerformStartsThisHold);
			}
		}

		if (Rows.Num() == 0 && AggroedNoTarget == 0)
		{
			// A zero needs a denominator, and this one is honest about being an
			// empty set rather than reporting a PASS nobody earned.
			OutDetail = FString::Printf(
				TEXT("NO-COVERAGE: %d PrimalFauna controller(s) seen, none holding a target, so nothing was asserted. stallThresholdSecs=%.1f aggroedNoTarget=0"),
				ControllersSeen, StallSeconds);
			return true;
		}

		OutDetail = FString::Printf(
			TEXT("controllersSeen=%d withTarget=%d stalled=%d deadTargetsHeld=%d stallThresholdSecs=%.1f %s"),
			ControllersSeen, Rows.Num(), Stalled, DeadTargetsHeld, StallSeconds,
			Offenders.IsEmpty() ? TEXT("") : *Offenders);

		// The self-test asserts the dead-target half AND the unbound half. The stall
		// count is reported in OutDetail so the number is never lost, but it does not
		// fail the suite while the deferred attack-range defect can produce it.
		return (DeadTargetsHeld == 0) && (AggroedNoTarget == 0);
	}
}

namespace
{
	// Registered the same way every other PFA self-test is (see PFACheats.h).
	const bool bRegistered_EngagementLiveness = []
	{
		RegisterPFASelfTest({TEXT("EngagementLiveness"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				return PFAEngagement::EvaluateEngagementLiveness(World, OutDetail);
			}});
		return true;
	}();

	// PFA.EngagementCensus -- an on-demand full-detail pass, so a human or the
	// automated tester can sample the moment a creature stops attacking instead
	// of waiting for the 10s heartbeat to line up with it.
	FAutoConsoleCommandWithWorld EngagementCensusCommand(
		TEXT("PFA.EngagementCensus"),
		TEXT("Log one PrimalFauna engagement census pass with a line per creature that holds a target."),
		FConsoleCommandWithWorldDelegate::CreateLambda(
			[](UWorld* World)
			{
				if (!World)
				{
					UE_LOG(LogPrimalFauna, Warning,
						TEXT("[PFA] PFA.EngagementCensus aborted: no world context."));
					return;
				}
				PFAEngagement::RunPass(World, TEXT("console"), /*bLogEveryCreature=*/true);
			}));
}
