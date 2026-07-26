// PFASicklejawAnim.cpp -- w29-1: the ONE-SHOT clip layer.
//
// WHY THIS FILE EXISTS
// The w24/w28 locomotion driver plays a LOOP chosen from speed + creature
// state. That covers gaits and idles, and it is the whole of what a "state
// machine over one animation node" can express. But 36 clips were authored and
// only 6 of them are loops -- the comfort breaks, the threat display, the limp,
// the sleep and the prey pin are all ONE-SHOTS: they play once, they say
// something, and the creature goes back to what it was doing. Those are exactly
// what the single-node player CAN still do, and they are most of what makes an
// idle creature look alive rather than parked.
//
// WHAT IT DELIBERATELY DOES NOT DO
// The 12 additive layers (breathing, six tail moods, head-look, saccades) are
// NOT reachable from here and no amount of scheduling changes that:
// USkeletalMeshComponent's single-node path plays exactly ONE sequence, so
// "idle PLUS breathing PLUS an angry tail" has no representation. That needs a
// real UAnimInstance with a blend graph, which is engineering, not wiring --
// tracked as its own packet. Everything in this file works within the one-node
// limit rather than pretending around it.
//
// Split out of PFASicklejaw.cpp (798 lines and over the workspace 500-line
// rule) so the animation layer has its own home instead of making that worse.

#include "Species/PFASicklejaw.h"
#include "PrimalFauna.h"
#include "PFACheats.h"

