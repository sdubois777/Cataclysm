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
