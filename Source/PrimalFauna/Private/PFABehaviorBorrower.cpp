#include "PFABehaviorBorrower.h"
#include "PrimalFauna.h"
#include "PFACheats.h"
#include "PFASubsystem.h"
#include "PFACreatureBase.h"
#include "Species/PFASicklejaw.h"

#include "BehaviorTree/BehaviorTree.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"

#include "Engine/World.h"
#include "EngineUtils.h" // TActorIterator (BorrowedBTAssigned self-test)

namespace
{
	const TCHAR* PrimarySearchRoot = TEXT("/Game/FactoryGame");
	const TCHAR* WidenedSearchRoot = TEXT("/Game");

	// Query every UBehaviorTree asset under PackageRoot (recursive) into OutAssets.
	void QueryBehaviorTrees(IAssetRegistry& Registry, const TCHAR* PackageRoot, TArray<FAssetData>& OutAssets)
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(UBehaviorTree::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true; // include any UBehaviorTree subclasses
		Filter.PackagePaths.Add(FName(PackageRoot));
		Filter.bRecursivePaths = true;
		Registry.GetAssets(Filter, OutAssets);
	}

	// Name heuristic: tiers strongest -> weakest. First tier with ANY match
	// wins; within a tier the first asset wins. Case-insensitive Contains on the
	// asset name. Returns the chosen index + a human reason, or INDEX_NONE.
	// Deliberately does NOT fall back to "any BT": assigning an arbitrary,
	// non-creature BT to a creature could assert/misbehave -- a clean FAIL with
	// the full census logged is the safer Phase-1 outcome (the human hardcodes a
	// path next round from the census).
	int32 PickCandidate(const TArray<FAssetData>& Assets, FString& OutReason)
	{
		struct FTier { const TCHAR* Token; const TCHAR* Why; };
		static const FTier Tiers[] = {
			{ TEXT("Hog"),      TEXT("Sicklejaw is anchored to Alpha Hog, doc 01 §1") },
			{ TEXT("Stinger"),  TEXT("next-closest hostile archetype") },
			{ TEXT("Spitter"),  TEXT("hostile archetype") },
			{ TEXT("Creature"), TEXT("generic creature tree") },
			{ TEXT("Enemy"),    TEXT("generic enemy tree") },
			{ TEXT("Wildlife"), TEXT("generic wildlife tree") },
		};

		for (const FTier& Tier : Tiers)
		{
			for (int32 i = 0; i < Assets.Num(); ++i)
			{
				if (Assets[i].AssetName.ToString().Contains(Tier.Token, ESearchCase::IgnoreCase))
				{
					OutReason = FString::Printf(TEXT("name contains '%s' (%s)"), Tier.Token, Tier.Why);
					return i;
				}
			}
		}
		OutReason = TEXT("no discovered BT name matched Hog/Stinger/Spitter/Creature/Enemy/Wildlife");
		return INDEX_NONE;
	}
}

