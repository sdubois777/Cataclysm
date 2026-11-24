// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSeedMix.h"
#include "Engine/DataTable.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "UObject/Package.h"

/**
 * A FLOOR'S CREATURES DRAW THEIR RARITY AND THEIR MODIFIERS INDEPENDENTLY. Issue #2749's placement seeding slice,
 * ruled by the coordinating session on 2026-11-19 (B): "on one floor, the Elites' first modifier draws are not confined
 * to a band of the pool".
 *
 * Each test builds no floor. It asks the game's own placement seeds (`ACataclysmDungeonGameMode::
 * RaritySeedForPlacementOn` and `ModifierSeedForPlacementOn`) for 250 placements on each of 20 floor seeds, rolls the
 * rarity with `UCataclysmEnemyRarity::RollRarityStep` over the real rarity table, and draws an Elite's first modifier
 * with `UCataclysmEnemyModifiers::Draw` over the real modifier table, exactly as `PopulateFloor` and
 * `DrawModifiersForRarity` do. THE CONTROL beside it is the formula the game used until this slice,
 * `FloorSeed ^ (Salt + Index)`, computed here, so each test shows its measure tells the two apart.
 *
 * THE FIGURES ARE PREDICTED EXACTLY by `sim/analyse_placement_seeds.py`, which replays the engine's `FRandomStream` in
 * single precision over the same tables and seeds; they are registered in `docs/DECISIONS.md` before the window.
 */
namespace CataclysmSeedingTest
{
	/** The floor seeds: `SeedForFloor(1000 + n, 1)` for n of 0 to 19, as the generator makes a floor's seed. */
	TArray<int32> FloorSeeds()
	{
		TArray<int32> Seeds;
		for (int32 N = 0; N < 20; ++N)
		{
			Seeds.Add(FCataclysmFloorGenerator::SeedForFloor(1000 + N, 1));
		}
		return Seeds;
	}

	constexpr int32 Placements = 250;

	/** The rung that carries one modifier, `CountForRarityStep(1)`: an Elite. Checked by each test. */
	constexpr int32 EliteStep = 1;

	/** The formula the game used until this slice: the floor seed XOR the salt plus the index, never nought. */
	int32 OldSeed(int32 FloorSeed, int32 Salt, int32 Index)
	{
		const int32 Mixed = FloorSeed ^ (Salt + Index);
		return Mixed != 0 ? Mixed : 1;
	}

	constexpr int32 OldRaritySalt = 0x72720000;

	/** One floor: every placement's rung, and the pool index of each Elite's first modifier, in placement order. */
	struct FSeedingFloor
	{
		TArray<int32> Rungs;
		TArray<int32> EliteFirstModifiers;
	};

	FSeedingFloor Place(int32 FloorSeed, bool bOldFormula, const UDataTable* Rarities, const UDataTable* Modifiers,
						const TArray<FName>& Pool)
	{
		FSeedingFloor Floor;
		for (int32 Index = 0; Index < Placements; ++Index)
		{
			// THE OLD RARITY SEED WAS NOT KEPT FROM NOUGHT: `FloorSeed ^ (Salt + Index)` exactly.
			FRandomStream Rarity(bOldFormula ? FloorSeed ^ (OldRaritySalt + Index)
											 : ACataclysmDungeonGameMode::RaritySeedForPlacementOn(FloorSeed, Index));
			const int32 Rung = UCataclysmEnemyRarity::RollRarityStep(Rarities, Rarity);
			Floor.Rungs.Add(Rung);
			if (Rung != EliteStep)
			{
				continue;
			}
			const int32 ModifierSeed = bOldFormula
				? OldSeed(FloorSeed, ACataclysmDungeonGameMode::ModifiersFromTheFloorSalt, Index)
				: ACataclysmDungeonGameMode::ModifierSeedForPlacementOn(
					FloorSeed, Index, ACataclysmDungeonGameMode::ModifiersFromTheFloorSalt);
			FRandomStream Stream(ModifierSeed);
			const TArray<FName> Drawn = UCataclysmEnemyModifiers::Draw(Modifiers, FName(TEXT("Demonic")), 1, Stream, {});
			if (Drawn.Num() == 1)
			{
				Floor.EliteFirstModifiers.Add(Pool.IndexOfByKey(Drawn[0]));
			}
		}
		return Floor;
	}

