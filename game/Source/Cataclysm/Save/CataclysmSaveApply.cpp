// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveApply.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmFloorContents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmDroppedItem.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/**
	 * Every concrete creature class, and the archetype name each one claims.
	 *
	 * BUILT ONCE AND KEPT. Walking every loaded UClass is not something to do
	 * per creature while rebuilding a floor. Nothing loads a new creature class
	 * after start-up in this project -- every one of them is C++ -- so a cache
	 * built on first use is complete.
	 *
	 * SKELETONS AND REINSTANCED CLASSES ARE SKIPPED. The editor keeps
	 * `SKEL_`-prefixed and `REINST_`-prefixed duplicates of a class alive while
	 * it is being recompiled, and each of those claims the same archetype name
	 * as the real one. Without this the map's contents would depend on whether
	 * somebody had just pressed compile.
	 */
	const TMap<FName, TSubclassOf<ACataclysmEnemyCharacter>>& ArchetypeToClass()
	{
		static TMap<FName, TSubclassOf<ACataclysmEnemyCharacter>> Map;
		static bool bBuilt = false;

		if (bBuilt)
		{
			return Map;
		}
		bBuilt = true;

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class || !Class->IsChildOf(ACataclysmEnemyCharacter::StaticClass()))
			{
				continue;
			}

			if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated
										| CLASS_NewerVersionExists))
			{
				continue;
			}

			const FString Name = Class->GetName();
			if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")))
			{
				continue;
			}

			const ACataclysmEnemyCharacter* Default =
				Class->GetDefaultObject<ACataclysmEnemyCharacter>();
			if (!Default)
			{
				continue;
			}

			// FIRST CLAIM WINS AND THE SECOND IS REPORTED. Two classes answering
			// to one archetype name means a saved floor could come back as
			// either of them, which is a coin toss a player would see as a
			// different monster. There is no such pair today and this says so if
			// one is ever made.
			const FName Archetype = Default->ArchetypeRow;
			if (const TSubclassOf<ACataclysmEnemyCharacter>* Already = Map.Find(Archetype))
			{
				UE_LOG(LogCataclysm, Warning,
					TEXT("Two creature classes both claim the archetype '%s': %s and "
						 "%s. A saved floor holding that archetype will come back as "
						 "the first. Give one of them its own row in "
						 "game/Data/EnemyArchetypes.csv."),
					*Archetype.ToString(), *(*Already)->GetName(), *Name);
				continue;
			}

			Map.Add(Archetype, Class);
		}

		return Map;
	}
}

TSubclassOf<ACataclysmEnemyCharacter> FCataclysmSaveApply::ClassForArchetype(
	FName ArchetypeRow)
{
	const TMap<FName, TSubclassOf<ACataclysmEnemyCharacter>>& Map = ArchetypeToClass();
	if (const TSubclassOf<ACataclysmEnemyCharacter>* Found = Map.Find(ArchetypeRow))
	{
		return *Found;
	}
	return nullptr;
}

TArray<FName> FCataclysmSaveApply::KnownArchetypes()
{
	TArray<FName> Names;
	ArchetypeToClass().GetKeys(Names);
	return Names;
}

bool FCataclysmSaveApply::VitalsInto(AActor& Actor, float Health, float Mana,
									 float EnergyShield)
{
	UAbilitySystemComponent* AbilitySystem =
		const_cast<UAbilitySystemComponent*>(UCataclysmTargeting::AbilitySystemOf(&Actor));
	if (!AbilitySystem)
	{
		return false;
	}

	// EACH ATTRIBUTE IS CHECKED BEFORE IT IS WRITTEN. Writing to an attribute
	// the ability system does not hold raises an engine ensure rather than
	// failing quietly, and not every character holds every set -- an enemy has
	// no energy shield unless its archetype gives it one.
	//
	// THE BASE VALUE, NOT A MODIFIER. These are what the character HAS rather
	// than a bonus on top of it, which is the same reason
	// `ACataclysmEnemyCharacter::ApplyStartingAttributes` writes base values
	// rather than applying a gameplay effect.
	bool bWroteAnything = false;

	const FGameplayAttribute HealthAttribute =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	if (AbilitySystem->HasAttributeSetForAttribute(HealthAttribute))
	{
		AbilitySystem->SetNumericAttributeBase(HealthAttribute, Health);
		bWroteAnything = true;
	}

	const FGameplayAttribute ManaAttribute =
		UCataclysmVitalAttributeSet::GetManaAttribute();
	if (AbilitySystem->HasAttributeSetForAttribute(ManaAttribute))
	{
		AbilitySystem->SetNumericAttributeBase(ManaAttribute, Mana);
		bWroteAnything = true;
	}

	const FGameplayAttribute ShieldAttribute =
		UCataclysmVitalAttributeSet::GetEnergyShieldAttribute();
	if (AbilitySystem->HasAttributeSetForAttribute(ShieldAttribute))
	{
		AbilitySystem->SetNumericAttributeBase(ShieldAttribute, EnergyShield);
		bWroteAnything = true;
	}

	return bWroteAnything;
}

