#include "PFACreatureController.h"
#include "PrimalFauna.h"
#include "PFACheats.h"
#include "PFACreatureBase.h"
#include "Species/PFASicklejaw.h"

#include "Creature/FGCreature.h"
#include "Creature/FGCreatureSpawner.h"
#include "FGCreatureSubsystem.h"
#include "FGCharacterPlayer.h"

namespace
{
	void LogRuleVerdict(const AFGCreature* SelfCreature, const AActor* Target, int32 RuleNumber, bool bHostile)
	{
		if (!PFADiag::IsEnabled())
		{
			return;
		}
		// Contract format per docs/02-hostility-and-factions.md §2 -- do not
		// reformat (this is NOT the [PFA][TEST] self-test grammar).
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] %s vs %s: rule%d -> %s"),
			SelfCreature ? *SelfCreature->GetName() : TEXT("<null>"),
			Target ? *Target->GetName() : TEXT("<null>"),
			RuleNumber,
			bHostile ? TEXT("HOSTILE") : TEXT("NOT_HOSTILE"));
	}
}

APFACreatureController::APFACreatureController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool APFACreatureController::CanBeHostileTowards(const AActor* actor) const
{
	const AFGCreature* SelfCreature = GetControlledCreature();
	if (!actor || !SelfCreature)
	{
		return Super::CanBeHostileTowards(actor);
	}

	const AFGCreature* TargetCreature = Cast<AFGCreature>(actor);

	// Rule 1: tamed creature -> never. Hardcoded, not config.
	//
	// IsTamed() is declared WITHOUT const (Creature/FGCreature.h:484 --
	// verified against the header: "bool IsTamed();", a
	// BlueprintImplementableEvent with no const qualifier) despite being a
	// pure query -- vanilla creature BPs implement it by reading tamed-status
	// data, they don't mutate anything. Our `actor`/TargetCreature are const
	// (CanBeHostileTowards's own signature is `const AActor* actor ... const`,
	// mandated by the base class), so calling this non-const-qualified query
	// needs a const_cast -- not a real mutation, just a header wart.
	if (TargetCreature && const_cast<AFGCreature*>(TargetCreature)->IsTamed())
	{
		LogRuleVerdict(SelfCreature, actor, 1, false);
		return false;
	}

	// Rule 2: protected species (the game's own never-target list) -> never.
	if (TargetCreature)
	{
		if (AFGCreatureSubsystem* CreatureSubsystem = AFGCreatureSubsystem::Get(GetWorld()))
		{
			if (CreatureSubsystem->IsProtectedCreature(TargetCreature->GetClass()))
			{
				LogRuleVerdict(SelfCreature, actor, 2, false);
				return false;
			}
		}
	}

	// Rule 3: our own species (same class) -> never (packmates).
	if (actor->GetClass() == SelfCreature->GetClass())
	{
		LogRuleVerdict(SelfCreature, actor, 3, false);
		return false;
	}

	// Rule 4: another PrimalFauna species -> never (v1; post-1.0 flavor
	// option per doc 02 §2 row 4). Rule 3 already excluded same-class, so
	// reaching here with an APFACreatureBase means a DIFFERENT species.
	if (Cast<APFACreatureBase>(actor) != nullptr)
	{
		LogRuleVerdict(SelfCreature, actor, 4, false);
		return false;
	}

	// Rule 5: a player -> defer to Super (preserves the vanilla per-player
	// Passive/Retaliate setting, doc 02 §2 row 5), THEN apply our species
	// aggression gate. We only ever make the result MORE restrictive.
	if (Cast<AFGCharacterPlayer>(actor) != nullptr)
	{
		if (!Super::CanBeHostileTowards(actor))
		{
			LogRuleVerdict(SelfCreature, actor, 5, false);
			return false;
		}
		const bool bResult = PassesAggressionModeGate(actor);
		LogRuleVerdict(SelfCreature, actor, 5, bResult);
		return bResult;
	}

	// Rules 6/7: a vanilla creature (not tamed/protected/ours -- already
	// filtered by rules 1-4 above). Split on IsPassiveCreature(); both apply
	// the same species aggression gate as rule 5.
	if (TargetCreature)
	{
		if (!TargetCreature->IsPassiveCreature())
		{
			const bool bResult = HuntVanillaHostilesDefault && PassesAggressionModeGate(actor);
			LogRuleVerdict(SelfCreature, actor, 6, bResult);
			return bResult;
		}
		else
		{
			const bool bResult = HuntPassiveFaunaDefault && PassesAggressionModeGate(actor);
			LogRuleVerdict(SelfCreature, actor, 7, bResult);
			return bResult;
		}
	}

	// Rule 8: anything else (vehicles, buildings, unknown) -> defer to Super.
	const bool bResult = Super::CanBeHostileTowards(actor);
	LogRuleVerdict(SelfCreature, actor, 8, bResult);
	return bResult;
}

