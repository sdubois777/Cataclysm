// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmLeech.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

const TCHAR* UCataclysmRetaliation::AmountStat = TEXT("retaliation");
const TCHAR* UCataclysmRetaliation::RadiusMetresStat =
	TEXT("retaliation_radius_metres");
const TCHAR* UCataclysmRetaliation::LeechesStat = TEXT("retaliation_leeches");
const TCHAR* UCataclysmRetaliation::CannotBeMitigatedStat =
	TEXT("retaliation_cannot_be_mitigated");
const TCHAR* UCataclysmRetaliation::PenetrationStat = TEXT("retaliation_penetration");
const TCHAR* UCataclysmRetaliation::BlockedDamageAddedStat = TEXT("blocked_damage_added_to_next_retaliation");
const TCHAR* UCataclysmRetaliation::BigHitShareStat = TEXT("retaliation_for_big_hits");

float UCataclysmRetaliation::TakenMultiplierOf(const AActor* Target)
{
	const UCataclysmAbilitySystemComponent* Taking =
		Cast<const UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Target));
	return Taking ? 1.0f + Taking->RetaliationTakenPercentNow() / 100.0f : 1.0f;
}

namespace
{
	/**
	 * How much health one actor has right now, or zero if it can hold none.
	 *
	 * NAMED FOR WHAT IT IS FOR RATHER THAN FOR WHAT IT DOES, because the Unreal
	 * unity build concatenates these translation units: a private helper called
	 * `HealthOf` in two files compiles until both are clean and then collides.
	 * That has cost a build cycle here before.
	 */
	float HealthOfRetaliationTarget(const AActor* Actor)
	{
		const UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		const FGameplayAttribute Health =
			UCataclysmVitalAttributeSet::GetHealthAttribute();
		if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Health))
		{
			return 0.0f;
		}
		return AbilitySystem->GetNumericAttribute(Health);
	}

	/**
	 * One of this character's stats, judged against its state right now.
	 *
	 * THE ATTRIBUTE IS THE FALLBACK, so an ability system this project did not
	 * make, and one that has not had a character stat line applied to it yet,
	 * both answer with exactly what the attribute holds.
	 */
	float StatOfRetaliator(const UAbilitySystemComponent* Defender,
						   const TCHAR* Stat, const FGameplayAttribute& Attribute)
	{
		if (!Defender || !Defender->HasAttributeSetForAttribute(Attribute))
		{
			return 0.0f;
		}

		const float Held = Defender->GetNumericAttribute(Attribute);
		const UCataclysmAbilitySystemComponent* Asking =
			Cast<const UCataclysmAbilitySystemComponent>(Defender);

		// NO SKILL TAGS. See the header: retaliation is not a skill and carries
		// none of its own.
		return Asking ? Asking->StatForSkill(FName(Stat), FGameplayTagContainer(),
											 Held)
					  : Held;
	}
}

float UCataclysmRetaliation::AmountFor(const UAbilitySystemComponent* Defender,
									   float DamageTaken, float PercentInPlaceOfTheStat)
{
	if (DamageTaken <= 0.0f)
	{
		// A BLOW WORTH NOTHING SENDS NOTHING BACK. The caller only reaches
		// here when something got through, so this is a guard rather than an
		// ordinary case, and it keeps the share below from being taken of a
		// negative number.
		return 0.0f;
	}

	// A REFLECTED HIT STATES ITS OWN SHARE, the whole of it, and does not read the defender's stat.
	float Percent = PercentInPlaceOfTheStat >= 0.0f ? PercentInPlaceOfTheStat : StatOfRetaliator(
		Defender, AmountStat,
		UCataclysmCombatAttributeSet::GetRetaliationAttribute());

	// AND ABSORB AND RETURN'S BONUS, ADDED TO THE SHARE, for a payment answering a big hit (V3a, 2026-11-25). Not for a
	// reflected hit, which states its own share.
	const UCataclysmAbilitySystemComponent* Paying = Cast<const UCataclysmAbilitySystemComponent>(Defender);
	if (PercentInPlaceOfTheStat < 0.0f && Paying && Paying->IsPayingForABigHit())
	{
		Percent += Paying->StatForSkill(FName(BigHitShareStat), FGameplayTagContainer(), 0.0f);
	}
	if (Percent <= 0.0f)
	{
		return 0.0f;
	}

	// AND IT COUNTS AS DAMAGE. See the header: the share is what the stat
	// says, and then every increase the character carries applies to it the
	// way it applies to an ordinary attack. NO SKILL TAGS, for the reason
	// StatOfRetaliator gives: retaliation is not a skill and carries none.
	return UCataclysmSkillEffects::ModifiedDamage(
		Defender, DamageTaken * Percent / 100.0f, FGameplayTagContainer());
}

