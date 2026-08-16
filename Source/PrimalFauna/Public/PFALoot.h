#pragma once

#include "CoreMinimal.h"
#include "FGRecipe.h"
#include "Resources/FGItemDescriptor.h"
#include "PFALoot.generated.h"

// Sicklejaw's species-specific remains item (docs/01 section 5, docs/07 section 6).
// Visuals are resolved after engine initialization; content loads in native CDO
// constructors are a proven boot-crash pattern on this toolchain.
UCLASS()
class PRIMALFAUNA_API UPFASicklejawRemainsDescriptor : public UFGItemDescriptor
{
    GENERATED_BODY()

public:
    UPFASicklejawRemainsDescriptor();

    // Loads the PrimalFauna-owned mesh and icons onto this native CDO after
    // content mount. Returns false (and describes the missing dependency) so
    // the world-module belt call can retry if content is not ready yet.
    static bool ResolveDeferredContent(FString& OutDetail);
};

// Mirrors the vanilla creature-remains constructor recipes: one remains in,
// one Alien Protein out, craftable in the Constructor and at a Work Bench.
UCLASS()
class PRIMALFAUNA_API UPFARecipe_ProteinSicklejaw : public UFGRecipe
{
    GENERATED_BODY()

public:
    UPFARecipe_ProteinSicklejaw();

    // Resolves the vanilla Alien Protein descriptor and places it on this
    // recipe's native CDO after content mount.
    static bool ResolveDeferredContent(FString& OutDetail);
};

namespace PFALoot
{
    // Idempotent post-engine-init content resolver. Called from both root
    // modules at CONSTRUCTION before SML's recipe registration phase.
    PRIMALFAUNA_API bool ResolveDeferredContent();
    PRIMALFAUNA_API bool IsDeferredContentReady();
}
