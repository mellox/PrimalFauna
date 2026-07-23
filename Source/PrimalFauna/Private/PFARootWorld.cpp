#include "PFARootWorld.h"
#include "PFASubsystem.h"

URootGameWorld_PrimalFauna::URootGameWorld_PrimalFauna()
{
    bRootModule = true;
    ModSubsystems.Add(APFASubsystem::StaticClass());
}
