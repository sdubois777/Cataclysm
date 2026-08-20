// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveGather.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "EngineUtils.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmInventoryComponent.h"

void FCataclysmSaveGather::VitalsOf(const AActor& Actor, float& OutHealth,
									float& OutMana, float& OutEnergyShield)
{
	OutHealth = 0.0f;
	OutMana = 0.0f;
	OutEnergyShield = 0.0f;

	// THROUGH THE TARGETING HELPER RATHER THAN A CAST, because a player
	// character's ability system belongs to its player state and not to the
	// pawn. `ACataclysmPlayerCharacter::InitAbilityActorInfo` puts it there
	// deliberately, so that it survives death, and a cast to
	// IAbilitySystemInterface on the pawn is what already answers that.
	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(&Actor);
	if (!AbilitySystem)
	{
		return;
	}

	OutHealth = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	OutMana = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetManaAttribute());
	OutEnergyShield = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
}

void FCataclysmSaveGather::MaximumsOf(const AActor& Actor, float& OutMaxHealth,
									  float& OutMaxEnergyShield)
{
	OutMaxHealth = 0.0f;
	OutMaxEnergyShield = 0.0f;

	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(&Actor);
	if (!AbilitySystem)
	{
		return;
	}

	OutMaxHealth = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	OutMaxEnergyShield = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute());
}

FCataclysmSavedCreature FCataclysmSaveGather::CreatureFrom(
	const ACataclysmEnemyCharacter& Creature)
{
	FCataclysmSavedCreature Saved;

	Saved.ArchetypeRow = Creature.ArchetypeRow;
	Saved.RarityStep = Creature.RarityStep;
	Saved.ModifierRows = Creature.ModifierRows;
	Saved.Location = Creature.GetActorLocation();

	// YAW ALONE. A creature does not pitch or roll, and the record says so.
	Saved.Yaw = Creature.GetActorRotation().Yaw;

	float Mana = 0.0f;
	VitalsOf(Creature, Saved.Health, Mana, Saved.EnergyShield);
	MaximumsOf(Creature, Saved.MaxHealth, Saved.MaxEnergyShield);

	return Saved;
}

FCataclysmSavedCharacterPlacement FCataclysmSaveGather::PlacementFrom(
	const ACataclysmPlayerCharacter& Character, const FGuid& CharacterId)
{
	FCataclysmSavedCharacterPlacement Placed;

	Placed.CharacterId = CharacterId;
	Placed.Location = Character.GetActorLocation();
	Placed.Yaw = Character.GetActorRotation().Yaw;

	VitalsOf(Character, Placed.Health, Placed.Mana, Placed.EnergyShield);

	return Placed;
}

FCataclysmSavedGroundItem FCataclysmSaveGather::GroundItemFrom(
	const ACataclysmDroppedItem& Drop)
{
	FCataclysmSavedGroundItem Saved;

	Saved.Item = Drop.Item;
	Saved.Material = Drop.Material;
	Saved.MaterialQuantity = Drop.MaterialQuantity;
	Saved.Location = Drop.GetActorLocation();

	// THE NAME, THE COLOUR, THE RARITY AND THE TIER ARE NOT COPIED. All four are
	// worked out from the two fields above plus the game's data tables, so a
	// record holding them would be holding a second copy of something derived --
	// and an old file would carry the OLD name after an item was renamed in the
	// design workbook. `ACataclysmDroppedItem::DescribeItself` works them out
	// again on the way back in.
	return Saved;
}

ACataclysmPlayerCharacter* FCataclysmSaveGather::CharacterIn(const UWorld& World)
{
	// A const world is iterated through a non-const pointer because
	// TActorIterator takes one. Nothing here writes to the world.
	UWorld* Mutable = const_cast<UWorld*>(&World);
	for (TActorIterator<ACataclysmPlayerCharacter> It(Mutable); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

FCataclysmSavedFloor FCataclysmSaveGather::FloorFrom(const UWorld& World,
													 FName Dungeon, int32 Floor,
													 const FGuid& SoloCharacterId)
{
	FCataclysmSavedFloor Saved;
	Saved.Dungeon = Dungeon;
	Saved.Floor = Floor;

	UWorld* Mutable = const_cast<UWorld*>(&World);

	for (TActorIterator<ACataclysmEnemyCharacter> It(Mutable); It; ++It)
	{
		ACataclysmEnemyCharacter* Creature = *It;
		if (!IsValid(Creature))
		{
			continue;
		}

		// A CORPSE IS NOT A CREATURE THE PLAYER HAS TO FIGHT AGAIN. A creature
		// killed a moment ago is still an actor while its death clip runs, and
		// writing it into the record would put it back on its feet when the
		// floor was restored -- with the health it had when it died, which is
		// zero, so it would die again immediately. Either way the record would
		// be describing a fight that is over.
		if (UCataclysmSkillEffects::IsDead(Creature))
		{
			continue;
		}

		Saved.Creatures.Add(CreatureFrom(*Creature));
	}

	for (TActorIterator<ACataclysmDroppedItem> It(Mutable); It; ++It)
	{
		if (!IsValid(*It))
		{
			continue;
		}
		Saved.GroundItems.Add(GroundItemFrom(**It));
	}

	for (TActorIterator<ACataclysmPlayerCharacter> It(Mutable); It; ++It)
	{
		ACataclysmPlayerCharacter* Character = *It;
		if (!IsValid(Character))
		{
			continue;
		}

		// A DEAD CHARACTER IS NOT GATHERED EITHER, for the same reason. What
		// happens to a run when its character dies is the death penalty, which
		// is issue #315's business and not this one's; a placement at zero
		// health would simply restore the moment of death over and over.
		if (UCataclysmSkillEffects::IsDead(Character))
		{
			continue;
		}

		Saved.Characters.Add(PlacementFrom(*Character, SoloCharacterId));
	}

	return Saved;
}

void FCataclysmSaveGather::CarriedSlotsFrom(
	const UCataclysmInventoryComponent& Inventory,
	TArray<FCataclysmCarriedSlot>& OutSlots)
{
	// THE WHOLE ARRAY INCLUDING THE EMPTY SLOTS, and that is deliberate. The
	// component holds exactly 48 and never resizes, so which slot an item is in
	// is part of what a player arranged. Copying only the filled ones would
	// shuffle everything up against the left edge on the next load.
	OutSlots = Inventory.GetSlots();
}

bool FCataclysmSaveGather::CharacterFrom(const ACataclysmPlayerCharacter& Character,
										 UCataclysmCharacterSave& Record)
{
	const UCataclysmInventoryComponent* Inventory =
		Character.FindComponentByClass<UCataclysmInventoryComponent>();
	if (!Inventory)
	{
		return false;
	}

	CarriedSlotsFrom(*Inventory, Record.CarriedSlots);

	// EVERY OTHER FIELD IS LEFT ALONE RATHER THAN ZEROED. Level, experience,
	// attribute allocation, the passive tree, the 18 equipped slots, the residue
	// and a Solo Self-Found character's private stash have no runtime source at
	// all -- issues #50, #38 and #42 -- so writing a zero over each would turn a
	// record loaded from disk into an empty one every time it was refreshed.
	return true;
}
