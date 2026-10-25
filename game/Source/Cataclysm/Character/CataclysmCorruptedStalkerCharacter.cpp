// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmCorruptedStalkerCharacter.h"

#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Items/CataclysmDropRoll.h"
#include "Player/CataclysmPowerScore.h"
#include "UObject/Package.h"

namespace
{
	FCataclysmStalkerPiece StalkerPiece(ECataclysmGearSlot Slot, const TCHAR* Base, const TCHAR* First,
										const TCHAR* Second)
	{
		FCataclysmStalkerPiece Piece;
		Piece.Slot = Slot;
		Piece.Base = FName(Base);
		Piece.Affixes = { FName(First), FName(Second) };
		return Piece;
	}

	FCataclysmStalkerEntry EmptySlotFor(const TCHAR* DamageType)
	{
		FCataclysmStalkerEntry Entry;
		Entry.DamageType = FName(DamageType);
		return Entry;
	}

	/**
	 * The Demonic entry: a Ravager with a Greataxe. AUTHORED BY THE DUNGEON SESSION ON 2026-10-25 for layer 1, under
	 * the owner's delegation of unstated numbers: armour and health on the armour pieces and the rings, damage on the
	 * gloves, necklace, relic and weapon, and the thirteen passive points of the Ravager's root and Thick Hide, as the
	 * diagnostic window w54d measured. Every affix is allowed on its piece's slot; a test reads `Affixes.csv` to say so.
	 */
	FCataclysmStalkerEntry DemonicEntry()
	{
		FCataclysmStalkerEntry Entry;
		Entry.DamageType = FName(TEXT("Demonic"));
		Entry.Name = TEXT("Kaedric the Hollowed");
		Entry.ClassName = TEXT("Ravager");
		Entry.WeaponType = TEXT("Greataxe");
		Entry.Allocation.Add(FName(TEXT("Ravager_basic_spine_000")), 1);
		Entry.Allocation.Add(FName(TEXT("Ravager_basic_spine_001")), 12);

		const TCHAR* const FlatHealth = TEXT("Stat_Flat_maximum_health");
		const TCHAR* const MoreHealth = TEXT("Stat_Increased_maximum_health");
		const TCHAR* const FlatArmour = TEXT("Stat_Flat_armor");
		const TCHAR* const MoreArmour = TEXT("Stat_Increased_armor");
		const TCHAR* const FlatDamage = TEXT("Stat_Flat_damage");
		const TCHAR* const MoreDamage = TEXT("Stat_Increased_damage");
		using Slot = ECataclysmGearSlot;
		Entry.Pieces = {
			StalkerPiece(Slot::Head, TEXT("Head_Helm"), FlatHealth, MoreArmour),
			StalkerPiece(Slot::Chest, TEXT("Chest_Cuirass"), FlatHealth, MoreArmour),
			StalkerPiece(Slot::Shoulders, TEXT("Shoulders_Pauldrons"), FlatHealth, FlatArmour),
			StalkerPiece(Slot::Gloves, TEXT("Gloves_Gauntlets"), FlatDamage, MoreDamage),
			StalkerPiece(Slot::Pants, TEXT("Pants_Greaves"), FlatHealth, FlatArmour),
			StalkerPiece(Slot::Boots, TEXT("Boots_Sabatons"), FlatHealth, FlatArmour),
			StalkerPiece(Slot::Belt, TEXT("Belt_Girdle"), FlatHealth, MoreArmour),
			StalkerPiece(Slot::Necklace, TEXT("Necklace_Amulet"), FlatDamage, MoreDamage),
			StalkerPiece(Slot::Relic, TEXT("Relic_Idol"), FlatDamage, MoreDamage),
			StalkerPiece(Slot::Ring1, TEXT("Ring_Band"), FlatHealth, MoreHealth),
			StalkerPiece(Slot::Ring2, TEXT("Ring_Band"), FlatHealth, MoreHealth),
			StalkerPiece(Slot::Ring3, TEXT("Ring_Band"), FlatHealth, MoreHealth),
			StalkerPiece(Slot::Ring4, TEXT("Ring_Band"), FlatHealth, MoreHealth),
			StalkerPiece(Slot::Ring5, TEXT("Ring_Band"), FlatHealth, MoreHealth),
			StalkerPiece(Slot::Ring6, TEXT("Ring_Band"), FlatHealth, MoreHealth),
			StalkerPiece(Slot::Ring7, TEXT("Ring_Band"), FlatHealth, MoreHealth),
			StalkerPiece(Slot::Ring8, TEXT("Ring_Band"), FlatHealth, MoreHealth),
			StalkerPiece(Slot::Weapon1, TEXT("Weapon_Greataxe"), FlatDamage, MoreDamage),
		};
		return Entry;
	}
}

