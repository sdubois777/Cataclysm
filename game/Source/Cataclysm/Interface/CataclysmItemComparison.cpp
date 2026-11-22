// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmItemComparison.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmComparisonStandIn.h"
#include "Character/CataclysmPlayerBuild.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmGameInstance.h"
#include "UObject/Package.h"

const TCHAR* UCataclysmItemComparison::BasicHitLineName = TEXT("Basic attack hit, before the target's defences");

const TCHAR* UCataclysmItemComparison::NothingChangesLine = TEXT("No total on the character sheet changes.");

FString UCataclysmItemComparison::FamilyHitLineName(FName Family)
{
	return FString::Printf(TEXT("Basic attack hit on a %s creature's health, before its defences"), *Family.ToString());
}

TArray<FName> UCataclysmItemComparison::RunFamiliesIn(const UObject* WorldContext)
{
	TArray<FName> Families;
	if (const UCataclysmEmpireRun* Run = UCataclysmGameInstance::EmpireRunFor(WorldContext, /*bStartIfNone=*/false))
	{
		for (const ECataclysmType Cataclysm : Run->ActiveCataclysms)
		{
			if (Cataclysm != ECataclysmType::None)
			{
				Families.AddUnique(UCataclysmRoster::NameFor(Cataclysm));
			}
		}
	}
	return Families;
}

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

	// AND THE LINES THE SHEET DOES NOT HAVE, LAST: ruling (e)'s plain hit, then the hit on each family's creatures,
	// the coordinating session's ruling of 2026-11-19. A family only one reading priced has nothing to compare.
	const FString Hit = ChangeLineNamed(BasicHitLineName, /*bPercentage=*/false, Now.BasicHit, With.BasicHit);
	if (!Hit.IsEmpty())
	{
		Lines.Add(Hit);
	}
	for (int32 Index = 0; Index < Now.Families.Num(); ++Index)
	{
		const int32 InWith = With.Families.IndexOfByKey(Now.Families[Index]);
		if (!Now.FamilyHits.IsValidIndex(Index) || !With.FamilyHits.IsValidIndex(InWith))
		{
			continue;
		}
		const FString OnFamily = ChangeLineNamed(FamilyHitLineName(Now.Families[Index]), /*bPercentage=*/false,
												 Now.FamilyHits[Index], With.FamilyHits[InWith]);
		if (!OnFamily.IsEmpty())
		{
			Lines.Add(OnFamily);
		}
	}
	return Lines;
}

// ---------------------------------------------------------------------------
// The basic attack's hit
// ---------------------------------------------------------------------------

namespace
{
	/** The basic attack a set of gear swings and its damage percent, as `BasicHitOf` and `BasicHitOnAFamilyOf` price
	 *  it; false with no worn weapon that supplies one. */
	bool ComparisonBasicAttackOf(const UCataclysmEquipmentComponent& Wearing, FName CreationDamageType,
					   FCataclysmWeaponSkill& OutBasic, float& OutPercent, FString& OutWeaponType)
	{
		// THE FIRST WORN WEAPON'S, as the player's basic attack is: `UCataclysmWeaponSlotsComponent` takes the first
		// entry of `WornWeaponTypes` for it.
		const TArray<FString> WeaponTypes = Wearing.WornWeaponTypes();
		if (WeaponTypes.IsEmpty())
		{
			return false;
		}

		// WITH THE ELEMENT IT WOULD BE GRANTED WITH: the first carried type in fill order, which is what the weapon
		// slots component's `GetDamageType` answers once these weapons are worn.
		const TArray<FString> DamageTypes = UCataclysmWeaponSlotsComponent::InFillOrder(
			UCataclysmEquipmentComponent::CarriedDamageTypesOf(Wearing.WornWeapons(), CreationDamageType),
			CreationDamageType);
		OutBasic = UCataclysmWeaponSlotsComponent::BasicAttackCarrying(
			UCataclysmItemModifiers::LoadBaseTable(), WeaponTypes[0],
			DamageTypes.IsEmpty() ? FString() : DamageTypes[0]);
		if (OutBasic.Slot == ECataclysmAbilitySlot::None)
		{
			return false;
		}

		// ITS OWN PERCENT, AND ITS SLOT'S WHEN IT STATES NONE, which is `UCataclysmSkillTemplate::GetDamagePercent`'s
		// order. The basic attack states none, so this is the Basic slot's figure, the design's 100.
		OutPercent = OutBasic.DamagePercent;
		if (OutPercent < 0.0f)
		{
			const FCataclysmSkillSlotNumbers Numbers = UCataclysmSkillSlots::NumbersFor(
				UCataclysmSkillSlots::LoadGeneratedTable(), UCataclysmWeaponSkills::DesignedSlotOf(OutBasic));
			OutPercent = Numbers.bFound ? Numbers.DamagePercent : 0.0f;
		}
		OutWeaponType = WeaponTypes[0];
		return true;
	}
}

