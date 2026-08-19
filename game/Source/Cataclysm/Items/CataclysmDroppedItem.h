// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/CataclysmItem.h"
#include "CataclysmDroppedItem.generated.h"

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
