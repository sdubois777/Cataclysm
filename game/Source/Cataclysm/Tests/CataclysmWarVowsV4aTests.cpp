// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTaunt.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"

/**
 * THE WAR VOW OPTIONS' FOURTH SLICE, FIRST HALF, V4a, 2026-11-25, on the coordinating session's rulings on the
 * Saboteur's B options that day: Baited's taunt on `trap_armed`, and Trail of Wire's last trap placed again on the
 * movement skill. Skill traps are placed through `ACataclysmMinion::PlaceSkillTrap` as
 * `CataclysmSaboteurTrapTimingTests.cpp` places them.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV4aTest
{
	constexpr float WarVowsV4aMetre = 100.0f;

	/** A player at the origin that can place skill traps: a pool of 100 holding 100, every slot a trap's. */
	struct FWarVowsV4aTrapper
	{
		explicit FWarVowsV4aTrapper(UWorld* World)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Character)
			{
				return;
			}
			Character->SetPlayerState(State);
			Character->OnRep_PlayerState();
			System = State->GetCataclysmAbilitySystemComponent();
			if (System)
			{
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												100.0f);
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);
				Holds(false);
			}
		}
		bool IsUsable() const { return Character && System; }

		/** The trap flag, and Hair Trigger's instant arming when asked. Replaces the whole stat line. */
		void Holds(bool bArmInstantly) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			const auto Add = [&Stats](const TCHAR* Stat)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = 1.0f;
				FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Stat));
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row);
			};
			Add(TEXT("skill_slot_is_a_trap"));
			if (bArmInstantly)
			{
				Add(ACataclysmMinion::TrapsArmInstantlyStat);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		/** A Heavy trap placed `Ahead` metres east of the player, or null. */
		ACataclysmMinion* Place(float Ahead) const
		{
			return ACataclysmMinion::PlaceSkillTrap(Character, ECataclysmAbilitySlot::Heavy,
													Character->GetActorLocation()
														+ FVector(Ahead * WarVowsV4aMetre, 0.0f, 0.0f));
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A creature on the monsters' side at `Metres` east of the origin. */
	ACataclysmEnemyCharacter* WarVowsV4aCreatureAt(UWorld* World, float Metres)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * WarVowsV4aMetre, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(100000.0f);
		}
		return Made;
	}

	/** Baited's action as the generated row builds it: 4 seconds, within 8 metres, on `trap_armed`. */
	FCataclysmPoolAction WarVowsV4aBaited()
	{
		FCataclysmPoolAction Baited;
		Baited.Event = FName(TEXT("trap_armed"));
		Baited.Percent = 4.0f;
		Baited.EventValue = 8.0f;
		Baited.bTauntNearTrap = true;
		Baited.TriggerKey = FName(TEXT("WarVowsV4a:baited"));
		return Baited;
	}
}

