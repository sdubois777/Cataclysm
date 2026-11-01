// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveApply.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmMovement.h"
#include "AbilitySystem/CataclysmPotions.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmFloorContents.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Player/CataclysmPlayerState.h"
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

			// NOT A FLOOR SOURCE -- an Eternal Chorus's source, a Necrotic Bloom's flower -- which names
			// no archetype row, as the base class -- the sandbox's training dummy -- names none. Without
			// this they would all claim that empty name and a saved dummy could come back as a source. A
			// source is never saved anyway: it is raised by a rule, and `FCataclysmSaveGather::FloorFrom`
			// skips those. Issues #1820 and #41.
			if (Class->IsChildOf(ACataclysmFloorSourceCharacter::StaticClass()))
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
		// AND HELD TO WHAT IS NOT RESERVED, for a save written before the
		// reservation grew. Issue #1833, ruled 2026-09-30.
		if (UCataclysmAbilitySystemComponent* Holding =
				Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
		{
			Holding->HoldHealthToUnreserved();
		}
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
	// and health is what has happened to it. THE ORDER IS LOAD-BEARING: rarity is
	// part of the stat block since issue #848, so `SetRarityStep` re-applies it
	// through `ApplyStartingAttributes` and refills health to the new maximum.
	// Written after it, the saved figures below are what stand. Issue #1958; this
	// said until then that neither scaled the other.
	Creature->SetRarityStep(Saved.RarityStep);
	Creature->ModifierRows = Saved.ModifierRows;
	// A RISEN CREATURE STILL PAYS NOTHING WHEN IT DIES AGAIN, the owner's decision of 2026-09-17.
	Creature->bRisenFromTheDead = Saved.bRisenFromTheDead;

	// ITS ATTACK DAMAGE, which nothing put back until #753's Group 2. BEFORE THE MAXIMUMS AND VITALS: `SetAttackDamage`
	// runs `ApplyStartingAttributes`, which rewrites the maximum and the health from the creature's figures, and the
	// two writes below put the saved ones back over that.
	if (Saved.StartingHealth > 0.0f)
	{
		Creature->SetHealth(Saved.StartingHealth);
	}
	if (Saved.Armour >= 0.0f)
	{
		Creature->SetArmour(Saved.Armour);
	}
	if (Saved.AttackDamage > 0.0f)
	{
		Creature->SetAttackDamage(Saved.AttackDamage);
	}
	if (!FMath::IsNearlyEqual(Saved.PlacedDamageMultiplier, 1.0f))
	{
		Creature->SetPlacedDamageMultiplier(Saved.PlacedDamageMultiplier);
	}
	// AND HOW FAR IT SEES AND WHICH OF ITS ROWS THE FLOOR GAVE, each its own default when not saved. Issue #753, Group 3a.
	Creature->SightRadiusMultiplier = Saved.SightRadiusMultiplier;
	Creature->FloorRealityRow = Saved.FloorRealityRow;

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
	// THE BODY OF THIS FUNCTION MOVED, and this is now the save record's half of
	// it. Restoring a floor and dropping an item out of the bag both need to put
	// one KNOWN item on the ground, which nothing else in the project does --
	// every other path rolls a new item. Two copies of that, one of them private
	// to the save system where no other caller could reach it, is what issues
	// #1176 and #1190 both ran into. UCataclysmDropSpawner::PutOnTheFloor is the
	// one copy; what is left here is unpacking the record.
	ACataclysmDroppedItem* Drop = UCataclysmDropSpawner::PutOnTheFloor(
		&World, Saved.Location, Saved.Item, Saved.Material,
		Saved.MaterialQuantity);
	// AND WHETHER A RAISED CREATURE DROPPED IT, so Trick or Treat rolls for it no more after a load than before.
	// Issue #753, Group 3d-4a.
	if (Drop)
	{
		Drop->bDroppedByARaisedCreature = Saved.bDroppedByARaisedCreature; // A load's: the raised creature's mark.
	}
	return Drop;
}

