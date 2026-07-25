// PFASoundCensus.cpp -- PFA.SoundCensus: enumerate the Wwise events that the
// SHIPPING game actually contains.
//
// WHY THIS EXISTS
// We want creature audio, and the cheapest correct route is to reuse base-game
// Wwise events -- a reused event inherits the player's creature-volume slider,
// its bus, its attenuation and its RTPCs, where a custom event ignores all of
// that until we build them ourselves. But we cannot READ the list: SML's
// WwiseAudio content is a stub with zero events, and the shipped .utoc index is
// not plain text. Every candidate event path anyone writes down is therefore a
// guess.
//
// The behaviour-tree borrower already solved this exact problem for a different
// asset class: run an IAssetRegistry query INSIDE the cooked shipping game,
// where the real assets are mounted. It works -- the test logs contain its
// output. This is that, with the class swapped.
//
// One build plus a three-minute run turns "here are some plausible-looking
// paths" into a list of paths that provably exist, which is the difference
// between wiring sound and guessing at it.

#include "PrimalFauna.h"
#include "PFACheats.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AkAudioEvent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

namespace
{
	// Substrings that make an event interesting for a CREATURE. The census logs
	// everything it finds, but tags these so a 3000-line dump is still readable.
	const TCHAR* CreatureHints[] = {
		TEXT("creature"), TEXT("hog"), TEXT("stinger"), TEXT("spitter"),
		TEXT("hatcher"), TEXT("doggo"), TEXT("crab"), TEXT("giraffe"),
		TEXT("bird"), TEXT("rabbit"), TEXT("alpha"),
		TEXT("roar"), TEXT("growl"), TEXT("attack"), TEXT("bite"),
		TEXT("death"), TEXT("die"), TEXT("hurt"), TEXT("damage"), TEXT("pain"),
		TEXT("footstep"), TEXT("foot"), TEXT("step"), TEXT("idle"), TEXT("alert"),
	};

	bool LooksCreatureRelated(const FString& Path)
	{
		const FString Lower = Path.ToLower();
		for (const TCHAR* Hint : CreatureHints)
		{
			if (Lower.Contains(Hint))
			{
				return true;
			}
		}
		return false;
	}

	void SoundCensusCommand(const TArray<FString>& Args, UWorld* /*World*/)
	{
		// Optional filter: PFA.SoundCensus hog   -> only paths containing "hog".
		const FString Needle = Args.Num() > 0 ? Args[0].ToLower() : FString();

		IAssetRegistry* Registry = IAssetRegistry::Get();
		if (!Registry)
		{
			UE_LOG(LogPrimalFauna, Error,
				TEXT("[PFA] SoundCensus ABORT: IAssetRegistry::Get() is null."));
			return;
		}

		FARFilter Filter;
		Filter.ClassPaths.Add(UAkAudioEvent::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> Found;
		Registry->GetAssets(Filter, Found);

		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] ===== SoundCensus: %d UAkAudioEvent asset(s) visible to the asset registry%s ====="),
			Found.Num(),
			Needle.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (filter '%s')"), *Needle));

		if (Found.Num() == 0)
		{
			// A real answer, not a failure: it means the registry cannot see
			// cooked audio and reuse has to be sourced another way. Say so
			// plainly rather than leaving an empty log to be misread as a crash.
			UE_LOG(LogPrimalFauna, Warning,
				TEXT("[PFA] SoundCensus found NOTHING. Either the shipping game's Wwise events are not indexed by the asset registry, ")
				TEXT("or they are mounted under a path the registry does not scan. Reuse-by-path cannot be planned from this build; ")
				TEXT("fall back to harvesting event OBJECT POINTERS off a vanilla creature CDO (mDeathSound / mTakeDamageSound / mFootstepAudioEvents)."));
			return;
		}

		int32 Interesting = 0;
		for (const FAssetData& Asset : Found)
		{
			const FString Path = Asset.GetObjectPathString();
			if (!Needle.IsEmpty() && !Path.ToLower().Contains(Needle))
			{
				continue;
			}
			const bool bHint = LooksCreatureRelated(Path);
			if (bHint)
			{
				++Interesting;
			}
			// CREATURE-tagged lines are the ones worth reading; everything else
			// is logged too so the census is a complete record rather than a
			// filtered opinion.
			UE_LOG(LogPrimalFauna, Display, TEXT("[PFA] SoundCensus %s %s"),
				bHint ? TEXT("CREATURE?") : TEXT("        -"), *Path);
		}

		UE_LOG(LogPrimalFauna, Display,
			TEXT("[PFA] ===== SoundCensus DONE: %d total, %d tagged creature-related. ")
			TEXT("Copy the CREATURE? paths into the sound plan; they provably exist in THIS build. ====="),
			Found.Num(), Interesting);
	}

	FAutoConsoleCommandWithWorldAndArgs CCmdPFASoundCensus(
		TEXT("PFA.SoundCensus"),
		TEXT("Primal Fauna: lists every UAkAudioEvent the asset registry can see in the running game, tagging creature-related ones. Optional arg filters by substring (e.g. PFA.SoundCensus hog). Read-only."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SoundCensusCommand));
}
