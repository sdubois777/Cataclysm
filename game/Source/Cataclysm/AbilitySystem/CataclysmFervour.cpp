// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmTargetCandidates.h"
#include "AbilitySystemComponent.h"

const TCHAR* UCataclysmFervour::FromDamageStat = TEXT("fervour_from_damage");
const TCHAR* UCataclysmFervour::FromCostStat = TEXT("fervour_from_cost");
const TCHAR* UCataclysmFervour::LostToHealingStat =
	TEXT("fervour_lost_to_healing");
const TCHAR* UCataclysmFervour::LossSuppressedStat =
	TEXT("fervour_loss_suppressed");
const TCHAR* UCataclysmFervour::PerSecondStat = TEXT("fervour_per_second");
const TCHAR* UCataclysmFervour::PerCastStat = TEXT("fervour_per_cast");
const TCHAR* UCataclysmFervour::OnDroppingLowStat =
	TEXT("fervour_on_dropping_low");
const TCHAR* UCataclysmFervour::FromMinionsStat =
	TEXT("fervour_from_minions");
const TCHAR* UCataclysmFervour::OnMinionDeathStat =
	TEXT("fervour_on_minion_death");
const TCHAR* UCataclysmFervour::PerEnemyInReachStat =
	TEXT("fervour_per_enemy_in_reach");
const TCHAR* UCataclysmFervour::DecayPerSecondStat =
	TEXT("fervour_decay_per_second");
const TCHAR* UCataclysmFervour::DecayGraceMetresStat =
	TEXT("fervour_decay_grace_metres");
const TCHAR* UCataclysmFervour::HealthRestoredOnKillStat =
	TEXT("health_restored_on_kill");
const TCHAR* UCataclysmFervour::IncreasedDamageBoughtPerExtraEnemyHitStat =
	TEXT("increased_damage_bought_per_extra_enemy_hit");
const TCHAR* UCataclysmFervour::PerEnemyHitStat = TEXT("fervour_per_enemy_hit");
const TCHAR* UCataclysmFervour::HealthRestoredOnKillAtNoCostStat =
	TEXT("health_restored_on_kill_at_no_cost");
