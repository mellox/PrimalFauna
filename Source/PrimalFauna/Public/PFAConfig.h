#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "Configuration/ConfigManager.h"
#include "PFAConfig.generated.h"

// Plain struct mirror of the configuration. Field names MUST match the
// section property keys registered in UPFAConfig exactly --
// UConfigManager::FillConfigurationStruct maps them by name.
//
// Only the "General" section (docs/04-config-and-ui.md §1) ships in this
// packet (W0.1). Per-species / Factions / Sandwyrm sections arrive in later
// packets -- see the flat-vs-nested-section note in PFAConfig.cpp before
// extending this struct.
USTRUCT(BlueprintType)
struct PRIMALFAUNA_API FPFAConfigStruct
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    bool Enabled{true};

    UPROPERTY(BlueprintReadWrite)
    float NestDensity{1.0f};

    UPROPERTY(BlueprintReadWrite)
    bool EnableExperimentalFeatures{false};

    UPROPERTY(BlueprintReadWrite)
    bool EnableDiagnostics{false};

    // Retrieves the active configuration values for this mod.
    static FPFAConfigStruct GetActiveConfig(UObject* WorldContext);
};

// C++-defined SML mod configuration. Registered by the root game instance
// module; SML generates the in-game settings UI and persists values to
// FactoryGame/Configs/PrimalFauna.cfg.
UCLASS()
class PRIMALFAUNA_API UPFAConfig : public UModConfiguration
{
    GENERATED_BODY()

public:
    UPFAConfig();

    // The property tree is built here rather than in the constructor so each
    // property can be outer'd to the root section. SML's dirty/save chain
    // walks property Outers, so the chain must be property -> section ->
    // (duplicated into) root value holder -> config manager. Properties
    // created as constructor subobjects are outer'd to the config object,
    // which silently breaks saving of user edits.
    virtual void PostInitProperties() override;
};
