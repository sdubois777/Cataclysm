// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AbilitySystem/CataclysmImpactEffect.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
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
	const UDataTable* Table = UCataclysmImpactEffect::LoadElementVisuals();
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
		if (!UCataclysmImpactEffect::ColoursFor(DamageType, Primary, Secondary))
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
		UCataclysmImpactEffect::ColoursFor(NAME_None, Unused, AlsoUnused));

	return true;
}

// --------------------------------------------------------------------------
// End to end, through the spawn the game actually uses
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmImpactCarriesItsRowToTheComponent,
	"Cataclysm.Effects.ImpactPointCarriesItsRowColoursToTheComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpactCarriesItsRowToTheComponent::RunTest(const FString& Parameters)
{
	const UDataTable* Table = UCataclysmImpactEffect::LoadElementVisuals();
	if (!Table)
	{
		AddError(TEXT("DT_ElementVisuals does not exist."));
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	if (!World)
	{
		AddError(TEXT("could not create a test world."));
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	FURL URL;
	World->InitializeActorsForPlay(URL);
	World->BeginPlay();

	AActor* Struck = World->SpawnActor<AActor>();

	// THIS IS THE ONE THAT PROVES THE HEADLINE CLAIM. Everything above checks a
	// link in the chain; this walks the whole of it -- table row, lookup, the
	// FName setter, the component -- through the same call the attribute set
	// makes when a blow lands. If a parameter name stops matching the asset, or
	// a setter stops being called, only this notices.
	int32 Checked = 0;
	for (const TCHAR* TypeName : CataclysmImpactEffectTest::EveryDamageType)
	{
		const FName DamageType(TypeName);
		const FCataclysmElementVisualRow* Row =
			Table->FindRow<FCataclysmElementVisualRow>(
				DamageType, TEXT("impact effect test"), /*bWarnIfMissing=*/false);
		if (!Row)
		{
			continue;
		}

		UNiagaraComponent* Component = UCataclysmImpactEffect::SpawnAt(
			Struck, FVector(100.0f, 200.0f, 300.0f), FVector::UpVector,
			DamageType);
		if (!Component)
		{
			AddError(FString::Printf(
				TEXT("a %s hit spawned no component."), TypeName));
			continue;
		}

		bool bPrimaryRead = false;
		const FLinearColor Primary = Component->GetVariableColor(
			UCataclysmImpactEffect::ElementColourParameter, bPrimaryRead);
		bool bSecondaryRead = false;
		const FLinearColor Secondary = Component->GetVariableColor(
			UCataclysmImpactEffect::ElementColourDarkParameter, bSecondaryRead);

		TestTrue(FString::Printf(
			TEXT("the %s impact's primary colour parameter can be read back, "
				 "which it cannot be if the asset does not expose it"), TypeName),
			bPrimaryRead);
		TestTrue(FString::Printf(
			TEXT("the %s impact's dark colour parameter can be read back"),
			TypeName), bSecondaryRead);

		TestTrue(FString::Printf(
			TEXT("the %s impact carries its row's primary colour. Row says %s, "
				 "component holds %s"), TypeName,
			*Row->PrimaryColour.ToString(), *Primary.ToString()),
			Primary.Equals(Row->PrimaryColour, 1.0e-4f));
		TestTrue(FString::Printf(
			TEXT("the %s impact carries its row's dark colour. Row says %s, "
				 "component holds %s"), TypeName,
			*Row->SecondaryColour.ToString(), *Secondary.ToString()),
			Secondary.Equals(Row->SecondaryColour, 1.0e-4f));

		// Two damage types holding the same colour would pass every check above
		// while meaning the lookup returned something constant. The palette
		// test already proves the eight rows differ, so it is enough here that
		// the component's colour tracks the row it was given.
		++Checked;
	}

	TestEqual(TEXT("every damage type reached a component"), Checked, 8);

	// An untyped hit leaves the asset's own default alone. White is that
	// default and no designed row is white, so a white impact on screen means
	// exactly one thing: nothing set a colour.
	UNiagaraComponent* Untyped = UCataclysmImpactEffect::SpawnAt(
		Struck, FVector::ZeroVector, FVector::UpVector, NAME_None);
	if (Untyped)
	{
		bool bRead = false;
		const FLinearColor Colour = Untyped->GetVariableColor(
			UCataclysmImpactEffect::ElementColourParameter, bRead);
		TestTrue(TEXT("an untyped hit keeps the asset's authored white default"),
			Colour.Equals(FLinearColor::White, 1.0e-4f));
	}
	else
	{
		AddError(TEXT("an untyped hit spawned no component. It should still "
					  "play, with the system's own defaults."));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
