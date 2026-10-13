#include "PFASubsystem.h"
#include "PFAConfig.h"
#include "PFACheats.h"
#include "PFANestSpawner.h"
#include "PFAPopulationPolicy.h"
#include "PFAPopulationResources.h"
#include "Species/PFASicklejaw.h"
#include "PrimalFauna.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "FGMinimapCaptureActor.h"
#include "FGMapAreaTexture.h"
#include "FGMapArea.h"
#include "FGWaterVolume.h"
#include "FGBuildableSubsystem.h"
#include "Buildables/FGBuildable.h"
#include "Resources/FGResourceNode.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "AI/FGCreatureController.h"
#include "UObject/UnrealType.h"
#include "String/LexFromString.h"
#include "Templates/UnrealTemplate.h"

namespace
{
    // Map-domain samples only: NEVER treated as terrain or biome rectangles.
    // Every point is classified by the game's native map-area texture; actual
    // spawning additionally requires resident terrain and creature navigation.
    constexpr int32 GridSide = 45;
    constexpr double GridStep = 20000.0;
    constexpr double GridOrigin = -400000.0;

    uint32 PopulationHash(int32 Seed, const FString& Key)
    {
        return FCrc::StrCrc32(*Key, static_cast<uint32>(Seed));
    }

    double NearestPopulationPlayer(const TArray<FVector>& Players, const FVector& Point, bool bUnknownHeight = false)
    {
        double Distance = TNumericLimits<double>::Max();
        for (const FVector& Player : Players) { Distance = FMath::Min(Distance, bUnknownHeight ? FVector::Dist2D(Player, Point) : FVector::Dist(Player, Point)); }
        return Distance;
    }

    struct FPopulationSite
    {
        FString Id;
        FName Biome;
        FVector Location = FVector::ZeroVector;
        bool bEligible = true;
        bool bResident = false;
    };
}

FName APFASubsystem::PopulationBiomeAt(const FVector& Point) const
{
    if (!PopulationMap) { return NAME_None; }
    const TSubclassOf<UFGMapArea> Area = PopulationMap->GetMapAreaForWorldLocation(Point);
    return Area ? Area->GetFName() : NAME_None;
}

bool APFASubsystem::IsPopulationProtected(const FVector& Point, float ExtraRadius) const
{
    using namespace PFAPopulationPolicy;
    for (const FVector& Start : PopulationStarterLocations)
    {
        if (FVector::DistSquared2D(Point, Start) < FMath::Square(StarterClearanceCm + ExtraRadius)) { return true; }
    }
    for (const FBox& Bounds : PopulationFactoryBounds)
    {
        if (Bounds.ExpandBy(FactoryClearanceCm + ExtraRadius).IsInsideOrOn(Point)) { return true; }
    }
    return false;
}

bool APFASubsystem::ValidatePopulationGround(const FVector& Requested, FVector& OutGround) const
{
    UWorld* World = GetWorld();
    if (!World) { return false; }
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PFAPopulationGround), true);
    // A resource location supplies a real height; wilderness Z is sampled from
    // the surface. Reject buildings, pawns and water instead of standing on them.
    const bool bKnownHeight = Requested.Z != 0.0;
    const FVector Start = Requested + FVector(0, 0, bKnownHeight ? 3000.0 : 100000.0);
    const FVector End = Requested - FVector(0, 0, bKnownHeight ? 6000.0 : 100000.0);
    if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)
        || Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.75 || !Hit.GetActor()
        || Hit.GetActor()->IsA<AFGBuildable>() || Hit.GetActor()->IsA<APawn>()
        || Hit.GetActor()->IsA<AFGResourceNode>()) { return false; }
    OutGround = Hit.ImpactPoint + FVector(0, 0, 100);
    // The map's open ocean has no water volume in places (existing NodeShuffle
    // and HostileScaling evidence). Land below this conservative floor is skipped.
    if (Hit.ImpactPoint.Z < -4500.0) { return false; }
    for (TActorIterator<AFGWaterVolume> It(World); It; ++It)
    {
        if (It->EncompassesPoint(Hit.ImpactPoint) || It->EncompassesPoint(OutGround)) { return false; }
    }
    if (IsPopulationProtected(OutGround, 2500.0f)) { return false; }
    return !World->OverlapBlockingTestByChannel(OutGround + FVector(0, 0, 150), FQuat::Identity,
        ECC_Pawn, FCollisionShape::MakeCapsule(100.0f, 150.0f), Params);
}

