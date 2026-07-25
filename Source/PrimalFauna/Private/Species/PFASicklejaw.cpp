#include "Species/PFASicklejaw.h"
#include "PrimalFauna.h"
#include "PFACheats.h"
#include "PFACreatureController.h"
#include "Actions/PFACreatureActionMelee.h"

#include "FGHealthComponent.h"
#include "FGCreatureSubsystem.h"
#include "Creature/FGCreature.h"

#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "TimerManager.h"
#include "FGHealthComponent.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

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

	// w32-1 roam tuning. Wide enough that a creature visibly relocates rather
	// than shuffling, short enough that it stays near its nest; the wait band is
	// deliberately long and jittered so a group doesn't move in lockstep (the
	// same reasoning as the idle-break jitter).
	constexpr float SicklejawRoamMinCm = 600.0f;
	constexpr float SicklejawRoamMaxCm = 2500.0f;
	constexpr float SicklejawRoamWaitMinSec = 4.0f;
	constexpr float SicklejawRoamWaitMaxSec = 14.0f;

	constexpr float SicklejawSpawnDistanceCm = 6000.0f;
	constexpr float SicklejawDespawnDistanceCm = 9000.0f;
	constexpr float SicklejawTerritoryRadiusCm = 3500.0f;          // inert while default mode is Hostile (doc 04 §1); wired for when Territorial is selected

	const TCHAR* PlaceholderMeshPath = TEXT("/PrimalFauna/Placeholder/SK_PFA_Placeholder.SK_PFA_Placeholder");

	// W2.2: the user-approved rebuild (48-bone tail rig, 42.6k tris, 2048
	// textures) imported by tools/import_sicklejaw_ue.py. Primary mesh; the
	// placeholder above stays as the graceful fallback.
	const TCHAR* SicklejawMeshPath = TEXT("/PrimalFauna/Sicklejaw/SK_Sicklejaw.SK_Sicklejaw");

	// w18-8: designed standing height (doc 01 §1, matches the 1.65m rig the
	// asset was authored at) -- the auto-scale target for the mis-scaled
	// placeholder (FBX meter-units imported as cm; see AssignPlaceholderMesh).
	constexpr float SicklejawTargetHeightCm = 165.0f;

	// w18-8: live override for the placeholder mesh scale factor. 0 (default)
	// = AUTO (target height / measured mesh height, clamped 1..500); >0 =
	// explicit multiplier. Read at assign time: change it, then PFA.KillAll +
	// PFA.SpawnNest to see the new size -- no rebuild (fp-weapon live-tune
	// pattern).
	static TAutoConsoleVariable<float> CVarPFAPlaceholderMeshScale(
		TEXT("pfa.PlaceholderMeshScale"), 0.0f,
		TEXT("Placeholder Sicklejaw mesh scale factor. 0 = auto (165cm target / measured mesh height). >0 = explicit multiplier."),
		ECVF_Default);

	// w23-2 (review hardening #3): live vertical-plant adjustment. The feet
	// formula plants the BOUNDING-BOX bottom at the capsule bottom; a 48-bone
	// ref pose whose tail droops below the feet plane lifts the body by the
	// tail-to-feet gap (creature floats). This knob turns that residual
	// visual failure into a console tweak + respawn instead of a rebuild.
	// w32-2: yaw applied to the mesh component so the creature's authored
	// forward (+Y in the rig: Jaw at y=+0.37, Tail05 at y=-0.67, all bones at
	// x=0) lines up with the actor's forward (+X). -90 maps +Y onto +X.
	// Live-tunable because "does it face me" is an eyeball judgement: respawn
	// after changing, then bake the winning value into the default here.
	static TAutoConsoleVariable<float> CVarPFAMeshYaw(
		TEXT("pfa.MeshYaw"), -90.0f,
		TEXT("Yaw (deg) applied to the Sicklejaw mesh component so its authored +Y forward matches the actor's +X forward. -90 is the derived value; try 90/0/180 if the creature still faces wrong. Respawn to apply."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarPFAMeshZOffset(
		TEXT("pfa.MeshZOffset"), 0.0f,
		TEXT("Additional Z offset (cm) applied to the Sicklejaw mesh after feet planting. Negative sinks, positive lifts. Respawn to apply."),
		ECVF_Default);

	// w24-4: live kill-switch for locomotion playback (session-14 finding:
	// the FIRST real PlayAnimation visually destroyed the mesh -- creatures
	// present/on-surface/animating per log, invisible to the eye; suspected
	// anim-vs-mesh unit-scale mismatch launching bones ~166x out of position).
	// 0 = skip arming (visible statues) while the anim DATA is fixed.
	static TAutoConsoleVariable<int32> CVarPFALocomotionAnims(
		TEXT("pfa.LocomotionAnims"), 1,
		TEXT("1 (default): play idle/walk/run on Sicklejaws. 0: skip animation playback (statues) -- escape hatch while anim data is fixed. Respawn to apply."),
		ECVF_Default);

	// w26-1 foot-plant keeper tuning. Tolerance is a hysteresis band (stops
	// the 1-gain correction oscillating); the cap bounds any pathological
	// bounds reading to something visibly wrong-but-not-launched.
	// 3cm was too tight (w26-2 review): physics-asset bounds track the GAIT,
	// so normal body bob -- multiplied into world space by ScaleFactor --
	// would make the keeper shove the mesh up and down 5x/second, trading
	// "sunk" for "twitching". Correct GROSS mis-planting only, and stay
	// below the FeetPlanted self-test's 25cm tolerance so the test still
	// guards what the keeper leaves behind.
	constexpr float FootPlantToleranceCm = 12.0f;
	constexpr float FootPlantMaxCorrectionCm = 300.0f;

	// w26-1: live kill-switch for the running foot-plant correction (the
	// static w18-8 offset remains if disabled). Same live-tune pattern as
	// pfa.MeshZOffset / pfa.LocomotionAnims.
	static TAutoConsoleVariable<int32> CVarPFAFootPlantAuto(
		TEXT("pfa.FootPlantAuto"), 1,
		TEXT("1 (default): each locomotion tick, keep the animated mesh's lowest point at the capsule bottom. 0: static assign-time offset only."),
		ECVF_Default);

	// w24-2 locomotion anim PACKAGES. w24-1 in-game evidence (MISSING x3
	// despite the assets being in the cooked pak): the FBX anim import put
	// the MESH payload into each primary package (five ~611KB cooked twins of
	// the mesh) and the actual takes into suffixed sibling assets
	// (*_Anim_preset_* / *_Anim_Armature_preset_*, proper 5-16KB cooked anim
	// sizes) -- and LoadObject<UAnimSequence> rightly nulls on a mesh-classed
	// object. The import script's skeleton-match check was fooled because
	// `skeleton` is a property on BOTH classes. Fix: don't guess object
	// names -- load each candidate PACKAGE and scan it for whatever
	// UAnimSequence it actually contains (FindSequenceInPackages below).
	// Candidates orderd primary-first so a future clean re-import wins.
	const TCHAR* SicklejawIdleAnimPackages[] = {
		TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_Idle"),
		TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_Idle_Anim_Armature_preset_idle"),
		TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_Idle_Anim_preset_idle"),
	};
	const TCHAR* SicklejawWalkAnimPackages[] = {
		TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_Walk"),
		TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_Walk_Anim_Armature_preset_walk"),
		TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_Walk_Anim_preset_walk"),
	};
	const TCHAR* SicklejawRunAnimPackages[] = {
		TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_Run"),
	};

	// w28-1: the STATE clips. The driver has played idle/walk/run since w24;
	// these are what make the creature read differently depending on what it
	// is doing -- a calm roamer, a creature that has noticed you, and one
	// that has decided to kill you should not share one idle.
	const TCHAR* SicklejawIdleAlertAnimPackages[] = {
		TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_IdleAlert"),
	};
	const TCHAR* SicklejawIdleAggressiveAnimPackages[] = {
		TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_IdleAggressive"),
	};
	const TCHAR* SicklejawProwlAnimPackages[] = {
		TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_Prowl"),
	};

	// Prowl is authored at an implied 110 cm/s (validator-measured), so it
	// owns the slow band. Anything between prowl and run speed is a walk.
	constexpr float LocomotionProwlThresholdCms = 30.0f;
	constexpr float LocomotionWalkBandStartCms = 200.0f;

	// Speed thresholds (cm/s) for the idle/walk/run pick. Walk 400 / run 600
	// per the ctor's authored speeds; the boundaries sit between states.
	constexpr float LocomotionWalkThresholdCms = 30.0f;
	constexpr float LocomotionRunThresholdCms = 480.0f;
	constexpr float LocomotionTickSeconds = 0.2f;
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

	// w32-1 (user, in game): creatures stood rooted until aggroed. Roam distance
	// defaults to (0,0) on a C++-only CDO, so the roam behaviour was picking a
	// destination zero centimetres away. A pack predator should drift.
	// TODO(W1.2-census): anchors, like every other number in this ctor. A
	// vanilla-creature census would tell us what a Hog or Stinger actually uses.
	SetRoaming(SicklejawRoamMinCm, SicklejawRoamMaxCm, SicklejawRoamWaitMinSec, SicklejawRoamWaitMaxSec);

	// --- Territory + default aggression (doc 02 §3, doc 04 §1: Sicklejaw = Hostile) ---
	TerritoryRadiusCm = SicklejawTerritoryRadiusCm;
	mAggressionMode = EPFAAggression::Hostile;

	// --- Flags (doc 07 §3) ---
	mIsArachnid = false; // explicit; matches the engine's own zero-init default

	// --- Attacks (doc 07 §5, doc 08 W1.4): the bite. UPFACreatureActionMelee is
	// our subclass of UFGCreatureActionMeleeAttack (its tuning fields are
	// protected -- see that class for why we subclass instead of grant/reflect).
	// Created as an instanced default subobject (matching how vanilla creatures
	// author actions), wrapped in an FCreatureAction enabled for the Fight state
	// (FGCreature.h:106,149), and appended to mCreatureActionsSetup (:584 --
	// protected, inherited, set directly like mMoveSpeedData/mPerceptionSettings
	// above). Montage-less: the action carries no montage (docs/08 §4 assumption
	// 2). Pounce/leap (doc 01 §1b) is a W2 fast-follow, not this packet. ---
	UPFACreatureActionMelee* BiteAction = CreateDefaultSubobject<UPFACreatureActionMelee>(TEXT("SicklejawMeleeAction"));
	FCreatureAction BiteEntry;
	BiteEntry.Action = BiteAction;
	BiteEntry.RequiredStates = static_cast<uint8>(ECreatureState::CS_Fight);
	BiteEntry.StartsWithCooldown = false;
	BiteEntry.ActionName = TEXT("SicklejawMelee"); // cosmetic (array display / dumps)
	mCreatureActionsSetup.Add(BiteEntry);
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
	// W2.2: real asset first, placeholder as loud fallback -- a missing or
	// broken rebuild import degrades to the proven dummy (visible, testable)
	// instead of silence, and the log states which asset is live. The
	// variable keeps its placeholder-era name to minimize diff churn; it
	// holds whichever asset was chosen.
	// w23-2 (review hardening): the real mesh must be non-null AND carry a
	// skeleton to be accepted -- a skeletonless "broken import" now falls
	// back to the placeholder as the comment promises, instead of slipping
	// past to the meshless guard below.
	bool bRealMeshLive = false;
	USkeletalMesh* PlaceholderMesh = LoadObject<USkeletalMesh>(nullptr, SicklejawMeshPath);
	if (PlaceholderMesh && PlaceholderMesh->GetSkeleton())
	{
		bRealMeshLive = true;
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: REAL Sicklejaw mesh loaded (%s)."), *GetName(), SicklejawMeshPath);
	}
	else
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: real mesh %s %s -- FALLING BACK to the placeholder (run tools/import_sicklejaw_ue.py)."),
			*GetName(), SicklejawMeshPath,
			PlaceholderMesh ? TEXT("loaded but has NO Skeleton") : TEXT("not found"));
		PlaceholderMesh = LoadObject<USkeletalMesh>(nullptr, PlaceholderMeshPath);
	}
	if (!PlaceholderMesh)
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: NO mesh available (real + placeholder both missing) -- creature will run meshless/invisible."),
			*GetName());
		return;
	}

	if (!PlaceholderMesh->GetSkeleton())
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: placeholder mesh loaded but has no Skeleton -- refusing SetSkinnedAsset (sf-skeletal-mesh-import-crash guard). Creature will run meshless."),
			*GetName());
		return;
	}

	// ---- w18-8: THE ACTUAL ROOT CAUSE (proven by the w18-7 forensics line:
	// assetR=237(my earlier repair) compR=1cm LODs=1 LOD0verts=1189
	// sections=1 materials=1): the asset is a real, renderable mesh -- but
	// ~1.65 CENTIMETERS tall. The FBX was exported in Blender meter units
	// and tools/import_placeholder_ue.py hardcodes import_uniform_scale=1.0,
	// so UE (cm) read 1.65 units as 1.65cm (the documented Blender->UE
	// gotcha that hit the laser rifle at 0.53cm). Every earlier hypothesis
	// (burial / zero bounds / missing proxy) was a secondary symptom of a
	// speck-sized mesh. Fix: measure the mesh's REAL height and scale the
	// COMPONENT so the creature stands at its designed height; a live CVar
	// overrides the factor so size tunes in-game with a respawn, no rebuild.
	// The proper asset-side fix (re-import at scale 100) is folded into the
	// W2.2 import standard (doc 05).

	// Repair only a TRULY degenerate asset (radius < 0.01cm); a tiny-but-real
	// bounds is the honest measurement the auto-scale below needs. (w18-5's
	// <1cm threshold overwrote the real ~1cm evidence -- reverted.)
	if (PlaceholderMesh->GetImportedBounds().SphereRadius < 0.01f)
	{
		PlaceholderMesh->SetImportedBounds(FBoxSphereBounds(FBox(FVector(-2.0f, -2.0f, 0.0f), FVector(2.0f, 2.0f, 2.0f))));
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: placeholder mesh bounds were EXACTLY zero -- stub bounds injected so auto-scale can proceed."), *GetName());
	}

	const FBoxSphereBounds MeasuredBounds = PlaceholderMesh->GetImportedBounds();
	const float MeshHeightCm = FMath::Max(MeasuredBounds.BoxExtent.Z * 2.0f, 0.01f);

	// pfa.PlaceholderMeshScale: 0 = AUTO (SicklejawHeightCm / measured mesh
	// height, clamped 1..500); >0 = explicit factor. Read at assign time --
	// change it, then PFA.KillAll + PFA.SpawnNest to see the new size live.
	float ScaleFactor = CVarPFAPlaceholderMeshScale.GetValueOnGameThread();
	if (ScaleFactor <= 0.0f)
	{
		ScaleFactor = FMath::Clamp(SicklejawTargetHeightCm / MeshHeightCm, 1.0f, 500.0f);
	}

	MeshComponent->SetSkinnedAssetAndUpdate(PlaceholderMesh, true);
	MeshComponent->SetRelativeScale3D(FVector(ScaleFactor));
	// Feet at the capsule bottom (C++-only pawn: nobody set the usual -halfHeight
	// mesh offset a BP character template carries).
	float CapsuleHalfForMesh = 90.0f;
	if (const UCapsuleComponent* MeshCapsule = GetCapsuleComponent())
	{
		CapsuleHalfForMesh = MeshCapsule->GetScaledCapsuleHalfHeight();
	}
	const FVector OldRelLoc = MeshComponent->GetRelativeLocation();
	// w18-9 (cold-review hardening): plant the FEET at the capsule bottom for
	// ANY mesh-origin convention (feet-origin OR pelvis-centered). FeetLocalZ
	// is where the lowest point of the mesh sits in mesh-local space; scaled,
	// it corrects the offset so a centered-origin rig can't spawn waist-deep
	// -- the one failure mode the scale CVar could not tune away.
	const float FeetLocalZ = MeasuredBounds.Origin.Z - MeasuredBounds.BoxExtent.Z;
	MeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f,
		-CapsuleHalfForMesh - ScaleFactor * FeetLocalZ + CVarPFAMeshZOffset.GetValueOnGameThread()));

	// ---- w32-2 MESH FACING. User, in game: "The creatures do turn, they just
	// don't face me. Kinda like their asset front is not at where they face."
	// That sentence moved this from "rotation is broken" (which I had been
	// chasing through the controller for two builds) to "rotation works and the
	// mesh is mounted at the wrong yaw" -- a different bug with a different fix.
	//
	// Measured rather than guessed: in the source rig the Jaw sits at y=+0.37m
	// and Tail05 at y=-0.67m, with EVERY bone at x=0. The creature is authored
	// facing +Y. UE's imported bounds agree (176cm along Y, 67cm across X). An
	// actor's forward is +X, so the mesh needs a -90 yaw to line its nose up
	// with the direction the actor is actually facing -- and we never set one,
	// so the body pointed 90 degrees off the way it was travelling and looking.
	//
	// CVar-tunable because facing is judged by eye, not by arithmetic: if -90 is
	// the wrong sign for this asset, `pfa.MeshYaw 90` settles it in-game in
	// seconds with no rebuild, then the value gets baked here (the same
	// live-tune-then-bake pattern as the laser rifle's aim correction).
	const float MeshYawDeg = CVarPFAMeshYaw.GetValueOnGameThread();
	const FRotator OldMeshRot = MeshComponent->GetRelativeRotation();
	MeshComponent->SetRelativeRotation(FRotator(0.0f, MeshYawDeg, 0.0f));
	MeshComponent->UpdateBounds();

	// Log both forward vectors so the log alone says whether they agree: the dot
	// is +1 when the mesh's nose points where the actor is facing.
	const FVector ActorForward = GetActorForwardVector();
	const FVector MeshForward = MeshComponent->GetComponentTransform().TransformVectorNoScale(FVector::YAxisVector);
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: mesh yaw %s -> %.0f deg (cvar pfa.MeshYaw); actorFwd=%s meshNoseFwd=%s dot=%.2f (want ~+1.00)"),
		*GetName(), *OldMeshRot.ToCompactString(), MeshYawDeg,
		*ActorForward.ToCompactString(), *MeshForward.ToCompactString(),
		FVector::DotProduct(ActorForward.GetSafeNormal2D(), MeshForward.GetSafeNormal2D()));

	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: placeholder mesh assigned+scaled (%s): measuredHeight=%.2fcm scaleFactor=%.1f (%s) -> compR=%.0fcm worldScaleZ=%.1f relLoc=(%s -> %s) capsuleHalf=%.0f"),
		*GetName(), *PlaceholderMesh->GetName(),
		MeshHeightCm, ScaleFactor,
		CVarPFAPlaceholderMeshScale.GetValueOnGameThread() <= 0.0f ? TEXT("auto") : TEXT("cvar"),
		MeshComponent->Bounds.SphereRadius,
		MeshComponent->GetComponentTransform().GetScale3D().Z,
		*OldRelLoc.ToCompactString(), *MeshComponent->GetRelativeLocation().ToCompactString(),
		CapsuleHalfForMesh);

	// w24-1: arm the locomotion driver ONLY on the real mesh -- the anims
	// target SK_Sicklejaw_Skeleton; playing them on the placeholder's
	// different skeleton would be undefined.
	if (bRealMeshLive)
	{
		StartLocomotionAnim();
	}
}

