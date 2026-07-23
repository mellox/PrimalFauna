#pragma once

#include "CoreMinimal.h"
#include "Creature/FGCreatureSpawner.h"
#include "PFANestSpawner.generated.h"

// ---------------------------------------------------------------------------
// APFANestSpawner -- a real AFGCreatureSpawner subclass for PrimalFauna
// territory nests (docs/03-spawning-and-territories.md §1, docs/06 S1-S4).
//
// Exists for exactly two things:
//   1. Identity -- IsA<APFANestSpawner>() cleanly separates our nests from
//      vanilla spawners for APFASubsystem's adopt/materialize registry scan,
//      without touching a single vanilla spawner anywhere.
//   2. Two SaveGame id fields (NestId/RegionId) the registry uses to
//      re-associate a live spawner with its record across saves (doc 03 §3
//      step 1 "adopt").
//
// Deliberately overrides NOTHING from AFGCreatureSpawner -- no BeginPlay,
// Tick, or IFGSaveInterface hook. Doc 03 §1 is explicit: this subclass "must
// NOT change spawn mechanics" so HostileScaling's generic
// TActorIterator<AFGCreatureSpawner> treatment (doc 06 §3) and radar/scanner
// listing (FGBuildableRadarTower.h:113) stay byte-for-byte identical to a
// vanilla spawner. If a future packet needs to override something here, that
// is an architecture deviation for the orchestrator to decide, not a default.
// ---------------------------------------------------------------------------
UCLASS()
class PRIMALFAUNA_API APFANestSpawner : public AFGCreatureSpawner
{
	GENERATED_BODY()

public:
	APFANestSpawner();

	// Registry identity. Set exactly once by ConfigureNest. Public because
	// APFASubsystem's adopt scan reads these directly off every live
	// TActorIterator<APFANestSpawner> hit -- no access problem, these are OUR
	// OWN class's fields, not inherited AFGCreatureSpawner ones.
	UPROPERTY(SaveGame, VisibleInstanceOnly, Category = "PrimalFauna")
	FName NestId;

	UPROPERTY(SaveGame, VisibleInstanceOnly, Category = "PrimalFauna")
	FName RegionId;

	// Write-once configuration (doc 06 S2: "write mSpawnData/mNumberOfCreatures/
	// mRespawnTimeIndays ONCE at nest creation ... then never rewrite counts").
	// Self-accesses AFGCreatureSpawner's PROTECTED fields (mCreatureClass,
	// mNumberOfCreatures, mCanSpawnDuringDay/Night, mSpawnRadius -- all
	// confirmed protected in FGCreatureSpawner.h:196-217, reachable via plain
	// C++ inheritance from this subclass, NO AccessTransformer added) plus the
	// PUBLIC mRespawnTimeIndays (FGCreatureSpawner.h:263 -- public despite
	// living amongst otherwise-private members near the bottom of the class;
	// verified against the real header, not assumed protected).
	//
	// MUST be called on a DEFERRED spawn (SpawnActor with bDeferConstruction,
	// see APFASubsystem::ReconcileNests) BEFORE FinishSpawning() runs BeginPlay.
	// AFGCreatureSpawner::BeginPlay() is closed-source (FGCreatureSpawner.cpp is
	// an SML link stub), so whether it reads mCreatureClass/mNumberOfCreatures
	// at BeginPlay time is unverifiable statically -- configuring before
	// BeginPlay ever runs is the only ordering that is correct regardless of
	// what BeginPlay turns out to do, matching the guarantee a level-placed
	// spawner gets from having its properties already serialized before its
	// own BeginPlay.
	//
	// A second call is refused and logged as an S2 contract violation instead
	// of silently re-applying -- ongoing count scaling belongs to
	// HostileScaling alone (doc 06 S2/S3).
	void ConfigureNest(FName InNestId, FName InRegionId, TSubclassOf<class AFGCreature> InCreatureClass,
		int32 InMinCreatures, int32 InMaxCreatures, float InSpawnRadius,
		bool bInCanSpawnDuringDay, bool bInCanSpawnDuringNight, int32 InRespawnTimeIndays);

private:
	// Write-once guard for ConfigureNest. Not exposed -- internal bookkeeping
	// only (doc 06 S2 enforcement), not a general "is this nest ready" query.
	bool bConfigured = false;
};