#include "FGHealthComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
	const TCHAR* AnimPackagePrefix = TEXT("/PrimalFauna/Sicklejaw/Anims/A_Sicklejaw_");

	// The looping driver ticks at 0.2s (LocomotionTickSeconds in
	// PFASicklejaw.cpp). A one-shot's deadline gets that much slack so its LAST
	// frame is actually on screen for a moment before the loop takes the pose
	// back -- without it, a clip whose length lands just under a tick boundary
	// gets cut on its final pose, which is precisely the frame it was authored
	// to land on (the threat display's held strike, the sleep's settle).
	constexpr float OneShotTailSlackSeconds = 0.2f;

	// BaseState sentinel for PFA.PlayAnim: a dev-requested clip is never
	// interrupted by a state change, because the whole point is to watch the
	// clip through at real scale in the real game.
	constexpr int32 DevOneShotBaseState = -2;

	// BaseState sentinel for an attack strike. Also immune to state changes --
	// a bite is a committed act, and releasing it the instant the creature's
	// speed band flickers would cut the strike in half. It still expires on the
	// clip's own clock, so the worst case is ~1.2s of a bite pose while moving,
	// not a stuck creature.
	constexpr int32 AttackOneShotBaseState = -3;

	struct FIdleBreakDef
	{
		const TCHAR* ClipSuffix;
		float Weight;
	};

	// WEIGHTS -- and an honest account of why they are not the measured ones.
	//
	// _team/primalfauna-creature-behavior-reference.md §7.1 measured comfort-bout
	// composition as preen 72.5% / head-shake 15.9 / scratch 4.0 / stretch 3.7 /
	// tail-wag 2.5 / yawn 1.4, at ~5.7 bouts per HOUR. Reproduced literally that
	// would mean:
	//   - a comfort act about once every 10 minutes, when a player's encounter
	//     with a creature lasts seconds. The entire authored comfort set would be
	//     invisible in normal play.
	//   - the tail of the distribution (scratch, stretch) appearing roughly once
	//     per four hours of watching ONE creature.
	// So this keeps the measured ORDERING -- preening dominates, scratching is
	// rare -- and compresses the tail so every authored act is actually seen.
	// LookAround and Sniff are not in §7.1's comfort table at all; they are
	// vigilance acts, weighted high here because they read at distance where
	// preening does not. That is a GAMEPLAY judgement, tagged as one.
	// (No yawn or tail-wag clip exists yet -- their share is spread, not faked.)
	const FIdleBreakDef SicklejawIdleBreaks[] = {
		{ TEXT("LookAround"),          22.0f },   // vigilance -- gameplay weight
		{ TEXT("IdleBreak_Preen"),     20.0f },   // §7.1's dominant comfort act
		{ TEXT("Sniff"),               14.0f },   // vigilance -- gameplay weight
		{ TEXT("IdleBreak_HeadShake"), 12.0f },
		{ TEXT("ShakeOff"),            12.0f },
		{ TEXT("Stretch"),             10.0f },
		{ TEXT("IdleBreak_Scratch"),   10.0f },
	};

	static TAutoConsoleVariable<int32> CVarPFAIdleBreaks(
		TEXT("pfa.IdleBreaks"), 1,
		TEXT("1 (default): calm Sicklejaws occasionally play a comfort/vigilance break (preen, sniff, look around, scratch...). 0: idle loop only."),
		ECVF_Default);

	// Mean gap between breaks for a calm, undisturbed creature. The measured
	// real-animal figure is ~10.5 minutes (see above); this is the gameplay
	// cadence and is a knob precisely because it is a judgement call.
	//
	// w46-1: 14.0 -> 8.5. User on w45-3: they want visibly more going on when a
	// creature has arrived somewhere. The log says only FIVE breaks fired in an
	// entire session (LookAround x2, Stretch, Sniff, ShakeOff) with all 7 clips
	// resolved 7/7, so the authored inventory was effectively invisible -- the same
	// failure mode as w44-1's unreachable comfort set, just quantitative instead of
	// absolute. Jitter is unchanged, so the gap is now 4.7-13.6s.
	//
	// HONEST ABOUT THE LIKELY REAL LIMITER: the gate is DesiredState == 0 (calm
	// idle) and that session was dominated by roaming -- Walk 23 / Prowl 19 poses
	// against Idle 15 -- so the binding constraint is probably how little time a
	// creature spends calm and still, not the mean. The gate is NOT being widened
	// (a creature grooming while hunted reads as broken), so the new diagnostics in
	// MaybeStartOneShot below report which of the two it actually is: a break that
	// comes due while the creature is busy now says so out loud.
	static TAutoConsoleVariable<float> CVarPFAIdleBreakMeanSeconds(
		TEXT("pfa.IdleBreakMeanSeconds"), 8.5f,
		TEXT("Mean seconds between idle breaks on a calm Sicklejaw (jittered 0.55x-1.6x, so 4.7-13.6s). Research cadence is ~630s; 8.5 is the gameplay-visible compression (was 14.0 through w45-3)."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarPFAThreatenDisplay(
		TEXT("pfa.ThreatenDisplay"), 1,
		TEXT("1 (default): a Sicklejaw that notices you performs ONE ritualised threat display per alert episode before settling into its alert idle. 0: skip the display."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarPFALimpHealthFraction(
		TEXT("pfa.LimpHealthFraction"), 0.35f,
		TEXT("Below this health fraction a Sicklejaw limps instead of walking/prowling (never instead of running -- a wounded animal still flees at speed)."),
		ECVF_Default);

	// Single-candidate clip resolver. The multi-candidate probe in
	// PFASicklejaw.cpp exists for the legacy *_Anim_preset_* twins that the
	// w24-2 multi-take FBXs produced; the current one-clip-per-FBX export
	// makes the primary package always correct, so this stays simple. Still
	// scans the package for whatever UAnimSequence it contains rather than
	// trusting the object name -- that assumption is what cost w24 three
	// builds.
	UAnimSequence* ResolveClip(const TCHAR* ClipSuffix, const FString& OwnerName)
	{
		const FString PackagePath = FString(AnimPackagePrefix) + ClipSuffix;
		UPackage* Package = LoadPackage(nullptr, *PackagePath, LOAD_None);
		if (!Package)
		{
			if (PFADiag::IsEnabled())
			{
				UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s: clip probe %s: package NOT FOUND"),
					*OwnerName, *PackagePath);
			}
			return nullptr;
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
		if (!Found && PFADiag::IsEnabled())
		{
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s: clip probe %s: package loaded but holds NO UAnimSequence"),
				*OwnerName, *PackagePath);
		}
		return Found;
	}
}

void APFASicklejaw::ResolveOneShotClips()
{
	const FString OwnerName = GetName();

	IdleBreakClips.Reset();
	int32 ResolvedBreaks = 0;
	for (const FIdleBreakDef& Def : SicklejawIdleBreaks)
	{
		UAnimSequence* Sequence = ResolveClip(Def.ClipSuffix, OwnerName);
		IdleBreakClips.Add(Sequence);
		if (Sequence)
		{
			++ResolvedBreaks;
		}
	}
	ThreatenAnim = ResolveClip(TEXT("Threaten"), OwnerName);
	LimpAnim = ResolveClip(TEXT("Limp"), OwnerName);

	// Always-on (not diag-gated): "the creature never preens" is otherwise
	// indistinguishable from "the clips did not resolve", and that ambiguity is
	// what made the w24 invisible-animation hunt take three sessions.
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: one-shot clips resolved -- idle breaks %d/%d, threaten=%s, limp=%s"),
		*OwnerName, ResolvedBreaks, IdleBreakClips.Num(),
		ThreatenAnim ? TEXT("ok") : TEXT("absent"),
		LimpAnim ? TEXT("ok") : TEXT("absent"));
}

// w31-2: the additive tail-mood layer's clip. Lives here with the other clip
// resolution rather than beside its caller, so ResolveClip stays the single
// package-probing routine -- w24 paid three builds for guessing at asset names
// in more than one place.
bool APFASicklejaw::ResolveTailMoodClip(const FString& ClipSuffix)
{
	if (ClipSuffix == TailMoodClipResolved)
	{
		return TailMoodAnim != nullptr;   // steady state: one string compare
	}
	TailMoodClipResolved = ClipSuffix;
	TailMoodAnim = nullptr;

	// Same boundary validation as PFA.PlayAnim (review W6): a CVar string is
	// untrusted input and is concatenated straight into a package path below.
	if (ClipSuffix.IsEmpty() || ClipSuffix.Contains(TEXT("/")) || ClipSuffix.Contains(TEXT("\\")) || ClipSuffix.Contains(TEXT("..")))
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: pfa.TailMoodClip '%s' rejected -- expected a bare clip suffix like Add_Tail_Threat, no paths."),
			*GetName(), *ClipSuffix);
		return false;
	}

	UAnimSequence* Sequence = ResolveClip(*ClipSuffix, GetName());
	if (!Sequence)
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: pfa.TailMoodClip '%s' did not resolve -- expected package %s%s. The tail layer stays off."),
			*GetName(), *ClipSuffix, AnimPackagePrefix, *ClipSuffix);
		return false;
	}

	// Same skeleton guard as every other clip path (w29-2 review W3): an
	// additive layer played against the wrong bone hierarchy is undefined, and
	// unlike a gait it would be blended INTO a pose that otherwise looks right,
	// making it much harder to attribute.
	const USkeletalMeshComponent* MeshComponent = GetMesh();
	const USkeletalMesh* SkeletalMesh = MeshComponent ? Cast<USkeletalMesh>(MeshComponent->GetSkinnedAsset()) : nullptr;
	const USkeleton* MeshSkeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;
	if (!MeshSkeleton || MeshSkeleton != Sequence->GetSkeleton())
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: tail-mood clip '%s' SKIPPED -- skeleton mismatch (mesh=%s clip=%s)."),
			*GetName(), *ClipSuffix,
			MeshSkeleton ? *MeshSkeleton->GetName() : TEXT("<none>"),
			Sequence->GetSkeleton() ? *Sequence->GetSkeleton()->GetName() : TEXT("<none>"));
		return false;
	}

	TailMoodAnim = Sequence;
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: tail-mood clip resolved: %s (length=%.2fs additiveType=%d; 0 = NOT additive)"),
		*GetName(), *Sequence->GetName(), Sequence->GetPlayLength(),
		static_cast<int32>(Sequence->GetAdditiveAnimType()));
	return true;
}