float UCataclysmRetaliation::RadiusMetresFor(
	const UAbilitySystemComponent* Defender)
{
	// FLOORED AT ZERO. Nothing states a negative radius, and a negative one
	// would be handed to a sphere search that cannot use it. `PreAttributeChange`
	// on the combat set already floors the attribute; this floors what a
	// conditional row could otherwise produce.
	return FMath::Max(
		0.0f,
		StatOfRetaliator(
			Defender, RadiusMetresStat,
			UCataclysmCombatAttributeSet::GetRetaliationRadiusMetresAttribute()));
}

bool UCataclysmRetaliation::LeechesFor(const UAbilitySystemComponent* Defender)
{
	// ANY VALUE ABOVE ZERO IS YES, which is how every other flag stat in the
	// project is read. Two sources would sum to 2 and mean the same thing.
	return StatOfRetaliator(
			   Defender, LeechesStat,
			   UCataclysmCombatAttributeSet::GetRetaliationLeechesAttribute())
		> 0.0f;
}

bool UCataclysmRetaliation::CannotBeMitigatedFor(
	const UAbilitySystemComponent* Defender)
{
	// ANY VALUE ABOVE ZERO IS YES, as `LeechesFor` above reads its flag.
	//
	// ASKED OF THE STAT LINE AND NOT THROUGH `StatOfRetaliator`, because this
	// stat has no gameplay attribute to fall back on: the fallback is nought.
	// NO SKILL TAGS, for the reason that helper gives.
	const UCataclysmAbilitySystemComponent* Asking =
		Cast<const UCataclysmAbilitySystemComponent>(Defender);
	return Asking
		&& Asking->StatForSkill(FName(CannotBeMitigatedStat), FGameplayTagContainer(), 0.0f) > 0.0f;
}

TArray<AActor*> UCataclysmRetaliation::TargetsOf(
	const UAbilitySystemComponent* Defender, AActor* Attacker)
{
	TArray<AActor*> Targets;

	// FIRST AND ALWAYS, whether or not it is inside the radius. See the header.
	if (Attacker)
	{
		Targets.Add(Attacker);
	}

	const float RadiusMetres = RadiusMetresFor(Defender);
	if (RadiusMetres <= 0.0f)
	{
		// WHICH IS EVERY CHARACTER IN THE GAME WITHOUT REPRISAL WAVE, so the
		// search is skipped rather than run with a radius of nothing.
		return Targets;
	}

	// THE AVATAR AND NOT THE OWNER. `GetOwningActor` on an attribute set answers
	// with the ability system's owner, and for the player that is the player
	// state, which is not in the world and has no location to search around.
	// The same distinction is why the hit effect and the death path both reach
	// for the avatar; issues #562 and #565.
	AActor* Avatar = Defender ? Defender->GetAvatarActor() : nullptr;
	if (!Avatar)
	{
		return Targets;
	}

	// AND THE AVATAR IS ALSO WHOSE SIDE DECIDES WHO IS AN ENEMY, which is what
	// keeps the wave off the character's own summons and off anything else on
	// its side. `FindEnemiesInSphere` also refuses scenery, refuses the dead,
	// and never returns the actor passed to it.
	const TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
		Avatar->GetWorld(), Avatar, Avatar->GetActorLocation(),
		RadiusMetres * CentimetresPerMetre);

	for (AActor* Found : Nearby)
	{
		// NOBODY IS STRUCK TWICE. The attacker is normally standing inside the
		// sphere as well, so without this it would take two payments.
		if (Found != Attacker)
		{
			Targets.Add(Found);
		}
	}

	return Targets;
}

