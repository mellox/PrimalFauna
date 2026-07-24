#include "PrimalFauna.h"

DEFINE_LOG_CATEGORY(LogPrimalFauna);

void FPrimalFaunaModule::StartupModule()
{
    UE_LOG(LogPrimalFauna, Display, TEXT("===== PrimalFauna BUILD 2026-07-24-w18-7 LOADED ====="));

    // Deliberately no SUBSCRIBE_METHOD / SUBSCRIBE_UOBJECT_METHOD hooks here.
    // PrimalFauna's entire gameplay surface lives in our own subclasses of
    // the creature framework, so there is nothing vanilla to hook in v1.
    // See docs/07-technical-architecture.md §1.
}

IMPLEMENT_GAME_MODULE(FPrimalFaunaModule, PrimalFauna);
