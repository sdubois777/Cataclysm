// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmLethality.h"
#include "Character/CataclysmPassiveTree.h"
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

	/**
	 * The health it would have at full.
	 *
	 * KEPT AS WELL AS THE HEALTH LEFT, AND IT IS NOT REDUNDANT. A creature's
	 * maximum health is a property of the ENCOUNTER rather than of the species:
	 * the same Brute is tougher deeper in a dungeon, and `ACataclysmGameMode`
	 * already sets it per spawn. Without this, a restored creature would fall
	 * back to whatever its class defaults to -- and the vital attribute set
	 * clamps health to the maximum, so a boss with 137 health left and a
	 * default maximum of 100 would come back with 100. **That was a real
	 * failure and a test caught it**, which is why the field exists.
	 *
	 * ZERO MEANS THE CREATURE KEEPS WHATEVER ITS CLASS GIVES IT, which is what
	 * a record written before this field existed reads back as.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	float MaxHealth = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	float EnergyShield = 0.0f;

	/** The energy shield it would have at full. Zero for the same reason.
	 *  Most creatures have none at all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	float MaxEnergyShield = 0.0f;
};

/**
 * One drop lying on the floor that nobody picked up. Section 6 keeps these.
 *
 * A DROP IS EITHER GEAR OR A STACK OF CRAFTING MATERIALS, never both, and
 * `ACataclysmDroppedItem` is one actor class for the two because everything
 * about lying on a floor is the same for them: a position, a name, a colour and
 * a click. This mirrors that, and the pair of material fields is the pair
 * `FCataclysmCarriedSlot` already uses for the same distinction.
 *
 * WHAT IS NOT HERE: the drop's printed name, the colour it is drawn in, its
 * rarity and the material's tier. All four are worked out from the two fields
 * above plus the game's data tables, so persisting them would store a second
 * copy of something derived -- and a copy written by an older build would be
 * the OLD name after an item was renamed in the design workbook.
 * `ACataclysmDroppedItem::DescribeItself` is what fills them in again.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmSavedGroundItem
{
	GENERATED_BODY()

	/** The gear item. Its Base is None when the drop is crafting materials. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	FCataclysmItem Item;

	/** Which crafting material this is, as a row key in
	 *  `game/Data/CraftingMaterials.csv`. None when the drop is gear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	FName Material;

	/** How many of that material lie here. Zero when the drop is gear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	int32 MaterialQuantity = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	FVector Location = FVector::ZeroVector;

	/** Whether this drop is crafting materials rather than a piece of gear. */
	bool IsMaterial() const { return !Material.IsNone() && MaterialQuantity > 0; }
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
 * WHAT IS DELIBERATELY ABSENT: the 18 equipped slots, and a Solo Self-Found
 * character's private empire tree allocation. Both need a shape that does not
 * exist in the game yet, and section 5 says adding a field later with a sensible
 * default is not a version bump, so waiting costs nothing and guessing costs a
 * migration.
 *
 * THREE THINGS LEFT THAT LIST IN TWO DAYS, each on the day the running game
 * first produced it: the attribute allocation on 2026-08-24, the two character
 * creation choices on 2026-08-25, and the passive tree allocation the same day.
 * All three are issue #50.
 */
UCLASS(BlueprintType)
class CATACLYSM_API UCataclysmCharacterSave : public UCataclysmSaveRecord
{
	GENERATED_BODY()

public:
	static const FName TypeName;

	/**
	 * 2 SINCE 2026-08-24, AND IT WAS 1. `SpentAttributePoints` below arrived
	 * when attribute allocation became something the running game produces, so
	 * a character written before that has no such field at all. Issue #50.
	 */
	static constexpr int32 SchemaVersionNow = 2;

	virtual FName RecordType() const override { return TypeName; }
	virtual int32 CurrentSchemaVersion() const override { return SchemaVersionNow; }
	virtual TArrayView<const FCataclysmSaveMigrationStep> MigrationSteps() const override;

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

