// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

/**
 * A skill is worth what it is worth, wherever it is put. Issue #836.
 *
 * WHAT CHANGED AND WHY. Until 2026-08-22 a skill had no damage figure, cooldown
 * or mana cost of its own: `game/Data/SkillSlots.csv` held one of each per slot
 * and the game applied it to whatever skill sat there. That was right while a
 * skill could only go in the slot it was designed for.
 *
 * The project owner decided that **a slot is a key** and any skill may go in any
 * slot. With the numbers on the slot, the same skill would have been worth 250%
 * of weapon damage on the right mouse button and 400% on R -- its power
 * following the key rather than the skill. `docs/DECISIONS.md` records it.
 *
 * WHAT THIS FILE GUARDS, AND THE SECOND IS THE ONE THAT MATTERS TODAY.
 *
 *   **A skill that states a figure gets it.** That is the new behaviour and
 *   nothing in the game exercises it yet, because every one of the 398 rows in
 *   `game/Data/WeaponSkills.csv` is blank. Without a test it would be built,
 *   merged, and first exercised months later by whoever wrote the first number.
 *
 *   **A skill that states nothing still takes its slot's figure.** Every skill
 *   in the game is in that state, so this is what stops the change altering
 *   anything before a number is written. A fallback that quietly answered zero
 *   would make every skill in the game deal no damage, and the only thing that
 *   would notice is play.
 *
 *   **Except an aura, which deals none.** Ruled by the owner on 2026-11-24,
 *   issue #2875: an aura deals damage only if its description says so, so an
 *   aura stating no figure answers nought on any key, and the slot's figure is
 *   still what a skill of any other shape stating nothing takes.
 */
namespace CataclysmSkillNumbersTest
{
	/** A skill of a concrete shape, in a named slot, stating nothing. */
	UCataclysmStrikeSkill* MakeSkillIn(ECataclysmAbilitySlot Slot)
	{
		UCataclysmStrikeSkill* Skill =
			NewObject<UCataclysmStrikeSkill>(GetTransientPackage());
		Skill->Slot = Slot;
		return Skill;
	}

	/** What the slot table says a skill in this slot is worth. */
	float SlotDamagePercent(ECataclysmAbilitySlot Slot)
	{
		const FCataclysmSkillSlotNumbers Numbers = UCataclysmSkillSlots::NumbersFor(
			UCataclysmSkillSlots::LoadGeneratedTable(), Slot);
		return Numbers.bFound ? Numbers.DamagePercent : -1.0f;
	}

