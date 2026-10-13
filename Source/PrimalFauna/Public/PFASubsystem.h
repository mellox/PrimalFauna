#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "FGSaveInterface.h"
#include "UObject/SoftObjectPath.h"
#include "PFASubsystem.generated.h"

// Doc 03 §3 nest registry record / doc 06 S1-S4. One entry per territory
// nest, independent of whether a live APFANestSpawner currently backs it --
// the registry is the durable truth; APFASubsystem::ReconcileNests() adopts
// or materializes a live spawner to match it every refresh tick. FSoftClassPath
// (not TSubclassOf<AFGCreature>) so this struct carries no hard class
// reference -- resolved via FSoftClassPath::TryLoadClass<AFGCreature>() only
// at materialization time (PFASubsystem.cpp), matching the pattern content-and-
// assets.md recommends for save-stable species references.
USTRUCT()
struct FPFANestRecord
{
    GENERATED_BODY()

    UPROPERTY(SaveGame)
    FName NestId;

    UPROPERTY(SaveGame)
    FName RegionId;

    UPROPERTY(SaveGame)
    FVector Location = FVector::ZeroVector;

    UPROPERTY(SaveGame)
    FSoftClassPath SpeciesClass;

    UPROPERTY(SaveGame)
    int32 CreaturesMin = 1;

    UPROPERTY(SaveGame)
    int32 CreaturesMax = 1;

    UPROPERTY(SaveGame)
    float SpawnRadius = 1000.0f;

    UPROPERTY(SaveGame)
    int32 RespawnDays = 1;

    // Dormant world slots retain their native actor and kill records. Population
    // reconciliation stops future spawning and retires creatures only when safe.
    UPROPERTY(SaveGame)
    bool bDormant = false;

    // World slots have stable ids across config changes and both kinds of reroll.
    // Test-region records from older saves keep their original lifecycle.
    UPROPERTY(SaveGame) bool bWorldPopulation = false;
    UPROPERTY(SaveGame) int32 PopulationSlot = 0;
    UPROPERTY(SaveGame) int32 CandidateAttempt = 0;
    UPROPERTY(SaveGame) int32 LayoutRevision = -1;
    UPROPERTY(SaveGame) bool bHasPlacement = false;
    UPROPERTY(SaveGame) bool bResourceGuard = false;
    UPROPERTY(SaveGame) FString ResourceSiteId;
    UPROPERTY(SaveGame) FVector ResourceSiteLocation = FVector::ZeroVector;
    UPROPERTY(SaveGame) bool bMaterializedEver = false;
};

// Server-side subsystem for the Primal Fauna mod. W0.1 shipped the skeleton
// (save-interface stub + 10s heartbeat/config-echo timer). W1.5 adds the
// nest registry + adopt/materialize reconciliation described in
// docs/03-spawning-and-territories.md §3 and docs/06-integration-and-compat.md
// §3 (contract items S1-S4). Census and worm director state described in
// docs/07-technical-architecture.md §2 remain later packets.
UCLASS()
class PRIMALFAUNA_API APFASubsystem : public AModSubsystem, public IFGSaveInterface
{
    GENERATED_BODY()

public:
    APFASubsystem();

    // Cross-translation-unit accessor for the SubsystemAlive self-test
    // (PFACheats.cpp), which iterates the world for this actor and needs to
    // read its heartbeat without a Friend/AccessTransformer grant.
    FORCEINLINE int32 GetTickCount() const { return TickCount; }

    // W1.4 read-backs for the BorrowedBTAssigned self-test
    // (PFABehaviorBorrower.cpp): the behavior-borrow discovery stats captured in
    // BeginPlay, so a FAIL can report WHY (no BTs vs. no heuristic match).
    FORCEINLINE int32 GetBTDiscoveredCount() const { return BTDiscoveredCount; }
    FORCEINLINE const FString& GetBTSearchRoot() const { return BTSearchRoot; }

    // W1.7: the Section-B re-test proved CDO->instance propagation of the
    // borrowed BT does NOT happen through the closed-source spawn path
    // (GetBehaviorTreeAsset() null on EVERY instance, even ones spawned
    // minutes after a verified CDO write). APFACreatureController::OnPossess
    // falls back to this rooted reference and writes it onto the INSTANCE.
    FORCEINLINE class UBehaviorTree* GetBorrowedBehaviorTree() const { return mBorrowedBehaviorTree; }

