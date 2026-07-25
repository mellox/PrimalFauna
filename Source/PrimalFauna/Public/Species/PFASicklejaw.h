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

	// --- w29-1 ONE-SHOT CLIP LAYER (PFASicklejawAnim.cpp) ---------------------
	// Rides on top of the looping state machine using the SAME single-node
	// player: PlayOneShotClip swaps in a non-looping clip and marks the pose as
	// owned until it finishes; the looping selection yields while that is true
	// and re-applies afterwards. This is the limit of what one node can do --
	// it plays exactly ONE sequence, so layered additives (breathing, tail
	// mood) are NOT reachable this way and still need a real AnimInstance.
	bool PlayOneShotClip(UAnimSequence* Sequence, const TCHAR* Label, bool bHoldFinalFrame, int32 BaseState);
	// True => a one-shot owns the pose this tick; the caller must not touch it.
	bool UpdateOneShotLayer(int32 DesiredState);
	// Threat display when alert, comfort/vigilance breaks when calm.
	bool MaybeStartOneShot(int32 DesiredState);
	// Limp instead of the walk/prowl gait once badly hurt (never instead of Run).
	UAnimSequence* ResolveWoundedGait(int32 DesiredState) const;
	void ResolveOneShotClips();
	void ClearOneShot(const TCHAR* Reason);
	void ScheduleNextIdleBreak(float NowSeconds);

public:
	// PFA.PlayAnim <clip> [hold] [radius]: play ANY imported clip by name on
	// this creature. The verification lever for the whole animation set --
	// 36 clips exist and gameplay only reaches a handful, so without this the
	// rest can only be judged in Blender renders, never in the actual game
	// at the actual scale under the actual lighting.
	bool DevPlayClipByName(const FString& ClipName, bool bHoldFinalFrame);
	void DevClearClipOverride();

	// w30-1: the attack VISUAL for the montage-less melee path
	// (Actions/PFACreatureActionMelee.cpp). Vanilla plays attacks as montages
	// through UFGCreatureActionAnimatedBase, which needs a UAnimInstance we
	// deliberately don't have -- so the strike rides the same one-shot layer as
	// everything else. Unlike a comfort break this is NOT released by a state
	// change: an attack is a committed act and must be seen through, so it ends
	// on its own clock (or when the action cleans up).
	bool PlayAttackOneShot(const FString& ClipName);
	void StopAttackOneShot();
	// Lets the impact timer confirm the strike is still on screen before it
	// deals damage, so the two state machines can't drift apart silently. The
	// safe failure is "no damage, loudly logged"; the unsafe one is "damage
	// with no visible cause", which is the bug this whole packet exists to fix.
	bool IsPlayingAttackOneShot() const;

private:

	// UPROPERTY-rooted so the runtime-loaded sequences can't be GC'd from
	// under the playing component (cookbook GC-rooting convention).
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionIdleAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionWalkAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionRunAnim;

	// w28-1: state clips. Optional -- each degrades to Idle/Walk when absent.
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionIdleAlertAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionIdleAggressiveAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LocomotionProwlAnim;

	// w29-1 one-shot clips. Parallel to the weight table in PFASicklejawAnim.cpp;
	// entries may be null (an unresolved clip is simply never picked).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> IdleBreakClips;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ThreatenAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LimpAnim;
	// GC-rooted for the same reason as the loops: the single-node player holds a
	// raw pointer to whatever is playing (cookbook GC-rooting convention).
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveOneShotAnim;

	// 0 = nothing playing. World-seconds deadline, not a countdown, so a timer
	// hitch can't strand a one-shot forever.
	float OneShotEndWorldSeconds = 0.0f;
	bool bOneShotHoldsFinalFrame = false;
	// The looping state that was true when the one-shot started; if the creature
	// leaves it (starts moving, spots the player, commits to a fight) the pose is
	// released immediately -- a creature must never finish preening at something
	// that is trying to kill it.
	int32 OneShotBaseState = -1;
	float NextIdleBreakWorldSeconds = 0.0f;
	// One ritualised threat display per alert episode, re-armed on leaving alert.
	bool bThreatenedThisEpisode = false;

	FTimerHandle LocomotionTimerHandle;
	// 0=idle 1=walk 2=run 3=idle-alert 4=idle-aggressive 5=prowl,
	// -1 = nothing playing yet (forces the first apply).
	int32 LocomotionState = -1;
	// Part of the state identity, not a separate state: see TickLocomotionAnim.
	bool bLocomotionWoundedActive = false;

	// w26-1 (user report: creatures sunk into the terrain once anims played).
	// The w18-8/w23-2 feet-plant offset was computed ONCE from the mesh's
	// REFERENCE-pose bounds; an animated pose sits at a different height, so
	// a static offset silently goes wrong the moment a clip plays -- and
	// nothing re-checked it (the regression this packet also adds tests for).
	// This is the running correction that keeps the mesh's LOWEST point at
	// the capsule bottom every locomotion tick, for whatever pose is live.
	// Capsule-relative, so it never fights terrain, falling, or slopes.
	float FootPlantCorrectionCm = 0.0f;
	int32 FootPlantLogCount = 0;
};