void APFASubsystem::LogPopulationDecision(const FPFANestRecord& Record, APFANestSpawner* Spawner, const TCHAR* Reason)
{
    if (!PFADiag::IsEnabled()) { return; }
    const FString Signature = FString::Printf(TEXT("%s/%d/%d/%d"), Reason, Record.CandidateAttempt,
        Spawner ? Spawner->bPopulationAllowed : false, Spawner ? Spawner->GetCreatures().Num() : 0);
    if (PopulationLastDecisions.FindRef(Record.NestId) == Signature && TickCount % 6 != 0) { return; }
    PopulationLastDecisions.Add(Record.NestId, Signature);
    UE_LOG(LogPrimalFauna, Display,
        TEXT("[PFA] Population slot=%s biome=%s reason=%s role=%s dormant=%d placed=%d attempt=%d allowed=%d live=%d waiting=%d site=%s location=%s"),
        *Record.NestId.ToString(), *Record.RegionId.ToString(), Reason,
        Record.bResourceGuard ? TEXT("guard") : TEXT("wilderness"), Record.bDormant, Record.bHasPlacement,
        Record.CandidateAttempt, Spawner ? Spawner->bPopulationAllowed : false,
        Spawner ? Spawner->GetCreatures().Num() : 0, Spawner ? Spawner->HasPopulationSpawnRequests() : false,
        *Record.ResourceSiteId, *Record.Location.ToCompactString());
}