bool APFASicklejaw::PlayOneShotClip(UAnimSequence* Sequence, const TCHAR* Label, bool bHoldFinalFrame, int32 BaseState)
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	const UWorld* World = GetWorld();
	if (!MeshComponent || !Sequence || !World)
	{
		return false;
	}

	// bLooping=false: the player runs the clip once and HOLDS its final frame,
	// which is what "freeze and never blend back" needs (sleep). Both drivers
	// do that -- the single-node player by design, and the sequence-player node
	// because a non-looping asset player CLAMPS its accumulated time to the
	// clip length rather than wrapping (FAnimNode_SequencePlayerBase::
	// UpdateAssetPlayer). Same observable behaviour, two different mechanisms,
	// so it is on the in-game checklist rather than assumed.
	//
	// w31-1: was MeshComponent->PlayAnimation(Sequence, false). Routed through
	// the funnel so one-shots -- comfort breaks, threat displays, attack
	// strikes, dev clip previews -- keep working on BOTH drivers. Had this call
	// been left direct, pfa.UseAnimInstance=1 would have torn the AnimInstance
	// down on the first idle break (PlayAnimation forces single-node mode), and
	// the A/B would have silently compared the old path against itself.
	ApplyChosenPose(Sequence, false);
	ActiveOneShotAnim = Sequence;
	OneShotBaseState = BaseState;
	bOneShotHoldsFinalFrame = bHoldFinalFrame;
	OneShotEndWorldSeconds = World->GetTimeSeconds() + Sequence->GetPlayLength() + OneShotTailSlackSeconds;
	// Whatever loop was playing is gone; force the looping selection to re-apply
	// from scratch once this releases.
	LocomotionState = -1;

	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: one-shot -> %s (clip=%s length=%.2fs hold=%s baseState=%d)"),
		*GetName(), Label, *Sequence->GetName(), Sequence->GetPlayLength(),
		bHoldFinalFrame ? TEXT("yes") : TEXT("no"), BaseState);
	return true;
}

