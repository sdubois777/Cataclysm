// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmConsumptionDoubleCharacter.h"

#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "UObject/Package.h"

bool ACataclysmConsumptionDoubleCharacter::WearThePlayersBuild(const FCataclysmPlayerBuild& Build)
{
	UAbilitySystemComponent* Abilities = GetAbilitySystemComponent();
	if (!Abilities)
	{
		return false;
	}

	// AT THE PLAYER'S OWN LEVEL, WITH THE PLAYER'S OWN POINTS AND ITEMS (R3), not at the tier's reference level the
	// Stalker is raised at: the build is applied as it was handed over.
	HeldBuild = Build;

	// ITS ITEMS AS THEY WERE WORN, ON AN EQUIPMENT COMPONENT THAT BELONGS TO NO ACTOR, for the reason the Stalker's
	// header gives. `RestoreWornGear` and not `EquipInto`: it puts each item in the slot it was worn in and decides
	// nothing, so two two-handed weapons a passive allowed the player stay as they were worn.
	UCataclysmEquipmentComponent* Wardrobe = NewObject<UCataclysmEquipmentComponent>(GetTransientPackage());
	Wardrobe->RestoreWornGear(HeldBuild.WornPairs());

	// THE WEAPONS THE PLAYER WORE, IN ITS HANDS, as the player's own hands draw them. Issue #2806.
	DrawWornWeapons(Wardrobe);

	// THE ONE GATHERER (issue #1054): the items, the attribute points, the passive tree counted up to the points the
	// build earned, and the class line at the build's level, with the pools filled. NO PER-TIER MULTIPLIER (R4).
	if (Wardrobe->RefreshAttributesAs(Abilities, &HeldBuild, ECataclysmPoolFill::FillToMaximum) <= 0)
	{
		return false;
	}

	// THE STALKER'S STARTING FERVOUR, ITS ULTIMATE'S COST, so it can cast its Ultimate once in an encounter. The owner's
	// answer of 2026-10-26 for the Stalker ("Start with enough Fervour"), taken for the double because it is the same
	// creature with the same brain. Fervour is one bar every class shares, and the class line fills none of it.
	Abilities->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
									   UCataclysmDungeonModifierEffects::CorruptedStalkerStartingFervour);

	bHoldsAPlayerBuild = true;
	Pieces.Reset();
	for (const FCataclysmWornItem& Entry : HeldBuild.Worn)
	{
		Pieces.Add(Entry.Item);
	}

	// ITS SKILLS, AS THE PLAYER'S SLOTS ARE FILLED: every combination of a worn weapon type and a carried damage type
	// (`PoolFor`, the types in fill order), and the player's choices on their keys (`FillWithChoices`). A held skill
	// with no shape is skipped by the brain (`EnemyAbilities`).
	const TArray<FString> WeaponTypes = Wardrobe->WornWeaponTypes();
	const TArray<FString> DamageTypes = UCataclysmWeaponSlotsComponent::InFillOrder(
		UCataclysmEquipmentComponent::CarriedDamageTypesOf(Wardrobe->WornWeapons(), HeldBuild.CreationDamageType),
		HeldBuild.CreationDamageType);

	// AND THE KEYS THE PLAYER'S GAME FILLED BY ITSELF, since issue #837 slice 1 (ruled 2026-11-24: a double holds the
	// keys the player actually had): the remembered defaults after the choices, as the player's own fill reads them.
	// FROM THE GENERATED TABLE, or from the copy a test handed it (`UseWeaponSkillTableForTests`, issue #2928); always
	// the generated one in play.
	const UDataTable* SkillTable = WeaponSkillTableForTests.Get();
	if (!SkillTable)
	{
		SkillTable = UCataclysmWeaponSkills::LoadGeneratedTable();
	}
	Skills = UCataclysmWeaponSkills::FillWithChoices(
		UCataclysmWeaponSkills::PoolFor(SkillTable, WeaponTypes, DamageTypes),
		HeldBuild.SkillAssignment, HeldBuild.RememberedDefaults);

	// ITS FIRST WEAPON'S BASIC ATTACK, the first OCCUPIED weapon slot's, as the player's basic attack is.
	BasicAttack = WeaponTypes.IsEmpty()
		? FCataclysmWeaponSkill()
		: UCataclysmWeaponSkills::BasicAttackFor(UCataclysmItemModifiers::LoadBaseTable(), WeaponTypes[0]);

	// AND IT DEALS THE BUILD'S DAMAGE TYPE, the one its character chose at creation, as the Stalker deals its entry's.
	// Ruled by the coordinating session on 2026-11-25 for slice B3. A build captured before the choice was recorded
	// holds none, and the double then keeps its class's type, Demonic.
	if (!HeldBuild.CreationDamageType.IsNone())
	{
		DamageType = HeldBuild.CreationDamageType;
	}
	return true;
}

void ACataclysmConsumptionDoubleCharacter::DropWhatItWore(UWorld* World, const FVector& At, FRandomStream& Stream)
{
	// NONE OF ITS GEAR (O2): its items are copies of the player's own, and dropping them would duplicate gear.
}
