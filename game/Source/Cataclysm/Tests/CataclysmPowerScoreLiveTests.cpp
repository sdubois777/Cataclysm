// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Player/CataclysmPowerScore.h"
#include "Tests/CataclysmTestWorld.h"
#include "Tests/CataclysmTestRun.h"

/**
 * THE PLAYER'S OWN POWER SCORE, read from the character. Issue #2824, slice a, the coordinating session's rulings of
 * 2026-11-22: the score built from the running character (its level, each worn piece's rarity and gear level with the
 * hands as one piece, each filled socket's gem rarity, and the eight resistances after the run's tier penalty, held at
 * 0 to 70), shown on the character sheet with its four terms; and the design's Dungeon Score (its "Dungeon Score
 * Formula": the middle floor's six rarity scores weighted by how common each rarity is), the owner's "Dungeons also
 * need their power scores as well", shown beside the player's wherever a dungeon is offered.
 *
 * THE REFERENCE IS THE CODE'S, NOT THE DOCUMENT'S: a character built to the tier 8 reference must score what
 * `UCataclysmPowerScore::Score(ReferenceCharacter(8))` gives. That was the tier 8 anchor, 6,327, until the owner's plain
 * weights of 2026-11-24 (issue #2853); it is 6,528 now, 3.2% above the anchor.
 *
 * WRITTEN BEFORE THE CHANGE, WITHOUT A BUILD.
 */
namespace CataclysmPowerScoreLiveTest
{
	using Power = UCataclysmPowerScore;

	/** A piece of `Rarity` at `GearLevel` with `Gems` filled sockets of `GemRarity`, on any base. */
	FCataclysmItem Piece(ECataclysmRarity Rarity, int32 GearLevel, int32 Gems = 0,
						 ECataclysmRarity GemRarity = ECataclysmRarity::Everyday)
	{
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Test_Power_Score_Piece"));
		Item.GearLevel = GearLevel;
		Item.EnchantmentCount = UCataclysmItemValues::EnchantmentsFor(Rarity);
		Item.Affixes.SetNum(UCataclysmItemValues::AffixSlotsFor(Rarity));
		for (int32 Index = 0; Index < Gems; ++Index)
		{
			FCataclysmSocket Socket;
			Socket.Gem = FName(TEXT("Gem_Of_The_Viper"));
			Socket.GemRarity = GemRarity;
			Item.Sockets.Add(Socket);
		}
		return Item;
	}

	/** Every slot empty. */
	TArray<FCataclysmItem> Bare()
	{
		TArray<FCataclysmItem> Worn;
		Worn.SetNum(static_cast<int32>(ECataclysmGearSlot::Count));
		return Worn;
	}

	FCataclysmItem& In(TArray<FCataclysmItem>& Worn, ECataclysmGearSlot Slot)
	{
		return Worn[static_cast<int32>(Slot)];
	}

	/**
	 * The tier 8 reference as worn gear: all seventeen pieces outside the hands and one weapon Cataclysmic at +10, and
	 * the reference's 45 Cataclysmic gems -- six in the weapon, three in each of the thirteen pieces after the first four.
	 */
	TArray<FCataclysmItem> TierEightReference()
	{
		TArray<FCataclysmItem> Worn = Bare();
		int32 Placed = 0;
		for (int32 Index = 0; Index < static_cast<int32>(ECataclysmGearSlot::Count); ++Index)
		{
			const ECataclysmGearSlot Slot = static_cast<ECataclysmGearSlot>(Index);
			if (Slot == ECataclysmGearSlot::Weapon2)
			{
				continue;
			}
			const int32 Gems = Slot == ECataclysmGearSlot::Weapon1 ? 6 : (Placed++ >= 4 ? 3 : 0);
			Worn[Index] = Piece(ECataclysmRarity::Cataclysmic, 10, Gems, ECataclysmRarity::Cataclysmic);
		}
		return Worn;
	}

	TArray<float> Resistances(float Each)
	{
		TArray<float> Out;
		Out.Init(Each, Power::ResistanceCount);
		return Out;
	}

	int32 ScoreOf(int32 Level, const TArray<FCataclysmItem>& Worn, const TArray<float>& Held)
	{
		return Power::Score(Power::ScoredCharacterFrom(Level, Worn, Held));
	}
}