bool FCataclysmSaveApply::MaximumsInto(AActor& Actor, float MaxHealth,
									   float MaxEnergyShield)
{
	UAbilitySystemComponent* AbilitySystem =
		const_cast<UAbilitySystemComponent*>(UCataclysmTargeting::AbilitySystemOf(&Actor));
	if (!AbilitySystem)
	{
		return false;
	}

	bool bWroteAnything = false;

	const FGameplayAttribute MaxHealthAttribute =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	if (MaxHealth > 0.0f
		&& AbilitySystem->HasAttributeSetForAttribute(MaxHealthAttribute))
	{
		AbilitySystem->SetNumericAttributeBase(MaxHealthAttribute, MaxHealth);
		bWroteAnything = true;
	}

	const FGameplayAttribute MaxShieldAttribute =
		UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute();
	if (MaxEnergyShield > 0.0f
		&& AbilitySystem->HasAttributeSetForAttribute(MaxShieldAttribute))
	{
		AbilitySystem->SetNumericAttributeBase(MaxShieldAttribute, MaxEnergyShield);
		bWroteAnything = true;
	}

	return bWroteAnything;
}

ACataclysmEnemyCharacter* FCataclysmSaveApply::CreatureInto(
	UWorld& World, const FCataclysmSavedCreature& Saved)
{
	if (Saved.Health <= 0.0f)
	{
		// A RECORD SHOULD NEVER HOLD A DEAD CREATURE, because the gather skips
		// them. One arriving here came from a file somebody edited, and spawning
		// it would put a corpse on the floor that dies again on its first tick.
		UE_LOG(LogCataclysm, Warning,
			TEXT("A saved floor holds a '%s' with %.1f health. A creature with none "
				 "left is not put back."),
			*Saved.ArchetypeRow.ToString(), Saved.Health);
		return nullptr;
	}

	const TSubclassOf<ACataclysmEnemyCharacter> Class =
		ClassForArchetype(Saved.ArchetypeRow);
	if (!Class)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("A saved floor holds the archetype '%s' and no creature class claims "
				 "it, so it is left out of the restored floor rather than replaced "
				 "with a different creature."),
			*Saved.ArchetypeRow.ToString());
		return nullptr;
	}

	FActorSpawnParameters Parameters;
	// ALWAYS SPAWN. The creature stood there before, so the place is one the
	// game already accepted; refusing it now because something else is briefly
	// overlapping would silently thin out a restored floor.
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACataclysmEnemyCharacter* Creature = World.SpawnActor<ACataclysmEnemyCharacter>(
		Class, Saved.Location, FRotator(0.0f, Saved.Yaw, 0.0f), Parameters);
	if (!Creature)
	{
		return nullptr;
	}

	// RARITY AND MODIFIERS BEFORE HEALTH, because rarity is what a creature is
	// and health is what has happened to it. Neither scales the other today --
	// rarity decides what drops and how the creature is labelled -- so the order
	// is for a reader rather than for the arithmetic.
	Creature->SetRarityStep(Saved.RarityStep);
	Creature->ModifierRows = Saved.ModifierRows;

	// AND THE VITALS LAST, AFTER THE CREATURE HAS APPLIED ITS OWN STAT BLOCK.
	// `ApplyStartingAttributes` runs from BeginPlay and writes the archetype's
	// full health, so writing the saved figures before that would be
	// overwritten by it.
	//
	// THE MAXIMUM BEFORE WHAT IS LEFT OF IT, because the vital attribute set
	// clamps health to the maximum. A creature with 137 health left and a
	// class default maximum of 100 came back with 100 until this order was
	// right. These two lines are what keep the damage: a boss that was down to
	// a tenth comes back down to a tenth.
	MaximumsInto(*Creature, Saved.MaxHealth, Saved.MaxEnergyShield);
	VitalsInto(*Creature, Saved.Health, /*Mana=*/0.0f, Saved.EnergyShield);

	return Creature;
}

