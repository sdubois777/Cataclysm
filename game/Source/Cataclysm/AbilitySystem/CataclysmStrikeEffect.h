// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmStrikeEffect.generated.h"

/**
 * Forward declared rather than included, so this header pulls in no Niagara
 * type. The Niagara module is a PRIVATE dependency of this module and should
 * stay one -- see Cataclysm.Build.cs. A pointer to an incomplete class needs
 * nothing from that module, so anything may include this header; only code that
 * dereferences the result has to include NiagaraComponent.h itself.
 */
class UNiagaraComponent;

/**
 * What a melee swing looks like.
 *
 * WHAT THIS FILLS, AND IT IS THE LARGEST EFFECT GAP THERE WAS. Of the 51
 * designed Demonic skills in `game/Data/WeaponSkills.csv`, 16 have the `Strike`
 * shape against `Projectile`'s 10, which makes it the most common shape in the
 * game. Every one of them drew NOTHING WHATSOEVER: a swing happened, damage
 * landed, and the screen did not change until the hit burst went off on the
 * target. That is most of what the project owner meant on 2026-08-21 by "your
 * skills effects are basically the same as they were before... that hardly
 * counts as a real game skill". Issue #811.
 *
 * A NINTH SHAPE, AND ADDING IT IS A DESIGN DECISION AS WELL AS AN AUTHORING
 * JOB. `docs/Niagara_Conventions.md` section 5 lists eight effect shapes and a
 * melee strike is not among them; the closest, `NS_Impact_Point`, is what a blow
 * looks like where it LANDS rather than what the swing looks like where it
 * STARTS. The two are different moments and want different assets: one is at the
 * target and lasts an instant, the other is at the caster and sweeps. So
 * `NS_Strike_Arc` is a new shape rather than a reuse, and the conventions
 * document now lists nine.
 *
 * ONE SYSTEM SERVES ALL EIGHT DAMAGE TYPES, exactly as the impact burst and the
 * projectile body do. The colours come from `DT_ElementVisuals` at the moment of
 * the swing, so a Demonic cleave and a Void cleave are the same asset with
 * different rows and changing a colour is a data edit.
 *
 * THE ARC MESH AND ITS MATERIAL COME OUT OF A GITIGNORED PACK, which is the
 * arrangement `game/docs/effect-source-assets.md` already records for the
 * projectile's streak and the enemy Blueprints' Paragon meshes. On a fresh clone
 * without the pack the emitter draws with the engine's default material.
 * `Cataclysm.Effects.StrikeArcDrawsTheSlashMesh` names what it expects, so the
 * substitution is reported rather than silent.
 */
