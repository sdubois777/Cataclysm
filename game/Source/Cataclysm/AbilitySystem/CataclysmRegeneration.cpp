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
const TCHAR* UCataclysmRegeneration::HealingReceivedStat = TEXT("healing_received");
const TCHAR* UCataclysmRegeneration::HealingSkillHealthRestoredStat =
	TEXT("healing_skill_health_restored");
const TCHAR* UCataclysmRegeneration::HealingSkillHealthAsEnergyShieldStat =
	TEXT("healing_skill_health_as_energy_shield");
const TCHAR* UCataclysmRegeneration::ManaRegenStat = TEXT("mana_regen");
const TCHAR* UCataclysmRegeneration::EnergyShieldRegenStat =
	TEXT("energy_shield_regen");
const TCHAR* UCataclysmRegeneration::ShieldRechargesWhileDamagedStat =
	TEXT("shield_recharges_while_damaged");
const TCHAR* UCataclysmRegeneration::ShieldRechargeHasNoDelayStat =
	TEXT("shield_recharge_has_no_delay");
const TCHAR* UCataclysmRegeneration::ManaRegenRestoresShieldStat =
	TEXT("mana_regen_restores_shield");
const TCHAR* UCataclysmRegeneration::EnergyShieldRechargeCeilingReductionStat =
	TEXT("energy_shield_recharge_ceiling_reduction");

float UCataclysmRegeneration::HealthHealingCeiling(const UAbilitySystemComponent& AbilitySystem,
												  float CeilingShare)
{
	float Ceiling = AbilitySystem.GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute())
		* FMath::Clamp(CeilingShare, 0.0f, 1.0f);

	// THE CEILING: "You cannot be healed above 50% of your maximum health",
	// and the enchantment rows that say the same with other shares.
	const float Reduction = FMath::Clamp(
		AbilitySystem.GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealingCeilingReductionAttribute()),
		0.0f, 100.0f);
	Ceiling *= (100.0f - Reduction) / 100.0f;

	// AND WHAT IS RESERVED STAYS UNFILLED. Issue #1833.
	if (const UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<const UCataclysmAbilitySystemComponent>(&AbilitySystem))
	{
		Ceiling = FMath::Min(Ceiling, Cataclysm->UnreservedMaximumHealth());
	}
	return FMath::Max(0.0f, Ceiling);
}

void UCataclysmRegeneration::TopUp(UAbilitySystemComponent& AbilitySystem,
								   const FGameplayAttribute& Pool,
								   const FGameplayAttribute& Maximum,
								   float Gain,
								   const FGameplayTagContainer& Healing,
								   float CeilingShare)
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
	// THE FIST'S LIVING PYRE COMES THROUGH HERE TOO, since issue #1608, and this
	// never refuses it: The Whole Weight is a Greatsword Ultimate and Living Pyre
	// is a Fist Ultimate, and a character holds one weapon, so the two can never
	// be up at once.
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
		// A ROW MAY RAISE OR LOWER WHAT IS OFFERED FIRST, AS ITS OWN MULTIPLIER.
		// Ruled 2026-10-06: "You gain 10% more life from all sources". See
		// `HealingReceivedStat`. The amount offered is the base the row's
		// buckets apply to, and a character with no such row is offered what it
		// was. Never below nothing: a heal does not take health away.
		if (const UCataclysmAbilitySystemComponent* Asking =
				Cast<const UCataclysmAbilitySystemComponent>(&AbilitySystem))
		{
			Gain = FMath::Max(0.0f, Asking->StatAppliedTo(FName(HealingReceivedStat),
														  FGameplayTagContainer(), Gain));
		}

		// AND WHAT RIDES ON THE AILMENTS IT CARRIES, added and capped with it.
		// Issue #1833, ruled 2026-10-06: "Disease effects reduce enemy healing by
		// 50%-100%".
		const float AmountReduction = FMath::Clamp(
			AbilitySystem.GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealingReceivedReductionAttribute())
				+ UCataclysmAbilitySystemComponent::AilmentRiderPercentOn(
					  &AbilitySystem, ECataclysmAilmentRider::HealingReceived),
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

	// AND A CALLER MAY ALLOW ONLY A SHARE OF THAT. Issue #1833, ruled
	// 2026-09-30: "Your energy shield cannot recharge above 50% of its
	// maximum" is the shield's regeneration step passing a half. A pool
	// already above the share is not drained; it simply gains nothing.
	Ceiling *= FMath::Clamp(CeilingShare, 0.0f, 1.0f);

	// AND A CHARACTER MAY BE FORBIDDEN TO BE HEALED ALL THE WAY UP. Issue
	// #988. The Masochist's Point of No Return keystone reads "You cannot be
	// healed above 50% of your maximum health, but you deal 25% more damage."
	//
	// HERE RATHER THAN AT EACH CALLER, because this is the one place health
	// regeneration and life leech both restore health, and the node says
	// "cannot be healed" rather than naming one of them.
	// The answer itself is `HealthHealingCeiling`, which Wrung Out also asks
	// before it charges for a heal. Issue #1607.
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
		Ceiling = HealthHealingCeiling(AbilitySystem, CeilingShare);
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
	//
	// BUT WHAT HAD NOWHERE TO GO IS OVERHEAL, AND A ROW MAY KEEP IT. Ruled
	// 2026-10-07: "Overheal converts to a temporary shield absorbing up to
	// 10%-20% of your max HP". See `UCataclysmAbilitySystemComponent::NoteOverheal`.
	//
	// HERE, BEFORE THE RETURN BELOW, because a heal on a character already at
	// its ceiling is all overheal and that return is where it leaves.
	//
	// IN POINTS OF HEALTH: what is offered at this line, which is after the
	// row that raises a heal and after whatever reduces healing received,
	// less what fits under the ceiling. THE CEILING, NOT THE MAXIMUM: a heal
	// stopped by a healing ceiling or by reserved health overflows there.
	//
	// HEALTH ONLY. Mana and the energy shield come through this function too,
	// and the sentence is about healing.
	//
	// A HEAL REFUSED ABOVE, by a held swing or by a full reduction, never
	// reaches this line and leaves no overheal: nothing was offered.
	//
	// AND NOT THE CHARACTER'S OWN REGENERATION. Ruled 2026-10-07: "overheal
	// converts" is a heal the character received beyond what fitted.
	// Regeneration comes through here every step, at full health too, so
	// counting it would make the row a second shield that refills by itself,
	// which the sentence does not promise. Regeneration is known by the tag
	// `ApplyStep` hands over with it, `Keyword.Regeneration`; leech carries
	// `Keyword.Leech` and still counts.
	const FGameplayTag OwnRegeneration = UCataclysmFervour::RegenerationTag();
	const bool bIsOwnRegeneration = OwnRegeneration.IsValid() && Healing.HasTag(OwnRegeneration);
	if (Pool == UCataclysmVitalAttributeSet::GetHealthAttribute() && !bIsOwnRegeneration)
	{
		if (UCataclysmAbilitySystemComponent* Overhealed =
				Cast<UCataclysmAbilitySystemComponent>(&AbilitySystem))
		{
			const float Room = Ceiling > 0.0f ? FMath::Max(0.0f, Ceiling - Current) : 0.0f;
			Overhealed->NoteOverheal(Gain - FMath::Min(Gain, Room));
		}
	}

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

