// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmWeaponSlotsComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmUndesignedSkill.h"
#include "Items/CataclysmItem.h"
#include "Data/CataclysmDataRows.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "Cataclysm.h"
#include "Engine/DataTable.h"
#include "GameplayTagsManager.h"

UCataclysmWeaponSlotsComponent::UCataclysmWeaponSlotsComponent()
{
	// Nothing to do per frame. Slots change when a weapon changes, which is an
	// event, not a poll.
	PrimaryComponentTick.bCanEverTick = false;

	UndesignedSkillClass = UCataclysmUndesignedSkill::StaticClass();
}

UCataclysmAbilitySystemComponent* UCataclysmWeaponSlotsComponent::GetAbilitySystem() const
{
	// Through the interface rather than by searching the owner for a component.
	// The player's ability system lives on the player state, not on the pawn,
	// so looking on the owning actor would find nothing.
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	return Cast<UCataclysmAbilitySystemComponent>(
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner));
}

FString UCataclysmWeaponSlotsComponent::GetEquippedSubType() const
{
	if (EquippedWeaponType.IsEmpty())
	{
		return FString();
	}

	const UDataTable* Bases = ItemBaseTable
		? ItemBaseTable.Get()
		: UCataclysmItemModifiers::LoadBaseTable();
	if (!Bases)
	{
		return FString();
	}

	FString Found;
	Bases->ForeachRow<FCataclysmItemBaseRow>(
		TEXT("UCataclysmWeaponSlotsComponent::GetEquippedSubType"),
		[&](const FName&, const FCataclysmItemBaseRow& Row)
		{
			if (Found.IsEmpty()
				&& Row.WeaponType.Equals(EquippedWeaponType, ESearchCase::IgnoreCase))
			{
				Found = Row.SubType;
			}
		});

	return Found;
}

FString UCataclysmWeaponSlotsComponent::SubTypeOf(const AActor* Actor)
{
	if (!Actor)
	{
		return FString();
	}

	if (const UCataclysmWeaponSlotsComponent* Slots =
			Actor->FindComponentByClass<UCataclysmWeaponSlotsComponent>())
	{
		return Slots->GetEquippedSubType();
	}

	// No weapon slots at all, which is every enemy: they attack from their own
	// attack damage rather than from a weapon. Empty is the answer every hit gave
	// before this existed, so nothing about an enemy's hit changes.
	return FString();
}

