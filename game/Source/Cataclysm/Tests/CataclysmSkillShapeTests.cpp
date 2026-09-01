// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

/**
 * Tests for reading a skill's shape and its numbers out of the data.
 *
 * WHAT THIS GUARDS. A shape parameter that fails to read is worse than one that
 * is absent, because a radius of zero produces a skill that activates, spends
 * mana, starts its cooldown and hits nothing -- which is indistinguishable from
 * a skill somebody forgot to finish. That is the same shape of failure as issue
 * #155's cooldown of zero, which went unnoticed across all 77 designed skills.
 * So the parser reports rather than defaulting, and these pin that it does.
 */

namespace CataclysmSkillShapeTest
{
	constexpr float M = 100.0f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShapeParamsParseTest,
	"Cataclysm.SkillShape.MetresInTheSheetBecomeCentimetresInTheGame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmShapeParamsParseTest::RunTest(const FString&)
{
	using namespace CataclysmSkillShapeTest;

	// Molten Cleave's cell, verbatim from the Weapon Skills sheet.
	FString Error;
	const FCataclysmSkillShapeParams Params = UCataclysmSkillShapes::ParseParams(
		TEXT("Radius=4; Angle=120; Burn=1; GroundRadius=4; GroundDuration=6"),
		&Error);

	TestTrue(TEXT("It read cleanly"), Params.bValid);
	TestEqual(TEXT("Nothing was reported"), Error, FString());

	// THE CONVERSION IS THE POINT OF THIS TEST. The sheet says 4 metres and
	// Unreal works in centimetres, so a skill that hit 4 centimetres would still
	// run, still spend mana and look almost right.
	TestEqual(TEXT("4 metres becomes 400 centimetres"), Params.RadiusCm, 4 * M);
	TestEqual(TEXT("An angle is degrees and is not converted"),
		Params.AngleDegrees, 120.0f);
	TestEqual(TEXT("A duration is seconds and is not converted"),
		Params.GroundDuration, 6.0f);
	TestEqual(TEXT("Ground radius converts too"), Params.GroundRadiusCm, 4 * M);
	TestTrue(TEXT("Burn=1 sets it alight"), Params.bBurns);
	TestTrue(TEXT("It leaves ground"), Params.LeavesGround());

	// A fractional metre, because Emberhurl writes 1.5.
	const FCataclysmSkillShapeParams Fraction =
		UCataclysmSkillShapes::ParseParams(TEXT("Radius=1.5"));
	TestEqual(TEXT("1.5 metres becomes 150 centimetres"), Fraction.RadiusCm, 1.5f * M);

	// Speed is the one distance written in centimetres already.
	const FCataclysmSkillShapeParams Speed =
		UCataclysmSkillShapes::ParseParams(TEXT("Speed=1800"));
	TestEqual(TEXT("Speed is not converted"), Speed.SpeedCmPerSecond, 1800.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShapeParamsRefuseTest,
	"Cataclysm.SkillShape.AnUnreadableParameterIsReportedRatherThanZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmShapeParamsRefuseTest::RunTest(const FString&)
{
	// A misspelling. Without the report this reads as a radius of zero, and the
	// skill hits nothing with no indication why.
	FString Error;
	const FCataclysmSkillShapeParams Misspelled =
		UCataclysmSkillShapes::ParseParams(TEXT("Radiuss=4"), &Error);
	TestFalse(TEXT("A misspelled parameter does not read cleanly"), Misspelled.bValid);
	TestTrue(TEXT("And it says which one"), Error.Contains(TEXT("Radiuss")));
	TestEqual(TEXT("And the radius really is zero, which is why it must report"),
		Misspelled.RadiusCm, 0.0f);

	FString NotANumber;
	const FCataclysmSkillShapeParams Bad =
		UCataclysmSkillShapes::ParseParams(TEXT("Radius=wide"), &NotANumber);
	TestFalse(TEXT("A non-numeric value does not read cleanly"), Bad.bValid);
	TestTrue(TEXT("And it says so"), NotANumber.Contains(TEXT("not a number")));

	FString NoEquals;
	const FCataclysmSkillShapeParams Malformed =
		UCataclysmSkillShapes::ParseParams(TEXT("Radius 4"), &NoEquals);
	TestFalse(TEXT("A missing equals does not read cleanly"), Malformed.bValid);

	FString BadMode;
	const FCataclysmSkillShapeParams Mode =
		UCataclysmSkillShapes::ParseParams(TEXT("Mode=Teleport"), &BadMode);
	TestFalse(TEXT("A movement mode that does not exist is refused"), Mode.bValid);

	// A DELIBERATE ZERO IS NOT AN ERROR, and telling the two apart is the reason
	// the parser inspects the text rather than trusting Atof's return.
	FString None;
	const FCataclysmSkillShapeParams Zero =
		UCataclysmSkillShapes::ParseParams(TEXT("GroundRadius=0"), &None);
	TestTrue(TEXT("A written zero reads cleanly"), Zero.bValid);
	TestEqual(TEXT("And is zero"), Zero.GroundRadiusCm, 0.0f);

	// An empty cell is a skill with no numbers, not a broken one.
	const FCataclysmSkillShapeParams Empty = UCataclysmSkillShapes::ParseParams(TEXT(""));
	TestTrue(TEXT("An empty cell reads cleanly"), Empty.bValid);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShapeNamesTest,
	"Cataclysm.SkillShape.EveryShapeInTheDataHasATemplate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmShapeNamesTest::RunTest(const FString&)
{
	// TWO LISTS OF THE SAME SEVEN NAMES CAN DISAGREE. The generator holds them
	// in SHAPE_PARAMS and UCataclysmSkillShapes holds them in ShapeNames, so a
	// shape added to the sheet and not to C++ would read as None and grant the
	// placeholder -- a skill that generates cleanly and silently does nothing.
	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!Table)
	{
		AddError(TEXT("Could not load the weapon skill table."));
		return false;
	}

	int32 WithAShape = 0;
	TArray<FString> Problems;

	Table->ForeachRow<FCataclysmWeaponSkillRow>(TEXT("FCataclysmShapeNamesTest"),
		[&](const FName& RowName, const FCataclysmWeaponSkillRow& Row)
		{
			if (Row.Shape.IsEmpty())
			{
				if (!Row.ShapeParams.IsEmpty())
				{
					Problems.Add(FString::Printf(
						TEXT("%s has shape parameters but no shape"), *RowName.ToString()));
				}
				return;
			}

			++WithAShape;

			const ECataclysmSkillShape Shape =
				UCataclysmSkillShapes::ShapeFromName(Row.Shape);
			if (Shape == ECataclysmSkillShape::None)
			{
				Problems.Add(FString::Printf(
					TEXT("%s names the shape '%s', which no template implements"),
					*RowName.ToString(), *Row.Shape));
				return;
			}

			if (!UCataclysmWeaponSkills::TemplateFor(Shape))
			{
				Problems.Add(FString::Printf(
					TEXT("%s is shape '%s', which has no ability class"),
					*RowName.ToString(), *Row.Shape));
			}

			FString Error;
			UCataclysmSkillShapes::ParseParams(Row.ShapeParams, &Error);
			if (!Error.IsEmpty())
			{
				Problems.Add(FString::Printf(TEXT("%s: %s"),
					*RowName.ToString(), *Error));
			}
		});

	for (const FString& Problem : Problems)
	{
		AddError(Problem);
	}

	// The sixteen designed Demonic skills. Pinned as a floor rather than an
	// exact count so that designing more does not fail this, but losing the
	// column entirely does.
	TestTrue(FString::Printf(
		TEXT("At least sixteen rows carry a shape (found %d)"), WithAShape),
		WithAShape >= 16);

	return Problems.IsEmpty();
}

/**
 * The Minions parameter is read as a list of kinds, not as a number.
 *
 * WHAT WENT WRONG. Issue #622: this parser treated every key except Mode and
 * Effect as a number, so "Minions=Imp:1" went through FCString::Atof, came back
 * zero, and the guard against unreadable numbers rejected the WHOLE parameter
 * cell -- taking Count and MaxActive with it. Every summoning skill in the game
 * therefore arrived with no idea what it summoned, and the only thing that ever
 * said so was the full automation suite, which had not been run in a while.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionsParameterTest,
	"Cataclysm.SkillShape.MinionsNamesWhatASkillProduces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMinionsParameterTest::RunTest(const FString&)
{
	// Summon Imp's cell, verbatim from the Weapon Skills sheet.
	FString Error;
	const FCataclysmSkillShapeParams One = UCataclysmSkillShapes::ParseParams(
		TEXT("Count=1; MaxActive=3; Radius=3; Minions=Imp:1"), &Error);

	TestTrue(FString::Printf(TEXT("Summon Imp's cell reads cleanly, said: %s"),
		*Error), One.bValid);
	TestEqual(TEXT("it names one kind"), One.Minions.Num(), 1);
	if (One.Minions.Num() == 1)
	{
		TestEqual(TEXT("and that kind is the Imp"), One.Minions[0].Type,
			FString(TEXT("Imp")));
		TestEqual(TEXT("one of it"), One.Minions[0].Count, 1);
	}

	// THE REST OF THE CELL SURVIVES, which is the half of the bug that was
	// easiest to miss: one unreadable entry marked the whole cell invalid.
	TestEqual(TEXT("Count still read"), One.Count, 1);
	TestEqual(TEXT("MaxActive still read"), One.MaxActive, 3);

	// Iron Fortress's cell. Two kinds at once, which is why this is a list.
	FString FortressError;
	const FCataclysmSkillShapeParams Two = UCataclysmSkillShapes::ParseParams(
		TEXT("Duration=20; Minions=Ballista:2, SpikeTrap:3; HealthPercent=150"),
		&FortressError);

	TestTrue(FString::Printf(TEXT("Iron Fortress's cell reads cleanly, said: %s"),
		*FortressError), Two.bValid);
	TestEqual(TEXT("it names two kinds"), Two.Minions.Num(), 2);
	if (Two.Minions.Num() == 2)
	{
		TestEqual(TEXT("two ballistae"), Two.Minions[0].Type, FString(TEXT("Ballista")));
		TestEqual(TEXT("two of them"), Two.Minions[0].Count, 2);
		// The space after the comma is in the real cell and must not survive
		// into the name, or no row would ever match it.
		TestEqual(TEXT("three spike traps"), Two.Minions[1].Type,
			FString(TEXT("SpikeTrap")));
		TestEqual(TEXT("three of them"), Two.Minions[1].Count, 3);
	}

	// HealthPercent IS NOT HealthCostPercent. One raises what is deployed above
	// its type's own health; the other charges the caster health to use the
	// skill. Reading either as the other would be silent.
	TestEqual(TEXT("HealthPercent reached its own field"),
		Two.MinionHealthPercent, 150.0f);
	TestEqual(TEXT("and did not land on the caster's health cost"),
		Two.HealthCostPercent, 0.0f);

	return true;
}

/**
 * A Minions cell that cannot be read is refused rather than silently emptied.
 *
 * A skill that summons nothing and reports nothing is exactly the failure the
 * parser exists to prevent, and it looks identical to a skill nobody finished.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBadMinionsParameterTest,
	"Cataclysm.SkillShape.AnUnreadableMinionsCellIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBadMinionsParameterTest::RunTest(const FString&)
{
	struct FCase
	{
		const TCHAR* Cell;
		const TCHAR* Why;
	};

	const FCase Cases[] = {
		{ TEXT("Minions=Imp"),        TEXT("no count at all") },
		{ TEXT("Minions=Imp:many"),   TEXT("a count that is not a number") },
		{ TEXT("Minions=Imp:0"),      TEXT("a count of zero, which says nothing") },
		{ TEXT("Minions=:2"),         TEXT("a count with no type") },
	};

	for (const FCase& Case : Cases)
	{
		FString Error;
		const FCataclysmSkillShapeParams Params =
			UCataclysmSkillShapes::ParseParams(Case.Cell, &Error);

		TestFalse(FString::Printf(TEXT("%s is refused (%s)"), Case.Cell, Case.Why),
			Params.bValid);
		TestFalse(FString::Printf(TEXT("and says why for %s"), Case.Cell),
			Error.IsEmpty());
	}

	// And the good case still passes, so the four above are not being refused by
	// something that refuses everything.
	FString GoodError;
	const FCataclysmSkillShapeParams Good =
		UCataclysmSkillShapes::ParseParams(TEXT("Minions=Imp:2"), &GoodError);
	TestTrue(TEXT("a well formed cell is still accepted"), Good.bValid);
	TestEqual(TEXT("and reads its count"), Good.Minions.Num() == 1
		? Good.Minions[0].Count : -1, 2);

	return true;
}

/**
 * The eighth shape is known to the C++, and something runs it.
 *
 * WHAT WENT WRONG. Issue #621: Deployable was added to the generator's shape
 * list on issue #338 and not to the C++ list, so ShapeFromName returned None and
 * the three skills naming it were granted the placeholder that fills a slot and
 * does nothing. Nothing reported it for as long as nobody ran the full suite.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeployableShapeTest,
	"Cataclysm.SkillShape.DeployableIsAShapeWithATemplate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeployableShapeTest::RunTest(const FString&)
{
	const ECataclysmSkillShape Shape =
		UCataclysmSkillShapes::ShapeFromName(TEXT("Deployable"));

	TestEqual(TEXT("the Shape column's word maps to the enum"),
		Shape, ECataclysmSkillShape::Deployable);
	TestTrue(TEXT("and an ability class runs it"),
		UCataclysmWeaponSkills::TemplateFor(Shape) != nullptr);

	// SPELLED AS THE SHEET SPELLS IT. The lookup ignores case, and a word that
	// is merely close must still miss, or a typo in the sheet would silently
	// select the wrong template.
	TestEqual(TEXT("case does not matter"),
		UCataclysmSkillShapes::ShapeFromName(TEXT("deployable")),
		ECataclysmSkillShape::Deployable);
	TestEqual(TEXT("but a different word is not it"),
		UCataclysmSkillShapes::ShapeFromName(TEXT("Deploy")),
		ECataclysmSkillShape::None);

	// It is its own shape rather than an alias for Summon. A summon spawns
	// things that walk to the enemy; a deployable places things that stay put.
	TestNotEqual(TEXT("Deployable is not Summon"),
		UCataclysmWeaponSkills::TemplateFor(ECataclysmSkillShape::Deployable),
		UCataclysmWeaponSkills::TemplateFor(ECataclysmSkillShape::Summon));

	return true;
}

/**
 * Burning ground states what it deals, and the rule it states is one hit.
 *
 * WHAT WENT WRONG. Issue #590: the engine derived a patch's damage from the Burn
 * status effect -- 20% of a hit over 4 seconds, so 5% per second -- rather than
 * from the GroundPercent the sheet writes. Because that figure ignored the
 * patch's own duration, a three second patch was worth 15% of a hit and a ten
 * second one 50%: a longer patch was automatically a bigger one, which is the
 * exact property the rule decided on issue #361 exists to remove.
 *
 * CHECKED AGAINST THE REAL TABLE rather than one made-up cell, because the rule
 * is a property of all 22 rows that leave ground and one of them being wrong is
 * what would actually happen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundPercentTest,
	"Cataclysm.SkillShape.BurningGroundCostsExactlyOneHitOverItsWholeLife",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGroundPercentTest::RunTest(const FString&)
{
	// Molten Cleave's cell, verbatim. 6 seconds at 16.7% a second is one hit.
	FString Error;
	const FCataclysmSkillShapeParams Params = UCataclysmSkillShapes::ParseParams(
		TEXT("Radius=4; Angle=120; Burn=1; GroundRadius=4; GroundDuration=6; "
			 "GroundPercent=16.7"), &Error);

	TestTrue(FString::Printf(TEXT("the cell reads cleanly, said: %s"), *Error),
		Params.bValid);
	TestEqual(TEXT("GroundPercent reached its own field"),
		Params.GroundPercent, 16.7f);

	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!Table)
	{
		AddError(TEXT("Could not load the weapon skill matrix. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	int32 Checked = 0;
	Table->ForeachRow<FCataclysmWeaponSkillRow>(TEXT("FCataclysmGroundPercentTest"),
		[&](const FName& RowName, const FCataclysmWeaponSkillRow& Row)
		{
			const FCataclysmSkillShapeParams Read =
				UCataclysmSkillShapes::ParseParams(Row.ShapeParams);
			if (!Read.LeavesGround())
			{
				return;
			}

			++Checked;

			if (Read.GroundPercent <= 0.0f)
			{
				AddError(FString::Printf(
					TEXT("%s leaves ground and states no GroundPercent, so that "
						 "ground would deal nothing"), *RowName.ToString()));
				return;
			}

			// The rule: a full stay costs one hit. Percent per second times
			// seconds is 100. Half a percentage point of slack, because the
			// sheet writes the figure to one decimal -- 16.7 for six seconds is
			// 100.2, not 100.
			const float WholeStay = Read.GroundPercent * Read.GroundDuration;
			if (!FMath::IsNearlyEqual(WholeStay, 100.0f, 0.5f))
			{
				AddError(FString::Printf(
					TEXT("%s: standing in its ground for all %.0f seconds costs "
						 "%.1f%% of a hit, and the rule is 100%%"),
					*RowName.ToString(), Read.GroundDuration, WholeStay));
			}
		});

	// A floor, so that losing the column entirely fails rather than passing
	// vacuously with nothing to check.
	//
	// TEN SINCE 2026-09-01, NOT TWENTY. The Demonic verb rewrite made burning
	// ground the Greataxe's verb rather than the whole element's habit, so the
	// number of rows leaving ground fell from 22 to 12.
	// FEWEST_SKILLS_LEAVING_GROUND in tools/tests/test_burning_ground_damage.py
	// is this same floor and moved to 10 at the time; this copy did not, and a
	// stale DT_WeaponSkills asset hid the disagreement, because the asset still
	// held the data where 22 rows left ground. Rebuilding the asset showed it.
	TestTrue(FString::Printf(
		TEXT("at least ten rows leave burning ground (found %d)"), Checked),
		Checked >= 10);

	return true;
}

/**
 * A minion is made from its type's row, not from one set of constants.
 *
 * WHAT WENT WRONG. `game/Data/MinionTypes.csv` has held five stat blocks since
 * issue #336 and no gameplay code read it: every minion carried the same
 * compile-time reach, notice radius and firing rate, so a ballista and an imp
 * were the same creature under different names. Issue #340 recorded it; issue
 * #622 is what needed it, because a skill that cannot say what it summons cannot
 * be told apart from one that summons the wrong thing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionTypeTableTest,
	"Cataclysm.SkillShape.EveryMinionTypeStatesItsOwnNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMinionTypeTableTest::RunTest(const FString&)
{
	const UDataTable* Table = ACataclysmMinion::LoadTypeTable();
	if (!Table)
	{
		AddError(TEXT("DT_MinionTypes does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	// Two that must differ, and differ in every number that drives behaviour. If
	// a change made the lookup always return the same row, this is what says so.
	const FCataclysmMinionTypeRow* Imp =
		ACataclysmMinion::FindType(Table, TEXT("Imp"));
	const FCataclysmMinionTypeRow* Ballista =
		ACataclysmMinion::FindType(Table, TEXT("Ballista"));

	if (!Imp || !Ballista)
	{
		AddError(TEXT("The minion type table has no Imp row or no Ballista row."));
		return false;
	}

	TestTrue(TEXT("a ballista reaches further than an imp"),
		Ballista->ReachCm > Imp->ReachCm);
	TestTrue(TEXT("and fires more slowly"),
		Ballista->AttackIntervalSeconds > Imp->AttackIntervalSeconds);

	// THE ONE THAT MAKES A DEPLOYABLE A DEPLOYABLE. A ballista does not move and
	// an imp does, and that difference is data rather than a rule in the
	// ability class. Issue #621.
	TestEqual(TEXT("a ballista does not move"), Ballista->MoveSpeed, 0.0f);
	TestTrue(TEXT("an imp does"), Imp->MoveSpeed > 0.0f);

	// A name the table does not have gets nothing, rather than the first row.
	TestNull(TEXT("a misspelled type is not found"),
		ACataclysmMinion::FindType(Table, TEXT("Balista")));
	TestNull(TEXT("and neither is an empty name"),
		ACataclysmMinion::FindType(Table, FString()));
	TestNull(TEXT("and a null table is survivable"),
		ACataclysmMinion::FindType(nullptr, TEXT("Imp")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnHasNumbersTest,
	"Cataclysm.SkillShape.BurnStatesBothADurationAndAnAmount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBurnHasNumbersTest::RunTest(const FString&)
{
	// FIFTEEN OF THE SIXTEEN DESIGNED DEMONIC SKILLS APPLY BURN, and until issue
	// #895 the design stated neither how long it lasts nor what it deals. Both
	// halves are needed: a burn lasting no time and a burn worth no damage are
	// both a burn that does nothing, and neither is distinguishable from one
	// nobody wrote.
	//
	// RENAMED FROM "...AShareOfTheHit" on 2026-08-24, when the project owner
	// moved the ailments from a percent of the hit to a flat amount per tick.
	const FCataclysmStatusEffectNumbers Burn = UCataclysmSkillEffects::BurnNumbers();

	TestTrue(TEXT("Burn is usable"), Burn.bUsable);
	TestTrue(FString::Printf(TEXT("Burn lasts a positive time (%.1fs)"),
		Burn.DurationSeconds), Burn.DurationSeconds > 0.0f);

	// ASKED THROUGH DamagePerTickAgainst RATHER THAN OF ONE COLUMN. Burn stated a
	// percent of the hit until 2026-08-24 and states a flat amount since, and
	// either is a per-tick amount this path can apply. Asserting on PercentOfHit
	// alone is what this did, and it would now fail on a working burn.
	const float PerTick = Burn.DamagePerTickAgainst(100.0f);
	TestTrue(FString::Printf(
		TEXT("Burn is worth a positive amount per tick (%.1f on a 100 hit)"),
		PerTick), PerTick > 0.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
