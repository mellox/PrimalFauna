#include "PrimalFauna.h"

DEFINE_LOG_CATEGORY(LogPrimalFauna);

void PFAInstallPopulationSpawnGuards();

void FPrimalFaunaModule::StartupModule()
{
    // Marker is a MONOTONIC BUILD ID, not a packet label (W2.2 review catch:
    // numbering by packet decremented it below the committed w23-1 and would
    // have made log forensics non-unique).
	UE_LOG(LogPrimalFauna, Display, TEXT("===== PrimalFauna BUILD 2026-10-24-w60-2 LOADED ====="));

    PFAInstallPopulationSpawnGuards();
}

IMPLEMENT_GAME_MODULE(FPrimalFaunaModule, PrimalFauna);
