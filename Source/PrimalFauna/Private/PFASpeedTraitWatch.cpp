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
	// 0.5s. Fast enough that a state-change-driven write is caught within one
	// beat of it happening (state changes are seconds apart), slow enough to be
	// free: one float compare per creature per half second, no allocation.
	constexpr float SpeedTraitWatchIntervalSeconds = 0.5f;

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
	const TCHAR* Verdict =
		bBackToPreTrait ? TEXT("TRAIT DISCARDED (value is exactly the pre-trait number)") :
		(ExactTableMatch != nullptr) ? TEXT("TRAIT DISCARDED (value is an exact mMoveSpeedData entry -> SetMoveSpeed is the writer)") :
		(TraitScaledTableMatch != nullptr) ? TEXT("trait PRESERVED (value is a table entry x the trait)") :
		TEXT("UNEXPLAINED (matches neither the table nor the pre-trait value)");

	const bool bAlwaysOn = (mWalkSpeedChangeCount <= WalkSpeedChangeAlwaysOnLogLimit);
	if (bAlwaysOn || PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] %s: WALK SPEED CHANGED #%d -- %.1f -> %.1f cm/s (traitBaseline=%.1f preTrait=%.1f trait=x%.3f) exactTableMatch=%s traitScaledTableMatch=%s verdict=%s%s"),
			*GetName(), mWalkSpeedChangeCount, Previous, Live,
			mTraitWalkSpeedBaselineCms, mPreTraitWalkSpeedCms, mSpeedTraitMultiplier,
			ExactTableMatch ? ExactTableMatch : TEXT("none"),
			TraitScaledTableMatch ? TraitScaledTableMatch : TEXT("none"),
			Verdict,
			(mWalkSpeedChangeCount == WalkSpeedChangeAlwaysOnLogLimit)
				? TEXT(" [further changes on this creature are pfa.Diag-gated from here]") : TEXT(""));
	}
}
