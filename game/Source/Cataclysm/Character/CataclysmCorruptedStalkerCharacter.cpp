// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmCorruptedStalkerCharacter.h"

#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmPotions.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AIController.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
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

	// AT THE REFERENCE CHARACTER'S GEAR LEVEL, ruled by the coordinating session on 2026-10-25 (issue #1308): the
	// tier's ceiling is upgraded to `min(10, Tier + 2)`, and "rebuilt to the peak of player power" grows as a player
	// does. At gear level 0 its attack damage stopped rising at 414 from tier 6, where the affix tier stops at 7, while
	// a player's goes on rising with its upgrades: window w58 measured the plateau.
	const TArray<FCataclysmScoredGear> Reference = UCataclysmPowerScore::ReferenceCharacter(DifficultyTier).Gear;
	const int32 GearLevel = Reference.Num() > 0 ? Reference[0].Upgrade : 0;
	TArray<FCataclysmItem> Items;
	for (const FCataclysmStalkerPiece& Piece : Entry.Pieces)
	{
		FCataclysmItem Item;
		Item.Base = Piece.Base;
		Item.GearLevel = GearLevel;
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

	// THE MULTIPLIERS, a table of eight each, ruled by the coordinating session on 2026-10-26 from the simulation's fight
	// model. See `CorruptedStalkerHealthMultiplier` and `CorruptedStalkerDamageMultiplier`.
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Health = Abilities->GetNumericAttribute(MaxHealth)
		* Effects::CorruptedStalkerHealthMultiplier(DifficultyTier);
	Abilities->SetNumericAttributeBase(MaxHealth, Health);
	Abilities->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
	const FGameplayAttribute AttackDamage = UCataclysmCombatAttributeSet::GetAttackDamageAttribute();
	Abilities->SetNumericAttributeBase(AttackDamage,
		Abilities->GetNumericAttribute(AttackDamage) * Effects::CorruptedStalkerDamageMultiplier(DifficultyTier));

	// AND NOTHING WRITES OVER IT FROM HERE ON. The diagnostic window w54d measured one archetype setter taking armour
	// from 416.1 to 0 and crit from 0 to 5.
	bHoldsAPlayerBuild = true;
	BasicAttack = UCataclysmWeaponSkills::BasicAttackFor(UCataclysmItemModifiers::LoadBaseTable(), Entry.WeaponType);
	Skills = UCataclysmWeaponSkills::SkillsFor(UCataclysmWeaponSkills::LoadGeneratedTable(), Entry.WeaponType,
											   Entry.DamageType.ToString());
	WornEntryName = Entry.Name;
	return true;
}

float ACataclysmCorruptedStalkerCharacter::AttackReachCm() const
{
	return bHoldsAPlayerBuild && BasicAttack.Slot != ECataclysmAbilitySlot::None && BasicAttack.Params.RadiusCm > 0.0f
		? BasicAttack.Params.RadiusCm
		: Super::AttackReachCm();
}

TArray<ECataclysmAbilitySlot> ACataclysmCorruptedStalkerCharacter::ChoiceOrder()
{
	using Slot = ECataclysmAbilitySlot;
	return {Slot::Aura, Slot::Support, Slot::Movement, Slot::Ultimate, Slot::Heavy, Slot::Special};
}

const FCataclysmWeaponSkill* ACataclysmCorruptedStalkerCharacter::SkillIn(ECataclysmAbilitySlot Slot) const
{
	// BY ITS DESIGNED SLOT, the slot every rule reads since issue #837 layer 3; a Stalker presses no keys.
	return Skills.FindByPredicate([Slot](const FCataclysmWeaponSkill& Skill)
		{ return UCataclysmWeaponSkills::DesignedSlotOf(Skill) == Slot; });
}

bool ACataclysmCorruptedStalkerCharacter::IsRunningASkillIn(ECataclysmAbilitySlot Slot) const
{
	const UAbilitySystemComponent* Abilities = GetAbilitySystemComponent();
	if (!Abilities)
	{
		return false;
	}
	// EVERY GRANT IS ITS OWN SPEC, because `UCataclysmTriggeredSkill::Trigger` grants a new one each time, and the
	// slot is stamped on the instance it grants.
	for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
	{
		const UCataclysmGameplayAbility* Running = Cast<UCataclysmGameplayAbility>(Spec.GetPrimaryInstance());
		if (Spec.IsActive() && Running && Running->GetDesignedSlot() == Slot)
		{
			return true;
		}
	}
	return false;
}

