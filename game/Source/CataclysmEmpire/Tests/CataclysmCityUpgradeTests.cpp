// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireRun.h"

/**
 * Tests for city upgrades: the slots, what a purchase refuses, and what each of
 * the ten built effects does.
 *
 * WHAT THESE PIN AND WHAT `Cataclysm.CityUpgrade.EveryRowOfTheTableMapsToAnEffect`
 * PINS. These check behaviour: that a slot is spent, that a duplicate is
 * refused, that a resisted bite takes less. That test, in the `Cataclysm` module
 * beside the DataTable, checks the other half -- that all 24 rows of
 * `game/Data/CityUpgrades.csv` are recognised and that no two map to one effect.
 * Neither can do the other's job, because this module cannot see the table at
 * all.
 *
 * WHY THE PILLAR IS USED FOR THE TIMED TESTS. Advancing a day fires surges, and
 * a dungeon landing on the city under test would move the very figure being
 * measured. `UCataclysmSurgeScheduler::PillarTargetWeight` is zero, so the
 * Pillar is never a target while it is sealed. Every test that relies on that
 * asserts it afterwards rather than trusting it, because a test whose control
 * quietly broke reports a pass that means nothing.
 */

namespace CataclysmCityUpgradeTest
{
	UCataclysmEmpireRun* MakeRun(int32 LethalityRung = 0)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(/* InSeed */ 12345, ECataclysmSurgeMode::Static,
				   LethalityRung);
		return Run;
	}

	/** An upgrade built by hand, so a test never depends on the DataTable. */
	FCataclysmCityUpgrade Make(ECataclysmCityUpgradeEffect Effect, float Value,
							   FName RowName, float IntervalDays = 0.0f)
	{
		FCataclysmCityUpgrade Upgrade;
		Upgrade.RowName = RowName;
		Upgrade.Effect = Effect;
		Upgrade.Value = Value;
		Upgrade.IntervalDays = IntervalDays;
		return Upgrade;
	}

	/**
	 * Puts a dungeon on a city with a chosen timer, without waiting for a surge
	 * to roll one.
	 *
	 * THE TWO LISTS ARE WRITTEN TOGETHER, which is what
	 * `UCataclysmEmpireRun::ClearDungeon` warns about: the run holds the dungeon
	 * and the clock holds its timer, and a dungeon in one and not the other is
	 * the bug that split exists to prevent.
	 */
	void PlaceDungeon(UCataclysmEmpireRun& Run, int32 CityId, int32 DungeonId,
					  float ResolveDays, float DefenceBite = 0.0f)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.CityId = CityId;
		Dungeon.Floors = 10;
		Dungeon.ResolveDays = ResolveDays;
		Dungeon.DefenceBite = DefenceBite;

		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}
}

