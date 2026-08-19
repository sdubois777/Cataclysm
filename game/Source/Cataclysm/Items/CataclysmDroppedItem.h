// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/CataclysmItem.h"
#include "Math/Box2D.h"
#include "CataclysmDroppedItem.generated.h"

class UCataclysmInventoryComponent;
class UDataTable;

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
	/**
	 * The difficulty tier a drop is rolled at until something tracks one.
	 *
	 * NOTHING IN THE ENGINE KNOWS WHAT TIER IS BEING PLAYED. The dungeon
	 * runtime is issue #41 and does not exist, so there is no dungeon to ask.
	 * This is a placeholder, and it is the deepest tier on purpose: it is the
	 * only value at which every rarity and every affix tier is reachable, so a
	 * drop seen in the editor exercises the whole roll rather than a slice of
	 * it. Replace it with the dungeon's own tier when there is one.
	 */
	static constexpr int32 UntrackedDifficultyTier = 8;

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
};

/**
 * Picking a drop up: what is clickable, what is close enough, and what happens.
 *
 * SEPARATE FROM BOTH THE ACTOR AND THE HEADS-UP DISPLAY, for the reason
 * UCataclysmDropSpawner gives and one more. AHUD::PostRender checks
 * FApp::CanEverRender() before calling DrawHUD, and the automation test command
 * passes -nullrhi, so nothing that runs inside a draw call can be tested at all.
 * Keeping the three judgements here -- which name the cursor is over, whether
 * the character is near enough, and what moving the item does -- leaves all
 * three covered while the drawing itself stays untested.
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
	 * over it; here nothing is automatic and this is how near a click has to
	 * happen from. Nothing in this project picks anything up by walking over it.
	 *
	 * A CLICK FROM FURTHER AWAY IS NOT REFUSED. The character walks to the drop
	 * and takes it on arrival, which is what every game in the genre does.
	 * ACataclysmPlayerController holds that part.
	 *
	 * EXPECTED TO MOVE ONCE IT HAS BEEN PLAYED. Three metres is about a third of
	 * the way across the screen at the default camera distance, and whether that
	 * reads as generous or fiddly is not something the number can settle.
	 */
	static constexpr float PickupRangeCm = 300.0f;

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