namespace
{
	// w24-2: load each candidate package and return the FIRST UAnimSequence
	// object inside, regardless of what the FBX importer named it. Logs every
	// probe so the log alone shows which package+object won (or why none did).
	UAnimSequence* FindSequenceInPackages(const TCHAR* const* PackagePaths, int32 NumPackages, const TCHAR* Label, const FString& OwnerName)
	{
		for (int32 i = 0; i < NumPackages; ++i)
		{
			UPackage* Package = LoadPackage(nullptr, PackagePaths[i], LOAD_None);
			if (!Package)
			{
				if (PFADiag::IsEnabled())
				{
					UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s: anim probe [%s] package %s: NOT FOUND"),
						*OwnerName, Label, PackagePaths[i]);
				}
				continue;
			}
			UAnimSequence* Found = nullptr;
			ForEachObjectWithPackage(Package, [&Found](UObject* Object)
			{
				if (UAnimSequence* Sequence = Cast<UAnimSequence>(Object))
				{
					Found = Sequence;
					return false;
				}
				return true;
			});
			if (Found)
			{
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] %s: anim [%s] resolved: package=%s object=%s length=%.2fs"),
					*OwnerName, Label, PackagePaths[i], *Found->GetName(), Found->GetPlayLength());
				return Found;
			}
			if (PFADiag::IsEnabled())
			{
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] %s: anim probe [%s] package %s loaded but contains NO UAnimSequence"),
					*OwnerName, Label, PackagePaths[i]);
			}
		}
		return nullptr;
	}
}

