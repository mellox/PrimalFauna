// ===========================================================================
// PFAPerceptionDebug.cpp -- W1.8 perception diagnostics + force-commands.
//
// A SEPARATE file from PFACheats.cpp because that file is already 611+ lines
// (over the 500-line workspace limit -- a PENDING SPLIT flagged here: the
// DumpCreature/SelfTest/SpawnNest/KillAll commands there should eventually move
// into focused sibling TUs the way this file is one). All commands here are
// plain IConsoleManager registrations (same pattern as PFACheats.cpp): no
// UFUNCTION(exec), so they work identically in editor/PIE and packaged Shipping
// and don't require "cheats enabled".
//
// Commands:
//   PFA.DumpController  -- the DIAGNOSIS. Dumps the perception/state wiring of
//                          the looked-at/nearest creature's controller. Works on
//                          BOTH our APFACreatureController AND a vanilla
//                          BP_CreatureController_C (all reads go through
//                          AFGCreatureController/AAIController public API), so a
//                          vanilla creature's dump is the REFERENCE we diff ours
//                          against.
//   PFA.AggroNearest    -- FORCE aggro on the nearest PrimalFauna creature toward
//                          the player (drives Fight-state entry without needing
//                          the perception path to have fired).
//   PFA.DamageNearest   -- apply a small point-damage to the nearest PrimalFauna
//                          creature with the player as instigator/causer
//                          (exercises retaliation AND the HostileScaling-compatible
//                          damage pipeline).
//
// NOVEL GAME-API SYMBOLS (SHIPPING-EXPORT TRAP, docs/07 §1 -- run
// tools/check_imports.ps1 after packaging, before any game session):
//   AFGCreatureController::SetAggroForTarget  FGCreatureController.h:272
//       FactoryGame UFUNCTION(BlueprintCallable), NON-inline -> a DIRECT call is
//       a novel FactoryGame import. Lower-risk class than the ClearKillOrphanTimer
//       trap (that was a PLAIN non-UFUNCTION method); BlueprintCallable UFUNCTIONs
//       are part of the exported BP surface, but MUST still be import-checked
//       (same stance as PFACheats.cpp's Kill()). ** the one FactoryGame import to
//       verify for this packet. **
//   UGameplayStatics::ApplyPointDamage  GameplayStatics.h:496  ENGINE_API UFUNCTION
//       -- an ENGINE import (not FactoryGame), stable/always-present; out of scope
//       for the FactoryGame-only import check, listed for completeness.
//   Everything else read here is inline/getter (GetPerceptionSettings:370,
//   GetAggroFightThreshold:396, GetCurrentCreatureState:182, GetAggroTargets:216,
//   GetCurrentTarget:208, GetAIPerceptionComponent AIController.h:441) or engine
//   AIMODULE_API (GetSensesConfigIterator/GetDominantSense/GetListenerId) -- no
//   novel FactoryGame direct imports.
// ===========================================================================

#include "PFACheats.h"
#include "PrimalFauna.h"
#include "PFACreatureBase.h"
#include "PFACreatureController.h"

#include "Creature/FGCreature.h"
#include "AI/FGCreatureController.h"
#include "FGHealthComponent.h"

