// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmSkillLevels.h"

#include "AbilitySystem/CataclysmSkillTemplate.h"
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
	const FName Row = RowThatLevels(Ability);
	const ACataclysmPlayerState* Holder = Row.IsNone() ? nullptr : PlayerHolding(Ability);
	return Holder ? DamageFactor(Holder->SkillLevelOf(Row)) : 1.0;
}