const TCHAR* UCataclysmFervour::OnEnemyDeathNearbyStat =
	TEXT("fervour_on_enemy_death_nearby");

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

	// AND THE RITUALIST'S RATE, WHICH IS PER MINION RATHER THAN FLAT.
	// Issue #1518: "1 per second for each minion you have".
	//
	// A SECOND STAT RATHER THAN A SECOND VALUE OF THE ONE ABOVE, so the
	// Ritualist's `Binding Sigils` node -- "+2% increased Fervour gained from
	// your minions" -- cannot reach the Masochist's Low Life keystone, which
	// grants Fervour for being hurt. One character can reach all 24 class
	// trees, so a character holding both nodes is ordinary.
	//
	// THE MINION COUNT IS NOT MULTIPLIED IN HERE. The row carries the scale
	// `minions_held`, so `StatForSkill` has already multiplied the rate by how
	// many minions the character is commanding, counted from the world at the
	// moment of this call. A Ritualist commanding nothing gets zero out of this
	// line and no special case is needed to make that true.
	//
	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, and the fallback is zero
	// for the reason `GainForCast` gives for its own: a scaled bonus is never
	// folded into a gameplay attribute -- it would be stale the moment a minion
	// was summoned or died -- so the attribute is zero even for a Ritualist
	// holding the node, and passing it would be passing zero the long way round.
	const float PerMinionPerSecond = Cataclysm->StatForSkill(
		FName(FromMinionsStat), FGameplayTagContainer(), 0.0f);

	// AND THE RAVAGER'S RATE, WHICH IS PER ENEMY STANDING NEAR RATHER THAN PER
	// MINION. Issue #1515: "1 per second for every enemy within 4 metres of
	// you". `Ravager_basic_spine_000` is its only source.
	//
	// A THIRD STAT RATHER THAN A THIRD VALUE OF EITHER ABOVE, for the reason
	// the minion rate gives for being separate from the flat one. The
	// Ravager's `Held Ground` node reads "+2% increased Fervour gained from
	// enemies near you", and a shared stat would hand that increase to Low Life
	// and to the Ritualist's minions as well. One character can reach all 24
	// class trees, so holding all three nodes is ordinary rather than exotic.
	//
	// THE BODIES ARE NOT COUNTED IN HERE, exactly as the minions are not. The
	// row carries `Scale=enemies_in_reach` with `ReachMetres=4`, so
	// `StatForSkill` has already multiplied the rate by how many hostile actors
	// stand inside that radius, counted from the world at the moment of this
	// call. A Ravager standing alone gets zero out of this line with no special
	// case written for it.
	//
	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, and the fallback is zero
	// for the same reason again: a scaled bonus is never folded into a gameplay
	// attribute -- it would be stale the moment anything moved -- so the
	// attribute reads zero even for a Ravager holding the node.
	const float PerEnemyNearPerSecond = Cataclysm->StatForSkill(
		FName(PerEnemyInReachStat), FGameplayTagContainer(), 0.0f);

	// SUMMED RATHER THAN ONE OR THE OTHER, and clamped once below. A character
	// in several trees -- hurt, holding minions and standing in a crowd -- is
	// earning from every rule at once and should receive all of them.
	const float PerSecondAltogether =
		FMath::Max(0.0f, PerSecond) + FMath::Max(0.0f, PerMinionPerSecond)
		+ FMath::Max(0.0f, PerEnemyNearPerSecond);
	if (PerSecondAltogether <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME UNTIL A POINT IS SPENT IN LOW LIFE OR IN
		// THE RITUALIST'S OR THE RAVAGER'S STARTING NODE; every character
		// holding Low Life that is not hurt enough for the condition; every
		// Ritualist commanding nothing at all; and every Ravager standing
		// alone.
		return 0.0f;
	}

	const float Wanted = PerSecondAltogether * SecondsInStep;

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

