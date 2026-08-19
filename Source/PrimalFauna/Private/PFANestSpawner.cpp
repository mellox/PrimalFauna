#include "PFANestSpawner.h"
#include "PFACheats.h"
#include "PrimalFauna.h"

#include "Creature/FGCreature.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// w58-3. A slot that has been waiting this long for a native spawn location
	// is treated as STALLED rather than in flight, which is the only state in
	// which we will re-enter the closed-source SpawnCreatures() on a nest that
	// still has outstanding requests. The same value rate-limits how often one
	// nest may be re-driven.
	//
	// This number is a THRESHOLD CHOICE, not a measurement. What was measured
	// (2026-08-18-w58-2 session): stalled slots' wait times climbed monotonically
	// past 1000 s with zero observed transitions from waiting to live, across 770
	// slots. Nothing in that session measured how long a HEALTHY projection takes,
	// because not one completed -- so 60 s is picked to sit far above any
	// plausible in-flight latency and far below the observed permanent stall, and
	// it is the value a runtime test should challenge first.
	constexpr float PFANestStallSeconds = 60.0f;

	// Minimum world seconds between two SpawnCreatures() calls on ONE nest,
	// whatever the reason for the second. Deliberately a SEPARATE constant from
	// the stall window above: "how long before an outstanding request is
	// presumed dead" and "how often we may re-enter the closed-source path" are
	// different quantities, and one number serving both means any retune of one
	// silently retunes the other. Equal by default; change them independently.
	constexpr float PFANestRedriveCooldownSeconds = 60.0f;
}

APFANestSpawner::APFANestSpawner()
{
}

const TCHAR* APFANestSpawner::DriveDecisionToString(EPFANestDriveDecision Decision)
{
	switch (Decision)
	{
	case EPFANestDriveDecision::Driven:               return TEXT("driven");
	case EPFANestDriveDecision::DrivenAfterStall:     return TEXT("driven-after-stall");
	case EPFANestDriveDecision::SkipCreaturesLive:    return TEXT("skip-creatures-live");
	case EPFANestDriveDecision::SkipNoUnspawned:      return TEXT("skip-no-unspawned");
	case EPFANestDriveDecision::SkipNoPlayer:         return TEXT("skip-no-player");
	case EPFANestDriveDecision::SkipPlayerOutOfRange: return TEXT("skip-player-out-of-range");
	case EPFANestDriveDecision::SkipRequestInFlight:  return TEXT("skip-request-in-flight");
	case EPFANestDriveDecision::SkipRedriveCooldown:  return TEXT("skip-redrive-cooldown");
	case EPFANestDriveDecision::SkipNativeNotReady:   return TEXT("skip-native-not-ready");
	case EPFANestDriveDecision::DeferPassBudget:      return TEXT("defer-pass-budget");
	}
	return TEXT("unknown");
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

	// w58-3: NO SpawnCreatures() here. The spawn half moved to TryDriveSpawn,
	// which the caller runs on this same pass (and on every later pass) behind a
	// player-distance gate and a per-pass request budget. What is reported below
	// is therefore the CONFIGURATION outcome only -- the drive outcome is the
	// separate "[PFA] Nest drive" line, so neither line can imply the other.
	const int32 LiveAfterConfig = GetCreatures().Num();
	const int32 UnspawnedAfterConfig = GetNumUnspawnedCreatures();

	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Nest adoption recovery: nest=%s snapshot=%s class=%s slotsBefore=%d slotsAfter=%d liveBefore=%d unspawnedBefore=%d populatedMissing=%s decision=%s outcome=config-only"),
			*NestId.ToString(), bHadSnapshotAtEntry ? TEXT("yes") : TEXT("no"),
			*GetNameSafe(mCreatureClass.Get()), SlotsBefore, GetTotalNumCreatures(), LiveAfterConfig,
			UnspawnedAfterConfig, bPopulatedMissingSlots ? TEXT("true") : TEXT("false"),
			bLegacyRecovery ? TEXT("rehydrate-legacy") : TEXT("verify-snapshot"));
	}
}

void APFANestSpawner::ScanSpawnSlots(int32& OutWaitingSlots, float& OutOldestWaitSeconds,
	int32& OutUnstampedWaitingSlots) const
{
	OutWaitingSlots = 0;
	OutOldestWaitSeconds = 0.0f;
	OutUnstampedWaitingSlots = 0;

	const UWorld* World = GetWorld();
	const float WorldTime = World ? World->GetTimeSeconds() : 0.0f;
	for (const FSpawnData& Slot : mSpawnData)
	{
		if (Slot.WaitingForSpawnLocation)
		{
			++OutWaitingSlots;
			if (Slot.SpawnLocationWaitTimeStamp >= 0.0f)
			{
				OutOldestWaitSeconds = FMath::Max(OutOldestWaitSeconds,
					WorldTime - Slot.SpawnLocationWaitTimeStamp);
			}
			else
			{
				// Waiting with no timestamp: its age is UNKNOWN, not zero.
				// Counted separately so oldestWait=0.0 can never be read as
				// "this request just started" when it means "never stamped".
				++OutUnstampedWaitingSlots;
			}
		}
	}
}

float APFANestSpawner::MeasureNearestPlayerDistance() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return -1.0f;
	}

	const FVector NestLocation = GetActorLocation();
	float Nearest = -1.0f;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		const float Distance = FVector::Dist(NestLocation, Pawn->GetActorLocation());
		if (Nearest < 0.0f || Distance < Nearest)
		{
			Nearest = Distance;
		}
	}
	return Nearest;
}

