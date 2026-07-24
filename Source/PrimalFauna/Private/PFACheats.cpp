#include "PFACheats.h"
#include "PrimalFauna.h"
#include "PFAConfig.h"
#include "PFASubsystem.h"
#include "PFACreatureBase.h"

#include "Creature/FGCreature.h"
#include "Creature/FGCreatureSpawner.h"
#include "FGHealthComponent.h"
#include "BehaviorTree/BehaviorTree.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"

// ---- Diagnostics gate (see PFACheats.h for why Display+manual-gate, not Verbose) ----
namespace
{
	int32 GPFADiagCVarValue = 0;
	int32 GPFAAutoTestCVarValue = 0;
	bool GPFAConfigDiagnosticsEnabled = false; // pushed by APFASubsystem::RefreshTick

	FAutoConsoleVariableRef CVarPFADiag(
		TEXT("pfa.Diag"), GPFADiagCVarValue,
		TEXT("Primal Fauna: 1 = force verbose [PFA] diagnostics on, OR'd with 'Enable Diagnostics'. 0 (default) defers to config."),
		ECVF_Default);

	FAutoConsoleVariableRef CVarPFAAutoTest(
		TEXT("pfa.AutoTest"), GPFAAutoTestCVarValue,
		TEXT("Primal Fauna: 1 = auto-run PFA.SelfTest once on the subsystem's first refresh tick. 0 (default) = explicit only."),
		ECVF_Default);
}

namespace PFADiag
{
	bool IsEnabled()
	{
		return (GPFADiagCVarValue != 0) || GPFAConfigDiagnosticsEnabled;
	}

	void SetConfigDiagnostics(bool bEnabled)
	{
		GPFAConfigDiagnosticsEnabled = bEnabled;
	}

	bool IsAutoTestRequested()
	{
		return GPFAAutoTestCVarValue != 0;
	}
}

// ---- Self-test registry ----
namespace
{
	TArray<FPFASelfTest>& GetPFASelfTestRegistry()
	{
		// Meyer's singleton: safe fill order across TUs regardless of static-init order.
		static TArray<FPFASelfTest> Registry;
		return Registry;
	}
}

void RegisterPFASelfTest(const FPFASelfTest& Test)
{
	GetPFASelfTestRegistry().Add(Test);
}

void PFA_RunSelfTests(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogPrimalFauna, Error, TEXT("[PFA][TEST] SelfTest aborted: no world context."));
		return;
	}

	// World-level equivalent of AActor::HasAuthority(): NM_Client means we are
	// connected to someone else's server, i.e. no authority (server-side only).
	if (World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA][TEST] SelfTest aborted: this is a network client with no authority. ")
			TEXT("Run PFA.SelfTest from the host/server."));
		return;
	}

	const TArray<FPFASelfTest>& Registry = GetPFASelfTestRegistry();
	int32 PassCount = 0;
	for (const FPFASelfTest& Test : Registry)
	{
		FString Detail;
		bool bPass = false;
		if (Test.Run)
		{
			bPass = Test.Run(World, Detail);
		}
		else
		{
			Detail = TEXT("test has no Run function bound");
		}

		if (bPass) { ++PassCount; }

		// Contract grammar -- do not reformat (docs/08 §1: the sf-game-tester
		// agent greps for this instead of playing manually).
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA][TEST] %s: %s %s"),
			*Test.Name.ToString(), bPass ? TEXT("PASS") : TEXT("FAIL"), *Detail);
	}

	UE_LOG(LogPrimalFauna, Display, TEXT("[PFA][TEST] SUMMARY: %d/%d PASS"), PassCount, Registry.Num());
}

// ---- PFA.DumpCreature / PFA.DumpAllCreatures helpers ----
namespace
{
	// ~25.8 degree half-angle look cone (tunable; not specified by the packet).
	constexpr float LookConeCosineThreshold = 0.90f;
	constexpr float NearestFallbackRadiusCm = 5000.0f; // 50m, per the packet spec

	// Nearest creature to the look direction (no distance cap); falls back to
	// the nearest within 50m if the cone finds nothing. Null if neither hits.
	AFGCreature* FindLookedAtOrNearestCreature(UWorld* World, float& OutDistanceCm, bool& bOutFromLookDirection)
	{
		OutDistanceCm = 0.0f;
		bOutFromLookDirection = false;

		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			return nullptr;
		}

		FVector EyeLocation;
		FRotator EyeRotation;
		PC->GetPlayerViewPoint(EyeLocation, EyeRotation);
		const FVector LookForward = EyeRotation.Vector();

