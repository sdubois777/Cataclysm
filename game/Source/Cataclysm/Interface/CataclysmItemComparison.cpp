// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmItemComparison.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmComparisonStandIn.h"
#include "Character/CataclysmPlayerBuild.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "UObject/Package.h"

const TCHAR* UCataclysmItemComparison::BasicHitLineName = TEXT("Basic attack hit, before the target's defences");

const TCHAR* UCataclysmItemComparison::NothingChangesLine = TEXT("No total on the character sheet changes.");

// ---------------------------------------------------------------------------
// The lines
// ---------------------------------------------------------------------------

FString UCataclysmItemComparison::ChangeLine(const FString& Stat, float Now, float With)
{
	return ChangeLineNamed(UCataclysmCharacterSheetLayout::NameFor(Stat),
						   UCataclysmCharacterSheetLayout::IsPercentage(Stat), Now, With);
}

FString UCataclysmItemComparison::ChangeLineNamed(const FString& Name, bool bPercentage, float Now, float With)
{
	// WRITTEN AS THE SHEET WRITES A FIGURE, so the comparison and the sheet agree to the digit.
	const auto Written = [bPercentage](float Figure)
	{
		return bPercentage ? UCataclysmCharacterSheetLayout::Percent(Figure)
						   : UCataclysmCharacterSheetLayout::Number(Figure);
	};
	const FString NowText = Written(Now);
	const FString WithText = Written(With);

	// THE FIGURES AS SHOWN DECIDE, which the header says why.
	if (NowText == WithText)
	{
		return FString();
	}

	return FString::Printf(TEXT("%s: %s to %s, %s %s"), *Name, *NowText, *WithText,
						   With > Now ? TEXT("up") : TEXT("down"), *Written(FMath::Abs(With - Now)));
}

TArray<FString> UCataclysmItemComparison::ChangeLines(const FCataclysmComparisonReading& Now,
													  const FCataclysmComparisonReading& With)
{
	TArray<FString> Lines;
	const TArray<FString>& Stats = UCataclysmCharacterSheetLayout::SheetStats();
	for (int32 Index = 0; Index < Stats.Num(); ++Index)
	{
		if (!Now.Figures.IsValidIndex(Index) || !With.Figures.IsValidIndex(Index))
		{
			break;
		}
		const FString Line = ChangeLine(Stats[Index], Now.Figures[Index], With.Figures[Index]);
		if (!Line.IsEmpty())
		{
			Lines.Add(Line);
		}
	}

	// AND THE ONE LINE THE SHEET DOES NOT HAVE, LAST: ruling (e).
	const FString Hit = ChangeLineNamed(BasicHitLineName, /*bPercentage=*/false, Now.BasicHit, With.BasicHit);
	if (!Hit.IsEmpty())
	{
		Lines.Add(Hit);
	}
	return Lines;
}

// ---------------------------------------------------------------------------
// The basic attack's hit
// ---------------------------------------------------------------------------

float UCataclysmItemComparison::BasicHitOf(const UAbilitySystemComponent* AbilitySystem,
										   const UCataclysmEquipmentComponent& Wearing, FName CreationDamageType)
{
	// THE FIRST WORN WEAPON'S, as the player's basic attack is: `UCataclysmWeaponSlotsComponent` takes the first entry
	// of `WornWeaponTypes` for it.
	const TArray<FString> WeaponTypes = Wearing.WornWeaponTypes();
	if (!AbilitySystem || WeaponTypes.IsEmpty())
	{
		return 0.0f;
	}

	// WITH THE ELEMENT IT WOULD BE GRANTED WITH: the first carried type in fill order, which is what the weapon slots
	// component's `GetDamageType` answers once these weapons are worn.
	const TArray<FString> DamageTypes = UCataclysmWeaponSlotsComponent::InFillOrder(
		UCataclysmEquipmentComponent::CarriedDamageTypesOf(Wearing.WornWeapons(), CreationDamageType),
		CreationDamageType);
	const FCataclysmWeaponSkill Basic = UCataclysmWeaponSlotsComponent::BasicAttackCarrying(
		UCataclysmItemModifiers::LoadBaseTable(), WeaponTypes[0],
		DamageTypes.IsEmpty() ? FString() : DamageTypes[0]);
	if (Basic.Slot == ECataclysmAbilitySlot::None)
	{
		return 0.0f;
	}

	// ITS OWN PERCENT, AND ITS SLOT'S WHEN IT STATES NONE, which is `UCataclysmSkillTemplate::GetDamagePercent`'s
	// order. The basic attack states none, so this is the Basic slot's figure, the design's 100.
	float Percent = Basic.DamagePercent;
	if (Percent < 0.0f)
	{
		const FCataclysmSkillSlotNumbers Numbers = UCataclysmSkillSlots::NumbersFor(
			UCataclysmSkillSlots::LoadGeneratedTable(), UCataclysmWeaponSkills::DesignedSlotOf(Basic));
		Percent = Numbers.bFound ? Numbers.DamagePercent : 0.0f;
	}

	return UCataclysmSkillEffects::SkillHitWithNoTarget(AbilitySystem, Basic.Tags, Percent);
}

