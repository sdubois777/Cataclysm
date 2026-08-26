// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for enemy abilities carrying gameplay tags. Issue #519.
 *
 * WHAT WAS MISSING. A player's skill carries a tag list from the Tags column of
 * the weapon skill matrix, and an enemy's ability carried none, so nothing could
 * ask what an enemy attack is. The project owner asked for it on 2026-08-12.
 *
 * WHAT IT ALREADY COST. Whether a hit is area damage is read off the skill's own
 * tags, and the two enemy abilities that sweep a volume could not say so, so each
 * said it a second way at its call site by passing an area delivery. Those second
 * routes are gone and the tags are now the only answer, which is what makes the
 * evasion tests below load-bearing rather than decoration.
 */

namespace CataclysmEnemyTagTest
{
	using FShapes = UCataclysmSkillShapes;

	/** Every ability tag list in the game, with a name for the failure text. */
	struct FAbilityTags
	{
		const TCHAR* What;
		const TCHAR* Cell;
	};

	static const FAbilityTags EveryAbility[] = {
		{ TEXT("an enemy's basic attack"),
		  ACataclysmEnemyCharacter::BasicAttackTags },
		{ TEXT("a charge"), ACataclysmEnemyCharacter::ChargeTags },
		{ TEXT("the Brute's stomp"), ACataclysmBruteCharacter::StompTags },
		{ TEXT("the Brute's thrown rock"),
		  ACataclysmBruteCharacter::RockThrowTags },
		{ TEXT("the Abyssal Warden's molten roar"),
		  ACataclysmAbyssalWardenCharacter::MoltenRoarTags },
		// THE GATEKEEPER'S TWO WERE MISSING FROM THIS LIST, which is why the
		// audit for issue #1012 had to read the source rather than trust it. A
		// list calling itself "every ability tag list in the game" that is not
		// every one is worse than no list, because it reads as complete.
		{ TEXT("the Gatekeeper's Dread Cleave"),
		  ACataclysmGatekeeperCharacter::CleaveTags },
		{ TEXT("the Gatekeeper's Soul Harvest"),
		  ACataclysmGatekeeperCharacter::SoulHarvestTags },
	};

	/**
	 * Which enemy abilities count as melee. Issue #1012.
	 *
	 * THE RULE IS THE ONE ISSUE #999 SETTLED FOR THE PLAYER'S SKILLS, applied to
	 * the enemy's: an ability carrying `Type.Strike` carries `Type.Melee`. The
	 * project owner answered the two arguable cases on 2026-08-26.
	 *
	 *   is a charge melee?             yes -- it is a body making contact
	 *   is a point-blank boss attack?  yes -- otherwise a melee-scoped defence
	 *                                  stops ordinary swings and NOTHING a boss
	 *                                  does, because every boss attack is one
	 *
	 * THE THROWN ROCK IS THE ONE THAT IS NOT, and it is what makes a melee scope
	 * worth having. If everything were melee, a defence scoped to melee would be
	 * a defence scoped to nothing.
	 *
	 * WHY THIS IS PINNED PER ABILITY RATHER THAN DERIVED FROM THE STRIKE TAG.
	 * Deriving it would assert that the code agrees with itself. What is worth
	 * guarding is the DECISION -- that these six are melee and that one is not --
	 * so a creature added later has to be listed here on purpose.
	 */
	struct FMeleeExpectation
	{
		const TCHAR* What;
		const TCHAR* Cell;
		bool bIsMelee;
	};

	static const FMeleeExpectation MeleeByAbility[] = {
		{ TEXT("an enemy's basic attack"),
		  ACataclysmEnemyCharacter::BasicAttackTags, true },
		{ TEXT("a charge"), ACataclysmEnemyCharacter::ChargeTags, true },
		{ TEXT("the Brute's stomp"),
		  ACataclysmBruteCharacter::StompTags, true },
		{ TEXT("the Abyssal Warden's molten roar"),
		  ACataclysmAbyssalWardenCharacter::MoltenRoarTags, true },
		{ TEXT("the Gatekeeper's Dread Cleave"),
		  ACataclysmGatekeeperCharacter::CleaveTags, true },
		{ TEXT("the Gatekeeper's Soul Harvest"),
		  ACataclysmGatekeeperCharacter::SoulHarvestTags, true },
		{ TEXT("the Brute's thrown rock"),
		  ACataclysmBruteCharacter::RockThrowTags, false },
	};

