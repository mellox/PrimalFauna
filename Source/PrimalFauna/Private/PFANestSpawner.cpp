#include "PFANestSpawner.h"
#include "PFACheats.h"
#include "PrimalFauna.h"

#include "Creature/FGCreature.h"
#include "GameFramework/Pawn.h"

APFANestSpawner::APFANestSpawner()
{
}

void APFANestSpawner::CaptureRuntimeConfigurationSnapshot()
{
	bHasRuntimeConfigSnapshot = true;
	SavedCreatureClass = mCreatureClass;
	SavedNumberOfCreatures = mNumberOfCreatures;
	bSavedCanSpawnDuringDay = mCanSpawnDuringDay;
	bSavedCanSpawnDuringNight = mCanSpawnDuringNight;
	SavedSpawnRadius = mSpawnRadius;
	SavedRespawnTimeInDays = mRespawnTimeIndays;
}

void APFANestSpawner::RestoreRuntimeConfigurationSnapshot()
{
	mCreatureClass = SavedCreatureClass;
	mNumberOfCreatures = SavedNumberOfCreatures;
	mCanSpawnDuringDay = bSavedCanSpawnDuringDay;
	mCanSpawnDuringNight = bSavedCanSpawnDuringNight;
	mSpawnRadius = SavedSpawnRadius;
	mRespawnTimeIndays = SavedRespawnTimeInDays;
	bConfigured = true;
}

void APFANestSpawner::PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
	Super::PreSaveGame_Implementation(SaveVersion, GameVersion);
	// ConfigureNest (or legacy adoption) captured PFA's authored baseline.
	// Do not refresh it from live fields here: HostileScaling deliberately
	// treats respawn scaling and starvation radius widening as per-session,
	// non-SaveGame state that must reset on the next load.
	if (!bHasRuntimeConfigSnapshot)
	{
		CaptureRuntimeConfigurationSnapshot();
	}

	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Nest save snapshot: nest=%s class=%s slots=%d creatures=%d-%d radius=%.0f day=%s night=%s respawnDays=%d decision=preserve-authored outcome=ready"),
			*NestId.ToString(), *GetNameSafe(mCreatureClass.Get()), GetTotalNumCreatures(),
			SavedNumberOfCreatures.Min, SavedNumberOfCreatures.Max, SavedSpawnRadius,
			bSavedCanSpawnDuringDay ? TEXT("true") : TEXT("false"),
			bSavedCanSpawnDuringNight ? TEXT("true") : TEXT("false"), SavedRespawnTimeInDays);
	}
}

void APFANestSpawner::PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
	const bool bHadSnapshot = bHasRuntimeConfigSnapshot;
	if (bHadSnapshot)
	{
		// Restore before the base hook: its documented job includes recoupling
		// saved FSpawnData to creatures, which requires a valid creature class.
		RestoreRuntimeConfigurationSnapshot();
	}
	bRuntimeAdoptionChecked = false;

	Super::PostLoadGame_Implementation(SaveVersion, GameVersion);

	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Nest load restore: nest=%s snapshot=%s class=%s slots=%d liveCreatures=%d unspawned=%d decision=%s outcome=%s"),
			*NestId.ToString(), bHadSnapshot ? TEXT("yes") : TEXT("no"),
			*GetNameSafe(mCreatureClass.Get()), GetTotalNumCreatures(), GetCreatures().Num(),
			GetNumUnspawnedCreatures(), bHadSnapshot ? TEXT("restore-before-super") : TEXT("defer-legacy-adoption"),
			(bHadSnapshot && mCreatureClass) ? TEXT("ready") : TEXT("needs-recovery"));
	}
}