float UCataclysmItemComparison::BasicHitOf(const UAbilitySystemComponent* AbilitySystem,
										   const UCataclysmEquipmentComponent& Wearing, FName CreationDamageType)
{
	FCataclysmWeaponSkill Basic;
	float Percent = 0.0f;
	FString WeaponType;
	if (!AbilitySystem || !ComparisonBasicAttackOf(Wearing, CreationDamageType, Basic, Percent, WeaponType))
	{
		return 0.0f;
	}
	return UCataclysmSkillEffects::SkillHitWithNoTarget(AbilitySystem, Basic.Tags, Percent);
}

float UCataclysmItemComparison::BasicHitOnAFamilyOf(const UAbilitySystemComponent* AbilitySystem,
													const UCataclysmEquipmentComponent& Wearing,
													FName CreationDamageType, FName Family)
{
	FCataclysmWeaponSkill Basic;
	float Percent = 0.0f;
	FString WeaponType;
	if (!AbilitySystem || !ComparisonBasicAttackOf(Wearing, CreationDamageType, Basic, Percent, WeaponType))
	{
		return 0.0f;
	}

	// THE FAMILY'S INCREASE JOINS THE INCREASES, as `ApplyHit` adds `DamageAgainstTypeOf`; `SkillHitWithNoTarget`'s
	// `ExtraIncreases` is that bracket.
	const float Hit = UCataclysmSkillEffects::SkillHitWithNoTarget(
		AbilitySystem, Basic.Tags, Percent, /*SkillHealthCostPercent=*/0.0f, /*MetresMovedBeforeBlow=*/0.0f,
		UCataclysmSkillEffects::DamageAgainstTypeNamed(AbilitySystem, Family));

	// AND A SLASHING WEAPON'S MORE AGAINST HEALTH, which the calculation's step 9 multiplies the damage health takes
	// by. The first worn weapon's sub-type, as `UCataclysmWeaponSlotsComponent::GetEquippedSubType` reads it.
	const bool bSlashing = UCataclysmWeaponSlotsComponent::SubTypeOfWeaponType(
		UCataclysmItemModifiers::LoadBaseTable(), WeaponType).Equals(TEXT("Slashing"), ESearchCase::IgnoreCase);
	return bSlashing ? Hit * (1.0f + UCataclysmDamageCalculation::SubtypeBonus / 100.0f) : Hit;
}

// ---------------------------------------------------------------------------
// Reading a set of gear, and comparing two
// ---------------------------------------------------------------------------

FCataclysmComparisonReading UCataclysmItemComparison::ReadingOf(const TArray<FCataclysmItem>& WornSlots,
																const UCataclysmEquipmentComponent& Live,
																const UAbilitySystemComponent* LiveAbilities,
																ACataclysmComparisonStandIn& StandIn,
																const TArray<FName>* Families)
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
	const FName Creation = bNumbers ? Numbers.CreationDamageType : FName();
	Reading.BasicHit = BasicHitOf(Gathered, *Wardrobe, Creation);

	// AND THE HIT ON EACH OF THE RUN'S FAMILIES, the coordinating session's ruling of 2026-11-19.
	Reading.Families = Families ? *Families : RunFamiliesIn(&Live);
	for (const FName Family : Reading.Families)
	{
		Reading.FamilyHits.Add(BasicHitOnAFamilyOf(Gathered, *Wardrobe, Creation, Family));
	}
	Reading.bRead = true;
	return Reading;
}

