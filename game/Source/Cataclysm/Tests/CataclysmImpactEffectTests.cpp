// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmElementVisuals.h"
#include "AbilitySystem/CataclysmImpactEffect.h"
#include "AbilitySystemComponent.h"
#include "Components/SceneComponent.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraEffectType.h"
#include "NiagaraSystem.h"

/**
 * NS_Impact_Point, and the chain that colours it from the damage type table.
 *
 * WHAT THESE ARE GUARDING AGAINST is a whole class of silent failure. Niagara
 * ignores a parameter a system does not expose -- no warning, no error, nothing
 * in the log -- so misspelling a parameter name, or renaming one in the asset,
 * leaves code that compiles, runs, spawns an effect and never changes its
 * colour. Nothing about that looks broken until somebody notices every damage
 * type is the same shade. Every check below exists because its failure mode is
 * invisible rather than loud.
 */

namespace CataclysmImpactEffectTest
{
	const TCHAR* EveryDamageType[] = {
		TEXT("War"), TEXT("Demonic"), TEXT("Death"), TEXT("Pestilence"),
		TEXT("Famine"), TEXT("Celestial"), TEXT("Chaos"), TEXT("Void"),
	};

	UNiagaraSystem* LoadImpactSystem()
	{
		return LoadObject<UNiagaraSystem>(
			nullptr, UCataclysmImpactEffect::SystemAssetPath);
	}
}

// --------------------------------------------------------------------------
// The asset itself
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpactPointSetsItsEffectType,
	"Cataclysm.Effects.ImpactPointSetsTheEnemyEffectType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpactPointSetsItsEffectType::RunTest(const FString& Parameters)
{
	UNiagaraSystem* System = CataclysmImpactEffectTest::LoadImpactSystem();
	if (!System)
	{
		AddError(FString::Printf(
			TEXT("NS_Impact_Point does not exist at %s."),
			UCataclysmImpactEffect::SystemAssetPath));
		return false;
	}

	// A system with no effect type is culled by nothing whatsoever. Niagara
	// ships no default effect type and no project-wide one, so "unset" means
	// twenty Brutes attacking produce twenty uncapped, undistanced systems.
	// docs/Niagara_Conventions.md section 4 says such a system "is not
	// reviewable and should not be committed".
	const UNiagaraEffectType* Type = System->GetEffectType();
	if (!Type)
	{
		AddError(TEXT("NS_Impact_Point has no effect type, so nothing culls it "
					  "by distance or by instance count."));
		return false;
	}

	TestEqual(TEXT("NS_Impact_Point uses FXT_Enemy"),
		Type->GetName(), FString(TEXT("FXT_Enemy")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpactPointExposesTheStandardBlock,
	"Cataclysm.Effects.ImpactPointExposesTheStandardParameterBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpactPointExposesTheStandardBlock::RunTest(const FString& Parameters)
{
	UNiagaraSystem* System = CataclysmImpactEffectTest::LoadImpactSystem();
	if (!System)
	{
		AddError(TEXT("NS_Impact_Point does not exist."));
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
	// docs/Niagara_Conventions.md section 2 fixes this list.
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
			TEXT("NS_Impact_Point exposes %s. Without it, setting that "
				 "parameter does nothing at all and reports nothing."), Name),
			Exposed.Contains(FString(Name)));
	}

	// The two the code actually writes, checked against the constants the code
	// writes them with rather than against a second copy of the strings. A
	// rename in one place and not the other is exactly the silent break.
	TestTrue(TEXT("the parameter name the code writes for the primary colour is "
				  "one the asset exposes"),
		Exposed.Contains(FString(TEXT("User.")) +
			UCataclysmImpactEffect::ElementColourParameter.ToString()));
	TestTrue(TEXT("the parameter name the code writes for the dark colour is "
				  "one the asset exposes"),
		Exposed.Contains(FString(TEXT("User.")) +
			UCataclysmImpactEffect::ElementColourDarkParameter.ToString()));

	return true;
}

// --------------------------------------------------------------------------
// The lookup
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpactColoursComeFromTheTable,
	"Cataclysm.Effects.ImpactColoursComeFromTheDamageTypeTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpactColoursComeFromTheTable::RunTest(const FString& Parameters)
{
	const UDataTable* Table = UCataclysmElementVisuals::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ElementVisuals does not exist."));
		return false;
	}

	int32 Checked = 0;
	for (const TCHAR* TypeName : CataclysmImpactEffectTest::EveryDamageType)
	{
		const FName DamageType(TypeName);

		const FCataclysmElementVisualRow* Row =
			Table->FindRow<FCataclysmElementVisualRow>(
				DamageType, TEXT("impact effect test"), /*bWarnIfMissing=*/false);
		if (!Row)
		{
			AddError(FString::Printf(
				TEXT("DT_ElementVisuals has no row named %s."), TypeName));
			continue;
		}

		FLinearColor Primary;
		FLinearColor Secondary;
		if (!UCataclysmElementVisuals::ColoursFor(DamageType, Primary, Secondary))
		{
			AddError(FString::Printf(
				TEXT("a %s hit found no colours, so it would draw the "
					 "system's own white default."), TypeName));
			continue;
		}

		// Read straight off the row rather than against colours copied into
		// this file. The palette is pinned to the design document by
		// tools/tests/test_element_visuals_match_the_design.py; what this test
		// asks is a different question -- whether the effect gets what the
		// table says -- and a second copy of the numbers here would answer
		// neither question well.
		TestTrue(FString::Printf(TEXT("%s takes its primary from its row"),
			TypeName), Primary.Equals(Row->PrimaryColour, 1.0e-6f));
		TestTrue(FString::Printf(TEXT("%s takes its secondary from its row"),
			TypeName), Secondary.Equals(Row->SecondaryColour, 1.0e-6f));

		++Checked;
	}

	// Without this the loop above passes on an empty table, which is what a
	// stale or unbuilt asset actually looks like.
	TestEqual(TEXT("every damage type was checked"), Checked, 8);

	// An untyped hit is a normal case and must report itself as one, because
	// that is what leaves the system's authored default in place instead of
	// writing a colour nobody chose.
	FLinearColor Unused;
	FLinearColor AlsoUnused;
	TestFalse(TEXT("an untyped hit finds no row"),
		UCataclysmElementVisuals::ColoursFor(NAME_None, Unused, AlsoUnused));

	return true;
}