	/**
	 * How the character's attribute points are spread across the eight.
	 *
	 * A CHARACTER HAS ONE POINT FOR EVERY LEVEL, so the total here can never
	 * exceed `Level` above. `ACataclysmPlayerState::SpendAttributePoints` is
	 * what enforces that; nothing revalidates it on load, which is the same
	 * trust every other field here is given.
	 *
	 * WHAT IS SAVED IS THE POINTS AND NOT WHAT THEY ARE WORTH. The eight gear
	 * affixes that increase an attribute are already saved as part of the gear
	 * that carries them, and `game/Data/Attributes.csv` says what a point does,
	 * so storing the resolved value would be storing the same thing twice and
	 * inviting the two to disagree. Issue #50.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FCataclysmAttributePoints SpentAttributePoints;

	/**
	 * The weapon type the player chose when the character was created.
	 *
	 * A WeaponType in `game/Data/ItemBases.csv`, such as `Greataxe`.
	 * `docs/Cataclysm_GDD_v2.md` section IV: it decides the character's initial
	 * skill set together with the damage type below. Issue #50.
	 *
	 * NOT A VERSION BUMP. `docs/Save_System_Design.md` section 5 says a field
	 * added later with a sensible default is not one, and the default here is
	 * `NAME_None`, which is what a record written before the creator existed
	 * reads back as and means "nobody chose".
	 *
	 * WHAT NOBODY CHOSE READS AS. `ACataclysmPlayerState::GetChosenWeaponType`
	 * answers `UCataclysmCharacterCreation::DefaultWeaponType` for an empty
	 * one, so an old save loads the character it always was.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FName StartingWeaponType;

	/**
	 * The damage type the player chose when the character was created.
	 *
	 * One of the eight. It decides which of the weapon's skills the character
	 * has, and which three class passive trees it can spend points in.
	 *
	 * IT IS NOT DERIVABLE FROM THE WEAPON WORN, which is why it is stored. A
	 * Greataxe can carry War, Demonic, Death or Chaos, and which of the four
	 * this character is has nothing to do with the axe.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FName StartingDamageType;

	/**
	 * Where this character's passive points are spent.
	 *
	 * ONE ENTRY PER NODE THAT HOLDS SOMETHING, keyed by the node's row name in
	 * `game/Data/PassiveNodes.csv` -- the tree and the node identifier together,
	 * because a node identifier is unique only within its tree.
	 *
	 * EVERY FIELD OF IT CARRIES `SaveGame` and that is not optional: the save
	 * writer walks only properties with that marker, so without it this
	 * serialises as an empty object and a character's whole tree is lost on
	 * every save with nothing reporting it. `FCataclysmAttributePoints` carries
	 * the same warning for the same reason.
	 *
	 * WHAT IS SAVED IS WHERE THE POINTS WENT, NOT WHAT THEY GRANT. Since
	 * 2026-08-25 a node can carry an authored effect, so there is now something
	 * resolved that could be stored -- and storing it would still be wrong, for
	 * the reason `SpentAttributePoints` gives above: `game/Data/PassiveEffects.csv`
	 * already says what a point in a node is worth, so saving the resolved value
	 * would store the same thing twice and invite the two to disagree.
	 * `UCataclysmPassiveTree::AccumulateInto` works it out again on load.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FCataclysmPassiveAllocation PassiveAllocation;

	/**
	 * The unique Cataclysm bosses this character has defeated at least once.
	 *
	 * TEN PASSIVE POINTS EACH, ONCE. `docs/Cataclysm_GDD_v2.md` section XII
	 * says "Defeating a unique Cataclysm boss for the first time: 10 bonus
	 * passive points", and eight bosses at ten points is the 80 that takes the
	 * budget from 150 to 230.
	 *
	 * NAMES RATHER THAN A COUNT, because "first time" is a fact about which
	 * boss. A count could be raised eight times by beating one boss eight times.
	 *
	 * NOTHING FILLS IT YET. No unique Cataclysm boss exists in the game.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	TArray<FName> DefeatedCataclysmBosses;

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

	/**
	 * The current day of the run.
	 *
	 * A WHOLE NUMBER, AND NOT THE WHOLE CLOCK. It used to say it was, on the
	 * grounds that one floor cost exactly one day so the day count and the
	 * progress count were the same number. That is no longer true: a floor costs
	 * one day only as a starting rate, and once a city upgrade or the empire
	 * tree has shortened a walk, `UCataclysmDayClock::PartialDay` holds time
	 * that has been spent and has not yet added up to a day.
	 *
	 * WRITTEN FROM `UCataclysmEmpireRun::Day` by
	 * `UCataclysmSaveWriter::WriteTheRunRecord`, and only when there is a run to
	 * read. A save written with no empire run going leaves whatever was here
	 * alone rather than writing a zero, so a record loaded from disk and then
	 * refreshed does not lose the day it already held.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	int32 Day = 0;

	/**
	 * Time spent that has not yet added up to a whole day, from
	 * `UCataclysmDayClock::PartialDay`. Always at least 0 and below 1.
	 *
	 * WHY THE DAY ALONE IS NOT ENOUGH. A city upgrade can lower the days a
	 * dungeon takes to walk while its floor count stays where it is, so a fifty
	 * floor dungeon may cost two days and each of its floors a twenty-fifth of a
	 * day. The carry is then almost never zero at the moment a save is written,
	 * and dropping it loses up to just under a whole day of empire progress on
	 * every save -- or grants one, depending on which way a later rounding
	 * falls. That is drift nobody would attribute to the save system.
	 *
	 * IT WOULD HAVE BEEN WORTH NOTHING BEFORE ISSUE #1266. While a floor always
	 * cost exactly one day the carry was zero at every floor boundary, so a save
	 * could not catch it holding anything. Separating a dungeon's walk time from
	 * its floor count is what made it real.
	 *
	 * ZERO IS THE HONEST DEFAULT, and it is what a save written before this
	 * field existed reads back as -- which is also what it meant at the time, for
	 * the reason above. **Adding a field with a sensible default is not a schema
	 * version bump**: `docs/Save_System_Design.md` section 5 says so, because a
	 * `UPROPERTY(SaveGame)` field missing from a file simply keeps its default.
	 * So `SchemaVersionNow` stays at 1 and no migration step is needed, and
	 * `Run_v1.json` still loads.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	float PartialDay = 0.0f;

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
