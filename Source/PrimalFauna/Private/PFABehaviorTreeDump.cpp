// ===========================================================================
// PFABehaviorTreeDump.cpp -- w47-1 `PFA.DumpBehaviorTree`, a READ-ONLY
// structural dump of the BehaviorTree our creatures are actually running.
//
// WHY THIS EXISTS
// We borrow a vanilla creature tree (BT_CreatureBehavior, docs/07 §4) and have
// now spent FOUR builds reasoning about what its roam branch reads
// (w32-1 -> w38-1 -> w44-1 -> w45-3, including one phantom-regression chase).
// That guessing was never necessary:
//
//   A UBehaviorTree is DATA, and every class inside it is open engine code we
//   already link against.
//
// Verified against the engine source ON THIS MACHINE for this packet, not
// recalled -- C:\Program Files\Unreal Engine - CSS\Engine\Source\Runtime\AIModule:
//   * UBehaviorTree::RootNode / ::BlackboardAsset  -- plain public UPROPERTYs
//     (Classes/BehaviorTree/BehaviorTree.h:20-21, :41-42; GENERATED_UCLASS_BODY
//     leaves the class body public, which is also why PFACreatureController.cpp
//     already reads BTAsset->BlackboardAsset and compiles).
//   * UBTCompositeNode::Children (TArray<FBTCompositeChild>{ChildComposite,
//     ChildTask, Decorators, DecoratorOps}) and ::Services -- public UPROPERTYs
//     (BTCompositeNode.h:88-95, inside the GENERATED_UCLASS_BODY public region).
//   * UBTTaskNode::Services -- public (BTTaskNode.h:90, public region opens :53).
//   * UBTNode::GetNodeName() (BTNode.h:123) and virtual GetStaticDescription()
//     (:163) are AIMODULE_API and sit ABOVE the `#if WITH_EDITOR` block at :165,
//     so both exist in Shipping. GetStaticDescription is exactly what the editor
//     prints under each node.
//   * UBlackboardData::Keys is a public TArray<FBlackboardEntry>
//     (BlackboardData.h:62) and FBlackboardEntry carries EntryName / KeyType /
//     bInstanceSynced (:19-35).
//   * UBlackboardComponent::DescribeKeyValue(FName, EBlackboardDescription::Type)
//     is AIMODULE_API (BlackboardComponent.h:219) -- the live VALUES.
//   * FactoryGame ships ZERO native BT node classes (no BTTask_*/BTService_*/
//     BTDecorator_* under Source/FactoryGame/Public/AI/), so every node in the
//     borrowed tree is a stock engine node or a Blueprint node -- and a BP node's
//     configured UPROPERTYs are reachable by reflection (the census pattern from
//     docs/07 §1).
//
// WHAT IT ANSWERS IN ONE SESSION
//   * Does the roam branch consume MinRoamingDistance/MaxRoamingDistance at all?
//   * Does anything read FGBBKeys::Spline (the roaming-design doc's Option A)?
//   * What anchors the roam destination -- an EQS query, a context actor, the
//     Spawner key? (EQS task nodes print their query template + params here.)
//   * What keys exist that we have NEVER written (the vanilla-parity shape that
//     has now bitten this mod six times).
//
// READ-ONLY BY CONSTRUCTION. Every call in this file is a getter, a UPROPERTY
// read, or a reflection read: nothing restarts the tree, nothing writes a
// blackboard value, nothing allocates into the AI systems. The one thing to be
// careful of when extending it: do NOT call anything named Set*/Modify*/Request*
// on the BT component or blackboard.
//
// FILE SIZE, STATED HONESTLY: 661 lines (sibling
// PFACreatureControllerPerception.cpp: 1242).
// w47-2 correction -- the w47-1 header claimed 625 when the file was 634, and cited
// the perception TU at 1067 when it was already 1186. That is the exact shape of the
// w46-1 review's F8 finding (a header making a false claim about its own file size),
// repeated one packet later by the header that was written to not repeat it.
// NOT SPLIT, and the honest framing is split-by-concern rather than a per-file
// apology: this codebase does not enforce a line limit (three files in the w47-1
// commit alone exceed 500). The unit here is ONE console command -- its tree walk and
// its blackboard walk share one output format, one stats struct and one pair of caps,
// so splitting them would put two halves of one format in two files and make it
// harder to keep consistent. Roughly half the file is comment.
//
// EXPORT-SAFETY NOTE (the engine flavour of the shipping-export trap, docs/07 §1)
// FBlackboardKeySelector is `USTRUCT(BlueprintType)` with NO AIMODULE_API tag
// (BehaviorTreeTypes.h:622-624), so its generated `StaticStruct()` is not
// exported from AIModule and calling it would be a LINK error. We therefore
// identify that struct BY NAME ("BlackboardKeySelector") and read its members
// directly -- member reads need no symbol. Same reasoning is why the tree walk
// uses `Children`/`Services` directly instead of the FORCEINLINE accessors.
// ===========================================================================