float UCataclysmFervour::DecayStep(ACataclysmCharacterBase* Character,
								  float SecondsInStep)
{
	if (!Character || SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!Cataclysm)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		Cataclysm->GetSet<UCataclysmClassResourceAttributeSet>();
	if (!Resource)
	{
		// No class resource set means no pool to drain, which is every enemy.
		return 0.0f;
	}

	// THE RATE FIRST, BECAUSE IT IS THE CHEAPEST QUESTION AND REFUSES MOST
	// CHARACTERS. Zero for everyone without `Ravager_basic_spine_000`, which is
	// every character in the game until a point is spent there, so the walk for
	// nearby bodies below is never paid for by anyone the rule does not apply
	// to. That ordering is the whole reason this is not expensive.
	const float PerSecond = Cataclysm->StatForSkill(
		FName(DecayPerSecondStat), FGameplayTagContainer(), 0.0f);
	if (PerSecond <= 0.0f)
	{
		return 0.0f;
	}

	// AND NOTHING TO DRAIN IS ALSO A REFUSAL, checked before the walk for the
	// same reason. A Ravager standing alone with an empty pool is the ordinary
	// state between fights and must not cost a search of the world every step.
	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = Cataclysm->GetNumericAttribute(Pool);
	if (Before <= 0.0f)
	{
		return 0.0f;
	}

	// THE RADIUS IS A STAT AND TWO NODES ADD TO IT. The starting node grants 4
	// and `Ravager_keystone_d_kC` No Ground Given grants 4 more, so a character
	// holding both is asking about 8 -- which is what that keystone's sentence
	// names. See `DecayGraceMetresStat`.
	const float Metres = Cataclysm->StatForSkill(
		FName(DecayGraceMetresStat), FGameplayTagContainer(), 0.0f);

	// A RADIUS OF NOTHING IS NOT A RADIUS OF EVERYTHING. A character with the
	// decay rate and no radius -- which no authored data produces, because one
	// node grants both -- would otherwise be judged out of contact always. The
	// safe direction for an unknown reading is the one that does not strengthen
	// the rule against the character.
	if (Metres > 0.0f)
	{
		if (UCataclysmTargetCandidates* Candidates =
				UCataclysmTargetCandidates::In(Character->GetWorld()))
		{
			TArray<float> Distances;
			Candidates->HostileDistancesWithinMetres(
				Character, Character->GetActorLocation(), Metres, Distances);
			if (Distances.Num() > 0)
			{
				// CONTACT HOLDS, SO THE CLOCK RESTARTS AND NOTHING DRAINS.
				Cataclysm->NoteEnemyInReach();
				return 0.0f;
			}
		}
	}

	if (!Cataclysm->OutOfContactFor(DecayGraceSeconds))
	{
		// INSIDE THE THREE SECONDS. The node grants the grace so that stepping
		// behind a pillar does not empty a bar the character spent a fight
		// filling.
		return 0.0f;
	}

	// CLAMPED BEFORE IT IS WRITTEN, the same rule `GainPerSecondStep` follows
	// and for the reason it gives: `ApplyModToAttribute` writes a base value,
	// and whether that reaches `PreAttributeChange` depends on whether an
	// aggregator happens to exist for the attribute.
	const float Change =
		FMath::Clamp(Before - PerSecond * SecondsInStep,
					 0.0f, Resource->GetMaxClassResource())
		- Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}

	Cataclysm->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return Cataclysm->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::GainForCast(UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
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

	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE. Issue #1051. The Last
	// Drop's row carries a health condition, so the attribute is zero even for
	// a character holding the option and a plain read would answer zero for
	// ever with nothing reporting it. The same move `GainPerSecondStep` above
	// makes for Low Life.
	//
	// NO TAGS. The grant is for casting a skill at all rather than for casting
	// a skill of a particular kind, so there is nothing for tags to scope.
	// The node says "every skill you cast".
	const float PerCast = Cataclysm->StatForSkill(
		FName(PerCastStat), FGameplayTagContainer(), 0.0f);
	if (PerCast <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME WITHOUT THAT CAPSTONE OPTION, and every
		// character holding it that is not hurt enough for the condition.
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the same rule every other write to the
	// pool in this file follows and for the reason they give:
	// `ApplyModToAttribute` writes a base value and whether that reaches
	// `PreAttributeChange` depends on whether an aggregator happens to exist.
	const float Change =
		FMath::Clamp(Before + PerCast, 0.0f, Resource->GetMaxClassResource())
		- Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}

	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::GainOnDroppingLow(UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
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

	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, the standing rule for
	// anything a later node might put a condition on. Issue #1069.
	//
	// AND THE FALLBACK IS THE ATTRIBUTE, WHICH IS NOT WHAT `GainForCast` ABOVE
	// PASSES. That one passes zero because The Last Drop's row carries a health
	// condition, and a conditional bonus is never folded into an attribute, so
	// the attribute is zero even for a character holding the option. Rock
	// Bottom's row carries no condition, so it IS folded in, and passing zero
	// would throw the answer away whenever no stat line has been recorded --
	// which is the ordinary case for an ability system before its first
	// refresh.
	//
	// NO TAGS. This arrives because health moved rather than because a skill of
	// a particular kind was used, so there is nothing for tags to scope.
	const float OnDropping = Cataclysm->StatForSkill(
		FName(OnDroppingLowStat), FGameplayTagContainer(),
		Resource->GetFervourOnDroppingLow());
	if (OnDropping <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the rule every other write to the pool in
	// this file follows and for the reason they give.
	const float Change =
		FMath::Clamp(Before + OnDropping, 0.0f, Resource->GetMaxClassResource())
		- Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}

	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::RestoreHealthOnKill(UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to spend, which is every enemy.
		return 0.0f;
	}

	// THE NODE FIRST, BECAUSE IT REFUSES ALMOST EVERYONE. Zero for every
	// character without `Ravager_basic_d_c1`, so nobody else touches the pool
	// or reads their health on a kill.
	const float Percent = Cataclysm->StatForSkill(
		FName(HealthRestoredOnKillStat), FGameplayTagContainer(), 0.0f);
	if (Percent <= 0.0f)
	{
		return 0.0f;
	}

	// AN UNPAYABLE COST BUYS NOTHING. See the header for why a character
	// holding less than the cost restores nothing rather than a share: the same
	// tree's `Bought With Ruin` states that rule outright.
	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Held = AbilitySystem->GetNumericAttribute(Pool);
	if (Held < KillRestoreCost)
	{
		return 0.0f;
	}

	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Maximum = AbilitySystem->GetNumericAttribute(MaxHealth);
	const float Before = AbilitySystem->GetNumericAttribute(Health);

	// NOTHING TO RESTORE IS NOTHING TO BUY. A character already at full health
	// keeps its Fervour: paying for a restoration that restores nothing is a
	// cost with no effect, and no sentence describes one.
	if (Maximum <= 0.0f || Before >= Maximum)
	{
		return 0.0f;
	}

	// PAID BEFORE THE HEALING, so a character whose healing is refused
	// elsewhere still pays for the attempt the node describes. Clamped the way
	// every write to the pool in this file is clamped, for the reason `Move`
	// gives.
	const float Spend =
		FMath::Clamp(Held - KillRestoreCost, 0.0f, Resource->GetMaxClassResource())
		- Held;
	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Spend);

	// `TopUp` IS WHAT THE LIFE LEECH HEALS WITH, so this healing obeys the same
	// ceiling and the same received-healing reductions every other restoration
	// of health does, rather than a second copy of those rules.
	UCataclysmRegeneration::TopUp(*AbilitySystem, Health, MaxHealth,
								  Maximum * Percent / 100.0f);

	return AbilitySystem->GetNumericAttribute(Health) - Before;
}

