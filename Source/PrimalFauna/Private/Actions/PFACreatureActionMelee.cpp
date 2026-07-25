#include "Actions/PFACreatureActionMelee.h"
#include "PrimalFauna.h"
#include "PFACheats.h"
#include "Species/PFASicklejaw.h"

#include "Creature/FGCreature.h"          // FCreatureAction, ECreatureState, GetCreatureActionsSetup
#include "Creature/FGCreatureAction.h"    // FCreatureActionTargetParams, ECreatureActionTargetSelectionType
#include "DamageTypes/FGDamageType.h"     // UFGDamageType::mDamageAmount (public)
#include "FGCharacterBase.h"              // ProcessHit param GetName()
#include "FGHealthComponent.h"            // w18-11 health-delta damage verification
#include "PFAFactionConfig.h"             // W2.3 live damage multiplier
#include "GameFramework/Pawn.h"           // GetPawn()->GetName()
#include "GameFramework/Controller.h"     // w18-11 TakeDamage instigator
#include "Components/PrimitiveComponent.h" // w18-11 synthetic-hit component fill
#include "Engine/DamageEvents.h"          // w18-11 FPointDamageEvent
#include "Engine/World.h"                 // w30-1 impact timer
#include "TimerManager.h"                 // w30-1 impact timer

namespace
{
	// w30-1: seconds from the strike starting to the frame the jaws shut.
	// 0.792 = the authored Bite HitNotify at frame 19 of a 24fps clip -- the
	// number the animation was built around, not a tuned guess.
	// SET TO 0 to restore the exact w18-10 behaviour (damage on action start,
	// no impact timing). That escape hatch exists because this is the first
	// change to combat TIMING rather than combat numbers.
	static TAutoConsoleVariable<float> CVarPFABiteImpactDelay(
		TEXT("pfa.BiteImpactDelay"), 0.792f,
		TEXT("Seconds between a Sicklejaw's strike starting and its damage landing (authored impact frame = 0.792). 0 = damage immediately on action start, the pre-w30 behaviour."),
		ECVF_Default);

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
	// w18-10 addition to the force condition: ALSO require the base's own
	// performability check. The w18-9 session showed that forcing a positive
	// score while the action is cooling down / not performable makes the BT
	// hammer it ~10x/s -- each attempt rejected with an engine LogAction
	// Warning ("not performable ... Current state: EAS_Setup"). Gating the
	// force on Super::CanPerformAction keeps the score honest during cooldown
	// (score 0 -> BT picks nothing -> no churn) without touching the
	// single-action bring-up guarantee while the bite IS available.
	const bool bForce = (score <= 0.0f) &&
		(!mCreature.IsValid() || mCreature->GetCreatureActionsSetup().Num() <= 1) &&
		Super::CanPerformAction_Implementation();
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
			TEXT("[PFA] Sicklejaw action=Melee PerformAction START (montage=%s) target=%s"),
			mActionMontage ? TEXT("set") : TEXT("null->direct-hit path"),
			GetCurrentTarget() ? *GetCurrentTarget()->GetName() : TEXT("none"));
	}
	Super::PerformAction_Implementation();

	// W1.8 verdict (see class comment): with no montage there is no anim-notify
	// and therefore no hit sweep -- the action completes without ever dealing
	// damage. Perform the hit directly through the same gate + the base's own
	// ProcessHit so damage stays on the standard pipeline (HostileScaling
	// contract, doc 06). Removed automatically from play once a real attack
	// montage is authored (W2.2 anim era).
	if (!mActionMontage)
	{
		// w30-1: play the strike, then land the damage on the frame the jaws
		// shut. Order matters -- the clip starts first so that even if the
		// damage path bails (out of range, dead target) the creature is still
		// visibly seen to lunge and miss, which is what a near-miss should
		// look like.
		if (APFASicklejaw* Sicklejaw = Cast<APFASicklejaw>(mCreature.Get()))
		{
			Sicklejaw->PlayAttackOneShot(TEXT("Bite"));
		}

		const float ImpactDelay = FMath::Max(0.0f, CVarPFABiteImpactDelay.GetValueOnGameThread());
		UWorld* World = mCreature.IsValid() ? mCreature->GetWorld() : nullptr;
		if (ImpactDelay > 0.0f && World)
		{
			// Re-arm rather than stack: a second PerformAction before the first
			// impact fired would otherwise queue two hits off one bite.
			World->GetTimerManager().ClearTimer(BiteImpactTimerHandle);
			World->GetTimerManager().SetTimer(
				BiteImpactTimerHandle, this, &UPFACreatureActionMelee::OnBiteImpact, ImpactDelay, false);
			if (PFADiag::IsEnabled())
			{
				UE_LOG(LogPrimalFauna, Display,
					TEXT("[PFA] Sicklejaw action=Melee strike started, damage scheduled for impact frame in %.3fs"),
					ImpactDelay);
			}
		}
		else
		{
			// pfa.BiteImpactDelay 0, or no world to time against: the proven
			// w18-10 behaviour, unchanged.
			TryDirectMeleeHit();
		}
	}
}

