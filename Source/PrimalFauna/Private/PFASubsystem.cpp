#include "PFASubsystem.h"
#include "PrimalFauna.h"
#include "PFAConfig.h"
#include "PFAFactionConfig.h"
#include "PFACheats.h"
#include "PFABehaviorBorrower.h"
#include "PFANestSpawner.h"
#include "PFACreatureBase.h"
#include "PFALoot.h"
#include "Species/PFASicklejaw.h"

#include "BehaviorTree/BehaviorTree.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Creature/FGCreature.h"
#include "FGRecipeManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
    constexpr float RefreshIntervalSeconds = 10.0f;
    constexpr int32 HeartbeatEveryNTicks = 6;
    constexpr int32 MaxBTBorrowAttempts = 5;

    // ---- W1.5 test-nest defaults (docs/08 packet: "the first PFA.SpawnNest
    // ... creates a nest AT THE PLAYER's ground position"). TODO(W2.5): these
    // become real per-species FPFANestSpec values from the curated region
    // table; until then PFA.SpawnNest always uses Sicklejaw with these
    // placeholder counts. ----
    constexpr int32 TestNestCreaturesMin = 1;
    constexpr int32 TestNestCreaturesMax = 3;
    constexpr float TestNestSpawnRadiusCm = 1500.0f;
    constexpr int32 TestNestRespawnDaysDefault = 1;
    const FName TestRegionId(TEXT("Test"));

    // Ground trace tuning for SpawnNestAtPlayer -- generous range so a player
    // standing on a tall structure or in a shallow depression still resolves
    // to real terrain (matches the NodeShuffle TerrainZ trace's +-1000cm
    // shape, widened slightly since PrimalFauna has no equivalent to
    // NodeShuffle's own multi-sample fallback).
    constexpr float GroundTraceUpCm = 500.0f;
    constexpr float GroundTraceDownCm = 5000.0f;
}

APFASubsystem::APFASubsystem()
{
    PrimaryActorTick.bCanEverTick = false;
}

void APFASubsystem::BeginPlay()
{
    Super::BeginPlay();

    // All PrimalFauna gameplay decisions are server-authoritative; clients
    // receive results via replication (doc 04 §2).
    if (!HasAuthority())
    {
        return;
    }

    // W1.4 (docs/07 §4): borrow a vanilla BehaviorTree onto our creatures' CDO,
    // server-side, post-mount. This subsystem is spawned by the world module
    // (PFARootWorld), so its BeginPlay is the post-mount hook the packet calls
    // for. Runtime AssetRegistry discovery because no vanilla hostile-BT path
    // was ever captured (docs/CENSUS.md). Idempotent across worlds. The returned
    // tree is held in mBorrowedBehaviorTree to keep it alive (cookbook #34); the
    // stats feed the BorrowedBTAssigned self-test.
    {
        const PFABehaviorBorrower::FResult BorrowResult = PFABehaviorBorrower::DiscoverAndBorrow(GetWorld());
        mBorrowedBehaviorTree = BorrowResult.ChosenTree;
        BTDiscoveredCount = BorrowResult.DiscoveredCount;
        BTSearchRoot = BorrowResult.SearchRoot;
        BTBorrowAttempts = 1;
    }

    GetWorldTimerManager().SetTimer(
        RefreshTimerHandle, this, &APFASubsystem::RefreshTick,
        RefreshIntervalSeconds, true, 2.0f);
}

