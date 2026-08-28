// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDebuffs.h"

// For asking a character what a stat is worth with its own state in hand,
// rather than reading the attribute. Issue #1033.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the attribute the duration stat is folded into. Issue #1033.
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"

const TCHAR* UCataclysmDebuffs::DurationStat = TEXT("debuff_duration_taken");

const TCHAR* const UCataclysmDebuffs::DebuffRootNames[] = {
	// EVERY DAMAGE OVER TIME, AS ONE BRANCH. Bleed, Burn, Disease, Generic,
	// Necrosis, Poison and Void Splinter all hang off this, so naming the parent
	// covers the ones that exist and the ones the design has not built yet.
	TEXT("Keyword.DoT"),

	// AND BEING STUNNED, WHICH IS NOT DAMAGE AND IS STILL A DEBUFF. All three
	// games in the genre count it: Diablo 4 lists Stun among its eleven crowd
	// control effects and Path of Exile lists stunning among its debuffs.
	//
	// `State.StunImmune` IS DELIBERATELY NOT HERE. It arrives with the stun,
	// from the same call, and it protects the character rather than harming it.
	// See the header.
	TEXT("State.Stunned"),
};

const int32 UCataclysmDebuffs::DebuffRootCount = UE_ARRAY_COUNT(DebuffRootNames);

FGameplayTagContainer UCataclysmDebuffs::DebuffRoots()
{
	FGameplayTagContainer Roots;
	for (int32 Index = 0; Index < DebuffRootCount; ++Index)
	{
		// ASKED FOR BY NAME AND NOT REQUIRED TO EXIST, the same way every other
		// tag in this module is fetched. A vocabulary that has lost one leaves
		// it out of the container instead of failing to start the game, and the
		// count is then short by that branch rather than wrong about everything.
		const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(DebuffRootNames[Index]), /*ErrorIfNotFound=*/false);
		if (Tag.IsValid())
		{
			Roots.AddTag(Tag);
		}
	}
	return Roots;
}

int32 UCataclysmDebuffs::CountOn(const UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0;
	}

	const FGameplayTagContainer Roots = DebuffRoots();
	if (Roots.IsEmpty())
	{
		return 0;
	}

	// WHAT WAS REALLY APPLIED, NOT WHAT THE CHARACTER ANSWERS YES TO. See the
	// header: the engine counts a tag against its parents as well, so a single
	// bleed makes `HasMatchingGameplayTag` true for both `Keyword.DoT.Bleed` and
	// `Keyword.DoT`. This list holds one entry per effect.
	FGameplayTagContainer Owned;
	AbilitySystem->GetOwnedGameplayTags(Owned);

	int32 Count = 0;
	for (const FGameplayTag& Tag : Owned)
	{
		// MATCHES THE ROOT ITSELF AND ANYTHING UNDER IT. `MatchesAny` asks
		// whether this tag or one of its parents is in the container, which is
		// the direction wanted here: `Keyword.DoT.Bleed` matches the root
		// `Keyword.DoT`, and `Keyword.Leech` matches nothing.
		if (Tag.MatchesAny(Roots))
		{
			++Count;
		}
	}

	return Count;
}

int32 UCataclysmDebuffs::CountOnActor(const AActor* Actor)
{
	return CountOn(UCataclysmTargeting::AbilitySystemOf(Actor));
}

FGameplayTag UCataclysmDebuffs::BleedTag()
{
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Bleed")), /*ErrorIfNotFound=*/false);
}

bool UCataclysmDebuffs::IsBleeding(const UAbilitySystemComponent* AbilitySystem)
{
	const FGameplayTag Bleed = BleedTag();
	return AbilitySystem && Bleed.IsValid()
		&& AbilitySystem->HasMatchingGameplayTag(Bleed);
}

float UCataclysmDebuffs::DurationOn(const UAbilitySystemComponent* Defender,
								   float DurationSeconds)
{
	const FGameplayAttribute Attribute =
		UCataclysmCombatAttributeSet::GetDebuffDurationTakenAttribute();

	if (DurationSeconds <= 0.0f || !Defender
		|| !Defender->HasAttributeSetForAttribute(Attribute))
	{
		// NO COMBAT SET MEANS THE DURATION IS UNCHANGED, which is the ordinary
		// answer rather than a fault: a target that cannot hold the stat is not
		// a target that halves everything put on it.
		return DurationSeconds;
	}

	// ASKED FOR RATHER THAN READ, so a future row carrying a condition works.
	// Neither of the two rows today carries one, so the attribute holds the
	// same answer and both routes agree.
	//
	// NO SKILL TAGS. This is the DEFENDER'S stat, and the skill in the
	// attacker's hand is the wrong question to scope it by -- the same reason
	// the retaliation and damage-taken readings pass none either.
	const UCataclysmAbilitySystemComponent* Asking =
		Cast<const UCataclysmAbilitySystemComponent>(Defender);
	const float Held = Defender->GetNumericAttribute(Attribute);
	const float Percent = Asking
		? Asking->StatForSkill(FName(DurationStat), FGameplayTagContainer(),
							   Held)
		: Held;

	// FLOORED AT NOTHING RATHER THAN AT THE NORMAL DURATION. A stat of zero is
	// a legitimate future rule -- an effect that ends at once -- and a negative
	// one is not, because a duration below nothing would make
	// `ApplyTagForDuration` refuse the effect outright and read as immunity.
	// Nothing today reduces this stat: both rows raise it.
	return DurationSeconds * FMath::Max(0.0f, Percent) / NormalDuration;
}
