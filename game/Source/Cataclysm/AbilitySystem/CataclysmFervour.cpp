// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"

const TCHAR* UCataclysmFervour::FromDamageStat = TEXT("fervour_from_damage");
const TCHAR* UCataclysmFervour::FromCostStat = TEXT("fervour_from_cost");
const TCHAR* UCataclysmFervour::LostToHealingStat =
	TEXT("fervour_lost_to_healing");
const TCHAR* UCataclysmFervour::LossSuppressedStat =
	TEXT("fervour_loss_suppressed");
const TCHAR* UCataclysmFervour::PerSecondStat = TEXT("fervour_per_second");

FGameplayTag UCataclysmFervour::LeechTag()
{
	// The same refusal `RegenerationTag` makes, for the same reason: a test may
	// run before the tag table is loaded, and an empty tag matches nothing.
	return FGameplayTag::RequestGameplayTag(TEXT("Keyword.Leech"),
											/*ErrorIfNotFound=*/false);
}

FGameplayTag UCataclysmFervour::RegenerationTag()
{
	// ErrorIfNotFound IS FALSE FOR THE REASON UCataclysmDamageCalculation GIVES
	// FOR ITS OWN TAGS: a test may run before the tag table is loaded, and an
	// empty tag matches nothing, which is the right answer there.
	return FGameplayTag::RequestGameplayTag(TEXT("Keyword.Regeneration"),
											/*ErrorIfNotFound=*/false);
}

float UCataclysmFervour::FervourFor(float HealthChanged, float MaxHealth,
									float RatePerPercent)
{
	if (HealthChanged <= 0.0f || MaxHealth <= 0.0f || RatePerPercent <= 0.0f)
	{
		return 0.0f;
	}

	// THE SHARE OF THE CHARACTER'S WHOLE HEALTH, IN PERCENT, TIMES THE RATE.
	// A character with 500 health and one with 5000 fill the bar at the same
	// speed relative to how much of themselves they have spent, which is what
	// "1 per 1% of maximum health" means and is why the rate is not per point.
	return HealthChanged / MaxHealth * 100.0f * RatePerPercent;
}

float UCataclysmFervour::RateFor(const UAbilitySystemComponent* AbilitySystem,
								 FName Stat,
								 const FGameplayTagContainer& Context)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	// NO CLASS RESOURCE ATTRIBUTE SET MEANS NO FERVOUR. Only the player state
	// carries one; every enemy and every minion in the game answers here. That
	// is a guard rather than a fault, and it is why nothing below has to ask
	// whether the thing it is looking at is a player.
	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	if (!Resource)
	{
		return 0.0f;
	}

	const TMap<FString, FGameplayAttribute>& ByName = RateAttributes();
	const FGameplayAttribute* Attribute = ByName.Find(Stat.ToString());
	if (!Attribute)
	{
		return 0.0f;
	}

	const float Plain = AbilitySystem->GetNumericAttribute(*Attribute);

	// THE SAME PIPELINE THE STAT WAS WORKED OUT BY, RUN AGAIN WITH THE HIT'S
	// TAGS. Without this the Masochist's Shared Agony node -- which increases
	// Fervour gained from damage over time specifically -- would be dropped
	// before it reached anything, because the attribute holds the value worked
	// out with no tags in hand. Issue #943 is the same gap on every other stat.
	if (const UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		return Cataclysm->StatForSkill(Stat, Context, Plain);
	}

	return Plain;
}

float UCataclysmFervour::GainFromDamage(UAbilitySystemComponent* AbilitySystem,
										float HealthLost,
										const FGameplayTagContainer& HitTags)
{
	return Move(AbilitySystem, FName(FromDamageStat), HealthLost, HitTags,
				/*Sign=*/1.0f);
}

float UCataclysmFervour::GainFromHealthCost(
	UAbilitySystemComponent* AbilitySystem, float HealthSpent)
{
	// NO TAGS. A health cost is paid by the character rather than delivered by
	// a hit, so there is nothing for it to carry and no node scopes to it.
	return Move(AbilitySystem, FName(FromCostStat), HealthSpent,
				FGameplayTagContainer(), /*Sign=*/1.0f);
}

