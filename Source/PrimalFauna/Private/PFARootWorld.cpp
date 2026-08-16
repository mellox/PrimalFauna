#include "PFARootWorld.h"
#include "PrimalFauna.h"
#include "PFALoot.h"
#include "PFASubsystem.h"

URootGameWorld_PrimalFauna::URootGameWorld_PrimalFauna()
{
    bRootModule = true;
    ModSubsystems.Add(APFASubsystem::StaticClass());

    // SML registers the custom schematic and discovers its recipe unlock at
    // world-module initialization. This is the same content-registration path
    // used by the proven Laser Rifle / Energy Cell recipes.
    mSchematics.Add(UPFASchematic_SicklejawLoot::StaticClass());
}

void URootGameWorld_PrimalFauna::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
    if (Phase == ELifecyclePhase::CONSTRUCTION)
    {
        // Belt call before SML initializes any registered content. The game-
        // instance module normally resolved this already; the function is
        // idempotent and retries if that earlier content load was premature.
        const bool bContentReady = PFALoot::ResolveDeferredContent();
        const int32 UnlockCount = UFGSchematic::GetUnlocks(
            UPFASchematic_SicklejawLoot::StaticClass()).Num();
        UE_LOG(LogPrimalFauna, Display,
            TEXT("[PFA] Loot registration anchor: schematic=%s recipe=%s unlocks=%d contentResolved=%s decision=stage outcome=%s"),
            *UPFASchematic_SicklejawLoot::StaticClass()->GetName(),
            *UPFARecipe_ProteinSicklejaw::StaticClass()->GetName(), UnlockCount,
            bContentReady ? TEXT("yes") : TEXT("no"),
            (bContentReady && UnlockCount == 1) ? TEXT("ready") : TEXT("FAILED"));
    }

    Super::DispatchLifecycleEvent(Phase);
}
