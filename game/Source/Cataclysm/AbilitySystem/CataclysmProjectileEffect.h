// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmProjectileEffect.generated.h"

class ACataclysmProjectile;

/**
 * Forward declared rather than included, so this header pulls in no Niagara
 * type. The Niagara module is a PRIVATE dependency of this module and should
 * stay one -- see Cataclysm.Build.cs. A pointer to an incomplete class needs
 * nothing from that module, so anything may include this header; only code that
 * dereferences the result has to include NiagaraComponent.h itself.
 */
class UNiagaraComponent;

/**
 * What a projectile looks like while it is in the air.
 *
 * WHAT THIS FILLS. ACataclysmProjectile's own header said it plainly: "NO
 * PARTICLE EFFECT, on the other hand. That is a content gap." Every fired thing
 * in the game -- ten designed player projectile skills, the Brute's rock, the
 * Corrupted Sentinel's shot, the Succubus's bolt and the Gatekeeper's gout --
 * crossed the room as a bare grey sphere with nothing around it.
 *
 * TWO PARTS, AND THEY LIVE IN DIFFERENT SPACES ON PURPOSE. NS_Proj_Body has a
 * `Core` emitter in LOCAL space, so its one sprite rides the projectile as the
 * glowing head, and a `Trail` emitter in WORLD space, so each spark it spawns is
 * left behind where it was born instead of being dragged along. Swapping those
 * two settings gives a head that falls behind and a trail that never trails,
 * which is the single thing most likely to be got wrong here.
 * Cataclysm.Effects.ProjectileHeadRidesAndTrailStaysBehind is what notices.
 *
 * ONE SYSTEM SERVES ALL EIGHT DAMAGE TYPES, exactly as the impact burst does.
 * The colours come from DT_ElementVisuals at the moment of firing, so a Demonic
 * bolt and a Void bolt are the same asset with different rows.
 * docs/Niagara_Conventions.md section 5 sets it out.
 *
 * A PLAYER'S PROJECTILE IS UNTYPED AND DRAWS WHITE, and that is the existing
 * damage rule showing through rather than a fault here.
 * UCataclysmSkillEffects::DamageTypeOf types an enemy's damage and not a
 * player's, because an enemy has one generic resistance and nothing to choose
 * between. game/Data/WeaponSkills.csv does give each player skill a DamageType,
 * and nothing carries it as far as an effect. Issue #803.
 */