float UCataclysmRetaliation::StrikeBack(AActor* Creature, AActor* Attacker)
{
	if (Creature == nullptr || Attacker == nullptr || Creature == Attacker)
	{
		return 0.0f;
	}

	// A RETURNED BLOW: the four flags `Pay` gives every returned blow, and the mark the hit log reads. See the header.
	FCataclysmHitDelivery Delivery;
	Delivery.bCannotBeRetaliatedAgainst = true;
	Delivery.bCannotCriticallyStrike = true;
	Delivery.bCarriesNoWeaponSubType = true;
	Delivery.bCannotLeech = true;
	Delivery.bIsRetaliation = true;

	// THE CREATURE'S OWN ATTACK DAMAGE, THROUGH ITS OWN ATTACK'S PIPELINE, and the attacker's defences take their share.
	const float Before = HealthOfRetaliationTarget(Attacker);
	UCataclysmSkillEffects::ApplyHit(Creature, Attacker, StrikeBackDamagePercent, FGameplayTagContainer(), Delivery);
	return FMath::Max(0.0f, Before - HealthOfRetaliationTarget(Attacker));
}

float UCataclysmRetaliation::PayPulse(UAbilitySystemComponent* Defender, AActor* Instigator, float RadiusMetres,
									   bool bApplyBleed)
{
	const UCataclysmAbilitySystemComponent* Paying = Cast<UCataclysmAbilitySystemComponent>(Defender);
	return Paying ? PayPulseFor(Defender, Instigator, RadiusMetres, Paying->LastBlowTakenThisFloor(), 100.0f, bApplyBleed)
				  : 0.0f;
}

float UCataclysmRetaliation::PayPulseFor(UAbilitySystemComponent* Defender, AActor* Instigator, float RadiusMetres,
										  float BlowWorth, float PercentOfPayment, bool bApplyBleed)
{
	UCataclysmAbilitySystemComponent* Paying = Cast<UCataclysmAbilitySystemComponent>(Defender);
	if (!Paying || !Instigator || RadiusMetres <= 0.0f || PercentOfPayment <= 0.0f)
	{
		return 0.0f;
	}
	const float Amount = AmountFor(Defender, BlowWorth, -1.0f) * PercentOfPayment / 100.0f
		* UCataclysmSkillEffects::KeptUnderWitheredTouch(Instigator);
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}

	// DELIVERED AS `Pay` DELIVERS: no retaliation back, no critical strike, no weapon subtype, no ordinary leech, marked as
	// a retaliation payment, and unmitigated where the defender's row says so.
	FCataclysmHitDelivery Delivery;
	Delivery.bCannotBeRetaliatedAgainst = true;
	Delivery.bCannotCriticallyStrike = true;
	Delivery.bCarriesNoWeaponSubType = true;
	Delivery.bCannotLeech = true;
	Delivery.bIsRetaliation = true;
	Delivery.bCannotBeMitigated = CannotBeMitigatedFor(Defender);

	const FCataclysmAilmentKind* Bleed = bApplyBleed ? UCataclysmAilments::KindNamed(TEXT("Bleed")) : nullptr;
	float TakenAltogether = 0.0f;
	for (AActor* Target : UCataclysmTargeting::FindEnemiesInSphere(Instigator->GetWorld(), Instigator,
																	Instigator->GetActorLocation(),
																	RadiusMetres * CentimetresPerMetre))
	{
		const float Before = HealthOfRetaliationTarget(Target);
		// AND THORNWALL'S STACKS ON THIS TARGET, a multiplier of its own (V3a, 2026-11-25).
		UCataclysmSkillEffects::ApplyDirectDamage(Instigator, Target, Amount * TakenMultiplierOf(Target), Delivery);
		const float Taken = FMath::Max(0.0f, Before - HealthOfRetaliationTarget(Target));
		TakenAltogether += Taken;
		Paying->ActOnEvent(FName(TEXT("retaliation_dealt")), /*EventTags=*/nullptr, /*EventAmount=*/Taken,
						   /*bLanded=*/Taken > 0.0f, Target);
		// "THIS ALSO APPLIES BLEED TO EVERY ENEMY IT STRIKES": an enemy it took health from. Laid directly, as the
		// sentence names it, and not through the ailment rule an event's status row keeps (a tenth of maximum health
		// until the owner's ruling of 2026-11-26, more than nought dealt since). A judgement.
		if (Bleed && Taken > 0.0f)
		{
			UCataclysmAilments::Apply(Instigator, Target, *Bleed, /*Magnitude=*/1.0f, /*Skill=*/nullptr, NAME_None,
									  /*Seconds=*/0.0f);
		}
	}

	if (TakenAltogether > 0.0f && LeechesFor(Defender))
	{
		UCataclysmLeech::NoteRetaliation(Defender, TakenAltogether);
	}
	return TakenAltogether;
}

