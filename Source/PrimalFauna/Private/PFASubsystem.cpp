#include "PFASubsystem.h"
#include "PrimalFauna.h"
#include "PFAConfig.h"
#include "PFACheats.h"
#include "PFABehaviorBorrower.h"

#include "BehaviorTree/BehaviorTree.h"
#include "TimerManager.h"

namespace
{
    constexpr float RefreshIntervalSeconds = 10.0f;
    constexpr int32 HeartbeatEveryNTicks = 6;
    constexpr int32 MaxBTBorrowAttempts = 5;
}

APFASubsystem::APFASubsystem()
{
    PrimaryActorTick.bCanEverTick = false;
}

void APFASubsystem::BeginPlay()
{
    Super::BeginPlay();

    // All PrimalFauna gameplay decisions are server-authoritative; clients
    // receive results via replication (doc 04 §2).
    if (!HasAuthority())
    {
        return;
    }

    // W1.4 (docs/07 §4): borrow a vanilla BehaviorTree onto our creatures' CDO,
    // server-side, post-mount. This subsystem is spawned by the world module
    // (PFARootWorld), so its BeginPlay is the post-mount hook the packet calls
    // for. Runtime AssetRegistry discovery because no vanilla hostile-BT path
    // was ever captured (docs/CENSUS.md). Idempotent across worlds. The returned
    // tree is held in mBorrowedBehaviorTree to keep it alive (cookbook #34); the
    // stats feed the BorrowedBTAssigned self-test.
    {
        const PFABehaviorBorrower::FResult BorrowResult = PFABehaviorBorrower::DiscoverAndBorrow(GetWorld());
        mBorrowedBehaviorTree = BorrowResult.ChosenTree;
        BTDiscoveredCount = BorrowResult.DiscoveredCount;
        BTSearchRoot = BorrowResult.SearchRoot;
        BTBorrowAttempts = 1;
    }

    GetWorldTimerManager().SetTimer(
        RefreshTimerHandle, this, &APFASubsystem::RefreshTick,
        RefreshIntervalSeconds, true, 2.0f);
}

void APFASubsystem::RefreshTick()
{
    ++TickCount;

    const FPFAConfigStruct Config = FPFAConfigStruct::GetActiveConfig(this);

    // Config-gated diagnostics (sml-gotchas.md §3): push the live config
    // value into the file-static gate every tick so PFADiag::IsEnabled()
    // reflects in-game config edits within one tick, matching the config
    // property's own "takes effect within ~10s, no reload" promise
    // (PFAConfig.cpp). Cheap -- one config-manager lookup per 10s.
    PFADiag::SetConfigDiagnostics(Config.EnableDiagnostics);

    if (!bLoggedConfigSnapshot)
    {
        UE_LOG(LogPrimalFauna, Display,
            TEXT("[PFA] Config: Enabled=%s NestDensity=%.2f Diagnostics=%s"),
            Config.Enabled ? TEXT("true") : TEXT("false"),
            Config.NestDensity,
            Config.EnableDiagnostics ? TEXT("true") : TEXT("false"));
        bLoggedConfigSnapshot = true;
    }

    // pfa.AutoTest=1 auto-runs the self-test registry once, on the first
    // tick after world load, entirely through this existing heartbeat --
    // no separate engine hook (docs/08 W1.1). Already server-only: BeginPlay
    // returns early for non-authority, so RefreshTick never fires on clients.
    if (!bRanAutoTest && PFADiag::IsAutoTestRequested())
    {
        bRanAutoTest = true;
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] pfa.AutoTest=1: running self-tests on first refresh tick."));
        PFA_RunSelfTests(GetWorld());
    }

    // W1.4 review W2: re-attempt the BT borrow if discovery raced a still-
    // scanning asset registry and saw ZERO assets (null tree with a non-zero
    // census = heuristic miss, retrying cannot help). Cap keeps a genuinely
    // empty registry from spamming forever.
    if (!mBorrowedBehaviorTree && BTDiscoveredCount == 0 && BTBorrowAttempts < MaxBTBorrowAttempts)
    {
        ++BTBorrowAttempts;
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] BT borrow retry %d/%d (previous discovery found 0 assets)"),
            BTBorrowAttempts, MaxBTBorrowAttempts);
        const PFABehaviorBorrower::FResult BorrowResult = PFABehaviorBorrower::DiscoverAndBorrow(GetWorld());
        mBorrowedBehaviorTree = BorrowResult.ChosenTree;
        BTDiscoveredCount = BorrowResult.DiscoveredCount;
        BTSearchRoot = BorrowResult.SearchRoot;
    }

    if (TickCount % HeartbeatEveryNTicks == 0)
    {
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Heartbeat #%d"), TickCount);
    }
}
