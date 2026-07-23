#include "PFACreatureBase.h"
#include "PrimalFauna.h"
#include "PFACreatureController.h"
#include "PFACheats.h"

#include "AI/FGCreatureController.h"
#include "Creature/FGCreature.h"
#include "FGHealthComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Net/UnrealNetwork.h"

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

	if (NewMode == EPFAAggression::Passive && OldMode != EPFAAggression::Passive)
	{
		// Doc 02 §3: "Verify in-game that flipping it live cleanly de-aggros
		// (call NullifyAllAggro() on transition)." NullifyAllAggro is public
		// on AFGCreatureController (AI/FGCreatureController.h:280).
		if (AFGCreatureController* PFACreatureAI = GetCreatureController())
		{
			PFACreatureAI->NullifyAllAggro();
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
