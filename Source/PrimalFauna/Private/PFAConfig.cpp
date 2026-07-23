#include "PFAConfig.h"

#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

FPFAConfigStruct FPFAConfigStruct::GetActiveConfig(UObject* WorldContext)
{
    FPFAConfigStruct ConfigStruct{};
    const FConfigId ConfigId{"PrimalFauna", ""};
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
    {
        UConfigManager* ConfigManager = World->GetGameInstance()->GetSubsystem<UConfigManager>();
        ConfigManager->FillConfigurationStruct(ConfigId, FDynamicStructInfo{FPFAConfigStruct::StaticStruct(), &ConfigStruct});
    }
    return ConfigStruct;
}

UPFAConfig::UPFAConfig()
{
    ConfigId = FConfigId{"PrimalFauna", ""};
    DisplayName = FText::FromString(TEXT("Primal Fauna"));
    Description = FText::FromString(TEXT("Original creature roster for Satisfactory. Works with HostileScaling automatically; no dependency either way. More setting sections (per-species, Factions, Sandwyrm) arrive as those systems ship."));
}

void UPFAConfig::PostInitProperties()
{
    Super::PostInitProperties();

    // Only the class default object's tree is used by SML (it acts as the
    // template the config manager duplicates into its live value holder).
    if (RootSection != nullptr || !HasAnyFlags(RF_ClassDefaultObject))
    {
        return;
    }

    // The raw C++ config property classes serialize fine but provide no
    // editor widget, which leaves the in-game Mods menu panel empty. SML's
    // Blueprint subclasses wire up the actual widgets, so instantiate those,
    // falling back to the native classes if they cannot be loaded.
    UClass* SectionClass = LoadClass<UConfigPropertySection>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection.BP_ConfigPropertySection_C"));
    if (!SectionClass) { SectionClass = UConfigPropertySection::StaticClass(); }

    UClass* BoolClass = LoadClass<UConfigPropertyBool>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyBool.BP_ConfigPropertyBool_C"));
    if (!BoolClass) { BoolClass = UConfigPropertyBool::StaticClass(); }

    UClass* FloatClass = LoadClass<UConfigPropertyFloat>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyFloat.BP_ConfigPropertyFloat_C"));
    if (!FloatClass) { FloatClass = UConfigPropertyFloat::StaticClass(); }

    UConfigPropertySection* Root = NewObject<UConfigPropertySection>(this, SectionClass, TEXT("RootSection"));
    if (UCP_Section* RootWidget = Cast<UCP_Section>(Root))
    {
        RootWidget->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
        RootWidget->HasHeader = false;
    }
    // This mod applies config changes live (the subsystem's 10s refresh
    // tick re-reads every property), so allow in-game (pause menu) edits.
    Root->bRequiresWorldReload = false;

    // Properties are outer'd to the section (NOT to this config object) so
    // that MarkDirty's Outer-chain walk reaches the save handler.
    //
    // NOTE for whoever adds the next config section (per-species / Factions
    // / Sandwyrm, docs/04-config-and-ui.md §1): this packet keeps every
    // property directly under Root -- the flat layout proven compiling and
    // working in HostileScalingConfig.cpp -- rather than nesting a second
    // UConfigPropertySection (e.g. "General") inside Root. SML's reflection
    // does support nested sections (UConfigPropertySection::
    // FillConfigStruct_Implementation resolves through
    // FReflectedObject::GetStructProperty, which just needs a matching
    // nested USTRUCT field on the destination struct), but that path is
    // UNPROVEN in any mod in this workspace and untestable by the agent
    // that wrote this scaffold (no build/in-game access in this packet).
    // Decide flat-with-prefixed-keys (e.g. "SicklejawEnabled") vs true
    // nested sections deliberately before extending this file -- the two
    // are a breaking schema change relative to each other (SML discards
    // stored values once when a config's property shape changes).
    const auto AddBool = [&](const TCHAR* Key, bool Default, const TCHAR* Display, const TCHAR* Tip)
    {
        UConfigPropertyBool* Prop = NewObject<UConfigPropertyBool>(Root, BoolClass, FName(Key));
        Prop->Value = Default;
        // The in-game Reset button does Value = DefaultValue, so DefaultValue
        // must be set explicitly or reset snaps to zero/minimum instead.
        Prop->DefaultValue = Default;
        Prop->DisplayName = FText::FromString(Display);
        Prop->Tooltip = FText::FromString(Tip);
        Prop->bRequiresWorldReload = false;
        Root->SectionProperties.Add(Key, Prop);
    };

    const auto AddFloat = [&](const TCHAR* Key, float Default, float Min, float Max, const TCHAR* Display, const TCHAR* Tip)
    {
        UConfigPropertyFloat* Prop = NewObject<UConfigPropertyFloat>(Root, FloatClass, FName(Key));
        Prop->Value = Default;
        // The in-game Reset button does Value = DefaultValue, so DefaultValue
        // must be set explicitly or reset snaps to zero/minimum instead.
        Prop->DefaultValue = Default;
        Prop->DisplayName = FText::FromString(Display);
        Prop->Tooltip = FText::FromString(Tip);
        Prop->bRequiresWorldReload = false;
        if (UCP_Float* FloatWidget = Cast<UCP_Float>(Prop))
        {
            FloatWidget->WidgetType = ECP_FloatWidgetType::CPF_Spinbox;
            FloatWidget->MinValue = Min;
            FloatWidget->MaxValue = Max;
        }
        Root->SectionProperties.Add(Key, Prop);
    };

    AddBool(TEXT("Enabled"), true,
        TEXT("Enabled"),
        TEXT("Master switch. Off = no nests injected, existing Primal Fauna creatures despawned on next refresh, worm events suppressed. Safe to toggle mid-save. Takes effect within ~10s, no reload."));

    AddFloat(TEXT("NestDensity"), 1.0f, 0.25f, 3.0f,
        TEXT("Nest Density"),
        TEXT("Scales how many territory nests materialize. Does NOT change per-spawner creature counts -- that is Hostile Scaling's setting. 1.0 = default. Takes effect within ~10s, no reload."));

    AddBool(TEXT("EnableExperimentalFeatures"), false,
        TEXT("Enable Experimental Features"),
        TEXT("Reserved for early, unproven features. No experimental features are implemented yet in this build, so this switch currently has no effect."));

    AddBool(TEXT("EnableDiagnostics"), false,
        TEXT("Enable Diagnostics"),
        TEXT("Writes verbose [PFA] diagnostic logging (per-entity/decision detail) to the game log. Leave off for clean logs during normal play. Takes effect within ~10s, no reload. (This scaffold build has no gated diagnostics yet - the switch becomes active as creature systems ship.)"));

    RootSection = Root;
}