bool FCataclysmSaveApply::PlacementInto(
	ACataclysmPlayerCharacter& Character,
	const FCataclysmSavedCharacterPlacement& Saved)
{
	// TELEPORTED RATHER THAN MOVED. `SetActorLocation` with sweeping on would
	// drag the character along the floor from wherever it happens to be
	// standing, and stop it against the first thing in the way.
	const FVector Was = Character.GetActorLocation();
	Character.SetActorLocationAndRotation(Saved.Location,
										  FRotator(0.0f, Saved.Yaw, 0.0f),
										  /*bSweep=*/false, nullptr,
										  ETeleportType::TeleportPhysics);

	// AN INSTANT RELOCATION, not ground walked. Issue #2388.
	UCataclysmMovement::NoteRelocatedInstantly(&Character, Was);

	// THE CLASS RESOURCE, BEFORE THE VITALS for the potions' reason below. Its write is clamped to the maximum less
	// the Fervour the thralls hold back (`PreAttributeChange`); the loader re-links the thralls first, so the clamp
	// meets the reserve the save met. Issue #753, Group 2.
	if (UAbilitySystemComponent* Resource = UCataclysmTargeting::AbilitySystemOf(&Character))
	{
		if (Resource->HasAttributeSetForAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()))
		{
			Resource->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
											  Saved.ClassResource);
		}
	}

	// THE POTIONS BEFORE THE VITALS, because a placement at no health dies in `VitalsInto`, and the death's own
	// write must find the saved charges. A list of the wrong length was not saved and is left alone. Issue #753.
	if (Saved.PotionCharges.Num() == UCataclysmPotions::SlotCount)
	{
		if (UCataclysmAbilitySystemComponent* Holder = Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(&Character)))
		{
			for (int32 Slot = 0; Slot < UCataclysmPotions::SlotCount; ++Slot)
			{
				Holder->SetPotionCharges(Slot, Saved.PotionCharges[Slot]);
			}
			Holder->SetPotionsDrunk(Saved.PotionsDrunk);
		}
	}

	return VitalsInto(Character, Saved.Health, Saved.Mana, Saved.EnergyShield);
}

FCataclysmFloorRestored FCataclysmSaveApply::DungeonFloorInto(
	ACataclysmDungeonGameMode& Dungeon, const FCataclysmSavedFloor& Saved)
{
	FCataclysmFloorRestored Restored;
	UWorld* World = Dungeon.GetWorld();
	if (!World)
	{
		return Restored;
	}

	// EVERY CREATURE BUT THE ONES THE FLOOR'S RULES RAISED, AND EVERY DROP. The rebuilt floor placed those fresh, and
	// the save holds none of them (`FCataclysmSaveGather::FloorFrom` skips them), so clearing them would leave the
	// floor without them. Ruled 2026-10-30.
	Restored.Removed = UCataclysmFloorContents::ClearTheFloor(*World, /*bCarryFollowers=*/false,
															  /*bKeepTheRulesCreatures=*/true);
	Dungeon.FloorEnemies.RemoveAll([](const TObjectPtr<ACataclysmEnemyCharacter>& Enemy)
	{
		return !IsValid(Enemy);
	});

	// EACH ONE PUT BACK IS THE FLOOR'S, in `FloorEnemies`, which is what the stairs, the clear and a kill's payout
	// count. One spawned beside the list would let the stairs open with it standing.
	for (const FCataclysmSavedCreature& Creature : Saved.Creatures)
	{
		if (ACataclysmEnemyCharacter* Back = CreatureInto(*World, Creature))
		{
			// A THRALL IS THE PLAYER'S, not one of the floor's own: kept out of the list the stairs count.
			if (!Creature.bCommandedByThePlayer)
			{
				Dungeon.FloorEnemies.Add(Back);
			}
			Restored.Spawned.Emplace(Back, static_cast<int32>(&Creature - Saved.Creatures.GetData()));
			++Restored.Creatures;
		}
		else
		{
			++Restored.Refused;
		}
	}

	for (const FCataclysmSavedGroundItem& Drop : Saved.GroundItems)
	{
		if (GroundItemInto(*World, Drop))
		{
			++Restored.GroundItems;
		}
		else
		{
			++Restored.Refused;
		}
	}

	// NO CHARACTER IS PLACED HERE. The loader places the character once, last.
	return Restored;
}