void APFASubsystem::RefreshTick()
{
    ++TickCount;

    if (!bLootRecipeReady)
    {
        bLootRecipeReady = EnsureLootRecipeAvailable();
    }

    const FPFAConfigStruct Config = FPFAConfigStruct::GetActiveConfig(this);

    // Config-gated diagnostics (sml-gotchas.md §3): push the live config
    // value into the file-static gate every tick so PFADiag::IsEnabled()
    // reflects in-game config edits within one tick, matching the config
    // property's own "takes effect within ~10s, no reload" promise
    // (PFAConfig.cpp). Cheap -- one config-manager lookup per 10s.
    PFADiag::SetConfigDiagnostics(Config.EnableDiagnostics);

    // W2.3: push the Factions/balance keys into their file-static snapshot
    // (PFAFactionConfig) -- the same live-apply pattern as the diagnostics
    // gate above; controller rules 6/7 + the bite's damage fallback read the
    // statics per evaluation with zero config-manager lookups.
    PFAFactionConfig::Push(Config);

    // W2.3: live aggression retarget. Stamp every live PrimalFauna creature
    // only when the configured value CHANGES (first tick always applies the
    // persisted config once via the -1 sentinel); fresh spawns self-stamp in
    // APFACreatureBase::BeginPlay. Passive transitions de-aggro through
    // SetAggressionMode's own NullifyAllAggro (doc 02 §3).
    const int32 ConfigAggression = FMath::Clamp(Config.SicklejawAggression, 0, 2);
    if (ConfigAggression != LastPushedAggression)
    {
        int32 RetargetedCount = 0;
        for (TActorIterator<APFACreatureBase> It(GetWorld()); It; ++It)
        {
            It->SetAggressionMode(static_cast<EPFAAggression>(ConfigAggression));
            ++RetargetedCount;
        }
        UE_LOG(LogPrimalFauna, Display,
            TEXT("[PFA] Config: SicklejawAggression -> %d applied to %d live creature(s)."),
            ConfigAggression, RetargetedCount);
        LastPushedAggression = ConfigAggression;
    }

    if (!bLoggedConfigSnapshot)
    {
        UE_LOG(LogPrimalFauna, Display,
            TEXT("[PFA] Config: Enabled=%s NestDensity=%.2f Diagnostics=%s SicklejawAggression=%d SicklejawDmgMult=%.2f HuntVanillaHostiles=%s HuntPassiveFauna=%s PackAssist=%s"),
            Config.Enabled ? TEXT("true") : TEXT("false"),
            Config.NestDensity,
            Config.EnableDiagnostics ? TEXT("true") : TEXT("false"),
            ConfigAggression,
            Config.SicklejawDamageMultiplier,
            Config.HuntVanillaHostiles ? TEXT("true") : TEXT("false"),
            Config.HuntPassiveFauna ? TEXT("true") : TEXT("false"),
            Config.PackAssist ? TEXT("true") : TEXT("false"));
        bLoggedConfigSnapshot = true;
    }

    // Doc 03 §3: adopt/materialize every registry nest, every refresh tick --
    // the SAME path SpawnNestAtPlayer triggers immediately on demand. Runs
    // before the self-test block below so a pfa.AutoTest=1 run's
    // NestRegistryConsistent test sees an already-reconciled state this tick.
    ReconcileNests();

    // pfa.AutoTest=1 auto-runs the self-test registry once, on the first
    // tick after world load, entirely through this existing heartbeat --
    // no separate engine hook (docs/08 W1.1). Already server-only: BeginPlay
    // returns early for non-authority, so RefreshTick never fires on clients.
    if (!bRanAutoTest && PFADiag::IsAutoTestRequested())
    {
        bRanAutoTest = true;
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] pfa.AutoTest=1: running self-tests on first refresh tick."));
        PFA_RunSelfTests(GetWorld());
    }

    // w26-2: second one-shot, the first tick a live creature exists -- the
    // spatial tests (FeetPlanted / FacingTarget) can only assert against a
    // real posed body, and the first-tick run above always predates one.
    if (bRanAutoTest && !bRanAutoTestWithCreatures && PFADiag::IsAutoTestRequested())
    {
        for (TActorIterator<APFACreatureBase> It(GetWorld()); It; ++It)
        {
            if (*It && !It->IsActorBeingDestroyed())
            {
                bRanAutoTestWithCreatures = true;
                UE_LOG(LogPrimalFauna, Display,
                    TEXT("[PFA] pfa.AutoTest=1: re-running self-tests now that live creatures exist (spatial tests need a posed body)."));
                PFA_RunSelfTests(GetWorld());
                break;
            }
        }
    }

    // W1.4 review W2: re-attempt the BT borrow if discovery raced a still-
    // scanning asset registry and saw ZERO assets (null tree with a non-zero
    // census = heuristic miss, retrying cannot help). Cap keeps a genuinely
    // empty registry from spamming forever.
    if (!mBorrowedBehaviorTree && BTDiscoveredCount == 0 && BTBorrowAttempts < MaxBTBorrowAttempts)
    {
        ++BTBorrowAttempts;
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] BT borrow retry %d/%d (previous discovery found 0 assets)"),
            BTBorrowAttempts, MaxBTBorrowAttempts);
        const PFABehaviorBorrower::FResult BorrowResult = PFABehaviorBorrower::DiscoverAndBorrow(GetWorld());
        mBorrowedBehaviorTree = BorrowResult.ChosenTree;
        BTDiscoveredCount = BorrowResult.DiscoveredCount;
        BTSearchRoot = BorrowResult.SearchRoot;
    }

    if (TickCount % HeartbeatEveryNTicks == 0)
    {
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Heartbeat #%d"), TickCount);
    }
}

