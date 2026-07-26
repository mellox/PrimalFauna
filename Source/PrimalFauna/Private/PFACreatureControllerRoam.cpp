// ===========================================================================
// PFACreatureControllerRoam.cpp -- APFACreatureController's w46-1 ROAM-LEG
// TELEMETRY. Third TU of the same class (after PFACreatureController.cpp and
// PFACreatureControllerPerception.cpp). Split because this measurement is a
// self-contained concern with its own tuning CVars and its own reason to exist;
// keeping it out of the 1000-line perception TU is the point. (w47-1 correction:
// the original header cited "the 500-line workspace file limit" as the reason,
// which read as a claim that this codebase honours that limit -- three files in
// the same commit already exceed it, PFACreatureControllerPerception.cpp at 1067.
// Splitting by CONCERN is the honest rationale; the line count is a symptom.)
// Declared in PFACreatureController.h, called from Tick.
//
// WHY THIS EXISTS -- a measurement, not a fix.
// The user, on build w45-3: "when it wandered it seem to do for the same length,
// instead of wandering to something it found interesting or having a random of
// where wandering too" and "it wanders to someplace and then stands there for a
// long time rarely doing anything else but standing there."
//
// We publish the roam band to the blackboard as [600..2500]cm (w44-1, verified
// TOOK in the log) and the VANILLA BehaviorTree picks the destination from it.
// That tree is a closed Blueprint asset we borrow, so there is NO static way to
// know whether its roam branch randomises inside the band or simply takes
// MaxRoamingDistance every time. "Same length every time" is exactly what the
// latter looks like from the outside -- but so is "randomised and the player
// watched three long legs in a row". Guessing here has already cost this project
// three commits (the w44-1 phantom regression), so the answer is measured.
//
// One session with `pfa.Diag 1` and a creature roaming now settles it: if the
// `straight=` values across legs cluster at ~2500 the tree ignores the band's
// minimum and the fix is ours to make (narrow the band, or drive the roam
// destination ourselves); if they scatter between 600 and 2500 the band works and
// the user's impression came from the leg TIMING instead -- which the same line
// also reports as `dur=` and `waitBefore=`.
//
// IT ALSO ANSWERS THE PROWL QUESTION FOR FREE. Prowl played 19 times against
// Walk's 23 in that session, and prowl is selected purely by speed band
// (>= LocomotionProwlThresholdCms = 30cm/s, Species/PFASicklejaw.cpp) with no
// gating on aggression at all -- while the recorded design intent is
// "Prowl (Territorial)", a stalking gait. If the logged roam speed sits in the
// prowl band then casual wandering is being animated as stalking, and the log
// says so numerically instead of by eye.
//
// TRACKING IS UNCONDITIONAL; ONLY THE LOGGING IS GATED (see the header comment).
// ===========================================================================

#include "PFACreatureController.h"
#include "PrimalFauna.h"
#include "PFACheats.h"

#include "Creature/FGCreature.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
// Explicit rather than transitive (IWYU discipline, same as the AIModule entry in
// PrimalFauna.Build.cs): the two CVars below are the whole tuning surface of this
// file and must not depend on another header happening to pull the manager in.
#include "HAL/IConsoleManager.h"

namespace
{
	// Same 0.25s cadence TickAggroRangeDecay batches to. Fast enough that a leg's
	// start and end are located within a quarter second, cheap enough to run on
	// every creature every frame without a per-frame allocation (this samples
	// scalars only -- no arrays, no copies).
	constexpr float RoamLegSampleInterval = 0.25f;

	// How many CONSECUTIVE below-threshold samples end a leg. A creature that dips
	// under the threshold for a single sample is steering around geometry, not
	// finished travelling; counting that as a completed leg would split one roam
	// into several short ones and fabricate exactly the "legs are short and
	// variable" answer we are trying to measure. 3 samples = 0.75s.
	constexpr int32 RoamLegStopDebounceSamples = 3;

	// PRINT-ONLY MIRROR of the gait bands. The authoritative table lives in
	// Species/PFASicklejaw.cpp's anonymous namespace (LocomotionProwlThresholdCms
	// 30, LocomotionWalkBandStartCms 200, LocomotionRunThresholdCms 480) and stays
	// there -- these two values are never used to SELECT anything, only to report
	// which band the measured roam speed fell in. If the real bands move and these
	// drift, a diagnostic line becomes misleading and nothing else breaks.
	constexpr float RoamGaitProwlBandStartCms = 30.0f;
	constexpr float RoamGaitWalkBandStartCms = 200.0f;

	// Live knobs, because both are judgement calls about what counts as "moving"
	// and what counts as "standing there for a long time" -- the two things the
	// user's reports are about.
	static TAutoConsoleVariable<float> CVarPFARoamLegMinSpeedCms(
		TEXT("pfa.RoamLegMinSpeedCms"), 20.0f,
		TEXT("Speed (cm/s) at or above which the roam-leg telemetry considers a creature to be travelling. Below it (for 0.75s) the leg is treated as complete."),
		ECVF_Default);

