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
            // IWYU discipline since we #include its headers directly.
            "AIModule"
        });
    }
}
