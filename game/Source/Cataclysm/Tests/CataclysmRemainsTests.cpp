// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmBoneGiants.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmNecromancerArmy.h"
#include "AbilitySystem/CataclysmRemains.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmFloorContents.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Necromancer's remains, its Ossuary limb, the Abomination and the Bone Colossus: the Death finish's slice D
 * (2026-11-26), on the design the coordinating session approved that day.
 *
 * A REAL DEATH CHARACTER HOLDING REAL ROWS wherever a node's rows can be reached: a possessed player character, Death
 * chosen, nothing worn, level 60, its passive allocation set and its equipment refreshed, so `DT_PassiveEffects` as the
 * generator wrote it is what is tested (the shape `Cataclysm.DeathTrees.` uses). EXPECTED FIGURES ARE READ FROM THAT
 * TABLE (`RowValue`), not written here, as the owner asked (issue #2928). Lone Revenant's flag is slice B's and is not
 * on this branch, so its test grants the flag by hand to a bare holder.
 *
 * DEATHS ARE ANNOUNCED through the world's combat events, as a real death is, naming the creature and its killer; the
 * creature is not otherwise hurt. Every creature stands at least 2 m from every other unless a test needs it nearer.
 */
namespace CataclysmRemainsTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float M = 100.0f;

	const FName Root(TEXT("Necromancer_basic_spine_000"));
	const FName BoneFuse(TEXT("Necromancer_basic_os_stem0"));
	const FName Gravefinder(TEXT("Necromancer_basic_os_stem1"));
	const FName RaisedOnBones(TEXT("Necromancer_basic_os_stem2"));
	const FName MantleOfBone(TEXT("Necromancer_basic_os_a0"));
	const FName GatheredRemains(TEXT("Necromancer_basic_os_a1"));
	const FName FallenAgain(TEXT("Necromancer_basic_os_a2"));
	const FName BoneBound(TEXT("Necromancer_basic_os_b0"));
	const FName PickedClean(TEXT("Necromancer_basic_os_b1"));
	const FName TwoFromOne(TEXT("Necromancer_basic_os_b2"));
	const FName Festering(TEXT("Necromancer_basic_os_c0"));
	const FName GraveSpores(TEXT("Necromancer_basic_os_c1"));
	const FName GraveRobber(TEXT("Necromancer_keystone_os_kB"));
	const FName PlaguePit(TEXT("Necromancer_keystone_os_kC"));
	const FName ManyMadeOne(TEXT("Necromancer_basic_lg_d0"));
	const FName FirstRite(TEXT("Necromancer_capstone_25"));
	const FName FinalRite(TEXT("Necromancer_capstone_200"));

	/** One row's `ValuePerPoint` in the shipped effect table, or -1 when the row is missing. */
	float RowValue(const TCHAR* RowName)
	{
		const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
		const FCataclysmPassiveEffectRow* Row =
			Effects ? Effects->FindRow<FCataclysmPassiveEffectRow>(FName(RowName), TEXT("remains test")) : nullptr;
		return Row ? Row->ValuePerPoint : -1.0f;
	}

	/** A Death character standing at the origin, its tables and pools reachable. */
	struct FRemainsNecromancer
	{
		bool Make(FAutomationTestBase& Test, UWorld* InWorld)
		{
			World = InWorld;
			ACataclysmPlayerState* SpawnedState = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!SpawnedState || !Controller || !Character)
			{
				Test.AddError(TEXT("set-up: a possessed player could not be built"));
				return false;
			}
			Controller->SetPlayerState(SpawnedState);
			Controller->Possess(Character);
			State = Character->GetPlayerState<ACataclysmPlayerState>();
			Equipment = Character->GetEquipment();
			System = State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
			Events = UCataclysmCombatEvents::In(World);
			if (!Test.TestNotNull(TEXT("set-up: the player state"), State)
				|| !Test.TestNotNull(TEXT("set-up: equipment"), Equipment)
				|| !Test.TestNotNull(TEXT("set-up: the ability system"), System)
				|| !Test.TestNotNull(TEXT("set-up: the combat events"), Events))
			{
				return false;
			}
			State->SetCreationChoice(FName(TEXT("Staff")), FName(TEXT("Death")));
			// THE STARTING WEAPON TAKEN OFF, for the reason `Cataclysm.DeathTrees.` gives: it was made before Death was
			// chosen. With nothing worn the creation type is what the character carries.
			Equipment->UnequipEverything();
			State->SetLevelAndExperience(60, 0);
			Equipment->RefreshAttributes(System);
			return Test.TestTrue(TEXT("set-up: a Death character reaches the Necromancer tree"),
								 State->ReachableTrees().Contains(TEXT("Necromancer")));
		}

		/** The starting node and these nodes, with an optional capstone option chosen. */
		void Hold(const TMap<FName, int32>& Nodes, FName Capstone = NAME_None, int32 Option = 0)
		{
			FCataclysmPassiveAllocation Allocation;
			Allocation.Add(Root, 1);
			for (const TPair<FName, int32>& Node : Nodes)
			{
				Allocation.Add(Node.Key, Node.Value);
			}
			if (!Capstone.IsNone())
			{
				Allocation.Add(Capstone, 1);
				Allocation.SetChosenOption(Capstone, Option);
			}
			State->SetPassiveAllocation(Allocation, TArray<FName>());
			Equipment->RefreshAttributes(System);
		}

		float Fervour() const
		{
			return System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}

		float Health() const
		{
			return System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		float MaximumHealth() const
		{
			return System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		}

		/** Announce `Victim`'s death, killed by `Killer` (the character when null). */
		void Kill(AActor* Victim, AActor* Killer = nullptr) const
		{
			FCataclysmDeathNotice Notice;
			Notice.Victim = Victim;
			Notice.Killer = Killer ? Killer : Character;
			Notice.KillingCauser = Notice.Killer;
			Notice.Location = Victim->GetActorLocation();
			Events->OnDeath.Broadcast(Notice);
		}

		UWorld* World = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmCombatEvents* Events = nullptr;
	};

	/** A skill of the Heavy slot granted to `Player`'s character with these shape parameters, at level 1. */
	UCataclysmStrikeSkill* GrantRemainsSwing(const FRemainsNecromancer& Player, const TCHAR* Shape)
	{
		const FGameplayAbilitySpecHandle Handle = Player.System->GiveAbilityInSlot(
			UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Heavy, /*Level=*/1, Player.Character);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? Player.System->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmStrikeSkill* Swing = Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Swing)
		{
			Swing->SkillName = TEXT("A remains test swing");
			Swing->Params = UCataclysmSkillShapes::ParseParams(Shape);
		}
		return Swing;
	}

	ACataclysmEnemyCharacter* RemainsCreatureAt(UWorld* World, const FVector& Where)
	{
		return World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
	}

	float RemainsHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Actor);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	float RemainsMaximumHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Actor);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : 0.0f;
	}

	TArray<ACataclysmRemainsMarker*> RemainsInWorld(UWorld* World)
	{
		TArray<ACataclysmRemainsMarker*> Found;
		for (TActorIterator<ACataclysmRemainsMarker> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				Found.Add(*It);
			}
		}
		return Found;
	}

	bool RemainsCarries(const AActor* Actor, const TCHAR* TagName)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Actor);
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), /*ErrorIfNotFound=*/false);
		return Its && Tag.IsValid() && Its->HasMatchingGameplayTag(Tag);
	}

	/** Pins the roll `UCataclysmRemains` makes, and puts it back. */
	struct FRemainsPinnedRoll
	{
		explicit FRemainsPinnedRoll(float Roll) { UCataclysmRemains::PinnedRollForTests = Roll; }
		~FRemainsPinnedRoll() { UCataclysmRemains::PinnedRollForTests = -1.0f; }
	};

	/** Pins the roll a status row makes against its chance (`Cataclysm.StatusRoll`), and puts it back. */
	struct FRemainsPinnedStatusRoll
	{
		explicit FRemainsPinnedStatusRoll(float Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.StatusRoll"));
			if (Variable)
			{
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}
		~FRemainsPinnedStatusRoll()
		{
			if (Variable)
			{
				Variable->Set(-1.0f, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
	};

	/** Counts one action event raised on an ability system while it lives. */
	struct FRemainsEventCount
	{
		FRemainsEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
			: System(InSystem)
			, Event(InEvent)
		{
			Handle = System->OnActionEvent.AddLambda([this](FName Raised) { Count += Raised == Event ? 1 : 0; });
		}
		~FRemainsEventCount() { System->OnActionEvent.Remove(Handle); }
		UCataclysmAbilitySystemComponent* System = nullptr;
		FName Event;
		int32 Count = 0;
		FDelegateHandle Handle;
	};
}

// ==========================================================================
// Bone Fuse and Gathered Remains
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsBoneFuseTest,
	"Cataclysm.Remains.BoneFuseLeavesRemainsOfAnEnemysDeathThatOnlyItsHolderOwnsAndSees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Bone Fuse's real row: an enemy's death leaves remains where it fell, left for the character, owned by it, its disc
 * seen by its owner only, recording the creature's maximum health, rarity and kind, lasting `BaseSeconds`; the HUD
 * names it for its owner. CONTROLS: the same death with the starting node alone leaves none, and a minion of the
 * character's own dying leaves none (it is not an enemy).
 */
bool FCataclysmRemainsBoneFuseTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	ACataclysmEnemyCharacter* First = RemainsCreatureAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Second = RemainsCreatureAt(World, FVector(3 * M, 3 * M, 0));
	ACataclysmMinion* Own = ACataclysmMinion::Spawn(Player.Character, FVector(-3 * M, 0, 0), 60.0f, false, TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: two creatures"), First) || !TestNotNull(TEXT("set-up"), Second)
		|| !TestNotNull(TEXT("set-up: a minion"), Own))
	{
		return false;
	}

	Player.Hold({});
	Player.Kill(First);
	const int32 WithoutBoneFuse = RemainsInWorld(World).Num();

	Player.Hold({{BoneFuse, 1}});
	Player.Kill(Own);
	const int32 AfterOwnMinion = RemainsInWorld(World).Num();
	Player.Kill(Second);
	const TArray<ACataclysmRemainsMarker*> Left = RemainsInWorld(World);

	TestEqual(TEXT("CONTROL: with the starting node alone an enemy's death leaves no remains"), WithoutBoneFuse, 0);
	TestEqual(TEXT("CONTROL: with Bone Fuse a minion of the character's own leaves none here"), AfterOwnMinion, 0);
	if (!TestEqual(TEXT("with Bone Fuse an enemy's death leaves one remains"), Left.Num(), 1))
	{
		return false;
	}
	const ACataclysmRemainsMarker* Remains = Left[0];
	TestEqual(TEXT("left for the character"), Remains->LeftFor.Get(), static_cast<AActor*>(Player.Character));
	TestEqual(TEXT("owned by it"), Remains->GetOwner(), static_cast<AActor*>(Player.Character));
	TestTrue(TEXT("its disc is seen by its owner only"), Remains->Disc && Remains->Disc->bOnlyOwnerSee);
	TestTrue(TEXT("where the creature fell"),
			 FVector::Dist2D(Remains->GetActorLocation(), Second->GetActorLocation()) < 1.0f);
	TestEqual(TEXT("recording its maximum health"), Remains->MaximumHealth, RemainsMaximumHealthOf(Second), 0.01f);
	TestEqual(TEXT("its rarity"), Remains->RarityStep, Second->RarityStep);
	TestEqual(TEXT("and its kind"), Remains->Kind, Second->ArchetypeRow);
	TestEqual(TEXT("lasting Bone Fuse's 10 seconds"), Remains->GetLifeSpan(), ACataclysmRemainsMarker::BaseSeconds, 0.01f);

	TArray<ACataclysmRemainsMarker*> Named;
	UCataclysmRemains::RemainsToName(Player.Character, FVector::ZeroVector, 20 * M, Named);
	TestEqual(TEXT("the HUD names it for its owner"), Named.Num(), 1);
	UCataclysmRemains::RemainsToName(Second, FVector::ZeroVector, 20 * M, Named);
	TestEqual(TEXT("and for nobody else"), Named.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsGatheredTest,
	"Cataclysm.Remains.GatheredRemainsLengthensTheirLifeByItsRowsAndTheyAreGoneWhenItEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Gathered Remains' real row at 8 points: remains last `BaseSeconds` times one and the row's 25% a point, 30 seconds.
 * They are there just before that and gone just after. CONTROL: with Bone Fuse alone, 10.
 */
bool FCataclysmRemainsGatheredTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	ACataclysmEnemyCharacter* First = RemainsCreatureAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Second = RemainsCreatureAt(World, FVector(3 * M, 3 * M, 0));
	const float PerPoint = RowValue(TEXT("Necromancer_basic_os_a1#1"));
	if (!TestNotNull(TEXT("set-up: creatures"), First) || !TestNotNull(TEXT("set-up"), Second)
		|| !TestTrue(TEXT("set-up: Gathered Remains' row"), PerPoint > 0.0f))
	{
		return false;
	}

	Player.Hold({{BoneFuse, 1}});
	Player.Kill(First);
	TArray<ACataclysmRemainsMarker*> Left = RemainsInWorld(World);
	const float Plain = Left.Num() == 1 ? Left[0]->GetLifeSpan() : -1.0f;
	for (ACataclysmRemainsMarker* One : Left)
	{
		One->Destroy();
	}

	Player.Hold({{BoneFuse, 1}, {GatheredRemains, 8}});
	Player.Kill(Second);
	Left = RemainsInWorld(World);
	if (!TestEqual(TEXT("one remains"), Left.Num(), 1))
	{
		return false;
	}
	const float Expected = ACataclysmRemainsMarker::BaseSeconds * (1.0f + PerPoint * 8.0f / 100.0f);
	TestEqual(TEXT("CONTROL: with Bone Fuse alone they last 10 seconds"), Plain, ACataclysmRemainsMarker::BaseSeconds, 0.01f);
	TestEqual(TEXT("with 8 points of Gathered Remains, three times as long (30)"), Left[0]->GetLifeSpan(), Expected, 0.01f);
	TestEqual(TEXT("which is the sentence's 30 seconds"), Expected, 30.0f, 0.01f);

	CataclysmTestWorld::RunClock(World, Expected - 0.5f);
	TestEqual(TEXT("still there half a second before it ends"), RemainsInWorld(World).Num(), 1);
	CataclysmTestWorld::RunClock(World, 1.0f);
	TestEqual(TEXT("and gone half a second after"), RemainsInWorld(World).Num(), 0);
	return true;
}

// ==========================================================================
// Raising on remains
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsRaiseTest,
	"Cataclysm.Remains.AMinionRaisedOnOwnRemainsWithinGravefindersReachGainsRaisedOnBonesAndFervour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Gravefinder at 2 points chooses the nearest of the character's own remains within 2 m of where a minion rises, and
 * no other's; a minion raised on them carries `Minion.RaisedOnRemains`, its health raised by Raised on Bones' real
 * rows at 6 points; the remains are consumed; Bone Fuse's and Grave Robber's real rows pay their Fervour.
 * CONTROLS: remains 2.5 m off and another player's remains 0.5 m off are not chosen; before the raise the minion has
 * no tag.
 */
bool FCataclysmRemainsRaiseTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	Player.Hold({{BoneFuse, 1}, {Gravefinder, 2}, {RaisedOnBones, 6}, {GraveRobber, 1}});
	const float FuseFervour = RowValue(TEXT("Necromancer_basic_os_stem0#2"));
	const float RobberFervour = RowValue(TEXT("Necromancer_keystone_os_kB#1"));
	const float BonesPercent = RowValue(TEXT("Necromancer_basic_os_stem2#2"));
	ACataclysmEnemyCharacter* Dead = RemainsCreatureAt(World, FVector(8 * M, 8 * M, 0));
	if (!TestNotNull(TEXT("set-up: a creature"), Dead) || !TestTrue(TEXT("set-up: the rows"), FuseFervour > 0.0f
			&& RobberFervour > 0.0f && BonesPercent > 0.0f))
	{
		return false;
	}

	const FVector Rise(0, 5 * M, 0);
	ACataclysmRemainsMarker* Near = UCataclysmRemains::LeaveFor(Player.Character, Dead, Rise + FVector(1.5f * M, 0, 0));
	ACataclysmRemainsMarker* Far = UCataclysmRemains::LeaveFor(Player.Character, Dead, Rise + FVector(-2.5f * M, 0, 0));
	ACataclysmRemainsMarker* Foreign = UCataclysmRemains::LeaveFor(Player.Character, Dead, Rise + FVector(0, 0.5f * M, 0));
	if (!TestNotNull(TEXT("set-up: near"), Near) || !TestNotNull(TEXT("set-up: far"), Far)
		|| !TestNotNull(TEXT("set-up: foreign"), Foreign))
	{
		return false;
	}
	Foreign->LeftFor = Dead;

	TestEqual(TEXT("Gravefinder chooses the own remains 1.5 m off, not the other's at 0.5 or its own at 2.5"),
			  UCataclysmRemains::ChooseToRaiseOn(Player.Character, Rise), Near);

	ACataclysmMinion* Raised = ACataclysmMinion::Spawn(Player.Character, Near->GetActorLocation(), 60.0f, false,
													   TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: an Imp"), Raised))
	{
		return false;
	}
	const FGameplayTagContainer Before = Raised->TypeTags;
	const float HealthBefore = RemainsMaximumHealthOf(Raised);
	const float Without = UCataclysmCommand::SummonerMultiplierFor(Player.Character, TEXT("minion_health"), Before);
	const float FervourBefore = Player.Fervour();

	const bool bConsumed = UCataclysmRemains::RaiseOn(Player.Character, Raised, Near);

	TestFalse(TEXT("CONTROL: before the raise the Imp has no Raised on Remains tag"),
			  Before.HasTagExact(UCataclysmRemains::RaisedOnRemainsTag()));
	TestTrue(TEXT("raised on them, it carries the tag"),
			 Raised->TypeTags.HasTagExact(UCataclysmRemains::RaisedOnRemainsTag()));
	TestTrue(TEXT("and the remains are consumed"), bConsumed && !IsValid(Near));
	TestTrue(TEXT("CONTROL: the remains not chosen are still there"), IsValid(Far) && IsValid(Foreign));
	TestEqual(TEXT("its health raised by Raised on Bones' 6 points of 5% increased"),
			  RemainsMaximumHealthOf(Raised), HealthBefore * (Without + BonesPercent * 6.0f / 100.0f) / Without, 0.5f);
	TestEqual(TEXT("and Bone Fuse's and Grave Robber's Fervour paid, 3 and 5"), Player.Fervour() - FervourBefore,
			  FuseFervour + RobberFervour, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsTwoFromOneTest,
	"Cataclysm.Remains.TwoFromOneKeepsTheRemainsOnItsRollAndBoneBoundLowersTheReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two From One's real row at 8 points (32%): a roll below the chance keeps the remains; at the chance they are
 * consumed. Bone-Bound's real row at 8 points: a reserve of 10 raised on remains is 10 less 32%. CONTROL: without
 * Two From One a winning roll still consumes them, and without Bone-Bound the reserve is unchanged.
 */
bool FCataclysmRemainsTwoFromOneTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Dead = RemainsCreatureAt(World, FVector(8 * M, 8 * M, 0));
	const float Chance = RowValue(TEXT("Necromancer_basic_os_b2#1")) * 8.0f;
	const float Less = RowValue(TEXT("Necromancer_basic_os_b0#1")) * 8.0f;
	if (!TestNotNull(TEXT("set-up: a creature"), Dead) || !TestTrue(TEXT("set-up: the rows"), Chance > 0 && Less > 0))
	{
		return false;
	}
	const auto RaiseOne = [&Player, Dead]()
	{
		ACataclysmRemainsMarker* Remains = UCataclysmRemains::LeaveFor(Player.Character, Dead, FVector(0, 3 * M, 0), true);
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Player.Character, FVector(0, 3 * M, 0), 60.0f, false,
														TEXT("Imp"));
		return UCataclysmRemains::RaiseOn(Player.Character, Imp, Remains);
	};

	Player.Hold({});
	bool bControlConsumed = false;
	{
		FRemainsPinnedRoll Pinned(0.0f);
		bControlConsumed = RaiseOne();
	}
	const float PlainReserve = UCataclysmRemains::ReserveOnRemains(Player.Character, 10.0f);

	Player.Hold({{TwoFromOne, 8}, {BoneBound, 8}});
	bool bKept = false;
	bool bAtTheChance = false;
	{
		FRemainsPinnedRoll Pinned(Chance - 1.0f);
		bKept = !RaiseOne();
	}
	{
		FRemainsPinnedRoll Pinned(Chance);
		bAtTheChance = RaiseOne();
	}

	TestTrue(TEXT("CONTROL: without Two From One a raise consumes its remains"), bControlConsumed);
	TestTrue(TEXT("with 8 points a roll below 32 keeps them"), bKept);
	TestTrue(TEXT("and a roll of 32 consumes them"), bAtTheChance);
	TestEqual(TEXT("CONTROL: without Bone-Bound a reserve of 10 stays 10"), PlainReserve, 10.0f, 0.001f);
	TestEqual(TEXT("with 8 points of Bone-Bound it is 32% less"),
			  UCataclysmRemains::ReserveOnRemains(Player.Character, 10.0f), 10.0f * (1.0f - Less / 100.0f), 0.001f);
	return true;
}

// ==========================================================================
// Detonating remains
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsMantleTest,
	"Cataclysm.Remains.MantleOfBoneDetonatesOwnRemainsInsideASkillsAreaForAShareOfTheirCreaturesHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Mantle of Bone's and Festering's real rows at 12 and 8 points: a detonation deals 6% of the creature's maximum health,
 * 16% more for each ailment it died with; only the character's own remains inside the area go off; an enemy within 3 m
 * of them is struck and one 4 m off is not; the remains are consumed and Picked Clean's event is raised. CONTROLS: the
 * same area with the starting node alone detonates nothing; another player's remains inside it stay.
 */
bool FCataclysmRemainsMantleTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Dead = RemainsCreatureAt(World, FVector(-8 * M, 8 * M, 0));
	ACataclysmEnemyCharacter* Within = RemainsCreatureAt(World, FVector(10 * M, 2.5f * M, 0));
	ACataclysmEnemyCharacter* Beyond = RemainsCreatureAt(World, FVector(10 * M, -4 * M, 0));
	const float PerStep = RowValue(TEXT("Necromancer_basic_os_a0#2"));
	const float PerAilment = RowValue(TEXT("Necromancer_basic_os_c0#1")) * 8.0f;
	if (!TestNotNull(TEXT("set-up: creatures"), Dead) || !TestNotNull(TEXT("set-up"), Within)
		|| !TestNotNull(TEXT("set-up"), Beyond) || !TestTrue(TEXT("set-up: the rows"), PerStep > 0 && PerAilment > 0))
	{
		return false;
	}
	const auto Everything = [](const AActor&) { return true; };

	ACataclysmRemainsMarker* Own = UCataclysmRemains::LeaveFor(Player.Character, Dead, FVector(10 * M, 0, 0), true);
	ACataclysmRemainsMarker* Foreign = UCataclysmRemains::LeaveFor(Player.Character, Dead, FVector(12 * M, 0, 0), true);
	if (!TestNotNull(TEXT("set-up: own"), Own) || !TestNotNull(TEXT("set-up: foreign"), Foreign))
	{
		return false;
	}
	Foreign->LeftFor = Dead;
	Own->Ailments.AddDefaulted(2);

	Player.Hold({});
	const int32 Plain = UCataclysmRemains::DetonateWhere(Player.Character, Everything);

	Player.Hold({{MantleOfBone, 12}, {Festering, 8}});
	const float Expected = Own->MaximumHealth * PerStep * 6.0f / 100.0f * (1.0f + PerAilment * 2.0f / 100.0f);
	const float Stated = UCataclysmRemains::DetonationDamage(Player.Character, Own);
	const float WithinBefore = RemainsHealthOf(Within);
	const float BeyondBefore = RemainsHealthOf(Beyond);
	FRemainsEventCount Consumed(Player.System, TEXT("remains_consumed"));
	const int32 Detonated = UCataclysmRemains::DetonateWhere(Player.Character, Everything);

	TestEqual(TEXT("CONTROL: without Mantle of Bone nothing is detonated"), Plain, 0);
	TestEqual(TEXT("a detonation deals 6% of the creature's maximum health, 16% more for each of its two ailments"),
			  Stated, Expected, 0.01f);
	TestEqual(TEXT("with Mantle of Bone the character's own remains go off, and only they"), Detonated, 1);
	TestFalse(TEXT("they are consumed"), IsValid(Own));
	TestTrue(TEXT("CONTROL: another player's remains inside the area stay"), IsValid(Foreign));
	TestEqual(TEXT("and Picked Clean's event is raised once"), Consumed.Count, 1);
	TestTrue(TEXT("an enemy 2.5 m from them is struck"), RemainsHealthOf(Within) < WithinBefore);
	TestEqual(TEXT("CONTROL: one 4 m from them is not"), RemainsHealthOf(Beyond), BeyondBefore, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsArmourTest,
	"Cataclysm.Remains.MantleOfBonesArmourCountsOwnRemainsWithinSixMetresUpToTen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Mantle of Bone's armour rows at 12 points, on the scale `remains_in_reach`: 1% increased armour for every 2 points
 * for each of the character's own remains within 6 m, up to 10. Three within 6 m add 18%; eleven add 60%, the cap.
 * CONTROLS: none near adds nothing; remains 7 m off and another player's within 6 m add nothing.
 */
bool FCataclysmRemainsArmourTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Dead = RemainsCreatureAt(World, FVector(-20 * M, 20 * M, 0));
	const float PerStep = RowValue(TEXT("Necromancer_basic_os_a0#8"));
	if (!TestNotNull(TEXT("set-up: a creature"), Dead) || !TestTrue(TEXT("set-up: the armour row"), PerStep > 0.0f))
	{
		return false;
	}
	Player.Hold({{MantleOfBone, 12}});
	const auto Armour = [&Player]()
	{
		return Player.System->IncreasesForStat(FName(TEXT("armor")), FGameplayTagContainer());
	};

	const float None = Armour();
	UCataclysmRemains::LeaveFor(Player.Character, Dead, FVector(7 * M, 0, 0), true);
	ACataclysmRemainsMarker* Foreign = UCataclysmRemains::LeaveFor(Player.Character, Dead, FVector(0, 1 * M, 0), true);
	if (!TestNotNull(TEXT("set-up: foreign"), Foreign))
	{
		return false;
	}
	Foreign->LeftFor = Dead;
	const float NoneNear = Armour();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UCataclysmRemains::LeaveFor(Player.Character, Dead, FVector(2 * M, Index * M, 0), true);
	}
	const float Three = Armour();
	for (int32 Index = 0; Index < 8; ++Index)
	{
		UCataclysmRemains::LeaveFor(Player.Character, Dead, FVector(-2 * M, Index * 0.5f * M, 0), true);
	}
	const float Eleven = Armour();

	TestEqual(TEXT("CONTROL: remains 7 m off and another player's add nothing"), NoneNear, None, 0.0001f);
	TestEqual(TEXT("three own remains within 6 m add 6 steps of 1% each, 18%"), Three - None,
			  PerStep * 6.0f * 3.0f / 100.0f, 0.0001f);
	TestEqual(TEXT("eleven add only ten's worth, 60%"), Eleven - None, PerStep * 6.0f * 10.0f / 100.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsPlaguePitTest,
	"Cataclysm.Remains.PlaguePitPassesTheAilmentsTheirCreatureDiedWithAndGraveSporesInflictsNecrosis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Remains record a Necrosis the creature died carrying, with its seconds left. Plague Pit's real row passes it to an
 * enemy the detonation strikes; Grave Spores' real row at 6 points, its status roll pinned at nought, inflicts
 * Necrosis on each enemy struck. CONTROLS: with Mantle of Bone alone the enemy struck carries no Necrosis; Grave Spores
 * with its roll pinned at 99 inflicts none.
 */
bool FCataclysmRemainsPlaguePitTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	const FCataclysmAilmentKind* Necrosis = UCataclysmAilments::KindNamed(TEXT("Necrosis"));
	ACataclysmEnemyCharacter* Dying = RemainsCreatureAt(World, FVector(-8 * M, 8 * M, 0));
	if (!TestNotNull(TEXT("set-up: Necrosis"), Necrosis) || !TestNotNull(TEXT("set-up: a creature"), Dying))
	{
		return false;
	}
	UCataclysmAilments::Apply(Player.Character, Dying, *Necrosis, UCataclysmAilments::NormalMagnitude);
	const auto LeaveAt = [&Player, Dying](const FVector& Where)
	{
		return UCataclysmRemains::LeaveFor(Player.Character, Dying, Where, true);
	};
	ACataclysmRemainsMarker* Recorded = LeaveAt(FVector(0, 30 * M, 0));
	if (!TestNotNull(TEXT("set-up: remains"), Recorded))
	{
		return false;
	}
	const FCataclysmRemainsAilment* Held = Recorded->Ailments.FindByPredicate(
		[](const FCataclysmRemainsAilment& One) { return One.Ailment == TEXT("Necrosis"); });
	if (!TestNotNull(TEXT("the remains record the Necrosis the creature died with"), Held))
	{
		return false;
	}
	TestTrue(TEXT("with seconds left and what it dealt a second"), Held->SecondsLeft > 0.0f && Held->DamagePerSecond > 0.0f);

	const auto StruckCarriesNecrosis = [&](const FVector& Where)
	{
		ACataclysmEnemyCharacter* Struck = RemainsCreatureAt(World, Where + FVector(1.5f * M, 0, 0));
		ACataclysmRemainsMarker* Remains = LeaveAt(Where);
		UCataclysmRemains::Detonate(Player.Character, Remains);
		return RemainsCarries(Struck, TEXT("Keyword.DoT.Necrosis"));
	};

	Player.Hold({{MantleOfBone, 2}});
	const bool bPlain = StruckCarriesNecrosis(FVector(10 * M, 0, 0));
	Player.Hold({{MantleOfBone, 2}, {PlaguePit, 1}});
	const bool bPassed = StruckCarriesNecrosis(FVector(10 * M, 10 * M, 0));

	// GRAVE SPORES ON REMAINS WHOSE CREATURE DIED CARRYING NOTHING.
	ACataclysmEnemyCharacter* Clean = RemainsCreatureAt(World, FVector(-8 * M, -8 * M, 0));
	const auto SporesStrike = [&](const FVector& Where, float Roll)
	{
		FRemainsPinnedStatusRoll Pinned(Roll);
		ACataclysmEnemyCharacter* Struck = RemainsCreatureAt(World, Where + FVector(1.5f * M, 0, 0));
		ACataclysmRemainsMarker* Remains = UCataclysmRemains::LeaveFor(Player.Character, Clean, Where, true);
		UCataclysmRemains::Detonate(Player.Character, Remains);
		return RemainsCarries(Struck, TEXT("Keyword.DoT.Necrosis"));
	};
	Player.Hold({{MantleOfBone, 2}, {GraveSpores, 6}});
	const bool bSporesMiss = SporesStrike(FVector(-10 * M, 0, 0), 99.0f);
	const bool bSporesHit = SporesStrike(FVector(-10 * M, 10 * M, 0), 0.0f);

	TestFalse(TEXT("CONTROL: with Mantle of Bone alone the enemy struck carries no Necrosis"), bPlain);
	TestTrue(TEXT("with Plague Pit it carries the Necrosis the creature died with"), bPassed);
	TestFalse(TEXT("CONTROL: Grave Spores' roll pinned at 99 inflicts none"), bSporesMiss);
	TestTrue(TEXT("and pinned at nought it inflicts Necrosis"), bSporesHit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsPickedCleanTest,
	"Cataclysm.Remains.PickedCleanRestoresHealthForEveryThreePointsWhenRemainsAreConsumed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Picked Clean's real rows at 6 points: consuming remains restores 1% of maximum health for every 3 points, 2%.
 * CONTROL: at 2 points it restores nothing.
 */
bool FCataclysmRemainsPickedCleanTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Dead = RemainsCreatureAt(World, FVector(8 * M, 8 * M, 0));
	const float PerStep = RowValue(TEXT("Necromancer_basic_os_b1#1"));
	if (!TestNotNull(TEXT("set-up: a creature"), Dead) || !TestTrue(TEXT("set-up: the row"), PerStep > 0.0f))
	{
		return false;
	}
	const auto ConsumeOne = [&Player, Dead]()
	{
		Player.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(),
											   Player.MaximumHealth() * 0.5f);
		const float Before = Player.Health();
		UCataclysmRemains::Consume(Player.Character,
								   UCataclysmRemains::LeaveFor(Player.Character, Dead, FVector(0, 3 * M, 0), true));
		return Player.Health() - Before;
	};

	Player.Hold({{PickedClean, 2}});
	const float AtTwo = ConsumeOne();
	Player.Hold({{PickedClean, 6}});
	const float AtSix = ConsumeOne();

	TestEqual(TEXT("CONTROL: at 2 points consuming remains restores nothing"), AtTwo, 0.0f, 0.01f);
	TestEqual(TEXT("at 6 points it restores 2% of maximum health"), AtSix,
			  Player.MaximumHealth() * PerStep * 2.0f / 100.0f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsFallenAgainTest,
	"Cataclysm.Remains.FallenAgainLeavesRemainsForAMinionKilledOnItsRollAndNoneForAMachine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Fallen Again's real row at 8 points (48%), through the commander's death hook: an Imp killed with the roll below the
 * chance leaves remains for its commander; with the roll at the chance it leaves none; a Bolt Turret killed with the
 * roll at nought leaves none, since a machine is placed and not raised.
 */
bool FCataclysmRemainsFallenAgainTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	Player.Hold({{FallenAgain, 8}});
	const float Chance = RowValue(TEXT("Necromancer_basic_os_a2#1")) * 8.0f;
	ACataclysmMinion* Lucky = ACataclysmMinion::Spawn(Player.Character, FVector(3 * M, 0, 0), 60.0f, false, TEXT("Imp"));
	ACataclysmMinion* Unlucky =
		ACataclysmMinion::Spawn(Player.Character, FVector(0, 3 * M, 0), 60.0f, false, TEXT("Imp"));
	ACataclysmMinion* Machine =
		ACataclysmMinion::Spawn(Player.Character, FVector(-3 * M, 0, 0), 60.0f, false, TEXT("BoltTurret"));
	if (!TestNotNull(TEXT("set-up"), Lucky) || !TestNotNull(TEXT("set-up"), Unlucky)
		|| !TestNotNull(TEXT("set-up"), Machine) || !TestTrue(TEXT("set-up: the row"), Chance > 0.0f))
	{
		return false;
	}
	const auto KillIt = [](ACataclysmMinion* Minion)
	{
		if (UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Minion))
		{
			Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
		}
	};
	int32 AfterUnlucky = 0;
	int32 AfterMachine = 0;
	{
		FRemainsPinnedRoll Pinned(Chance);
		KillIt(Unlucky);
		AfterUnlucky = RemainsInWorld(World).Num();
	}
	{
		FRemainsPinnedRoll Pinned(0.0f);
		KillIt(Machine);
		AfterMachine = RemainsInWorld(World).Num();
		KillIt(Lucky);
	}
	const TArray<ACataclysmRemainsMarker*> Left = RemainsInWorld(World);

	TestEqual(TEXT("CONTROL: a roll at the 48% chance leaves no remains"), AfterUnlucky, 0);
	TestEqual(TEXT("CONTROL: a machine killed leaves none"), AfterMachine, 0);
	if (TestEqual(TEXT("an Imp killed with the roll below the chance leaves one"), Left.Num(), 1))
	{
		TestEqual(TEXT("for its commander"), Left[0]->LeftFor.Get(), static_cast<AActor*>(Player.Character));
		TestEqual(TEXT("recording the Imp"), Left[0]->Kind, FName(TEXT("Imp")));
	}
	return true;
}

// ==========================================================================
// The Abomination
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsAbominationTest,
	"Cataclysm.Remains.TheAbominationRisesAtHalfTheFloorsStartingCreaturesFromAShareOfEveryKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Final Rite's real option 1. Four creatures the floor started with and one a rule sent. Killing the rule's creature
 * and one starting creature raises nothing; the second starting creature, half of four, raises the Abomination where it
 * fell, releasing the Imp holding 10 of the 100 Fervour, reserving 100, its health 10% of the three killed creatures'
 * maximum health (with the character's minion health rows) and its blow 5% of their blows. A kill after it rises grows
 * it by the same share. CONTROLS: before the count nothing stands; the rule's creature's kill did not count toward it.
 */
bool FCataclysmRemainsAbominationTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	Player.Hold({}, FinalRite, 1);
	if (!TestTrue(TEXT("set-up: the pool holds 100"), Player.System->MaximumClassResource() >= 100.0f))
	{
		return false;
	}
	TArray<ACataclysmEnemyCharacter*> Started;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		ACataclysmEnemyCharacter* Creature = RemainsCreatureAt(World, FVector(6 * M, (Index - 2) * 3 * M, 0));
		if (!TestNotNull(TEXT("set-up: a creature"), Creature))
		{
			return false;
		}
		UCataclysmBoneGiants::NoteStartedWith(Creature);
		Started.Add(Creature);
	}
	ACataclysmEnemyCharacter* Sent = RemainsCreatureAt(World, FVector(-6 * M, 0, 0));
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Player.Character, FVector(0, -6 * M, 0), 60.0f, false, TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: a rule's creature"), Sent) || !TestNotNull(TEXT("set-up: an Imp"), Imp))
	{
		return false;
	}
	Sent->bRaisedByARule = true;
	UCataclysmCommand::HoldReserve(Player.Character, Imp, 10.0f);

	float SummedHealth = 0.0f;
	float SummedBlows = 0.0f;
	const auto KillAndSum = [&](ACataclysmEnemyCharacter* Victim)
	{
		SummedHealth += RemainsMaximumHealthOf(Victim);
		SummedBlows += Victim->OwnAttackDamage();
		Player.Kill(Victim);
	};

	KillAndSum(Sent);
	KillAndSum(Started[0]);
	const bool bBefore = UCataclysmBoneGiants::AbominationOf(Player.Character) != nullptr;
	const int32 CountedBefore = UCataclysmBoneGiants::KilledOfStartBy(Player.Character);
	KillAndSum(Started[1]);
	ACataclysmMinion* Abomination = UCataclysmBoneGiants::AbominationOf(Player.Character);

	TestFalse(TEXT("CONTROL: one starting creature and a rule's killed raise nothing"), bBefore);
	TestEqual(TEXT("CONTROL: the rule's creature did not count"), CountedBefore, 1);
	TestEqual(TEXT("half of four is two"), UCataclysmBoneGiants::KillsNeededFor(Player.Character), 2);
	if (!TestNotNull(TEXT("the second starting creature killed raises the Abomination"), Abomination))
	{
		return false;
	}
	TestTrue(TEXT("where it fell"), FVector::Dist2D(Abomination->GetActorLocation(), Started[1]->GetActorLocation()) < 1.0f);
	TestFalse(TEXT("the Imp holding 10 Fervour was released to make room"), IsValid(Imp));
	TestEqual(TEXT("it reserves 100"), Abomination->ReservedFervour, UCataclysmBoneGiants::AbominationReserve, 0.01f);
	const float Rows = UCataclysmCommand::SummonerMultiplierFor(Player.Character, TEXT("minion_health"),
																Abomination->TypeTags);
	TestEqual(TEXT("its health 10% of the three killed creatures' maximum health"), RemainsMaximumHealthOf(Abomination),
			  SummedHealth * UCataclysmBoneGiants::AbominationHealthSharePercent / 100.0f * Rows, 0.5f);
	TestEqual(TEXT("its blow 5% of their blows"), Abomination->OwnDamagePerHit,
			  SummedBlows * UCataclysmBoneGiants::AbominationDamageSharePercent / 100.0f, 0.01f);

	const float HealthRisen = RemainsMaximumHealthOf(Abomination);
	const float Third = RemainsMaximumHealthOf(Started[2]);
	Player.Kill(Started[2]);
	TestEqual(TEXT("a kill after it rises grows it by 10% of that creature's health"),
			  RemainsMaximumHealthOf(Abomination) - HealthRisen,
			  Third * UCataclysmBoneGiants::AbominationHealthSharePercent / 100.0f * Rows, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsManyMadeOneTest,
	"Cataclysm.Remains.ManyMadeOneTakesTwoOffTheAbominationsShareForEachPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Many Made One's real row at 8 points: of ten starting creatures, 34% rounded up, 4, must be killed. CONTROL: without
 * it, 50%, 5.
 */
bool FCataclysmRemainsManyMadeOneTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	for (int32 Index = 0; Index < 10; ++Index)
	{
		if (ACataclysmEnemyCharacter* Creature = RemainsCreatureAt(World, FVector(6 * M, (Index - 5) * 3 * M, 0)))
		{
			UCataclysmBoneGiants::NoteStartedWith(Creature);
		}
	}
	const float Less = RowValue(TEXT("Necromancer_basic_lg_d0#1")) * 8.0f;
	Player.Hold({}, FinalRite, 1);
	const int32 Plain = UCataclysmBoneGiants::KillsNeededFor(Player.Character);
	Player.Hold({{ManyMadeOne, 8}}, FinalRite, 1);
	const int32 With = UCataclysmBoneGiants::KillsNeededFor(Player.Character);

	TestEqual(TEXT("set-up: ten starting creatures"), UCataclysmBoneGiants::StartedWithCount(World), 10);
	TestEqual(TEXT("CONTROL: without Many Made One, half of ten"), Plain, 5);
	TestEqual(TEXT("with 8 points, 34% of ten rounded up"), With,
			  FMath::CeilToInt(10.0f * (UCataclysmBoneGiants::AbominationKillPercent - Less) / 100.0f));
	TestEqual(TEXT("which is 4"), With, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsLoneRevenantTest,
	"Cataclysm.Remains.LoneRevenantsFlagGivesTheAbominationFourHundredPercentMoreAndNoMoreReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Lone Revenant's flag (slice B's `lone_revenant_held`, granted by hand, alone, without the node's other rows) gives the
 * Abomination 400% more health and damage, and it still reserves 100. Two bare holders, each with a pool of 200, each
 * killing the same kind of creature, one with the flag. CONTROL: the one without it.
 */
bool FCataclysmRemainsLoneRevenantTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const auto MakeHolder = [World](const FVector& Where, bool bRevenant)
	{
		AActor* Holder = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
		UCataclysmAbilitySystemComponent* System = NewObject<UCataclysmAbilitySystemComponent>(Holder);
		System->RegisterComponent();
		UCataclysmVitalAttributeSet* Vitals = NewObject<UCataclysmVitalAttributeSet>(Holder);
		UCataclysmCombatAttributeSet* Combat = NewObject<UCataclysmCombatAttributeSet>(Holder);
		UCataclysmClassResourceAttributeSet* Resource = NewObject<UCataclysmClassResourceAttributeSet>(Holder);
		System->AddAttributeSetSubobject(Vitals);
		System->AddAttributeSetSubobject(Combat);
		System->AddAttributeSetSubobject(Resource);
		System->InitAbilityActorInfo(Holder, Holder);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 200.0f);
		TMap<FName, FCataclysmStatInputs> Inputs;
		const auto Flag = [&Inputs](const TCHAR* Stat)
		{
			FCataclysmStatModifier One;
			One.Bucket = ECataclysmStatBucket::Flat;
			One.Source = ECataclysmModifierSource::PassiveKeystone;
			One.Value = 1.0f;
			Inputs.FindOrAdd(FName(Stat)).Modifiers = {One};
		};
		Flag(UCataclysmBoneGiants::AbominationRisesStat);
		if (bRevenant)
		{
			Flag(UCataclysmNecromancerArmy::LoneRevenantHeldStat);
		}
		System->SetStatInputs(MoveTemp(Inputs));
		return Holder;
	};
	AActor* Plain = MakeHolder(FVector(0, -20 * M, 0), false);
	AActor* Revenant = MakeHolder(FVector(0, 20 * M, 0), true);
	ACataclysmEnemyCharacter* ForPlain = RemainsCreatureAt(World, FVector(5 * M, -20 * M, 0));
	ACataclysmEnemyCharacter* ForRevenant = RemainsCreatureAt(World, FVector(5 * M, 20 * M, 0));
	if (!TestNotNull(TEXT("set-up"), ForPlain) || !TestNotNull(TEXT("set-up"), ForRevenant))
	{
		return false;
	}
	UCataclysmBoneGiants::NoteStartedWith(ForPlain);
	UCataclysmBoneGiants::NoteStartedWith(ForRevenant);
	const auto KillFor = [](AActor* Holder, AActor* Victim)
	{
		FCataclysmDeathNotice Notice;
		Notice.Victim = Victim;
		Notice.Killer = Holder;
		Notice.Location = Victim->GetActorLocation();
		UCataclysmBoneGiants::NoteDeath(Holder, Notice);
	};
	KillFor(Plain, ForPlain);
	KillFor(Revenant, ForRevenant);
	ACataclysmMinion* PlainOne = UCataclysmBoneGiants::AbominationOf(Plain);
	ACataclysmMinion* RevenantOne = UCataclysmBoneGiants::AbominationOf(Revenant);
	if (!TestNotNull(TEXT("CONTROL: the holder without the flag raises one"), PlainOne)
		|| !TestNotNull(TEXT("the holder with it raises one"), RevenantOne))
	{
		return false;
	}
	// EACH AGAINST ITS OWN VICTIM, so a difference between the two creatures cannot read as the flag's.
	const float More = 1.0f + UCataclysmBoneGiants::LoneRevenantMorePercent / 100.0f;
	const float Health = UCataclysmBoneGiants::AbominationHealthSharePercent / 100.0f;
	const float Blow = UCataclysmBoneGiants::AbominationDamageSharePercent / 100.0f;
	TestEqual(TEXT("CONTROL: without the flag, 10% of its creature's health"), RemainsMaximumHealthOf(PlainOne),
			  RemainsMaximumHealthOf(ForPlain) * Health, 0.5f);
	TestEqual(TEXT("CONTROL: and 5% of its blow"), PlainOne->OwnDamagePerHit, ForPlain->OwnAttackDamage() * Blow, 0.01f);
	TestEqual(TEXT("with it, 400% more health"), RemainsMaximumHealthOf(RevenantOne),
			  RemainsMaximumHealthOf(ForRevenant) * Health * More, 0.5f);
	TestEqual(TEXT("and 400% more damage"), RevenantOne->OwnDamagePerHit, ForRevenant->OwnAttackDamage() * Blow * More,
			  0.01f);
	TestEqual(TEXT("and it still reserves 100"), RevenantOne->ReservedFervour, PlainOne->ReservedFervour, 0.01f);
	TestEqual(TEXT("which is 100"), RevenantOne->ReservedFervour, UCataclysmBoneGiants::AbominationReserve, 0.01f);
	return true;
}

// ==========================================================================
// The Bone Colossus, and the floor's end
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsColossusTest,
	"Cataclysm.Remains.TheBoneColossusRisesOnTheFirstRemainsIsFedWithinSixMetresAndShrinksWhenHurt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The First Rite's real option 2. The first enemy's death leaves remains the Colossus rises on (consuming them, holding
 * 20 Fervour). A death 4 m from it feeds it: its maximum health grows by 10% of that creature's. A death 8 m from it
 * does not, and those remains stay. Brought to half the health it rose with, its blow halves and it is drawn smaller.
 * CONTROL: before the first death no Colossus stands.
 */
bool FCataclysmRemainsColossusTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	Player.Hold({}, FirstRite, 2);
	ACataclysmEnemyCharacter* First = RemainsCreatureAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Fed = RemainsCreatureAt(World, FVector(3 * M, 4 * M, 0));
	ACataclysmEnemyCharacter* TooFar = RemainsCreatureAt(World, FVector(3 * M, -8 * M, 0));
	if (!TestNotNull(TEXT("set-up"), First) || !TestNotNull(TEXT("set-up"), Fed) || !TestNotNull(TEXT("set-up"), TooFar))
	{
		return false;
	}

	const bool bBefore = UCataclysmBoneGiants::ColossusOf(Player.Character) != nullptr;
	Player.Kill(First);
	ACataclysmMinion* Colossus = UCataclysmBoneGiants::ColossusOf(Player.Character);
	TestFalse(TEXT("CONTROL: before the first death no Colossus stands"), bBefore);
	if (!TestNotNull(TEXT("the first remains raise the Colossus"), Colossus))
	{
		return false;
	}
	TestEqual(TEXT("consuming them"), RemainsInWorld(World).Num(), 0);
	TestEqual(TEXT("holding 20 Fervour"), Colossus->ReservedFervour, UCataclysmBoneGiants::ColossusReserve, 0.01f);

	const float Risen = RemainsMaximumHealthOf(Colossus);
	const float StartBlow = Colossus->OwnDamagePerHit;
	const float FedHealth = RemainsMaximumHealthOf(Fed);
	Player.Kill(Fed);
	const float AfterFed = RemainsMaximumHealthOf(Colossus);
	Player.Kill(TooFar);

	TestEqual(TEXT("a death 4 m from it feeds it 10% of that creature's maximum health"), AfterFed - Risen,
			  FedHealth * UCataclysmBoneGiants::ColossusFedHealthSharePercent / 100.0f, 0.5f);
	TestEqual(TEXT("CONTROL: a death 8 m from it does not, and its remains stay"), RemainsInWorld(World).Num(), 1);
	TestEqual(TEXT("and it was not fed"), RemainsMaximumHealthOf(Colossus), AfterFed, 0.01f);

	UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Colossus);
	Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Risen * 0.5f);
	UCataclysmBoneGiants::StepColossusOf(Player.Character);
	TestEqual(TEXT("at half the health it rose with its blow is half"), Colossus->OwnDamagePerHit, StartBlow * 0.5f, 0.01f);
	TestTrue(TEXT("and it is drawn smaller"), Colossus->GetActorScale3D().X < 0.99f);

	// AND LEAVING THE FLOOR ENDS IT, as a floor change clears the floor (followers carried), with the remains left.
	UCataclysmFloorContents::ClearTheFloor(*World, /*bCarryFollowers=*/true);
	TestFalse(TEXT("leaving the floor ends the Colossus"), IsValid(Colossus));
	TestEqual(TEXT("and the remains"), RemainsInWorld(World).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsFloorEndTest,
	"Cataclysm.Remains.LeavingTheFloorEndsTheRemainsTheGiantsAndTheTally",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmFloorContents::ClearTheFloor`, as a floor change calls it (followers carried): every remains and the
 * Abomination are destroyed and the tally of starting creatures is forgotten (the Bone Colossus's end is in its own
 * test, since the Necromancer's pool of 100 cannot hold both giants). CONTROL: an Imp of the character's, a follower,
 * is carried and stands.
 */
bool FCataclysmRemainsFloorEndTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FRemainsNecromancer Player;
	if (!Player.Make(*this, World))
	{
		return false;
	}
	Player.Hold({{BoneFuse, 1}}, FinalRite, 1);
	ACataclysmEnemyCharacter* First = RemainsCreatureAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Third = RemainsCreatureAt(World, FVector(3 * M, -10 * M, 0));
	if (!TestNotNull(TEXT("set-up"), First) || !TestNotNull(TEXT("set-up"), Third))
	{
		return false;
	}
	UCataclysmBoneGiants::NoteStartedWith(Third);
	Player.Kill(First);
	ACataclysmMinion* Abomination = UCataclysmBoneGiants::RaiseAbomination(Player.Character, FVector(-6 * M, 6 * M, 0));
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Player.Character, FVector(-3 * M, 0, 0), 60.0f, false, TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: an Abomination"), Abomination) || !TestNotNull(TEXT("set-up: an Imp"), Imp)
		|| !TestEqual(TEXT("set-up: remains lie"), RemainsInWorld(World).Num(), 1)
		|| !TestEqual(TEXT("set-up: a starting creature noted"), UCataclysmBoneGiants::StartedWithCount(World), 1))
	{
		return false;
	}

	UCataclysmFloorContents::ClearTheFloor(*World, /*bCarryFollowers=*/true);

	TestEqual(TEXT("every remains is gone"), RemainsInWorld(World).Num(), 0);
	TestFalse(TEXT("the Abomination is gone"), IsValid(Abomination));
	TestEqual(TEXT("and the floor's tally is forgotten"), UCataclysmBoneGiants::StartedWithCount(World), 0);
	TestTrue(TEXT("CONTROL: an Imp, a follower, is carried"), IsValid(Imp));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsMantleFromAnyWeaponSkillTest,
	"Cataclysm.Remains.MantleOfBoneDetonatesRemainsFromAWeaponSkillWithNoDetonatesKeyAndBurstingRanksHearsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The bundle's assembly (2026-11-26): slice C removed the `Detonates` gate from every weapon skill's landing, so Mantle
 * of Bone ("Your weapon skills that land on remains detonate them") reaches remains from a skill whose row states no
 * `Detonates=1`; and slice C's `NoteDetonation` is wired into a remains detonation, for Bursting Ranks. A 4 m, 90 degree
 * swing stating neither key, with the character's own remains 2 m ahead and its own Imp beside them: with Mantle of Bone
 * held, the remains are consumed, the caster's `minion_detonated` event is raised once, and the Imp STANDS (no
 * `Detonates=1`, no Lit Fuse). THE CONTROL: the starting node alone, the same swing, and the remains stay.
 */
bool FCataclysmRemainsMantleFromAnyWeaponSkillTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;

	bool bConsumed[2] = {false, false};
	int32 Heard[2] = {-1, -1};
	bool bImpStands = false;
	for (int32 WithMantle = 0; WithMantle < 2; ++WithMantle)
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FRemainsNecromancer Player;
		if (!Player.Make(*this, World))
		{
			return false;
		}
		ACataclysmEnemyCharacter* Dead = RemainsCreatureAt(World, FVector(-8 * M, 8 * M, 0));
		ACataclysmRemainsMarker* Own =
			Dead ? UCataclysmRemains::LeaveFor(Player.Character, Dead, FVector(2 * M, 0, 0), true) : nullptr;
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Player.Character, FVector(2 * M, 0.5f * M, 0), /*Lifetime=*/20.0f,
														/*bBurns=*/false, TEXT("Imp"));
		if (!TestNotNull(TEXT("set-up: the remains"), Own) || !TestNotNull(TEXT("set-up: the Imp"), Imp))
		{
			return false;
		}
		Imp->RecordExplosionRadius(3 * M);
		Player.Hold(WithMantle == 1 ? TMap<FName, int32>{{MantleOfBone, 12}} : TMap<FName, int32>());
		UCataclysmStrikeSkill* Swing = GrantRemainsSwing(Player, TEXT("Radius=4; Angle=90"));
		if (!TestNotNull(TEXT("set-up: the swing"), Swing))
		{
			return false;
		}
		FRemainsEventCount Detonations(Player.System, TEXT("minion_detonated"));
		Swing->SwingOnce();
		bConsumed[WithMantle] = !IsValid(Own);
		Heard[WithMantle] = Detonations.Count;
		if (WithMantle == 1)
		{
			bImpStands = IsValid(Imp) && !UCataclysmSkillEffects::IsDead(Imp);
		}
		if (IsValid(Imp))
		{
			Imp->Destroy();
		}
	}

	TestFalse(TEXT("CONTROL: without Mantle of Bone the swing leaves the remains"), bConsumed[0]);
	TestEqual(TEXT("CONTROL: and no detonation is heard"), Heard[0], 0);
	TestTrue(TEXT("WITH MANTLE OF BONE A SWING WITH NO DETONATES KEY DETONATES THE REMAINS"), bConsumed[1]);
	TestEqual(TEXT("AND BURSTING RANKS' EVENT IS RAISED ONCE, for the remains"), Heard[1], 1);
	TestTrue(TEXT("and the caster's Imp in the same arc stands: no Detonates key and no Lit Fuse"), bImpStands);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemainsGiantsAreNotDetonatedTest,
	"Cataclysm.Remains.NeitherGiantIsDetonatedByALitFuseSwingWhileAnOrdinaryMinionBesideItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The coordinating session's ruling of 2026-11-26 at the bundle's assembly: the Abomination and the Bone Colossus are
 * exempt from every detonation of their summoner's own minions (`UCataclysmBoneGiants::IsAGiant`, asked by
 * `UCataclysmDetonation`'s one filter). For each giant a character holding Lit Fuse (every weapon skill detonates its
 * own minions in the area) and the giant's vow swings a 4 m, 90 degree skill over the giant with an ordinary Imp of its
 * beside it: THE IMP EXPLODES (the control that the swing detonates), THE GIANT STANDS.
 */
bool FCataclysmRemainsGiantsAreNotDetonatedTest::RunTest(const FString&)
{
	using namespace CataclysmRemainsTest;
	const FName LitFuse(TEXT("Necromancer_basic_ch_stem0"));

	for (int32 Which = 0; Which < 2; ++Which)
	{
		const TCHAR* Name = Which == 0 ? TEXT("the Abomination") : TEXT("the Bone Colossus");
		UWorld* World = MakeWorldThatHasBegunPlay();
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		FRemainsNecromancer Player;
		if (!Player.Make(*this, World))
		{
			return false;
		}
		ACataclysmMinion* Giant = nullptr;
		if (Which == 0)
		{
			Player.Hold({{LitFuse, 1}}, FinalRite, 1);
			// ONE KILL FIRST, far from the swing: the Abomination's health is a share of the floor's summed kills.
			if (ACataclysmEnemyCharacter* Fed = RemainsCreatureAt(World, FVector(-30 * M, 30 * M, 0)))
			{
				Player.Kill(Fed);
			}
			Giant = UCataclysmBoneGiants::RaiseAbomination(Player.Character, FVector(3 * M, 0, 0));
		}
		else
		{
			Player.Hold({{LitFuse, 1}}, FirstRite, 2);
			ACataclysmEnemyCharacter* First = RemainsCreatureAt(World, FVector(3 * M, 0, 0));
			if (First)
			{
				Player.Kill(First);
			}
			Giant = UCataclysmBoneGiants::ColossusOf(Player.Character);
		}
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Player.Character, FVector(2 * M, 0.5f * M, 0), /*Lifetime=*/20.0f,
														/*bBurns=*/false, TEXT("Imp"));
		if (!TestNotNull(*FString::Printf(TEXT("set-up: %s rises"), Name), Giant)
			|| !TestNotNull(TEXT("set-up: the Imp"), Imp))
		{
			return false;
		}
		Imp->RecordExplosionRadius(3 * M);
		TestTrue(*FString::Printf(TEXT("set-up: %s is known as a giant"), Name), UCataclysmBoneGiants::IsAGiant(Giant));
		TestFalse(TEXT("set-up: the Imp is not"), UCataclysmBoneGiants::IsAGiant(Imp));
		UCataclysmStrikeSkill* Swing = GrantRemainsSwing(Player, TEXT("Radius=4; Angle=90"));
		if (!TestNotNull(TEXT("set-up: the swing"), Swing))
		{
			return false;
		}
		Swing->SwingOnce();
		TestFalse(*FString::Printf(TEXT("with %s: the Imp beside it explodes"), Name), IsValid(Imp));
		TestTrue(*FString::Printf(TEXT("%s STANDS"), Name),
				 IsValid(Giant) && !UCataclysmSkillEffects::IsDead(Giant));
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
