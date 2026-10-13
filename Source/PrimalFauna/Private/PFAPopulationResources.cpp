#include "PFAPopulationResources.h"
#include "PrimalFauna.h"
#include "EngineUtils.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceDescriptor.h"
#include "FGPortableMiner.h"
#include "Resources/FGResourceDeposit.h"
#include "Resources/FGResourceNodeFrackingSatellite.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "UObject/UnrealType.h"

bool PFAReadPopulationResources(UWorld* World, TArray<FPFAPopulationResource>& Out)
{
    Out.Reset();
    // No access-transformer coupling: portable-miner proximity is deliberately
    // conservative and also protects freshly placed miners before node recoupling.
    TArray<FVector> PortableLocations;
    for (TActorIterator<AFGPortableMiner> It(World); It; ++It)
    {
        PortableLocations.Add(It->GetActorLocation());
    }
    auto EligibleNode = [&](AFGResourceNode* Node)
    {
        if (!IsValid(Node) || Node->IsHidden() || Node->IsOccupied() || !Node->GetResourceClass()) { return false; }
        for (const FVector& Position : PortableLocations)
        {
            if (FVector::DistSquared(Position, Node->GetActorLocation()) < FMath::Square(2000.0)) { return false; }
        }
        return true;
    };

    UClass* ProviderClass = FindObject<UClass>(nullptr, TEXT("/Script/NodeShuffle.NodeShuffleSubsystem"));
    if (!ProviderClass)
    {
        for (TActorIterator<AFGResourceNode> It(World); It; ++It)
        {
            AFGResourceNode* Node = *It;
            if (!IsValid(Node) || Node->IsHidden() || Node->IsA<AFGResourceDeposit>()
                || Node->IsA<AFGResourceNodeFrackingSatellite>()) { continue; }
            FPFAPopulationResource& Row = Out.AddDefaulted_GetRef();
            Row.Id = Node->GetPathName();
            Row.Location = Node->GetActorLocation();
            Row.Node = Node;
            Row.bEligible = EligibleNode(Node);
        }
        return true;
    }

    // Disabled/not-ready provider: retain guard identities but suspend new work.
    // NodeShuffle currently leaves its layout/world untouched while disabled.
    UGameInstance* GI = World->GetGameInstance();
    UConfigManager* Manager = GI ? GI->GetSubsystem<UConfigManager>() : nullptr;
    UConfigPropertySection* Root = Manager ? Manager->GetConfigurationRootSection(FConfigId{TEXT("NodeShuffle"), TEXT("")}) : nullptr;
    const UConfigPropertyBool* Enabled = Root ? Cast<UConfigPropertyBool>(Root->SectionProperties.FindRef(TEXT("Enabled"))) : nullptr;
    if (!Enabled || !Enabled->Value) { return false; }

    AActor* Provider = nullptr;
    for (TActorIterator<AActor> It(World, ProviderClass); It; ++It) { Provider = *It; break; }
    if (!IsValid(Provider)) { return false; }
    FArrayProperty* Layout = FindFProperty<FArrayProperty>(ProviderClass, TEXT("Layout"));
    FStructProperty* Entry = Layout ? CastField<FStructProperty>(Layout->Inner) : nullptr;
    FMapProperty* Spawned = FindFProperty<FMapProperty>(ProviderClass, TEXT("SpawnedNodes"));
    FStructProperty* MapKey = Spawned ? CastField<FStructProperty>(Spawned->KeyProp) : nullptr;
    FObjectPropertyBase* MapValue = Spawned ? CastField<FObjectPropertyBase>(Spawned->ValueProp) : nullptr;
    if (!Entry || !MapKey || MapKey->Struct != TBaseStructure<FGuid>::Get() || !MapValue
        || !MapValue->PropertyClass->IsChildOf(AFGResourceNode::StaticClass())) { return false; }

    UScriptStruct* Schema = Entry->Struct;
    FStructProperty* Id = FindFProperty<FStructProperty>(Schema, TEXT("EntryGuid"));
    FStructProperty* Location = FindFProperty<FStructProperty>(Schema, TEXT("Location"));
    FBoolProperty* Active = FindFProperty<FBoolProperty>(Schema, TEXT("bActive"));
    FBoolProperty* Pinned = FindFProperty<FBoolProperty>(Schema, TEXT("bPinned"));
    FBoolProperty* Settled = FindFProperty<FBoolProperty>(Schema, TEXT("bRayCasted"));
    FBoolProperty* NewNode = FindFProperty<FBoolProperty>(Schema, TEXT("bIsNewNode"));
    FStrProperty* VanillaPath = FindFProperty<FStrProperty>(Schema, TEXT("VanillaNodePath"));
    if (!Id || Id->Struct != TBaseStructure<FGuid>::Get() || !Location
        || Location->Struct != TBaseStructure<FVector>::Get() || !Active || !Pinned || !Settled || !NewNode || !VanillaPath)
    {
        return false;
    }

    TMap<FGuid, AFGResourceNode*> Live;
    FScriptMapHelper Map(Spawned, Spawned->ContainerPtrToValuePtr<void>(Provider));
    for (int32 I = 0; I < Map.GetMaxIndex(); ++I)
    {
        if (Map.IsValidIndex(I))
        {
            Live.Add(*reinterpret_cast<const FGuid*>(Map.GetKeyPtr(I)),
                Cast<AFGResourceNode>(MapValue->GetObjectPropertyValue(Map.GetValuePtr(I))));
        }
    }
    FScriptArrayHelper Rows(Layout, Layout->ContainerPtrToValuePtr<void>(Provider));
    // Empty layout can mean NodeShuffle has not completed its initial roll.
    if (Rows.Num() == 0) { return false; }
    for (int32 I = 0; I < Rows.Num(); ++I)
    {
        const void* Data = Rows.GetRawPtr(I);
        const FGuid Guid = *Id->ContainerPtrToValuePtr<FGuid>(Data);
        if (!Guid.IsValid() || !Active->GetPropertyValue_InContainer(Data)) { continue; }
        const bool bNew = NewNode->GetPropertyValue_InContainer(Data);
        if (bNew && !Settled->GetPropertyValue_InContainer(Data)) { continue; }
        FPFAPopulationResource& Row = Out.AddDefaulted_GetRef();
        Row.Id = TEXT("NS_") + Guid.ToString(EGuidFormats::Digits);
        Row.Location = *Location->ContainerPtrToValuePtr<FVector>(Data);
        AFGResourceNode* Node = Live.FindRef(Guid);
        if (!bNew)
        {
            Node = FindObject<AFGResourceNode>(nullptr, *VanillaPath->GetPropertyValue_InContainer(Data));
        }
        Row.Node = Node;
        // Require the actual actor at the final saved location, not merely a
        // candidate that is valid for a miner or a node still redealing terrain.
        Row.bEligible = !Pinned->GetPropertyValue_InContainer(Data)
            && (!IsValid(Node) || (EligibleNode(Node)
                && FVector::DistSquared(Node->GetActorLocation(), Row.Location) < FMath::Square(200.0)));
        // Unmaterialized/streamed-out is not evidence of removal or occupation.
        // The persistent site can be retained, but a new guard needs Row.Node.
    }
    return true;
}