void APFASicklejaw::StartLocomotionAnim()
{
	if (CVarPFALocomotionAnims.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: locomotion playback DISABLED by pfa.LocomotionAnims=0 -- ref-pose statue by choice."),
			*GetName());
		return;
	}

	const FString OwnerName = GetName();
	LocomotionIdleAnim = FindSequenceInPackages(SicklejawIdleAnimPackages, UE_ARRAY_COUNT(SicklejawIdleAnimPackages), TEXT("idle"), OwnerName);
	LocomotionWalkAnim = FindSequenceInPackages(SicklejawWalkAnimPackages, UE_ARRAY_COUNT(SicklejawWalkAnimPackages), TEXT("walk"), OwnerName);
	LocomotionRunAnim = FindSequenceInPackages(SicklejawRunAnimPackages, UE_ARRAY_COUNT(SicklejawRunAnimPackages), TEXT("run"), OwnerName);
	// w28-1 state clips -- optional by design: each falls back to plain Idle
	// or Walk if absent, so an incomplete import degrades instead of going
	// dark (the w24-3 partial-arm lesson, applied to the state layer).
	LocomotionIdleAlertAnim = FindSequenceInPackages(SicklejawIdleAlertAnimPackages, UE_ARRAY_COUNT(SicklejawIdleAlertAnimPackages), TEXT("idleAlert"), OwnerName);
	LocomotionIdleAggressiveAnim = FindSequenceInPackages(SicklejawIdleAggressiveAnimPackages, UE_ARRAY_COUNT(SicklejawIdleAggressiveAnimPackages), TEXT("idleAggressive"), OwnerName);
	LocomotionProwlAnim = FindSequenceInPackages(SicklejawProwlAnimPackages, UE_ARRAY_COUNT(SicklejawProwlAnimPackages), TEXT("prowl"), OwnerName);
	// w29-1: the one-shot set (comfort breaks, threat display, limp). Resolved
	// here so ALL clip loading happens in one place at arm time -- a creature
	// must never stall mid-tick loading a package the moment it decides to preen.
	ResolveOneShotClips();
	// w24-3 (overnight tester finding): the all-or-nothing gate kept the
	// driver dark when only run was missing -- idle+walk resolved but NOT
	// EVEN IDLE played. PARTIAL-ARM instead: any resolved sequence arms the
	// driver, and TickLocomotionAnim falls back down the gait chain
	// (run->walk->idle) for missing slots -- a missing take degrades to a
	// slower gait, never to a statue. Only the nothing-resolved case stays
	// dark (honest warning).
	if (!LocomotionIdleAnim && !LocomotionWalkAnim && !LocomotionRunAnim)
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: NO locomotion anims resolved at all -- ref-pose slide remains; run tools/import_sicklejaw_ue.py."),
			*GetName());
		return;
	}
	if (!LocomotionIdleAnim || !LocomotionWalkAnim || !LocomotionRunAnim)
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: locomotion anims partial (idle=%s walk=%s run=%s) -- arming with gait fallback (missing slots borrow the next-slower clip)."),
			*GetName(),
			LocomotionIdleAnim ? TEXT("ok") : TEXT("MISSING"),
			LocomotionWalkAnim ? TEXT("ok") : TEXT("MISSING"),
			LocomotionRunAnim ? TEXT("ok") : TEXT("MISSING"));
	}

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	}
	// Raw member fn on the templated timer API (heartbeat/rescue precedent:
	// no UFUNCTION -- stays off the reflection/export surface).
	GetWorldTimerManager().SetTimer(LocomotionTimerHandle, this, &APFASicklejaw::TickLocomotionAnim, LocomotionTickSeconds, true);
	TickLocomotionAnim();
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: locomotion driver armed (%.1fs cadence; gait bands prowl>=%.0f walk>=%.0f run>=%.0f cm/s; state clips alert=%s aggressive=%s prowl=%s)."),
		*GetName(), LocomotionTickSeconds,
		LocomotionProwlThresholdCms, LocomotionWalkBandStartCms, LocomotionRunThresholdCms,
		LocomotionIdleAlertAnim ? TEXT("ok") : TEXT("absent"),
		LocomotionIdleAggressiveAnim ? TEXT("ok") : TEXT("absent"),
		LocomotionProwlAnim ? TEXT("ok") : TEXT("absent"));
}