    // IFGSaveInterface. NestRegistry (below) is SaveGame as of W1.5, so
    // ShouldSave's "true" now actually matters; every hook besides
    // PostLoadGame_Implementation is still an empty stub -- W1.5 has nothing
    // that needs Pre/PostSaveGame or PreLoadGame specifically (the registry
    // array serializes on its own via ordinary SaveGame reflection).
    virtual bool ShouldSave_Implementation() const override { return true; }
    virtual bool NeedTransform_Implementation() override { return false; }
    virtual void PreSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PostSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PreLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override {}

    // Doc 03 §4 [assumption -> slice test] instrumentation: logs
    // "[PFA] Nest persistence: records=<n> preexisting-live=<m>" -- the raw
    // evidence the Phase-1 gate reads to verdict "dynamic APFANestSpawners
    // persist natively" vs "fall back to the transient model" (doc 03 §4).
    // See PFASubsystem.cpp for what "preexisting-live" can and cannot prove.
    virtual void PostLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override;

    virtual void GatherDependencies_Implementation(TArray<UObject*>& out_dependentObjects) override {}

    // ---- Nest registry public API (docs/03 §3, docs/06 S1-S4) ----

    // Server-only. Ground-projects the local player's location (simple
    // downward line trace -- no water/candidate sampling, that is W2.1's
    // PFA.DumpCandidates), appends ONE new record to NestRegistry with
    // RegionId "Test" and the next free index for that region, then triggers
    // an immediate ReconcileNests() so PFA.SpawnNest (PFACheats.cpp) doesn't
    // make the tester wait for the next 10s tick. Deliberately reuses the
    // SAME reconciliation RefreshTick calls every 10s -- doc 03 §3 is
    // explicit that materialization must be ONE code path, not a direct-spawn
    // special case. Returns false (OutDetail explains why) if there is no
    // world/player pawn to spawn near, or if this instance has no authority.
    bool SpawnNestAtPlayer(FString& OutDetail);

    // Read-only registry consistency check for the NestRegistryConsistent
    // self-test (PFASubsystem.cpp): every record should resolve to either a
    // live matching APFANestSpawner or the dormant
    // flag, and every live APFANestSpawner found in the world should match
    // some record. Never mutates; safe to call at any tick, including before
    // the first reconcile has ever run (0 records is a trivial PASS).
    bool CheckNestRegistryConsistency(FString& OutDetail) const;

protected:
    virtual void BeginPlay() override;

private:
    // Named RefreshTick, never "Tick" -- AActor::Tick() is a real virtual,
    // and a same-named member here would clang name-hide it instead of
    // overriding it (sml-gotchas.md, world-modification.md §7).
    void RefreshTick();

    // Existing saves must receive the directly-registered remains recipe too.
    // Called from RefreshTick until the replicated recipe manager confirms it.
    bool EnsureLootRecipeAvailable();

    FTimerHandle RefreshTimerHandle;

    // Ticks since BeginPlay; also doubles as the heartbeat log's "#N" so the
    // log directly shows elapsed uptime (TickCount * 10s).
    int32 TickCount = 0;

    // Logged once on the first RefreshTick so the config the server is
    // actually running with is visible from a single log read (doc 04 §2:
    // MP diagnosability -- only the server's config affects gameplay).
    bool bLoggedConfigSnapshot = false;

    bool bLootRecipeReady = false;
    int32 LootRecipeReadyAttempts = 0;

    // W2.3: last SicklejawAggression value pushed to live creatures; -1 =
    // never pushed, so the first RefreshTick always applies the persisted
    // config once (new spawns self-stamp in APFACreatureBase::BeginPlay).
    int32 LastPushedAggression = -1;

    // Set once the pfa.AutoTest self-test run has fired, so it runs at most
    // once per world (docs/08 W1.1: "hook via the subsystem's first
    // RefreshTick, NOT a new engine hook").
    bool bRanAutoTest = false;

    // w26-2 (review finding): the one-shot above fires on the FIRST refresh
    // tick, when no creature has spawned/posed/acquired a target yet -- so
    // the w26-1 SPATIAL tests (FeetPlanted / FacingTarget) always took their
    // NO-COVERAGE branch in unattended runs, making them decorative. This
    // second one-shot re-runs the suite the first time a live creature
    // actually exists, which is when those tests can assert anything.
    bool bRanAutoTestWithCreatures = false;