// --------------------------------------------------------------------------
// The types, which are the other half of a name matching
// --------------------------------------------------------------------------

/**
 * A parameter can carry the right name and still refuse the value.
 *
 * SetVariableLinearColor writes into a store keyed by name AND type. A
 * parameter called ElementColour that is a float, because somebody rebuilt the
 * asset and picked the wrong type in the dropdown, matches the name check above
 * and silently discards every colour written to it. Nothing reports that
 * either, so it is checked here.
 *
 * WHAT THIS FILE CANNOT CHECK, stated plainly rather than left to be assumed:
 * no test here spawns the effect. Niagara's CreateNiagaraSystem returns null
 * unless FApp::CanEverRender(), and tools/unreal_build.py runs the automation
 * tests with -nullrhi, so a spawned component cannot exist in this harness at
 * all. That was established by reading NiagaraFunctionLibrary.cpp after a first
 * version of this file tried it and got null for all eight damage types. Issue
 * #559 carries what covering the spawn itself would take.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpactParametersAreTheRightTypes,
	"Cataclysm.Effects.ImpactPointParametersAreTheRightTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpactParametersAreTheRightTypes::RunTest(const FString& Parameters)
{
	UNiagaraSystem* System = CataclysmImpactEffectTest::LoadImpactSystem();
	if (!System)
	{
		AddError(TEXT("NS_Impact_Point does not exist."));
		return false;
	}

	TMap<FString, FNiagaraTypeDefinition> Types;
	for (const FNiagaraVariableWithOffset& Variable :
			System->GetExposedParameters().ReadParameterVariables())
	{
		Types.Add(Variable.GetName().ToString(), Variable.GetType());
	}

	struct FExpected
	{
		const TCHAR* Name;
		const FNiagaraTypeDefinition& Type;
		const TCHAR* Written;
	};

	// TargetPosition is a Position and not a Vector deliberately. The Vector
	// setters are single precision underneath and lose accuracy far from the
	// world origin, which is a dungeon-sized problem rather than a theoretical
	// one. docs/Niagara_Conventions.md section 2 records the trap.
	const FExpected Expected[] = {
		{ TEXT("User.ElementColour"),     FNiagaraTypeDefinition::GetColorDef(),    TEXT("SetVariableLinearColor") },
		{ TEXT("User.ElementColourDark"), FNiagaraTypeDefinition::GetColorDef(),    TEXT("SetVariableLinearColor") },
		{ TEXT("User.Intensity"),         FNiagaraTypeDefinition::GetFloatDef(),    TEXT("SetVariableFloat") },
		{ TEXT("User.Scale"),             FNiagaraTypeDefinition::GetFloatDef(),    TEXT("SetVariableFloat") },
		{ TEXT("User.Duration"),          FNiagaraTypeDefinition::GetFloatDef(),    TEXT("SetVariableFloat") },
		{ TEXT("User.ImpactNormal"),      FNiagaraTypeDefinition::GetVec3Def(),     TEXT("SetVariableVec3") },
		{ TEXT("User.TargetPosition"),    FNiagaraTypeDefinition::GetPositionDef(), TEXT("SetVariablePosition") },
	};

	for (const FExpected& One : Expected)
	{
		const FNiagaraTypeDefinition* Actual = Types.Find(FString(One.Name));
		if (!Actual)
		{
			AddError(FString::Printf(
				TEXT("NS_Impact_Point does not expose %s at all."), One.Name));
			continue;
		}

		TestTrue(FString::Printf(
			TEXT("%s is the type %s writes. Expected %s, asset has %s"),
			One.Name, One.Written, *One.Type.GetName(), *Actual->GetName()),
			*Actual == One.Type);
	}

	return true;
}

// --------------------------------------------------------------------------
// Where the effect is placed
// --------------------------------------------------------------------------

/**
 * The regression test for issue #562.
 *
 * Every blow an enemy landed on the player drew its effect in the middle of the
 * level instead of on the player. The cause was reading the impact point of a
 * hit result that never blocked anything: that point is (0,0,0), which is not
 * "no answer" but a specific wrong one, because the world origin is a real place
 * and the player start sits near it.
 *
 * The player's own attacks looked correct throughout, because a swept attack
 * produces a genuine blocking hit. That is what hid the fault, and it is why the
 * non-blocking case is the one worth pinning.
 *
 * THIS RUNS WHERE THE OTHERS CANNOT. It touches no Niagara component, so the
 * -nullrhi automation harness described in #559 can execute it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpactIsPlacedOnWhatWasHit,
	"Cataclysm.Effects.ImpactIsPlacedOnWhatWasHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpactIsPlacedOnWhatWasHit::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	if (!World)
	{
		AddError(TEXT("could not create a test world."));
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);

	// Deliberately far from the origin, so "on the actor" and "at the world
	// origin" cannot be confused for one another. The whole bug was that they
	// were.
	const FVector Somewhere(1500.0f, -2400.0f, 90.0f);
	AActor* Struck = World->SpawnActor<AActor>();

	// A BARE AActor CANNOT BE MOVED. It spawns with no root component, so
	// SetActorLocation has nothing to move and GetActorLocation answers with the
	// origin -- which is the exact value this test has to tell a real location
	// apart from. The first version of this test spawned one, and every
	// assertion below failed against the test's own setup rather than against
	// the code it was written to check.
	USceneComponent* Root = NewObject<USceneComponent>(Struck);
	Struck->SetRootComponent(Root);
	Root->RegisterComponent();
	Struck->SetActorLocation(Somewhere);

	if (!Struck->GetActorLocation().Equals(Somewhere))
	{
		AddError(TEXT("the test actor could not be moved, so nothing below "
					  "would distinguish 'on the actor' from 'at the origin'."));
		World->DestroyWorld(false);
		return false;
	}

	FVector Normal = FVector::ZeroVector;

	// 1. A hit result that never blocked anything. This is what an enemy's blow
	// carries, and reading its impact point is what put the effect in the middle
	// of the level.
	const FHitResult NeverHitAnything;
	const FVector Unblocked =
		UCataclysmImpactEffect::ImpactLocationFor(&NeverHitAnything, Struck, Normal);
	TestEqual(TEXT("a hit result that blocked nothing places the effect on the "
				   "damaged actor, not at the world origin"), Unblocked, Somewhere);
	TestFalse(TEXT("and that case is not the world origin"),
		Unblocked.IsNearlyZero());
	TestEqual(TEXT("with no surface to read, the effect faces up"),
		Normal, FVector::UpVector);

	// 2. No hit result at all, which a burn tick or a stat line carries.
	Normal = FVector::ZeroVector;
	TestEqual(TEXT("no hit result at all places the effect on the damaged actor"),
		UCataclysmImpactEffect::ImpactLocationFor(nullptr, Struck, Normal),
		Somewhere);

	// 3. A real blocking hit is used, which is the case that already worked and
	// must keep working. Without this the guard could be satisfied by ignoring
	// hit results altogether.
	FHitResult Blocked;
	Blocked.bBlockingHit = true;
	Blocked.ImpactPoint = FVector(10.0f, 20.0f, 30.0f);
	Blocked.ImpactNormal = FVector(0.0f, 1.0f, 0.0f);
	Normal = FVector::ZeroVector;
	TestEqual(TEXT("a hit that really blocked places the effect where it landed"),
		UCataclysmImpactEffect::ImpactLocationFor(&Blocked, Struck, Normal),
		FVector(10.0f, 20.0f, 30.0f));
	TestEqual(TEXT("and the effect faces the surface it struck"),
		Normal, FVector(0.0f, 1.0f, 0.0f));

	// 4. A blocking hit carrying no normal still has to face somewhere.
	Blocked.ImpactNormal = FVector::ZeroVector;
	Normal = FVector::ZeroVector;
	UCataclysmImpactEffect::ImpactLocationFor(&Blocked, Struck, Normal);
	TestEqual(TEXT("a blocking hit with no surface normal still faces up"),
		Normal, FVector::UpVector);

	// 5. Nothing to place it on and nothing that hit. It must not crash, and
	// the origin is the only answer left.
	Normal = FVector::ZeroVector;
	TestEqual(TEXT("with no actor and no hit, the origin is all that is left"),
		UCataclysmImpactEffect::ImpactLocationFor(nullptr, nullptr, Normal),
		FVector::ZeroVector);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

/**
 * The second half of issue #562, and the half that actually caused it.
 *
 * An attribute set's GetOwningActor answers with the ability system's OWNER.
 * For the player, ACataclysmPlayerCharacter::InitAbilityActorInfo makes the
 * owner the player state -- deliberately, because it survives death -- and the
 * avatar the pawn. A player state is not placed in the world and reports the
 * origin, so every blow an enemy landed on the player drew its effect in the
 * middle of the level.
 *
 * An enemy puts its ability system on the character itself, so owner and avatar
 * are the same object and enemy-facing effects were placed correctly. That is
 * why the fault was invisible in one direction and obvious in the other, and it
 * is why this test uses two DIFFERENT actors: with one actor it would pass no
 * matter which accessor the code called.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpactDrawsOnTheAvatarNotTheOwner,
	"Cataclysm.Effects.ImpactDrawsOnTheAvatarNotTheOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpactDrawsOnTheAvatarNotTheOwner::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	if (!World)
	{
		AddError(TEXT("could not create a test world."));
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);

	// Stands in for the player state: holds the component, is not in the world.
	AActor* Owner = World->SpawnActor<AActor>();

	// Stands in for the pawn: the thing that is actually somewhere.
	AActor* Avatar = World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Avatar);
	Avatar->SetRootComponent(Root);
	Root->RegisterComponent();
	Avatar->SetActorLocation(FVector(800.0f, 300.0f, 50.0f));

	UAbilitySystemComponent* AbilitySystem =
		NewObject<UAbilitySystemComponent>(Owner);
	AbilitySystem->RegisterComponent();
	AbilitySystem->InitAbilityActorInfo(Owner, Avatar);

	const AActor* Chosen = UCataclysmImpactEffect::ActorToDrawOn(AbilitySystem);

	TestEqual(TEXT("the effect is drawn on the avatar, the thing standing in "
				   "the world"), Chosen, (const AActor*)Avatar);
	TestNotEqual(TEXT("and never on the owner, which for the player is the "
					  "player state and has no position"),
		Chosen, (const AActor*)Owner);

	// Without an ability system there is nothing to draw on, and guessing a
	// position is what caused the original fault.
	TestNull(TEXT("no ability system means no actor to draw on"),
		UCataclysmImpactEffect::ActorToDrawOn(nullptr));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

/**
 * The regression test for issue #563.
 *
 * One player attack drew seven bursts in five seconds. Two were the strike
 * itself. The other five were a burn ticking once a second, each one reaching
 * health through the same meta attribute a blow does, and each one therefore
 * drawing the same full impact burst as the strike that started it.
 *
 * A burn ticking is not a blow landing. docs/Niagara_Conventions.md gives
 * ailments their own shape, NS_Status_Applied, which is what a burn should use
 * once it exists.
 *
 * The other two bursts, 0.21 seconds apart, are CORRECT and are deliberately
 * left alone. The project owner identified them as the returning projectile
 * ability: it strikes once going out and once coming back, so two strikes
 * should draw two bursts. An earlier reading of the same log called that a
 * combat fault. It was not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpactSkipsWhatIsNotABlow,
	"Cataclysm.Effects.ImpactIsNotDrawnForAnythingButABlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpactSkipsWhatIsNotABlow::RunTest(const FString& Parameters)
{
	// A blow that connected. Everything below is this, with one thing changed,
	// so each assertion isolates the reason it was refused.
	FCataclysmIncomingHit Landed;
	Landed.Damage = 52.1f;

	FCataclysmDamageResult Connected;
	Connected.DealtToHealth = 52.1f;

	TestTrue(TEXT("a blow that took health is drawn"),
		UCataclysmImpactEffect::ShouldDrawFor(Landed, Connected));

	// Stopped by an energy shield rather than health, and still a blow.
	FCataclysmDamageResult OntoShield;
	OntoShield.AbsorbedByShield = 30.0f;
	TestTrue(TEXT("a blow a shield absorbed is still drawn"),
		UCataclysmImpactEffect::ShouldDrawFor(Landed, OntoShield));

	// THE ONE THAT MATTERS. Same damage arriving, same health lost, but it is a
	// burn ticking rather than something striking.
	FCataclysmIncomingHit Burning;
	Burning.Damage = 2.6f;
	Burning.bIsDamageOverTime = true;

	FCataclysmDamageResult Ticked;
	Ticked.DealtToHealth = 2.6f;

	TestFalse(TEXT("a burn ticking is not a blow and draws no impact"),
		UCataclysmImpactEffect::ShouldDrawFor(Burning, Ticked));

	// And a burn ticking into a shield is refused for the same reason, not
	// accidentally allowed through the shield branch.
	TestFalse(TEXT("a burn ticking into a shield draws no impact either"),
		UCataclysmImpactEffect::ShouldDrawFor(Burning, OntoShield));

	// Nothing arrived at all: evaded, or mitigated to nothing. Without this the
	// effect would mean "an attack happened" rather than "that landed".
	const FCataclysmDamageResult Nothing;
	TestFalse(TEXT("a blow that was stopped entirely draws nothing"),
		UCataclysmImpactEffect::ShouldDrawFor(Landed, Nothing));

	return true;
}
