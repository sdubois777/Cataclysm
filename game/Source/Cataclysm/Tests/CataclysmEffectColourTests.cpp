// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmImpactEffect.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmProjectileEffect.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A PLAYER'S SKILL HAS A DAMAGE TYPE AND ITS EFFECTS SHOULD BE DRAWN IN IT.
 *
 * Issue #803. Every player skill names a damage type in
 * `game/Data/WeaponSkills.csv` -- 51 of the 58 shaped rows are Demonic and 7 are
 * War -- and nothing read that column for a visual effect. So every bolt and
 * every burst a player produced drew in the authored default, which is white,
 * and a Demonic skill and a War skill looked identical.
 *
 * THE REASON IT WAS HARD IS THAT ONE QUESTION WAS ANSWERING TWO. A player's
 * damage reaches the defender untyped on purpose: an enemy holds one generic
 * resistance and has nothing to choose between. That ruling is about
 * resistances and says nothing about colour, but the same function was giving
 * both answers.
 *
 * WHAT THESE TESTS PIN. The two answers are now separate and both are still
 * right: a player's hit draws in the skill's colour AND arrives untyped, and an
 * enemy's hit is unchanged in both respects.
 */
namespace CataclysmEffectColourTest
{
	/** Every part of a fighter these tests need, on a plain actor. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, float AttackDamage)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem =
				NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);

			Vitals = NewVitals;
			Combat = NewCombat;
			Resistances = NewResist;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Vitals->SetMaxHealth(1'000'000.0f);
			Vitals->SetHealth(1'000'000.0f);
			LastHealth = 1'000'000.0f;

			Combat->SetAttackDamage(AttackDamage);
		}

		~FScopedFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** Health lost since this was last read. */
		float TakeDamageReading()
		{
			const float Now = Vitals->GetHealth();
			const float Lost = LastHealth - Now;
			LastHealth = Now;
			return Lost;
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
		TObjectPtr<UCataclysmResistanceAttributeSet> Resistances = nullptr;
		float LastHealth = 0.0f;
	};

	/**
	 * Critical strikes pinned off for the duration.
	 *
	 * WITHOUT THIS EVERY DAMAGE ASSERTION BELOW READS HALF AS MUCH AGAIN, AT
	 * RANDOM. The same guard exists in CataclysmDamageTypeTests.cpp for the same
	 * reason.
	 */
	struct FScopedNoCriticalStrikes
	{
		FScopedNoCriticalStrikes()
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("Cataclysm.CritRoll"));
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(100.0f, ECVF_SetByConsole);
			}
		}

		~FScopedNoCriticalStrikes()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/** The tag container a skill row of that damage type produces. */
	static FGameplayTagContainer TagsForElement(const TCHAR* Element)
	{
		FGameplayTagContainer Tags;
		const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Element), /*ErrorIfNotFound=*/false);
		if (Tag.IsValid())
		{
			Tags.AddTag(Tag);
		}
		return Tags;
	}
}

