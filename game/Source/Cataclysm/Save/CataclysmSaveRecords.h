// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmLethality.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecord.h"
#include "Templates/SubclassOf.h"
#include "CataclysmSaveRecords.generated.h"

/**
 * WHAT THESE THREE CLASSES ARE, AND WHAT THEY ARE NOT YET.
 *
 * They are the SHAPE of the three records `docs/Save_System_Design.md` section 2
 * describes, so that the storage layer, the migration chain and the fixture
 * tests have something real to write, read and migrate.
 *
 * MOST OF THE FIELDS THE DESIGN LISTS HAVE NO RUNTIME SOURCE. Nothing in the
 * game yet produces a character level, an attribute allocation, a passive tree
 * allocation, 18 equipped slots, a stash, an empire graph or a dungeon timer.
 * Those are issues #50, #38, #42 and others. The fields below that have no
 * source are still declared, because a field added later with a sensible default
 * is NOT a schema version bump -- section 5, "What a version bump means in
 * practice" -- so declaring them early costs nothing and leaves the file shape
 * settled. Each one says what fills it.
 *
 * WHAT IS DELIBERATELY ABSENT is listed on each class rather than left to be
 * noticed, because a reader comparing this against the design document should be
 * able to tell an omission from an oversight.
 */

/**
 * One creature on the floor, as it stood when the game last wrote itself.
 *
 * THIS IS THE LEFT-HAND COLUMN OF THE TABLE IN SECTION 6 and nothing else. The
 * project owner set the rule on 2026-08-20: the game saves itself constantly so
 * that a Hardcore character cannot leave a losing boss fight by closing the
 * game, and a fight resumes as "a neutral restart with the damage kept". A boss
 * keeps every point taken off it.
 *
 * WHAT IS NOT HERE IS NOT AN OVERSIGHT. How far through a wind-up the creature
 * was, where it was in its attack cycle, what it had in flight, and how long its
 * buffs had left are all excluded on purpose. Section 6: that is the data whose
 * shape changes with every patch, and section 5 requires a migration for any
 * persisted field whose shape changes, so persisting combat choreography would
 * mean writing a migration every patch for state nobody wants preserved. It can
 * be tightened later, one field at a time, each as its own version bump.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmSavedCreature
{
	GENERATED_BODY()

	/** Which row of `game/Data/EnemyArchetypes.csv` this creature is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	FName ArchetypeRow;

	/** Its rung on the rarity ladder, as `ACataclysmEnemyCharacter::RarityStep`
	 *  holds it: 0 is Common and 4 and above is a boss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	int32 RarityStep = 0;

	/** Rows of `game/Data/EnemyModifiers.csv`, as `ModifierRows` holds them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	TArray<FName> ModifierRows;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	FVector Location = FVector::ZeroVector;

	/** Which way it faces. Yaw alone, because a creature does not pitch or roll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	float Yaw = 0.0f;

	/** The health it had. THE WHOLE POINT OF THE RECORD: a boss that was down to
	 *  a tenth comes back down to a tenth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	float Health = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	float EnergyShield = 0.0f;
};

/** One item lying on the floor that nobody picked up. Section 6 keeps these. */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmSavedGroundItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	FCataclysmItem Item;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	FVector Location = FVector::ZeroVector;
};

/**
 * Where one character was standing and what it had left.
 *
 * KEYED BY THE CHARACTER'S IDENTIFIER because the run record is shared by the
 * party. In solo play there is one of these; in co-operative play there are up
 * to four, and each names which character record it belongs to.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmSavedCharacterPlacement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	FGuid CharacterId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	float Yaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	float Health = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	float Mana = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	float EnergyShield = 0.0f;
};

/**
 * The floor being fought on, as it stands.
 *
 * `Floor` IS ZERO WHEN NOBODY IS IN A DUNGEON, which is the ordinary case: a
 * party standing in a city has no floor. Floors are counted from 1, so zero
 * cannot be mistaken for the first one, and a record whose floor is zero should
 * have no creatures in it.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmSavedFloor
{
	GENERATED_BODY()

	/** Which dungeon the floor belongs to, or None when nobody is in one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	FName Dungeon;

	/** Which floor of it, counted from 1. Zero means nobody is in a dungeon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	int32 Floor = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	TArray<FCataclysmSavedCreature> Creatures;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	TArray<FCataclysmSavedGroundItem> GroundItems;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	TArray<FCataclysmSavedCharacterPlacement> Characters;

	/** Whether anybody is on a floor at all. */
	bool IsOccupied() const { return Floor > 0; }
};

