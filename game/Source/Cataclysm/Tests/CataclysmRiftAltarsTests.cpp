// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmHeavenlyCoreCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmRiftAltarCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmFloorModifierPanelLayout.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * A Rift's four altars: the Demonic Quest dungeon, issue #51 slice B1. Since 2026-11-24. WRITTEN BEFORE ITS FIRST
 * BUILD, WITHOUT A COMPILER: a window builds it and runs these.
 *
 * THE OWNER'S RULINGS OF 2026-11-24 (issue #51), each the option chosen, verbatim: "Rift = Quest dungeon
 * (Recommended)" -- a Rift is a Demonic Quest dungeon, and sealing it is one quest objective -- and "One per floor,
 * spread (Recommended)": "Altar *i* on floor ceil(*i* x floors / 4), the spread ruled for a Fallen City's bosses on
 * 2026-10-29 (#1466); a Rift of fewer than four floors puts the rest on its last floor. An altar does not attack, has
 * the floor boss's health, and seals its floor's stairs while it stands, as the Heavenly Core does. The Rift is sealed
 * when the last altar falls and the boss is dead."
 *
 * AND LATER THE SAME EVENING (issue #51, the owner's second comment): "A Rift is a Quest dungeon of exactly 4 floors,
 * one altar on each floor (this replaces the altar spread ceil(i x floors / 4), which now always gives one per
 * floor)." The owner, verbatim: "let's make them 4 floors?" So FOUR FLOORS IS THE RULED CASE, and these tests walk a
 * Rift four floors deep wherever they can. WHERE THE DEPTH IS SET IS SLICE B2'S, on the empire side, so a test that
 * needs a Rift of four floors builds one. The spread is kept general, for a Rift of another depth: one Demonic rolled
 * before the empire makes Rifts four floors deep (20 to 70 floors, `UCataclysmSurgeScheduler::SpecFor`), and one whose
 * depth a city's floor upgrades moved, if slice B2 lets them move a Rift's. The tests of those depths say so.
 *
 * EVERY FIXTURE'S STARTING STATE, AND WHERE IT COMES FROM:
 * - the run is `UCataclysmEmpireRun::Begin(1)` and one `AdvanceDay`, as the Heavenly Core's and the Cataclysm Boss's
 *   tests build theirs, and the first dungeon of it that is not a Cow Level is taken and rewritten: its depth, its kind,
 *   who sent it, its sub-type, and no modifiers, so its floors draw none. A Rift is a Quest dungeon Demonic sent, Elite
 *   as `UCataclysmSurgeScheduler::QuestSubTypeFor` makes it, four floors deep unless a test says otherwise;
 * - `EnemyScale` is 0, so the population places the boss at the exit and nothing else, as `Cataclysm.BossFloor.`'s;
 * - the player is a possessed `ACataclysmPlayerCharacter` at the world's origin, with no gear.
 * WHAT THESE DO NOT RUN: a real save and load, which `Cataclysm.SaveLoad.ARiftsFallenAltarStaysFallenAfterALoad`
 * does; and a fall into a chasm, which `Cataclysm.DungeonModifierEffects.ARiftsAltarFloorHasNoChasmsAndAFallLands...`
 * does beside the other Those in the Dark tests, whose helpers it uses.
 */
namespace CataclysmRiftAltarsTest
{
	/** A player character possessed by a controller, as the dungeon game mode finds one. */
	struct FRiftAltarsPlayer
	{
		explicit FRiftAltarsPlayer(UWorld* World)
		{
			State = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (State && Controller && Character)
			{
				Controller->SetPlayerState(State);
				Controller->Possess(Character);
			}
		}

		bool IsUsable() const { return State && Controller && Character && Character->GetAbilitySystemComponent(); }

		ACataclysmPlayerState* State = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
	};

	/** The world, the game mode and the run every world test here starts from. */
	struct FRiftAltarsScene
	{
		ACataclysmDungeonGameMode* Mode = nullptr;
		UCataclysmEmpireRun* Run = nullptr;
	};

	FRiftAltarsScene MakeRiftAltarsScene(UWorld* World)
	{
		FRiftAltarsScene Out;
		Out.Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
		Out.Run = NewObject<UCataclysmEmpireRun>();
		if (Out.Mode && Out.Run)
		{
			Out.Run->Begin(1);
			Out.Run->AdvanceDay();
			Out.Mode->SetEmpireRunForTests(Out.Run);
			// THE BOSS AND NOTHING ELSE ON A FLOOR. See the file's comment.
			Out.Mode->EnemyScale = 0.0f;
		}
		return Out;
	}

	/**
	 * Takes the first dungeon of the run that is not a Cow Level, makes it `Floors` deep, of this kind, sent by this
	 * Cataclysm, of this sub-type and carrying no modifiers, and enters it. Answers its identifier, or `INDEX_NONE`. A
	 * second call takes the same dungeon again, after leaving the walk under way.
	 */
	int32 EnterARiftAltarsDungeon(FAutomationTestBase& Test, ACataclysmDungeonGameMode* Mode, UCataclysmEmpireRun* Run,
								  int32 Floors, ECataclysmDungeonType Type, ECataclysmType Sender,
								  ECataclysmDungeonSubType SubType)
	{
		int32 Chosen = INDEX_NONE;
		for (int32 Index = 0; Index < Run->Dungeons.Num() && Chosen == INDEX_NONE; ++Index)
		{
			if (Run->Dungeons[Index].SubType != ECataclysmDungeonSubType::CowLevel)
			{
				Chosen = Index;
			}
		}
		if (!Test.TestTrue(TEXT("set-up: the run has a dungeon to walk"), Chosen != INDEX_NONE))
		{
			return INDEX_NONE;
		}

		FCataclysmDungeon& Dungeon = Run->Dungeons[Chosen];
		Dungeon.Floors = Floors;
		Dungeon.Type = Type;
		Dungeon.Cataclysm = Sender;
		Dungeon.SubType = SubType;
		Dungeon.Modifiers.Reset();
		Dungeon.ModifierScore = 0.0f;
		const int32 DungeonId = Dungeon.DungeonId;
		if (!Test.TestTrue(TEXT("set-up: the dungeon was entered"), Mode->EnterEmpireDungeon(DungeonId)))
		{
			return INDEX_NONE;
		}
		return DungeonId;
	}

	/** A Rift: a Quest dungeon Demonic sent, Elite as its Cataclysm's Quest dungeons are. */
	int32 EnterARift(FAutomationTestBase& Test, ACataclysmDungeonGameMode* Mode, UCataclysmEmpireRun* Run, int32 Floors)
	{
		return EnterARiftAltarsDungeon(Test, Mode, Run, Floors, ECataclysmDungeonType::Quest, ECataclysmType::Demonic,
									   ECataclysmDungeonSubType::Elite);
	}

	/** The floor's Gatekeeper, the one the population placed at the exit, or null. */
	ACataclysmEnemyCharacter* RiftAltarsFloorGatekeeper(const ACataclysmDungeonGameMode* Mode)
	{
		for (ACataclysmEnemyCharacter* Enemy : Mode->FloorEnemies)
		{
			if (IsValid(Enemy) && Enemy->IsA<ACataclysmGatekeeperCharacter>() && !UCataclysmSkillEffects::IsDead(Enemy))
			{
				return Enemy;
			}
		}
		return nullptr;
	}

	/** The panel's line for the Rift, as the panel would draw it, or empty when the panel lists none. */
	FString RiftPanelLine(const ACataclysmDungeonGameMode* Mode)
	{
		using Layout = UCataclysmFloorModifierPanelLayout;
		const TArray<FCataclysmFloorModifierLine> Lines = Layout::LinesFor(
			Mode->RowsTheFloorPanelLists(), UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
			Mode->LiveCountsForTheFloor());
		for (const FCataclysmFloorModifierLine& Line : Lines)
		{
			if (Line.bIsTheRiftAltars)
			{
				return Layout::NameLineFor(Line);
			}
		}
		return FString();
	}

	/** Whether the stairs are sealed by exactly these keys, in `StairsSealedBy`'s order. */
	bool SealedByExactly(const ACataclysmDungeonGameMode* Mode, const TArray<FName>& Keys)
	{
		return Mode->StairsSealedBy() == Keys;
	}

	/** How much health a creature can hold. */
	float RiftAltarsMaxHealthOf(const ACataclysmEnemyCharacter* Creature)
	{
		const UAbilitySystemComponent* Abilities = Creature ? Creature->GetAbilitySystemComponent() : nullptr;
		return Abilities ? Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : -1.0f;
	}

	/**
	 * One blow from the player that is certain to kill: no evasion, block or retaliation, no energy shield, and one point
	 * of health, then a blow through the game's own hit, so its death notice reaches the floor's rules. A creature's
	 * drawn modifiers are taken off first (a Sacrificial Bond keeps only a share of a blow), and with them its starting
	 * figures put back, as `Cataclysm.BossFloor.` readies its boss; an altar draws none and keeps its own.
	 */
	bool FellIt(FAutomationTestBase& Test, const FRiftAltarsPlayer& Player, ACataclysmEnemyCharacter* Victim,
				const TCHAR* What)
	{
		if (!Test.TestNotNull(FString::Printf(TEXT("set-up: %s stands"), What), Victim))
		{
			return false;
		}
		if (!Victim->IsA<ACataclysmRiftAltarCharacter>())
		{
			Victim->ModifierRows.Reset();
			Victim->ApplyStartingAttributes();
		}
		if (UAbilitySystemComponent* Abilities = Victim->GetAbilitySystemComponent())
		{
			Abilities->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Abilities->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Abilities->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetRetaliationAttribute(), 0.0f);
			Abilities->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
			Abilities->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
		}
		UCataclysmSkillEffects::ApplyHit(Player.Character, Victim, 100000.0f);
		return Test.TestTrue(FString::Printf(TEXT("set-up: the player's blow felled %s"), What),
							 UCataclysmSkillEffects::IsDead(Victim));
	}

	/** One run fixture, read through the save system as a load reads it. Null, with an error, when it will not load. */
	const UCataclysmRunSave* ReadRiftAltarsRunFixture(FAutomationTestBase& Test, const TCHAR* Name,
													  ECataclysmSaveLoadResult& OutResult)
	{
		FString Text;
		FString Reason;
		if (!CataclysmSaveFixtures::Read(Name, Text, Reason))
		{
			Test.AddError(Reason);
			return nullptr;
		}
		FString Message;
		OutResult = ECataclysmSaveLoadResult::NotValidJson;
		const UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), OutResult, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name,
										  FCataclysmSaveStorage::Describe(OutResult), *Message));
		}
		return Read;
	}

	/** A list of ints written "a,b,c", for a message and a comparison. */
	FString RiftAltarsJoined(const TArray<int32>& Numbers)
	{
		FString Out;
		for (const int32 Number : Numbers)
		{
			Out += Out.IsEmpty() ? FString::FromInt(Number) : FString::Printf(TEXT(",%d"), Number);
		}
		return Out;
	}

	/** The floor each of a Rift's four altars stands on, "a,b,c,d", by `FCataclysmDungeonFloorRules::RiftAltarFloor`. */
	FString AltarFloorsOf(int32 TotalFloors)
	{
		TArray<int32> Floors;
		for (int32 Altar = 1; Altar <= FCataclysmDungeonFloorRules::RiftAltars; ++Altar)
		{
			Floors.Add(FCataclysmDungeonFloorRules::RiftAltarFloor(TotalFloors, Altar));
		}
		return RiftAltarsJoined(Floors);
	}
}

