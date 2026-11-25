// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmSkillLevels.h"
// What a re-price reaches: the commanded, the summon skill that made them and its reserve. Issue #2802.
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"

#include "AbilitySystem/CataclysmSkillTemplate.h"
// For the ability system an instigator acts through, to find the player a carried row belongs to. Slice 2a.
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmExperience.h"
#include "Player/CataclysmPlayerState.h"

namespace CataclysmSkillLevelsTable
{
	/**
	 * Path of Exile's "Requires Level" column, the character level each gem level asks for, read off poedb.tw's
	 * Fireball and Cleave pages on 2026-11-19: the same at every level of both. Index 0 is skill level 1.
	 */
	constexpr int32 PairedCharacterLevels[UCataclysmSkillLevels::MaxLevel] = {
		1, 2, 4, 7, 11, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 67, 70};
}

int32 UCataclysmSkillLevels::PairedCharacterLevel(int32 SkillLevel)
{
	return CataclysmSkillLevelsTable::PairedCharacterLevels[FMath::Clamp(SkillLevel, FirstLevel, MaxLevel) - 1];
}

double UCataclysmSkillLevels::ExperienceToReach(int32 SkillLevel)
{
	// LEVEL 1 IS FREE: its paired character level is 1, which costs nothing to reach.
	return PriceShareOfTheCharacters
		* static_cast<double>(UCataclysmExperience::TotalToReach(PairedCharacterLevel(SkillLevel)));
}

int32 UCataclysmSkillLevels::LevelFor(double Experience, int32 CharacterLevel)
{
	// THE HIGHEST LEVEL PAID FOR, NO HIGHER THAN THE CHARACTER'S PAIRED LEVEL ALLOWS. Path of Exile's required level is
	// the cap: a gem cannot be levelled past what the character's level allows, and its experience waits.
	int32 Level = FirstLevel;
	for (int32 Next = FirstLevel + 1; Next <= MaxLevel; ++Next)
	{
		if (PairedCharacterLevel(Next) > CharacterLevel || Experience < ExperienceToReach(Next))
		{
			break;
		}
		Level = Next;
	}
	return Level;
}

double UCataclysmSkillLevels::DamageFactor(int32 SkillLevel)
{
	return FMath::Pow(DamageFactorPerLevel, static_cast<double>(FMath::Clamp(SkillLevel, FirstLevel, MaxLevel) - 1));
}

double UCataclysmSkillLevels::CostFactor(int32 SkillLevel)
{
	return FMath::Pow(CostFactorPerLevel, static_cast<double>(FMath::Clamp(SkillLevel, FirstLevel, MaxLevel) - 1));
}

double UCataclysmSkillLevels::CostFactorOf(const UGameplayAbility* Ability)
{
	// A SKILL PRICED FOR A SCREEN, NOT GRANTED, at the level it is shown at. Issue #2804.
	if (const UCataclysmSkillTemplate* Shown = Cast<const UCataclysmSkillTemplate>(Ability);
		Shown && Shown->PreviewSkillLevel > 0)
	{
		return CostFactor(Shown->PreviewSkillLevel);
	}
	const FName Row = RowThatLevels(Ability);
	const ACataclysmPlayerState* Holder = Row.IsNone() ? nullptr : PlayerHolding(Ability);
	return Holder ? CostFactor(Holder->SkillLevelOf(Row)) : 1.0;
}

