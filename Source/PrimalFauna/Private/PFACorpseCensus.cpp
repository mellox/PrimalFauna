// ===========================================================================
// PFACorpseCensus.cpp -- w58-1. See PFACorpseCensus.h for why this exists.
//
// LOG GRAMMAR (deliberate, per the workspace log rules):
//   * The legend line contains NO "name value" pair written in the form the
//     fields themselves print, so counting a field in a log can anchor on the
//     field's own syntax without the legend matching too. Every explanation
//     below is prose.
//   * Every number printed is read from THIS pass. Nothing is baked in --
//     including the two corpse-lifetime knobs, which are read back off the
//     live actor rather than re-typed from the constants that set them.
//   * The census states presence and distance. It never states a cause.
// ===========================================================================
#include "PFACorpseCensus.h"

#include "PFACheats.h"
#include "PFACreatureBase.h"
#include "PrimalFauna.h"

#include "FGHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "UObject/WeakObjectPtr.h"

namespace
{
	// Corpses seen dead-and-present in the PREVIOUS pass. Weak, so an entry
	// going stale is itself the measurement ("was here, is not here now") and
	// nothing in this file can keep a corpse actor alive.
	TArray<TWeakObjectPtr<APFACreatureBase>>& PreviousDeadPresent()
	{
		static TArray<TWeakObjectPtr<APFACreatureBase>> Corpses;
		return Corpses;
	}

	// The world the previous pass ran in. A world change (load, travel) must
	// not be reported as a pile of corpses vanishing, so the carry-over set is
	// dropped and the first pass in a new world reports zero gone.
	TWeakObjectPtr<UWorld>& PreviousWorld()
	{
		static TWeakObjectPtr<UWorld> World;
		return World;
	}

	int32& PassCounter()
	{
		static int32 Passes = 0;
		return Passes;
	}

	bool& LegendEmitted()
	{
		static bool bEmitted = false;
		return bEmitted;
	}
}

