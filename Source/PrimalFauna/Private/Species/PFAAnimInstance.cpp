// PFAAnimInstance.cpp -- w31-1: the native anim graph.
//
// The design rationale, the thread-safety contract and the reason the
// _Standalone sequence player is not interchangeable with the plain one all
// live in PFAAnimInstance.h. This file is the mechanism.

#include "Species/PFAAnimInstance.h"
#include "Species/PFASicklejaw.h"
#include "PrimalFauna.h"
#include "PFACheats.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

// ---------------------------------------------------------------------------
// FPFAAnimInstanceProxy
// ---------------------------------------------------------------------------

void FPFAAnimInstanceProxy::WireGraph()
{
	LocomotionPlayer.SetSequence(nullptr);
	LocomotionPlayer.SetLoopAnimation(true);
	AppliedPoseSerial = INDEX_NONE;

	TailMoodPlayer.SetSequence(nullptr);
	TailMoodPlayer.SetLoopAnimation(true);   // a mood is a state, not an event
	AppliedTailSerial = INDEX_NONE;

	// The links. Order is load-bearing: SetLinkNode MUST run before
	// InitializeRootNode walks the tree, or FPoseLinkBase::AttemptRelink finds
	// a null LinkedNode with a LinkID of INDEX_NONE (there is no AnimBlueprint
	// to resolve an ID against), silently leaves the branch unlinked, and the
	// layer never evaluates while the rest of the graph looks perfectly
	// healthy. Initialize() calls this before Super for exactly that reason.
	TailAdditive.Base.SetLinkNode(&LocomotionPlayer);
	TailAdditive.Additive.SetLinkNode(&TailMoodPlayer);
	// Start OFF. Alpha is pushed from the game thread each PreUpdate; zero here
	// means the very first evaluated frame is a plain locomotion pose even if
	// the CVar is on, rather than a frame of additive applied to a graph that
	// has not been told what its mood clip is yet.
	TailAdditive.Alpha = 0.0f;
}

void FPFAAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	WireGraph();

	// Super LAST: FAnimInstanceProxy::Initialize is what calls
	// GetCustomRootNode() and caches the result as RootNode, so the graph must
	// already be wired when it runs.
	FAnimInstanceProxy::Initialize(InAnimInstance);

	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] anim graph initialised (native proxy: ApplyAdditive[ Base=SequencePlayer, Additive=SequencePlayer ]) for %s"),
		*GetNameSafe(InAnimInstance));
}

FAnimNode_Base* FPFAAnimInstanceProxy::GetCustomRootNode()
{
	return &TailAdditive;
}

void FPFAAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	// Every node in the graph, root included. The engine uses this list to give
	// nodes their OnInitializeAnimInstance / PreUpdate / dynamic-reset hooks --
	// a node left out of it still evaluates but silently loses those, which is
	// the kind of partial-wiring bug that presents as "works, mostly".
	OutNodes.Add(&TailAdditive);
	OutNodes.Add(&LocomotionPlayer);
	OutNodes.Add(&TailMoodPlayer);
}

void FPFAAnimInstanceProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	++UpdatedFrames;
	FAnimInstanceProxy::UpdateAnimationNode(InContext);
}

void FPFAAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	// GAME THREAD. Everything the worker thread will read this frame is written
	// here and nowhere else.
	if (UPFAAnimInstance* PFAInstance = Cast<UPFAAnimInstance>(InAnimInstance))
	{
		// Out: the worker's counter, from the update task that has already
		// finished. Reading it here rather than mid-flight is what makes a
		// plain int32 safe.
		PFAInstance->UpdatedFramesMirror = UpdatedFrames;

		// In: the pose the game thread wants. Serial-compared, not
		// pointer-compared, so that re-requesting the SAME clip restarts it --
		// see AppliedPoseSerial in the header for why that is load-bearing.
		if (PFAInstance->DesiredPoseSerial != AppliedPoseSerial)
		{
			AppliedPoseSerial = PFAInstance->DesiredPoseSerial;
			LocomotionPlayer.SetSequence(PFAInstance->DesiredPose);
			LocomotionPlayer.SetLoopAnimation(PFAInstance->bDesiredPoseLoops);
			// Rewind. Without this the new clip inherits the previous one's
			// accumulated time and starts partway through -- and for a repeat
			// of the same clip it would not restart at all.
			LocomotionPlayer.SetAccumulatedTime(0.0f);

			if (PFADiag::IsEnabled())
			{
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] anim graph pose -> %s (loop=%s serial=%d updatedFrames=%d)"),
					*GetNameSafe(PFAInstance->DesiredPose),
					PFAInstance->bDesiredPoseLoops ? TEXT("yes") : TEXT("no"),
					AppliedPoseSerial, UpdatedFrames);
			}
		}

		// In: the additive tail layer. Alpha is copied every frame because it is
		// a live-tunable knob; the CLIP is serial-gated so that re-pushing the
		// same mood on every pose change does not rewind the tail mid-swing.
		TailAdditive.Alpha = PFAInstance->DesiredTailAlpha;
		if (PFAInstance->DesiredTailSerial != AppliedTailSerial)
		{
			AppliedTailSerial = PFAInstance->DesiredTailSerial;
			TailMoodPlayer.SetSequence(PFAInstance->DesiredTailMood);
			TailMoodPlayer.SetAccumulatedTime(0.0f);

			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] anim graph tail layer -> %s (alpha=%.2f serial=%d)"),
				*GetNameSafe(PFAInstance->DesiredTailMood),
				PFAInstance->DesiredTailAlpha, AppliedTailSerial);
		}
	}

	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
}

// ---------------------------------------------------------------------------
// UPFAAnimInstance
// ---------------------------------------------------------------------------

FAnimInstanceProxy* UPFAAnimInstance::CreateAnimInstanceProxy()
{
	return new FPFAAnimInstanceProxy(this);
}

void UPFAAnimInstance::SetPose(UAnimSequence* InSequence, bool bInLoop)
{
	// No early-out on "same sequence, same loop flag". This is the replacement
	// for PlayAnimation, and PlayAnimation restarts unconditionally; the
	// one-shot layer relies on that (a second idle break that rolls the same
	// clip must play again from the start, not be swallowed as a no-op).
	DesiredPose = InSequence;
	bDesiredPoseLoops = bInLoop;
	++DesiredPoseSerial;
}

void UPFAAnimInstance::SetTailMood(UAnimSequence* InSequence, float InAlpha)
{
	// Clip changes bump the serial (and so restart the layer); alpha changes do
	// not. A mood is a continuous state -- sliding its weight up and down must
	// not snap the tail back to the start of the loop.
	if (DesiredTailMood != InSequence)
	{
		DesiredTailMood = InSequence;
		++DesiredTailSerial;
	}
	// A missing clip must read as OFF, not as "full weight on nothing", which
	// would leave the ApplyAdditive node evaluating an empty branch.
	DesiredTailAlpha = InSequence ? FMath::Clamp(InAlpha, 0.0f, 1.0f) : 0.0f;
}

// ---------------------------------------------------------------------------
// THE DRIVER SWITCH -- APFASicklejaw's side of the change.
//
// These two methods are APFASicklejaw's but live here, with the AnimInstance
// they install, so that "everything about the native animation path" is one
// file. The alternative was appending them to PFASicklejaw.cpp (835 lines) or
// PFASicklejawAnim.cpp (605), both already past the workspace 500-line rule.
// ---------------------------------------------------------------------------
namespace
{
	// THE gate for this whole packet. Default 0: the creature runs exactly the
	// shipping single-node path, byte for byte, and none of the code above ever
	// executes. Live-switchable so the two paths can be compared inside one
	// session against the same creature, in the same place, on the same save --
	// which is the only comparison that would actually catch a regression.
	static TAutoConsoleVariable<int32> CVarPFAUseAnimInstance(
		TEXT("pfa.UseAnimInstance"), 0,
		TEXT("0 (default): drive Sicklejaw animation through the proven single-node player (PlayAnimation). 1: drive it through the native UPFAAnimInstance anim graph. Takes effect on the next pose change (<=0.2s) -- no respawn, no rebuild. Check with PFA.AnimGraph."),
		ECVF_Default);