ACataclysmDroppedItem* FCataclysmSaveApply::GroundItemInto(
	UWorld& World, const FCataclysmSavedGroundItem& Saved)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACataclysmDroppedItem* Drop = World.SpawnActor<ACataclysmDroppedItem>(
		ACataclysmDroppedItem::StaticClass(), Saved.Location, FRotator::ZeroRotator,
		Parameters);
	if (!Drop)
	{
		return nullptr;
	}

	Drop->Item = Saved.Item;
	Drop->Material = Saved.Material;
	Drop->MaterialQuantity = Saved.MaterialQuantity;

	// THE NAME, THE COLOUR, THE RARITY AND THE TIER ARE WORKED OUT AGAIN rather
	// than restored, because all four follow from what the drop IS. A record
	// holding them would hand back the name an item had when it was dropped,
	// which is the wrong one after a rename in the design workbook.
	Drop->DescribeItself();

	return Drop;
}

bool FCataclysmSaveApply::PlacementInto(
	ACataclysmPlayerCharacter& Character,
	const FCataclysmSavedCharacterPlacement& Saved)
{
	// TELEPORTED RATHER THAN MOVED. `SetActorLocation` with sweeping on would
	// drag the character along the floor from wherever it happens to be
	// standing, and stop it against the first thing in the way.
	Character.SetActorLocationAndRotation(Saved.Location,
										  FRotator(0.0f, Saved.Yaw, 0.0f),
										  /*bSweep=*/false, nullptr,
										  ETeleportType::TeleportPhysics);

	return VitalsInto(Character, Saved.Health, Saved.Mana, Saved.EnergyShield);
}

FCataclysmFloorRestored FCataclysmSaveApply::FloorInto(
	UWorld& World, const FCataclysmSavedFloor& Saved)
{
	FCataclysmFloorRestored Restored;

	// EMPTIED BY THE DUNGEON RATHER THAN HERE, since issue #1176. What a floor
	// holds is the dungeon's question, and going down a flight of stairs asks it
	// too -- `UCataclysmFloorContents` is where the one answer now lives.
	Restored.Removed = UCataclysmFloorContents::ClearTheFloor(World);

	for (const FCataclysmSavedCreature& Creature : Saved.Creatures)
	{
		if (CreatureInto(World, Creature))
		{
			++Restored.Creatures;
		}
		else
		{
			++Restored.Refused;
		}
	}

	for (const FCataclysmSavedGroundItem& Drop : Saved.GroundItems)
	{
		if (GroundItemInto(World, Drop))
		{
			++Restored.GroundItems;
		}
		else
		{
			++Restored.Refused;
		}
	}

	// THE CHARACTER IS MOVED, NOT SPAWNED. It is already in the world -- it is
	// the thing the player is driving -- so a placement puts it back where it
	// was rather than making a second one.
	//
	// SOLO PLAY IS WHAT EXISTS, so the first placement is applied to the one
	// character. Co-operative play will have to match each placement to the
	// character record its identifier names, which is why the record holds an
	// array and an identifier rather than one position.
	for (TActorIterator<ACataclysmPlayerCharacter> It(&World); It; ++It)
	{
		ACataclysmPlayerCharacter* Character = *It;
		if (!IsValid(Character) || Restored.Characters >= Saved.Characters.Num())
		{
			continue;
		}

		if (PlacementInto(*Character, Saved.Characters[Restored.Characters]))
		{
			++Restored.Characters;
		}
		else
		{
			++Restored.Refused;
		}
	}

	return Restored;
}