#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "Engine/HitResult.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// Nearest OUR-creature (APFACreatureBase) to a point. Distinct from
	// PFA_FindLookedAtOrNearestCreature (any AFGCreature, look-cone based): the
	// force-commands act on our own creatures specifically, and target the
	// nearest one to the player regardless of look direction.
	APFACreatureBase* FindNearestPFACreature(UWorld* World, const FVector& Origin, float& OutDistCm)
	{
		APFACreatureBase* Best = nullptr;
		float BestSq = FLT_MAX;
		for (TActorIterator<APFACreatureBase> It(World); It; ++It)
		{
			APFACreatureBase* Creature = *It;
			if (!Creature)
			{
				continue;
			}
			const float Sq = FVector::DistSquared(Origin, Creature->GetActorLocation());
			if (Sq < BestSq)
			{
				BestSq = Sq;
				Best = Creature;
			}
		}
		OutDistCm = Best ? FMath::Sqrt(BestSq) : 0.0f;
		return Best;
	}

	// Reads back the player's current aggro entry from a controller's aggro list
	// (GetAggroTargets is inline -- avoids the non-inline GetAggroForTarget as a
	// second novel symbol). Returns -1 if the player isn't an aggro target yet.
	float ReadbackAggroFor(const AFGCreatureController* Controller, const AActor* Target)
	{
		for (const FFGCreatureAggroData& Aggro : Controller->GetAggroTargets())
		{
			if (Aggro.Target == Target)
			{
				return Aggro.Aggro;
			}
		}
		return -1.0f;
	}

	// -------------------------------------------------------------------------
	// PFA.DumpController -- the diagnosis (see file header).
	// -------------------------------------------------------------------------
	void DumpControllerCommand(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] DumpController aborted: no world context."));
			return;
		}

		float DistanceCm = 0.0f;
		bool bFromLook = false;
		AFGCreature* Creature = PFA_FindLookedAtOrNearestCreature(World, DistanceCm, bFromLook);
		if (!Creature)
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] DumpController: no creature found (nothing in the look direction, none within 50m fallback)."));
			return;
		}

		AController* RawController = Creature->GetController();
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] DumpController: %s controller=%s (%.0fcm away, %s)"),
			*Creature->GetName(),
			RawController ? *RawController->GetClass()->GetName() : TEXT("unpossessed"),
			DistanceCm, bFromLook ? TEXT("look direction") : TEXT("nearest 50m fallback"));

		// Creature-side authored perception settings -- works on OURS and VANILLA
		// (GetPerceptionSettings is inline on AFGCreature). Reference baseline:
		// vanilla creatures have Sight/Hearing authored here too.
		const FFGCreaturePerceptionSettings& Settings = Creature->GetPerceptionSettings();
		// w18-12: curve presence -- NULL curves on a C++-only CDO were the
		// sight-aggro root cause (aggro/sec multiplies with the curve); this
		// line also baselines vanilla creatures (both should read present).
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA]   aggroCurves: visibilityGain=%s aggroGain=%s (null = sight-aggro cannot accrue)"),
			Creature->GetVisibilityGainCurve() ? TEXT("present") : TEXT("NULL"),
			Creature->GetAggroGainRateCurve() ? TEXT("present") : TEXT("NULL"));
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA]   Creature.mPerceptionSettings: Sight=%s Hearing=%s ImmediateVisibilityRadius=%.0f"),
			Settings.SightConfig ? TEXT("present") : TEXT("null"),
			Settings.HearingConfig ? TEXT("present") : TEXT("null"),
			Settings.ImmediateVisibilityRadius);

		AFGCreatureController* Controller = Cast<AFGCreatureController>(RawController);
		if (!Controller)
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA]   (controller is null or not an AFGCreatureController -- no perception component / state to dump)"));
			return;
		}

		// Controller-side perception component -- the thing that must have the
		// creature's senses CONFIGURED for any stimulus to fire. This is the
		// smoking-gun line: pre-W1.8 ours = 0 senses; vanilla = 2; post-W1.8
		// ours should also read 2.
		const UAIPerceptionComponent* Perception = Controller->GetAIPerceptionComponent();
		if (!Perception)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA]   AIPerceptionComponent: NULL -- this controller has no perception component at all!"));
		}
		else
		{
			int32 SenseCount = 0;
			FString SenseList;
			for (auto It = Perception->GetSensesConfigIterator(); It; ++It)
			{
				const UAISenseConfig* Cfg = *It;
				if (!Cfg)
				{
					continue;
				}
				if (SenseCount > 0)
				{
					SenseList += TEXT(", ");
				}
				SenseList += Cfg->GetClass()->GetName();
				++SenseCount;
			}

			const UClass* DominantSenseClass = Perception->GetDominantSense().Get();
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA]   AIPerceptionComponent=%s configuredSenses=%d [%s]"),
				*Perception->GetClass()->GetName(), SenseCount,
				SenseCount > 0 ? *SenseList : TEXT("none"));
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA]   dominantSense=%s stimuliListenerRegistered=%s onTargetPerceptionUpdated.bound=%s"),
				DominantSenseClass ? *DominantSenseClass->GetName() : TEXT("none"),
				Perception->GetListenerId().IsValid() ? TEXT("true") : TEXT("false"),
				Perception->OnTargetPerceptionUpdated.IsBound() ? TEXT("true") : TEXT("false"));
		}

		// State machine + aggro (all inline public on AFGCreatureController).
		// w48-2: DESIRED state alongside CURRENT, plus whether CS_Roam is even
		// enabled on this creature. The w48-1 session proved a creature parks in
		// CS_Alert with aggroTargets=0 / currentTarget=none after a successful
		// disengage; this dump previously could not distinguish "the machine wants
		// alert" from "the machine wants roam but the transition is not applied",
		// which are the two branches with opposite fixes.
		// Both work on VANILLA creatures too (Controller is typed
		// AFGCreatureController here), so a hog dump is the baseline for what the
		// desired/current pair looks like on a working state machine.
		// EXPORTS: BOTH are real FactoryGame imports in the built DLL, and both are
		// exported by the shipping game (dumpbin ordinals 18422 and 23664), verified
		// before these lines were written and re-proven by check_imports.ps1 every
		// build. Note GetDesiredCreatureState is virtual and STILL imports -- it is a
		// base virtual we never override, so our vtable already carried it; the
		// "virtual => never imports" shorthand is wrong for that shape. Full
		// reasoning at ForceRoamAfterDisengage in PFACreatureController.cpp.
		const AActor* CurrentTarget = Controller->GetCurrentTarget();
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA]   creatureState=%s desiredState=%s aggroTargets=%d currentTarget=%s roamStateEnabled=%s"),
			*CreatureStateEnumToString(Controller->GetCurrentCreatureState()),
			*CreatureStateEnumToString(Controller->GetDesiredCreatureState()),
			Controller->GetAggroTargets().Num(),
			CurrentTarget ? *CurrentTarget->GetName() : TEXT("none"),
			Creature->IsCreatureStateEnabled(ECreatureState::CS_Roam) ? TEXT("true") : TEXT("false"));

		// w18-3 (invisible-spawn diagnosis): position + ground probe + mesh
		// render state in ONE dump, so the log alone distinguishes "buried
		// under terrain" / "mesh not rendering" / "displaced" without needing
		// eyes on the creature. The probe starts just above the creature's
		// head (NOT sky-high) so a bridge/canopy overhead can't masquerade as
		// ground; burial deeper than capsule+50cm reads as ground=NONE.
		const FVector CreatureLoc = Creature->GetActorLocation();
		float CapsuleHalfCm = 90.0f;
		if (const UCapsuleComponent* Capsule = Creature->GetCapsuleComponent())
		{
			CapsuleHalfCm = Capsule->GetScaledCapsuleHalfHeight();
		}
		const float FeetZ = CreatureLoc.Z - CapsuleHalfCm;
		const APlayerController* ProbePC = World->GetFirstPlayerController();
		const APawn* ProbePlayerPawn = ProbePC ? ProbePC->GetPawn() : nullptr;

		FHitResult GroundHit;
		FCollisionQueryParams ProbeParams(FName(TEXT("PFADumpGroundProbe")), false, Creature);
		const FVector ProbeStart(CreatureLoc.X, CreatureLoc.Y, CreatureLoc.Z + CapsuleHalfCm + 50.0f);
		const FVector ProbeEnd(CreatureLoc.X, CreatureLoc.Y, CreatureLoc.Z - 3000.0f);
		const bool bGroundHit = World->LineTraceSingleByChannel(GroundHit, ProbeStart, ProbeEnd, ECC_Visibility, ProbeParams);
		const float SunkenCm = bGroundHit ? (GroundHit.ImpactPoint.Z - FeetZ) : 0.0f;
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA]   loc=%s zVsPlayer=%+.0fcm ground=%s sunken=%+.0fcm (positive = feet below ground)"),
			*CreatureLoc.ToCompactString(),
			ProbePlayerPawn ? (CreatureLoc.Z - ProbePlayerPawn->GetActorLocation().Z) : 0.0f,
			bGroundHit ? TEXT("hit") : TEXT("NONE-in-probe(deep-buried-or-void)"),
			SunkenCm);

		if (const USkeletalMeshComponent* MeshComp = Creature->GetMesh())
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA]   mesh=%s registered=%s visible=%s actorHidden=%s recentlyRendered(5s)=%s boundsRadius=%.0fcm"),
				MeshComp->GetSkinnedAsset() ? *MeshComp->GetSkinnedAsset()->GetName() : TEXT("NONE"),
				MeshComp->IsRegistered() ? TEXT("true") : TEXT("false"),
				MeshComp->IsVisible() ? TEXT("true") : TEXT("false"),
				Creature->IsHidden() ? TEXT("true") : TEXT("false"),
				Creature->WasRecentlyRendered(5.0f) ? TEXT("true") : TEXT("false"),
				MeshComp->Bounds.SphereRadius);
		}
		else
		{
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   mesh=<no USkeletalMeshComponent>"));
		}
	}

	// -------------------------------------------------------------------------
	// PFA.AggroNearest -- force aggro on the nearest PrimalFauna creature toward
	// the player. Server-only.
	// -------------------------------------------------------------------------
	void AggroNearestCommand(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] PFA.AggroNearest aborted: no world context."));
			return;
		}
		if (World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] PFA.AggroNearest aborted: network client has no authority. Run from the host/server."));
			return;
		}

		APlayerController* PC = World->GetFirstPlayerController();
		APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
		if (!PlayerPawn)
		{
			UE_LOG(LogPrimalFauna, Error, TEXT("[PFA] PFA.AggroNearest FAIL: no local player pawn to aggro toward."));
			return;
		}

		float DistCm = 0.0f;
		APFACreatureBase* Nearest = FindNearestPFACreature(World, PlayerPawn->GetActorLocation(), DistCm);
		if (!Nearest)
		{
			UE_LOG(LogPrimalFauna, Error, TEXT("[PFA] PFA.AggroNearest FAIL: no live APFACreatureBase in this world."));
			return;
		}

		// W2.5: must be OUR controller -- ForceAggro needs the Friend-granted
		// native registration (the raw SetAggroForTarget path proved inert on
		// unregistered targets: session-15 Readback=-1.0).
		APFACreatureController* Controller = Cast<APFACreatureController>(Nearest->GetController());
		if (!Controller)
		{
			UE_LOG(LogPrimalFauna, Error,
				TEXT("[PFA] PFA.AggroNearest FAIL: nearest creature %s is unpossessed or not APFACreatureController (%s)."),
				*Nearest->GetName(),
				Nearest->GetController() ? *Nearest->GetController()->GetClass()->GetName() : TEXT("none"));
			return;
		}

		// docs/02 §4 pack-assist threshold semantics: aggro >= mAggroFightThreshold
		// transitions to Fight. +10 guarantees we clear it. GetAggroFightThreshold
		// is inline on AFGCreature (FGCreature.h:396).
		const float Threshold = Nearest->GetAggroFightThreshold();
		const float NewAggro = Threshold + 10.0f;
		const bool bForced = Controller->ForceAggro(PlayerPawn, NewAggro);

		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] PFA.AggroNearest: %s (%.0fcm) ForceAggro(%s, %.1f) forced=%s (fightThreshold=%.1f +10). Readback=%.1f state now=%s"),
			*Nearest->GetName(), DistCm, *PlayerPawn->GetName(), NewAggro,
			bForced ? TEXT("true") : TEXT("false"), Threshold,
			ReadbackAggroFor(Controller, PlayerPawn),
			*CreatureStateEnumToString(Controller->GetCurrentCreatureState()));
	}

	// -------------------------------------------------------------------------
	// PFA.DamageNearest -- 5 point-damage to the nearest PrimalFauna creature,
	// player as instigator + causer. Exercises retaliation (native OnPawnTakeDamage
	// -> aggressor registration) AND the standard damage pipeline (HostileScaling
	// hooks TakePointDamage). Server-only.
	// -------------------------------------------------------------------------
	void DamageNearestCommand(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] PFA.DamageNearest aborted: no world context."));
			return;
		}
		if (World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] PFA.DamageNearest aborted: network client has no authority. Run from the host/server."));
			return;
		}

		APlayerController* PC = World->GetFirstPlayerController();
		APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
		if (!PlayerPawn)
		{
			UE_LOG(LogPrimalFauna, Error, TEXT("[PFA] PFA.DamageNearest FAIL: no local player pawn to be the instigator/causer."));
			return;
		}

		float DistCm = 0.0f;
		APFACreatureBase* Nearest = FindNearestPFACreature(World, PlayerPawn->GetActorLocation(), DistCm);
		if (!Nearest)
		{
			UE_LOG(LogPrimalFauna, Error, TEXT("[PFA] PFA.DamageNearest FAIL: no live APFACreatureBase in this world."));
			return;
		}

		const UFGHealthComponent* Health = Nearest->GetHealthComponent();
		const float HealthBefore = Health ? Health->GetCurrentHealth() : -1.0f;

		const FVector Dir = (Nearest->GetActorLocation() - PlayerPawn->GetActorLocation()).GetSafeNormal();
		FHitResult Hit;
		Hit.Location = Nearest->GetActorLocation();
		Hit.ImpactPoint = Nearest->GetActorLocation();
		Hit.ImpactNormal = -Dir;

		const float Applied = UGameplayStatics::ApplyPointDamage(
			Nearest, 5.0f, Dir, Hit, PC, PlayerPawn, UDamageType::StaticClass());

		const float HealthAfter = Health ? Health->GetCurrentHealth() : -1.0f;
		AFGCreatureController* Controller = Cast<AFGCreatureController>(Nearest->GetController());
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] PFA.DamageNearest: %s (%.0fcm) point-damage 5 by %s -> ApplyPointDamage returned %.1f, health %.1f->%.1f. Player aggroReadback=%.1f state=%s (retaliation should register the player as an aggressor)."),
			*Nearest->GetName(), DistCm, *PlayerPawn->GetName(), Applied, HealthBefore, HealthAfter,
			Controller ? ReadbackAggroFor(Controller, PlayerPawn) : -1.0f,
			Controller ? *CreatureStateEnumToString(Controller->GetCurrentCreatureState()) : TEXT("no-controller"));
	}

	// ---- Console registrations ----
	FAutoConsoleCommandWithWorldAndArgs CCmdPFADumpController(
		TEXT("PFA.DumpController"),
		TEXT("Primal Fauna: dumps the perception component + senses + state of the controller of the looked-at/nearest creature. Works on ours AND vanilla (diff them)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			DumpControllerCommand(World);
		}));

	FAutoConsoleCommandWithWorldAndArgs CCmdPFAAggroNearest(
		TEXT("PFA.AggroNearest"),
		TEXT("Primal Fauna: forces the nearest PrimalFauna creature to aggro on the player (fightThreshold+10). Drives Fight-state entry without perception. Server-side only."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			AggroNearestCommand(World);
		}));

	FAutoConsoleCommandWithWorldAndArgs CCmdPFADamageNearest(
		TEXT("PFA.DamageNearest"),
		TEXT("Primal Fauna: applies 5 point-damage to the nearest PrimalFauna creature with the player as instigator/causer. Exercises retaliation + the damage pipeline. Server-side only."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			DamageNearestCommand(World);
		}));
}

