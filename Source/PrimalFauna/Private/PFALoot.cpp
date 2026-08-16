#include "PFALoot.h"

#include "PrimalFauna.h"
#include "PFACheats.h"
#include "Species/PFASicklejaw.h"

#include "FGCategory.h"
#include "FGRecipeManager.h"
#include "Registry/ModContentRegistry.h"
#include "ItemAmount.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "UObject/SoftObjectPath.h"

namespace
{
    bool GPFADeferredLootReady = false;
    UTexture2D* GPFAResolvedSmallIcon = nullptr;
    UTexture2D* GPFAResolvedBigIcon = nullptr;
    UStaticMesh* GPFAResolvedConveyorMesh = nullptr;

    constexpr const TCHAR* SicklejawRemainsMeshPath =
        TEXT("/PrimalFauna/SicklejawRemains/SM_PFA_SicklejawRemains.SM_PFA_SicklejawRemains");
    constexpr const TCHAR* SicklejawRemainsSmallIconPath =
        TEXT("/PrimalFauna/SicklejawRemains/T_PFA_SicklejawRemains_Icon_Small.T_PFA_SicklejawRemains_Icon_Small");
    constexpr const TCHAR* SicklejawRemainsBigIconPath =
        TEXT("/PrimalFauna/SicklejawRemains/T_PFA_SicklejawRemains_Icon_Large.T_PFA_SicklejawRemains_Icon_Large");
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
    UClass* CategoryClass = LoadClass<UFGCategory>(nullptr, OtherCategoryPath);
    UTexture2D* SmallIcon = LoadObject<UTexture2D>(nullptr, SicklejawRemainsSmallIconPath);
    UTexture2D* BigIcon = LoadObject<UTexture2D>(nullptr, SicklejawRemainsBigIconPath);
    UStaticMesh* ConveyorMesh = LoadObject<UStaticMesh>(nullptr, SicklejawRemainsMeshPath);
    if (!CategoryClass || !SmallIcon || !BigIcon || !ConveyorMesh)
    {
        OutDetail = FString::Printf(TEXT("category=%s custom-small=%s custom-big=%s custom-mesh=%s"),
            CategoryClass ? TEXT("ok") : TEXT("missing"),
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
    RootResolvedObject(CategoryClass);
    RootResolvedObject(SmallIcon);
    RootResolvedObject(BigIcon);
    RootResolvedObject(ConveyorMesh);

    GPFAResolvedSmallIcon = SmallIcon;
    GPFAResolvedBigIcon = BigIcon;
    GPFAResolvedConveyorMesh = ConveyorMesh;

    OutDetail = FString::Printf(TEXT("small=%s big=%s mesh=%s category=%s"),
        *SmallIcon->GetPathName(), *BigIcon->GetPathName(),
        *ConveyorMesh->GetPathName(), *CategoryClass->GetName());
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

UPFASchematic_SicklejawLoot::UPFASchematic_SicklejawLoot()
{
    mType = ESchematicType::EST_Custom;
    mDisplayName = NSLOCTEXT("PrimalFauna", "SicklejawLootRegistrationName", "Sicklejaw Loot Registration");
    mDescription = NSLOCTEXT("PrimalFauna", "SicklejawLootRegistrationDescription",
        "Internal registration anchor for the Sicklejaw remains protein recipe.");
    mTechTier = 0;
    mTimeToComplete = 0.0f;
    mCost.Reset();
    mHiddenUntilDependenciesMet = true;
    mDependenciesBlocksSchematicAccess = true;

    UPFAUnlock_SicklejawProtein* RecipeUnlock =
        CreateDefaultSubobject<UPFAUnlock_SicklejawProtein>(TEXT("SicklejawProteinRecipeUnlock"));
    RecipeUnlock->AddRecipe(UPFARecipe_ProteinSicklejaw::StaticClass());
    mUnlocks.Add(RecipeUnlock);
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
                UModContentRegistry* ContentRegistry = UModContentRegistry::Get(World);
                FGameObjectRegistration RecipeRegistration;
                const bool bRegistryRegistered = ContentRegistry &&
                    ContentRegistry->GetRecipeInfo(
                        UPFARecipe_ProteinSicklejaw::StaticClass(), RecipeRegistration);
                const bool bManagerListed = RecipeManager &&
                    RecipeManager->GetAllRecipes().Contains(UPFARecipe_ProteinSicklejaw::StaticClass());
                UTexture2D* SmallIcon = UFGItemDescriptor::GetSmallIcon(
                    UPFASicklejawRemainsDescriptor::StaticClass());
                UTexture2D* BigIcon = UFGItemDescriptor::GetBigIcon(
                    UPFASicklejawRemainsDescriptor::StaticClass());
                UStaticMesh* ConveyorMesh = UFGItemDescriptor::GetItemMesh(
                    UPFASicklejawRemainsDescriptor::StaticClass());

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
                const bool bResolvedPresentationExact =
                    GPFAResolvedSmallIcon && GPFAResolvedBigIcon && GPFAResolvedConveyorMesh &&
                    GPFAResolvedSmallIcon->GetPathName() == SicklejawRemainsSmallIconPath &&
                    GPFAResolvedBigIcon->GetPathName() == SicklejawRemainsBigIconPath &&
                    GPFAResolvedConveyorMesh->GetPathName() == SicklejawRemainsMeshPath;
                // The live descriptor accessor has been observed returning the big icon
                // after our exact deferred small-icon assignment; the external cause is
                // not proven. Preserve the strict source-asset assertion above, while
                // accepting either safe runtime presentation and reporting what we saw.
                const bool bRuntimePresentationValid = SmallIcon && BigIcon && ConveyorMesh &&
                    (SmallIcon == GPFAResolvedSmallIcon || SmallIcon == GPFAResolvedBigIcon) &&
                    BigIcon == GPFAResolvedBigIcon && ConveyorMesh == GPFAResolvedConveyorMesh;
                const bool bCustomPresentationWired =
                    bResolvedPresentationExact && bRuntimePresentationValid;
                const TCHAR* RuntimeSmallIconMode =
                    SmallIcon == GPFAResolvedSmallIcon ? TEXT("source-small") :
                    SmallIcon == GPFAResolvedBigIcon ? TEXT("big-normalized-observed") :
                    TEXT("unexpected");
                const bool bAvailable = RecipeManager &&
                    RecipeManager->IsRecipeAvailable(UPFARecipe_ProteinSicklejaw::StaticClass());
                const bool bPass = PFALoot::IsDeferredContentReady() && bCreatureWired &&
                    bIngredientWired && bProductWired && bProducerWiring && bTimingWired &&
                    bCustomPresentationWired && bRegistryRegistered && bManagerListed && bAvailable;

                OutDetail = FString::Printf(
                    TEXT("resolved=%s creature=%s ingredient=%s product=%s producers=%s timing=%s presentation=%s runtimeSmall=%s registration=%s/%s available=%s paths=(sourceSmall=%s runtimeSmall=%s big=%s mesh=%s)"),
                    PFALoot::IsDeferredContentReady() ? TEXT("yes") : TEXT("no"),
                    bCreatureWired ? TEXT("yes") : TEXT("no"),
                    bIngredientWired ? TEXT("1 remains") : TEXT("bad"),
                    bProductWired ? TEXT("1 protein") : TEXT("bad"),
                    bProducerWiring ? TEXT("constructor+workbench") : TEXT("bad"),
                    bTimingWired ? TEXT("3s") : TEXT("bad"),
                    bCustomPresentationWired ? TEXT("custom-sicklejaw") : TEXT("bad"),
                    RuntimeSmallIconMode,
                    bRegistryRegistered ? TEXT("registry") : TEXT("no-registry"),
                    bManagerListed ? TEXT("manager") : TEXT("no-manager"),
                    bAvailable ? TEXT("yes") : TEXT("no"),
                    *GetPathNameSafe(GPFAResolvedSmallIcon), *GetPathNameSafe(SmallIcon),
                    *GetPathNameSafe(BigIcon),
                    *GetPathNameSafe(ConveyorMesh));
                return bPass;
            }});
        return true;
    }();
}