namespace PFACorpseCensus
{
	void RunPass(UWorld* World, const TCHAR* Trigger, bool bLogEveryCorpse)
	{
		if (!World)
		{
			return;
		}

		TArray<TWeakObjectPtr<APFACreatureBase>>& Previous = PreviousDeadPresent();
		if (PreviousWorld().Get() != World)
		{
			Previous.Reset();
			PreviousWorld() = World;
		}

		const double NowSeconds = static_cast<double>(World->GetTimeSeconds());
		const bool bDetail = bLogEveryCorpse || PFADiag::IsEnabled();

		int32 Alive = 0;
		int32 DeadPresent = 0;
		int32 DeadMeshless = 0;
		int32 DeadWithoutRecordedOrigin = 0;
		int32 DeadFlaggedRagdolled = 0;
		// Sampled off a live corpse, not from the constants that set them: if a
		// CDO edit ever fails to reach an instance, the census says so. Sampled
		// from ONE corpse (the last iterated), so the field name says "sampled".
		float SampledMaxStaySeconds = -1.0f;
		float SampledRemoveCheckSeconds = -1.0f;
		int32 LifetimeKnobDisagreements = 0;

		TArray<TWeakObjectPtr<APFACreatureBase>> CurrentDeadPresent;
		TArray<FString> DetailLines;

		for (TActorIterator<APFACreatureBase> It(World); It; ++It)
		{
			APFACreatureBase* Creature = *It;
			if (!Creature || Creature->IsActorBeingDestroyed())
			{
				continue;
			}

			const UFGHealthComponent* Health = Creature->GetHealthComponent();
			const bool bDead = Health && Health->IsDead();
			if (!bDead)
			{
				++Alive;
				continue;
			}

			++DeadPresent;
			CurrentDeadPresent.Add(Creature);

			// Read the lifetime knobs off a LIVE corpse rather than from the
			// constants that set them: if a CDO edit ever fails to reach an
			// instance, the census says so instead of repeating the intent.
			const float ThisMaxStay = Creature->GetCorpseMaxStayTimeSeconds();
			const float ThisRemoveCheck = Creature->GetCorpseRemoveCheckTimeSeconds();
			if (SampledMaxStaySeconds >= 0.0f
				&& (!FMath::IsNearlyEqual(ThisMaxStay, SampledMaxStaySeconds)
					|| !FMath::IsNearlyEqual(ThisRemoveCheck, SampledRemoveCheckSeconds)))
			{
				++LifetimeKnobDisagreements;
			}
			SampledMaxStaySeconds = ThisMaxStay;
			SampledRemoveCheckSeconds = ThisRemoveCheck;

			const bool bRagdollFlag = Creature->IsRagdolled();
			if (bRagdollFlag)
			{
				++DeadFlaggedRagdolled;
			}

			const FVector ActorLoc = Creature->GetActorLocation();

			FString AgeField = TEXT("ageSeconds=unrecorded");
			FString FromDeathField = TEXT("actorFromDeathCm=unrecorded");
			FString DeathLocField = TEXT("deathLoc=unrecorded");
			if (Creature->HasDeathObservation())
			{
				const FVector DeathLoc = Creature->GetRecordedDeathLocationCm();
				const double DeathTime = Creature->GetRecordedDeathWorldTimeSeconds();
				AgeField = FString::Printf(TEXT("ageSeconds=%.1f"), NowSeconds - DeathTime);
				FromDeathField = FString::Printf(TEXT("actorFromDeathCm=%.0f"),
					FVector::Dist(ActorLoc, DeathLoc));
				DeathLocField = FString::Printf(TEXT("deathLoc=(%.0f,%.0f,%.0f)"),
					DeathLoc.X, DeathLoc.Y, DeathLoc.Z);
			}
			else
			{
				++DeadWithoutRecordedOrigin;
			}

			FString MeshFromActorField = TEXT("meshFromActorCm=meshless");
			FString MeshVsCapsuleField = TEXT("meshBottomVsCapsuleBottomCm=meshless");
			const USkeletalMeshComponent* MeshComponent = Creature->GetMesh();
			if (MeshComponent && MeshComponent->GetSkinnedAsset())
			{
				const FBoxSphereBounds& WorldBounds = MeshComponent->Bounds;
				MeshFromActorField = FString::Printf(TEXT("meshFromActorCm=%.0f"),
					FVector::Dist(WorldBounds.Origin, ActorLoc));

				float CapsuleHalfCm = 88.0f;
				if (const UCapsuleComponent* Capsule = Creature->GetCapsuleComponent())
				{
					CapsuleHalfCm = Capsule->GetScaledCapsuleHalfHeight();
				}
				const float MeshBottomZ = WorldBounds.Origin.Z - WorldBounds.BoxExtent.Z;
				const float CapsuleBottomZ = ActorLoc.Z - CapsuleHalfCm;
				MeshVsCapsuleField = FString::Printf(TEXT("meshBottomVsCapsuleBottomCm=%+.0f"),
					MeshBottomZ - CapsuleBottomZ);
			}
			else
			{
				++DeadMeshless;
			}

			if (bDetail)
			{
				DetailLines.Add(FString::Printf(
					TEXT("[PFA] CorpseCensus corpse=%s %s ragdollFlag=%d %s %s %s actorLoc=(%.0f,%.0f,%.0f) %s"),
					*Creature->GetName(), *AgeField, bRagdollFlag ? 1 : 0,
					*FromDeathField, *MeshFromActorField, *MeshVsCapsuleField,
					ActorLoc.X, ActorLoc.Y, ActorLoc.Z, *DeathLocField));
			}
		}

		// "Gone" is a set difference over actor identity and nothing more:
		// present-and-dead last pass, not present now.
		int32 GoneSincePrev = 0;
		for (const TWeakObjectPtr<APFACreatureBase>& Was : Previous)
		{
			if (!CurrentDeadPresent.Contains(Was))
			{
				++GoneSincePrev;
			}
		}

		const int32 Pass = ++PassCounter();

		if (!LegendEmitted())
		{
			LegendEmitted() = true;
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] CorpseCensus legend: one pass iterates every PrimalFauna creature actor in the world. ")
				TEXT("A creature counts as a corpse when its health component reports dead. ")
				TEXT("The gone count is a set difference between this pass and the previous one -- ")
				TEXT("dead and present before, not present now -- and nothing here tests WHY an actor is absent, ")
				TEXT("so it covers destruction, streaming and level unload alike. ")
				TEXT("Distances are centimetres. The mesh-versus-capsule figure is signed, negative meaning the ")
				TEXT("mesh bounds bottom sits below the capsule bottom. Corpse ages and origins are only available ")
				TEXT("for creatures that died while this build was running. ")
				TEXT("The two sampled lifetime figures are read off one corpse that this pass actually saw, ")
				TEXT("so they read none when no corpse was present, and the disagreement counter is how many ")
				TEXT("other corpses this pass reported different values."));
		}

		FString LifetimeFields = TEXT("sampledMaxStaySeconds=none sampledRemoveCheckSeconds=none sampledLifetimeDisagreements=0");
		if (DeadPresent > 0)
		{
			LifetimeFields = FString::Printf(
				TEXT("sampledMaxStaySeconds=%.1f sampledRemoveCheckSeconds=%.1f sampledLifetimeDisagreements=%d"),
				SampledMaxStaySeconds, SampledRemoveCheckSeconds, LifetimeKnobDisagreements);
		}
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] CorpseCensus pass=%d trigger=%s alive=%d deadPresent=%d goneSincePrev=%d ")
			TEXT("deadFlaggedRagdolled=%d deadMeshless=%d deadWithoutRecordedOrigin=%d %s detail=%d"),
			Pass, Trigger, Alive, DeadPresent, GoneSincePrev,
			DeadFlaggedRagdolled, DeadMeshless, DeadWithoutRecordedOrigin,
			*LifetimeFields, bDetail ? 1 : 0);

		for (const FString& Line : DetailLines)
		{
			UE_LOG(LogPrimalFauna, Display, TEXT("%s"), *Line);
		}

		Previous = MoveTemp(CurrentDeadPresent);
	}
}

namespace
{
	// PFA.CorpseCensus -- an on-demand pass with full per-corpse detail, so a
	// human or the automated tester can sample a corpse at a chosen moment
	// (right after a kill, then again 30s later) instead of waiting for the
	// 10s heartbeat to line up with what they want to see.
	FAutoConsoleCommandWithWorld CorpseCensusCommand(
		TEXT("PFA.CorpseCensus"),
		TEXT("Log one PrimalFauna corpse census pass with a line per corpse."),
		FConsoleCommandWithWorldDelegate::CreateLambda(
			[](UWorld* World)
			{
				if (!World)
				{
					UE_LOG(LogPrimalFauna, Warning,
						TEXT("[PFA] PFA.CorpseCensus aborted: no world context."));
					return;
				}
				PFACorpseCensus::RunPass(World, TEXT("console"), /*bLogEveryCorpse=*/true);
			}));
}