void UPFACreatureActionMelee::OnBiteImpact()
{
	// Everything is re-validated inside TryDirectMeleeHit (creature, target,
	// range, cone, authority), which is exactly why the delay is safe: by the
	// time this fires the world may have moved on, and a bite at an absent or
	// out-of-range target must simply miss.
	if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] Sicklejaw action=Melee impact frame reached -- resolving hit"));
	}
	TryDirectMeleeHit();
}

void UPFACreatureActionMelee::TryDirectMeleeHit()
{
	// w18-12 (review rider): defensive server-authority gate -- actions run
	// server-side by construction, but damage must never apply client-side
	// if that construction ever changes (MP, doc 06).
	if (!HasAuthority())
	{
		return;
	}

	AFGCreature* Creature = mCreature.Get();
	AActor* Target = GetCurrentTarget();
	if (!Creature || !Target)
	{
		if (PFADiag::IsEnabled())
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] Sicklejaw action=Melee direct-hit SKIPPED creature=%s target=%s"),
				Creature ? TEXT("valid") : TEXT("null"), Target ? TEXT("valid") : TEXT("null"));
		}
		return;
	}

	AFGCharacterBase* HitCharacter = Cast<AFGCharacterBase>(Target);
	const float DistanceCm = Creature->GetDistanceTo(Target);
	const FVector ToTarget2D = (Target->GetActorLocation() - Creature->GetActorLocation()).GetSafeNormal2D();
	const float CosToTarget = FVector::DotProduct(Creature->GetActorForwardVector().GetSafeNormal2D(), ToTarget2D);
	// mAttackAngle is the full cone width ("targets need to be within this
	// angle"), so the forward-dot test uses the half-angle.
	const float CosHalfCone = FMath::Cos(FMath::DegreesToRadians(mAttackAngle * 0.5f));
	const bool bInRange = DistanceCm <= mAttackRange;
	const bool bInCone = CosToTarget >= CosHalfCone;

	if (HitCharacter && bInRange && bInCone)
	{
		// Synthetic hit at the target -- there is no real sweep trace to
		// source one from on this path. w18-11: FULLY populated (actor
		// handle, component, trace endpoints) -- session 8 showed base
		// ProcessHit applied KNOCKBACK but no damage; one candidate cause is
		// the base damaging via HitResult.GetActor(), which the w18-10
		// synthetic hit left null.
		FHitResult SyntheticHit;
		SyntheticHit.bBlockingHit = true;
		SyntheticHit.Location = Target->GetActorLocation();
		SyntheticHit.ImpactPoint = Target->GetActorLocation();
		SyntheticHit.Normal = -ToTarget2D;
		SyntheticHit.ImpactNormal = -ToTarget2D;
		SyntheticHit.HitObjectHandle = FActorInstanceHandle(HitCharacter);
		SyntheticHit.TraceStart = Creature->GetActorLocation();
		SyntheticHit.TraceEnd = Target->GetActorLocation();
		if (UPrimitiveComponent* TargetRoot = Cast<UPrimitiveComponent>(HitCharacter->GetRootComponent()))
		{
			SyntheticHit.Component = TargetRoot;
		}

		// w18-11 health-delta verification + standard-pipeline fallback.
		// Session 8 in-game: CONNECTED dmg=20.0 logged repeatedly, knockback
		// felt, HP unchanged -- so the closed-source base ProcessHit applies
		// knockback but its damage application does not land from our call
		// site (either damage lives in the unreachable private sweep, or the
		// old null actor-handle starved it -- both closed-source). Rather
		// than guess: measure the target's health across the base call and,
		// ONLY if it did not drop, apply mDamageTypes ourselves via
		// AActor::TakeDamage with a FPointDamageEvent (instigator = creature
		// controller, causer = creature) -- the standard pipeline, so the
		// HostileScaling contract (doc 06) holds on either path. Never both:
		// the fallback is gated on the base having provably applied nothing.
		UFGHealthComponent* TargetHealth = HitCharacter->GetHealthComponent();
		const float HealthBefore = TargetHealth ? TargetHealth->GetCurrentHealth() : -1.0f;

		ProcessHit(HitCharacter, SyntheticHit);

		const float HealthAfterBase = TargetHealth ? TargetHealth->GetCurrentHealth() : -1.0f;
		const bool bBaseApplied = TargetHealth && (HealthAfterBase < HealthBefore);
		bool bFallbackApplied = false;
		// W2.3: our balance knob applies at the SOURCE (doc 04 §1 / doc 07
		// §6) -- scales the outgoing base before the pipeline, composing
		// independently with HostileScaling's own incoming-damage hook.
		// This multiplier lives INSIDE the montage-less fallback, so it dies
		// with TryDirectMeleeHit if that path ever stops running.
		// STATUS 2026-07-25 (w30-1): the montage path that would have retired
		// this is NOT coming. Vanilla plays attacks through
		// UFGCreatureActionAnimatedBase -> UAnimMontage -> UAnimInstance, and we
		// have no AnimInstance (single-node playback by design). So the strike
		// VISUAL now rides the one-shot clip layer while damage stays right
		// here, on the proven path, with the knob intact. The old
		// TODO(W2.2) "re-home this onto the montage path" only becomes live if
		// we later adopt a real AnimInstance -- see the additive-layer decision
		// in _team/primalfauna-state.md.
		const float DamageMultiplier = PFAFactionConfig::SicklejawDamageMultiplier();
		if (!bBaseApplied && TargetHealth)
		{
			AController* InstigatorController = Creature->GetController();
			for (const TObjectPtr<UFGDamageType>& DamageType : mDamageTypes)
			{
				if (DamageType && DamageType->mDamageAmount > 0.0f)
				{
					const float Amount = DamageType->mDamageAmount * DamageMultiplier;
					FPointDamageEvent DamageEvent(Amount, SyntheticHit, ToTarget2D, DamageType->GetClass());
					HitCharacter->TakeDamage(Amount, DamageEvent, InstigatorController, Creature);
					bFallbackApplied = true;
				}
			}
		}

		if (PFADiag::IsEnabled())
		{
			const float HealthFinal = TargetHealth ? TargetHealth->GetCurrentHealth() : -1.0f;
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] Sicklejaw action=Melee direct-hit RESULT target=%s health %.1f -> %.1f (baseApplied=%s fallbackApplied=%s dmgMult=%.2f)"),
				*Target->GetName(), HealthBefore, HealthFinal,
				bBaseApplied ? TEXT("true") : TEXT("false"),
				bFallbackApplied ? TEXT("true") : TEXT("false"),
				DamageMultiplier);
		}
	}
	else if (PFADiag::IsEnabled())
	{
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] Sicklejaw action=Melee direct-hit MISS target=%s isCharacter=%s dist=%.0f/%.0f inCone=%s (cos=%.2f need>=%.2f)"),
			*Target->GetName(), HitCharacter ? TEXT("true") : TEXT("false"),
			DistanceCm, mAttackRange, bInCone ? TEXT("true") : TEXT("false"),
			CosToTarget, CosHalfCone);
	}
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

	// w30-1: an action that ends before its impact frame must not land a hit
	// afterwards -- otherwise a bite interrupted by death, a state change or a
	// lost target still bites. Kill the pending impact and release the strike
	// pose so the locomotion loop takes the creature back immediately.
	if (mCreature.IsValid())
	{
		if (UWorld* World = mCreature->GetWorld())
		{
			World->GetTimerManager().ClearTimer(BiteImpactTimerHandle);
		}
		if (APFASicklejaw* Sicklejaw = Cast<APFASicklejaw>(mCreature.Get()))
		{
			Sicklejaw->StopAttackOneShot();
		}
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