// ---------------------------------------------------------------------------
// The rules
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeSlotsTest,
	"Cataclysm.CityUpgrade.HereticGivesACityTwoSlotsInsteadOfThree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeSlotsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityUpgradeTest;

	// THE DESIGN DOCUMENT'S SENTENCE, section IX: "Each city has upgrade slots
	// (3 normally, 2 on Heretic difficulty)", and the difficulty table's Heretic
	// row says the same from the other side.
	TestEqual(TEXT("Standard gives three"),
			  UCataclysmCityUpgradeRules::SlotsFor(0), 3);
	TestEqual(TEXT("Hardcore gives three as well"),
			  UCataclysmCityUpgradeRules::SlotsFor(1), 3);
	TestEqual(TEXT("Heretic gives two"),
			  UCataclysmCityUpgradeRules::SlotsFor(2), 2);

	// AND A RUN CARRIES IT THROUGH TO THE MAP. Issue #318 is the same figure in
	// the simulation, where it still cannot be measured.
	UCataclysmEmpireRun* Standard = MakeRun(0);
	UCataclysmEmpireRun* Heretic = MakeRun(2);

	TestEqual(TEXT("a Standard run's cities have three slots"),
			  Standard->Map->UpgradeSlots, 3);
	TestEqual(TEXT("a Heretic run's cities have two"),
			  Heretic->Map->UpgradeSlots, 2);

	TestEqual(TEXT("and an untouched city has all of them free"),
			  Heretic->Map->FreeUpgradeSlots(0), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeFreeAndInstantTest,
	"Cataclysm.CityUpgrade.AnUpgradeIsStillFreeAndImmediate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeFreeAndInstantTest::RunTest(const FString& Parameters)
{
	// THIS TEST EXISTS TO BE CHANGED DELIBERATELY. The design has five empire
	// tree nodes that only make sense if a city upgrade costs gold and takes
	// days to build, and neither number is written anywhere; there is no gold in
	// this game at all. Both constants are zero until that is designed, which
	// the project owner decided on 2026-09-05. Issue #1264.
	TestEqual(TEXT("an upgrade costs no gold"),
			  UCataclysmCityUpgradeRules::GoldCost, 0);
	TestEqual(TEXT("and takes no days to build"),
			  UCataclysmCityUpgradeRules::BuildDays, 0);
	TestTrue(TEXT("so it is free and immediate"),
			 UCataclysmCityUpgradeRules::IsFreeAndInstant());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeWhichAreBuiltTest,
	"Cataclysm.CityUpgrade.TenOfTheTwentyFourEffectsAreBuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeWhichAreBuiltTest::RunTest(const FString& Parameters)
{
	// ONE VALUE PER ROW OF THE SHEET. If a row is added, a value belongs here
	// and this fails until it is added.
	TestEqual(TEXT("the enum names twenty-four effects"),
			  UCataclysmCityUpgradeRules::AllEffects().Num(), 24);

	// THE TEN ARCHITECT UPGRADES AND NOTHING ELSE. Listed rather than counted,
	// so marking the wrong one built fails here instead of shipping an upgrade
	// that silently does nothing.
	const TArray<ECataclysmCityUpgradeEffect> Expected = {
		ECataclysmCityUpgradeEffect::MaxDefence,
		ECataclysmCityUpgradeEffect::MaxPopulation,
		ECataclysmCityUpgradeEffect::RemoveDungeons,
		ECataclysmCityUpgradeEffect::RestoreDefence,
		ECataclysmCityUpgradeEffect::RestorePopulation,
		ECataclysmCityUpgradeEffect::ResistDefenceLoss,
		ECataclysmCityUpgradeEffect::ResistPopulationLoss,
		ECataclysmCityUpgradeEffect::HealDefenceEvery,
		ECataclysmCityUpgradeEffect::RecoverPopulationEvery,
		ECataclysmCityUpgradeEffect::RestoreDefenceOnClear,
	};

	for (const ECataclysmCityUpgradeEffect Effect :
		 UCataclysmCityUpgradeRules::AllEffects())
	{
		const bool bShouldBeBuilt = Expected.Contains(Effect);

		TestEqual(*FString::Printf(
					  TEXT("%s is %s"),
					  *UCataclysmCityUpgradeRules::EffectName(Effect),
					  bShouldBeBuilt ? TEXT("built") : TEXT("not built")),
				  UCataclysmCityUpgradeRules::IsBuilt(Effect), bShouldBeBuilt);
	}

	TestEqual(TEXT("which is ten of the twenty-four"),
			  UCataclysmCityUpgradeRules::BuiltEffectCount(), 10);

	// `None` IS NOT AN UPGRADE and must never be buildable, or a
	// default-constructed struct would buy something.
	TestFalse(TEXT("None is not built"),
			  UCataclysmCityUpgradeRules::IsBuilt(
				  ECataclysmCityUpgradeEffect::None));

	return true;
}

// ---------------------------------------------------------------------------
// What a purchase refuses
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeRefusalsTest,
	"Cataclysm.CityUpgrade.EveryRefusalSaysWhichOneItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeRefusalsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityUpgradeTest;

	const FCataclysmCityUpgrade Resist =
		Make(ECataclysmCityUpgradeEffect::ResistDefenceLoss, 0.25f,
			 TEXT("Resist"));

	// A RUN THAT NEVER BEGAN has no map to buy anything on.
	UCataclysmEmpireRun* Unbegun = NewObject<UCataclysmEmpireRun>();
	TestEqual(TEXT("a run that has not begun refuses"),
			  Unbegun->BuyCityUpgrade(0, Resist),
			  ECataclysmCityUpgradeResult::RunHasNotBegun);

	UCataclysmEmpireRun* Run = MakeRun();

	TestEqual(TEXT("a default upgrade is not an upgrade"),
			  Run->BuyCityUpgrade(0, FCataclysmCityUpgrade()),
			  ECataclysmCityUpgradeResult::NotAnUpgrade);

	TestEqual(TEXT("there is no city 99"),
			  Run->BuyCityUpgrade(99, Resist),
			  ECataclysmCityUpgradeResult::NoSuchCity);

	// AN EFFECT THAT DOES NOTHING IS REFUSED RATHER THAN SOLD. Fourteen of the
	// twenty-four are waiting on a system that does not exist, and a slot spent
	// on one would buy the player nothing at all.
	const FCataclysmCityUpgrade NotBuilt =
		Make(ECataclysmCityUpgradeEffect::DungeonCap, 15.0f, TEXT("Cap"));

	TestEqual(TEXT("an effect that is not built is refused"),
			  Run->BuyCityUpgrade(0, NotBuilt),
			  ECataclysmCityUpgradeResult::EffectNotBuiltYet);

	TestEqual(TEXT("and buying it changed nothing"),
			  Run->Map->Find(0)->Upgrades.Num(), 0);

	// A FALLEN CITY CANNOT BE IMPROVED.
	Run->Map->Fall(0);
	TestEqual(TEXT("a fallen city refuses"),
			  Run->BuyCityUpgrade(0, Resist),
			  ECataclysmCityUpgradeResult::CityHasFallen);

	// A DUPLICATE IS REFUSED BY ROW NAME, so a slot cannot be spent twice on the
	// same upgrade.
	TestEqual(TEXT("the first purchase goes through"),
			  Run->BuyCityUpgrade(1, Resist),
			  ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("the same upgrade again is refused"),
			  Run->BuyCityUpgrade(1, Resist),
			  ECataclysmCityUpgradeResult::AlreadyBought);
	TestEqual(TEXT("and only one slot was spent"),
			  Run->Map->Find(1)->Upgrades.Num(), 1);

	// AND THE SLOTS RUN OUT AT THREE.
	TestEqual(TEXT("a second, different upgrade fits"),
			  Run->BuyCityUpgrade(1,
								  Make(ECataclysmCityUpgradeEffect::MaxDefence,
									   0.2f, TEXT("MaxDef"))),
			  ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("a third fits"),
			  Run->BuyCityUpgrade(1,
								  Make(ECataclysmCityUpgradeEffect::MaxPopulation,
									   0.2f, TEXT("MaxPop"))),
			  ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("a fourth does not"),
			  Run->BuyCityUpgrade(1,
								  Make(ECataclysmCityUpgradeEffect::RestoreDefence,
									   0.5f, TEXT("Restore"))),
			  ECataclysmCityUpgradeResult::NoSlotsLeft);

	TestEqual(TEXT("so the city holds three"),
			  Run->Map->Find(1)->Upgrades.Num(), 3);
	TestEqual(TEXT("and has none free"), Run->Map->FreeUpgradeSlots(1), 0);

	return true;
}

// ---------------------------------------------------------------------------
// What each built effect does
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeMaximaTest,
	"Cataclysm.CityUpgrade.ARaisedMaximumDoesNotMakeAFullCityLookDamaged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeMaximaTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityUpgradeTest;

	UCataclysmEmpireRun* Run = MakeRun();

	const float StartMax = Run->Map->Find(0)->MaxDefence;

	// A CITY AT FULL DEFENCE. Buying "increase max defence by 20%" must leave it
	// at full defence. Raising only the maximum would make an untouched city
	// read as 83% damaged the moment a player improved it.
	TestEqual(TEXT("it starts full"), Run->Map->Find(0)->DefenceFraction(), 1.0f,
			  0.0001f);

	TestEqual(TEXT("the purchase goes through"),
			  Run->BuyCityUpgrade(0,
								  Make(ECataclysmCityUpgradeEffect::MaxDefence,
									   0.2f, TEXT("MaxDef"))),
			  ECataclysmCityUpgradeResult::Bought);

	TestEqual(TEXT("the maximum rose by a fifth"),
			  Run->Map->Find(0)->MaxDefence, StartMax * 1.2f, 0.01f);
	TestEqual(TEXT("and it is still full"),
			  Run->Map->Find(0)->DefenceFraction(), 1.0f, 0.0001f);

	// A DAMAGED CITY KEEPS THE SAME ABSOLUTE SHORTFALL. City 1 is bitten for a
	// tenth of its maximum first, so it is short by exactly that much; after the
	// upgrade it must still be short by exactly that much, not by a tenth of the
	// new, larger maximum.
	const float SecondMax = Run->Map->Find(1)->MaxDefence;
	Run->Map->Bite(1, 0.1f, 0.0f);

	const float Missing = SecondMax - Run->Map->Find(1)->Defence;
	TestEqual(TEXT("it is short by a tenth"), Missing, SecondMax * 0.1f, 0.01f);

	Run->BuyCityUpgrade(1, Make(ECataclysmCityUpgradeEffect::MaxDefence, 0.2f,
								TEXT("MaxDef")));

	TestEqual(TEXT("and is short by the same amount afterwards"),
			  Run->Map->Find(1)->MaxDefence - Run->Map->Find(1)->Defence,
			  Missing, 0.01f);

	// POPULATION BEHAVES THE SAME WAY.
	const float StartPopulation = Run->Map->Find(2)->MaxPopulation;

	Run->BuyCityUpgrade(2, Make(ECataclysmCityUpgradeEffect::MaxPopulation, 0.2f,
								TEXT("MaxPop")));

	TestEqual(TEXT("maximum population rose by a fifth"),
			  Run->Map->Find(2)->MaxPopulation, StartPopulation * 1.2f, 0.01f);
	TestEqual(TEXT("and the city is still full of people"),
			  Run->Map->Find(2)->Population,
			  Run->Map->Find(2)->MaxPopulation, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeRestoreTest,
	"Cataclysm.CityUpgrade.AOneTimeRestoreCannotOverfillACity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeRestoreTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityUpgradeTest;

	UCataclysmEmpireRun* Run = MakeRun();

	// THE RESTORE IS A SHARE OF THE MAXIMUM, NOT OF WHAT IS MISSING. A city down
	// to 30% restored by 50% ends at 80%, because it got back half of its
	// maximum rather than half of the gap. That is the same convention `Bite`
	// uses from the other side, and it is what makes a restore worth a fixed
	// amount rather than more on a healthier city.
	Run->Map->Bite(0, 0.7f, 0.7f);
	TestEqual(TEXT("it is down to three tenths"),
			  Run->Map->Find(0)->DefenceFraction(), 0.3f, 0.001f);

	Run->BuyCityUpgrade(0, Make(ECataclysmCityUpgradeEffect::RestoreDefence,
								0.5f, TEXT("RestoreDef")));

	TestEqual(TEXT("half its maximum came back, taking it to eight tenths"),
			  Run->Map->Find(0)->DefenceFraction(), 0.8f, 0.001f);

	Run->BuyCityUpgrade(0, Make(ECataclysmCityUpgradeEffect::RestorePopulation,
								0.5f, TEXT("RestorePop")));

	TestEqual(TEXT("and population likewise"),
			  Run->Map->Find(0)->Population,
			  Run->Map->Find(0)->MaxPopulation * 0.8f, 0.01f);

	// AND IT CANNOT OVERFILL. A city only a tenth down, restored by half, would
	// reach 140% of its maximum if nothing clamped it. City 1 is untouched so
	// far, so this is a fresh case rather than a continuation of the one above.
	Run->Map->Bite(1, 0.1f, 0.1f);
	TestEqual(TEXT("the second city is a tenth down"),
			  Run->Map->Find(1)->DefenceFraction(), 0.9f, 0.001f);

	Run->BuyCityUpgrade(1, Make(ECataclysmCityUpgradeEffect::RestoreDefence,
								0.5f, TEXT("RestoreDef")));
	Run->BuyCityUpgrade(1, Make(ECataclysmCityUpgradeEffect::RestorePopulation,
								0.5f, TEXT("RestorePop")));

	TestEqual(TEXT("defence stopped exactly at full rather than reaching 140%"),
			  Run->Map->Find(1)->Defence, Run->Map->Find(1)->MaxDefence, 0.01f);
	TestEqual(TEXT("and so did population"),
			  Run->Map->Find(1)->Population, Run->Map->Find(1)->MaxPopulation,
			  0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeResistTest,
	"Cataclysm.CityUpgrade.AResistantCityLosesLessToTheSameDungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeResistTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityUpgradeTest;

	UCataclysmEmpireRun* Run = MakeRun();

	// TWO CITIES OF THE SAME TIER, so the comparison is of the upgrade and not
	// of the cities. Cities 0 and 1 are both on the rim.
	const FCataclysmCity* Plain = Run->Map->Find(0);
	const FCataclysmCity* Resistant = Run->Map->Find(1);

	if (!TestEqual(TEXT("the two cities are the same tier"),
				   static_cast<int32>(Plain->Tier),
				   static_cast<int32>(Resistant->Tier)))
	{
		return false;
	}

	Run->BuyCityUpgrade(1, Make(ECataclysmCityUpgradeEffect::ResistDefenceLoss,
								0.25f, TEXT("ResistDef")));
	Run->BuyCityUpgrade(1,
						Make(ECataclysmCityUpgradeEffect::ResistPopulationLoss,
							 0.25f, TEXT("ResistPop")));

	// THE SAME BITE ON BOTH.
	Run->Map->Bite(0, 0.4f, 0.4f);
	Run->Map->Bite(1, 0.4f, 0.4f);

	const float PlainLost = Plain->MaxDefence - Plain->Defence;
	const float ResistantLost = Resistant->MaxDefence - Resistant->Defence;

	TestEqual(TEXT("the plain city lost the whole four tenths"),
			  PlainLost, Plain->MaxDefence * 0.4f, 0.01f);

	// A QUARTER LESS, WHICH IS THREE TENTHS OF THE MAXIMUM RATHER THAN FOUR.
	TestEqual(TEXT("the resistant one lost a quarter less"),
			  ResistantLost, Resistant->MaxDefence * 0.3f, 0.01f);

	TestEqual(TEXT("and the same holds for population"),
			  Resistant->MaxPopulation - Resistant->Population,
			  Resistant->MaxPopulation * 0.3f, 0.01f);

	// THE COMPARISON IS ONLY WORTH ANYTHING IF THE TWO ACTUALLY DIFFER.
	TestTrue(TEXT("the resistant city really did lose less"),
			 ResistantLost < PlainLost - 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeIntervalTest,
	"Cataclysm.CityUpgrade.AnIntervalRepairFiresAfterItsIntervalAndNotBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeIntervalTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityUpgradeTest;

	UCataclysmEmpireRun* Run = MakeRun();

	// THE PILLAR, BECAUSE NO SURGE EVER TARGETS IT while it is sealed --
	// `UCataclysmSurgeScheduler::PillarTargetWeight` is zero. A dungeon landing
	// on the city under test would move the figure being measured. Asserted at
	// the end rather than trusted.
	const int32 CityId = Run->Map->PillarId;

	Run->Map->Bite(CityId, 0.5f, 0.5f);
	const float Damaged = Run->Map->Find(CityId)->Defence;

	TestEqual(TEXT("the purchase goes through"),
			  Run->BuyCityUpgrade(
				  CityId, Make(ECataclysmCityUpgradeEffect::HealDefenceEvery,
							   0.05f, TEXT("Heal"), /* IntervalDays */ 20.0f)),
			  ECataclysmCityUpgradeResult::Bought);

	// NOTHING ON DAY ONE, and nothing on day nineteen. An upgrade bought on day
	// zero first fires on day twenty.
	Run->AdvanceDays(19);

	TestEqual(TEXT("nineteen days pass and nothing is repaired"),
			  Run->Map->Find(CityId)->Defence, Damaged, 0.01f);

	Run->AdvanceDay();

	TestEqual(TEXT("on the twentieth day it repairs a twentieth"),
			  Run->Map->Find(CityId)->Defence,
			  Damaged + Run->Map->Find(CityId)->MaxDefence * 0.05f, 0.01f);

	// AND ONLY ONCE. Nineteen more days must add nothing further.
	const float Healed = Run->Map->Find(CityId)->Defence;
	Run->AdvanceDays(19);

	TestEqual(TEXT("and does not fire again until the next interval"),
			  Run->Map->Find(CityId)->Defence, Healed, 0.01f);

	Run->AdvanceDay();
	TestTrue(TEXT("then it fires a second time"),
			 Run->Map->Find(CityId)->Defence > Healed + 0.01f);

	// THE CONTROL. If a dungeon ever landed on the Pillar, everything above was
	// measuring a bite as well as a repair and none of it is evidence.
	TestEqual(TEXT("no dungeon ever stood on the Pillar"),
			  Run->DungeonsOn(CityId).Num(), 0);
	TestFalse(TEXT("and it never fell"), Run->Map->Find(CityId)->bFallen);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeFallenCityTest,
	"Cataclysm.CityUpgrade.AFallenCityRepairsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeFallenCityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityUpgradeTest;

	UCataclysmEmpireRun* Run = MakeRun();

	const int32 CityId = Run->Map->PillarId;

	Run->BuyCityUpgrade(
		CityId, Make(ECataclysmCityUpgradeEffect::HealDefenceEvery, 0.05f,
					 TEXT("Heal"), /* IntervalDays */ 5.0f));

	// THE CITY FALLS BEFORE THE FIRST TRIGGER. A fallen city has no defence to
	// repair, and letting one climb off zero while still marked fallen would put
	// the map in a state nothing else expects.
	Run->Map->Fall(CityId);

	if (!TestTrue(TEXT("the city has fallen"),
				  Run->Map->Find(CityId)->bFallen))
	{
		return false;
	}

	Run->AdvanceDays(12);

	TestEqual(TEXT("two intervals pass and its defence is still nothing"),
			  Run->Map->Find(CityId)->Defence, 0.0f, 0.001f);
	TestTrue(TEXT("and it is still fallen"), Run->Map->Find(CityId)->bFallen);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeClearRestoreTest,
	"Cataclysm.CityUpgrade.ClearingADungeonRepairsTheCityButAbsorbingOneDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeClearRestoreTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityUpgradeTest;

	UCataclysmEmpireRun* Run = MakeRun();

	const int32 CityId = Run->Map->PillarId;

	Run->BuyCityUpgrade(
		CityId, Make(ECataclysmCityUpgradeEffect::RestoreDefenceOnClear, 0.05f,
					 TEXT("OnClear")));

	Run->Map->Bite(CityId, 0.5f, 0.0f);
	const float Damaged = Run->Map->Find(CityId)->Defence;
	const float MaxDefence = Run->Map->Find(CityId)->MaxDefence;

	// THE PLAYER BEATS A DUNGEON STANDING ON IT.
	PlaceDungeon(*Run, CityId, /* DungeonId */ 900, /* ResolveDays */ 50.0f);

	TestTrue(TEXT("the dungeon was there to clear"), Run->ClearDungeon(900));

	TestEqual(TEXT("clearing it repaired a twentieth of the maximum"),
			  Run->Map->Find(CityId)->Defence, Damaged + MaxDefence * 0.05f,
			  0.01f);

	// AND A DUNGEON ABSORBED BY THE CITY FALLING REPAIRS NOTHING. This is the
	// trap the split between `ClearDungeon` and `RemoveDungeon` exists to
	// prevent: a city that falls absorbs every dungeon standing on it, and if
	// that went through `ClearDungeon` the city's own killers would heal it on
	// the way down.
	const int32 Second = 1;

	Run->BuyCityUpgrade(
		Second, Make(ECataclysmCityUpgradeEffect::RestoreDefenceOnClear, 0.05f,
					 TEXT("OnClear")));

	// THREE DUNGEONS, so a mistake would be three times 5% rather than a
	// rounding difference. The one that resolves takes the whole city.
	PlaceDungeon(*Run, Second, /* DungeonId */ 901, /* ResolveDays */ 40.0f);
	PlaceDungeon(*Run, Second, /* DungeonId */ 902, /* ResolveDays */ 40.0f);
	PlaceDungeon(*Run, Second, /* DungeonId */ 903, /* ResolveDays */ 1.0f,
				 /* DefenceBite */ 5.0f);

	Run->AdvanceDay();

	if (!TestTrue(TEXT("the city fell"), Run->Map->Find(Second)->bFallen))
	{
		return false;
	}

	TestEqual(TEXT("its defence is nothing at all"),
			  Run->Map->Find(Second)->Defence, 0.0f, 0.001f);
	TestEqual(TEXT("and every dungeon on it was absorbed"),
			  Run->DungeonsOn(Second).Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeRemoveDungeonsTest,
	"Cataclysm.CityUpgrade.RemovingAShareOfDungeonsTakesTheMostUrgentFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeRemoveDungeonsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmCityUpgradeTest;

	UCataclysmEmpireRun* Run = MakeRun();

	const int32 CityId = Run->Map->PillarId;

	// FOUR DUNGEONS WITH DIFFERENT TIMERS, so a quarter is exactly one and which
	// one is a real question rather than a rounding artefact.
	PlaceDungeon(*Run, CityId, /* DungeonId */ 910, /* ResolveDays */ 30.0f);
	PlaceDungeon(*Run, CityId, /* DungeonId */ 911, /* ResolveDays */ 40.0f);
	PlaceDungeon(*Run, CityId, /* DungeonId */ 912, /* ResolveDays */ 20.0f);
	PlaceDungeon(*Run, CityId, /* DungeonId */ 913, /* ResolveDays */ 50.0f);

	TestEqual(TEXT("four dungeons stand on it"),
			  Run->DungeonsOn(CityId).Num(), 4);

	TestEqual(TEXT("the purchase goes through"),
			  Run->BuyCityUpgrade(
				  CityId, Make(ECataclysmCityUpgradeEffect::RemoveDungeons,
							   0.25f, TEXT("Remove"))),
			  ECataclysmCityUpgradeResult::Bought);

	TestEqual(TEXT("a quarter of four is one, so three are left"),
			  Run->DungeonsOn(CityId).Num(), 3);

	// THE MOST URGENT ONE WENT. Dungeon 912 was due in twenty days, sooner than
	// any other. The sheet names no order, so this is a judgement recorded in
	// docs/DECISIONS.md rather than something the design states.
	TestFalse(TEXT("the soonest to resolve is gone"),
			  Run->DungeonsOn(CityId).Contains(912));
	TestTrue(TEXT("the one due in thirty days is still there"),
			 Run->DungeonsOn(CityId).Contains(910));
	TestTrue(TEXT("and so is the one due in fifty"),
			 Run->DungeonsOn(CityId).Contains(913));

	// AND ITS TIMER WENT WITH IT. A dungeon left on the clock would keep biting
	// a city that no longer has it.
	TestEqual(TEXT("the removed dungeon has no timer left"),
			  Run->Clock->Timers.FilterByPredicate(
						   [](const FCataclysmDungeonTimer& Timer)
						   {
							   return Timer.DungeonId == 912;
						   })
				  .Num(),
			  0);

	// A QUARTER OF ONE DUNGEON IS NONE. Rounding, so that a proportional effect
	// on a single dungeon does not quietly become the whole of it.
	const int32 Lonely = 2;
	PlaceDungeon(*Run, Lonely, /* DungeonId */ 920, /* ResolveDays */ 30.0f);

	Run->BuyCityUpgrade(Lonely,
						Make(ECataclysmCityUpgradeEffect::RemoveDungeons, 0.25f,
							 TEXT("Remove")));

	TestEqual(TEXT("a quarter of one dungeon removes none"),
			  Run->DungeonsOn(Lonely).Num(), 1);

	// AND DISPERSING A DUNGEON IS NOT BEATING ONE. This is the case the split
	// between `ClearDungeon` and `RemoveDungeon` actually protects: a city that
	// falls is already marked fallen, so the check on that would catch a mistake
	// there anyway, but a HEALTHY city buying "remove 25% of dungeons" is not
	// protected by anything else. If that removal went through `ClearDungeon`,
	// the city would be repaired once per dungeon dispersed by building work,
	// which nobody walked.
	const int32 Both = 3;

	Run->BuyCityUpgrade(
		Both, Make(ECataclysmCityUpgradeEffect::RestoreDefenceOnClear, 0.05f,
				   TEXT("OnClear")));

	Run->Map->Bite(Both, 0.5f, 0.0f);
	const float BeforeRemoval = Run->Map->Find(Both)->Defence;

	PlaceDungeon(*Run, Both, /* DungeonId */ 930, /* ResolveDays */ 30.0f);
	PlaceDungeon(*Run, Both, /* DungeonId */ 931, /* ResolveDays */ 40.0f);
	PlaceDungeon(*Run, Both, /* DungeonId */ 932, /* ResolveDays */ 20.0f);
	PlaceDungeon(*Run, Both, /* DungeonId */ 933, /* ResolveDays */ 50.0f);

	Run->BuyCityUpgrade(Both,
						Make(ECataclysmCityUpgradeEffect::RemoveDungeons, 0.5f,
							 TEXT("Remove")));

	TestEqual(TEXT("half of four dungeons were dispersed"),
			  Run->DungeonsOn(Both).Num(), 2);

	TestFalse(TEXT("the city is not fallen, so nothing else is protecting it"),
			  Run->Map->Find(Both)->bFallen);

	TestEqual(TEXT("and dispersing them repaired nothing"),
			  Run->Map->Find(Both)->Defence, BeforeRemoval, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
