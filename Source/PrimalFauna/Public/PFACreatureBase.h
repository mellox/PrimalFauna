#pragma once

#include "CoreMinimal.h"
#include "Creature/Enemy/FGEnemy.h"
#include "PFACreatureBase.generated.h"

// Doc 04 §1 "Aggression" config values, doc 02 §3 species aggression modes.
// Numeric values are contract -- they mirror the (not-yet-existing) Integer
// 0-2 config property doc 04 describes, so do not renumber without checking
// that doc.
UENUM(BlueprintType)
enum class EPFAAggression : uint8
{
	Passive     = 0 UMETA(DisplayName = "Passive"),
	Territorial = 1 UMETA(DisplayName = "Territorial"),
	Hostile     = 2 UMETA(DisplayName = "Hostile"),
};

// ---------------------------------------------------------------------------
// APFACreatureBase -- shared base for every PrimalFauna species (docs/07
// §2/§3). Derives through AFGEnemy purely for ecosystem convention (AFGEnemy
// is an empty marker, Creature/Enemy/FGEnemy.h:12).
//
// Owns the mechanics every species needs and none should reimplement:
//   - wiring AIControllerClass to APFACreatureController (doc 02 §1: "we own
//     our creatures' controller class")
//   - the doc 02 §3 aggression-mode storage + the mode-0 (Passive) instance
//     flip of AFGCreature::mIsPassiveCreature
//   - a single audited call site for CDO-authored base health (doc 06 §2 C2:
//     "never runtime-multiply")
//   - the sole access point for the two other AFGCreature fields that turned
//     out private, not protected (see the setters' comments below): reached
//     via UE reflection (FindFProperty / SetPropertyValue_InContainer), NOT
//     an AccessTransformer -- this packet's own rule is "zero
//     AccessTransformers (protected access ONLY via self-inheritance)", and
//     self-inheritance genuinely cannot reach a private base member. See
//     PFACreatureBase.cpp for the reflection helpers and the AccessTransformer
//     alternative flagged there for the cold reviewer.
//
// Species subclasses (Species/PFASicklejaw.h, ...) never touch
// AFGCreature's private members directly -- they call this class's protected
// setters instead, which centralizes both the reflection calls AND (if a
// future reviewer prefers the Friend-grant alternative instead) the ONE
// place a Friend grant would need to live, since C++ friendship is not
// inherited -- granting APFACreatureBase friend access to AFGCreature would
// NOT make a further subclass (APFASicklejaw) a friend too.
// ---------------------------------------------------------------------------
UCLASS(Abstract)
class PRIMALFAUNA_API APFACreatureBase : public AFGEnemy
{
	GENERATED_BODY()

public:
	APFACreatureBase(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Doc 02 §3. Server-authoritative runtime mode change (e.g. a future
	// config-driven retarget, doc 04 §3) -- NOT how a species sets its OWN
	// ctor-time default (species ctors write the protected mAggressionMode
	// field directly; see PFASicklejaw.cpp). Mode 0 (Passive) additionally
	// flips the instance's AFGCreature::mIsPassiveCreature flag so native
	// systems AND HostileScaling both treat this ONE creature as passive
	// without touching the class default (doc 07 §3, doc 06 §2 C5).
	//
	// mIsPassiveCreature is PRIVATE on AFGCreature (Creature/FGCreature.h:659)
	// -- verified against the real header, which contradicts docs/07's
	// assumption that every field this packet needs is protected. Written via
	// UE reflection (PFACreatureBase.cpp) rather than a Friend
	// AccessTransformer, per this packet's "zero AccessTransformers" rule.
	UFUNCTION(BlueprintCallable, Category = "PrimalFauna")
	void SetAggressionMode(EPFAAggression NewMode);

	UFUNCTION(BlueprintPure, Category = "PrimalFauna")
	EPFAAggression GetAggressionMode() const { return mAggressionMode; }

	// Territorial-mode radius (doc 02 §3): APFACreatureController gates
	// initiation on distance-to-GetSpawner() <= this value. Per-species;
	// TODO(W1.5): once nests exist, consider deriving this from the owning
	// spawner's GetSpawnRadius() (AFGCreatureSpawner.h:106, public) instead
	// of a fixed constant.
	UFUNCTION(BlueprintPure, Category = "PrimalFauna")
	float GetTerritoryRadiusCm() const { return TerritoryRadiusCm; }

	// BNE override (Creature/FGCreature.h:344). W1.3 scope is a log line only
	// -- real item-drop spawning arrives with W2's loot items (doc 07 §6).
	virtual void SpawnDeathItem_Implementation() override;

	// W1.4 (docs/07 §4 borrowed-BT): PFABehaviorBorrower assigns a discovered
	// vanilla UBehaviorTree onto this species' CDO. mBehaviorTree is PROTECTED
	// on AFGCreature (FGCreature.h:548) -- reachable from this subclass via
	// inheritance (docs/07 §1), so NO Friend grant is needed (contrast the
	// PRIVATE spawn/despawn fields, which do use the grant). Public because the
	// borrower (a free function, not a subclass) calls it on the CDO; the loaded
	// tree is GC-rooted by the world subsystem, not here.
	void SetBorrowedBehaviorTree(class UBehaviorTree* BehaviorTree);

protected:
	// Species ctors call this instead of GetHealthComponent()->SetMaxHealth()
	// directly. GetHealthComponent() is public (FGCharacterBase.h:210) so
	// this isn't an ACCESS requirement -- it's a single audited call site for
	// the "CDO-authored, never runtime-multiplied" invariant (doc 06 §2 C2)
	// and one place to null-guard + log.
	void SetBaseHealth(float NewMaxHealth);

	// Species ctors call this to set both fields at once. Unlike
	// SetBaseHealth, this IS an access requirement: AFGCreature::mSpawnDistance
	// / mDespawnDistance are PRIVATE (Creature/FGCreature.h:691,695 -- verified
	// against the header; docs/07 assumed protected), so this base class is
	// the sole reflection-based access point (PFACreatureBase.cpp -- see its
	// comment for why reflection instead of a Friend AccessTransformer).
	void SetSpawnDespawnDistance(float NewSpawnDistanceCm, float NewDespawnDistanceCm);

	// Per-species territory radius (doc 02 §3). Species ctors set this
	// directly (protected, plain UPROPERTY -- no Friend grant needed, this
	// field lives on OUR class). Default is a generic placeholder so a
	// species that forgets to set it still behaves sanely.
	UPROPERTY(EditDefaultsOnly, Category = "PrimalFauna|Territory")
	float TerritoryRadiusCm;

	// Doc 02 §3 aggression mode, read by APFACreatureController's rule table
	// via GetAggressionMode(). Species ctors set their CDO default directly
	// (plain assignment -- this is OUR field, protected, no Friend needed);
	// SetAggressionMode() is the runtime/authority-gated API for changing a
	// LIVE creature's mode after spawn. Replicated so clients can see it too,
	// though nothing server-authoritative in W1.3 depends on replication --
	// the controller always reads its own server-side copy.
	UPROPERTY(Replicated)
	EPFAAggression mAggressionMode;
};