/**
 * The account record: what every character in one partition shares.
 *
 * ONE PER PARTITION, AND THERE ARE SIX. Population times lethality mode, from
 * `docs/Save_System_Design.md` section 3 and `UCataclysmSavePartition`. A Solo
 * Self-Found character has no account record at all; its empire upgrade points
 * and its private stash live in its own character record.
 *
 * IT CARRIES THE PARTITION IT BELONGS TO. The slot name already says it, but a
 * file can be copied into the wrong slot by hand, and a record that names its
 * own partition can be checked against the slot it was read from. Without that,
 * a Hardcore account record dropped into the Standard slot would quietly become
 * the Standard tree.
 *
 * WHAT IS DELIBERATELY ABSENT: the empire upgrade TREE ALLOCATION -- which nodes
 * are filled and to what depth. The tree's node graph exists as
 * `docs/Empire_Development_Tree_Final.json` but nothing in the game reads it
 * yet, and guessing at the allocation's shape now would cost a migration later
 * for a field that had never held anything. The banked points are here because
 * an integer cannot be got wrong.
 */
UCLASS(BlueprintType)
class CATACLYSM_API UCataclysmAccountSave : public UCataclysmSaveRecord
{
	GENERATED_BODY()

public:
	static const FName TypeName;
	static constexpr int32 SchemaVersionNow = 1;

	virtual FName RecordType() const override { return TypeName; }
	virtual int32 CurrentSchemaVersion() const override { return SchemaVersionNow; }

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	ECataclysmLethality Lethality = ECataclysmLethality::Standard;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	ECataclysmPopulation Population = ECataclysmPopulation::Offline;

	/**
	 * Empire upgrade points banked and not yet spent.
	 *
	 * EARNED INTO A PARTITION AND NOT INTO A CHARACTER. `docs/Cataclysm_GDD_v2.md`,
	 * "Difficulty Options": a point earned by a character is earned into that
	 * character's lethality mode, and is shared with every other character in the
	 * same mode and population.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	int32 EmpireUpgradePoints = 0;

	/**
	 * The characters belonging to this partition, by identifier.
	 *
	 * AN ACCOUNT'S 24 CHARACTER SLOTS ARE ONE POOL ACROSS ALL SIX PARTITIONS, so
	 * this list is not capped at 24 on its own. Issue #577 and
	 * `tools/tests/test_character_slots.py` carry that rule.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	TArray<FGuid> Characters;

	/**
	 * The shared stash: 600 slots in six tabs, held once per partition.
	 *
	 * THE LARGEST THING IN THE FORMAT, which section 3 warns implementers about.
	 * Nothing fills it yet -- there is no stash in the game -- but the slot type
	 * is the one the carried inventory already uses, because the design gives the
	 * stash the same rule: one item to one slot, materials stacking by name.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	TArray<FCataclysmCarriedSlot> Stash;
};

/**
 * The character record: what belongs to one character and travels with it.
 *
 * IT SURVIVES A FAILED RUN. Issue #315 settled that nothing in the design
 * destroys a character, so this record is never deleted as a consequence of
 * play. Losing the capital, dying in the Last Stand and being killed by the
 * corrupted double all cost the run and not the character.
 *
 * WHAT IS DELIBERATELY ABSENT: the attribute allocation, the passive class tree
 * allocation, the 18 equipped slots, and a Solo Self-Found character's private
 * empire tree allocation. Each needs a shape that does not exist in the game
 * yet, and section 5 says adding a field later with a sensible default is not a
 * version bump, so waiting costs nothing and guessing costs a migration.
 */
UCLASS(BlueprintType)
class CATACLYSM_API UCataclysmCharacterSave : public UCataclysmSaveRecord
{
	GENERATED_BODY()

public:
	static const FName TypeName;
	static constexpr int32 SchemaVersionNow = 1;

	virtual FName RecordType() const override { return TypeName; }
	virtual int32 CurrentSchemaVersion() const override { return SchemaVersionNow; }

