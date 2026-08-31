// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/CataclysmItem.h"
#include "Math/Box2D.h"
#include "CataclysmDroppedItem.generated.h"

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
 * PICKING IT UP IS NOT BUILT. There is no inventory to put an item into -- the
 * design fixes the carried inventory at 48 slots and none of it exists yet --
 * so this can be dropped and read and not yet taken. Issue #707.
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
	 * Which material tier this drop is, 1 to 5. Zero when the drop is gear.
	 *
	 * KEPT BESIDE THE MATERIAL for the reason Rarity is kept beside the item:
	 * the border's thickness is read every frame and the tier cannot change
	 * while the drop lies there.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Drop")
	int32 MaterialTier = 0;

	/** Whether this drop is crafting materials rather than a piece of gear. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Drop")
	bool IsMaterial() const
	{
		return !Material.IsNone() && MaterialQuantity > 0;
	}

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
	 * @return how many actors were spawned
	 */
	static int32 SpawnDropsFor(UWorld* World, int32 EnemyRarityStep,
							   float MagicFind, float LootQuantity,
							   const FVector& At, FRandomStream& Stream);

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
	 *
	 * @return how many actors were spawned
	 */
	static int32 SpawnMaterialsFor(UWorld* World, FName EnemyRarity,
								   float MagicFind, const FVector& At,
								   int32 Count, int32 AlreadyOnTheFloor,
								   int32 TotalDrops, int32 DifficultyTier,
								   FRandomStream& Stream);
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
	 * IT IS SHORTER THAN AutomaticMaterialRangeCm BELOW, WHICH MEANS A CRAFTING
	 * MATERIAL IS NEVER NAMED. The sweep runs every frame and takes any material
	 * within fifteen metres, so a material is only still lying there when it is
	 * too far away to have a tag. That is recorded as issue #1117 rather than
	 * fixed here: it may be exactly what is wanted, since a material collects
	 * itself and its name is never something a player has to click.
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
	 * @param bIsMaterial  ACataclysmDroppedItem::IsMaterial for the drop
	 * @return false for gear at any distance
	 */
	static bool ComesAutomatically(bool bIsMaterial, const FVector& Character,
								   const FVector& Drop);

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
	 */
	static bool TakeInto(UCataclysmInventoryComponent* Inventory,
						 ACataclysmDroppedItem* Drop);
};
