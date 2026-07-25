#include "PFACreatureBase.h"
#include "PrimalFauna.h"
#include "PFACreatureController.h"
#include "PFACheats.h"
#include "PFAConfig.h"

#include "AI/FGCreatureController.h"
#include "Creature/FGCreature.h"
#include "FGHealthComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

namespace
{
	const TCHAR* AggressionModeToString(EPFAAggression Mode)
	{
		switch (Mode)
		{
		case EPFAAggression::Passive:     return TEXT("Passive");
		case EPFAAggression::Territorial: return TEXT("Territorial");
		case EPFAAggression::Hostile:     return TEXT("Hostile");
		default:                          return TEXT("<unknown>");
		}
	}

	// Generic placeholder territory radius (30m) for any species that doesn't
	// set its own in its ctor. Only Territorial-mode species (none in W1.3 --
	// Sicklejaw defaults to Hostile) actually consult this value.
	constexpr float DefaultTerritoryRadiusCm = 3000.0f;

	// w18-3 buried-spawn rescue tuning (RescueBuriedSpawn below). Probe TOP is
	// head + 50cm -- deliberately low so overhead geometry (bridge/canopy)
	// can't read as "ground"; the cost is that burial deeper than that reads
	// ground=NONE and is only logged, not rescued.
	constexpr float BuriedProbeAboveHeadCm = 50.0f;
	constexpr float BuriedProbeBelowCm = 3000.0f;
	constexpr float BuriedRescueThresholdCm = 30.0f;
	constexpr float BuriedLiftClearanceCm = 5.0f;

	// w18-4 (cold-review mandatory guard): only an up-facing, walkable surface
	// is valid rescue ground. A downward-facing first hit inside the 50cm
	// above-head window (low ceiling / foundation underside / rock overhang)
	// has ImpactNormal.Z <= 0 and would otherwise read as "buried by ~2m" and
	// launch a correctly-grounded creature up onto the roof. 0.7 admits
	// slopes to ~45 deg; real lowland nest terrain reads ~1.0.
	constexpr float BuriedWalkableNormalZ = 0.7f;

	// w26-1: yaw rate for orient-to-movement. 360 deg/s = a predator can
	// come about in one second; fast enough to keep a strafing player inside
	// the 90-degree melee cone without looking like a turret.
	constexpr float CreatureYawRateDegPerSec = 360.0f;

	// w18-4 (cold-review recommendation): live kill-switch for the rescue
	// TELEPORT (the probe + logging always run) -- follows the pfa.Diag
	// pattern so a misbehaving rescue can be silenced in-game, no rebuild.
	static TAutoConsoleVariable<int32> CVarPFARescueBuriedSpawn(
		TEXT("pfa.RescueBuriedSpawn"), 1,
		TEXT("1 (default): lift creatures whose feet spawned below the terrain surface onto the ground. 0: probe + log only, never teleport."),
		ECVF_Default);

	// ---------------------------------------------------------------------
	// mIsPassiveCreature / mSpawnDistance / mDespawnDistance are PRIVATE on
	// AFGCreature (Creature/FGCreature.h:659,691,695) -- self-inheritance
	// cannot reach them. Per the W1.3 cold-review adjudication these are
	// written DIRECTLY under the Friend grant in Config/AccessTransformers.ini
	// (Friend=(Class="AFGCreature", FriendClass="APFACreatureBase")): a
	// game-update field rename becomes a COMPILE ERROR at the next build
	// instead of a silent runtime no-op -- decisive for mIsPassiveCreature,
	// which feeds both our Passive mode and HostileScaling's skip-check
	// (docs/06 C5). The reflection-write alternative that shipped first in
	// this packet is preserved in git history if ever needed.
	// ---------------------------------------------------------------------
}

