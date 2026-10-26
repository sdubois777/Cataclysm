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

	// THE STAND-IN'S DEFAULT, the one damage type it held before issue #837 layer 2. See the field.
	DamageTypes.Add(TEXT("Demonic"));
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

int32 UCataclysmWeaponSlotsComponent::GetEquippedWeaponHands() const
{
	if (EquippedWeaponType.IsEmpty())
	{
		return -1;
	}

	const UDataTable* Bases = ItemBaseTable
		? ItemBaseTable.Get()
		: UCataclysmItemModifiers::LoadBaseTable();
	if (!Bases)
	{
		return -1;
	}

	// THE SAME MATCH `GetEquippedSubType` MAKES, so the two can never read
	// different rows for one weapon.
	int32 Found = -1;
	Bases->ForeachRow<FCataclysmItemBaseRow>(
		TEXT("UCataclysmWeaponSlotsComponent::GetEquippedWeaponHands"),
		[&](const FName&, const FCataclysmItemBaseRow& Row)
		{
			if (Found < 0
				&& Row.WeaponType.Equals(EquippedWeaponType, ESearchCase::IgnoreCase))
			{
				Found = Row.Hands;
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

FString UCataclysmWeaponSlotsComponent::DamageTypeOf(const AActor* Actor)
{
	if (!Actor)
	{
		return FString();
	}

	if (const UCataclysmWeaponSlotsComponent* Slots =
			Actor->FindComponentByClass<UCataclysmWeaponSlotsComponent>())
	{
		return Slots->GetDamageType();
	}

	// No weapon slots at all, which is every enemy. See the header for why
	// empty is the right answer rather than a gap. Issue #975.
	return FString();
}

TArray<FString> UCataclysmWeaponSlotsComponent::DamageTypesOf(const AActor* Actor)
{
	const UCataclysmWeaponSlotsComponent* Slots =
		Actor ? Actor->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
	return Slots ? Slots->GetDamageTypes() : TArray<FString>();
}

bool UCataclysmWeaponSlotsComponent::CarriesDamageType(const AActor* Actor, FName DamageType)
{
	if (DamageType.IsNone())
	{
		return false;
	}
	const FString Asked = DamageType.ToString();
	return DamageTypesOf(Actor).ContainsByPredicate(
		[&Asked](const FString& Carried) { return Carried.Equals(Asked, ESearchCase::IgnoreCase); });
}

TArray<FString> UCataclysmWeaponSlotsComponent::InFillOrder(const TArray<FName>& Carried, FName CreationDamageType)
{
	// THE CREATION TYPE FIRST, WHEN IT IS CARRIED (Q11), and then the rest as the worn weapons list them.
	TArray<FString> Ordered;
	const bool bCreationFirst = !CreationDamageType.IsNone() && Carried.Contains(CreationDamageType);
	if (bCreationFirst)
	{
		Ordered.Add(CreationDamageType.ToString());
	}
	for (const FName& Type : Carried)
	{
		if (!Type.IsNone() && !(bCreationFirst && Type == CreationDamageType))
		{
			Ordered.Add(Type.ToString());
		}
	}
	return Ordered;
}

int32 UCataclysmWeaponSlotsComponent::EquipWeaponType(const FString& NewWeaponType)
{
	// ONE WEAPON TYPE IS A LIST OF ONE, AND NONE IS AN EMPTY LIST: taking the weapon off, as it always was.
	TArray<FString> OneWeapon;
	if (!NewWeaponType.IsEmpty())
	{
		OneWeapon.Add(NewWeaponType);
	}
	return EquipWeaponTypes(OneWeapon);
}

int32 UCataclysmWeaponSlotsComponent::EquipWeaponTypes(const TArray<FString>& NewWeaponTypes)
{
	const int32 Filled = FillSlotsFromWeaponTypes(NewWeaponTypes);

	// ONE LINE PER EQUIP AT Log LEVEL, BECAUSE Verbose IS NOT WRITTEN TO THE
	// FILE. LogCataclysm is declared with a default verbosity of Log, so the
	// three Verbose lines inside FillSlotsFromWeaponTypes below never reach
	// game/Saved/Logs/Cataclysm.log, and before this nothing about equipping a
	// weapon appeared there at all.
	//
	// WHAT THAT COST. Issue #1221 is a play report of 2026-09-03 saying the
	// abilities were not changing correctly as weapons changed. Answering it
	// meant reading 613 skill activations out of the log and matching every
	// skill name against game/Data/WeaponSkills.csv by hand, and even then the
	// log could not say which weapon type the game believed was equipped --
	// which is the one fact the report turned on.
	//
	// IT IS NOT NOISY. An equip is an event and not a frame. This runs when
	// UCataclysmEquipmentComponent::AnnounceChange fires, which is when
	// something worn changes; the 2026-09-03 session had six weapon changes in
	// twenty-five minutes.
	//
	// THE COUNT IS "of" WHAT THE WEAPON OFFERED, NOT OF SIX. A weapon offers
	// as many skills as the matrix and its own basic attack give it, and the
	// two numbers differing is itself worth seeing: it means an ability the
	// weapon has was not granted.
	//
	// EVERY WEAPON TYPE AND EVERY DAMAGE TYPE, joined by "and", since issue #837
	// layer 2; one of each reads exactly as it did.
	const FString Weapons = FString::Join(WornWeaponTypes, TEXT(" and "));
	const FString Types = FString::Join(DamageTypes, TEXT(" and "));
	UE_LOG(LogCataclysm, Log,
		TEXT("Weapon change: %s, %s damage, %d of %d skill slots filled."),
		Weapons.IsEmpty() ? TEXT("no weapon") : *Weapons,
		Types.IsEmpty() ? TEXT("no") : *Types,
		Filled, AvailableSkills.Num());

	return Filled;
}

int32 UCataclysmWeaponSlotsComponent::FillSlotsFromWeaponTypes(
	const TArray<FString>& NewWeaponTypes)
{
	// Taken back first, and unconditionally. Refilling without emptying would
	// leave the previous weapon's abilities granted alongside the new ones, and
	// two abilities carrying the same slot tag means one key firing both.
	// COPIED FIRST, because emptying clears the list a caller may have handed back.
	const TArray<FString> Asked = NewWeaponTypes;
	UnequipWeapon();

	// EVERY WORN WEAPON TYPE, IN SLOT ORDER, and the first is the one the basic
	// attack, the sub-type and the hands are read off, as the one type was.
	for (const FString& WeaponType : Asked)
	{
		if (!WeaponType.IsEmpty())
		{
			WornWeaponTypes.Add(WeaponType);
		}
	}
	EquippedWeaponType = WornWeaponTypes.IsEmpty() ? FString() : WornWeaponTypes[0];
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
		// is what DamageTypes holds, the first of them.
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
		//
		// ONE ELEMENT, THE FIRST CARRIED TYPE, since issue #837 layer 2: the
		// creation type when it is carried. The design says the basic attack "is
		// enhanced by all damage types present"; the owner ruled on 2026-10-25 that
		// nothing should say "increased X damage", which the coordinating session
		// read as making a tag per type moot (Q9). NOT A TAG PER TYPE, because what
		// does read the tag is a skill's own scoped bonus -- Burning Wrath's more
		// damage and Conflagration's increase to allies, each scoped to its own
		// element by UCataclysmSelfBuffSkill::GrantIncrease and
		// UCataclysmAuraSkill::HelpAlliesInside -- and a tag per type
		// would let a two-type basic attack take both elements' bonuses. With one
		// type carried this is the tag it always was.
		const FString DamageType = GetDamageType();
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
		// THE POOL, AND ITS DEFAULT FILL, since issue #837 layer 2: every worn
		// weapon type with every carried damage type, and for each slot the first
		// entry designed for it. One weapon of one type is `SkillsFor` exactly.
		SkillPool = UCataclysmWeaponSkills::PoolFor(
			WeaponSkillTable, WornWeaponTypes, DamageTypes);
		AvailableSkills.Append(UCataclysmWeaponSkills::DefaultFillFrom(SkillPool));
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

	// THE ATTACK DAMAGE ATTRIBUTE IS NOT WRITTEN HERE, AND USED TO BE. This
	// component wrote it from the equipped weapon TYPE, which is how issue #840
	// happened: a second worn weapon changed nothing and an upgrade level never
	// applied. Issue #845 then found that a weapon's attack damage AFFIXES were
	// being gathered and dropped, because two things would otherwise write the
	// same attribute and the last one to run would win.
	//
	// UCataclysmPlayerClassStats::ApplyTo owns it now, and it is the only
	// writer. A weapon's damage reaches it as an ordinary flat modifier from
	// UCataclysmEquipmentComponent::GatherModifiers, alongside every affix on
	// every other piece. This component's job is which SKILLS exist.
	//
	// THE BASE CRITICAL STRIKE CHANCE HAS NOW MOVED WITH THEM TOO. Issue #894.
	// It is still the skill's rather than the character's, which is what the
	// design says, but the four affixes naming it could not scale a value this
	// component SET on every equip. It arrives as a base override from
	// UCataclysmEquipmentComponent::StatBasesFromWeapons instead, alongside the
	// swing rate, and UCataclysmPlayerClassStats::ApplyTo is the only writer of
	// all three.

	if (AvailableSkills.IsEmpty())
	{
		// A weapon its damage type does not cover AND which arms nobody, which
		// today is only the Shield. Verbose, not a warning: the design says not
		// every damage type has skills for every weapon type, and issue #619 says
		// a Shield composes no hit.
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The %s has no %s skills and no basic attack, so no slot was "
				 "filled."),
			*EquippedWeaponType, *FString::Join(DamageTypes, TEXT(" or ")));
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
			UCataclysmWeaponSkills::StampOnto(*Template, Skill);
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
			*StartingWeaponType, *FString::Join(DamageTypes, TEXT(" and ")));
	}

	return Filled;
}

void UCataclysmWeaponSlotsComponent::UnequipWeapon()
{
	if (UCataclysmAbilitySystemComponent* AbilitySystem = GetAbilitySystem())
	{
		GrantedHandles.TakeFromAbilitySystem(AbilitySystem);
	}

	AvailableSkills.Reset();
	EquippedWeaponType.Reset();
	// AND THE POOL AND THE WEAPON TYPES IT CAME FROM, issue #837 layer 2. The damage types stay: they are what the
	// worn weapons carry, which the next fill is handed, and not something a weapon coming off takes away here.
	WornWeaponTypes.Reset();
	SkillPool.Reset();

	// NO ATTRIBUTE IS WRITTEN HERE, AND THREE USED TO BE. Taking a weapon off
	// has to leave the character hitting for nothing, swinging at nothing and
	// critically striking never, rather than keeping whatever they last held.
	// All three are now the job of UCataclysmPlayerClassStats::ApplyTo, which
	// recomputes every stat from what is worn, and
	// UCataclysmEquipmentComponent::RefreshAttributes runs it whenever
	// equipment changes. Attack damage and attack speed moved in issue #845 and
	// critical strike chance in issue #894.
}
