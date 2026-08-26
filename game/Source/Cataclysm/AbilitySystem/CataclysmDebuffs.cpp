// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDebuffs.h"

#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"

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
