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
	// Always-on, not diag-gated: this is a behaviour change taking effect, and
	// the whole reason the bug survived was that its evidence was invisible.
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: wrote roam band [%.0f..%.0f] to the blackboard (was %.0f max) -- vanilla's BP controller normally does this."),
		*GetName(), Band.Min, Band.Max, BBMax);
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

	// Gated: only SPEAKS when diagnostics are on (the timer keeps firing cheaply
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
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: heartbeat-move -> moveStatus=%d (0=Idle 1=Waiting 2=Paused 3=Moving) velocity=%.0fcm/s bbObjectKeys: %s"),
		*GetName(),
		static_cast<int32>(GetMoveStatus()),
		ControlledPawn ? ControlledPawn->GetVelocity().Size2D() : -1.0f,
		*BlackboardDump);
}