void APFASicklejaw::ClearOneShot(const TCHAR* Reason)
{
	if (PFADiag::IsEnabled() && ActiveOneShotAnim)
	{
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s: one-shot %s released (%s)"),
			*GetName(), *ActiveOneShotAnim->GetName(), Reason);
	}
	ActiveOneShotAnim = nullptr;
	OneShotEndWorldSeconds = 0.0f;
	bOneShotHoldsFinalFrame = false;
	OneShotBaseState = -1;
	// Force the looping selection to re-apply -- otherwise the cached
	// LocomotionState still names the loop that the one-shot displaced and
	// nothing would ever restart it (the creature would hold the final pose
	// forever, which is exactly the bug this line exists to prevent).
	LocomotionState = -1;
}

bool APFASicklejaw::UpdateOneShotLayer(int32 DesiredState)
{
	const bool bActive = (ActiveOneShotAnim != nullptr);
	if (!bActive)
	{
		return false;
	}

	// NEGATIVE BaseStates are SENTINELS and are immune to this comparison; they
	// expire only on their own clock. Real looping states are always 0..5.
	//
	// Written as `>= 0` rather than as a list of sentinels because the list
	// version already failed once: w30-1 added AttackOneShotBaseState (-3) and
	// left this test special-casing only DevOneShotBaseState (-2), so
	// `DesiredState != -3` was trivially true on every tick and every attack
	// strike was cleared within 0.2s -- the bite animation was killed at ~10%
	// of its length while the damage still landed at 0.792s. Exactly the
	// "damage with no visible cause" bug that packet existed to fix, reintroduced
	// by the fix. A third sentinel would have broken it again.
	if (OneShotBaseState >= 0 && DesiredState != OneShotBaseState)
	{
		ClearOneShot(TEXT("situation changed"));
		return false;
	}

	if (bOneShotHoldsFinalFrame)
	{
		return true;   // holds until the situation changes or a dev clears it
	}

	// FAIL CLOSED on a null world (review W2). The obvious spelling of this --
	// `World ? GetTimeSeconds() : 0.0f` -- pins now at 0.0, and 0.0 is forever
	// less than a deadline set at a positive time, so a null world would mean
	// "still playing" PERMANENTLY: the creature holds one pose for the rest of
	// its life. Every other null-world path in this file returns false; this one
	// silently did the opposite.
	const UWorld* World = GetWorld();
	if (!World)
	{
		ClearOneShot(TEXT("no world -- releasing rather than holding the pose forever"));
		return false;
	}
	if (World->GetTimeSeconds() < OneShotEndWorldSeconds)
	{
		return true;
	}

	ClearOneShot(TEXT("finished"));
	return false;
}

