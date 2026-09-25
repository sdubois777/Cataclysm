// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmRegeneration.h"
// For asking the pipeline what a rate is worth, rather than reading the
// gameplay attribute it was folded into. Issue #1038.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
// For emptying Fervour when health comes back. Issue #954.
#include "AbilitySystem/CataclysmFervour.h"
// For a patch of burning ground that heals whoever left it faster while they
// stand in it. Blood Pyre. Issue #1162.
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
// For a swing drawn back whose row says its caster cannot be healed. The
// Greatsword's The Whole Weight. Issue #1162.
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"

const TCHAR* UCataclysmRegeneration::HealthRegenStat = TEXT("health_regen");
const TCHAR* UCataclysmRegeneration::ManaRegenStat = TEXT("mana_regen");
const TCHAR* UCataclysmRegeneration::EnergyShieldRegenStat =
	TEXT("energy_shield_regen");
const TCHAR* UCataclysmRegeneration::ShieldRechargesWhileDamagedStat =
	TEXT("shield_recharges_while_damaged");
const TCHAR* UCataclysmRegeneration::ShieldRechargeHasNoDelayStat =
	TEXT("shield_recharge_has_no_delay");
const TCHAR* UCataclysmRegeneration::ManaRegenRestoresShieldStat =
	TEXT("mana_regen_restores_shield");

