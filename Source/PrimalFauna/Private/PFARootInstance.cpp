#include "PFARootInstance.h"
#include "PFAConfig.h"

URootInstance_PrimalFauna::URootInstance_PrimalFauna()
{
    bRootModule = true;
    ModConfigurations.Add(UPFAConfig::StaticClass());
}
