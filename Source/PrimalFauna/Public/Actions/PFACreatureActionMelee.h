#pragma once

#include "CoreMinimal.h"
#include "Creature/Actions/FGCreatureActionMeleeAttack.h"
#include "PFACreatureActionMelee.generated.h"

class AFGCharacterBase;

// ---------------------------------------------------------------------------
// UPFACreatureActionMelee -- the Sicklejaw's bite (docs/07 §5, docs/08 W1.4).
//
// WHY A SUBCLASS (design decision, W1.4):
// docs/07 §5 says "CreateDefaultSubobject<UFGCreatureActionMeleeAttack> in the
// species ctor, tune fields." But every tunable field on that class --
// mAttackRange (FGCreatureActionMeleeAttack.h:71), mMinRange (:75),
// mAttackAngle (:79), mKnockbackAmount (:87), mDamageTypes (:95) -- is
// PROTECTED (block opens at :64). APFASicklejaw is NOT a subclass of the melee
// action, so it cannot write those fields directly, and this packet is
// forbidden from adding a new AccessTransformer Friend grant (handoff rule:
// "the existing Friend grant covers only APFACreatureBase->AFGCreature").
// The mod's own architecture (docs/07 §1) is the answer: "protected members
// ... are reached from inside our subclasses (C++ protected inheritance
// access)." So we subclass and set the protected fields in OUR ctor. This is
// compile-safe (a game-update field rename breaks the BUILD, not runtime) and
// needs no grant. Reflection writes were the rejected alternative (W1.3
// precedent chose compile-safety over silent runtime no-ops).
//
// MONTAGE-LESS (docs/08 §4 assumption item 2): mActionMontage
// (FGCreatureActionAnimated.h:31) is deliberately LEFT NULL. The base melee
// hit is driven by an anim-notify (UFGCreatureActionMeleeAttack::
// OnAnimNotify_Implementation + UFGCreatureActionAnimatedBase::
// mAnimNotifyTriggers), so montage-less the ProcessHit->damage path may never
// fire. That is exactly the assumption W1.4 must make TESTABLE, not paper over.
// We therefore instrument the four pipeline stages below so ONE in-game test
// reveals precisely where (if anywhere) montage-less breaks; we do NOT force
// the damage. See the handoff for the ranked montage-less fallback levers.
//
// W1.8 VERDICT (w18-9 in-game session, 2026-07-24): montage-less = NO HIT,
// definitively. PerformAction fired and completed instantly, ProcessHit never
// followed -- the hit sweep only exists inside the montage's anim-notify.
// RESOLUTION (w18-10): PerformAction now runs TryDirectMeleeHit() when
// mActionMontage is null -- the same range/angle gate the sweep enforces,
// with damage still applied by the base ProcessHit (mDamageTypes+knockback),
// so the standard damage pipeline / HostileScaling contract (doc 06) holds.
// Self-retiring: ships a real attack montage in the W2.2 anim era and the
// direct path stops running.
// ---------------------------------------------------------------------------
UCLASS()
class PRIMALFAUNA_API UPFACreatureActionMelee : public UFGCreatureActionMeleeAttack
{
	GENERATED_BODY()

public:
	// Must forward FObjectInitializer: UFGCreatureActionMeleeAttack declares only
	// UFGCreatureActionMeleeAttack(const FObjectInitializer&) (that header:30) --
	// there is no no-arg base ctor to default-construct.
	UPFACreatureActionMelee(const FObjectInitializer& ObjectInitializer);

	// --- Self-test read-backs (MeleeActionConfigured, PFACreatureActionMelee.cpp).
	// mAttackRange / mDamageTypes are protected on the base; these public getters
	// are the audited, compile-safe way the self-test reads what the ctor built
	// without a Friend grant or reflection. ---
	float GetConfiguredAttackRange() const { return mAttackRange; }

	// Sum of mDamageAmount over every configured damage-type instance. The
	// self-test's "damage > 0" bar. Defined in the .cpp so this header need not
	// pull in FGDamageType.h just to touch UFGDamageType::mDamageAmount.
	float GetConfiguredBiteDamage() const;

	int32 GetConfiguredDamageTypeCount() const { return mDamageTypes.Num(); }

protected:
	// --- Staged instrumentation (docs/08 W1.4 "instrument action-scoring/target
	// lines from the first build"). Each override is a pure wrap: log behind
	// PFADiag::IsEnabled(), then call Super -- EXCEPT ScoreAction, which also
	// forces a positive score (the one behavioral choice; see the .cpp). The
	// stages let a tester read the log and distinguish the failure modes:
	//   ScoreAction logs    -> the borrowed BT is invoking action scoring at all
	//   CanPerformAction    -> the action is eligible (range/angle to target)
	//   PerformAction       -> the action started (a montage WOULD play here)
	//   ProcessHit          -> a hit connected and damage was applied
	// "Idle creature + no ScoreAction logs while aggroed & in range" => the
	// borrowed BT does NOT drive our actions (BT incompatible -> fallback recipe).
	// "PerformAction logs but ProcessHit never does" => montage-less hit gap. ---
	virtual bool ScoreAction_Implementation(float& score) const override;
	virtual bool CanPerformAction_Implementation() const override;
	virtual void PerformAction_Implementation() override;
	virtual void ProcessHit_Implementation(AFGCharacterBase* HitCharacter, const FHitResult& HitResult) override;
	//   CleanupAction        -> the action finished (success flag) rather than hung
	virtual void CleanupAction_Implementation(bool actionSuccess) override;

private:
	// w18-10: the montage-less hit (see the class comment's W1.8 VERDICT).
	// Re-implements only the sweep's GATE (current target within mAttackRange
	// and the mAttackAngle forward cone); the damage/knockback application is
	// the base ProcessHit, untouched. Server-side only by construction
	// (creature actions execute on the server); the base's private client-fx
	// multicast is not reachable -- cosmetic hit fx absent until montages.
	void TryDirectMeleeHit();

	// --- w30-1: strike visual + impact-timed damage --------------------------
	// The bite has dealt damage with no animation at all since w18-10, because
	// vanilla plays attacks as montages (UFGCreatureActionAnimatedBase) and
	// montages need a UAnimInstance we don't have. The strike now rides the
	// one-shot clip layer instead, and the damage moves to the frame the jaws
	// actually shut (Bite HitNotify @ f19 of 24fps = 0.792s) so the hit lands
	// when the player can see why.
	//
	// This CHANGES COMBAT TIMING, so it is revertible in game without a
	// rebuild: pfa.BiteImpactDelay 0 restores the w18-10 immediate hit exactly.
	// The delayed path re-runs the FULL range/cone gate at impact, so backing
	// out of a committed bite now makes it whiff -- a behaviour change, and an
	// improvement, but call it out rather than let it be discovered.
	void OnBiteImpact();
	FTimerHandle BiteImpactTimerHandle;
};
