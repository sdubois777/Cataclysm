// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR VOW OPTIONS' SECOND SLICE, V2b, 2026-11-25, on the coordinating session's rulings that day: the Fervour share
 * conditions judged against the maximum the `class_resource` rows make; Warlord, Pure Carnage, Ragnarok's shockwaves,
 * Blood Oath and Spiteful Guard. Blood Oath's and Spiteful Guard's stats are measured by their probes in
 * `CataclysmStatExemptionTests.cpp`; this file holds that a War player is handed them.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV2bTest
{
	/** One capstone option to hand over. */
	struct FWarVowsV2bOption
	{
		const TCHAR* Node;
		int32 Option;
	};

	/** A possessed level 100 War player, its equipment and its ability system. */
	struct FWarVowsV2bPlayer
	{
		explicit FWarVowsV2bPlayer(UWorld* World)
		{
			State = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));
			State->SetLevelAndExperience(100, 0);
			System = State->GetCataclysmAbilitySystemComponent();
			Equipment = Character->GetEquipment();
		}
		bool IsUsable() const { return State && Character && System && Equipment; }

		/** Put a War weapon of this base in this slot through the component's own equip. */
		bool Wear(FAutomationTestBase& Test, const TCHAR* Base, ECataclysmGearSlot Slot) const
		{
			FCataclysmItem Item;
			Item.Base = FName(Base);
			Item.GearLevel = 0;
			Item.DamageTypes = {FName(TEXT("War"))};
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			const ECataclysmEquipResult Result = Equipment->EquipInto(Item, Slot, Removed, AlsoRemoved);
			return Test.TestTrue(*FString::Printf(TEXT("set-up: %s goes on"), Base),
								 Result == ECataclysmEquipResult::Equipped || Result == ECataclysmEquipResult::Swapped);
		}

		/** Hand over one point in each of these capstones with these options, and refresh. */
		void Take(const TArray<FWarVowsV2bOption>& Options) const
		{
			FCataclysmPassiveAllocation Allocation;
			for (const FWarVowsV2bOption& Each : Options)
			{
				Allocation.Add(FName(Each.Node), 1);
				Allocation.SetChosenOption(FName(Each.Node), Each.Option);
			}
			State->SetPassiveAllocation(Allocation, TArray<FName>());
			Equipment->RefreshAttributes(System);
		}

		void SetFervour(float Held) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Held);
		}

		/** The pool actions handed over that act on the characters nearby with this kind. */
		TArray<FCataclysmPoolAction> Nearby(ECataclysmNearbyAction Kind) const
		{
			TArray<FCataclysmPoolAction> Found;
			for (const FCataclysmPoolAction& Action : System->GetPoolActions())
			{
				if (Action.Nearby == Kind)
				{
					Found.Add(Action);
				}
			}
			return Found;
		}

		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};

	/** A bare actor with an ability system and 100,000 health, standing at the origin, acting on the actions it is
	 *  handed. The shape `CataclysmBlockReflectTests.cpp`'s fighter has. */
	struct FWarVowsV2bWearer
	{
		explicit FWarVowsV2bWearer(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			System = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			System->RegisterComponent();
			System->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			System->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			System->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			System->AddAttributeSetSubobject(NewObject<UCataclysmAllResistanceAttributeSet>(Actor));
			System->InitAbilityActorInfo(Actor, Actor);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			SetHealthPercent(100.0f);
		}
		void SetHealthPercent(float Percent) const
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f * Percent);
		}
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A creature on the monsters' side `Metres` from the origin along X, with no defences and 100,000 health. */
	ACataclysmEnemyCharacter* WarVowsV2bCreatureAt(UWorld* World, float Metres)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * 100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(100000.0f);
			Made->SetArmour(0.0f);
			UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent();
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}
		return Made;
	}

	float WarVowsV2bHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	float WarVowsV2bArmourRemovedFrom(const AActor* Actor)
	{
		const UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
		return System ? System->ArmourRemovedPercentNow() : -1.0f;
	}
}

