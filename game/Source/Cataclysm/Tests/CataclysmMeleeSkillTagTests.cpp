// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmPassiveTree.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"

/**
 * A melee-scoped Demonic row reaches the weapon skills that are melee attacks and
 * not the spells. Issue #944.
 *
 * MELEE FOLLOWS THE WEAPON ATTACK, NOT THE AREA SHAPE. Ruled 2026-09-27 under the
 * owner's delegation, where Path of Exile and Last Epoch agree: a slam or a burst
 * made with a melee weapon is melee, a spell is not. Seven point-blank Demonic
 * skills and The Whole Weight gained `Type.Melee` then; Anathema, the Wand's
 * Ultimate, is a spell and did not.
 *
 * READ FROM THE GAME'S OWN TABLES: the skills' tags from the generated
 * DT_WeaponSkills, and the row from the passive effect table through
 * `ModifiersFor`, asked through `UCataclysmStatPipeline::ModifierApplies`, the
 * match every stat read uses. Carnage, `Masochist_keystone_fc_kA`, is the row:
 * "more" attack damage scoped to `Type.Melee`, with no condition to hold.
 */
namespace CataclysmMeleeSkillTagTest
{
	const FCataclysmWeaponSkill* Named(const TArray<FCataclysmWeaponSkill>& Skills,
									   const TCHAR* Name)
	{
		return Skills.FindByPredicate(
			[Name](const FCataclysmWeaponSkill& Skill) { return Skill.Name == Name; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMeleeRowReachesAMeleeSkillTest,
	"Cataclysm.MeleeSkills.AMeleeScopedRowReachesBreakTheWorldAndNotAnathema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMeleeRowReachesAMeleeSkillTest::RunTest(const FString&)
{
	using namespace CataclysmMeleeSkillTagTest;

	const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EffectTable = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("the weapon skill table loads"), Skills)
		|| !TestNotNull(TEXT("the node table loads"), NodeTable)
		|| !TestNotNull(TEXT("the effect table loads"), EffectTable))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	const TArray<FCataclysmWeaponSkill> Warhammer =
		UCataclysmWeaponSkills::SkillsFor(Skills, TEXT("Warhammer"), TEXT("Demonic"));
	const TArray<FCataclysmWeaponSkill> Wand =
		UCataclysmWeaponSkills::SkillsFor(Skills, TEXT("Wand"), TEXT("Demonic"));
	const FCataclysmWeaponSkill* BreakTheWorld = Named(Warhammer, TEXT("Break the World"));
	const FCataclysmWeaponSkill* Anathema = Named(Wand, TEXT("Anathema"));
	if (!TestNotNull(TEXT("set-up: the Demonic Warhammer has Break the World"), BreakTheWorld)
		|| !TestNotNull(TEXT("set-up: the Demonic Wand has Anathema"), Anathema))
	{
		return false;
	}

	FCataclysmPassiveAllocation Taken;
	Taken.Add(FName(TEXT("Masochist_keystone_fc_kA")), 1);
	const TMap<FName, TArray<FCataclysmStatModifier>> Granted =
		UCataclysmPassiveTree::ModifiersFor(Taken, NodeTable, EffectTable,
											{FName(TEXT("Demonic"))});
	const TArray<FCataclysmStatModifier>* Attack = Granted.Find(FName(TEXT("attack_damage")));
	if (!TestNotNull(TEXT("set-up: Carnage grants attack damage"), Attack)
		|| !TestEqual(TEXT("set-up: one attack damage modifier"), Attack->Num(), 1))
	{
		return false;
	}
	const FCataclysmStatModifier& Carnage = (*Attack)[0];
	const FGameplayTag Melee =
		FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("set-up: Type.Melee is a registered tag"), Melee.IsValid())
		|| !TestTrue(TEXT("set-up: and Carnage is scoped to it"), Carnage.RequiredTags.HasTagExact(Melee)))
	{
		return false;
	}

	TestTrue(TEXT("Break the World carries Type.Melee"), BreakTheWorld->Tags.HasTagExact(Melee));
	TestTrue(TEXT("so Carnage reaches Break the World"),
			 UCataclysmStatPipeline::ModifierApplies(Carnage, BreakTheWorld->Tags));
	TestFalse(TEXT("Anathema, a spell, does not carry Type.Melee"), Anathema->Tags.HasTagExact(Melee));
	TestFalse(TEXT("so Carnage does not reach Anathema"),
			  UCataclysmStatPipeline::ModifierApplies(Carnage, Anathema->Tags));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
