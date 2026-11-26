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
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * BOLTED TOGETHER, THE WAR VOW OPTIONS' LAST SLICE, V5c, 2026-11-25: "A gadget you place within 4 metres of another of
 * yours is bolted onto it: one gadget, with their health added together, that fires all their attacks with 10% more
 * damage for each gadget in it." On the coordinating session's rulings B1 and BT1-BT4. Skill gadgets placed by a
 * converted slot's press and Bolt Turrets placed by a War cast stand in for gadgets; the rules are keyed on what a
 * machine is, so either bolts the same way.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV5cTest
{
	constexpr float WarVowsV5cMetre = 100.0f;

	/** One flat stat row, with the tag it requires or none. */
	struct FWarVowsV5cStat
	{
		const TCHAR* Stat;
		float Value;
		const TCHAR* RequiredTag = nullptr;
	};

	/** A possessed player at the origin with a pool it is told, held full, and 1,000 mana. */
	struct FWarVowsV5cPlayer
	{
		explicit FWarVowsV5cPlayer(UWorld* World)
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
		void Holds(const TArray<FWarVowsV5cStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FWarVowsV5cStat& Each : Rows)
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

	ACataclysmMinion* WarVowsV5cMachineAt(AActor* Player, const FVector& Where, const TCHAR* Type)
	{
		return ACataclysmMinion::Spawn(Player, Where, /*Lifetime=*/600.0f, /*bBurns=*/false, Type);
	}

	ACataclysmEnemyCharacter* WarVowsV5cCreatureAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(1000000.0f);
		}
		return Made;
	}

	float WarVowsV5cHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	const TCHAR* const WarVowsV5cBoltTurret = TEXT("Duration=600; Minions=BoltTurret:1; FervourReserve=5");
	const TCHAR* const WarVowsV5cSpikeTrap = TEXT("Duration=600; Minions=SpikeTrap:1; FervourReserve=5");

	/** Bolted Together's two figures, 4 m and 10%. */
	const FWarVowsV5cStat WarVowsV5cBoltReach{ACataclysmMinion::GadgetBoltReachMetresStat, 4.0f};
	const FWarVowsV5cStat WarVowsV5cBoltedMore{ACataclysmMinion::BoltedMorePercentPerGadgetStat, 10.0f};
}


