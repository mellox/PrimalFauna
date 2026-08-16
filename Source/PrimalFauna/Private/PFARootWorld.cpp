#include "PFARootWorld.h"
#include "PrimalFauna.h"
#include "PFALoot.h"
#include "PFASubsystem.h"

#include "Registry/ModContentRegistry.h"

URootGameWorld_PrimalFauna::URootGameWorld_PrimalFauna()
{
    bRootModule = true;
    ModSubsystems.Add(APFASubsystem::StaticClass());
}

void URootGameWorld_PrimalFauna::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
    if (Phase == ELifecyclePhase::CONSTRUCTION)
    {
        // Belt call before SML initializes any registered content. The game-
        // instance module normally resolved this already; the function is
        // idempotent and retries if that earlier content load was premature.
        PFALoot::ResolveDeferredContent();
    }
    else if (Phase == ELifecyclePhase::INITIALIZATION)
    {
        // Direct registration is one of docs/01 section 5's two approved
        // loading anchors and avoids exposing a meaningless player schematic.
        UModContentRegistry* Registry = UModContentRegistry::Get(GetWorld());
        if (Registry && PFALoot::IsDeferredContentReady())
        {
            Registry->RegisterRecipe(GetOwnerModReference(), UPFARecipe_ProteinSicklejaw::StaticClass());
            UE_LOG(LogPrimalFauna, Display,
                TEXT("[PFA] Loot registration: recipe=%s mod=%s decision=register outcome=submitted"),
                *UPFARecipe_ProteinSicklejaw::StaticClass()->GetName(), *GetOwnerModReference().ToString());
        }
        else
        {
            UE_LOG(LogPrimalFauna, Error,
                TEXT("[PFA] Loot registration: registry=%s contentResolved=%s decision=register outcome=FAILED"),
                Registry ? TEXT("ready") : TEXT("null"),
                PFALoot::IsDeferredContentReady() ? TEXT("yes") : TEXT("no"));
        }
    }

    Super::DispatchLifecycleEvent(Phase);
}