PFABehaviorBorrower::FResult PFABehaviorBorrower::DiscoverAndBorrow(UWorld* World)
{
	FResult Result;

	IAssetRegistry* Registry = IAssetRegistry::Get();
	if (!Registry)
	{
		UE_LOG(LogPrimalFauna, Error, TEXT("[PFA] BT borrow ABORT: IAssetRegistry::Get() is null -- cannot discover a behavior tree."));
		Result.ChosenReason = TEXT("asset registry unavailable");
		return Result;
	}

	// In editor/PIE the registry may still be scanning; a cooked shipping game
	// has it loaded synchronously by world load. Log (don't block) so the one
	// case where discovery could under-count is visible from the log alone.
	if (Registry->IsLoadingAssets())
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] BT borrow: asset registry still scanning (editor/PIE?) -- discovery may under-count; a cooked build loads it synchronously."));
	}

	// /Game/FactoryGame first; if that yields zero BTs (content rooted
	// elsewhere), widen to /Game and log it. Robust to the path assumption
	// being wrong -- the log reveals which root actually held the trees.
	TArray<FAssetData> Assets;
	Result.SearchRoot = PrimarySearchRoot;
	QueryBehaviorTrees(*Registry, PrimarySearchRoot, Assets);
	if (Assets.Num() == 0)
	{
		UE_LOG(LogPrimalFauna, Warning,
			TEXT("[PFA] BT census: 0 BehaviorTree assets under %s -- widening to %s (verify the content root)."),
			PrimarySearchRoot, WidenedSearchRoot);
		Result.SearchRoot = WidenedSearchRoot;
		QueryBehaviorTrees(*Registry, WidenedSearchRoot, Assets);
	}
	Result.DiscoveredCount = Assets.Num();

	// Census: EVERY discovered path at Display -- the hostile-BT census the
	// smoke session never captured (docs/CENSUS.md). If this proves spammy a
	// later packet can gate per-path lines behind PFADiag::IsEnabled() and keep
	// only the summary + chosen line at Display (handoff note documents this).
	UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] BT census: %d BehaviorTree asset(s) under %s"),
		Result.DiscoveredCount, *Result.SearchRoot);
	for (const FAssetData& Asset : Assets)
	{
		UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] BT census: %s"), *Asset.GetObjectPathString());
	}

	if (Result.DiscoveredCount == 0)
	{
		UE_LOG(LogPrimalFauna, Error,
			TEXT("[PFA] BT borrow FAIL: no BehaviorTree assets discovered anywhere under %s. Sicklejaw mBehaviorTree stays null (creature idles -- degraded, not broken). Author BT_PFAGeneric (docs/BT-FALLBACK-RECIPE.md)."),
			WidenedSearchRoot);
		Result.ChosenReason = TEXT("zero BehaviorTree assets discovered");
		return Result;
	}

	const int32 ChosenIndex = PickCandidate(Assets, Result.ChosenReason);
	if (ChosenIndex == INDEX_NONE)
	{
		UE_LOG(LogPrimalFauna, Error,
			TEXT("[PFA] BT borrow FAIL: %d BT(s) discovered but none matched the name heuristic (%s). mBehaviorTree stays null. Inspect the [PFA] BT census lines above, then hardcode a path or author BT_PFAGeneric (docs/BT-FALLBACK-RECIPE.md)."),
			Result.DiscoveredCount, *Result.ChosenReason);
		return Result;
	}

	Result.ChosenPath = Assets[ChosenIndex].GetObjectPathString();
	UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] BT borrow: chose %s -- %s"), *Result.ChosenPath, *Result.ChosenReason);

	UBehaviorTree* Tree = LoadObject<UBehaviorTree>(nullptr, *Result.ChosenPath);
	if (!Tree)
	{
		UE_LOG(LogPrimalFauna, Error,
			TEXT("[PFA] BT borrow FAIL: LoadObject<UBehaviorTree>(%s) returned null. mBehaviorTree stays null."),
			*Result.ChosenPath);
		Result.ChosenReason = FString::Printf(TEXT("LoadObject failed for %s"), *Result.ChosenPath);
		return Result;
	}
	Result.ChosenTree = Tree;

	// Assign to the APFASicklejaw CDO. mBehaviorTree is protected (FGCreature.h:548);
	// APFACreatureBase::SetBorrowedBehaviorTree reaches it via inheritance (no
	// grant). The CALLER (subsystem) roots Result.ChosenTree.
	if (APFACreatureBase* SicklejawCdo = APFASicklejaw::StaticClass()->GetDefaultObject<APFACreatureBase>())
	{
		SicklejawCdo->SetBorrowedBehaviorTree(Tree);
		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] BT borrow: assigned %s to APFASicklejaw CDO mBehaviorTree (readback=%s)."),
			*Tree->GetName(),
			SicklejawCdo->GetBehaviorTreeAsset() ? *SicklejawCdo->GetBehaviorTreeAsset()->GetName() : TEXT("null!"));
	}
	else
	{
		UE_LOG(LogPrimalFauna, Error, TEXT("[PFA] BT borrow FAIL: APFASicklejaw CDO is null; cannot assign mBehaviorTree."));
		Result.ChosenReason = TEXT("Sicklejaw CDO null at assign");
	}

	return Result;
}

// ---- Self-test: BorrowedBTAssigned (idiom from PFACheats.cpp). Asserts the
// discovery+assign actually wired the Sicklejaw CDO. The FAIL detail carries
// the discovered-count so a failure says WHY (no BTs vs. no heuristic match)
// and points at the census lines + fallback recipe. ----
namespace
{
	const bool bRegistered_BorrowedBTAssigned = []
	{
		RegisterPFASelfTest({TEXT("BorrowedBTAssigned"),
			[](UWorld* World, FString& OutDetail) -> bool
			{
				const APFASicklejaw* Cdo = GetDefault<APFASicklejaw>();
				const UBehaviorTree* BT = Cdo ? Cdo->GetBehaviorTreeAsset() : nullptr;

				// Discovery stats from the subsystem (set in its BeginPlay borrow).
				int32 DiscoveredCount = -1;
				FString SearchRoot = TEXT("<subsystem not found>");
				for (TActorIterator<APFASubsystem> It(World); It; ++It)
				{
					DiscoveredCount = It->GetBTDiscoveredCount();
					SearchRoot = It->GetBTSearchRoot();
					break;
				}

				if (BT)
				{
					OutDetail = FString::Printf(TEXT("Sicklejaw CDO mBehaviorTree=%s (discovered %d BT(s) under %s)"),
						*BT->GetPathName(), DiscoveredCount, *SearchRoot);
					return true;
				}

				OutDetail = FString::Printf(
					TEXT("Sicklejaw CDO mBehaviorTree is NULL; discovered %d BT(s) under %s, none assigned -- see [PFA] BT census lines + docs/BT-FALLBACK-RECIPE.md"),
					DiscoveredCount, *SearchRoot);
				return false;
			}});
		return true;
	}();
}
