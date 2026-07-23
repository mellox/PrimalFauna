#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "FGSaveInterface.h"
#include "PFASubsystem.generated.h"

// Server-side subsystem for the Primal Fauna mod. This packet (W0.1) only
// ships the skeleton: a save-interface stub and a 10s heartbeat/config-echo
// timer, so the mod's presence and config wiring are verifiable from the
// log alone before any creature gameplay exists. Later packets add the
// nest registry, census, and worm director state described in
// docs/07-technical-architecture.md §2.
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

    // IFGSaveInterface -- no savegame data yet in this packet, so every
    // hook is an empty stub; ShouldSave still returns true so the actor is
    // already registered with the save system once fields are added later.
    virtual bool ShouldSave_Implementation() const override { return true; }
    virtual bool NeedTransform_Implementation() override { return false; }
    virtual void PreSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PostSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PreLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PostLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void GatherDependencies_Implementation(TArray<UObject*>& out_dependentObjects) override {}

protected:
    virtual void BeginPlay() override;

private:
    // Named RefreshTick, never "Tick" -- AActor::Tick() is a real virtual,
    // and a same-named member here would clang name-hide it instead of
    // overriding it (sml-gotchas.md, world-modification.md §7).
    void RefreshTick();

    FTimerHandle RefreshTimerHandle;

    // Ticks since BeginPlay; also doubles as the heartbeat log's "#N" so the
    // log directly shows elapsed uptime (TickCount * 10s).
    int32 TickCount = 0;

    // Logged once on the first RefreshTick so the config the server is
    // actually running with is visible from a single log read (doc 04 §2:
    // MP diagnosability -- only the server's config affects gameplay).
    bool bLoggedConfigSnapshot = false;

    // Set once the pfa.AutoTest self-test run has fired, so it runs at most
    // once per world (docs/08 W1.1: "hook via the subsystem's first
    // RefreshTick, NOT a new engine hook").
    bool bRanAutoTest = false;

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
};