#define CATACLYSM_WAR_VOWS_V4A_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Baited
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V4A_TEST(FCataclysmWarVowsV4aBaitedTest,
	"Cataclysm.WarVowsV4a.ATrapThatArmsDrawsEveryEnemyWithinEightMetresToIt")
{
	using namespace CataclysmWarVowsV4aTest;
	// A PLAYER HOLDING BAITED'S ACTION AND HAIR TRIGGER'S INSTANT ARMING places a trap 2 metres east: it arms as it is
	// placed, raises `trap_armed`, and the creatures 5 and 9 metres east (3 and 7 from the trap) go for it; the one 13
	// metres east, 11 from the trap, does not. THE FIRST CONTROL: a trap placed without instant arming, 30 metres away,
	// has not armed, and the creature beside it is not taunted. Ending the trap's taunts frees the two, as a trap that
	// goes off and rearms ends them.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV4aTrapper Player(World);
	ACataclysmEnemyCharacter* Near = WarVowsV4aCreatureAt(World, 5.0f);
	ACataclysmEnemyCharacter* Edge = WarVowsV4aCreatureAt(World, 9.0f);
	ACataclysmEnemyCharacter* Beyond = WarVowsV4aCreatureAt(World, 13.0f);
	ACataclysmEnemyCharacter* ByTheSlowTrap = WarVowsV4aCreatureAt(World, 32.0f);
	if (!TestTrue(TEXT("set-up: a player who can place traps, and four creatures"),
				  Player.IsUsable() && Near && Edge && Beyond && ByTheSlowTrap))
	{
		return false;
	}
	Player.System->SetPoolActions({WarVowsV4aBaited()});

	const ACataclysmMinion* Slow = Player.Place(30.0f);
	if (!TestNotNull(TEXT("set-up: the control's trap is placed"), Slow))
	{
		return false;
	}
	TestFalse(TEXT("control: a trap with an arming time has not armed"), Slow->bSkillTrapArmed);
	TestNull(TEXT("control: so the creature beside it is not taunted"), UCataclysmTaunt::TaunterOf(ByTheSlowTrap));

	Player.Holds(/*bArmInstantly=*/true);
	Player.System->SetPoolActions({WarVowsV4aBaited()});
	ACataclysmMinion* Bait = Player.Place(2.0f);
	if (!TestNotNull(TEXT("set-up: the instant trap is placed"), Bait)
		|| !TestTrue(TEXT("set-up: and armed as it is placed"), Bait->bSkillTrapArmed))
	{
		return false;
	}
	TestTrue(TEXT("the creature 3 metres from the trap goes for it"), UCataclysmTaunt::TaunterOf(Near) == Bait);
	TestTrue(TEXT("and the one 7 metres from it"), UCataclysmTaunt::TaunterOf(Edge) == Bait);
	TestNull(TEXT("and the one 11 metres from it does not"), UCataclysmTaunt::TaunterOf(Beyond));
	TestEqual(TEXT("for 4 seconds"), UCataclysmTaunt::SecondsLeftOn(Near), 4.0f, 0.05f);

	TestEqual(TEXT("ending the trap's taunts ends both"), UCataclysmTaunt::EndTauntsBy(Bait), 2);
	TestNull(TEXT("and the creature goes back to its own choice"), UCataclysmTaunt::TaunterOf(Near));
	return true;
}

// ---------------------------------------------------------------------------
// Trail of Wire
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V4A_TEST(FCataclysmWarVowsV4aTrailOfWireTest,
	"Cataclysm.WarVowsV4a.TheMovementSkillPlacesTheLastTrapAgainWhereTheMovementBeganArmed")
{
	using namespace CataclysmWarVowsV4aTest;
	// A PLAYER HOLDING TRAIL OF WIRE'S ACTION. THE FIRST CONTROL: before any trap is placed the movement skill places
	// nothing. Then a Heavy trap is placed 4 metres east, and the player stands 10 metres east: the movement skill places
	// a second trap there, on the next tick, from the Heavy slot, armed although it has an arming time. THE SECOND
	// CONTROL: after a death the record is clear and nothing is placed.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV4aTrapper Player(World);
	if (!TestTrue(TEXT("set-up: a player who can place traps"), Player.IsUsable()))
	{
		return false;
	}
	FCataclysmPoolAction Trail;
	Trail.Event = FName(TEXT("movement_skill"));
	Trail.Percent = 100.0f;
	Trail.bPlaceLastTrapAgain = true;
	Trail.TriggerKey = FName(TEXT("WarVowsV4a:trail"));
	Player.System->SetPoolActions({Trail});

	Player.System->NoteMovementSkillUsed();
	World->GetTimerManager().Tick(0.1f);
	TestEqual(TEXT("control: before any trap is placed, the movement skill places none"),
			  ACataclysmMinion::SkillTrapsOf(Player.Character).Num(), 0);

	const ACataclysmMinion* First = Player.Place(4.0f);
	if (!TestNotNull(TEXT("set-up: the first trap is placed"), First))
	{
		return false;
	}
	TestTrue(TEXT("the record holds the Heavy slot"),
			 Player.System->LastSkillTrapSlotPlacedFrom() == ECataclysmAbilitySlot::Heavy);
	const FVector Began(10.0f * WarVowsV4aMetre, 0.0f, 0.0f);
	Player.Character->SetActorLocation(Began);
	Player.System->NoteMovementSkillUsed();
	Player.Character->SetActorLocation(FVector(20.0f * WarVowsV4aMetre, 0.0f, 0.0f));
	TestEqual(TEXT("nothing is placed inside the press"), ACataclysmMinion::SkillTrapsOf(Player.Character).Num(), 1);
	World->GetTimerManager().Tick(0.1f);
	const TArray<ACataclysmMinion*> Traps = ACataclysmMinion::SkillTrapsOf(Player.Character);
	if (TestEqual(TEXT("on the next tick a second trap stands"), Traps.Num(), 2))
	{
		const ACataclysmMinion* Again = Traps[0] == First ? Traps[1] : Traps[0];
		TestTrue(TEXT("where the movement began, not where the player ended"),
				 FVector::Dist2D(Again->GetActorLocation(), Began) < 1.0f);
		TestTrue(TEXT("from the Heavy slot"), Again->SkillTrapSlot == ECataclysmAbilitySlot::Heavy);
		TestTrue(TEXT("armed at once, though its slot has an arming time"), Again->bSkillTrapArmed);
		TestFalse(TEXT("control: the first trap, with the same arming time, is not armed yet"), First->bSkillTrapArmed);
	}

	Player.System->ClearWhatDeathEnds();
	TestTrue(TEXT("control: a death clears the record"),
			 Player.System->LastSkillTrapSlotPlacedFrom() == ECataclysmAbilitySlot::None);
	return true;
}