	static TAutoConsoleVariable<float> CVarPFARoamLegWaitReportSeconds(
		TEXT("pfa.RoamLegWaitReportSeconds"), 15.0f,
		TEXT("Report once (pfa.Diag) when a creature has stood still this long since its last roam leg ended -- the numeric form of 'it stands there for a long time'."),
		ECVF_Default);

	// w47-2 (w47-1 cold review W2): ENTRIES THAT ACTUALLY HOLD AGGRO, not entries.
	// `GetAggroTargets().Num() > 0` is NOT a test for interest -- perception
	// registers a merely-SEEN actor at ZERO aggro, which the perception TU states
	// explicitly one file over ("a zero-aggro entry is not interest",
	// PFACreatureControllerPerception.cpp). So w47-1's pursuit test was true for any
	// creature with the player anywhere in its 4,000cm sight radius, which would have
	// stamped kind=PURSUIT on essentially EVERY leg -- and the standing analysis
	// instruction for this telemetry is "discard kind=PURSUIT legs", so the analyst
	// would have discarded exactly the roam samples this packet exists to produce.
	// Plain loop rather than Algo::CountIf: one fewer header, same code.
	int32 CountLiveAggroEntries(const TArray<FFGCreatureAggroData>& AggroTargets)
	{
		int32 LiveEntries = 0;
		for (const FFGCreatureAggroData& AggroData : AggroTargets)
		{
			if (AggroData.Aggro > KINDA_SMALL_NUMBER)
			{
				++LiveEntries;
			}
		}
		return LiveEntries;
	}
}