APFACreatureBase::APFACreatureBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TerritoryRadiusCm(DefaultTerritoryRadiusCm)
	, mAggressionMode(EPFAAggression::Hostile)
{
	AIControllerClass = APFACreatureController::StaticClass();

	// w18-12 -- SIGHT-AGGRO root cause (in-game sessions 6-9: creatures never
	// initiated without being damaged): AFGCreature's aggro pipeline is
	// curve-driven -- mVisibilityGainRateCurve ("considered visible once its
	// visibility value reaches 1.0", FGCreature.h:609-611) and
	// mAggroGainRateCurve (aggro/sec "Gets multiplied with AggroGainRateCurve",
	// :605-607/:613-615). Vanilla creatures reference curve ASSETS from their
	// BP; a C++-only CDO leaves both NULL, so sight-visibility never accrues
	// and the aggro product is zero -- only the curve-free damage-aggro path
	// ever fired. Author constant-1.0 curves as CDO subobjects: runtime-built
	// keys, NOT a content load, so cookbook gotcha #33 (no ctor content
	// loads) does not apply. Result: visibility accrues at 1.0/s at any
	// distance inside sight range (the 5m ImmediateVisibilityRadius still
	// short-circuits up close), and aggro accrues at the authored
	// mAggroGainRate x 1.0 -- restoring the doc 01 numbers (10/s -> Fight at
	// 50 after ~5s of being seen). Species can override with real shaped
	// curves later (W2.3 tuning).
	UCurveFloat* VisibilityCurve = CreateDefaultSubobject<UCurveFloat>(TEXT("PFAVisibilityGainCurve"));
	VisibilityCurve->FloatCurve.AddKey(0.0f, 1.0f);
	VisibilityCurve->FloatCurve.AddKey(10000.0f, 1.0f);
	mVisibilityGainRateCurve = VisibilityCurve;

	UCurveFloat* AggroCurve = CreateDefaultSubobject<UCurveFloat>(TEXT("PFAAggroGainCurve"));
	AggroCurve->FloatCurve.AddKey(0.0f, 1.0f);
	AggroCurve->FloatCurve.AddKey(10000.0f, 1.0f);
	mAggroGainRateCurve = AggroCurve;
}

