// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"
#include "Player/CataclysmPowerScore.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * OVERWHELM. Issue #2824, slice b, the coordinating session's rulings of 2026-11-22: a creature whose Enemy Score stands
 * above the player's live Power Score takes min(50%, 25% x the gap / the tier's width) off the player's mitigation, in
 * points. Off resistance before the 70 cap, as penetration is (the design's damage order, step 4); off the evasion
 * chance, the block chance and armour's reduction percentage (Q7); and a tick of damage over time meets it through
 * resistance (Q8). `overwhelm` in `sim/cataclysm_sim/combat.py` is the model these figures come from, and
 * `tools/tests/test_overwhelm_is_the_simulations.py` holds the two constants and the two worked figures to it.
 *
 * WRITTEN BEFORE THE CHANGE, WITHOUT A BUILD.
 */
namespace CataclysmOverwhelmTest
{
	using Power = UCataclysmPowerScore;

	/** The design's worked floor: tier 8, a Cataclysm dungeon with no sub-type, the last of its 50 floors, no modifier. */
	FCataclysmScoredFloor WorkedFloor()
	{
		FCataclysmScoredFloor Floor;
		Floor.DifficultyTier = 8;
		Floor.Type = ECataclysmDungeonType::Cataclysm;
		Floor.SubType = ECataclysmDungeonSubType::None;
		Floor.TotalFloors = 50;
		Floor.FloorNumber = 50;
		Floor.ModifierScore = 0.0f;
		return Floor;
	}

	/** The console variable every hit's tier is read from, so a test can set it and put it back. */
	IConsoleVariable* TierVariable()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DifficultyTier"));
	}

	/** A bare defender with the three sets a hit reads, its health large enough that no blow reaches the floor. */
	struct FDefender
	{
		explicit FDefender(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			Abilities = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			Abilities->RegisterComponent();
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			Combat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			Resistances = NewObject<UCataclysmResistanceAttributeSet>(Actor);
			Abilities->AddAttributeSetSubobject(NewVitals);
			Abilities->AddAttributeSetSubobject(Combat.Get());
			Abilities->AddAttributeSetSubobject(Resistances.Get());
			Abilities->InitAbilityActorInfo(Actor, Actor);
			// 10,000 AND NOT 1,000,000, so every figure below lands on the float grid (issue #1728).
			NewVitals->SetMaxHealth(10'000.0f);
			NewVitals->SetHealth(10'000.0f);
		}

		/** A 1,000 Demonic blow carrying `Overwhelm` points, with both rolls pinned. */
		float Landed(float Overwhelm, bool bTick = false, float EvasionRoll = 100.0f, float BlockRoll = 100.0f) const
		{
			FCataclysmIncomingHit Hit;
			Hit.Damage = 1'000.0f;
			Hit.DamageType = FName(TEXT("Demonic"));
			Hit.bIsDamageOverTime = bTick;
			Hit.OverwhelmPoints = Overwhelm;
			return UCataclysmDamageCalculation::Resolve(Hit, Abilities, /*Tier=*/1, EvasionRoll, BlockRoll)
				.DealtToHealth;
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> Abilities = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
		TObjectPtr<UCataclysmResistanceAttributeSet> Resistances = nullptr;
	};

	/** A possessed player at level 1 wearing nothing, or null with the failure reported. */
	ACataclysmPlayerCharacter* MakePlayer(FAutomationTestBase& Test, UWorld* World)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestTrue(TEXT("set-up: a player state, a controller and a character"), State && Controller && Character))
		{
			return nullptr;
		}
		Controller->SetPlayerState(State);
		Controller->Possess(Character);
		State->SetLevelAndExperience(1, 0);
		return Character;
	}
}

#define CATACLYSM_OVERWHELM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// OW1. The design's worked figures: a tier 8 player at 6,327 loses 8.4 points to a Common and 20.9 to a Cataclysm Boss.
// --------------------------------------------------------------------------------------------------------------------
/**
 * FROM THE CONSTANTS THE CODE HOLDS, NOT THE DOCUMENT'S ROUNDING: 25% x (6,733 - 6,327) / 1,207 is 8.4093 points and
 * 25% x (7,336 - 6,327) / 1,207 is 20.8989. `overwhelm` in the simulation gives the same two to four places, which the
 * Python check holds. Prints "OVERWHELM WORKED".
 */
CATACLYSM_OVERWHELM_TEST(FCataclysmOverwhelmWorkedFiguresTest,
	"Cataclysm.Overwhelm.ATierEightPlayerLosesEightPointFourToACommonAndTwentyPointNineToACataclysmBoss")
{
	using namespace CataclysmOverwhelmTest;
	const FCataclysmScoredFloor Floor = WorkedFloor();
	const int32 Player = Power::Score(Power::ReferenceCharacter(8));
	const int32 Common = UCataclysmEnemyScore::ScoreFor(Floor, 0);
	const int32 CataclysmBoss = UCataclysmEnemyScore::ScoreFor(Floor, UCataclysmEnemyScore::RarityStepCount - 1);
	const double Width = UCataclysmEnemyScore::TierWidth(8);
	const float FromCommon = Power::OverwhelmPointsAt(Player, Floor, 0);
	const float FromBoss = Power::OverwhelmPointsAt(Player, Floor, UCataclysmEnemyScore::RarityStepCount - 1);
	AddInfo(FString::Printf(TEXT("OVERWHELM WORKED: player %d, Common %d, Cataclysm Boss %d, width %.0f; %.4f and %.4f"),
							Player, Common, CataclysmBoss, Width, FromCommon, FromBoss));

	TestEqual(TEXT("set-up: the tier 8 reference scores 6,327"), Player, 6327);
	TestEqual(TEXT("set-up: a Common on the worked floor scores 6,733"), Common, 6733);
	TestEqual(TEXT("set-up: a Cataclysm Boss there scores 7,336"), CataclysmBoss, 7336);
	TestEqual(TEXT("set-up: tier 8 is 1,207 wide"), Width, 1207.0, 0.001);
	TestEqual(TEXT("**a Common takes 8.4093 points**"), FromCommon, 8.4093f, 0.0005f);
	TestEqual(TEXT("**a Cataclysm Boss takes 20.8989 points**"), FromBoss, 20.8989f, 0.0005f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// OW2. A player at or above the creature's score loses nothing.
// --------------------------------------------------------------------------------------------------------------------
/** CONTROL: one point below it loses something, so the zero is the floor and not a function that answers nothing. */
CATACLYSM_OVERWHELM_TEST(FCataclysmOverwhelmNothingAboveTest,
	"Cataclysm.Overwhelm.APlayerAtOrAboveTheCreaturesScoreLosesNothing")
{
	using Power = UCataclysmPowerScore;
	const float Level = Power::OverwhelmFor(6733, 6733, 1207.0);
	const float Above = Power::OverwhelmFor(7000, 6733, 1207.0);
	const float FarAbove = Power::OverwhelmFor(20000, 6733, 1207.0);
	const float Below = Power::OverwhelmFor(6732, 6733, 1207.0);
	AddInfo(FString::Printf(TEXT("OVERWHELM ABOVE: level %.6f, above %.6f, far above %.6f, one below %.6f"), Level, Above,
							FarAbove, Below));

	TestEqual(TEXT("**level with it, nothing**"), Level, 0.0f);
	TestEqual(TEXT("**above it, nothing**"), Above, 0.0f);
	TestEqual(TEXT("**far above it, still nothing and not a bonus**"), FarAbove, 0.0f);
	TestTrue(TEXT("CONTROL: one point below it, something"), Below > 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// OW3. Held at 50%.
// --------------------------------------------------------------------------------------------------------------------
/** Two tier widths of gap reach the cap exactly; one point less is under it; any more stays at it. Prints "OVERWHELM CAP". */
CATACLYSM_OVERWHELM_TEST(FCataclysmOverwhelmCapTest, "Cataclysm.Overwhelm.ItIsHeldAtHalf")
{
	using Power = UCataclysmPowerScore;
	const float AtTwoWidths = Power::OverwhelmFor(0, 2414, 1207.0);
	const float JustUnder = Power::OverwhelmFor(1, 2414, 1207.0);
	const float FarPast = Power::OverwhelmFor(0, 100000, 1207.0);
	const float NoWidth = Power::OverwhelmFor(0, 7336, 0.0);
	AddInfo(FString::Printf(TEXT("OVERWHELM CAP: two widths %.6f, one point less %.6f, far past %.6f, no width %.6f"),
							AtTwoWidths, JustUnder, FarPast, NoWidth));

	TestEqual(TEXT("set-up: the cap is the simulation's 50%"), Power::OverwhelmCap, 0.50f);
	TestEqual(TEXT("two tier widths of gap reach it"), AtTwoWidths, 0.50f, 0.000001f);
	TestTrue(TEXT("one point less is under it"), JustUnder < 0.50f);
	TestEqual(TEXT("**far past it, held at it**"), FarPast, 0.50f);
	TestEqual(TEXT("a tier with no width gives nothing, as the simulation's does"), NoWidth, 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// OW4. Off resistance before the cap, so over-capped resistance absorbs it; and a tick meets it the same way.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A 1,000 Demonic blow at tier 1 (no tier penalty). At 50 resistance it lands for 500, and with 20 Overwhelm for 700. At
 * 90, over the 70 cap by 20, it lands for 300 either way: the 20 is absorbed. At 80 it lands for 400 with it, 10
 * absorbed and 10 felt. A tick of damage over time at 50 with 20 lands for 700 (Q8). Prints "OVERWHELM RESIST".
 */
CATACLYSM_OVERWHELM_TEST(FCataclysmOverwhelmResistanceTest,
	"Cataclysm.Overwhelm.ItComesOffResistanceBeforeTheCapSoOverCappingAbsorbsIt")
{
	using namespace CataclysmOverwhelmTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FDefender D(World);

	D.Resistances->SetDemonicResistance(50.0f);
	const float AtFiftyWithout = D.Landed(0.0f);
	const float AtFiftyWith = D.Landed(20.0f);
	const float TickAtFiftyWith = D.Landed(20.0f, /*bTick=*/true);
	D.Resistances->SetDemonicResistance(90.0f);
	const float AtNinetyWithout = D.Landed(0.0f);
	const float AtNinetyWith = D.Landed(20.0f);
	D.Resistances->SetDemonicResistance(80.0f);
	const float AtEightyWith = D.Landed(20.0f);
	AddInfo(FString::Printf(TEXT("OVERWHELM RESIST: at 50 %.2f then %.2f (tick %.2f); at 90 %.2f then %.2f; at 80 with "
								 "20, %.2f"),
							AtFiftyWithout, AtFiftyWith, TickAtFiftyWith, AtNinetyWithout, AtNinetyWith, AtEightyWith));

	TestEqual(TEXT("CONTROL: at 50 resistance, 500"), AtFiftyWithout, 500.0f, 0.01f);
	TestEqual(TEXT("**at 50 with 20 Overwhelm, 700**"), AtFiftyWith, 700.0f, 0.01f);
	TestEqual(TEXT("CONTROL: at 90 the cap holds it to 70, 300"), AtNinetyWithout, 300.0f, 0.01f);
	TestEqual(TEXT("**at 90 with 20 Overwhelm, still 300: the over-cap absorbs it**"), AtNinetyWith, 300.0f, 0.01f);
	TestEqual(TEXT("**at 80 with 20, 400: ten absorbed and ten felt**"), AtEightyWith, 400.0f, 0.01f);
	TestEqual(TEXT("**a tick of damage over time meets it through resistance, 700**"), TickAtFiftyWith, 700.0f, 0.01f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// OW5. Off the evasion chance, the block chance and armour's reduction percentage, each held at nought.
// --------------------------------------------------------------------------------------------------------------------
/**
 * Q7. 30 evasion against a roll of 25 evades; less 10 Overwhelm it is 20 and does not; less 4 it is 26 and still does.
 * 30 block against 25 the same, a block landing 500 of 1,000. 800 armour at tier 1 removes 50%, so 500 lands; less 10
 * points it removes 40%, so 600; less 60 points it removes nothing, so 1,000 and not more. Prints "OVERWHELM DEFENCES".
 */
CATACLYSM_OVERWHELM_TEST(FCataclysmOverwhelmDefencesTest,
	"Cataclysm.Overwhelm.ItComesOffEvasionBlockAndArmourHeldAtNought")
{
	using namespace CataclysmOverwhelmTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FDefender D(World);

	D.Combat->SetEvasion(30.0f);
	const float EvadedWithout = D.Landed(0.0f, false, /*EvasionRoll=*/25.0f);
	const float EvadedWithFour = D.Landed(4.0f, false, /*EvasionRoll=*/25.0f);
	const float EvadedWithTen = D.Landed(10.0f, false, /*EvasionRoll=*/25.0f);
	D.Combat->SetEvasion(0.0f);

	D.Combat->SetBlockChance(30.0f);
	const float BlockedWithout = D.Landed(0.0f, false, 100.0f, /*BlockRoll=*/25.0f);
	const float BlockedWithTen = D.Landed(10.0f, false, 100.0f, /*BlockRoll=*/25.0f);
	D.Combat->SetBlockChance(0.0f);

	D.Combat->SetArmor(800.0f);
	const float ArmouredWithout = D.Landed(0.0f);
	const float ArmouredWithTen = D.Landed(10.0f);
	const float ArmouredWithSixty = D.Landed(60.0f);
	AddInfo(FString::Printf(TEXT("OVERWHELM DEFENCES: evasion %.2f, %.2f (4), %.2f (10); block %.2f, %.2f (10); armour "
								 "%.2f, %.2f (10), %.2f (60)"),
							EvadedWithout, EvadedWithFour, EvadedWithTen, BlockedWithout, BlockedWithTen,
							ArmouredWithout, ArmouredWithTen, ArmouredWithSixty));

	TestEqual(TEXT("CONTROL: 30 evasion against 25 evades"), EvadedWithout, 0.0f);
	TestEqual(TEXT("less 4 points it is 26, which still evades"), EvadedWithFour, 0.0f);
	TestEqual(TEXT("**less 10 points it is 20, which does not: the blow lands**"), EvadedWithTen, 1000.0f, 0.01f);
	TestEqual(TEXT("CONTROL: 30 block against 25 blocks half"), BlockedWithout, 500.0f, 0.01f);
	TestEqual(TEXT("**less 10 points it does not block**"), BlockedWithTen, 1000.0f, 0.01f);
	TestEqual(TEXT("set-up: 800 armour at tier 1 removes half"), UCataclysmDamageCalculation::ArmorReduction(800.0f, 1),
			  50.0f, 0.001f);
	TestEqual(TEXT("CONTROL: so 500 lands"), ArmouredWithout, 500.0f, 0.01f);
	TestEqual(TEXT("**less 10 points armour removes 40%, so 600**"), ArmouredWithTen, 600.0f, 0.01f);
	TestEqual(TEXT("**less 60 points it removes nothing, and does not add**"), ArmouredWithSixty, 1000.0f, 0.01f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// OW6. A creature's Overwhelm on a player: its world's floor, its own rung, the player's live score.
// --------------------------------------------------------------------------------------------------------------------
/**
 * At tier 1 in a bare world (the floor `FloorIn` answers with its defaults there: one Basic floor of one), a level 1
 * player wearing nothing against a Common and a Cataclysm Boss, which the simulation scores 352 and 545 there, inside
 * two tier widths (385 each) of a bare player, so neither is held at the cap. Each must be 100 x min(0.5, 0.25 x (its
 * score - the player's) / tier 1's width), worked out here from the scores rather than through `OverwhelmPointsAt`,
 * and the Boss's above the Common's.
 * Nothing for a missing creature or player. THE WIRING INTO A HIT, `UCataclysmVitalAttributeSet` filling
 * `OverwhelmPoints` from this, is held by text in `tools/tests/test_overwhelm_is_the_simulations.py`, not run here.
 * Prints "OVERWHELM CREATURE".
 */
CATACLYSM_OVERWHELM_TEST(FCataclysmOverwhelmCreatureTest,
	"Cataclysm.Overwhelm.ACreatureAppliesItsOwnRungsOverwhelmOnItsFloor")
{
	using namespace CataclysmOverwhelmTest;
	IConsoleVariable* Tier = TierVariable();
	if (!TestNotNull(TEXT("set-up: the difficulty tier console variable"), Tier))
	{
		return false;
	}
	const int32 TierBefore = Tier->GetInt();
	Tier->Set(1, ECVF_SetByCode);
	ON_SCOPE_EXIT { Tier->Set(TierBefore, ECVF_SetByCode); };

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmPlayerCharacter* Player = MakePlayer(*this, World);
	ACataclysmEnemyCharacter* Common =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Boss =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(-1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!Player || !TestTrue(TEXT("set-up: two creatures"), Common && Boss))
	{
		return false;
	}
	Common->RarityStep = 0;
	Boss->RarityStep = UCataclysmEnemyScore::RarityStepCount - 1;

	const FCataclysmScoredFloor Floor = UCataclysmEnemyScore::FloorIn(World);
	const int32 PlayerScore = Power::LiveScoreOf(Player);
	const double Width = UCataclysmEnemyScore::TierWidth(Floor.DifficultyTier);
	const auto Expected = [&](int32 Step)
	{
		const double Gap = FMath::Max(0.0, static_cast<double>(UCataclysmEnemyScore::ScoreFor(Floor, Step) - PlayerScore));
		return static_cast<float>(100.0 * FMath::Min(0.5, 0.25 * Gap / Width));
	};
	const float FromCommon = Power::OverwhelmPointsAgainst(Player, Common);
	const float FromBoss = Power::OverwhelmPointsAgainst(Player, Boss);
	AddInfo(FString::Printf(TEXT("OVERWHELM CREATURE: tier %d, player %d, Common %.4f (expected %.4f), Cataclysm Boss "
								 "%.4f (expected %.4f)"),
							Floor.DifficultyTier, PlayerScore, FromCommon, Expected(0), FromBoss,
							Expected(UCataclysmEnemyScore::RarityStepCount - 1)));

	TestEqual(TEXT("set-up: the floor is tier 1's"), Floor.DifficultyTier, 1);
	TestTrue(TEXT("set-up: the Boss's is under the cap"), FromBoss < 100.0f * Power::OverwhelmCap);
	TestEqual(TEXT("**a Common applies its rung's Overwhelm**"), FromCommon, Expected(0), 0.001f);
	TestEqual(TEXT("**a Cataclysm Boss applies its rung's**"), FromBoss,
			  Expected(UCataclysmEnemyScore::RarityStepCount - 1), 0.001f);
	TestTrue(TEXT("the Boss's is above the Common's"), FromBoss > FromCommon);
	TestEqual(TEXT("no creature, nothing"), Power::OverwhelmPointsAgainst(Player, nullptr), 0.0f);
	TestEqual(TEXT("no player, nothing"), Power::OverwhelmPointsAgainst(nullptr, Boss), 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// OW7. The live score is cached, and worked out again on gear, level and tier.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A resistance written straight onto the attribute, with no stat refresh, is not seen: the cached score stays, while
 * the score worked out afresh moves (the control that the write mattered). Equipping a helm and gaining levels each
 * bring the cached score back to the fresh one. Then 100 resistance is written, unseen again, and moving the tier from 1
 * to 8 brings it back too: at tier 8 the 100 counts 100 less the tier's penalty where the cache held the refresh's. Prints "OVERWHELM CACHE".
 */
CATACLYSM_OVERWHELM_TEST(FCataclysmOverwhelmCacheTest,
	"Cataclysm.Overwhelm.TheLiveScoreIsCachedAndWorkedOutAgainOnGearLevelAndTier")
{
	using namespace CataclysmOverwhelmTest;
	IConsoleVariable* Tier = TierVariable();
	if (!TestNotNull(TEXT("set-up: the difficulty tier console variable"), Tier))
	{
		return false;
	}
	const int32 TierBefore = Tier->GetInt();
	Tier->Set(1, ECVF_SetByCode);
	ON_SCOPE_EXIT { Tier->Set(TierBefore, ECVF_SetByCode); };

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmPlayerCharacter* Player = MakePlayer(*this, World);
	if (!Player)
	{
		return false;
	}
	UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Player);
	UCataclysmEquipmentComponent* Equipment = Player->GetEquipment();
	ACataclysmPlayerState* State = Player->GetPlayerState<ACataclysmPlayerState>();
	const FGameplayAttribute Resisted =
		UCataclysmDamageCalculation::ResistanceAttributeFor(UCataclysmItemModifiers::DamageTypeNames()[0]);
	if (!TestTrue(TEXT("set-up: abilities, equipment, a player state and a resistance attribute"),
				  Abilities && Equipment && State && Resisted.IsValid() && Abilities->HasAttributeSetForAttribute(Resisted)))
	{
		return false;
	}
	const auto Fresh = [&]()
	{
		return Power::Score(Power::ScoredCharacterOf(Player, ACataclysmGameMode::DifficultyTierIn(Player)));
	};

	const int32 First = Power::LiveScoreOf(Player);
	Abilities->SetNumericAttributeBase(Resisted, 70.0f);
	const int32 AfterWrite = Power::LiveScoreOf(Player);
	const int32 FreshAfterWrite = Fresh();

	FCataclysmItem Helm;
	Helm.Base = FName(TEXT("Head_Helm"));
	Helm.GearLevel = 10;
	Helm.EnchantmentCount = UCataclysmItemValues::EnchantmentsFor(ECataclysmRarity::Cataclysmic);
	Helm.Affixes.SetNum(UCataclysmItemValues::AffixSlotsFor(ECataclysmRarity::Cataclysmic));
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	Equipment->EquipInto(Helm, ECataclysmGearSlot::Head, Removed, AlsoRemoved);
	const int32 AfterHelm = Power::LiveScoreOf(Player);
	const int32 FreshAfterHelm = Fresh();

	State->SetLevelAndExperience(60, 0);
	const int32 AfterLevel = Power::LiveScoreOf(Player);
	const int32 FreshAfterLevel = Fresh();

	Abilities->SetNumericAttributeBase(Resisted, 100.0f);
	const int32 BeforeTier = Power::LiveScoreOf(Player);
	Tier->Set(8, ECVF_SetByCode);
	const int32 AfterTier = Power::LiveScoreOf(Player);
	const int32 FreshAfterTier = Fresh();
	AddInfo(FString::Printf(TEXT("OVERWHELM CACHE: first %d; after the write %d (fresh %d); after the helm %d (fresh %d); "
								 "after level 60 %d (fresh %d); 100 written %d; at tier 8 %d (fresh %d)"),
							First, AfterWrite, FreshAfterWrite, AfterHelm, FreshAfterHelm, AfterLevel, FreshAfterLevel,
							BeforeTier, AfterTier, FreshAfterTier));

	TestTrue(TEXT("CONTROL: the write moves the score worked out afresh"), FreshAfterWrite != First);
	TestEqual(TEXT("**a write with no refresh is not seen: the cached score stays**"), AfterWrite, First);
	TestTrue(TEXT("set-up: the helm moves the score"), FreshAfterHelm != First);
	TestEqual(TEXT("**the helm brings it back to the fresh score**"), AfterHelm, FreshAfterHelm);
	TestTrue(TEXT("set-up: the levels move the score"), FreshAfterLevel != FreshAfterHelm);
	TestEqual(TEXT("**so do the levels**"), AfterLevel, FreshAfterLevel);
	TestEqual(TEXT("the 100 written is not seen either"), BeforeTier, AfterLevel);
	TestTrue(TEXT("set-up: at tier 8 the fresh score differs from the cached one"), FreshAfterTier != BeforeTier);
	TestEqual(TEXT("**and the tier brings it back to the fresh score**"), AfterTier, FreshAfterTier);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// OW8. The floor panel's line says what a Common and a Cataclysm Boss take on this floor.
// --------------------------------------------------------------------------------------------------------------------
/** On the worked floor at 6,327, "8.4" and "20.9"; at 8,000, none; with no player, nothing. Prints "OVERWHELM LINE". */
CATACLYSM_OVERWHELM_TEST(FCataclysmOverwhelmLineTest,
	"Cataclysm.Overwhelm.TheFloorPanelSaysWhatACommonAndACataclysmBossTakeOff")
{
	using namespace CataclysmOverwhelmTest;
	const FString Below = Power::OverwhelmText(6327, WorkedFloor());
	const FString Above = Power::OverwhelmText(8000, WorkedFloor());
	const FString Nobody = Power::OverwhelmText(INDEX_NONE, WorkedFloor());
	AddInfo(FString::Printf(TEXT("OVERWHELM LINE: \"%s\" / \"%s\" / \"%s\""), *Below, *Above, *Nobody));

	TestTrue(TEXT("**below every creature: a Common's 8.4**"), Below.Contains(TEXT("8.4 from a Common")));
	TestTrue(TEXT("**and a Cataclysm Boss's 20.9**"), Below.Contains(TEXT("20.9 from a Cataclysm Boss")));
	TestTrue(TEXT("above every creature: none"), Above.Contains(TEXT("none")));
	TestTrue(TEXT("no player: no line"), Nobody.IsEmpty());
	return true;
}

#undef CATACLYSM_OVERWHELM_TEST

#endif // WITH_AUTOMATION_TESTS
