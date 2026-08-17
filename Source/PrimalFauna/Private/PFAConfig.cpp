#include "PFAConfig.h"

#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
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
        // Null-guarded: called every RefreshTick for the subsystem's lifetime, so
        // world-teardown edges (no GameInstance / subsystem already gone) must
        // degrade to returning defaults instead of dereferencing null.
        UGameInstance* GameInstance = World->GetGameInstance();
        UConfigManager* ConfigManager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
        if (ConfigManager)
        {
            ConfigManager->FillConfigurationStruct(ConfigId, FDynamicStructInfo{FPFAConfigStruct::StaticStruct(), &ConfigStruct});
        }
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

    UClass* IntegerClass = LoadClass<UConfigPropertyInteger>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyInteger.BP_ConfigPropertyInteger_C"));
    if (!IntegerClass) { IntegerClass = UConfigPropertyInteger::StaticClass(); }

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
    // LAYOUT DECISION (W2.3, 2026-07-24, resolves the scaffold's open note):
    // FLAT with prefixed keys ("SicklejawAggression", not a nested Sicklejaw
    // section). Rationale: flat is the layout PROVEN in HostileScaling and in
    // this file's own shipped General keys; nested sections are unproven in
    // this workspace and the two layouts are a breaking schema change
    // relative to each other. Prefixed DisplayNames ("Sicklejaw: ...",
    // "Factions: ...") give the UI grouping cheaply. Revisit only if the v1
    // roster makes the flat page unwieldy -- BEFORE release (keys freeze at
    // v1; pre-release schema churn is free, SML resets stored values once).
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
        TEXT("Writes verbose [PFA] diagnostic logging (per-entity/decision detail) to the game log. Currently gates PFA.DumpCreature's per-action detail; more diagnostic paths join as creature systems ship. Leave off for clean logs during normal play. Takes effect within ~10s, no reload."));

    // --- W2.3: Sicklejaw + Factions keys (doc 04 §1; flat layout, see the
    // decision note above). Integer property mirrors AddFloat's widget
    // pattern with a spinbox range.
    const auto AddInteger = [&](const TCHAR* Key, int32 Default, int32 Min, int32 Max, const TCHAR* Display, const TCHAR* Tip)
    {
        UConfigPropertyInteger* Prop = NewObject<UConfigPropertyInteger>(Root, IntegerClass, FName(Key));
        Prop->Value = Default;
        // The in-game Reset button does Value = DefaultValue, so DefaultValue
        // must be set explicitly or reset snaps to zero/minimum instead.
        Prop->DefaultValue = Default;
        Prop->DisplayName = FText::FromString(Display);
        Prop->Tooltip = FText::FromString(Tip);
        Prop->bRequiresWorldReload = false;
        if (UCP_Integer* IntWidget = Cast<UCP_Integer>(Prop))
        {
            IntWidget->WidgetType = ECP_IntegerWidgetType::CPI_Spinbox;
            IntWidget->MinValue = Min;
            IntWidget->MaxValue = Max;
        }
        Root->SectionProperties.Add(Key, Prop);
    };

    AddInteger(TEXT("SicklejawAggression"), 2, 0, 2,
        TEXT("Sicklejaw: Aggression"),
        TEXT("0 = Passive (exists, never initiates -- other mods' scaling correctly skips passive creatures). 1 = Territorial (initiates only inside its nest territory or when provoked). 2 = Hostile (initiates on detection; default). Applies to live creatures within ~10s, no reload."));

    AddFloat(TEXT("SicklejawDamageMultiplier"), 1.0f, 0.25f, 2.0f,
        TEXT("Sicklejaw: Damage Multiplier"),
        TEXT("Scales the Sicklejaw's outgoing attack damage at the source (bite base 20). Composes independently with Hostile Scaling's own damage setting (theirs scales incoming damage processing; ours sets the outgoing base). 1.0 = default. Takes effect within ~10s, no reload."));

    AddBool(TEXT("HuntVanillaHostiles"), true,
        TEXT("Factions: Hunt Vanilla Hostiles"),
        TEXT("Predators may fight vanilla hostile creatures (hogs, stingers, spitters) -- the creature-vs-creature spectacle switch. Takes effect within ~10s, no reload."));

    AddBool(TEXT("HuntPassiveFauna"), false,
        TEXT("Factions: Hunt Passive Fauna"),
        TEXT("Allow targeting passive wildlife. Default off -- protects lizard doggos and ambient fauna. Tamed creatures are NEVER targets regardless of this setting (hardcoded). Takes effect within ~10s, no reload."));

    AddBool(TEXT("PackAssist"), true,
        TEXT("Factions: Pack Assist"),
        TEXT("Living idle/default or roaming PrimalFauna packmates from the same nest inherit a member's combat target. Already-active packmates keep their native state. Takes effect within ~10s, no reload."));

    RootSection = Root;
}