// ---------------------------------------------------------------------------
// Reading a set of gear, and comparing two
// ---------------------------------------------------------------------------

FCataclysmComparisonReading UCataclysmItemComparison::ReadingOf(const TArray<FCataclysmItem>& WornSlots,
																const UCataclysmEquipmentComponent& Live,
																const UAbilitySystemComponent* LiveAbilities,
																ACataclysmComparisonStandIn& StandIn)
{
	FCataclysmComparisonReading Reading;
	UCataclysmAbilitySystemComponent* Gathered = StandIn.GetCataclysmAbilitySystem();
	if (!Gathered)
	{
		return Reading;
	}

	// THE FLOOR'S LINES, WHICH THE GATHERER READS OFF THE ABILITY SYSTEM IT IS HANDED: copied from the player's, so a
	// floor that takes a share of maximum health takes it from both columns. Empty outside a dungeon.
	TMap<FName, TArray<FCataclysmStatModifier>> FloorLines;
	if (const UCataclysmAbilitySystemComponent* LiveCataclysm = Cast<const UCataclysmAbilitySystemComponent>(LiveAbilities))
	{
		FloorLines = LiveCataclysm->GetDungeonStatModifiers();
	}
	Gathered->SetDungeonStatModifiers(MoveTemp(FloorLines));

	// THE SLOTS AS LISTED, PUT BACK EXACTLY, on an equipment component that belongs to no actor: the corrupted
	// double's way (`ACataclysmConsumptionDoubleCharacter::WearThePlayersBuild`). `RestoreWornGear` decides nothing,
	// so what `PreviewEquip` reported is what is gathered.
	TArray<TPair<ECataclysmGearSlot, FCataclysmItem>> Worn;
	for (int32 Index = 0; Index < WornSlots.Num() && Index < UCataclysmEquipmentComponent::SlotCount; ++Index)
	{
		if (!WornSlots[Index].Base.IsNone())
		{
			Worn.Emplace(static_cast<ECataclysmGearSlot>(Index), WornSlots[Index]);
		}
	}
	UCataclysmEquipmentComponent* Wardrobe = NewObject<UCataclysmEquipmentComponent>(GetTransientPackage());
	Wardrobe->RestoreWornGear(Worn);
	Wardrobe->SetDisabledSlot(Live.GetDisabledSlot());

	// THE ONE GATHERER (issue #1054), WITH THE PLAYER'S NUMBERS read as the live refresh reads them.
	FCataclysmPlayerBuild Numbers;
	const bool bNumbers = Live.PlayerNumbers(Numbers);
	Wardrobe->RefreshAttributesAs(Gathered, bNumbers ? &Numbers : nullptr, ECataclysmPoolFill::FillToMaximum);

	for (const FString& Stat : UCataclysmCharacterSheetLayout::SheetStats())
	{
		Reading.Figures.Add(UCataclysmCharacterSheetLayout::FigureFor(Stat, Gathered));
	}
	Reading.BasicHit = BasicHitOf(Gathered, *Wardrobe, bNumbers ? Numbers.CreationDamageType : FName());
	Reading.bRead = true;
	return Reading;
}