	/** The fewest adjacent rows of a pool of `Size`, wrapping round its end, that hold every index. */
	int32 NarrowestBand(const TArray<int32>& Indices, int32 Size)
	{
		TArray<int32> Present;
		for (const int32 Index : Indices)
		{
			Present.AddUnique(Index);
		}
		if (Present.Num() <= 1)
		{
			return Present.Num();
		}
		Present.Sort();
		int32 WidestGap = 0;
		for (int32 At = 0; At < Present.Num(); ++At)
		{
			const int32 Next = Present[(At + 1) % Present.Num()];
			WidestGap = FMath::Max(WidestGap, ((Next - Present[At]) % Size + Size) % Size);
		}
		return Size - WidestGap + 1;
	}

	int32 Distinct(const TArray<int32>& Indices)
	{
		TArray<int32> Seen;
		for (const int32 Index : Indices)
		{
			Seen.AddUnique(Index);
		}
		return Seen.Num();
	}
}

#define CATACLYSM_SEEDING_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

/**
 * ON EVERY ONE OF THE 20 FLOORS, THE ELITES' FIRST MODIFIERS COME FROM ACROSS THE POOL. Registered: with the game's
 * seeds the fewest distinct on a floor is 15 of the 18 Demonic rows (at least 12 asked), and no floor's Elites lie in a
 * band of 5 adjacent rows or fewer (the narrowest is 16). THE CONTROL: with the old formula every floor's Elites carry at
 * most 5 distinct, and every floor's lie in a band of at most 5 rows.
 */
CATACLYSM_SEEDING_TEST(FCataclysmSeedingBandTest,
	"Cataclysm.Seeding.AFloorsElitesDrawTheirFirstModifiersFromAcrossThePoolAndNotFromABand")
{
	using namespace CataclysmSeedingTest;

	const UDataTable* Rarities = UCataclysmEnemyRarity::LoadEnemyRarityTable();
	const UDataTable* Modifiers = UCataclysmEnemyModifiers::LoadEnemyModifierTable();
	if (!TestNotNull(TEXT("the rarity table"), Rarities) || !TestNotNull(TEXT("the modifier table"), Modifiers))
	{
		return false;
	}
	const TArray<FName> Pool = UCataclysmEnemyModifiers::PoolFor(Modifiers, FName(TEXT("Demonic")));
	TestEqual(TEXT("set-up: the Demonic pool is its 8 rows and Generic's 10"), Pool.Num(), 18);
	TestEqual(TEXT("set-up: an Elite carries one modifier"), UCataclysmEnemyModifiers::CountForRarityStep(EliteStep), 1);

	int32 FewestNow = TNumericLimits<int32>::Max();
	int32 NarrowestNow = TNumericLimits<int32>::Max();
	int32 MostBefore = 0;
	int32 WidestBefore = 0;
	int32 FewestElites = TNumericLimits<int32>::Max();
	for (const int32 FloorSeed : FloorSeeds())
	{
		const FSeedingFloor Now = Place(FloorSeed, /*bOldFormula=*/false, Rarities, Modifiers, Pool);
		const FSeedingFloor Before = Place(FloorSeed, /*bOldFormula=*/true, Rarities, Modifiers, Pool);
		FewestElites = FMath::Min(FewestElites, FMath::Min(Now.EliteFirstModifiers.Num(), Before.EliteFirstModifiers.Num()));
		FewestNow = FMath::Min(FewestNow, Distinct(Now.EliteFirstModifiers));
		NarrowestNow = FMath::Min(NarrowestNow, NarrowestBand(Now.EliteFirstModifiers, Pool.Num()));
		MostBefore = FMath::Max(MostBefore, Distinct(Before.EliteFirstModifiers));
		WidestBefore = FMath::Max(WidestBefore, NarrowestBand(Before.EliteFirstModifiers, Pool.Num()));
	}

	AddInfo(FString::Printf(TEXT("SEEDING BAND: the mix: fewest distinct %d, narrowest band %d; the old formula: most "
								 "distinct %d, widest band %d; fewest Elites on a floor %d"),
							FewestNow, NarrowestNow, MostBefore, WidestBefore, FewestElites));

	TestTrue(TEXT("set-up: every floor has at least 30 Elites to measure"), FewestElites >= 30);
	TestTrue(TEXT("every floor's Elites carry at least 12 distinct first modifiers"), FewestNow >= 12);
	TestTrue(TEXT("no floor's Elites lie in a band of 5 adjacent rows or fewer"), NarrowestNow > 5);
	TestTrue(TEXT("CONTROL: with the old formula every floor's Elites carry at most 5 distinct"), MostBefore <= 5);
	TestTrue(TEXT("CONTROL: and every floor's lie in a band of at most 5 rows"), WidestBefore <= 5);
	return true;
}