// --------------------------------------------------------------------------------------------------------------------
// PL1. Worn gear built to the tier 8 reference scores the reference: 6,528 under the owner's plain weights.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerScoreLiveReferenceTest,
	"Cataclysm.PowerScoreLive.GearBuiltToTheTierEightReferenceScoresTheReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Level 100, eighteen Cataclysmic pieces at +10 (the hands as one), 45 Cataclysmic gems and eight resistances at 70.
 * It scores what `ReferenceCharacter(8)` scores, 6,528: 1,000 from level, 3,168 from gear, 1,800 from gems and 560 from
 * resistances. SINCE ISSUE #2853 THAT IS NOT THE TIER 8 ANCHOR, 6,327: the weights are the owner's plain numbers of
 * 2026-11-24 and the finished character lands 3.2% above the anchor rather than on it. Prints "LIVE REFERENCE".
 */
bool FCataclysmPowerScoreLiveReferenceTest::RunTest(const FString&)
{
	using namespace CataclysmPowerScoreLiveTest;
	const FCataclysmScoredCharacter Built = Power::ScoredCharacterFrom(100, TierEightReference(), Resistances(70.0f));
	const FCataclysmPowerBreakdown Live = Power::Breakdown(Built);
	const int32 Reference = Power::Score(Power::ReferenceCharacter(8));
	AddInfo(FString::Printf(TEXT("LIVE REFERENCE: %d pieces, %d gems, live %d (level %.1f, gear %.1f, gems %.1f, "
								 "resistances %.1f), reference %d, anchor %d"),
							Built.Gear.Num(), Built.Gems.Num(), Live.Total, Live.FromLevel, Live.FromGear, Live.FromGems,
							Live.FromResistances, Reference, Power::TierAnchors()[8]));

	TestEqual(TEXT("eighteen pieces, the hands as one"), Built.Gear.Num(), Power::GearPieces);
	TestEqual(TEXT("45 gems"), Built.Gems.Num(), Power::TotalSockets);
	TestEqual(TEXT("**it scores the tier 8 reference**"), Live.Total, Reference);
	TestEqual(TEXT("**which is 6,528 under the plain weights**"), Reference, 6528);
	TestEqual(TEXT("1,000 from level"), Live.FromLevel, 1000.0f);
	TestEqual(TEXT("3,168 from gear: eighteen pieces of 2 x 8 x 11"), Live.FromGear, 3168.0f);
	TestEqual(TEXT("1,800 from gems: 45 of 5 x 8"), Live.FromGems, 1800.0f);
	TestEqual(TEXT("560 from resistances: eight of 70"), Live.FromResistances, 560.0f);
	TestEqual(TEXT("set-up: the tier 8 anchor did not move"), Power::TierAnchors()[8], 6327);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PL2. The hands count once: a second weapon adds nothing, the better of two counts, and six of their gems count.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerScoreLiveHandsTest,
	"Cataclysm.PowerScoreLive.TheHandsCountOnceWithTheBetterPieceAndSixGems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * From the tier 8 reference: a second Cataclysmic +10 weapon with no gems scores the same; an Everyday +0 weapon in the
 * first hand beside a Cataclysmic +10 in the second scores the same (the higher-scoring of the two, the ruling's Q4);
 * and two weapons of six gems each -- the capstone's twelve -- count six. CONTROL: an Everyday +0 weapon alone in the
 * hands scores less. Prints "LIVE HANDS".
 */
bool FCataclysmPowerScoreLiveHandsTest::RunTest(const FString&)
{
	using namespace CataclysmPowerScoreLiveTest;
	const TArray<float> Capped = Resistances(70.0f);
	const int32 Reference = ScoreOf(100, TierEightReference(), Capped);

	TArray<FCataclysmItem> Two = TierEightReference();
	In(Two, ECataclysmGearSlot::Weapon2) = Piece(ECataclysmRarity::Cataclysmic, 10);
	const int32 SecondWeapon = ScoreOf(100, Two, Capped);

	TArray<FCataclysmItem> Swapped = TierEightReference();
	In(Swapped, ECataclysmGearSlot::Weapon1) = Piece(ECataclysmRarity::Everyday, 0, 6, ECataclysmRarity::Cataclysmic);
	In(Swapped, ECataclysmGearSlot::Weapon2) = Piece(ECataclysmRarity::Cataclysmic, 10);
	const int32 BetterInTheSecondHand = ScoreOf(100, Swapped, Capped);

	TArray<FCataclysmItem> Capstone = TierEightReference();
	In(Capstone, ECataclysmGearSlot::Weapon2) = Piece(ECataclysmRarity::Cataclysmic, 10, 6, ECataclysmRarity::Cataclysmic);
	const FCataclysmScoredCharacter CapstoneBuilt = Power::ScoredCharacterFrom(100, Capstone, Capped);

	TArray<FCataclysmItem> Weak = TierEightReference();
	In(Weak, ECataclysmGearSlot::Weapon1) = Piece(ECataclysmRarity::Everyday, 0, 6, ECataclysmRarity::Cataclysmic);
	const int32 WeakAlone = ScoreOf(100, Weak, Capped);
	AddInfo(FString::Printf(TEXT("LIVE HANDS: reference %d; a second weapon %d; the better in the second hand %d; capstone "
								 "%d gems, %d; a weak weapon alone %d"),
							Reference, SecondWeapon, BetterInTheSecondHand, CapstoneBuilt.Gems.Num(),
							Power::Score(CapstoneBuilt), WeakAlone));

	TestEqual(TEXT("**a second weapon adds nothing**"), SecondWeapon, Reference);
	TestEqual(TEXT("**the better of the two hands counts**"), BetterInTheSecondHand, Reference);
	TestEqual(TEXT("**twelve gems in the hands count six**"), CapstoneBuilt.Gems.Num(), Power::TotalSockets);
	TestEqual(TEXT("so the capstone scores the reference"), Power::Score(CapstoneBuilt), Reference);
	TestTrue(TEXT("CONTROL: a weak weapon alone scores less"), WeakAlone < Reference);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PL3. Resistance counts between 0 and 70.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerScoreLiveResistanceTest,
	"Cataclysm.PowerScoreLive.ResistanceCountsBetweenNoughtAndSeventy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A level 10 character with no gear: resistance at 95 scores as at 70; at -20, which a tier's penalty can leave, as at
 * 0. CONTROL: 40 scores between them. Prints "LIVE RESISTANCE".
 */
bool FCataclysmPowerScoreLiveResistanceTest::RunTest(const FString&)
{
	using namespace CataclysmPowerScoreLiveTest;
	const TArray<FCataclysmItem> None = Bare();
	const int32 AtNinetyFive = ScoreOf(10, None, Resistances(95.0f));
	const int32 AtSeventy = ScoreOf(10, None, Resistances(70.0f));
	const int32 AtForty = ScoreOf(10, None, Resistances(40.0f));
	const int32 AtNought = ScoreOf(10, None, Resistances(0.0f));
	const int32 BelowNought = ScoreOf(10, None, Resistances(-20.0f));
	AddInfo(FString::Printf(TEXT("LIVE RESISTANCE: 95 %d, 70 %d, 40 %d, 0 %d, -20 %d"), AtNinetyFive, AtSeventy, AtForty,
							AtNought, BelowNought));

	TestEqual(TEXT("**above 70 adds nothing**"), AtNinetyFive, AtSeventy);
	TestEqual(TEXT("**below 0 takes nothing away**"), BelowNought, AtNought);
	TestTrue(TEXT("CONTROL: 40 scores between 0 and 70"), AtNought < AtForty && AtForty < AtSeventy);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PL4. A socketed gem adds its rarity times the gem weight.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerScoreLiveGemTest,
	"Cataclysm.PowerScoreLive.ASocketedGemAddsItsRarityTimesTheGemWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An Everyday helm with one Superb gem (rarity 3) against the same helm with its socket empty: the gems' term is 3 times
 * `GemWeight`, and an empty socket adds nothing. Prints "LIVE GEM".
 */
bool FCataclysmPowerScoreLiveGemTest::RunTest(const FString&)
{
	using namespace CataclysmPowerScoreLiveTest;
	TArray<FCataclysmItem> WithGem = Bare();
	In(WithGem, ECataclysmGearSlot::Head) = Piece(ECataclysmRarity::Everyday, 0, 1, ECataclysmRarity::Superb);
	TArray<FCataclysmItem> Empty = Bare();
	In(Empty, ECataclysmGearSlot::Head) = Piece(ECataclysmRarity::Everyday, 0);
	FCataclysmSocket Unfilled;
	In(Empty, ECataclysmGearSlot::Head).Sockets.Add(Unfilled);
	const FCataclysmPowerBreakdown Gem = Power::Breakdown(Power::ScoredCharacterFrom(1, WithGem, Resistances(0.0f)));
	const FCataclysmPowerBreakdown None = Power::Breakdown(Power::ScoredCharacterFrom(1, Empty, Resistances(0.0f)));
	AddInfo(FString::Printf(TEXT("LIVE GEM: with the gem %.4f, the socket empty %.4f, weight %.4f"), Gem.FromGems,
							None.FromGems, Power::GemWeight()));

	TestEqual(TEXT("**one Superb gem adds 3 x the gem weight**"), Gem.FromGems, 3.0f * Power::GemWeight(), 1.0e-3f);
	TestEqual(TEXT("an empty socket adds nothing"), None.FromGems, 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PL5. The character's own level, worn gear and resistance, the resistance after the run's tier penalty.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerScoreLiveCharacterTest,
	"Cataclysm.PowerScoreLive.TheCharacterIsReadWithItsResistanceAfterTheTiersPenalty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A possessed player at level 32, wearing one Cataclysmic +10 helm, with 100 resistance to the first damage type and
 * none to the rest. At tier 1 that resistance counts 70; at tier 8 it counts 100 less the tier's penalty, held at 0 to
 * 70 (the ruling's Q5). CONTROL: a type with none counts 0 at both. Prints "LIVE CHARACTER".
 */
bool FCataclysmPowerScoreLiveCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmPowerScoreLiveTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player state, a controller and a character"), State && Controller && Character))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Character);
	State->SetLevelAndExperience(32, 0);
	UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Character);
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	const FName FirstType = UCataclysmItemModifiers::DamageTypeNames()[0];
	const FGameplayAttribute Resisted = UCataclysmDamageCalculation::ResistanceAttributeFor(FirstType);
	if (!TestTrue(TEXT("set-up: its abilities, its equipment and a resistance attribute"),
				  Abilities && Equipment && Resisted.IsValid() && Abilities->HasAttributeSetForAttribute(Resisted)))
	{
		return false;
	}
	FCataclysmItem Helm = Piece(ECataclysmRarity::Cataclysmic, 10);
	Helm.Base = FName(TEXT("Head_Helm"));
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	Equipment->EquipInto(Helm, ECataclysmGearSlot::Head, Removed, AlsoRemoved);
	// THE RESISTANCE AFTER THE HELM, AND NOT BEFORE IT: equipping announces a change of gear, whose stat refresh
	// (`UCataclysmPlayerClassStats::ApplyTo`) rewrites every stat attribute from the class line, and a 100 set before
	// it read 0 in window power-score-w1's first run.
	Abilities->SetNumericAttributeBase(Resisted, 100.0f);

	const FCataclysmScoredCharacter AtOne = Power::ScoredCharacterOf(Character, 1);
	const FCataclysmScoredCharacter AtEight = Power::ScoredCharacterOf(Character, 8);
	const float Penalty = UCataclysmDamageCalculation::ResistancePenaltyAt(8);
	const float Expected = FMath::Clamp(100.0f - Penalty, 0.0f, Power::ResistanceCap);
	AddInfo(FString::Printf(TEXT("LIVE CHARACTER: level %d, %d pieces (rarity %d, +%d); %s at tier 1 %.1f, at tier 8 %.1f "
								 "(penalty %.1f); the second type %.1f"),
							AtOne.Level, AtOne.Gear.Num(), AtOne.Gear.Num() > 0 ? AtOne.Gear[0].Rarity : -1,
							AtOne.Gear.Num() > 0 ? AtOne.Gear[0].Upgrade : -1, *FirstType.ToString(),
							AtOne.Resistances.Num() > 0 ? AtOne.Resistances[0] : -1.0f,
							AtEight.Resistances.Num() > 0 ? AtEight.Resistances[0] : -1.0f, Penalty,
							AtOne.Resistances.Num() > 1 ? AtOne.Resistances[1] : -1.0f));

	TestEqual(TEXT("its level"), AtOne.Level, 32);
	TestTrue(TEXT("its helm, Cataclysmic at +10"), AtOne.Gear.Num() == 1 && AtOne.Gear[0].Rarity == Power::MaxRarity
				 && AtOne.Gear[0].Upgrade == 10);
	if (!TestEqual(TEXT("eight resistances"), AtOne.Resistances.Num(), Power::ResistanceCount)
		|| !TestEqual(TEXT("eight at tier 8 too"), AtEight.Resistances.Num(), Power::ResistanceCount))
	{
		return false;
	}
	TestEqual(TEXT("**at tier 1 the 100 counts 70**"), AtOne.Resistances[0], 70.0f, 0.01f);
	TestEqual(TEXT("**at tier 8 it counts 100 less the tier's penalty, held at 0 to 70**"), AtEight.Resistances[0],
			  Expected, 0.01f);
	TestTrue(TEXT("set-up: tier 8's penalty is above nothing, so the two differ"), Penalty > 30.0f);
	TestEqual(TEXT("CONTROL: a type with none counts 0"), AtOne.Resistances[1], 0.0f, 0.01f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PL6. The character sheet's line and its four terms.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerScoreLiveSheetTest,
	"Cataclysm.PowerScoreLive.TheSheetSaysTheScoreItsTierRangeAndItsFourTerms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Power Score 4,745" on the sheet's status line; on hover, the tier range it falls in and each term, rounded, with its
 * weight beside it since issue #2853. A score of 7,000 is above tier 8's range; one of 300 is in tier 1's. Prints
 * "LIVE SHEET".
 */
bool FCataclysmPowerScoreLiveSheetTest::RunTest(const FString&)
{
	FCataclysmPowerBreakdown Breakdown;
	Breakdown.FromLevel = 632.7f;
	Breakdown.FromGear = 2950.2f;
	Breakdown.FromGems = 1000.4f;
	Breakdown.FromResistances = 161.6f;
	Breakdown.Total = 4745;
	const FString Line = UCataclysmCharacterSheetLayout::PowerScoreLine(Breakdown.Total);
	const FString Tooltip = UCataclysmCharacterSheetLayout::PowerScoreTooltip(Breakdown);
	FCataclysmPowerBreakdown Above;
	Above.Total = 7000;
	FCataclysmPowerBreakdown Low;
	Low.Total = 300;
	const FString AboveTooltip = UCataclysmCharacterSheetLayout::PowerScoreTooltip(Above);
	const FString LowTooltip = UCataclysmCharacterSheetLayout::PowerScoreTooltip(Low);
	AddInfo(FString::Printf(TEXT("LIVE SHEET: \"%s\" | \"%s\" | \"%s\" | \"%s\""), *Line,
							*Tooltip.Replace(TEXT("\n"), TEXT(" / ")), *AboveTooltip.Replace(TEXT("\n"), TEXT(" / ")),
							*LowTooltip.Replace(TEXT("\n"), TEXT(" / "))));

	TestEqual(TEXT("**the status line's words**"), Line, FString(TEXT("Power Score 4,745")));
	TestEqual(TEXT("**and its tooltip, the range and each term beside its weight**"), Tooltip,
			  FString(TEXT("Power Score 4,745, in tier 7's range of 4,058 to 5,120.\n"
						   "From level: 633 (10 a level)\n"
						   "From gear: 2,950 (2 a rarity step of each piece, times 1 + its gear level)\n"
						   "From gems: 1,000 (5 a rarity step of each socketed gem)\n"
						   "From resistances: 162 (1 a resistance percent, up to 70)")));
	TestTrue(TEXT("a score above tier 8's range says so"),
			 AboveTooltip.StartsWith(TEXT("Power Score 7,000, above tier 8's range, which ends at 6,327.")));
	TestTrue(TEXT("CONTROL: 300 is in tier 1's range"),
			 LowTooltip.StartsWith(TEXT("Power Score 300, in tier 1's range of 0 to 385.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PL7. A dungeon on the map: its Dungeon Score, the design's formula, and the line beside it naming both scores.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerScoreLiveDungeonTest,
	"Cataclysm.PowerScoreLive.ADungeonShowsItsDungeonScoreBesideThePlayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An Elite dungeon of 22 floors on the run's first city: its scored floor is floor 11 of 22, at the run's tier, kind,
 * sub-type and ring; its Dungeon Score is the design's formula worked out here by hand -- that floor's score at each
 * rarity times the rarity table's spawn weight, summed in the table's order, then rounded half up -- and its line names
 * that score and the player's. With no player score, only the dungeon's. CONTROL: a dungeon not on the map gives no
 * line. Prints "LIVE DUNGEON".
 */
bool FCataclysmPowerScoreLiveDungeonTest::RunTest(const FString&)
{
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
	Run->Begin(7);
	if (!TestNotNull(TEXT("set-up: the run has a map"), Run->Map.Get())
		|| !TestNotNull(TEXT("set-up: and a clock"), Run->Clock.Get())
		|| !TestNotNull(TEXT("set-up: the enemy rarity table"), UCataclysmEnemyRarity::LoadEnemyRarityTable()))
	{
		return false;
	}
	FCataclysmDungeon Dungeon;
	Dungeon.DungeonId = 960;
	Dungeon.CityId = Run->Map->Cities[0].CityId;
	Dungeon.Floors = 22;
	Dungeon.ResolveDays = 9.0f;
	Dungeon.Type = ECataclysmDungeonType::Basic;
	Dungeon.SubType = ECataclysmDungeonSubType::Elite;
	Run->Dungeons.Add(Dungeon);
	Run->Clock->AddDungeon(960, 22);
	Run->Clock->SetResolveDays(960, 9.0f);

	FCataclysmScoredFloor ByHand;
	ByHand.DifficultyTier = Run->DifficultyTier;
	ByHand.Type = ECataclysmDungeonType::Basic;
	ByHand.SubType = ECataclysmDungeonSubType::Elite;
	ByHand.TotalFloors = 22;
	ByHand.FloorNumber = 11;
	ByHand.ModifierScore = Run->ModifierScoreCarriedBy(960);
	ByHand.CityTier = Dungeon.CityTier;
	double Sum = 0.0;
	for (int32 Step = 0; Step < UCataclysmEnemyScore::RarityStepCount; ++Step)
	{
		const double Weight = FMath::RoundToDouble(static_cast<double>(UCataclysmEnemyRarity::SpawnWeightForStep(
			UCataclysmEnemyRarity::LoadEnemyRarityTable(), Step)) * 10000.0) / 10000.0;
		Sum += static_cast<double>(UCataclysmEnemyScore::ScoreFor(ByHand, Step)) * Weight;
	}
	const int32 Expected = static_cast<int32>(FMath::FloorToDouble(Sum + 0.5));

	FCataclysmScoredFloor Middle;
	const bool bFound = UCataclysmEnemyScore::FloorOfDungeon(Run, 960, Middle);
	const int32 Score = UCataclysmEnemyScore::DungeonScoreOf(Run, 960);
	const FString Line = UCataclysmEmpireMapLayout::DungeonScoreLine(Run, 960, 1100);
	const FString Unknown = UCataclysmEmpireMapLayout::DungeonScoreLine(Run, 960, INDEX_NONE);
	const FString Missing = UCataclysmEmpireMapLayout::DungeonScoreLine(Run, 999, 1100);
	AddInfo(FString::Printf(TEXT("LIVE DUNGEON: found %d, floor %d of %d, tier %d; score %d, by hand %d; \"%s\" | \"%s\" | "
								 "\"%s\""),
							bFound, Middle.FloorNumber, Middle.TotalFloors, Middle.DifficultyTier, Score, Expected, *Line,
							*Unknown, *Missing));

	TestTrue(TEXT("the dungeon is found"), bFound);
	TestEqual(TEXT("**its scored floor is the middle one, 11 of 22**"), Middle.FloorNumber, 11);
	TestEqual(TEXT("of 22"), Middle.TotalFloors, 22);
	TestEqual(TEXT("at the run's tier"), Middle.DifficultyTier, Run->DifficultyTier);
	TestTrue(TEXT("of its kind and sub-type"), Middle.Type == ECataclysmDungeonType::Basic
				 && Middle.SubType == ECataclysmDungeonSubType::Elite);
	TestEqual(TEXT("**its Dungeon Score is the design's formula**"), Score, Expected);
	TestEqual(TEXT("**the line names both scores**"), Line,
			  FString::Printf(TEXT("Dungeon Score %s; your Power Score 1,100."),
							  *UCataclysmEmpireMapLayout::ScoreText(Expected)));
	TestEqual(TEXT("with no player score, the dungeon's alone"), Unknown,
			  FString::Printf(TEXT("Dungeon Score %s."), *UCataclysmEmpireMapLayout::ScoreText(Expected)));
	TestTrue(TEXT("CONTROL: a dungeon not on the map gives no line"), Missing.IsEmpty());
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PL8. The Dungeon Score is the simulation's, on six dungeons; modifiers raise it; any floor of a dungeon gives the same.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerScoreLiveDungeonModelTest,
	"Cataclysm.PowerScoreLive.TheDungeonScoreIsTheSimulationsOnSixDungeons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `sim/cataclysm_sim/scoring.py`'s `dungeon_score` on six dungeons, each figure written out here and checked against the
 * simulation by `tools/tests/test_the_dungeon_score_is_the_simulations.py`, which reads this table back: the first is the
 * simulation's own self-test, 201 for tier 1, Basic, Timed, 50 floors. The fourth is the third with 12.5 of modifiers,
 * and scores above it. Asked of floor 1 and of the last floor, each gives the same figure: the score is the middle
 * floor's whichever floor it is asked from. Prints "DUNGEON MODEL".
 */
bool FCataclysmPowerScoreLiveDungeonModelTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("set-up: the enemy rarity table"), UCataclysmEnemyRarity::LoadEnemyRarityTable()))
	{
		return false;
	}
	struct FCase
	{
		int32 Tier;
		ECataclysmDungeonType Type;
		ECataclysmDungeonSubType SubType;
		int32 Floors;
		float ModifierScore;
		int32 Expected;
	};
	// THE SIMULATION'S FIGURES. tools/tests/test_the_dungeon_score_is_the_simulations.py reads these rows back.
	const FCase Cases[] = {
		{ 1, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Timed, 50, 0.0f, 201 },
		{ 4, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::Elite, 22, 0.0f, 2009 },
		{ 6, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Sacrificial, 30, 0.0f, 3757 },
		{ 6, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Sacrificial, 30, 12.5f, 3770 },
		{ 8, ECataclysmDungeonType::FallenCity, ECataclysmDungeonSubType::None, 15, 0.0f, 5873 },
		{ 3, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Horde, 11, 4.0f, 1252 },
	};
	TArray<FString> Said;
	TArray<int32> Got;
	for (const FCase& Case : Cases)
	{
		FCataclysmScoredFloor Floor;
		Floor.DifficultyTier = Case.Tier;
		Floor.Type = Case.Type;
		Floor.SubType = Case.SubType;
		Floor.TotalFloors = Case.Floors;
		Floor.ModifierScore = Case.ModifierScore;
		Floor.FloorNumber = 1;
		const int32 FromTheFirst = UCataclysmEnemyScore::DungeonScoreFor(Floor);
		Floor.FloorNumber = Case.Floors;
		const int32 FromTheLast = UCataclysmEnemyScore::DungeonScoreFor(Floor);
		Got.Add(FromTheFirst);
		Said.Add(FString::Printf(TEXT("T%d %d floors: %d (asked from the last floor %d), the simulation's %d"), Case.Tier,
								 Case.Floors, FromTheFirst, FromTheLast, Case.Expected));
		TestEqual(*FString::Printf(TEXT("**tier %d, %d floors, modifiers %.1f: the simulation's %d**"), Case.Tier,
								   Case.Floors, Case.ModifierScore, Case.Expected),
				  FromTheFirst, Case.Expected);
		TestEqual(TEXT("and the same asked from its last floor"), FromTheLast, FromTheFirst);
	}
	AddInfo(FString::Printf(TEXT("DUNGEON MODEL: %s"), *FString::Join(Said, TEXT(" | "))));
	TestTrue(TEXT("**modifiers raise the Dungeon Score**"), Got.Num() == 6 && Got[3] > Got[2]);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PL9. A worn Masterful +0 piece scores 2 x its rarity, as much as the tier 1 reference's Everyday +3. Issue #2853.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerScoreLiveMasterfulPieceTest,
	"Cataclysm.PowerScoreLive.AMasterfulPieceAtPlusNoughtScoresTwiceItsRarity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner's plain weights of 2026-11-24: a piece is 2 x rarity x (1 + gear level). A level 1 character wearing one
 * Masterful +0 helm (rarity 4, read from what fills the helm's slots, as in play) scores 8 from gear, where the derived
 * weights gave 24.9; the same helm at Everyday +3, the tier 1 reference's piece, also scores 8, so wearing the rarer
 * piece never lowers the score. CONTROL: the Masterful helm at +3 scores 32, so the 8 is not a flat figure per piece.
 * Prints "LIVE MASTERFUL".
 */
bool FCataclysmPowerScoreLiveMasterfulPieceTest::RunTest(const FString&)
{
	using namespace CataclysmPowerScoreLiveTest;
	const TArray<float> None = Resistances(0.0f);
	TArray<FCataclysmItem> Masterful = Bare();
	In(Masterful, ECataclysmGearSlot::Head) = Piece(ECataclysmRarity::Masterful, 0);
	TArray<FCataclysmItem> EverydayPlusThree = Bare();
	In(EverydayPlusThree, ECataclysmGearSlot::Head) = Piece(ECataclysmRarity::Everyday, 3);
	TArray<FCataclysmItem> MasterfulPlusThree = Bare();
	In(MasterfulPlusThree, ECataclysmGearSlot::Head) = Piece(ECataclysmRarity::Masterful, 3);

	const FCataclysmScoredCharacter Built = Power::ScoredCharacterFrom(1, Masterful, None);
	const FCataclysmPowerBreakdown AtNought = Power::Breakdown(Built);
	const FCataclysmPowerBreakdown Reference = Power::Breakdown(Power::ScoredCharacterFrom(1, EverydayPlusThree, None));
	const FCataclysmPowerBreakdown AtThree = Power::Breakdown(Power::ScoredCharacterFrom(1, MasterfulPlusThree, None));
	AddInfo(FString::Printf(TEXT("LIVE MASTERFUL: %d piece (rarity %d, +%d) from gear %.4f; Everyday +3 %.4f; Masterful +3 "
								 "%.4f"),
							Built.Gear.Num(), Built.Gear.Num() > 0 ? Built.Gear[0].Rarity : -1,
							Built.Gear.Num() > 0 ? Built.Gear[0].Upgrade : -1, AtNought.FromGear, Reference.FromGear,
							AtThree.FromGear));

	if (!TestEqual(TEXT("set-up: one piece is worn"), Built.Gear.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("set-up: it is read as Masterful, rarity 4"), Built.Gear[0].Rarity, 4);
	TestEqual(TEXT("**a Masterful +0 piece scores 2 x its rarity, 8**"), AtNought.FromGear,
			  2.0f * static_cast<float>(Built.Gear[0].Rarity));
	TestEqual(TEXT("which is 8"), AtNought.FromGear, 8.0f);
	TestEqual(TEXT("**as much as the tier 1 reference's Everyday +3**"), Reference.FromGear, AtNought.FromGear);
	TestEqual(TEXT("CONTROL: at +3 it scores 2 x 4 x 4, 32"), AtThree.FromGear, 32.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// PL10. The sheet's tooltip for a level 6 character shows each term beside its plain weight. Issue #2853.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPowerScoreLiveSheetWeightsTest,
	"Cataclysm.PowerScoreLive.TheSheetShowsEachTermBesideItsPlainWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner's screen of 2026-11-24 showed 38 from level for a level 6 character and the owner asked why. A level 6
 * character wearing a Masterful +0 helm with one Superb gem, and 10 resistance to one damage type: 60 from level, 8 from
 * gear, 15 from gems and 10 from resistances, 93 in all, and the tooltip says each figure beside the weight it came
 * from. Prints "LIVE SHEET WEIGHTS".
 */
bool FCataclysmPowerScoreLiveSheetWeightsTest::RunTest(const FString&)
{
	using namespace CataclysmPowerScoreLiveTest;
	TArray<FCataclysmItem> Worn = Bare();
	In(Worn, ECataclysmGearSlot::Head) = Piece(ECataclysmRarity::Masterful, 0, 1, ECataclysmRarity::Superb);
	TArray<float> Held = Resistances(0.0f);
	Held[0] = 10.0f;
	const FCataclysmPowerBreakdown Breakdown = Power::Breakdown(Power::ScoredCharacterFrom(6, Worn, Held));
	const FString Line = UCataclysmCharacterSheetLayout::PowerScoreLine(Breakdown.Total);
	const FString Tooltip = UCataclysmCharacterSheetLayout::PowerScoreTooltip(Breakdown);
	AddInfo(FString::Printf(TEXT("LIVE SHEET WEIGHTS: level %.1f, gear %.1f, gems %.1f, resistances %.1f, total %d; \"%s\" "
								 "| \"%s\""),
							Breakdown.FromLevel, Breakdown.FromGear, Breakdown.FromGems, Breakdown.FromResistances,
							Breakdown.Total, *Line, *Tooltip.Replace(TEXT("\n"), TEXT(" / "))));

	TestEqual(TEXT("**60 from level**"), Breakdown.FromLevel, 60.0f);
	TestEqual(TEXT("8 from gear"), Breakdown.FromGear, 8.0f);
	TestEqual(TEXT("15 from gems"), Breakdown.FromGems, 15.0f);
	TestEqual(TEXT("10 from resistances"), Breakdown.FromResistances, 10.0f);
	TestEqual(TEXT("93 in all"), Breakdown.Total, 93);
	TestEqual(TEXT("the status line"), Line, FString(TEXT("Power Score 93")));
	TestEqual(TEXT("**the tooltip says each term beside its plain weight**"), Tooltip,
			  FString(TEXT("Power Score 93, in tier 1's range of 0 to 385.\n"
						   "From level: 60 (10 a level)\n"
						   "From gear: 8 (2 a rarity step of each piece, times 1 + its gear level)\n"
						   "From gems: 15 (5 a rarity step of each socketed gem)\n"
						   "From resistances: 10 (1 a resistance percent, up to 70)")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