	// w31-2. Separate gate from pfa.UseAnimInstance so a failure can be
	// attributed: if the creature is fine at UseAnimInstance=1/TailMoodLayer=0
	// and breaks at TailMoodLayer=1, the additive node is the cause and nothing
	// else is in question.
	//
	// Mode 2 exists because of a defect in the ASSETS, found statically while
	// building this: none of the twelve A_Sicklejaw_Add_* clips is actually
	// flagged additive. Their AdditiveAnimType is AAT_None -- the FBX import
	// (tools/import_sicklejaw_ue.py) never sets it, so they imported as
	// ordinary full-pose sequences whose names merely say "Add". Blending a
	// full pose through AccumulateAdditivePose adds absolute bone transforms on
	// top of the gait, i.e. doubles every rotation: visible garbage.
	// So mode 1 REFUSES a non-additive clip and says why, and mode 2 forces it
	// anyway -- for proving the node evaluates, at the price of looking wrong.
	static TAutoConsoleVariable<int32> CVarPFATailMoodLayer(
		TEXT("pfa.TailMoodLayer"), 0,
		TEXT("Additive tail-mood layer (needs pfa.UseAnimInstance 1). 0 (default): off. 1: on, but SKIPPED unless the clip is really flagged additive. 2: FORCE on even for a non-additive clip -- proves the ApplyAdditive node evaluates, expect a visibly wrong pose."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarPFATailMoodAlpha(
		TEXT("pfa.TailMoodAlpha"), 1.0f,
		TEXT("Blend weight of the additive tail-mood layer, 0..1. Live -- no respawn."),
		ECVF_Default);

	static TAutoConsoleVariable<FString> CVarPFATailMoodClip(
		TEXT("pfa.TailMoodClip"), TEXT("Add_Tail_Threat"),
		TEXT("Which tail-mood clip the additive layer plays, as a bare suffix: Add_Tail_Calm / Add_Tail_Alert / Add_Tail_Threat / Add_Tail_Aggressive / Add_Tail_Fear / Add_Tail_Wounded. Re-resolved when this changes."),
		ECVF_Default);

	// w31-3 (cold review D1): push every one of the four CVars above at the
	// live creatures the moment it is typed, instead of waiting for a pose
	// change that may never come.
	//
	// A sink, not a per-tick poll: the poll would cost four CVar reads on every
	// creature's 0.2s timer forever, to serve an event that happens when a
	// developer types a word. The sink costs nothing until then. There is no
	// world here (this runs at console-set time, on the game thread), so the
	// creatures are found via GEngine's world list -- the same way the mod's
	// other global CVar-driven behaviour would have to.
	void RefreshAnimationDriverOnAllCreatures(IConsoleVariable* /*Var*/)
	{
		if (!GEngine)
		{
			return;
		}
		int32 Refreshed = 0;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			// Game worlds only. A CVar sink fires for every world the process
			// has, and reaching into an inactive or preview world to mutate
			// actors is how a dev knob turns into a crash report.
			if (!World || !World->IsGameWorld())
			{
				continue;
			}
			for (TActorIterator<APFASicklejaw> It(World); It; ++It)
			{
				It->RefreshAnimationDriver();
				++Refreshed;
			}
		}
		// Always-on. This line is the receipt that typing the CVar reached the
		// creatures; "0 live Sicklejaw(s)" is itself the answer to the most
		// likely confusion (nothing spawned yet).
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] animation CVar changed -> refreshed the driver on %d live Sicklejaw(s). Confirm with PFA.AnimGraph."),
			Refreshed);
	}

	// Registered once, at static init, alongside the CVars themselves.
	const bool bRegistered_AnimCVarSinks = []
	{
		const FConsoleVariableDelegate Sink =
			FConsoleVariableDelegate::CreateStatic(&RefreshAnimationDriverOnAllCreatures);
		CVarPFAUseAnimInstance->SetOnChangedCallback(Sink);
		CVarPFATailMoodLayer->SetOnChangedCallback(Sink);
		CVarPFATailMoodAlpha->SetOnChangedCallback(Sink);
		CVarPFATailMoodClip->SetOnChangedCallback(Sink);
		return true;
	}();
}