#define CATACLYSM_WAR_VOWS_V5C_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_WAR_VOWS_V5C_TEST(FCataclysmWarVowsV5cWithinTest,
	"Cataclysm.WarVowsV5c.AGadgetPlacedWithinFourMetresIsBoltedOnAndOneBeyondIsNot")
{
	using namespace CataclysmWarVowsV5cTest;
	// A POOL OF 100 AND THE HEAVY SLOT CONVERTED. A gadget placed at the origin, a second 3 m away: the second is bolted
	// onto the first, which is two gadgets in one and says so on its status line. THE CONTROLS: a third 10 m away stands
	// on its own; and a player without Bolted Together keeps two gadgets placed 3 m apart.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5cPlayer Player(World);
	const FWarVowsV5cPlayer Plain(World);
	if (!TestTrue(TEXT("set-up: two possessed players"), Player.IsUsable() && Plain.IsUsable()))
	{
		return false;
	}
	const ECataclysmAbilitySlot Heavy = ECataclysmAbilitySlot::Heavy;
	const FVector East(WarVowsV5cMetre, 0.0f, 0.0f);
	const FVector North(0.0f, WarVowsV5cMetre, 0.0f);

	Plain.Holds({{UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, 1.0f, TEXT("Slot.Heavy")}});
	Plain.Pool(100.0f);
	Plain.StandsAt(North * 40.0f);
	Plain.Presses(Heavy);
	Plain.StandsAt(North * 40.0f + East * 3.0f);
	Plain.Presses(Heavy);
	TestEqual(TEXT("control: without Bolted Together two gadgets 3 m apart stay two"), Plain.Gadgets().Num(), 2);

	Player.Holds({{UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, 1.0f, TEXT("Slot.Heavy")},
				  WarVowsV5cBoltReach, WarVowsV5cBoltedMore});
	Player.Pool(100.0f);
	Player.Presses(Heavy);
	ACataclysmMinion* First = Player.System->SkillGadgetIn(Heavy);
	Player.StandsAt(East * 3.0f);
	Player.Presses(Heavy);
	const TArray<ACataclysmMinion*> AfterTwo = Player.Gadgets();
	if (!TestNotNull(TEXT("set-up: a first gadget"), First)
		|| !TestEqual(TEXT("a second placed 3 m away is bolted on: one gadget stands"), AfterTwo.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("the first, where it was placed"),
			 AfterTwo[0] == First && FVector::Dist2D(First->GetActorLocation(), FVector::ZeroVector) < 1.0f);
	TestEqual(TEXT("two gadgets in one"), First->GadgetsBoltedIn, 2);
	TestEqual(TEXT("holding the second's attack"), First->BoltedAttacks.Num(), 1);
	TestEqual(TEXT("its status line names it and the count"), UCataclysmCombatOverlay::BoltedTextFor(First),
			  FName::NameToDisplayString(First->TypeName, false) + TEXT(" x2"));

	Player.StandsAt(East * 10.0f);
	Player.Presses(Heavy);
	TestEqual(TEXT("control: a third placed 10 m away stands on its own"), Player.Gadgets().Num(), 2);
	return true;
}

CATACLYSM_WAR_VOWS_V5C_TEST(FCataclysmWarVowsV5cClustersTest,
	"Cataclysm.WarVowsV5c.TwoClustersBuiltApartStayTwoUntilAMoveMakesThemOne")
{
	using namespace CataclysmWarVowsV5cTest;
	// A POOL OF 60, FOUR GADGETS OF 15: two placed at the origin and 2 m east, two more 20 m north and 2 m beyond. Two
	// clusters of two (THE CONTROL). A press with no room left moves every gadget onto the ring around the aim, within
	// 4 m of each other, and they bolt into the first placed: one gadget of four, holding the other three's attacks.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5cPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const ECataclysmAbilitySlot Heavy = ECataclysmAbilitySlot::Heavy;
	const FVector East(WarVowsV5cMetre, 0.0f, 0.0f);
	const FVector North(0.0f, WarVowsV5cMetre, 0.0f);
	Player.Holds({{UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, 1.0f, TEXT("Slot.Heavy")},
				  WarVowsV5cBoltReach, WarVowsV5cBoltedMore});
	Player.Pool(60.0f);
	Player.Presses(Heavy);
	ACataclysmMinion* First = Player.System->SkillGadgetIn(Heavy);
	Player.StandsAt(East * 2.0f);
	Player.Presses(Heavy);
	Player.StandsAt(North * 20.0f);
	Player.Presses(Heavy);
	Player.StandsAt(North * 20.0f + East * 2.0f);
	Player.Presses(Heavy);
	const TArray<ACataclysmMinion*> Two = Player.Gadgets();
	if (!TestNotNull(TEXT("set-up: a first gadget"), First)
		|| !TestEqual(TEXT("control: two clusters built apart stay two"), Two.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("control: each of two"), Two[0]->GadgetsBoltedIn == 2 && Two[1]->GadgetsBoltedIn == 2);
	TestEqual(TEXT("control: holding all four reserves"), UCataclysmCommand::ReservedFervourOf(Player.Character), 60.0f,
			  0.001f);

	const FVector Aim = East * 40.0f;
	Player.Presses(Heavy, &Aim);
	const TArray<ACataclysmMinion*> One = Player.Gadgets();
	if (TestEqual(TEXT("a press with no room moves them and they become one"), One.Num(), 1))
	{
		TestTrue(TEXT("the first placed"), One[0] == First);
		TestEqual(TEXT("four gadgets in it"), First->GadgetsBoltedIn, 4);
		TestEqual(TEXT("holding the other three's attacks"), First->BoltedAttacks.Num(), 3);
		TestEqual(TEXT("and still holding all four reserves"), UCataclysmCommand::ReservedFervourOf(Player.Character),
				  60.0f, 0.001f);
	}
	return true;
}

CATACLYSM_WAR_VOWS_V5C_TEST(FCataclysmWarVowsV5cFiresTest,
	"Cataclysm.WarVowsV5c.ABoltedTurretFiresEveryAttackItHoldsAtTheSameTargetAtTenPercentMoreEach")
{
	using namespace CataclysmWarVowsV5cTest;
	// TWO BOLT TURRET CASTS FROM ONE PLACE: the second is bolted onto the first, which holds its own blow and the
	// second's. Struck at a creature, it deals both blows, each 20% more (two gadgets): 2 x 1.2 = 2.4 blows. THE
	// CONTROL: a turret spawned far away, bolted to nothing, deals one blow at another creature.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5cPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({WarVowsV5cBoltReach, WarVowsV5cBoltedMore});
	Player.Pool(100.0f);
	UCataclysmDeployableSkill* Turret = Player.Grants(ECataclysmAbilitySlot::Special, WarVowsV5cBoltTurret,
													  TEXT("Bolt Turret"));
	ACataclysmMinion* Lone = WarVowsV5cMachineAt(Player.Character, FVector(0.0f, -60.0f * WarVowsV5cMetre, 0.0f),
												 TEXT("BoltTurret"));
	ACataclysmEnemyCharacter* Far = WarVowsV5cCreatureAt(World, FVector(0.0f, 80.0f * WarVowsV5cMetre, 0.0f));
	ACataclysmEnemyCharacter* Near = WarVowsV5cCreatureAt(World, FVector(0.0f, 90.0f * WarVowsV5cMetre, 0.0f));
	if (!TestTrue(TEXT("set-up: a skill, a lone turret and two creatures"), Turret && Lone && Far && Near))
	{
		return false;
	}
	const float LoneBefore = WarVowsV5cHealthOf(Far);
	Lone->AttackTarget(Far);
	const float OneBlow = LoneBefore - WarVowsV5cHealthOf(Far);
	if (!TestTrue(*FString::Printf(TEXT("control: a lone turret's one blow takes something: %.2f"), OneBlow),
				  OneBlow > 0.0f))
	{
		return false;
	}

	Player.Casts(Turret);
	Player.Casts(Turret);
	TArray<ACataclysmMinion*> Bolted = Player.Gadgets();
	Bolted.Remove(Lone);
	if (!TestEqual(TEXT("the second cast is bolted onto the first: one turret stands there"), Bolted.Num(), 1))
	{
		return false;
	}
	ACataclysmMinion* Survivor = Bolted[0];
	TestEqual(TEXT("two gadgets in it"), Survivor->GadgetsBoltedIn, 2);
	TestEqual(TEXT("20% more on each attack"), Survivor->BoltedMoreMultiplier(), 1.2f, 0.0001f);
	const float NearBefore = WarVowsV5cHealthOf(Near);
	Survivor->AttackTarget(Near);
	TestEqual(TEXT("struck once, it deals both blows at the same creature, each 20% more"),
			  NearBefore - WarVowsV5cHealthOf(Near), 2.0f * 1.2f * OneBlow, 0.5f);
	return true;
}

CATACLYSM_WAR_VOWS_V5C_TEST(FCataclysmWarVowsV5cHealthTest,
	"Cataclysm.WarVowsV5c.HealthAddsTheLaterLifeIsKeptAndTheReservesAreReleasedTogether")
{
	using namespace CataclysmWarVowsV5cTest;
	// A BOLT TURRET CAST, THEN A SKILL GADGET PLACED 2 m FROM IT: the turret absorbs the gadget. Its maximum health
	// gains the gadget's, its current health stays full; it holds both reserves (5 and 15); and its life, 600 s, becomes
	// the gadget's, which never ends. Killed, it releases both reserves at once. THE CONTROL: before the bolt, the
	// turret's figures are its own.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5cPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({{UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, 1.0f, TEXT("Slot.Heavy")},
				  WarVowsV5cBoltReach, WarVowsV5cBoltedMore});
	Player.Pool(100.0f);
	UCataclysmDeployableSkill* Turret = Player.Grants(ECataclysmAbilitySlot::Special, WarVowsV5cBoltTurret,
													  TEXT("Bolt Turret"));
	ACataclysmEnemyCharacter* Breaker = WarVowsV5cCreatureAt(World, FVector(0.0f, 50.0f * WarVowsV5cMetre, 0.0f));
	Player.Casts(Turret);
	const TArray<ACataclysmMinion*> Cast = Player.Gadgets();
	if (!TestTrue(TEXT("set-up: one turret and a creature"), Turret && Breaker && Cast.Num() == 1))
	{
		return false;
	}
	ACataclysmMinion* Survivor = Cast[0];
	UAbilitySystemComponent* Its = Survivor->GetAbilitySystemComponent();
	const FGameplayAttribute Maximum = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const float TurretMaximum = Its->GetNumericAttribute(Maximum);
	TestTrue(TEXT("control: the turret's life runs out"), Survivor->GetLifeSpan() > 0.0f);
	TestEqual(TEXT("control: it holds its own 5"), UCataclysmCommand::ReservedFervourOf(Player.Character), 5.0f, 0.001f);

	Player.StandsAt(Survivor->GetActorLocation() + FVector(2.0f * WarVowsV5cMetre, 0.0f, 0.0f));
	Player.Presses(ECataclysmAbilitySlot::Heavy);
	if (!TestEqual(TEXT("the skill gadget placed beside it is bolted on"), Player.Gadgets().Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("its maximum health gained the gadget's"), Its->GetNumericAttribute(Maximum) > TurretMaximum);
	TestEqual(TEXT("and its current health is its new maximum, both having been full"), Its->GetNumericAttribute(Health),
			  Its->GetNumericAttribute(Maximum), 0.5f);
	TestEqual(TEXT("its life is the later of the two: the gadget's, which never ends"), Survivor->GetLifeSpan(), 0.0f);
	TestEqual(TEXT("it holds both reserves"), UCataclysmCommand::ReservedFervourOf(Player.Character), 20.0f, 0.001f);

	UCataclysmSkillEffects::ApplyDirectDamage(Breaker, Survivor, 1.0e8f);
	TestTrue(TEXT("set-up: killed"), UCataclysmSkillEffects::IsDead(Survivor));
	TestEqual(TEXT("both reserves are released at once"), UCataclysmCommand::ReservedFervourOf(Player.Character), 0.0f,
			  0.001f);
	return true;
}

CATACLYSM_WAR_VOWS_V5C_TEST(FCataclysmWarVowsV5cConvertedSlotTest,
	"Cataclysm.WarVowsV5c.AGadgetTheBasicSlotConversionPlacesBoltsLikeAnyOther")
{
	using namespace CataclysmWarVowsV5cTest;
	// THE BASIC ATTACK CONVERTED BY `skill_slot_is_a_gadget` REQUIRING `Slot.Basic`, the row the enchantment "Your basic
	// attack is converted into a deployable gadget ..." writes: its gadget placed 3 m from a Bolt Turret is bolted onto
	// the turret, which then holds the basic slot's cast. THE CONTROL: one placed 10 m away stands alone.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5cPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const ECataclysmAbilitySlot Basic = ECataclysmAbilitySlot::BasicAttack;
	Player.Holds({{UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, 1.0f, TEXT("Slot.Basic")},
				  WarVowsV5cBoltReach, WarVowsV5cBoltedMore});
	Player.Pool(100.0f);
	UCataclysmDeployableSkill* Turret = Player.Grants(ECataclysmAbilitySlot::Special, WarVowsV5cBoltTurret,
													  TEXT("Bolt Turret"));
	Player.Casts(Turret);
	const TArray<ACataclysmMinion*> Cast = Player.Gadgets();
	if (!TestTrue(TEXT("set-up: one turret"), Turret && Cast.Num() == 1))
	{
		return false;
	}
	ACataclysmMinion* Survivor = Cast[0];
	Player.StandsAt(Survivor->GetActorLocation() + FVector(10.0f * WarVowsV5cMetre, 0.0f, 0.0f));
	Player.Presses(Basic);
	TestEqual(TEXT("control: the basic gadget placed 10 m away stands alone"), Player.Gadgets().Num(), 2);
	Player.StandsAt(Survivor->GetActorLocation() + FVector(0.0f, 3.0f * WarVowsV5cMetre, 0.0f));
	Player.Presses(Basic);
	TestEqual(TEXT("one placed 3 m from the turret is bolted on"), Player.Gadgets().Num(), 2);
	const bool bHoldsTheBasicSlot = Survivor->BoltedAttacks.ContainsByPredicate(
		[Basic](const ACataclysmMinion::FBoltedAttack& Each) { return Each.Slot == Basic; });
	TestTrue(TEXT("and the turret holds the basic slot's cast"), bHoldsTheBasicSlot);
	return true;
}

CATACLYSM_WAR_VOWS_V5C_TEST(FCataclysmWarVowsV5cTrapTest,
	"Cataclysm.WarVowsV5c.ATrapIsNeverBolted")
{
	using namespace CataclysmWarVowsV5cTest;
	// A BOLT TURRET CAST, THEN A SPIKE TRAP CAST AT THE SAME PLACE: the trap stands on its own, and the turret is still
	// one gadget. THE CONTROL: a second turret cast at the same place is bolted on.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5cPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({WarVowsV5cBoltReach, WarVowsV5cBoltedMore});
	Player.Pool(100.0f);
	UCataclysmDeployableSkill* Turret = Player.Grants(ECataclysmAbilitySlot::Special, WarVowsV5cBoltTurret,
													  TEXT("Bolt Turret"));
	UCataclysmDeployableSkill* Spike = Player.Grants(ECataclysmAbilitySlot::Heavy, WarVowsV5cSpikeTrap, TEXT("Spikes"));
	if (!TestTrue(TEXT("set-up: two skills"), Turret && Spike))
	{
		return false;
	}
	Player.Casts(Turret);
	Player.Casts(Spike);
	TestEqual(TEXT("the trap stands on its own beside the turret"),
			  ACataclysmMinion::DevicesOf(Player.Character).Num(), 2);
	TArray<ACataclysmMinion*> Gadgets = Player.Gadgets();
	TestTrue(TEXT("and the turret is one gadget"), Gadgets.Num() == 1 && Gadgets[0]->GadgetsBoltedIn == 1);
	Player.Casts(Turret);
	Gadgets = Player.Gadgets();
	TestTrue(TEXT("control: a second turret at the same place is bolted on"),
			 Gadgets.Num() == 1 && Gadgets[0]->GadgetsBoltedIn == 2);
	TestEqual(TEXT("control: and the trap still stands"), ACataclysmMinion::DevicesOf(Player.Character).Num(), 2);
	return true;
}

#undef CATACLYSM_WAR_VOWS_V5C_TEST

#endif // WITH_AUTOMATION_TESTS