void APFANestSpawner::ConfigureNest(FName InNestId, FName InRegionId, TSubclassOf<AFGCreature> InCreatureClass,
	int32 InMinCreatures, int32 InMaxCreatures, float InSpawnRadius,
	bool bInCanSpawnDuringDay, bool bInCanSpawnDuringNight, int32 InRespawnTimeIndays)
{
	if (bConfigured)
	{
		// S2 contract violation guard (see header comment). Refuse instead of
		// re-applying: a second write here would fight HostileScaling's own
		// idempotent scaling pass on these same fields (doc 06 S2 -- "one
		// writer per field: creation values are ours, ongoing scaling is
		// HostileScaling's"). NestId still holds the value from the FIRST
		// (successful) call at this point, so it identifies which nest this is.
		UE_LOG(LogPrimalFauna, Error,
			TEXT("[PFA] %s: ConfigureNest called a SECOND time (existing NestId=%s, requested NestId=%s) -- IGNORED. This is an S2 contract violation; investigate the caller (APFASubsystem::ReconcileNests must materialize each record at most once)."),
			*GetName(), *NestId.ToString(), *InNestId.ToString());
		return;
	}
	bConfigured = true;

	NestId = InNestId;
	RegionId = InRegionId;

	// Protected AFGCreatureSpawner fields, self-accessed via inheritance --
	// see header comment (all confirmed protected in FGCreatureSpawner.h; this
	// file adds zero entries to Config/AccessTransformers.ini).
	mCreatureClass = InCreatureClass;
	mNumberOfCreatures = FInt32Interval(InMinCreatures, InMaxCreatures);
	mCanSpawnDuringDay = bInCanSpawnDuringDay;
	mCanSpawnDuringNight = bInCanSpawnDuringNight;
	mSpawnRadius = InSpawnRadius;

	// Public on the base class (FGCreatureSpawner.h:263) -- set here anyway so
	// ConfigureNest remains the ONE audited call site for every write-once
	// field, public or protected.
	mRespawnTimeIndays = InRespawnTimeIndays;
	CaptureRuntimeConfigurationSnapshot();

	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] Nest %s (region=%s) configured: class=%s creatures=%d-%d radius=%.0f day=%s night=%s respawnDays=%d"),
		*NestId.ToString(), *RegionId.ToString(),
		InCreatureClass ? *InCreatureClass->GetName() : TEXT("none"),
		InMinCreatures, InMaxCreatures, InSpawnRadius,
		bInCanSpawnDuringDay ? TEXT("true") : TEXT("false"),
		bInCanSpawnDuringNight ? TEXT("true") : TEXT("false"),
		InRespawnTimeIndays);
}

void APFANestSpawner::EnsureRuntimeConfigurationAfterAdoption(TSubclassOf<AFGCreature> InCreatureClass,
	int32 InMinCreatures, int32 InMaxCreatures, float InSpawnRadius,
	bool bInCanSpawnDuringDay, bool bInCanSpawnDuringNight, int32 InRespawnTimeIndays)
{
	if (bRuntimeAdoptionChecked)
	{
		return;
	}
	bRuntimeAdoptionChecked = true;

	const bool bHadSnapshotAtEntry = bHasRuntimeConfigSnapshot;
	const bool bLegacyRecovery = !bHadSnapshotAtEntry || !mCreatureClass;
	const int32 SlotsBefore = GetTotalNumCreatures();
	bool bPopulatedMissingSlots = false;
	if (bLegacyRecovery)
	{
		mCreatureClass = InCreatureClass;
		// Preserve a legacy save's exact effective slot count (including a
		// HostileScaling multiplier) instead of resetting it to the registry.
		// Only a genuinely empty legacy array falls back to the authored range.
		mNumberOfCreatures = SlotsBefore > 0
			? FInt32Interval(SlotsBefore, SlotsBefore)
			: FInt32Interval(InMinCreatures, InMaxCreatures);
		mCanSpawnDuringDay = bInCanSpawnDuringDay;
		mCanSpawnDuringNight = bInCanSpawnDuringNight;
		mSpawnRadius = InSpawnRadius;
		mRespawnTimeIndays = InRespawnTimeIndays;
		bConfigured = true;

		if (SlotsBefore == 0)
		{
			bPopulatedMissingSlots = PopulateSpawnData();
		}
		// The live interval above preserves a legacy save's effective slot
		// count for this session. The durable snapshot remains PFA-authored so
		// HostileScaling's transient changes do not become our next baseline.
		bHasRuntimeConfigSnapshot = true;
		SavedCreatureClass = InCreatureClass;
		SavedNumberOfCreatures = FInt32Interval(InMinCreatures, InMaxCreatures);
		bSavedCanSpawnDuringDay = bInCanSpawnDuringDay;
		bSavedCanSpawnDuringNight = bInCanSpawnDuringNight;
		SavedSpawnRadius = InSpawnRadius;
		SavedRespawnTimeInDays = InRespawnTimeIndays;
	}

	RegisterAsNavigationInvoker(true);
	TryRecoupleCreatureAndSpawner();

	const int32 LiveBeforeSpawn = GetCreatures().Num();
	const int32 UnspawnedBefore = GetNumUnspawnedCreatures();
	const bool bReadyToSpawn = LiveBeforeSpawn == 0 && UnspawnedBefore > 0 && IsReadyToSpawn();
	if (bReadyToSpawn)
	{
		SpawnCreatures();
	}

	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Nest adoption recovery: nest=%s snapshot=%s class=%s slotsBefore=%d slotsAfter=%d liveBefore=%d unspawnedBefore=%d populatedMissing=%s ready=%s decision=%s outcome=%s"),
			*NestId.ToString(), bHadSnapshotAtEntry ? TEXT("yes") : TEXT("no"),
			*GetNameSafe(mCreatureClass.Get()), SlotsBefore, GetTotalNumCreatures(), LiveBeforeSpawn,
			UnspawnedBefore, bPopulatedMissingSlots ? TEXT("true") : TEXT("false"),
			bReadyToSpawn ? TEXT("true") : TEXT("false"),
			bLegacyRecovery ? TEXT("rehydrate-legacy") : TEXT("verify-snapshot"),
			bReadyToSpawn ? TEXT("spawn-initiated") : TEXT("native-state-kept"));
	}
}

