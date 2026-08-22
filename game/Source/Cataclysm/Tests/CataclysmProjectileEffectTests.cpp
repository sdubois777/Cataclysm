// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmElementVisuals.h"
#include "AbilitySystem/CataclysmImpactEffect.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmProjectileEffect.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"
#include "NiagaraEffectType.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * NS_Proj_Body: what a projectile looks like while it is in the air.
 *
 * WHAT THESE ARE GUARDING AGAINST is the same class of silent failure the
 * impact burst's tests describe. Niagara ignores a parameter a system does not
 * expose -- no warning, no error, nothing in the log -- so a misspelled or
 * renamed parameter leaves code that compiles, runs, spawns an effect and never
 * changes its colour.
 *
 * AND ONE FAILURE THAT IS SPECIFIC TO THIS SHAPE. A trail is a trail only
 * because its particles are simulated in WORLD space and therefore stay where
 * they were born while the projectile moves on. Its head rides the projectile
 * only because that emitter is in LOCAL space. Those two flags are one checkbox
 * each in the editor, they are invisible in a binary asset, and swapping them
 * gives a head that falls behind and a trail that never trails. Nothing else in
 * the project would notice.
 *
 * NOTHING HERE SPAWNS THE EFFECT. Niagara's CreateNiagaraSystem checks
 * FApp::CanEverRender() before doing anything at all and the automation command
 * in tools/unreal_build.py passes -nullrhi, so a spawned component cannot be
 * observed by any test in this project. Issue #559. What can be checked is the
 * asset, and the arithmetic and the lookups that feed it.
 */

namespace CataclysmProjectileEffectTest
{
	UNiagaraSystem* LoadProjectileSystem()
	{
		return LoadObject<UNiagaraSystem>(
			nullptr, UCataclysmProjectileEffect::SystemAssetPath);
	}

	/** The named emitter's handle, or null. */
	const FNiagaraEmitterHandle* EmitterNamed(const UNiagaraSystem* System,
											  const TCHAR* Name)
	{
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (Handle.GetName() == FName(Name))
			{
				return &Handle;
			}
		}
		return nullptr;
	}
}

