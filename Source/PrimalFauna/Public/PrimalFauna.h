#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPrimalFauna, Log, All);

// PrimalFauna owns every class its gameplay touches -- our own
// AFGCreature / AFGCreatureController / AFGCreatureMovementComponent
// subclasses -- instead of hooking vanilla classes (contrast HostileScaling,
// which hooks vanilla creatures because it re-tunes THEM). See
// docs/07-technical-architecture.md §1 ("own every class you change").
// Consequence: hooks on vanilla code are an architecture deviation --
// escalate before adding one. ONE deviation exists since w59-1 (2026-09-21):
// PFAInstallPopulationSpawnGuards (PFAPopulationSpawner.cpp) hooks three
// AFGCreatureSpawner entry points, game builds only (#if !WITH_EDITOR).
class FPrimalFaunaModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override;
    virtual bool IsGameModule() const override { return true; }
};