/**
 * TWO PLACEMENTS NEXT TO EACH OTHER IN A FLOOR'S LIST ARE BOTH ELITE ABOUT AS OFTEN AS TWO INDEPENDENT DRAWS WOULD BE:
 * the Elite weight is 0.2, so 4%. Registered, averaged over the 20 floors: 4.54% with the game's seeds, asked under 6%;
 * THE CONTROL, the old formula, 12.29%, asked above 9%, because consecutive rarity seeds were a fixed distance apart.
 */
CATACLYSM_SEEDING_TEST(FCataclysmSeedingNeighbourTest,
	"Cataclysm.Seeding.NeighbouringPlacementsAreBothEliteAboutAsOftenAsIndependentDrawsWouldBe")
{
	using namespace CataclysmSeedingTest;

	const UDataTable* Rarities = UCataclysmEnemyRarity::LoadEnemyRarityTable();
	const UDataTable* Modifiers = UCataclysmEnemyModifiers::LoadEnemyModifierTable();
	if (!TestNotNull(TEXT("the rarity table"), Rarities) || !TestNotNull(TEXT("the modifier table"), Modifiers))
	{
		return false;
	}
	const TArray<FName> Pool = UCataclysmEnemyModifiers::PoolFor(Modifiers, FName(TEXT("Demonic")));

	double SumNow = 0.0;
	double SumBefore = 0.0;
	const TArray<int32> Seeds = FloorSeeds();
	for (const int32 FloorSeed : Seeds)
	{
		for (const bool bOldFormula : {false, true})
		{
			const FSeedingFloor Floor = Place(FloorSeed, bOldFormula, Rarities, Modifiers, Pool);
			int32 Pairs = 0;
			for (int32 At = 0; At + 1 < Floor.Rungs.Num(); ++At)
			{
				Pairs += Floor.Rungs[At] == EliteStep && Floor.Rungs[At + 1] == EliteStep;
			}
			(bOldFormula ? SumBefore : SumNow) += static_cast<double>(Pairs) / (Floor.Rungs.Num() - 1);
		}
	}
	const double Now = SumNow / Seeds.Num();
	const double Before = SumBefore / Seeds.Num();

	AddInfo(FString::Printf(TEXT("SEEDING NEIGHBOURS: both Elite %.4f with the mix, %.4f with the old formula"), Now,
							Before));
	TestTrue(TEXT("neighbouring placements are both Elite under 6% of the time"), Now < 0.06);
	TestTrue(TEXT("CONTROL: with the old formula, above 9%"), Before > 0.09);
	return true;
}

/**
 * A RUN LOADED FROM A SAVE KEEPS ITS DRAWS: the slice moved how a NEW run seeds its Cataclysm stream, and a loaded run's
 * stream is the state its save stored (`FCataclysmSaveGather` writes `GetCurrentSeed()`, `FCataclysmSaveApply::RunInto`
 * puts it back). Ruled by the coordinating session on 2026-11-19: "add one assertion that a run loaded from a committed
 * fixture draws the same next Cataclysm as before the change". The committed `Run_v18.json` holds the state 1357924680
 * and the active Cataclysms Demonic, Death, War and Pestilence; the engine's generator from that state draws index 0
 * then 2, so Demonic then War, as `sim/analyse_placement_seeds.py`'s `Stream` computes, before and after the slice.
 * It followed the committed run file as the run record moved: `Run_v9.json` until the rebase onto `e583b0788`
 * (version 10, issue #2725), `Run_v10.json` until the rebase onto `3d0193bfa` (version 11, issue #2741 slice 5b),
 * `Run_v11.json` until slice 6b took the run record to version 12 (issue #2741) and moved this test when it was
 * rebased onto this slice, `Run_v12.json` until issue #2825 took it to version 13, `Run_v13.json` until the gems window took it to
 * version 14 (issue #46), `Run_v14.json` until issue #2741 slice 9 took it to version 15, `Run_v15.json` until issue
 * #2838 took it to version 16, `Run_v16.json` until issue #2840 took it to version 17, and `Run_v17.json` until
 * issue #2848 took it to version 18; since each, only the migration test reads the older file. All ten hold the same stream
 * state and Cataclysms.
 *
 * THE DRAW IS `RollCataclysm`'S, WRITTEN OUT: `ActiveCataclysms[CataclysmStream.RandRange(0, Num - 1)]`. That function is
 * private and reached only through a surge; the stream it draws from is public.
 */
