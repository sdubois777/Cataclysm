// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "NiagaraEffectType.h"

/**
 * The four Niagara effect types carry the settings they were built with.
 *
 * WHAT AN EFFECT TYPE IS. The asset that decides when a particle system is
 * culled, how often it updates, and how many of it may exist at once. **Nothing
 * in Niagara is culled by default**: a system with no effect type set runs at
 * full cost at any distance, off screen, and in unlimited numbers. The vertical
 * slice puts twenty Brutes on screen attacking at once and the binding
 * constraint on the development machine is an 8 GB graphics card.
 *
 * WHY THIS TEST HAS TO EXIST, AND IT IS NOT "IN CASE THE GENERATOR IS BUGGY".
 * tools/generate_effect_types.py sets these through Unreal's Python interface,
 * where **a struct is returned as a copy**. Changing a copy and forgetting to
 * write it back is not an error: every call succeeds, the script reports having
 * built the asset, and the asset keeps its old numbers. The culling settings
 * here live two structs deep, so that failure has two chances to happen and
 * neither would say anything.
 *
 * A .uasset is also a binary blob in Git LFS, so no reviewer can read one in a
 * pull request. Reading the asset back is the only check there is.
 *
 * WHY IT IS AN AUTOMATION TEST AND NOT A PYTHON ONE. These are binary assets and
 * the Python test suite has no engine. Continuous integration is a single Linux
 * job that builds no C++, so nothing on a pull request runs this -- the same gap
 * issue #226 describes for the DataTable assets.
 *
 * WHAT IS NOT ASSERTED. That any of these numbers is right. Every one is a
 * starting point with no measurement behind it, which
 * docs/Niagara_Conventions.md says outright, and issue #547 is the missing
 * performance budget and profiling method. This file only says the asset holds
 * what the script meant to put in it.
 */

namespace CataclysmEffectTypeTest
{
	const TCHAR* Folder = TEXT("/Game/Effects/EffectTypes");

