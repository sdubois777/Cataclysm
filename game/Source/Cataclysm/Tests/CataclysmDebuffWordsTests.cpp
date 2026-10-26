// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Issue #2379: Cripple, Weaken, Shred and Stun named on screen. Each word is said while its tag is held on a living
 * actor, and the creature's status line carries it; with no tag, neither does. The tag is put on as a loose tag, the
 * holding the text functions read, so each test is about the word and not about how an ailment lands.
 */
namespace CataclysmDebuffWordsTest
{
	/** A creature in the world, out of the way. */
	ACataclysmEnemyCharacter* DebuffWordsCreature(UWorld* World)
	{
		FActorSpawnParameters Always;
		Always.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), FVector(30000.0f, 30000.0f, 0.0f), FRotator::ZeroRotator, Always);
		if (Made)
		{
			Made->SetHealth(1000.0f);
		}
		return Made;
	}

	/**
	 * The one check each word's test makes: the control first, with no tag nothing is said and the status line lacks
	 * the word; then, with the tag held, the word is said and the status line has it; then, the tag taken off, it is
	 * not said again. Six assertions after the set-up.
	 */
	bool WordFollowsTheTag(FAutomationTestBase& Test, const TCHAR* TagName, const TCHAR* Word,
						   FString (*TextFor)(const AActor*))
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("set-up: a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), /*ErrorIfNotFound=*/false);
		ACataclysmEnemyCharacter* Creature = DebuffWordsCreature(World);
		UAbilitySystemComponent* AbilitySystem =
			Creature ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Creature) : nullptr;
		if (!Test.TestTrue(*FString::Printf(TEXT("set-up: %s is a registered tag, on a creature with an ability system"),
											 TagName),
						   Tag.IsValid() && AbilitySystem))
		{
			return false;
		}

		Test.TestEqual(TEXT("control: with no tag, nothing is said"), TextFor(Creature), FString());
		Test.TestFalse(TEXT("control: nor does the status line say it"),
					   UCataclysmCombatOverlay::StatusLineFor(Creature).Contains(Word));

		AbilitySystem->AddLooseGameplayTag(Tag);
		Test.TestEqual(*FString::Printf(TEXT("with the tag held, the text is \"%s\""), Word), TextFor(Creature),
					   FString(Word));
		Test.TestTrue(TEXT("and the status line holds it"), UCataclysmCombatOverlay::StatusLineFor(Creature).Contains(Word));

		AbilitySystem->RemoveLooseGameplayTag(Tag);
		Test.TestEqual(TEXT("the tag taken off, nothing is said again"), TextFor(Creature), FString());
		Test.TestFalse(TEXT("and the status line does not say it"),
					   UCataclysmCombatOverlay::StatusLineFor(Creature).Contains(Word));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDebuffWordsCrippledTest,
	"Cataclysm.DebuffWords.CrippledIsSaidWhileCrippleIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDebuffWordsCrippledTest::RunTest(const FString&)
{
	return CataclysmDebuffWordsTest::WordFollowsTheTag(*this, TEXT("Status.Debuff.Cripple"), TEXT("Crippled"),
		&UCataclysmCombatOverlay::CrippledTextFor);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDebuffWordsWeakenedTest,
	"Cataclysm.DebuffWords.WeakenedIsSaidWhileWeakenIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDebuffWordsWeakenedTest::RunTest(const FString&)
{
	return CataclysmDebuffWordsTest::WordFollowsTheTag(*this, TEXT("Status.Debuff.Weaken"), TEXT("Weakened"),
		&UCataclysmCombatOverlay::WeakenedTextFor);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDebuffWordsShreddedTest,
	"Cataclysm.DebuffWords.ShreddedIsSaidWhileShredIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDebuffWordsShreddedTest::RunTest(const FString&)
{
	return CataclysmDebuffWordsTest::WordFollowsTheTag(*this, TEXT("Status.Debuff.Shred"), TEXT("Shredded"),
		&UCataclysmCombatOverlay::ShreddedTextFor);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDebuffWordsStunnedTest,
	"Cataclysm.DebuffWords.StunnedIsSaidWhileStunIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDebuffWordsStunnedTest::RunTest(const FString&)
{
	return CataclysmDebuffWordsTest::WordFollowsTheTag(*this, TEXT("State.Stunned"), TEXT("Stunned"),
		&UCataclysmCombatOverlay::StunnedTextFor);
}

#endif // WITH_AUTOMATION_TESTS
