// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCastEffect.h"
#include "AbilitySystem/CataclysmElementVisuals.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"
#include "NiagaraSystem.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * THE BEAT THAT WAS MISSING FROM EVERY SKILL.
 *
 * A skill used to begin with nothing happening at the caster: the body of the
 * bolt and the burst where it landed both existed, and the third of the three
 * -- the flash at the caster -- had never been built. Issue #811, and
 * `docs/Niagara_Conventions.md` section 5A for why three is the number.
 *
 * WHAT THESE CAN AND CANNOT CHECK. No test in this project can see a Niagara
 * spawn: the harness runs with `-nullrhi` and Niagara refuses to create a
 * component when `FApp::CanEverRender()` is false, which is issue #559. So the
 * asset, the arithmetic and the fact that the call happens are what is testable,
 * and whether it looks right is judged in the editor instead.
 */
namespace CataclysmCastEffectTest
{
	static FGameplayTag TagNamed(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Name), /*ErrorIfNotFound=*/false);
	}
}

// --------------------------------------------------------------------------
// The asset
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCastEffectAssetExists,
	"Cataclysm.Effects.TheCastBurstAssetLoadsAndCarriesTheStandardBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCastEffectAssetExists::RunTest(const FString&)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(
		nullptr, UCataclysmCastEffect::SystemAssetPath);
	if (!TestNotNull(TEXT("NS_Cast_Windup loads"), System))
	{
		return false;
	}

	// THE PARAMETER NAMES THE C++ SETS MUST BE THE ONES THE ASSET EXPOSES, and
	// nothing else checks that. A renamed user parameter would leave every
	// SetVariable call silently doing nothing: the spawn still succeeds, the
	// burst still plays, and it plays white for ever.
	const FNiagaraUserRedirectionParameterStore& Store =
		System->GetExposedParameters();
	TArray<FNiagaraVariable> Exposed;
	Store.GetParameters(Exposed);

	TSet<FName> Names;
	for (const FNiagaraVariable& Variable : Exposed)
	{
		Names.Add(Variable.GetName());
	}

	for (const FName Wanted : {UCataclysmCastEffect::ElementColourParameter,
							   UCataclysmCastEffect::ElementColourDarkParameter,
							   UCataclysmCastEffect::ScaleParameter})
	{
		TestTrue(*FString::Printf(TEXT("the system exposes %s"),
								  *Wanted.ToString()),
				 Names.Contains(Wanted)
					 || Names.Contains(FName(*(TEXT("User.")
											   + Wanted.ToString()))));
	}

	return true;
}

// --------------------------------------------------------------------------
// How big it is
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCastEffectScale,
	"Cataclysm.Effects.ACastBurstGrowsWithTheSkillButNotWithoutLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCastEffectScale::RunTest(const FString&)
{
	// ONE METRE OF RADIUS IS ONE UNIT OF SCALE, so a two metre skill is twice a
	// one metre skill and the authored size is what a one metre skill gets.
	TestEqual(TEXT("a one metre skill draws the authored size"),
		UCataclysmCastEffect::ScaleFor(100.0f), 1.0f, 0.001f);
	TestEqual(TEXT("a two metre skill draws twice that"),
		UCataclysmCastEffect::ScaleFor(200.0f), 2.0f, 0.001f);

	// A SELF BUFF STATES NO RADIUS AT ALL and must still light the caster.
	// Before the clamp this returned zero and the burst would have been drawn at
	// no size, which is the same as not drawing it.
	TestEqual(TEXT("a skill with no radius takes the minimum"),
		UCataclysmCastEffect::ScaleFor(0.0f),
		UCataclysmCastEffect::MinimumScale, 0.001f);
	TestEqual(TEXT("and so does a negative one"),
		UCataclysmCastEffect::ScaleFor(-500.0f),
		UCataclysmCastEffect::MinimumScale, 0.001f);

	// THE UPPER CLAMP IS THE POINT OF THIS SHAPE. A muzzle sits on the caster's
	// own body and is seen at arm's length every time a button is pressed.
	// Pyroclasm covers five metres; five times the authored size on the player
	// would fill the screen.
	TestEqual(TEXT("a five metre skill is capped"),
		UCataclysmCastEffect::ScaleFor(500.0f),
		UCataclysmCastEffect::MaximumScale, 0.001f);
	TestTrue(TEXT("and the cap is tighter than the ground zone's, which is 12"),
		UCataclysmCastEffect::MaximumScale < 12.0f);

	return true;
}