bool APFASubsystem::EnsureLootRecipeAvailable()
{
    ++LootRecipeReadyAttempts;
    AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(GetWorld());
    const TSubclassOf<UFGRecipe> RecipeClass = UPFARecipe_ProteinSicklejaw::StaticClass();
    if (!RecipeManager)
    {
        if (LootRecipeReadyAttempts == 5)
        {
            UE_LOG(LogPrimalFauna, Warning,
                TEXT("[PFA] Loot recipe availability: attempt=%d manager=null recipe=%s decision=retry outcome=not-ready"),
                LootRecipeReadyAttempts, *GetNameSafe(RecipeClass));
        }
        return false;
    }

    const bool bRegistered = RecipeManager->GetAllRecipes().Contains(RecipeClass);
    const bool bWasAvailable = RecipeManager->IsRecipeAvailable(RecipeClass);
    if (!bRegistered)
    {
        if (LootRecipeReadyAttempts == 5)
        {
            UE_LOG(LogPrimalFauna, Warning,
                TEXT("[PFA] Loot recipe availability: attempt=%d registered=no available=%s recipe=%s decision=retry outcome=registry-not-ready"),
                LootRecipeReadyAttempts, bWasAvailable ? TEXT("yes") : TEXT("no"), *GetNameSafe(RecipeClass));
        }
        return false;
    }

    if (!bWasAvailable)
    {
        RecipeManager->AddAvailableRecipe(RecipeClass);
    }

    const bool bNowAvailable = RecipeManager->IsRecipeAvailable(RecipeClass);
    if (bNowAvailable)
    {
        UE_LOG(LogPrimalFauna, Display,
            TEXT("[PFA] Loot recipe availability: attempt=%d registered=yes previouslyAvailable=%s decision=%s outcome=available recipe=%s"),
            LootRecipeReadyAttempts,
            bWasAvailable ? TEXT("yes") : TEXT("no"),
            bWasAvailable ? TEXT("verify") : TEXT("grant"),
            *GetNameSafe(RecipeClass));
    }
    else
    {
        UE_LOG(LogPrimalFauna, Error,
            TEXT("[PFA] Loot recipe availability: attempt=%d registered=yes previouslyAvailable=%s decision=%s outcome=FAILED recipe=%s"),
            LootRecipeReadyAttempts,
            bWasAvailable ? TEXT("yes") : TEXT("no"),
            bWasAvailable ? TEXT("verify") : TEXT("grant"),
            *GetNameSafe(RecipeClass));
    }
    return bNowAvailable;
}