bool APFASicklejaw::EnsureAnimationDriver()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return false;
	}

	const bool bWant = (CVarPFAUseAnimInstance.GetValueOnGameThread() != 0) && !bAnimInstanceDriverFailed;

	// The COMPONENT is the authority on what is installed, not our own flag.
	// SetAnimationMode is public engine API and PlayAnimation calls it
	// unconditionally, so the mesh can lose its AnimInstance without us doing
	// anything; trusting a cached bool here would mean happily pushing poses
	// into an instance that no longer exists.
	const bool bHaveInstance = (Cast<UPFAAnimInstance>(MeshComponent->AnimScriptInstance) != nullptr);
	if (bWant == bHaveInstance)
	{
		return bHaveInstance;
	}

	// A switch is about to tear down and rebuild the animation tree. Tell the
	// foot-plant keeper to stand down while the pose is in flux (cold review
	// D2): two ticks at the 0.2s locomotion cadence, which comfortably covers
	// the re-init plus the first real evaluation.
	FootPlantSkipTicks = 2;

	if (bWant)
	{
		// One call does the whole install: SetAnimInstanceClass sets
		// AnimationMode to AnimationBlueprint and re-runs InitAnim, which
		// spawns the instance. A native UAnimInstance subclass is accepted
		// because NeedToSpawnAnimScriptInstance only demands skeleton
		// compatibility when the class carries an IAnimClassInterface, which a
		// non-Blueprint class does not.
		MeshComponent->SetAnimInstanceClass(UPFAAnimInstance::StaticClass());

		UPFAAnimInstance* Installed = Cast<UPFAAnimInstance>(MeshComponent->AnimScriptInstance);
		const bool bInstalled = (Installed != nullptr);
		if (!bInstalled)
		{
			// Latch off permanently for this creature. Loud, never diag-gated:
			// a creature silently running the old path while the CVar says
			// otherwise would make every subsequent A/B measurement a lie.
			bAnimInstanceDriverFailed = true;
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] %s: pfa.UseAnimInstance=1 but no UPFAAnimInstance was created (mesh=%s animMode=%d instance=%s) -- FALLING BACK to single-node playback permanently for this creature."),
				*GetName(),
				*GetNameSafe(MeshComponent->GetSkinnedAsset()),
				static_cast<int32>(MeshComponent->GetAnimationMode()),
				*GetNameSafe(MeshComponent->AnimScriptInstance));
			return false;
		}

		// Seed the fresh graph with the pose that is ALREADY playing, before
		// anything else can look at it (cold review D2). The install itself has
		// just evaluated the graph once with a null sequence -- i.e. reference
		// pose -- and without this the creature would stay in that T-pose until
		// the next locomotion tick happened to change state, which for a
		// standing creature can be a long time.
		if (ActivePoseAnim)
		{
			Installed->SetPose(ActivePoseAnim, bActivePoseLoops);
			PushTailMood(Installed);
		}

		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: animation driver -> ANIMINSTANCE (native anim graph installed, seeded with %s; PFA.AnimGraph reports whether it evaluates)."),
			*GetName(), ActivePoseAnim ? *ActivePoseAnim->GetName() : TEXT("<no pose yet>"));
		return true;
	}

	// Back to the shipping path. Re-seat the live pose immediately for the same
	// reason as above -- SetAnimationMode also rebuilds the tree, and the
	// single-node player starts empty.
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	if (ActivePoseAnim)
	{
		MeshComponent->PlayAnimation(ActivePoseAnim, bActivePoseLoops);
	}
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: animation driver -> SINGLE-NODE (pfa.UseAnimInstance=0, re-seeded with %s)."),
		*GetName(), ActivePoseAnim ? *ActivePoseAnim->GetName() : TEXT("<no pose yet>"));
	return false;
}

