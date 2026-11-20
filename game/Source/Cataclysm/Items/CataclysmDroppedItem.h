// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/CataclysmItem.h"
#include "Math/Box2D.h"
#include "CataclysmDroppedItem.generated.h"

struct FCataclysmEmpireTreeDropRules;

class UCataclysmInventoryComponent;
class UDataTable;
class UWorld;

/**
 * One item lying on the dungeon floor, waiting to be picked up.
 *
 * WHAT A PLAYER SEES IS THE NAME, NOT A MODEL. Decided by the project owner on
 * 2026-08-18: "the thing that should be visible is the items name. So a player
 * sees a drop as a nametag and clicking on it loots the item." So this actor
 * carries no mesh. It is a position in the world, an item, and the two things
 * needed to draw a name over it: the text and the colour.
 *
 * THE NAME AND THE COLOUR ARE WORKED OUT ONCE, AT SPAWN, rather than every
 * frame. Both are read from data tables, and the heads-up display redraws every
 * drop on screen on every frame; doing the lookups there would repeat them
 * sixty times a second for something that cannot change while the item is on
 * the floor.
 *
 * PICKED UP BY `UCataclysmDropPickup::TakeInto`, into the carried inventory: a click
 * takes any drop, and walking near a crafting material takes it. This said picking
 * up was not built, which stopped being true when the inventory was; corrected while
 * building `Chaos_Trick_or_Treat`. Issue #707.
 */
UCLASS()
class CATACLYSM_API ACataclysmDroppedItem : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmDroppedItem();

	/** The item itself, whole: its base, upgrade level, affixes, sockets and
	 *  the residue it carries. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	FCataclysmItem Item;

	/** What the player reads: `<rarity> <base> of <word>`. Empty only when the
	 *  item could not be named, which means it is malformed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	FString DisplayName;

	/** The colour the name is drawn in, from the item's rarity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	FLinearColor NameColour = FLinearColor::White;

	/**
	 * The rarity the name is drawn for, worked out once at spawn.
	 *
	 * STORED RATHER THAN RECOMPUTED, for the reason the name and the colour
	 * are: the heads-up display redraws every drop on screen on every frame, and
	 * an item's rarity cannot change while it lies on the floor.
	 *
	 * IT DECIDES THE BORDER'S THICKNESS as well as the colour, which is what
	 * lets a player who cannot separate two hues still separate two rarities.
	 * See UCataclysmDropPickup::NameBorderThicknessFor. Issue #718.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	ECataclysmRarity Rarity = ECataclysmRarity::Everyday;

	/**
	 * Which crafting material this drop is, as a row key in
	 * `game/Data/CraftingMaterials.csv`. None when the drop is gear.
	 *
	 * ONE ACTOR FOR BOTH KINDS, because everything about lying on a floor is
	 * the same for both: a position, a name, a colour, a border and a click.
	 * What differs is only where the name and the colour come from, which is
	 * settled once at spawn. A second actor class would be a second copy of the
	 * scatter, the projection, the layout and the pick-up.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	FName Material;

	/** How many of that material lie here. Zero when the drop is gear. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	int32 MaterialQuantity = 0;

	/**
	 * Whether a creature a dungeon rule raised mid-floor dropped this. Trick or Treat rolls
	 * nothing for a marked drop, so a trick's pair cannot start another trick. Issues #1820
	 * and #41.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	bool bDroppedByARaisedCreature = false;

	/**
	 * Whether this drop is infested: The Infested Hoard's extra drop, "Infested " in front of its name, never
	 * collected automatically, and a stack of Infestation to whoever takes it by hand. Issues #1820 and #41.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	bool bInfested = false;

	/**
	 * Which material tier this drop is, 1 to 5. Zero when the drop is gear.
	 *
	 * KEPT BESIDE THE MATERIAL for the reason Rarity is kept beside the item:
	 * the border's thickness is read every frame and the tier cannot change
	 * while the drop lies there.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	int32 MaterialTier = 0;

	/**
	 * How much gold this pile holds, or zero when the drop is gear or a crafting material. Issue #2742.
	 *
	 * ONE ACTOR FOR A THIRD KIND, for the reason given above for materials: lying on a floor, a name, a colour and
	 * a position are the same for gold. What differs is where it goes when taken: NOT INTO THE INVENTORY, because gold
	 * is not an item (`docs/Cataclysm_GDD_v2.md`, Storage), but into the balance of the character playing -- its
	 * lethality mode's, held by the account, or a Solo Self-Found character's own (`UCataclysmDropPickup::BankGold`).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	int32 Gold = 0;

	/** Whether this drop is crafting materials rather than a piece of gear. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	bool IsMaterial() const
	{
		return !Material.IsNone() && MaterialQuantity > 0;
	}

	/** Whether this drop is a gold pile. Issue #2742. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	bool IsGold() const
	{
		return Gold > 0;
	}

	/**
	 * Whether this drop is an interchangeable quantity -- a crafting material or gold -- and so collected without a
	 * click and named at any distance. Issue #2742, ruled 2026-11-18 (G3): the owner's 2026-08-23 reason for
	 * materials, "a material is an interchangeable quantity and collecting it is not a decision", holds for gold.
	 */
	bool IsAQuantity() const
	{
		return IsMaterial() || IsGold();
	}

	/**
	 * The colour a gold pile's name is drawn in: a muted gold, `#D9C27A`. Issue #2742.
	 *
	 * NOT THE LEGENDARY GEAR YELLOW (`#FFD100`) and further from it than a hue alone: paler and less saturated, with
	 * the thinnest border where a Legendary item has five pixels, and the word "gold" in the name. A judgement, to be
	 * tuned by eye, which is why it is one named constant.
	 */
	static const FLinearColor GoldNameColour;

	/** "1,234": a whole amount with a comma every three digits, for a pile's name and the balance on screen. */
	static FString GoldAmountText(int64 Amount);

	/** A gold pile's name, and a balance as a screen shows it: "12 gold", "1,234 gold". */
	static FString GoldName(int64 Amount);

	/**
	 * Work out this drop's printed name, its colour, its rarity and its
	 * material tier from what the drop IS.
	 *
	 * WHY A DROP CAN NEED TELLING. A drop the spawner made was described as it
	 * was made, with tables the spawner had already loaded for the whole batch.
	 * A drop restored from a save record arrives with only its substance -- the
	 * item, or the material and how many -- because `FCataclysmSavedGroundItem`
	 * deliberately does not persist anything that can be worked out again. Issue
	 * #751.
	 *
	 * PERSISTING THE NAME WOULD BE WORSE THAN RECOMPUTING IT. An item renamed in
	 * the design workbook would come back off an old save under the name it had
	 * when it dropped, and nothing would ever correct it.
	 *
	 * IT LOADS THE DATA TABLES ITSELF, one drop at a time, which is why the
	 * spawner does not call it: the spawner is placing a whole kill's worth of
	 * drops in a loop and loads each table once for all of them.
	 * `Cataclysm.SaveApply.ARestoredDropIsDescribedTheSameWayASpawnedOneIs` is
	 * what keeps the two answers together.
	 *
	 * A MISSING TABLE LEAVES THE DROP UNDESCRIBED rather than failing. That is
	 * what the spawner does too, and each Load call has already said which table
	 * is missing and why.
	 */
	void DescribeItself();

	/** How far above the actor's own position the name is drawn, in
	 *  centimetres. Clear of the floor without floating. */
	static constexpr float NameHeightCm = 60.0f;
};

