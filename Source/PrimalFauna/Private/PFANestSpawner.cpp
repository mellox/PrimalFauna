#include "PFANestSpawner.h"
#include "PrimalFauna.h"

#include "Creature/FGCreature.h"

APFANestSpawner::APFANestSpawner()
{
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

	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] Nest %s (region=%s) configured: class=%s creatures=%d-%d radius=%.0f day=%s night=%s respawnDays=%d"),
		*NestId.ToString(), *RegionId.ToString(),
		InCreatureClass ? *InCreatureClass->GetName() : TEXT("none"),
		InMinCreatures, InMaxCreatures, InSpawnRadius,
		bInCanSpawnDuringDay ? TEXT("true") : TEXT("false"),
		bInCanSpawnDuringNight ? TEXT("true") : TEXT("false"),
		InRespawnTimeIndays);
}