void APFASubsystem::RefreshWorldPopulation()
{
    using namespace PFAPopulationPolicy;
    UWorld* World = GetWorld();
    if (!HasAuthority() || !World) { return; }
    const FPFAConfigStruct Config = FPFAConfigStruct::GetActiveConfig(this);
    const bool bEnabled = PFAPopulationPolicy::IsEnabled(Config);
    const FString State = FString::Printf(
        TEXT("state=%s reason=%s Enabled=%d WorldPopulation=%d ExperimentalFeatures=%d (experimental flag does not control population)"),
        bEnabled ? TEXT("enabled") : TEXT("disabled"),
        !Config.Enabled ? TEXT("mod-disabled") : (!Config.EnableWorldPopulation ? TEXT("world-population-off") : TEXT("world-population-on")),
        Config.Enabled, Config.EnableWorldPopulation, Config.EnableExperimentalFeatures);
    if (State != LastPopulationState)
    {
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Population %s"), *State);
        LastPopulationState = State;
    }
    if (!bEnabled && !NestRegistry.ContainsByPredicate([](const FPFANestRecord& Record)
        { return Record.bWorldPopulation; })) { return; }
    PopulationPlayerLocations.Reset();
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC = It->Get();
        if (PC && PC->GetPawn()) { PopulationPlayerLocations.Add(PC->GetPawn()->GetActorLocation()); }
    }
    PopulationFactoryBounds.Reset();
    AFGBuildableSubsystem* Buildings = AFGBuildableSubsystem::Get(World);
    if (Buildings)
    {
        for (AFGBuildable* Building : Buildings->GetAllBuildablesRef())
        {
            if (IsValid(Building)) { PopulationFactoryBounds.Add(Building->GetComponentsBoundingBox(true)); }
        }
    }
    // Capture actual map spawn anchors, never the first player's current position
    // in an old save. Persist anchors as streamed starts can disappear later.
    for (TActorIterator<APlayerStart> It(World); It; ++It)
    {
        if (It->IsNetStartupActor()) { PopulationStarterLocations.AddUnique(It->GetActorLocation()); }
    }

    TMap<FName, APFANestSpawner*> Live;
    for (TActorIterator<APFANestSpawner> It(World); It; ++It)
    {
        Live.Add(It->NestId, *It);
        if (It->bPopulationManaged)
        {
            It->bPopulationAllowed = false; // re-earn permission each pass
            It->bPopulationWandering = false;
        }
    }
    // Do not create a map/seed/plan while the mod or World Population is disabled.
    if (bEnabled && PopulationSeed == 0) { PopulationSeed = static_cast<int32>(FGuid::NewGuid().A | 1u); }
    if (bEnabled && !PopulationMap)
    {
        for (TActorIterator<AFGMinimapCaptureActor> It(World); It; ++It)
        {
            UFGMapAreaTexture* Map = It->GetMapAreaTexture();
            // Querying a map texture before native initialization can divide by
            // zero/index empty data. Verify the actual runtime backing data first.
            FIntProperty* Width = Map ? FindFProperty<FIntProperty>(Map->GetClass(), TEXT("mDataWidth")) : nullptr;
            FArrayProperty* Data = Map ? FindFProperty<FArrayProperty>(Map->GetClass(), TEXT("mAreaData")) : nullptr;
            FObjectPropertyBase* Capture = Map ? FindFProperty<FObjectPropertyBase>(Map->GetClass(), TEXT("mCaptureActor")) : nullptr;
            if (Width && Data && Capture && Width->GetPropertyValue_InContainer(Map) > 0
                && Capture->GetObjectPropertyValue_InContainer(Map)
                && FScriptArrayHelper(Data, Data->ContainerPtrToValuePtr<void>(Map)).Num() > 0)
            {
                PopulationMap = Map;
                break;
            }
        }
    }
    if (bEnabled && PopulationMap && !bPopulationMapReady)
    {
        // Bounded discovery: at most 512 native lookups per ten-second heartbeat.
        const int32 End = FMath::Min(PopulationMapCursor + 512, GridSide * GridSide);
        for (; PopulationMapCursor < End; ++PopulationMapCursor)
        {
            const FVector Point(GridOrigin + (PopulationMapCursor % GridSide) * GridStep,
                GridOrigin + (PopulationMapCursor / GridSide) * GridStep, 0);
            const FName Biome = PopulationBiomeAt(Point);
            if (!Biome.IsNone()) { PopulationBiomeCells.FindOrAdd(Biome).Add(Point); }
        }
        bPopulationMapReady = PopulationMapCursor == GridSide * GridSide;
    }

    // Startup is not a population change. Wait for the native map and protection
    // census without retiring the saved packs while that evidence is unavailable.
    if (bEnabled && (!bPopulationMapReady || !Buildings || PopulationStarterLocations.IsEmpty()))
    {
        for (const auto& Entry : Live)
        {
            if (Entry.Value->bPopulationManaged) { Entry.Value->QuiescePopulationSpawning(TEXT("startup-evidence-unavailable")); }
        }
        if (TickCount % 6 == 0)
        {
            UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] Population waiting: map=%d buildings=%d starterAnchors=%d"),
                bPopulationMapReady, Buildings != nullptr, PopulationStarterLocations.Num());
        }
        return;
    }

    TMap<FName, int32> Overrides;
    TArray<FString> OverrideTokens;
    Config.BiomePackOverrides.ParseIntoArray(OverrideTokens, TEXT(";"), true);
    FString InvalidOverrides;
    for (const FString& Token : OverrideTokens)
    {
        FString Key, Value;
        int32 Count = -1;
        if (!Token.Split(TEXT("="), &Key, &Value) || !LexTryParseString(Count, *Value.TrimStartAndEnd())
            || Count < 0 || Count > MaxPacksPerBiome || !PopulationBiomeCells.Contains(FName(*Key.TrimStartAndEnd())))
        {
            InvalidOverrides += Token + TEXT("; ");
            continue;
        }
        Overrides.Add(FName(*Key.TrimStartAndEnd()), Count);
    }
    if (bPopulationMapReady && InvalidOverrides != LastPopulationConfigWarning)
    {
        if (!InvalidOverrides.IsEmpty())
        {
            UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] Population ignored invalid/unknown biome overrides: %s"), *InvalidOverrides);
        }
        LastPopulationConfigWarning = InvalidOverrides;
    }

    TMap<FName, int32> Budgets;
    TArray<FName> Biomes;
    PopulationBiomeCells.GetKeys(Biomes);
    Biomes.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
    if (bEnabled && bPopulationMapReady && Buildings && PopulationStarterLocations.Num() > 0)
    {
        for (const FName Biome : Biomes)
        {
            FRandomStream Roll(static_cast<int32>(PopulationHash(PopulationSeed, Biome.ToString())));
            const int32* Override = Overrides.Find(Biome);
            const int32 Count = Budget(Config.PacksPerBiomeMin, Config.PacksPerBiomeMax, Roll.FRand(),
                Config.NestDensity, PopulationBiomeCells[Biome].Num(), Config.ScaleLargeBiomes, Override ? *Override : -1);
            Budgets.Add(Biome, Count);
            int32 Existing = 0;
            for (const FPFANestRecord& Record : NestRegistry)
            {
                if (Record.bWorldPopulation && Record.RegionId == Biome) { ++Existing; }
            }
            for (int32 Slot = Existing; Slot < Count && NestRegistry.Num() < MaxWorldSlots; ++Slot)
            {
                FPFANestRecord& Record = NestRegistry.AddDefaulted_GetRef();
                Record.NestId = FName(*FString::Printf(TEXT("World_%s_%02d"), *Biome.ToString(), Slot));
                Record.RegionId = Biome;
                Record.PopulationSlot = Slot;
                Record.bWorldPopulation = true;
                Record.bDormant = true;
                Record.SpeciesClass = FSoftClassPath(APFASicklejaw::StaticClass());
                Record.CreaturesMin = 2;
                Record.CreaturesMax = 4;
                Record.SpawnRadius = 1500.0f;
                Record.RespawnDays = 3;
            }
            if (TickCount % 6 == 0 || Existing < Count)
            {
                UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Population biome=%s budget=%d sampledCells=%d guardsWithinBudget=%d"),
                    *Biome.ToString(), Count, PopulationBiomeCells[Biome].Num(),
                    GuardBudget(Count, Config.GuardResourceSites, Config.GuardPacksPerBiome));
            }
        }
    }

    TArray<FPFAPopulationResource> Resources;
    const bool bResourceSnapshotReady = bEnabled && PFAReadPopulationResources(World, Resources);
    Resources.Sort([](const FPFAPopulationResource& A, const FPFAPopulationResource& B) { return A.Id < B.Id; });
    TArray<FPopulationSite> Sites;
    for (const FPFAPopulationResource& Resource : Resources)
    {
        const FName Biome = PopulationBiomeAt(Resource.Location);
        if (!Budgets.Contains(Biome)) { continue; }
        FPopulationSite* Site = Sites.FindByPredicate([&](const FPopulationSite& Other)
        {
            return Other.Biome == Biome && FVector::DistSquared(Other.Location, Resource.Location) < FMath::Square(12000.0);
        });
        if (!Site)
        {
            Site = &Sites.AddDefaulted_GetRef();
            Site->Id = Resource.Id;
            Site->Biome = Biome;
            Site->Location = Resource.Location;
        }
        // An occupied member protects the whole grouped site.
        Site->bEligible &= Resource.bEligible && !IsPopulationProtected(Resource.Location, 5000.0f);
        Site->bResident |= Resource.Node.IsValid();
    }
    for (const FPFANestRecord& Record : NestRegistry)
    {
        if (!Record.bWorldPopulation || !Record.bResourceGuard || Record.ResourceSiteId.StartsWith(TEXT("NS_"))
            || Record.ResourceSiteId.IsEmpty()
            || FindObject<UClass>(nullptr, TEXT("/Script/NodeShuffle.NodeShuffleSubsystem"))
            || Resources.ContainsByPredicate([&](const FPFAPopulationResource& Resource)
                { return Resource.Id == Record.ResourceSiteId; })
            || Sites.ContainsByPredicate([&](const FPopulationSite& Site)
                { return Site.Id == Record.ResourceSiteId; })) { continue; }
        // No provider record exists for vanilla resources: absence from a streamed
        // actor scan cannot authorize a replacement. Keep the saved site reserved.
        FPopulationSite& Site = Sites.AddDefaulted_GetRef();
        Site.Id = Record.ResourceSiteId;
        Site.Location = Record.ResourceSiteLocation;
        Site.Biome = Record.RegionId;
    }
    Sites.Sort([&](const FPopulationSite& A, const FPopulationSite& B)
    {
        const uint32 AH = PopulationHash(PopulationSeed, A.Id), BH = PopulationHash(PopulationSeed, B.Id);
        return AH == BH ? A.Id < B.Id : AH < BH;
    });
    TSet<FString> AssignedSites;
    const double Spacing = FMath::Clamp(Config.PackSpacingMeters, 300.0f, 1200.0f) * 100.0;
    auto IsSpaced = [&](const FPFANestRecord& Record, const FVector& Point)
    {
        for (const FPFANestRecord& Other : NestRegistry)
        {
            if (Other.NestId == Record.NestId || (Other.bWorldPopulation && !Other.bHasPlacement)) { continue; }
            const APFANestSpawner* OtherSpawner = Live.FindRef(Other.NestId);
            const bool bOccupied = OtherSpawner && OtherSpawner->GetCreatures().Num() > 0;
            if ((!Other.bDormant || bOccupied) && FVector::DistSquared2D(Point, Other.Location) < FMath::Square(Spacing)) { return false; }
        }
        return true;
    };

    int32 ActiveCount = 0, DeferredCount = 0;
    int32 GroundBudget = 8;
    for (FPFANestRecord& Record : NestRegistry)
    {
        if (!Record.bWorldPopulation) { continue; }
        APFANestSpawner* Spawner = Live.FindRef(Record.NestId);
        if (Spawner) { Spawner->bPopulationManaged = true; }
        const int32 Count = Budgets.FindRef(Record.RegionId);
        const int32 GuardCount = GuardBudget(Count, Config.GuardResourceSites, Config.GuardPacksPerBiome);
        const bool bWantGuard = Record.PopulationSlot < GuardCount;
        bool bWanted = Record.PopulationSlot < Count;
        const FPopulationSite* Site = nullptr;
        if (bWanted && bWantGuard)
        {
            if (!bResourceSnapshotReady)
            {
                Record.bDormant = true;
                if (Spawner) { Spawner->QuiescePopulationSpawning(TEXT("provider-not-ready")); }
                ++DeferredCount;
                LogPopulationDecision(Record, Spawner, TEXT("provider-not-ready"));
                continue; // an unavailable provider is not an authoritative reroll
            }
            auto AcceptSite = [&](const FPopulationSite& Candidate)
            {
                return Candidate.Biome == Record.RegionId && Candidate.bEligible && !AssignedSites.Contains(Candidate.Id)
                    && (PopulationHash(PopulationSeed, Candidate.Id) / 4294967296.0) < FMath::Clamp(Config.ResourceGuardChance, 0.0f, 1.0f);
            };
            Site = Sites.FindByPredicate([&](const FPopulationSite& Candidate)
                { return Candidate.Id == Record.ResourceSiteId && AcceptSite(Candidate); });
            if (!Site) { Site = Sites.FindByPredicate(AcceptSite); }
            bWanted = bResourceSnapshotReady && Site;
            if (Site) { AssignedSites.Add(Site->Id); }
        }
        const bool bSamePlacement = Record.bHasPlacement && Record.bResourceGuard == bWantGuard
            && (bWantGuard ? (Site && Site->Id == Record.ResourceSiteId
                && FVector::DistSquared(Site->Location, Record.ResourceSiteLocation) < FMath::Square(200.0))
                : Record.LayoutRevision == Config.TerritoryLayoutRevision);
        const bool bProtected = Record.bHasPlacement && IsPopulationProtected(Record.Location, 2500.0f);
        const bool bKeep = KeepPlacement(bWanted, bSamePlacement, bProtected, IsSpaced(Record, Record.Location));
        Record.bDormant = !bKeep;
        if (Spawner)
        {
            Spawner->bPopulationManaged = true;
            if (!bKeep)
            {
                // First fence/deactivate native ticking and cancel its transient
                // synchronous location retries. Saved slots/kill history stay put.
                // Actual native reentry, players and combat still defer retirement.
                if (Spawner->QuiescePopulationSpawning(TEXT("placement-not-kept")) && Spawner->CanRetirePopulation())
                {
                    // Actor destruction runs synchronous EndPlay/OnDestroyed
                    // listeners while native code still holds slot references.
                    TGuardValue<int32> Retiring(Spawner->PopulationNativeCallDepth, Spawner->PopulationNativeCallDepth + 1);
                    Spawner->StopSpawning();
                    Spawner->DestroyCreatures();
                    // Quiescence retained this invoker for nearby/live packs;
                    // native StopSpawning skips unregister when already inactive.
                    Spawner->RegisterAsNavigationInvoker(false);
                }
            }
        }
        if (!bKeep && bWanted && (!Spawner || Spawner->CanRelocatePopulation()))
        {
            const TArray<FVector>* Cells = PopulationBiomeCells.Find(Record.RegionId);
            if (!Cells || Cells->IsEmpty()) { ++DeferredCount; LogPopulationDecision(Record, Spawner, TEXT("no-biome-candidates")); continue; }
            FRandomStream Rng(static_cast<int32>(PopulationHash(PopulationSeed,
                FString::Printf(TEXT("%s/%d/%d"), *Record.NestId.ToString(), Config.TerritoryLayoutRevision, Record.CandidateAttempt))));
            FVector Candidate;
            if (bWantGuard)
            {
                if (!Site || !Site->bResident) { ++DeferredCount; LogPopulationDecision(Record, Spawner, TEXT("guard-node-not-resident")); continue; }
                const float Angle = Rng.FRandRange(0.0f, 2.0f * PI);
                Candidate = Site->Location + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 3500.0;
            }
            else
            {
                Candidate = (*Cells)[Rng.RandRange(0, Cells->Num() - 1)]
                    + FVector(Rng.FRandRange(-9000, 9000), Rng.FRandRange(-9000, 9000), 0);
            }
            // Distant candidates retain their deterministic attempt number. Only
            // examined, invalid terrain advances it, never mere streaming absence.
            double Distance = NearestPopulationPlayer(PopulationPlayerLocations, Candidate, !bWantGuard);
            if (Distance > TerrainResidentDistanceCm || Distance < SafePlayerDistanceCm || GroundBudget <= 0)
            {
                ++DeferredCount;
                LogPopulationDecision(Record, Spawner, GroundBudget <= 0 ? TEXT("trace-budget") : TEXT("candidate-outside-safe-resident-band"));
                continue;
            }
            --GroundBudget;
            FVector Ground;
            if (PopulationBiomeAt(Candidate) != Record.RegionId || IsPopulationProtected(Candidate, 2500.0f)
                || !IsSpaced(Record, Candidate) || !ValidatePopulationGround(Candidate, Ground))
            {
                ++Record.CandidateAttempt;
                ++DeferredCount;
                LogPopulationDecision(Record, Spawner, TEXT("candidate-rejected-ground-biome-spacing-or-protection"));
                continue;
            }
            if (!CanMaterialize(NearestPopulationPlayer(PopulationPlayerLocations, Ground),
                IsPopulationProtected(Ground, 2500.0f), true)) { ++DeferredCount; LogPopulationDecision(Record, Spawner, TEXT("ground-outside-safe-band")); continue; }
            if (Spawner && !Spawner->SetActorLocation(Ground)) { ++DeferredCount; LogPopulationDecision(Record, Spawner, TEXT("relocation-failed")); continue; }
            Record.Location = Ground;
            Record.bHasPlacement = true;
            Record.bDormant = false;
            Record.bResourceGuard = bWantGuard;
            Record.ResourceSiteId = Site ? Site->Id : FString();
            Record.ResourceSiteLocation = Site ? Site->Location : FVector::ZeroVector;
            Record.LayoutRevision = Config.TerritoryLayoutRevision;
            if (Spawner) { Spawner->RegisterAsNavigationInvoker(true); }
        }
        if (!Record.bDormant && Spawner)
        {
            // A dormant nest may have been safely retired and unregistered.
            // Re-enabling its saved placement must bootstrap nav again, even
            // for guards/non-wandering slots, before requiring a nav projection.
            Spawner->RegisterAsNavigationInvoker(true);
            UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
            FNavLocation Projected;
            const AFGCreature* CDO = GetDefault<APFASicklejaw>();
            const int32 WildernessIndex = Record.PopulationSlot - GuardCount;
            const int32 WanderCount = FMath::RoundToInt((Count - GuardCount)
                * FMath::Clamp(Config.WanderingPackFraction, 0.0f, 1.0f));
            const bool bPatrolSlot = !bWantGuard && WildernessIndex < WanderCount;
            if (Nav && bPatrolSlot && NearestPopulationPlayer(PopulationPlayerLocations, Record.Location) < 40000.0)
            {
                // Bootstrap enough navigation for a real patrol BEFORE asking for
                // a projected point. No pre-existing test nest or live creature is
                // required; the inert native nest is the navigation invoker.
                const float Radius = FMath::Clamp(Config.WanderingRadiusMeters, 25.0f, 200.0f) * 100.0f + 3000.0f;
                Nav->RegisterNavigationInvoker(Spawner, Radius, Radius + 3000.0f);
            }
            const bool bNavReady = Nav && Nav->ProjectPointToNavigation(Record.Location, Projected,
                FVector(200, 200, 300), &CDO->GetNavAgentPropertiesRef());
            // Nav readiness is session state; do not save a previous session's proof.
            Spawner->bPopulationAllowed = bNavReady && !IsPopulationProtected(Record.Location, 2500.0f);
            if (!Spawner->bPopulationAllowed) { Spawner->QuiescePopulationSpawning(TEXT("navigation-or-protection-blocked")); }
            Spawner->bPopulationWandering = bPatrolSlot && Spawner->bPopulationAllowed;
            if (Spawner->bPopulationWandering) { RefreshPopulationPatrol(Spawner, Record, Config); }
            else { Spawner->bHasPatrolGoal = false; }
            if (Spawner->bPopulationAllowed) { ++ActiveCount; } else { ++DeferredCount; }
        }
        LogPopulationDecision(Record, Spawner, Record.bDormant
            ? (bWanted ? TEXT("awaiting-safe-relocation") : TEXT("disabled-surplus-or-no-eligible-site"))
            : (!Spawner ? (Record.bMaterializedEver ? TEXT("missing-saved-spawner-fail-closed") : TEXT("awaiting-spawner-invoker"))
                : (Spawner->bPopulationAllowed ? TEXT("active-native-lifecycle") : TEXT("awaiting-creature-navmesh"))));
    }
    if (bEnabled && TickCount % 6 == 0)
    {
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Population: biomes=%d records=%d allowed=%d deferred=%d mapReady=%d starterAnchors=%d resourceSnapshot=%d (budgets are capacity, not spawned counts)"),
            Budgets.Num(), NestRegistry.Num(), ActiveCount, DeferredCount, bPopulationMapReady,
            PopulationStarterLocations.Num(), bResourceSnapshotReady);
    }
}

