// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Misc/ScopeExit.h"

/**
 * Void's Touch of Nothing, issue #1722: a creature carrying it lays one stack on the player for each hit that takes
 * health, up to five, lost after eight seconds with no such hit. Each stack refuses ONE buff the player would gain, by
 * the project owner's "Buffs + gear procs" of 2026-10-30: a random buff draw, a self-buff skill's cast (still paid
 * for), and a worn or set row's timed grant that is not a drawback's. A Masochist stack and a drawback's timed stack
 * are not buffs it refuses.
 */
namespace CataclysmTouchOfNothingTest
{
	ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World, const FVector& Where = FVector::ZeroVector)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!PlayerState || !Character)
		{
			return nullptr;
		}
		Character->SetPlayerState(PlayerState);
		Character->OnRep_PlayerState();
		if (UCataclysmAbilitySystemComponent* System = PlayerState->GetCataclysmAbilitySystemComponent())
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 0.0f);
		}
		return Character;
	}

	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	int32 Held(const AActor* Actor)
	{
		const UCataclysmAbilitySystemComponent* System = SystemOf(Actor);
		return System ? UCataclysmStacks::Held(System, ECataclysmStackKind::TouchOfNothing) : -1;
	}

	/** A worn row's own stack, granted on `Event` for 5 seconds and up to 10, from a benefit or a drawback. */
	FCataclysmPoolAction StackOn(FName Event, FName Key, bool bFromADrawback)
	{
		FCataclysmPoolAction Grant;
		Grant.Event = Event;
		Grant.StackKey = Key;
		Grant.StackSeconds = 5.0f;
		Grant.StackCap = 10;
		Grant.TriggerKey = Key;
		Grant.bFromADrawback = bFromADrawback;
		return Grant;
	}

	/** Gives the player a self buff in the Support slot and uses it; the row is the ally-buff tests' Burning Wrath. */
	UCataclysmSelfBuffSkill* CastSelfBuff(ACataclysmPlayerCharacter* Caster)
	{
		UCataclysmAbilitySystemComponent* System = SystemOf(Caster);
		if (!System)
		{
			return nullptr;
		}
		const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
			UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Support, /*Level=*/100, Caster);
		FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
		UCataclysmSelfBuffSkill* Skill = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			return nullptr;
		}
		Skill->SkillName = TEXT("Burning Wrath");
		Skill->Params = UCataclysmSkillShapes::ParseParams(
			TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning"));
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT(""));
		System->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		return Skill;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTouchOfNothingLaysStacksTest,
	"Cataclysm.TouchOfNothing.ACarriersHitsLayUpToFiveStacksAndTheCountIsShown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTouchOfNothingLaysStacksTest::RunTest(const FString&)
{
	using namespace CataclysmTouchOfNothingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	ACataclysmEnemyCharacter* Carrier = World->SpawnActor<ACataclysmEnemyCharacter>(FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Plain = World->SpawnActor<ACataclysmEnemyCharacter>(FVector(-500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player and two creatures"), Player && SystemOf(Player) && Carrier && Plain))
	{
		return false;
	}
	Carrier->ModifierRows.Add(FName(UCataclysmEnemyModifiers::TouchOfNothingRow));

	// CONTROL, FIRST: a creature without the modifier lays nothing.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Plain, Player, 1.0f, FCataclysmHitDelivery());
	}
	TestEqual(TEXT("control: a plain creature's hits lay no Touch of Nothing"), Held(Player), 0);

	// SEVEN HITS FROM THE CARRIER: the count stops at five.
	TArray<FString> Shown;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Carrier, Player, 1.0f, FCataclysmHitDelivery());
		Shown.Add(UCataclysmCombatOverlay::TouchOfNothingTextFor(Player));
	}
	TestEqual(TEXT("the text after each of seven hits"), FString::Join(Shown, TEXT(", ")),
			  FString(TEXT("Touch of Nothing 1, Touch of Nothing 2, Touch of Nothing 3, Touch of Nothing 4, "
						   "Touch of Nothing 5, Touch of Nothing 5, Touch of Nothing 5")));
	TestEqual(TEXT("so five stacks are held"), Held(Player), 5);

	// AND THEY ARE LOST EIGHT SECONDS AFTER THE LAST HIT.
	World->TimeSeconds += 8.1f;
	TestEqual(TEXT("none are held 8.1 seconds after the last hit"), Held(Player), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTouchOfNothingRefusesADrawTest,
	"Cataclysm.TouchOfNothing.AHeldStackRefusesOneRandomBuffDrawAndIsSpent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTouchOfNothingRefusesADrawTest::RunTest(const FString&)
{
	using namespace CataclysmTouchOfNothingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	UCataclysmAbilitySystemComponent* System = Player ? SystemOf(Player) : nullptr;
	if (!TestNotNull(TEXT("set-up: a player with an ability system"), System))
	{
		return false;
	}

	UCataclysmStacks::NoteTouchOfNothing(System);
	TestEqual(TEXT("set-up: one stack held"), Held(Player), 1);
	TestEqual(TEXT("control: nothing has been negated yet"), UCataclysmCombatOverlay::NegatedTextFor(Player), FString());

	TestFalse(TEXT("the draw is refused"), System->HoldARandomBuff());
	TestTrue(TEXT("so no buff is held"), System->HeldRandomBuff().IsNone());
	TestEqual(TEXT("and the stack is spent"), Held(Player), 0);
	TestEqual(TEXT("and the player is told"), UCataclysmCombatOverlay::NegatedTextFor(Player), FString(TEXT("Negated")));

	// WITH NO STACK, THE NEXT DRAW ARRIVES.
	TestTrue(TEXT("with no stack held the next draw is held"), System->HoldARandomBuff());
	TestFalse(TEXT("and names a buff"), System->HeldRandomBuff().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTouchOfNothingRefusesACastTest,
	"Cataclysm.TouchOfNothing.AHeldStackRefusesASelfBuffCastWhichIsStillPaidFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTouchOfNothingRefusesACastTest::RunTest(const FString&)
{
	using namespace CataclysmTouchOfNothingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Control = SpawnPlayer(World, FVector(0.0f, -5000.0f, 0.0f));
	ACataclysmPlayerCharacter* Touched = SpawnPlayer(World, FVector(0.0f, 5000.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: two players"), Control && Touched && SystemOf(Control) && SystemOf(Touched)))
	{
		return false;
	}

	// CONTROL: with no stack the self buff runs, and its cooldown is spent.
	UCataclysmSelfBuffSkill* Running = CastSelfBuff(Control);
	if (!TestTrue(TEXT("control: the self buff runs"), Running && Running->IsActive()))
	{
		return false;
	}
	TestTrue(TEXT("control: and its cooldown was spent"),
			 SystemOf(Control)->HasMatchingGameplayTag(UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Support)));

	// WITH A STACK HELD: the cast is paid for and the buff never runs.
	UCataclysmStacks::NoteTouchOfNothing(SystemOf(Touched));
	UCataclysmSelfBuffSkill* Refused = CastSelfBuff(Touched);
	if (!TestNotNull(TEXT("set-up: the self buff was given"), Refused))
	{
		return false;
	}
	TestFalse(TEXT("with a stack held the self buff does not run"), Refused->IsActive());
	TestTrue(TEXT("but its cooldown was spent, as the owner ruled"),
			 SystemOf(Touched)->HasMatchingGameplayTag(UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Support)));
	TestEqual(TEXT("and the stack is spent"), Held(Touched), 0);
	TestEqual(TEXT("and the player is told"), UCataclysmCombatOverlay::NegatedTextFor(Touched), FString(TEXT("Negated")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTouchOfNothingRefusesAProcTest,
	"Cataclysm.TouchOfNothing.AHeldStackRefusesAGearProcButNotADrawbackOrAMasochistStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTouchOfNothingRefusesAProcTest::RunTest(const FString&)
{
	using namespace CataclysmTouchOfNothingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	UCataclysmAbilitySystemComponent* System = Player ? SystemOf(Player) : nullptr;
	if (!TestNotNull(TEXT("set-up: a player with an ability system"), System))
	{
		return false;
	}

	const FName Event(TEXT("hit_dealt"));
	const FName Benefit(TEXT("touch_of_nothing_test_benefit"));
	const FName Curse(TEXT("touch_of_nothing_test_curse"));

	// CONTROL, FIRST: with no stack, a benefit's timed stack arrives.
	System->SetPoolActions({StackOn(Event, Benefit, /*bFromADrawback=*/false)});
	System->ActOnEvent(Event);
	TestEqual(TEXT("control: with no stack held the benefit's stack arrives"), System->OwnStacksHeld(Benefit), 1);

	// A HELD STACK REFUSES THE NEXT ONE, and is spent.
	UCataclysmStacks::NoteTouchOfNothing(System);
	System->ActOnEvent(Event);
	TestEqual(TEXT("with a stack held the benefit's next stack is refused"), System->OwnStacksHeld(Benefit), 1);
	TestEqual(TEXT("and the stack is spent"), Held(Player), 0);

	// A DRAWBACK'S TIMED STACK IS NO BUFF: it arrives, and the held stack is not spent on it.
	UCataclysmStacks::NoteTouchOfNothing(System);
	System->SetPoolActions({StackOn(Event, Curse, /*bFromADrawback=*/true)});
	System->ActOnEvent(Event);
	TestEqual(TEXT("a drawback's stack arrives while one is held"), System->OwnStacksHeld(Curse), 1);
	TestEqual(TEXT("and the held stack is not spent on it"), Held(Player), 1);

	// NOR IS A MASOCHIST STACK: Bloodlust, granted for a hit taken, arrives, and the held stack is kept.
	UCataclysmStacks::NoteDamageTaken(System);
	TestEqual(TEXT("a Masochist stack arrives while one is held"),
			  UCataclysmStacks::Held(System, ECataclysmStackKind::Bloodlust), 1);
	TestEqual(TEXT("and the held stack is still kept"), Held(Player), 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