CATACLYSM_SEEDING_TEST(FCataclysmSeedingLoadedRunTest,
	"Cataclysm.Seeding.ARunLoadedFromTheCommittedFixtureDrawsTheCataclysmsItsSavedStreamGives")
{
	FString Text;
	FString Reason;
	if (!TestTrue(TEXT("set-up: Run_v18.json is read"), CataclysmSaveFixtures::Read(TEXT("Run_v18.json"), Text, Reason)))
	{
		AddError(Reason);
		return false;
	}
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Why;
	UCataclysmRunSave* Record = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Why));
	if (!TestNotNull(TEXT("set-up: the fixture reads into a run record"), Record))
	{
		AddError(Why);
		return false;
	}

	// THE WHOLE MAP, FROM A FRESH RUN, because `RunInto` puts back only a record of every city and the fixture holds two:
	// the diagnostic window seed-diag (2026-11-20) failed this set-up with "the record holds 2 cities and the map has
	// 25". Nothing the Cataclysm stream draws reads a city, and every other field, the stream's state and the active
	// Cataclysms among them, is the fixture's. The fixture's one dungeon stands on city 7, which a whole map has.
	UCataclysmEmpireRun* Fresh = NewObject<UCataclysmEmpireRun>();
	Fresh->Begin(2749);
	TestEqual(TEXT("set-up: the fixture holds two cities"), Record->Cities.Num(), 2);
	UCataclysmEmpireRun* Loaded = NewObject<UCataclysmEmpireRun>();
	// Each call made before its message is written, so the message carries `RunInto`'s reason.
	const bool bGathered = FCataclysmSaveGather::CitiesFrom(*Fresh, *Record);
	const bool bLoaded = bGathered && FCataclysmSaveApply::RunInto(*Record, *Loaded, Why);
	if (!TestTrue(TEXT("set-up: a fresh run's cities are gathered into the record"), bGathered)
		|| !TestTrue(FString::Printf(TEXT("set-up: and into a run (%s)"), *Why), bLoaded))
	{
		return false;
	}

	TestEqual(TEXT("the loaded run's Cataclysm stream is the state the save stored"),
			  static_cast<int64>(Loaded->CataclysmStream.GetCurrentSeed()), static_cast<int64>(1357924680));
	if (!TestEqual(TEXT("set-up: four active Cataclysms"), Loaded->ActiveCataclysms.Num(), 4))
	{
		return false;
	}
	const ECataclysmType First = Loaded->ActiveCataclysms[Loaded->CataclysmStream.RandRange(0, 3)];
	const ECataclysmType Second = Loaded->ActiveCataclysms[Loaded->CataclysmStream.RandRange(0, 3)];
	AddInfo(FString::Printf(TEXT("SEEDING LOADED RUN: next %s, then %s"), *UEnum::GetValueAsString(First),
							*UEnum::GetValueAsString(Second)));
	TestEqual(TEXT("its next Cataclysm is Demonic"), static_cast<int32>(First), static_cast<int32>(ECataclysmType::Demonic));
	TestEqual(TEXT("and the one after is War"), static_cast<int32>(Second), static_cast<int32>(ECataclysmType::War));
	return true;
}

#undef CATACLYSM_SEEDING_TEST

#endif // WITH_AUTOMATION_TESTS