void APFASubsystem::RefreshPopulationPatrol(APFANestSpawner* Spawner, const FPFANestRecord& Record,
    const FPFAConfigStruct& Config)
{
    if (!Spawner) { return; }
    // Recheck the cached goal every census, even while the next selection is not
    // due: a factory may have appeared since the previous waypoint was chosen.
    if (Spawner->bHasPatrolGoal && (PopulationBiomeAt(Spawner->PopulationPatrolGoal) != Record.RegionId
        || IsPopulationProtected(Spawner->PopulationPatrolGoal, 2500.0f))) { Spawner->bHasPatrolGoal = false; }
    if (Spawner->IsPopulationEngaged() || GetWorld()->GetTimeSeconds() < Spawner->NextPatrolTime) { return; }
    Spawner->NextPatrolTime = GetWorld()->GetTimeSeconds() + FMath::Clamp(Config.WanderRetargetSeconds, 30.0f, 300.0f);
    Spawner->bHasPatrolGoal = false;
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav) { return; }
    FRandomStream Rng(static_cast<int32>(PopulationHash(PopulationSeed,
        Record.NestId.ToString() + FString::FromInt(FMath::FloorToInt(GetWorld()->GetTimeSeconds() / 30.0f)))));
    for (int32 Attempt = 0; Attempt < 4; ++Attempt)
    {
        const float Angle = Rng.FRandRange(0.0f, PI * 2.0f);
        const float Radius = Rng.FRandRange(0.4f, 1.0f) * FMath::Clamp(Config.WanderingRadiusMeters, 25.0f, 200.0f) * 100.0f;
        const FVector Candidate = Record.Location + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Radius;
        FNavLocation Projected;
        if (PopulationBiomeAt(Candidate) != Record.RegionId || IsPopulationProtected(Candidate, 2500.0f)
            || !Nav->ProjectPointToNavigation(Candidate, Projected, FVector(300, 300, 1500),
                &GetDefault<APFASicklejaw>()->GetNavAgentPropertiesRef())) { continue; }
        if (PopulationBiomeAt(Projected.Location) != Record.RegionId || IsPopulationProtected(Projected.Location, 2500.0f)) { continue; }
        bool bPathsSafe = true;
        const TArray<AFGCreature*> Creatures = Spawner->GetCreatures();
        if (Creatures.IsEmpty()) { return; }
        for (AFGCreature* Creature : Creatures)
        {
            if (!IsValid(Creature)) { continue; }
            UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),
                Creature->GetActorLocation(), Projected.Location, Creature);
            if (!Path || !Path->IsValid() || Path->IsPartial()) { bPathsSafe = false; break; }
            for (int32 I = 1; I < Path->PathPoints.Num() && bPathsSafe; ++I)
            {
                const FVector A = Path->PathPoints[I - 1], B = Path->PathPoints[I];
                const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist(A, B) / 1000.0));
                for (int32 Step = 0; Step <= Steps; ++Step)
                {
                    const FVector Point = FMath::Lerp(A, B, static_cast<double>(Step) / Steps);
                    if (PopulationBiomeAt(Point) != Record.RegionId || IsPopulationProtected(Point, 2500.0f))
                    { bPathsSafe = false; break; }
                }
            }
        }
        if (bPathsSafe)
        {
            Spawner->PopulationPatrolGoal = Projected.Location;
            Spawner->bHasPatrolGoal = true;
            if (PFADiag::IsEnabled())
            {
                UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Population patrol: nest=%s waypoint=%s"),
                    *Record.NestId.ToString(), *Projected.Location.ToCompactString());
            }
            return;
        }
    }
}
