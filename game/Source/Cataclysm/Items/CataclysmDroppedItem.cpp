// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmDroppedItem.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmEmpireTree.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Empire/CataclysmSeedMix.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"

// A MUTED GOLD, #D9C27A in sRGB. See the header for why it is not the Legendary yellow. Issue #2742.
const FLinearColor ACataclysmDroppedItem::GoldNameColour = FLinearColor(FColor(0xD9, 0xC2, 0x7A));

FString ACataclysmDroppedItem::GoldAmountText(int64 Amount)
{
	// BY HAND RATHER THAN THROUGH FText::AsNumber, whose grouping follows the culture: a test reading "1,234" must not
	// depend on the machine's locale, and this text is the same everywhere the game shows gold.
	const bool bNegative = Amount < 0;
	FString Digits = FString::Printf(TEXT("%lld"), bNegative ? -Amount : Amount);
	for (int32 At = Digits.Len() - 3; At > 0; At -= 3)
	{
		Digits.InsertAt(At, TEXT(','));
	}
	return bNegative ? TEXT("-") + Digits : Digits;
}

FString ACataclysmDroppedItem::GoldName(int64 Amount)
{
	return GoldAmountText(Amount) + TEXT(" gold");
}

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
	// A GOLD PILE NEEDS NO TABLE: its name is its amount, and its colour and border are gold's own. Issue #2742.
	if (IsGold())
	{
		DisplayName = GoldName(Gold);
		NameColour = GoldNameColour;
		Rarity = ECataclysmRarity::Everyday;
		MaterialTier = 0;
		return;
	}

	// A GEM IS NAMED BY THE GEMS SHEET AND ITS RARITY, "Of Life (Superb)", and is coloured and bordered by its rarity as
	// gear is: `NameBorderThicknessOf` reads `Rarity`, so it is set to the gem's. Issue #46, slice G5.
	if (IsGem())
	{
		DisplayName = UCataclysmGems::FloorNameOf(Gem, GemRarity);
		Rarity = GemRarity;
		MaterialTier = 0;
		if (const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable())
		{
			NameColour = UCataclysmDropSpawner::ColourFor(Rarities, GemRarity);
		}
		return;
	}

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

		// A CRAFTING MATERIAL IS NAMED AT ANY DISTANCE, and gear is not. Issue
		// #1117, decided by the project owner on 2026-09-01.
		//
		// WHY THE TEN METRE RULE HAD TO EXEMPT ONE OF THEM. A material within
		// fifteen metres is collected without being clicked, so a material was
		// only ever still lying there when it was already too far away to be
		// named. The rule and the sweep did not overlap at all, and the effect
		// was that a material's name was never drawn in play.
		//
		// SO WHAT A PLAYER SEES IS A MATERIAL ACROSS THE ROOM, and it winks out
		// as they walk into the fifteen metres and it comes to them. That is the
		// whole of the feedback that a kill produced materials.
		//
		// THE MIRROR OF ComesAutomatically, WHICH EXEMPTS GEAR FROM THE SWEEP
		// for the same shape of reason: the two kinds of drop are collected
		// differently, so they are shown differently.
		//
		// AND A GOLD PILE IS NAMED AS A MATERIAL IS, at any distance, because it
		// is swept as a material is. Issue #2742, ruled 2026-11-18 (G3).
		if (!Drop->IsAQuantity()
			&& !IsWithinNameRange(Standing, Drop->GetActorLocation()))
		{
			continue;
		}

		OutDrops.Add(Drop);
	}
}