bool FCataclysmSaveApply::ThrallInto(ACataclysmEnemyCharacter& Creature, const FCataclysmSavedCreature& Saved,
									ACataclysmPlayerCharacter& Player)
{
	if (!Saved.bCommandedByThePlayer)
	{
		return false;
	}
	// SUBJUGATE'S TWO WRITES FOR OWNER AND SIDE, in its order: `UCataclysmTeams::TeamOf` walks the owner chain.
	Creature.SetOwner(&Player);
	Creature.SetGenericTeamId(UCataclysmTeams::TeamOf(&Player));
	if (const UWorld* World = Creature.GetWorld())
	{
		Creature.CommandedSinceSeconds = World->GetTimeSeconds();
	}
	Creature.bIsSecondSelf = Saved.bSecondSelf;
	Creature.ReservedFervour = Saved.ReservedFervour;

	// THE MULTIPLIER, SO A LATER `ApplyStartingAttributes` KEEPS IT; that call rewrites the maximum and the health,
	// so the saved figures are written back after it.
	Creature.SetCommanderHealthMultiplier(Saved.CommanderHealthMultiplier);
	MaximumsInto(Creature, Saved.MaxHealth, Saved.MaxEnergyShield);
	float Mana = 0.0f;
	VitalsInto(Creature, Saved.Health, Mana, Saved.EnergyShield);
	return true;
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
		if (ACataclysmEnemyCharacter* Back = CreatureInto(World, Creature))
		{
			Restored.Spawned.Emplace(Back, static_cast<int32>(&Creature - Saved.Creatures.GetData()));
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

bool FCataclysmSaveApply::RunInto(const UCataclysmRunSave& Record, UCataclysmEmpireRun& Run, FString& OutWhy)
{
	// REFUSED BEFORE ANYTHING IS TOUCHED. A record that cannot be put back whole
	// is not put back in part: the run a caller handed over stays as it was.
	if (Record.Cities.Num() != UCataclysmEmpireMap::CityCount)
	{
		OutWhy = FString::Printf(TEXT("the record holds %d cities and the map has %d"),
								 Record.Cities.Num(), UCataclysmEmpireMap::CityCount);
		return false;
	}
	for (int32 Index = 0; Index < Record.Cities.Num(); ++Index)
	{
		if (Record.Cities[Index].CityId != Index)
		{
			OutWhy = FString::Printf(TEXT("the record's city at %d says it is city %d"),
									 Index, Record.Cities[Index].CityId);
			return false;
		}
	}
	if (!UCataclysmEmpireRun::DungeonsAgreeWithTimers(Record.Dungeons, Record.DungeonTimers, OutWhy))
	{
		return false;
	}

	// THE POOL, AS `UCataclysmGameInstance::BeginEmpireRun` FILLS IT, before
	// `Begin`, which does not clear it. The empire module cannot load the table.
	Run.ModifierPool = UCataclysmDungeonModifierTable::LoadPool();

	// THE RUN'S OWN OBJECTS, built by `Begin` with the record's mode, rung and
	// tier. Everything it draws from the seed of 0 is written over below.
	Run.Begin(0, Record.SurgeMode, Record.SurgeLethalityRung, Record.DifficultyTier);

	// THE CITIES: each saved field onto the rebuilt map, by id.
	Run.Map->UpgradeSlots = Record.CityUpgradeSlots;
	for (const FCataclysmCity& Saved : Record.Cities)
	{
		FCataclysmCity* City = Run.Map->FindMutable(Saved.CityId);
		City->MaxDefence = Saved.MaxDefence;
		City->MaxPopulation = Saved.MaxPopulation;
		City->Defence = Saved.Defence;
		City->Population = Saved.Population;
		City->bFallen = Saved.bFallen;
		City->bDoomed = Saved.bDoomed;
		City->bErased = Saved.bErased;
		City->VoidStacks = Saved.VoidStacks;
		City->LeftEdict = Saved.LeftEdict;
		City->LeftEdictUntilDay = Saved.LeftEdictUntilDay;
		City->Upgrades = Saved.Upgrades;
	}

	// THE CLOCK, THE DUNGEONS AND THE DAYS OWED.
	Run.Clock->Day = Record.Day;
	Run.Clock->PartialDay = Record.PartialDay;
	Run.Clock->CurrentDungeonId = Record.CurrentDungeonId;
	Run.Clock->Timers = Record.DungeonTimers;
	Run.Dungeons = Record.Dungeons;
	Run.OwedDays = Record.DaysOwed;

	// THE SCHEDULER. Its mode and rung came through `Begin`.
	Run.Surges->SurgeIndex = Record.SurgeIndex;
	Run.Surges->SurgesFired = Record.SurgesFired;
	Run.Surges->NextSurgeDay = Record.NextSurgeDay;
	Run.Surges->LastSurgeDay = Record.LastSurgeDay;

	// THE STREAMS, FROM WHERE EACH HAD GOT TO, and the Corrupted Stalker's seed.
	Run.Stream.Initialize(Record.RandomStreamSeed);
	Run.CataclysmStream.Initialize(Record.CataclysmStreamSeed);
	Run.ModifierStream.Initialize(Record.ModifierStreamSeed);
	Run.CorruptedStalkerSeed = Record.CorruptedStalkerSeed;

	// WHAT THE PLAYER HAS ACHIEVED (#1374) AND WHAT THE RUN IS (#1471). The
	// objectives by Cataclysm REPLACE the keys `Begin` drew from the seed of 0;
	// they are not added into them. The tier came through `Begin`.
	Run.QuestObjectives = Record.QuestObjectives;
	Run.QuestObjectivesByCataclysm = Record.QuestObjectivesByCataclysm;
	Run.BasicDungeonsCleared = Record.BasicDungeonsCleared;
	Run.DungeonsCleared = Record.DungeonsCleared;
	Run.DungeonsDetonated = Record.DungeonsDetonated;
	Run.QuestHits = Record.QuestHits;
	Run.SurgesFromEmptyBoard = Record.SurgesFromEmptyBoard;
	Run.NextDungeonId = Record.NextDungeonId;
	Run.LastHitDay = Record.LastHitDay;
	Run.LastHitCityId = Record.LastHitCityId;
	Run.bLastHitWasAQuestTimer = Record.bLastHitWasAQuestTimer;
	Run.LastMoveDay = Record.LastMoveDay;
	Run.LastMoveFromCityId = Record.LastMoveFromCityId;
	Run.LastMoveToCityId = Record.LastMoveToCityId;
	Run.ActiveCataclysms = Record.ActiveCataclysms;

	OutWhy.Reset();
	return true;
}

bool FCataclysmSaveApply::CharacterInto(const UCataclysmCharacterSave& Record, ACataclysmPlayerCharacter& Character,
										int32& OutSkillsRefused, FString& OutWhy)
{
	OutSkillsRefused = 0;

	// REFUSED BEFORE ANYTHING IS TOUCHED, as `RunInto` refuses: a record that
	// cannot be put back whole is not put back in part.
	ACataclysmPlayerState* State = Character.GetPlayerState<ACataclysmPlayerState>();
	UCataclysmInventoryComponent* Inventory = Character.GetInventory();
	UCataclysmEquipmentComponent* Equipment = Character.GetEquipment();
	if (!State || !Inventory || !Equipment)
	{
		OutWhy = TEXT("the character has no player state, inventory or equipment to restore into");
		return false;
	}
	if (Record.CarriedSlots.Num() != UCataclysmInventoryComponent::SlotCount)
	{
		OutWhy = FString::Printf(TEXT("the record holds %d carried slots and the inventory has %d"),
								 Record.CarriedSlots.Num(), UCataclysmInventoryComponent::SlotCount);
		return false;
	}
	TArray<TPair<ECataclysmGearSlot, FCataclysmItem>> Worn;
	TSet<ECataclysmGearSlot> Named;
	for (const FCataclysmWornItem& Saved : Record.WornGear)
	{
		if (static_cast<int32>(Saved.Slot) >= static_cast<int32>(ECataclysmGearSlot::Count) || Named.Contains(Saved.Slot))
		{
			OutWhy = FString::Printf(TEXT("the record names worn slot %d, which does not exist or is named twice"),
									 static_cast<int32>(Saved.Slot));
			return false;
		}
		Named.Add(Saved.Slot);
		Worn.Add(TPair<ECataclysmGearSlot, FCataclysmItem>(Saved.Slot, Saved.Item));
	}

	// THE LEVEL, THE EXPERIENCE AND THE KILLS.
	State->SetLevelAndExperience(Record.Level, Record.Experience);
	State->SetLifetimeKills(Record.LifetimeKills);

	// THE CREATION CHOICE, when one was made. It refreshes the stat line on the
	// gear possession put on; the gear below replaces that.
	if (!Record.StartingWeaponType.IsNone() || !Record.StartingDamageType.IsNone())
	{
		State->SetCreationChoice(Record.StartingWeaponType, Record.StartingDamageType);
	}

	// THE NAME AND PARTITION, as the record holds them, so the next save writes them back. Select-screen step 1 (#753).
	State->SetIdentity(Record.CharacterName, Record.Partition); // A load's: the name and partition.

	// THE ATTRIBUTE POINTS, AS SPENT.
	State->RestoreSpentAttributePoints(Record.SpentAttributePoints);

	// THE WORN GEAR AND THE BAG, EACH IN ITS SAVED PLACE. The bag replaces the
	// weapon possession put into it when the creation choice swapped weapons.
	Equipment->RestoreWornGear(Worn);
	Inventory->RestoreSlots(Record.CarriedSlots);

	// THE PASSIVE ALLOCATION, THE BOSSES AND THE FIGURE, after the gear, and not
	// computed again.
	State->RestorePassiveAllocation(Record.PassiveAllocation, Record.DefeatedCataclysmBosses,
									Record.PassivePointsAnAllocationWasHandedWith);

	// ONE ANNOUNCEMENT, NOW THAT EVERYTHING IS BACK: the stat line, the second
	// two-handed weapon's permission, and the skill slots from the worn weapons.
	Equipment->AnnounceRestored();

	// THE SKILL CHOICES, checked against the skills the worn weapons give.
	for (const FCataclysmSkillChoice& Choice : Record.SkillAssignment)
	{
		FString Reason;
		if (!State->AssignSkill(Choice.Slot, Choice.SkillRow, Reason))
		{
			++OutSkillsRefused;
		}
	}

	OutWhy.Reset();
	return true;
}