// --------------------------------------------------------------------------
// The bolt in flight
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerBoltTakesTheSkillsColour,
	"Cataclysm.Effects.APlayersBoltIsColouredByTheSkillThatFiredIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerBoltTakesTheSkillsColour::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A PLAYER STANDS IN AS A PLAIN ACTOR, which is exactly what
	// UCataclysmSkillEffects::DamageTypeOf sees: anything that is not an
	// ACataclysmEnemyCharacter is untyped.
	FScopedFighter Player(World, 100.0f);

	ACataclysmProjectile* Bolt = ACataclysmProjectile::Fire(
		Player.Actor, FVector::ZeroVector, FVector(1'000.0f, 0.0f, 0.0f),
		/*InRadiusCm=*/40.0f, /*InSpeed=*/1'500.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		TagsForElement(TEXT("Element.Demonic")), /*bInBurns=*/false);

	if (!TestNotNull(TEXT("a bolt in the air"), Bolt))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Bolt)) { Bolt->Destroy(); } };

	// THE WHOLE OF ISSUE #803 IN ONE ASSERTION. Before this the answer was None
	// and NS_Proj_Body kept its authored white.
	TestEqual(TEXT("a player's bolt is drawn as its skill's damage type"),
		UCataclysmProjectileEffect::DamageTypeFor(Bolt), FName(TEXT("Demonic")));

	// AND A SKILL OF A DIFFERENT TYPE IS A DIFFERENT COLOUR, which is the half
	// that would still fail if the answer were hard-coded. 7 of the 58 shaped
	// rows are War.
	ACataclysmProjectile* WarBolt = ACataclysmProjectile::Fire(
		Player.Actor, FVector::ZeroVector, FVector(1'000.0f, 0.0f, 0.0f),
		/*InRadiusCm=*/40.0f, /*InSpeed=*/1'500.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		TagsForElement(TEXT("Element.War")), /*bInBurns=*/false);
	if (TestNotNull(TEXT("a second bolt"), WarBolt))
	{
		TestEqual(TEXT("a War skill's bolt is drawn as War"),
			UCataclysmProjectileEffect::DamageTypeFor(WarBolt),
			FName(TEXT("War")));
		WarBolt->Destroy();
	}

	// A SKILL THAT NAMES NO TYPE STILL DRAWS THE AUTHORED DEFAULT rather than
	// guessing one. Nothing in the shipped data does this, and the answer still
	// has to be defined.
	ACataclysmProjectile* Plain = ACataclysmProjectile::Fire(
		Player.Actor, FVector::ZeroVector, FVector(1'000.0f, 0.0f, 0.0f),
		/*InRadiusCm=*/40.0f, /*InSpeed=*/1'500.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FGameplayTagContainer(), /*bInBurns=*/false);
	if (TestNotNull(TEXT("an untagged bolt"), Plain))
	{
		TestEqual(TEXT("a skill with no damage type names none"),
			UCataclysmProjectileEffect::DamageTypeFor(Plain), FName(NAME_None));
		Plain->Destroy();
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyBoltStillTakesTheFirersColour,
	"Cataclysm.Effects.AnEnemysBoltIsStillColouredByTheEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyBoltStillTakesTheFirersColour::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Enemy)) { Enemy->Destroy(); } };

	Enemy->DamageType = FName(TEXT("Void"));

	// THE FIRER WINS OVER THE SKILL, and it has to. An enemy's damage type is
	// what its hits are resisted as, so a bolt drawn in a different colour would
	// mislead in the one case where the colour tells the player something they
	// can act on. The skill tag below deliberately disagrees.
	ACataclysmProjectile* Bolt = ACataclysmProjectile::Fire(
		Enemy, FVector::ZeroVector, FVector(1'000.0f, 0.0f, 0.0f),
		/*InRadiusCm=*/40.0f, /*InSpeed=*/1'500.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		TagsForElement(TEXT("Element.Demonic")), /*bInBurns=*/false);

	if (TestNotNull(TEXT("a bolt in the air"), Bolt))
	{
		TestEqual(TEXT("an enemy's bolt is drawn as the enemy's own type"),
			UCataclysmProjectileEffect::DamageTypeFor(Bolt),
			FName(TEXT("Void")));
		Bolt->Destroy();
	}

	return true;
}

// --------------------------------------------------------------------------
// The burst where the blow lands
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerBurstTakesTheSkillsColour,
	"Cataclysm.Effects.APlayersHitBurstIsColouredByTheSkillThatLandedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerBurstTakesTheSkillsColour::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	const FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Player(World, 1'000.0f);
	FScopedFighter Struck(World, 0.0f);

	// THE DEFENDER RESISTS DEMONIC HITS. It is standing in for a player-shaped
	// defender on purpose, because that is the only shape whose resistance
	// depends on the hit's type at all -- an enemy's generic resistance meets
	// everything and would hide the difference this test is about.
	Struck.Resistances->SetDemonicResistance(40.0f);
	Struck.LastHealth = Struck.Vitals->GetHealth();

	const int32 AskedBefore = UCataclysmImpactEffect::TimesAsked;
	UCataclysmImpactEffect::LastDamageTypeAsked = NAME_None;

	// EVERY STEP THE REAL GAME TAKES. ApplyHit builds the effect, puts the tags
	// on it, applies it, and PostGameplayEffectExecute reads them back and asks
	// for the burst.
	UCataclysmSkillEffects::ApplyHit(Player.Actor, Struck.Actor, 100.0f,
									 TagsForElement(TEXT("Element.Demonic")));

	TestEqual(TEXT("the landed blow asked for a burst"),
		UCataclysmImpactEffect::TimesAsked, AskedBefore + 1);

	// THE FIRST HALF OF ISSUE #803. Before this the answer was None and
	// NS_Impact_Point kept its authored white spark and black core.
	TestEqual(TEXT("and asked for it in the skill's own damage type"),
		UCataclysmImpactEffect::LastDamageTypeAsked, FName(TEXT("Demonic")));

	// THE SECOND HALF, AND THE ONE THAT MUST NOT HAVE BROKEN. The damage still
	// arrives untyped, so the defender's 40 percent Demonic resistance does not
	// meet it and the full 1,000 lands. If the colour tag were being read as a
	// damage type this would read 600.
	TestEqual(TEXT("and the hit still arrived untyped, so nothing resisted it"),
		Struck.TakeDamageReading(), 1'000.0f, 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyHitIsStillResistedByItsType,
	"Cataclysm.Effects.AnEnemysHitIsStillResistedByItsOwnDamageType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyHitIsStillResistedByItsType::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	const FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Enemy)) { Enemy->Destroy(); } };

	Enemy->SetAttackDamage(1'000.0f);
	Enemy->DamageType = FName(TEXT("Demonic"));

	FScopedFighter Struck(World, 0.0f);
	Struck.Resistances->SetDemonicResistance(40.0f);
	Struck.LastHealth = Struck.Vitals->GetHealth();

	UCataclysmImpactEffect::LastDamageTypeAsked = NAME_None;

	UCataclysmSkillEffects::ApplyHit(Enemy, Struck.Actor, 100.0f);

	// THE RULE THAT PAYS FOR EVERYTHING ELSE. An enemy's hit is typed, so the
	// player's resistance to that type applies. Issue #486 was this not
	// happening, and the marker added for issue #803 must not undo it.
	TestEqual(TEXT("the 40 percent Demonic resistance still applies"),
		Struck.TakeDamageReading(), 600.0f, 1.0f);

	TestEqual(TEXT("and the burst is drawn as the enemy's type, as before"),
		UCataclysmImpactEffect::LastDamageTypeAsked, FName(TEXT("Demonic")));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
