// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmDungeonModifier.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Tests/CataclysmTestRun.h"

/**
 * Demonic's Infernal Rifts on the empire side: issue #51, slice B2.
 *
 * THE OWNER'S RULINGS OF 2026-11-24, each verbatim from issue #51: "Rift = Quest dungeon (Recommended)", "Anywhere but
 * the Pillar (Recommended)", "Every death, no gap (Recommended)", "let's make them 4 floors?", "Keep the short timer for
 * rifts", "Rifts always have 8 dungeon modifiers. Keep it spicy", "No limit" and "(b) 8 modifiers only (Recommended)".
 * A Rift is `FCataclysmDungeon::IsARift`: a Quest dungeon Demonic sent.
 *
 * EVERY RULE HAS A CONTROL: a Quest dungeon another Cataclysm sent, which is not a Rift, or the same Rift where the
 * rule should not act. The model's half is `sim/tests/test_demonic_rifts_as_ruled.py`, and
 * `tools/tests/test_demonic_rifts_port.py` holds the two to the same rules.
 */
namespace CataclysmRiftsTest
{
	/** A run begun on `Seed`, a day passed, and its board and timers cleared, so nothing but what a test puts down
	 *  stands on it. */
	UCataclysmEmpireRun* MakeEmptyRun(int32 Seed = 1)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(Seed, ECataclysmSurgeMode::Static);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	/** A dungeon put down by hand, with its timer, on `City`, opened today. */
	void Place(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmDungeonType Type, ECataclysmType Sender,
			   const FCataclysmCity& City, float ResolveDays = 10000.0f)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.Type = Type;
		Dungeon.SubType = Type == ECataclysmDungeonType::Quest ? ECataclysmDungeonSubType::Elite
															   : ECataclysmDungeonSubType::None;
		Dungeon.Cataclysm = Sender;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Floors = 4;
		Dungeon.ResolveDays = ResolveDays;
		Dungeon.SpawnedDay = Run.Day();
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}

	/** An ordinary dungeon that never runs out, on the Pillar, so no empty-board wave lands (`HoldTheBoard`'s reason in
	 *  `CataclysmEmpireRunTests.cpp`). A wave never targets the Pillar and no Rift sends to it. */
	void HoldTheBoard(UCataclysmEmpireRun& Run)
	{
		Place(Run, 9999, ECataclysmDungeonType::Basic, ECataclysmType::None, *Run.Map->Find(Run.Map->PillarId));
	}

	/** The first city of `Tier`. */
	const FCataclysmCity& FirstOf(const UCataclysmEmpireRun& Run, ECataclysmCityTier Tier)
	{
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.Tier == Tier)
			{
				return City;
			}
		}
		check(false);
		return Run.Map->Cities[0];
	}

	/** The dungeons the Rifts sent over these days, in order. */
	TArray<int32> SentOver(UCataclysmEmpireRun& Run, int32 Days)
	{
		TArray<int32> Sent;
		for (int32 Index = 0; Index < Days; ++Index)
		{
			Sent.Append(Run.AdvanceDay().RiftSent);
		}
		return Sent;
	}

	/** `Count` modifier rows of one Cataclysm, dangers 5 to 20 in turn, keyed `<Name>_<n>`. */
	void AddRows(TArray<FCataclysmDungeonModifier>& Pool, ECataclysmType Cataclysm, const TCHAR* Name, int32 Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FCataclysmDungeonModifier Row;
			Row.RowKey = FName(*FString::Printf(TEXT("%s_%d"), Name, Index));
			Row.ModifierName = Row.RowKey;
			Row.Cataclysm = Cataclysm;
			Row.Danger = 5.0f * (1 + Index % 4);
			Pool.Add(Row);
		}
	}
}