void APFASubsystem::PostLoadGame_Implementation(int32 saveVersion, int32 gameVersion)
{
    // Doc 03 §4 [assumption -> slice test]: NestRegistry (below) is already
    // deserialized by this point -- SaveGame properties load before
    // PostLoadGame_Implementation runs -- so "records" is trustworthy.
    // "preexisting-live" is NOT equally trustworthy by construction: it is a
    // TActorIterator sweep taken at THIS exact point in SML's load sequence,
    // which is precisely the unknown doc 03 §4 flags (do dynamically-spawned
    // save-interface actors restore before or after a mod subsystem's own
    // PostLoadGame_Implementation?). If APFANestSpawners load before this,
    // PreexistingLive will already show them; if they load after, this reads
    // 0 even on a save that goes on to adopt everything cleanly a moment
    // later in RefreshTick's first ReconcileNests() pass (whose own
    // "[PFA] Nests: ..." census line is the second half of this evidence).
    // Do not read a 0 here in isolation as proof persistence failed --
    // compare it against the first census line that follows.
    const UWorld* World = GetWorld();
    int32 PreexistingLive = 0;
    if (World)
    {
        for (TActorIterator<APFANestSpawner> It(World); It; ++It)
        {
            if (*It)
            {
                ++PreexistingLive;
            }
        }
    }

    // Contract grammar -- docs/08 W1.5 packet: the Phase-1 gate reads this
    // exact line shape. Do not reformat.
    UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Nest persistence: records=%d preexisting-live=%d"),
        NestRegistry.Num(), PreexistingLive);
}