	/** An aura in a named slot, stating nothing. Issue #2875. */
	UCataclysmAuraSkill* MakeAuraIn(ECataclysmAbilitySlot Slot)
	{
		UCataclysmAuraSkill* Skill =
			NewObject<UCataclysmAuraSkill>(GetTransientPackage());
		Skill->Slot = Slot;
		return Skill;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillTakesItsSlotsNumbersWhenSilent,
	"Cataclysm.SkillNumbers.ASkillStatingNothingTakesItsSlotsFigures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillTakesItsSlotsNumbersWhenSilent::RunTest(const FString& Parameters)
{
	using namespace CataclysmSkillNumbersTest;

	// EVERY SKILL IN THE GAME IS IN THIS STATE, so this is what stops issue
	// #836's change altering anything before a number is written. A fallback
	// answering zero would make every skill deal no damage and only play would
	// notice.
	const ECataclysmAbilitySlot Heavy = ECataclysmAbilitySlot::Heavy;

	const float FromSlot = SlotDamagePercent(Heavy);
	if (!TestTrue(TEXT("the slot table has a figure for the Heavy slot"),
				  FromSlot > 0.0f))
	{
		return false;
	}

	UCataclysmStrikeSkill* Skill = MakeSkillIn(Heavy);

	TestEqual(TEXT("a skill states nothing by default"),
		Skill->DamagePercentOverride, -1.0f);
	TestEqual(TEXT("and takes its slot's damage percentage"),
		Skill->GetDamagePercent(), FromSlot, 0.01f);

	TestEqual(TEXT("it states no cooldown by default"),
		Skill->CooldownOverride, -1.0f);
	TestTrue(TEXT("and takes its slot's cooldown"),
		Skill->GetBaseCooldown() > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillKeepsItsOwnNumbers,
	"Cataclysm.SkillNumbers.ASkillStatingItsOwnFiguresKeepsThemInAnySlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillKeepsItsOwnNumbers::RunTest(const FString& Parameters)
{
	using namespace CataclysmSkillNumbersTest;

	// THE SAME SKILL IN TWO SLOTS, AND IT IS WORTH THE SAME IN BOTH. That is the
	// whole decision in one assertion: before it, the Heavy slot's 250% and the
	// Ultimate slot's 400% would have made these two answers differ.
	const float Stated = 137.5f;

	UCataclysmStrikeSkill* InHeavy = MakeSkillIn(ECataclysmAbilitySlot::Heavy);
	InHeavy->DamagePercentOverride = Stated;

	UCataclysmStrikeSkill* InUltimate =
		MakeSkillIn(ECataclysmAbilitySlot::Ultimate);
	InUltimate->DamagePercentOverride = Stated;

	TestEqual(TEXT("a skill that states its damage keeps it in the Heavy slot"),
		InHeavy->GetDamagePercent(), Stated, 0.01f);
	TestEqual(TEXT("and the same skill keeps it in the Ultimate slot"),
		InUltimate->GetDamagePercent(), Stated, 0.01f);

	// AND THE TWO SLOTS REALLY DO DIFFER, so the test above is not passing
	// because the slot table happens to hold one number twice.
	TestTrue(TEXT("the two slots would otherwise have given different figures"),
		!FMath::IsNearlyEqual(SlotDamagePercent(ECataclysmAbilitySlot::Heavy),
							  SlotDamagePercent(ECataclysmAbilitySlot::Ultimate),
							  0.01f));

	// ZERO IS A REAL ANSWER AND NOT SILENCE, which is why the sentinel is -1. A
	// Support skill deals 0% of weapon damage by design.
	UCataclysmStrikeSkill* Free = MakeSkillIn(ECataclysmAbilitySlot::Heavy);
	Free->DamagePercentOverride = 0.0f;
	TestEqual(TEXT("a skill stating zero damage deals zero, not its slot's"),
		Free->GetDamagePercent(), 0.0f, 0.01f);

	UCataclysmStrikeSkill* Instant = MakeSkillIn(ECataclysmAbilitySlot::Heavy);
	Instant->CooldownOverride = 0.0f;
	TestEqual(TEXT("a skill stating no cooldown waits for nothing"),
		Instant->GetBaseCooldown(), 0.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillNumbersAuraStatingNothingTest,
	"Cataclysm.SkillNumbers.AnAuraStatingNoDamageDealsNoneOnAnyKeyAndKeepsAFigureItStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled by the owner on 2026-11-24, issue #2875, "No damage unless stated (Recommended)": an aura deals damage only if
 * its description says so. An aura stating no figure answers nought in the Aura slot, and in the Ultimate slot, Living
 * Pyre's. One stating a figure keeps it: Conflagration's 10 (#2869) and Living Pyre's 66.67 (#2875).
 *
 * THE CONTROL is a strike stating nothing in the Aura slot. It takes the slot's figure, which is above nought, so the
 * nought an aura answers is the aura's rule and not the slot's number.
 */
bool FCataclysmSkillNumbersAuraStatingNothingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSkillNumbersTest;

	const float AuraSlotFigure = SlotDamagePercent(ECataclysmAbilitySlot::Aura);
	if (!TestTrue(FString::Printf(TEXT("control: the Aura slot's own figure is above nought (%.1f)"),
								  AuraSlotFigure),
				  AuraSlotFigure > 0.0f))
	{
		return false;
	}

	UCataclysmStrikeSkill* Strike = MakeSkillIn(ECataclysmAbilitySlot::Aura);
	TestEqual(TEXT("control: a strike stating nothing in the Aura slot takes the slot's figure"),
		Strike->GetDamagePercent(), AuraSlotFigure, 0.01f);

	UCataclysmAuraSkill* Silent = MakeAuraIn(ECataclysmAbilitySlot::Aura);
	TestEqual(TEXT("an aura states nothing by default"),
		Silent->DamagePercentOverride, -1.0f);
	TestEqual(TEXT("and an aura stating nothing in the Aura slot deals nothing, not the slot's figure"),
		Silent->GetDamagePercent(), 0.0f, 0.0f);

	UCataclysmAuraSkill* SilentUltimate = MakeAuraIn(ECataclysmAbilitySlot::Ultimate);
	TestTrue(TEXT("control: the Ultimate slot's own figure is above nought"),
		SlotDamagePercent(ECataclysmAbilitySlot::Ultimate) > 0.0f);
	TestEqual(TEXT("nor in the Ultimate slot, which is Living Pyre's"),
		SilentUltimate->GetDamagePercent(), 0.0f, 0.0f);

	UCataclysmAuraSkill* Stated = MakeAuraIn(ECataclysmAbilitySlot::Aura);
	Stated->DamagePercentOverride = 10.0f;
	TestEqual(TEXT("an aura stating 10, Conflagration's figure, deals 10"),
		Stated->GetDamagePercent(), 10.0f, 0.01f);

	UCataclysmAuraSkill* StatedUltimate = MakeAuraIn(ECataclysmAbilitySlot::Ultimate);
	StatedUltimate->DamagePercentOverride = 66.67f;
	TestEqual(TEXT("and one stating 66.67 in the Ultimate slot, Living Pyre's figure, deals 66.67"),
		StatedUltimate->GetDamagePercent(), 66.67f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponSkillCarriesTheThreeFigures,
	"Cataclysm.SkillNumbers.TheSkillTableCarriesAllThreeFigures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWeaponSkillCarriesTheThreeFigures::RunTest(const FString& Parameters)
{
	// THE COLUMNS EXIST AND EVERY ROW IS SILENT. The second half is the state
	// this change was landed in: the mechanism before the numbers, so nothing
	// behaves differently until somebody writes one. When the first number is
	// written this test says so rather than failing -- it counts rather than
	// forbids.
	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!Table)
	{
		AddError(TEXT("The weapon skill table could not be loaded."));
		return false;
	}

	int32 Rows = 0;
	int32 StateDamage = 0;
	int32 StateCooldown = 0;
	int32 StateManaCost = 0;

	for (const TPair<FName, uint8*>& Entry : Table->GetRowMap())
	{
		const FCataclysmWeaponSkillRow* Row =
			reinterpret_cast<const FCataclysmWeaponSkillRow*>(Entry.Value);
		if (!Row)
		{
			continue;
		}
		++Rows;

		// A NEGATIVE OTHER THAN -1 IS A BROKEN ROW. The generator refuses one,
		// so this only fires for a table edited in the editor rather than
		// generated, which is exactly the case the generator cannot cover.
		for (const TPair<const TCHAR*, float>& Field : {
				 TPair<const TCHAR*, float>(TEXT("damage percentage"), Row->DamagePercent),
				 TPair<const TCHAR*, float>(TEXT("cooldown"), Row->Cooldown),
				 TPair<const TCHAR*, float>(TEXT("mana cost"), Row->ManaCost)})
		{
			if (Field.Value < 0.0f && !FMath::IsNearlyEqual(Field.Value, -1.0f))
			{
				AddError(FString::Printf(
					TEXT("%s states a %s of %.2f. Negative means nothing except "
						 "exactly -1, which means the row says nothing."),
					*Entry.Key.ToString(), Field.Key, Field.Value));
			}
		}

		StateDamage += Row->DamagePercent >= 0.0f ? 1 : 0;
		StateCooldown += Row->Cooldown >= 0.0f ? 1 : 0;
		StateManaCost += Row->ManaCost >= 0.0f ? 1 : 0;
	}

	TestTrue(TEXT("the weapon skill table has rows"), Rows > 0);

	AddInfo(FString::Printf(
		TEXT("%d rows. %d state their own damage percentage, %d their own "
			 "cooldown, %d their own mana cost."),
		Rows, StateDamage, StateCooldown, StateManaCost));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