void APFASicklejaw::ApplyChosenPose(UAnimSequence* Sequence, bool bLoop)
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent || !Sequence)
	{
		return;
	}

	// Record BEFORE switching drivers, so that a switch triggered by this very
	// call seeds the new graph with the pose we are about to play rather than
	// with the previous one (cold review D2). Re-seeding and then applying the
	// same pose is harmless -- both collapse into one PreUpdate.
	ActivePoseAnim = Sequence;
	bActivePoseLoops = bLoop;

	if (EnsureAnimationDriver())
	{
		if (UPFAAnimInstance* AnimInstance = Cast<UPFAAnimInstance>(MeshComponent->AnimScriptInstance))
		{
			AnimInstance->SetPose(Sequence, bLoop);
			PushTailMood(AnimInstance);
			return;
		}
		// Unreachable unless the instance vanished between the check and here.
		// Latch, so the fallback below cannot start a switch/PlayAnimation
		// thrash loop with EnsureAnimationDriver.
		bAnimInstanceDriverFailed = true;
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: AnimInstance disappeared between install and use -- single-node fallback latched."), *GetName());
	}

	MeshComponent->PlayAnimation(Sequence, bLoop);

	// w35-1 (user, in game): "It looks like they do the same things at the same
	// time, not as independent entities."
	//
	// They were synchronised because nothing ever desynchronised them.
	// PlayAnimation starts every clip at position 0, so two creatures that enter
	// the same state on the same frame -- which is exactly what happens when a
	// nest spawns a group, or when a pack all notices the player at once -- walk
	// in lockstep forever. Correct code, no error, and instantly readable as
	// machinery rather than animals.
	//
	// A per-creature phase offset, generated ONCE at spawn, fixes it: the same
	// creature keeps its own phase across state changes (so it reads as an
	// individual rather than as jitter), while two creatures never share one.
	// LOOPS only -- a one-shot must start at frame 0 or its authored impact
	// frames land at the wrong time, which would silently break the bite.
	if (bLoop && LocomotionPhaseOffset > 0.0f)
	{
		MeshComponent->SetPosition(LocomotionPhaseOffset * Sequence->GetPlayLength(), /*bFireNotifies*/ false);
	}
}

void APFASicklejaw::RefreshAnimationDriver()
{
	// Make the animation CVars mean what their help text says (cold review D1).
	// ApplyChosenPose -- where every CVar in this file is read -- only runs on a
	// pose CHANGE, and a creature can hold one pose indefinitely: standing in
	// Fight, chasing at constant speed, or pinned by PFA.PlayAnim <clip> 1,
	// which short-circuits TickLocomotionAnim before any pose call at all. In
	// those states typing pfa.UseAnimInstance 1 appeared to do nothing, which
	// would have made the whole A/B read as "the new path does not install".
	if (!GetMesh())
	{
		return;
	}

	// Force the looping selection to re-apply on the next tick -- the existing
	// idiom for "whatever was cached is no longer authoritative" (ClearOneShot
	// uses the same line for the same reason).
	LocomotionState = -1;

	// And act NOW rather than waiting for that tick, because the pinned-clip
	// case never reaches it. EnsureAnimationDriver re-seats the live pose on
	// whichever driver it installs, so this is a complete handover, not just a
	// mode flip.
	const bool bAnimInstanceLive = EnsureAnimationDriver();
	if (bAnimInstanceLive)
	{
		if (UPFAAnimInstance* AnimInstance = Cast<UPFAAnimInstance>(GetMesh()->AnimScriptInstance))
		{
			// Re-push the tail layer even when the driver did not change: the
			// tail CVars route through here too.
			PushTailMood(AnimInstance);
		}
	}
}