	/** How many names a cell lists, counted without asking the tag manager. */
	static int32 NamesIn(const TCHAR* Cell)
	{
		TArray<FString> Names;
		FString(Cell).ParseIntoArray(Names, TEXT(","), /*InCullEmpty=*/true);
		return Names.Num();
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmEnemyAbilityTagsExistTest,
	"Cataclysm.EnemyTags.EveryEnemyAbilityStatesTagsTheVocabularyKnows")
{
	using namespace CataclysmEnemyTagTest;

	// A MISSPELLED TAG IS DROPPED SILENTLY AND SCOPES NOTHING.
	// UCataclysmSkillShapes::TagsFromCell asks the tag manager for each name with
	// ErrorIfNotFound false, warns, and leaves it out. So a typo would compile,
	// run, and quietly make the Brute's stomp evadable. Counting the names in the
	// cell against the tags that came back is what catches it.
	for (const FAbilityTags& Ability : EveryAbility)
	{
		const int32 Stated = NamesIn(Ability.Cell);
		const FGameplayTagContainer Parsed = FShapes::TagsFromCell(Ability.Cell);

		TestTrue(FString::Printf(TEXT("%s states some tags"), Ability.What),
			Stated > 0);
		TestEqual(FString::Printf(
			TEXT("%s: every one of its %d tags is in the vocabulary (%s)"),
			Ability.What, Stated, Ability.Cell),
			Parsed.Num(), Stated);
	}

	return true;
}

CATACLYSM_TEST(FCataclysmEnemyAreaAbilitiesSaySoTest,
	"Cataclysm.EnemyTags.AnEnemysAreaAbilitiesSaySoInTheirOwnTags")
{
	using namespace CataclysmEnemyTagTest;

	// THE TAGS ARE THE ONLY ANSWER NOW. Both of these used to pass an area
	// delivery at the call site as well, which is the second route issue #519 was
	// opened to remove.
	TestTrue(TEXT("the Brute's stomp is area damage"),
		UCataclysmSkillEffects::IsAreaDamage(
			FShapes::TagsFromCell(ACataclysmBruteCharacter::StompTags)));
	TestTrue(TEXT("and so is the Abyssal Warden's molten roar"),
		UCataclysmSkillEffects::IsAreaDamage(FShapes::TagsFromCell(
			ACataclysmAbyssalWardenCharacter::MoltenRoarTags)));

	// AND THE ONES THAT TOUCH ONE TARGET ARE NOT, which is what makes the two
	// above a statement about those abilities rather than about every enemy hit.
	TestFalse(TEXT("an enemy's basic attack is not area damage"),
		UCataclysmSkillEffects::IsAreaDamage(
			FShapes::TagsFromCell(ACataclysmEnemyCharacter::BasicAttackTags)));
	TestFalse(TEXT("nor is a charge"),
		UCataclysmSkillEffects::IsAreaDamage(
			FShapes::TagsFromCell(ACataclysmEnemyCharacter::ChargeTags)));
	TestFalse(TEXT("nor is the Brute's thrown rock"),
		UCataclysmSkillEffects::IsAreaDamage(
			FShapes::TagsFromCell(ACataclysmBruteCharacter::RockThrowTags)));

	return true;
}

CATACLYSM_TEST(FCataclysmStompCannotBeEvadedTest,
	"Cataclysm.EnemyTags.ABrutesStompCannotBeEvadedBecauseOfItsTags")
{
	// THE GUARD FOR WHAT WAS REMOVED. The stomp's call site no longer says "this
	// is area damage"; its tags do. If they stopped reaching the hit, the stomp
	// would quietly become evadable and nothing else in the suite would notice --
	// no test anywhere covered this before issue #519.
	//
	// A DEFENDER AT 100% EVASION IS DECIDED RATHER THAN LIKELY. The roll is a
	// random number in [0, 100) compared with strictly less than, so at 100 it
	// always evades and no roll has to be pinned.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Caught =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(10000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a Brute"), Brute)
		|| !TestNotNull(TEXT("something to catch"), Caught))
	{
		return false;
	}