TArray<FCataclysmEnemyAbility> ACataclysmCorruptedStalkerCharacter::EnemyAbilities() const
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	const UAbilitySystemComponent* Abilities = GetAbilitySystemComponent();
	const UDataTable* SlotTable = UCataclysmSkillSlots::LoadGeneratedTable();
	const auto Read = [Abilities](const FGameplayAttribute& Attribute)
	{
		return Abilities && Abilities->HasAttributeSetForAttribute(Attribute)
			? Abilities->GetNumericAttribute(Attribute) : 0.0f;
	};
	const float Mana = Read(Vital::GetManaAttribute());
	const float MaxMana = Read(Vital::GetMaxManaAttribute());
	const float Health = Read(Vital::GetHealthAttribute());
	const float MaxHealth = Read(Vital::GetMaxHealthAttribute());
	const float Fervour = Read(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
	constexpr float Anywhere = 1000000.0f;

	TArray<FCataclysmEnemyAbility> Choices;
	for (const ECataclysmAbilitySlot Slot : ChoiceOrder())
	{
		FCataclysmEnemyAbility Choice;
		Choice.Name = FName(*UEnum::GetValueAsString(Slot));
		Choice.WindUpSeconds = 0.0f;
		const FCataclysmWeaponSkill* Skill = SkillIn(Slot);
		const FCataclysmSkillSlotNumbers Numbers = UCataclysmSkillSlots::NumbersFor(SlotTable, Slot);

		// ITS MANA AT THE LEVEL `Trigger` GRANTS AT, which is the level a player's own grant is at too.
		const float ManaCost = Skill
			? UCataclysmSkillSlots::ManaCostAtLevel(Skill->ManaCost >= 0.0f ? Skill->ManaCost
																			  : Numbers.ManaCostAtLevel100, 1)
			: 0.0f;
		bool bMay = bHoldsAPlayerBuild && Skill && Mana >= ManaCost && !IsRunningASkillIn(Slot);
		float Nearest = 0.0f;
		float Farthest = Anywhere;
		float Cooldown = Skill ? (Skill->Cooldown >= 0.0f ? Skill->Cooldown : Numbers.Cooldown) : 0.0f;
		switch (Slot)
		{
		case ECataclysmAbilitySlot::Aura:
			bMay = bMay && MaxMana > 0.0f && Mana >= Effects::CorruptedStalkerAuraOffBelowManaShare * MaxMana;
			break;
		case ECataclysmAbilitySlot::Support:
			bMay = bMay && MaxHealth > 0.0f && Health < Effects::CorruptedStalkerSupportBelowHealthShare * MaxHealth;
			Cooldown = FMath::Max(Cooldown, Skill ? Skill->Params.Duration : 0.0f);
			break;
		case ECataclysmAbilitySlot::Movement:
			Nearest = Effects::CorruptedStalkerMovementBeyondCm;
			break;
		case ECataclysmAbilitySlot::Ultimate:
			bMay = bMay && Fervour >= Numbers.FervourCost;
			Farthest = Skill ? Skill->Params.RadiusCm : 0.0f;
			break;
		default:
			// A STRIKE REACHES ITS RADIUS, A SHOT ITS RANGE.
			Farthest = Skill ? FMath::Max(Skill->Params.RadiusCm, Skill->Params.RangeCm) : 0.0f;
			break;
		}
		Choice.MinRangeCm = bMay ? Nearest : 1.0f;
		Choice.MaxRangeCm = bMay ? Farthest : 0.0f;
		Choice.Shape = Skill ? Skill->Shape : ECataclysmSkillShape::None;
		Choice.CooldownSeconds = Skill
			? UCataclysmGameplayAbility::CooldownAfterReduction(Abilities, Cooldown, Skill->Tags) : 0.0f;
		Choices.Add(Choice);
	}
	return Choices;
}

void ACataclysmCorruptedStalkerCharacter::UseEnemyAbility(int32 Index, AActor* Target, const FVector& AimedAt)
{
	const TArray<ECataclysmAbilitySlot> Order = ChoiceOrder();
	const FCataclysmWeaponSkill* Skill = Order.IsValidIndex(Index) ? SkillIn(Order[Index]) : nullptr;
	if (!bHoldsAPlayerBuild || !Skill)
	{
		return;
	}
	const FVector From = GetActorLocation();
	const FVector Aim = Target ? Target->GetActorLocation() : AimedAt;
	UCataclysmTriggeredSkill::Trigger(this, *Skill, Aim, /*DamageShare=*/1.0f, /*bPaysCost=*/true, &From);

	// A LEAP MOVED IT AT ONCE, so the path it was following is stale; the controller chases again on its next pass.
	if (Order[Index] == ECataclysmAbilitySlot::Movement)
	{
		if (AAIController* Brain = Cast<AAIController>(GetController()))
		{
			Brain->StopMovement();
		}
	}
}

