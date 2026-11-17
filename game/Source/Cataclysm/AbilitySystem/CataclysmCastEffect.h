// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "CataclysmCastEffect.generated.h"

class UNiagaraComponent;

/**
 * The burst at the caster when a skill goes off.
 *
 * WHY THIS EXISTS. Commercial ability effect packs are organised as three
 * systems per skill -- a muzzle, a body and a hit -- and Riot's League of
 * Legends style guide calls the same three beats anticipation, impact and
 * dissipation. Gabriel Aguiar's Magic Projectiles pack ships 240 effects split
 * 66 muzzles, 91 projectiles, 83 hits. This project had the body and the hit and
 * nothing at the caster, so a third of every skill was missing and every skill
 * began with nothing happening. `docs/Niagara_Conventions.md` section 5A is the
 * research; issue #811 is the project owner rejecting the effects three times.
 *
 * IT PLAYS AT THE MOMENT OF RELEASE, NOT BEFORE IT, AND THE NAME OVERSTATES IT.
 * The conventions document calls this shape `NS_Cast_Windup`, and a wind-up is
 * anticipation: something the player sees BEFORE the skill acts. No skill in
 * this project has a cast time. Every template's `ActivateAbility` commits and
 * acts in the same frame, so there is no window to anticipate in. Giving skills
 * one would change how the game plays, which is not something an effects change
 * should decide. So this fires at the same instant the skill does, which makes
 * it a muzzle flash rather than a wind-up. The asset keeps the document's name
 * so the two do not drift apart.
 *
 * ONE CALL SITE, EVERY SKILL. `UCataclysmSkillTemplate::CommitAndBegin` is the
 * single function all eight skill shapes call first, and it already knows
 * whether the skill really went off: it returns false when the cost or the
 * cooldown refused it. A skill that was refused draws nothing, which is the
 * behaviour wanted -- a flash on a skill that did not fire would read as a bug.
 *
 * A PLAYER'S SHAPE. Skill templates are granted only through the player's
 * weapon slots, in `UCataclysmWeaponSlotsComponent`, so nothing an enemy does
 * reaches here. That is why the system carries `FXT_PlayerSkill` rather than
 * `FXT_Enemy`, and it is the first system in the project to use it.
 */
UCLASS()
class CATACLYSM_API UCataclysmCastEffect : public UObject
{
	GENERATED_BODY()

public:
	/** The system this plays. `/Game/Effects/Systems/Skills/NS_Cast_Windup`. */
	static const TCHAR* SystemAssetPath;

	/** The damage type's primary hue, from `DT_ElementVisuals`. */
	static const FName ElementColourParameter;

	/** The damage type's dark anchor, from `DT_ElementVisuals`. */
	static const FName ElementColourDarkParameter;

	/** How big the burst is, from the skill's own radius. */
	static const FName ScaleParameter;

	/**
	 * The burst's size for a skill of this radius, clamped.
	 *
	 * ONE METRE OF RADIUS IS ONE UNIT OF SCALE, so a three metre swing and a
	 * three metre bolt light the caster the same amount. The authored size is
	 * one, which is what a one metre skill gets.
	 *
	 * CLAMPED AT BOTH ENDS AND THE ENDS ARE NARROWER THAN THE OTHER SHAPES'.
	 * A muzzle sits on the caster and is seen at arm's length every time a
	 * button is pressed, so it cannot grow with the skill the way a ground zone
	 * does: `Pyroclasm` covers five metres and a five-times burst on the
	 * player's own body would cover the screen. A radius of zero or less is a
	 * real case -- a self buff states no radius at all -- and takes the minimum
	 * rather than vanishing, because a buff going off should still be visible.
	 */
	static float ScaleFor(float RadiusCm);

	/** A self buff states no radius. It still lights the caster. */
	static constexpr float MinimumScale = 0.6f;

	/** Beyond this the burst covers the caster instead of lighting them. */
	static constexpr float MaximumScale = 2.5f;

	/**
	 * Which of the eight damage types to draw this in, or None.
	 *
	 * THE CASTER FIRST, THEN THE SKILL, which is the shape
	 * `UCataclysmStrikeEffect::DamageTypeFor` established and
	 * `UCataclysmProjectileEffect::DamageTypeFor` followed. An enemy's own type
	 * is what its hits are resisted as; a player's hits carry no type at all, so
	 * the skill's own `Element.*` tag is what colours a player's effects. Issue
	 * #803.
	 *
	 * @param Caster        who is casting
	 * @param SkillElement  the skill's own `Element.*` tag, from
	 *                      `UCataclysmSkillTemplate::ElementTag()`. An invalid
	 *                      tag means the skill names no damage type.
	 */
	static FName DamageTypeFor(const AActor* Caster,
							   const FGameplayTag& SkillElement);

	/**
	 * Which way the burst points, from the direction the skill was aimed.
	 *
	 * FLATTENED, for the same reason the swing arc is: the camera looks down at
	 * 60 degrees and a burst tipped towards the sky presents its edge. A zero
	 * direction is a real case -- a self buff aims at nothing and
	 * `AimDirection` answers zero -- and takes the caster's own facing.
	 */
	static FRotator FacingFor(const FVector& Direction, const AActor* Caster);

	/**
	 * How many times a skill has asked for a cast burst since the editor
	 * started.
	 *
	 * IT EXISTS BECAUSE NO TEST IN THIS PROJECT CAN SEE A NIAGARA SPAWN. The
	 * automation command passes `-nullrhi` and Niagara refuses to create a
	 * component when `FApp::CanEverRender()` is false, so `PlayFor` can spawn
	 * nothing and there is nothing to assert about. Issue #559.
	 *
	 * WITHOUT IT THE CALL COULD BE DELETED AND THE SUITE WOULD STAY GREEN, which
	 * is what happened to the projectile's body effect until 2026-08-22 and to
	 * the hit burst until the same day.
	 */
	static int32 TimesAsked;

	/** The damage type the last burst was asked for, or None. */
	static FName LastDamageTypeAsked;

	/**
	 * Draw the burst at the caster.
	 *
	 * Returns the component, or null when there is no caster, no world, the
	 * asset is missing, or the effect type's scalability refused the spawn.
	 * Every automation test gets null, which is the harness rather than a fault.
	 */
	static UNiagaraComponent* PlayFor(const AActor* Caster,
									  const FVector& Direction,
									  FName DamageType, float RadiusCm);
};
