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
// Does not override spawn mechanics (BeginPlay/Tick/SpawnCreatures). It does
// mirror the base spawner's non-SaveGame authoring fields through the save
// interface. Runtime-spawned actors have no level archetype to restore those
// fields from, so without the mirror their saved mSpawnData returns but its
// creature class/day-night/radius/respawn configuration resets to the CDO.
// HostileScaling's generic
// TActorIterator<AFGCreatureSpawner> treatment (doc 06 §3) and radar/scanner
// listing (FGBuildableRadarTower.h:113) stay byte-for-byte identical to a
// vanilla spawner.
// ---------------------------------------------------------------------------
UCLASS()
class PRIMALFAUNA_API APFANestSpawner : public AFGCreatureSpawner
{
	GENERATED_BODY()

public:
	APFANestSpawner();

	// AFGCreatureSpawner's spawn-data/kill records are SaveGame, but the
	// runtime authoring fields they depend on are not. Capture their exact
	// authored values once and restore them before the base PostLoad handler
	// recouples creatures. Session-transient HostileScaling adjustments must
	// not leak into a later session when that mod is disabled or absent.
	virtual void PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
	virtual void PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion) override;

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

	// One-shot adoption seam. New saves already restored their snapshot before
	// the base PostLoad hook. A pre-fix save has no snapshot, so this rehydrates
	// only the missing base configuration from the registry while preserving
	// any saved mSpawnData/kill state and existing effective slot count.
	void EnsureRuntimeConfigurationAfterAdoption(TSubclassOf<class AFGCreature> InCreatureClass,
		int32 InMinCreatures, int32 InMaxCreatures, float InSpawnRadius,
		bool bInCanSpawnDuringDay, bool bInCanSpawnDuringNight, int32 InRespawnTimeIndays);

	bool HasRuntimeConfigurationSnapshot() const { return bHasRuntimeConfigSnapshot; }

	// Diagnostics-only, read-only view into inherited spawn state. This must not
	// call IsReadyToSpawn(): the native function can populate slots, trace for a
	// nearby base, and advance native timing. Reconcile invokes this only behind
	// pfa.Diag.
	void LogSpawnDecisionDiagnostics(const class APawn* PlayerPawn);

private:
	// Write-once guard for ConfigureNest. Not exposed -- internal bookkeeping
	// only (doc 06 S2 enforcement), not a general "is this nest ready" query.
	bool bConfigured = false;

	// The inherited fields below are EditAnywhere but not SaveGame because
	// vanilla map spawners recover them from their placed archetype. Our nests
	// are runtime actors, so their authored values need an explicit mirror.
	// This intentionally excludes session-transient modifications made by
	// HostileScaling (notably respawn time and starvation-widened radius).
	UPROPERTY(SaveGame)
	bool bHasRuntimeConfigSnapshot = false;

	UPROPERTY(SaveGame)
	TSubclassOf<class AFGCreature> SavedCreatureClass;

	UPROPERTY(SaveGame)
	FInt32Interval SavedNumberOfCreatures;

	UPROPERTY(SaveGame)
	bool bSavedCanSpawnDuringDay = true;

	UPROPERTY(SaveGame)
	bool bSavedCanSpawnDuringNight = true;

	UPROPERTY(SaveGame)
	float SavedSpawnRadius = 0.0f;

	UPROPERTY(SaveGame)
	int32 SavedRespawnTimeInDays = -1;

	// Per-instance, per-load one-shot guard. Reconcile runs every ten seconds;
	// adoption recovery must not become a second spawn loop.
	bool bRuntimeAdoptionChecked = false;

	bool bHasSpawnDiagnosticSignature = false;
	uint32 LastSpawnDiagnosticSignature = 0;
	double LastSpawnDiagnosticTimeSeconds = -1.0e30;

	void CaptureRuntimeConfigurationSnapshot();
	void RestoreRuntimeConfigurationSnapshot();
};