void UCataclysmSkillLevels::RepriceReservesOf(const ACataclysmPlayerState* Holder, FName SkillRow)
{
	const UAbilitySystemComponent* System = Holder ? Holder->GetAbilitySystemComponent() : nullptr;
	const AActor* Commander = Holder ? Holder->GetPawn() : nullptr;
	if (!System || !Commander || SkillRow.IsNone())
	{
		return;
	}
	const UCataclysmSummonSkill* Maker = nullptr;
	for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
	{
		const UCataclysmSummonSkill* Skill = Cast<UCataclysmSummonSkill>(Spec.GetPrimaryInstance());
		if (Skill && Skill->SkillRow == SkillRow)
		{
			Maker = Skill;
			break;
		}
	}
	// NOUGHT WHEN THE SKILL SUMMONS NOTHING, which leaves thralls and minions alone and still reaches the skill's
	// gadgets and traps below.
	const float Now = Maker ? Maker->FervourReserveOfOne() : 0.0f;
	for (AActor* Follower : UCataclysmCommand::ThingsCommandedBy(Commander))
	{
		ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Follower);
		const ACataclysmEnemyCharacter* Thrall = Cast<ACataclysmEnemyCharacter>(Follower);
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
		const bool bMadeByIt =
			(Thrall && Thrall->TakenBySkillRow == SkillRow) || (Minion && Minion->SummoningSkillRow == SkillRow);
		// A THRALL AT ITS OWN RARITY'S FIGURE, since the owner's ruling of 2026-11-24, "Reserve grows with rarity": the
		// figure for a Common would cut a Herald's 90 to 30 at the skill's next level.
		const float NowForIt = Thrall && Maker ? Maker->FervourReserveOfOne(nullptr, Thrall) : Now;
		if (Body && bMadeByIt && NowForIt > 0.0f && Body->ReservedFervour > NowForIt)
		{
			Body->ReservedFervour = NowForIt;
		}

		// A SKILL GADGET OR A SKILL TRAP STANDING FOR A SLOT THAT HOLDS THE SKILL, at what one placed now reserves.
		// A trap is asked first: a slot converted into both makes a trap (T17).
		const bool bTrap = Minion && Minion->IsSkillTrap();
		const bool bGadget = Minion && !bTrap && Minion->IsSkillGadget();
		const ECataclysmAbilitySlot Slot =
			bTrap ? Minion->SkillTrapSlot : (bGadget ? Minion->SkillGadgetSlot : ECataclysmAbilitySlot::None);
		if (Body && Slot != ECataclysmAbilitySlot::None && RowInSlot(Commander, Slot) == SkillRow)
		{
			const float Placed = bTrap ? ACataclysmMinion::SkillTrapReserveFor(Commander, Slot)
									   : ACataclysmMinion::SkillGadgetReserveFor(Commander, Slot);
			if (Placed > 0.0f && Body->ReservedFervour > Placed)
			{
				Body->ReservedFervour = Placed;
			}
		}
	}
}

FName UCataclysmSkillLevels::RowInSlot(const AActor* Player, ECataclysmAbilitySlot Slot)
{
	const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Player);
	const FGameplayTag SlotTag = CataclysmAbilitySlots::Tag(Slot);
	if (!System || !SlotTag.IsValid())
	{
		return NAME_None;
	}
	// THE SPEC CARRYING THE SLOT'S TAG, as `UCataclysmAbilitySystemComponent::AbilityInputTagPressed` finds the skill a
	// key starts. Its instance, since only a given, instantiated skill has a holder (`PlayerHolding`).
	for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(SlotTag))
		{
			return RowThatLevels(Spec.GetPrimaryInstance());
		}
	}
	return NAME_None;
}

double UCataclysmSkillLevels::CostFactorOfSlot(const AActor* Player, ECataclysmAbilitySlot Slot)
{
	const FName Row = RowInSlot(Player, Slot);
	const ACataclysmPlayerState* Holder = Row.IsNone() ? nullptr : PlayerOf(Player);
	return Holder ? CostFactor(Holder->SkillLevelOf(Row)) : 1.0;
}

float UCataclysmSkillLevels::FractionFor(double Experience, int32 CharacterLevel)
{
	const int32 Level = LevelFor(Experience, CharacterLevel);
	if (Level >= MaxLevel)
	{
		return 1.0f;
	}
	const double From = ExperienceToReach(Level);
	const double Span = ExperienceToReach(Level + 1) - From;
	return Span > 0.0 ? static_cast<float>(FMath::Clamp((Experience - From) / Span, 0.0, 1.0)) : 1.0f;
}

FString UCataclysmSkillLevels::TextFor(double Experience, int32 CharacterLevel)
{
	const int32 Level = LevelFor(Experience, CharacterLevel);
	if (Level >= MaxLevel)
	{
		return FString::Printf(TEXT("Level %d: maximum"), MaxLevel);
	}
	const double From = ExperienceToReach(Level);
	const double To = ExperienceToReach(Level + 1);
	// PAID FOR AND WAITING: the next level's experience is held, and the character's level is what stops it.
	if (Experience >= To)
	{
		return FString::Printf(TEXT("Level %d: waits for character level %d"), Level, PairedCharacterLevel(Level + 1));
	}
	// ROUNDED DOWN TO A TENTH, as the character's own bar is (`UCataclysmExperienceBar::TextFor`), so one short of
	// the next level reads 99.9% and never 100.0%.
	const double Share = (Experience - From) * 1000.0 / (To - From);
	const int64 Tenths = FMath::Clamp<int64>(static_cast<int64>(FMath::FloorToDouble(Share)), 0, 999);
	return FString::Printf(TEXT("Level %d: %lld.%lld%%"), Level, Tenths / 10, Tenths % 10);
}

