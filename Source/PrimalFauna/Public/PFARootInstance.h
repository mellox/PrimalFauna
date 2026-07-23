#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "PFARootInstance.generated.h"

// Root game instance module. SML discovers it automatically because
// bRootModule is true. Registers the mod configuration.
UCLASS()
class PRIMALFAUNA_API URootInstance_PrimalFauna : public UGameInstanceModule
{
    GENERATED_BODY()

public:
    URootInstance_PrimalFauna();
};