UCLASS()
class CATACLYSM_API UCataclysmStrikeEffect : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The system built for this, answering issue #811. */
	static const TCHAR* SystemAssetPath;

	/**
	 * The user parameters this class writes. Bare names without the `User.`
	 * prefix, which Niagara adds itself, and taken from
	 * CataclysmEffectParameterNames so there is one spelling of each in the
	 * whole project.
	 *
	 * A PARAMETER THE SYSTEM DOES NOT EXPOSE IS IGNORED IN SILENCE -- no
	 * warning, no error, no effect -- so a typo here does not fail, it just
	 * quietly stops the colours arriving.
	 * `Cataclysm.Effects.StrikeArcExposesTheStandardParameterBlock` is what
	 * notices.
	 */
	static const FName ElementColourParameter;
	static const FName ElementColourDarkParameter;
	static const FName ScaleParameter;

	/**
	 * What `User.Scale` is set to for a swing of this radius.
	 *
	 * THE ARC MESH REACHES ONE METRE AT SCALE ONE, measured from its own bounds:
	 * `SM_slash` occupies roughly 200 by 200 centimetres centred on its origin.
	 * So dividing the swing's radius by 100 makes the drawn arc reach exactly as
	 * far as the cone that decides who was hit, rather than being a size that
	 * happens to suit one skill.
	 *
	 * IT IS THE SAME CONVENTION EVERY OTHER SYSTEM USES. `User.Scale` is
	 * documented across the project as "the ability's radius in centimetres
	 * divided by 100", and `UCataclysmProjectileEffect::ScaleFor` does the same
	 * arithmetic on a body radius.
	 *
	 * CLAMPED AT BOTH ENDS, AND THE BOUNDS ARE GUARDS RATHER THAN DESIGN. The
	 * designed strikes run from 2.5 metres (Emberpierce, Searing Hook) to 7
	 * (Rain of Cinders, Firestorm of Lashes), so neither bound is a number
	 * anything designed asks for. They exist so a future data row asking for
	 * zero cannot draw an invisible swing and one asking for a kilometre cannot
	 * fill the level.
	 *
	 * SEPARATE AND STATIC SO A TEST CAN REACH IT. Nothing here needs a world, a
	 * component or a rendering device, so the automation harness can exercise it
	 * -- which issue #559 records it cannot do for the spawn itself.
	 */
	static float ScaleFor(float RadiusCm);

	/** The smallest and largest `User.Scale` a swing may ask for. */
	static constexpr float MinimumScale = 0.5f;
	static constexpr float MaximumScale = 8.0f;

	/**
	 * Which way the arc points, given where the caster aimed.
	 *
	 * THE MESH IS LAID OUT ALONG ITS OWN +X, so the effect is spawned with a
	 * rotation whose forward axis is the aim direction and the mesh needs no
	 * orientation work inside Niagara at all. Pitch is dropped: a swing sweeps
	 * across the ground, and letting the arc tilt to follow a cursor that is
	 * above or below the caster would stand it on edge.
	 *
	 * A ZERO DIRECTION GIVES A ZERO ROTATION rather than a NaN.
	 * `FVector::Rotation` on a zero vector is not defined to return anything
	 * useful, and `AimDirection` can return zero when a controller has no cursor
	 * yet -- which is every automation test.
	 */
	static FRotator FacingFor(const FVector& Direction);

	/**
	 * Which damage type the arc draws in.
	 *
	 * DRAWING IN A DAMAGE TYPE AND DEALING ONE ARE DIFFERENT QUESTIONS, and
	 * treating them as the same is what makes every player effect in the project
	 * white. Issue #803. `UCataclysmSkillEffects::DamageTypeOf` answers the
	 * damage question, and its answer for a player is deliberately NAME_None:
	 * the project owner settled on 2026-08-12 that a player has eight
	 * resistances because eight Cataclysms attack them, while an enemy has one
	 * generic resistance that meets every hit, so a player's blow has nothing to
	 * choose between and carries no type.
	 *
	 * BUT A PLAYER'S SKILL STILL IS ONE. Every row of the Weapon Skills sheet
	 * carries exactly one `Element.*` tag, because the sheet is a matrix of
	 * weapon type against damage type. Molten Cleave is a Demonic skill whether
	 * or not its damage is typed, and drawing it white throws away a fact the
	 * data already states.
	 *
	 * THE CASTER WINS WHERE BOTH ANSWER. An enemy's own damage type is what its
	 * hits are resisted as, so an effect that disagreed with it would be
	 * misleading in the one case where the colour carries information the player
	 * can act on.
	 *
	 * @param Caster       whoever swung. Null gives the skill's own answer.
	 * @param SkillElement the swinging skill's `Element.*` tag, normally
	 *                     `UCataclysmSkillTemplate::ElementTag()`. An invalid
	 *                     tag is allowed and means the skill says nothing.
	 *
	 * Returns NAME_None when neither answers, and NAME_None means "draw the
	 * authored default", not "something went wrong".
	 *
	 * IT ONLY FIXES THIS SHAPE. The projectile body and the hit burst still ask
	 * `DamageTypeOf` directly and still draw white for a player. #803 stays open
	 * for them.
	 */
	static FName DamageTypeFor(const AActor* Caster,
							   const FGameplayTag& SkillElement);

	/**
	 * How many times an arc has been asked for since the editor started.
	 *
	 * IT EXISTS BECAUSE NO TEST IN THIS PROJECT CAN SEE A NIAGARA SPAWN. The
	 * automation command passes `-nullrhi` and Niagara refuses to create a
	 * component when `FApp::CanEverRender()` is false, so `PlayAt` returns null
	 * in every test and there is nothing to assert about. Issue #559.
	 *
	 * WITHOUT IT THE WHOLE SHAPE COULD BE DELETED FROM THE CALL CHAIN AND THE
	 * SUITE WOULD STAY GREEN, which is precisely the defect that
	 * `UCataclysmClassStats` had: it worked, it had tests, and no code path
	 * reached it. `Cataclysm.Effects.EverySwingAsksForAnArc` drives a real swing
	 * and reads this, so removing the call from
	 * `UCataclysmStrikeSkill::SwingOnce` fails a test rather than quietly
	 * removing every melee effect in the game.
	 *
	 * COUNTED AT ENTRY AND NOT AT SUCCESS, on purpose. The question it answers
	 * is "did the swing ask for an arc", not "did an arc appear"; the second is
	 * unanswerable here and depends on the renderer, the effect type's
	 * scalability and the distance to the camera.
	 *
	 * A COUNTER IN SHIPPING CODE IS A COST AND IT IS ONE INCREMENT PER SWING.
	 * The alternative was leaving the most common skill shape in the game with
	 * no guard at all on whether it is still wired up.
	 */
	static int32 TimesAsked;

	/**
	 * Draws the swing.
	 *
	 * @param WorldContextObject anything with a world. The caster, normally.
	 * @param Location where the caster is standing.
	 * @param Direction where they aimed. Need not be normalised.
	 * @param DamageType the leaf of the caster's `Element.*` tag, or NAME_None.
	 * @param RadiusCm how far the swing reaches, which is also how far the arc
	 *                 is drawn.
	 *
	 * Returns the component so a caller can inspect what it was given. Null when
	 * there is no world, the system asset is missing, or the effect type's
	 * scalability rejected the spawn.
	 *
	 * THAT LAST ONE IS NOT A FAILURE. It is `FXT_Enemy` working: past 4000 cm,
	 * or outside the view frustum, or beyond twenty live instances of this
	 * system, the effect is refused before it starts and the swing draws nothing
	 * -- which is what it did before this existed. That is the whole reason the
	 * effect type exists.
	 *
	 * IT ALSO RETURNS NULL IN EVERY AUTOMATION TEST, and that is a property of
	 * the harness rather than of this code. Niagara's `CreateNiagaraSystem`
	 * checks `FApp::CanEverRender()` before it does anything at all, and the test
	 * command in `tools/unreal_build.py` passes `-nullrhi`. So no automation test
	 * can observe a spawned component, and none of them try. Issue #559 is what
	 * would be needed to cover the spawn itself.
	 *
	 * NOT ATTACHED TO THE CASTER, unlike the projectile's body effect. A swing
	 * is over in a quarter of a second and the caster keeps moving; an attached
	 * arc would slide along with them and read as a held object rather than as a
	 * blow that has already been struck.
	 */
	static UNiagaraComponent* PlayAt(const UObject* WorldContextObject,
									 const FVector& Location,
									 const FVector& Direction,
									 FName DamageType,
									 float RadiusCm);
};