void APFASubsystem::ReconcileNests()
{
    // Both call sites (RefreshTick, SpawnNestAtPlayer) are already
    // server-only; this is a no-op safety belt, not load-bearing -- same
    // reasoning PFA_RunSelfTests documents for its own NM_Client guard.
    if (!HasAuthority())
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // ADOPT (doc 03 §3 step 1): map every live nest spawner to its NestId.
    // Rebuilt fresh every pass -- correctly drops stale pointers to
    // despawned/GC'd actors with no extra bookkeeping, and cheap at the nest
    // counts W1.5-W3 will ever see.
    TMap<FName, APFANestSpawner*> LiveByNestId;
    for (TActorIterator<APFANestSpawner> It(World); It; ++It)
    {
        APFANestSpawner* Spawner = *It;
        if (Spawner)
        {
            // W1.5 review I2: a silent overwrite here would hide two live
            // spawners sharing a NestId (orphan/adopt-by-name debris) until
            // the self-test ran -- make the reconcile pass itself loud.
            if (APFANestSpawner** AlreadyMapped = LiveByNestId.Find(Spawner->NestId))
            {
                UE_LOG(LogPrimalFauna, Warning,
                    TEXT("[PFA] Nests: DUPLICATE live spawners share NestId %s ('%s' displaced by '%s' in the adopt map) -- investigate orphan."),
                    *Spawner->NestId.ToString(), *(*AlreadyMapped)->GetName(), *Spawner->GetName());
            }
            LiveByNestId.Add(Spawner->NestId, Spawner);
        }
    }

    int32 AdoptedCount = 0;
    int32 MaterializedCount = 0;

    for (FPFANestRecord& Record : NestRegistry)
    {
        if (Record.bDormant)
        {
            continue; // W2.3 wires real dormancy; nothing to reconcile yet.
        }

        if (LiveByNestId.Contains(Record.NestId))
        {
            ++AdoptedCount;
            continue;
        }

        // MATERIALIZE (doc 03 §3 step 2, doc 06 S1/S2).
        UClass* ResolvedClass = Record.SpeciesClass.TryLoadClass<AFGCreature>();
        if (!ResolvedClass)
        {
            UE_LOG(LogPrimalFauna, Error,
                TEXT("[PFA] Nest %s: cannot materialize -- SpeciesClass '%s' failed to load."),
                *Record.NestId.ToString(), *Record.SpeciesClass.ToString());
            continue;
        }

        const FName DeterministicName(*FString::Printf(TEXT("PFANest_%s"), *Record.NestId.ToString()));
        const FTransform SpawnTransform(FRotator::ZeroRotator, Record.Location);

        // W1.5 review W1 (fix A1): if an actor already CARRIES the deterministic
        // name -- the native-persistence success case where the spawner actor
        // restored from the save but its NestId SaveGame field failed to
        // round-trip, so the id-based adopt pass above missed it -- ADOPT IT BY
        // NAME and repair the ids instead of spawning a colliding duplicate.
        // Under the engine's default Required_Fatal NameMode a collision is a
        // hard assert on load (LevelActor.cpp:584), and even a rename would
        // break S1's stable GetPathName key (HostileScaling's baseline ->
        // compounding). Prevention beats both failure modes.
        bool bAdoptedByName = false;
        for (TActorIterator<APFANestSpawner> NameIt(World); NameIt; ++NameIt)
        {
            APFANestSpawner* Existing = *NameIt;
            if (Existing && Existing->GetFName() == DeterministicName)
            {
                UE_LOG(LogPrimalFauna, Warning,
                    TEXT("[PFA] Nest %s: adopted-by-name '%s' (NestId did not round-trip through the save; ids repaired -- investigate if this repeats every load)."),
                    *Record.NestId.ToString(), *Existing->GetName());
                Existing->NestId = Record.NestId;
                Existing->RegionId = Record.RegionId;
                ++AdoptedCount;
                bAdoptedByName = true;
                break;
            }
        }
        if (bAdoptedByName)
        {
            continue;
        }

        // Deferred spawn (bDeferConstruction) so ConfigureNest can set every
        // protected/public field BEFORE FinishSpawning() runs BeginPlay -- see
        // PFANestSpawner.h's ConfigureNest comment for why this ordering
        // matters against a closed-source AFGCreatureSpawner::BeginPlay we
        // cannot inspect. This is the standard UE "configure before BeginPlay"
        // pattern (the same one SpawnActorFromClass + FinishSpawningActor
        // gives Blueprints), applied here in C++ because the raw templated
        // SpawnActorDeferred<T> helper on UWorld does not expose a Name param.
        FActorSpawnParameters SpawnParams;
        SpawnParams.Name = DeterministicName;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParams.bDeferConstruction = true;
        // W1.5 review W1 (belt to fix A1): never let a residual name collision
        // hard-assert (engine default is Required_Fatal); Requested renames
        // instead, which the GetFName() guard below then reports as the S1
        // violation it is. The adopt-by-name pass above should make this
        // unreachable -- the belt exists for the case we didn't predict.
        SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

        APFANestSpawner* NewSpawner = Cast<APFANestSpawner>(
            World->SpawnActor(APFANestSpawner::StaticClass(), &SpawnTransform, SpawnParams));

        if (!NewSpawner)
        {
            UE_LOG(LogPrimalFauna, Error,
                TEXT("[PFA] Nest %s: SpawnActor<APFANestSpawner> FAILED at %s."),
                *Record.NestId.ToString(), *Record.Location.ToCompactString());
            continue;
        }

        if (NewSpawner->GetFName() != DeterministicName)
        {
            // S1 violation guard: a name collision silently renamed the
            // actor. GetPathName() (HostileScaling's baseline key, doc 06 S1)
            // is no longer the deterministic one we intended -- loud because
            // a later session could re-baseline from stale mSpawnData and
            // compound (doc 06 §4's mandatory 2-cycle compounding check).
            UE_LOG(LogPrimalFauna, Error,
                TEXT("[PFA] Nest %s: actor name collision -- wanted '%s', got '%s'. S1 (stable names) may be violated."),
                *Record.NestId.ToString(), *DeterministicName.ToString(), *NewSpawner->GetName());
        }

        NewSpawner->ConfigureNest(Record.NestId, Record.RegionId, TSubclassOf<AFGCreature>(ResolvedClass),
            Record.CreaturesMin, Record.CreaturesMax, Record.SpawnRadius,
            /*bInCanSpawnDuringDay=*/ true, /*bInCanSpawnDuringNight=*/ true, // TODO(W2.5/W3.2): per-species day/night from FPFANestSpec -- Duskstalker's night gating (doc 03 intro) needs this wired to real data
            Record.RespawnDays);

        NewSpawner->FinishSpawning(SpawnTransform);

        NewSpawner->RegisterAsNavigationInvoker(true);

        // W1.5 review W2 (fix A2): the closed-source BeginPlay (ran inside
        // FinishSpawning above) may have populated spawn data already -- a
        // second populate could silently double mSpawnData (S2's hazard
        // class). Populate only when empty; log both counts so a double would
        // be visible from the log alone.
        const int32 PrePopulateCount = NewSpawner->GetTotalNumCreatures();
        bool bPopulated = false;
        if (PrePopulateCount == 0)
        {
            bPopulated = NewSpawner->PopulateSpawnData();
        }
        const int32 PostPopulateCount = NewSpawner->GetTotalNumCreatures();

        UE_LOG(LogPrimalFauna, Display,
            TEXT("[PFA] Nest %s: materialized %s at %s, PopulateSpawnData=%s prePopulate=%d postPopulate=%d"),
            *Record.NestId.ToString(), *NewSpawner->GetName(), *Record.Location.ToCompactString(),
            bPopulated ? TEXT("true") : TEXT("false"), PrePopulateCount, PostPopulateCount);

        ++MaterializedCount;
    }

    // "live" = AdoptedCount + MaterializedCount: both counters only increment
    // on a VERIFIED success (a live-map hit, or a non-null post-spawn
    // pointer) -- never on a continue'd failure path -- so this sum is an
    // honest total, not an optimistic one; a SpawnActor failure above shows
    // up here as a genuine shortfall against NestRegistry.Num().
    const int32 LiveCount = AdoptedCount + MaterializedCount;

    const bool bAnyChangeThisPass = (AdoptedCount > 0 || MaterializedCount > 0);
    if (bAnyChangeThisPass || !bHasReconciledNestsOnce)
    {
        // Contract grammar -- docs/08 W1.5 packet: the Phase-1 gate reads this
        // exact line shape. Do not reformat.
        UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Nests: records=%d live=%d adopted=%d materialized=%d"),
            NestRegistry.Num(), LiveCount, AdoptedCount, MaterializedCount);

        if (PFADiag::IsEnabled())
        {
            for (const FPFANestRecord& Record : NestRegistry)
            {
                UE_LOG(LogPrimalFauna, Display,
                    TEXT("[PFA]   Nest %s region=%s loc=%s species=%s creatures=%d-%d radius=%.0f respawnDays=%d dormant=%s"),
                    *Record.NestId.ToString(), *Record.RegionId.ToString(), *Record.Location.ToCompactString(),
                    *Record.SpeciesClass.ToString(), Record.CreaturesMin, Record.CreaturesMax,
                    Record.SpawnRadius, Record.RespawnDays, Record.bDormant ? TEXT("true") : TEXT("false"));
            }
        }
    }
    bHasReconciledNestsOnce = true;
}