void APFACreatureBase::BeginPlay()
{
	Super::BeginPlay();

	// W1.6 current-health init gap fixup (docs/08 packet, Phase-1 gate
	// Section B item 8: fresh nest Sicklejaws read Health=0.0/200.0). Server-
	// only -- health is server-authoritative (replicated to clients, never
	// locally decided). [assumption -> in-game test, docs/07 §1 methodology]:
	// this is a spawn-init gap our C++-only species never got (vanilla BP
	// creatures likely heal-to-full somewhere in their own spawn/BeginPlay BP
	// graph), NOT a sign a creature is meant to spawn near-dead. Guarded on
	// !IsDead() so this can never resurrect a creature that legitimately died
	// at 0 health -- only fixes a creature that was never alive at all.
	if (!HasAuthority())
	{
		return;
	}

	if (UFGHealthComponent* Health = GetHealthComponent())
	{
		const float CurrentBefore = Health->GetCurrentHealth();
		if (CurrentBefore <= 0.0f && !Health->IsDead())
		{
			// ResetHealth() (FGHealthComponent.h:124) -- verified UFUNCTION
			// (BlueprintCallable, Category = "Health"), not the shipping-
			// export-trap risk class (docs/07 §1: that trap is specifically
			// plain non-UFUNCTION methods). New symbol for this mod's DLL --
			// tools/check_imports.ps1 must run on the next package.
			Health->ResetHealth();

			// Gated (PFADiag), matching every other per-instance write-audit
			// log in this file (SetAggressionMode/SetSpawnDespawnDistance/
			// SetBorrowedBehaviorTree all follow this same convention).
			if (PFADiag::IsEnabled())
			{
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] %s: BeginPlay current-health init gap detected (was %.1f/%.1f, IsDead=false) -- ResetHealth() -> %.1f/%.1f"),
					*GetName(), CurrentBefore, Health->GetMaxHealth(),
					Health->GetCurrentHealth(), Health->GetMaxHealth());
			}
		}
	}
	else
	{
		// Should not happen (see SetBaseHealth's own comment: AFGCharacterBase
		// creates mHealthComponent before any subclass ctor body runs, well
		// before BeginPlay) -- logged, not asserted, same degrade philosophy
		// as the rest of this file.
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: BeginPlay found no health component to check for the current-health init gap."),
			*GetName());
	}

	// ---- w26-1 ROTATION POLICY (user report: "when aggroing onto me and
	// coming to me they don't turn to face me"). UE offers two MUTUALLY
	// EXCLUSIVE yaw mechanisms for a Character, and a C++-only creature
	// inherits neither in a working state -- vanilla creatures get theirs
	// from the BP layer we replaced (the same parity-gap class as the BT
	// launch, perception wiring and aggro curves). Result: they translate
	// toward the target without ever facing it, which also silently breaks
	// the melee cone (mAttackAngle is measured from the forward vector).
	// Fix: turn ON orient-to-movement (a running predator faces where it
	// runs) and force controller-yaw OFF so the two can never fight.
	//
	// The controller also sets an AI focus on its target -- note that with
	// bUseControllerRotationYaw=false the focus does NOT rotate the body
	// (APawn::FaceRotation early-outs); it steers ControlRotation only,
	// which FG uses for view traces and action aiming. Body yaw comes from
	// orient-to-movement, so a STATIONARY creature holds its last heading.
	// If in-game shows they stop tracking a strafing player at melee range,
	// the fix is bUseControllerDesiredRotation=true with
	// bOrientRotationToMovement=false (mutually exclusive in
	// UCharacterMovementComponent::PhysicsRotation -- pick one).
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		const bool bOrientBefore = Movement->bOrientRotationToMovement;
		const bool bYawBefore = bUseControllerRotationYaw;
		const FRotator RateBefore = Movement->RotationRate;

		Movement->bOrientRotationToMovement = true;
		// YAW ONLY. Keep whatever pitch/roll rate the FG movement component
		// shipped with: mAlignRotationToFloor (FGCreatureController.h:475 --
		// "don't forget to set rotation rates in movement component to use
		// this values! Only pitch is supported") dies if the pitch rate is
		// 0, and a zero rate means "preserve current", not "force zero".
		Movement->RotationRate = FRotator(RateBefore.Pitch, CreatureYawRateDegPerSec, RateBefore.Roll);
		bUseControllerRotationYaw = false;

		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: rotation policy -> orientToMovement %s->true, yawRate %.0f->%.0f deg/s (pitch/roll rate kept %.0f/%.0f), useControllerYaw %s->false"),
			*GetName(),
			bOrientBefore ? TEXT("true") : TEXT("false"),
			RateBefore.Yaw, CreatureYawRateDegPerSec, RateBefore.Pitch, RateBefore.Roll,
			bYawBefore ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: no CharacterMovementComponent -- rotation policy NOT applied (creature may never face its target)."),
			*GetName());
	}

	// w18-3 buried-spawn rescue (in-game evidence 2026-07-24: nest spawns at
	// lowland nests -- e.g. Test_5 at Z=-2756 -- left creatures present and
	// perceiving through ImmediateVisibilityRadius, but invisible and stuck
	// CS_Alert ~2m under the terrain; the flat-plateau nests w15/w17 validated
	// on masked this). The native spawn flow projects spawn points
	// asynchronously (FSpawnData::WaitingForSpawnLocation,
	// FGCreatureSpawner.h:45-49) and evidently falls back unprojected. Probe
	// on the NEXT tick -- after the spawner/possession has finalized
	// placement, so it can't be overwritten -- and lift the creature onto the
	// surface if its feet ended below ground. Raw member fn on the templated
	// timer API (heartbeat precedent: no UFUNCTION wanted -- stays off the
	// reflection/export surface).
	GetWorldTimerManager().SetTimerForNextTick(this, &APFACreatureBase::RescueBuriedSpawn);

	// W2.3: stamp the configured aggression on spawn (server-only section).
	// The subsystem retargets LIVE creatures only when the config value
	// CHANGES, so a fresh spawn must self-apply the current value.
	// v1 single-species: the Sicklejaw key governs every APFACreatureBase
	// until per-species keys ship with the roster. SetAggressionMode no-ops
	// on same-mode, so the default (2=Hostile) is a silent pass-through.
	const int32 ConfiguredAggression = FMath::Clamp(
		FPFAConfigStruct::GetActiveConfig(this).SicklejawAggression, 0, 2);
	SetAggressionMode(static_cast<EPFAAggression>(ConfiguredAggression));
}

