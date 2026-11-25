// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Interface/CataclysmSkillBar.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE GADGET RULE, V5g, 2026-11-25: the owner's second message ("no moving unless you don't have enough fervour to put
 * out another ... then they all move to where you select"), and the coordinating session's rulings G1-G9. A gadget
 * press places another whenever the pool can hold its reserve; only with no room does every gadget the player commands
 * move to the aimed point, in a 1.5 m ring in placing order. Traps never move.
 *
 * GENERIC (G8): the rules are keyed on what a machine is -- `ACataclysmMinion::IsGadget` (deployable and not a trap),
 * `ACataclysmMinion::DevicesOf` (the player commands it) and the reserve (`UCataclysmCommand::HasRoomToReserve`) --
 * and these tests move a skill gadget, a War Bolt Turret and a slotless machine of the `SkillGadget` type, which is
 * neither a War machine nor a skill gadget, together.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV5gTest
{
	constexpr float WarVowsV5gMetre = 100.0f;

	/** One flat stat row, with the tag it requires or none. */
	struct FWarVowsV5gStat
	{
		const TCHAR* Stat;
		float Value;
		const TCHAR* RequiredTag = nullptr;
	};

	/** A possessed player at the origin with a pool it is told, held full, and 1,000 mana. */
	struct FWarVowsV5gPlayer
	{
		explicit FWarVowsV5gPlayer(UWorld* World)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			System = State->GetCataclysmAbilitySystemComponent();
			if (System)
			{
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			}
		}
		bool IsUsable() const { return Character && System; }

		/** REPLACES THE WHOLE STAT LINE with these flat rows. */
		void Holds(const TArray<FWarVowsV5gStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FWarVowsV5gStat& Each : Rows)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = Each.Value;
				if (Each.RequiredTag)
				{
					Row.RequiredTags.AddTag(
						FGameplayTag::RequestGameplayTag(FName(Each.RequiredTag), /*ErrorIfNotFound=*/false));
				}
				Stats.FindOrAdd(FName(Each.Stat)).Modifiers.Add(Row);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		/** A pool of `Maximum`, held full. */
		void Pool(float Maximum) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Maximum);
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Maximum);
		}

		/** One press and release of `Slot`'s key, aimed at `Aim` when given. */
		void Presses(ECataclysmAbilitySlot Slot, const FVector* Aim = nullptr) const
		{
			const FGameplayTag Key = CataclysmAbilitySlots::Tag(Slot);
			System->PressOfASkillGadgetSlot(Key, Aim);
			System->ReleaseOfASkillGadgetSlot(Key);
		}

		/** Move the player, so it does not stand on a gadget placed at its feet. */
		void StandsAt(const FVector& Where) const
		{
			Character->SetActorLocation(Where, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		}

		/** Every gadget the player commands, in placing order. */
		TArray<ACataclysmMinion*> Gadgets() const
		{
			TArray<ACataclysmMinion*> Found = ACataclysmMinion::DevicesOf(Character);
			Found.RemoveAll([](const ACataclysmMinion* Each) { return !Each->IsGadget(); });
			Found.Sort([](const ACataclysmMinion& A, const ACataclysmMinion& B) { return A.SpawnOrder < B.SpawnOrder; });
			return Found;
		}

		/** A War deployable granted in `Slot` with these shape parameters, free of cooldown. */
		UCataclysmDeployableSkill* Grants(ECataclysmAbilitySlot Slot, const TCHAR* ParamText, const TCHAR* Name) const
		{
			const FGameplayAbilitySpecHandle Handle =
				System->GiveAbilityInSlot(UCataclysmDeployableSkill::StaticClass(), Slot, /*Level=*/1, Character);
			FGameplayAbilitySpec* Spec = Handle.IsValid() ? System->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmDeployableSkill* Skill = Spec ? Cast<UCataclysmDeployableSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->SkillName = Name;
				Skill->Params = UCataclysmSkillShapes::ParseParams(ParamText);
				Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Deployable, Type.Minion"));
				Skill->CooldownOverride = 0.0f;
			}
			return Skill;
		}

		/** One use of `Skill`. */
		bool Casts(UCataclysmDeployableSkill* Skill) const
		{
			return Skill && System->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(), false);
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	ACataclysmMinion* WarVowsV5gMachineAt(AActor* Player, const FVector& Where, const TCHAR* Type)
	{
		return ACataclysmMinion::Spawn(Player, Where, /*Lifetime=*/600.0f, /*bBurns=*/false, Type);
	}

	float WarVowsV5gHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	const TCHAR* const WarVowsV5gBoltTurret = TEXT("Duration=600; Minions=BoltTurret:1; FervourReserve=5");
	const TCHAR* const WarVowsV5gFortress = TEXT("Duration=600; Minions=Ballista:2, SpikeTrap:3; FervourReserve=5");
}