void APFACreatureController::TickRoamLegTelemetry(float DeltaSeconds)
{
	mRoamLegAccumulator += DeltaSeconds;
	if (mRoamLegAccumulator < RoamLegSampleInterval)
	{
		return;
	}
	mRoamLegAccumulator = 0.0f;

	const APawn* RoamPawn = GetPawn();
	const UWorld* World = GetWorld();
	if (!RoamPawn || !World)
	{
		return;
	}

	const FVector Location = RoamPawn->GetActorLocation();
	const double Now = World->GetTimeSeconds();
	const float SpeedCms = RoamPawn->GetVelocity().Size2D();
	const float MinSpeedCms = FMath::Max(1.0f, CVarPFARoamLegMinSpeedCms.GetValueOnGameThread());
	const bool bMovingNow = (SpeedCms >= MinSpeedCms);

	// The BLACKBOARD band, not the CDO band: the blackboard is what the vanilla
	// tree actually reads, so it is the only band a leg length can be compared
	// against honestly (they have disagreed before -- w38-1 read roamBB=[0..0]
	// against roamCDO=[600..2500]).
	//
	// w47-1 (w46-1 review F7): these are PRINT-ONLY values and they were read at
	// 4Hz per creature even with diagnostics off. Folded into a lambda called from
	// inside the log gates instead -- nothing about the measurement itself depends
	// on them, so nothing about the measurement changes.
	auto ReadRoamBand = [this](float& OutMin, float& OutMax)
	{
		OutMin = -1.0f;
		OutMax = -1.0f;
		if (const UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
		{
			OutMin = BlackboardComp->GetValueAsFloat(FName(TEXT("MinRoamingDistance")));
			OutMax = BlackboardComp->GetValueAsFloat(FName(TEXT("MaxRoamingDistance")));
		}
	};
	float BandMin = -1.0f;
	float BandMax = -1.0f;

	if (!bRoamLegActive)
	{
		if (bMovingNow)
		{
			// ---- a leg begins ------------------------------------------------
			++mRoamLegIndex;
			bRoamLegActive = true;
			mRoamLegStartLocation = Location;
			mRoamLegLastLocation = Location;
			mRoamLegLastMovingLocation = Location;
			mRoamLegStartTimeSeconds = Now;
			mRoamLegLastMovingTimeSeconds = Now;
			mRoamLegPathCm = 0.0f;
			mRoamLegPeakSpeedCms = SpeedCms;
			mRoamLegSamples = 1;
			mRoamLegProwlBandSamples =
				(SpeedCms >= RoamGaitProwlBandStartCms && SpeedCms < RoamGaitWalkBandStartCms) ? 1 : 0;
			mRoamLegStalls = 0;
			mRoamLegBelowThresholdSamples = 0;
			// A leg that has a target at ANY point during it is a pursuit, not a
			// roam, and mixing the two would poison the whole measurement.
			// w47-1 (w46-1 review F5.2): a current target is not the only way a leg
			// fails to be a roam sample. A creature that holds AGGRO but has no
			// current target (the flicker window, w26-3) is engaged, and a leg that
			// runs in any non-Roam state is a reaction -- knockback, flee, the frame
			// after a ResetTargetingState. Both are tracked SEPARATELY from the
			// target flag so the completion line can name WHICH disqualified it,
			// rather than claiming "had a target" about a creature that had none.
			// w47-2 (review W2): LIVE aggro entries only -- see CountLiveAggroEntries.
			bRoamLegHadTarget = (GetCurrentTarget() != nullptr) || (CountLiveAggroEntries(GetAggroTargets()) > 0);
			mRoamLegStateAtStart = GetCurrentCreatureState();
			bRoamLegNonRoamState = (mRoamLegStateAtStart != ECreatureState::CS_Roam);
			// The wait is only knowable at the START of the next leg, so it is
			// stashed here and printed on the completion line.
			mRoamLegWaitBeforeSeconds =
				(mRoamLegEndedTimeSeconds > 0.0) ? (Now - mRoamLegEndedTimeSeconds) : -1.0;
			bRoamWaitReported = false;

			if (PFADiag::IsEnabled())
			{
				ReadRoamBand(BandMin, BandMax);
				// Logged as well as the completion line so a session that ends
				// mid-leg still shows that a leg started and how long the creature
				// had been standing before it did.
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] %s: roam leg #%d START -- speed=%.0fcm/s waitBefore=%.1fs band=[%.0f..%.0f] stateAtStart=%s target=%s"),
					*GetName(), mRoamLegIndex, SpeedCms,
					mRoamLegWaitBeforeSeconds,
					BandMin, BandMax,
					*CreatureStateEnumToString(GetCurrentCreatureState()),
					GetCurrentTarget() ? *GetCurrentTarget()->GetName() : TEXT("none"));
			}
			return;
		}

		// ---- standing still: report a long wait once per wait ----------------
		// This is the numeric answer to "it wanders to someplace and then stands
		// there for a long time". A single line naming moveStatus separates "the
		// tree issued no new move" (Idle) from "it issued one and the creature is
		// not travelling" (Moving with ~0 velocity = a nav/path problem).
		const float WaitReportSeconds = CVarPFARoamLegWaitReportSeconds.GetValueOnGameThread();
		if (!bRoamWaitReported && mRoamLegEndedTimeSeconds > 0.0 && WaitReportSeconds > 0.0f &&
			(Now - mRoamLegEndedTimeSeconds) >= WaitReportSeconds)
		{
			// w47-1 (w46-1 review F2): latch INSIDE the gate. Setting it before the
			// diag check meant enabling pfa.Diag mid-wait permanently lost this line
			// -- the w18-3 mistake, third instance.
			if (PFADiag::IsEnabled())
			{
				bRoamWaitReported = true;
				ReadRoamBand(BandMin, BandMax);
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] %s: roam WAIT -- standing still %.1fs since leg #%d ended (moveStatus=%d 0=Idle 1=Waiting 2=Paused 3=Moving, velocity=%.0fcm/s, state=%s, target=%s, band=[%.0f..%.0f]). Idle here means the tree has issued no new roam move."),
					*GetName(), Now - mRoamLegEndedTimeSeconds, mRoamLegIndex,
					static_cast<int32>(GetMoveStatus()), SpeedCms,
					*CreatureStateEnumToString(GetCurrentCreatureState()),
					GetCurrentTarget() ? *GetCurrentTarget()->GetName() : TEXT("none"),
					BandMin, BandMax);
			}
		}
		return;
	}

	// ---- inside a leg --------------------------------------------------------
	mRoamLegPathCm += FVector::Dist2D(mRoamLegLastLocation, Location);
	mRoamLegLastLocation = Location;
	mRoamLegPeakSpeedCms = FMath::Max(mRoamLegPeakSpeedCms, SpeedCms);
	++mRoamLegSamples;
	if (SpeedCms >= RoamGaitProwlBandStartCms && SpeedCms < RoamGaitWalkBandStartCms)
	{
		++mRoamLegProwlBandSamples;
	}
	// w47-2 (review W2): LIVE aggro entries only, same as the start check.
	const int32 LiveAggroEntries = CountLiveAggroEntries(GetAggroTargets());
	if (GetCurrentTarget() != nullptr || LiveAggroEntries > 0)
	{
		bRoamLegHadTarget = true;
	}
	// w47-1 (review F5.2): a leg that ENTERS a non-Roam state mid-flight is no longer
	// a roam sample either -- the same reason the start check exists.
	if (GetCurrentCreatureState() != ECreatureState::CS_Roam)
	{
		bRoamLegNonRoamState = true;
	}

	if (bMovingNow)
	{
		mRoamLegLastMovingLocation = Location;
		mRoamLegLastMovingTimeSeconds = Now;
		if (mRoamLegBelowThresholdSamples > 0)
		{
			// It dipped and recovered: that was a stall, not the end of a leg.
			++mRoamLegStalls;
			mRoamLegBelowThresholdSamples = 0;
		}
		return;
	}

	++mRoamLegBelowThresholdSamples;
	if (mRoamLegBelowThresholdSamples < RoamLegStopDebounceSamples)
	{
		return;
	}

	// ---- the leg is complete ------------------------------------------------
	// Measured to the LAST MOVING sample, so the debounce window is not counted as
	// part of the leg (it belongs to the wait that follows).
	const float StraightCm = FVector::Dist2D(mRoamLegStartLocation, mRoamLegLastMovingLocation);
	const double DurationSeconds = FMath::Max(mRoamLegLastMovingTimeSeconds - mRoamLegStartTimeSeconds,
		static_cast<double>(RoamLegSampleInterval));
	const float AvgSpeedCms = mRoamLegPathCm / static_cast<float>(DurationSeconds);
	const float WanderRatio = (StraightCm > 1.0f) ? (mRoamLegPathCm / StraightCm) : -1.0f;
	const int32 ProwlBandPercent = (mRoamLegSamples > 0)
		? FMath::RoundToInt(100.0f * static_cast<float>(mRoamLegProwlBandSamples) / static_cast<float>(mRoamLegSamples))
		: 0;
	// w47-1 (w46-1 review F5.1): named avgBand, NOT gaitBand. It is computed from
	// AVERAGE speed while the real gait selector (Species/PFASicklejaw.cpp) runs on
	// INSTANTANEOUS speed every 0.2s anim tick -- so a leg that never played Prowl
	// for a single frame could print `gaitBand=PROWL` and be read as an answer to the
	// Prowl question. `prowlBandSamples=%` is the honest number for that question and
	// is right next to it.
	const TCHAR* AvgBand =
		(AvgSpeedCms < RoamGaitProwlBandStartCms) ? TEXT("below-prowl") :
		(AvgSpeedCms < RoamGaitWalkBandStartCms) ? TEXT("prowl-range") : TEXT("walk-or-run");

	// kind= now names WHICH disqualifier fired (review F5.2), because "had a target"
	// was about to be printed for creatures that had none.
	// w47-2 (review W2): kind= is a DERIVED label, and this packet already changed
	// how one of its inputs is computed once. The DONE line therefore also prints the
	// raw fields it is derived from (hadTarget, aggroEntries, and the two states
	// already at the end of the line), so a later analyst can re-derive or re-classify
	// legs from an old log instead of trusting a label whose definition moved.
	const TCHAR* LegKind =
		(bRoamLegHadTarget && bRoamLegNonRoamState) ? TEXT("ENGAGED (target/aggro AND non-Roam state -- not a roam sample)") :
		bRoamLegHadTarget ? TEXT("PURSUIT (target or aggro held -- not a roam sample)") :
		bRoamLegNonRoamState ? TEXT("REACTION (non-Roam state -- knockback/flee/post-reset, not a roam sample)") :
		TEXT("roam");

	if (PFADiag::IsEnabled())
	{
		ReadRoamBand(BandMin, BandMax);
		// ONE line, everything a leg is: how far it went in a straight line (the
		// number that answers "does the tree randomise inside [600..2500]"), how
		// far it actually walked, how long it took, the speed band the AVERAGE fell
		// in, the share of samples that were in the prowl band (the honest gait
		// number), and how long it had stood still beforehand.
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: roam leg #%d DONE kind=%s hadTarget=%d aggroEntries=%d straight=%.0fcm path=%.0fcm wander=%.2f dur=%.1fs avg=%.0fcm/s peak=%.0fcm/s avgBand=%s prowlBandSamples=%d%% stalls=%d waitBefore=%.1fs band=[%.0f..%.0f] stateAtStart=%s stateNow=%s"),
			*GetName(), mRoamLegIndex, LegKind,
			bRoamLegHadTarget ? 1 : 0, LiveAggroEntries,
			StraightCm, mRoamLegPathCm, WanderRatio, DurationSeconds,
			AvgSpeedCms, mRoamLegPeakSpeedCms, AvgBand, ProwlBandPercent,
			mRoamLegStalls, mRoamLegWaitBeforeSeconds,
			BandMin, BandMax,
			*CreatureStateEnumToString(mRoamLegStateAtStart),
			*CreatureStateEnumToString(GetCurrentCreatureState()));
	}

	bRoamLegActive = false;
	// The wait starts when the creature actually STOPPED, not when the debounce
	// expired -- otherwise every reported wait is 0.75s short.
	mRoamLegEndedTimeSeconds = mRoamLegLastMovingTimeSeconds;
	bRoamWaitReported = false;
}
