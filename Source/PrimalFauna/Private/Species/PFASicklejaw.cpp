#include "Species/PFASicklejaw.h"
#include "PrimalFauna.h"
#include "PFACheats.h"
#include "PFACreatureController.h"

#include "FGHealthComponent.h"
#include "FGCreatureSubsystem.h"
#include "Creature/FGCreature.h"

#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

namespace
{
	// ---- TODO(W1.2-census): every constant below is a PLACEHOLDER anchor
	// ratio per docs/01-creature-roster.md §1 -- replace once PFA.DumpCreature
	// census data on live vanilla creatures (Alpha Hog, Stinger) lands. ----
	constexpr float SicklejawBaseHealth = 200.0f;                 // doc 08 packet spec: "~Alpha Hog HP"

	constexpr float SicklejawWalkSpeedCms = 400.0f;
	constexpr float SicklejawRunSpeedCms = 600.0f;
	constexpr float SicklejawSprintSpeedCms = 780.0f;              // doc 01 §1: "~1.3x player sprint"

	constexpr float SicklejawSightRadiusCm = 4000.0f;              // doc 01 §1: "sight range ~ Stinger"
	constexpr float SicklejawLoseSightRadiusCm = 4500.0f;          // > SightRadius (standard AI-perception convention, avoids flicker)
	constexpr float SicklejawPeripheralVisionAngleDegrees = 90.0f;
	constexpr float SicklejawHearingRangeCm = 3500.0f;             // doc 02 §5: "strong hearing (sprint noise attracts)"

	constexpr float SicklejawMaximumAggroLevel = 100.0f;
	constexpr float SicklejawAggroFightThreshold = 50.0f;
	constexpr float SicklejawAggroGainRate = 10.0f;
	constexpr float SicklejawAggroReductionRate = 5.0f;

	constexpr float SicklejawSpawnDistanceCm = 6000.0f;
	constexpr float SicklejawDespawnDistanceCm = 9000.0f;
	constexpr float SicklejawTerritoryRadiusCm = 3500.0f;          // inert while default mode is Hostile (doc 04 §1); wired for when Territorial is selected

	const TCHAR* PlaceholderMeshPath = TEXT("/PrimalFauna/Placeholder/SK_PFA_Placeholder.SK_PFA_Placeholder");
}

APFASicklejaw::APFASicklejaw(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// --- Identity/stats (doc 07 §3, doc 06 §2 C2: CDO-authored, never runtime-multiplied) ---
	SetBaseHealth(SicklejawBaseHealth);

	mMoveSpeedData.Add(FMoveSpeedPair{EMoveSpeed::MS_Walk, SicklejawWalkSpeedCms});
	mMoveSpeedData.Add(FMoveSpeedPair{EMoveSpeed::MS_Run, SicklejawRunSpeedCms});
	mMoveSpeedData.Add(FMoveSpeedPair{EMoveSpeed::MS_Sprint, SicklejawSprintSpeedCms});

	// --- Perception (doc 02 §5, doc 07 §3: instanced sense configs built in
	// the ctor. UFGAIPerceptionComponent (AI/FGAIPerceptionComponent.h) is
	// FactoryGame's own UAIPerceptionComponent subclass; its closed-source
	// OnRegister() override is the most likely place these get wired into a
	// live sense -- unverifiable statically, see the packet handoff. ---
	UAISenseConfig_Sight* SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SicklejawSightConfig"));
	SightConfig->Implementation = UAISense_Sight::StaticClass();
	SightConfig->SightRadius = SicklejawSightRadiusCm;
	SightConfig->LoseSightRadius = SicklejawLoseSightRadiusCm;
	SightConfig->PeripheralVisionAngleDegrees = SicklejawPeripheralVisionAngleDegrees;
	mPerceptionSettings.SightConfig = SightConfig;

	UAISenseConfig_Hearing* HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("SicklejawHearingConfig"));
	HearingConfig->Implementation = UAISense_Hearing::StaticClass();
	HearingConfig->HearingRange = SicklejawHearingRangeCm;
	mPerceptionSettings.HearingConfig = HearingConfig;

	// --- Aggro pacing (doc 07 §3, FGCreature.h:595-615) ---
	mMaximumAggroLevel = SicklejawMaximumAggroLevel;
	mAggroFightThreshold = SicklejawAggroFightThreshold;
	mAggroGainRate = SicklejawAggroGainRate;
	mAggroReductionRate = SicklejawAggroReductionRate;

	// --- Spawn/despawn (doc 07 §3, FGCreature.h:691-695 -- private, routed
	// through APFACreatureBase's Friend-granted setter) ---
	SetSpawnDespawnDistance(SicklejawSpawnDistanceCm, SicklejawDespawnDistanceCm);

	// --- Territory + default aggression (doc 02 §3, doc 04 §1: Sicklejaw = Hostile) ---
	TerritoryRadiusCm = SicklejawTerritoryRadiusCm;
	mAggressionMode = EPFAAggression::Hostile;

	// --- Flags (doc 07 §3) ---
	mIsArachnid = false; // explicit; matches the engine's own zero-init default
}