		AFGCreature* BestInCone = nullptr;
		float BestDot = LookConeCosineThreshold;
		float BestInConeDist = 0.0f;

		AFGCreature* BestOverall = nullptr;
		float BestOverallDistSq = FMath::Square(NearestFallbackRadiusCm);

		for (TActorIterator<AFGCreature> It(World); It; ++It)
		{
			AFGCreature* Creature = *It;
			if (!Creature)
			{
				continue;
			}

			const FVector ToCreature = Creature->GetActorLocation() - EyeLocation;
			const float DistSq = ToCreature.SizeSquared();
			if (DistSq < KINDA_SMALL_NUMBER)
			{
				continue; // degenerate (standing inside it)
			}
			const float Dist = FMath::Sqrt(DistSq);

			const float Dot = FVector::DotProduct(LookForward, ToCreature / Dist);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				BestInCone = Creature;
				BestInConeDist = Dist;
			}
			if (DistSq < BestOverallDistSq)
			{
				BestOverallDistSq = DistSq;
				BestOverall = Creature;
			}
		}

		if (BestInCone)
		{
			OutDistanceCm = BestInConeDist;
			bOutFromLookDirection = true;
			return BestInCone;
		}
		if (BestOverall)
		{
			OutDistanceCm = FMath::Sqrt(BestOverallDistSq);
			bOutFromLookDirection = false;
			return BestOverall;
		}
		return nullptr;
	}

	// Every numeric/bool UPROPERTY on Action via read-only reflection -- no
	// AccessTransformer, no per-action-subclass knowledge needed. Caller gates
	// this behind PFADiag::IsEnabled() -- it is real per-entity detail.
	void LogActionReflection(const UFGAction* Action)
	{
		for (TFieldIterator<FProperty> PropIt(Action->GetClass()); PropIt; ++PropIt)
		{
			const FProperty* Prop = *PropIt;
			FString ValueStr;
			if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
			{
				ValueStr = FString::Printf(TEXT("%.3f"), FloatProp->GetPropertyValue_InContainer(Action));
			}
			else if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
			{
				ValueStr = FString::Printf(TEXT("%.3f"), DoubleProp->GetPropertyValue_InContainer(Action));
			}
			else if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop))
			{
				ValueStr = FString::Printf(TEXT("%d"), IntProp->GetPropertyValue_InContainer(Action));
			}
			else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
			{
				ValueStr = FString::Printf(TEXT("%d"), ByteProp->GetPropertyValue_InContainer(Action));
			}
			else if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				ValueStr = BoolProp->GetPropertyValue_InContainer(Action) ? TEXT("true") : TEXT("false");
			}
			else
			{
				continue; // skip object refs / arrays / structs -- "key numeric UPROPERTYs" only
			}
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]       %s = %s"), *Prop->GetName(), *ValueStr);
		}
	}

	void DumpCreatureCommand(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] DumpCreature aborted: no world context."));
			return;
		}

		float DistanceCm = 0.0f;
		bool bFromLookDirection = false;
		AFGCreature* Creature = FindLookedAtOrNearestCreature(World, DistanceCm, bFromLookDirection);
		if (!Creature)
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] DumpCreature: no creature found (nothing in the look direction, none within %.0fm fallback)."),
				NearestFallbackRadiusCm / 100.0f);
			return;
		}

		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] DumpCreature: %s (%.0fcm away, %s)"),
			*Creature->GetName(), DistanceCm, bFromLookDirection ? TEXT("look direction") : TEXT("nearest 50m fallback"));
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   Class=%s"), *Creature->GetClass()->GetPathName());
		if (const UBehaviorTree* BT = Creature->GetBehaviorTreeAsset())
		{
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   BehaviorTree=%s"), *BT->GetPathName());
		}
		else
		{
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   BehaviorTree=none"));
		}
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   AIControllerClass(configured)=%s AIController(live)=%s"),
			Creature->AIControllerClass ? *Creature->AIControllerClass->GetName() : TEXT("none"),
			Creature->GetController() ? *Creature->GetController()->GetClass()->GetName() : TEXT("unpossessed"));
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   IsPassiveCreature=%s IsTamed=%s"),
			Creature->IsPassiveCreature() ? TEXT("true") : TEXT("false"),
			Creature->IsTamed() ? TEXT("true") : TEXT("false"));

		if (const UFGHealthComponent* Health = Creature->GetHealthComponent())
		{
			float CdoMaxHealth = -1.0f;
			if (const AFGCreature* Cdo = Creature->GetClass()->GetDefaultObject<AFGCreature>())
			{
				if (const UFGHealthComponent* CdoHealth = Cdo->GetHealthComponent())
				{
					CdoMaxHealth = CdoHealth->GetMaxHealth();
				}
			}
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   Health=%.1f/%.1f (CDO anchor max=%.1f)"),
				Health->GetCurrentHealth(), Health->GetMaxHealth(), CdoMaxHealth);
		}
		else
		{
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   Health=no health component"));
		}

		if (AFGCreatureSpawner* Spawner = Creature->GetSpawner())
		{
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   Spawner=%s"), *Spawner->GetPathName());
		}
		else
		{
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   Spawner=none (not spawner-owned)"));
		}

		const TArray<FCreatureAction> Actions = Creature->GetCreatureActionsSetup();
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   CreatureActionsSetup: %d entries%s"),
			Actions.Num(), PFADiag::IsEnabled() ? TEXT("") : TEXT(" (enable pfa.Diag for per-action detail)"));
		if (!PFADiag::IsEnabled())
		{
			return; // per-entity action/property detail is diagnostics-gated
		}
		for (const FCreatureAction& Entry : Actions)
		{
			if (Entry.Action)
			{
				UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]     Action=%s RequiredStates=%d StartsWithCooldown=%s"),
					*Entry.Action->GetClass()->GetName(), Entry.RequiredStates,
					Entry.StartsWithCooldown ? TEXT("true") : TEXT("false"));
				LogActionReflection(Entry.Action);
			}
			else
			{
				UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]     Action=none (unset slot) RequiredStates=%d"), Entry.RequiredStates);
			}
		}
	}

	void DumpAllCreaturesCommand(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] DumpAllCreatures aborted: no world context."));
			return;
		}

		TMap<UClass*, int32> CountsByClass;
		int32 Total = 0;
		for (TActorIterator<AFGCreature> It(World); It; ++It)
		{
			AFGCreature* Creature = *It;
			if (!Creature) { continue; }
			CountsByClass.FindOrAdd(Creature->GetClass())++;
			++Total;
		}

		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] DumpAllCreatures: %d live creature(s) across %d class(es)."),
			Total, CountsByClass.Num());
		for (const TPair<UClass*, int32>& Pair : CountsByClass)
		{
			float CdoMaxHealth = -1.0f;
			if (const AFGCreature* Cdo = Pair.Key ? Pair.Key->GetDefaultObject<AFGCreature>() : nullptr)
			{
				if (const UFGHealthComponent* CdoHealth = Cdo->GetHealthComponent())
				{
					CdoMaxHealth = CdoHealth->GetMaxHealth();
				}
			}
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA]   %s: count=%d CDO-max-health=%.1f"),
				Pair.Key ? *Pair.Key->GetName() : TEXT("<null class>"), Pair.Value, CdoMaxHealth);
		}
	}

	// ---- PFA.SpawnNest (W1.5): thin console-command wrapper. All the real
	// work (ground trace, registry append, immediate reconcile) lives on
	// APFASubsystem::SpawnNestAtPlayer (PFASubsystem.cpp) -- this function
	// only does the standard world/authority guard (matching PFA.SpawnTest's
	// pattern, Species/PFASicklejaw.cpp) and finds the live subsystem
	// instance, the same way the SubsystemAlive self-test does. ----
	void SpawnNestCommand(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] PFA.SpawnNest aborted: no world context."));
			return;
		}
		if (World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] PFA.SpawnNest aborted: this is a network client with no authority. Run PFA.SpawnNest from the host/server."));
			return;
		}

		for (TActorIterator<APFASubsystem> It(World); It; ++It)
		{
			FString Detail;
			const bool bSuccess = It->SpawnNestAtPlayer(Detail);
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] PFA.SpawnNest: %s (%s)"),
				bSuccess ? TEXT("OK") : TEXT("FAIL"), *Detail);
			return;
		}

		UE_LOG(LogPrimalFauna, Error, TEXT("[PFA] PFA.SpawnNest FAIL: no APFASubsystem in this world."));
	}

	// ---- PFA.KillAll (W1.6, docs/08 packet): real body replacing the
	// pre-W1.3 stub. Iterates every live APFACreatureBase (our abstract
	// species base, docs/07 §2 -- covers Sicklejaw and every future species
	// with zero per-species code) and kills it through its OWN health
	// component, matching the standard damage/death pipeline every other
	// creature in the game dies through (workspace guardrail, docs/06:
	// "damage through standard pipeline with causer/instigator set" --
	// Kill() still routes through Died()/DeathDelegate, see FGHealthComponent.h
	// :133's own doc comment "Will trigger DeathDelegate").
	//
	// UFGHealthComponent::Kill() (FGHealthComponent.h:133) -- verified
	// UFUNCTION(BlueprintCallable, Category = "Damage"), NOT the shipping-
	// export-trap risk class (docs/07 §1: that trap was specifically a PLAIN
	// NON-UFUNCTION method, AFGCreature::ClearKillOrphanTimer). Kill()/
	// ResetHealth() being UFUNCTION-reflected is exactly the "prefer UFUNCTION
	// /inline/getter paths for novel calls" rule in practice -- still a NEW
	// symbol for this mod's DLL, so tools/check_imports.ps1 must run on the
	// next package before any game session, same as every other novel call.
	void KillAllCommand(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] PFA.KillAll aborted: no world context."));
			return;
		}
		if (World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] PFA.KillAll aborted: this is a network client with no authority. Run PFA.KillAll from the host/server."));
			return;
		}

		int32 KilledCount = 0;
		int32 AlreadyDeadCount = 0;
		int32 NoHealthComponentCount = 0;

		for (TActorIterator<APFACreatureBase> It(World); It; ++It)
		{
			APFACreatureBase* Creature = *It;
			if (!Creature)
			{
				continue;
			}

			UFGHealthComponent* Health = Creature->GetHealthComponent();
			if (!Health)
			{
				++NoHealthComponentCount;
				UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] PFA.KillAll: %s has no health component, skipping."), *Creature->GetName());
				continue;
			}

			if (Health->IsDead())
			{
				++AlreadyDeadCount;
				continue;
			}

			// Snapshot before Kill() zeroes current health, so the per-kill
			// line shows what was actually killed (diagnostics-first rule:
			// print the values, not just "applied").
			const float HealthBefore = Health->GetCurrentHealth();
			const float MaxHealth = Health->GetMaxHealth();

			Health->Kill();
			++KilledCount;

			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] PFA.KillAll: killed %s (was %.1f/%.1f, IsDead now=%s)."),
				*Creature->GetName(), HealthBefore, MaxHealth, Health->IsDead() ? TEXT("true") : TEXT("false"));
		}

		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] PFA.KillAll: SUMMARY killed=%d alreadyDead=%d noHealthComponent=%d"),
			KilledCount, AlreadyDeadCount, NoHealthComponentCount);
	}
}