void APFASicklejaw::ScheduleNextIdleBreak(float NowSeconds)
{
	const float Mean = FMath::Max(1.0f, CVarPFAIdleBreakMeanSeconds.GetValueOnGameThread());
	// Jitter, not a fixed period: two creatures standing together must not
	// preen in lockstep, which reads as machinery instantly.
	NextIdleBreakWorldSeconds = NowSeconds + Mean * FMath::FRandRange(0.55f, 1.6f);

	// w46-1: make the SCHEDULE observable, not just its outcome. "Only five breaks
	// all session" has three completely different causes -- clips absent (already
	// covered by the always-on resolve line), schedule never armed, or armed and
	// repeatedly suppressed -- and until now the log could not tell them apart.
	// Printing the deadline is what makes the count auditable against elapsed time.
	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: next idle break due in %.1fs (at t=%.1f; mean=%.1fs jittered 0.55-1.6x)"),
			*GetName(), NextIdleBreakWorldSeconds - NowSeconds, NextIdleBreakWorldSeconds, Mean);
	}
}

bool APFASicklejaw::MaybeStartOneShot(int32 DesiredState)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const float NowSeconds = World->GetTimeSeconds();

	// ---- alert (state 3): ONE ritualised threat display per episode. -------
	// Research: a display is a truncated attack -- the creature commits to the
	// shape of a strike and stops short. It is the beat that tells the player
	// "you have been seen and this is your warning", so it fires once on
	// noticing and then the alert idle carries the tracking.
	if (DesiredState == 3)
	{
		if (!bThreatenedThisEpisode && ThreatenAnim && CVarPFAThreatenDisplay.GetValueOnGameThread() != 0)
		{
			bThreatenedThisEpisode = true;
			return PlayOneShotClip(ThreatenAnim, TEXT("threaten"), false, 3);
		}
		return false;
	}

	// w38-2 BUG FIX -- this is a direct cause of the user's "they all seem to do
	// the same thing... over and over".
	//
	// The intent was "re-arm when the creature stops being alert". What it
	// actually did was re-arm on ANY DesiredState != 3 -- and DesiredState is a
	// GAIT-and-intent composite, so simply WALKING (state 1) while alert cleared
	// the flag. An investigating creature that walks, stops, walks, stops
	// replayed the whole 1.6s threat display on every single stop. The display
	// meant to fire once per encounter fired continuously.
	//
	// The flag has to key off the creature's actual ALERTNESS, not off a value
	// that also encodes how fast it happens to be moving. Re-arm only when it
	// has genuinely disengaged -- back to a calm idle or to roaming.
	// Lesson for the state machine generally: DesiredState conflates gait with
	// intent, so any "have I done X this episode" flag hung off it will re-arm
	// on movement. See ASSET-COORDINATION's note about checks that cannot fail.
	if (DesiredState == 0)
	{
		if (bThreatenedThisEpisode && PFADiag::IsEnabled())
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: threat display re-armed (creature returned to a calm idle)."), *GetName());
		}
		bThreatenedThisEpisode = false;
	}

	// ---- calm idle (state 0) only: comfort and vigilance breaks. -----------
	// Deliberately NOT while alert or fighting: a creature that grooms itself
	// while being hunted reads as broken, and the research is explicit that
	// comfort behaviour is what an animal does when it feels unthreatened.
	const bool bIdleBreaksEnabled = (CVarPFAIdleBreaks.GetValueOnGameThread() != 0);
	if (DesiredState != 0 || !bIdleBreaksEnabled)
	{
		// w46-1: a break that came DUE while the creature was busy is not lost -- it
		// fires the moment the creature settles, because the deadline is absolute
		// world time. But it IS the most likely reason the user sees so few, and
		// "scheduled and suppressed" was previously indistinguishable in the log from
		// "never scheduled". Named explicitly so the fix, if one is needed, is aimed
		// at the right thing: more calm TIME (roam/wait tuning), not a shorter mean.
		// Once per suppression episode -- this sits on a per-tick path and the w18-2
		// lesson (400 log lines/s) is not being relearned.
		if (bIdleBreaksEnabled && NextIdleBreakWorldSeconds > 0.0f &&
			NowSeconds >= NextIdleBreakWorldSeconds && !bLoggedIdleBreakSuppressed)
		{
			bLoggedIdleBreakSuppressed = true;
			if (PFADiag::IsEnabled())
			{
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] %s: idle break DUE %.1fs ago but SUPPRESSED -- the creature is not calm (DesiredState=%d; 0=calm idle, 1=walk, 2=run, 3=alert, 4=aggressive, 5=prowl). It will play as soon as it settles. The gate is deliberate: grooming while hunted reads as broken."),
					*GetName(), NowSeconds - NextIdleBreakWorldSeconds, DesiredState);
			}
		}
		return false;
	}
	// Calm again: re-arm the suppression report so the NEXT busy episode is also
	// visible (a flag latched for the creature's life would report once and then
	// hide a creature that is busy 95% of the time -- exactly the case at issue).
	bLoggedIdleBreakSuppressed = false;

	if (NextIdleBreakWorldSeconds <= 0.0f)
	{
		// First calm tick: arm the schedule but don't fire, so a creature does
		// not preen the instant it spawns.
		ScheduleNextIdleBreak(NowSeconds);
		return false;
	}
	if (NowSeconds < NextIdleBreakWorldSeconds)
	{
		return false;
	}

	// Weighted pick over the clips that actually resolved -- an absent clip
	// contributes no weight rather than producing a silent do-nothing tick.
	float TotalWeight = 0.0f;
	const int32 Count = FMath::Min(IdleBreakClips.Num(), (int32)UE_ARRAY_COUNT(SicklejawIdleBreaks));
	for (int32 i = 0; i < Count; ++i)
	{
		if (IdleBreakClips[i])
		{
			TotalWeight += SicklejawIdleBreaks[i].Weight;
		}
	}
	if (TotalWeight <= 0.0f)
	{
		ScheduleNextIdleBreak(NowSeconds);
		return false;
	}

	float Roll = FMath::FRandRange(0.0f, TotalWeight);
	for (int32 i = 0; i < Count; ++i)
	{
		if (!IdleBreakClips[i])
		{
			continue;
		}
		Roll -= SicklejawIdleBreaks[i].Weight;
		if (Roll <= 0.0f)
		{
			ScheduleNextIdleBreak(NowSeconds);
			return PlayOneShotClip(IdleBreakClips[i], SicklejawIdleBreaks[i].ClipSuffix, false, 0);
		}
	}

	ScheduleNextIdleBreak(NowSeconds);
	return false;
}

