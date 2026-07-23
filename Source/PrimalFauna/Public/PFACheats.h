#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class UWorld;

// ---------------------------------------------------------------------------
// PFACheats -- dev console commands + the self-test framework (docs/08 W1.1
// expanded scope). This is how an automated agent (not a human) drives and
// verifies PrimalFauna in-game: it types console commands and greps the log
// for the grammar PFA_RunSelfTests emits below.
//
// Registers (all as file-scope statics in PFACheats.cpp -- nothing here
// needs calling from StartupModule; they self-register with IConsoleManager
// at module load, the same mechanism the engine itself uses for CVars like
// CVarCreatureDebug in Creature/FGCreature.h):
//   PFA.SelfTest          runs every registered self-test (see below)
//   PFA.DumpCreature       census-dumps the creature the player is looking at
//   PFA.DumpAllCreatures   per-class census of every live AFGCreature
//   PFA.SpawnNest          stub -- arrives in W1.5
//   PFA.WormForceEvent     stub -- arrives in W4
//   pfa.Diag   (CVar, int) 1 = force verbose [PFA] diagnostics on
//   pfa.AutoTest (CVar, int) 1 = auto-run PFA.SelfTest on first refresh tick
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Diagnostics gate -- sml-gotchas.md §3 "config-gated diagnostics" pattern.
//
// Hooks/commands are registered at module load, far too early to read
// per-world config, so the gate is a cheap file-static any PFA code can poll
// on the game thread (no atomics needed -- see the gotcha entry). IsEnabled()
// is a live OR of two independent inputs: the pfa.Diag console variable
// (instant, for ad-hoc debugging) and the mod's "Enable Diagnostics" config
// option (APFASubsystem::RefreshTick pushes the live config value in via
// SetConfigDiagnostics every tick, so a pause-menu edit takes effect within
// ~10s with no reload, matching the config property's own description text).
//
// Deliberate choice: gated content in this packet logs at Display severity
// behind an explicit `if (PFADiag::IsEnabled())`, NOT at UE_LOG's Verbose
// severity. LogPrimalFauna's default RUNTIME verbosity is "Log" (see its
// DECLARE_LOG_CATEGORY_EXTERN in PrimalFauna.h), which filters out Verbose-
// severity lines regardless of this flag -- so pfa.Diag=1 would silently do
// nothing for Verbose-severity content unless something also raised the
// category's runtime verbosity (a second, unwired knob). Manual gate +
// Display severity makes pfa.Diag/EnableDiagnostics the single, reliable
// switch, matching the only proven precedent in this codebase (PFASubsystem
// is Display-only throughout). See the W1.1 handoff notes for the
// alternative considered (syncing LogPrimalFauna.SetVerbosity() to this
// flag) and why it was not chosen for this packet.
// ---------------------------------------------------------------------------
namespace PFADiag
{
	// True if verbose [PFA] diagnostics should be logged right now.
	PRIMALFAUNA_API bool IsEnabled();

	// Called by APFASubsystem::RefreshTick every tick with the live config
	// value. Do not call this from anywhere else -- it is the one write path
	// for the config-driven half of the gate.
	PRIMALFAUNA_API void SetConfigDiagnostics(bool bEnabled);

	// True if pfa.AutoTest is set. APFASubsystem::RefreshTick checks this
	// once, on its first tick, to decide whether to auto-run the self-test
	// registry on world load (docs/08 W1.1: "hook via the subsystem's first
	// RefreshTick, NOT a new engine hook").
	PRIMALFAUNA_API bool IsAutoTestRequested();
}

// ---------------------------------------------------------------------------
// Self-test framework. An extensible registry: this header defines the type
// and the entry points; PFACheats.cpp registers W1.1's own 4 tests at the
// bottom of the file as the worked example. LATER packets (W1.3, W1.5, ...)
// add their OWN tests from their OWN .cpp files without touching this file's
// logic -- at file scope, after #include "PFACheats.h":
//
//     static const bool bRegistered_MyTest = [] {
//         RegisterPFASelfTest({TEXT("MyTest"),
//             [](UWorld* World, FString& OutDetail) -> bool
//             {
//                 OutDetail = TEXT("...");
//                 return true; // or false
//             }});
//         return true;
//     }();
//
// This IIFE-at-static-init idiom (rather than a registration macro) keeps
// each call site's control flow explicit and debuggable; see PFACheats.cpp's
// own 4 registrations for copy-paste starting points. The registry itself is
// a function-local static (Meyer's singleton), so cross-translation-unit
// static initialization ORDER never matters -- only that every registration
// has run before something reads the registry, which is guaranteed because
// nothing reads it before the engine starts ticking the world.
// ---------------------------------------------------------------------------
struct PRIMALFAUNA_API FPFASelfTest
{
	FName Name;
	TFunction<bool(UWorld* World, FString& OutDetail)> Run;
};

// Adds a test to the registry. Safe to call from a file-scope static
// initializer in any PFA .cpp file (see the idiom above).
PRIMALFAUNA_API void RegisterPFASelfTest(const FPFASelfTest& Test);

// Runs every registered self-test against World and logs, Display level:
//   [PFA][TEST] <Name>: PASS <detail>
//   [PFA][TEST] <Name>: FAIL <detail>
// followed by exactly one summary line:
//   [PFA][TEST] SUMMARY: <passCount>/<total> PASS
// This grammar is contract -- docs/08 §1 names an automated game-testing
// agent that greps for it instead of playing manually. Do not reformat it.
//
// Server-side only: if World is null or we are a network client (no
// authority over server-side gameplay state), this logs one clear abort
// line and does nothing else -- callable safely from the console command
// (which could in principle run on a client) and from RefreshTick's
// pfa.AutoTest path (already server-only via APFASubsystem::HasAuthority(),
// so the check there is a no-op safety belt, not load-bearing).
PRIMALFAUNA_API void PFA_RunSelfTests(UWorld* World);
