#include "PFALoot.h"

#include "PrimalFauna.h"
#include "PFACheats.h"
#include "Species/PFASicklejaw.h"

#include "FGCategory.h"
#include "FGRecipeManager.h"
#include "ItemAmount.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "UObject/SoftObjectPath.h"

namespace
{
    bool GPFADeferredLootReady = false;

    constexpr const TCHAR* HogRemainsDescriptorPath =
        TEXT("/Game/FactoryGame/Resource/Parts/AnimalParts/Desc_HogParts.Desc_HogParts_C");
    constexpr const TCHAR* OtherCategoryPath =
        TEXT("/Game/FactoryGame/Resource/ItemCategories/Cat_Other.Cat_Other_C");
    constexpr const TCHAR* AlienProteinDescriptorPath =
        TEXT("/Game/FactoryGame/Resource/Parts/AlienProtein/Desc_AlienProtein.Desc_AlienProtein_C");
    constexpr const TCHAR* ConstructorPath =
        TEXT("/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Build_ConstructorMk1.Build_ConstructorMk1_C");
    constexpr const TCHAR* WorkBenchPath =
        TEXT("/Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkBenchComponent.BP_WorkBenchComponent_C");

    void RootResolvedObject(UObject* Object)
    {
        if (Object && !Object->IsRooted())
        {
            Object->AddToRoot();
        }
    }
}

UPFASicklejawRemainsDescriptor::UPFASicklejawRemainsDescriptor()
{
    mUseDisplayNameAndDescription = true;
    mDisplayName = NSLOCTEXT("PrimalFauna", "SicklejawRemainsName", "Sicklejaw Remains");
    mDescription = NSLOCTEXT("PrimalFauna", "SicklejawRemainsDescription",
        "Dense remains harvested from a Sicklejaw. Can be processed into Alien Protein.");
    mAbbreviatedDisplayName = NSLOCTEXT("PrimalFauna", "SicklejawRemainsShortName", "Sicklejaw Remains");
    mStackSize = EStackSize::SS_SMALL;
    mCanBeDiscarded = true;
    mRememberPickUp = true;
    mForm = EResourceForm::RF_SOLID;
    mIsAlienItem = true;
    mMenuPriority = 0.0f;

    // Native descriptor CDOs do not receive the Blueprint PostLoad path that
    // normally computes this cache (cookbook gotcha 29).
    UpdateCachedStackSize();
}

bool UPFASicklejawRemainsDescriptor::ResolveDeferredContent(FString& OutDetail)
{
    UClass* HogRemainsClass = LoadClass<UFGItemDescriptor>(nullptr, HogRemainsDescriptorPath);
    UClass* CategoryClass = LoadClass<UFGCategory>(nullptr, OtherCategoryPath);
    if (!HogRemainsClass || !CategoryClass)
    {
        OutDetail = FString::Printf(TEXT("hog-remains=%s category=%s"),
            HogRemainsClass ? TEXT("ok") : TEXT("missing"),
            CategoryClass ? TEXT("ok") : TEXT("missing"));
        return false;
    }

    UTexture2D* SmallIcon = UFGItemDescriptor::GetSmallIcon(HogRemainsClass);
    UTexture2D* BigIcon = UFGItemDescriptor::GetBigIcon(HogRemainsClass);
    UStaticMesh* ConveyorMesh = UFGItemDescriptor::GetItemMesh(HogRemainsClass);
    if (!SmallIcon || !BigIcon || !ConveyorMesh)
    {
        OutDetail = FString::Printf(TEXT("hog visuals small=%s big=%s mesh=%s"),
            SmallIcon ? TEXT("ok") : TEXT("missing"),
            BigIcon ? TEXT("ok") : TEXT("missing"),
            ConveyorMesh ? TEXT("ok") : TEXT("missing"));
        return false;
    }

    UPFASicklejawRemainsDescriptor* CDO = GetMutableDefault<UPFASicklejawRemainsDescriptor>();
    CDO->mSmallIcon = SmallIcon;
    CDO->mPersistentBigIcon = BigIcon;
    CDO->mConveyorMesh = ConveyorMesh;
    CDO->mCategory = CategoryClass;

    // Assets assigned only to a native CDO are not sufficient GC roots on this
    // toolchain. Keep every deferred dependency alive for the process lifetime.
    RootResolvedObject(HogRemainsClass);
    RootResolvedObject(CategoryClass);
    RootResolvedObject(SmallIcon);
    RootResolvedObject(BigIcon);
    RootResolvedObject(ConveyorMesh);

    OutDetail = FString::Printf(TEXT("icon=%s mesh=%s category=%s"),
        *SmallIcon->GetName(), *ConveyorMesh->GetName(), *CategoryClass->GetName());
    return true;
}

UPFARecipe_ProteinSicklejaw::UPFARecipe_ProteinSicklejaw()
{
    mDisplayNameOverride = true;
    mDisplayName = NSLOCTEXT("PrimalFauna", "ProteinSicklejawRecipeName", "Alien Protein (Sicklejaw Remains)");
    mIngredients.Add(FItemAmount(UPFASicklejawRemainsDescriptor::StaticClass(), 1));
    mManufactoringDuration = 3.0f;
    mManualManufacturingMultiplier = 1.0f;
    mManufacturingMenuPriority = 0.0f;

    // Soft paths are data only: no content load occurs in this constructor.
    // These are the same two producers referenced by Recipe_Protein_Hog.
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(ConstructorPath)));
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(WorkBenchPath)));
}