UAnimSequence* APFASicklejaw::ResolveWoundedGait(int32 DesiredState) const
{
	// Walk (1) and prowl (5) only. The Limp clip is authored at an implied
	// 180 cm/s, so using it for the run band would slide the feet badly -- and
	// a wounded animal fleeing at full speed is not limping anyway.
	if (!LimpAnim || (DesiredState != 1 && DesiredState != 5))
	{
		return nullptr;
	}
	const UFGHealthComponent* Health = GetHealthComponent();
	if (!Health)
	{
		return nullptr;
	}
	const float MaxHealth = Health->GetMaxHealth();
	if (MaxHealth <= 0.0f)
	{
		return nullptr;
	}
	const float Fraction = Health->GetCurrentHealth() / MaxHealth;
	return (Fraction <= CVarPFALimpHealthFraction.GetValueOnGameThread()) ? LimpAnim.Get() : nullptr;
}

// ---------------------------------------------------------------------------
// PFA.PlayAnim -- the in-game viewer for the whole authored set.
//
// 36 clips are imported; the gameplay state machine reaches maybe ten of them.
// Without this the rest can only ever be judged from Blender renders, and the
// two defects that actually reached the user (a 1.65cm mesh, a creature sunk
// into terrain) were both things that looked fine in source and wrong in game.
// This is a dev command, not gameplay: it mutates live actors' poses, so it is
// explicit-intent only, exactly like PFA.SpawnNest.
// ---------------------------------------------------------------------------
bool APFASicklejaw::DevPlayClipByName(const FString& ClipName, bool bHoldFinalFrame)
{
	// Validate at the boundary (review W6): a console string is untrusted input,
	// and it is concatenated straight into a package path below. Path separators
	// and traversal would aim LoadPackage somewhere we never meant to look. This
	// stays inside UE's virtual package space either way, but a command is a
	// system boundary and gets checked like one.
	if (ClipName.IsEmpty() || ClipName.Contains(TEXT("/")) || ClipName.Contains(TEXT("\\")) || ClipName.Contains(TEXT("..")))
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: PFA.PlayAnim rejected clip name '%s' -- expected a bare clip suffix like ClawPin or IdleBreak_Preen, no paths."),
			*GetName(), *ClipName);
		return false;
	}

	UAnimSequence* Sequence = ResolveClip(*ClipName, GetName());
	if (!Sequence)
	{
		return false;
	}

	// Skeleton guard (review W3). StartLocomotionAnim only arms the driver when
	// the REAL mesh loaded, because these clips target SK_Sicklejaw_Skeleton and
	// playing them on the placeholder's skeleton is undefined -- w24 paid for
	// that lesson. The dev command bypassed that check entirely and could hand a
	// mismatched sequence straight to PlayAnimation on a creature that lost the
	// real-mesh race or fell back after a broken import.
	const USkeletalMeshComponent* MeshComponent = GetMesh();
	const USkeletalMesh* SkeletalMesh = MeshComponent ? Cast<USkeletalMesh>(MeshComponent->GetSkinnedAsset()) : nullptr;
	const USkeleton* MeshSkeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;
	const USkeleton* ClipSkeleton = Sequence->GetSkeleton();
	if (!MeshSkeleton || !ClipSkeleton || MeshSkeleton != ClipSkeleton)
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: PFA.PlayAnim '%s' SKIPPED -- skeleton mismatch (mesh skeleton=%s, clip skeleton=%s). ")
			TEXT("This creature is not on the real Sicklejaw mesh, so the clip would be played against the wrong bone hierarchy."),
			*GetName(), *ClipName,
			MeshSkeleton ? *MeshSkeleton->GetName() : TEXT("<none>"),
			ClipSkeleton ? *ClipSkeleton->GetName() : TEXT("<none>"));
		return false;
	}

	// Review I2: the one-shot layer is driven BY the locomotion timer, so if that
	// timer never armed (no gait clip resolved on this creature) nothing will
	// ever notice this clip expiring and it holds its last frame forever. Still
	// play it -- seeing the clip is the point -- but say so, because "it froze"
	// would otherwise look like a bug in the layer rather than a missing driver.
	if (!GetWorldTimerManager().IsTimerActive(LocomotionTimerHandle))
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: PFA.PlayAnim '%s' -- locomotion driver is NOT armed on this creature, so nothing will release the pose when the clip ends. It will hold its final frame; run PFA.PlayAnim with no args to clear."),
			*GetName(), *ClipName);
	}

	return PlayOneShotClip(Sequence, *ClipName, bHoldFinalFrame, DevOneShotBaseState);
}