void UCataclysmRegeneration::TopUp(UAbilitySystemComponent& AbilitySystem,
								   const FGameplayAttribute& Pool,
								   const FGameplayAttribute& Maximum,
								   float Gain,
								   const FGameplayTagContainer& Healing)
{
	if (Gain <= 0.0f)
	{
		return;
	}

	// A CHARACTER WITH A SWING DRAWN BACK MAY BE UNABLE TO BE HEALED. The
	// Greatsword's The Whole Weight: "you cannot move, act or be healed",
	// written as `HoldForbids=Acting, Healing`. Issue #1162.
	//
	// HERE BECAUSE THIS IS THE ONE PLACE HEALTH REGENERATION AND LIFE LEECH BOTH
	// RESTORE HEALTH, which is the same argument the healing ceiling below makes
	// for sitting here rather than at each caller.
	//
	// EVERY POOL, NOT ONLY HEALTH, and that is the row's own wording read
	// plainly: "you cannot be healed" says nothing comes back while the swing is
	// up. A held Ultimate lasts three seconds.
	//
	// THE FIST'S LIVING PYRE RETURNS HEALTH BY ANOTHER ROUTE AND IS NOT REACHED
	// BY THIS, which costs nothing: The Whole Weight is a Greatsword Ultimate and
	// Living Pyre is a Fist Ultimate, and a character holds one weapon, so the
	// two can never be up at once.
	if (UCataclysmStrikeSkill::AHeldSwingForbids(AbilitySystem.GetOwnerActor(),
												 TEXT("Healing")))
	{
		return;
	}

	// AND A CURSE MAY CUT HOW MUCH OF EACH AMOUNT ARRIVES. Issue #41, slice 5.
	// The dungeon modifier Death's Embrace: "Players periodically gain stacks of
	// a debuff called Embrace of Death, which reduces healing received."
	//
	// ON THE GAIN AND NOT ON THE CEILING, which is the whole difference between
	// this stat and the ceiling reduction below, and the two are a hazard to each
	// other. This cuts how much of each amount ARRIVES; that caps how HIGH the
	// amounts may take a character. Someone at half health under a fifty per cent
	// reduction is healed half as fast and may still reach full.
	//
	// REGENERATION AND LEECH ALIKE, because both arrive here and the project
	// owner ruled on 2026-09-12 that healing received covers everything that
	// restores health. So an enchantment row reading "healing effects on you are
	// reduced" reduces regeneration too; that its words do not say so is issue
	// #1609.
	//
	// HEALTH ONLY, which is the ruling rather than the shape of the code: mana
	// and the energy shield come through this same function. Reducing THEIR
	// rates needs nothing here -- a Less multiplier on `mana_regen` reaches it
	// through the stat pipeline, the way Starvation already works on
	// `max_health`.
	//
	// BEFORE THE CEILING BELOW, so the two compose in the order the player reads
	// them: this much was offered, this much arrives, and it may not take you
	// past there. Neither touches the other's variable, so the order changes no
	// number -- it is written this way to be read.
	//
	// A FULL HUNDRED RETURNS RATHER THAN APPLYING NOTHING. Fervour is what would
	// notice: it is emptied by the health that came back, so a zero would raise a
	// healing event that healed nobody.
	if (Pool == UCataclysmVitalAttributeSet::GetHealthAttribute())
	{
		const float AmountReduction = FMath::Clamp(
			AbilitySystem.GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealingReceivedReductionAttribute()),
			0.0f, 100.0f);
		Gain *= (100.0f - AmountReduction) / 100.0f;
		if (Gain <= 0.0f)
		{
			return;
		}
	}

	const float Current = AbilitySystem.GetNumericAttribute(Pool);
	float Ceiling = AbilitySystem.GetNumericAttribute(Maximum);

	// A SHIELD REFILLS TO THE MAXIMUM ITS CLAMP AND ITS BAR USE. Issue #1515,
	// found while building Shared Blood: until 2026-09-24 this read the
	// attribute, which holds the unscaled figure, so a shield a scaled row
	// raised -- Hollow Crown's, issue #1973 -- showed a maximum refill could
	// never reach. `MaximumEnergyShieldAsked` is the figure the clamp in
	// `UCataclysmVitalAttributeSet::PreAttributeChange` and the bar both read.
	if (Pool == UCataclysmVitalAttributeSet::GetEnergyShieldAttribute())
	{
		if (const UCataclysmVitalAttributeSet* Vitals =
				AbilitySystem.GetSet<UCataclysmVitalAttributeSet>())
		{
			Ceiling = Vitals->MaximumEnergyShieldAsked();
		}
	}

	// AND A CHARACTER MAY BE FORBIDDEN TO BE HEALED ALL THE WAY UP. Issue
	// #988. The Masochist's Point of No Return keystone reads "You cannot be
	// healed above 50% of your maximum health, but you deal 25% more damage."
	//
	// HERE RATHER THAN AT EACH CALLER, because this is the one place health
	// regeneration and life leech both restore health, and the node says
	// "cannot be healed" rather than naming one of them.
	//
	// HEALTH ONLY. The node says health, and mana and the energy shield come
	// through this same function.
	//
	// A RESPAWN IS NOT HEALING AND IS NOT CAPPED.
	// `ACataclysmPlayerCharacter::Revive` writes health back with
	// `SetNumericAttributeBase` rather than through here, so it is untouched.
	// That is the right answer -- a respawn is a new life -- and issue #956 is
	// the open question about what else that direct write should do.
	//
	// THE STAT IS A REDUCTION OF THE CEILING, so zero leaves the ceiling where
	// it was and no character without the node is changed by a single number.
	if (Pool == UCataclysmVitalAttributeSet::GetHealthAttribute())
	{
		const float Reduction = FMath::Clamp(
			AbilitySystem.GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealingCeilingReductionAttribute()),
			0.0f, 100.0f);
		Ceiling *= (100.0f - Reduction) / 100.0f;
	}

	// A POOL WITH NO MAXIMUM IS NOT A POOL. A class with no energy shield is a
	// design position rather than an error state, and its shield maximum is
	// zero; adding to it would be adding to something that does not exist, and
	// the clamp would throw the value away anyway.
	//
	// IT NOW CATCHES A SECOND CASE, and both want the same answer. Issue #988. A
	// health ceiling reduced by the full hundred per cent leaves a ceiling of
	// zero, and a character already at or above a reduced ceiling is simply not
	// healed. Neither is a fault, and neither loses anything: what would have
	// been restored had nowhere to go.
	if (Ceiling <= 0.0f || Current >= Ceiling)
	{
		return;
	}

	const float Restored = FMath::Min(Gain, Ceiling - Current);
	AbilitySystem.ApplyModToAttribute(Pool, EGameplayModOp::Additive, Restored);

	// AND HEALTH COMING BACK EMPTIES FERVOUR. Issue #954. The Masochist's
	// starting node states it: "healing removes Fervour at the same rate, 1 per
	// 1% of maximum health restored, so your health regeneration is what empties
	// it rather than a timer".
	//
	// HEALTH ONLY. Mana and the energy shield have nothing to do with it, and
	// the design's rule names health.
	//
	// WHAT FIT, NOT WHAT WAS OFFERED. Healing that overflowed a full health bar
	// restored nothing, so it removes nothing. Without that a Masochist standing
	// at full health would have its bar drained by a regeneration rate that was
	// putting nothing anywhere.
	if (Pool == UCataclysmVitalAttributeSet::GetHealthAttribute())
	{
		UCataclysmFervour::RemoveForHealing(&AbilitySystem, Restored, Healing);
	}
}

