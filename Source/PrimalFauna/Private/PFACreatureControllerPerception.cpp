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

#include "Creature/FGCreature.h"

#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Sight.h"

#include "Engine/World.h"
#include "TimerManager.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
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

	// Step 7 (w26-1): keep the AI focus on whatever we are hunting, so the
	// pawn's rotation policy has something to aim at while closing. Another
	// job the vanilla BP controller layer did for its creatures.
	if (!FocusSyncTimerHandle.IsValid())
	{
		GetWorldTimerManager().SetTimer(FocusSyncTimerHandle, this,
			&APFACreatureController::TickFocusSync, 0.25f, /*bLoop=*/true);
	}
}

void APFACreatureController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// The mode CVar lives in PFACreatureBase.cpp's anonymous namespace; look
	// it up by name so neither file has to export it.
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
	FVector ToTarget = Target->GetActorLocation() - ControlledPawn->GetActorLocation();
	ToTarget.Z = 0.0f;
	if (ToTarget.SizeSquared() < 1.0f)
	{
		return;
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

void APFACreatureController::LogCreatureStateHeartbeat()
{
	// Gated: only speaks when diagnostics are on (the timer keeps firing cheaply
	// otherwise). GetCurrentCreatureState/GetAggroTargets/GetCurrentTarget are
	// all inherited public inline getters (FGCreatureController.h:182/216/208);
	// CreatureStateEnumToString is an inline free fn (FGCreature.h:111).
	if (!PFADiag::IsEnabled())
	{
		return;
	}
	const AActor* CurrentTarget = GetCurrentTarget();
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: state heartbeat -> state=%s aggroTargets=%d currentTarget=%s (30s gated diag; stuck-in-CS_Default reveals a state-machine kick is needed)"),
		*GetName(),
		*CreatureStateEnumToString(GetCurrentCreatureState()),
		GetAggroTargets().Num(),
		CurrentTarget ? *CurrentTarget->GetName() : TEXT("none"));

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
		if (BlackboardDump.IsEmpty())
		{
			BlackboardDump = TEXT("<no Object-typed keys>");
		}
	}
	const APawn* ControlledPawn = GetPawn();
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: heartbeat-move -> moveStatus=%d (0=Idle 1=Waiting 2=Paused 3=Moving) velocity=%.0fcm/s bbObjectKeys: %s"),
		*GetName(),
		static_cast<int32>(GetMoveStatus()),
		ControlledPawn ? ControlledPawn->GetVelocity().Size2D() : -1.0f,
		*BlackboardDump);
}