// ---- Self-test: ControllerPerceptionConfigured (entity-free CDO check). The
// full LIVE-instance check is PFA.DumpController in the micro-test; this proves
// the core W1.8 assumption at world load WITHOUT a creature -- that the
// APFACreatureController CDO inherits a perception component from the base ctor
// (so WireCreaturePerception's GetAIPerceptionComponent() will be non-null). A
// FAIL here means the base does NOT create the component in its ctor -> the
// ctor-creation fallback documented in WireCreaturePerception is needed. ----
namespace
{
	const bool bRegistered_ControllerPerceptionConfigured = []
	{
		RegisterPFASelfTest({TEXT("ControllerPerceptionConfigured"),
			[](UWorld* /*World*/, FString& OutDetail) -> bool
			{
				const APFACreatureController* Cdo = GetDefault<APFACreatureController>();
				const UAIPerceptionComponent* Perception = Cdo ? Cdo->GetAIPerceptionComponent() : nullptr;
				OutDetail = FString::Printf(TEXT("CDO=%s AIPerceptionComponent(CDO)=%s (live per-instance check via PFA.DumpController)"),
					Cdo ? TEXT("present") : TEXT("null"),
					Perception ? *Perception->GetClass()->GetName() : TEXT("null"));
				return Cdo != nullptr && Perception != nullptr;
			}});
		return true;
	}();
}
