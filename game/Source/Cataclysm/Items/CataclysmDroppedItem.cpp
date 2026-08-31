// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmDroppedItem.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Player/CataclysmGameMode.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
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

void ACataclysmDroppedItem::DescribeItself()
{
	if (IsMaterial())
	{
		const UDataTable* Materials = UCataclysmDropRoll::LoadCraftingMaterialTable();
		const UDataTable* Tiers = UCataclysmDropRoll::LoadMaterialTierTable();
		if (!Materials || !Tiers)
		{
			return;
		}

		// THE TIER COMES FROM THE MATERIAL rather than being remembered, which
		// is the whole reason it is not in the save record: four materials share
		// tier 1 and the tier is a property of which material this is.
		MaterialTier = UCataclysmDropRoll::MaterialTierOf(Materials, Material);
		DisplayName = UCataclysmDropRoll::MaterialNameOf(Materials, Material);
		NameColour = UCataclysmDropRoll::MaterialColourFor(Tiers, MaterialTier);
		return;
	}

	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	if (!Bases || !Affixes || !Rarities)
	{
		return;
	}

	DisplayName = UCataclysmItemName::NameOf(Item, Bases, Affixes);

	// THE RARITY IS KEPT AS WELL AS THE COLOUR IT PRODUCES, because the name's
	// border thickness needs the rung rather than the hue. Issue #718.
	ECataclysmRarity Rung = ECataclysmRarity::Everyday;
	if (UCataclysmItemValues::RarityOf(Item.EnchantmentCount,
									   Item.Affixes.Num(), Rung))
	{
		Rarity = Rung;
		NameColour = UCataclysmDropSpawner::ColourFor(Rarities, Rung);
	}
}

bool UCataclysmDropPickup::IsWithinPickupRange(const FVector& Character,
											  const FVector& Drop)
{
	// FLAT. See the header for why height is ignored.
	const FVector2D Flat(Character.X - Drop.X, Character.Y - Drop.Y);
	return Flat.SizeSquared() <= PickupRangeCm * PickupRangeCm;
}

bool UCataclysmDropPickup::IsWithinNameRange(const FVector& Character,
											 const FVector& Drop)
{
	// FLAT. See the header for why height is ignored.
	const FVector2D Flat(Character.X - Drop.X, Character.Y - Drop.Y);
	return Flat.SizeSquared() <= NameShownRangeCm * NameShownRangeCm;
}

void UCataclysmDropPickup::DropsToName(const UWorld* World,
									   const FVector& Standing,
									   TArray<ACataclysmDroppedItem*>& OutDrops)
{
	// EMPTIED FIRST, so a caller that reuses one array across frames cannot
	// carry last frame's drops into this one. ACataclysmHUD::DrawDropNames is
	// exactly such a caller.
	OutDrops.Reset();

	if (!World)
	{
		return;
	}

	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		ACataclysmDroppedItem* Drop = *It;
		if (!IsValid(Drop))
		{
			continue;
		}

		// A DROP WITH NO NAME IS MALFORMED and there is nothing to draw for it.
		if (Drop->DisplayName.IsEmpty())
		{
			continue;
		}

		if (!IsWithinNameRange(Standing, Drop->GetActorLocation()))
		{
			continue;
		}

		OutDrops.Add(Drop);
	}
}

