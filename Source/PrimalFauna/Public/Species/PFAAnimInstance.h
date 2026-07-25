#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_ApplyAdditive.h"
#include "PFAAnimInstance.generated.h"

class UAnimSequence;

// ---------------------------------------------------------------------------
// UPFAAnimInstance -- w31-1: the native anim graph, and the FIRST FALSIFIABLE
// INCREMENT of it.
//
// WHY THIS EXISTS AT ALL
// Everything animated so far rides USkeletalMeshComponent's SINGLE-NODE path
// (SetAnimationMode(AnimationSingleNode) + PlayAnimation). That path plays
// exactly ONE sequence, which is why two things were flatly impossible rather
// than merely unbuilt:
//   - the 12 authored additive layers (breathing, six tail moods, head-look,
//     saccades) -- "idle PLUS breathing PLUS an angry tail" has no
//     representation in one node (PFASicklejawAnim.cpp says so at the top);
//   - attack montages -- vanilla plays creature attacks through
//     UFGCreatureActionAnimatedBase -> UAnimMontage -> UAnimInstance, and w30-1
//     had to route the bite through the one-shot layer instead because we had
//     no UAnimInstance to play a montage on.
// Both are blocked by the same root cause, and this class is that root cause
// removed.
//
// WHY NATIVE AND NOT AN ANIMBLUEPRINT
// The headless-AnimBlueprint route is DEAD, not merely awkward: Unreal Python
// exposes 106 AnimGraphNode_* classes but does NOT expose EdGraphPin at all, so
// nodes can be created and never wired. A hand-built native graph is the only
// authorable option in a Blueprint-free, headless project.
//
// WHAT THIS INCREMENT DELIBERATELY IS NOT
// It is not the animation system. It is ONE FAnimNode_SequencePlayer reading a
// sequence pointer the game thread hands it -- functionally identical to what
// PlayAnimation already does -- so that the thing being tested is ONLY "does a
// custom FAnimInstanceProxy survive contact with the shipping game", with no
// behaviour change tangled into the answer. The state-selection logic in
// TickLocomotionAnim is reused VERBATIM; this changes where the chosen sequence
// goes, not how it is chosen. Additive layers and montages are the next
// increments and are not in this file yet.
//
// It is gated behind pfa.UseAnimInstance (default 0 = OFF). At 0 the creature
// runs the shipping single-node path untouched; at 1 it runs this. Both paths
// coexist so they are A/B-able in one session with no rebuild.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// THREAD SAFETY -- the highest-risk part of this file, so it is stated once,
// here, and every member below is designed against it.
//
// Update_AnyThread and Evaluate_AnyThread run on the ANIMATION WORKER THREAD,
// not the game thread. The rules this proxy follows, without exception:
//
//   1. The worker thread reads ONLY PODs owned by this proxy, plus the engine
//      anim nodes it owns. It never touches a UObject we manage, never reads a
//      CVar, never calls GetWorld(), never logs a UObject name.
//   2. Every piece of gameplay state crosses into the proxy in PreUpdate(),
//      which the engine calls on the GAME THREAD before the parallel update
//      task is kicked (UAnimInstance::PreUpdateAnimation -> proxy.PreUpdate).
//      That is the ONLY writer of the nodes' inputs.
//   3. The one raw UObject pointer the worker DOES dereference is the
//      UAnimSequence inside the sequence-player node -- unavoidable, that is
//      what an anim node is. It is kept alive by TWO independent UPROPERTY
//      references (DesiredPose here, and the creature's own clip members in
//      PFASicklejaw.h), so it cannot be collected out from under an in-flight
//      evaluation.
//
// Where a choice existed, this file takes the conservative one and says so at
// the site. An intermittent worker-thread crash in a creature mod would be far
// worse than shipping nothing, because it would be blamed on everything except
// the animation graph.
// ---------------------------------------------------------------------------
USTRUCT()
struct FPFAAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

public:
	FPFAAnimInstanceProxy() = default;

	explicit FPFAAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	// GAME THREAD. Wires the graph, then hands off to the engine, which stores
	// GetCustomRootNode()'s result and initialises the tree from it.
	virtual void Initialize(UAnimInstance* InAnimInstance) override;

	// GAME THREAD. The one and only crossing point: gameplay state snapshotted
	// into the nodes before the worker thread can look at them -- and, in the
	// other direction, the worker's frame counter mirrored back out.
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;