FPFANestDriveResult APFANestSpawner::TryDriveSpawn(int32 RemainingSlotBudget)
{
	FPFANestDriveResult Result;

	const UWorld* World = GetWorld();
	const double NowSeconds = World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;

	Result.LiveBefore = GetCreatures().Num();
	const int32 SlotsBefore = GetTotalNumCreatures();
	Result.UnspawnedBefore = GetNumUnspawnedCreatures();
	ScanSpawnSlots(Result.WaitingBefore, Result.OldestWaitSeconds, Result.UnstampedWaiting);
	Result.WaitingAfter = Result.WaitingBefore;
	Result.NearestPlayerDistance = MeasureNearestPlayerDistance();
	Result.SpawnDistance = GetSpawnDistance();

	const bool bStalled = Result.WaitingBefore > 0 &&
		Result.OldestWaitSeconds >= PFANestStallSeconds;
	const bool bRedriveCoolingDown =
		(NowSeconds - LastDriveTimeSeconds) < static_cast<double>(PFANestRedriveCooldownSeconds);

	// Ordered so the CHEAP, NON-MUTATING predicates run first and the mutating
	// native query runs last: IsReadyToSpawn() can populate slots, trace for a
	// nearby base and advance native timing (see build.ps1's diagnostics gate),
	// so a nest we were never going to drive must not pay it.
	if (Result.LiveBefore != 0)
	{
		// Preserves the pre-w58-3 skip that kept a save-restored nest's native
		// state untouched (the "liveBefore != 0 => do not spawn" half of the old
		// bReadyToSpawn predicate).
		Result.Decision = EPFANestDriveDecision::SkipCreaturesLive;
	}
	else if (Result.UnspawnedBefore <= 0)
	{
		Result.Decision = EPFANestDriveDecision::SkipNoUnspawned;
	}
	else if (Result.NearestPlayerDistance < 0.0f)
	{
		Result.Decision = EPFANestDriveDecision::SkipNoPlayer;
	}
	else if (Result.NearestPlayerDistance > Result.SpawnDistance)
	{
		// THE FIX. The pre-w58-3 path had no distance test at all, so a world
		// load drove every restored nest at once with the player kilometres away.
		Result.Decision = EPFANestDriveDecision::SkipPlayerOutOfRange;
	}
	else if (Result.WaitingBefore > 0 && !bStalled)
	{
		Result.Decision = EPFANestDriveDecision::SkipRequestInFlight;
	}
	else if (bRedriveCoolingDown)
	{
		// Unconditional. LastDriveTimeSeconds is written on EVERY drive, so
		// reading it only when requests are outstanding left the fresh-drive
		// side ungoverned: a nest whose drive produced neither a waiting slot
		// nor a creature would be re-driven every heartbeat, forever, charging
		// nothing against the pass budget.
		Result.Decision = EPFANestDriveDecision::SkipRedriveCooldown;
	}
	else if (RemainingSlotBudget <= 0)
	{
		// Not a refusal: the caller retries this nest on the next pass, which is
		// how the per-pass remainder is carried forward.
		Result.Decision = EPFANestDriveDecision::DeferPassBudget;
	}
	else if (!IsReadyToSpawn())
	{
		Result.Decision = EPFANestDriveDecision::SkipNativeNotReady;
	}
	else
	{
		Result.Decision = bStalled
			? EPFANestDriveDecision::DrivenAfterStall
			: EPFANestDriveDecision::Driven;
		Result.bDriven = true;
		LastDriveTimeSeconds = NowSeconds;

		SpawnCreatures();

		float OldestWaitAfter = 0.0f;
		ScanSpawnSlots(Result.WaitingAfter, OldestWaitAfter, Result.UnstampedWaiting);
		Result.NewLive = FMath::Max(0, GetCreatures().Num() - Result.LiveBefore);
		// Slots this pass COMMITTED to the closed-source spawn-location path.
		// NOT a delta: on a re-drive of a stalled nest the previously-waiting
		// slots are re-issued and the delta is zero, which would make the
		// per-pass cap vacuous in exactly the case it exists to bound. Charge
		// every slot left outstanding after the call, plus any that resolved
		// into a live creature during it.
		Result.BudgetUsed = Result.WaitingAfter + Result.NewLive;
	}

	const bool bDecisionChanged =
		!bHasLoggedDriveDecision || LastLoggedDriveDecision != Result.Decision;
	const bool bPeriodicReport = (NowSeconds - LastDriveLogTimeSeconds) >= 60.0;
	if (PFADiag::IsEnabled() && (Result.bDriven || bDecisionChanged || bPeriodicReport))
	{
		bHasLoggedDriveDecision = true;
		LastLoggedDriveDecision = Result.Decision;
		LastDriveLogTimeSeconds = NowSeconds;

		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Nest drive: nest=%s decision=%s slotsBefore=%d slotsAfter=%d live=%d unspawned=%d waitingBefore=%d waitingAfter=%d oldestWait=%.1fs waitingUnstamped=%d newLive=%d budgetUsed=%d budgetOffered=%d nearestPlayer=%.0f spawnDistance=%.0f stallWindow=%.0fs cooldown=%.0fs"),
			*NestId.ToString(), DriveDecisionToString(Result.Decision), SlotsBefore,
			GetTotalNumCreatures(), Result.LiveBefore,
			Result.UnspawnedBefore, Result.WaitingBefore, Result.WaitingAfter,
			Result.OldestWaitSeconds, Result.UnstampedWaiting, Result.NewLive, Result.BudgetUsed,
			RemainingSlotBudget,
			Result.NearestPlayerDistance, Result.SpawnDistance, PFANestStallSeconds,
			PFANestRedriveCooldownSeconds);
	}

	return Result;
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
