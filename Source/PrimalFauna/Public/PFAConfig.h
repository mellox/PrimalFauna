#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "Configuration/ConfigManager.h"
#include "PFAConfig.generated.h"

// Plain struct mirror of the configuration. Field names MUST match the
// section property keys registered in UPFAConfig exactly --
// UConfigManager::FillConfigurationStruct maps them by name.
//
// Shipped sections (docs/04-config-and-ui.md §1): General (W0.1) + Sicklejaw
// + Factions (W2.3, FLAT prefixed keys -- decision note in PFAConfig.cpp).
// Sandwyrm keys + per-species Enabled (despawn machinery) arrive with their
// systems -- pre-release schema churn is free (SML resets stored values once
// on shape change); keys freeze at v1 release.
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

    // --- W2.3: Sicklejaw section (flat keys; doc 04 §1 per-species table) ---

    // 0 = Passive, 1 = Territorial, 2 = Hostile (doc 02 §3).
    UPROPERTY(BlueprintReadWrite)
    int32 SicklejawAggression{2};

    UPROPERTY(BlueprintReadWrite)
    float SicklejawDamageMultiplier{1.0f};

    // --- W2.3: Factions section (doc 04 §1 / doc 02) ---

    UPROPERTY(BlueprintReadWrite)
    bool HuntVanillaHostiles{true};

    UPROPERTY(BlueprintReadWrite)
    bool HuntPassiveFauna{false};

    UPROPERTY(BlueprintReadWrite)
    bool PackAssist{true};

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
