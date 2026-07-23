#include "PFASubsystem.h"
#include "PrimalFauna.h"
#include "PFAConfig.h"
#include "PFACheats.h"

#include "TimerManager.h"

namespace
{
    constexpr float RefreshIntervalSeconds = 10.0f;
    constexpr int32 HeartbeatEveryNTicks = 6;
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

    if (TickCount % HeartbeatEveryNTicks == 0)
    {
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Heartbeat #%d"), TickCount);
    }
}