    // W1.4 (docs/07 §4): GC root for the borrowed vanilla BehaviorTree that
    // PFABehaviorBorrower assigns onto the Sicklejaw CDO in BeginPlay. An asset
    // referenced only from a CDO is invisible to GC (cookbook gotcha #34); this
    // UPROPERTY on the live, world-rooted subsystem keeps it alive. Transient --
    // re-discovered every load, never serialized to the save.
    UPROPERTY(Transient)
    TObjectPtr<class UBehaviorTree> mBorrowedBehaviorTree = nullptr;

    // Discovery stats cached for the BorrowedBTAssigned self-test.
    int32 BTDiscoveredCount = -1;
    FString BTSearchRoot;

    // W1.4 review W2: retry counter for the asset-registry race (a scan still in
    // progress at BeginPlay can return ZERO assets; RefreshTick re-attempts up
    // to the cap instead of idling the species all session). Retries only fire
    // on DiscoveredCount == 0 -- a heuristic miss over a full census will not
    // improve by retrying.
    int32 BTBorrowAttempts = 0;

    // ---- W1.5 nest registry (docs/03 §3, docs/06 S1-S4) ----

    // The durable truth. RefreshTick (every 10s) and SpawnNestAtPlayer (on
    // demand, immediately) both funnel through the SAME ReconcileNests() to
    // adopt-or-materialize a live APFANestSpawner per record -- see that
    // function for why there is deliberately only one path.
    UPROPERTY(SaveGame)
    TArray<FPFANestRecord> NestRegistry;

    // Doc 03 §3: scans the world for live APFANestSpawners, adopts any that
    // match a record's NestId, and materializes (SpawnActor, deterministic
    // name, ConfigureNest, RegisterAsNavigationInvoker, PopulateSpawnData) any
    // record with no live match. Never destroys a spawner (S4 is out of scope
    // for W1.5 -- no dormancy/deactivation logic beyond the bDormant flag
    // existing). HasAuthority()-guarded even though both call sites already
    // are, for the same "no-op safety belt" reason PFA_RunSelfTests documents.
    void ReconcileNests();
    void RefreshWorldPopulation();
    void LogPopulationDecision(const FPFANestRecord& Record, class APFANestSpawner* Spawner, const TCHAR* Reason);
    TMap<FName, FString> PopulationLastDecisions;
    bool IsPopulationProtected(const FVector& Point, float ExtraRadius = 0.0f) const;
    FName PopulationBiomeAt(const FVector& Point) const;
    bool ValidatePopulationGround(const FVector& Requested, FVector& OutGround) const;
    void RefreshPopulationPatrol(class APFANestSpawner* Spawner, const FPFANestRecord& Record,
        const struct FPFAConfigStruct& Config);

    UPROPERTY(SaveGame) int32 PopulationSeed = 0;
    UPROPERTY(SaveGame) TArray<FVector> PopulationStarterLocations;
    UPROPERTY(Transient) TObjectPtr<class UFGMapAreaTexture> PopulationMap = nullptr;
    TMap<FName, TArray<FVector>> PopulationBiomeCells;
    TArray<FBox> PopulationFactoryBounds;
    TArray<FVector> PopulationPlayerLocations;
    FString LastPopulationConfigWarning;
    FString LastPopulationState;
    bool bPopulationMapReady = false;
    int32 PopulationMapCursor = 0;

    // Set after ReconcileNests() has run at least once, so the
    // "[PFA] Nests: ..." census line logs unconditionally that first time
    // (doc 08 packet spec: "Display, once per change + on load") even when
    // there happen to be zero adopted/materialized changes that pass (e.g. an
    // empty registry on a fresh save).
    bool bHasReconciledNestsOnce = false;

    // ---- w58-3 nest-drive census bookkeeping (see ReconcileNests) ----
    // Pass counter for the "[PFA] NestDrive" line; increments once per
    // ReconcileNests call, so PFA.SpawnNest's immediate reconcile advances it
    // too and a gap in the sequence is real, not a dropped log line.
    int32 NestDrivePassCount = 0;

    // Rate limiting for that census line -- same shape as the per-nest
    // diagnostic's dedup: print on change, on any drive, or once a minute.
    bool bHasNestDriveCensusSignature = false;
    uint32 LastNestDriveCensusSignature = 0;
    double LastNestDriveCensusTimeSeconds = -1.0e30;
};