#define CATACLYSM_WAR_VOWS_V2B_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The Fervour share
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V2B_TEST(FCataclysmWarVowsV2bFervourShareTest,
	"Cataclysm.WarVowsV2b.AFervourShareIsJudgedAgainstTheMaximumRagnaroksDoublingMakes")
{
	using namespace CataclysmWarVowsV2bTest;
	// A LEVEL 100 WAR PLAYER WEARING A GREATAXE, holding Unstoppable and Ragnarok through `SetPassiveAllocation`: its
	// maximum Fervour attribute is 100 and Ragnarok's row makes the bar 200, so Unstoppable's "above 75% Fervour" holds
	// at 151 held and not at 76. Its pin flag is the reading. THE CONTROL: the same player holding Eternal Hunger in
	// Ragnarok's place has a bar of 100, and the same row holds at 76 and not at 75.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV2bPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player with equipment"), Player.IsUsable())
		|| !Player.Wear(*this, TEXT("Weapon_Greataxe"), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	const auto Holds = [&Player](float Held)
	{
		Player.SetFervour(Held);
		return Player.System->StatForSkill(FName(UCataclysmSkillEffects::PinSuppressedStat), FGameplayTagContainer(),
										   0.0f) > 0.0f;
	};
	const FGameplayAttribute MaximumAttribute = UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute();

	Player.Take({{TEXT("Berserker_capstone_50"), 1}, {TEXT("Berserker_capstone_200"), 1}});
	if (!TestEqual(TEXT("set-up: the maximum Fervour attribute is 100"),
				   Player.System->GetNumericAttribute(MaximumAttribute), 100.0f, 0.001f)
		|| !TestEqual(TEXT("set-up: Ragnarok's row makes the bar 200"), Player.System->MaximumClassResource(), 200.0f,
					  0.001f))
	{
		return false;
	}
	TestEqual(TEXT("the conditions read a maximum of 200"),
			  Player.System->CurrentConditions().ClassResourceMaximum, 200.0f, 0.001f);
	TestTrue(TEXT("Unstoppable holds at 151 of 200"), Holds(151.0f));
	TestFalse(TEXT("and not at 76 of 200"), Holds(76.0f));

	Player.Take({{TEXT("Berserker_capstone_50"), 1}, {TEXT("Berserker_capstone_200"), 3}});
	TestEqual(TEXT("control: with Eternal Hunger the conditions read a maximum of 100"),
			  Player.System->CurrentConditions().ClassResourceMaximum, 100.0f, 0.001f);
	TestTrue(TEXT("control: and Unstoppable holds at 76 of 100"), Holds(76.0f));
	TestFalse(TEXT("control: and not at 75 of 100"), Holds(75.0f));
	return true;
}

// ---------------------------------------------------------------------------
// The two nearby actions, in the engine
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V2B_TEST(FCataclysmWarVowsV2bNovaByHitTest,
	"Cataclysm.WarVowsV2b.ANovaSizedByAHitStrikesEveryEnemyWithinItsRadiusForItsShare")
{
	using namespace CataclysmWarVowsV2bTest;
	// A WEARER WITH ONE ACTION: 30% of what a critical strike took from health, within 4 metres. A critical strike that
	// took 200 from the creature 2 metres away costs it and the creature 3 metres away 60 each; the creature 7 metres
	// away loses nothing. THE CONTROL: a critical strike that took nothing from health strikes nobody.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV2bWearer Wearer(World);
	ACataclysmEnemyCharacter* Struck = WarVowsV2bCreatureAt(World, 2.0f);
	ACataclysmEnemyCharacter* Beside = WarVowsV2bCreatureAt(World, 3.0f);
	ACataclysmEnemyCharacter* Far = WarVowsV2bCreatureAt(World, 7.0f);
	if (!TestTrue(TEXT("set-up: three creatures"), Struck && Beside && Far))
	{
		return false;
	}
	FCataclysmPoolAction Nova;
	Nova.Event = FName(TEXT("critical_strike"));
	Nova.Percent = 30.0f;
	Nova.EventValue = 4.0f;
	Nova.Nearby = ECataclysmNearbyAction::SmiteByHit;
	Nova.TriggerKey = FName(TEXT("WarVowsV2b:nova"));
	Wearer.System->SetPoolActions({Nova});

	const float StruckBefore = WarVowsV2bHealthOf(Struck);
	const float BesideBefore = WarVowsV2bHealthOf(Beside);
	const float FarBefore = WarVowsV2bHealthOf(Far);
	Wearer.System->ActOnEvent(FName(TEXT("critical_strike")), nullptr, /*EventAmount=*/0.0f, /*bLanded=*/true,
							  Struck);
	TestEqual(TEXT("control: a critical strike that took nothing strikes nobody"),
			  WarVowsV2bHealthOf(Beside), BesideBefore, 0.01f);

	World->TimeSeconds += 1.0f;
	Wearer.System->ActOnEvent(FName(TEXT("critical_strike")), nullptr, /*EventAmount=*/200.0f, /*bLanded=*/true,
							  Struck);
	TestEqual(TEXT("the creature struck loses 30% of the 200"), StruckBefore - WarVowsV2bHealthOf(Struck), 60.0f, 0.5f);
	TestEqual(TEXT("and so does the one 3 metres away"), BesideBefore - WarVowsV2bHealthOf(Beside), 60.0f, 0.5f);
	TestEqual(TEXT("and the one 7 metres away, outside the 4, loses nothing"), WarVowsV2bHealthOf(Far), FarBefore,
			  0.01f);
	return true;
}

CATACLYSM_WAR_VOWS_V2B_TEST(FCataclysmWarVowsV2bArmourNearbyTest,
	"Cataclysm.WarVowsV2b.AnArmourStackGoesOnEveryEnemyNearbyOnlyWhileBothConditionsHold")
{
	using namespace CataclysmWarVowsV2bTest;
	// A WEARER WITH ONE ACTION: 15% of armour for 3 seconds, one stack, on every enemy within the 5 metres of the nearby
	// actions, on a hit dealt at or below 50% health AND at or above 20% -- two conditions a bare actor can meet, in
	// Warlord's shape. THE CONTROLS: at 60% health the first fails and at 10% the second, and nothing is placed; at 40%
	// both hold, and the creatures 2 and 4 metres away lose 15% of their armour while the one 8 metres away loses
	// none. A second hit refreshes the one stack rather than adding another.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV2bWearer Wearer(World);
	ACataclysmEnemyCharacter* Struck = WarVowsV2bCreatureAt(World, 2.0f);
	ACataclysmEnemyCharacter* Beside = WarVowsV2bCreatureAt(World, 4.0f);
	ACataclysmEnemyCharacter* Far = WarVowsV2bCreatureAt(World, 8.0f);
	if (!TestTrue(TEXT("set-up: three creatures"), Struck && Beside && Far))
	{
		return false;
	}
	FCataclysmPoolAction Warlord;
	Warlord.Event = FName(TEXT("hit_dealt"));
	Warlord.Percent = 15.0f;
	Warlord.Nearby = ECataclysmNearbyAction::ArmourRemoved;
	Warlord.PlacedKey = FName(TEXT("WarVowsV2b:enemy_armor_removed_nearby"));
	Warlord.StackSeconds = 3.0f;
	Warlord.StackCap = 1;
	Warlord.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
	Warlord.ConditionValue = 50.0f;
	Warlord.Condition2 = ECataclysmStatCondition::HealthAtOrAbovePercent;
	Warlord.ConditionValue2 = 20.0f;
	Warlord.TriggerKey = FName(TEXT("WarVowsV2b:warlord"));
	Wearer.System->SetPoolActions({Warlord});

	const auto Hit = [&](float HealthPercent)
	{
		Wearer.SetHealthPercent(HealthPercent);
		World->TimeSeconds += 0.5f;
		Wearer.System->ActOnEvent(FName(TEXT("hit_dealt")), nullptr, /*EventAmount=*/50.0f, /*bLanded=*/true, Struck);
	};
	Hit(60.0f);
	TestEqual(TEXT("control: at 60% health the first condition fails and nothing is placed"),
			  WarVowsV2bArmourRemovedFrom(Struck), 0.0f, 0.001f);
	Hit(10.0f);
	TestEqual(TEXT("control: at 10% health the second condition fails and nothing is placed"),
			  WarVowsV2bArmourRemovedFrom(Struck), 0.0f, 0.001f);
	Hit(40.0f);
	TestEqual(TEXT("at 40% health the creature struck loses 15% of its armour"), WarVowsV2bArmourRemovedFrom(Struck),
			  15.0f, 0.001f);
	TestEqual(TEXT("and so does the one 4 metres away"), WarVowsV2bArmourRemovedFrom(Beside), 15.0f, 0.001f);
	TestEqual(TEXT("and the one 8 metres away loses none"), WarVowsV2bArmourRemovedFrom(Far), 0.0f, 0.001f);
	Hit(40.0f);
	TestEqual(TEXT("a second hit refreshes the one stack: still 15%"), WarVowsV2bArmourRemovedFrom(Struck), 15.0f,
			  0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// The options, handed to a War player
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V2B_TEST(FCataclysmWarVowsV2bHandedOverTest,
	"Cataclysm.WarVowsV2b.AWarPlayerTakingTheOptionsIsHandedTheirActionsAndStats")
{
	using namespace CataclysmWarVowsV2bTest;
	// LEVEL 100 WAR PLAYERS THROUGH `SetPassiveAllocation`, the way the game hands a build over. A Berserker holding
	// Warlord, Pure Carnage and Ragnarok is handed one nearby armour removal (15%, 3 seconds, one stack, on a melee hit
	// dealt, above 50% Fervour with two hands) and two novas sized by a hit (30% within 4 metres on a critical strike in
	// Frenzy; 50% within 6 metres on a hit dealt in Wrath). A Bulwark holding Blood Oath and Spiteful Guard is handed 25
	// retaliation penetration and the flag. THE CONTROLS: the third options of the same Berserker capstones, and Shield
	// Oath and Bastion, hand over none of it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FWarVowsV2bPlayer Berserker(World);
	if (!TestTrue(TEXT("set-up: a possessed player with equipment"), Berserker.IsUsable())
		|| !Berserker.Wear(*this, TEXT("Weapon_Greataxe"), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}

	Berserker.Take({{TEXT("Berserker_capstone_25"), 3}, {TEXT("Berserker_capstone_100"), 3},
					{TEXT("Berserker_capstone_200"), 3}});
	TestEqual(TEXT("control: the third options hand over no nearby armour removal"),
			  Berserker.Nearby(ECataclysmNearbyAction::ArmourRemoved).Num(), 0);
	TestEqual(TEXT("control: and no nova sized by a hit"), Berserker.Nearby(ECataclysmNearbyAction::SmiteByHit).Num(),
			  0);

	Berserker.Take({{TEXT("Berserker_capstone_25"), 1}, {TEXT("Berserker_capstone_100"), 2},
					{TEXT("Berserker_capstone_200"), 1}});
	const TArray<FCataclysmPoolAction> Warlord = Berserker.Nearby(ECataclysmNearbyAction::ArmourRemoved);
	if (TestEqual(TEXT("Warlord hands over one nearby armour removal"), Warlord.Num(), 1))
	{
		TestEqual(TEXT("of 15%"), Warlord[0].Percent, 15.0f, 0.001f);
		TestEqual(TEXT("for 3 seconds"), Warlord[0].StackSeconds, 3.0f, 0.001f);
		TestEqual(TEXT("one stack"), Warlord[0].StackCap, 1);
		TestFalse(TEXT("under its own key"), Warlord[0].PlacedKey.IsNone());
		TestEqual(TEXT("on a hit dealt"), Warlord[0].Event, FName(TEXT("hit_dealt")));
		TestTrue(TEXT("by a melee skill"),
				 Warlord[0].RequiredTags.HasTagExact(UCataclysmDamageCalculation::MeleeTag()));
		TestTrue(TEXT("above"), Warlord[0].Condition == ECataclysmStatCondition::ClassResourceAbovePercent);
		TestEqual(TEXT("50% Fervour"), Warlord[0].ConditionValue, 50.0f, 0.001f);
		TestTrue(TEXT("with two hands, the second condition"),
				 Warlord[0].Condition2 == ECataclysmStatCondition::WieldingTwoHandedWeapon);
	}
	const TArray<FCataclysmPoolAction> Novas = Berserker.Nearby(ECataclysmNearbyAction::SmiteByHit);
	if (TestEqual(TEXT("Pure Carnage and Ragnarok hand over two novas sized by a hit"), Novas.Num(), 2))
	{
		const FCataclysmPoolAction& Carnage = Novas[0].Event == FName(TEXT("critical_strike")) ? Novas[0] : Novas[1];
		const FCataclysmPoolAction& Shockwaves = &Carnage == &Novas[0] ? Novas[1] : Novas[0];
		TestEqual(TEXT("Pure Carnage on a critical strike"), Carnage.Event, FName(TEXT("critical_strike")));
		TestEqual(TEXT("30%"), Carnage.Percent, 30.0f, 0.001f);
		TestEqual(TEXT("within 4 metres"), Carnage.EventValue, 4.0f, 0.001f);
		TestTrue(TEXT("in Frenzy"), Carnage.Condition == ECataclysmStatCondition::InFrenzy);
		TestEqual(TEXT("Ragnarok's shockwaves on a hit dealt"), Shockwaves.Event, FName(TEXT("hit_dealt")));
		TestEqual(TEXT("50%"), Shockwaves.Percent, 50.0f, 0.001f);
		TestEqual(TEXT("within 6 metres"), Shockwaves.EventValue, 6.0f, 0.001f);
		TestTrue(TEXT("in Wrath"), Shockwaves.Condition == ECataclysmStatCondition::InWrath);
	}

	FWarVowsV2bPlayer Bulwark(World);
	if (!TestTrue(TEXT("set-up: a second player"), Bulwark.IsUsable())
		|| !Bulwark.Wear(*this, TEXT("Weapon_Greataxe"), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	const auto Stat = [&Bulwark](const TCHAR* Name)
	{
		return Bulwark.System->StatForSkill(FName(Name), FGameplayTagContainer(), 0.0f);
	};
	Bulwark.Take({{TEXT("Bulwark_capstone_25"), 1}, {TEXT("Bulwark_capstone_100"), 1}});
	TestEqual(TEXT("control: Shield Oath hands over no retaliation penetration"),
			  Stat(UCataclysmRetaliation::PenetrationStat), 0.0f, 0.001f);
	TestEqual(TEXT("control: and Bastion no store of blocked damage"),
			  Stat(UCataclysmRetaliation::BlockedDamageAddedStat), 0.0f, 0.001f);
	Bulwark.Take({{TEXT("Bulwark_capstone_25"), 3}, {TEXT("Bulwark_capstone_100"), 3}});
	TestEqual(TEXT("Blood Oath hands over 25 retaliation penetration"), Stat(UCataclysmRetaliation::PenetrationStat),
			  25.0f, 0.001f);
	TestEqual(TEXT("and Spiteful Guard the flag"), Stat(UCataclysmRetaliation::BlockedDamageAddedStat), 1.0f, 0.001f);
	return true;
}

#undef CATACLYSM_WAR_VOWS_V2B_TEST

#endif // WITH_AUTOMATION_TESTS
