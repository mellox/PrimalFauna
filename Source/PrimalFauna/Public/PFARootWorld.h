#pragma once

#include "CoreMinimal.h"
#include "Module/GameWorldModule.h"
#include "PFARootWorld.generated.h"

// Root game world module. Spawns the PrimalFauna subsystem into each game world.
UCLASS()
class PRIMALFAUNA_API URootGameWorld_PrimalFauna : public UGameWorldModule
{
    GENERATED_BODY()

public:
    URootGameWorld_PrimalFauna();
};