float UCataclysmRegeneration::GainPerStep(float RatePerSecond,
										  float SecondsInStep)
{
	if (RatePerSecond <= 0.0f || SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	return RatePerSecond * SecondsInStep;
}

bool UCataclysmRegeneration::ShieldMayRefill(float SecondsSinceLastDamage)
{
	return SecondsSinceLastDamage >= ShieldRefillDelaySeconds;
}

void UCataclysmRegeneration::ApplyStep(AActor* Character, float SecondsInStep,
									   float SecondsSinceLastDamage)
{
	if (!Character || SecondsInStep <= 0.0f)
	{
		return;
	}

	// A CORPSE DOES NOT HEAL. An enemy is destroyed on the tick AFTER it dies,
	// because ACataclysmEnemyCharacter::HandleDeath runs inside the gameplay
	// effect callback that dealt the killing blow, so there is a real window in
	// which a dead creature is still standing there with an ability system.
	if (UCataclysmSkillEffects::IsDead(Character))
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Character);
	if (!AbilitySystem
		|| !AbilitySystem->GetSet<UCataclysmVitalAttributeSet>())
	{
		return;
	}

	// THE LIVE MAXIMUM FIRST, so the pool below fills to it. Issue #1815: "Each
	// active minion reduces your maximum HP by 3%-6%" moves with a count no event
	// announces, so every step asks. Only a character with such a row pays more
	// than one look at its own `max_health` line.
	if (UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		if (Cataclysm->MaximumHealthMovesWithState())
		{
			Cataclysm->RefreshLiveMaximumHealth();
		}
	}

	// THE HEALTH STEP SAYS IT IS REGENERATION, and the other two do not bother
	// because Fervour reads health alone. The Masochist's Staunch node reduces
	// "the Fervour removed by your own health regeneration" specifically, which
	// is narrower than healing, so the source has to travel with the amount.
	// Leech pays through the same function and carries nothing, which is what
	// keeps that node off it. Issue #954.
	FGameplayTagContainer Regeneration;
	Regeneration.AddTag(UCataclysmFervour::RegenerationTag());

	// EACH RATE IS ASKED FOR RATHER THAN READ OFF ITS ATTRIBUTE. Issue #1038.
	// A bonus carrying a condition or a scale is never folded into a gameplay
	// attribute -- it would be stale the moment the character's state moved --
	// so reading the attribute answered the base rate for ever and nothing
	// reported it. The Masochist's Stigmatic capstone option, "each debuff on
	// you grants 4% increased health regeneration", is the first node to need
	// this and would have done nothing without it.
	//
	// THE ATTRIBUTE IS THE FALLBACK, NOT ZERO, and that difference matters.
	// `UCataclysmFervour::GainPerSecondStep` passes zero because nothing else
	// supplies its stat; all three rates here have a real base from the class
	// stat line, and `StatForSkill` returns the fallback whenever the pipeline
	// recorded nothing for the stat -- which is every enemy and every player
	// before its first refresh. Passing zero would delete their regeneration.
	//
	// NO TAGS, because nothing is happening. This is a pool coming back on its
	// own rather than a skill being used, so there is no event whose tags could
	// scope a modifier.
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	const auto RateOf = [&](const TCHAR* Stat,
							const FGameplayAttribute& Attribute) -> float
	{
		const float Stored = AbilitySystem->GetNumericAttribute(Attribute);
		return Cataclysm ? Cataclysm->StatForSkill(FName(Stat),
												   FGameplayTagContainer(),
												   Stored)
						 : Stored;
	};

	// AND STANDING IN A PATCH OF GROUND YOU LEFT MAY HEAL YOU FASTER. The Fist's
	// Blood Pyre: "standing in your own pyre does you no harm and DOUBLES YOUR
	// HEALTH REGENERATION." One in the ordinary case, which is every character
	// in the game not standing in its own Blood Pyre. Issue #1162.
	//
	// ON THE RATE AND NOT ON THE GAIN, so it composes with everything else that
	// touches the rate rather than being applied after them. The two give the
	// same number today; they would not once a second scale existed.
	//
	// HEALTH ONLY. The row says "health regeneration", and mana and the energy
	// shield below are deliberately untouched.
	const float FromOwnGround =
		ACataclysmGroundZone::RegenerationScaleFor(Character);

	TopUp(*AbilitySystem, UCataclysmVitalAttributeSet::GetHealthAttribute(),
		  UCataclysmVitalAttributeSet::GetMaxHealthAttribute(),
		  GainPerStep(
			  RateOf(HealthRegenStat,
					 UCataclysmVitalAttributeSet::GetHealthRegenAttribute())
				  * FromOwnGround,
			  SecondsInStep),
		  Regeneration);

	// THE MANA RATE IS KEPT, BECAUSE A KEYSTONE BELOW READS IT. Issue #1515.
	// The Long Game puts half of it into the energy shield, and asking for it a
	// second time down there would run the whole stat pipeline twice for one
	// number.
	const float ManaRate =
		RateOf(ManaRegenStat,
			   UCataclysmVitalAttributeSet::GetManaRegenAttribute());

	TopUp(*AbilitySystem, UCataclysmVitalAttributeSet::GetManaAttribute(),
		  UCataclysmVitalAttributeSet::GetMaxManaAttribute(),
		  GainPerStep(ManaRate, SecondsInStep));

	// WHETHER THIS CHARACTER HOLDS EITHER OF THE TWO KEYSTONES THAT CHANGE HOW
	// THE SHIELD RECHARGES. Issue #1515. Both are flags and both read zero for
	// every character that has not bought the node, so everything below behaves
	// exactly as it did for all of them.
	const auto HoldsFlag = [&](const TCHAR* Stat,
							   const FGameplayAttribute& Attribute) -> bool
	{
		// THE ATTRIBUTE-SET CHECK IS NOT OPTIONAL. Reading an attribute whose
		// set the component does not hold raises an engine ensure rather than
		// answering zero, and a creature carries no combat set at all.
		if (!Cataclysm || !Cataclysm->HasAttributeSetForAttribute(Attribute))
		{
			return false;
		}
		return Cataclysm->StatForSkill(FName(Stat), FGameplayTagContainer(),
									   AbilitySystem->GetNumericAttribute(
										   Attribute)) > 0.0f;
	};

	// THE SHIELD WAITS AND THE OTHER TWO DO NOT. Three seconds since the
	// character last took damage, restarted by taking damage again inside the
	// window, and damage over time restarts it as well. All three rules are
	// stated in the Energy Shield section of docs/Cataclysm_GDD_v2.md.
	//
	// ABLATIVE SUPPLIES A RATE INSIDE THAT WAIT RATHER THAN SHORTENING IT.
	// `Ritualist_keystone_c_kB`: "Your Energy Shield recharges while you are
	// taking damage, at half its usual rate." A shorter wait would recharge at
	// the FULL rate sooner, which is a different and stronger thing than the row
	// says. So the wait is untouched, `ShieldMayRefill` still answers the
	// question it always answered -- five assertions call it directly -- and
	// this scale decides what the character gets inside the window.
	//
	// ZERO IN THE DEFAULT BRANCH IS EXACTLY TODAY'S BEHAVIOUR, so the old `if`
	// is subsumed rather than removed.
	//
	// AND NO WAIT AT ALL IS THE FULL RATE INSIDE IT. Issue #1833, the small
	// engine halves: "Energy shield regeneration begins immediately after
	// taking damage with no delay" is the stronger thing Ablative is not, so it
	// is asked first and wins over Ablative's half.
	const float RechargeScale =
		(ShieldMayRefill(SecondsSinceLastDamage)
		 || HoldsFlag(ShieldRechargeHasNoDelayStat,
					  UCataclysmCombatAttributeSet::GetShieldRechargeHasNoDelayAttribute()))
			? 1.0f
			: (HoldsFlag(ShieldRechargesWhileDamagedStat,
						 UCataclysmCombatAttributeSet::GetShieldRechargesWhileDamagedAttribute())
				   ? AblativeRechargeFraction
				   : 0.0f);

	// AND THE LONG GAME ADDS A SECOND SOURCE, OUTSIDE THE WAIT ENTIRELY.
	// `Ritualist_keystone_d_kA`: "Your Mana Regeneration also restores your
	// Energy Shield, at half its rate." Mana regeneration is itself ungated, and
	// the row makes mana regeneration the thing that acts.
	//
	// INSIDE THE WAIT IT WOULD ONLY ADD RATE AT MOMENTS THE SHIELD IS ALREADY
	// RECHARGING, which is "increased Energy Shield Regeneration" -- and that is
	// `Ritualist_basic_spine_008` Warded Mind, a BASIC node in the same tree. A
	// keystone that duplicates a basic node beside it is not a keystone.
	// `docs/DECISIONS.md` carries that ruling and this sentence.
	const float FromMana =
		HoldsFlag(ManaRegenRestoresShieldStat,
				  UCataclysmCombatAttributeSet::GetManaRegenRestoresShieldAttribute())
			? ManaRate * ManaRegenToShieldFraction
			: 0.0f;

	const float ShieldRate =
		RateOf(EnergyShieldRegenStat,
			   UCataclysmVitalAttributeSet::GetEnergyShieldRegenAttribute())
			* RechargeScale
		+ FromMana;

	TopUp(*AbilitySystem,
		  UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(),
		  UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(),
		  GainPerStep(ShieldRate, SecondsInStep));

	// AND THE RATE IS KEPT, for the minions sharing this shield. Issue #1515,
	// Shared Blood: their shields refill at the same share of their maximum.
	if (UCataclysmAbilitySystemComponent* Keeps =
			Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		Keeps->NoteShieldRechargeRate(ShieldRate);
	}

	SharedBloodStep(Character, SecondsInStep, /*bFill=*/false);
}

