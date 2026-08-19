// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmDroppedItem.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmInventoryComponent.h"

ACataclysmDroppedItem::ACataclysmDroppedItem()
{
	// NOTHING TICKS. The name is worked out at spawn and the heads-up display
	// draws it; a drop on the floor has nothing to do between those two.
	PrimaryActorTick.bCanEverTick = false;

	// AN ACTOR WITH NO ROOT COMPONENT CANNOT BE ANYWHERE, and this one had none.
	//
	// `AActor::GetActorLocation` answers `RootComponent ?
	// RootComponent->GetComponentLocation() : FVector::ZeroVector`, and the
	// transform handed to `SpawnActor` is applied to the root component, so with
	// no root it is discarded. Every drop this class ever produced sat at the
	// world origin, whatever position the spawner asked for, and the scatter that
	// spreads several drops from one kill around a circle moved nothing.
	//
	// THE PROJECT OWNER SAW IT FIRST, on 2026-08-19: every item from every kill
	// piled in the middle of the room rather than lying where the creature died.
	// Issue #723.
	//
	// A BARE SCENE COMPONENT, because there is nothing to draw. What a player
	// sees is the item's NAME, drawn by ACataclysmHUD::DrawDropNames over this
	// actor's position, so the actor needs a position and no appearance. A mesh
	// here would be a model lying on the floor, which is the thing this design
	// decided against on 2026-08-18.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool UCataclysmDropPickup::IsWithinPickupRange(const FVector& Character,
											  const FVector& Drop)
{
	// FLAT. See the header for why height is ignored.
	const FVector2D Flat(Character.X - Drop.X, Character.Y - Drop.Y);
	return Flat.SizeSquared() <= PickupRangeCm * PickupRangeCm;
}