UCLASS()
class CATACLYSM_API UCataclysmProjectileEffect : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The system built for this, in issue #558's build order at step 6. */
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
	 * Cataclysm.Effects.ProjectileBodyReadsItsColoursFromTheTable is what
	 * notices.
	 */
	static const FName ElementColourParameter;
	static const FName ElementColourDarkParameter;
	static const FName ScaleParameter;

	/**
	 * What `User.Scale` is set to for a projectile of this body width.
	 *
	 * THE ASSET MULTIPLIES IT BY A CENTIMETRE FIGURE rather than treating it as
	 * a component scale, which is the rule Diablo IV's visual effects lead
	 * states and docs/Niagara_Conventions.md section 5 repeats: intensity and
	 * size drive spawn rate, velocity and sprite size, never the component's
	 * own transform. So this is the projectile's radius in metres, and the head
	 * sprite comes out at 2.75 times it.
	 *
	 * IT FOLLOWS THE MESH THE PLAYER ALREADY SEES. ACataclysmProjectile sizes
	 * its placeholder sphere to BodyRadiusCm too, so the glow stays
	 * proportionate to the object rather than being a fixed size that happens to
	 * suit one skill. A piercing skill's projectile is as wide as the line it
	 * hits along, which is metres rather than centimetres, and its head is
	 * correspondingly large -- that is the object being large, not the effect
	 * disagreeing with it.
	 *
	 * CLAMPED AT BOTH ENDS, AND THE BOUNDS ARE GUARDS RATHER THAN DESIGN. The
	 * design states no body radius anywhere, so nothing stops a future data row
	 * asking for zero, which would draw an invisible head, or for a hundred
	 * metres, which would fill the screen. Neither bound is a number anything
	 * designed asks for.
	 *
	 * SEPARATE AND STATIC SO A TEST CAN REACH IT. Nothing here needs a world, a
	 * component or a rendering device, so the automation harness can exercise it
	 * -- which issue #559 records it cannot do for the spawn itself.
	 */
	static float ScaleFor(float BodyRadiusCm);

	/** The smallest and largest `User.Scale` a projectile may ask for. */
	static constexpr float MinimumScale = 0.2f;
	static constexpr float MaximumScale = 4.0f;

	/**
	 * Which damage type a projectile's effect draws in.
	 *
	 * IT IS THE FIRER'S TYPE AND NOT THE PROJECTILE'S, because a projectile has
	 * none of its own: ACataclysmProjectile::Fire is given the firing skill's
	 * tags, and the damage type is not among them. It reaches a defender as a
	 * dynamic asset tag added by UCataclysmSkillEffects::ApplyTypedSpec, worked
	 * out from the attacker at the moment of the hit. This asks the same
	 * question of the same actor, so a projectile's colour and a projectile's
	 * resistance check can never disagree.
	 *
	 * Returns NAME_None for a player's projectile and for a null one, and
	 * NAME_None means "draw the authored default", not "something went wrong".
	 */
	static FName DamageTypeFor(const ACataclysmProjectile* Projectile);

	/**
	 * Gives a projectile its head and its trail, and colours them.
	 *
	 * Returns the component so a caller can inspect what it was given. Null when
	 * there is no projectile, no world, the system asset is missing, or the
	 * effect type's scalability rejected the spawn.
	 *
	 * THAT LAST ONE IS NOT A FAILURE. It is FXT_Enemy working: past 4000 cm, or
	 * outside the view frustum, or beyond twenty live instances of this system,
	 * the effect is refused before it starts. That is the whole reason the
	 * effect type exists.
	 *
	 * WHAT A REFUSAL COSTS GOT BIGGER ON 2026-08-22. It used to leave the
	 * projectile crossing the room as a bare sphere. The constructor no longer
	 * loads that sphere -- it had no material and drew in Unreal's default grey,
	 * which is issue #811 -- so a spell whose effect is refused now draws
	 * nothing at all. That is the right trade at 4000 cm and off screen, and it
	 * is worth knowing.
	 *
	 * IT ALSO RETURNS NULL IN EVERY AUTOMATION TEST, and that is a property of
	 * the harness rather than of this code. Niagara's CreateNiagaraSystem checks
	 * FApp::CanEverRender() before it does anything at all, and the test command
	 * in tools/unreal_build.py passes -nullrhi. So no automation test can
	 * observe a spawned component, and none of them try. Issue #559 is what
	 * would be needed to cover the spawn itself.
	 *
	 * THE EFFECT DIES WITH THE PROJECTILE, sparks and all, because it is
	 * attached to it. A trail that outlived its projectile would need the
	 * component detached and left to finish on its own, and the moment a
	 * projectile stops is the moment the impact burst goes off in the same
	 * place, so there is nothing to see through.
	 */
	static UNiagaraComponent* AttachTo(ACataclysmProjectile* Projectile);

	/**
	 * How many times a projectile has asked for its body since the editor
	 * started.
	 *
	 * IT EXISTS BECAUSE NO TEST IN THIS PROJECT CAN SEE A NIAGARA SPAWN, as the
	 * comment on `AttachTo` above already explains at length. Issue #559.
	 *
	 * WITHOUT IT NOTHING CONNECTED THIS TO THE GAME. Until 2026-08-22 the only
	 * test standing between a fired projectile and an invisible one asserted
	 * that the placeholder component had a static mesh -- which passed because
	 * the constructor loaded an engine sphere, not because this function was
	 * ever called. Delete the `AttachTo` line from `ACataclysmProjectile::Fire`
	 * and the whole suite stayed green.
	 * `Cataclysm.AI.AFiredProjectileHasSomethingOnScreenToSee` reads this now.
	 */
	static int32 TimesAsked;
};
