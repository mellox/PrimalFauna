#include "PFASubsystem.h"
#include "PrimalFauna.h"
#include "PFAConfig.h"

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

    if (!bLoggedConfigSnapshot)
    {
        const FPFAConfigStruct Config = FPFAConfigStruct::GetActiveConfig(this);
        UE_LOG(LogPrimalFauna, Display,
            TEXT("[PFA] Config: Enabled=%s NestDensity=%.2f Diagnostics=%s"),
            Config.Enabled ? TEXT("true") : TEXT("false"),
            Config.NestDensity,
            Config.EnableDiagnostics ? TEXT("true") : TEXT("false"));
        bLoggedConfigSnapshot = true;
    }

    if (TickCount % HeartbeatEveryNTicks == 0)
    {
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Heartbeat #%d"), TickCount);
    }
}