int32 UCataclysmDropPickup::IndexOfNameUnderPoint(const TArray<FBox2D>& Rects,
												 const FVector2D& Point)
{
	// BACKWARDS, so the last name drawn -- the one on top -- is the one found.
	for (int32 Index = Rects.Num() - 1; Index >= 0; --Index)
	{
		// IsInsideOrOn RATHER THAN IsInside, WHICH EXCLUDES THE BOUNDARY. A
		// name is a small target -- twenty pixels tall at the current text
		// scale -- and a click on its outermost row of pixels is the player
		// pointing at it. The strict test was written first and the two corner
		// cases in CataclysmDropPickupTests.cpp caught it.
		if (Rects[Index].bIsValid && Rects[Index].IsInsideOrOn(Point))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UCataclysmDropPickup::SeparateOverlappingNames(TArray<FBox2D>& Rects,
													float GapPx)
{
	// HIGHEST FIRST. The name nearest the top of the screen keeps its place and
	// every other one moves down past it, so the result does not depend on the
	// order the world happened to hand the drops over in.
	TArray<int32> Order;
	Order.Reserve(Rects.Num());
	for (int32 Index = 0; Index < Rects.Num(); ++Index)
	{
		if (Rects[Index].bIsValid)
		{
			Order.Add(Index);
		}
	}

	// StableSort, NOT Sort. TArray::Sort is an introsort and gives no order at
	// all to elements the comparator calls equal, and two drops from one kill
	// really can produce two names at exactly the same height AND the same left
	// edge -- that is precisely the case the project owner reported, every name
	// printed on the same spot. With an unstable sort the layout of an unchanged
	// scene could differ from one frame to the next, so the names would jitter
	// while nothing moved. Order is built in array order, so a stable sort
	// settles every tie by which drop the world handed over first.
	//
	// FOUND BY A TEST RATHER THAN BY WATCHING IT. Three identical rectangles
	// came back in the order 148, 100, 124 instead of 100, 124, 148.
	Order.StableSort([&Rects](int32 A, int32 B)
	{
		if (Rects[A].Min.Y != Rects[B].Min.Y)
		{
			return Rects[A].Min.Y < Rects[B].Min.Y;
		}
		// LEFTMOST BREAKS A TIE between two names at the same height. Two that
		// match on both are left in the order they arrived, by the stable sort.
		return Rects[A].Min.X < Rects[B].Min.X;
	});

	for (int32 Position = 1; Position < Order.Num(); ++Position)
	{
		// AGAINST EVERY NAME ALREADY PLACED, REPEATEDLY, because moving clear of
		// one can move onto another. Bounded by the number already placed: each
		// pass that moves anything moves this name below at least one of them,
		// and there are only that many to get below.
		for (int32 Pass = 0; Pass < Position; ++Pass)
		{
			bool bMoved = false;

			for (int32 Earlier = 0; Earlier < Position; ++Earlier)
			{
				FBox2D& Mine = Rects[Order[Position]];
				const FBox2D& Theirs = Rects[Order[Earlier]];

				// TWO NAMES SIDE BY SIDE DO NOT OVERLAP however close their
				// heights are, so both axes have to be checked. Only the
				// vertical one carries the gap, because that is the direction
				// anything moves.
				const bool bShareColumns = Mine.Min.X <= Theirs.Max.X
										&& Theirs.Min.X <= Mine.Max.X;
				const bool bShareRows = Mine.Min.Y < Theirs.Max.Y + GapPx
									 && Theirs.Min.Y < Mine.Max.Y + GapPx;

				if (!bShareColumns || !bShareRows)
				{
					continue;
				}

				const float Push = Theirs.Max.Y + GapPx - Mine.Min.Y;
				Mine.Min.Y += Push;
				Mine.Max.Y += Push;
				bMoved = true;
			}

			if (!bMoved)
			{
				break;
			}
		}
	}
}

bool UCataclysmDropPickup::TakeInto(UCataclysmInventoryComponent* Inventory,
									ACataclysmDroppedItem* Drop)
{
	if (!Inventory || !IsValid(Drop))
	{
		return false;
	}

	const int32 Slot = Inventory->AddItem(Drop->Item);
	if (Slot == INDEX_NONE)
	{
		// NO ROOM. The drop is untouched and stays where it is.
		return false;
	}

	Drop->Destroy();
	return true;
}

FVector UCataclysmDropSpawner::ScatterOffset(int32 Index, int32 Count)
{
	if (Count <= 1)
	{
		return FVector::ZeroVector;
	}

	// THE RADIUS GROWS WITH THE COUNT so twelve drops from a Cataclysm Boss do
	// not crowd each other into one unreadable stack. 25 cm a drop keeps two
	// names about a name's width apart and keeps twelve inside three metres.
	const float Radius = 25.0f * static_cast<float>(Count);
	const float Angle = 2.0f * PI * static_cast<float>(Index)
		/ static_cast<float>(Count);

	return FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0f);
}

FLinearColor UCataclysmDropSpawner::ColourFor(const UDataTable* GearRarityTable,
											  ECataclysmRarity Rarity)
{
	const FCataclysmGearRarityRow* Row =
		UCataclysmDropRoll::RarityRow(GearRarityTable, Rarity);
	return Row ? Row->Colour : FLinearColor::White;
}

FName UCataclysmDropSpawner::EnemyRarityForStep(const UDataTable* EnemyDropTable,
												int32 Step)
{
	if (!EnemyDropTable)
	{
		return NAME_None;
	}

	FName Found = NAME_None;
	EnemyDropTable->ForeachRow<FCataclysmEnemyDropRow>(
		TEXT("UCataclysmDropSpawner::EnemyRarityForStep"),
		[&](const FName& Key, const FCataclysmEnemyDropRow& Row)
		{
			if (Found.IsNone() && Row.Step == Step)
			{
				Found = Key;
			}
		});
	return Found;
}

int32 UCataclysmDropSpawner::SpawnDropsFor(UWorld* World, int32 EnemyRarityStep,
										   float MagicFind, float LootQuantity,
										   const FVector& At,
										   FRandomStream& Stream)
{
	if (!World)
	{
		return 0;
	}

	const UDataTable* Drops = UCataclysmDropRoll::LoadEnemyDropTable();
	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* Sockets = UCataclysmDropRoll::LoadItemSocketTable();
	const UDataTable* Tiers = UCataclysmDropRoll::LoadAffixTierTable();
	if (!Drops || !Rarities || !Bases || !Affixes || !Sockets || !Tiers)
	{
		// Each Load* has already said which table is missing and why.
		return 0;
	}

	const FName EnemyRarity = EnemyRarityForStep(Drops, EnemyRarityStep);
	if (EnemyRarity.IsNone())
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("Nothing in game/Data/EnemyDrops.csv has a Step of %d, so a "
				 "creature of that rarity drops nothing."), EnemyRarityStep);
		return 0;
	}

	const int32 Count = UCataclysmDropRoll::RollDropCount(
		UCataclysmDropRoll::ExpectedGearDrops(Drops, EnemyRarity, LootQuantity),
		Stream);
	if (Count <= 0)
	{
		return 0;
	}

	// THE ENEMY'S MAGIC FIND IS ADDED TO THE PLAYER'S, not multiplied by it,
	// which is what Path of Exile does with its own sources. Added here rather
	// than by the caller so no caller can forget it.
	const float Together =
		MagicFind + UCataclysmDropRoll::MagicFindFrom(Drops, EnemyRarity);

	int32 Spawned = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// THE SLOT IS ROLLED PER ITEM, so two drops from one kill can be for
		// different slots.
		const FString Slot = UCataclysmDropRoll::RollSlot(Bases, Stream);

		FCataclysmItem Item;
		if (!UCataclysmDropRoll::RollItem(Bases, Affixes, Rarities, Sockets,
										  Tiers, Slot, UntrackedDifficultyTier,
										  Together, Stream, Item))
		{
			// RollItem has already said why. One item failing to roll is not a
			// reason to drop the rest on the floor unspawned.
			continue;
		}

		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ACataclysmDroppedItem* Drop = World->SpawnActor<ACataclysmDroppedItem>(
			ACataclysmDroppedItem::StaticClass(),
			At + ScatterOffset(Index, Count), FRotator::ZeroRotator,
			Parameters);
		if (!Drop)
		{
			continue;
		}

		Drop->Item = Item;
		Drop->DisplayName = UCataclysmItemName::NameOf(Item, Bases, Affixes);

		ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
		if (UCataclysmItemValues::RarityOf(Item.EnchantmentCount,
										   Item.Affixes.Num(), Rarity))
		{
			Drop->NameColour = ColourFor(Rarities, Rarity);
		}

		++Spawned;
	}

	return Spawned;
}
