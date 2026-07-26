// ===========================================================================
// PFASpeedTraitWatch.cpp -- w47-1. APFACreatureBase's SPEED-TRAIT WATCH: a
// second TU of that class (the multi-TU pattern APFACreatureController already
// uses), so PFACreatureBase.cpp does not grow another 90 lines.
//
// MEASUREMENT ONLY. It never writes MaxWalkSpeed. See PFACreatureBase.h's
// NoteSpeedTraitApplied comment for the full suspicion; the short version:
//
//   * Species/PFASicklejaw.cpp applies a per-individual speed trait ONCE at
//     BeginPlay as `Movement->MaxWalkSpeed *= SpeedTrait` (+/-8%).
//   * AFGCreature::SetMoveSpeed(EMoveSpeed) -- public UFUNCTION(BlueprintCallable),
//     Creature/FGCreature.h:341 -- resets MaxWalkSpeed from the creature's
//     mMoveSpeedData table (:643; we author MS_Walk 400 / MS_Run 600 /
//     MS_Sprint 780). Being a BlueprintCallable UFUNCTION, ANY Blueprint can call
//     it, which includes a Blueprint node inside the borrowed BT_CreatureBehavior
//     and vanilla's own BP creature layer.
//   * Nothing in our code calls it. So if the live value ever leaves the trait
//     product, someone else moved it -- and if the new value is an EXACT table
//     entry, SetMoveSpeed is named as the mechanism by arithmetic.
//
// WHY A LOG AND NOT A FIX (workspace rule: build what was asked; do not fix
// speculatively): the SML-side body is a stub, so static analysis cannot see the
// call. Both plausible fixes have a cost if the premise is wrong --
// re-multiplying MaxWalkSpeed on a timer would fight the native writer, and
// scaling the INSTANCE's mMoveSpeedData at BeginPlay (the better one: it survives
// SetMoveSpeed because the table is what SetMoveSpeed reads) does nothing at all
// if SetMoveSpeed is never called. One session of this log picks between them.
//
// WHAT THE LOG SAYS
//   [PFA] <name>: speed-trait baseline -- ...            (always on, once)
//   [PFA] <name>: WALK SPEED CHANGED #n ...              (always on for the
//                                                         first 5, then gated)
// The change line carries: live value, what it was, the trait baseline, the
// pre-trait value, whether the trait still divides out, and whether the new value
// matches an mMoveSpeedData entry exactly (naming the EMoveSpeed).
// ===========================================================================

#include "PFACreatureBase.h"
#include "PrimalFauna.h"
#include "PFACheats.h"

#include "Creature/FGCreature.h"

#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

namespace
{
	// w47-2 (review I4): the sampler interval now lives ONCE, on the class
	// (APFACreatureBase::SpeedTraitWatchIntervalSeconds), because the SetTimer call in
	// PFACreatureBase.cpp and the baseline line printed here must never be able to
	// disagree about it.

	// Tolerance for "the value moved". Speeds here are hundreds of cm/s and every
	// candidate writer sets a table value or a product of one, so anything above
	// float noise is a real write.
	constexpr float SpeedTraitEpsilonCms = 0.5f;

	// EMoveSpeed -> text WITHOUT touching the UEnum reflection machinery.
	// StaticEnum<EMoveSpeed>() would reach for FactoryGame's generated
	// Z_Construct_UEnum_* symbol -- exactly the shipping-export class of risk
	// docs/07 §1 exists to avoid -- for a diagnostic string. A switch costs
	// nothing and can never 127-kill the module.
	const TCHAR* MoveSpeedToString(EMoveSpeed Speed)
	{
		switch (Speed)
		{
		case EMoveSpeed::MS_Walk:   return TEXT("MS_Walk");
		case EMoveSpeed::MS_Run:    return TEXT("MS_Run");
		case EMoveSpeed::MS_Sprint: return TEXT("MS_Sprint");
		default:                    return TEXT("MS_Undefined");
		}
	}
}