	UNiagaraEffectType* Load(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), Folder, Name, Name);
		return LoadObject<UNiagaraEffectType>(nullptr, *Path);
	}

	/** The one scalability entry the generator writes, or null. */
	const FNiagaraSystemScalabilitySettings* OnlyEntry(const UNiagaraEffectType* Type)
	{
		const TArray<FNiagaraSystemScalabilitySettings>& Settings =
			Type->GetSystemScalabilitySettings().Settings;
		return Settings.Num() == 1 ? &Settings[0] : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEffectTypesExist,
	"Cataclysm.Effects.TheFourEffectTypesExist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEffectTypesExist::RunTest(const FString& Parameters)
{
	using namespace CataclysmEffectTypeTest;

	// Named rather than counted, because a folder with four assets in it says
	// nothing about whether they are the four every system is supposed to pick
	// from. A system referencing a name that does not exist is a null effect
	// type, which is the no-culling case this whole arrangement exists to stop.
	for (const TCHAR* Name : {TEXT("FXT_Enemy"), TEXT("FXT_PlayerSkill"),
							  TEXT("FXT_Ambient"), TEXT("FXT_MustBeSeen")})
	{
		if (!Load(Name))
		{
			AddError(FString::Printf(
				TEXT("%s/%s does not exist. Run "
					 "`python tools/run_editor_python.py "
					 "tools/generate_effect_types.py`."), Folder, Name));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyEffectTypeCulls,
	"Cataclysm.Effects.EnemyEffectsAreCulledHardEnoughForTwentyBrutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyEffectTypeCulls::RunTest(const FString& Parameters)
{
	using namespace CataclysmEffectTypeTest;

	const UNiagaraEffectType* Type = Load(TEXT("FXT_Enemy"));
	if (!Type)
	{
		AddError(TEXT("FXT_Enemy does not exist."));
		return false;
	}

	TestEqual(TEXT("enemy effects update at medium frequency"),
		static_cast<int32>(Type->UpdateFrequency),
		static_cast<int32>(ENiagaraScalabilityUpdateFrequency::Medium));

	// "Kill and Clear" in the editor. The engine name is DeactivateImmediate,
	// and docs/Niagara_Conventions.md states the editor's name, which is the
	// trap the generator's header comment describes.
	TestEqual(TEXT("and are killed and cleared when culled, not put to sleep"),
		static_cast<int32>(Type->CullReaction),
		static_cast<int32>(ENiagaraCullReaction::DeactivateImmediate));

	// WITHOUT A SIGNIFICANCE HANDLER THE INSTANCE LIMITS DO NOTHING, because
	// nothing decides which instance is the least important when the limit is
	// reached. It is an instanced sub-object rather than a setting, so it is
	// the one most likely to be missing.
	TestNotNull(TEXT("and something decides which instance matters least"),
		Type->GetSignificanceHandler());

	const FNiagaraSystemScalabilitySettings* Entry = OnlyEntry(Type);
	if (!TestNotNull(TEXT("FXT_Enemy has exactly one scalability entry"), Entry))
	{
		return false;
	}

	TestTrue(TEXT("enemy effects are culled by distance"), Entry->bCullByDistance);
	TestEqual(TEXT("at 4000 cm, which is past the frame at maximum zoom"),
		Entry->MaxDistance, 4000.0f, 0.01f);

	TestTrue(TEXT("and by how many exist at once"), Entry->bCullMaxInstanceCount);
	TestEqual(TEXT("capped at 60 across every enemy effect"),
		Entry->MaxInstances, 60);

	TestTrue(TEXT("and by how many of ONE system exist"),
		Entry->bCullPerSystemMaxInstanceCount);
	TestEqual(TEXT("capped at 20, which is the twenty-Brutes case"),
		Entry->MaxSystemInstances, 20);

	TestTrue(TEXT("an enemy effect nobody is looking at is culled"),
		Entry->VisibilityCulling.bCullWhenNotRendered);
	TestTrue(TEXT("and one outside the camera's frustum is culled"),
		Entry->VisibilityCulling.bCullByViewFrustum);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOtherEffectTypesAreSet,
	"Cataclysm.Effects.TheOtherThreeCarryTheirOwnSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOtherEffectTypesAreSet::RunTest(const FString& Parameters)
{
	using namespace CataclysmEffectTypeTest;

	// --- what the player casts -------------------------------------------
	if (const UNiagaraEffectType* Skill = Load(TEXT("FXT_PlayerSkill")))
	{
		TestEqual(TEXT("player skill effects update at low frequency"),
			static_cast<int32>(Skill->UpdateFrequency),
			static_cast<int32>(ENiagaraScalabilityUpdateFrequency::Low));

		if (const FNiagaraSystemScalabilitySettings* Entry = OnlyEntry(Skill))
		{
			TestEqual(TEXT("and are culled at 6000 cm, further out than an "
						   "enemy's, because the player is looking for them"),
				Entry->MaxDistance, 6000.0f, 0.01f);
			TestEqual(TEXT("capped at 40 at once"), Entry->MaxInstances, 40);
		}
		else
		{
			AddError(TEXT("FXT_PlayerSkill has no scalability entry."));
		}
	}
	else
	{
		AddError(TEXT("FXT_PlayerSkill does not exist."));
	}

	// --- scenery ----------------------------------------------------------
	if (const UNiagaraEffectType* Ambient = Load(TEXT("FXT_Ambient")))
	{
		// ASLEEP, NOT KILLED, and this is the one setting that differs between
		// the four in a way that matters at runtime. A killed ambient effect
		// does not come back when the player walks toward it again; an asleep
		// one does. "Asleep" is the editor's name for DeactivateResume.
		TestEqual(TEXT("ambient effects go to sleep when culled, so they are "
					   "still there when the player comes back"),
			static_cast<int32>(Ambient->CullReaction),
			static_cast<int32>(ENiagaraCullReaction::DeactivateResume));

		if (const FNiagaraSystemScalabilitySettings* Entry = OnlyEntry(Ambient))
		{
			TestEqual(TEXT("and are culled at 5000 cm"),
				Entry->MaxDistance, 5000.0f, 0.01f);
			TestTrue(TEXT("and when nobody is looking at them"),
				Entry->VisibilityCulling.bCullWhenNotRendered);
		}
		else
		{
			AddError(TEXT("FXT_Ambient has no scalability entry."));
		}
	}
	else
	{
		AddError(TEXT("FXT_Ambient does not exist."));
	}

	// --- the escape hatch --------------------------------------------------
	if (const UNiagaraEffectType* MustBeSeen = Load(TEXT("FXT_MustBeSeen")))
	{
		// IT IS SUPPOSED TO CULL NOTHING. It exists so that "this must never
		// disappear" is a choice somebody made rather than the accident of
		// forgetting to set an effect type at all. If it ever grew culling
		// settings it would stop being distinguishable from the other three and
		// the choice would stop meaning anything.
		const TArray<FNiagaraSystemScalabilitySettings>& Settings =
			MustBeSeen->GetSystemScalabilitySettings().Settings;

		bool bCullsSomething = false;
		for (const FNiagaraSystemScalabilitySettings& Entry : Settings)
		{
			bCullsSomething = bCullsSomething || Entry.bCullByDistance
				|| Entry.bCullMaxInstanceCount
				|| Entry.bCullPerSystemMaxInstanceCount
				|| Entry.VisibilityCulling.bCullWhenNotRendered
				|| Entry.VisibilityCulling.bCullByViewFrustum;
		}

		TestFalse(TEXT("FXT_MustBeSeen culls nothing, which is the whole point "
					   "of it"), bCullsSomething);
	}
	else
	{
		AddError(TEXT("FXT_MustBeSeen does not exist."));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