#define CATACLYSM_WAR_VOWS_V5G_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_WAR_VOWS_V5G_TEST(FCataclysmWarVowsV5gPressTest,
	"Cataclysm.WarVowsV5g.APressPlacesAnotherGadgetWhileThePoolHoldsItAndOtherwiseMovesEveryGadget")
{
	using namespace CataclysmWarVowsV5gTest;
	// A POOL OF 35 AND A HEAVY SLOT CONVERTED INTO A GADGET, which reserves 15. A Bolt Turret, a slotless machine of the
	// SkillGadget type and a Spike Trap stand already, reserving nothing. The first two presses each place a gadget, the
	// second the newest; the third has no room: nothing is placed, and every gadget -- the turret, the slotless machine and
	// both skill gadgets -- moves to the aimed point in a 1.5 m ring in placing order, keeping its health. THE CONTROL:
	// the Spike Trap stays where it was.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5gPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const ECataclysmAbilitySlot Heavy = ECataclysmAbilitySlot::Heavy;
	Player.Holds({{UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, 1.0f, TEXT("Slot.Heavy")}});
	Player.Pool(35.0f);
	const FVector East(WarVowsV5gMetre, 0.0f, 0.0f);
	const FVector North(0.0f, WarVowsV5gMetre, 0.0f);
	ACataclysmMinion* Turret = WarVowsV5gMachineAt(Player.Character, East * 20.0f, TEXT("BoltTurret"));
	ACataclysmMinion* Slotless =
		WarVowsV5gMachineAt(Player.Character, East * 20.0f + North * 5.0f, ACataclysmMinion::SkillGadgetTypeName);
	ACataclysmMinion* Spike = WarVowsV5gMachineAt(Player.Character, East * -20.0f, TEXT("SpikeTrap"));
	if (!TestTrue(TEXT("set-up: a turret, a slotless machine and a Spike Trap"), Turret && Slotless && Spike)
		|| !TestTrue(TEXT("set-up: the slotless machine is a gadget and no skill gadget"),
					 Slotless->IsGadget() && !Slotless->IsSkillGadget())
		|| !TestTrue(TEXT("set-up: the Heavy slot is converted"), Player.System->SlotIsASkillGadget(Heavy)))
	{
		return false;
	}
	const FVector SpikeStood = Spike->GetActorLocation();

	Player.Presses(Heavy);
	ACataclysmMinion* First = Player.System->SkillGadgetIn(Heavy);
	Player.StandsAt(North * 10.0f);
	Player.Presses(Heavy);
	ACataclysmMinion* Second = Player.System->SkillGadgetIn(Heavy);
	if (!TestTrue(TEXT("two presses place two gadgets"), First && Second && First != Second))
	{
		return false;
	}
	TestEqual(TEXT("the slot answers the newest"), Player.System->SkillGadgetIn(Heavy), Second);
	TestTrue(TEXT("the second where the player stood"), FVector::Dist2D(Second->GetActorLocation(), North * 10.0f) < 1.0f);
	TestEqual(TEXT("each reserving 15"), UCataclysmCommand::ReservedFervourOf(Player.Character), 30.0f, 0.001f);

	UCataclysmSkillEffects::ApplyDirectDamage(Turret, First, 10.0f);
	const float FirstHealth = WarVowsV5gHealthOf(First);
	const FVector Aim = East * 40.0f + North * 40.0f;
	Player.Presses(Heavy, &Aim);
	const TArray<ACataclysmMinion*> Moved = Player.Gadgets();
	TestEqual(TEXT("with no room the third press places nothing: four gadgets still"), Moved.Num(), 4);
	TestEqual(TEXT("and reserves nothing more"), UCataclysmCommand::ReservedFervourOf(Player.Character), 30.0f, 0.001f);
	for (const ACataclysmMinion* Each : Moved)
	{
		TestEqual(*FString::Printf(TEXT("%s is moved onto the 1.5 m ring around the aim"), *Each->GetName()),
				  static_cast<float>(FVector::Dist2D(Each->GetActorLocation(), Aim)), ACataclysmMinion::GadgetMoveRingRadiusCm, 1.0f);
	}
	if (Moved.Num() == 4)
	{
		TestTrue(TEXT("in placing order: the turret, the first placed, due east of the aim"),
				 Moved[0] == Turret && FVector::Dist2D(Turret->GetActorLocation(), Aim + East * 1.5f) < 1.0f);
		TestTrue(TEXT("and the slotless machine a quarter turn on"),
				 Moved[1] == Slotless && FVector::Dist2D(Slotless->GetActorLocation(), Aim + North * 1.5f) < 1.0f);
	}
	TestEqual(TEXT("a moved gadget keeps its health"), WarVowsV5gHealthOf(First), FirstHealth, 0.001f);
	TestTrue(TEXT("control: the Spike Trap, a trap, stays where it was"),
			 FVector::Dist2D(Spike->GetActorLocation(), SpikeStood) < 1.0f);
	return true;
}