// ---------------------------------------------------------------------------
// The options, handed to a War player
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V4A_TEST(FCataclysmWarVowsV4aHandedOverTest,
	"Cataclysm.WarVowsV4a.AWarPlayerTakingTheOptionsIsHandedTheirActions")
{
	// A LEVEL 100 WAR PLAYER THROUGH `SetPassiveAllocation`: Baited hands over one taunt (4 seconds, 8 metres, on
	// `trap_armed`), and Trail of Wire one placing (on `movement_skill`). THE CONTROLS: the third options of the same
	// capstones hand over neither.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player"), State && Controller && Character))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Character);
	State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));
	State->SetLevelAndExperience(100, 0);
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestTrue(TEXT("set-up: its ability system and equipment"), System && Equipment))
	{
		return false;
	}
	const auto Take = [&](int32 Option)
	{
		FCataclysmPassiveAllocation Allocation;
		for (const TCHAR* Node : {TEXT("Saboteur_capstone_25"), TEXT("Saboteur_capstone_50")})
		{
			Allocation.Add(FName(Node), 1);
			Allocation.SetChosenOption(FName(Node), Option);
		}
		State->SetPassiveAllocation(Allocation, TArray<FName>());
		Equipment->RefreshAttributes(System);
	};
	const auto Count = [System](bool FCataclysmPoolAction::* Flag)
	{
		int32 Found = 0;
		for (const FCataclysmPoolAction& Action : System->GetPoolActions())
		{
			Found += Action.*Flag ? 1 : 0;
		}
		return Found;
	};

	Take(3);
	TestEqual(TEXT("control: the third options hand over no taunt"), Count(&FCataclysmPoolAction::bTauntNearTrap), 0);
	TestEqual(TEXT("control: and no placing"), Count(&FCataclysmPoolAction::bPlaceLastTrapAgain), 0);

	Take(1);
	for (const FCataclysmPoolAction& Action : System->GetPoolActions())
	{
		if (Action.bTauntNearTrap)
		{
			TestEqual(TEXT("Baited: on trap_armed"), Action.Event, FName(TEXT("trap_armed")));
			TestEqual(TEXT("for 4 seconds"), Action.Percent, 4.0f, 0.001f);
			TestEqual(TEXT("within 8 metres"), Action.EventValue, 8.0f, 0.001f);
		}
		if (Action.bPlaceLastTrapAgain)
		{
			TestEqual(TEXT("Trail of Wire: on movement_skill"), Action.Event, FName(TEXT("movement_skill")));
		}
	}
	TestEqual(TEXT("Baited hands over one taunt"), Count(&FCataclysmPoolAction::bTauntNearTrap), 1);
	TestEqual(TEXT("and Trail of Wire one placing"), Count(&FCataclysmPoolAction::bPlaceLastTrapAgain), 1);
	return true;
}

#undef CATACLYSM_WAR_VOWS_V4A_TEST

#endif // WITH_AUTOMATION_TESTS