void APFASicklejaw::PushTailMood(UPFAAnimInstance* AnimInstance)
{
	// GAME THREAD. Every decision about the additive layer is made here, so the
	// worker thread only ever sees a sequence pointer and a float.
	if (!AnimInstance)
	{
		return;
	}

	const int32 Mode = CVarPFATailMoodLayer.GetValueOnGameThread();
	if (Mode == 0)
	{
		AnimInstance->SetTailMood(nullptr, 0.0f);
		return;
	}

	// Console strings are untrusted input and this one is concatenated into a
	// package path; ResolveTailMoodClip validates it at that boundary, exactly
	// like PFA.PlayAnim does (w29-2 review W6). It also caches, so this only
	// touches the package system when the CVar actually changes.
	if (!ResolveTailMoodClip(CVarPFATailMoodClip.GetValueOnGameThread()))
	{
		AnimInstance->SetTailMood(nullptr, 0.0f);
		return;
	}

	// THE GUARD THIS LAYER EXISTS BEHIND. AccumulateAdditivePose treats its
	// input as a DELTA; a clip that is not flagged additive is a full pose, and
	// adding a full pose to the gait doubles every bone transform. None of the
	// authored Add_* clips currently carries the flag (see the CVar comment),
	// so mode 1 declines and explains itself instead of shipping a creature
	// that folds in half.
	// THREE conditions, not one (cold review D3). `!= AAT_None` alone passes
	// exactly the two states a half-finished asset fix produces:
	//   - AAT_LocalSpaceBase with no usable base pose (RefPoseType/RefPoseSeq/
	//     RefFrameIndex unset). UAnimSequence::IsValidAdditive is the engine's
	//     own test for that, and the sequence player would log its
	//     AdditiveMismatchWarning and emit garbage.
	//   - AAT_RotationOffsetMeshSpace. FAnimNode_ApplyAdditive HARD-CODES
	//     AAT_LocalSpaceBase when it accumulates, so a mesh-space additive is
	//     applied in the wrong space -- wrong result, and, unlike the case
	//     above, NO warning from anywhere. Mesh-space is the natural authoring
	//     choice for a head-look, i.e. the very next layer we want.
	const EAdditiveAnimationType AdditiveType = TailMoodAnim->GetAdditiveAnimType();
	const bool bIsAdditive =
		(AdditiveType == AAT_LocalSpaceBase) && TailMoodAnim->IsValidAdditive();
	if (!bIsAdditive && Mode != 2)
	{
		// First occurrence per creature is always-on: "the tail layer does
		// nothing" and "the tail layer is disabled on purpose" are otherwise
		// indistinguishable, which is the ambiguity that cost w24 three
		// sessions on the locomotion clips.
		if (TailMoodSkipLogCount < 1 || PFADiag::IsEnabled())
		{
			++TailMoodSkipLogCount;
			// Name WHICH of the three conditions failed. "Not additive" alone
			// would send someone to re-import a clip that is already flagged
			// and whose real problem is a missing base pose or the wrong space.
			const TCHAR* Reason =
				(AdditiveType == AAT_None) ? TEXT("AdditiveAnimType=AAT_None -- imported as an ordinary pose, not an additive one")
				: (AdditiveType != AAT_LocalSpaceBase) ? TEXT("additive but MESH-SPACE -- FAnimNode_ApplyAdditive only accumulates local-space, so re-author as Local Space")
				: TEXT("flagged additive but IsValidAdditive() is false -- the base pose is missing (check RefPoseType / RefPoseSeq / RefFrameIndex)");
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] %s: tail-mood layer SKIPPED -- clip %s: %s. ")
				TEXT("Blending it anyway would corrupt every bone transform. Fix the ASSET (re-import as Additive / Local Space with a ref-pose base -- tools/import_sicklejaw_ue.py never sets these), or set pfa.TailMoodLayer 2 to force it and prove the node runs."),
				*GetName(), *GetNameSafe(TailMoodAnim), Reason);
		}
		AnimInstance->SetTailMood(nullptr, 0.0f);
		return;
	}

	const float Alpha = CVarPFATailMoodAlpha.GetValueOnGameThread();
	AnimInstance->SetTailMood(TailMoodAnim, Alpha);

	if (!bIsAdditive && TailMoodForceLogCount < 1)
	{
		++TailMoodForceLogCount;
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: tail-mood layer FORCED (pfa.TailMoodLayer 2) with the NON-additive clip %s at alpha %.2f. ")
			TEXT("The pose will be visibly wrong; this mode only answers 'does the ApplyAdditive node evaluate at all'."),
			*GetName(), *GetNameSafe(TailMoodAnim), Alpha);
	}
}