float UCataclysmFervour::RestoreHealthOnKillAtNoCost(
	UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no attribute to hold the option, which is
		// every enemy.
		return 0.0f;
	}

	// THE OPTION FIRST, BECAUSE IT REFUSES ALMOST EVERYONE. Zero for every
	// character without Long Hold, so nobody else reads their health on a kill.
	const float Percent = Cataclysm->StatForSkill(
		FName(HealthRestoredOnKillAtNoCostStat), FGameplayTagContainer(),
		Resource->GetHealthRestoredOnKillAtNoCost());
	if (Percent <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Maximum = AbilitySystem->GetNumericAttribute(MaxHealth);
	const float Before = AbilitySystem->GetNumericAttribute(Health);
	if (Maximum <= 0.0f || Before >= Maximum)
	{
		return 0.0f;
	}

	// `TopUp`, FOR THE REASON `RestoreHealthOnKill` GIVES: the same ceiling and
	// the same received-healing reductions as every other restoration.
	UCataclysmRegeneration::TopUp(*AbilitySystem, Health, MaxHealth,
								  Maximum * Percent / 100.0f);

	return AbilitySystem->GetNumericAttribute(Health) - Before;
}

float UCataclysmFervour::BuyDamageForEnemiesStruckTogether(
	UAbilitySystemComponent* AbilitySystem,
	const FGameplayTagContainer& SkillTags, int32 EnemiesStruckTogether)
{
	// ONE ENEMY OR NONE BUYS NOTHING AND COSTS NOTHING: the sentence counts the
	// enemies "beyond the first". An unknown count of -1 lands here too.
	const int32 Beyond = EnemiesStruckTogether - 1;
	if (!AbilitySystem || Beyond <= 0)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		return 0.0f;
	}

	// THROUGH THE PIPELINE WITH THE SKILL'S TAGS, fallback zero. The row requires
	// `Type.Melee`, so an attack without that tag reads zero here and neither
	// pays nor buys.
	const float PercentPerEnemy = Cataclysm->StatForSkill(
		FName(IncreasedDamageBoughtPerExtraEnemyHitStat), SkillTags, 0.0f);
	if (PercentPerEnemy <= 0.0f)
	{
		return 0.0f;
	}

	// ALL OR NOTHING FOR THE WHOLE ATTACK: "If you cannot pay, the attack still
	// hits but gains nothing." Holding less than the whole cost spends nothing.
	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Held = AbilitySystem->GetNumericAttribute(Pool);
	const float Cost = ExtraEnemyHitCost * static_cast<float>(Beyond);
	if (Held < Cost)
	{
		return 0.0f;
	}

	// PAID BEFORE ANY BLOW, and clamped the way every write to the pool in this
	// file is clamped, for the reason `Move` gives.
	const float Spend =
		FMath::Clamp(Held - Cost, 0.0f, Resource->GetMaxClassResource()) - Held;
	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Spend);

	return PercentPerEnemy * static_cast<float>(Beyond);
}

