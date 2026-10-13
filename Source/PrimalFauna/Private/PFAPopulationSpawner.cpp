#include "PFANestSpawner.h"
#include "PFACreatureController.h"
#include "PFAPopulationPolicy.h"
#include "PFACheats.h"
#include "PrimalFauna.h"
#include "Templates/UnrealTemplate.h"
#include "Creature/FGCreature.h"
#include "FGHealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Patching/NativeHookManager.h"

void PFAInstallPopulationSpawnGuards()
{
    // These three are NONVIRTUAL in the verified FGCreatureSpawner.h. Hooks
    // short-circuit only our explicitly managed nests, preserving every vanilla
    // path and HostileScaling's count/respawn ownership. SpawnSingleCreature also
    // fences native tick retries for requests that predate a resource reroll or
    // config disable; the shipping Tick calls that entry point directly.
    // Do not hook CanSpawnCreature: the shipping prologue contains a short
    // conditional branch that this SML/funchook build relocates incorrectly
    // (w59-2 crash dump, 2026-09-26). It only queries slot eligibility; fencing
    // the actual spawn entry points below preserves the population restrictions.
    SUBSCRIBE_METHOD(AFGCreatureSpawner::IsReadyToSpawn, [](auto& Scope, AFGCreatureSpawner* Self)
    {
        APFANestSpawner* Nest = Cast<APFANestSpawner>(Self);
        if (Nest && !Nest->CanPopulationSpawn()) { Scope.Override(false); }
        else if (Nest)
        {
            TGuardValue<int32> NativeCall(Nest->PopulationNativeCallDepth, Nest->PopulationNativeCallDepth + 1);
            Scope(Self);
        }
    });
    SUBSCRIBE_METHOD(AFGCreatureSpawner::SpawnCreatures, [](auto& Scope, AFGCreatureSpawner* Self)
    {
        APFANestSpawner* Nest = Cast<APFANestSpawner>(Self);
        if (Nest && !Nest->CanPopulationSpawn()) { Scope.Cancel(); }
        else if (Nest)
        {
            TGuardValue<int32> NativeCall(Nest->PopulationNativeCallDepth, Nest->PopulationNativeCallDepth + 1);
            Scope(Self);
        }
    });
    SUBSCRIBE_METHOD(AFGCreatureSpawner::SpawnSingleCreature, [](auto& Scope, AFGCreatureSpawner* Self)
    {
        APFANestSpawner* Nest = Cast<APFANestSpawner>(Self);
        if (Nest && !Nest->CanPopulationSpawn()) { Scope.Cancel(); }
        else if (Nest)
        {
            TGuardValue<int32> NativeCall(Nest->PopulationNativeCallDepth, Nest->PopulationNativeCallDepth + 1);
            Scope(Self);
        }
    });
}

bool APFANestSpawner::QuiescePopulationSpawning(const TCHAR* Reason)
{
    if (!HasAuthority() || !IsInGameThread() || !bPopulationManaged) { return false; }
    bPopulationAllowed = false;
    bPopulationWandering = false;
    bHasPatrolGoal = false;
    const bool bWasActive = IsSpawnerActive();
    const int32 Cancelled = PFAPopulationPolicy::QuiesceLocationRetries(
        bPopulationManaged, bPopulationAllowed, PopulationNativeCallDepth, mSpawnData,
        [this]()
        {
            // The verified native StopSpawning has exactly three effects:
            // active=false, unregister invoker, disable tick. Preserve the
            // invoker here for live packs and empty-nest navigation bootstrap;
            // unregister only after safe retirement. No native call is in flight.
            mIsActive = false;
            SetActorTickEnabled(false);
            return !IsSpawnerActive() && !IsActorTickEnabled();
        });
    if (PFADiag::IsEnabled() && (Cancelled != 0 || bWasActive))
    {
        UE_LOG(LogPrimalFauna, Display,
            TEXT("[PFA] Population quiesce: nest=%s reason=%s activeBefore=%d nativeDepth=%d slots=%d cancelledRetries=%d outcome=%s"),
            *NestId.ToString(), Reason, bWasActive, PopulationNativeCallDepth, mSpawnData.Num(),
            FMath::Max(0, Cancelled), Cancelled < 0 ? TEXT("deferred-native-call-or-stop") : TEXT("quiescent-history-preserved"));
    }
    return Cancelled >= 0;
}

bool APFANestSpawner::HasPopulationSpawnRequests() const
{
    for (const FSpawnData& Slot : mSpawnData)
    {
        if (Slot.WaitingForSpawnLocation) { return true; }
    }
    return false;
}

bool APFANestSpawner::IsPopulationEngaged() const
{
    for (const FSpawnData& Slot : mSpawnData)
    {
        AFGCreature* Creature = Slot.Creature;
        if (!IsValid(Creature) || !Creature->GetHealthComponent() || Creature->GetHealthComponent()->IsDead()) { continue; }
        const AFGCreatureController* Controller = Cast<AFGCreatureController>(Creature->GetController());
        // An unpossessed live creature is not safe to silently relocate/delete.
        if (!Controller || Controller->GetCurrentTarget()) { return true; }
        const ECreatureState State = Controller->GetCurrentCreatureState();
        if (State != ECreatureState::CS_Default && State != ECreatureState::CS_Roam) { return true; }
        for (const FFGCreatureAggroData& Aggro : Controller->GetAggroTargets())
        {
            if (Aggro.Aggro > KINDA_SMALL_NUMBER) { return true; }
        }
    }
    return false;
}

bool APFANestSpawner::CanRetirePopulation() const
{
    double Nearest = TNumericLimits<double>::Max();
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC = It->Get();
        if (!PC || !PC->GetPawn()) { continue; }
        const FVector Player = PC->GetPawn()->GetActorLocation();
        Nearest = FMath::Min(Nearest, FVector::Dist(Player, GetActorLocation()));
        for (const FSpawnData& Slot : mSpawnData)
        {
            if (IsValid(Slot.Creature))
            {
                Nearest = FMath::Min(Nearest, FVector::Dist(Player, Slot.Creature->GetActorLocation()));
            }
        }
    }
    return PFAPopulationPolicy::SafeToRetire(Nearest, IsPopulationEngaged(), HasPopulationSpawnRequests(),
        PopulationNativeCallDepth, IsSpawnerActive());
}

bool APFANestSpawner::CanRelocatePopulation() const
{
    return PFAPopulationPolicy::CanRelocateSlots(CanRetirePopulation(), mSpawnData,
        [](const auto& Creature) { return IsValid(Creature); });
}

FPathFollowingRequestResult APFACreatureController::MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath)
{
    const AFGCreature* Creature = Cast<AFGCreature>(GetPawn());
    const APFANestSpawner* Nest = Creature ? Cast<APFANestSpawner>(Creature->GetSpawner()) : nullptr;
    if (Nest && Nest->bPopulationWandering && Nest->bHasPatrolGoal && !GetCurrentTarget()
        && !MoveRequest.IsMoveToActorRequest()
        && GetCurrentCreatureState() == ECreatureState::CS_Roam && !Nest->IsPopulationEngaged())
    {
        // The native tree issues movement at its normal cadence. Replace only
        // an idle roaming goal; leave chase, attack, flee and return-home intact.
        FAIMoveRequest Patrol(MoveRequest);
        Patrol.UpdateGoalLocation(Nest->PopulationPatrolGoal);
        Patrol.SetAllowPartialPath(false);
        Patrol.SetProjectGoalLocation(false); // already projected and path-checked
        return Super::MoveTo(Patrol, OutPath);
    }
    return Super::MoveTo(MoveRequest, OutPath);
}