/**
 * Turning a kill into items on the floor.
 *
 * SEPARATE FROM THE ACTOR so that every decision can be tested without a world.
 * The automation test command runs with -nullrhi and spawning actors needs a
 * world; what can be tested anywhere is where each drop lands and how many
 * there are. The same split UCataclysmCombatOverlay uses, and for the same
 * reason.
 */
UCLASS()
class CATACLYSM_API UCataclysmDropSpawner : public UObject
{
	GENERATED_BODY()

public:
	// A DROP IS ROLLED AT THE TIER BEING PLAYED, and until issue #868 it was
	// rolled at a constant 8. The placeholder that held it lived here and said
	// "nothing in the engine knows what tier is being played". That stopped
	// being true: ACataclysmGameMode::DifficultyTierIn answers it and
	// SpawnDropsFor asks it. It is deleted rather than left at a value nothing
	// reads, because a constant named for a gap that has closed is the kind of
	// thing a later change quietly starts using again.
	//
	// TO SEE THE WHOLE RANGE OF A ROLL, which is what the placeholder was
	// really for, set `Cataclysm.DifficultyTier 8`.

	/**
	 * Where one of several drops from the same kill lands, relative to the
	 * corpse, in centimetres.
	 *
	 * SPREAD AROUND A CIRCLE rather than piled on one point, because a Cataclysm
	 * Boss drops twelve items and twelve names drawn at one position would be
	 * one unreadable stack. The radius grows with the count so a big drop does
	 * not crowd itself.
	 *
	 * FLAT, WITH NO HEIGHT. The floor decides that, and nothing here knows where
	 * the floor is. See the note on SpawnDropsFor.
	 */
	static FVector ScatterOffset(int32 Index, int32 Count);

	/**
	 * How far in front of a character a deliberately dropped item lands, in
	 * centimetres.
	 *
	 * WELL INSIDE PICKUP RANGE, WHICH IS THE POINT. The project owner decided on
	 * 2026-09-02 that dropping asks for no confirmation at any rarity, and the
	 * reason that is safe is that the item lies on the floor and can be picked
	 * straight back up. That is only true if it lands within
	 * UCataclysmDropPickup::PickupRangeCm, which is 300, so this is half of it
	 * and a mis-click costs nothing. Guarded by
	 * Cataclysm.DropPickup.ADroppedItemLandsWithinReachOfTheCharacterWhoDroppedIt.
	 *
	 * IN FRONT RATHER THAN UNDER FOOT so the name the heads-up display draws is
	 * not hidden behind the character.
	 */
	static constexpr float DropInFrontCm = 150.0f;

	/**
	 * Where an item dropped by this character should land.
	 *
	 * FLAT, WITH NO HEIGHT CHANGE, for the same reason ScatterOffset is: nothing
	 * here knows where the floor is. It does not matter for reaching the drop
	 * again, because UCataclysmDropPickup::IsWithinPickupRange ignores height,
	 * and it cannot strand the item, because ACataclysmDroppedItem has no
	 * physics and no mesh -- it stays exactly where it is put.
	 */
	static FVector DropSpotInFrontOf(const AActor& Character);

