#pragma once

#include "CoreMinimal.h"
#include "PFACreatureBase.h"
#include "PFASicklejaw.generated.h"

class UAnimSequence;

// ---------------------------------------------------------------------------
// APFASicklejaw -- the vertical-slice species (docs/01-creature-roster.md §1,
// docs/07-technical-architecture.md §3). Raptor-class pack hunter, default
// aggression Hostile.
//
// ALL numeric anchors in PFASicklejaw.cpp are PLACEHOLDERS tagged
// TODO(W1.2-census): doc 01 §1 frames every stat here as an "anchor RATIO"
// relative to a vanilla creature (Alpha Hog health, Stinger sight range,
// ~1.3x player sprint speed) -- the W1.2 census packet (PFA.DumpCreature
// against live vanilla creatures) is what fixes the real numbers. Nothing
// here should be read as balance-final.
//
// Wired by W1.4 (docs/08 W1.4):
//   - mCreatureActionsSetup -- gains ONE montage-less bite (UPFACreatureActionMelee,
//     Actions/PFACreatureActionMelee.h), enabled for the Fight state. Appended
//     in the ctor below.
//   - mBehaviorTree -- NOT set on the CDO at ctor time; PFABehaviorBorrower
//     discovers a vanilla BehaviorTree at world load (server-side) and assigns
//     it to this CDO via APFACreatureBase::SetBorrowedBehaviorTree. If discovery
//     finds nothing, mBehaviorTree stays null and the creature idles (degraded,
//     not broken) -- BorrowedBTAssigned self-test FAILs with the found-count.
//
// NOT set in this packet (deliberately):
//   - AnimInstance class (no placeholder AnimBP exists yet; the 2 imported
//     anim FBXs become raw UAnimSequence assets only, per
//     tools/import_placeholder_ue.py)
//   - mCreatureFamily (doc 07 §3 marks this an open [assumption] to resolve
//     "during W1", not specifically W1.3/W1.4; deferred until doc 02 §7 item 1's
//     in-game test tells us whether the family system actually gates our
//     creature-vs-creature targeting)
// ---------------------------------------------------------------------------
UCLASS()
class PRIMALFAUNA_API APFASicklejaw : public APFACreatureBase
{
	GENERATED_BODY()

public:
	APFASicklejaw(const FObjectInitializer& ObjectInitializer);

protected:
	// sf-skeletal-mesh-import-crash (memory): the placeholder mesh load and
	// SetSkinnedAsset happen HERE, never in the ctor -- a broken/missing
	// skeleton asserts at component registration, which runs before
	// BeginPlay, so a ctor-time assign would crash on a bad import instead
	// of degrading. Tolerates a missing mesh entirely: a meshless creature
	// must still function (movement/AI/health are all mesh-independent).
	virtual void BeginPlay() override;

private:
	// w24-1 locomotion driver ("they just slide along the ground"): a
	// speed-driven idle/walk/run player on the component's single-node anim
	// path (SetAnimationMode + PlayAnimation) -- deliberately NO AnimBP
	// (event graphs are not headless-authorable; direct playback is). Only
	// armed when the REAL mesh loaded (the anims target SK_Sicklejaw_Skeleton;
	// the placeholder's skeleton is a different asset). Montage-driven
	// slash/hurt stay a later packet -- the bite keeps the proven
	// montage-less direct-hit path.
	void StartLocomotionAnim();
	void TickLocomotionAnim();

	// UPROPERTY-rooted so the runtime-loaded sequences can't be GC'd from
	// under the playing component (cookbook GC-rooting convention).
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionIdleAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionWalkAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionRunAnim;

	FTimerHandle LocomotionTimerHandle;
	// 0=idle 1=walk 2=run, -1 = nothing playing yet (forces the first apply).
	int32 LocomotionState = -1;
};