float UCataclysmRetaliation::PayReflected(UAbilitySystemComponent* Defender, AActor* Instigator, AActor* Attacker,
										  float DamageTaken)
{
	return Pay(Defender, Instigator, Attacker, DamageTaken, /*PercentInPlaceOfTheStat=*/100.0f);
}

float UCataclysmRetaliation::Pay(UAbilitySystemComponent* Defender,
								 AActor* Instigator, AActor* Attacker,
								 float DamageTaken, float PercentInPlaceOfTheStat)
{
	// THE BLOW IS REMEMBERED WHETHER OR NOT ANYTHING IS PAID FOR IT, for Vengeful Discharge's pulse, which pays later on
	// the last blow taken this floor. The Fervour spends' follow-up, 2026-11-25.
	if (UCataclysmAbilitySystemComponent* Remembering = Cast<UCataclysmAbilitySystemComponent>(Defender))
	{
		Remembering->NoteBlowTakenForRetaliation(DamageTaken);
	}

	// AND WITHERED TOUCH ON THE DEFENDING BODY, ruled 2026-10-26 (#2541): the curse cuts a share of another's blow that
	// the player's own increases raise, and `AmountFor` prices this one through the defender's, as a retaliation or as a
	// reflected melee hit. `Instigator` is the defending body, the character the curse lands on. A minion carries no
	// curse, so its payment keeps its whole figure. A block's reflect is not paid here, and no increase raises it.
	float Amount = AmountFor(Defender, DamageTaken, PercentInPlaceOfTheStat)
		* UCataclysmSkillEffects::KeptUnderWitheredTouch(Instigator);
	// AND A STORED "NEXT RETALIATION DEALS MORE", SPENT BY A PAYMENT THAT SENDS SOMETHING: Spiteful (the War trees' engine
	// slice E2, 2026-11-27). A payment of nothing leaves it held.
	if (Amount > 0.0f)
	{
		if (UCataclysmAbilitySystemComponent* Spending = Cast<UCataclysmAbilitySystemComponent>(Defender))
		{
			Amount *= 1.0f + Spending->SpendNextRetaliationMorePercent() / 100.0f;
		}
	}
	if (Amount <= 0.0f)
	{
		// A CHARACTER WITH NONE OF THE STAT SENDS NOTHING BACK. Three things
		// grant it: the Masochist's class line, the Spaulders shoulder base as
		// an implicit, and the `Stat_Flat_retaliation` suffix, which fits seven
		// slots and can therefore reach any class. Enemies have none, so this is
		// the ordinary case rather than a fault.
		return 0.0f;
	}

	// FOUR THINGS THIS BLOW MAY NOT DO. See the header for why each one, and
	// note that the first is what stops two retaliating characters reflecting
	// at one another without end. It became necessary the moment retaliation
	// started going through the damage formula rather than around it.
	FCataclysmHitDelivery Delivery;
	Delivery.bCannotBeRetaliatedAgainst = true;
	Delivery.bCannotCriticallyStrike = true;
	Delivery.bCarriesNoWeaponSubType = true;
	Delivery.bCannotLeech = true;

	// AND IT SAYS WHAT IT IS: a blow returned for a blow taken, which the player's hit log names as such rather than as
	// an attack the defender made. Issue #2749 (B1). It changes nothing about how the blow resolves.
	Delivery.bIsRetaliation = true;

	// AND ONE THING THE STRUCK CHARACTER MAY NOT DO TO IT, FOR A DEFENDER WHOSE
	// ROWS SAY SO. Ruled 2026-10-12: "Retaliation damage cannot be reduced or
	// mitigated by the attacker". Asked once for the whole payment, so every
	// target of a wave takes the same kind of blow. A REFLECTED HIT IS PAID
	// THROUGH THIS FUNCTION AND TAKES THE FLAG TOO: the sentence that reflects a
	// melee hit pays it "back as retaliation damage". See
	// `FCataclysmHitDelivery::bCannotBeMitigated` for what is skipped.
	Delivery.bCannotBeMitigated = CannotBeMitigatedFor(Defender);

	// AND WHAT BLOCKS STORED, ON THE FIRST BLOW OF THIS PAYMENT ONLY. The War vow options' second slice, V2b,
	// 2026-11-25: Spiteful Guard, "Blocked damage is stored and added to your next retaliation hit". The whole store,
	// once, on the first target, which is the attacker when there is one, and the store empties; a payment with no
	// target keeps it. A blow's own block is noted after this payment for it is made, so what it blocked is added to
	// the next one. Added to what is sent, so each target's defences take their share of it as of the rest.
	const TArray<AActor*> Targets = TargetsOf(Defender, Attacker);
	float Stored = 0.0f;
	if (!Targets.IsEmpty())
	{
		if (UCataclysmAbilitySystemComponent* Storing = Cast<UCataclysmAbilitySystemComponent>(Defender))
		{
			Stored = Storing->SpendStoredBlockedDamage();
		}
	}

	float TakenAltogether = 0.0f;
	for (AActor* Target : Targets)
	{
		// MEASURED RATHER THAN ASSUMED, and that is the design's overkill rule.
		// What a target with 25 health left actually loses to a payment of 400
		// is 25, and the target's own armour and resistance now take a share
		// before that -- so what was sent and what landed differ twice over.
		const float Before = HealthOfRetaliationTarget(Target);
		// AND THORNWALL'S STACKS ON THIS TARGET, a multiplier of its own on the receiving side, of the whole blow sent,
		// what blocks stored included (V3a, ruled 2026-11-25).
		UCataclysmSkillEffects::ApplyDirectDamage(Instigator, Target, (Amount + Stored) * TakenMultiplierOf(Target),
												  Delivery);
		Stored = 0.0f;
		const float Taken =
			FMath::Max(0.0f, Before - HealthOfRetaliationTarget(Target));
		TakenAltogether += Taken;

		// AND THE DEFENDER'S ROWS HEAR OF IT, once for each target, with that
		// target and what reached its health. Issue #1833 group D: "Your
		// retaliation damage also applies a random DoT to attackers". Landed
		// only when health was taken, so a payment every defence absorbed
		// fires nothing.
		if (UCataclysmAbilitySystemComponent* Paying =
				Cast<UCataclysmAbilitySystemComponent>(Defender))
		{
			Paying->ActOnEvent(FName(TEXT("retaliation_dealt")), /*EventTags=*/nullptr,
							   /*EventAmount=*/Taken, /*bLanded=*/Taken > 0.0f, Target);
		}
	}

	// AND THE DEFENDER LEECHES FROM IT, IF IT HAS BOUGHT THAT. Issue #1048, the
	// Masochist's Feeding Wound. Nothing happens for anybody else, because the
	// flag is zero for every character without that capstone option.
	if (TakenAltogether > 0.0f && LeechesFor(Defender))
	{
		UCataclysmLeech::NoteRetaliation(Defender, TakenAltogether);
	}

	return TakenAltogether;
}