float UCataclysmRegeneration::HealingSkillAmount(const UAbilitySystemComponent& AbilitySystem,
												 float Amount)
{
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}

	// THE AMOUNT IS THE FIGURE THE ROWS APPLY TO, as `TopUp` hands
	// `HealingReceivedStat` the amount it was offered. So the stat needs no
	// base, and a character with no row is handed its amount back.
	//
	// NO TAGS. Both callers are a healing skill paying its health, so nothing
	// is left for a tag to choose between.
	const UCataclysmAbilitySystemComponent* Asking =
		Cast<const UCataclysmAbilitySystemComponent>(&AbilitySystem);
	if (!Asking)
	{
		return Amount;
	}
	return FMath::Max(0.0f, Asking->StatAppliedTo(FName(HealingSkillHealthRestoredStat),
												  FGameplayTagContainer(), Amount));
}

float UCataclysmRegeneration::GiveHealingSkillShield(UAbilitySystemComponent& AbilitySystem,
													 float HealthArrived)
{
	const UCataclysmAbilitySystemComponent* Asking =
		Cast<const UCataclysmAbilitySystemComponent>(&AbilitySystem);
	if (HealthArrived <= 0.0f || !Asking
		|| !AbilitySystem.GetSet<UCataclysmVitalAttributeSet>())
	{
		return 0.0f;
	}

	// A PERCENTAGE ON A BASE OF NOUGHT, so a character with no row answers
	// nought and is given nothing. Never below nought: the row gives shield
	// and takes none away.
	const float Percent = FMath::Max(
		0.0f, Asking->StatAppliedTo(FName(HealingSkillHealthAsEnergyShieldStat),
									FGameplayTagContainer(), 0.0f));
	if (Percent <= 0.0f)
	{
		return 0.0f;
	}

	// THROUGH `TopUp`, so the shield stops at the maximum its clamp and its bar
	// use, and a character with no energy shield at all is given nothing. What
	// was added is read off the shield, because `TopUp` does not return it.
	const FGameplayAttribute Shield = UCataclysmVitalAttributeSet::GetEnergyShieldAttribute();
	const float ShieldBefore = AbilitySystem.GetNumericAttribute(Shield);
	TopUp(AbilitySystem, Shield, UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(),
		  HealthArrived * Percent / 100.0f);
	return FMath::Max(0.0f, AbilitySystem.GetNumericAttribute(Shield) - ShieldBefore);
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

		// AND A RESERVATION THAT GREW TAKES ITS SHARE. Issue #1833. "Each minion
		// reserves 100-500 hp" grows with a count no event announces, like the
		// live maximum above, so every step asks. Here rather than inside
		// `RefreshLiveMaximumHealth`, which runs only for a character whose
		// maximum moves with its state and would miss one whose reservation does.
		Cataclysm->HoldHealthToUnreserved();
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

	// THE RATE IN TWO PARTS: what the character regenerates anywhere, and the
	// extra its own pyre adds. With a scale of 2 the extra is the rate once
	// more, so the two parts add up to the doubled rate this step always paid.
	// With a scale of 1, which is every character not standing in its own
	// Blood Pyre, the extra is nought.
	//
	// A CHARACTER WITH NO HEALTH REGENERATION HAS NO EXTRA EITHER. The pyre
	// multiplies a rate, and nought multiplied is nought. A rate below nought is
	// treated as none, as `GainPerStep` treats it.
	const float HealthRate =
		RateOf(HealthRegenStat, UCataclysmVitalAttributeSet::GetHealthRegenAttribute());
	const float ExtraRateFromOwnGround = FMath::Max(0.0f, HealthRate) * (FromOwnGround - 1.0f);

	// AND A ROW MAY MAKE A HEALING SKILL RESTORE MORE. Ruled 2026-10-09:
	// "Healing skills restore 30%-60% more HP", and Blood Pyre is a healing
	// skill. THE EXTRA ONLY: what the pyre adds is the health the skill
	// restores, and the character's base regeneration is not the skill's. With
	// a scale of 2 and 50% more, the whole rate is `rate * (1 + (2 - 1) * 1.5)`.
	// The stat is asked of this character, who is the patch's owner:
	// `RegenerationScaleFor` counts only patches this character left.
	const float ExtraRateWithRows = ExtraRateFromOwnGround > 0.0f
		? HealingSkillAmount(*AbilitySystem, ExtraRateFromOwnGround)
		: 0.0f;
	const float BaseGain = GainPerStep(HealthRate, SecondsInStep);
	const float ExtraGain = GainPerStep(ExtraRateWithRows, SecondsInStep);

	const float HealthBeforeStep = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	TopUp(*AbilitySystem, UCataclysmVitalAttributeSet::GetHealthAttribute(),
		  UCataclysmVitalAttributeSet::GetMaxHealthAttribute(),
		  GainPerStep(HealthRate + ExtraRateWithRows, SecondsInStep),
		  Regeneration);

	// AND A SHARE OF THE HEALTH THE PYRE RESTORED ARRIVES AS ENERGY SHIELD.
	// Ruled 2026-10-09: "Healing skills also restore 10%-20% of the healed
	// amount as energy shield".
	//
	// OF WHAT ARRIVED, read off health, because `TopUp` does not return it. A
	// character at full health gained nothing in this step and is given no
	// shield.
	//
	// ONE `TopUp` PAID THE BASE AND THE EXTRA TOGETHER, so the extra's part of
	// what arrived is worked out: the same share of what arrived as the extra
	// was of what was offered. A labelled judgement of 2026-10-09. Every rule
	// `TopUp` applies between the offer and the arrival treats the offer as one
	// amount, so none of them says which part it cut.
	if (ExtraGain > 0.0f)
	{
		const float HealthArrived = AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute()) - HealthBeforeStep;
		if (HealthArrived > 0.0f)
		{
			GiveHealingSkillShield(*AbilitySystem,
								   HealthArrived * ExtraGain / (BaseGain + ExtraGain));
		}
	}

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

	// AND ONLY UP TO THE RECHARGE CEILING: what is left of 100 after the
	// reduction the character carries. Issue #1833, ruled 2026-09-30: "Your
	// energy shield cannot recharge above 50% of its maximum" is
	// `energy_shield_recharge_ceiling_reduction` flat 50, the shape the healing
	// ceiling has (`healing_ceiling_reduction`). Regeneration only, a labelled
	// judgement of the same ruling: "recharge" is not leech or any other
	// restoration.
	const UCataclysmAbilitySystemComponent* Ceilinged =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	const float RechargeCeilingShare = Ceilinged
		? 1.0f - FMath::Clamp(Ceilinged->StatAppliedTo(
								  FName(EnergyShieldRechargeCeilingReductionStat),
								  FGameplayTagContainer(), 0.0f),
							  0.0f, 100.0f) / 100.0f
		: 1.0f;
	// AND WHETHER THIS STEP FILLS IT. Issue #1833 group E part 2: "When your
	// energy shield fully recharges, release a nova". Below its maximum before
	// the step and at it after; a ceiling below the maximum means the shield
	// never fully recharges, a labelled judgement of 2026-10-02. A recharge only
	// runs after the wait with no damage taken, so one fill is one event.
	const float ShieldMaximum = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute());
	const float ShieldBefore = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
	TopUp(*AbilitySystem,
		  UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(),
		  UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(),
		  GainPerStep(ShieldRate, SecondsInStep), FGameplayTagContainer(),
		  RechargeCeilingShare);
	if (ShieldMaximum > 0.0f && ShieldBefore < ShieldMaximum
		&& AbilitySystem->GetNumericAttribute(
			   UCataclysmVitalAttributeSet::GetEnergyShieldAttribute()) >= ShieldMaximum)
	{
		if (UCataclysmAbilitySystemComponent* Recharged =
				Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
		{
			Recharged->NoteEnergyShieldRecharged();
		}
	}

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