bool UCataclysmDropPickup::ComesAutomatically(bool bIsMaterial,
											  const FVector& Character,
											  const FVector& Drop,
											  bool bInfested)
{
	// GEAR NEVER DOES, AT ANY DISTANCE. Checked before the arithmetic so the
	// rule reads as the rule rather than as a consequence of a radius.
	//
	// NOR DOES AN INFESTED DROP, MATERIAL OR NOT: The Infested Hoard's loot is a
	// choice between a haul and a drain, and one that collected itself would be
	// neither. Ruled under the owner's delegation, 2026-09-25. Issues #1820, #41.
	if (!bIsMaterial || bInfested)
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
	// A GOLD PILE HAS THE THINNEST BORDER: it has no rarity and no tier, and the
	// border is what sets it apart from a Legendary item's five pixels. Issue #2742.
	if (Drop.IsGold())
	{
		return ThinnestNameBorderPx;
	}

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
									ACataclysmDroppedItem* Drop, bool bByHand)
{
	if (!Inventory || !IsValid(Drop))
	{
		return false;
	}

	// GOLD GOES TO THE ACCOUNT AND NOT INTO THE BAG, and is not announced as loot. Issue #2742. A click lands here
	// only for a pile the sweep did not take -- one the balance refused, which BankGold leaves lying.
	if (Drop->IsGold())
	{
		const AActor* Holder = Inventory->GetOwner();
		return BankGold(Holder ? Holder->GetWorld() : nullptr, { Drop }) > 0;
	}

	// A PIECE OF GEAR TAKES A GEAR SLOT, and can fail for want of one, leaving the drop where it is. A MATERIAL GOES TO
	// THE MATERIALS TAB AND A GEM TO THE GEMS TAB, onto the stack of its kind (a gem's of its gem and rarity, issue #46,
	// slice G5), and since #2849 neither is ever refused for room: a full gear tab does not stop them.
	const bool bTaken = Drop->IsMaterial()
		? Inventory->AddMaterial(Drop->Material, Drop->MaterialQuantity)
		: Drop->IsGem()
			? Inventory->AddGems(Drop->Gem, Drop->GemRarity, Drop->GemQuantity)
			: Inventory->AddItem(Drop->Item) != INDEX_NONE;

	if (!bTaken)
	{
		// NO ROOM FOR GEAR, or a stack of nothing. The drop is untouched and stays where it is.
		return false;
	}

	// A PIECE OF GEAR TAKEN IS SAID IN THE PLAY LOOP, by the name the floor showed for it: the coordinating session's
	// ruling of 2026-11-19. A material is not; `FCataclysmPlayLoopLog::PickedUpLine` says why. Nor is a gem, a quantity
	// collected without a click for the same reason (issue #46, slice G5); gold returned above.
	if (!Drop->IsAQuantity())
	{
		FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::PickedUpLine(Drop->DisplayName, Drop->Item.GearLevel));
	}

	// WHERE IT LAY IS READ BEFORE THE ACTOR GOES, and the take announced after.
	const FVector Where = Drop->GetActorLocation();
	const bool bMarked = Drop->bDroppedByARaisedCreature;
	const bool bInfested = Drop->bInfested;
	Drop->Destroy();
	UCataclysmCombatEvents::NoteLootTaken(Inventory->GetOwner(), Where, bByHand, bMarked, bInfested);
	return true;
}

int64 UCataclysmDropPickup::BankGold(UWorld* World, const TArray<ACataclysmDroppedItem*>& Piles)
{
	int64 Total = 0;
	for (const ACataclysmDroppedItem* Pile : Piles)
	{
		if (IsValid(Pile) && Pile->IsGold())
		{
			Total += Pile->Gold;
		}
	}
	if (Total <= 0 || World == nullptr)
	{
		return 0;
	}

	// THE BALANCE OF THE CHARACTER PLAYING: its lethality mode's in the account record, or a Solo Self-Found
	// character's own (the owner's ruling of 2026-11-18), decided as `AddGoldFor` decides. A DEBT IS PAID DOWN FIRST
	// (issue #2741 slice 6e, D3): Global Credit's "Debt repaid from next gold drops".
	bool bIntoTheCharacter = false;
	int64 Repaid = 0;
	if (!UCataclysmGameInstance::AddPickedUpGoldFor(World, Total, bIntoTheCharacter, Repaid))
	{
		// NOBODY PLAYING, OR A BANK REFUSED (`AccountFor` logged which, the first time). Left lying.
		return 0;
	}

	// IN THE BALANCE FIRST, AND ONLY THEN OFF THE FLOOR, so nothing destroys gold that was not banked.
	for (ACataclysmDroppedItem* Pile : Piles)
	{
		if (IsValid(Pile) && Pile->IsGold())
		{
			Pile->Destroy();
		}
	}
	if (UCataclysmGameInstance* Instance = UCataclysmGameInstance::In(World))
	{
		Instance->GoldPickedUpThisWalk += Total;
	}

	// AND FOR CLEANUP CREW, in the walk's own saved count (issue #2741 slice 6b, B3).
	ACataclysmDungeonGameMode::NoteGoldPickedUpIn(World, Total);

	// AND THE RECORDS ARE WRITTEN, once for every pile taken together. THE ACCOUNT RECORD when the gold went there.
	// THE RUN AND CHARACTER RECORDS ALWAYS, through `InventoryChanged`, as an item picked up writes them: the run
	// record's floor must lose the piles in the same write that gains the gold, or a load before the next clock write
	// would put piles back that the balance already holds; and a Solo Self-Found character's balance is in its
	// character record.
	if (!bIntoTheCharacter)
	{
		UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::AccountChanged);
	}
	UCataclysmSaveWriter::NoteTriggerIn(World, ECataclysmSaveTrigger::InventoryChanged);
	return Total;
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