FCataclysmComparisonResult UCataclysmItemComparison::Compare(const FCataclysmItem& Hovered,
															 UCataclysmEquipmentComponent& Live,
															 const UAbilitySystemComponent* LiveAbilities,
															 ACataclysmComparisonStandIn& StandIn,
															 const FCataclysmComparisonReading* Now)
{
	FCataclysmComparisonResult Result;

	// WHAT THE EQUIP ACTION WOULD DO, decided by `Equip` itself (ruling (a)).
	const FCataclysmEquipPreview Preview = Live.PreviewEquip(Hovered);
	if (!Preview.bWearable)
	{
		return Result;
	}

	const FCataclysmComparisonReading Before =
		Now && Now->bRead ? *Now : ReadingOf(Live.GetSlots(), Live, LiveAbilities, StandIn);
	const FCataclysmComparisonReading After = ReadingOf(Preview.SlotsAfter, Live, LiveAbilities, StandIn);
	if (!Before.bRead || !After.bRead)
	{
		return Result;
	}

	Result.bCompared = true;
	Result.Slot = Preview.Slot;
	Result.Replaced = Preview.Replaced;
	Result.ChangeLines = ChangeLines(Before, After);
	Result.BasicHitNow = Before.BasicHit;
	Result.BasicHitWith = After.BasicHit;
	return Result;
}

// ---------------------------------------------------------------------------
// The columns
// ---------------------------------------------------------------------------

FString UCataclysmItemComparison::HeadingFor(const FCataclysmComparisonResult& Result, const UDataTable* BaseTable,
											 const UDataTable* AffixTable)
{
	if (Result.Replaced.IsEmpty())
	{
		return FString::Printf(TEXT("If worn in %s:"), *UCataclysmGearSlots::DisplayName(Result.Slot));
	}

	TArray<FString> Names;
	for (const FCataclysmItem& Off : Result.Replaced)
	{
		// AN ITEM WHOSE BASE IS NOT IN THE TABLE HAS NO NAME TO GIVE, so it is called by its base.
		const FString Name = UCataclysmItemName::NameOf(Off, BaseTable, AffixTable);
		Names.Add(Name.IsEmpty() ? Off.Base.ToString() : Name);
	}
	return FString::Printf(TEXT("If worn, in place of %s:"), *FString::Join(Names, TEXT(" and ")));
}

void UCataclysmItemComparison::Columns(const FCataclysmCarriedSlot& Hovered, const FCataclysmComparisonResult& Result,
									   const UDataTable* BaseTable, const UDataTable* AffixTable,
									   const UDataTable* CraftingMaterialTable,
									   const UDataTable* PositiveEnchantmentTable,
									   const UDataTable* NegativeEnchantmentTable, FString& OutLeft,
									   FString& OutRight)
{
	TArray<FString> Left = UCataclysmItemTooltip::LinesFor(Hovered, BaseTable, AffixTable, CraftingMaterialTable,
														   PositiveEnchantmentTable, NegativeEnchantmentTable);
	OutRight.Reset();
	if (!Result.bCompared)
	{
		OutLeft = FString::Join(Left, TEXT("\n"));
		return;
	}

	// A BLANK LINE, THEN WHAT WEARING IT WOULD DO.
	Left.Add(FString());
	Left.Add(HeadingFor(Result, BaseTable, AffixTable));
	if (Result.ChangeLines.IsEmpty())
	{
		Left.Add(FString(NothingChangesLine));
	}
	else
	{
		Left.Append(Result.ChangeLines);
	}
	OutLeft = FString::Join(Left, TEXT("\n"));

	// THE SECOND COLUMN: WHAT WOULD COME OFF, EACH AS ITS OWN TOOLTIP READS.
	TArray<FString> Right;
	if (Result.Replaced.IsEmpty())
	{
		Right.Add(FString::Printf(TEXT("Compared with nothing: %s is empty."),
								  *UCataclysmGearSlots::DisplayName(Result.Slot)));
	}
	else
	{
		Right.Add(FString(TEXT("Worn now:")));
		for (int32 Index = 0; Index < Result.Replaced.Num(); ++Index)
		{
			if (Index > 0)
			{
				Right.Add(FString());
			}
			FCataclysmCarriedSlot AsCarried;
			AsCarried.Item = Result.Replaced[Index];
			Right.Append(UCataclysmItemTooltip::LinesFor(AsCarried, BaseTable, AffixTable, CraftingMaterialTable,
														 PositiveEnchantmentTable, NegativeEnchantmentTable));
		}
	}
	OutRight = FString::Join(Right, TEXT("\n"));
}
