// ===========================================================================
// PFASpatialRegression.cpp -- w26-1: SPATIAL regression self-tests.
//
// WHY THIS FILE EXISTS (the honest version, 2026-07-24):
// every self-test before this one asserted DATA -- config values, CDO fields,
// registry counts, action setup. Nothing asserted where a creature's body
// actually IS. So when the animation packet changed the live pose, the
// feet-planting that had been verified by eye ONCE (w23-2) silently
// regressed: creatures shipped sunk to the shoulders in the terrain, and the
// user found it, not the harness. Same story for facing -- the melee cone is
// measured from the forward vector, so a creature that never turns is a
// combat bug hiding as a cosmetic one.
//
// These two tests close that class: they run on LIVE creatures, every
// PFA.SelfTest / pfa.AutoTest pass, and they are log-assertable, so the
// automated tester catches a spatial regression without a human eyeball.
//
// CONTRACT for both: with NO live creatures they PASS with an explicit
// "no live creatures" detail -- a self-test must never fail merely because
// the world is empty, but the detail makes an empty run visible rather than
// silently green.
// ===========================================================================
#include "PFACheats.h"
#include "PrimalFauna.h"
#include "PFACreatureBase.h"
#include "PFACreatureController.h"

#include "AI/FGCreatureController.h"
#include "FGHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace
{
	// A creature whose mesh bottom sits within this of the capsule bottom
	// reads as "standing on its feet". Generous enough for a mid-stride pose
	// (a lifted foot raises the bounds bottom) but far tighter than the
	// ~100cm sink the w25-2 animation regression produced.
	constexpr float FeetPlantedToleranceCm = 25.0f;

	// cos(60 deg): the creature is "facing" its target if the target is
	// inside a 120-degree frontal arc. The melee cone is 90 degrees, so
	// anything passing this is at worst a small turn from being able to bite.
	constexpr float FacingCosThreshold = 0.5f;

	// ---- FeetPlanted: the direct regression guard for "sunk into terrain" /
	// "floating". Compares each live creature's animated mesh-bounds bottom
	// against its capsule bottom -- the same quantity the w26-1 foot-plant
	// keeper drives to zero, so this test fails loudly if that keeper is
	// ever disabled, broken, or outrun by a new pose.
	const bool bRegistered_FeetPlanted = []
	{
		RegisterPFASelfTest({TEXT("FeetPlanted"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				if (!World)
				{
					OutDetail = TEXT("no world context");
					return false;
				}

				int32 Checked = 0;
				int32 Meshless = 0;
				float WorstDeltaCm = 0.0f;
				FString WorstName = TEXT("none");

				for (TActorIterator<APFACreatureBase> It(World); It; ++It)
				{
					APFACreatureBase* Creature = *It;
					if (!Creature || Creature->IsActorBeingDestroyed())
					{
						continue;
					}
					const USkeletalMeshComponent* MeshComponent = Creature->GetMesh();
					if (!MeshComponent || !MeshComponent->GetSkinnedAsset())
					{
						++Meshless;
						continue;
					}
					// Corpses are exempt: the foot-plant keeper deliberately
					// stops on death and the held authored collapse is not
					// capsule-relative -- judging it would turn every
					// post-combat self-test red for the wrong reason.
					if (const UFGHealthComponent* Health = Creature->GetHealthComponent())
					{
						if (Health->IsDead())
						{
							continue;
						}
					}
					float CapsuleHalfCm = 88.0f;
					if (const UCapsuleComponent* Capsule = Creature->GetCapsuleComponent())
					{
						CapsuleHalfCm = Capsule->GetScaledCapsuleHalfHeight();
					}
					const FBoxSphereBounds& WorldBounds = MeshComponent->Bounds;
					const float MeshBottomZ = WorldBounds.Origin.Z - WorldBounds.BoxExtent.Z;
					const float CapsuleBottomZ = Creature->GetActorLocation().Z - CapsuleHalfCm;
					// Signed: negative = mesh hangs below the capsule (sunk),
					// positive = mesh floats above it.
					const float DeltaCm = MeshBottomZ - CapsuleBottomZ;

					++Checked;
					if (FMath::Abs(DeltaCm) > FMath::Abs(WorstDeltaCm))
					{
						WorstDeltaCm = DeltaCm;
						WorstName = Creature->GetName();
					}
				}

				if (Checked == 0)
				{
					// NO-COVERAGE is a literal grep token: a run that greps
					// only for PASS cannot tell "guarded" from "nothing was
					// there to guard". One extra grep separates them.
					OutDetail = FString::Printf(
						TEXT("NO-COVERAGE: no live meshed creatures to check (meshless=%d) -- spawn some and re-run"),
						Meshless);
					return true;
				}

				const bool bPass = FMath::Abs(WorstDeltaCm) <= FeetPlantedToleranceCm;
				OutDetail = FString::Printf(
					TEXT("checked=%d meshless=%d worst=%s %+.0fcm vs capsule bottom (tolerance %.0fcm; negative=sunk, positive=floating)"),
					Checked, Meshless, *WorstName, WorstDeltaCm, FeetPlantedToleranceCm);
				return bPass;
			}});
		return true;
	}();

	// ---- w58-1 CORPSE COVERAGE -------------------------------------------
	// FeetPlanted exempts corpses on purpose (the foot-plant keeper stops at
	// death and the held authored collapse is not capsule-relative), and that
	// exemption is exactly why the w57-10 body-despawn regression had no
	// spatial assertion covering it. The right answer is a check written FOR
	// corpses, not a widened exemption, so these two tests judge a corpse by
	// what a corpse can legitimately be held to:
	//
	//   * its mesh must still be attached to its own actor, and
	//   * a corpse does not travel.
	//
	// Both are GROSS-DISPLACEMENT detectors, deliberately loose. They are not
	// pose-quality judges -- a settled corpse in any pose passes -- so a
	// failure means the body is somewhere it cannot legitimately be.
	constexpr float CorpseMeshSeparationToleranceCm = 500.0f;
	constexpr float CorpseDriftToleranceCm = 1500.0f;

	const bool bRegistered_CorpsePlanted = []
	{
		RegisterPFASelfTest({TEXT("CorpsePlanted"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				if (!World)
				{
					OutDetail = TEXT("no world context");
					return false;
				}

				int32 Judged = 0;
				int32 Meshless = 0;
				int32 WithoutRecordedOrigin = 0;
				float WorstMeshSeparationCm = 0.0f;
				FString WorstMeshName = TEXT("none");
				float WorstDriftCm = 0.0f;
				FString WorstDriftName = TEXT("none");

				for (TActorIterator<APFACreatureBase> It(World); It; ++It)
				{
					APFACreatureBase* Creature = *It;
					if (!Creature || Creature->IsActorBeingDestroyed())
					{
						continue;
					}
					const UFGHealthComponent* Health = Creature->GetHealthComponent();
					if (!Health || !Health->IsDead())
					{
						continue;
					}
					const USkeletalMeshComponent* MeshComponent = Creature->GetMesh();
					if (!MeshComponent || !MeshComponent->GetSkinnedAsset())
					{
						++Meshless;
						continue;
					}

					const FVector ActorLoc = Creature->GetActorLocation();
					const float MeshSeparationCm =
						FVector::Dist(MeshComponent->Bounds.Origin, ActorLoc);
					++Judged;
					if (MeshSeparationCm > WorstMeshSeparationCm)
					{
						WorstMeshSeparationCm = MeshSeparationCm;
						WorstMeshName = Creature->GetName();
					}

					// Only corpses that died during THIS session carry an
					// origin; one loaded from a save legitimately has none and
					// is counted rather than judged.
					if (!Creature->HasDeathObservation())
					{
						++WithoutRecordedOrigin;
						continue;
					}
					const float DriftCm =
						FVector::Dist(ActorLoc, Creature->GetRecordedDeathLocationCm());
					if (DriftCm > WorstDriftCm)
					{
						WorstDriftCm = DriftCm;
						WorstDriftName = Creature->GetName();
					}
				}

				if (Judged == 0)
				{
					OutDetail = FString::Printf(
						TEXT("NO-COVERAGE: no meshed corpse present to check (meshless corpses %d) -- kill something and re-run"),
						Meshless);
					return true;
				}

				const bool bPass = WorstMeshSeparationCm <= CorpseMeshSeparationToleranceCm
					&& WorstDriftCm <= CorpseDriftToleranceCm;
				const FString DriftField = (Judged - WithoutRecordedOrigin) > 0
					? FString::Printf(TEXT("worstDrift=%s %.0fcm from where it died (limit %.0fcm)"),
						*WorstDriftName, WorstDriftCm, CorpseDriftToleranceCm)
					: FString::Printf(TEXT("worstDrift=UNMEASURED (0 of %d corpses carried a recorded origin)"),
						Judged);
				OutDetail = FString::Printf(
					TEXT("judged=%d meshlessCorpses=%d corpsesWithoutRecordedOrigin=%d ")
					TEXT("worstMeshSeparation=%s %.0fcm from its own actor (limit %.0fcm) %s"),
					Judged, Meshless, WithoutRecordedOrigin,
					*WorstMeshName, WorstMeshSeparationCm, CorpseMeshSeparationToleranceCm,
					*DriftField);
				return bPass;
			}});
		return true;
	}();

	// ---- CorpseRagdollFlagClear: the direct regression pin on the w58-1
	// un-latch. A dead Sicklejaw reading back as ragdolled means the flag was
	// re-latched after Died, or the un-latch did not run -- which is the exact
	// state that made AFGCharacterBase::SyncRagdoll drive the corpse mesh from
	// physics data that does not exist. This test asserts the READ-BACK only;
	// it does not claim anything about what the mesh is doing.
	const bool bRegistered_CorpseRagdollFlagClear = []
	{
		RegisterPFASelfTest({TEXT("CorpseRagdollFlagClear"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				if (!World)
				{
					OutDetail = TEXT("no world context");
					return false;
				}

				int32 Corpses = 0;
				int32 FlaggedRagdolled = 0;
				FString FirstFlaggedName = TEXT("none");

				for (TActorIterator<APFACreatureBase> It(World); It; ++It)
				{
					APFACreatureBase* Creature = *It;
					if (!Creature || Creature->IsActorBeingDestroyed())
					{
						continue;
					}
					const UFGHealthComponent* Health = Creature->GetHealthComponent();
					if (!Health || !Health->IsDead())
					{
						continue;
					}
					++Corpses;
					if (Creature->IsRagdolled())
					{
						if (FlaggedRagdolled == 0)
						{
							FirstFlaggedName = Creature->GetName();
						}
						++FlaggedRagdolled;
					}
				}

				if (Corpses == 0)
				{
					OutDetail = TEXT("NO-COVERAGE: no corpse present to check -- kill something and re-run");
					return true;
				}

				OutDetail = FString::Printf(
					TEXT("corpses=%d flaggedRagdolled=%d firstFlagged=%s"),
					Corpses, FlaggedRagdolled, *FirstFlaggedName);
				return FlaggedRagdolled == 0;
			}});
		return true;
	}();

	// ---- FacingTarget: guards the rotation policy (w26-1). A creature that
	// holds a target but never turns toward it cannot land a melee hit (the
	// attack cone is measured off the forward vector), so this is a combat
	// regression test wearing cosmetic clothes. Only creatures that HAVE a
	// resolved target and are close enough to be engaging are judged --
	// a roamer may legitimately face anywhere.
	const bool bRegistered_FacingTarget = []
	{
		RegisterPFASelfTest({TEXT("FacingTarget"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				if (!World)
				{
					OutDetail = TEXT("no world context");
					return false;
				}

				// Only judge creatures already inside this range of their
				// target -- far-off pursuers are mid-path and may be
				// rounding an obstacle rather than pointing at the target.
				constexpr float JudgeWithinCm = 1500.0f;

				int32 Judged = 0;
				int32 WithTarget = 0;
				float WorstCos = 1.0f;
				FString WorstName = TEXT("none");

				for (TActorIterator<APFACreatureBase> It(World); It; ++It)
				{
					APFACreatureBase* Creature = *It;
					if (!Creature || Creature->IsActorBeingDestroyed())
					{
						continue;
					}
					const AFGCreatureController* Controller =
						Cast<AFGCreatureController>(Creature->GetController());
					if (!Controller)
					{
						continue;
					}
					const AActor* Target = Controller->GetCurrentTarget();
					if (!Target)
					{
						continue;
					}
					++WithTarget;

					const FVector ToTarget = Target->GetActorLocation() - Creature->GetActorLocation();
					if (ToTarget.Size2D() > JudgeWithinCm)
					{
						continue;
					}
					const FVector ToTargetDir = ToTarget.GetSafeNormal2D();
					const FVector Forward = Creature->GetActorForwardVector().GetSafeNormal2D();
					const float FacingCos = FVector::DotProduct(Forward, ToTargetDir);

					++Judged;
					if (FacingCos < WorstCos)
					{
						WorstCos = FacingCos;
						WorstName = Creature->GetName();
					}
				}

				if (Judged == 0)
				{
					OutDetail = FString::Printf(
						TEXT("NO-COVERAGE: no engaged creature within %.0fcm of a resolved target (withTarget=%d) -- aggro one and re-run"),
						JudgeWithinCm, WithTarget);
					return true;
				}

				const bool bPass = WorstCos >= FacingCosThreshold;
				OutDetail = FString::Printf(
					TEXT("judged=%d withTarget=%d worst=%s facingCos=%.2f (need >=%.2f, i.e. target inside a 120-degree frontal arc)"),
					Judged, WithTarget, *WorstName, WorstCos, FacingCosThreshold);
				return bPass;
			}});
		return true;
	}();
}