// ---------------------------------------------------------------------------
// PFA.AnimGraph -- the A/B verification lever.
//
// The whole point of this increment is falsifiability, and "the creature still
// animates" is NOT evidence that the new path ran: at pfa.UseAnimInstance 0 it
// animates via the old one, and a graph that installs and never evaluates looks
// identical to a graph that works. This prints, per creature, which driver is
// live and how many frames the worker thread has actually updated the graph --
// the two facts that separate those cases.
// ---------------------------------------------------------------------------
namespace
{
	void AnimGraphStatusCommand(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] PFA.AnimGraph aborted: no world context."));
			return;
		}

		int32 Count = 0;
		for (TActorIterator<APFASicklejaw> It(World); It; ++It)
		{
			APFASicklejaw* Creature = *It;
			++Count;

			const USkeletalMeshComponent* MeshComponent = Creature->GetMesh();
			const UAnimInstance* Instance = MeshComponent ? MeshComponent->AnimScriptInstance.Get() : nullptr;
			const UPFAAnimInstance* PFAInstance = Cast<const UPFAAnimInstance>(Instance);

			if (!PFAInstance)
			{
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] PFA.AnimGraph %s: driver=SINGLE-NODE (animMode=%d animInstance=%s) -- the shipping path."),
					*Creature->GetName(),
					MeshComponent ? static_cast<int32>(MeshComponent->GetAnimationMode()) : -1,
					*GetNameSafe(Instance));
				continue;
			}

			const int32 Frames = PFAInstance->GetUpdatedFrames();
			const UAnimSequence* TailMood = PFAInstance->GetRequestedTailMood();
			const float TailAlpha = PFAInstance->GetRequestedTailAlpha();
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] PFA.AnimGraph %s: driver=ANIMINSTANCE pose=%s loop=%s tailLayer=%s alpha=%.2f%s updatedFrames=%d -> %s"),
				*Creature->GetName(),
				*GetNameSafe(PFAInstance->GetRequestedPose()),
				PFAInstance->DoesRequestedPoseLoop() ? TEXT("yes") : TEXT("no"),
				TailMood ? *TailMood->GetName() : TEXT("off"),
				TailAlpha,
				// The additive type is the thing that decides whether the layer
				// can be correct, so it is printed next to the clip rather than
				// left to be inferred from a warning further up the log.
				TailMood ? ((TailMood->GetAdditiveAnimType() == AAT_LocalSpaceBase && TailMood->IsValidAdditive())
							? TEXT(" (local-space additive)") : TEXT(" (NOT A VALID LOCAL-SPACE ADDITIVE -- forced)")) : TEXT(""),
				Frames,
				Frames > 0 ? TEXT("GRAPH IS EVALUATING")
						   : TEXT("GRAPH INSTALLED BUT HAS NEVER EVALUATED -- this is the failure to chase"));
		}

		if (Count == 0)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] PFA.AnimGraph: no Sicklejaw in this world (spawn one with PFA.SpawnNest)."));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CCmdPFAAnimGraph(
		TEXT("PFA.AnimGraph"),
		TEXT("Primal Fauna: per-Sicklejaw animation driver status -- single-node vs native AnimInstance, the live pose, and how many frames the worker thread has actually updated the graph."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			AnimGraphStatusCommand(World);
		}));
}