bool UCataclysmDropPickup::ComesAutomatically(bool bIsMaterial,
											  const FVector& Character,
											  const FVector& Drop)
{
	// GEAR NEVER DOES, AT ANY DISTANCE. Checked before the arithmetic so the
	// rule reads as the rule rather than as a consequence of a radius.
	if (!bIsMaterial)
	{
		return false;
	}

	// FLAT. See the header for why height is ignored.
	const FVector2D Flat(Character.X - Drop.X, Character.Y - Drop.Y);
	return Flat.SizeSquared()
		<= AutomaticMaterialRangeCm * AutomaticMaterialRangeCm;
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

int32 UCataclysmDropPickup::NameBorderThicknessFor(ECataclysmRarity Rarity)
{
	// THE ENUM'S OWN ORDER IS THE LADDER. Everyday is 0 and Cataclysmic is 7,
	// so this is one pixel at the bottom and eight at the top.
	return ThinnestNameBorderPx + static_cast<int32>(Rarity);
}

int32 UCataclysmDropPickup::NameBorderThicknessForMaterialTier(int32 Tier)
{
	// CLAMPED AT THE BOTTOM so a drop whose tier failed to read still gets a
	// border rather than none at all. A missing border would look like a fault
	// in the drawing rather than in the data.
	return ThinnestNameBorderPx + FMath::Max(0, Tier - 1);
}

int32 UCataclysmDropPickup::NameBorderThicknessOf(
	const ACataclysmDroppedItem& Drop)
{
	return Drop.IsMaterial()
		? NameBorderThicknessForMaterialTier(Drop.MaterialTier)
		: NameBorderThicknessFor(Drop.Rarity);
}

FBox2D UCataclysmDropPickup::TagAround(const FBox2D& Text,
									   int32 BorderThickness)
{
	if (!Text.bIsValid)
	{
		// NOTHING TO GROW. A name that failed to project has no place on screen.
		return Text;
	}

	const float Outward = static_cast<float>(NameBorderPaddingPx
											 + FMath::Max(0, BorderThickness));

	return FBox2D(Text.Min - FVector2D(Outward, Outward),
				  Text.Max + FVector2D(Outward, Outward));
}

bool UCataclysmDropPickup::TakeInto(UCataclysmInventoryComponent* Inventory,
									ACataclysmDroppedItem* Drop)
{
	if (!Inventory || !IsValid(Drop))
	{
		return false;
	}

	// A MATERIAL GOES ONTO A STACK AND A PIECE OF GEAR TAKES A SLOT. Both can
	// fail for the same reason -- no free slot -- and both leave the drop where
	// it is when they do.
	const int32 Slot = Drop->IsMaterial()
		? Inventory->AddMaterial(Drop->Material, Drop->MaterialQuantity)
		: Inventory->AddItem(Drop->Item);

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

void UCataclysmDropSpawner::PlayerLootStats(const UWorld* World,
										   float& OutMagicFind,
										   float& OutLootQuantity)
{
	const APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	LootStatsOf(Controller ? Controller->GetPawn() : nullptr,
				OutMagicFind, OutLootQuantity);
}

void UCataclysmDropSpawner::LootStatsOf(const AActor* Character,
										float& OutMagicFind,
										float& OutLootQuantity)
{
	// THE BASELINES FIRST, so every early return below leaves the caller with
	// the figures a character carrying nothing would have given it.
	OutMagicFind = 0.0f;
	OutLootQuantity = UCataclysmDropRoll::BaselineLootQuantity;

	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Character);
	if (!AbilitySystem)
	{
		return;
	}

	// EACH ATTRIBUTE IS CHECKED FOR SEPARATELY. They live in one set today, so
	// either both are present or neither is, but reading an attribute whose
	// set the component does not hold raises an engine ensure, and that is not
	// a thing to risk on the assumption that they stay together.
	const FGameplayAttribute MagicFind =
		UCataclysmCombatAttributeSet::GetMagicFindAttribute();
	if (AbilitySystem->HasAttributeSetForAttribute(MagicFind))
	{
		OutMagicFind = AbilitySystem->GetNumericAttribute(MagicFind);
	}

	const FGameplayAttribute LootQuantity =
		UCataclysmCombatAttributeSet::GetLootQuantityAttribute();
	if (AbilitySystem->HasAttributeSetForAttribute(LootQuantity))
	{
		OutLootQuantity = AbilitySystem->GetNumericAttribute(LootQuantity);
	}
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
	const UDataTable* WeaponSkills =
		UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!Drops || !Rarities || !Bases || !Affixes || !Sockets || !Tiers
		|| !WeaponSkills)
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

	// BOTH COUNTS ARE ROLLED BEFORE ANYTHING IS SPAWNED, so gear and materials
	// share one scatter circle instead of landing on two of different sizes.
	// ScatterOffset spaces a drop by how many there are in total, so a gear
	// item that thought there were five of it and a material that thought there
	// were seventeen would sit at two different radii and could overlap.
	const int32 Count = UCataclysmDropRoll::RollDropCount(
		UCataclysmDropRoll::ExpectedGearDrops(Drops, EnemyRarity, LootQuantity),
		Stream);
	const int32 MaterialCount = UCataclysmDropRoll::RollDropCount(
		UCataclysmDropRoll::ExpectedMaterialDrops(Drops, EnemyRarity,
												  LootQuantity),
		Stream);
	const int32 Total = Count + MaterialCount;
	if (Total <= 0)
	{
		return 0;
	}

	// THE ENEMY'S MAGIC FIND IS ADDED TO THE PLAYER'S, not multiplied by it,
	// which is what Path of Exile does with its own sources. Added here rather
	// than by the caller so no caller can forget it.
	const float Together =
		MagicFind + UCataclysmDropRoll::MagicFindFrom(Drops, EnemyRarity);

	// THE TIER BEING PLAYED, AND UNTIL ISSUE #868 THIS WAS THE CONSTANT 8. That
	// placeholder was put here when nothing in the engine knew the tier, and it
	// stopped being true: ACataclysmGameMode::DifficultyTierIn answers it, the
	// `Cataclysm.DifficultyTier` console variable sets it, and
	// UCataclysmVitalAttributeSet already resolves damage through it. So combat
	// respected the chosen tier and loot did not.
	//
	// THREE GATES RUN THROUGH THIS ONE NUMBER. Gear rarity equals the tier,
	// affixes roll up to the tier plus one, and the best upgrade stone that can
	// drop is the tier plus two. Every one of them was reading 8, so a character
	// on tier 1 could be handed Cataclysmic gear with T7 affixes.
	//
	// THE PLACEHOLDER'S REASON WAS A GOOD ONE and is not lost: a drop seen in
	// the editor exercised the whole roll rather than a slice of it. Set
	// `Cataclysm.DifficultyTier 8` to get that back deliberately.
	const int32 DifficultyTier = ACataclysmGameMode::DifficultyTierIn(World);

	int32 Spawned = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// THE SLOT IS ROLLED PER ITEM, so two drops from one kill can be for
		// different slots.
		const FString Slot = UCataclysmDropRoll::RollSlot(Bases, Stream);

		FCataclysmItem Item;
		if (!UCataclysmDropRoll::RollItem(Bases, Affixes, Rarities, Sockets,
										  Tiers, WeaponSkills, Slot,
										  DifficultyTier, Together, Stream, Item))
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
			At + ScatterOffset(Index, Total), FRotator::ZeroRotator,
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
			// THE RARITY IS KEPT AS WELL AS THE COLOUR IT PRODUCES, because the
			// border's thickness needs the rung rather than the hue. Issue #718.
			Drop->Rarity = Rarity;
			Drop->NameColour = ColourFor(Rarities, Rarity);
		}

		++Spawned;
	}

	// THE MATERIALS CONTINUE ROUND THE SAME CIRCLE, starting where the gear
	// stopped. Count rather than Spawned, because a gear item that failed to
	// roll still used up its place: leaving a gap is better than putting a
	// material on top of the item after it.
	Spawned += SpawnMaterialsFor(World, EnemyRarity, Together, At, MaterialCount,
								 Count, Total, DifficultyTier, Stream);

	return Spawned;
}

