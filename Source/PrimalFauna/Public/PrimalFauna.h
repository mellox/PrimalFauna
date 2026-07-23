#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPrimalFauna, Log, All);

// PrimalFauna owns every class its gameplay touches -- our own
// AFGCreature / AFGCreatureController / AFGCreatureMovementComponent
// subclasses -- instead of hooking vanilla classes (contrast HostileScaling,
// which hooks vanilla creatures because it re-tunes THEM). See
// docs/07-technical-architecture.md §1 ("own every class you change").
// Consequence: this module registers ZERO SUBSCRIBE_METHOD /
// SUBSCRIBE_UOBJECT_METHOD hooks. If a future packet proposes one, that is
// an architecture deviation -- escalate before adding it.
class FPrimalFaunaModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override;
    virtual bool IsGameModule() const override { return true; }
};