#include "PFACheats.h"
#include "PrimalFauna.h"

#include "Creature/FGCreature.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
// w47-2 (review I1): FBlackboard::InvalidKey, explicitly rather than transitively --
// the key-absent test below is the whole point of that fix and must not depend on
// another header happening to pull the namespace in (same IWYU discipline as
// PFACreatureControllerRoam.cpp's IConsoleManager include).
#include "BehaviorTree/Blackboard/BlackboardKey.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
	// One grep prefix for the whole dump, so a tester can pull the entire thing
	// out of a 60MB FactoryGame.log with a single filter and paste it back.
	#define PFA_BTDUMP_PREFIX TEXT("[PFA][BTDUMP] ")

	// Safety rails. A cooked creature tree is ~30-80 nodes; these exist so a
	// pathological or cyclic asset cannot turn a diagnostic into a hang or a
	// 100k-line log. Hitting either one is itself reported.
	constexpr int32 BTDumpMaxDepth = 24;
	constexpr int32 BTDumpMaxNodes = 600;

	// Per-node property budget. Default mode prints only properties that DIFFER
	// from the node class's CDO (i.e. what the tree author actually configured);
	// `PFA.DumpBehaviorTree full` prints everything.
	constexpr int32 BTDumpMaxPropsDefault = 10;
	constexpr int32 BTDumpMaxPropsFull = 64;
	constexpr int32 BTDumpMaxPropChars = 160;

	struct FBTDumpStats
	{
		int32 Nodes = 0;
		int32 Composites = 0;
		int32 Tasks = 0;
		int32 Decorators = 0;
		int32 Services = 0;
		int32 MaxDepth = 0;
		bool bHitNodeCap = false;
		bool bHitDepthCap = false;
	};

	// GetStaticDescription() is multi-line for many nodes (the editor renders it
	// stacked). A multi-line UE_LOG breaks the one-line-per-node grep contract, so
	// flatten it and cap it.
	FString FlattenDescription(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\r\n"), TEXT(" / "));
		Out.ReplaceInline(TEXT("\n"), TEXT(" / "));
		Out.ReplaceInline(TEXT("\r"), TEXT(" / "));
		Out.ReplaceInline(TEXT("\t"), TEXT(" "));
		while (Out.Contains(TEXT("  ")))
		{
			Out.ReplaceInline(TEXT("  "), TEXT(" "));
		}
		Out.TrimStartAndEndInline();
		if (Out.Len() > 300)
		{
			Out = Out.Left(300) + TEXT("...<truncated>");
		}
		return Out;
	}

	// Reflection census of ONE node (see the file header for why the key-selector
	// struct is matched by name rather than by StaticStruct()).
	//
	//   OutKeys     -- every FBlackboardKeySelector property: which blackboard key
	//                  this node reads/writes. THE load-bearing field for the roam
	//                  question.
	//   OutProps    -- configured scalars/objects (default mode: only those that
	//                  differ from the CDO), which is how a Blueprint node's
	//                  parameters and an EQS task's query template become visible.
	//   OutSubtrees -- any UBehaviorTree-valued property (BTTask_RunBehavior's
	//                  BehaviorAsset, BehaviorTreeTypes.h-adjacent), so the walk
	//                  can follow subtrees instead of stopping at their edge.
	void CensusNode(const UBTNode* Node, bool bFull, FString& OutKeys, FString& OutProps,
		TArray<const UBehaviorTree*>& OutSubtrees)
	{
		if (!Node)
		{
			return;
		}

		UClass* NodeClass = Node->GetClass();
		const UObject* Cdo = NodeClass ? NodeClass->GetDefaultObject() : nullptr;
		const int32 MaxProps = bFull ? BTDumpMaxPropsFull : BTDumpMaxPropsDefault;
		int32 PropsPrinted = 0;
		int32 PropsSkipped = 0;

		for (TFieldIterator<FProperty> PropIt(NodeClass); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop)
			{
				continue;
			}

			// --- blackboard key selectors ------------------------------------
			if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				if (StructProp->Struct && StructProp->Struct->GetFName() == FName(TEXT("BlackboardKeySelector")))
				{
					const FBlackboardKeySelector* Selector =
						StructProp->ContainerPtrToValuePtr<FBlackboardKeySelector>(Node);
					if (Selector)
					{
						OutKeys += FString::Printf(TEXT("%s='%s'(%s) "),
							*Prop->GetName(),
							Selector->SelectedKeyName.IsNone() ? TEXT("<none>") : *Selector->SelectedKeyName.ToString(),
							Selector->SelectedKeyType ? *Selector->SelectedKeyType->GetName() : TEXT("type-unresolved"));
					}
					continue;
				}
			}

			// --- subtree references ------------------------------------------
			if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
			{
				const UObject* Value = ObjProp->GetObjectPropertyValue_InContainer(Node);
				if (const UBehaviorTree* Subtree = Cast<UBehaviorTree>(Value))
				{
					OutSubtrees.AddUnique(Subtree);
				}
			}

			// --- configured values -------------------------------------------
			if (!bFull && Cdo && Prop->Identical_InContainer(Node, Cdo))
			{
				continue; // untouched default: noise
			}
			if (PropsPrinted >= MaxProps)
			{
				++PropsSkipped;
				continue;
			}

			FString ValueStr;
			// Delta = nullptr on purpose: passing the CDO would make this a DELTA
			// export and print empty strings for structs whose members happen to
			// match. The "is it a default" question is answered above instead.
			Prop->ExportText_InContainer(0, ValueStr, Node, /*Delta=*/nullptr, /*Parent=*/nullptr, PPF_None);
			ValueStr.ReplaceInline(TEXT("\r\n"), TEXT(" "));
			ValueStr.ReplaceInline(TEXT("\n"), TEXT(" "));
			if (ValueStr.Len() > BTDumpMaxPropChars)
			{
				ValueStr = ValueStr.Left(BTDumpMaxPropChars) + TEXT("...");
			}
			OutProps += FString::Printf(TEXT("%s=%s "), *Prop->GetName(), *ValueStr);
			++PropsPrinted;
		}

		if (PropsSkipped > 0)
		{
			OutProps += FString::Printf(TEXT("(+%d more props -- run `PFA.DumpBehaviorTree full`) "), PropsSkipped);
		}
	}

	FString IndentFor(int32 Depth)
	{
		return FString::ChrN(FMath::Clamp(Depth, 0, BTDumpMaxDepth) * 2, TCHAR(' '));
	}

	void LogNodeLine(int32 Depth, const TCHAR* Kind, const UBTNode* Node, const FString& Extra, bool bFull,
		TArray<const UBehaviorTree*>& OutSubtrees)
	{
		if (!Node)
		{
			return;
		}

		FString Keys;
		FString Props;
		CensusNode(Node, bFull, Keys, Props, OutSubtrees);

		UE_LOG(LogPrimalFauna, Display,
			PFA_BTDUMP_PREFIX TEXT("d%-2d %s%s %s '%s'%s%s%s%s"),
			Depth, *IndentFor(Depth), Kind,
			*Node->GetClass()->GetName(),
			*Node->GetNodeName(),
			Extra.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" %s"), *Extra),
			Keys.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" keys: %s"), *Keys),
			Props.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("| props: %s"), *Props),
			*FString::Printf(TEXT("| desc=\"%s\""), *FlattenDescription(Node->GetStaticDescription())));
	}

	void DumpDecorators(const TArray<TObjectPtr<UBTDecorator>>& Decorators,
		const TArray<FBTDecoratorLogic>& DecoratorOps, int32 Depth, FBTDumpStats& Stats, bool bFull,
		TArray<const UBehaviorTree*>& OutSubtrees)
	{
		for (const TObjectPtr<UBTDecorator>& Decorator : Decorators)
		{
			if (!Decorator)
			{
				continue;
			}
			++Stats.Nodes;
			++Stats.Decorators;
			const FString Extra = FString::Printf(TEXT("inversed=%s abortMode=%d(0=None 1=LowerPri 2=Self 3=Both)"),
				Decorator->IsInversed() ? TEXT("YES") : TEXT("no"),
				static_cast<int32>(Decorator->GetFlowAbortMode()));
			LogNodeLine(Depth, TEXT("DECO "), Decorator.Get(), Extra, bFull, OutSubtrees);
		}
		if (DecoratorOps.Num() > 0)
		{
			FString Ops;
			for (const FBTDecoratorLogic& Op : DecoratorOps)
			{
				Ops += FString::Printf(TEXT("(op=%d n=%d) "), static_cast<int32>(Op.Operation.GetValue()), Op.Number);
			}
			UE_LOG(LogPrimalFauna, Display,
				PFA_BTDUMP_PREFIX TEXT("d%-2d %sDECO-LOGIC (1=Test 2=And 3=Or 4=Not): %s"),
				Depth, *IndentFor(Depth), *Ops);
		}
	}

	void DumpServices(const TArray<TObjectPtr<UBTService>>& Services, int32 Depth, FBTDumpStats& Stats, bool bFull,
		TArray<const UBehaviorTree*>& OutSubtrees)
	{
		for (const TObjectPtr<UBTService>& Service : Services)
		{
			if (!Service)
			{
				continue;
			}
			++Stats.Nodes;
			++Stats.Services;
			LogNodeLine(Depth, TEXT("SVC  "), Service.Get(), FString(), bFull, OutSubtrees);
		}
	}

	// Depth-first walk. Recursion is bounded by BTDumpMaxDepth and BTDumpMaxNodes,
	// both reported in the trailer so a truncated dump can never be mistaken for a
	// complete one ("a check that cannot fail is not a check", shared gates §2).
	void DumpComposite(const UBTCompositeNode* Composite, int32 Depth, FBTDumpStats& Stats, bool bFull,
		TArray<const UBehaviorTree*>& OutSubtrees)
	{
		if (!Composite)
		{
			return;
		}
		if (Depth > BTDumpMaxDepth)
		{
			Stats.bHitDepthCap = true;
			return;
		}
		if (Stats.Nodes >= BTDumpMaxNodes)
		{
			Stats.bHitNodeCap = true;
			return;
		}

		++Stats.Nodes;
		++Stats.Composites;
		Stats.MaxDepth = FMath::Max(Stats.MaxDepth, Depth);

		LogNodeLine(Depth, TEXT("COMP "), Composite,
			FString::Printf(TEXT("children=%d services=%d"), Composite->Children.Num(), Composite->Services.Num()),
			bFull, OutSubtrees);
		DumpServices(Composite->Services, Depth + 1, Stats, bFull, OutSubtrees);

		for (int32 ChildIdx = 0; ChildIdx < Composite->Children.Num(); ++ChildIdx)
		{
			const FBTCompositeChild& Child = Composite->Children[ChildIdx];
			UE_LOG(LogPrimalFauna, Display,
				PFA_BTDUMP_PREFIX TEXT("d%-2d %schild[%d] of '%s' (decorators=%d)"),
				Depth + 1, *IndentFor(Depth + 1), ChildIdx, *Composite->GetNodeName(), Child.Decorators.Num());

			DumpDecorators(Child.Decorators, Child.DecoratorOps, Depth + 1, Stats, bFull, OutSubtrees);

			if (Child.ChildComposite)
			{
				DumpComposite(Child.ChildComposite.Get(), Depth + 1, Stats, bFull, OutSubtrees);
			}
			else if (Child.ChildTask)
			{
				if (Stats.Nodes >= BTDumpMaxNodes)
				{
					Stats.bHitNodeCap = true;
					return;
				}
				++Stats.Nodes;
				++Stats.Tasks;
				Stats.MaxDepth = FMath::Max(Stats.MaxDepth, Depth + 1);
				LogNodeLine(Depth + 1, TEXT("TASK "), Child.ChildTask.Get(),
					FString::Printf(TEXT("services=%d"), Child.ChildTask->Services.Num()), bFull, OutSubtrees);
				DumpServices(Child.ChildTask->Services, Depth + 2, Stats, bFull, OutSubtrees);
			}
			else
			{
				UE_LOG(LogPrimalFauna, Warning,
					PFA_BTDUMP_PREFIX TEXT("d%-2d %schild[%d] of '%s' has NEITHER a composite NOR a task -- malformed/stripped node."),
					Depth + 1, *IndentFor(Depth + 1), ChildIdx, *Composite->GetNodeName());
			}
		}
	}

	// Blackboard: the asset SCHEMA (every key, its type class, its filter) and,
	// when a live component exists, the CURRENT VALUE next to it -- schema and
	// state in one place, which is the whole point.
	void DumpBlackboard(const UBlackboardData* Asset, const UBlackboardComponent* Live)
	{
		if (!Asset)
		{
			UE_LOG(LogPrimalFauna, Warning,
				PFA_BTDUMP_PREFIX TEXT("BLACKBOARD: the tree carries NO blackboard asset -- no keys exist, so every key we publish is a no-op."));
			return;
		}

		UE_LOG(LogPrimalFauna, Display,
			PFA_BTDUMP_PREFIX TEXT("--- BLACKBOARD asset=%s ownKeys=%d parent=%s liveComponent=%s ---"),
			*Asset->GetName(), Asset->Keys.Num(),
			Asset->Parent ? *Asset->Parent->GetName() : TEXT("none"),
			Live ? TEXT("present") : TEXT("NONE (values unavailable)"));

		auto DumpKeyArray = [Live](const TArray<FBlackboardEntry>& Entries, const FString& Origin)
		{
			for (int32 KeyIdx = 0; KeyIdx < Entries.Num(); ++KeyIdx)
			{
				const FBlackboardEntry& Entry = Entries[KeyIdx];

				// The key type's own configured filter (UBlackboardKeyType_Object::
				// BaseClass, _Class::BaseClass, _Enum::EnumType, ...) -- dumped by
				// reflection so every key type is covered without casting to each.
				// This is what answers "what actor type does the Spawner/roam anchor
				// key even accept" (roaming design §1.2).
				FString Filter;
				if (const UBlackboardKeyType* KeyType = Entry.KeyType.Get())
				{
					const UObject* KeyTypeCdo = KeyType->GetClass()->GetDefaultObject();
					for (TFieldIterator<FProperty> PropIt(KeyType->GetClass()); PropIt; ++PropIt)
					{
						FProperty* Prop = *PropIt;
						FString ValueStr;
						Prop->ExportText_InContainer(0, ValueStr, KeyType, nullptr, nullptr, PPF_None);
						// Print all of the key type's own properties: there are only
						// ever one or two, and which one is the filter differs per
						// type. Suppress the noise case of an untouched default only
						// when it is also empty.
						if (ValueStr.IsEmpty() && KeyTypeCdo && Prop->Identical_InContainer(KeyType, KeyTypeCdo))
						{
							continue;
						}
						Filter += FString::Printf(TEXT("%s=%s "), *Prop->GetName(), *ValueStr);
					}
				}

				FString LiveValue = TEXT("<no live blackboard>");
				if (Live)
				{
					// w47-2 (w47-1 cold review I1): ASK WHETHER THE KEY EXISTS FIRST.
					// DescribeKeyValue returns an empty string for a key the live
					// component does not have, and w47-1 mapped empty to "<empty>" --
					// so "this key is absent from the live blackboard" and "this key
					// exists and is unset" printed IDENTICALLY. That is worst exactly
					// where it matters: on the `LIVE blackboard asset DIFFERS` path and
					// in every `--- SUBTREE ---` block, where the whole point is that the
					// schema being dumped may NOT be the live one, and a reader would
					// wrongly conclude the key exists. GetKeyID is AIMODULE_API
					// (BlackboardComponent.h:58) and already in this module's import
					// table, so this adds no export surface. Read-only.
					if (Live->GetKeyID(Entry.EntryName) == FBlackboard::InvalidKey)
					{
						LiveValue = TEXT("<not in live blackboard>");
					}
					else
					{
						// AIMODULE_API, BlackboardComponent.h:219. Read-only.
						LiveValue = Live->DescribeKeyValue(Entry.EntryName, EBlackboardDescription::OnlyValue);
						if (LiveValue.IsEmpty())
						{
							LiveValue = TEXT("<empty -- key EXISTS, value unset>");
						}
					}
				}

				UE_LOG(LogPrimalFauna, Display,
					PFA_BTDUMP_PREFIX TEXT("  key[%d] %s '%s' type=%s instanceSynced=%s filter:{%s} value=%s"),
					KeyIdx, *Origin, *Entry.EntryName.ToString(),
					Entry.KeyType ? *Entry.KeyType->GetClass()->GetName() : TEXT("NULL-TYPE"),
					Entry.bInstanceSynced ? TEXT("true") : TEXT("false"),
					Filter.IsEmpty() ? TEXT("none") : *Filter,
					*LiveValue);
			}
		};

		// INHERITED keys first: they are just as real to the tree as the asset's own.
		//
		// w47-1 build 1 CAUGHT A WRONG ASSUMPTION -- the first version of this dumped
		// UBlackboardData::ParentKeys (the engine's flattened cache of the parent
		// chain) and failed to compile: that array is inside `#if
		// WITH_EDITORONLY_DATA` (BlackboardData.h:54-58) and does not exist in a
		// Shipping build, even though `Parent` and `Keys` next to it do. So walk the
		// `Parent` chain ourselves, which works in Shipping AND shows the real
		// inheritance order instead of a flattened list. Depth-capped and
		// visited-checked because a malformed asset chain must not hang a diagnostic.
		{
			TArray<const UBlackboardData*> Ancestors;
			TSet<const UBlackboardData*> Seen;
			Seen.Add(Asset);
			for (const UBlackboardData* Ancestor = Asset->Parent.Get();
				Ancestor && Ancestors.Num() < 16 && !Seen.Contains(Ancestor);
				Ancestor = Ancestor->Parent.Get())
			{
				Seen.Add(Ancestor);
				Ancestors.Add(Ancestor);
			}
			// Furthest ancestor first, so the printed order matches key resolution
			// order (a child asset's key overrides a parent's key of the same name).
			for (int32 Idx = Ancestors.Num() - 1; Idx >= 0; --Idx)
			{
				DumpKeyArray(Ancestors[Idx]->Keys,
					FString::Printf(TEXT("inherited<-%s"), *Ancestors[Idx]->GetName()));
			}
		}
		DumpKeyArray(Asset->Keys, TEXT("own"));
	}

	void DumpBehaviorTreeCommand(UWorld* World, const TArray<FString>& Args)
	{
		if (!World)
		{
			UE_LOG(LogPrimalFauna, Warning, TEXT("[PFA] PFA.DumpBehaviorTree aborted: no world context."));
			return;
		}

		const bool bFull = Args.ContainsByPredicate([](const FString& Arg)
		{
			return Arg.Equals(TEXT("full"), ESearchCase::IgnoreCase);
		});

		float DistanceCm = 0.0f;
		bool bFromLook = false;
		AFGCreature* Creature = PFA_FindLookedAtOrNearestCreature(World, DistanceCm, bFromLook);
		if (!Creature)
		{
			UE_LOG(LogPrimalFauna, Display,
				TEXT("[PFA] PFA.DumpBehaviorTree: no creature found (nothing in the look direction, none within 50m fallback)."));
			return;
		}

		UE_LOG(LogPrimalFauna, Display, PFA_BTDUMP_PREFIX TEXT("===== BEGIN (mode=%s) ====="),
			bFull ? TEXT("full -- every property") : TEXT("default -- only properties changed from class defaults; add `full` for all"));

		AController* RawController = Creature->GetController();
		AAIController* AIController = Cast<AAIController>(RawController);
		UE_LOG(LogPrimalFauna, Display,
			PFA_BTDUMP_PREFIX TEXT("creature=%s class=%s dist=%.0fcm(%s) state=%s controller=%s"),
			*Creature->GetName(), *Creature->GetClass()->GetName(), DistanceCm,
			bFromLook ? TEXT("look") : TEXT("nearest-50m"),
			*CreatureStateEnumToString(Creature->GetCurrentBehaviorState()),
			RawController ? *RawController->GetClass()->GetName() : TEXT("unpossessed"));

		// The LIVE tree first (what it is really running), CDO asset as fallback
		// (what it was told to run). Naming which source we walked matters: they
		// disagreeing IS a finding (that is the whole W1.7 instance-BT story).
		const UBehaviorTreeComponent* BTComp =
			AIController ? Cast<UBehaviorTreeComponent>(AIController->GetBrainComponent()) : nullptr;
		const UBehaviorTree* Tree = nullptr;
		const TCHAR* TreeSource = TEXT("none");

		if (BTComp)
		{
			// All three are FORCEINLINE public (BehaviorTreeComponent.h:573-585) --
			// no import surface, and all read-only.
			const UBTNode* ActiveNode = BTComp->GetActiveNode();
			UE_LOG(LogPrimalFauna, Display,
				PFA_BTDUMP_PREFIX TEXT("brain=%s running=%s rootTree=%s currentTree=%s activeNode=%s(%s) activeTasks=\"%s\""),
				*BTComp->GetClass()->GetName(),
				BTComp->IsRunning() ? TEXT("true") : TEXT("false"),
				BTComp->GetRootTree() ? *BTComp->GetRootTree()->GetName() : TEXT("none"),
				BTComp->GetCurrentTree() ? *BTComp->GetCurrentTree()->GetName() : TEXT("none"),
				ActiveNode ? *ActiveNode->GetNodeName() : TEXT("none"),
				ActiveNode ? *ActiveNode->GetClass()->GetName() : TEXT("-"),
				*FlattenDescription(BTComp->DescribeActiveTasks()));
			Tree = BTComp->GetRootTree();
			TreeSource = TEXT("LIVE BehaviorTreeComponent root tree");
		}
		else
		{
			UE_LOG(LogPrimalFauna, Warning,
				PFA_BTDUMP_PREFIX TEXT("no UBehaviorTreeComponent on this controller (brain=%s) -- falling back to the creature's assigned asset; live blackboard values will be unavailable."),
				(AIController && AIController->GetBrainComponent())
					? *AIController->GetBrainComponent()->GetClass()->GetName() : TEXT("null"));
		}

		if (!Tree)
		{
			// FORCEINLINE public getter (FGCreature.h:355) -- no import.
			Tree = Creature->GetBehaviorTreeAsset();
			TreeSource = TEXT("creature mBehaviorTree asset (NOT proven running)");
		}

		if (!Tree)
		{
			UE_LOG(LogPrimalFauna, Warning,
				PFA_BTDUMP_PREFIX TEXT("===== END: no behavior tree at all (no live component tree, no assigned asset) ====="));
			return;
		}

		UE_LOG(LogPrimalFauna, Display, PFA_BTDUMP_PREFIX TEXT("tree=%s path=%s source=%s"),
			*Tree->GetName(), *Tree->GetPathName(), TreeSource);

		const UBlackboardComponent* LiveBlackboard = AIController ? AIController->GetBlackboardComponent() : nullptr;
		DumpBlackboard(Tree->BlackboardAsset.Get(), LiveBlackboard);
		if (LiveBlackboard && LiveBlackboard->GetBlackboardAsset() != Tree->BlackboardAsset)
		{
			// Worth shouting about: our writes go to the LIVE component's schema, so
			// if it differs from the tree's asset, key-name assumptions can be wrong.
			UE_LOG(LogPrimalFauna, Warning,
				PFA_BTDUMP_PREFIX TEXT("LIVE blackboard asset (%s) DIFFERS from the tree's asset (%s) -- dumping the live one too."),
				LiveBlackboard->GetBlackboardAsset() ? *LiveBlackboard->GetBlackboardAsset()->GetName() : TEXT("none"),
				Tree->BlackboardAsset ? *Tree->BlackboardAsset->GetName() : TEXT("none"));
			DumpBlackboard(LiveBlackboard->GetBlackboardAsset(), LiveBlackboard);
		}

		UE_LOG(LogPrimalFauna, Display,
			PFA_BTDUMP_PREFIX TEXT("--- TREE STRUCTURE (d<n> = depth; COMP=composite TASK=task DECO=decorator SVC=service) ---"));

		FBTDumpStats Stats;
		TArray<const UBehaviorTree*> PendingSubtrees;
		TSet<const UBehaviorTree*> VisitedTrees;
		VisitedTrees.Add(Tree);

		if (Tree->RootDecorators.Num() > 0)
		{
			UE_LOG(LogPrimalFauna, Display,
				PFA_BTDUMP_PREFIX TEXT("d0  ROOT-DECORATORS (%d, used when this tree is run as a subtree):"),
				Tree->RootDecorators.Num());
			DumpDecorators(Tree->RootDecorators, Tree->RootDecoratorOps, 0, Stats, bFull, PendingSubtrees);
		}
		DumpComposite(Tree->RootNode.Get(), 0, Stats, bFull, PendingSubtrees);

		// Subtrees (BTTask_RunBehavior's BehaviorAsset). The roam logic could live
		// in one, so stopping at the reference would defeat the purpose.
		int32 SubtreesDumped = 0;
		for (int32 Idx = 0; Idx < PendingSubtrees.Num(); ++Idx)
		{
			const UBehaviorTree* Subtree = PendingSubtrees[Idx];
			if (!Subtree || VisitedTrees.Contains(Subtree))
			{
				continue;
			}
			VisitedTrees.Add(Subtree);
			++SubtreesDumped;
			UE_LOG(LogPrimalFauna, Display,
				PFA_BTDUMP_PREFIX TEXT("--- SUBTREE %s (path=%s) referenced by the tree above ---"),
				*Subtree->GetName(), *Subtree->GetPathName());
			DumpBlackboard(Subtree->BlackboardAsset.Get(), LiveBlackboard);
			DumpComposite(Subtree->RootNode.Get(), 0, Stats, bFull, PendingSubtrees);
		}

		UE_LOG(LogPrimalFauna, Display,
			PFA_BTDUMP_PREFIX TEXT("===== END nodes=%d (composites=%d tasks=%d decorators=%d services=%d) maxDepth=%d subtrees=%d truncated=%s%s ====="),
			Stats.Nodes, Stats.Composites, Stats.Tasks, Stats.Decorators, Stats.Services,
			Stats.MaxDepth, SubtreesDumped,
			(Stats.bHitNodeCap || Stats.bHitDepthCap) ? TEXT("YES") : TEXT("no"),
			Stats.bHitNodeCap ? TEXT(" (NODE CAP HIT -- dump is INCOMPLETE)")
				: (Stats.bHitDepthCap ? TEXT(" (DEPTH CAP HIT -- dump is INCOMPLETE)") : TEXT("")));
	}

	FAutoConsoleCommandWithWorldAndArgs CCmdPFADumpBehaviorTree(
		TEXT("PFA.DumpBehaviorTree"),
		TEXT("Primal Fauna: READ-ONLY structural dump of the behavior tree the looked-at/nearest creature is running -- composites/tasks/decorators/services with class, name, static description and blackboard-key selectors, plus the blackboard schema AND live values. Optional arg `full` prints every node property instead of only the configured ones. Works on vanilla creatures too (diff them)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			DumpBehaviorTreeCommand(World, Args);
		}));

	#undef PFA_BTDUMP_PREFIX
}
