#pragma once

#include "CoreMinimal.h"
#include "AI/FGCreatureController.h"
#include "PFACreatureController.generated.h"

// ---------------------------------------------------------------------------
// APFACreatureController -- implements docs/02-hostility-and-factions.md §2's
// 8-rule target policy table for every PrimalFauna creature. Doc 02 §1: "We
// own our creatures' controller class, so the entire faction policy is plain
// C++ virtual overrides ... no SML hooks needed for our own creatures."
//
// Rule table -> code map (see PFACreatureController.cpp for the full ladder):
//   Rules 1-8 all live in CanBeHostileTowards() -- the single source of truth.
//   IsValidTarget() composes Super's own validity gate with CanBeHostileTowards
//   (doc 02 §2: "IsValidTarget ... appl[ies] the same table").
//   CanBeAlertedByTarget_Implementation() delegates to CanBeHostileTowards
//   directly rather than trusting the closed-source default's documented
//   "runs CanBeHostileTowards by default" behavior (doc 07 §1: own every
//   class we change, don't assume binary defaults).
// ---------------------------------------------------------------------------
UCLASS()
class PRIMALFAUNA_API APFACreatureController : public AFGCreatureController
{
	GENERATED_BODY()

public:
	APFACreatureController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Doc 02 §2 rules 1-8, evaluated in order, first match wins. See the .cpp
	// for the full ladder and per-rule [PFA] verdict logging.
	virtual bool CanBeHostileTowards(const AActor* actor) const override;

	// Doc 02 §2: applies the same table as CanBeHostileTowards, ANDed with
	// Super's own validity gate so we're never MORE permissive than the
	// native baseline.
	virtual bool IsValidTarget(const AActor* actor) const override;

	// TODO(W2.3): these stand in for the not-yet-existing "Factions" config
	// section (docs/04-config-and-ui.md §1, keys HuntVanillaHostiles /
	// HuntPassiveFauna) until the flat-vs-nested-section config layout
	// question (PFAConfig.cpp's note) is settled and the keys exist. Values
	// are doc 04's documented DEFAULTS, copied exactly -- also read by
	// PFASicklejaw.cpp's SicklejawControllerPolicy self-test as a drift guard
	// against this comment going stale.
	static constexpr bool HuntVanillaHostilesDefault = true;
	static constexpr bool HuntPassiveFaunaDefault = false;

protected:
	// BNE (AI/FGCreatureController.h:387). Base's own doc comment says it
	// "runs CanBeHostileTowards by default" -- see the class comment above
	// for why we make that explicit instead of relying on it.
	virtual bool CanBeAlertedByTarget_Implementation(const AActor* target) const override;

private:
	// Doc 02 §3 Territorial-mode gate, applied to rules 5/6/7. Returns true
	// (permissive) for Hostile mode, false for Passive, and for Territorial
	// checks recent-damage-by-target OR distance-to-spawner <= the species'
	// territory radius -- falling through to Hostile (with a one-time gated
	// log) if the creature has no spawner yet (nests arrive W1.5).
	bool PassesAggressionModeGate(const AActor* Target) const;

	// Doc 02 §3 "or after being damaged by the target" -- reads the inherited
	// (protected) mRecentDamageTaken, which the native controller already
	// prunes by mRecentDamageMaxAge, so any entry present here is by
	// definition recent; we don't need our own recency window.
	bool WasRecentlyDamagedBy(const AActor* Target) const;

	// Set once PassesAggressionModeGate has logged the "no spawner yet"
	// fallback for this controller, so it logs once instead of once per
	// evaluation. Mutable because CanBeHostileTowards and friends are const.
	mutable bool bLoggedNoSpawnerFallback = false;
};