FVector UCataclysmDropSpawner::DropSpotInFrontOf(const AActor& Character)
{
	// THE FORWARD VECTOR AND NOT THE CAMERA'S, because what a player is looking
	// at and which way the character is facing are two different things, and the
	// item should land where the character could reach for it.
	return Character.GetActorLocation()
		+ Character.GetActorForwardVector() * DropInFrontCm;
}

ACataclysmDroppedItem* UCataclysmDropSpawner::PutOnTheFloor(
	UWorld* World, const FVector& At, const FCataclysmItem& Item,
	FName Material, int32 MaterialQuantity, int32 Gold, FName Gem,
	ECataclysmRarity GemRarity, int32 GemQuantity)
{
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACataclysmDroppedItem* Drop = World->SpawnActor<ACataclysmDroppedItem>(
		ACataclysmDroppedItem::StaticClass(), At, FRotator::ZeroRotator,
		Parameters);
	if (!Drop)
	{
		return nullptr;
	}

	Drop->Item = Item;
	Drop->Material = Material;
	Drop->MaterialQuantity = MaterialQuantity;
	Drop->Gold = FMath::Max(0, Gold);
	Drop->Gem = Gem;
	Drop->GemRarity = GemRarity;
	Drop->GemQuantity = FMath::Max(0, GemQuantity);

	// THE NAME, THE COLOUR, THE RARITY AND THE TIER ARE WORKED OUT rather than
	// passed in, because all four follow from what the drop IS. A caller holding
	// them would hand back the name an item had when it was picked up, which is
	// the wrong one after a rename in the design workbook.
	Drop->DescribeItself();

	return Drop;
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
										   FRandomStream& Stream,
										   bool bMarked, bool bInfested,
										   int32 GearCountGiven,
										   int32 MaterialCountGiven,
										   float LowTierMaterialExtraChance,
										   ECataclysmRarity LowestRarity,
										   float MaterialsMore)
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
	const UDataTable* PositiveEnchantments =
		UCataclysmDropRoll::LoadPositiveEnchantmentTable();
	const UDataTable* NegativeEnchantments =
		UCataclysmDropRoll::LoadNegativeEnchantmentTable();
	if (!Drops || !Rarities || !Bases || !Affixes || !Sockets || !Tiers
		|| !WeaponSkills || !PositiveEnchantments || !NegativeEnchantments)
	{
		// Each Load* has already said which table is missing and why.
		return 0;
	}

	// AND THE ENCHANTMENT EFFECTS, for "This weapon has 2-4 damage types" alone.
	// Not required: without it a drop is exactly what it was, so its absence is
	// no reason to drop nothing. Its loader says when it is missing.
	const UDataTable* EnchantmentEffects =
		UCataclysmItemModifiers::LoadEnchantmentEffectTable();

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
	//
	// A COUNT GIVEN IS USED INSTEAD OF ROLLED, which is how The Infested Hoard
	// asks for exactly one drop. Every other caller gives none.
	const int32 Count = GearCountGiven >= 0
		? GearCountGiven
		: UCataclysmDropRoll::RollDropCount(
			  UCataclysmDropRoll::ExpectedGearDrops(Drops, EnemyRarity, LootQuantity),
			  Stream);
	// A CITY'S "MORE CRAFTING MATERIALS" (issue #2781, X2) is the material roll's alone, given as a loot quantity, which
	// the expected count is in proportion to. Exactly 1 leaves the figure as it was.
	const int32 MaterialCount = MaterialCountGiven >= 0
		? MaterialCountGiven
		: UCataclysmDropRoll::RollDropCount(
			  UCataclysmDropRoll::ExpectedMaterialDrops(Drops, EnemyRarity,
													   LootQuantity * FMath::Max(0.0f, MaterialsMore)),
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

	// AND WHETHER THIS FLOOR MAKES DROPS CHAOTIC, asked the same way. `Chaos_Chaotic_Loot`;
	// issues #1820 and #41.
	const bool bChaotic = ACataclysmGameMode::DropsAreChaoticIn(World);

	int32 Spawned = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// THE SLOT IS ROLLED PER ITEM, so two drops from one kill can be for
		// different slots.
		const FString Slot = UCataclysmDropRoll::RollSlot(Bases, Stream);

		FCataclysmItem Item;
		if (!UCataclysmDropRoll::RollItem(Bases, Affixes, Rarities, Sockets,
										  Tiers, WeaponSkills,
										  PositiveEnchantments,
										  NegativeEnchantments, Slot,
										  DifficultyTier, Together, Stream, Item,
										  bChaotic, EnchantmentEffects, LowestRarity))
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
		Drop->bDroppedByARaisedCreature = bMarked;
		Drop->bInfested = bInfested;
		Drop->DisplayName = UCataclysmItemName::NameOf(Item, Bases, Affixes);
		if (bInfested)
		{
			// THE MARK THE PLAYER READS, on the name the floor draws. Issues #1820, #41.
			Drop->DisplayName = TEXT("Infested ") + Drop->DisplayName;
		}

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
								 Count, Total, DifficultyTier, Stream, bMarked,
								 bInfested, LowTierMaterialExtraChance);

	return Spawned;
}