	/**
	 * Put one particular item on the floor. Nothing is rolled.
	 *
	 * LIFTED OUT OF THE SAVE SYSTEM, where it was a private static called
	 * `FCataclysmSaveApply::GroundItemInto` and was the only code in the project
	 * that could put a KNOWN item on the ground -- everything else rolls a new
	 * one. Restoring a floor from a save and dropping an item out of the bag are
	 * the same operation, and issues #1176 and #1190 both needed it. One copy,
	 * here beside the two spawners that roll.
	 *
	 * @param Item              the gear item, or a default one when dropping a
	 *                          crafting material
	 * @param Material          the material's row key, or None for gear
	 * @param MaterialQuantity  how many, or 0 for gear
	 *
	 * @return the actor, or null when the world was missing or the spawn was
	 *         refused. A caller that is taking the item from somewhere else
	 *         MUST check this before removing it, or the item is destroyed.
	 */
	static ACataclysmDroppedItem* PutOnTheFloor(UWorld* World, const FVector& At,
												const FCataclysmItem& Item,
												FName Material,
												int32 MaterialQuantity,
												int32 Gold = 0);

	/** The colour an item of this rarity has its name drawn in, or white when
	 *  the table is missing it. */
	static FLinearColor ColourFor(const UDataTable* GearRarityTable,
								  ECataclysmRarity Rarity);

	/**
	 * The row key of the enemy drop table row whose Step is this.
	 *
	 * ACataclysmEnemyCharacter knows its rarity as a Step from 0 to 5, and the
	 * drop table is keyed on the rarity's name. This is the join, and it reads
	 * the table rather than holding a second copy of the ladder.
	 */
	static FName EnemyRarityForStep(const UDataTable* EnemyDropTable,
									int32 Step);

	/**
	 * Roll everything a kill drops and put it on the floor.
	 *
	 * @param At  where the kill happened. Each drop is offset from it by
	 *            ScatterOffset; nothing here traces to the floor, so a drop on
	 *            a slope sits at the corpse's height rather than the ground's.
	 *            That is the same limitation the telegraph markers have, which
	 *            is issue #690.
	 *
	 * @param LowTierMaterialExtraChance  Scavenger's chance for each tier 1 to 3
	 *            material to be a stack of two (`SpawnMaterialsFor`); nought, every
	 *            caller's but a kill's, rolls nothing
	 * @param LowestRarity  the lowest rarity its gear may roll (`RollItem`);
	 *            Everyday, every caller's but The High Roller's, is the
	 *            ordinary roll
	 *
	 * @return how many actors were spawned
	 */
	static int32 SpawnDropsFor(UWorld* World, int32 EnemyRarityStep,
							   float MagicFind, float LootQuantity,
							   const FVector& At, FRandomStream& Stream,
							   bool bMarked = false, bool bInfested = false,
							   int32 GearCountGiven = -1, int32 MaterialCountGiven = -1,
							   float LowTierMaterialExtraChance = 0.0f,
							   ECataclysmRarity LowestRarity = ECataclysmRarity::Everyday);

	/** The rung named Elite, which Opportunistic Looting asks for; and the first boss rung, as
	 *  `ACataclysmEnemyCharacter::IsBoss` reads it, which The High Roller asks for. Issue #2741 slice 5b. */
	static constexpr int32 EliteRarityStep = 1;
	static constexpr int32 FirstBossRarityStep = 4;

	/** The highest crafting material tier Scavenger's "T3 and below" reaches, tiers counted 1 to 5, commonest first. */
	static constexpr int32 ScavengerHighestTier = 3;

	/**
	 * The empire upgrade tree's extra drops for one kill, rolled after everything else the kill drops so every draw
	 * before them is as it was. Issue #2741 slice 5b, each reading the coordinating session's D6 and E1 of 2026-11-19:
	 *
	 *   Opportunistic Looting  an Elite (the rung named Elite, no other) drops one more gear item, rolled as its others
	 *   Plunderer's Eye        any kill drops one more crafting material
	 *   The High Roller        a Boss or Cataclysm Boss drops one more gear item of Legendary or rarer
	 *
	 * EACH THROUGH `SpawnDropsFor` WITH ITS COUNT GIVEN, as The Infested Hoard's one extra drop is, so the item, its
	 * rarity, its name and its colour are made as any drop's are; Scavenger's chance reaches the extra material too. A
	 * chance of nought draws nothing from the stream, so a kill with none of these nodes bought rolls exactly what it
	 * rolled before. KILLS ONLY (E1): a dungeon rule's reward of drops is not a kill and does not reach here.
	 *
	 * @return how many actors were spawned
	 */
	static int32 SpawnTheTreesExtraDropsFor(UWorld* World, int32 EnemyRarityStep, float MagicFind, float LootQuantity,
											const FVector& At, FRandomStream& Stream,
											const FCataclysmEmpireTreeDropRules& Rules, bool bMarked = false);

	/**
	 * The Infested Hoard's one extra drop: exactly one, gear or a crafting material in the ratio of the creature's
	 * own expected gear to materials, rolled at its rarity and magic find as its own drops are, and infested.
	 * Issues #1820 and #41.
	 *
	 * THROUGH `SpawnDropsFor` WITH ITS COUNTS GIVEN rather than rolled, so the item, its rarity, its name and its
	 * scatter are made exactly as every other drop's are.
	 *
	 * @return how many actors were spawned: one, or none when the roll failed
	 */
	static int32 SpawnOneInfestedDropFor(UWorld* World, int32 EnemyRarityStep, float MagicFind,
										 const FVector& At, FRandomStream& Stream);