CATACLYSM_WAR_VOWS_V5G_TEST(FCataclysmWarVowsV5gWarCastTest,
	"Cataclysm.WarVowsV5g.AWarGadgetCastWithNoRoomMovesEveryGadgetAndTrapsStay")
{
	using namespace CataclysmWarVowsV5gTest;
	// A POOL OF 5: a Bolt Turret cast places a turret, reserving 5 (THE CONTROL). With the player 30 m away, a second cast
	// has no room: nothing is placed and the turret moves to where the player aims, which with no cursor is the player.
	// Iron Fortress, with no room for its set, moves the turret too, and leaves a Spike Trap where it stood (THE CONTROL:
	// traps never move).
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5gPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({});
	Player.Pool(5.0f);
	UCataclysmDeployableSkill* Turret = Player.Grants(ECataclysmAbilitySlot::Special, WarVowsV5gBoltTurret, TEXT("Bolt Turret"));
	UCataclysmDeployableSkill* Fortress =
		Player.Grants(ECataclysmAbilitySlot::Ultimate, WarVowsV5gFortress, TEXT("Iron Fortress"));
	if (!TestTrue(TEXT("set-up: the two skills"), Turret && Fortress)
		|| !TestTrue(TEXT("set-up: each places a gadget"), Turret->PlacesAGadget() && Fortress->PlacesAGadget()))
	{
		return false;
	}

	Player.Casts(Turret);
	TArray<ACataclysmMinion*> Placed = Player.Gadgets();
	if (!TestEqual(TEXT("control: the first cast places a turret"), Placed.Num(), 1))
	{
		return false;
	}
	ACataclysmMinion* Placed0 = Placed[0];
	TestEqual(TEXT("control: and moves nothing"), Turret->GadgetsMovedForWantOfRoom, 0);

	const FVector Away(30.0f * WarVowsV5gMetre, 0.0f, 0.0f);
	Player.StandsAt(Away);
	Player.Casts(Turret);
	TestTrue(TEXT("the second cast has no room"), Turret->bRefusedForRoom);
	TestEqual(TEXT("so it places nothing"), Player.Gadgets().Num(), 1);
	TestEqual(TEXT("and moves the one gadget"), Turret->GadgetsMovedForWantOfRoom, 1);
	TestTrue(TEXT("onto the point aimed at, the player's place with no cursor"),
			 FVector::Dist2D(Placed0->GetActorLocation(), Away) < 1.0f);

	ACataclysmMinion* Spike = WarVowsV5gMachineAt(Player.Character, FVector(-20.0f * WarVowsV5gMetre, 0.0f, 0.0f),
												  TEXT("SpikeTrap"));
	if (!TestNotNull(TEXT("set-up: a Spike Trap"), Spike))
	{
		return false;
	}
	const FVector SpikeStood = Spike->GetActorLocation();
	const FVector Elsewhere(0.0f, 30.0f * WarVowsV5gMetre, 0.0f);
	Player.StandsAt(Elsewhere);
	Player.Casts(Fortress);
	TestEqual(TEXT("Iron Fortress with no room for its set moves the turret"), Fortress->GadgetsMovedForWantOfRoom, 1);
	TestTrue(TEXT("to the player"), FVector::Dist2D(Placed0->GetActorLocation(), Elsewhere) < 1.0f);
	TestTrue(TEXT("control: and the Spike Trap stays"), FVector::Dist2D(Spike->GetActorLocation(), SpikeStood) < 1.0f);
	TestEqual(TEXT("control: nothing of the fortress is placed"), Player.Gadgets().Num(), 1);
	return true;
}

