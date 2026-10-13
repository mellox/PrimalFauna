using UnrealBuildTool;

public class PrimalFauna : ModuleRules
{
    public PrimalFauna(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "FactoryGame",
            "SML",
            // APFASicklejaw's ctor directly constructs UAISenseConfig_Sight /
            // UAISenseConfig_Hearing (doc 02 §5 perception tuning). FactoryGame
            // already depends on AIModule publicly (FactoryGame.Build.cs:37), so
            // this is already transitively available -- listed explicitly per
            // IWYU discipline since we #include its headers directly. AIModule
            // also provides UBehaviorTree (W1.4 borrowed-BT, PFABehaviorBorrower).
            "AIModule",
            "NavigationSystem",
            // W1.4: PFABehaviorBorrower queries IAssetRegistry to discover a
            // vanilla UBehaviorTree at world load (no hostile-BT path was ever
            // censused). Runtime module, present in cooked/Shipping builds.
            "AssetRegistry",
            // Wwise. Satisfactory's audio is Wwise (UAkAudioEvent), not UE audio --
            // UGameplayStatics::PlaySound* does nothing here. BOTH this and the
            // "Wwise" entry in PrimalFauna.uplugin are required: without the
            // .uplugin entry the cook dies with
            // "Plugin does not list plugin 'Wwise' as a dependency" ->
            // CouldNotAutoloadMatchingModule, exit 6.
            "AkAudio",
            // w31-2: UPFAAnimInstance's graph includes FAnimNode_ApplyAdditive
            // (AnimNodes/AnimNode_ApplyAdditive.h), which lives in
            // AnimGraphRuntime rather than Engine. FactoryGame already depends
            // on AnimGraphRuntime publicly (FactoryGame.Build.cs), so like
            // AIModule above this is already transitively available and is
            // listed explicitly per IWYU discipline since we #include its
            // header directly. Proven present in the shipping game: the module
            // ships as Engine\Binaries\Win64\FactoryGameSteam-AnimGraphRuntime-
            // Win64-Shipping.dll.
            //
            // The mod imports exactly ONE symbol from it -- the
            // FAnimNode_ApplyAdditive constructor. That is not a sign the node
            // is barely used; it is because the constructor is defined
            // out-of-line inside AnimGraphRuntime, so MSVC emits the node's
            // vtable there too and our module needs nothing else. Contrast
            // FAnimNode_SequencePlayer_Standalone, whose constructor is inline
            // in our translation unit: its vtable is emitted HERE and pulls in
            // ~10 ENGINE_API slot targets. Both verified present by
            // tools/check_imports.ps1 -- do not cite either as a general rule
            // without re-checking the import table.
            "AnimGraphRuntime"
        });
    }
}