int32 UCataclysmDropSpawner::SpawnOneInfestedDropFor(UWorld* World, int32 EnemyRarityStep,
													 float MagicFind, const FVector& At,
													 FRandomStream& Stream)
{
	const UDataTable* Drops = UCataclysmDropRoll::LoadEnemyDropTable();
	if (!World || !Drops)
	{
		return 0;
	}
	const FName EnemyRarity = EnemyRarityForStep(Drops, EnemyRarityStep);
	const float Gear = UCataclysmDropRoll::ExpectedGearDrops(
		Drops, EnemyRarity, UCataclysmDropRoll::BaselineLootQuantity);
	const float Materials = UCataclysmDropRoll::ExpectedMaterialDrops(
		Drops, EnemyRarity, UCataclysmDropRoll::BaselineLootQuantity);
	if (EnemyRarity.IsNone() || Gear + Materials <= 0.0f)
	{
		return 0;
	}
	// GEAR OR A MATERIAL IN THE CREATURE'S OWN RATIO, which is four gear to one
	// material for every rarity in game/Data/EnemyDrops.csv today (issue #2829).
	const bool bGear = Stream.FRand() * (Gear + Materials) < Gear;
	return SpawnDropsFor(World, EnemyRarityStep, MagicFind,
						 UCataclysmDropRoll::BaselineLootQuantity, At, Stream,
						 /*bMarked=*/false, /*bInfested=*/true,
						 /*GearCountGiven=*/bGear ? 1 : 0,
						 /*MaterialCountGiven=*/bGear ? 0 : 1);
}

int32 UCataclysmDropSpawner::SpawnTheTreesExtraDropsFor(UWorld* World, int32 EnemyRarityStep, float MagicFind,
														float LootQuantity, const FVector& At, FRandomStream& Stream,
														const FCataclysmEmpireTreeDropRules& Rules, bool bMarked)
{
	if (!World)
	{
		return 0;
	}

	// EACH CHANCE ROLLED ONLY ABOVE NOUGHT AND ONLY FOR ITS RUNGS, so a node not bought, or a rung it does not name,
	// draws nothing from the stream. In the order the header lists them. See the header for the readings.
	int32 Spawned = 0;
	if (Rules.EliteExtraItemChance > 0.0f && EnemyRarityStep == EliteRarityStep
		&& Stream.FRand() < Rules.EliteExtraItemChance)
	{
		Spawned += SpawnDropsFor(World, EnemyRarityStep, MagicFind, LootQuantity, At, Stream, bMarked,
								 /*bInfested=*/false, /*GearCountGiven=*/1, /*MaterialCountGiven=*/0,
								 Rules.LowTierMaterialExtraChance);
	}
	if (Rules.ExtraMaterialChance > 0.0f && Stream.FRand() < Rules.ExtraMaterialChance)
	{
		Spawned += SpawnDropsFor(World, EnemyRarityStep, MagicFind, LootQuantity, At, Stream, bMarked,
								 /*bInfested=*/false, /*GearCountGiven=*/0, /*MaterialCountGiven=*/1,
								 Rules.LowTierMaterialExtraChance);
	}
	if (Rules.BossLegendaryChance > 0.0f && EnemyRarityStep >= FirstBossRarityStep
		&& Stream.FRand() < Rules.BossLegendaryChance)
	{
		Spawned += SpawnDropsFor(World, EnemyRarityStep, MagicFind, LootQuantity, At, Stream, bMarked,
								 /*bInfested=*/false, /*GearCountGiven=*/1, /*MaterialCountGiven=*/0,
								 Rules.LowTierMaterialExtraChance, ECataclysmRarity::Legendary);
	}
	return Spawned;
}