// ---- Console commands: plain IConsoleManager registrations (not UFUNCTION(exec)
// cheat-manager commands) -- matches the engine's own CVarCreatureDebug-style
// pattern, works identically in editor/PIE and packaged Shipping, no #if
// !WITH_EDITOR guard needed (additive registrations, not binary hooks), and
// does not require "cheats enabled" the way UFUNCTION(exec) commands can. ----
namespace
{
	FAutoConsoleCommandWithWorldAndArgs CCmdPFASelfTest(
		TEXT("PFA.SelfTest"),
		TEXT("Primal Fauna: runs every registered self-test, logs [PFA][TEST] PASS/FAIL + SUMMARY lines. Server-side only."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			PFA_RunSelfTests(World);
		}));

	FAutoConsoleCommandWithWorldAndArgs CCmdPFADumpCreature(
		TEXT("PFA.DumpCreature"),
		TEXT("Primal Fauna: logs everything about the AFGCreature nearest the local player's look direction (50m fallback)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			DumpCreatureCommand(World);
		}));

	FAutoConsoleCommandWithWorldAndArgs CCmdPFADumpAllCreatures(
		TEXT("PFA.DumpAllCreatures"),
		TEXT("Primal Fauna: census summary of every live AFGCreature -- per-class counts and CDO max-health anchors."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			DumpAllCreaturesCommand(World);
		}));

	FAutoConsoleCommandWithWorldAndArgs CCmdPFASpawnNest(
		TEXT("PFA.SpawnNest"),
		TEXT("Primal Fauna: spawn a territory nest at the player's ground position (region 'Test') and reconcile immediately. Server-side only."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			SpawnNestCommand(World);
		}));

	FAutoConsoleCommandWithWorldAndArgs CCmdPFAWormForceEvent(
		TEXT("PFA.WormForceEvent"),
		TEXT("Primal Fauna: force a Sandwyrm emergence event. NOT YET IMPLEMENTED (arrives in W4)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* /*World*/)
		{
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] PFA.WormForceEvent not available until W4 ships (Sandwyrm director does not exist yet)."));
		}));

	FAutoConsoleCommandWithWorldAndArgs CCmdPFAKillAll(
		TEXT("PFA.KillAll"),
		TEXT("Primal Fauna: kills every live PrimalFauna creature (APFACreatureBase) via its health component's Kill(), logs a per-kill line plus a killed/alreadyDead/noHealthComponent summary. Server-side only."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			KillAllCommand(World);
		}));
}

