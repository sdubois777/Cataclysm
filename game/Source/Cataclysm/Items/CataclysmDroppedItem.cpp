// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmDroppedItem.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Items/CataclysmDropRoll.h"

ACataclysmDroppedItem::ACataclysmDroppedItem()
{
	// NOTHING TICKS. The name is worked out at spawn and the heads-up display
	// draws it; a drop on the floor has nothing to do between those two.
	PrimaryActorTick.bCanEverTick = false;
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
