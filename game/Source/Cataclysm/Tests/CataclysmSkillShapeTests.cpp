// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnHasNumbersTest,
	"Cataclysm.SkillShape.BurnStatesBothADurationAndAShareOfTheHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBurnHasNumbersTest::RunTest(const FString&)
{
	// FIFTEEN OF THE SIXTEEN DESIGNED DEMONIC SKILLS APPLY BURN, and until this
	// change the design stated neither how long it lasts nor what it deals. Both
	// halves are needed: a burn lasting no time and a burn worth no damage are
	// both a burn that does nothing, and neither is distinguishable from one
	// nobody wrote.
	const FCataclysmStatusEffectNumbers Burn = UCataclysmSkillEffects::BurnNumbers();

	TestTrue(TEXT("Burn is usable"), Burn.bUsable);
	TestTrue(FString::Printf(TEXT("Burn lasts a positive time (%.1fs)"),
		Burn.DurationSeconds), Burn.DurationSeconds > 0.0f);
	TestTrue(FString::Printf(TEXT("Burn is worth a positive share (%.0f%%)"),
		Burn.PercentOfHit), Burn.PercentOfHit > 0.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