protected:
	// The engine's documented entry point for a graph that is NOT compiled from
	// an AnimBlueprint: FAnimInstanceProxy::Initialize takes this as RootNode
	// when there is no IAnimClassInterface, and InitializeRootNode drives
	// Initialize_AnyThread / CacheBones / Update / Evaluate through it from then
	// on. Nothing else is needed to have a working native graph.
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

	// WORKER THREAD. Overridden ONLY to count that the graph really ran; the
	// base implementation does all the work. Deliberately the whole of our
	// worker-thread footprint -- one integer increment, no UObject, no CVar,
	// no world, nothing that can be collected or reallocated underneath it.
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;

private:
	// THE GRAPH. One node, on purpose (see the header comment).
	//
	// _Standalone, NOT the plain FAnimNode_SequencePlayer. This is not a style
	// choice and getting it wrong is silent: FAnimNode_SequencePlayer's
	// bLoopAnimation / PlayRate / StartPosition are #if WITH_EDITORONLY_DATA and
	// its accessors are constant-folded at cook time, so in a Shipping build
	// SetLoopAnimation() cannot take -- a one-shot would loop forever and a loop
	// might not. The _Standalone variant exists precisely for graphs built
	// outside the AnimBP compiler and carries real, settable members.
	FAnimNode_SequencePlayer_Standalone LocomotionPlayer;

	// --- w31-2: the additive layer. ONE of them, on purpose. ----------------
	//
	// The graph is  TailAdditive( Base = LocomotionPlayer, Additive = TailMoodPlayer ).
	// TailAdditive is the ROOT, always, even when the layer is switched off:
	// FAnimNode_ApplyAdditive with a non-relevant alpha evaluates ONLY its Base
	// and never touches the Additive branch, so an off layer is an exact
	// pass-through rather than a different graph. Choosing the root at
	// Initialize instead would mean the layer could not be toggled without
	// re-initialising the whole tree, and a root that changes shape between
	// sessions is a much worse thing to debug than one unused node.
	//
	// WHY ApplyAdditive AND NOT LayeredBoneBlend -- the alternative was
	// considered and rejected on the DATA, not on taste:
	//   - These twelve layers are AUTHORED ADDITIVE (A_Sicklejaw_Add_*): each
	//     clip is a DELTA from a base pose. ApplyAdditive's contract is exactly
	//     that. LayeredBoneBlend blends a FULL pose, so handing it a delta
	//     would drive the tail toward near-identity transforms -- a collapsed
	//     tail, not an angry one.
	//   - A mood must ADD to whatever the gait's tail is already doing (the
	//     tail keeps swinging while the creature walks, and gets angrier).
	//     Additive composes; a layered blend REPLACES the branch it covers, so
	//     at full weight the walk's tail motion would simply disappear.
	//   - "Tail-only" already lives in the DATA -- non-tail bones carry
	//     identity deltas -- so the per-bone machinery is redundant here.
	//     LayeredBoneBlend would add BlendMasks/LayerSetup/BlendWeights
	//     parallel arrays plus skeleton-GUID-keyed weight tables rebuilt at
	//     runtime: a lot of unproven state for a first additive increment.
	// LayeredBoneBlend IS the right node later, for a full-pose action that
	// must own the tail outright (a scripted tail pin), or if a mood clip turns
	// out to contaminate bones outside the tail and needs hard scoping.
	//
	// NEXT INCREMENT -- MONTAGES NEED AN FAnimNode_Slot. This graph has no slot
	// node, so UAnimInstance::Montage_Play would run its whole state machine and
	// produce no pose change at all: silent, and exactly the kind of thing that
	// gets rediscovered in game. Nothing regresses today (the melee action's
	// mActionMontage is deliberately null -- Actions/PFACreatureActionMelee.cpp),
	// but "we have a UAnimInstance now, so montages work" is NOT yet true.
	FAnimNode_SequencePlayer_Standalone TailMoodPlayer;
	FAnimNode_ApplyAdditive TailAdditive;

	// Same restart semantics as the pose serial, for the same reason.
	int32 AppliedTailSerial = INDEX_NONE;

	// Serial of the pose currently loaded into LocomotionPlayer. Compared
	// against the game thread's serial in PreUpdate so that re-requesting the
	// SAME sequence still RESTARTS it -- which is what the single-node path
	// this replaces does on every PlayAnimation call, and what the one-shot
	// layer depends on (two consecutive idle breaks that happen to roll the
	// same clip must both play from frame 0, not resume mid-clip).
	int32 AppliedPoseSerial = INDEX_NONE;

	// Worker-thread scratch. Incremented in Update_AnyThread's call chain (via
	// the root node), copied out on the GAME THREAD in the NEXT PreUpdate --
	// i.e. read strictly after the update task that wrote it has completed, so
	// a plain int32 is correct and an atomic would only add cost.
	//
	// It exists because "the graph is installed" and "the graph is actually
	// evaluating" are different claims and only the second is worth anything.
	// Zero after several seconds of play is the signature of a graph that
	// initialised and then never ran, which otherwise looks exactly like a
	// creature standing in its reference pose for an unrelated reason.
	int32 UpdatedFrames = 0;

	// Re-wired on every Initialize rather than once in the constructor.
	// FAnimInstanceProxy is copy-constructible (the engine copies BP proxies),
	// and a copied FPoseLink would still point at the ORIGINAL proxy's nodes --
	// a dangling cross-proxy graph that would evaluate the wrong creature's
	// pose or worse. Re-wiring at Initialize makes a copy self-correcting.
	void WireGraph();
};