void APFACreatureBase::RescueBuriedSpawn()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}

	const FVector Loc = GetActorLocation();
	float CapsuleHalfCm = 90.0f;
	if (const UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		CapsuleHalfCm = Capsule->GetScaledCapsuleHalfHeight();
	}
	const float FeetZ = Loc.Z - CapsuleHalfCm;

	FHitResult Hit;
	FCollisionQueryParams Params(FName(TEXT("PFABuriedSpawnProbe")), false, this);
	const FVector Start(Loc.X, Loc.Y, Loc.Z + CapsuleHalfCm + BuriedProbeAboveHeadCm);
	const FVector End(Loc.X, Loc.Y, Loc.Z - BuriedProbeBelowCm);
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		// Always-on Warning: either deep-buried (below probe reach) or spawned
		// over a void -- both are anomalies the log must surface.
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: buried-spawn probe found NO ground from head+%.0fcm down to -%.0fcm (loc=%s) -- deep-buried or over void; left as-is."),
			*GetName(), BuriedProbeAboveHeadCm, BuriedProbeBelowCm, *Loc.ToCompactString());
		return;
	}

	const float SunkenCm = Hit.ImpactPoint.Z - FeetZ;
	if (SunkenCm > BuriedRescueThresholdCm)
	{
		// w18-4 cold-review guard: reject non-walkable/overhead first hits --
		// see BuriedWalkableNormalZ above. Warn-only: a real deep burial under
		// an overhang stays put and stays visible in the log.
		if (Hit.ImpactNormal.Z < BuriedWalkableNormalZ)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] %s: buried-spawn probe hit a non-walkable/overhead surface (normalZ=%.2f impact=%s sunken=%.0fcm) -- not ground; NOT rescuing."),
				*GetName(), Hit.ImpactNormal.Z, *Hit.ImpactPoint.ToCompactString(), SunkenCm);
			return;
		}

		if (CVarPFARescueBuriedSpawn.GetValueOnGameThread() == 0)
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: buried-spawn rescue SUPPRESSED by pfa.RescueBuriedSpawn=0 (feet %.0fcm below ground at %s) -- probe-only mode."),
				*GetName(), SunkenCm, *Loc.ToCompactString());
			return;
		}

		const FVector NewLoc(Loc.X, Loc.Y, Hit.ImpactPoint.Z + CapsuleHalfCm + BuriedLiftClearanceCm);
		const bool bMoved = SetActorLocation(NewLoc, false, nullptr, ETeleportType::TeleportPhysics);
		// Always-on Display: a corrective world mutation must announce itself
		// in every log, not only under pfa.Diag.
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: buried-spawn rescue: feet were %.0fcm BELOW ground (spawn=%s groundZ=%.0f) -> lifted to %s (moved=%s)"),
			*GetName(), SunkenCm, *Loc.ToCompactString(), Hit.ImpactPoint.Z,
			*NewLoc.ToCompactString(), bMoved ? TEXT("true") : TEXT("false"));
	}
	else if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: buried-spawn probe: on surface (feet %+.0fcm relative to ground) -- no rescue needed."),
			*GetName(), -SunkenCm);
	}
}

void APFACreatureBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APFACreatureBase, mAggressionMode);
}