float APFACreatureBase::GetLiveMaxWalkSpeedCms() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	return Movement ? Movement->MaxWalkSpeed : -1.0f;
}

void APFACreatureBase::NoteSpeedTraitApplied(float TraitMultiplier, float PreTraitWalkSpeedCms, float AppliedWalkSpeedCms)
{
	mSpeedTraitMultiplier = TraitMultiplier;
	mPreTraitWalkSpeedCms = PreTraitWalkSpeedCms;
	mTraitWalkSpeedBaselineCms = AppliedWalkSpeedCms;
	mLastObservedWalkSpeedCms = AppliedWalkSpeedCms;
	mWalkSpeedChangeCount = 0;

	// Always on: this is the number every later change line is measured against,
	// and a session log without it cannot answer the question at all.
	FString Table;
	for (const FMoveSpeedPair& Pair : mMoveSpeedData)
	{
		Table += FString::Printf(TEXT("%s=%.0f "), MoveSpeedToString(Pair.MoveSpeedType), Pair.Speed);
	}
	UE_LOG(LogPrimalFauna, Display,
		TEXT("[PFA] %s: speed-trait baseline -- preTrait=%.1f x%.3f -> MaxWalkSpeed=%.1f cm/s. Watching every %.1fs for overwrites (nothing in PrimalFauna writes this again; a jump to an exact table value means AFGCreature::SetMoveSpeed ran). mMoveSpeedData: %s"),
		*GetName(), PreTraitWalkSpeedCms, TraitMultiplier, AppliedWalkSpeedCms,
		SpeedTraitWatchIntervalSeconds,
		Table.IsEmpty() ? TEXT("<empty -- SetMoveSpeed would set 0!>") : *Table);
}