	/**
	 * Whether a kill in this world drops gold: the character playing has a balance to put it in
	 * (`UCataclysmGameInstance::GoldHeldBy`). Issue #2742.
	 *
	 * GOLD BELONGS TO A BALANCE, so a kill with no character playing drops none, and neither does one whose account
	 * record would not read. That is every automation test that kills a creature with no player, which therefore drops
	 * what it dropped before. A SOLO SELF-FOUND CHARACTER'S KILLS DROP GOLD into its own balance: the owner ruled so on
	 * 2026-11-18, reversing the coordinating session's G5.
	 */
	static bool GoldDropsIn(const UWorld* World);

	/**
	 * Roll the gold piles a kill drops and put them on the floor. Issue #2742.
	 *
	 * HOW MANY, from the creature's rung (`UCataclysmDropRoll::ExpectedGoldDrops`), and HOW MUCH EACH, from the tier
	 * being played and the floor (`UCataclysmDropRoll::RollGoldPile`): the Diablo II shape, more piles from a rarer
	 * monster and a pile's value from where it died. Neither magic find nor loot quantity enters (G2).
	 *
	 * CALLED BY A CREATURE'S OWN DEATH AND BY NOTHING ELSE, after its gear and materials, so the gold draws on the
	 * kill's stream after them and leaves every item they roll as it was. A dungeon rule's reward of drops is not a
	 * kill and pays no gold (ruled 2026-11-18).
	 *
	 * ON A RING OF ITS OWN, `GoldScatterOffset`, so a pile does not land on the spot a lone item takes.
	 *
	 * @return how many piles were placed
	 */
	static int32 SpawnGoldFor(UWorld* World, int32 EnemyRarityStep, const FVector& At, FRandomStream& Stream);

	/**
	 * Where one of a kill's gold piles lands, relative to the corpse: a ring one step wider than `ScatterOffset` gives
	 * the same count, turned half a step, so a pile never shares a spot with an item from the same kill -- including
	 * the lone item, which `ScatterOffset` puts on the corpse.
	 */
	static FVector GoldScatterOffset(int32 Index, int32 Count);

	/**
	 * The magic find and loot quantity the player in this world is carrying.
	 *
	 * UNTIL ISSUE #896 NOTHING ASKED. Every kill passed 0% magic find and the
	 * 100% loot quantity baseline, with a comment saying that nothing computed
	 * a character's stats at the moment of a kill. That had stopped being true:
	 * UCataclysmEquipmentComponent::RefreshAttributes writes them on every
	 * equipment change, so three affixes were granting numbers no roll read.
	 *
	 * THE BASELINES ARE ANSWERED WHEN THERE IS NOBODY TO ASK, and that is the
	 * right answer rather than a failure. A world with no player is every
	 * automation test that kills a creature directly, and the baselines are
	 * what those kills used before this existed, so none of them changes.
	 *
	 * THE FIRST PLAYER CONTROLLER'S PAWN, WHICH IS THE ONLY PLAYER THERE IS.
	 * Co-operative play is issue #56 and will have to decide whose magic find
	 * applies to a shared kill; there is one answer to that question today.
	 *
	 * @param OutMagicFind     added percentage, 0 for a character with none
	 * @param OutLootQuantity  percentage of normal, 100 for one with none
	 */
	static void PlayerLootStats(const UWorld* World, float& OutMagicFind,
								float& OutLootQuantity);

	/**
	 * The magic find and loot quantity a particular character is carrying.
	 *
	 * SPLIT FROM PlayerLootStats BECAUSE READING AND FINDING ARE TWO JOBS. When
	 * they were one function a failure could not say which half was wrong, and
	 * the first version of the test for it hit exactly that: it read back the
	 * baselines and there was no way to tell whether the character had not been
	 * found or its attributes had not been read.
	 *
	 * @param OutMagicFind     added percentage, 0 for a character with none
	 * @param OutLootQuantity  percentage of normal, 100 for one with none
	 */
	static void LootStatsOf(const AActor* Character, float& OutMagicFind,
							float& OutLootQuantity);

	/**
	 * Roll the crafting materials a kill drops and put them on the floor.
	 *
	 * CALLED BY SpawnDropsFor RATHER THAN BY A KILL. Materials come at twice the
	 * gear rate on their own separate roll -- chosen by the project owner on
	 * 2026-08-18, so an empire node raising material quantity does not also
	 * reduce gear -- but they come from the same kill, so nothing outside has to
	 * remember to ask for them.
	 *
	 * HOW MANY IS PASSED IN RATHER THAN ROLLED HERE. SpawnDropsFor rolls both
	 * counts before it spawns anything, so gear and materials can share one
	 * scatter circle; a material that worked out its own total would sit at a
	 * different radius from the gear and could land on top of it.
	 *
	 * THE DIFFICULTY TIER IS PASSED IN RATHER THAN ASKED FOR HERE, though this
	 * has the world to ask with. SpawnDropsFor already resolved it for the gear,
	 * and one kill's gear and materials answering differently would be a bug
	 * nothing could see.
	 *
	 * @param Count              how many materials to place
	 * @param AlreadyOnTheFloor  how many places on the circle the gear took, so
	 *                           the materials continue round it
	 * @param TotalDrops         how many drops this kill produces in all, which
	 *                           is what sets the circle's size
	 * @param DifficultyTier     which of the eight tiers is being played, which
	 *                           caps the upgrade stones this may drop
	 * @param LowTierMaterialExtraChance  Scavenger's chance for each material of
	 *                           tier 1 to 3 to be a stack of two (E2): the same
	 *                           drop holding two, not a second drop on the floor.
	 *                           Rolled only above nought, so nothing else draws
	 *                           differently
	 *
	 * @return how many actors were spawned
	 */
	static int32 SpawnMaterialsFor(UWorld* World, FName EnemyRarity,
								   float MagicFind, const FVector& At,
								   int32 Count, int32 AlreadyOnTheFloor,
								   int32 TotalDrops, int32 DifficultyTier,
								   FRandomStream& Stream, bool bMarked = false,
								   bool bInfested = false,
								   float LowTierMaterialExtraChance = 0.0f);
};