// ---- W1.1's own self-tests: the framework proving itself. Later packets copy
// this idiom into their OWN .cpp files -- nothing above needs to change. ----
namespace
{
	const bool bRegistered_ConfigLoaded = []
	{
		RegisterPFASelfTest({TEXT("ConfigLoaded"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				const FPFAConfigStruct Config = FPFAConfigStruct::GetActiveConfig(World);
				OutDetail = FString::Printf(TEXT("Enabled=%s NestDensity=%.2f ExperimentalFeatures=%s Diagnostics=%s"),
					Config.Enabled ? TEXT("true") : TEXT("false"),
					Config.NestDensity,
					Config.EnableExperimentalFeatures ? TEXT("true") : TEXT("false"),
					Config.EnableDiagnostics ? TEXT("true") : TEXT("false"));
				// GetActiveConfig() never null/crashes (defaults on lookup
				// failure); the bar is a sane positive density, not zeroed garbage.
				return Config.NestDensity > 0.0f;
			}});
		return true;
	}();

	const bool bRegistered_SubsystemAlive = []
	{
		RegisterPFASelfTest({TEXT("SubsystemAlive"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				for (TActorIterator<APFASubsystem> It(World); It; ++It)
				{
					const int32 Ticks = It->GetTickCount();
					OutDetail = FString::Printf(TEXT("found, TickCount=%d"), Ticks);
					return Ticks > 0;
				}
				OutDetail = TEXT("no APFASubsystem actor found in world");
				return false;
			}});
		return true;
	}();