int32 UCataclysmWeaponSlotsComponent::EquipWeaponType(const FString& NewWeaponType)
{
	// Taken back first, and unconditionally. Refilling without emptying would
	// leave the previous weapon's abilities granted alongside the new ones, and
	// two abilities carrying the same slot tag means one key firing both.
	UnequipWeapon();

	EquippedWeaponType = NewWeaponType;
	if (EquippedWeaponType.IsEmpty())
	{
		return 0;
	}

	AvailableSkills.Reset();

	// THE BASIC ATTACK COMES FROM THE WEAPON, NOT FROM THE MATRIX, so it is
	// collected first and separately. Two things follow from that and both are
	// intended. It does not depend on the matrix loading, and it does not depend
	// on the damage type: a War Wand has no matrix skills at all, because the
	// design says not every damage type covers every weapon, and it still swings.
	// Issue #524.
	if (!ItemBaseTable)
	{
		ItemBaseTable = UCataclysmItemModifiers::LoadBaseTable();
	}

	FCataclysmWeaponSkill Basic = UCataclysmWeaponSkills::BasicAttackFor(
		ItemBaseTable, EquippedWeaponType);
	if (Basic.Slot != ECataclysmAbilitySlot::None)
	{
		// THE ELEMENT TAG IS ADDED HERE BECAUSE ONLY THIS COMPONENT KNOWS IT.
		// One Item Bases row serves a weapon across every damage type it can
		// roll, so the row cannot state one; the equipped weapon's rolled type
		// is what DamageType holds.
		//
		// WITHOUT IT THE BASIC ATTACK IS THE ONE SLOT NO SCOPED MODIFIER
		// REACHES. UCataclysmStatPipeline::ModifierApplies asks whether the
		// skill in hand carries every tag a modifier requires, and every gear
		// increase the design has is scoped to an element. Burning Wrath grants
		// increased damage scoped to Element.Demonic; untagged, the basic attack
		// would be the only one of the seven granted skills it did not increase,
		// and it is the 100% weapon damage anchor the other six are percentages
		// of. Nothing reports that: ModifierApplies simply returns false.
		//
		// ErrorIfNotFound is false so a damage type with no registered tag adds
		// nothing rather than raising during equip, matching what
		// CataclysmAbilitySlots::Tag and UCataclysmSkillTemplate::ElementTag do.
		if (!DamageType.IsEmpty())
		{
			const FGameplayTag Element =
				UGameplayTagsManager::Get().RequestGameplayTag(
					FName(*FString::Printf(TEXT("Element.%s"), *DamageType)),
					/*ErrorIfNotFound=*/false);
			if (Element.IsValid())
			{
				Basic.Tags.AddTag(Element);
			}
			else
			{
				UE_LOG(LogCataclysm, Warning,
					TEXT("There is no Element.%s tag, so the %s's basic attack "
						 "carries no element and no scoped gear modifier will "
						 "reach it."),
					*DamageType, *EquippedWeaponType);
			}
		}

		AvailableSkills.Add(Basic);
	}

	if (!WeaponSkillTable)
	{
		WeaponSkillTable = UCataclysmWeaponSkills::LoadGeneratedTable();
	}

	if (WeaponSkillTable)
	{
		AvailableSkills.Append(UCataclysmWeaponSkills::SkillsFor(
			WeaponSkillTable, EquippedWeaponType, DamageType));
	}
	else
	{
		// LoadGeneratedTable has already said why. Not returned on any more,
		// because the basic attack does not come from that table and a character
		// who can still swing is better than one who cannot.
		UE_LOG(LogCataclysm, Warning,
			TEXT("The %s has no skill matrix to read, so only its basic attack "
				 "is available."),
			*EquippedWeaponType);
	}

	// THE NUMBER EVERY SKILL IS A PERCENTAGE OF, and until issue #173 nothing
	// set it: UCataclysmCombatAttributeSet::AttackDamage was initialised to zero
	// and never changed, so a Heavy Attack at 250% of it dealt nothing and the
	// burn rider, being a share of the hit, applied nothing either.
	//
	// Set before the abilities are granted, so a skill activated on the same
	// frame as the equip already sees it.
	ApplyWeaponDamage();

	if (AvailableSkills.IsEmpty())
	{
		// A weapon its damage type does not cover AND which arms nobody, which
		// today is only the Shield. Verbose, not a warning: the design says not
		// every damage type has skills for every weapon type, and issue #619 says
		// a Shield composes no hit.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The %s has no %s skills and no basic attack, so no slot was "
				 "filled."),
			*EquippedWeaponType, *DamageType);
		return 0;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem = GetAbilitySystem();
	if (!AbilitySystem)
	{
		// Not an error. The skills are still recorded, and the component is
		// readable before the ability system exists -- possession order is not
		// something this component controls.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The %s offers %d skills but there is no ability system to "
				 "grant them to yet."),
			*EquippedWeaponType, AvailableSkills.Num());
		return 0;
	}

	if (!IsValid(UndesignedSkillClass))
	{
		UE_LOG(LogCataclysm, Error,
			TEXT("No ability class to grant into a slot, so the %s filled none "
				 "of its %d slots."),
			*EquippedWeaponType, AvailableSkills.Num());
		return 0;
	}

	int32 Filled = 0;
	for (const FCataclysmWeaponSkill& Skill : AvailableSkills)
	{
		// THE ROW DECIDES WHICH CLASS IS GRANTED. A row naming a shape gets that
		// shape's shared template; a row with no shape still gets the
		// placeholder, which is all 61 War rows and every undesigned Demonic
		// one. That is what makes adding a skill of an existing shape a workbook
		// edit and no C++ at all, which is issue #37's second acceptance
		// criterion.
		TSubclassOf<UCataclysmGameplayAbility> AbilityClass =
			UCataclysmWeaponSkills::TemplateFor(Skill.Shape);
		if (!AbilityClass)
		{
			AbilityClass = UndesignedSkillClass;
		}

		const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
			AbilityClass, Skill.Slot, /*Level=*/1, /*SourceObject=*/this);

		if (!Handle.IsValid())
		{
			continue;
		}

		GrantedHandles.AddAbility(Handle);
		++Filled;

		// Stamped on the granted INSTANCE rather than on the class, because one
		// class stands for every skill of that shape. Two characters holding
		// different Projectile skills share UCataclysmProjectileSkill and differ
		// only in what is written here.
		FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UGameplayAbility* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;

		if (UCataclysmSkillTemplate* Template = Cast<UCataclysmSkillTemplate>(Instance))
		{
			Template->SkillName = Skill.Name;
			Template->SkillDescription = Skill.Description;
			Template->Params = Skill.Params;
			Template->SkillTags = Skill.Tags;
		}
		else if (UCataclysmUndesignedSkill* Placeholder =
					Cast<UCataclysmUndesignedSkill>(Instance))
		{
			Placeholder->SkillName = Skill.Name;
			Placeholder->SkillDescription = Skill.Description;
		}
	}

	UE_LOG(LogCataclysm, Verbose, TEXT("The %s filled %d of its %d slots."),
		*EquippedWeaponType, Filled, AvailableSkills.Num());

	return Filled;
}

