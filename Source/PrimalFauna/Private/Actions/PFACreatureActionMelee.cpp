#include "Actions/PFACreatureActionMelee.h"
#include "PrimalFauna.h"
#include "PFACheats.h"
#include "Species/PFASicklejaw.h"

#include "Creature/FGCreature.h"          // FCreatureAction, ECreatureState, GetCreatureActionsSetup
#include "Creature/FGCreatureAction.h"    // FCreatureActionTargetParams, ECreatureActionTargetSelectionType
#include "DamageTypes/FGDamageType.h"     // UFGDamageType::mDamageAmount (public)
#include "FGCharacterBase.h"              // ProcessHit param GetName()
#include "GameFramework/Pawn.h"           // GetPawn()->GetName()

namespace
{
	// ---- TODO(W1.2-census): every value is a PLACEHOLDER anchor. doc 01 §1
	// frames bite damage as "~ Alpha Hog charge"; docs/CENSUS.md only captured
	// passive fauna, so the real Alpha Hog charge damage is unknown. Nothing
	// here is balance-final. ----
	constexpr float SicklejawBiteDamage = 20.0f;       // doc 01 §1: "~ Alpha Hog charge"
	constexpr float SicklejawAttackRangeCm = 250.0f;   // reach of the hit sweep
	constexpr float SicklejawMinRangeCm = 150.0f;      // must close to within this to start (<= AttackRange)
	constexpr float SicklejawAttackAngleDeg = 90.0f;   // forward cone the target must be within
	constexpr float SicklejawKnockbackAmount = 500.0f; // light shove on hit

	// Flat score forced when native scoring returns <= 0 (see ScoreAction).
	constexpr float MeleeFallbackScore = 1.0f;
}

UPFACreatureActionMelee::UPFACreatureActionMelee(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// --- Melee tuning: protected fields on UFGCreatureActionMeleeAttack, set
	// here via protected inheritance (docs/07 §1 -- no Friend grant, no
	// reflection). ---
	mAttackRange = SicklejawAttackRangeCm;
	mMinRange = SicklejawMinRangeCm;
	mAttackAngle = SicklejawAttackAngleDeg;
	mKnockbackAmount = SicklejawKnockbackAmount;

	// Damage: an instanced UFGDamageType whose mDamageAmount (public,
	// FGDamageType.h:47) is the base bite damage. mDamageTypes is protected +
	// Instanced (FGCreatureActionMeleeAttack.h:95); this is our subclass so we
	// append directly. [assumption -> in-game test]: the melee action passes
	// mDamageTypes[i]->mDamageAmount as the applied damageAmount
	// (UFGDamageType::ProcessDamage takes damageAmount as a parameter -- the
	// wiring is binary-side, so confirm the creature deals ~this much in-game).
	UFGDamageType* BiteDamage = CreateDefaultSubobject<UFGDamageType>(TEXT("SicklejawBiteDamage"));
	BiteDamage->mDamageAmount = SicklejawBiteDamage;
	mDamageTypes.Add(BiteDamage);

	// Target selection = HighestAggro (docs/07 §5 offers Closest / HighestAggro).
	// Justification: a pack predator commits to the thing it is hunting -- the
	// player it has aggroed -- not merely the nearest actor, which under
	// creature-vs-creature aggro (doc 02) could be a passing packmate. Visibility
	// requirement left at TVR_None for bring-up: fewer gates => the action is
	// more likely to fire, so montage-less behavior is observable; tighten in W2
	// once the core loop is proven. (mTargetRequirements is protected on
	// UFGCreatureAction, FGCreatureAction.h:123 -- inherited, set directly.)
	mTargetRequirements.TargetType = ECreatureActionTargetSelectionType::HighestAggro;

	// mActionMontage (FGCreatureActionAnimated.h:31) LEFT NULL on purpose --
	// montage-less bring-up (docs/08 §4 assumption item 2). No animation field is
	// touched; their defaults + the null montage are exactly what the test probes.
}

float UPFACreatureActionMelee::GetConfiguredBiteDamage() const
{
	float Sum = 0.0f;
	for (const TObjectPtr<UFGDamageType>& DamageType : mDamageTypes)
	{
		if (DamageType)
		{
			Sum += DamageType->mDamageAmount;
		}
	}
	return Sum;
}

// ===========================================================================
// Staged instrumentation. Each override logs behind PFADiag::IsEnabled() then
// calls Super (pure wrap), EXCEPT ScoreAction which also forces a positive
// score -- the one behavioral change, flagged for the cold reviewer.
// ===========================================================================