bool UPFARecipe_ProteinSicklejaw::ResolveDeferredContent(FString& OutDetail)
{
    UClass* AlienProteinClass = LoadClass<UFGItemDescriptor>(nullptr, AlienProteinDescriptorPath);
    if (!AlienProteinClass)
    {
        OutDetail = TEXT("Alien Protein descriptor missing");
        return false;
    }

    UPFARecipe_ProteinSicklejaw* CDO = GetMutableDefault<UPFARecipe_ProteinSicklejaw>();
    CDO->mProduct.Reset();
    CDO->mProduct.Add(FItemAmount(AlienProteinClass, 1));
    RootResolvedObject(AlienProteinClass);

    OutDetail = FString::Printf(TEXT("product=%s amount=1 producers=%d"),
        *AlienProteinClass->GetName(), CDO->mProducedIn.Num());
    return true;
}

namespace PFALoot
{
    bool ResolveDeferredContent()
    {
        if (GPFADeferredLootReady)
        {
            return true;
        }

        FString DescriptorDetail;
        FString RecipeDetail;
        const bool bDescriptorReady = UPFASicklejawRemainsDescriptor::ResolveDeferredContent(DescriptorDetail);
        const bool bRecipeReady = UPFARecipe_ProteinSicklejaw::ResolveDeferredContent(RecipeDetail);
        GPFADeferredLootReady = bDescriptorReady && bRecipeReady;

        if (GPFADeferredLootReady)
        {
            UE_LOG(LogPrimalFauna, Display,
                TEXT("[PFA] Loot content resolve: descriptor=ready (%s) recipe=ready (%s) outcome=ready"),
                *DescriptorDetail, *RecipeDetail);
        }
        else
        {
            UE_LOG(LogPrimalFauna, Error,
                TEXT("[PFA] Loot content resolve: descriptor=%s (%s) recipe=%s (%s) outcome=retry-required"),
                bDescriptorReady ? TEXT("ready") : TEXT("failed"), *DescriptorDetail,
                bRecipeReady ? TEXT("ready") : TEXT("failed"), *RecipeDetail);
        }
        return GPFADeferredLootReady;
    }

    bool IsDeferredContentReady()
    {
        return GPFADeferredLootReady;
    }
}

// Static wiring + live availability. The death roundtrip itself remains an
// in-game test because a self-test must not kill a player's creature or spawn
// persistent world loot merely to prove registration.
namespace
{
    const bool bRegistered_SicklejawLootWiring = []
    {
        RegisterPFASelfTest({TEXT("SicklejawLootWiring"),
            [](UWorld* World, FString& OutDetail) -> bool
            {
                const APFASicklejaw* SicklejawCDO = GetDefault<APFASicklejaw>();
                const TArray<FItemAmount> Ingredients = UFGRecipe::GetIngredients(World, UPFARecipe_ProteinSicklejaw::StaticClass());
                const TArray<FItemAmount> Products = UFGRecipe::GetProducts(UPFARecipe_ProteinSicklejaw::StaticClass());
                const TArray<TSubclassOf<UObject>> Producers =
                    UFGRecipe::GetProducedIn(UPFARecipe_ProteinSicklejaw::StaticClass());
                AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);

                const bool bCreatureWired = SicklejawCDO &&
                    SicklejawCDO->GetDeathLootDescriptor() == UPFASicklejawRemainsDescriptor::StaticClass();
                const bool bIngredientWired = Ingredients.Num() == 1 &&
                    Ingredients[0].ItemClass == UPFASicklejawRemainsDescriptor::StaticClass() &&
                    Ingredients[0].Amount == 1;
                const bool bProductWired = Products.Num() == 1 && Products[0].ItemClass &&
                    Products[0].ItemClass->GetName().Contains(TEXT("Desc_AlienProtein")) && Products[0].Amount == 1;
                const bool bConstructorProducer = Producers.ContainsByPredicate(
                    [](const TSubclassOf<UObject>& Producer)
                    {
                        return Producer && Producer->GetPathName() == ConstructorPath;
                    });
                const bool bWorkBenchProducer = Producers.ContainsByPredicate(
                    [](const TSubclassOf<UObject>& Producer)
                    {
                        return Producer && Producer->GetPathName() == WorkBenchPath;
                    });
                const bool bProducerWiring = Producers.Num() == 2 && bConstructorProducer && bWorkBenchProducer;
                const bool bTimingWired =
                    FMath::IsNearlyEqual(
                        UFGRecipe::GetManufacturingDuration(UPFARecipe_ProteinSicklejaw::StaticClass()), 3.0f) &&
                    FMath::IsNearlyEqual(
                        UFGRecipe::GetManualManufacturingDuration(UPFARecipe_ProteinSicklejaw::StaticClass()), 3.0f);
                const bool bAvailable = RecipeManager &&
                    RecipeManager->IsRecipeAvailable(UPFARecipe_ProteinSicklejaw::StaticClass());
                const bool bPass = PFALoot::IsDeferredContentReady() && bCreatureWired &&
                    bIngredientWired && bProductWired && bProducerWiring && bTimingWired && bAvailable;

                OutDetail = FString::Printf(
                    TEXT("resolved=%s creature=%s ingredient=%s product=%s producers=%s timing=%s available=%s"),
                    PFALoot::IsDeferredContentReady() ? TEXT("yes") : TEXT("no"),
                    bCreatureWired ? TEXT("yes") : TEXT("no"),
                    bIngredientWired ? TEXT("1 remains") : TEXT("bad"),
                    bProductWired ? TEXT("1 protein") : TEXT("bad"),
                    bProducerWiring ? TEXT("constructor+workbench") : TEXT("bad"),
                    bTimingWired ? TEXT("3s") : TEXT("bad"),
                    bAvailable ? TEXT("yes") : TEXT("no"));
                return bPass;
            }});
        return true;
    }();
}