void APFANestSpawner::LogSpawnDecisionDiagnostics(const APawn* PlayerPawn)
{
	if (!PFADiag::IsEnabled())
	{
		return;
	}

	int32 WaitingSlots = 0;
	int32 SpawnableSlots = 0;
	int32 KilledSlots = 0;
	float OldestWaitSeconds = 0.0f;
	const UWorld* World = GetWorld();
	const float WorldTime = World ? World->GetTimeSeconds() : 0.0f;
	for (const FSpawnData& Slot : mSpawnData)
	{
		if (Slot.WaitingForSpawnLocation)
		{
			++WaitingSlots;
			if (Slot.SpawnLocationWaitTimeStamp >= 0.0f)
			{
				OldestWaitSeconds = FMath::Max(OldestWaitSeconds,
					WorldTime - Slot.SpawnLocationWaitTimeStamp);
			}
		}
		KilledSlots += Slot.WasKilled ? 1 : 0;
		SpawnableSlots += CanSpawnCreature(Slot) ? 1 : 0;
	}

	const int32 LiveCreatures = GetCreatures().Num();
	const int32 UnspawnedCreatures = GetNumUnspawnedCreatures();
	const bool bActive = IsSpawnerActive();
	const bool bCorrectTime = IsTimeForCreature();
	const bool bNearBase = IsNearBase();
	const float SpawnDistance = GetSpawnDistance();
	const float SpawnRadius = GetSpawnRadius();
	const float FirstPlayerDistance = PlayerPawn
		? FVector::Dist(GetActorLocation(), PlayerPawn->GetActorLocation())
		: -1.0f;
	const bool bObservedEligible =
		!bActive && bCorrectTime && SpawnableSlots > 0 && UnspawnedCreatures > 0;

	const TCHAR* Summary = TEXT("observed-eligible");
	if (WaitingSlots > 0)
	{
		Summary = TEXT("await-location");
	}
	else if (LiveCreatures > 0)
	{
		Summary = TEXT("creatures-live");
	}
	else if (bActive)
	{
		Summary = TEXT("spawner-active");
	}
	else if (!bCorrectTime)
	{
		Summary = TEXT("day-night-blocked");
	}
	else if (SpawnableSlots == 0)
	{
		Summary = TEXT("no-spawnable-slot");
	}

	uint32 Signature = GetTypeHash(WaitingSlots);
	Signature = HashCombine(Signature, GetTypeHash(SpawnableSlots));
	Signature = HashCombine(Signature, GetTypeHash(LiveCreatures));
	Signature = HashCombine(Signature, GetTypeHash(UnspawnedCreatures));
	Signature = HashCombine(Signature, GetTypeHash(KilledSlots));
	Signature = HashCombine(Signature, GetTypeHash(bActive));
	Signature = HashCombine(Signature, GetTypeHash(bCorrectTime));
	Signature = HashCombine(Signature, GetTypeHash(bNearBase));
	Signature = HashCombine(Signature, GetTypeHash(bObservedEligible));
	Signature = HashCombine(Signature, GetTypeHash(FMath::RoundToInt(SpawnDistance)));
	Signature = HashCombine(Signature, GetTypeHash(FMath::RoundToInt(SpawnRadius)));
	Signature = HashCombine(Signature,
		GetTypeHash(FMath::RoundToInt(FirstPlayerDistance / 1000.0f)));

	const double NowSeconds = World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
	const bool bPeriodicBlockedReport = WaitingSlots > 0 &&
		(NowSeconds - LastSpawnDiagnosticTimeSeconds) >= 60.0;
	if (bHasSpawnDiagnosticSignature && Signature == LastSpawnDiagnosticSignature && !bPeriodicBlockedReport)
	{
		return;
	}
	bHasSpawnDiagnosticSignature = true;
	LastSpawnDiagnosticSignature = Signature;
	LastSpawnDiagnosticTimeSeconds = NowSeconds;

	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] Nest spawn observation: nest=%s active=%s timeAllowed=%s nearBase=%s observedEligible=%s slots=%d live=%d unspawned=%d spawnable=%d waiting=%d killed=%d oldestWait=%.1fs spawnDistance=%.0f firstPlayerDistance=%.0f radius=%.0f summary=%s outcome=observed-only"),
		*NestId.ToString(), bActive ? TEXT("true") : TEXT("false"),
		bCorrectTime ? TEXT("true") : TEXT("false"), bNearBase ? TEXT("true") : TEXT("false"),
		bObservedEligible ? TEXT("true") : TEXT("false"), mSpawnData.Num(), LiveCreatures,
		UnspawnedCreatures, SpawnableSlots, WaitingSlots, KilledSlots, OldestWaitSeconds,
		SpawnDistance, FirstPlayerDistance, SpawnRadius, Summary);
}
