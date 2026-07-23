#pragma once

#include "CoreMinimal.h"

class UWorld;
class UBehaviorTree;

// ---------------------------------------------------------------------------
// PFABehaviorBorrower -- runtime AssetRegistry discovery of a vanilla
// UBehaviorTree to "borrow" onto our creatures' CDO (docs/07 §4, docs/08 W1.4).
//
// WHY DISCOVERY INSTEAD OF A HARDCODED PATH: the W1.2 census never captured a
// hostile-creature BT path (docs/CENSUS.md -- the smoke session only saw
// passive fauna). There is no vanilla path to hardcode yet. So we query the
// asset registry at world load, LOG every UBehaviorTree found under
// /Game/FactoryGame ("[PFA] BT census: <path>" -- this DOUBLES as the census
// we are missing), pick one by a name heuristic, load it, and assign it to the
// APFASicklejaw CDO's mBehaviorTree (FGCreature.h:548, protected -- written via
// APFACreatureBase::SetBorrowedBehaviorTree).
//
// WHY A FREE-FUNCTION SET (not a UPFABehaviorBorrower UObject): discovery is
// stateless; the only state that must persist is the GC root for the loaded
// tree, and that MUST live on a live rooted object anyway (cookbook gotcha
// #34 -- a UPROPERTY on the world subsystem). A UObject borrower would add a
// second rooted object for no benefit. So this pure function returns the chosen
// tree + stats; APFASubsystem owns the GC root and caches the stats for the
// self-test. Matches this codebase's namespace+free-function style (PFACheats).
// ---------------------------------------------------------------------------
namespace PFABehaviorBorrower
{
	struct FResult
	{
		// The loaded tree (null if none discovered / matched / loaded). The
		// CALLER must keep this alive in a UPROPERTY on a rooted object -- this
		// function does NOT root it (cookbook #34: an asset referenced only from
		// a CDO is invisible to GC).
		UBehaviorTree* ChosenTree = nullptr;
		FString ChosenPath;         // object path of ChosenTree, or empty
		FString ChosenReason;       // which heuristic tier matched, or why none did
		FString SearchRoot;         // package path actually searched (/Game/FactoryGame, or widened /Game)
		int32 DiscoveredCount = 0;  // number of UBehaviorTree assets discovered under SearchRoot
	};

	// Server-side. Discovers + census-logs + heuristic-picks + loads a vanilla
	// BehaviorTree and assigns it to the APFASicklejaw CDO. Idempotent: safe to
	// call once per world load (re-assigns the same CDO field). Returns the
	// chosen tree + stats; the caller roots the tree and caches the stats.
	PRIMALFAUNA_API FResult DiscoverAndBorrow(UWorld* World);
}