/**
 * Picking a drop up: what is clickable, what is close enough, and what happens.
 *
 * SEPARATE FROM BOTH THE ACTOR AND THE HEADS-UP DISPLAY, for the reason
 * UCataclysmDropSpawner gives and one more. AHUD::PostRender checks
 * FApp::CanEverRender() before calling DrawHUD, and the automation test command
 * passes -nullrhi, so nothing that runs inside a draw call can be tested at all.
 * Keeping the four judgements here -- which drops are near enough to name, which
 * name the cursor is over, whether the character is near enough to take one, and
 * what moving the item does -- leaves all four covered while the drawing itself
 * stays untested.
 *
 * THE FIRST OF THE FOUR ARRIVED WITH ISSUE #1116, and it is the reason that
 * sentence used to say three. It was written inside ACataclysmHUD::DrawDropNames
 * to begin with, where nothing could reach it.
 */
UCLASS()
class CATACLYSM_API UCataclysmDropPickup : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * How close the character has to be to take an item, in centimetres.
	 *
	 * THREE METRES, FROM DIABLO'S THREE YARDS. That is the default pickup
	 * radius across the Diablo games, and it is the only published figure the
	 * genre offers: Path of Exile increased its pickup range in 3.25 without
	 * stating either number, and Last Epoch publishes none. So this is the right
	 * order of magnitude taken from a shipped game rather than a measured
	 * equivalent, which is the same footing as the 0.16 drops a Common enemy
	 * gives.
	 *
	 * IT IS NOT THE SAME THING AS DIABLO'S RADIUS AND THE DIFFERENCE MATTERS.
	 * There the radius is what a character sweeps up automatically by walking
	 * over it; this one is how near a click has to happen from.
	 *
	 * SINCE ISSUE #851 A CRAFTING MATERIAL IS SWEPT UP, and this sentence used
	 * to say nothing in this project was. Gear still is not: a piece of gear is
	 * a decision and has to be walked to and looked at, and a material is not.
	 * AutomaticMaterialRangeCm below is that radius.
	 *
	 * A CLICK FROM FURTHER AWAY IS NOT REFUSED. The character walks to the drop
	 * and takes it on arrival, which is what every game in the genre does.
	 * ACataclysmPlayerController holds that part.
	 *
	 * BUT SINCE ISSUE #1116 IT CANNOT COME FROM FURTHER THAN
	 * NameShownRangeCm, and that sentence used to have no upper bound at all. A
	 * click finds a drop by the name drawn over it, and no name is drawn past
	 * ten metres, so ten metres is now the furthest a walk-and-collect can be
	 * started from. The project owner chose that on 2026-08-31 rather than
	 * having it arrive as a side effect.
	 *
	 * EXPECTED TO MOVE ONCE IT HAS BEEN PLAYED. Three metres is about a third of
	 * the way across the screen at the default camera distance, and whether that
	 * reads as generous or fiddly is not something the number can settle.
	 */
	static constexpr float PickupRangeCm = 300.0f;

	/**
	 * How near a character has to be for a drop's name to be drawn over it, in
	 * centimetres. Issue #1116.
	 *
	 * TEN METRES, ASKED FOR BY THE PROJECT OWNER ON 2026-08-31 after a play
	 * session where the names covered the screen. Nothing limited them before:
	 * ACataclysmHUD::DrawDropNames drew a tag for every drop in the level at any
	 * distance, and SeparateOverlappingNames below then pushed the ones that
	 * collided apart, so a distant kill's worth of names spread across the view
	 * rather than stacking in one place where they could be ignored.
	 *
	 * MORE THAN THREE TIMES PickupRangeCm, AND THAT ORDERING IS THE POINT. A
	 * name has to appear before the item it names is reachable, or there is
	 * nothing to walk towards; a player reads the name, decides, and then
	 * approaches.
	 *
	 * WRITTEN AS A DISTANCE RATHER THAN AS A MULTIPLE OF PickupRangeCm, unlike
	 * AutomaticMaterialRangeCm below. This is a statement about how much of the
	 * screen should carry text, which the camera decides; the click range is a
	 * statement about arm's reach. Tying them together would drag this every
	 * time the reach was tuned, and they are not the same question.
	 *
	 * NEITHER GAME IN THE GENRE PUBLISHES ITS OWN FIGURE. Path of Exile and Last
	 * Epoch both limit ground labels to a radius around the character and
	 * neither states it, so ten metres is the project owner's judgement from
	 * play rather than a number copied from a shipped game. Expected to be tuned
	 * by eye, the same footing as PickupRangeCm above.
	 *
	 * IT APPLIES TO GEAR AND NOT TO A CRAFTING MATERIAL, which is named at any
	 * distance. Issue #1117. It has to: this range is SHORTER than
	 * AutomaticMaterialRangeCm below, the sweep runs every frame, so a material
	 * was only ever still lying there when it was already too far away to have a
	 * tag. The two windows did not overlap and a material's name was never drawn
	 * at all. The project owner chose on 2026-09-01 to exempt materials rather
	 * than accept that. UCataclysmDropPickup::DropsToName is where the exemption
	 * is, beside the reasoning for it.
	 */
	static constexpr float NameShownRangeCm = 1000.0f;

	/**
	 * How near a character has to be for a crafting material to come to it
	 * without being clicked. Issue #851.
	 *
	 * FIVE TIMES THE CLICK RANGE, decided by the project owner on 2026-08-23,
	 * and written as a multiple rather than as 1500 so the two move together.
	 * Tuning the click range and leaving this behind would be the kind of
	 * silent drift a second copy of a number always produces.
	 *
	 * MATERIALS ONLY, AND THAT IS THE WHOLE RULE. The project owner asked for
	 * automatic collection on 2026-08-23 and the issue is explicit about the
	 * scope: a piece of gear is a decision and has to be walked to and looked
	 * at, so collecting one without being asked would take that decision away.
	 * A material is a quantity of an interchangeable thing and collecting it
	 * costs the player nothing.
	 *
	 * FIFTEEN METRES IS NEARLY FOUR DUNGEON CELLS, which are 400 cm each, so
	 * this reaches most of the way across a room. That is what an automatic
	 * pickup is for -- Last Epoch and Diablo IV both collect materials and
	 * currency by walking near them -- and it is a tuning value like the click
	 * range above.
	 */
	static constexpr float AutomaticMaterialRangeCm = PickupRangeCm * 5.0f;

	/**
	 * Whether a character standing here can reach a drop lying there.
	 *
	 * MEASURED FLAT, IGNORING HEIGHT. A drop is spawned at the height of the
	 * corpse that produced it and nothing traces it to the floor, which is the
	 * same limitation the telegraph markers have and is issue #690. Measuring in
	 * three dimensions would make a drop from a tall creature, or one that died
	 * on a step, quietly harder to pick up than the same drop on flat ground.
	 */
	static bool IsWithinPickupRange(const FVector& Character, const FVector& Drop);

	/**
	 * Whether a character standing here is near enough for a drop lying there
	 * to have its name drawn over it. Issue #1116.
	 *
	 * A FUNCTION OVER TWO POSITIONS RATHER THAN A METHOD ON THE DROP, the same
	 * shape as IsWithinPickupRange above and for the same reason: building an
	 * ACataclysmDroppedItem needs a world, and the automation command runs with
	 * -nullrhi, so a rule that took the actor could not be tested at all. The
	 * drawing itself stays untested and this does not.
	 *
	 * MEASURED FLAT, IGNORING HEIGHT, for the reason IsWithinPickupRange gives:
	 * a drop from a tall creature, or one that died on a step, must not behave
	 * differently from the same drop on flat ground. Here the consequence would
	 * be a tag that flickered out as a player walked under a ledge the item was
	 * lying on.
	 *
	 * IT DECIDES WHAT IS CLICKABLE AS WELL AS WHAT IS VISIBLE, because
	 * ACataclysmHUD::DropUnderPoint tests a click against the rectangles
	 * ACataclysmHUD::DrawDropNames filled and this decides which drops get one.
	 */
	static bool IsWithinNameRange(const FVector& Character, const FVector& Drop);

	/**
	 * Every drop lying in this world that should have its name drawn, given
	 * where the character is standing. Issue #1116.
	 *
	 * WHY THE WALK IS HERE AND NOT IN THE DRAW CALL. It is the fourth judgement
	 * of the kind this class exists to hold, and it is the one the issue's
	 * acceptance is written about: which drops get a tag and therefore which
	 * ones can be clicked. Left inside ACataclysmHUD::DrawDropNames it could not
	 * be checked at all -- AHUD::PostRender tests FApp::CanEverRender() and the
	 * automation command passes -nullrhi -- so "a drop past ten metres draws no
	 * tag" would have been a claim resting on reading the code. Here it is
	 * driven with real drops spawned at real distances in a test world.
	 *
	 * IT TAKES A POSITION RATHER THAN FINDING THE PLAYER ITSELF, so the answer
	 * does not depend on there being a possessed pawn. The heads-up display
	 * already has one to hand.
	 *
	 * A CRAFTING MATERIAL IS NAMED AT ANY DISTANCE AND GEAR IS NOT. Issue #1117,
	 * decided by the project owner on 2026-09-01. The ten metre rule is shorter
	 * than the fifteen metre automatic sweep, so a material was only ever still
	 * lying there when it was already too far away to be named, and its name was
	 * therefore never drawn at all. The body of this function carries the whole
	 * reasoning.
	 *
	 * WHAT IT DOES NOT DECIDE IS WHETHER A DROP IS ON SCREEN. Rejecting what is
	 * behind the camera needs the projection, which needs the canvas, so
	 * DrawDropNames keeps that half. The split is between what the world knows
	 * and what only a frame knows.
	 *
	 * @param OutDrops  emptied before anything is added, so a caller may reuse
	 *                  one array frame after frame
	 */
	static void DropsToName(const UWorld* World, const FVector& Standing,
							TArray<ACataclysmDroppedItem*>& OutDrops);

	/**
	 * Whether a drop comes to the character on its own. Issue #851.
	 *
	 * TAKES WHETHER IT IS A MATERIAL RATHER THAN THE DROP ITSELF, so the rule
	 * is a function over plain values and a test can watch it. Building an
	 * ACataclysmDroppedItem needs a world; deciding this does not.
	 *
	 * MEASURED FLAT, IGNORING HEIGHT, for the same reason IsWithinPickupRange
	 * is: a drop from a tall creature, or one that died on a step, must not be
	 * quietly harder to collect than the same drop on flat ground.
	 *
	 * GOLD COMES AS A MATERIAL DOES, within the same fifteen metres. Issue #2742, ruled 2026-11-18 (G3): Path of
	 * Exile 2's gold is "proximity auto-pickup" and Diablo IV's and Last Epoch's are collected by walking near them;
	 * the radius is this game's material radius, because the owner's reason for sweeping materials holds for gold.
	 *
	 * @param bIsMaterial  ACataclysmDroppedItem::IsAQuantity for the drop: a crafting material or a gold pile. The
	 *                     name is the one it had before gold existed.
	 * @return false for gear at any distance
	 */
	static bool ComesAutomatically(bool bIsMaterial, const FVector& Character,
								   const FVector& Drop, bool bInfested = false);

	/**
	 * Takes gold piles off the floor into the balance of the character playing, and answers how much gold that was.
	 * Issue #2742.
	 *
	 * INTO THE BALANCE THE CHARACTER PLAYING HAS (`UCataclysmGameInstance::AddGoldFor`): its lethality mode's in the
	 * account record, or a Solo Self-Found character's own, which its character record saves.
	 *
	 * ALL OR NOTHING, AND NOTHING LEAVES THEM LYING THERE, as a full inventory leaves an item: no character playing,
	 * or -- for a character that is not Solo Self-Found -- no game instance of this project's or an account record
	 * that would not read (`UCataclysmGameInstance::AccountFor`). The piles are destroyed only after the gold is in the
	 * balance.
	 *
	 * ONE BANKING AND ONE SET OF SAVE TRIGGERS FOR THE WHOLE LIST, which is why it takes a list: the sweep collects
	 * every pile in reach in one frame. `AccountChanged` when the gold went to the account, and `InventoryChanged`
	 * always, which writes the run record (the floor without the piles) and the character record. The gold is added
	 * to `UCataclysmGameInstance::GoldPickedUpThisWalk` as well, when there is a game instance, which the PLAY LOOP
	 * line at a walk's end reports.
	 *
	 * NOT ANNOUNCED AS LOOT TAKEN. `UCataclysmCombatEvents::OnLootTaken` is what the dungeon rules answer, and no rule
	 * pays out on gold (ruled 2026-11-18).
	 *
	 * @return the gold banked; 0 when nothing was, and then every pile is still lying where it was
	 */
	static int64 BankGold(UWorld* World, const TArray<ACataclysmDroppedItem*>& Piles);

	/**
	 * Which of the drawn names the cursor is over, or INDEX_NONE for none.
	 *
	 * THE NAME IS THE CLICKABLE THING, not a model and not the ground under it.
	 * That is the project owner's decision of 2026-08-18 and it is what the
	 * genre does -- Last Epoch's players click ground labels and ask for the
	 * label hitbox to be made bigger. Diablo IV went the other way in patch
	 * 1.4.2, making the model the only target and the label inert, and its
	 * players complained; this design follows the label.
	 *
	 * THE LAST MATCH WINS, because the heads-up display draws the names in order
	 * and the last one drawn is the one on top. Two drops close together overlap
	 * on screen, and the one the player can actually read is the one they mean.
	 */
	static int32 IndexOfNameUnderPoint(const TArray<FBox2D>& Rects,
									   const FVector2D& Point);

	/**
	 * Clear space to leave between two names that would otherwise touch, in
	 * pixels.
	 *
	 * FOUR IS A STARTING POINT AND IS EXPECTED TO BE TUNED BY EYE. It is enough
	 * to read two stacked names as two rather than as one block of text, and
	 * small enough that five names from one kill still sit near the corpse. No
	 * test can see this; the project owner can.
	 */
	static constexpr float NameGapPx = 4.0f;

	/**
	 * Moves names down until no two of them overlap, in place.
	 *
	 * WHY THIS IS NEEDED EVEN THOUGH THE DROPS ARE SPREAD OUT IN THE WORLD.
	 * UCataclysmDropSpawner::ScatterOffset puts several drops from one kill
	 * around a circle on the ground, which separates them in world space. It
	 * does not separate their NAMES on screen: the camera looks down, so two
	 * drops on opposite sides of that circle can land at almost the same screen
	 * height, and an item name is far wider than it is tall. The project owner
	 * asked for this on 2026-08-19, having seen the names printed over each
	 * other. Issue #723.
	 *
	 * DOWNWARD ONLY, AND THE HIGHEST NAME NEVER MOVES. Something has to stay
	 * still or the whole group drifts as the camera turns, and the top one is
	 * the one furthest from the player at this camera angle, so pushing the rest
	 * down keeps a name closer to the item it belongs to than pushing them up
	 * would.
	 *
	 * THE NAME MOVES AND THE ITEM DOES NOT, so a name can end up a little above
	 * or below the drop it belongs to. That is the trade Path of Exile makes
	 * too, and it is the right one: a name that cannot be read identifies
	 * nothing, and clicking still works because the rectangle that moved is the
	 * same rectangle a click is tested against.
	 *
	 * INVALID RECTANGLES ARE LEFT ALONE and take part in nothing. A drop that
	 * failed to project has no place on screen to be pushed away from.
	 */
	static void SeparateOverlappingNames(TArray<FBox2D>& Rects, float GapPx);

	/**
	 * How thick the border around a drop's name is, in pixels, for its rarity.
	 *
	 * ONE PIXEL FOR Everyday AND ONE MORE A RUNG, so Cataclysmic sits inside
	 * eight. Decided by the project owner on 2026-08-19 and stated in the
	 * Interface Colour section of `docs/Cataclysm_GDD_v2.md`. Issue #718.
	 *
	 * WHY A NAME NEEDS ANYTHING BUT ITS COLOUR. That section requires it: "the
	 * frame and the drop marker must differ by shape or motion as well as by
	 * colour", because a player who cannot separate two hues still has to
	 * separate two rarities. About 8% of men have red-green colour blindness and
	 * the ramp puts green, yellow, orange and red on four adjacent rungs. Until
	 * this the requirement was stated and unmet.
	 *
	 * THICKNESS RATHER THAN AN ICON OR MOTION. An icon needs an art asset and
	 * this heads-up display draws on the canvas with no content of any kind;
	 * motion costs a tick on an actor that deliberately has none and makes a
	 * name harder to click. `docs/DECISIONS.md` carries the full comparison.
	 *
	 * DERIVED FROM THE ENUM RATHER THAN TABULATED. `ECataclysmRarity` already
	 * states the order, so a second list here would be a second answer that
	 * could disagree with it. That is the same reasoning
	 * `FCataclysmGearRarityRow` uses for having no Step column.
	 */
	static int32 NameBorderThicknessFor(ECataclysmRarity Rarity);

	/**
	 * How thick the border around a crafting material's name is, for its tier.
	 *
	 * ONE PIXEL A TIER, so Common sits inside one and Extremely Rare inside
	 * five. The same rule the gear names follow and the same requirement it
	 * answers: the Interface Colour section of `docs/Cataclysm_GDD_v2.md` says
	 * the drop marker must differ by shape or motion as well as by colour, and a
	 * material's name is a drop marker.
	 *
	 * A TIER 3 MATERIAL AND A SUPERB GEAR ITEM THEREFORE SHARE A THICKNESS. That
	 * is accepted rather than overlooked: the two are told apart by hue family
	 * -- every material is cyan and no gear rarity is -- and by the words in the
	 * name. A third shape channel to separate the categories would be one more
	 * thing for a player to learn. `docs/DECISIONS.md` records it.
	 */
	static int32 NameBorderThicknessForMaterialTier(int32 Tier);

	/** The border thickness for whichever kind a drop is. */
	static int32 NameBorderThicknessOf(const ACataclysmDroppedItem& Drop);

	/** The thinnest border any rarity gets. Everyday's. */
	static constexpr int32 ThinnestNameBorderPx = 1;

	/**
	 * Clear space between a name and the border around it, in pixels.
	 *
	 * WITHOUT IT THE BORDER TOUCHES THE TEXT, and at eight pixels thick it would
	 * eat into the letters of a Cataclysmic name. Three is enough to read the
	 * two as separate things at the sizes this draws.
	 */
	static constexpr int32 NameBorderPaddingPx = 3;

	/**
	 * The whole tag a rarity's name occupies: the text, its padding and its
	 * border.
	 *
	 * WHAT THE PLAYER CLICKS AND WHAT MUST NOT OVERLAP is the tag rather than
	 * the text inside it, so this is what SeparateOverlappingNames is given and
	 * what IndexOfNameUnderPoint tests a click against. A Cataclysmic name is 22
	 * pixels wider and taller than its text on every side; leaving that out
	 * would let two tags print over each other while their texts did not, and
	 * would leave a click on the border finding nothing.
	 *
	 * IT TAKES THE THICKNESS RATHER THAN THE RARITY, because a crafting material
	 * has a tier and no rarity and needs the same tag. NameBorderThicknessOf
	 * answers the thickness for whichever kind a drop is.
	 */
	static FBox2D TagAround(const FBox2D& Text, int32 BorderThickness);

	/**
	 * Moves a drop's item into an inventory and takes the drop off the floor.
	 *
	 * @return true when the item was taken. **False leaves everything as it
	 *         was**, with the drop still lying there, which is what a full
	 *         inventory has to do: there is no way out of a dungeon partway
	 *         through, so an item that will not fit stays on the floor and the
	 *         player decides what is worth a slot.
	 *
	 * THE ACTOR IS DESTROYED ONLY AFTER THE ITEM IS SAFELY IN A SLOT. The other
	 * order would destroy the item whenever the inventory was full.
	 *
	 * A TAKE IS ANNOUNCED on `UCataclysmCombatEvents::OnLootTaken`, saying whether
	 * it was by hand. Only the player controller's click passes true.
	 *
	 * A GOLD PILE IS BANKED RATHER THAN CARRIED, through `BankGold`, and is not
	 * announced: a click on one that the sweep has not taken lands here. Issue #2742.
	 */
	static bool TakeInto(UCataclysmInventoryComponent* Inventory,
						 ACataclysmDroppedItem* Drop, bool bByHand = false);
};