float UCataclysmFervour::RemoveForHealing(
	UAbilitySystemComponent* AbilitySystem, float HealthRestored,
	const FGameplayTagContainer& HealingTags)
{
	return Move(AbilitySystem, FName(LostToHealingStat), HealthRestored,
				HealingTags, /*Sign=*/-1.0f);
}

bool UCataclysmFervour::HasAGenerator(
	const UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem
		|| !AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		return false;
	}

	// THE ATTRIBUTES AND NOT `RateFor`, because this asks whether the character
	// has a generator at all rather than what one is worth to a particular hit.
	// A rate that only exists on damage over time still counts.
	for (const FGameplayAttribute& Attribute :
			UCataclysmClassResourceAttributeSet::GetRateAttributes())
	{
		if (AbilitySystem->GetNumericAttribute(Attribute) > 0.0f)
		{
			return true;
		}
	}
	return false;
}

const TMap<FString, FGameplayAttribute>& UCataclysmFervour::RateAttributes()
{
	// BUILT ONCE. A gameplay attribute holds a pointer to a reflected property,
	// so this cannot be a compile-time constant, and it is read on every hit.
	static const TMap<FString, FGameplayAttribute> Map = {
		{FromDamageStat,
		 UCataclysmClassResourceAttributeSet::GetFervourFromDamageAttribute()},
		{FromCostStat,
		 UCataclysmClassResourceAttributeSet::GetFervourFromCostAttribute()},
		{LostToHealingStat,
		 UCataclysmClassResourceAttributeSet::GetFervourLostToHealingAttribute()},
	};
	return Map;
}

bool UCataclysmFervour::LossIsSuppressed(
	const UAbilitySystemComponent* AbilitySystem,
	const FGameplayTagContainer& Healing)
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm
		|| !Cataclysm->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		// NO CLASS RESOURCE SET MEANS NO FERVOUR TO PROTECT, which is every
		// enemy in the game, and a component that is not this project's subclass
		// cannot be asked anything with tags in hand.
		return false;
	}

	// ASKED WITH THE HEALING'S TAGS, WHICH IS THE WHOLE MECHANISM. Issue #1006.
	// Sanguine Ledger's row requires `Keyword.Regeneration` and Wounds That
	// Feed's requires `Keyword.Leech`, so the same stat answers differently for
	// the two kinds of healing on the same character.
	//
	// A FALLBACK OF ZERO RATHER THAN THE ATTRIBUTE'S OWN VALUE. The attribute
	// holds what the flag is worth with NO healing in hand, and every row that
	// sets it requires a tag, so the attribute is zero for everybody. Passing it
	// would be passing zero the long way round, and passing anything else would
	// suppress the loss for healing that matched no row.
	return Cataclysm->StatForSkill(FName(LossSuppressedStat), Healing, 0.0f)
		> 0.0f;
}