const ACataclysmPlayerState* UCataclysmSkillLevels::PlayerHolding(const UGameplayAbility* Ability)
{
	// ONLY A GIVEN, INSTANTIATED ABILITY HAS A HOLDER: a default object has no actor info, and asking for it would
	// raise the engine's check. The player's ability system belongs to its player state.
	if (!Ability || !Ability->IsInstantiated())
	{
		return nullptr;
	}
	const FGameplayAbilityActorInfo* Info = Ability->GetCurrentActorInfo();
	const UAbilitySystemComponent* System = Info ? Info->AbilitySystemComponent.Get() : nullptr;
	return System ? Cast<const ACataclysmPlayerState>(System->GetOwner()) : nullptr;
}

FName UCataclysmSkillLevels::RowThatLevels(const UGameplayAbility* Ability)
{
	const UCataclysmSkillTemplate* Skill = Cast<const UCataclysmSkillTemplate>(Ability);
	if (!Skill || Skill->SkillRow.IsNone())
	{
		return NAME_None;
	}
	// THE BASIC ATTACK DOES NOT LEVEL (the owner, 2026-11-19: "No, skills only"). It is 100% of weapon damage by
	// definition, and the design's damage target and every affix are fitted to it. It carries no row today --
	// `UCataclysmWeaponSkills::BasicAttackFor` builds it from the weapon's base -- so the check above already stops
	// it; THIS ONE IS A SECOND GUARD, for a basic attack ever given a row, and no test can make it fire before then.
	if (Skill->GetDesignedSlot() == ECataclysmAbilitySlot::BasicAttack)
	{
		return NAME_None;
	}
	return PlayerHolding(Ability) ? Skill->SkillRow : NAME_None;
}

double UCataclysmSkillLevels::DamageFactorForBlow(const UGameplayAbility* Ability, const AActor* Target)
{
	// ON A CREATURE ONLY: a skill's blow on its own user, an ally or a prop keeps the stated figure, so a levelled
	// skill's self hit does not grow with it.
	if (!Cast<const ACataclysmEnemyCharacter>(Target))
	{
		return 1.0;
	}
	return LevelFactorOf(Ability);
}

double UCataclysmSkillLevels::LevelFactorOf(const UGameplayAbility* Ability)
{
	const FName Row = RowThatLevels(Ability);
	const ACataclysmPlayerState* Holder = Row.IsNone() ? nullptr : PlayerHolding(Ability);
	return Holder ? DamageFactor(Holder->SkillLevelOf(Row)) : 1.0;
}

FName UCataclysmSkillLevels::RowOfBlow(const UGameplayAbility* Ability, FName RowWithNoSkill)
{
	const FName Named = RowThatLevels(Ability);
	return Named.IsNone() ? RowWithNoSkill : Named;
}

const ACataclysmPlayerState* UCataclysmSkillLevels::PlayerOf(const AActor* Instigator)
{
	// THE PLAYER'S ABILITY SYSTEM BELONGS TO ITS PLAYER STATE, whether the pawn or the state is handed in.
	const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Instigator);
	return System ? Cast<const ACataclysmPlayerState>(System->GetOwner()) : nullptr;
}

double UCataclysmSkillLevels::DamageFactorForRow(FName Row, const ACataclysmPlayerState* Holder, const AActor* Target)
{
	// ON A CREATURE ONLY, as `DamageFactorForBlow` is.
	if (Row.IsNone() || !Holder || !Cast<const ACataclysmEnemyCharacter>(Target))
	{
		return 1.0;
	}
	return DamageFactor(Holder->SkillLevelOf(Row));
}

double UCataclysmSkillLevels::DamageFactorFor(const UGameplayAbility* Ability, FName RowWithNoSkill,
											  const AActor* Instigator, const AActor* Target)
{
	// THE SKILL OBJECT FIRST, exactly as slice 1 read it, so a blow whose skill is there keeps its figure.
	if (!RowThatLevels(Ability).IsNone())
	{
		return DamageFactorForBlow(Ability, Target);
	}
	return DamageFactorForRow(RowWithNoSkill, PlayerOf(Instigator), Target);
}