bool APFACreatureController::IsValidTarget(const AActor* actor) const
{
	return Super::IsValidTarget(actor) && CanBeHostileTowards(actor);
}

bool APFACreatureController::CanBeAlertedByTarget_Implementation(const AActor* target) const
{
	return CanBeHostileTowards(target);
}

bool APFACreatureController::PassesAggressionModeGate(const AActor* Target) const
{
	const APFACreatureBase* SelfBase = Cast<APFACreatureBase>(GetControlledCreature());
	if (!SelfBase)
	{
		// Our controller only ever possesses our own creatures (we set
		// AIControllerClass ourselves in APFACreatureBase's ctor); reaching
		// here would mean that wiring is broken elsewhere. Fail open
		// (Hostile-like) rather than silently blocking every evaluation.
		return true;
	}

	switch (SelfBase->GetAggressionMode())
	{
	case EPFAAggression::Passive:
		// Belt-and-suspenders: mIsPassiveCreature (set by SetAggressionMode)
		// should already suppress native aggro before we're even asked, but
		// never initiate here either way.
		return false;

	case EPFAAggression::Territorial:
	{
		if (WasRecentlyDamagedBy(Target))
		{
			return true;
		}
		if (AFGCreatureSpawner* Spawner = SelfBase->GetSpawner())
		{
			const float DistanceCm = FVector::Dist(Spawner->GetActorLocation(), Target->GetActorLocation());
			return DistanceCm <= SelfBase->GetTerritoryRadiusCm();
		}
		// No spawner yet -- nests arrive W1.5 (doc 03 §5). Fall through to
		// Hostile behavior so a Territorial creature isn't dead weight
		// before nests exist; log once so this isn't mistaken for a bug.
		if (!bLoggedNoSpawnerFallback && PFADiag::IsEnabled())
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] %s: Territorial mode with no spawner yet (nests arrive W1.5) -- falling through to Hostile behavior."),
				*SelfBase->GetName());
			bLoggedNoSpawnerFallback = true;
		}
		return true;
	}

	case EPFAAggression::Hostile:
	default:
		return true;
	}
}

bool APFACreatureController::WasRecentlyDamagedBy(const AActor* Target) const
{
	if (!Target)
	{
		return false;
	}
	for (const FFGRecentDamageTaken& Entry : mRecentDamageTaken)
	{
		if (Entry.DamageCauser == Target)
		{
			return true;
		}
		if (Entry.DamageInstigator && Entry.DamageInstigator->GetPawn() == Target)
		{
			return true;
		}
	}
	return false;
}

// ---- W1.3 self-test: entity-free wiring checks (see PFACheats.h for the
// registration idiom). Lives here (not PFASicklejaw.cpp) so it can read the
// HuntVanillaHostilesDefault / HuntPassiveFaunaDefault constants directly. ----
namespace
{
	const bool bRegistered_SicklejawControllerPolicy = []
	{
		RegisterPFASelfTest({TEXT("SicklejawControllerPolicy"),
			[](UWorld* /*World*/, FString& OutDetail) -> bool
			{
				const APFASicklejaw* Cdo = GetDefault<APFASicklejaw>();
				const UClass* ControllerClass = Cdo ? Cdo->AIControllerClass.Get() : nullptr;
				const bool bControllerWired = ControllerClass == APFACreatureController::StaticClass();
				const bool bDefaultsMatchDoc04 =
					(APFACreatureController::HuntVanillaHostilesDefault == true) &&
					(APFACreatureController::HuntPassiveFaunaDefault == false);

				OutDetail = FString::Printf(TEXT("AIControllerClass=%s HuntVanillaHostiles(default)=%s HuntPassiveFauna(default)=%s"),
					ControllerClass ? *ControllerClass->GetName() : TEXT("none"),
					APFACreatureController::HuntVanillaHostilesDefault ? TEXT("true") : TEXT("false"),
					APFACreatureController::HuntPassiveFaunaDefault ? TEXT("true") : TEXT("false"));
				return bControllerWired && bDefaultsMatchDoc04;
			}});
		return true;
	}();
}
