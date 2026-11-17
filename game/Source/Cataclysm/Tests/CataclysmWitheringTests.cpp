// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Misc/ScopeExit.h"

/**
 * Famine's Withering Blows, issue #1722: a creature carrying it adds one stack of Withering to the player for each of
 * its hits that takes health; the count is shown as "Withering N/10" from one to nine; at ten Withering is taken off,
 * every stack is spent and the player is given Wither, 30% less movement and attack speed for 4 seconds. The count
 * is lost when eight seconds pass between two such hits. The project owner chose these figures on 2026-10-30.
 */
namespace CataclysmWitheringTest
{
	/** A player character on its player state, with nothing that could turn a blow aside. */
	ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
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
			// AN ATTACK SPEED OF 1 A SECOND, so the seconds between swings can be read at all.
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.0f);
		}
		return Character;
	}

	/** A creature, carrying Withering Blows or not. */
	ACataclysmEnemyCharacter* SpawnCreature(UWorld* World, bool bCarries)
	{
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Creature && bCarries)
		{
			Creature->ModifierRows.Add(FName(UCataclysmEnemyModifiers::WitheringBlowsRow));
		}
		return Creature;
	}

	FGameplayTag Withering() { return UCataclysmSkillShapes::StatusTagFor(TEXT("Withering")); }
	FGameplayTag Wither() { return UCataclysmSkillShapes::StatusTagFor(TEXT("Wither")); }

	int32 Held(const AActor* Player)
	{
		const UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player));
		return System ? UCataclysmStacks::Held(System, ECataclysmStackKind::Withering) : -1;
	}

	/** One hit of one point: negligible damage that still takes health. */
	void Hit(AActor* Creature, AActor* Player)
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Creature, Player, /*Damage=*/1.0f, FCataclysmHitDelivery());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWitheringCountTest,
	"Cataclysm.Withering.TheCountFiresAtTenSpendsEveryStackAndLapsesAfterEightSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWitheringCountTest::RunTest(const FString&)
{
	using namespace CataclysmWitheringTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	UCataclysmAbilitySystemComponent* System =
		Player ? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player)) : nullptr;
	if (!TestNotNull(TEXT("set-up: a player with an ability system"), System))
	{
		return false;
	}

	// TWENTY NOTES: THE TENTH AND THE TWENTIETH FIRE, AND EACH FIRING SPENDS EVERY STACK.
	TArray<FString> Fired;
	TArray<FString> HeldAfter;
	for (int32 Note = 1; Note <= 20; ++Note)
	{
		if (UCataclysmStacks::NoteWithering(System))
		{
			Fired.Add(FString::FromInt(Note));
		}
		HeldAfter.Add(FString::FromInt(UCataclysmStacks::Held(System, ECataclysmStackKind::Withering)));
	}
	TestEqual(TEXT("the notes that fired"), FString::Join(Fired, TEXT(" ")), FString(TEXT("10 20")));
	TestEqual(TEXT("the count after each of twenty notes"), FString::Join(HeldAfter, TEXT(" ")),
			  FString(TEXT("1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0")));

	// AND THE WINDOW: nine stacks held just inside eight seconds, none just past them, and the next starts at one.
	for (int32 Note = 1; Note <= 9; ++Note)
	{
		UCataclysmStacks::NoteWithering(System);
	}
	World->TimeSeconds += 7.9f;
	TestEqual(TEXT("nine stacks are still held 7.9 seconds after the last"),
			  UCataclysmStacks::Held(System, ECataclysmStackKind::Withering), 9);
	World->TimeSeconds += 0.2f;
	TestEqual(TEXT("and none 8.1 seconds after it"), UCataclysmStacks::Held(System, ECataclysmStackKind::Withering), 0);
	TestFalse(TEXT("so the next note does not fire"), UCataclysmStacks::NoteWithering(System));
	TestEqual(TEXT("and starts again at one"), UCataclysmStacks::Held(System, ECataclysmStackKind::Withering), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWitheringShownThenSlowsTest,
	"Cataclysm.Withering.ACarriersHitsShowTheCountThenTheTenthSlowsWalkAndSwingForFourSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWitheringShownThenSlowsTest::RunTest(const FString&)
{
	using namespace CataclysmWitheringTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	UCataclysmAbilitySystemComponent* System =
		Player ? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player)) : nullptr;
	ACataclysmEnemyCharacter* Carrier = SpawnCreature(World, /*bCarries=*/true);
	ACataclysmEnemyCharacter* Plain = SpawnCreature(World, /*bCarries=*/false);
	if (!TestTrue(TEXT("set-up: a player, its ability system, its movement, both tags and two creatures"),
				  System && Player->GetCharacterMovement() && Withering().IsValid() && Wither().IsValid()
				  && Carrier && Plain))
	{
		return false;
	}
	const auto Walk = [Player]() { return Player->GetCharacterMovement()->MaxWalkSpeed; };
	const float WalkBefore = Walk();
	const float SwingBefore = UCataclysmBasicAttack::SecondsBetweenSwingsFor(System);
	if (!TestTrue(TEXT("set-up: the player walks and swings at some rate"), WalkBefore > 0.0f && SwingBefore > 0.0f))
	{
		return false;
	}

	// CONTROL, FIRST: ten hits from a creature without the modifier add nothing and show nothing.
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Hit(Plain, Player);
	}
	TestEqual(TEXT("control: a plain creature's hits add no Withering"), Held(Player), 0);
	TestFalse(TEXT("control: and put no Withering on the player"), System->HasMatchingGameplayTag(Withering()));
	TestEqual(TEXT("control: and no text"), UCataclysmCombatOverlay::WitheringTextFor(Player), FString());

	// HITS ONE TO NINE: THE COUNT IS SHOWN, AND NOTHING SLOWS.
	TArray<FString> Shown;
	bool bWitheredEarly = false;
	for (int32 Index = 1; Index <= 9; ++Index)
	{
		Hit(Carrier, Player);
		Shown.Add(UCataclysmCombatOverlay::WitheringTextFor(Player));
		bWitheredEarly |= System->HasMatchingGameplayTag(Wither());
	}
	TestEqual(TEXT("the text after each of the first nine hits"), FString::Join(Shown, TEXT(", ")),
			  FString(TEXT("Withering 1/10, Withering 2/10, Withering 3/10, Withering 4/10, Withering 5/10, "
						   "Withering 6/10, Withering 7/10, Withering 8/10, Withering 9/10")));
	TestTrue(TEXT("the player carries Withering at nine"), System->HasMatchingGameplayTag(Withering()));
	TestFalse(TEXT("and is not Withered before the tenth"), bWitheredEarly);
	TestEqual(TEXT("and walks at full speed"), Walk(), WalkBefore, 0.5f);

	// THE TENTH: WITHERING GOES, THE COUNT IS SPENT, AND WITHER SLOWS BOTH SPEEDS BY 30%.
	Hit(Carrier, Player);
	TestFalse(TEXT("at the tenth hit Withering is taken off"), System->HasMatchingGameplayTag(Withering()));
	TestEqual(TEXT("and its text is gone"), UCataclysmCombatOverlay::WitheringTextFor(Player), FString());
	TestEqual(TEXT("and every stack is spent"), Held(Player), 0);
	TestTrue(TEXT("and the player carries Wither"), System->HasMatchingGameplayTag(Wither()));
	TestEqual(TEXT("which the line above the player's bars names"), UCataclysmCombatOverlay::WitheredTextFor(Player),
			  FString(TEXT("Withered")));
	TestEqual(TEXT("the share of speed kept under Wither is seven tenths"),
			  UCataclysmSkillEffects::WitherMultiplierOn(Player), 0.7f, 0.0001f);
	TestEqual(TEXT("so the player walks at seven tenths"), Walk(), WalkBefore * 0.7f, 0.5f);
	TestEqual(TEXT("and waits ten sevenths as long between swings"),
			  UCataclysmBasicAttack::SecondsBetweenSwingsFor(System), SwingBefore / 0.7f, 0.001f);

	// AND IT LASTS FOUR SECONDS: still held at 3.9, gone at 4.1, and the walk is whole again.
	CataclysmTestWorld::RunClock(World, 3.9f);
	TestTrue(TEXT("Wither is still held 3.9 seconds later"), System->HasMatchingGameplayTag(Wither()));
	CataclysmTestWorld::RunClock(World, 0.2f);
	TestFalse(TEXT("and gone 4.1 seconds later"), System->HasMatchingGameplayTag(Wither()));
	TestEqual(TEXT("so the player walks at full speed again"), Walk(), WalkBefore, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWitheringResetTest,
	"Cataclysm.Withering.TheCountIsNoLongerShownOnceItLapsesAndABlowThatTookNoHealthAddsNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWitheringResetTest::RunTest(const FString&)
{
	using namespace CataclysmWitheringTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	UCataclysmAbilitySystemComponent* System =
		Player ? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player)) : nullptr;
	ACataclysmEnemyCharacter* Carrier = SpawnCreature(World, /*bCarries=*/true);
	if (!TestTrue(TEXT("set-up: a player, its ability system and a carrier"), System && Carrier && Withering().IsValid()))
	{
		return false;
	}

	for (int32 Index = 0; Index < 5; ++Index)
	{
		Hit(Carrier, Player);
	}
	TestEqual(TEXT("control: five hits show five"), UCataclysmCombatOverlay::WitheringTextFor(Player),
			  FString(TEXT("Withering 5/10")));

	// THE COUNT LAPSES: eight seconds and a little with no hit from the carrier.
	CataclysmTestWorld::RunClock(World, 8.1f);
	TestEqual(TEXT("8.1 seconds after the last hit the count is gone"), Held(Player), 0);
	TestEqual(TEXT("and so is its text"), UCataclysmCombatOverlay::WitheringTextFor(Player), FString());
	TestFalse(TEXT("and the Withering status, which lasts as long as the count"),
			  System->HasMatchingGameplayTag(Withering()));
	TestFalse(TEXT("and nothing was slowed"), System->HasMatchingGameplayTag(Wither()));

	Hit(Carrier, Player);
	TestEqual(TEXT("the next hit starts again at one"), UCataclysmCombatOverlay::WitheringTextFor(Player),
			  FString(TEXT("Withering 1/10")));

	// A BLOW THAT TAKES NO HEALTH ADDS NONE: an energy shield takes the whole point.
	System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 100.0f);
	System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 100.0f);
	const float HealthBefore = System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	Hit(Carrier, Player);
	if (TestEqual(TEXT("set-up: the shield took the blow and health did not move"),
				  System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()), HealthBefore, 0.001f))
	{
		TestEqual(TEXT("so the count is still one"), Held(Player), 1);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