UCLASS()
class PRIMALFAUNA_API UPFAAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// GAME THREAD ONLY. The replacement for USkeletalMeshComponent::PlayAnimation:
	// "play this sequence now". Each call restarts the clip, matching the
	// semantics of the call it replaces (see AppliedPoseSerial).
	void SetPose(UAnimSequence* InSequence, bool bInLoop);

	// GAME THREAD ONLY. w31-2: the additive tail-mood layer. A null sequence or
	// a zero alpha switches the layer off, and off means the ApplyAdditive node
	// is an exact pass-through of the locomotion pose.
	void SetTailMood(UAnimSequence* InSequence, float InAlpha);

	// GAME THREAD ONLY. What the graph is currently holding, for parity checks
	// and the status command.
	const UAnimSequence* GetRequestedPose() const { return DesiredPose; }
	bool DoesRequestedPoseLoop() const { return bDesiredPoseLoops; }
	const UAnimSequence* GetRequestedTailMood() const { return DesiredTailMood; }
	float GetRequestedTailAlpha() const { return DesiredTailAlpha; }

	// GAME THREAD ONLY. How many times the worker thread has actually updated
	// the graph. Zero after several seconds of play means the graph is
	// installed but dead -- the single most useful thing to be able to see.
	//
	// Reads a MIRROR the proxy pushes here in PreUpdate, NOT the proxy itself.
	// UAnimInstance::GetProxyOnGameThread would be the obvious spelling, but it
	// is a header-inline template that pulls in several more engine symbols,
	// and every additional engine symbol this module imports is another chance
	// at the shipping-export trap that 127-kills the whole mod at load
	// ([[sf-shipping-export-trap]]). A plain int is not worth that exposure.
	int32 GetUpdatedFrames() const { return UpdatedFramesMirror; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FPFAAnimInstanceProxy;

	// UPROPERTY-rooted so the sequence the worker thread is mid-evaluation on
	// cannot be garbage collected (cookbook GC-rooting convention; the same
	// reason APFASicklejaw holds its clips as TObjectPtr).
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> DesiredPose;

	bool bDesiredPoseLoops = true;

	// Bumped by every SetPose. Never reset -- wrapping after 2^31 pose changes
	// at 5Hz would take about thirteen years of continuous play, and the
	// comparison is an inequality, so even that only costs one missed restart.
	int32 DesiredPoseSerial = 0;

	// w31-2. GC-rooted for the same reason as DesiredPose: the worker thread
	// dereferences this sequence inside the additive branch.
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> DesiredTailMood;

	// Blend weight, 0..1. Pushed every frame rather than serialised, because it
	// is a live-tunable knob (pfa.TailMoodAlpha) and a float copy is cheaper
	// than the comparison that would avoid it.
	float DesiredTailAlpha = 0.0f;

	// Bumped only when the tail CLIP changes, so re-pushing the same mood every
	// pose change does not restart the layer mid-swing.
	int32 DesiredTailSerial = 0;

	// Written by the proxy on the game thread in PreUpdate. See GetUpdatedFrames.
	int32 UpdatedFramesMirror = 0;
};