void APFASicklejaw::TickLocomotionAnim()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	// Dead creatures keep their last pose (or the native death handling's
	// ragdoll) -- stop driving anims and let the timer die with the actor.
	if (UFGHealthComponent* Health = GetHealthComponent())
	{
		if (Health->IsDead())
		{
			GetWorldTimerManager().ClearTimer(LocomotionTimerHandle);
			return;
		}
	}

	// ---- w26-1 FOOT-PLANT KEEPER (runs every tick, independent of whether
	// the gait changed): compare the mesh's CURRENT world bounds bottom to
	// the capsule bottom and close the gap. Both move together, so this is a
	// pose correction, never a terrain one -- falling, slopes and stairs are
	// unaffected. Gain 1 with a tolerance band converges in a tick or two.
	// w31-3 (cold review D2): stand down for a couple of ticks right after an
	// animation-driver switch. Swapping drivers tears down and rebuilds the
	// anim tree, and the tree's first evaluation happens before the new graph
	// has been told which clip to play -- so the mesh momentarily reports
	// REFERENCE-POSE bounds. Left alone, the keeper would read that as a
	// planting error and apply a real vertical shift: a visible pop, i.e. the
	// w26-1 sunk-creature symptom, caused by the very code that fixed it.
	// Counting down here rather than in the switch keeps the keeper's own state
	// machine in one place.
	if (FootPlantSkipTicks > 0)
	{
		--FootPlantSkipTicks;
		if (PFADiag::IsEnabled())
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: foot-plant keeper standing down after a driver switch (%d tick(s) left)"),
				*GetName(), FootPlantSkipTicks);
		}
	}
	else if (CVarPFAFootPlantAuto.GetValueOnGameThread() != 0)
	{
		float CapsuleHalfCm = 88.0f;
		if (const UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			CapsuleHalfCm = Capsule->GetScaledCapsuleHalfHeight();
		}
		const FBoxSphereBounds& WorldBounds = MeshComponent->Bounds;
		const float MeshBottomZ = WorldBounds.Origin.Z - WorldBounds.BoxExtent.Z;
		const float CapsuleBottomZ = GetActorLocation().Z - CapsuleHalfCm;
		// >0 => the mesh hangs BELOW the capsule (sunk) => lift by that much.
		// pfa.MeshZOffset rides along as an ADDITIVE trim (w26-2 review: the
		// keeper otherwise drives that manual knob back to zero within one
		// tick, silently disabling it).
		const float DeltaZ = (CapsuleBottomZ + CVarPFAMeshZOffset.GetValueOnGameThread()) - MeshBottomZ;

		if (FMath::Abs(DeltaZ) > FootPlantToleranceCm)
		{
			const float NewCorrection = FMath::Clamp(FootPlantCorrectionCm + DeltaZ,
				-FootPlantMaxCorrectionCm, FootPlantMaxCorrectionCm);
			const float AppliedCm = NewCorrection - FootPlantCorrectionCm;
			if (!FMath::IsNearlyZero(AppliedCm))
			{
				FootPlantCorrectionCm = NewCorrection;
				FVector RelLoc = MeshComponent->GetRelativeLocation();
				RelLoc.Z += AppliedCm;
				MeshComponent->SetRelativeLocation(RelLoc);
				MeshComponent->UpdateBounds();

				// First few corrections per creature are always-on (they
				// explain a visible position change); the rest are diag-only
				// so a persistently-correcting pose can't flood the log.
				if (FootPlantLogCount < 3 || PFADiag::IsEnabled())
				{
					++FootPlantLogCount;
					UE_LOG(LogPrimalFauna, Display,
						TEXT("[PFA] %s: foot-plant correction %+.0fcm (mesh bottom was %+.0fcm vs capsule bottom; total correction %+.0fcm)"),
						*GetName(), AppliedCm, -DeltaZ, FootPlantCorrectionCm);
				}
			}
		}
	}

	const float SpeedCms = GetVelocity().Size2D();

	// ---- w28-1 STATE-DRIVEN SELECTION. Movement decides the gait; when the
	// creature is standing still, its INTENT decides the idle. A roamer that
	// has noticed you and one that has decided to kill you must not share a
	// pose -- that difference is most of what makes a creature look like it
	// is thinking rather than looping.
	ECreatureState CreatureState = ECreatureState::CS_Default;
	if (const AFGCreatureController* CreatureAI = GetCreatureController())
	{
		CreatureState = CreatureAI->GetCurrentCreatureState();
	}

	int32 DesiredState;
	if (SpeedCms >= LocomotionRunThresholdCms)
	{
		DesiredState = 2;   // Run
	}
	else if (SpeedCms >= LocomotionWalkBandStartCms)
	{
		DesiredState = 1;   // Walk
	}
	else if (SpeedCms >= LocomotionProwlThresholdCms)
	{
		DesiredState = 5;   // Prowl -- authored at 110cm/s, owns the slow band
	}
	else if (CreatureState == ECreatureState::CS_Fight)
	{
		DesiredState = 4;   // IdleAggressive: coiled, jaw parted, tail lashing
	}
	else if (CreatureState == ECreatureState::CS_Alert || CreatureState == ECreatureState::CS_Investigate)
	{
		DesiredState = 3;   // IdleAlert: taller, head up and tracking
	}
	else
	{
		DesiredState = 0;   // Idle
	}

	// ---- w29-1 ONE-SHOT LAYER gets first refusal on the pose. A break or a
	// threat display owns the mesh while it plays; it releases the moment the
	// creature's situation changes (see UpdateOneShotLayer). Placed AFTER
	// DesiredState is computed because "the situation changed" is defined as
	// exactly that value moving.
	if (UpdateOneShotLayer(DesiredState))
	{
		return;
	}
	if (MaybeStartOneShot(DesiredState))
	{
		return;
	}

	// Being wounded is part of the state IDENTITY, not just a post-pick swap:
	// a creature already walking when its health crosses the limp threshold
	// would otherwise keep the healthy gait until it happened to change state
	// (i.e. the limp would appear only after it stopped and set off again).
	UAnimSequence* WoundedGait = ResolveWoundedGait(DesiredState);
	const bool bWantWounded = (WoundedGait != nullptr);

	if (DesiredState == LocomotionState && bWantWounded == bLocomotionWoundedActive)
	{
		return;
	}
	LocomotionState = DesiredState;
	bLocomotionWoundedActive = bWantWounded;

	// w24-3 gait fallback chain: a missing clip borrows the next-slower one
	// (run->walk->idle, walk->idle, idle->walk as last resort) so partial
	// asset coverage still animates. Guaranteed non-null: the arm gate
	// requires at least one resolved sequence.
	// First non-null wins: every state degrades along a sensible chain so a
	// partial import animates rather than going dark (the w24-3 lesson,
	// extended to the state clips).
	const auto FirstAvailable = [](std::initializer_list<UAnimSequence*> Candidates) -> UAnimSequence*
	{
		for (UAnimSequence* Candidate : Candidates)
		{
			if (Candidate)
			{
				return Candidate;
			}
		}
		return nullptr;
	};

	UAnimSequence* Chosen = nullptr;
	const TCHAR* StateName = TEXT("idle");
	switch (DesiredState)
	{
	case 2:
		Chosen = FirstAvailable({LocomotionRunAnim, LocomotionWalkAnim, LocomotionIdleAnim});
		StateName = TEXT("run");
		break;
	case 1:
		Chosen = FirstAvailable({LocomotionWalkAnim, LocomotionProwlAnim, LocomotionIdleAnim});
		StateName = TEXT("walk");
		break;
	case 5:
		Chosen = FirstAvailable({LocomotionProwlAnim, LocomotionWalkAnim, LocomotionIdleAnim});
		StateName = TEXT("prowl");
		break;
	case 4:
		Chosen = FirstAvailable({LocomotionIdleAggressiveAnim, LocomotionIdleAlertAnim, LocomotionIdleAnim});
		StateName = TEXT("idle-aggressive");
		break;
	case 3:
		Chosen = FirstAvailable({LocomotionIdleAlertAnim, LocomotionIdleAnim});
		StateName = TEXT("idle-alert");
		break;
	default:
		Chosen = FirstAvailable({LocomotionIdleAnim, LocomotionWalkAnim});
		break;
	}
	// w29-1: a badly hurt creature limps instead of walking or prowling --
	// a MODIFIER on how you move, not a different intent, so it overrides the
	// gait pick rather than being a state of its own. (Resolved above, where it
	// also participates in the change detection.)
	if (WoundedGait)
	{
		Chosen = WoundedGait;
		StateName = TEXT("limp");
	}

	if (!Chosen)
	{
		Chosen = FirstAvailable({LocomotionRunAnim, LocomotionProwlAnim, LocomotionIdleAlertAnim, LocomotionIdleAggressiveAnim});
	}
	if (!Chosen)
	{
		return;
	}
	// w31-1: was MeshComponent->PlayAnimation(Chosen, true). Everything above
	// this line -- the foot-plant keeper, the speed bands, the creature-state
	// mapping, the one-shot layer, the wounded-gait override, the fallback
	// chains -- is UNCHANGED and runs identically on both animation drivers.
	// The AnimInstance packet redirects delivery only (see ApplyChosenPose).
	ApplyChosenPose(Chosen, true);

	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: locomotion -> %s (speed=%.0f cm/s, creatureState=%s, clip=%s)"),
			*GetName(), StateName, SpeedCms,
			*CreatureStateEnumToString(CreatureState), *Chosen->GetName());
	}
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

		// Stops the native kill-orphan timer from destroying a creature spawned
		// without a real spawner. NOT via AFGCreature::ClearKillOrphanTimer()
		// -- that symbol is a shipping-export trap (module-load error 127; see
		// PFACreatureBase.h) -- but via our Friend-granted direct handle clear.
		if (APFACreatureBase* AsPFACreature = Cast<APFACreatureBase>(SpawnedCreature))
		{
			AsPFACreature->ClearNativeOrphanTimer();
		}

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

		// W1.6 softening (docs/08 packet, Phase-1 gate Section B item 10): the
		// gate's OWN evidence showed this "FAIL" firing twice, always
		// none/unpossessed, while nest-path creatures (PFA.SpawnNest /
		// ReconcileNests) possessed correctly every time -- a detached
		// PFA.SpawnTest spawn can still be mid-possess on the SAME tick this
		// check runs. Re-checking after a delay is overkill for a dev
		// command, so the known-benign case (no controller at all) is now a
		// WARN and the roundtrip always completes (destroy) instead of
		// aborting early. A REAL wrong-class mismatch (controller present but
		// not APFACreatureController -- never actually observed, but would
		// mean AIControllerClass wiring is broken) stays at Error. The
		// distance-write assertion above is untouched.
		AController* Controller = SpawnedCreature->GetController();
		APFACreatureController* PFAController = Cast<APFACreatureController>(Controller);
		if (!PFAController)
		{
			if (!Controller)
			{
				UE_LOG(LogPrimalFauna, Warning,
					TEXT("[PFA] PFA.SpawnTest: unpossessed at check time (detached-spawn timing; nest-path creatures possess correctly -- see gate report)."));
			}
			else
			{
				UE_LOG(LogPrimalFauna, Error,
					TEXT("[PFA] PFA.SpawnTest: possessed controller is %s, expected APFACreatureController."),
					*Controller->GetClass()->GetName());
			}
		}
		else
		{
			const bool bHostileToPlayer = PFAController->CanBeHostileTowards(PlayerPawn);
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] PFA.SpawnTest: spawned %s, controller=%s, CanBeHostileTowards(player)=%s"),
				*SpawnedCreature->GetName(), *PFAController->GetClass()->GetName(),
				bHostileToPlayer ? TEXT("true") : TEXT("false"));
		}

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