const TCHAR* UCataclysmRegeneration::SharedBloodStat =
	TEXT("minion_energy_shield_percent_of_yours");

void UCataclysmRegeneration::SharedBloodStep(AActor* Character, float SecondsInStep,
											  bool bFill)
{
	const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Character);
	if (!Minion)
	{
		return;
	}
	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Minion->Summoner));
	UAbilitySystemComponent* Mine = UCataclysmTargeting::AbilitySystemOf(Character);
	if (!Theirs || !Mine || !Mine->GetSet<UCataclysmVitalAttributeSet>())
	{
		return;
	}

	const float Percent =
		Theirs->StatForSkill(FName(SharedBloodStat), FGameplayTagContainer(), 0.0f);
	if (Percent <= 0.0f)
	{
		return;
	}

	const float TheirMaximum = Theirs->MaximumEnergyShield();
	const float Maximum = FMath::Max(0.0f, TheirMaximum * Percent / 100.0f);
	Mine->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), Maximum);

	const FGameplayAttribute Shield = UCataclysmVitalAttributeSet::GetEnergyShieldAttribute();
	if (bFill || Mine->GetNumericAttribute(Shield) > Maximum)
	{
		Mine->SetNumericAttributeBase(Shield, Maximum);
		return;
	}

	const float TheirRate = Theirs->LastShieldRechargeRate();
	if (TheirRate <= 0.0f || TheirMaximum <= 0.0f || SecondsInStep <= 0.0f)
	{
		return;
	}
	TopUp(*Mine, Shield, UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(),
		  Maximum * (TheirRate / TheirMaximum) * SecondsInStep);
}