// ---------------------------------------------------------------------------
// What a Rift is: four floors, the timer of four floors
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsFourFloorsTest,
	"Cataclysm.Rifts.ARiftIsFourFloorsWithTheTimerOfFourFloorsWhateverTheTreeAdds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsFourFloorsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	// THE TREE ADDS FIVE FLOORS TO EVERY DUNGEON, so a Rift that took them would read nine.
	Run->Surges->FloorsTheTreeAdds = 5;

	const float Least = UCataclysmDayClock::ResolveDaysFor(UCataclysmSurgeScheduler::RiftFloors)
		* (1.0f - UCataclysmSurgeScheduler::ResolveJitter);
	const float Most = UCataclysmDayClock::ResolveDaysFor(UCataclysmSurgeScheduler::RiftFloors)
		* (1.0f + UCataclysmSurgeScheduler::ResolveJitter);

	for (const ECataclysmCityTier Tier : { ECataclysmCityTier::Outpost, ECataclysmCityTier::Bulwark,
										   ECataclysmCityTier::Sanctuary })
	{
		for (int32 Seed = 1; Seed <= 20; ++Seed)
		{
			FRandomStream Stream(Seed);
			const FCataclysmDungeon Rift = Run->Surges->MakeDungeon(100 + Seed, FirstOf(*Run, Tier), Run->Day(), Stream,
																	true, ECataclysmDungeonType::Quest,
																	ECataclysmType::Demonic);
			TestEqual(TEXT("a Rift is four floors"), Rift.Floors, 4);
			TestEqual(TEXT("and nothing the tree added is counted on it"), Rift.UpgradeFloors, 0);
			TestTrue(TEXT("its sub-type is Demonic's, Elite"), Rift.SubType == ECataclysmDungeonSubType::Elite);
			TestTrue(TEXT("its timer is a 4-floor dungeon's"), Rift.ResolveDays >= Least && Rift.ResolveDays <= Most);

			// CONTROL: the same roll for Death's Quest dungeon keeps the Quest row's depth and the tree's five.
			FRandomStream Same(Seed);
			const FCataclysmDungeon Horde = Run->Surges->MakeDungeon(200 + Seed, FirstOf(*Run, Tier), Run->Day(), Same,
																	 true, ECataclysmDungeonType::Quest,
																	 ECataclysmType::Death);
			TestTrue(TEXT("a Quest dungeon of another Cataclysm keeps the Quest row's depth"), Horde.Floors >= 25);
			TestEqual(TEXT("and takes the tree's floors"), Horde.UpgradeFloors, 5);
			TestTrue(TEXT("and its timer is a deep one's"), Horde.ResolveDays > Most);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsSameDrawsTest,
	"Cataclysm.Rifts.MakingARiftTakesTheDrawsAnyQuestDungeonTakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsSameDrawsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	// THE DEPTH ROLL IS TAKEN AND THROWN AWAY, so the stream stands where a Death Quest dungeon leaves it and every
	// later dungeon of the wave rolls what it rolled before Rifts existed.
	UCataclysmEmpireRun* Run = MakeEmptyRun();
	for (int32 Seed = 1; Seed <= 20; ++Seed)
	{
		FRandomStream ForARift(Seed);
		FRandomStream ForAHorde(Seed);
		Run->Surges->MakeDungeon(1, FirstOf(*Run, ECataclysmCityTier::Outpost), 1, ForARift, true,
								 ECataclysmDungeonType::Quest, ECataclysmType::Demonic);
		Run->Surges->MakeDungeon(1, FirstOf(*Run, ECataclysmCityTier::Outpost), 1, ForAHorde, true,
								 ECataclysmDungeonType::Quest, ECataclysmType::Death);
		TestEqual(TEXT("a Rift leaves the stream where any Quest dungeon does"), ForARift.GetCurrentSeed(),
				  ForAHorde.GetCurrentSeed());
	}
	return true;
}

// ---------------------------------------------------------------------------
// Where a Rift opens
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsOpenAnywhereTest,
	"Cataclysm.Rifts.ARiftOpensOnAnyStandingCityButThePillarByTierWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsOpenAnywhereTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	// AT THE START ONLY THE 12 RIM OUTPOSTS ARE EXPOSED; the 8 Bulwarks and 4 Sanctuaries are sealed. By the tier
	// weights 5, 3 and 1.5 a Rift opens on an Outpost 60 times in 90, a Bulwark 24 and a Sanctuary 6. 300 waves give
	// about 360 Rifts, so the bands below are several standard deviations wide.
	UCataclysmEmpireRun* Run = MakeEmptyRun();
	TMap<ECataclysmCityTier, int32> RiftsOn;
	int32 Rifts = 0;
	int32 HordesOffTheFront = 0;
	int32 Hordes = 0;

	for (int32 Seed = 1; Seed <= 300; ++Seed)
	{
		FRandomStream Stream(Seed);
		const TArray<FCataclysmDungeon> Wave = Run->Surges->RollWave(
			*Run->Map, 1, 0, Stream, TArray<int32>(), TArray<int32>(), []() { return ECataclysmType::Demonic; },
			false, /* RiftSeed */ Seed);
		for (const FCataclysmDungeon& Dungeon : Wave)
		{
			if (!Dungeon.IsARift())
			{
				continue;
			}
			const FCataclysmCity* City = Run->Map->Find(Dungeon.CityId);
			TestTrue(TEXT("a Rift opens on a city"), City != nullptr);
			TestFalse(TEXT("never on the Pillar"), Dungeon.CityId == Run->Map->PillarId);
			TestTrue(TEXT("never on a fallen city"), City != nullptr && City->IsAlive());
			TestEqual(TEXT("four floors"), Dungeon.Floors, 4);
			RiftsOn.FindOrAdd(Dungeon.CityTier) += 1;
			++Rifts;
		}

		// CONTROL: Death's Quest dungeons, same streams, keep to the front line.
		FRandomStream Same(Seed);
		for (const FCataclysmDungeon& Dungeon : Run->Surges->RollWave(
				 *Run->Map, 1, 0, Same, TArray<int32>(), TArray<int32>(), []() { return ECataclysmType::Death; },
				 false, Seed))
		{
			if (Dungeon.Type == ECataclysmDungeonType::Quest)
			{
				++Hordes;
				HordesOffTheFront += Run->Map->IsExposed(Dungeon.CityId) ? 0 : 1;
			}
		}
	}

	TestTrue(TEXT("enough Rifts were rolled to judge the shares"), Rifts >= 200);
	const float Outposts = static_cast<float>(RiftsOn.FindRef(ECataclysmCityTier::Outpost)) / FMath::Max(1, Rifts);
	const float Bulwarks = static_cast<float>(RiftsOn.FindRef(ECataclysmCityTier::Bulwark)) / FMath::Max(1, Rifts);
	const float Sanctuaries = static_cast<float>(RiftsOn.FindRef(ECataclysmCityTier::Sanctuary)) / FMath::Max(1, Rifts);
	TestTrue(TEXT("about 60 in 90 on an Outpost"), Outposts > 0.55f && Outposts < 0.78f);
	TestTrue(TEXT("about 24 in 90 on a Bulwark, sealed"), Bulwarks > 0.16f && Bulwarks < 0.38f);
	TestTrue(TEXT("about 6 in 90 on a Sanctuary, sealed"), Sanctuaries > 0.02f && Sanctuaries < 0.13f);

	TestTrue(TEXT("the control rolled Quest dungeons"), Hordes >= 200);
	TestEqual(TEXT("a Quest dungeon of another Cataclysm keeps to the front line"), HordesOffTheFront, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsOwnStreamTest,
	"Cataclysm.Rifts.ARiftsCityIsDrawnFromItsOwnStreamAndTheWaveRollsAsBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsOwnStreamTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	// A DEMONIC WAVE AND A DEATH WAVE ON ONE SEED TAKE THE SAME NUMBERS OFF THE WAVE'S STREAM, Rifts or none, and every
	// dungeon that is not a Rift lands on the same city with the same depth. Only the Rifts' cities differ.
	UCataclysmEmpireRun* Run = MakeEmptyRun();
	int32 RiftsMoved = 0;
	for (int32 Seed = 1; Seed <= 60; ++Seed)
	{
		FRandomStream Demonic(Seed);
		FRandomStream Death(Seed);
		const TArray<FCataclysmDungeon> WithRifts = Run->Surges->RollWave(
			*Run->Map, 1, 0, Demonic, TArray<int32>(), TArray<int32>(), []() { return ECataclysmType::Demonic; },
			false, Seed);
		const TArray<FCataclysmDungeon> Without = Run->Surges->RollWave(
			*Run->Map, 1, 0, Death, TArray<int32>(), TArray<int32>(), []() { return ECataclysmType::Death; }, false,
			Seed);
		TestEqual(TEXT("the wave's stream ends where it would without Rifts"), Demonic.GetCurrentSeed(),
				  Death.GetCurrentSeed());
		if (!TestEqual(TEXT("the same number of dungeons"), WithRifts.Num(), Without.Num()))
		{
			continue;
		}
		for (int32 Index = 0; Index < WithRifts.Num(); ++Index)
		{
			if (WithRifts[Index].IsARift())
			{
				RiftsMoved += WithRifts[Index].CityId != Without[Index].CityId ? 1 : 0;
				continue;
			}
			TestEqual(TEXT("a dungeon that is not a Rift lands where it did"), WithRifts[Index].CityId,
					  Without[Index].CityId);
			TestEqual(TEXT("with the depth it had"), WithRifts[Index].Floors, Without[Index].Floors);
		}
	}
	TestTrue(TEXT("some Rift opened away from the city the wave drew for it"), RiftsMoved > 0);
	return true;
}

// ---------------------------------------------------------------------------
// Its eight modifiers
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsEightModifiersTest,
	"Cataclysm.Rifts.ARiftCarriesEightDifferentModifiersWhateverTheTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsEightModifiersTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	// DEMONIC'S FOURTEEN ROWS AND DEATH'S FOURTEEN, as the real table holds 14 and 15. Seed 4's character faces Demonic
	// first, so at tier 1 the pool is Demonic's 14 and at tier 4 four Cataclysms' rows.
	for (const int32 Tier : { 1, 4 })
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		AddRows(Run->ModifierPool, ECataclysmType::Demonic, TEXT("Demonic"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Death, TEXT("Death"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::War, TEXT("War"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Pestilence, TEXT("Pestilence"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Chaos, TEXT("Chaos"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Void, TEXT("Void"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Famine, TEXT("Famine"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Celestial, TEXT("Celestial"), 14);
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten2; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(4, ECataclysmSurgeMode::Static, 0, Tier);
		TestTrue(TEXT("seed 4 faces Demonic"), Run->ActiveCataclysms.Contains(ECataclysmType::Demonic));

		for (int32 Id = 500; Id < 520; ++Id)
		{
			FRandomStream Stream(Id);
			FCataclysmDungeon Rift = Run->Surges->MakeDungeon(Id, FirstOf(*Run, ECataclysmCityTier::Outpost), 1, Stream,
															  true, ECataclysmDungeonType::Quest,
															  ECataclysmType::Demonic);
			Rift.Cataclysm = ECataclysmType::Demonic;
			Run->GiveModifiers(Rift);

			TSet<FName> Slotted;
			for (const FName Key : Rift.Modifiers)
			{
				if (Key != UCataclysmDungeonModifierRules::CorruptedStalkerKey())
				{
					Slotted.Add(Key);
					TestTrue(TEXT("drawn from the Cataclysms the run faces"),
							 Run->ModifierPool.ContainsByPredicate([&Key, Run](const FCataclysmDungeonModifier& Row)
							 {
								 return Row.RowKey == Key && Run->ActiveCataclysms.Contains(Row.Cataclysm);
							 }));
				}
			}
			TestEqual(TEXT("a Rift carries eight different modifiers"), Slotted.Num(),
					  UCataclysmDungeonModifierRules::RiftModifierCount);

			// CONTROL: an ordinary Demonic dungeon of the same run carries what the ordinary rule gives its tier
			// and sub-type, `UCataclysmDungeonModifierRules::CountFor`: one a tier, and twice that for a
			// Sacrificial dungeon, whose sub-type the stream may roll. Window rifts-w1 (2026-11-25) failed this
			// control while it said "one a tier": 4 of the 20 ids at each tier carried twice the tier (issue #51
			// slice B2). Each control dungeon is printed, so a run shows which rolled Sacrificial.
			FRandomStream Same(Id);
			FCataclysmDungeon Basic = Run->Surges->MakeDungeon(Id + 100, FirstOf(*Run, ECataclysmCityTier::Outpost), 1,
															   Same, false, ECataclysmDungeonType::Basic,
															   ECataclysmType::Demonic);
			Basic.Cataclysm = ECataclysmType::Demonic;
			Run->GiveModifiers(Basic);
			int32 BasicSlotted = 0;
			for (const FName Key : Basic.Modifiers)
			{
				BasicSlotted += Key != UCataclysmDungeonModifierRules::CorruptedStalkerKey() ? 1 : 0;
			}
			const int32 OrdinaryCount = UCataclysmDungeonModifierRules::CountFor(Tier, Basic.SubType);
			const FString ControlSubType = UCataclysmSurgeScheduler::SubTypeName(Basic.SubType); // Empty for none.
			AddInfo(FString::Printf(
				TEXT("RIFT CONTROL: tier %d, dungeon %d, sub-type %s, %d modifiers, the ordinary rule %d"), Tier,
				Id + 100, ControlSubType.IsEmpty() ? TEXT("none") : *ControlSubType, BasicSlotted, OrdinaryCount));
			TestEqual(TEXT("an ordinary dungeon carries what the ordinary rule gives its tier and sub-type"),
					  BasicSlotted, OrdinaryCount);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// No Demonic Guide in a tier 1 Rift
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsDemonicGuideTest,
	"Cataclysm.Rifts.ATierOneRiftNeverDrawsTheDemonicGuideAndATierTwoRiftMay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner, 2026-11-25, issue #51, verbatim: "Make demonic guide not spawn on the Rifts. It would be really annoying
 * if you're trying to destroy a pillar and you can't because that guy will kill you if you let him get out of range."
 * and "at least not on t1". 200 Rifts at tier 1 from a pool of Demonic's 14 rows and the Guide: none holds the Guide,
 * and each still holds eight different modifiers. CONTROLS: 200 Rifts at tier 2 from the same rows hold it at least
 * once, and so do 200 ordinary Demonic dungeons at tier 1. Prints "RIFT GUIDE".
 */
bool FCataclysmRiftsDemonicGuideTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;
	const FName Guide = UCataclysmDungeonModifierRules::DemonicGuideKey();
	int32 RiftsAtOne = 0;
	int32 RiftsAtTwo = 0;
	int32 OrdinaryAtOne = 0;

	for (const int32 Tier : { 1, 2 })
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		// AS THE EIGHT-MODIFIER TEST ABOVE: 14 rows a Cataclysm, so at tier 1 the pool is Demonic's 14 and the Guide.
		AddRows(Run->ModifierPool, ECataclysmType::Demonic, TEXT("Demonic"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Death, TEXT("Death"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::War, TEXT("War"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Pestilence, TEXT("Pestilence"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Chaos, TEXT("Chaos"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Void, TEXT("Void"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Famine, TEXT("Famine"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Celestial, TEXT("Celestial"), 14);
		FCataclysmDungeonModifier GuideRow;
		GuideRow.RowKey = Guide;
		GuideRow.ModifierName = Guide;
		GuideRow.Cataclysm = ECataclysmType::Demonic;
		GuideRow.Danger = 10.0f;
		Run->ModifierPool.Add(GuideRow);
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten3; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(4, ECataclysmSurgeMode::Static, 0, Tier);
		TestTrue(TEXT("seed 4 faces Demonic"), Run->ActiveCataclysms.Contains(ECataclysmType::Demonic));

		for (int32 Id = 600; Id < 800; ++Id)
		{
			FRandomStream Stream(Id);
			FCataclysmDungeon Rift = Run->Surges->MakeDungeon(Id, FirstOf(*Run, ECataclysmCityTier::Outpost), 1, Stream,
															  true, ECataclysmDungeonType::Quest,
															  ECataclysmType::Demonic);
			Rift.Cataclysm = ECataclysmType::Demonic;
			Run->GiveModifiers(Rift);
			TSet<FName> Slotted;
			for (const FName Key : Rift.Modifiers)
			{
				if (Key != UCataclysmDungeonModifierRules::CorruptedStalkerKey())
				{
					Slotted.Add(Key);
				}
			}
			TestEqual(TEXT("a Rift still carries eight different modifiers"), Slotted.Num(),
					  UCataclysmDungeonModifierRules::RiftModifierCount);
			(Tier == 1 ? RiftsAtOne : RiftsAtTwo) += Slotted.Contains(Guide) ? 1 : 0;

			if (Tier == 1)
			{
				FRandomStream Same(Id);
				FCataclysmDungeon Basic = Run->Surges->MakeDungeon(Id + 300, FirstOf(*Run, ECataclysmCityTier::Outpost),
																   1, Same, false, ECataclysmDungeonType::Basic,
																   ECataclysmType::Demonic);
				Basic.Cataclysm = ECataclysmType::Demonic;
				Run->GiveModifiers(Basic);
				OrdinaryAtOne += Basic.Modifiers.Contains(Guide) ? 1 : 0;
			}
		}
	}

	AddInfo(FString::Printf(TEXT("RIFT GUIDE: of 200 Rifts at tier 1, %d hold the Demonic Guide; at tier 2, %d; of 200 "
								 "ordinary Demonic dungeons at tier 1, %d"),
							RiftsAtOne, RiftsAtTwo, OrdinaryAtOne));
	TestEqual(TEXT("no Rift at tier 1 holds the Demonic Guide"), RiftsAtOne, 0);
	TestTrue(TEXT("CONTROL: a Rift at tier 2 can hold it"), RiftsAtTwo > 0);
	TestTrue(TEXT("CONTROL: an ordinary Demonic dungeon at tier 1 can hold it"), OrdinaryAtOne > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsModifierStreamTest,
	"Cataclysm.Rifts.ARiftsModifiersLeaveTheRunsModifierStreamWhereAQuestDungeonsWould",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsModifierStreamTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	// TWO IDENTICAL RUNS: one gives a Rift its modifiers, the other a Quest dungeon Death sent. The run's modifier
	// stream must stand at the same place after, so every later dungeon's modifiers are what they were.
	UCataclysmEmpireRun* WithARift = NewObject<UCataclysmEmpireRun>();
	UCataclysmEmpireRun* WithAHorde = NewObject<UCataclysmEmpireRun>();
	for (UCataclysmEmpireRun* Run : { WithARift, WithAHorde })
	{
		AddRows(Run->ModifierPool, ECataclysmType::Demonic, TEXT("Demonic"), 14);
		AddRows(Run->ModifierPool, ECataclysmType::Death, TEXT("Death"), 14);
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten4; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(4);
		Run->ActiveCataclysms = { ECataclysmType::Demonic, ECataclysmType::Death };
	}

	FRandomStream One(7);
	FCataclysmDungeon Rift = WithARift->Surges->MakeDungeon(
		10, FirstOf(*WithARift, ECataclysmCityTier::Outpost), 1, One, true, ECataclysmDungeonType::Quest,
		ECataclysmType::Demonic);
	Rift.Cataclysm = ECataclysmType::Demonic;
	WithARift->GiveModifiers(Rift);

	FRandomStream Two(7);
	FCataclysmDungeon Horde = WithAHorde->Surges->MakeDungeon(
		10, FirstOf(*WithAHorde, ECataclysmCityTier::Outpost), 1, Two, true, ECataclysmDungeonType::Quest,
		ECataclysmType::Death);
	Horde.Cataclysm = ECataclysmType::Death;
	WithAHorde->GiveModifiers(Horde);

	TestTrue(TEXT("the Rift drew more than the Horde dungeon"), Rift.Modifiers.Num() > Horde.Modifiers.Num());
	TestEqual(TEXT("and the run's modifier stream stands where the Horde dungeon left it"),
			  WithARift->ModifierStream.GetCurrentSeed(), WithAHorde->ModifierStream.GetCurrentSeed());
	return true;
}

// ---------------------------------------------------------------------------
// What it sends, every five days
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsSendTest,
	"Cataclysm.Rifts.ARiftSendsABasicDemonicDungeonOnItsFifthDayAndItsTenth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsSendTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	HoldTheBoard(*Run);
	const FCataclysmCity& Host = FirstOf(*Run, ECataclysmCityTier::Outpost);
	Place(*Run, 1, ECataclysmDungeonType::Quest, ECataclysmType::Demonic, Host);
	TestTrue(TEXT("the dungeon placed is a Rift"), Run->FindDungeon(1) && Run->FindDungeon(1)->IsARift());
	TestEqual(TEXT("on the day it opened it sends in five days"), Run->DaysUntilRiftSends(1), 5);

	TestEqual(TEXT("nothing on its first four days"), SentOver(*Run, 4).Num(), 0);
	TestEqual(TEXT("and on the fourth it sends tomorrow"), Run->DaysUntilRiftSends(1), 1);

	const TArray<int32> First = SentOver(*Run, 1);
	if (!TestEqual(TEXT("one dungeon on its fifth day"), First.Num(), 1))
	{
		return true;
	}
	const FCataclysmDungeon* Sent = Run->FindDungeon(First[0]);
	TestTrue(TEXT("it is on the map"), Sent != nullptr);
	if (Sent == nullptr)
	{
		return true;
	}
	TestTrue(TEXT("an ordinary dungeon"), Sent->Type == ECataclysmDungeonType::Basic);
	TestTrue(TEXT("Demonic sent it"), Sent->Cataclysm == ECataclysmType::Demonic);
	TestFalse(TEXT("it is no Rift"), Sent->IsARift());
	TestTrue(TEXT("on a city next to the Rift's"),
			 UCataclysmSurgeScheduler::AdjacentCities(Host).Contains(Sent->CityId));
	TestTrue(TEXT("one a surge could target"), Run->Map->IsExposed(Sent->CityId) && Sent->CityId != Run->Map->PillarId);
	const FCataclysmDungeonSpec Spec = UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::Basic,
																		 Run->Map->Find(Sent->CityId)->Tier);
	TestTrue(TEXT("as deep as a surge makes one for that city"),
			 Sent->Floors >= Spec.LeastFloors && Sent->Floors <= Spec.MostFloors);

	TestEqual(TEXT("nothing on its sixth to ninth days"), SentOver(*Run, 4).Num(), 0);
	TestEqual(TEXT("and one more on its tenth, with the first still standing"), SentOver(*Run, 1).Num(), 1);
	TestTrue(TEXT("the first still stands: no limit"), Run->FindDungeon(First[0]) != nullptr);
	TestTrue(TEXT("the Rift still stands"), Run->FindDungeon(1) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsOtherQuestTest,
	"Cataclysm.Rifts.AQuestDungeonAnotherCataclysmSentSendsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsOtherQuestTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	HoldTheBoard(*Run);
	Place(*Run, 1, ECataclysmDungeonType::Quest, ECataclysmType::Death, FirstOf(*Run, ECataclysmCityTier::Outpost));
	// AND AN ORDINARY DEMONIC DUNGEON, which is no Rift either.
	Place(*Run, 2, ECataclysmDungeonType::Basic, ECataclysmType::Demonic, FirstOf(*Run, ECataclysmCityTier::Outpost));
	TestEqual(TEXT("nothing is sent in ten days"), SentOver(*Run, 10).Num(), 0);
	TestEqual(TEXT("and no day until it sends is shown"), Run->DaysUntilRiftSends(1), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsMovedTest,
	"Cataclysm.Rifts.ARiftThatMovesKeepsItsCountAndSendsFromWhereItStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsMovedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	HoldTheBoard(*Run);
	const FCataclysmCity& Host = FirstOf(*Run, ECataclysmCityTier::Outpost);
	Place(*Run, 1, ECataclysmDungeonType::Quest, ECataclysmType::Demonic, Host);
	TestEqual(TEXT("one on its fifth day"), SentOver(*Run, 7).Num(), 1);

	// MOVED ON ITS SEVENTH DAY to an exposed neighbour, as its timer or its city's fall would move it.
	int32 NewHost = INDEX_NONE;
	for (const int32 CityId : UCataclysmSurgeScheduler::AdjacentCities(Host))
	{
		if (Run->Map->IsExposed(CityId) && CityId != Run->Map->PillarId)
		{
			NewHost = CityId;
			break;
		}
	}
	if (!TestTrue(TEXT("the Outpost has an exposed neighbour"), NewHost != INDEX_NONE))
	{
		return true;
	}
	for (FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		if (Dungeon.DungeonId == 1)
		{
			Dungeon.CityId = NewHost;
		}
	}

	const TArray<int32> Next = SentOver(*Run, 3);
	if (!TestEqual(TEXT("one more on its tenth day: the count did not restart"), Next.Num(), 1))
	{
		return true;
	}
	TestTrue(TEXT("sent from where it stands now"),
			 UCataclysmSurgeScheduler::AdjacentCities(*Run->Map->Find(NewHost)).Contains(
				 Run->FindDungeon(Next[0])->CityId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsNoNeighbourTest,
	"Cataclysm.Rifts.ARiftWithNoNeighbourASurgeCouldTargetSendsNothingUntilALaneOpens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsNoNeighbourTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	// A SANCTUARY'S NEIGHBOURS AT THE START ARE SEALED BULWARKS AND THE PILLAR. The control is the same Rift once an
	// Outpost shielding one of those Bulwarks has fallen: that Bulwark is exposed and the next dungeon goes to it.
	UCataclysmEmpireRun* Run = MakeEmptyRun();
	HoldTheBoard(*Run);
	const FCataclysmCity& Sanctuary = FirstOf(*Run, ECataclysmCityTier::Sanctuary);
	Place(*Run, 1, ECataclysmDungeonType::Quest, ECataclysmType::Demonic, Sanctuary);
	TestEqual(TEXT("nothing on its fifth day: no neighbour a surge could target"), SentOver(*Run, 5).Num(), 0);

	int32 Bulwark = INDEX_NONE;
	for (const int32 CityId : UCataclysmSurgeScheduler::AdjacentCities(Sanctuary))
	{
		if (Run->Map->Find(CityId)->Tier == ECataclysmCityTier::Bulwark)
		{
			Bulwark = CityId;
			break;
		}
	}
	if (!TestTrue(TEXT("the Sanctuary has a Bulwark beside it"), Bulwark != INDEX_NONE))
	{
		return true;
	}
	// BY THE MAP'S OWN FALL, which brings no surge: only the lane opens.
	Run->Map->Fall(Run->Map->Find(Bulwark)->Outward[0]);
	TestTrue(TEXT("the Bulwark is exposed now"), Run->Map->IsExposed(Bulwark));

	const TArray<int32> Sent = SentOver(*Run, 5);
	if (!TestEqual(TEXT("one on its tenth day"), Sent.Num(), 1))
	{
		return true;
	}
	TestTrue(TEXT("to an exposed neighbour"), Run->Map->IsExposed(Run->FindDungeon(Sent[0])->CityId)
		&& UCataclysmSurgeScheduler::AdjacentCities(Sanctuary).Contains(Run->FindDungeon(Sent[0])->CityId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsSealedTest,
	"Cataclysm.Rifts.ASealedRiftSendsNothingAndIsOneDemonicObjective",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsSealedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun();
	HoldTheBoard(*Run);
	Run->ActiveCataclysms = { ECataclysmType::Demonic };
	Run->QuestObjectivesByCataclysm.Add(ECataclysmType::Demonic, 0);
	Place(*Run, 1, ECataclysmDungeonType::Quest, ECataclysmType::Demonic, FirstOf(*Run, ECataclysmCityTier::Outpost));
	SentOver(*Run, 3);
	TestTrue(TEXT("sealing it is clearing it"), Run->ClearDungeon(1));
	TestEqual(TEXT("one of Demonic's objectives"), Run->QuestObjectivesByCataclysm.FindRef(ECataclysmType::Demonic), 1);
	TestEqual(TEXT("and a sealed Rift sends nothing"), SentOver(*Run, 7).Num(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// A death in one
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftsDeathSurgeTest,
	"Cataclysm.Rifts.ADeathInARiftBringsAnOrdinarySurgeOnTheDayOfAnotherSurge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftsDeathSurgeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftsTest;

	// ON THE DAY OF THE RUN'S FIRST SURGE, so any least-interval brake would refuse it. `EndTheDungeonForADeath` decides
	// it was a Rift; `Cataclysm.DeathCost.` holds that half.
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten5; // Issue #2928: the Cataclysms this run faces are named.
	Run->Begin(1);
	Run->AdvanceDay();
	const int32 Fired = Run->Surges->SurgesFired;
	TestTrue(TEXT("a surge has fired"), Fired >= 1);

	const FCataclysmDayReport Report = Run->SurgeForADeathInARift();
	TestTrue(TEXT("a surge fired"), Report.bSurged);
	TestEqual(TEXT("one"), Report.Surges, 1);
	TestTrue(TEXT("and it says a Rift's death brought it"), Report.bSurgedForARiftDeath);
	TestTrue(TEXT("it landed dungeons"), Report.SurgeDungeons > 0);
	TestEqual(TEXT("on the clock's day"), Report.Day, Run->Day());
	TestEqual(TEXT("counted as a surge"), Run->Surges->SurgesFired, Fired + 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