void APFASicklejaw::BeginPlay()
{
	Super::BeginPlay();

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		// Should not happen -- ACharacter's own ctor always creates this.
		UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] %s: no USkeletalMeshComponent to assign a mesh to."), *GetName());
		return;
	}

	// sf-skeletal-mesh-import-crash: NEVER a ConstructorHelpers/LoadObject
	// content load in the ctor; defer to BeginPlay and guard on
	// GetSkeleton() so a broken/not-yet-imported placeholder degrades to a
	// meshless-but-functional creature instead of crashing at component
	// registration (cookbook gotcha #33). tools/import_placeholder_ue.py is
	// what creates this asset; running this BEFORE that script has run is
	// the expected, tolerated "not found" case.
	USkeletalMesh* PlaceholderMesh = LoadObject<USkeletalMesh>(nullptr, PlaceholderMeshPath);
	if (!PlaceholderMesh)
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: placeholder mesh %s not found -- creature will run meshless/invisible. Run tools/import_placeholder_ue.py first, or this is expected before that import has run."),
			*GetName(), PlaceholderMeshPath);
		return;
	}

	if (!PlaceholderMesh->GetSkeleton())
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: placeholder mesh loaded but has no Skeleton -- refusing SetSkinnedAsset (sf-skeletal-mesh-import-crash guard). Creature will run meshless."),
			*GetName());
		return;
	}

	MeshComponent->SetSkinnedAsset(PlaceholderMesh);
	UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s: placeholder mesh assigned (%s)."), *GetName(), *PlaceholderMesh->GetName());
}

