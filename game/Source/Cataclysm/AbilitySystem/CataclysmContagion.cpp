// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmContagion.h"

// For asking a character what a stat is worth with its own state in hand,
// rather than reading the attribute, and for the aura's interval.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the list of debuffs a character is carrying.
#include "AbilitySystem/CataclysmDebuffs.h"
// For the effect table lookup and the two functions that apply a lasting effect.
#include "AbilitySystem/CataclysmSkillEffects.h"
// For finding the enemies standing in the radius, and for the ability system of
// an actor.
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "Engine/World.h"

const TCHAR* UCataclysmContagion::AuraDurationStat =
	TEXT("aura_debuff_duration");

const TCHAR* UCataclysmContagion::TormentChanceStat =
	TEXT("debuff_spread_chance");

bool UCataclysmContagion::SpreadOne(AActor* Instigator, AActor* Target,
									const FGameplayTag& EffectTag,
									float ExtraDurationPercent)
{
	if (!Instigator || !Target || !EffectTag.IsValid())
	{
		return false;
	}

	// WHAT THIS TAG IS, IN THE DESIGN'S OWN TABLE. A tag naming no row is
	// skipped rather than guessed at, which is what makes a stun stay put: see
	// the header.
	const FName RowName =
		UCataclysmSkillEffects::StatusEffectRowForTag(EffectTag);
	if (RowName.IsNone())
	{
		return false;
	}

	const FString RowString = RowName.ToString();
	const FCataclysmStatusEffectNumbers Numbers =
		UCataclysmSkillEffects::StatusEffectNumbers(*RowString, *RowString);

	// A ROW WITH NO DURATION IS AN EFFECT NOBODY WROTE. 41 of the 52 rows state
	// neither a duration nor an amount, and putting one of those on an enemy
	// would be a debuff that ends the instant it lands.
	if (Numbers.DurationSeconds <= 0.0f)
	{
		return false;
	}

	// AND THE NODE'S OWN BONUS ON TOP. Beacon of Despair lengthens what its aura
	// applies by 4% a point; Contagious Torment says nothing about duration and
	// passes zero. A negative would be a shortening no node asks for, so it is
	// clamped rather than trusted.
	const float Multiplier =
		1.0f + FMath::Max(ExtraDurationPercent, 0.0f) / 100.0f;
	const float Duration = Numbers.DurationSeconds * Multiplier;

	// WHICH APPLIER, DECIDED BY WHAT THE ROW STATES RATHER THAN BY WHICH TAG
	// BRANCH IT CAME FROM. See the header: six rows state an amount and are
	// damage over time; five state a duration and no amount, and are a debuff
	// whose magnitude this project has no hook for yet.
	if (Numbers.bUsable)
	{
		// bScalesWithInstigator LEFT TRUE, so the spreader's own damage over
		// time stats decide what each tick deals. This is the Masochist's own
		// effect and not a blow dealt in somebody else's name.
		return UCataclysmSkillEffects::ApplyDamageOverTime(
			Instigator, Target, Numbers.FlatDamagePerTick, Duration, EffectTag);
	}

	return UCataclysmSkillEffects::ApplyTagForDuration(Instigator, Target,
													   EffectTag, Duration);
}

FGameplayTag UCataclysmContagion::PickSpreadable(
	const UAbilitySystemComponent* Carrier, int32 PinnedIndex)
{
	const FGameplayTagContainer Carried = UCataclysmDebuffs::TagsOn(Carrier);
	if (Carried.IsEmpty())
	{
		return FGameplayTag();
	}

	// THE ONES THAT COULD ACTUALLY LAND, GATHERED BEFORE THE ROLL. Rolling first
	// and discarding afterwards would make a bleeding and stunned character
	// spread nothing half the time, which the sentence does not say.
	TArray<FGameplayTag> Candidates;
	for (const FGameplayTag& Tag : Carried)
	{
		if (!UCataclysmSkillEffects::StatusEffectRowForTag(Tag).IsNone())
		{
			Candidates.Add(Tag);
		}
	}

	if (Candidates.IsEmpty())
	{
		return FGameplayTag();
	}

	// PINNED FOR A TEST, ROLLED OTHERWISE. An index outside the list is treated
	// as unpinned rather than clamped, so a test naming a candidate that is not
	// there gets a random answer it can notice instead of a neighbour's.
	const int32 Index = (PinnedIndex >= 0 && PinnedIndex < Candidates.Num())
		? PinnedIndex
		: FMath::RandRange(0, Candidates.Num() - 1);

	return Candidates[Index];
}

