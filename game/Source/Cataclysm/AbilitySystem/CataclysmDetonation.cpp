// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDetonation.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBoneGiants.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRisingCurse.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
// For `UCataclysmSummonSkill`, which a minion's `SummonedBy` names: the skill that raised it.
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Cataclysm.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

const TCHAR* UCataclysmDetonation::WeaponSkillsDetonateOwnMinionsStat = TEXT("weapon_skills_detonate_own_minions");
const TCHAR* UCataclysmDetonation::DetonatesWhenReleasedStat = TEXT("minion_detonates_when_released");
const TCHAR* UCataclysmDetonation::DetonationDamageStat = TEXT("minion_detonation_damage");
const TCHAR* UCataclysmDetonation::DetonationAreaStat = TEXT("minion_detonation_area");
const TCHAR* UCataclysmDetonation::RotSecondsStat = TEXT("minion_detonation_rot_seconds");
const TCHAR* UCataclysmDetonation::RotPercentStat = TEXT("minion_detonation_rot_percent");
const TCHAR* UCataclysmDetonation::ExplodesAtLifespanEndChanceStat = TEXT("minion_explodes_at_lifespan_end_chance");
const TCHAR* UCataclysmDetonation::SpreadPercentStat = TEXT("minion_detonation_spreads_percent");
const TCHAR* UCataclysmDetonation::SpreadRadiusMetresStat = TEXT("minion_detonation_spread_radius_metres");
const TCHAR* UCataclysmDetonation::FervourPerReserveMultipleStat = TEXT("fervour_on_minion_detonated_reserve_multiple");
const TCHAR* UCataclysmDetonation::DetonateOnContactStat = TEXT("minions_detonate_on_contact");
const TCHAR* UCataclysmDetonation::RaisesHuskChanceStat = TEXT("minion_detonation_raises_husk_chance");
const TCHAR* UCataclysmDetonation::HuskSecondsStat = TEXT("minion_detonation_husk_seconds");
const TCHAR* UCataclysmDetonation::ExtraRaiseChanceStat = TEXT("minion_extra_raise_chance");
const TCHAR* UCataclysmDetonation::DetonatedEvent = TEXT("minion_detonated");

int32 UCataclysmDetonation::DetonationsMade = 0;

namespace
{
	/** A roll of `ChancePercent` in a hundred. At a hundred or more it always passes, at nought or less never. */
	bool RollsUnder(float ChancePercent)
	{
		if (ChancePercent <= 0.0f)
		{
			return false;
		}
		return ChancePercent >= 100.0f || FMath::FRand() * 100.0f < ChancePercent;
	}

	/**
	 * Whether `Minion` is one a detonation takes: raised, no machine, standing, and NO VOW GIANT. Every detonation of a
	 * summoner's own minions asks this first -- a skill's (`Detonates=1` or Lit Fuse, through `Detonate`), Made Room's
	 * release, Living Bombs' contact and Mass Grave's spread -- so the Abomination and the Bone Colossus are exempt from
	 * all of them in this one place (the coordinating session's ruling of 2026-11-26 at the bundle's assembly).
	 */
	bool MayDetonate(const ACataclysmMinion* Minion)
	{
		return IsValid(Minion) && !Minion->IsActorBeingDestroyed() && !Minion->bIsMachine
			&& !UCataclysmSkillEffects::IsDead(Minion) && !UCataclysmBoneGiants::IsAGiant(Minion);
	}
}

float UCataclysmDetonation::SummonerStat(const AActor* Summoner, const TCHAR* Stat, const FGameplayTagContainer& Tags)
{
	// `StatForSkill` RATHER THAN AN ATTRIBUTE: none of these stats has one, so the stat line is the only place its value
	// exists. Nought for a summoner with no stat line, which is ordinary: an enemy summoner never has one.
	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Summoner));
	return Theirs ? Theirs->StatForSkill(FName(Stat), Tags, 0.0f) : 0.0f;
}

bool UCataclysmDetonation::WeaponSkillsDetonateOwnMinions(const AActor* Caster)
{
	return SummonerStat(Caster, WeaponSkillsDetonateOwnMinionsStat) > 0.0f;
}