// --------------------------------------------------------------------------
// The asset itself
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileBodySetsItsEffectType,
	"Cataclysm.Effects.ProjectileBodySetsTheEnemyEffectType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileBodySetsItsEffectType::RunTest(const FString& Parameters)
{
	UNiagaraSystem* System = CataclysmProjectileEffectTest::LoadProjectileSystem();
	if (!System)
	{
		AddError(FString::Printf(
			TEXT("NS_Proj_Body does not exist at %s."),
			UCataclysmProjectileEffect::SystemAssetPath));
		return false;
	}

	// A system with no effect type is culled by nothing whatsoever. Niagara
	// ships no default effect type and no project-wide one, so "unset" means a
	// floor of creatures firing produces uncapped, undistanced systems.
	// docs/Niagara_Conventions.md section 4 says such a system "is not
	// reviewable and should not be committed".
	const UNiagaraEffectType* Type = System->GetEffectType();
	if (!Type)
	{
		AddError(TEXT("NS_Proj_Body has no effect type, so nothing culls it by "
					  "distance or by instance count."));
		return false;
	}

	// FXT_Enemy AND NOT FXT_PlayerSkill, and the choice is worth stating because
	// this one asset serves both sides: ten designed player projectile skills
	// and four creatures that fire. FXT_Enemy is the tighter of the two on
	// distance (4000 cm against 6000) and the looser on instance count (60
	// against 40), which is the safer pair for something that can be numerous.
	// NEITHER NUMBER HAS A MEASUREMENT BEHIND IT -- issue #547.
	TestEqual(TEXT("NS_Proj_Body uses FXT_Enemy"),
		Type->GetName(), FString(TEXT("FXT_Enemy")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileBodyExposesTheStandardBlock,
	"Cataclysm.Effects.ProjectileBodyExposesTheStandardParameterBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileBodyExposesTheStandardBlock::RunTest(const FString& Parameters)
{
	UNiagaraSystem* System = CataclysmProjectileEffectTest::LoadProjectileSystem();
	if (!System)
	{
		AddError(TEXT("NS_Proj_Body does not exist."));
		return false;
	}

	// Collected as full names, which is how the store holds them: Niagara
	// prefixes every user parameter with `User.` itself.
	TSet<FString> Exposed;
	for (const FNiagaraVariableWithOffset& Variable :
			System->GetExposedParameters().ReadParameterVariables())
	{
		Exposed.Add(Variable.GetName().ToString());
	}

	// THE NAMES ARE IDENTICAL ACROSS EVERY SYSTEM ON PURPOSE. A skill row sets
	// them without knowing which system it spawned, so the moment one system
	// calls it Colour and another calls it Tint the data-driven path is dead.
	// docs/Niagara_Conventions.md section 2 fixes this list, and a system
	// exposes all of it whether or not it uses every entry.
	const TCHAR* Required[] = {
		TEXT("User.ElementColour"),
		TEXT("User.ElementColourDark"),
		TEXT("User.Intensity"),
		TEXT("User.Scale"),
		TEXT("User.Duration"),
		TEXT("User.ImpactNormal"),
		TEXT("User.TargetPosition"),
	};

	for (const TCHAR* Name : Required)
	{
		TestTrue(FString::Printf(
			TEXT("NS_Proj_Body exposes %s. Without it, setting that parameter "
				 "does nothing at all and reports nothing."), Name),
			Exposed.Contains(FString(Name)));
	}

	// The three the code actually writes, checked against the constants the code
	// writes them with rather than against a second copy of the strings. A
	// rename in one place and not the other is exactly the silent break.
	const TPair<const TCHAR*, FName> Written[] = {
		{ TEXT("the primary colour"),
		  UCataclysmProjectileEffect::ElementColourParameter },
		{ TEXT("the dark colour"),
		  UCataclysmProjectileEffect::ElementColourDarkParameter },
		{ TEXT("the size"), UCataclysmProjectileEffect::ScaleParameter },
	};
	for (const TPair<const TCHAR*, FName>& Pair : Written)
	{
		TestTrue(FString::Printf(
			TEXT("the parameter name the code writes for %s is one the asset "
				 "exposes"), Pair.Key),
			Exposed.Contains(FString(TEXT("User.")) + Pair.Value.ToString()));
	}

	// AND THE TWO EFFECT CLASSES AGREE, which is the point of the shared
	// spellings in CataclysmEffectParameterNames. If these ever differ, one of
	// the two systems is being set with a name it does not expose and is drawing
	// its authored default in silence.
	TestEqual(TEXT("the impact burst and the projectile write the same primary "
				   "colour parameter name"),
		UCataclysmProjectileEffect::ElementColourParameter,
		UCataclysmImpactEffect::ElementColourParameter);
	TestEqual(TEXT("and the same dark colour parameter name"),
		UCataclysmProjectileEffect::ElementColourDarkParameter,
		UCataclysmImpactEffect::ElementColourDarkParameter);

	return true;
}

// --------------------------------------------------------------------------
// The one thing about this shape that nothing else would notice
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileHeadRidesAndTrailStaysBehind,
	"Cataclysm.Effects.ProjectileHeadRidesAndTrailStaysBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileHeadRidesAndTrailStaysBehind::RunTest(const FString& Parameters)
{
	using namespace CataclysmProjectileEffectTest;

	UNiagaraSystem* System = LoadProjectileSystem();
	if (!System)
	{
		AddError(TEXT("NS_Proj_Body does not exist."));
		return false;
	}

	// THREE AND EXACTLY THREE. Counted rather than merely looked up by name,
	// because a system created from an engine template arrives carrying that
	// template's own emitter. This system did: the Niagara stack reported no
	// error, no warning and a clean compile with a stray `Fountain` emitter in
	// it, and the only thing that said so was counting.
	TestEqual(TEXT("NS_Proj_Body has three emitters and no leftovers"),
		System->GetEmitterHandles().Num(), 3);

	const FNiagaraEmitterHandle* Head = EmitterNamed(System, TEXT("Core"));
	const FNiagaraEmitterHandle* Trail = EmitterNamed(System, TEXT("Trail"));
	const FNiagaraEmitterHandle* Streak = EmitterNamed(System, TEXT("Streak"));
	if (!Head || !Trail || !Streak)
	{
		AddError(TEXT("NS_Proj_Body must have emitters named Core, Trail and "
					  "Streak."));
		return false;
	}

	const FVersionedNiagaraEmitterData* HeadData = Head->GetEmitterData();
	const FVersionedNiagaraEmitterData* TrailData = Trail->GetEmitterData();
	const FVersionedNiagaraEmitterData* StreakData = Streak->GetEmitterData();
	if (!HeadData || !TrailData || !StreakData)
	{
		AddError(TEXT("an emitter of NS_Proj_Body carries no data."));
		return false;
	}

	// THE HEAD IS IN LOCAL SPACE so its one sprite rides the projectile.
	TestTrue(TEXT("the head is simulated in local space, so it stays on the "
				  "projectile instead of being left at the launch point"),
		HeadData->bLocalSpace);

	// THE TRAIL IS IN WORLD SPACE so each spark stays where it was born. In
	// local space every spark would be dragged along with the projectile and
	// there would be no trail at all -- just a denser head.
	TestFalse(TEXT("the trail is simulated in world space, so its sparks are "
				   "left behind rather than dragged along"),
		TrailData->bLocalSpace);

	// AND SO IS THE STREAK, for the same reason and more strongly: a ribbon
	// joins its particles in the order they were born, so one simulated in local
	// space collapses into a stub at the projectile rather than stretching out
	// behind it.
	TestFalse(TEXT("the streak is simulated in world space, so the ribbon "
				   "stretches out behind the projectile"),
		StreakData->bLocalSpace);

	TestTrue(TEXT("all three emitters are enabled"),
		Head->GetIsEnabled() && Trail->GetIsEnabled() && Streak->GetIsEnabled());

	// THE MATERIALS, BECAUSE A MISSING ONE IS INVISIBLE RATHER THAN LOUD. A
	// material without the matching Niagara usage flag makes the renderer fall
	// back to the engine default in silence, which is what happened to
	// NS_Impact_Point while it was being built. The head and the sparks share
	// one sprite material so that re-pointing the texture is a single edit; the
	// streak needs a ribbon material and uses the trail material out of the
	// installed pack.
	for (const TPair<const TCHAR*, const FVersionedNiagaraEmitterData*> Pair :
			{ TPair<const TCHAR*, const FVersionedNiagaraEmitterData*>(
				  TEXT("Core"), HeadData),
			  TPair<const TCHAR*, const FVersionedNiagaraEmitterData*>(
				  TEXT("Trail"), TrailData) })
	{
		bool bFoundTheSharedMaterial = false;
		for (const UNiagaraRendererProperties* Renderer : Pair.Value->GetRenderers())
		{
			const auto* Sprites =
				Cast<UNiagaraSpriteRendererProperties>(Renderer);
			if (Sprites && Sprites->Material &&
				Sprites->Material->GetName() == TEXT("M_Impact_Sprite"))
			{
				bFoundTheSharedMaterial = true;
			}
		}
		TestTrue(FString::Printf(
			TEXT("%s draws with M_Impact_Sprite rather than the engine's "
				 "default sprite material"), Pair.Key),
			bFoundTheSharedMaterial);
	}

	// THE STREAK IS A RIBBON AND NOT A LINE OF SPRITES, which is the difference
	// between a continuous streak and a dotted one. A ribbon renderer is a
	// different class from a sprite renderer, so this cannot be satisfied by an
	// emitter that merely draws something.
	//
	// ITS MATERIAL COMES OUT OF THE INSTALLED PACK and is therefore absent on a
	// fresh clone, exactly as the Paragon meshes are. The reference is checked
	// rather than the asset's contents, so this still says something when the
	// pack is not installed: a renderer whose material was never assigned
	// carries null and would fail here.
	bool bFoundARibbon = false;
	for (const UNiagaraRendererProperties* Renderer : StreakData->GetRenderers())
	{
		const auto* Ribbon = Cast<UNiagaraRibbonRendererProperties>(Renderer);
		if (Ribbon && Ribbon->Material)
		{
			bFoundARibbon = true;
			TestEqual(TEXT("the streak draws with the pack's trail material"),
				Ribbon->Material->GetName(), FString(TEXT("MI_Basic_trail05")));
		}
	}
	TestTrue(TEXT("the streak is drawn by a ribbon renderer, so it is a "
				  "continuous streak rather than a line of separate sprites"),
		bFoundARibbon);

	// A LIGHT ON THE HEAD, which is what makes a bolt light the floor it passes
	// over rather than being a bright shape pasted on top of an unlit room. It
	// is the one part of this effect that changes anything outside itself.
	bool bFoundALight = false;
	for (const UNiagaraRendererProperties* Renderer : HeadData->GetRenderers())
	{
		if (Cast<UNiagaraLightRendererProperties>(Renderer))
		{
			bFoundALight = true;
		}
	}
	TestTrue(TEXT("the head carries a light renderer, so the projectile lights "
				  "what it flies past"),
		bFoundALight);

	return true;
}

// --------------------------------------------------------------------------
// The arithmetic
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileScaleFollowsTheBody,
	"Cataclysm.Effects.ProjectileEffectSizeFollowsTheBodyWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileScaleFollowsTheBody::RunTest(const FString& Parameters)
{
	// ANCHORED TO THE PROJECTILE'S OWN CONSTANT AND TO A WRITTEN NUMBER, not to
	// anything UCataclysmProjectileEffect owns. A test that divides by the same
	// hundred the code divides by could not notice the hundred being wrong.
	// ACataclysmProjectile::DefaultBodyRadiusCm is 40, so the standard
	// projectile asks for 0.4 -- forty centimetres expressed in metres.
	TestEqual(TEXT("the standard projectile asks for its radius in metres"),
		UCataclysmProjectileEffect::ScaleFor(
			ACataclysmProjectile::DefaultBodyRadiusCm), 0.4f);

	// A PIERCING SKILL'S PROJECTILE IS AS WIDE AS THE LINE IT HITS ALONG.
	// Emberhurl, Chain of Coals, Hellbrand and Infernal Lance are all written
	// Radius=1.5 in game/Data/WeaponSkills.csv, which is 150 centimetres, and
	// ACataclysmProjectile::Fire gives such a projectile that as its body width.
	// The effect has to follow the object rather than stay a fixed size.
	TestEqual(TEXT("a metre and a half of piercing projectile asks for 1.5"),
		UCataclysmProjectileEffect::ScaleFor(150.0f), 1.5f);
	TestTrue(TEXT("and is not clipped by the upper guard"),
		1.5f < UCataclysmProjectileEffect::MaximumScale);

	// THE GUARDS ARE GUARDS AND NOT DESIGN. Nothing states a body radius
	// anywhere in the design, so nothing stops a future row asking for zero.
	// What matters is not the value of the floor but that the answer stays
	// above zero, because a scale of zero draws nothing at all.
	TestTrue(TEXT("a projectile with no width still draws something"),
		UCataclysmProjectileEffect::ScaleFor(0.0f) > 0.0f);
	TestEqual(TEXT("and takes the floor"),
		UCataclysmProjectileEffect::ScaleFor(0.0f),
		UCataclysmProjectileEffect::MinimumScale);
	TestEqual(TEXT("a hundred metres of projectile takes the ceiling"),
		UCataclysmProjectileEffect::ScaleFor(10000.0f),
		UCataclysmProjectileEffect::MaximumScale);

	return true;
}

// --------------------------------------------------------------------------
// The colour, which comes from whoever fired
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileTakesItsColourFromTheFirer,
	"Cataclysm.Effects.ProjectileTakesItsColourFromWhoFiredIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileTakesItsColourFromTheFirer::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("nothing fired nothing, and that is not a damage type"),
		UCataclysmProjectileEffect::DamageTypeFor(nullptr), FName(NAME_None));

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// NOT THE DEFAULT DAMAGE TYPE. ACataclysmEnemyCharacter::DamageType is
	// Demonic out of the box, so asserting Demonic would pass just as well if
	// the value never travelled at all.
	ACataclysmEnemyCharacter* Firer =
		World->SpawnActor<ACataclysmEnemyCharacter>();
	if (!TestNotNull(TEXT("a creature to fire it"), Firer))
	{
		return false;
	}
	Firer->DamageType = FName(TEXT("Void"));

	ACataclysmProjectile* Bolt = ACataclysmProjectile::Fire(
		Firer, FVector::ZeroVector, FVector(1000.0f, 0.0f, 0.0f),
		/*InRadiusCm=*/50.0f, /*InSpeed=*/1200.0f, /*InPierce=*/0,
		/*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FGameplayTagContainer(), /*bInBurns=*/false);
	if (!TestNotNull(TEXT("a projectile is fired"), Bolt))
	{
		return false;
	}

	TestEqual(TEXT("the projectile draws in the damage type of whoever fired it"),
		UCataclysmProjectileEffect::DamageTypeFor(Bolt), FName(TEXT("Void")));

	// AND THAT ANSWER HAS TO REACH A COLOUR, or the whole chain is a name that
	// matches nothing. This is the half the damage type lookup cannot check on
	// its own.
	FLinearColor Primary;
	FLinearColor Secondary;
	TestTrue(TEXT("and that damage type has a row to draw with"),
		UCataclysmElementVisuals::ColoursFor(
			UCataclysmProjectileEffect::DamageTypeFor(Bolt), Primary, Secondary));
	TestFalse(TEXT("which is not the system's white default"),
		Primary.Equals(FLinearColor::White, 1.0e-6f));

	// A PLAYER'S PROJECTILE IS UNTYPED, and that is the damage rule showing
	// through rather than a fault here: only an enemy's damage carries a type.
	// A plain actor stands in for the player, because that is what
	// UCataclysmSkillEffects::DamageTypeOf actually distinguishes -- an
	// ACataclysmEnemyCharacter from anything else. Issue #803 is the gap.
	AActor* Player = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("something that is not a creature"), Player))
	{
		return false;
	}

	ACataclysmProjectile* PlayerBolt = ACataclysmProjectile::Fire(
		Player, FVector::ZeroVector, FVector(1000.0f, 0.0f, 0.0f),
		/*InRadiusCm=*/50.0f, /*InSpeed=*/1200.0f, /*InPierce=*/0,
		/*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FGameplayTagContainer(), /*bInBurns=*/false);
	if (!TestNotNull(TEXT("the player's projectile is fired"), PlayerBolt))
	{
		return false;
	}

	TestEqual(TEXT("a projectile nobody typed draws the authored default"),
		UCataclysmProjectileEffect::DamageTypeFor(PlayerBolt), FName(NAME_None));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