bool UCataclysmDropSpawner::GoldDropsIn(const UWorld* World)
{
	// A BALANCE TO DROP IT FOR, whichever the character playing has: its lethality mode's in the account record, or a
	// Solo Self-Found character's own. The one place that decides is `GoldHeldBy`. See the header.
	int64 Held = 0;
	return UCataclysmGameInstance::GoldHeldBy(World, Held);
}

FVector UCataclysmDropSpawner::GoldScatterOffset(int32 Index, int32 Count)
{
	// ONE STEP WIDER THAN THE ITEMS' RING AND HALF A STEP ROUND, so a pile is never where an item of the same kill
	// is: `ScatterOffset` puts a lone item on the corpse, and a lone pile lands 50 cm off it.
	const int32 Piles = FMath::Max(1, Count);
	const float Radius = 25.0f * static_cast<float>(Piles + 1);
	const float Angle = 2.0f * PI * (static_cast<float>(Index) + 0.5f) / static_cast<float>(Piles);
	return FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0f);
}

int32 UCataclysmDropSpawner::SpawnGoldFor(UWorld* World, int32 EnemyRarityStep, const FVector& At,
										  FRandomStream& Stream, float GoldMultiplier)
{
	if (World == nullptr)
	{
		return 0;
	}

	const UDataTable* Drops = UCataclysmDropRoll::LoadEnemyDropTable();
	const UDataTable* GoldTable = UCataclysmDropRoll::LoadGoldTable();
	if (Drops == nullptr || GoldTable == nullptr)
	{
		// Each Load* has already said which table is missing and why.
		return 0;
	}

	const FName EnemyRarity = EnemyRarityForStep(Drops, EnemyRarityStep);
	if (EnemyRarity.IsNone())
	{
		return 0;
	}

	// HOW MANY: a Poisson draw on the rung's expected piles, as gear and materials are counted.
	const int32 Count = UCataclysmDropRoll::RollDropCount(
		UCataclysmDropRoll::ExpectedGoldDrops(Drops, EnemyRarity), Stream);
	if (Count <= 0)
	{
		return 0;
	}

	// HOW MUCH EACH: the tier being played and the floor the kill was on, asked once for the whole kill. A world with
	// no dungeon answers floor 1.
	const int32 DifficultyTier = ACataclysmGameMode::DifficultyTierIn(World);
	const int32 Floor = UCataclysmEnemyScore::FloorIn(World).FloorNumber;

	int32 Placed = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Rolled = UCataclysmDropRoll::RollGoldPile(GoldTable, DifficultyTier, Floor, Stream);
		if (Rolled <= 0)
		{
			continue;
		}

		// THE TREE'S GOLD NODES, AFTER THE PILE'S DRAWS (slice 6a): a multiplier of exactly 1 leaves the amount as rolled.
		const int32 Amount = GoldMultiplier == 1.0f
			? Rolled
			: FMath::Max(1, FMath::RoundToInt32(static_cast<double>(Rolled) * FMath::Max(0.0f, GoldMultiplier)));
		if (PutOnTheFloor(World, At + GoldScatterOffset(Index, Count), FCataclysmItem(), NAME_None, 0, Amount))
		{
			++Placed;

			// COUNTED FOR CLEANUP CREW, the gold a walk's kills dropped (issue #2741 slice 6b, B3).
			ACataclysmDungeonGameMode::NoteGoldDroppedIn(World, Amount);
		}
	}
	return Placed;
}

FVector UCataclysmDropSpawner::GemScatterOffset(int32 Index, int32 Count)
{
	// TWO STEPS WIDER THAN THE ITEMS' RING AND AN EIGHTH OF A STEP ROUND: a lone gem lands 75 cm off the corpse, where
	// neither the lone item (on the corpse) nor the lone pile (50 cm off it) is. Overlapping names are separated on
	// screen in any case (`UCataclysmDropPickup::SeparateOverlappingNames`).
	const int32 Gems = FMath::Max(1, Count);
	const float Radius = 25.0f * static_cast<float>(Gems + 2);
	const float Angle = 2.0f * PI * (static_cast<float>(Index) + 0.125f) / static_cast<float>(Gems);
	return FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0f);
}