float UCataclysmDetonation::DamageMultiplierFor(const ACataclysmMinion* Minion)
{
	// THE MINION ASKING IS NAMED, so `MultiplierForStatAgainst` fills its age (Swollen) and the distances of its fellows
	// (Close Packed) before the rows are judged.
	return IsValid(Minion)
		? UCataclysmCommand::SummonerMultiplierFor(Minion->Summoner.Get(), DetonationDamageStat, Minion->TypeTags, Minion)
		: 1.0f;
}

float UCataclysmDetonation::AreaMultiplierFor(const ACataclysmMinion* Minion)
{
	return IsValid(Minion)
		? UCataclysmCommand::SummonerMultiplierFor(Minion->Summoner.Get(), DetonationAreaStat, Minion->TypeTags)
		: 1.0f;
}

bool UCataclysmDetonation::IsALivingBomb(const ACataclysmMinion* Minion)
{
	// NO VOW GIANT, as `MayDetonate` exempts them: the Abomination and the Bone Colossus never go off, so a Living
	// Bomb's contact reach must not be theirs. Until 2026-11-27 it was, and a Colossus beside a Living Bombs vow walked
	// to contact and never struck (the owner's play test: "just sitting at the front getting attacked").
	return IsValid(Minion) && !Minion->bIsMachine && !UCataclysmBoneGiants::IsAGiant(Minion)
		&& SummonerStat(Minion->Summoner.Get(), DetonateOnContactStat, Minion->TypeTags) > 0.0f;
}

float UCataclysmDetonation::AttackReachCmFor(const ACataclysmMinion* Minion, float Reach)
{
	// THE REACH A BRAIN STOPS AT, cut to contact: `ACataclysmEnemyController` walks a minion in until its target is within
	// this, so a Skeletal Mage holding the vow runs in as an imp does rather than going off from across the room.
	return IsALivingBomb(Minion) ? FMath::Min(Reach, ContactAllowanceCm) : Reach;
}

TArray<ACataclysmMinion*> UCataclysmDetonation::RaisedMinionsAround(const AActor* Summoner, const FVector& Centre,
																	float RadiusCm, const ACataclysmMinion* Except)
{
	// THE BODY COUNTS, as `UCataclysmSkillTemplate::DetonateOwnMinionsAround` measures a minion: within the radius and its
	// own body's, along the ground. WHOSE IT IS, not which side, for the reason that function gives.
	TArray<ACataclysmMinion*> Found;
	if (RadiusCm <= 0.0f)
	{
		return Found;
	}
	for (AActor* Follower : UCataclysmCommand::ThingsCommandedBy(Summoner))
	{
		ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
		if (Minion == Except || !MayDetonate(Minion) || Minion->Summoner.Get() != Summoner)
		{
			continue;
		}
		const float Reach = RadiusCm + UCataclysmTargeting::BodyRadiusOf(Minion);
		if (FVector::DistSquared2D(Minion->GetActorLocation(), Centre) <= Reach * Reach)
		{
			Found.Add(Minion);
		}
	}
	return Found;
}