float UCataclysmFervour::GainForEnemiesHit(UAbilitySystemComponent* AbilitySystem,
										   const FGameplayTagContainer& SkillTags,
										   int32 EnemiesHit)
{
	if (!AbilitySystem || EnemiesHit <= 0)
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

	// THROUGH THE PIPELINE WITH THE SKILL'S TAGS, fallback zero, for the reason
	// every rate in this file gives: a row that later gains a condition or a
	// scope is never folded into an attribute, and a plain read would answer
	// zero for ever.
	const float PerEnemy = Cataclysm->StatForSkill(
		FName(PerEnemyHitStat), SkillTags, 0.0f);
	if (PerEnemy <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME WITHOUT THE RAVAGER'S STARTING NODE.
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the rule every write to the pool in this file
	// follows; `GainForCast` gives the reason.
	const float Change =
		FMath::Clamp(Before + PerEnemy * static_cast<float>(EnemiesHit), 0.0f,
					 Resource->GetMaxClassResource())
		- Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}

	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::GainOnMinionDeath(UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to fill. That is every enemy, and
		// it is also every minion -- which matters here, because the caller has
		// a dying minion in hand and must pass its COMMANDER'S ability system.
		// Passing the minion's own lands on this line.
		return 0.0f;
	}

	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, the standing rule for
	// anything a later node might put a condition on. Issue #1518.
	//
	// AND THE FALLBACK IS THE ATTRIBUTE, WHICH IS WHAT `GainOnDroppingLow`
	// PASSES AND NOT WHAT THE PER-SECOND RATE ABOVE DOES. This node's row
	// carries no condition and no scale, so it IS folded into the attribute,
	// and passing zero would throw the answer away whenever no stat line has
	// been recorded -- the ordinary case for an ability system before its first
	// refresh.
	//
	// NO TAGS. A minion dying is not a skill of a particular kind, so there is
	// nothing for tags to scope.
	const float OnDeath = Cataclysm->StatForSkill(
		FName(OnMinionDeathStat), FGameplayTagContainer(),
		Resource->GetFervourOnMinionDeath());
	if (OnDeath <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME UNTIL A POINT IS SPENT IN THE RITUALIST'S
		// STARTING NODE.
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the rule every other write to the pool in
	// this file follows and for the reason they give.
	const float Change =
		FMath::Clamp(Before + OnDeath, 0.0f, Resource->GetMaxClassResource())
		- Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}

	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::GainOnEnemyDeathNearby(UAbilitySystemComponent* AbilitySystem,
												float MetresAway)
{
	// TOO FAR, OR NOT KNOWN, GRANTS NOTHING. At the radius or inside it counts.
	if (!AbilitySystem || MetresAway < 0.0f
		|| MetresAway > EnemyDeathNearbyRadiusMetres)
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

	// WITH THE ATTRIBUTE AS THE FALLBACK, for the reason `GainOnMinionDeath`
	// gives. NO TAGS: a death nearby is not a skill of any kind.
	const float OnDeath = Cataclysm->StatForSkill(
		FName(OnEnemyDeathNearbyStat), FGameplayTagContainer(),
		Resource->GetFervourOnEnemyDeathNearby());
	if (OnDeath <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME WITHOUT FED BY THE FALLEN.
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the rule every write to the pool in this
	// file follows.
	const float Change =
		FMath::Clamp(Before + OnDeath, 0.0f, Resource->GetMaxClassResource())
		- Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}

	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}