float ACataclysmCorruptedStalkerCharacter::DesignedSecondsBetweenAttacks() const
{
	return bHoldsAPlayerBuild ? UCataclysmDungeonModifierEffects::CorruptedStalkerSecondsBetweenSkillUses
							  : Super::DesignedSecondsBetweenAttacks();
}

void ACataclysmCorruptedStalkerCharacter::AfterRegenerationStep()
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	Super::AfterRegenerationStep();
	UAbilitySystemComponent* Abilities = GetAbilitySystemComponent();
	if (!bHoldsAPlayerBuild || !Abilities || UCataclysmSkillEffects::IsDead(this))
	{
		return;
	}

	// ITS AURA OFF BELOW THE RULED SHARE OF ITS MANA.
	const float MaxMana = Abilities->GetNumericAttribute(Vital::GetMaxManaAttribute());
	if (MaxMana > 0.0f
		&& Abilities->GetNumericAttribute(Vital::GetManaAttribute()) < Effects::CorruptedStalkerAuraOffBelowManaShare * MaxMana)
	{
		TArray<FGameplayAbilitySpecHandle> Auras;
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			const UCataclysmGameplayAbility* Running = Cast<UCataclysmGameplayAbility>(Spec.GetPrimaryInstance());
			if (Spec.IsActive() && Running && Running->GetDesignedSlot() == ECataclysmAbilitySlot::Aura)
			{
				Auras.Add(Spec.Handle);
			}
		}
		for (const FGameplayAbilitySpecHandle& Handle : Auras)
		{
			Abilities->CancelAbilityHandle(Handle);
		}
	}

	// A POTION BELOW THE RULED SHARE OF ITS HEALTH, THE FIRST SLOT THAT CAN POUR ONE, under the player's rules:
	// `UCataclysmPotions::Drink` refuses while a heal runs and when a slot holds too few charges.
	const float MaxHealth = Abilities->GetNumericAttribute(Vital::GetMaxHealthAttribute());
	if (MaxHealth > 0.0f
		&& Abilities->GetNumericAttribute(Vital::GetHealthAttribute()) < Effects::CorruptedStalkerPotionBelowHealthShare * MaxHealth)
	{
		for (int32 Slot = 0; Slot < UCataclysmPotions::SlotCount; ++Slot)
		{
			const ECataclysmPotionRefusal Refused = UCataclysmPotions::Drink(this, Slot);
			if (Refused == ECataclysmPotionRefusal::None || Refused == ECataclysmPotionRefusal::AHealIsRunning)
			{
				break;
			}
		}
	}

	// AND A DRINK'S HEAL RUNS ON, as the player's does from the player's own step.
	UCataclysmPotions::HealStep(this, UCataclysmRegeneration::StepSeconds);
}

int32 ACataclysmCorruptedStalkerCharacter::RarityStepItPaysAs() const
{
	return bHoldsAPlayerBuild ? FirstBossRarityStep : Super::RarityStepItPaysAs();
}

int32 ACataclysmCorruptedStalkerCharacter::RandomGearDropsGiven() const
{
	return bHoldsAPlayerBuild ? 0 : Super::RandomGearDropsGiven();
}

TArray<int32> ACataclysmCorruptedStalkerCharacter::WornPiecesThatDrop(int32 PieceCount, FRandomStream& Stream)
{
	TArray<int32> Dropped;
	for (int32 Index = 0; Index < PieceCount; ++Index)
	{
		if (Stream.FRand() < UCataclysmDungeonModifierEffects::CorruptedStalkerPieceDropChance)
		{
			Dropped.Add(Index);
		}
	}
	return Dropped;
}

void ACataclysmCorruptedStalkerCharacter::DropWhatItWore(UWorld* World, const FVector& At, FRandomStream& Stream)
{
	if (!World || !bHoldsAPlayerBuild)
	{
		return;
	}
	// THE ITEMS AS IT WORE THEM: their values were rolled at the tier being played when it was raised.
	const TArray<int32> Dropped = WornPiecesThatDrop(Pieces.Num(), Stream);
	for (int32 Each = 0; Each < Dropped.Num(); ++Each)
	{
		ACataclysmDroppedItem* Drop = UCataclysmDropSpawner::PutOnTheFloor(
			World, At + UCataclysmDropSpawner::ScatterOffset(Each, Dropped.Num()), Pieces[Dropped[Each]], NAME_None, 0);
		if (Drop)
		{
			Drop->bDroppedByARaisedCreature = bRaisedByARule;
		}
	}
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