bool UCataclysmDetonation::Detonate(ACataclysmMinion* Minion, UCataclysmSkillTemplate* SetOffBy, float SharePercent,
									bool bMaySpread)
{
	if (!MayDetonate(Minion) || SharePercent <= 0.0f)
	{
		return false;
	}

	// EVERYTHING THAT OUTLIVES THE BLAST, READ NOW: the minion is destroyed by it.
	AActor* const Summoner = Minion->Summoner.Get();
	UCataclysmSkillTemplate* const Skill = SetOffBy ? SetOffBy : Minion->SummonedBy.Get();
	const FGameplayTagContainer Tags = Minion->TypeTags;
	const FVector Where = Minion->GetActorLocation();
	const float Reserved = Minion->ReservedFervour;
	const float RadiusCm = Minion->ExplosionRadiusCm * AreaMultiplierFor(Minion);
	UCataclysmAbilitySystemComponent* Told =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Summoner));

	// MASS GRAVE'S RING, GATHERED BEFORE THE BLAST, at the places the minions stand as this one goes off. ONE RING: a
	// minion the chain sets off does not set off its own neighbours, a judgement -- otherwise one detonation in a packed
	// army would take the whole army with it, at 40% each, and the keystone would be "detonating a minion detonates every
	// minion of yours" -- see `docs/DECISIONS.md`.
	TArray<TWeakObjectPtr<ACataclysmMinion>> Ring;
	const float SpreadPercent = bMaySpread ? SummonerStat(Summoner, SpreadPercentStat, Tags) : 0.0f;
	const float SpreadRadiusCm = SummonerStat(Summoner, SpreadRadiusMetresStat, Tags) * 100.0f;
	if (SpreadPercent > 0.0f && SpreadRadiusCm > 0.0f)
	{
		for (ACataclysmMinion* Near : RaisedMinionsAround(Summoner, Where, SpreadRadiusCm, Minion))
		{
			Ring.Add(Near);
		}
	}

	// TOLD AS A MINION'S DEATH FIRST, while the body stands, as `DetonateOwnMinionsWhere` always told it: Marrow Feast,
	// and the clocks Death Ward and Bone Knit ask.
	if (Told)
	{
		Told->NoteMinionDied(Minion, Tags);
	}

	// THE BLAST. With a skill, the detonation's figure at this share; with none, the minion's own explosion.
	const float Figure = Minion->ExplodeSetOffBy(Skill, SharePercent / 100.0f);
	++DetonationsMade;

	if (IsValid(Summoner) && Told)
	{
		// LAST BREATH: "you gain Fervour equal to twice what it reserved". AFTER THE BLAST, which destroyed the minion and
		// so freed its reserve: gained before, the pool would still be holding it back and the gain would not fit.
		// THROUGH `GainStated`, scaled by how fast the pool generates and handed to `RaisePool`, as every gain is. Not a
		// kill's payment.
		const float Multiple = SummonerStat(Summoner, FervourPerReserveMultipleStat, Tags);
		if (Multiple > 0.0f && Reserved > 0.0f)
		{
			UCataclysmFervour::GainStated(Told, Reserved * Multiple);
		}

		// LINGERING ROT: ground where it went off, for its seconds, dealing its share of the detonation's figure each
		// second (`ACataclysmGroundZone::TickSeconds` is one), as the skill that set it off: the same radius, credited to
		// the skill. ONLY A DETONATION WITH A FIGURE: the minion's own explosion, with no skill, leaves none.
		const float RotSeconds = SummonerStat(Summoner, RotSecondsStat, Tags);
		const float RotPercent = SummonerStat(Summoner, RotPercentStat, Tags);
		if (RotSeconds > 0.0f && RotPercent > 0.0f && Figure > 0.0f && RadiusCm > 0.0f)
		{
			UCataclysmSkillTemplate::LeaveAZoneFor(Summoner, Where, Where, RadiusCm, RotSeconds,
												   Figure * RotPercent / 100.0f,
												   Skill ? Skill->SkillTagsForStats() : Tags, NAME_None, NAME_None, Skill);
		}

		// LIVING BOMBS: "each detonation has a 50% chance to raise a husk that lasts 8 seconds where it went off". EVERY
		// detonation of the vow's holder, whatever set it off, as the sentence says "each".
		const float HuskChance = SummonerStat(Summoner, RaisesHuskChanceStat, Tags);
		const float HuskSeconds = SummonerStat(Summoner, HuskSecondsStat, Tags);
		if (HuskSeconds > 0.0f && RollsUnder(HuskChance))
		{
			RaiseHuskAt(Summoner, Where, HuskSeconds);
		}
	}

	// BURSTING RANKS COUNTS IT AFTER IT WENT OFF, so a detonation is raised by those before it and not by itself.
	NoteDetonation(Summoner, Tags);

	// AND THE RING, EACH AT MASS GRAVE'S SHARE OF THE WHOLE DETONATION, as the same skill's hit.
	for (const TWeakObjectPtr<ACataclysmMinion>& Each : Ring)
	{
		Detonate(Each.Get(), Skill, SpreadPercent, /*bMaySpread=*/false);
	}
	return true;
}

void UCataclysmDetonation::NoteDetonation(AActor* Character, const FGameplayTagContainer& Tags)
{
	UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
	if (!Theirs)
	{
		return;
	}
	// NO CHARACTER: what went off is gone, and remains are no character at all. THE NAME WRITTEN OUT, as every event is
	// fired, so `tools/tests/test_pool_action_names_match_the_engine.py` reads it; `DetonatedEvent` holds the same.
	Theirs->ActOnEvent(FName(TEXT("minion_detonated")), &Tags, /*EventAmount=*/0.0f, /*bLanded=*/true,
					   /*EventTarget=*/nullptr);
}

