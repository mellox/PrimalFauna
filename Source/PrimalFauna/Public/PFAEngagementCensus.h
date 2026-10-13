#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

// ---------------------------------------------------------------------------
// PFAEngagementCensus -- w58-5. The measurement half of the dead-target
// retention fix, and the ONE definition of "is this target alive".
//
// WHY THIS EXISTS: nothing in this mod ever tested whether the thing a
// creature was fighting was still alive. Health was checked on SELF
// (Actions/PFACreatureActionMelee.cpp) and on PACKMATE CANDIDATES
// (PFACreatureController.cpp) -- both the looking side of the relationship,
// never the looked-at side. The 2026-08-18 diagnosis measured two hogs held
// 46s and 50s after the pack could last hit them, with no field anywhere in
// the log naming the target's liveness.
//
// SYMMETRY: the liveness predicate lives HERE, in one place, and is used by
// both the drop path (PFACreatureControllerPerception.cpp) and by this
// census. A predicate defined twice is a predicate that will diverge.
//
// CONTRACT: this reports what it MEASURED and never states why. Every number
// it prints is sampled on the pass that prints it, or is a timestamped sample
// recorded by the code path that actually ran, with its AGE printed alongside
// so a stale sample cannot read as a fresh one.
// ---------------------------------------------------------------------------
namespace PFAEngagement
{
	// How a target actor graded when its liveness was last MEASURED. The names
	// say what was tested, not what it means: NoHealthComponent is NOT "alive"
	// and NOT "dead" -- it is "this actor is not a character we can ask".
	enum class ETargetLiveness : uint8
	{
		Alive,
		Dead,
		Destroyed,
		NoHealthComponent,
	};

	// Grades Target. Null / invalid / being-destroyed grades Destroyed. An
	// actor that is not an AFGCharacterBase, or whose health component is
	// missing, grades NoHealthComponent -- never Dead, because treating
	// "cannot ask" as "dead" is how a fix for a retention bug turns into a
	// creature that drops live targets it simply could not measure.
	PRIMALFAUNA_API ETargetLiveness GradeTargetLiveness(const AActor* Target);

	// Short token for logs: alive / dead / destroyed / nohealthcomponent.
	PRIMALFAUNA_API const TCHAR* LivenessToken(ETargetLiveness Liveness);

	// The binary field. Prints 1 for Alive, 0 for Dead, 0 for Destroyed, and
	// the literal unknown for NoHealthComponent -- so the field never claims a
	// measurement that was not taken.
	PRIMALFAUNA_API const TCHAR* LivenessAliveField(ETargetLiveness Liveness);

	// True only for grades that PROVE the target can no longer be fought:
	// Dead and Destroyed. NoHealthComponent is deliberately false.
	PRIMALFAUNA_API bool IsConfirmedNotFightable(ETargetLiveness Liveness);

	// How long (s) a controller must sit in CS_Fight on an unchanged target
	// with ZERO PerformAction starts before the engagement self-test calls it
	// a stall. Read from pfa.EngagementStallSeconds every call, so the number
	// printed in a log line is the number that ran, not a baked constant.
	PRIMALFAUNA_API float GetStallSeconds();

	// Runs ONE census pass over every PrimalFauna creature in World: a legend
	// (once), one always-on summary line, and one detail line per creature
	// that currently holds a target. Safe with a null World (logs nothing).
	// Trigger is a short literal naming what caused this pass.
	PRIMALFAUNA_API void RunPass(UWorld* World, const TCHAR* Trigger, bool bLogEveryCreature);

	// The self-test body, shared by the PFA.SelfTest registry entry and by the
	// always-on FAIL line the census emits during ordinary play. Returns false
	// when at least one controller is stalled or holds a confirmed-dead
	// target; OutDetail always carries the counts it measured.
	PRIMALFAUNA_API bool EvaluateEngagementLiveness(UWorld* World, FString& OutDetail);
}