	// On the player's side, so the Brute's sweep finds it as an enemy.
	Caught->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Caught->SetHealth(100000.0f);

	// Moved rather than spawned close, because two capsules created at contact
	// distance push each other apart.
	Caught->SetActorLocation(FVector(ACataclysmBruteCharacter::StompRadiusCm * 0.4f,
									 0.0f, Caught->GetActorLocation().Z));

	UAbilitySystemComponent* Defence = Caught->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the defender's ability system"), Defence))
	{
		return false;
	}
	// THE BRUTE NEEDS DAMAGE TO DEAL. A creature spawned without its archetype
	// row carries no attack damage, and a hit worth nothing is indistinguishable
	// from a hit that was evaded -- which is what the first run of this test
	// read as a failure.
	if (UAbilitySystemComponent* Offence = Brute->GetAbilitySystemComponent())
	{
		Offence->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1000.0f);
	}

	Defence->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetEvasionAttribute(), 100.0f);

	const auto HealthOf = [](const UAbilitySystemComponent* System)
	{
		return System->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute());
	};

	const float Before = HealthOf(Defence);
	Brute->UseEnemyAbility(ACataclysmBruteCharacter::StompAbility, Caught,
						   FVector::ZeroVector);

	TestTrue(FString::Printf(
		TEXT("a stomp lands on a defender that evades everything (%.0f dealt)"),
		Before - HealthOf(Defence)),
		HealthOf(Defence) < Before);

	return true;
}

CATACLYSM_TEST(FCataclysmBasicAttackCanBeEvadedTest,
	"Cataclysm.EnemyTags.AnEnemysBasicAttackCanStillBeEvaded")
{
	// THE OTHER HALF, and without it the test above proves only that damage
	// happened. An ordinary swing is one blow at one target, so a defender that
	// evades everything takes nothing from it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Attacker =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Defender =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an attacker"), Attacker)
		|| !TestNotNull(TEXT("a defender"), Defender))
	{
		return false;
	}

	Defender->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Defender->SetHealth(100000.0f);

	UAbilitySystemComponent* Offence = Attacker->GetAbilitySystemComponent();
	UAbilitySystemComponent* Defence = Defender->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the attacker's ability system"), Offence)
		|| !TestNotNull(TEXT("the defender's ability system"), Defence))
	{
		return false;
	}

	// Enough damage that landing would be unmistakable.
	Offence->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1000.0f);
	Defence->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetEvasionAttribute(), 100.0f);

	const float Before = Defence->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	Attacker->AttackTarget(Defender);

	TestEqual(TEXT("an ordinary swing is evaded entirely"),
		Defence->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute()), Before);

	return true;
}