bool UCataclysmDetonation::DetonateOnRelease(ACataclysmMinion* Minion)
{
	if (!MayDetonate(Minion) || SummonerStat(Minion->Summoner.Get(), DetonatesWhenReleasedStat, Minion->TypeTags) <= 0.0f)
	{
		return false;
	}
	// "WITH THE RADIUS AND DAMAGE OF THE SKILL THAT RAISED IT": no skill named, so `Detonate` takes the raising skill.
	return Detonate(Minion, /*SetOffBy=*/nullptr);
}

bool UCataclysmDetonation::RipenAtLifespanEnd(ACataclysmMinion* Minion)
{
	if (!MayDetonate(Minion) || Minion->ExplosionRadiusCm <= 0.0f)
	{
		return false;
	}
	const float Chance = SummonerStat(Minion->Summoner.Get(), ExplodesAtLifespanEndChanceStat, Minion->TypeTags);
	if (!RollsUnder(Chance))
	{
		return false;
	}
	UE_LOG(LogCataclysm, Display, TEXT("RIPENED: %s (%s) reached the end of its time and explodes anyway (%.0f%%)."),
		   *Minion->TypeName, *Minion->GetName(), Chance);
	Minion->Explode();
	return true;
}

bool UCataclysmDetonation::DetonateOnContact(ACataclysmMinion* Minion, const AActor* Target)
{
	if (!MayDetonate(Minion) || !IsValid(Target) || !IsALivingBomb(Minion))
	{
		return false;
	}
	const float Apart = FVector::Dist2D(Minion->GetActorLocation(), Target->GetActorLocation())
		- UCataclysmTargeting::BodyRadiusOf(Minion) - UCataclysmTargeting::BodyRadiusOf(Target);
	if (Apart > ContactAllowanceCm)
	{
		return false;
	}
	return Detonate(Minion, /*SetOffBy=*/nullptr);
}

ACataclysmMinion* UCataclysmDetonation::RaiseHuskAt(AActor* Commander, const FVector& Where, float Seconds)
{
	if (!IsValid(Commander) || UCataclysmSkillEffects::IsDead(Commander) || Seconds <= 0.0f)
	{
		return nullptr;
	}
	static const FString HuskKind(TEXT("Husk"));
	FCataclysmWeaponSkill Row;
	if (!UCataclysmRisingCurse::FindTheRowThatRaises(TEXT("Death"), HuskKind, Row))
	{
		UE_LOG(LogCataclysm, Warning, TEXT("No Death Summon row raises a Husk with a Duration, so no husk rose."));
		return nullptr;
	}
	const float Reserve = UCataclysmCommand::ReserveFor(Commander, Row.Params.FervourReserve, Row.Tags);
	if (!UCataclysmCommand::HasRoomToReserve(Commander, Reserve))
	{
		UE_LOG(LogCataclysm, Verbose, TEXT("No room in %s's pool for a husk reserving %.0f Fervour, so none rose."),
			   *GetNameSafe(Commander), Reserve);
		return nullptr;
	}
	ACataclysmMinion* Husk = ACataclysmMinion::Spawn(Commander, Where, Seconds, Row.Params.bBurns, HuskKind);
	if (!Husk)
	{
		return nullptr;
	}
	Husk->RecordExplosionRadius(Row.Params.RadiusCm);
	if (const UWorld* World = Husk->GetWorld())
	{
		Husk->RisenAtSeconds = World->GetTimeSeconds();
	}
	UCataclysmCommand::HoldReserve(Commander, Husk, Reserve);
	if (Row.Params.bTaunts)
	{
		Husk->BeginTaunting(Row.Params.RadiusCm, ACataclysmMinion::RowTauntEverySeconds);
	}
	UE_LOG(LogCataclysm, Display, TEXT("A detonation raised a husk for %s, lasting %.1f s and reserving %.0f Fervour."),
		   *GetNameSafe(Commander), Seconds, Reserve);
	return Husk;
}