FCataclysmComparisonResult UCataclysmItemComparison::Compare(const FCataclysmItem& Hovered,
															 UCataclysmEquipmentComponent& Live,
															 const UAbilitySystemComponent* LiveAbilities,
															 ACataclysmComparisonStandIn& StandIn,
															 const FCataclysmComparisonReading* Now,
															 const TArray<FName>* Families)
{
	FCataclysmComparisonResult Result;

	// WHAT THE EQUIP ACTION WOULD DO, decided by `Equip` itself (ruling (a)).
	const FCataclysmEquipPreview Preview = Live.PreviewEquip(Hovered);
	if (!Preview.bWearable)
	{
		return Result;
	}

	// THE SECOND READING PRICES THE FIRST'S FAMILIES, so the two always have the same lines to set side by side.
	const FCataclysmComparisonReading Before =
		Now && Now->bRead ? *Now : ReadingOf(Live.GetSlots(), Live, LiveAbilities, StandIn, Families);
	const FCataclysmComparisonReading After =
		ReadingOf(Preview.SlotsAfter, Live, LiveAbilities, StandIn, &Before.Families);
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
		// EACH PIECE BY THE NAME THE PLAYER SEES FOR IT: its whole name, and else its base's own name, as its inventory
		// cell shows it (`UCataclysmForge::NameOf` says why). Until the item comparison window ran (issue #832) the
		// heading fell back to the row key for a piece with no whole name, and read "If worn, in place of
		// Weapon_Sword:" to the player.
		Names.Add(UCataclysmForge::NameOf(Off, BaseTable, AffixTable));
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

bool UCataclysmItemComparison::TooltipColumnsFor(const FCataclysmCarriedSlot& Slot,
												 UCataclysmEquipmentComponent* Equipment,
												 const UAbilitySystemComponent* Abilities,
												 ACataclysmComparisonStandIn* StandIn,
												 FCataclysmComparisonReading* WornNow, const UDataTable* BaseTable,
												 const UDataTable* AffixTable, const UDataTable* CraftingMaterialTable,
												 const UDataTable* PositiveEnchantmentTable,
												 const UDataTable* NegativeEnchantmentTable, FString& OutLeft,
												 FString& OutRight)
{
	OutLeft.Reset();
	OutRight.Reset();

	// A PIECE OF GEAR IS COMPARED WITH WHAT WEARING IT WOULD REPLACE, since issue #832, in two columns, shown whenever
	// it is hovered with no key held (ruling (d)). Moved here from `UCataclysmInventoryWidget` for issue #2807, so the
	// ground hover runs the same steps.
	FCataclysmComparisonResult Compared;
	if (!Slot.Item.Base.IsNone() && Equipment && StandIn)
	{
		if (WornNow && !WornNow->bRead)
		{
			*WornNow = ReadingOf(Equipment->GetSlots(), *Equipment, Abilities, *StandIn);
		}
		Compared = Compare(Slot.Item, *Equipment, Abilities, *StandIn, WornNow);
	}

	if (Compared.bCompared)
	{
		Columns(Slot, Compared, BaseTable, AffixTable, CraftingMaterialTable, PositiveEnchantmentTable,
				NegativeEnchantmentTable, OutLeft, OutRight);
		return true;
	}

	// A MATERIAL, A GEM STACK, AN EMPTY SLOT, OR GEAR WITH NOTHING TO COMPARE AGAINST: its own text, read for the
	// character, whose own amount of a gem's stat says whether the gem does anything for them (issue #2835).
	OutLeft = UCataclysmItemTooltip::TextFor(Slot, BaseTable, AffixTable, CraftingMaterialTable,
											 PositiveEnchantmentTable, NegativeEnchantmentTable, Abilities);
	return false;
}