int32 UCataclysmContagion::AuraStep(AActor* Character, int32 PinnedIndex)
{
	// NOTHING FOR THE DEAD, for the reason `UCataclysmNova::Step` gives: a
	// creature is destroyed on the step after it dies, so there is a real window
	// in which a corpse is still standing there with an ability system.
	if (UCataclysmSkillEffects::IsDead(Character))
	{
		return 0;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!AbilitySystem)
	{
		return 0;
	}

	// ASKED BEFORE THE CLOCK IS, for the reason the nova gives: a character with
	// no point in the node answers zero here and never touches the timestamp,
	// which is every character in the game.
	//
	// NO SKILL TAGS. Nothing is being cast; this happens because time passed.
	//
	// A FALLBACK OF ZERO RATHER THAN OF THE ATTRIBUTE, which is also the right
	// answer for an ability system that has never had a stat line applied.
	const float ExtraDurationPercent = AbilitySystem->StatForSkill(
		FName(AuraDurationStat), FGameplayTagContainer(), 0.0f);
	if (ExtraDurationPercent <= 0.0f)
	{
		return 0;
	}

	if (!AbilitySystem->MayApplyAura())
	{
		// APPLIED LESS THAN THREE SECONDS AGO. This step runs several times a
		// second, so this is the usual answer for a character holding the node.
		return 0;
	}

	// ONE DEBUFF FOR THE WHOLE PULSE. See the header: "applies a random debuff to
	// enemies within 6 metres" makes the debuff a property of the pulse.
	const FGameplayTag Spreading = PickSpreadable(AbilitySystem, PinnedIndex);

	int32 Applied = 0;
	if (Spreading.IsValid())
	{
		// THE CHARACTER'S OWN LOCATION AND ITS OWN SIDE.
		// `FindEnemiesInSphere` decides who counts as an enemy from the actor
		// passed to it, refuses scenery and the dead, and never returns that
		// actor itself.
		for (AActor* Enemy : UCataclysmTargeting::FindEnemiesInSphere(
				 Character->GetWorld(), Character, Character->GetActorLocation(),
				 RadiusMetres * CentimetresPerMetre))
		{
			if (SpreadOne(Character, Enemy, Spreading, ExtraDurationPercent))
			{
				++Applied;
			}
		}
	}

	// AND THE INTERVAL RESTARTS WHETHER OR NOT ANYTHING CAUGHT ANYTHING, which
	// is the same rule the nova keeps: "every 3 seconds" does not make the pulse
	// depend on there being a target, and it must not depend on the character
	// happening to be carrying something either. A character that pulsed while
	// carrying nothing and then caught fire a moment later would otherwise
	// spread instantly rather than on the next beat.
	AbilitySystem->NoteAuraApplied(AuraIntervalSeconds);

	return Applied;
}

int32 UCataclysmContagion::SpreadOnDebuffDamage(AActor* Character,
												float PinnedRoll,
												int32 PinnedIndex)
{
	if (UCataclysmSkillEffects::IsDead(Character))
	{
		return 0;
	}

	const UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!AbilitySystem)
	{
		return 0;
	}

	// ASKED FIRST AND CHEAPLY. Every damage over time tick in the game reaches
	// this, on every character, so a character without the node has to cost one
	// stat read and nothing else -- no search for enemies and no tag walk.
	const float ChancePercent = AbilitySystem->StatForSkill(
		FName(TormentChanceStat), FGameplayTagContainer(), 0.0f);
	if (ChancePercent <= 0.0f)
	{
		return 0;
	}

	int32 Applied = 0;
	for (AActor* Enemy : UCataclysmTargeting::FindEnemiesInSphere(
			 Character->GetWorld(), Character, Character->GetActorLocation(),
			 RadiusMetres * CentimetresPerMetre))
	{
		// ONE ROLL PER ENEMY, because the sentence makes the chance a property
		// of the enemy: "enemies within 6 metres have a 1% chance per point".
		const float Roll = PinnedRoll >= 0.0f
			? PinnedRoll
			: FMath::FRandRange(0.0f, 100.0f);
		if (Roll >= ChancePercent)
		{
			continue;
		}

		// AND A FRESH DEBUFF FOR EACH ONE THAT CATCHES, for the same reason. Two
		// enemies can catch two different things from one tick.
		//
		// NO EXTRA DURATION. This node's sentence says nothing about how long
		// what it spreads lasts, so it spreads the row's own duration. Only
		// Beacon of Despair lengthens what it applies.
		const FGameplayTag Spreading = PickSpreadable(AbilitySystem, PinnedIndex);
		if (Spreading.IsValid()
			&& SpreadOne(Character, Enemy, Spreading, 0.0f))
		{
			++Applied;
		}
	}

	return Applied;
}