void APFASicklejaw::DevClearClipOverride()
{
	if (OneShotBaseState == DevOneShotBaseState)
	{
		ClearOneShot(TEXT("dev override cleared"));
	}
}

bool APFASicklejaw::PlayAttackOneShot(const FString& ClipName)
{
	UAnimSequence* Sequence = ResolveClip(*ClipName, GetName());
	if (!Sequence)
	{
		// Loud, not diag-gated: an attack with no visible strike is the exact
		// thing this method exists to prevent, and silence here would look
		// identical to the bug it fixes.
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: attack clip '%s' did not resolve -- the bite will deal damage with NO strike animation."),
			*GetName(), *ClipName);
		return false;
	}

	// Same skeleton guard as the dev path (w29-2 review W3): a creature that
	// fell back to the placeholder mesh must not be handed Sicklejaw clips.
	const USkeletalMeshComponent* MeshComponent = GetMesh();
	const USkeletalMesh* SkeletalMesh = MeshComponent ? Cast<USkeletalMesh>(MeshComponent->GetSkinnedAsset()) : nullptr;
	const USkeleton* MeshSkeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;
	if (!MeshSkeleton || MeshSkeleton != Sequence->GetSkeleton())
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] %s: attack clip '%s' SKIPPED -- skeleton mismatch; damage still applies, the strike just isn't shown."),
			*GetName(), *ClipName);
		return false;
	}

	return PlayOneShotClip(Sequence, *ClipName, /*bHoldFinalFrame*/ false, AttackOneShotBaseState);
}