void APFACreatureBase::SetAggressionMode(EPFAAggression NewMode)
{
	if (!HasAuthority())
	{
		return;
	}

	const EPFAAggression OldMode = mAggressionMode;
	mAggressionMode = NewMode;

	// Doc 02 §3 mode 0: flip the INSTANCE flag, never the CDO -- the class
	// stays authored-hostile (doc 07 §3) so HostileScaling and native
	// systems see exactly this creature as passive. Direct private-member
	// write under the AccessTransformers Friend grant (see note above).
	mIsPassiveCreature = (NewMode == EPFAAggression::Passive);

	// W2.5 (session-15, + w25-4 review fix): wipe per-target state on an
	// ACTUAL mode change only. A genuine Passive<->Hostile transition must
	// not leak stale aggro/visibility entries (aggroTargets>0 with
	// currentTarget=none, rule5 NOT_HOSTILE vs the player); a same-mode
	// re-stamp (BeginPlay self-apply, the subsystem's first-tick -1
	// sentinel) stays a NO-OP so it can't wipe live targeting or spam the
	// always-on ResetTargetingState line. Falls back to the doc 02 §3
	// Passive-only NullifyAllAggro if the controller is somehow not ours.
	if (OldMode != NewMode)
	{
		if (AFGCreatureController* PFACreatureAI = GetCreatureController())
		{
			if (APFACreatureController* OurController = Cast<APFACreatureController>(PFACreatureAI))
			{
				OurController->ResetTargetingState(TEXT("aggression-mode change"));
			}
			else if (NewMode == EPFAAggression::Passive)
			{
				PFACreatureAI->NullifyAllAggro();
			}
		}
	}

	if (PFADiag::IsEnabled())
	{
		// Read back via the PUBLIC getter (IsPassiveCreature(), FGCreature.h:303)
		// rather than the field directly -- this doubles as a round-trip
		// check that the reflection write above actually took effect.
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s: SetAggressionMode %s -> %s (mIsPassiveCreature=%s)"),
			*GetName(), AggressionModeToString(OldMode), AggressionModeToString(NewMode),
			IsPassiveCreature() ? TEXT("true") : TEXT("false"));
	}
}

void APFACreatureBase::SetBaseHealth(float NewMaxHealth)
{
	if (UFGHealthComponent* Health = GetHealthComponent())
	{
		Health->SetMaxHealth(NewMaxHealth);
	}
	else
	{
		// Should not happen: AFGCharacterBase creates mHealthComponent before
		// any subclass ctor body runs. Logged (not asserted) so a meshless/
		// componentless edge case degrades instead of crashing.
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: SetBaseHealth(%.1f) called with no health component -- AFGCharacterBase should have created it before a subclass ctor body runs."),
			*GetClass()->GetName(), NewMaxHealth);
	}
}

void APFACreatureBase::SetSpawnDespawnDistance(float NewSpawnDistanceCm, float NewDespawnDistanceCm)
{
	// Direct private-member writes under the AccessTransformers Friend grant
	// (see note at the top of this file).
	mSpawnDistance = NewSpawnDistanceCm;
	mDespawnDistance = NewDespawnDistanceCm;

	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s: SetSpawnDespawnDistance spawn=%.1f despawn=%.1f (getters: %.1f/%.1f)"),
			*GetName(), NewSpawnDistanceCm, NewDespawnDistanceCm, GetSpawnDistance(), GetDespawnDistance());
	}
}

void APFACreatureBase::SetBorrowedBehaviorTree(UBehaviorTree* BehaviorTree)
{
	// mBehaviorTree is protected on AFGCreature (FGCreature.h:548) -- inherited,
	// so written directly (no Friend grant, unlike the private spawn/despawn
	// fields). Assigning null is legal: the creature simply idles (degraded, not
	// broken) if discovery found nothing to borrow.
	mBehaviorTree = BehaviorTree;

	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s: SetBorrowedBehaviorTree -> %s (readback %s)"),
			*GetClass()->GetName(),
			BehaviorTree ? *BehaviorTree->GetName() : TEXT("null"),
			GetBehaviorTreeAsset() ? *GetBehaviorTreeAsset()->GetName() : TEXT("null"));
	}
}

void APFACreatureBase::ClearNativeOrphanTimer()
{
	// Direct private-handle clear under the Friend grant -- see the header
	// comment for the shipping-export trap that forbids calling the game's
	// own ClearKillOrphanTimer(). FTimerManager::ClearTimer is a header-inline
	// wrapper over the EXPORTED InternalClearTimer, so this binds cleanly
	// (verified against the shipping export table, gate session 2026-07-23).
	GetWorldTimerManager().ClearTimer(mKillOrphanHandle);
}

void APFACreatureBase::SpawnDeathItem_Implementation()
{
	// Preserve whatever the base class's own default does (unknown --
	// FGCreature.cpp is a closed-source link stub); W2's loot items replace
	// this log with real item-drop spawning (doc 07 §6, doc 01 §5).
	Super::SpawnDeathItem_Implementation();

	UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s died - loot arrives with W2 items"), *GetClass()->GetName());
}