	/** What names this character's own slot. Generated, never the player's
	 *  chosen name, so renaming is free and two characters may share a name. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FGuid CharacterId;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FString CharacterName;

	/**
	 * Lethality mode, population and the Solo Self-Found flag.
	 *
	 * ALL THREE ARE SET AT CREATION AND NONE EVER CHANGES, which is what lets
	 * them decide a storage layout. `UCataclysmSavePartition` turns this into the
	 * account record the character reads and writes, or into no account record at
	 * all when the character is Solo Self-Found.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FCataclysmCharacterPartition Partition;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	int32 Level = 1;

	/**
	 * Experience toward the next level.
	 *
	 * 64 BITS RATHER THAN 32. This genre's later levels cost totals well past two
	 * billion, and a field that silently wraps is worse than one that is too
	 * wide. JSON carries it exactly: a number is a double, which holds every
	 * integer up to 2^53.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	int64 Experience = 0;

	/** Cataclysmic Residue the character is carrying. A cost, never a benefit. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	float CataclysmicResidue = 0.0f;

	/**
	 * The 48 carried slots.
	 *
	 * THE ONE FIELD HERE THAT HAS A RUNTIME SOURCE TODAY:
	 * `UCataclysmInventoryComponent` holds exactly this array, and nothing
	 * resizes it.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	TArray<FCataclysmCarriedSlot> CarriedSlots;

	/**
	 * A Solo Self-Found character's own empire upgrade points.
	 *
	 * SEPARATE FROM THE ACCOUNT RECORD'S, and not a copy of it. A Solo Self-Found
	 * character shares an empire tree with no other character at all, not even
	 * another Solo Self-Found one, so its points cannot live in a record that
	 * anybody else reads. Left at zero and unused for every other character.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	int32 PrivateEmpireUpgradePoints = 0;

	/**
	 * A Solo Self-Found character's own 600-slot stash, for the same reason.
	 *
	 * THIS IS WHY A SOLO SELF-FOUND RECORD IS ABOUT TEN TIMES THE SIZE of an
	 * ordinary one, which section 3 tells implementers to expect. Empty for every
	 * other character.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	TArray<FCataclysmCarriedSlot> PrivateStash;
};

/**
 * The run record: the state of one run, discarded when the run ends.
 *
 * SHARED BY THE PARTY. Section 1: the death penalty "is paid once for the party,
 * not once per player" and is charged against the shared empire clock, so the
 * day count, the empire and the dungeon timers belong to the run rather than to
 * any one character.
 *
 * IT IS THE RECORD THAT CHANGES CONSTANTLY, and that is why the format has three
 * records rather than one. Section 6: the run is written on the frequent
 * cadence, the account record only when it actually changes, and it is that
 * split which makes a constant save affordable.
 *
 * WHAT IS DELIBERATELY ABSENT: the empire graph -- which cities stand, their
 * population, their defence and their filled upgrade slots -- the active
 * dungeons with their modifiers and resolve timers, the surge schedule, and
 * Cataclysm quest progress. All four live in the `CataclysmEmpire` module's
 * design and none of them has a runtime shape yet. `FCataclysmSavedFloor` IS
 * here, in full, because section 6 specifies its contents exactly and it is the
 * requirement the whole feature exists for.
 */
UCLASS(BlueprintType)
class CATACLYSM_API UCataclysmRunSave : public UCataclysmSaveRecord
{
	GENERATED_BODY()

public:
	static const FName TypeName;
	static constexpr int32 SchemaVersionNow = 1;

	virtual FName RecordType() const override { return TypeName; }
	virtual int32 CurrentSchemaVersion() const override { return SchemaVersionNow; }

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FGuid RunId;

	/** The current day. One dungeon floor costs exactly one day, so this is also
	 *  the run's whole clock. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	int32 Day = 0;

	/** The character records taking part: one in solo play, up to four in
	 *  co-operative play. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	TArray<FGuid> Characters;

	/** The floor being fought on, as it stands. Empty when nobody is in a
	 *  dungeon. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FCataclysmSavedFloor Floor;
};

/**
 * The three records the design defines, for anything that has to treat all of
 * them alike.
 *
 * WHAT USES IT IS THE TESTS, and that is the point. A check that every record
 * type's migration chain reaches its own version, or that every record writes
 * its version first, is only worth anything if it cannot miss one. A list here
 * that a reviewer can compare against `docs/Save_System_Design.md` section 1 is
 * harder to leave a record out of than three separate tests are.
 */
CATACLYSM_API TArray<TSubclassOf<UCataclysmSaveRecord>> CataclysmSaveRecordClasses();
