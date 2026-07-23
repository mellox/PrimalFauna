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
            "SML"
        });
    }
}
