// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class ACataclysmCharacterBase;
class UAbilitySystemComponent;
class UCataclysmAbilitySystemComponent;

/**
 * The Soul Collector's Siphon and the limb that acts on it (the Death trees' slice C2, 2026-11-25).
 *
 * THE KEYSTONE: "You siphon every enemy within 6 metres of you. The enemies in your Siphon are what the other nodes of
 * this limb act on, and what a skill that names your Siphon reads. Without this keystone you have no Siphon." Its row is
 * `siphon_radius_metres` at 6; a character without it has a radius of nought and no Siphon.
 *
 * IN THE SIPHON MEANS within the radius now, or out of it no longer than `siphon_linger_seconds` (Lingering Pull). The
 * record is the holder's (`UCataclysmAbilitySystemComponent::SiphonMembers`), kept by `Step` on the regeneration step.
 *
 * WHAT A MEMBER CARRIES WHILE IT IS IN: a cut to what it deals (`siphon_enemies_deal_less_percent`, Sapping Cold and
 * Enfeeble), read by `UCataclysmDebuffs::DealLessPercentOn` as the coordinating session ruled; and a slow of movement
 * and attack speed (`siphon_enemy_slow_percent`, Sapping Cold) through `ACataclysmEnemyCharacter::NoteGroundDown`.
 *
 * WHAT IS PAID ONCE A SECOND, as ruled on 2026-11-25 for the sentences that say "each second": Drain's Death damage
 * (`siphon_drain_weapon_percent`), Necrotic Siphon's chance of Necrosis (`siphon_necrosis_chance`), Life Siphon's
 * health (`siphon_life_percent`) and Living Harvest's Fervour (`siphon_fervour_per_second`). The last two are rows scaled
 * by `enemies_in_siphon` with their caps, so the count is applied by the pipeline and not here.
 *
 * THE AREA IS THE RADIUS'S INCREASE. Reaching Cold's and Pall of Souls' "increased area of your Siphon" are increased
 * rows on `siphon_radius_metres`, because an area in this game multiplies its radius
 * (`UCataclysmSkillEffects::AsMultiplierForSkill` on the area attribute). The character's own Area of Effect, which is
 * its skills', does not reach the Siphon, which is no skill (a judgement).
 *
 * AND AROUND THE EFFIGY: the Crossbow's Effigy, "If you have the Siphon keystone, the effigy siphons the enemies around it
 * too", is a machine placed by a row stating `SiphonsAround=1` (`ACataclysmMinion::bWidensSummonersSiphon`); while it
 * stands, `Step` takes in every enemy within the holder's own radius of it as well. Built by the Death finish slice A
 * (2026-11-26). Without the keystone the radius is nought and the Effigy is a decoy alone.
 */
class CATACLYSM_API FCataclysmSiphon
{
public:
	/** The Siphon's radius in metres: The Siphon's 6, Reaching Cold's and Pall of Souls' increases. */
	static const TCHAR* RadiusStat;

	/** The percent less a member deals: Sapping Cold's and Enfeeble's. */
	static const TCHAR* EnemiesDealLessStat;

	/** The percent a member's movement and attack speed are slowed: Sapping Cold's. */
	static const TCHAR* SlowStat;

	/** The seconds a member stays in after it leaves the radius: Lingering Pull's. */
	static const TCHAR* LingerStat;

	/** The percent of the holder's weapon damage Drain deals each member once a second, as Death damage. */
	static const TCHAR* DrainStat;

	/** The percent chance, once a second, that a member in for 3 seconds is given Necrosis: Necrotic Siphon's. */
	static const TCHAR* NecrosisChanceStat;

	/** The percent of the holder's maximum health restored once a second: Life Siphon's, scaled by the members. */
	static const TCHAR* LifeStat;

	/** The Fervour gained once a second: Living Harvest's, scaled by the members. */
	static const TCHAR* FervourStat;

	/** Necrotic Siphon: "An enemy that has been in your Siphon for 3 seconds". */
	static constexpr float NecrosisAfterSeconds = 3.0f;

	/**
	 * How long a member's cut and slow last past the step that put them on: three regeneration steps, the hold
	 * `UCataclysmDebuffs::GroundDownHeldSteps` gives Ground Down, so a body at the edge does not flicker in and out.
	 */
	static constexpr float HoldSeconds = 0.75f;

	/** The holder's Siphon radius in metres, or nought for a character with no Siphon. */
	static float RadiusMetresOf(const UAbilitySystemComponent* Holder);

	/**
	 * One regeneration step of `Holder`'s Siphon: who is in it, the cut and the slow on each member, and, once a
	 * second, `Pulse`. A character with no Siphon has its record cleared.
	 *
	 * @return how many enemies are in the Siphon after the step
	 */
	static int32 Step(ACataclysmCharacterBase* Holder, float StepSeconds);

	/** What is paid once a second: `DrainOnce`, `NecrosisOnce`, `LifeOnce` and `FervourOnce`. */
	static void Pulse(ACataclysmCharacterBase* Holder);

	/** Drain's Death damage to every member, once. @return the damage sent */
	static float DrainOnce(ACataclysmCharacterBase* Holder);

	/** Necrotic Siphon's roll for every member in for `NecrosisAfterSeconds`, once. @return how many were given it */
	static int32 NecrosisOnce(ACataclysmCharacterBase* Holder);

	/** Life Siphon's health, once. @return the health restored */
	static float LifeOnce(UAbilitySystemComponent* Holder);

	/** Living Harvest's Fervour, once, at the generation rate and clamped at the maximum. @return what arrived */
	static float FervourOnce(UAbilitySystemComponent* Holder);

	/**
	 * The line the screen shows above the skill bar for a character with a Siphon: how many enemies are in it and its
	 * radius. Empty for a character with none. Every system has a basic interface; this is the Siphon's.
	 */
	static FString DisplayLine(const UCataclysmAbilitySystemComponent* Holder);
};