int32 UCataclysmWeaponSlotsComponent::EquipStartingWeapon()
{
	if (StartingWeaponType.IsEmpty())
	{
		// A deliberate choice to begin holding nothing is legitimate, so this is
		// not a warning. It is also what a character creator would produce
		// before the player has chosen.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("No starting weapon type is set, so no slot was filled."));
		return 0;
	}

	const int32 Filled = EquipWeaponType(StartingWeaponType);

	// ASKED OF THE MATRIX SLOTS, NOT OF THE TOTAL, and that distinction keeps
	// this warning working. Since issue #524 the basic attack is granted from the
	// weapon base whatever the damage type covers, so counting it would let a
	// starting weapon with no designed skills at all report one filled slot and
	// stay silent.
	bool bAnyDesignedSkill = false;
	for (const FCataclysmWeaponSkill& Skill : AvailableSkills)
	{
		if (Skill.Slot != ECataclysmAbilitySlot::BasicAttack)
		{
			bAnyDesignedSkill = true;
			break;
		}
	}

	if (!bAnyDesignedSkill)
	{
		// LOUD, BECAUSE THIS IS A CHARACTER WHO CAN ONLY SWING. It means the
		// starting weapon type is one the damage type does not cover, or is
		// misspelled -- and the symptom is a game that runs normally and does
		// nothing when a skill key is pressed, which is exactly how issue #169
		// went unnoticed.
		UE_LOG(LogCataclysm, Warning,
			TEXT("The starting weapon is a %s and the damage type is %s, which "
				 "grants no skills at all. Check StartingWeaponType against the "
				 "Weapon Skills sheet of docs/All_Things_Cataclysm.xlsx."),
			*StartingWeaponType, *DamageType);
	}

	return Filled;
}