CATACLYSM_TEST(FCataclysmScopedModifierReachesAnEnemyAbilityTest,
	"Cataclysm.EnemyTags.AModifierScopedToOneKindOfEnemyAttackReachesOnlyThat")
{
	// THE CAPABILITY ISSUE #519 WAS OPENED FOR, asked directly. Its own words:
	// "Scoped stat modifiers. UCataclysmStatPipeline decides which modifiers
	// reach a hit from its tags. A modifier that should apply to one kind of
	// enemy attack and not another has nothing to match on."
	//
	// It has something to match on now. Nothing in the shipped game gives an
	// enemy a stat modifier yet, so this builds two and checks which arrive.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Attacker =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Defender =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an attacker"), Attacker)
		|| !TestNotNull(TEXT("a defender"), Defender))
	{
		return false;
	}

	Defender->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Defender->SetHealth(1000000.0f);

	UCataclysmAbilitySystemComponent* Offence =
		Cast<UCataclysmAbilitySystemComponent>(
			Attacker->GetAbilitySystemComponent());
	UAbilitySystemComponent* Defence = Defender->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("the attacker's ability system"), Offence)
		|| !TestNotNull(TEXT("the defender's ability system"), Defence))
	{
		return false;
	}

	Offence->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1000.0f);

	const auto HealthOf = [Defence]()
	{
		return Defence->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute());
	};

	const auto SwingFor = [&]()
	{
		const float Before = HealthOf();
		Attacker->AttackTarget(Defender);
		return Before - HealthOf();
	};

	// What one swing is worth before anything scopes to it.
	const float Plain = SwingFor();
	if (!TestTrue(TEXT("an ordinary swing deals something to measure against"),
		Plain > 0.0f))
	{
		return false;
	}

	const auto Scoped = [](const TCHAR* TagName)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Increased;
		Modifier.Source = ECataclysmModifierSource::GearAffix;
		Modifier.Value = 100.0f;
		Modifier.RequiredTags.AddTag(
			UGameplayTagsManager::Get().RequestGameplayTag(
				FName(TagName), /*ErrorIfNotFound=*/false));
		return Modifier;
	};

	// A TAG THE BASIC ATTACK DOES NOT CARRY. It is Type.Strike and Type.Melee;
	// a projectile it is not.
	Offence->AddStatModifier(Scoped(TEXT("Type.Projectile")));
	TestEqual(TEXT("a modifier scoped to a tag the swing lacks does nothing"),
		SwingFor(), Plain, 1.0f);

	// AND ONE IT DOES CARRY DOUBLES IT.
	Offence->AddStatModifier(Scoped(TEXT("Type.Melee")));
	TestEqual(TEXT("and one scoped to a tag it carries doubles the swing"),
		SwingFor(), Plain * 2.0f, 1.0f);

	return true;
}

/**
 * Which enemy abilities count as melee, and which one does not.
 *
 * WHY THIS EXISTS. Issue #1012. The project owner decided on 2026-08-26 that a
 * melee-scoped bonus should reach every enemy ability carrying `Type.Strike`,
 * which is the rule issue #999 settled for the player's own skills. Before that
 * decision only ONE of the seven carried `Type.Melee` -- the basic attack -- so
 * a defence scoped to melee would have stopped an ordinary swing and nothing
 * any boss in the game does.
 *
 * THE FAILURE THIS CATCHES IS SILENT AND IS NOT A CRASH. A modifier scoped to a
 * tag no ability carries is built, applied and simply never matched. Nothing at
 * run time reports it, and the character is quietly worse.
 */
CATACLYSM_TEST(FCataclysmEnemyMeleeAbilitiesSaySoTest,
	"Cataclysm.EnemyTags.EveryEnemyStrikeSaysItIsMeleeAndTheThrownRockDoesNot")
{
	using namespace CataclysmEnemyTagTest;

	for (const FMeleeExpectation& Ability : MeleeByAbility)
	{
		const FGameplayTagContainer Tags = FShapes::TagsFromCell(Ability.Cell);
		const bool bSaysMelee = Tags.HasTag(
			FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee")),
											 /*ErrorIfNotFound=*/false));

		TestEqual(*FString::Printf(
				TEXT("%s %s melee"), Ability.What,
				Ability.bIsMelee ? TEXT("is") : TEXT("is not")),
			bSaysMelee, Ability.bIsMelee);
	}

	// AND THE STRIKE TAG IS WHAT DECIDES IT, which is the rule rather than the
	// list. Stated separately so a creature added later fails HERE, with a
	// message naming the rule, rather than only in the list above.
	for (const FAbilityTags& Ability : EveryAbility)
	{
		const FGameplayTagContainer Tags = FShapes::TagsFromCell(Ability.Cell);
		const bool bStrikes = Tags.HasTag(
			FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Strike")),
											 /*ErrorIfNotFound=*/false));
		const bool bMelee = Tags.HasTag(
			FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee")),
											 /*ErrorIfNotFound=*/false));

		TestEqual(*FString::Printf(
				TEXT("%s carries Type.Melee exactly when it carries Type.Strike"),
				Ability.What),
			bMelee, bStrikes);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