bool UPFACreatureActionMelee::ScoreAction_Implementation(float& score) const
{
	const bool bBaseConsidered = Super::ScoreAction_Implementation(score);
	const float BaseScore = score;

	// BEHAVIORAL CHOICE (only non-logging change in this class): guarantee the
	// action is considered with a flat positive score. Montage-less bring-up
	// authors NO UFGActionScorer (mActionScorers empty), and
	// UFGAction::ScoreAction is documented "Processes and returns the sum of all
	// the action scorers. Must return true if the action is to be considered at
	// all." With zero scorers a generic borrowed BT could score us 0 and never
	// select the bite, making the Phase-1 loop unobservable. Sicklejaw has
	// exactly ONE action in W1.4, so a flat score crowds nothing out;
	// CanPerformAction still gates on range/angle. We log the base's own verdict
	// so the SAME test reveals whether forcing was even necessary. TODO: replace
	// with a real UFGActionScorer once a hostile-creature config is dumped
	// (W1.2), and revisit when a 2nd action (leap, W2) makes scores comparable.
	// Single-action self-disabling guard (W1.4 top-tier review): force ONLY
	// while this creature has <= 1 action. The moment a 2nd action lands (leap,
	// W2), forcing self-disables and native score comparability is restored --
	// authoring a real UFGActionScorer becomes mandatory by construction, not
	// by memory. mCreature (FGCreatureAction.h:111, protected, inherited) is
	// populated by InitializeAction before scoring; the !IsValid() fallback
	// keeps early bring-up safe.
	const bool bForce = (score <= 0.0f) &&
		(!mCreature.IsValid() || mCreature->GetCreatureActionsSetup().Num() <= 1);
	if (bForce)
	{
		score = MeleeFallbackScore;
	}

	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Sicklejaw action=Melee ScoreAction baseConsidered=%s baseScore=%.2f forced=%s -> score=%.2f target=%s"),
			bBaseConsidered ? TEXT("true") : TEXT("false"), BaseScore,
			bForce ? TEXT("true") : TEXT("false"), score,
			GetCurrentTarget() ? *GetCurrentTarget()->GetName() : TEXT("none"));
	}
	// Honest consideration: claim "consider me" only when the base did, or the
	// single-action force is active -- never unconditionally (review W1).
	return bBaseConsidered || bForce;
}

bool UPFACreatureActionMelee::CanPerformAction_Implementation() const
{
	const bool bCan = Super::CanPerformAction_Implementation();
	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Sicklejaw action=Melee CanPerformAction=%s target=%s pawn=%s"),
			bCan ? TEXT("true") : TEXT("false"),
			GetCurrentTarget() ? *GetCurrentTarget()->GetName() : TEXT("none"),
			GetPawn() ? *GetPawn()->GetName() : TEXT("none"));
	}
	return bCan;
}

void UPFACreatureActionMelee::PerformAction_Implementation()
{
	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Sicklejaw action=Melee PerformAction START (montage-less: mActionMontage null; a hit depends on the anim-notify path -- watch for a following ProcessHit line) target=%s"),
			GetCurrentTarget() ? *GetCurrentTarget()->GetName() : TEXT("none"));
	}
	Super::PerformAction_Implementation();
}

void UPFACreatureActionMelee::CleanupAction_Implementation(bool actionSuccess)
{
	// W1.4 review W3: the montage-less hypothesis is precisely "does the action
	// COMPLETE or hang?" (invariant 6). Logging the outcome makes IDLE-vs-hung
	// vs finished-no-hit distinguishable from one log read.
	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Sicklejaw action=Melee CleanupAction success=%s -- action completed, not hung"),
			actionSuccess ? TEXT("true") : TEXT("false"));
	}
	Super::CleanupAction_Implementation(actionSuccess);
}

void UPFACreatureActionMelee::ProcessHit_Implementation(AFGCharacterBase* HitCharacter, const FHitResult& HitResult)
{
	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Sicklejaw action=Melee ProcessHit hit=%s -- montage-less bite CONNECTED (damage applies via Super, dmg=%.1f)"),
			HitCharacter ? *HitCharacter->GetName() : TEXT("none"), GetConfiguredBiteDamage());
	}
	Super::ProcessHit_Implementation(HitCharacter, HitResult);
}

// ---- Self-test: MeleeActionConfigured (idiom from PFACheats.cpp). Reads back
// what the Sicklejaw ctor built on the CDO -- STATIC config only. Whether the
// montage-less bite actually LANDS damage in-game is a separate runtime item
// (assumption 2), which the ProcessHit instrumentation above surfaces. ----
namespace
{
	const bool bRegistered_MeleeActionConfigured = []
	{
		RegisterPFASelfTest({TEXT("MeleeActionConfigured"),
			[](UWorld* /*World*/, FString& OutDetail) -> bool
			{
				const APFASicklejaw* Cdo = GetDefault<APFASicklejaw>();
				if (!Cdo)
				{
					OutDetail = TEXT("GetDefault<APFASicklejaw>() returned null");
					return false;
				}

				const TArray<FCreatureAction> Actions = Cdo->GetCreatureActionsSetup();
				const UPFACreatureActionMelee* Melee = nullptr;
				uint8 RequiredStates = 0;
				for (const FCreatureAction& Entry : Actions)
				{
					if (const UPFACreatureActionMelee* AsMelee = Cast<UPFACreatureActionMelee>(Entry.Action))
					{
						Melee = AsMelee;
						RequiredStates = Entry.RequiredStates;
						break;
					}
				}

				if (!Melee)
				{
					OutDetail = FString::Printf(
						TEXT("no UPFACreatureActionMelee in mCreatureActionsSetup (%d entries) -- Sicklejaw ctor did not append the bite action"),
						Actions.Num());
					return false;
				}

				const float Damage = Melee->GetConfiguredBiteDamage();
				const float Range = Melee->GetConfiguredAttackRange();
				const int32 DamageTypeCount = Melee->GetConfiguredDamageTypeCount();
				const bool bFightGated = (RequiredStates & (uint8)ECreatureState::CS_Fight) != 0;

				OutDetail = FString::Printf(
					TEXT("range=%.1f damage=%.1f damageTypes=%d requiredStates=0x%02X fightGated=%s totalActions=%d"),
					Range, Damage, DamageTypeCount, RequiredStates, bFightGated ? TEXT("true") : TEXT("false"),
					Actions.Num());

				// Bar: the action exists, deals > 0 damage, and is enabled for
				// the Fight state so a Fight-state BT branch can select it.
				return Damage > 0.0f && DamageTypeCount > 0 && bFightGated;
			}});
		return true;
	}();
}