int32 UCataclysmDropSpawner::SpawnMaterialsFor(UWorld* World, FName EnemyRarity,
											   float MagicFind,
											   const FVector& At, int32 Count,
											   int32 AlreadyOnTheFloor,
											   int32 TotalDrops,
											   int32 DifficultyTier,
											   FRandomStream& Stream)
{
	if (!World || Count <= 0)
	{
		return 0;
	}

	const UDataTable* TierTable = UCataclysmDropRoll::LoadMaterialTierTable();
	const UDataTable* Materials = UCataclysmDropRoll::LoadCraftingMaterialTable();
	if (!TierTable || !Materials)
	{
		// Each Load* has already said which table is missing and why.
		return 0;
	}

	int32 Spawned = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Tier =
			UCataclysmDropRoll::RollMaterialTier(TierTable, MagicFind, Stream);
		const FName Material = UCataclysmDropRoll::RollMaterial(
			Materials, Tier, DifficultyTier, Stream);
		if (Material.IsNone())
		{
			// RollMaterial has already said why. One material failing to roll is
			// not a reason to drop the rest unspawned.
			continue;
		}

		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		// SCATTERED PAST THE GEAR RATHER THAN OVER IT. The gear from this kill
		// occupies the first AlreadyOnTheFloor positions of the circle and the
		// materials continue round the same one, which is why the caller passes
		// the total rather than letting this work one out.
		ACataclysmDroppedItem* Drop = World->SpawnActor<ACataclysmDroppedItem>(
			ACataclysmDroppedItem::StaticClass(),
			At + ScatterOffset(AlreadyOnTheFloor + Index, TotalDrops),
			FRotator::ZeroRotator, Parameters);
		if (!Drop)
		{
			continue;
		}

		// ONE PER DROP, NOT A STACK ON THE FLOOR. Each roll is one material, and
		// they stack when they are picked up rather than where they lie -- so a
		// player can see how many fell and can leave some.
		Drop->Material = Material;
		Drop->MaterialQuantity = 1;
		Drop->MaterialTier = Tier;
		Drop->DisplayName =
			UCataclysmDropRoll::MaterialNameOf(Materials, Material);
		Drop->NameColour =
			UCataclysmDropRoll::MaterialColourFor(TierTable, Tier);

		++Spawned;
	}

	return Spawned;
}