	const bool bRegistered_DiagGate = []
	{
		RegisterPFASelfTest({TEXT("DiagGate"),
			[](UWorld* /*World*/, FString& OutDetail) -> bool
			{
				IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("pfa.Diag"));
				if (!CVar)
				{
					OutDetail = TEXT("pfa.Diag CVar not registered");
					return false;
				}

				// Round-trip: forcing the CVar to 1 must read true unconditionally
				// (OR-gate); restoring it must return the gate to its prior reading.
				// Proves IsEnabled() reads the live CVar without assuming config state.
				// ECVF_SetByConsole, NOT SetByCode: a prior console `pfa.Diag` set
				// outranks SetByCode and silently ignores it -- the smoke session
				// (2026-07-23) proved that false-FAILs this test after any console
				// touch of the CVar. Console priority can always overwrite itself.
				const bool bBefore = PFADiag::IsEnabled();
				const int32 OriginalValue = CVar->GetInt();

				CVar->Set(1, ECVF_SetByConsole);
				const bool bForcedOn = PFADiag::IsEnabled();

				CVar->Set(OriginalValue, ECVF_SetByConsole);
				const bool bAfterRestore = PFADiag::IsEnabled();

				OutDetail = FString::Printf(TEXT("before=%d forcedOn=%d afterRestore=%d (cvar restored to %d)"),
					bBefore, bForcedOn, bAfterRestore, OriginalValue);
				return bForcedOn && (bAfterRestore == bBefore);
			}});
		return true;
	}();

	const bool bRegistered_CreatureCensus = []
	{
		RegisterPFASelfTest({TEXT("CreatureCensus"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				TSet<UClass*> ClassesSeen;
				int32 Total = 0;
				for (TActorIterator<AFGCreature> It(World); It; ++It)
				{
					if (!*It) { continue; }
					ClassesSeen.Add(It->GetClass());
					++Total;
				}
				OutDetail = (Total == 0)
					? TEXT("0 creatures (expected PASS on a creature-less save/before spawns settle)")
					: FString::Printf(TEXT("%d creatures across %d classes"), Total, ClassesSeen.Num());
				// Bar = the sweep executes at all, not a nonzero count.
				return true;
			}});
		return true;
	}();
}

// ---- Shared finder forwarder (declared in PFACheats.h). Thin wrapper giving
// sibling dev-command TUs (PFAPerceptionDebug.cpp) external-linkage access to
// the anonymous-namespace FindLookedAtOrNearestCreature above, without
// duplicating the ~65-line cone math or moving it out of this file. Anonymous-
// namespace names are visible unqualified through the rest of this TU, so this
// call resolves to the definition near the top of the file. ----
PRIMALFAUNA_API AFGCreature* PFA_FindLookedAtOrNearestCreature(
	UWorld* World, float& OutDistanceCm, bool& bOutFromLookDirection)
{
	return FindLookedAtOrNearestCreature(World, OutDistanceCm, bOutFromLookDirection);
}
