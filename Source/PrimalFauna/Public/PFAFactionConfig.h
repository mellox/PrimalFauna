#pragma once

#include "CoreMinimal.h"

struct FPFAConfigStruct;

// ---------------------------------------------------------------------------
// PFAFactionConfig -- live faction/balance config snapshot (docs/04 §1, W2.3).
// The PFADiag pattern applied to gameplay keys: APFASubsystem::RefreshTick
// (server-only) pushes the active config into file-statics every ~10s; hot
// per-evaluation readers (controller rules 6/7, the bite's damage fallback)
// read the statics with zero config-manager lookups per call. Clients keep
// the compile-time defaults -- correct by design: every consumer runs
// server-side (doc 04 §2), the same contract HostileScaling ships with.
// ---------------------------------------------------------------------------
namespace PFAFactionConfig
{
	// Pushed from the active config each subsystem refresh tick (server).
	void Push(const FPFAConfigStruct& Config);

	bool HuntVanillaHostiles();
	bool HuntPassiveFauna();

	// Key exists in the tree (doc 04 §1) but its consumer (doc 02 §4 shared
	// pack aggro) is a later packet -- tooltip says so (ExperimentalFeatures
	// precedent: honest no-effect keys beat schema churn).
	bool PackAssist();

	// Sicklejaw balance knob, clamped to doc 04's 0.25..2.0 at push time.
	// v1 single-species; becomes per-species when the roster ships.
	float SicklejawDamageMultiplier();
}