// ---------------------------------------------------------------------------
// Where the altars stand
// ---------------------------------------------------------------------------

/**
 * A Rift four floors deep, the owner's Rift, holds one altar on each floor. And a Rift of another depth -- 1 to 12
 * floors here, and 30 -- still holds its four where the first ruling puts them: altar i on floor ceil(i x floors / 4),
 * at most one a floor, the rest on the last floor of a Rift of fewer than four. From four floors on, they stand exactly
 * where a Fallen City holding four bosses puts its bosses, because both rules call the one spread. Prints "RIFT ALTAR
 * FLOORS". `tools/tests/test_a_rifts_four_altars.py` works the table below out again in Python, from the ruling's
 * sentence, so the expected figures are not this file's say-so alone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftAltarsSpreadTest,
	"Cataclysm.RiftAltars.AFourFloorRiftHoldsOneAltarAFloorAndAnyOtherDepthSpreadsItsFour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftAltarsSpreadTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftAltarsTest;
	using Rules = FCataclysmDungeonFloorRules;

	TestEqual(TEXT("a Rift holds four altars, the design's 'the four altars powering each one'"), Rules::RiftAltars, 4);

	// THE OWNER'S RIFT: FOUR FLOORS, ONE ALTAR ON EACH.
	TestEqual(TEXT("a Rift of four floors puts its altars on floors 1, 2, 3 and 4"), AltarFloorsOf(4),
			  FString(TEXT("1,2,3,4")));
	for (int32 Floor = 1; Floor <= 4; ++Floor)
	{
		TestEqual(FString::Printf(TEXT("floor %d of a Rift of four holds one altar"), Floor),
				  Rules::RiftAltarsOnFloor(4, Floor), 1);
		TestEqual(FString::Printf(TEXT("and %d stand above it"), Floor - 1), Rules::RiftAltarsAboveFloor(4, Floor),
				  Floor - 1);
	}
	TestEqual(TEXT("from floor 1 of four the next altar stands on floor 2"), Rules::NextRiftAltarFloorAfter(4, 1), 2);
	TestEqual(TEXT("and from its last floor there is none"), Rules::NextRiftAltarFloorAfter(4, 4), 0);

	// AND EVERY DEPTH FROM 1 TO 12 FLOORS: THE SPREAD, KEPT FOR A RIFT OF ANOTHER DEPTH, WITH FOUR'S ROW AGAIN AMONG
	// THEM. Fewer than four: one a floor, the rest on the last.
	struct FRiftAltarsRow
	{
		int32 Floors;
		const TCHAR* AltarFloors;
	};
	const FRiftAltarsRow Table[] = {
		{1, TEXT("1,1,1,1")},
		{2, TEXT("1,2,2,2")},
		{3, TEXT("1,2,3,3")},
		{4, TEXT("1,2,3,4")},
		{5, TEXT("2,3,4,5")},
		{6, TEXT("2,3,5,6")},
		{7, TEXT("2,4,6,7")},
		{8, TEXT("2,4,6,8")},
		{9, TEXT("3,5,7,9")},
		{10, TEXT("3,5,8,10")},
		{11, TEXT("3,6,9,11")},
		{12, TEXT("3,6,9,12")},
	};
	FString Seen;
	for (const FRiftAltarsRow& Row : Table)
	{
		const FString Floors = AltarFloorsOf(Row.Floors);
		Seen += FString::Printf(TEXT(" %d:%s"), Row.Floors, *Floors);
		TestEqual(FString::Printf(TEXT("a Rift of %d floor(s) puts its altars on floors %s"), Row.Floors, Row.AltarFloors),
				  Floors, FString(Row.AltarFloors));

		// EVERY ALTAR IS ON SOME FLOOR, AND THE PER-FLOOR COUNT AGREES: four in all, and all four above the floor below
		// the last.
		int32 Counted = 0;
		for (int32 Floor = 1; Floor <= Row.Floors; ++Floor)
		{
			Counted += Rules::RiftAltarsOnFloor(Row.Floors, Floor);
		}
		TestEqual(FString::Printf(TEXT("the floors of a Rift of %d hold four altars between them"), Row.Floors), Counted,
				  4);
		TestEqual(FString::Printf(TEXT("all four are above the floor below a Rift of %d"), Row.Floors),
				  Rules::RiftAltarsAboveFloor(Row.Floors, Row.Floors + 1), 4);
		TestTrue(FString::Printf(TEXT("the last floor of a Rift of %d holds the fourth"), Row.Floors),
				 Rules::RiftAltarFloor(Row.Floors, 4) == Row.Floors);

		// FROM FOUR FLOORS ON, A FALLEN CITY'S FOUR BOSSES' FLOORS: the same spread, one rule.
		if (Row.Floors >= 4)
		{
			FCataclysmDungeonIdentity FallenCity;
			FallenCity.TotalFloors = Row.Floors;
			FallenCity.Bosses = 4;
			TArray<int32> BossFloors;
			for (int32 Floor = 1; Floor <= Row.Floors; ++Floor)
			{
				if (Rules::BossAtTheExit(FallenCity, Floor))
				{
					BossFloors.Add(Floor);
				}
			}
			TestEqual(FString::Printf(TEXT("a Rift of %d floors spreads its altars as a Fallen City its four bosses"),
									  Row.Floors),
					  Floors, RiftAltarsJoined(BossFloors));
		}
	}
	AddInfo(FString::Printf(TEXT("RIFT ALTAR FLOORS:%s"), *Seen));

	// THE READING, MADE VISIBLE: below four floors the bare formula would put two altars on a Rift of two's first
	// floor. The rule holds them to one a floor and puts the rest on the last.
	TArray<int32> Bare;
	for (int32 Altar = 1; Altar <= 4; ++Altar)
	{
		Bare.Add(Rules::SpreadFloorOf(Altar, 4, 2));
	}
	TestEqual(TEXT("CONTROL: the bare formula for two floors gives 1,1,2,2"), RiftAltarsJoined(Bare),
			  FString(TEXT("1,1,2,2")));
	TestEqual(TEXT("and the ruling's one-a-floor gives 1,2,2,2"), AltarFloorsOf(2), FString(TEXT("1,2,2,2")));

	// A DEPTH DEMONIC ROLLED BEFORE THE EMPIRE MAKES RIFTS FOUR FLOORS DEEP: an ordinary Quest dungeon's 20 to 70. Of
	// 30 floors, the altars on 8, 15, 23 and 30, and what the floor panel reads off them.
	TestEqual(TEXT("a Rift of 30 floors puts its altars on 8, 15, 23 and 30"), AltarFloorsOf(30),
			  FString(TEXT("8,15,23,30")));
	TestEqual(TEXT("from floor 3 of 30 the next altar stands on floor 8"), Rules::NextRiftAltarFloorAfter(30, 3), 8);
	TestEqual(TEXT("from floor 8 the next stands on floor 15"), Rules::NextRiftAltarFloorAfter(30, 8), 15);
	TestEqual(TEXT("from the last floor there is none"), Rules::NextRiftAltarFloorAfter(30, 30), 0);
	TestEqual(TEXT("two stand above floor 16 of 30"), Rules::RiftAltarsAboveFloor(30, 16), 2);
	TestEqual(TEXT("none above floor 8, which holds the first"), Rules::RiftAltarsAboveFloor(30, 8), 0);

	// NO FLOOR, NO SUCH ALTAR: none.
	TestEqual(TEXT("a dungeon with no floor count holds no altar"), Rules::RiftAltarFloor(0, 1), 0);
	TestEqual(TEXT("there is no altar 0"), Rules::RiftAltarFloor(5, 0), 0);
	TestEqual(TEXT("nor an altar 5"), Rules::RiftAltarFloor(5, 5), 0);
	TestEqual(TEXT("and floor 0 holds none"), Rules::RiftAltarsOnFloor(5, 0), 0);
	return true;
}

/**
 * A Rift is a Quest dungeon Demonic sent, and nothing else is: the owner's "Rift = Quest dungeon". By its sender and not
 * its sub-type, which an ordinary dungeon can roll. And a Demonic Quest dungeon is Elite, as the ruling says a Rift is.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftAltarsWhatARiftIsTest,
	"Cataclysm.RiftAltars.ARiftIsAQuestDungeonDemonicSentAndNothingElseIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftAltarsWhatARiftIsTest::RunTest(const FString& Parameters)
{
	const auto Made = [](ECataclysmDungeonType Type, ECataclysmType Sender, ECataclysmDungeonSubType SubType)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.Type = Type;
		Dungeon.Cataclysm = Sender;
		Dungeon.SubType = SubType;
		return Dungeon;
	};

	TestTrue(TEXT("an Elite Quest dungeon Demonic sent is a Rift"),
			 Made(ECataclysmDungeonType::Quest, ECataclysmType::Demonic, ECataclysmDungeonSubType::Elite).IsARift());
	TestTrue(TEXT("and so is one of any sub-type, a dungeon from before Quest sub-types were fixed"),
			 Made(ECataclysmDungeonType::Quest, ECataclysmType::Demonic, ECataclysmDungeonSubType::None).IsARift());
	TestEqual(TEXT("Demonic's Quest dungeons are Elite, as a Rift is"),
			  static_cast<int32>(UCataclysmSurgeScheduler::QuestSubTypeFor(ECataclysmType::Demonic)),
			  static_cast<int32>(ECataclysmDungeonSubType::Elite));

	TestFalse(TEXT("CONTROL: a Quest dungeon Death sent is no Rift"),
			  Made(ECataclysmDungeonType::Quest, ECataclysmType::Death, ECataclysmDungeonSubType::Horde).IsARift());
	TestFalse(TEXT("CONTROL: nor a Quest dungeon nobody sent, as a test builds one"),
			  Made(ECataclysmDungeonType::Quest, ECataclysmType::None, ECataclysmDungeonSubType::Elite).IsARift());
	TestFalse(TEXT("CONTROL: nor an ordinary Elite dungeon Demonic sent"),
			  Made(ECataclysmDungeonType::Basic, ECataclysmType::Demonic, ECataclysmDungeonSubType::Elite).IsARift());
	TestFalse(TEXT("CONTROL: nor a Fallen City"),
			  Made(ECataclysmDungeonType::FallenCity, ECataclysmType::Demonic, ECataclysmDungeonSubType::None).IsARift());
	TestFalse(TEXT("CONTROL: nor the Cataclysm boss dungeon"),
			  Made(ECataclysmDungeonType::Cataclysm, ECataclysmType::Demonic, ECataclysmDungeonSubType::None).IsARift());
	return true;
}

// ---------------------------------------------------------------------------
// The altar, and the seal it holds
// ---------------------------------------------------------------------------

/**
 * Floor 1 of a Rift four floors deep holds one altar: of its own class, with no brain and no ability, paying nothing,
 * raised by the rule and not one of the floor's creatures, with the floor boss's health, and "Altar" under its health
 * bar. While it stands it alone seals the stairs, and the floor panel says so; the player's blow fells it, the stairs
 * open, the panel says one of four has fallen and where the next stands, and the stairs take the player to floor 2,
 * where the next altar stands.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftAltarsSealTest,
	"Cataclysm.RiftAltars.AnAltarHasTheFloorBossHealthNeverAttacksAndSealsItsFloorsStairsUntilItFalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftAltarsSealTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftAltarsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FRiftAltarsPlayer Player(World);
	const FRiftAltarsScene Scene = MakeRiftAltarsScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable())
		|| EnterARift(*this, Mode, Scene.Run, 4) == INDEX_NONE)
	{
		return false;
	}
	const FName AltarSeal(ACataclysmDungeonGameMode::RiftAltarsSealKey);

	TestTrue(TEXT("the dungeon walked is a Rift"), Mode->IsARift());
	TestEqual(TEXT("its floor 1 holds one altar by the ruling"), Mode->RiftAltarsOnThisFloor(), 1);
	const TArray<ACataclysmEnemyCharacter*> Standing = Mode->RiftAltarsStandingNow();
	if (!TestEqual(TEXT("and one stands there"), Standing.Num(), 1))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Altar = Standing[0];
	TestTrue(TEXT("of its own class"), Altar->IsA<ACataclysmRiftAltarCharacter>());
	TestNull(TEXT("with no brain, so it never attacks"), Altar->GetController());
	TestTrue(TEXT("and no ability"), Altar->EnemyAbilities().IsEmpty());
	TestFalse(TEXT("it pays nothing"), Altar->PaysForItsDeath());
	TestTrue(TEXT("raised by the rule"), Altar->bRaisedByARule);
	TestFalse(TEXT("and not one of the floor's creatures"), Mode->FloorEnemies.Contains(Altar));
	TestEqual(TEXT("its health is the floor boss's"), RiftAltarsMaxHealthOf(Altar), Mode->RiftAltarHealth(), 0.5f);
	TestEqual(TEXT("the Gatekeeper's 1248, as the Heavenly Core's"), Mode->RiftAltarHealth(), Mode->HeavenlyCoreHealth(),
			  0.001f);
	TestEqual(TEXT("its health bar says what it is"), UCataclysmCombatOverlay::RiftAltarTextFor(Altar),
			  FString(TEXT("Altar")));
	TestEqual(TEXT("CONTROL: the floor's boss's does not"),
			  UCataclysmCombatOverlay::RiftAltarTextFor(RiftAltarsFloorGatekeeper(Mode)), FString());
	TestEqual(TEXT("none has fallen"), Mode->RiftAltarsFallen(), 0);

	// WHILE IT STANDS: SEALED BY IT ALONE, THE PANEL SAYS SO, AND THE STAIRS GO NOWHERE.
	if (!TestTrue(TEXT("while it stands the altar alone seals the stairs"), SealedByExactly(Mode, {AltarSeal})))
	{
		return false;
	}
	TestEqual(TEXT("the floor panel names the Rift and says the altar seals the stairs"), RiftPanelLine(Mode),
			  FString(TEXT("Infernal Rift (altars 0 of 4 fallen; the altar on this floor seals the stairs)")));
	Mode->HandleStairsTaken();
	if (!TestEqual(TEXT("so taking the stairs leaves the player on floor 1"), Mode->FloorNumber, 1))
	{
		return false;
	}

	// THE PLAYER FELLS IT.
	if (!FellIt(*this, Player, Altar, TEXT("the altar")))
	{
		return false;
	}
	TestTrue(TEXT("once it has fallen none stands"), Mode->RiftAltarsStandingNow().IsEmpty());
	TestTrue(TEXT("and nothing seals the stairs"), Mode->StairsSealedBy().IsEmpty());
	TestEqual(TEXT("one of four has fallen"), Mode->RiftAltarsFallen(), 1);
	TestEqual(TEXT("the panel says so, and where the next stands"), RiftPanelLine(Mode),
			  FString(TEXT("Infernal Rift (altars 1 of 4 fallen; the next stands on floor 2)")));
	TestEqual(TEXT("and a fallen altar's health bar says nothing"), UCataclysmCombatOverlay::RiftAltarTextFor(Altar),
			  FString());

	// THE STAIRS TAKE THE PLAYER DOWN, TO THE NEXT ALTAR.
	Mode->HandleStairsTaken();
	if (!TestEqual(TEXT("now taking the stairs goes down to floor 2"), Mode->FloorNumber, 2))
	{
		return false;
	}
	TestEqual(TEXT("where the next altar stands"), Mode->RiftAltarsStandingNow().Num(), 1);
	TestTrue(TEXT("and seals floor 2's stairs"), SealedByExactly(Mode, {AltarSeal}));
	TestEqual(TEXT("the floor above's still counted fallen"), Mode->RiftAltarsFallen(), 1);
	return true;
}

/**
 * The owner's Rift, four floors deep, walked to its seal: one altar on each floor, each felled and its stairs taken, to
 * the last floor, where the fourth altar stands with the Rift's boss. Felled in that order: the altar first, which
 * leaves the boss alone sealing the way out, and the panel says the Rift waits for it, and the stairs do not clear it;
 * then the boss, and the stairs clear the Rift, which counts as one of Demonic's quest objectives.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftAltarsClearTest,
	"Cataclysm.RiftAltars.AFourFloorRiftIsNotSealedUntilItsLastAltarAndItsBossHaveFallen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftAltarsClearTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftAltarsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FRiftAltarsPlayer Player(World);
	const FRiftAltarsScene Scene = MakeRiftAltarsScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const int32 DungeonId = EnterARift(*this, Mode, Scene.Run, 4);
	if (DungeonId == INDEX_NONE)
	{
		return false;
	}
	const FName AltarSeal(ACataclysmDungeonGameMode::RiftAltarsSealKey);
	const FName BossSeal(ACataclysmDungeonGameMode::RiftBossSealKey);
	const int32 ObjectivesBefore = Scene.Run->QuestObjectivesFor(ECataclysmType::Demonic);

	// FLOORS 1 TO 3: ONE ALTAR EACH, FELLED, AND THE STAIRS TAKEN. Their bosses do not seal: only the last floor's does.
	for (int32 Floor = 1; Floor <= 3; ++Floor)
	{
		const TArray<ACataclysmEnemyCharacter*> Here = Mode->RiftAltarsStandingNow();
		if (!TestEqual(FString::Printf(TEXT("floor %d holds one altar"), Floor), Here.Num(), 1))
		{
			return false;
		}
		TestNull(FString::Printf(TEXT("and no Rift boss is recorded on floor %d, which is not the last"), Floor),
				 Mode->RiftBossNow());
		TestTrue(FString::Printf(TEXT("floor %d's altar alone seals its stairs"), Floor),
				 SealedByExactly(Mode, {AltarSeal}));
		if (!FellIt(*this, Player, Here[0], TEXT("the floor's altar")))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("%d of four have fallen"), Floor), Mode->RiftAltarsFallen(), Floor);
		Mode->HandleStairsTaken();
		if (!TestEqual(FString::Printf(TEXT("set-up: the stairs took the player to floor %d"), Floor + 1),
					   Mode->FloorNumber, Floor + 1))
		{
			return false;
		}
	}

	// FLOOR 4, THE LAST: THE FOURTH ALTAR AND THE BOSS.
	if (!TestTrue(TEXT("set-up: floor 4 is the last"), Mode->IsOnTheLastFloor()))
	{
		return false;
	}
	const TArray<ACataclysmEnemyCharacter*> Fourth = Mode->RiftAltarsStandingNow();
	ACataclysmEnemyCharacter* Boss = Mode->RiftBossNow();
	if (!TestEqual(TEXT("the last floor holds the fourth altar"), Fourth.Num(), 1)
		|| !TestNotNull(TEXT("and the Rift's boss"), Boss))
	{
		return false;
	}
	TestTrue(TEXT("which is the floor's Gatekeeper"), Boss == RiftAltarsFloorGatekeeper(Mode));
	TestEqual(TEXT("three of four have fallen above it"), Mode->RiftAltarsFallen(), 3);
	TestTrue(TEXT("the altar and the boss both seal the way out"), SealedByExactly(Mode, {AltarSeal, BossSeal}));
	TestEqual(TEXT("the panel says the altar seals the stairs"), RiftPanelLine(Mode),
			  FString(TEXT("Infernal Rift (altars 3 of 4 fallen; the altar on this floor seals the stairs)")));

	// THE FOURTH ALTAR: THE BOSS ALONE SEALS THE WAY OUT, AND THE RIFT IS NOT CLEARED.
	if (!FellIt(*this, Player, Fourth[0], TEXT("the fourth altar")))
	{
		return false;
	}
	TestEqual(TEXT("all four have fallen"), Mode->RiftAltarsFallen(), 4);
	if (!TestTrue(TEXT("with the four fallen the boss alone seals the way out"), SealedByExactly(Mode, {BossSeal})))
	{
		return false;
	}
	TestEqual(TEXT("and the panel says the Rift waits for it"), RiftPanelLine(Mode),
			  FString(TEXT("Infernal Rift (altars 4 of 4 fallen; the boss stands, and the Rift is sealed when it dies)")));
	Mode->HandleStairsTaken();
	if (!TestNotNull(TEXT("so taking the stairs does not clear the Rift"), Scene.Run->FindDungeon(DungeonId)))
	{
		return false;
	}
	TestEqual(TEXT("and no objective is counted"), Scene.Run->QuestObjectivesFor(ECataclysmType::Demonic),
			  ObjectivesBefore);

	// THE BOSS: THE WAY OUT OPENS AND THE STAIRS SEAL THE RIFT.
	if (!FellIt(*this, Player, Boss, TEXT("the Rift's boss")))
	{
		return false;
	}
	TestTrue(TEXT("with the boss dead nothing seals the way out"), Mode->StairsSealedBy().IsEmpty());
	TestEqual(TEXT("the panel says the stairs seal the Rift"), RiftPanelLine(Mode),
			  FString(TEXT("Infernal Rift (altars 4 of 4 fallen and the boss slain; the stairs seal the Rift)")));
	Mode->HandleStairsTaken();
	TestNull(TEXT("now taking the stairs clears the Rift"), Scene.Run->FindDungeon(DungeonId));
	TestEqual(TEXT("and sealing it is one of Demonic's quest objectives"),
			  Scene.Run->QuestObjectivesFor(ECataclysmType::Demonic), ObjectivesBefore + 1);
	return true;
}

/**
 * THE FALLBACK: a Rift of fewer than four floors, which the first ruling answers: "a Rift of fewer than four floors puts
 * the rest on its last floor". REACHABLE ONLY IF SOMETHING MOVES A RIFT'S DEPTH BELOW FOUR -- a city's floor upgrade,
 * if slice B2 lets one move a Rift's -- since the owner made a Rift four floors deep; kept because the spread is kept
 * general and this is the one depth range where a floor holds more than one altar. Two floors: one altar on the first,
 * three, the rest, on the last with the boss. Felled the other way round from the four-floor test: the boss first, which
 * leaves the altars sealing the way out; each altar felled still leaves the others; the last one opens it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftAltarsFewerFloorsTest,
	"Cataclysm.RiftAltars.ARiftOfFewerThanFourFloorsPutsTheRestOfItsAltarsOnItsLastFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftAltarsFewerFloorsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftAltarsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FRiftAltarsPlayer Player(World);
	const FRiftAltarsScene Scene = MakeRiftAltarsScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const int32 DungeonId = EnterARift(*this, Mode, Scene.Run, 2);
	if (DungeonId == INDEX_NONE)
	{
		return false;
	}
	const FName AltarSeal(ACataclysmDungeonGameMode::RiftAltarsSealKey);
	const FName BossSeal(ACataclysmDungeonGameMode::RiftBossSealKey);

	// FLOOR 1: ONE ALTAR.
	if (!TestEqual(TEXT("floor 1 of two holds one altar"), Mode->RiftAltarsStandingNow().Num(), 1)
		|| !FellIt(*this, Player, Mode->RiftAltarsStandingNow()[0], TEXT("floor 1's altar")))
	{
		return false;
	}
	Mode->HandleStairsTaken();
	if (!TestEqual(TEXT("set-up: the stairs took the player to floor 2"), Mode->FloorNumber, 2)
		|| !TestTrue(TEXT("set-up: the last floor"), Mode->IsOnTheLastFloor()))
	{
		return false;
	}

	// FLOOR 2, THE LAST: THE REST, THREE ALTARS, AND THE BOSS.
	TestEqual(TEXT("the last floor holds the three altars the first did not"), Mode->RiftAltarsOnThisFloor(), 3);
	const TArray<ACataclysmEnemyCharacter*> Three = Mode->RiftAltarsStandingNow();
	ACataclysmEnemyCharacter* Boss = Mode->RiftBossNow();
	if (!TestEqual(TEXT("and three stand there"), Three.Num(), 3) || !TestNotNull(TEXT("with the Rift's boss"), Boss))
	{
		return false;
	}
	TestTrue(TEXT("the altars and the boss both seal the way out"), SealedByExactly(Mode, {AltarSeal, BossSeal}));
	TestEqual(TEXT("the panel says the three seal the stairs"), RiftPanelLine(Mode),
			  FString(TEXT("Infernal Rift (altars 1 of 4 fallen; the 3 altars on this floor seal the stairs)")));

	// THE BOSS FIRST: THE ALTARS STILL SEAL IT, AND THE RIFT IS NOT CLEARED.
	if (!FellIt(*this, Player, Boss, TEXT("the Rift's boss")))
	{
		return false;
	}
	TestNull(TEXT("no Rift boss stands"), Mode->RiftBossNow());
	TestTrue(TEXT("the altars alone seal the way out now"), SealedByExactly(Mode, {AltarSeal}));
	Mode->HandleStairsTaken();
	if (!TestNotNull(TEXT("so taking the stairs does not clear the Rift"), Scene.Run->FindDungeon(DungeonId)))
	{
		return false;
	}

	// TWO ALTARS: THE THIRD STILL SEALS IT.
	if (!FellIt(*this, Player, Three[0], TEXT("the first of the three"))
		|| !FellIt(*this, Player, Three[1], TEXT("the second of the three")))
	{
		return false;
	}
	TestTrue(TEXT("with one altar left it still seals the way out"), SealedByExactly(Mode, {AltarSeal}));
	TestEqual(TEXT("three of four have fallen"), Mode->RiftAltarsFallen(), 3);
	TestEqual(TEXT("the panel says the one left seals the stairs"), RiftPanelLine(Mode),
			  FString(TEXT("Infernal Rift (altars 3 of 4 fallen; the altar on this floor seals the stairs)")));
	Mode->HandleStairsTaken();
	if (!TestNotNull(TEXT("and taking the stairs still does not clear the Rift"), Scene.Run->FindDungeon(DungeonId)))
	{
		return false;
	}

	// THE LAST ALTAR: THE WAY OUT OPENS AND THE STAIRS CLEAR THE RIFT.
	if (!FellIt(*this, Player, Three[2], TEXT("the last altar")))
	{
		return false;
	}
	TestTrue(TEXT("once the last altar has fallen, the boss being dead, nothing seals the way out"),
			 Mode->StairsSealedBy().IsEmpty());
	TestEqual(TEXT("all four have fallen"), Mode->RiftAltarsFallen(), 4);
	Mode->HandleStairsTaken();
	TestNull(TEXT("now taking the stairs clears the Rift"), Scene.Run->FindDungeon(DungeonId));
	return true;
}

/**
 * THE CONTROLS: a Quest dungeon another Cataclysm sent, an ordinary dungeon Demonic sent, and a floor with no empire
 * dungeon behind it hold no altar, seal nothing for one, record no Rift boss and list no Rift on the panel. Each is
 * Elite, as a Rift is, so each has a boss at every exit. THE POSITIVE CONTROL: the same dungeon, made a Rift, holds one
 * on the same floor, so the test can see an altar where there is one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftAltarsControlsTest,
	"Cataclysm.RiftAltars.AQuestDungeonAnotherCataclysmSentAndAnOrdinaryDungeonHoldNoAltars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftAltarsControlsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftAltarsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FRiftAltarsPlayer Player(World);
	const FRiftAltarsScene Scene = MakeRiftAltarsScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const FName AltarSeal(ACataclysmDungeonGameMode::RiftAltarsSealKey);
	const FName BossSeal(ACataclysmDungeonGameMode::RiftBossSealKey);

	struct FRiftAltarsControl
	{
		ECataclysmDungeonType Type;
		ECataclysmType Sender;
		const TCHAR* What;
	};
	const FRiftAltarsControl Controls[] = {
		{ECataclysmDungeonType::Quest, ECataclysmType::Death, TEXT("a Quest dungeon Death sent")},
		{ECataclysmDungeonType::Quest, ECataclysmType::None, TEXT("a Quest dungeon nobody sent")},
		{ECataclysmDungeonType::Basic, ECataclysmType::Demonic, TEXT("an ordinary dungeon Demonic sent")},
	};
	// ENTERING ONE LEAVES THE WALK BEFORE IT: `EnterEmpireDungeon` does that itself.
	for (const FRiftAltarsControl& Control : Controls)
	{
		if (EnterARiftAltarsDungeon(*this, Mode, Scene.Run, 2, Control.Type, Control.Sender,
									ECataclysmDungeonSubType::Elite) == INDEX_NONE)
		{
			return false;
		}
		for (int32 Floor = 1; Floor <= 2; ++Floor)
		{
			if (Floor > 1 && !TestTrue(FString::Printf(TEXT("set-up: %s's floor %d was reached"), Control.What, Floor),
									   Mode->GoToFloor(Floor)))
			{
				return false;
			}
			TestFalse(FString::Printf(TEXT("CONTROL: %s is no Rift"), Control.What), Mode->IsARift());
			TestEqual(FString::Printf(TEXT("CONTROL: %s's floor %d holds no altar"), Control.What, Floor),
					  Mode->RiftAltarsPlacedHere(), 0);
			TestTrue(FString::Printf(TEXT("CONTROL: and none stands on %s's floor %d"), Control.What, Floor),
					 Mode->RiftAltarsStandingNow().IsEmpty());
			TestFalse(FString::Printf(TEXT("CONTROL: no altar seals %s's floor %d"), Control.What, Floor),
					  Mode->StairsSealedBy().Contains(AltarSeal));
			TestFalse(FString::Printf(TEXT("CONTROL: nor a Rift's boss on %s's floor %d"), Control.What, Floor),
					  Mode->StairsSealedBy().Contains(BossSeal));
			TestNull(FString::Printf(TEXT("CONTROL: no Rift boss is recorded on %s's floor %d"), Control.What, Floor),
					 Mode->RiftBossNow());
			TestTrue(FString::Printf(TEXT("CONTROL: and %s's panel lists no Rift on floor %d"), Control.What, Floor),
					 RiftPanelLine(Mode).IsEmpty());
		}
		TestTrue(FString::Printf(TEXT("CONTROL: %s's last floor still has its boss"), Control.What),
				 RiftAltarsFloorGatekeeper(Mode) != nullptr);
	}

	// THE POSITIVE CONTROL: THE SAME DUNGEON, MADE A RIFT, FOUR FLOORS DEEP AS THE OWNER MADE ONE.
	if (EnterARift(*this, Mode, Scene.Run, 4) == INDEX_NONE)
	{
		return false;
	}
	TestEqual(TEXT("POSITIVE CONTROL: the same dungeon made a Rift holds an altar on its floor 1"),
			  Mode->RiftAltarsStandingNow().Num(), 1);
	TestFalse(TEXT("POSITIVE CONTROL: and its panel lists the Rift"), RiftPanelLine(Mode).IsEmpty());

	// AND NO EMPIRE DUNGEON BEHIND THE FLOOR: the sandbox's, which is no Rift. Leaving takes the altar away.
	Mode->LeaveEmpireDungeon();
	TestTrue(TEXT("leaving the Rift takes its altars away"), Mode->RiftAltarsStandingNow().IsEmpty());
	if (!TestTrue(TEXT("set-up: a floor with no empire dungeon behind it was built"), Mode->GoToFloor(1)))
	{
		return false;
	}
	TestFalse(TEXT("CONTROL: a floor with no empire dungeon behind it is no Rift"), Mode->IsARift());
	TestEqual(TEXT("CONTROL: and holds no altar"), Mode->RiftAltarsPlacedHere(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// A load
// ---------------------------------------------------------------------------

/**
 * A load finds the Rift's boss again, and does not take a risen one or a rule's creature for it.
 *
 * A LOAD REBUILDS THE FLOOR AND THEN REPLACES ITS CREATURES WITH THE SAVED ONES: the rebuilt boss is destroyed, and
 * `RestoreCreatureRoles` is handed each saved creature with its record. This test does those two steps by hand, as
 * `Cataclysm.BossFloor.ALoadFindsTheSavedCataclysmBossAndNotOneThatRoseFromTheDead` does. On the Rift's last floor the
 * saved Gatekeeper that neither rose from the dead nor was raised by a rule is its boss again, and seals the way out;
 * CONTROLS: one that rose, and one a rule raised, are not; and on floor 1 of the same Rift, which is not its last, no
 * Gatekeeper is.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftAltarsBossLoadTest,
	"Cataclysm.RiftAltars.ALoadFindsTheRiftsBossAgainAndNotOneThatRoseOrARulesCreature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftAltarsBossLoadTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftAltarsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FRiftAltarsPlayer Player(World);
	const FRiftAltarsScene Scene = MakeRiftAltarsScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable())
		|| EnterARift(*this, Mode, Scene.Run, 4) == INDEX_NONE)
	{
		return false;
	}
	const FName BossSeal(ACataclysmDungeonGameMode::RiftBossSealKey);

	const auto SpawnAGatekeeper = [World](const FVector& Where) -> ACataclysmEnemyCharacter*
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		return World->SpawnActor<ACataclysmGatekeeperCharacter>(ACataclysmGatekeeperCharacter::StaticClass(), Where,
															   FRotator::ZeroRotator, Spawn);
	};

	// CONTROL: FLOOR 1, WHICH IS NOT THE LAST. A saved Gatekeeper put back there is no Rift boss.
	ACataclysmEnemyCharacter* OnFloorOne = SpawnAGatekeeper(FVector(-1600.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a Gatekeeper was put back on floor 1"), OnFloorOne))
	{
		return false;
	}
	Mode->RestoreCreatureRoles(*OnFloorOne, FCataclysmSavedCreature());
	TestNull(TEXT("CONTROL: on floor 1, not the last, a saved Gatekeeper is no Rift boss"), Mode->RiftBossNow());

	// THE LAST FLOOR, REBUILT, WITH ITS BOSS. The load destroys the rebuilt floor's creatures, raising no death notice.
	if (!TestTrue(TEXT("set-up: floor 4 was reached"), Mode->GoToFloor(4))
		|| !TestTrue(TEXT("set-up: and is the last"), Mode->IsOnTheLastFloor()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Rebuilt = Mode->RiftBossNow();
	if (!TestNotNull(TEXT("set-up: the rebuilt floor's Rift boss stands"), Rebuilt))
	{
		return false;
	}
	Rebuilt->Destroy();
	if (!TestNull(TEXT("set-up: with the rebuilt boss destroyed no Rift boss stands"), Mode->RiftBossNow()))
	{
		return false;
	}

	// CONTROL: ONE THAT ROSE FROM THE DEAD.
	ACataclysmEnemyCharacter* Risen = SpawnAGatekeeper(FVector(-800.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a risen Gatekeeper was put back"), Risen))
	{
		return false;
	}
	FCataclysmSavedCreature RisenRecord;
	RisenRecord.bRisenFromTheDead = true;
	Mode->RestoreCreatureRoles(*Risen, RisenRecord);
	TestNull(TEXT("CONTROL: a Gatekeeper that rose from the dead is not taken for the Rift's boss"), Mode->RiftBossNow());

	// CONTROL: ONE A RULE RAISED, which a save keeps only with a tag naming the rule.
	ACataclysmEnemyCharacter* Raised = SpawnAGatekeeper(FVector(0.0f, 800.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a Gatekeeper a rule raised was put back"), Raised))
	{
		return false;
	}
	FCataclysmSavedCreature RaisedRecord;
	RaisedRecord.RaisedBy = FName(TEXT("Rift_Altars_Test_Rule"));
	Mode->RestoreCreatureRoles(*Raised, RaisedRecord);
	TestNull(TEXT("CONTROL: nor one a rule raised"), Mode->RiftBossNow());
	TestFalse(TEXT("CONTROL: so nothing of the boss's seals the way out"), Mode->StairsSealedBy().Contains(BossSeal));

	// THE SAVED BOSS.
	ACataclysmEnemyCharacter* Saved = SpawnAGatekeeper(FVector(800.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the saved Rift boss was put back"), Saved))
	{
		return false;
	}
	Mode->RestoreCreatureRoles(*Saved, FCataclysmSavedCreature());
	TestTrue(TEXT("the saved Gatekeeper is the Rift's boss again"), Mode->RiftBossNow() == Saved);
	TestTrue(TEXT("and it seals the way out again"), Mode->StairsSealedBy().Contains(BossSeal));
	return true;
}

/**
 * A floor's fallen altars are saved by their place in the order the floor placed them, and a rebuilt floor, which places
 * them again on the same cells in the same order, has those fallen felled again. ON THE FALLBACK DEPTH, a Rift two
 * floors deep, because a place is only told from another where a floor holds more than one altar, which a four-floor
 * Rift's never does (see `Cataclysm.RiftAltars.ARiftOfFewerThanFourFloorsPutsTheRestOfItsAltarsOnItsLastFloor` for when
 * that depth is reached). At its last floor's three altars: the second felled is saved as place 1. The floor is built
 * again (all three standing, where they stood), and the saved state put back leaves the first and the third standing,
 * where they stood, still sealing the stairs. CONTROL: a state naming a place the floor did not fill fells nothing. The
 * real save and load, on a Rift four floors deep, are `Cataclysm.SaveLoad.ARiftsFallenAltarStaysFallenAfterALoad`.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftAltarsSavedByPlaceTest,
	"Cataclysm.RiftAltars.AFloorsFallenAltarsAreSavedByPlaceAndPutBackFallen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftAltarsSavedByPlaceTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftAltarsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FRiftAltarsPlayer Player(World);
	const FRiftAltarsScene Scene = MakeRiftAltarsScene(World);
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("a possessed player"), Player.IsUsable())
		|| EnterARift(*this, Mode, Scene.Run, 2) == INDEX_NONE
		|| !TestTrue(TEXT("set-up: the last floor was reached"), Mode->GoToFloor(2)))
	{
		return false;
	}
	const FName AltarSeal(ACataclysmDungeonGameMode::RiftAltarsSealKey);

	const TArray<ACataclysmEnemyCharacter*> Placed = Mode->RiftAltarsStandingNow();
	if (!TestEqual(TEXT("set-up: the last floor placed three altars"), Placed.Num(), 3))
	{
		return false;
	}
	TArray<FVector> Where;
	for (const ACataclysmEnemyCharacter* Altar : Placed)
	{
		Where.Add(Altar->GetActorLocation());
	}
	TestTrue(TEXT("CONTROL: nothing has fallen, so nothing is saved fallen"),
			 Mode->FloorRuleStateNow().RiftAltarsDestroyed.IsEmpty());

	// THE SECOND FELLED, AND SAVED BY ITS PLACE.
	if (!FellIt(*this, Player, Placed[1], TEXT("the second altar")))
	{
		return false;
	}
	const FCataclysmSavedFloorRuleState State = Mode->FloorRuleStateNow();
	TestEqual(TEXT("the fallen altar is saved by its place, 1"), RiftAltarsJoined(State.RiftAltarsDestroyed),
			  FString(TEXT("1")));

	// THE FLOOR BUILT AGAIN: ALL THREE STAND, WHERE THEY STOOD, IN THE SAME ORDER.
	if (!TestTrue(TEXT("set-up: the floor was built again"), Mode->GoToFloor(2)))
	{
		return false;
	}
	const TArray<ACataclysmEnemyCharacter*> Rebuilt = Mode->RiftAltarsStandingNow();
	if (!TestEqual(TEXT("the rebuilt floor stands all three again"), Rebuilt.Num(), 3))
	{
		return false;
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestTrue(FString::Printf(TEXT("altar %d stands where it stood"), Index),
				 Rebuilt[Index]->GetActorLocation().Equals(Where[Index], 1.0f));
	}

	// CONTROL: A PLACE THE FLOOR DID NOT FILL FELLS NOTHING.
	FCataclysmSavedFloorRuleState Beyond;
	Beyond.RiftAltarsDestroyed = {7};
	Mode->RestoreFloorRuleState(Beyond);
	TestEqual(TEXT("CONTROL: a saved place the floor did not fill fells nothing"), Mode->RiftAltarsStandingNow().Num(), 3);

	// THE SAVED STATE PUT BACK.
	Mode->RestoreFloorRuleState(State);
	const TArray<ACataclysmEnemyCharacter*> After = Mode->RiftAltarsStandingNow();
	if (!TestEqual(TEXT("the saved state fells one of the three again"), After.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("the first still stands where it stood"), After[0]->GetActorLocation().Equals(Where[0], 1.0f));
	TestTrue(TEXT("and the third"), After[1]->GetActorLocation().Equals(Where[2], 1.0f));
	TestEqual(TEXT("two of four have fallen: floor 1's and this one"), Mode->RiftAltarsFallen(), 2);
	TestTrue(TEXT("the two standing still seal the stairs"), Mode->StairsSealedBy().Contains(AltarSeal));
	return true;
}

/**
 * The run record's step to 19 (issue #51 slice B1): `Run_v18.json`, written before a floor's rule state kept a Rift's
 * fallen altars, migrates to none fallen. CONTROL: `Run_v21.json` is read, not migrated, and states places 0 and 2, not
 * the default, so the field is read from the file. Every other field is checked by the tests that run every older run
 * file through the whole chain, each of which adds `CataclysmSaveFixtures::AddTheRunsVersion19Defaults`. Prints "RIFT
 * ALTARS FILES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRiftAltarsMigrationTest,
	"Cataclysm.RiftAltars.ARunFileFromVersion18MigratesWithNoAltarFallen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRiftAltarsMigrationTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmRiftAltarsTest;

	ECataclysmSaveLoadResult OldResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult NewResult = ECataclysmSaveLoadResult::NotValidJson;
	const UCataclysmRunSave* Old = ReadRiftAltarsRunFixture(*this, TEXT("Run_v18.json"), OldResult);
	const UCataclysmRunSave* Committed = ReadRiftAltarsRunFixture(*this, TEXT("Run_v21.json"), NewResult);
	if (!TestNotNull(TEXT("Run_v18.json loads"), Old) || !TestNotNull(TEXT("Run_v21.json loads"), Committed))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("RIFT ALTARS FILES: Run_v18.json %s, fallen [%s]; Run_v21.json %s, fallen [%s]"),
							FCataclysmSaveStorage::Describe(OldResult),
							*RiftAltarsJoined(Old->Floor.RuleState.RiftAltarsDestroyed),
							FCataclysmSaveStorage::Describe(NewResult),
							*RiftAltarsJoined(Committed->Floor.RuleState.RiftAltarsDestroyed)));

	TestEqual(TEXT("Run_v18.json is migrated"), static_cast<int32>(OldResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestTrue(TEXT("**and holds no Rift altar fallen**"), Old->Floor.RuleState.RiftAltarsDestroyed.IsEmpty());
	TestEqual(TEXT("CONTROL: Run_v21.json is read, not migrated"), static_cast<int32>(NewResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("CONTROL: and states places 0 and 2, not the default"),
			  RiftAltarsJoined(Committed->Floor.RuleState.RiftAltarsDestroyed), FString(TEXT("0,2")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