bool APFASubsystem::SpawnNestAtPlayer(FString& OutDetail)
{
    if (!HasAuthority())
    {
        OutDetail = TEXT("no authority (server-only)");
        return false;
    }

    UWorld* World = GetWorld();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
    if (!PlayerPawn)
    {
        OutDetail = TEXT("no local player pawn to spawn near");
        return false;
    }

    const FVector PlayerLocation = PlayerPawn->GetActorLocation();

    // Simple downward line trace (docs/08 W1.5 scope adjustment -- no water
    // check / candidate sampling here, that is W2.1's PFA.DumpCandidates
    // authoring workflow, doc 03 §2).
    FHitResult Hit;
    FCollisionQueryParams TraceParams(FName(TEXT("PFANestGroundTrace")), false, PlayerPawn);
    const FVector TraceStart = PlayerLocation + FVector(0.0f, 0.0f, GroundTraceUpCm);
    const FVector TraceEnd = PlayerLocation - FVector(0.0f, 0.0f, GroundTraceDownCm);
    const bool bHitGround = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, TraceParams);
    const FVector GroundLocation = bHitGround ? Hit.ImpactPoint : PlayerLocation;

    if (!bHitGround)
    {
        UE_LOG(LogPrimalFauna, Warning,
            TEXT("[PFA] PFA.SpawnNest: ground trace found nothing under the player (range +%.0f/-%.0fcm) -- using ungrounded player location %s."),
            GroundTraceUpCm, GroundTraceDownCm, *PlayerLocation.ToCompactString());
    }

    // "next free idx" (docs/08 packet): append-only in W1.5 (no deactivation/
    // removal exists yet), so counting existing "Test"-region records IS the
    // next free index.
    int32 NextIdx = 0;
    for (const FPFANestRecord& Existing : NestRegistry)
    {
        if (Existing.RegionId == TestRegionId)
        {
            ++NextIdx;
        }
    }

    FPFANestRecord NewRecord;
    NewRecord.NestId = FName(*FString::Printf(TEXT("%s_%d"), *TestRegionId.ToString(), NextIdx));
    NewRecord.RegionId = TestRegionId;
    NewRecord.Location = GroundLocation;
    NewRecord.SpeciesClass = FSoftClassPath(APFASicklejaw::StaticClass());
    NewRecord.CreaturesMin = TestNestCreaturesMin;
    NewRecord.CreaturesMax = TestNestCreaturesMax;
    NewRecord.SpawnRadius = TestNestSpawnRadiusCm;
    NewRecord.RespawnDays = TestNestRespawnDaysDefault;
    NewRecord.bDormant = false;

    NestRegistry.Add(NewRecord);

    UE_LOG(LogPrimalFauna, Display,
        TEXT("[PFA] PFA.SpawnNest: appended record %s (region=%s) at %s (groundTrace=%s). Reconciling immediately."),
        *NewRecord.NestId.ToString(), *NewRecord.RegionId.ToString(), *GroundLocation.ToCompactString(),
        bHitGround ? TEXT("hit") : TEXT("miss-fallback"));

    // Doc 03 §3: ONE code path -- append the record, then run the exact same
    // reconciliation RefreshTick runs every 10s, just triggered immediately so
    // the tester doesn't wait (docs/08 W1.5 packet spec). No special-cased
    // direct-spawn branch here.
    ReconcileNests();

    OutDetail = FString::Printf(TEXT("NestId=%s at %s"), *NewRecord.NestId.ToString(), *GroundLocation.ToCompactString());
    return true;
}