void UCataclysmWeaponSlotsComponent::ApplyWeaponDamage()
{
	UCataclysmAbilitySystemComponent* AbilitySystem = GetAbilitySystem();
	if (!AbilitySystem)
	{
		return;
	}

	// AN ABILITY SYSTEM WITHOUT COMBAT ATTRIBUTES IS A LEGITIMATE ACTOR, and
	// writing to an attribute it does not have raises an engine ensure --
	// "Unable to get attribute set for attribute AttackDamage" -- rather than
	// failing quietly. The player and every enemy carry the combat set; a bare
	// ability system holder, which several tests build, does not.
	const FGameplayAttribute Attribute =
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Attribute))
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The ability system has no combat attribute set, so the %s's "
				 "damage was not applied."),
			EquippedWeaponType.IsEmpty() ? TEXT("weapon") : *EquippedWeaponType);
		return;
	}

	if (!ItemBaseTable)
	{
		ItemBaseTable = UCataclysmItemModifiers::LoadBaseTable();
	}

	// SET, NOT ADDED, which is what makes swapping weapons safe. The character
	// holds one weapon, so its damage replaces whatever the last one supplied
	// rather than accumulating -- and an empty type sets zero, which is what
	// holding nothing is worth.
	const float Damage = EquippedWeaponType.IsEmpty()
		? 0.0f
		: UCataclysmItemModifiers::WeaponDamageForType(
			ItemBaseTable, EquippedWeaponType, WeaponGearLevel);

	AbilitySystem->SetNumericAttributeBase(Attribute, Damage);

	if (Damage <= 0.0f && !EquippedWeaponType.IsEmpty())
	{
		// A weapon that supplies no damage makes every skill deal nothing, and
		// the symptom is a game that runs normally and kills nothing.
		UE_LOG(LogCataclysm, Warning,
			TEXT("The %s supplies no attack damage, so every skill will deal "
				 "zero. Check its attack_damage implicit in the Item Bases "
				 "sheet of docs/All_Things_Cataclysm.xlsx."),
			*EquippedWeaponType);
	}
	else if (EquippedWeaponType.IsEmpty())
	{
		// Every equip clears the previous weapon first, so this runs on the way
		// through. Named separately because "The  supplies 0.0" reads as a bug.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("Holding nothing, so there is no attack damage."));
	}
	else
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The %s supplies %.1f attack damage at gear level %d."),
			*EquippedWeaponType, Damage, WeaponGearLevel);
	}

	// AND THE RATE IT SWINGS AT, WHICH NOTHING WROTE UNTIL ISSUE #647. The
	// attribute existed and was replicated, and was initialised to zero with the
	// comment "supplied by the equipped weapon" -- describing an intention that
	// was never built, in a form that reads as a statement of fact. Every weapon
	// in ItemBases.csv states a rate and the row struct carries the column; the
	// chain stopped here.
	//
	// TWO THINGS WERE WORTH NOTHING BECAUSE OF IT. Every increased attack speed
	// affix multiplied zero -- the reference build in
	// sim/cataclysm_sim/reference_build.py spends four suffix slots on exactly
	// that, and records the same failure having already happened once on the
	// simulation side, as issue #120. And the automatic basic attack had no rate
	// to fire at, which is half of why it was never built.
	//
	// SET, NOT ADDED, for the same reason the damage above is: a character holds
	// one weapon, so its rate replaces the last one's rather than accumulating,
	// and holding nothing sets zero. Zero is read as "never swings" rather than
	// as "swings infinitely fast".
	const FGameplayAttribute SpeedAttribute =
		UCataclysmCombatAttributeSet::GetAttackSpeedAttribute();
	if (AbilitySystem->HasAttributeSetForAttribute(SpeedAttribute))
	{
		const float AttackSpeed = EquippedWeaponType.IsEmpty()
			? 0.0f
			: UCataclysmItemModifiers::WeaponAttackSpeedForType(
				ItemBaseTable, EquippedWeaponType);

		AbilitySystem->SetNumericAttributeBase(SpeedAttribute, AttackSpeed);

		if (AttackSpeed <= 0.0f && !EquippedWeaponType.IsEmpty())
		{
			// A weapon with no rate never swings its basic attack, and the
			// symptom is a character that fights normally with its six skills
			// and silently earns no mana on hit.
			UE_LOG(LogCataclysm, Warning,
				TEXT("The %s states no attack speed, so its basic attack will "
					 "never swing. Check its AttackSpeed column in the Item "
					 "Bases sheet of docs/All_Things_Cataclysm.xlsx."),
				*EquippedWeaponType);
		}
		else if (!EquippedWeaponType.IsEmpty())
		{
			UE_LOG(LogCataclysm, Verbose,
				TEXT("The %s swings %.2f times a second."),
				*EquippedWeaponType, AttackSpeed);
		}
	}

	// AND THE BASE CRITICAL STRIKE CHANCE, WHICH IS THE SKILL'S AND NOT THE
	// WEAPON'S. It is written here because this is the moment a weapon's six
	// skills are granted, and the moment they are taken away again. The design
	// is explicit about whose it is: its stat source table says "the skill being
	// used" supplies critical strike chance, and the sentence after it says "A
	// character has no critical strike chance in the abstract."
	//
	// NOTHING WROTE IT UNTIL ISSUE #649, so it stood at the zero it was
	// initialised to, with the comment "supplied by the skill in use" describing
	// an intention nobody had built -- the same defect as the attack speed above,
	// one attribute over. A player never critically struck, and the three
	// critical strike affixes, the two gems, the Ferocity attribute and two whole
	// passive tree branches all scaled a base of zero and were worth nothing.
	//
	// ONE NUMBER FOR SIX SKILLS, which is correct only because every skill in the
	// game takes the default. `game/Data/WeaponSkills.csv` has no column for a
	// skill to state its own. Issue #657.
	//
	// SET, NOT ADDED, and zero when holding nothing, for the same reasons the
	// damage and the rate above are. A character holding nothing swings nothing.
	const FGameplayAttribute CritAttribute =
		UCataclysmCombatAttributeSet::GetCritChanceAttribute();
	if (AbilitySystem->HasAttributeSetForAttribute(CritAttribute))
	{
		AbilitySystem->SetNumericAttributeBase(
			CritAttribute,
			EquippedWeaponType.IsEmpty() ? 0.0f : DefaultSkillCritChancePercent);
	}
}

void UCataclysmWeaponSlotsComponent::UnequipWeapon()
{
	if (UCataclysmAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
	{
		GrantedHandles.TakeFromAbilitySystem(AbilitySystem);
	}

	AvailableSkills.Reset();
	EquippedWeaponType.Reset();

	// After clearing the type, so it sets zero. A character holding nothing has
	// no weapon damage; leaving the last weapon's figure behind would mean an
	// unarmed character kept hitting for whatever they last held.
	ApplyWeaponDamage();
}
