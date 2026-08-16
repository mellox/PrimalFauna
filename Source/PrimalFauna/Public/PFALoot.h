#pragma once

#include "CoreMinimal.h"
#include "FGRecipe.h"
#include "FGSchematic.h"
#include "Resources/FGItemDescriptor.h"
#include "Unlocks/FGUnlockRecipe.h"
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

// Registration anchor for the Sicklejaw protein recipe. This mirrors the
// proven Laser Rifle / Energy Cell chain: root-world schematic registration
// discovers the recipe through a concrete UFGUnlockRecipe. EST_Custom keeps
// the anchor out of player-facing milestone and MAM progression.
UCLASS()
class PRIMALFAUNA_API UPFAUnlock_SicklejawProtein : public UFGUnlockRecipe
{
    GENERATED_BODY()

public:
    void AddRecipe(TSubclassOf<UFGRecipe> Recipe)
    {
        if (Recipe)
        {
            mRecipes.AddUnique(Recipe);
        }
    }
};

UCLASS()
class PRIMALFAUNA_API UPFASchematic_SicklejawLoot : public UFGSchematic
{
    GENERATED_BODY()

public:
    UPFASchematic_SicklejawLoot();
};

namespace PFALoot
{
    // Idempotent post-engine-init content resolver. Called from both root
    // modules at CONSTRUCTION before SML's recipe registration phase.
    PRIMALFAUNA_API bool ResolveDeferredContent();
    PRIMALFAUNA_API bool IsDeferredContentReady();
}