CATACLYSM_WAR_VOWS_V5G_TEST(FCataclysmWarVowsV5gFollowingTest,
	"Cataclysm.WarVowsV5g.AFollowingGadgetMovesWithTheRestAndFollowsFromThere")
{
	using namespace CataclysmWarVowsV5gTest;
	// WALKING ARSENAL HELD (8 m) AND A POOL OF 15 HOLDING ONE HEAVY GADGET: a press with no room moves it to the aim, 30 m
	// from the player, and its next step follows from there, one step's worth toward the player. The coordinating
	// session's condition of 2026-11-25 on Walking Arsenal.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5gPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const ECataclysmAbilitySlot Heavy = ECataclysmAbilitySlot::Heavy;
	Player.Holds({{UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, 1.0f, TEXT("Slot.Heavy")},
				  {ACataclysmMinion::GadgetFollowWithinMetresStat, 8.0f}});
	Player.Pool(15.0f);
	Player.Presses(Heavy);
	ACataclysmMinion* Gadget = Player.System->SkillGadgetIn(Heavy);
	if (!TestNotNull(TEXT("set-up: one gadget placed"), Gadget))
	{
		return false;
	}
	const FVector Aim(30.0f * WarVowsV5gMetre, 0.0f, 0.0f);
	Player.Presses(Heavy, &Aim);
	TestTrue(TEXT("with no room it moves to the aim, 30 m from the player"),
			 FVector::Dist2D(Gadget->GetActorLocation(), Aim) < 1.0f);
	const float Walk = Player.Character->GetCharacterMovement()->MaxWalkSpeed;
	ACataclysmMinion::GadgetStep(Gadget);
	TestEqual(TEXT("and its next step follows from there"), static_cast<float>(FVector::Dist2D(Gadget->GetActorLocation(), FVector::ZeroVector)),
			  30.0f * WarVowsV5gMetre
				  - Walk * UCataclysmCommand::CommandedWalkSpeedShareOfCommander * ACataclysmMinion::DeviceStepSeconds,
			  0.5f);
	return true;
}

CATACLYSM_WAR_VOWS_V5G_TEST(FCataclysmWarVowsV5gWordsTest,
	"Cataclysm.WarVowsV5g.ThePopupSaysTheGadgetRuleForAGadgetAndAWarDeployable")
{
	using namespace CataclysmWarVowsV5gTest;
	// THE WORDS (ruled G6), on the bar read from a player: a Bolt Turret's box names its 5 and the rule; Iron Fortress,
	// gadgets and traps both, names a machine's 5 and the rule. THE CONTROL: a box built by hand with no gadget says no
	// rule.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5gPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({});
	Player.Pool(100.0f);
	if (!TestTrue(TEXT("set-up: the two skills"),
				  Player.Grants(ECataclysmAbilitySlot::Special, WarVowsV5gBoltTurret, TEXT("Bolt Turret"))
				  && Player.Grants(ECataclysmAbilitySlot::Ultimate, WarVowsV5gFortress, TEXT("Iron Fortress"))))
	{
		return false;
	}
	const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Player.Character);
	const FCataclysmSkillBarSlot* TurretBox =
		Bar.FindByPredicate([](const FCataclysmSkillBarSlot& Box) { return Box.Slot == ECataclysmAbilitySlot::Special; });
	const FCataclysmSkillBarSlot* FortressBox =
		Bar.FindByPredicate([](const FCataclysmSkillBarSlot& Box) { return Box.Slot == ECataclysmAbilitySlot::Ultimate; });
	if (!TestTrue(TEXT("set-up: both boxes"), TurretBox && FortressBox))
	{
		return false;
	}
	const FString Rule(UCataclysmSkillBar::GadgetPressRule);
	const FString TurretLine = UCataclysmSkillBar::CostLineFor(*TurretBox);
	TestTrue(*FString::Printf(TEXT("the turret's line names each gadget's 5 and ends with the rule: %s"), *TurretLine),
			 TurretLine.Contains(TEXT("Each gadget reserves 5 Fervour.")) && TurretLine.EndsWith(Rule));
	const FString FortressLine = UCataclysmSkillBar::CostLineFor(*FortressBox);
	TestTrue(*FString::Printf(TEXT("the fortress's names each machine's 5 and the rule: %s"), *FortressLine),
			 FortressLine.Contains(TEXT("Each machine reserves 5 Fervour.")) && FortressLine.EndsWith(Rule));

	FCataclysmSkillBarSlot Plain;
	Plain.bFilled = true;
	Plain.ManaCost = 10.0f;
	TestFalse(TEXT("control: a box with no gadget says no rule"), UCataclysmSkillBar::CostLineFor(Plain).Contains(Rule));
	return true;
}

#undef CATACLYSM_WAR_VOWS_V5G_TEST

#endif // WITH_AUTOMATION_TESTS
