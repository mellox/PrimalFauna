#include "PFARootInstance.h"
#include "PFAConfig.h"
#include "PFALoot.h"

URootInstance_PrimalFauna::URootInstance_PrimalFauna()
{
    bRootModule = true;
    ModConfigurations.Add(UPFAConfig::StaticClass());
}

void URootInstance_PrimalFauna::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
    if (Phase == ELifecyclePhase::CONSTRUCTION)
    {
        PFALoot::ResolveDeferredContent();
    }
    Super::DispatchLifecycleEvent(Phase);
}