void APFACreatureBase::TickSpeedTraitWatch()
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}
	const float Live = Movement->MaxWalkSpeed;

	// No species registered a trait (or this species applies none). Adopt the
	// first sample so change detection still works; say so once, because the
	// "traitSurvives" verdict below is then unavailable rather than false.
	if (mLastObservedWalkSpeedCms < 0.0f)
	{
		mLastObservedWalkSpeedCms = Live;
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: speed-trait watch armed with NO trait baseline (no NoteSpeedTraitApplied call) -- adopting live MaxWalkSpeed=%.1f cm/s as the reference; changes will be reported but 'traitSurvives' cannot be judged."),
			*GetName(), Live);
		return;
	}

	if (FMath::IsNearlyEqual(Live, mLastObservedWalkSpeedCms, SpeedTraitEpsilonCms))
	{
		return;
	}

	const float Previous = mLastObservedWalkSpeedCms;
	mLastObservedWalkSpeedCms = Live;
	++mWalkSpeedChangeCount;

	// Which mMoveSpeedData entry (if any) the new value IS. An exact match is the
	// signature of SetMoveSpeed; a match after dividing the trait back out would
	// instead mean someone applied the table THROUGH the trait.
	const TCHAR* ExactTableMatch = nullptr;
	const TCHAR* TraitScaledTableMatch = nullptr;
	for (const FMoveSpeedPair& Pair : mMoveSpeedData)
	{
		if (FMath::IsNearlyEqual(Live, Pair.Speed, SpeedTraitEpsilonCms))
		{
			ExactTableMatch = MoveSpeedToString(Pair.MoveSpeedType);
		}
		if (mSpeedTraitMultiplier > 0.0f &&
			FMath::IsNearlyEqual(Live, Pair.Speed * mSpeedTraitMultiplier, SpeedTraitEpsilonCms))
		{
			TraitScaledTableMatch = MoveSpeedToString(Pair.MoveSpeedType);
		}
	}

	const bool bBackToPreTrait =
		(mPreTraitWalkSpeedCms > 0.0f) && FMath::IsNearlyEqual(Live, mPreTraitWalkSpeedCms, SpeedTraitEpsilonCms);

	// The verdict, stated in the log so nobody has to re-derive it from numbers
	// months from now.
	//
	// w47-2 (w47-1 cold review W3): THE LADDER ORDER WAS HIDING THE CONCLUSION THIS
	// FILE EXISTS TO REACH. UCharacterMovementComponent::MaxWalkSpeed defaults to
	// 600, which is EXACTLY our MS_Run (Species/PFASicklejaw.cpp), so in the likeliest
	// case bBackToPreTrait and ExactTableMatch are BOTH true -- and testing
	// bBackToPreTrait first printed "value is exactly the pre-trait number",
	// suppressing the "SetMoveSpeed is the writer" attribution that is the entire
	// point of the measurement. The table match is now tested first and names the
	// pre-trait coincidence in its own text instead of being displaced by it.
	//
	// Also w47-2: when the rolled trait lands within ~0.13% of 1.0 (about 1.5% of
	// creatures at FRandRange(0.92,1.08) with a 0.5cm/s epsilon), the raw entry and
	// the entry x trait are the same number and BOTH matches fire. w47-1 silently
	// preferred "discarded" -- true by luck, stated with unearned confidence. Say so
	// instead. Deliberately NOT fixed by tightening the epsilon: a tighter epsilon
	// risks missing a REAL match through float drift in the native write, which
	// would cost a whole verdict rather than a caveat.
	FString Verdict;
	if (ExactTableMatch != nullptr && TraitScaledTableMatch != nullptr)
	{
		Verdict = FString::Printf(
			TEXT("AMBIGUOUS (this individual's trait is within epsilon of 1.0, so %s matches the raw mMoveSpeedData entry AND the entry x trait -- discarded and preserved are indistinguishable on THIS creature; read another one's line)"),
			ExactTableMatch);
	}
	else if (ExactTableMatch != nullptr)
	{
		Verdict = bBackToPreTrait
			? FString::Printf(TEXT("TRAIT DISCARDED (exact mMoveSpeedData entry %s -- which is ALSO the pre-trait value; SetMoveSpeed is the likely writer)"), ExactTableMatch)
			: FString::Printf(TEXT("TRAIT DISCARDED (exact mMoveSpeedData entry %s -> SetMoveSpeed is the writer)"), ExactTableMatch);
	}
	else if (TraitScaledTableMatch != nullptr)
	{
		Verdict = FString::Printf(TEXT("trait PRESERVED (value is mMoveSpeedData entry %s x the trait)"), TraitScaledTableMatch);
	}
	else if (bBackToPreTrait)
	{
		Verdict = TEXT("TRAIT DISCARDED (value is exactly the pre-trait number and matches no table entry -- someone restored the pre-trait speed without going through mMoveSpeedData)");
	}
	else
	{
		Verdict = TEXT("UNEXPLAINED (matches neither the table nor the pre-trait value)");
	}

	const bool bAlwaysOn = (mWalkSpeedChangeCount <= WalkSpeedChangeAlwaysOnLogLimit);
	if (bAlwaysOn || PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: WALK SPEED CHANGED #%d -- %.1f -> %.1f cm/s (traitBaseline=%.1f preTrait=%.1f trait=x%.3f) exactTableMatch=%s traitScaledTableMatch=%s verdict=%s%s"),
			*GetName(), mWalkSpeedChangeCount, Previous, Live,
			mTraitWalkSpeedBaselineCms, mPreTraitWalkSpeedCms, mSpeedTraitMultiplier,
			ExactTableMatch ? ExactTableMatch : TEXT("none"),
			TraitScaledTableMatch ? TraitScaledTableMatch : TEXT("none"),
			*Verdict,
			(mWalkSpeedChangeCount == WalkSpeedChangeAlwaysOnLogLimit)
				? TEXT(" [further changes on this creature are pfa.Diag-gated from here]") : TEXT(""));
	}
}
