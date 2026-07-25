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

	// --- w31-1 THE ONE PLACE A CHOSEN SEQUENCE BECOMES A PLAYED POSE ---------
	// Every clip this creature plays -- gaits, state idles, comfort breaks,
	// threat displays, limps, attack strikes, dev overrides -- goes through
	// here. That is the whole of the AnimInstance change on the gameplay side:
	// the SELECTION logic above is untouched, only the delivery is redirected.
	//
	// Funnelling matters beyond tidiness. USkeletalMeshComponent::PlayAnimation
	// FORCES the component back to single-node mode as its first act, so a
	// single surviving direct PlayAnimation call would silently tear the
	// AnimInstance down mid-session and the driver would thrash between the two
	// paths every tick. One funnel makes that unrepresentable.
	void ApplyChosenPose(UAnimSequence* Sequence, bool bLoop);

	// w38-1: the ground speed a gait clip was AUTHORED for, or 0 for clips with
	// no ground speed (idles, breaks, one-shots). The denominator that keeps the
	// feet on the ground when the creature moves at a speed the clip was not
	// built for -- e.g. charging at the 780cm/s sprint on a clip authored for
	// 600, which is what the user saw as sliding.
	float AuthoredSpeedForSequence(const UAnimSequence* Sequence) const;

	// Installs or removes the native AnimInstance so the live driver matches
	// pfa.UseAnimInstance. Returns TRUE if the AnimInstance path is live.
	// Called from ApplyChosenPose, i.e. only when a pose is actually changing,
	// so a session can be A/B'd by typing the CVar with no respawn and no
	// rebuild -- the next pose change moves the creature over.
	bool EnsureAnimationDriver();

public:
	// w31-3 (cold review D1): the animation CVars are read inside
	// ApplyChosenPose, which only runs when a pose actually CHANGES -- and a
	// creature standing still in Fight, or chasing at a constant speed, or
	// holding a dev clip, may not change pose for minutes. Typing
	// pfa.UseAnimInstance 1 then looked like it did nothing, which is worse
	// than a lever that is documented as needing a respawn. Public because the
	// CVar sink callbacks reach in from file scope, exactly like the PFA.*
	// console commands below.
	void RefreshAnimationDriver();

private:
	// w31-2: pushes the additive tail-mood layer's clip + weight into the anim
	// graph. Every decision (is the layer on, is the clip really additive, what
	// weight) is taken HERE on the game thread, so the animation worker thread
	// only ever receives a sequence pointer and a float.
	void PushTailMood(class UPFAAnimInstance* AnimInstance);

	// Loads the tail-mood clip named by pfa.TailMoodClip, caching by name so a
	// steady CVar never touches the package system twice. Returns true if
	// TailMoodAnim is usable. Validates the console string at the boundary.
	bool ResolveTailMoodClip(const FString& ClipSuffix);

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

	// w35-1: this creature's own phase in a looping clip, [0,1), rolled ONCE at
	// spawn. Without it every creature that enters a state on the same frame
	// plays the identical frame forever, and a spawned group reads as one
	// machine rather than several animals. Stable per creature so it reads as an
	// individual rather than as noise.
	float LocomotionPhaseOffset = 0.0f;

	// w31-1 animation-driver state. Latched on failure. If the AnimInstance was requested but the component
	// did not end up with one, this creature stops asking and runs the proven
	// single-node path for the rest of its life. Without the latch, every pose
	// change would retry the install, fall back through PlayAnimation (which
	// resets the mode to single-node), and retry again -- an invisible per-tick
	// re-initialisation of the whole animation tree.
	bool bAnimInstanceDriverFailed = false;

	// w31-3 (cold review D2): the pose that was last handed to a driver, so a
	// driver switch can hand the SAME pose straight to the new one. Recorded by
	// ApplyChosenPose, which is the funnel every clip goes through, so this
	// cannot drift from what is actually playing.
	//
	// Without it, SetAnimInstanceClass -> InitAnim(true) evaluates the fresh
	// graph while its sequence is still null, which resolves to the REFERENCE
	// POSE -- a T-pose frame, and worse, a bounds recompute that the foot-plant
	// keeper would then "correct" against a pose the creature is not in. That
	// is the visual signature of the w26-1 sunk-creature regression, and it is
	// not acceptable to reintroduce it even transiently.
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActivePoseAnim;
	bool bActivePoseLoops = true;

	// w31-3 (cold review D2): ticks for which the foot-plant keeper stands down
	// after a driver switch. The switch tears down and rebuilds the animation
	// tree, so for a moment the mesh's bounds describe a transient pose; the
	// keeper must not treat that as a planting error and shove the mesh. It
	// resumes on its own -- this is a stand-down, never a disable.
	int32 FootPlantSkipTicks = 0;

	// w31-2 additive tail-mood layer. GC-rooted like every other runtime-loaded
	// clip -- the animation worker thread holds a raw pointer to whatever is in
	// the additive branch.
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> TailMoodAnim;
	// Which pfa.TailMoodClip suffix TailMoodAnim was resolved from. Empty means
	// "never resolved". Cached so the steady state costs a string compare, not
	// a package lookup, on a path that runs 5x/second.
	FString TailMoodClipResolved;
	int32 TailMoodSkipLogCount = 0;
	int32 TailMoodForceLogCount = 0;

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