void APFASicklejaw::StopAttackOneShot()
{
	if (OneShotBaseState == AttackOneShotBaseState)
	{
		ClearOneShot(TEXT("attack action cleaned up"));
	}
}

bool APFASicklejaw::IsPlayingAttackOneShot() const
{
	return ActiveOneShotAnim != nullptr && OneShotBaseState == AttackOneShotBaseState;
}

namespace
{
	// Signature order is (Args, World) -- FConsoleCommandWithWorldAndArgsDelegate
	// passes the arg array first, which reads backwards from the class name.
	void PlayAnimCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] PFA.PlayAnim aborted: no world context."));
			return;
		}

		// No args = release any dev override and hand the pose back to gameplay.
		if (Args.Num() == 0)
		{
			int32 Cleared = 0;
			for (TActorIterator<APFASicklejaw> It(World); It; ++It)
			{
				It->DevClearClipOverride();
				++Cleared;
			}
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] PFA.PlayAnim: released dev clip override on %d Sicklejaw(s). Usage: PFA.PlayAnim <clip> [hold 0|1] [radiusCm]"),
				Cleared);
			return;
		}

		const FString ClipName = Args[0];
		const bool bHold = (Args.Num() > 1) && (Args[1] == TEXT("1"));
		const float RadiusCm = (Args.Num() > 2) ? FCString::Atof(*Args[2]) : 20000.0f;

		// Radius is measured from the local player pawn when there is one;
		// headless/unattended runs (no pawn) fall back to every creature.
		FVector Origin = FVector::ZeroVector;
		bool bHaveOrigin = false;
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				Origin = Pawn->GetActorLocation();
				bHaveOrigin = true;
			}
		}

		int32 Considered = 0;
		int32 Played = 0;
		for (TActorIterator<APFASicklejaw> It(World); It; ++It)
		{
			APFASicklejaw* Creature = *It;
			if (bHaveOrigin && FVector::Dist(Creature->GetActorLocation(), Origin) > RadiusCm)
			{
				continue;
			}
			++Considered;
			if (Creature->DevPlayClipByName(ClipName, bHold))
			{
				++Played;
			}
		}

		if (Considered == 0)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] PFA.PlayAnim '%s': no Sicklejaw within %.0fcm (spawn one with PFA.SpawnTest / PFA.SpawnNest)."),
				*ClipName, RadiusCm);
			return;
		}
		if (Played == 0)
		{
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] PFA.PlayAnim '%s': clip did NOT resolve on any of %d Sicklejaw(s) -- expected package %s%s. Enable pfa.Diag 1 for the probe log."),
				*ClipName, Considered, AnimPackagePrefix, *ClipName);
			return;
		}
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] PFA.PlayAnim '%s': playing on %d/%d Sicklejaw(s) (hold=%s radius=%.0fcm)."),
			*ClipName, Played, Considered, bHold ? TEXT("yes") : TEXT("no"), RadiusCm);
	}

	FAutoConsoleCommandWithWorldAndArgs CCmdPFAPlayAnim(
		TEXT("PFA.PlayAnim"),
		TEXT("PFA.PlayAnim <clip> [hold 0|1] [radiusCm] -- play any imported Sicklejaw clip by name (e.g. ClawPin, Sleep_SWS, IdleBreak_Preen, Threaten, TurnInPlaceLeft). No args releases the override."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlayAnimCommand));
}