// --------------------------------------------------------------------------
// What colour it draws in
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCastEffectColour,
	"Cataclysm.Effects.ACastBurstTakesItsColourFromTheCasterThenTheSkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCastEffectColour::RunTest(const FString&)
{
	using namespace CataclysmCastEffectTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A PLAYER STANDS IN AS A PLAIN ACTOR, which is what
	// UCataclysmSkillEffects::DamageTypeOf sees: anything that is not an
	// ACataclysmEnemyCharacter carries no damage type.
	AActor* Player = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("a caster"), Player))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Player)) { Player->Destroy(); } };

	TestEqual(TEXT("a player's cast takes the skill's own damage type"),
		UCataclysmCastEffect::DamageTypeFor(Player,
											TagNamed(TEXT("Element.Demonic"))),
		FName(TEXT("Demonic")));

	TestEqual(TEXT("and a different skill is a different colour"),
		UCataclysmCastEffect::DamageTypeFor(Player,
											TagNamed(TEXT("Element.War"))),
		FName(TEXT("War")));

	TestEqual(TEXT("a skill naming no damage type names none"),
		UCataclysmCastEffect::DamageTypeFor(Player, FGameplayTag()),
		FName(NAME_None));

	// THE CASTER WINS OVER THE SKILL, which is the order the strike arc and the
	// projectile body already use. Nothing an enemy does reaches this shape
	// today, and the three answering differently is how they would drift.
	ACataclysmEnemyCharacter* Enemy = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an enemy"), Enemy))
	{
		Enemy->DamageType = FName(TEXT("Void"));
		TestEqual(TEXT("an enemy's cast is its own type, not the skill's"),
			UCataclysmCastEffect::DamageTypeFor(
				Enemy, TagNamed(TEXT("Element.Demonic"))),
			FName(TEXT("Void")));
		Enemy->Destroy();
	}

	return true;
}

// --------------------------------------------------------------------------
// Which way it points
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCastEffectFacing,
	"Cataclysm.Effects.ACastBurstPointsWhereTheSkillWasAimed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCastEffectFacing::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("a caster"), Caster))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Caster)) { Caster->Destroy(); } };

	// A ROOT COMPONENT FIRST. A bare AActor has none, and SetActorRotation on an
	// actor with no root silently does nothing: the rotation reads back as zero
	// and the test below would be checking the fallback twice.
	USceneComponent* Root = NewObject<USceneComponent>(Caster);
	Root->RegisterComponent();
	Caster->SetRootComponent(Root);
	Caster->SetActorRotation(FRotator(0.0, 145.0, 0.0));

	if (!TestEqual(TEXT("the caster really is turned"),
				   Caster->GetActorRotation().Yaw, 145.0, 0.01))
	{
		return false;
	}

	// DOUBLE, NOT FLOAT. FRotator's components are double in Unreal 5 and mixing
	// the two makes TestEqual ambiguous and stops the module compiling.
	const FRotator East =
		UCataclysmCastEffect::FacingFor(FVector(1.0f, 0.0f, 0.0f), Caster);
	TestEqual(TEXT("aiming east points east"), East.Yaw, 0.0);

	const FRotator North =
		UCataclysmCastEffect::FacingFor(FVector(0.0f, 1.0f, 0.0f), Caster);
	TestEqual(TEXT("aiming north points north"), North.Yaw, 90.0);

	// FLATTENED. The camera looks down at 60 degrees, so a burst tipped at the
	// sky presents its edge and is nearly invisible.
	const FRotator Steep =
		UCataclysmCastEffect::FacingFor(FVector(1.0f, 0.0f, 4.0f), Caster);
	TestEqual(TEXT("an aim well above the ground is flattened"),
		Steep.Pitch, 0.0);
	TestEqual(TEXT("and keeps its direction"), Steep.Yaw, 0.0);

	// A SELF BUFF AIMS AT NOTHING. AimDirection answers zero for it, and the
	// caster's own facing is both the only answer available and the right one.
	const FRotator Buff =
		UCataclysmCastEffect::FacingFor(FVector::ZeroVector, Caster);
	TestEqual(TEXT("a skill aimed at nothing uses the caster's own facing"),
		Buff.Yaw, 145.0, 0.01);

	TestEqual(TEXT("and with no caster either it points along the world axis"),
		UCataclysmCastEffect::FacingFor(FVector::ZeroVector, nullptr).Yaw, 0.0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
