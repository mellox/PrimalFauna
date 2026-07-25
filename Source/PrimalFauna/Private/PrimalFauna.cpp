#include "PrimalFauna.h"

DEFINE_LOG_CATEGORY(LogPrimalFauna);

void FPrimalFaunaModule::StartupModule()
{
    // Marker is a MONOTONIC BUILD ID, not a packet label (W2.2 review catch:
    // numbering by packet decremented it below the committed w23-1 and would
    // have made log forensics non-unique).
    UE_LOG(LogPrimalFauna, Display, TEXT("===== PrimalFauna BUILD 2026-07-25-w41-1 LOADED ====="));

    // Deliberately no SUBSCRIBE_METHOD / SUBSCRIBE_UOBJECT_METHOD hooks here.
    // PrimalFauna's entire gameplay surface lives in our own subclasses of
    // the creature framework, so there is nothing vanilla to hook in v1.
    // See docs/07-technical-architecture.md §1.
}

IMPLEMENT_GAME_MODULE(FPrimalFaunaModule, PrimalFauna);