int32 UCataclysmDropSpawner::SpawnGemsFor(UWorld* World, int32 EnemyRarityStep, float MagicFind, float LootQuantity,
										  const FVector& At, const FRandomStream& KillStream, bool bMarked)
{
	if (World == nullptr)
	{
		return 0;
	}

	const UDataTable* Drops = UCataclysmDropRoll::LoadEnemyDropTable();
	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	const UDataTable* Gems = UCataclysmGems::LoadGeneratedTable();
	if (Drops == nullptr || Rarities == nullptr || Gems == nullptr)
	{
		// Each Load* has already said which table is missing and why.
		return 0;
	}

	const FName EnemyRarity = EnemyRarityForStep(Drops, EnemyRarityStep);
	if (EnemyRarity.IsNone())
	{
		return 0;
	}

	// A STREAM OF THE GEMS' OWN, seeded from the kill's through the mix and never by XOR (issue #2749): the kill's
	// stream is read, not drawn on, so everything it rolled before this is as it was.
	FRandomStream Stream(CataclysmSeedMix::Mix(KillStream.GetCurrentSeed(), GemStreamSalt, 0));

	// HOW MANY: a Poisson draw on the rung's expected gems, as gear, materials and gold are counted.
	const int32 Count = UCataclysmDropRoll::RollDropCount(
		UCataclysmDropRoll::ExpectedGemDrops(Drops, EnemyRarity, LootQuantity), Stream);
	if (Count <= 0)
	{
		return 0;
	}

	// THE GEAR'S RARITY ROLL, at the tier being played, with the creature's magic find added to the player's as
	// `SpawnDropsFor` adds it.
	const float Together = MagicFind + UCataclysmDropRoll::MagicFindFrom(Drops, EnemyRarity);
	const int32 DifficultyTier = ACataclysmGameMode::DifficultyTierIn(World);

	int32 Placed = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const ECataclysmRarity Rarity = UCataclysmDropRoll::RollRarity(Rarities, DifficultyTier, Together, Stream);
		const FName Gem = UCataclysmGems::RollGem(Gems, Stream);
		if (Gem.IsNone())
		{
			continue;
		}
		if (ACataclysmDroppedItem* Drop = PutOnTheFloor(World, At + GemScatterOffset(Index, Count), FCataclysmItem(),
														NAME_None, 0, 0, Gem, Rarity, 1))
		{
			Drop->bDroppedByARaisedCreature = bMarked;
			++Placed;
		}
	}
	return Placed;
}

int32 UCataclysmDropSpawner::SpawnMaterialsFor(UWorld* World, FName EnemyRarity,
											   float MagicFind,
											   const FVector& At, int32 Count,
											   int32 AlreadyOnTheFloor,
											   int32 TotalDrops,
											   int32 DifficultyTier,
											   FRandomStream& Stream,
											   bool bMarked, bool bInfested,
											   float LowTierMaterialExtraChance)
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

		// SCAVENGER: a material of tier 3 or below is two of it, at its chance.
		// Rolled only above nought, before the drop is made, so a kill without the
		// node draws what it drew before. Issue #2741 slice 5b (E2).
		int32 Quantity = 1;
		if (LowTierMaterialExtraChance > 0.0f && Tier <= ScavengerHighestTier
			&& Stream.FRand() < LowTierMaterialExtraChance)
		{
			Quantity = 2;
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
		//
		// EXCEPT SCAVENGER'S SECOND, which is the same drop holding two (E2): the
		// design halved material drops on 2026-08-23 and quartered them again on
		// 2026-11-22 (issue #2829) because the floor was cluttered, and two on one
		// drop is the same quantity on half the floor.
		Drop->Material = Material;
		Drop->MaterialQuantity = Quantity;
		Drop->bDroppedByARaisedCreature = bMarked;
		Drop->bInfested = bInfested;
		Drop->MaterialTier = Tier;
		Drop->DisplayName =
			UCataclysmDropRoll::MaterialNameOf(Materials, Material);
		if (bInfested)
		{
			Drop->DisplayName = TEXT("Infested ") + Drop->DisplayName;
		}
		Drop->NameColour =
			UCataclysmDropRoll::MaterialColourFor(TierTable, Tier);

		++Spawned;
	}

	return Spawned;
}
