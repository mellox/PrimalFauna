#pragma once

#include "CoreMinimal.h"

class UWorld;

// ---------------------------------------------------------------------------
// PFACorpseCensus -- w58-1. The measurement half of the corpse fix.
//
// WHY THIS EXISTS: the w57-10 "Sicklejaw bodies disappear" diagnosis
// (_team/primalfauna-body-despawn-diagnosis.md §1.5) could not separate two
// mechanisms, because NO diagnostic in this codebase covered a creature after
// its death frame:
//    (a) the corpse is still present but somewhere it should not be, or
//    (b) the corpse was removed sooner than intended.
// One census line that carries raw counts by category, plus each corpse's
// world location and its distance from where it died, separates them in the
// very next log without another round of inference.
//
// CONTRACT: this reports what it MEASURED and never states why. It prints no
// cause for a corpse being absent -- "present in the previous pass and absent
// now" is the whole claim, because actor iteration cannot tell destruction
// from streaming, and guessing between them is exactly the failure mode
// [[lessons-log-asserted-a-cause]] records.
// ---------------------------------------------------------------------------
namespace PFACorpseCensus
{
	// Runs ONE census pass over World and emits ONE summary line, plus one
	// detail line per corpse when bLogEveryCorpse is true or diagnostics are
	// enabled. Server-side callers only (the caller decides); safe with a null
	// World, which logs nothing.
	//
	// Trigger is a short literal naming what caused this pass ("heartbeat",
	// "console") and is printed, so a log reader can tell a scheduled sample
	// from a hand-typed one without matching timestamps.
	PRIMALFAUNA_API void RunPass(UWorld* World, const TCHAR* Trigger, bool bLogEveryCorpse);
}