bool APFASubsystem::CheckNestRegistryConsistency(FString& OutDetail) const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        OutDetail = TEXT("no world context");
        return false;
    }

    const int32 Records = NestRegistry.Num();
    if (Records == 0)
    {
        // docs/08 packet spec: "0 records is PASS 'no nests yet'" -- trivial
        // and unconditional, deliberately not cross-checked against orphan
        // live spawners (that class of check is doc 03 §3.3 deactivation
        // territory, out of scope for W1.5).
        OutDetail = TEXT("0 records (no nests yet)");
        return true;
    }

    TSet<FName> RegistryIds;
    int32 DormantCount = 0;
    for (const FPFANestRecord& Record : NestRegistry)
    {
        RegistryIds.Add(Record.NestId);
        if (Record.bDormant)
        {
            ++DormantCount;
        }
    }

    int32 LiveMatchedCount = 0;
    int32 LiveUnmatchedCount = 0;
    for (TActorIterator<APFANestSpawner> It(World); It; ++It)
    {
        APFANestSpawner* Spawner = *It;
        if (!Spawner)
        {
            continue;
        }
        if (RegistryIds.Contains(Spawner->NestId))
        {
            ++LiveMatchedCount;
        }
        else
        {
            ++LiveUnmatchedCount;
        }
    }

    // docs/08 packet spec: "records count == live+dormant match, every live
    // APFANestSpawner has a matching record".
    const bool bAllRecordsAccounted = (Records == LiveMatchedCount + DormantCount);
    const bool bNoOrphanSpawners = (LiveUnmatchedCount == 0);

    OutDetail = FString::Printf(TEXT("records=%d live=%d dormant=%d unmatchedLiveSpawners=%d"),
        Records, LiveMatchedCount, DormantCount, LiveUnmatchedCount);
    return bAllRecordsAccounted && bNoOrphanSpawners;
}

// ---- Self-test: registry consistency (idiom from PFACheats.cpp). Read-only
// -- safe at any tick, including tick 0 with an empty registry. ----
namespace
{
    const bool bRegistered_NestRegistryConsistent = []
    {
        RegisterPFASelfTest({TEXT("NestRegistryConsistent"),
            [](UWorld* World, FString& OutDetail) -> bool
            {
                for (TActorIterator<APFASubsystem> It(World); It; ++It)
                {
                    return It->CheckNestRegistryConsistency(OutDetail);
                }
                OutDetail = TEXT("no APFASubsystem actor found in world");
                return false;
            }});
        return true;
    }();
}
