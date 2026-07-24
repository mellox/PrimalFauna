#include "PFAFactionConfig.h"
#include "PFAConfig.h"
#include "PFACreatureController.h"

namespace
{
	// Defaults mirror the controller's doc-04 constexpr anchors -- the
	// SicklejawControllerPolicy self-test pins THOSE to the doc values and
	// this file initializes FROM them, so there is one source of truth and
	// a doc drift breaks the self-test, not gameplay.
	bool GHuntVanillaHostiles = APFACreatureController::HuntVanillaHostilesDefault;
	bool GHuntPassiveFauna = APFACreatureController::HuntPassiveFaunaDefault;
	bool GPackAssist = true;               // doc 04 §1 Factions default
	float GSicklejawDamageMultiplier = 1.0f;
}

void PFAFactionConfig::Push(const FPFAConfigStruct& Config)
{
	GHuntVanillaHostiles = Config.HuntVanillaHostiles;
	GHuntPassiveFauna = Config.HuntPassiveFauna;
	GPackAssist = Config.PackAssist;
	GSicklejawDamageMultiplier = FMath::Clamp(Config.SicklejawDamageMultiplier, 0.25f, 2.0f);
}

bool PFAFactionConfig::HuntVanillaHostiles()
{
	return GHuntVanillaHostiles;
}

bool PFAFactionConfig::HuntPassiveFauna()
{
	return GHuntPassiveFauna;
}

bool PFAFactionConfig::PackAssist()
{
	return GPackAssist;
}

float PFAFactionConfig::SicklejawDamageMultiplier()
{
	return GSicklejawDamageMultiplier;
}