// ---- PFA.SpawnTest: the "SicklejawSpawnRoundtrip" stretch item from the
// docs/08 W1.3 packet, implemented as an EXPLICIT console command rather
// than an auto-registered self-test. Rationale (see the packet handoff):
// spawning a real creature is a live world mutation, and self-tests can
// auto-run unattended via pfa.AutoTest=1 on every world load -- the existing
// 4 self-tests (PFACheats.cpp) are all read-only/non-mutating, and this
// would be the first to break that invariant. PFA.SpawnNest/PFA.WormForceEvent
// (PFACheats.cpp) are already console commands for the same reason: a
// world-mutating dev action needs explicit human intent, every time. ----
namespace
{
	void SpawnTestCommand(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] PFA.SpawnTest aborted: no world context."));
			return;
		}
		if (World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] PFA.SpawnTest aborted: this is a network client with no authority. Run PFA.SpawnTest from the host/server."));
			return;
		}

		AFGCreatureSubsystem* CreatureSubsystem = AFGCreatureSubsystem::Get(World);
		if (!CreatureSubsystem)
		{
			UE_LOG(LogPrimalFauna, Error, TEXT("[PFA] PFA.SpawnTest FAIL: no AFGCreatureSubsystem in this world."));
			return;
		}

		APlayerController* PC = World->GetFirstPlayerController();
		APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
		if (!PlayerPawn)
		{
			UE_LOG(LogPrimalFauna, Error, TEXT("[PFA] PFA.SpawnTest FAIL: no local player pawn to spawn near / test hostility against."));
			return;
		}

		const FVector SpawnLocation = PlayerPawn->GetActorLocation() + PlayerPawn->GetActorForwardVector() * 500.0f;
		const FTransform SpawnTransform(PlayerPawn->GetActorRotation(), SpawnLocation);

		AFGCreature* SpawnedCreature = CreatureSubsystem->BeginSpawningCreature(APFASicklejaw::StaticClass(), SpawnTransform);
		if (!SpawnedCreature)
		{
			UE_LOG(LogPrimalFauna, Error, TEXT("[PFA] PFA.SpawnTest FAIL: BeginSpawningCreature returned null."));
			return;
		}

		// Recouples the spawner<->creature link so the native kill-orphan
		// timer doesn't fire on a creature spawned without a real spawner
		// (AFGCreature::ClearKillOrphanTimer, FGCreature.h:438).
		SpawnedCreature->ClearKillOrphanTimer();

		// W1.3 review add-on: assert the Friend-granted private writes took
		// hold on a REAL spawned instance (spawn/despawn distances are set in
		// the ctor, so this exercises the write mechanism on every run).
		const bool bDistancesMatch =
			FMath::IsNearlyEqual(SpawnedCreature->GetSpawnDistance(), SicklejawSpawnDistanceCm) &&
			FMath::IsNearlyEqual(SpawnedCreature->GetDespawnDistance(), SicklejawDespawnDistanceCm);
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] PFA.SpawnTest: distance write check spawn=%.1f (expected %.1f) despawn=%.1f (expected %.1f) -> %s"),
			SpawnedCreature->GetSpawnDistance(), SicklejawSpawnDistanceCm,
			SpawnedCreature->GetDespawnDistance(), SicklejawDespawnDistanceCm,
			bDistancesMatch ? TEXT("OK") : TEXT("MISMATCH"));

		AController* Controller = SpawnedCreature->GetController();
		APFACreatureController* PFAController = Cast<APFACreatureController>(Controller);
		if (!PFAController)
		{
			UE_LOG(LogPrimalFauna, Error,
				TEXT("[PFA] PFA.SpawnTest FAIL: possessed controller is %s, expected APFACreatureController. (If this is 'none/unpossessed', BeginSpawningCreature may possess on a later tick -- rerun PFA.SpawnTest a moment later before concluding this is broken.)"),
				Controller ? *Controller->GetClass()->GetName() : TEXT("none/unpossessed"));
			SpawnedCreature->Destroy();
			return;
		}

		const bool bHostileToPlayer = PFAController->CanBeHostileTowards(PlayerPawn);
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] PFA.SpawnTest: spawned %s, controller=%s, CanBeHostileTowards(player)=%s"),
			*SpawnedCreature->GetName(), *PFAController->GetClass()->GetName(),
			bHostileToPlayer ? TEXT("true") : TEXT("false"));

		SpawnedCreature->Destroy();
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] PFA.SpawnTest: destroyed spawned creature. Roundtrip complete."));
	}

	FAutoConsoleCommandWithWorldAndArgs CCmdPFASpawnTest(
		TEXT("PFA.SpawnTest"),
		TEXT("Primal Fauna: spawns a Sicklejaw near the player, verifies its controller class + a live hostility read, then destroys it. Server-side only."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			SpawnTestCommand(World);
		}));
}

// ---- Self-test: entity-free CDO validation (idiom from PFACheats.cpp). ----
namespace
{
	const bool bRegistered_SicklejawCDOValid = []
	{
		RegisterPFASelfTest({TEXT("SicklejawCDOValid"),
			[](UWorld* /*World*/, FString& OutDetail) -> bool
			{
				const APFASicklejaw* Cdo = GetDefault<APFASicklejaw>();
				if (!Cdo)
				{
					OutDetail = TEXT("GetDefault<APFASicklejaw>() returned null");
					return false;
				}

				const UFGHealthComponent* Health = Cdo->GetHealthComponent();
				const float MaxHealth = Health ? Health->GetMaxHealth() : -1.0f;
				const bool bControllerOk = Cdo->AIControllerClass == APFACreatureController::StaticClass();

				OutDetail = FString::Printf(TEXT("MaxHealth=%.1f AIControllerClass=%s AggressionMode=%d"),
					MaxHealth,
					Cdo->AIControllerClass ? *Cdo->AIControllerClass->GetName() : TEXT("none"),
					static_cast<int32>(Cdo->GetAggressionMode()));
				return Health && MaxHealth > 0.0f && bControllerOk;
			}});
		return true;
	}();
}