float UCataclysmFervour::Move(UAbilitySystemComponent* AbilitySystem,
							  FName Stat, float HealthChanged,
							  const FGameplayTagContainer& Context, float Sign)
{
	if (!AbilitySystem || HealthChanged <= 0.0f)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmVitalAttributeSet* Vitals =
		AbilitySystem->GetSet<UCataclysmVitalAttributeSet>();
	if (!Resource || !Vitals)
	{
		return 0.0f;
	}

	// SOME HEALING DOES NOT REMOVE FERVOUR AT ALL. Issue #1006. Two Masochist
	// keystones say so: Sanguine Ledger for health regeneration and Wounds That
	// Feed for life leech. They set one flag, and the `Required Tags` on the row
	// decide which healing it covers -- which is why the question is asked with
	// this healing's own tags in hand rather than read off the attribute.
	//
	// ONLY THE REMOVING DIRECTION. A negative sign is Fervour leaving the pool,
	// which is healing; a positive one is damage taken or a cost paid, and no
	// node suppresses those. Asking on the way in as well would let a node meant
	// to protect the bar quietly stop it filling.
	if (Sign < 0.0f && LossIsSuppressed(AbilitySystem, Context))
	{
		return 0.0f;
	}

	const float Amount = FervourFor(
		HealthChanged, Vitals->GetMaxHealth(),
		RateFor(AbilitySystem, Stat, Context));
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED HERE RATHER THAN LEFT TO THE ATTRIBUTE SET, which is what
	// `UCataclysmRegeneration::TopUp` does with the health, mana and shield
	// pools and for the same reason. `ApplyModToAttribute` writes a base value,
	// and whether that reaches `PreAttributeChange` depends on whether an
	// aggregator exists for the attribute, which depends on whether any gameplay
	// effect happens to be modifying it. A rule that holds only sometimes is not
	// a rule, so the clamp is applied to the number before it is written.
	//
	// THAT ARGUMENT IS UNTESTED, AND THE PARAGRAPH ABOVE IS KEPT ONLY BECAUSE IT
	// MAY STILL BE RIGHT. Issue #1036. A guard proof on 2026-08-27 removed this
	// clamp, compiled, and ran every test under `Cataclysm.Fervour`: none
	// failed, including `ItStopsAtTheMaximumAndAtZero`, which goes through this
	// function. The reason is the `return` at the end -- it answers the change
	// this function can MEASURE rather than the change it asked for, so the
	// attribute set's own clamp makes the answer right with this one gone. No
	// test creates the situation the paragraph above describes, so nothing here
	// establishes that this clamp is needed.
	//
	// ONE OF THREE PLACES THAT CLAMP THE POOL, the other two being
	// `PreAttributeChange` and `PostGameplayEffectExecute` in
	// `UCataclysmClassResourceAttributeSet`. All three used to consult a flag
	// saying the pool had no maximum, granted by The Final Vow's second option
	// under issue #1029; issue #1031 rewrote all twelve Masochist capstone
	// options and that one no longer exists, so the maximum is simply the
	// maximum again.
	const float Wanted = FMath::Clamp(Before + Sign * Amount, 0.0f,
									  Resource->GetMaxClassResource());
	const float Change = Wanted - Before;
	if (FMath::IsNearlyZero(Change))
	{
		// ALREADY FULL, OR ALREADY EMPTY. Not a fault: a Masochist at maximum
		// Fervour taking another hit is the ordinary case, and it is what two of
		// the tree's keystones are about.
		return 0.0f;
	}

	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);

	// MEASURED RATHER THAN ASSUMED, so a caller reporting what it did says what
	// really happened rather than what it asked for.
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::GainPerSecondStep(
	UAbilitySystemComponent* AbilitySystem, float SecondsInStep)
{
	if (!AbilitySystem || SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to fill, which is every enemy.
		return 0.0f;
	}

	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE. Issue #1008. Low Life's row
	// carries a health condition, and a conditional bonus is never folded into
	// a gameplay attribute -- it would be stale the moment health moved -- so
	// the attribute is zero for a character holding the keystone and a plain
	// read would answer zero for ever with nothing reporting it.
	//
	// NO TAGS, because nothing is happening: this is Fervour arriving from the
	// passage of time rather than from a hit or a heal, so there is no event
	// whose tags could scope it.
	const float PerSecond = Cataclysm->StatForSkill(
		FName(PerSecondStat), FGameplayTagContainer(), 0.0f);
	if (PerSecond <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME UNTIL A POINT IS SPENT IN LOW LIFE, and
		// every character holding it that is not hurt enough for the condition.
		return 0.0f;
	}

	const float Wanted = PerSecond * SecondsInStep;

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the same rule `Move` above follows and for
	// the reason it gives: `ApplyModToAttribute` writes a base value and whether
	// that reaches `PreAttributeChange` depends on whether an aggregator happens
	// to exist for the attribute.
	const float Change =
		FMath::Clamp(Before + Wanted, 0.0f, Resource->GetMaxClassResource())
		- Before;
	if (FMath::IsNearlyZero(Change))
	{
		// A FULL BAR IS THE ORDINARY CASE for a character standing at low health
		// with this keystone, because ten a second fills it in ten seconds.
		return 0.0f;
	}

	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}
