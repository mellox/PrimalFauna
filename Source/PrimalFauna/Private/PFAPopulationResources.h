#pragma once
#include "CoreMinimal.h"

class UWorld;
class AFGResourceNode;
struct FPFAPopulationResource
{
    FString Id;
    FVector Location = FVector::ZeroVector;
    bool bEligible = false;
    TWeakObjectPtr<AFGResourceNode> Node;
};

// Read-only optional integration. False means the installed provider is not ready
// or changed schema: suspend guards, never silently substitute vanilla originals.
bool PFAReadPopulationResources(UWorld* World, TArray<FPFAPopulationResource>& Out);