ACataclysmCorruptedStalkerCharacter::ACataclysmCorruptedStalkerCharacter()
{
	// THE TWO SETS A PLAYER HOLDS AND A CREATURE DOES NOT, AND THE TYPED RESISTANCES, so `ApplyTo` writes every stat
	// it writes for a player. Without them it skips the primary attributes, the class resource and every resistance
	// a piece carries, silently: it writes only attributes whose set the ability system holds.
	PrimaryAttributes = CreateDefaultSubobject<UCataclysmPrimaryAttributeSet>(TEXT("PrimaryAttributes"));
	TypedResistanceAttributes =
		CreateDefaultSubobject<UCataclysmResistanceAttributeSet>(TEXT("TypedResistanceAttributes"));
	ClassResourceAttributes = CreateDefaultSubobject<UCataclysmClassResourceAttributeSet>(TEXT("ClassResourceAttributes"));
}

TArray<FCataclysmStalkerEntry> ACataclysmCorruptedStalkerCharacter::Entries()
{
	return {
		EmptySlotFor(TEXT("War")),
		DemonicEntry(),
		EmptySlotFor(TEXT("Death")),
		EmptySlotFor(TEXT("Pestilence")),
		EmptySlotFor(TEXT("Famine")),
		EmptySlotFor(TEXT("Celestial")),
		EmptySlotFor(TEXT("Chaos")),
		EmptySlotFor(TEXT("Void")),
	};
}

TArray<FCataclysmStalkerEntry> ACataclysmCorruptedStalkerCharacter::AuthoredEntries()
{
	return Entries().FilterByPredicate([](const FCataclysmStalkerEntry& Entry) { return Entry.IsAuthored(); });
}

TArray<FCataclysmItem> ACataclysmCorruptedStalkerCharacter::PiecesAt(const FCataclysmStalkerEntry& Entry,
																	  int32 DifficultyTier, FRandomStream& Stream)
{
	const int32 AffixTier = UCataclysmDropRoll::MaxAffixTierOnADrop(DifficultyTier);
	TArray<FCataclysmItem> Items;
	for (const FCataclysmStalkerPiece& Piece : Entry.Pieces)
	{
		FCataclysmItem Item;
		Item.Base = Piece.Base;
		if (Piece.Slot == ECataclysmGearSlot::Weapon1 || Piece.Slot == ECataclysmGearSlot::Weapon2)
		{
			Item.DamageTypes = { Entry.DamageType };
		}
		for (const FName& Affix : Piece.Affixes)
		{
			FCataclysmRolledAffix Rolled;
			Rolled.Affix = Affix;
			Rolled.Tier = AffixTier;
			Rolled.Roll = Stream.FRand();
			Item.Affixes.Add(Rolled);
		}
		Items.Add(Item);
	}
	return Items;
}

