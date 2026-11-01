// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveGather.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmPotions.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireRun.h"
#include "EngineUtils.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Player/CataclysmPlayerState.h"

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
	Saved.bRisenFromTheDead = Creature.bRisenFromTheDead;
	Saved.AttackDamage = Creature.GetStartingAttackDamage();
	Saved.StartingHealth = Creature.GetStartingMaxHealth();
	Saved.Armour = Creature.GetStartingArmour();
	Saved.PlacedDamageMultiplier = Creature.GetPlacedDamageMultiplier();
	Saved.SightRadiusMultiplier = Creature.SightRadiusMultiplier;
	Saved.FloorRealityRow = Creature.FloorRealityRow;
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

	// AND THE CLASS RESOURCE, Fervour for the Masochist. Issue #753, Group 2.
	if (const UAbilitySystemComponent* Resource = UCataclysmTargeting::AbilitySystemOf(&Character))
	{
		if (Resource->HasAttributeSetForAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()))
		{
			Placed.ClassResource =
				Resource->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}
	}

	// AND THE POTIONS, every slot and the drinks taken. Issue #753, the dungeon step.
	if (const UCataclysmAbilitySystemComponent* Holder = Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(&Character)))
	{
		for (int32 Slot = 0; Slot < UCataclysmPotions::SlotCount; ++Slot)
		{
			Placed.PotionCharges.Add(Holder->GetPotionCharges(Slot));
		}
		Placed.PotionsDrunk = Holder->GetPotionsDrunk();
	}

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
	Saved.bDroppedByARaisedCreature = Drop.bDroppedByARaisedCreature;

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
	const ACataclysmDungeonGameMode* DungeonMode = ACataclysmDungeonGameMode::InWorld(Mutable);

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

		// NOR IS A CREATURE A FLOOR RULE MADE: the Reaper, an echo, a Plague Convergence wave, the
		// Trick or Treat pair, an Unstable Portal's Warden. Each exists because of a rule's own state
		// -- a clock, a count, a pair -- and that state is not saved, so the creature is not either.
		// Written back it would be an ordinary creature of its kind, with a brain, that could be hurt
		// and paid for. `Cataclysm.LoadCharacter` restores a floor since 2026-10-30 (#753); until then
		// this said no path in play did. Issues #1820 and #41.
		//
		// EXCEPT ONE A RULE RAISED IN PLAY AND KNOWS AGAIN, since Group 3d-3a (Q5 of 3d, ruled 2026-10-31): the rule's
		// state is saved now, so the creature is saved as a record with a tag naming the rule, and the load puts it back
		// into the rule's list. `RaisedByOf` answers a tag only for a creature in one of the lists it names; a creature
		// in none of them is skipped here as before.
		FName RaisedBy = NAME_None;
		int32 RaisedGroup = INDEX_NONE;
		if (Creature->bRaisedByARule && !(DungeonMode && DungeonMode->RaisedByOf(*Creature, RaisedBy, RaisedGroup)))
		{
			continue;
		}

		// NOR AN ILLUSION: Illusory Enemies places it as a floor creature that is not real, and the record has no
		// flag for that, so written back it was a real creature. A fault since #751, found by the sweep of
		// 2026-10-30 (#753, Group 2). It is not saved, as a rule-raised creature is not.
		if (Creature->IsAnIllusion())
		{
			continue;
		}

		// A CREATURE THE PLAYER COMMANDS IS SAVED WITH THAT ROLE, since #753's Group 2. Group 1 skipped it, because the
		// record then held no owner and no team and it came back hostile; now the record carries what the load needs
		// to re-link it. And what the walk's rules hold for this creature: Soul Harvest's souls, Nothing Is Forgotten's
		// boss. Ruled 2026-10-30.
		FCataclysmSavedCreature Record = CreatureFrom(*Creature);
		if (Cast<ACataclysmPlayerCharacter>(UCataclysmCommand::CommanderOf(Creature)))
		{
			Record.bCommandedByThePlayer = true;
			Record.bSecondSelf = Creature->bIsSecondSelf;
			Record.ReservedFervour = Creature->ReservedFervour;
			Record.CommanderHealthMultiplier = Creature->GetCommanderHealthMultiplier();
		}
		if (DungeonMode)
		{
			DungeonMode->SoulHarvestHeldBy(Creature, Record.SoulHarvestSouls, Record.SoulHarvestHealthAdded);
			Record.bFedByNothingIsForgotten = DungeonMode->IsTheVoidsBoss(Creature);
			Record.SoulChainsGate = DungeonMode->SoulChainsGateBorneBy(Creature);
			DungeonMode->CreatureRolesInto(*Creature, Record);
			Record.RaisedBy = RaisedBy;
			Record.RaisedGroup = RaisedGroup;
		}
		Saved.Creatures.Add(Record);
	}

	for (TActorIterator<ACataclysmDroppedItem> It(Mutable); It; ++It)
	{
		if (!IsValid(*It))
		{
			continue;
		}
		Saved.GroundItems.Add(GroundItemFrom(**It));
	}

	// AND THE FLOOR OBJECTS THE PLAYER USED, with what else the floor's choices left. Issue #753, Group 3b.
	if (DungeonMode)
	{
		Saved.UsedObjects = DungeonMode->UsedFloorObjectsNow();
		Saved.Choices = DungeonMode->FloorChoicesNow();
		Saved.RuleState = DungeonMode->FloorRuleStateNow();
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

void FCataclysmSaveGather::WornGearFrom(const UCataclysmEquipmentComponent& Equipment,
										TArray<FCataclysmWornItem>& OutWorn)
{
	// ONLY WHAT IS WORN, EACH WITH ITS SLOT'S NAME. An empty slot has nothing to
	// say, and the slot is carried by name so the file does not depend on the
	// enum's order. Issue #753.
	OutWorn.Reset();
	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
	{
		if (const FCataclysmItem* Worn = Equipment.EquippedAt(Slot))
		{
			FCataclysmWornItem& Entry = OutWorn.AddDefaulted_GetRef();
			Entry.Slot = Slot;
			Entry.Item = *Worn;
		}
	}
}

bool FCataclysmSaveGather::RunClockFrom(const UCataclysmEmpireRun& Run,
									   UCataclysmRunSave& Record)
{
	// A RUN THAT HAS NOT BEGUN HAS NO CLOCK. `Begin` is what makes one, so a
	// freshly constructed run answers false and the record keeps whatever it
	// already held rather than being zeroed by a run that knows nothing.
	const UCataclysmDayClock* Clock = Run.Clock;
	if (Clock == nullptr)
	{
		return false;
	}

	Record.Day = Clock->Day;
	Record.PartialDay = Clock->PartialDay;
	Record.DaysOwed = Run.DaysOwed();

	return true;
}

bool FCataclysmSaveGather::SurgeScheduleFrom(const UCataclysmEmpireRun& Run,
											UCataclysmRunSave& Record)
{
	const UCataclysmSurgeScheduler* Surges = Run.Surges;
	if (Surges == nullptr)
	{
		return false;
	}

	Record.SurgeMode = Surges->Mode;
	Record.SurgeLethalityRung = Surges->LethalityRung;
	Record.SurgeIndex = Surges->SurgeIndex;
	Record.SurgesFired = Surges->SurgesFired;
	Record.NextSurgeDay = Surges->NextSurgeDay;

	// THE POSITION REACHED, NOT THE SEED THE RUN STARTED FROM. Saving the stream
	// itself would write only the starting seed, because that is the half of it
	// the engine marks as savable, and a restore would replay every surge
	// already generated. See `UCataclysmRunSave::RandomStreamSeed`.
	Record.RandomStreamSeed = Run.Stream.GetCurrentSeed();

	// AND WHAT A RESTORE NEEDS THAT NOTHING ABOVE HOLDS, issue #753: the day of
	// the last surge, which the empty-board and city-fall surges measure from,
	// and where the run's other two streams had got to, with the Corrupted
	// Stalker's seed. The three were made from the seed the run began with,
	// which is not kept, so none can be worked out again on a restore.
	Record.LastSurgeDay = Surges->LastSurgeDay;
	Record.CataclysmStreamSeed = Run.CataclysmStream.GetCurrentSeed();
	Record.ModifierStreamSeed = Run.ModifierStream.GetCurrentSeed();
	Record.CorruptedStalkerSeed = Run.CorruptedStalkerSeed;

	return true;
}

bool FCataclysmSaveGather::ProgressFrom(const UCataclysmEmpireRun& Run,
										UCataclysmRunSave& Record)
{
	// A RUN THAT HAS NOT BEGUN HAS ACHIEVED NOTHING, and `Begin` is what makes
	// its clock, so the clock is the test `RunClockFrom` uses too.
	if (Run.Clock == nullptr)
	{
		return false;
	}

	Record.QuestObjectives = Run.QuestObjectives;
	Record.QuestObjectivesByCataclysm = Run.QuestObjectivesByCataclysm;
	Record.BasicDungeonsCleared = Run.BasicDungeonsCleared;
	Record.DungeonsCleared = Run.DungeonsCleared;
	Record.DungeonsDetonated = Run.DungeonsDetonated;
	Record.QuestHits = Run.QuestHits;
	Record.SurgesFromEmptyBoard = Run.SurgesFromEmptyBoard;
	Record.NextDungeonId = Run.NextDungeonId;
	Record.LastHitDay = Run.LastHitDay;
	Record.LastHitCityId = Run.LastHitCityId;
	Record.bLastHitWasAQuestTimer = Run.bLastHitWasAQuestTimer;
	Record.LastMoveDay = Run.LastMoveDay;
	Record.LastMoveFromCityId = Run.LastMoveFromCityId;
	Record.LastMoveToCityId = Run.LastMoveToCityId;

	return true;
}

bool FCataclysmSaveGather::IdentityFrom(const UCataclysmEmpireRun& Run,
										UCataclysmRunSave& Record)
{
	// A RUN THAT HAS NOT BEGUN IS AT NO TIER AND FACES NOTHING YET. `Begin` sets
	// both, and makes the clock this tests for, as `ProgressFrom` does.
	if (Run.Clock == nullptr)
	{
		return false;
	}

	Record.DifficultyTier = Run.DifficultyTier;
	Record.ActiveCataclysms = Run.ActiveCataclysms;

	return true;
}

bool FCataclysmSaveGather::DungeonsFrom(const UCataclysmEmpireRun& Run,
									   UCataclysmRunSave& Record,
									   FString& OutWhy)
{
	const UCataclysmDayClock* Clock = Run.Clock;
	if (Clock == nullptr)
	{
		OutWhy = TEXT("the run has no clock, so it has not begun");
		return false;
	}

	// CHECKED BEFORE ANYTHING IS COPIED, so a pair that disagrees leaves the
	// record exactly as it was rather than half written.
	if (!UCataclysmEmpireRun::DungeonsAgreeWithTimers(
			Run.Dungeons, Clock->Timers, OutWhy))
	{
		return false;
	}

	Record.Dungeons = Run.Dungeons;
	Record.DungeonTimers = Clock->Timers;
	Record.CurrentDungeonId = Clock->CurrentDungeonId;

	return true;
}

bool FCataclysmSaveGather::CitiesFrom(const UCataclysmEmpireRun& Run,
									 UCataclysmRunSave& Record)
{
	// A RUN THAT HAS NOT BEGUN HAS NO MAP. `Begin` is what builds one, so a
	// freshly constructed run answers false and the record keeps whatever it
	// already held rather than being emptied by a run that knows nothing.
	const UCataclysmEmpireMap* Map = Run.Map;
	if (Map == nullptr)
	{
		return false;
	}

	Record.Cities = Map->Cities;
	Record.CityUpgradeSlots = Map->UpgradeSlots;

	return true;
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

	// AND WHAT IS WORN, since 2026-09-24. Recorded on issue #753 as a
	// precondition for loading: the record had no field for it. WRITTEN ONLY
	// WHEN THERE IS EQUIPMENT TO READ, for the reason the attribute allocation
	// below gives: emptying the field for want of a source would throw away what
	// a loaded record held.
	if (const UCataclysmEquipmentComponent* Equipment =
			Character.FindComponentByClass<UCataclysmEquipmentComponent>())
	{
		WornGearFrom(*Equipment, Record.WornGear);
	}

	// THE ATTRIBUTE ALLOCATION, SINCE 2026-08-24, and it is the second field
	// here with a runtime source. Issue #50.
	//
	// WRITTEN ONLY WHEN THERE IS A PLAYER STATE TO READ IT FROM, on exactly the
	// reasoning the comment below gives. A character with no player state has no
	// allocation, and zeroing the field would throw away one that was loaded off
	// disk a moment earlier.
	if (const ACataclysmPlayerState* State =
			Character.GetPlayerState<ACataclysmPlayerState>())
	{
		Record.SpentAttributePoints = State->GetSpentAttributePoints();

		// AND THE LEVEL AND THE PROGRESS INTO IT, since 2026-08-24. Issue #50.
		//
		// BOTH OR NEITHER, WHICH IS WHY THEY ARE WRITTEN TOGETHER. The pair
		// describes one thing -- how far the character has climbed -- and a
		// record holding a level from one moment beside progress from another
		// would put the character somewhere it has never been.
		//
		// THE LEVEL WRITTEN IS WHAT `GetCharacterLevel` ANSWERS, which for a
		// character that has neither gained a level nor loaded a save is
		// `Cataclysm.PlayerLevel`. That is deliberate: the console variable is
		// the starting level, so a character stood up at level 40 to test
		// something saves as level 40 rather than as level 1.
		Record.Level = State->GetCharacterLevel();
		Record.Experience = State->GetExperienceIntoLevel();

		// AND EVERY KILL IT HAS MADE. Issue #1833, the kill counter.
		Record.LifetimeKills = State->GetLifetimeKills();

		// AND WHAT WAS CHOSEN WHEN THE CHARACTER WAS CREATED, since 2026-08-25.
		// Issue #50.
		//
		// WRITTEN ONLY WHEN SOMEBODY ACTUALLY CHOSE, which is the difference
		// from the level above. `GetChosenWeaponType` answers the default for a
		// character that has not chosen, and writing that would turn "nobody
		// chose" into "chose the Greataxe" on the first save -- a decision the
		// player never made, recorded permanently, on every character an
		// automation test has ever stood up.
		if (State->HasChosenAtCreation())
		{
			Record.StartingWeaponType = State->GetChosenWeaponType();
			Record.StartingDamageType = State->GetChosenDamageType();
		}

		// AND THE PASSIVE TREE, since 2026-08-25. Issue #50.
		//
		// WRITTEN UNCONDITIONALLY, UNLIKE THE CREATION CHOICE ABOVE, and the
		// difference is what an empty value means. An empty creation choice
		// means "nobody chose" and the getters answer a default instead; an
		// empty allocation means exactly "no points are spent", which is the
		// truth about a character that has spent none. There is no default to
		// mistake it for.
		Record.PassiveAllocation = State->GetPassiveAllocation();
		Record.DefeatedCataclysmBosses = State->GetDefeatedCataclysmBosses();
		// AND THE FIGURE AN ALLOCATION WAS HANDED WITH, which nothing can work out
		// again from the rest. Issue #753, the character step.
		Record.PassivePointsAnAllocationWasHandedWith = State->PassivePointsAnAllocationWasHandedWith();

		// AND THE CHOSEN SKILLS, since issue #837 layer 3 (Q6: one field, no version bump). UNCONDITIONALLY, for the
		// passive tree's reason: an empty list means exactly "nothing chosen", the default fill.
		Record.SkillAssignment = State->GetSkillAssignment();
	}

	// EVERY OTHER FIELD IS LEFT ALONE RATHER THAN ZEROED. The residue and a Solo
	// Self-Found character's private stash have no runtime source at all --
	// issues #38 and #42 -- so writing a zero over each would turn a record
	// loaded from disk into an empty one every time it was refreshed. This
	// list named the passive tree and the equipped slots too until each gained a
	// source: the passive allocation above, and the worn gear since 2026-09-24.
	return true;
}

bool FCataclysmSaveGather::RunKillsFrom(const ACataclysmPlayerCharacter& Character,
										UCataclysmRunSave& Record)
{
	const ACataclysmPlayerState* State = Character.GetPlayerState<ACataclysmPlayerState>();
	if (!State)
	{
		return false;
	}
	Record.RunKills = State->GetRunKills();
	return true;
}