bool ACataclysmCorruptedStalkerCharacter::WearTheBuild(const FCataclysmStalkerEntry& Entry, int32 DifficultyTier,
														FRandomStream& Stream)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UAbilitySystemComponent* Abilities = GetAbilitySystemComponent();
	const UDataTable* ClassTable = UCataclysmPlayerClassStats::LoadTable();
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!Abilities || !ClassTable || !NodeTable || !EffectTable || !Entry.IsAuthored())
	{
		return false;
	}

	// ITS PIECES, GATHERED THROUGH AN EQUIPMENT COMPONENT THAT BELONGS TO NO ACTOR. See the header.
	Pieces = PiecesAt(Entry, DifficultyTier, Stream);
	UCataclysmEquipmentComponent* Wardrobe = NewObject<UCataclysmEquipmentComponent>(GetTransientPackage());
	for (int32 Index = 0; Index < Pieces.Num() && Index < Entry.Pieces.Num(); ++Index)
	{
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		Wardrobe->EquipInto(Pieces[Index], Entry.Pieces[Index].Slot, Removed, AlsoRemoved);
	}
	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers = Wardrobe->GatherModifiers();
	UCataclysmPassiveTree::AccumulateInto(Modifiers, Entry.Allocation, NodeTable, EffectTable, { Entry.DamageType });
	const TMap<FName, float> Bases = Wardrobe->StatBasesFromWeapons();

	// AT THE TIER'S REFERENCE LEVEL: the design document's reference character is "the ceiling at the end of each
	// difficulty tier", which is "the peak of player power for this dungeon tier".
	const int32 Level = UCataclysmPowerScore::ReferenceCharacter(DifficultyTier).Level;
	UCataclysmPlayerClassStats::ApplyTo(Abilities, ClassTable, Entry.ClassName, Level, &Modifiers,
										ECataclysmPoolFill::FillToMaximum, &Bases);

	// THE PLACEHOLDER MULTIPLIERS, ruled by the coordinating session on 2026-10-25: a working prototype first, and the
	// simulation's fight model brings their replacements. See `CorruptedStalkerHealthMultiplier`.
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Health = Abilities->GetNumericAttribute(MaxHealth)
		* Effects::CorruptedStalkerHealthMultiplier(DifficultyTier);
	Abilities->SetNumericAttributeBase(MaxHealth, Health);
	Abilities->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
	const FGameplayAttribute AttackDamage = UCataclysmCombatAttributeSet::GetAttackDamageAttribute();
	Abilities->SetNumericAttributeBase(AttackDamage,
		Abilities->GetNumericAttribute(AttackDamage) * Effects::CorruptedStalkerDamageMultiplier);

	// AND NOTHING WRITES OVER IT FROM HERE ON. The diagnostic window w54d measured one archetype setter taking armour
	// from 416.1 to 0 and crit from 0 to 5.
	bHoldsAPlayerBuild = true;
	BasicAttack = UCataclysmWeaponSkills::BasicAttackFor(UCataclysmItemModifiers::LoadBaseTable(), Entry.WeaponType);
	WornEntryName = Entry.Name;
	return true;
}

float ACataclysmCorruptedStalkerCharacter::AttackReachCm() const
{
	return bHoldsAPlayerBuild && BasicAttack.Slot != ECataclysmAbilitySlot::None && BasicAttack.Params.RadiusCm > 0.0f
		? BasicAttack.Params.RadiusCm
		: Super::AttackReachCm();
}

void ACataclysmCorruptedStalkerCharacter::AttackTarget(AActor* Target)
{
	if (!bHoldsAPlayerBuild || BasicAttack.Slot == ECataclysmAbilitySlot::None || !Target)
	{
		Super::AttackTarget(Target);
		return;
	}

	// ITS WEAPON'S BASIC ATTACK, STARTED FROM ITS OWN PLACE, the way the enemy-cast measurement of window w53 started a
	// creature's Strike, which hurt the player it was aimed at.
	const FVector From = GetActorLocation();
	UCataclysmTriggeredSkill::Trigger(this, BasicAttack, Target->GetActorLocation(), /*DamageShare=*/1.0f,
									  /*bPaysCost=*/false, &From);
}
