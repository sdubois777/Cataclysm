// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmNova.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * The nova a character releases while it is very low on health. Issue #1050.
 *
 * WHAT IT IS FOR. The Masochist's `basic_ll_b2` Unstable Aura: "While at or
 * below 10% health, you release a nova every 5 seconds dealing damage equal to
 * 1% of your missing health per point to enemies within 6 metres." It was the
 * last node in that tree that granted nothing and was neither blocked on other
 * work nor waiting on a design answer.
 *
 * THE ARITHMETIC IS TESTED SEPARATELY FROM THE WORLD, which is why
 * `UCataclysmNova` splits them. `AmountFrom` takes three numbers, so every edge
 * of it can be pinned without spawning anything; `Step` needs a world, real
 * actors with collision and a character stat line, so there are fewer of those
 * and they check what the arithmetic cannot: that the interval is kept, that the
 * radius is what the node says, and that a character above the threshold
 * releases nothing.
 *
 * THE STAT IS BUILT THE WAY `UCataclysmPlayerClassStats::ApplyTo` LEAVES IT
 * rather than written onto the gameplay attribute, and that is not a
 * convenience. The node's row carries a health condition, so `ApplyTo` refuses
 * it and the attribute stays at zero for a character holding the node. A test
 * that wrote the attribute would be testing a state the game never produces.
 */
namespace CataclysmNovaTest
{
	/** Centimetres in a metre, so the tests read like the node text does. */
	constexpr float NovaMetre = 100.0f;

	/**
	 * A combatant a nova can be released by or land on.
	 *
	 * Named apart from the harnesses in the neighbouring test files on purpose:
	 * the Unreal unity build concatenates these translation units, so two
	 * structs of one name in two files compile until both are clean and then
	 * collide.
	 *
	 * IT CARRIES COLLISION, because `UCataclysmTargeting::Gather` runs a sphere
	 * overlap on the Pawn object channel and a bare AActor has no collision at
	 * all. Without it the nova would look as though it struck nobody when what
	 * really happened is that nobody was there to be found.
	 */
	struct FNovaCombatant
	{
		explicit FNovaCombatant(UWorld* World,
								const FVector& Where = FVector::ZeroVector)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor));

			Vitals = NewVitals;
			Combat = NewCombat;
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Vitals->SetMaxHealth(1'000.0f);
			Vitals->SetHealth(1'000.0f);
		}

		~FNovaCombatant()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
	};

	/**
	 * Unstable Aura at its full eight points, as the passive tree delivers it:
	 * a flat 8 on the nova stat, applying only at or below a tenth of health.
	 */
	void GiveUnstableAura(FNovaCombatant& Who, float PerPoint = 1.0f,
						  int32 Points = 8)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Flat;
		// `PassiveKeystone` IS WHAT EVERY PASSIVE TREE MODIFIER USES, whatever
		// kind of node it came from. `UCataclysmPassiveTree` writes that source
		// for a basic node as well; the enumerator names the tree rather than
		// the node's kind, and only the More bucket reads it at all.
		Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
		Modifier.Value = PerPoint * Points;
		Modifier.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
		Modifier.ConditionValue = 10.0f;

		FCataclysmStatInputs Inputs;
		Inputs.Base = 0.0f;
		Inputs.Modifiers.Add(Modifier);

		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.Add(FName(UCataclysmNova::DamageStat), Inputs);
		Who.AbilitySystem->SetStatInputs(MoveTemp(Stats));
	}
}

#define CATACLYSM_NOVA_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_NOVA_TEST(FCataclysmNovaAmountTest,
	"Cataclysm.Nova.ItIsAShareOfWhatTheCharacterIsMissing")
{
	// EIGHT PER CENT OF WHAT IS MISSING, which is Unstable Aura at eight points.
	// A character with 1,000 maximum health standing at 80 is missing 920.
	TestEqual(TEXT("eight per cent of 920 missing"),
		UCataclysmNova::AmountFrom(8.0f, 80.0f, 1'000.0f), 73.6f, 0.01f);

	// AND IT GROWS AS THE CHARACTER IS HURT, which is what makes the node
	// strongest exactly where its own condition puts it. Half the health left is
	// less missing health, so a smaller nova.
	TestEqual(TEXT("and less at half health"),
		UCataclysmNova::AmountFrom(8.0f, 500.0f, 1'000.0f), 40.0f, 0.01f);

	// A CHARACTER AT FULL HEALTH IS MISSING NOTHING. Unreachable while the
	// node's own condition holds, and it is checked because the arithmetic must
	// not answer a negative or a nonsense figure at the boundary.
	TestEqual(TEXT("nothing missing is nothing dealt"),
		UCataclysmNova::AmountFrom(8.0f, 1'000.0f, 1'000.0f), 0.0f, 0.001f);

	// AND HEALTH ABOVE THE MAXIMUM DEALS NOTHING RATHER THAN HEALING. That is
	// reachable for an instant while an attribute set is being filled in.
	TestEqual(TEXT("health above the maximum is still nothing"),
		UCataclysmNova::AmountFrom(8.0f, 1'200.0f, 1'000.0f), 0.0f, 0.001f);

	// NO STAT IS NO NOVA, which is every character in the game.
	TestEqual(TEXT("no share is no damage"),
		UCataclysmNova::AmountFrom(0.0f, 80.0f, 1'000.0f), 0.0f, 0.001f);

	// AND NO MAXIMUM HEALTH ANSWERS NOTHING rather than treating the character
	// as infinitely hurt. An attribute set that has not been written yet reports
	// zero.
	TestEqual(TEXT("no maximum health is no damage"),
		UCataclysmNova::AmountFrom(8.0f, 0.0f, 0.0f), 0.0f, 0.001f);

	return true;
}

CATACLYSM_NOVA_TEST(FCataclysmNovaStrikesTest,
	"Cataclysm.Nova.ItStrikesEveryEnemyInRangeAndNoneOutsideIt")
{
	using namespace CataclysmNovaTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	constexpr float M = NovaMetre;

	FNovaCombatant Masochist(World, FVector::ZeroVector);
	FNovaCombatant Near(World, FVector(2 * M, 0.0f, 0.0f));
	FNovaCombatant Edge(World, FVector(5 * M, 0.0f, 0.0f));
	FNovaCombatant Outside(World, FVector(9 * M, 0.0f, 0.0f));

	GiveUnstableAura(Masochist);

	// AT FULL HEALTH THE NODE'S OWN CONDITION REFUSES IT, asserted first so
	// everything below is evidence of the node rather than of anything else the
	// step does.
	TestEqual(TEXT("a character at full health releases no nova"),
		UCataclysmNova::Step(Masochist.Actor), 0.0f, 0.001f);

	// AND AT A TWENTIETH OF ITS HEALTH IT DOES. Fifty out of a thousand is 5%,
	// which is at or below the node's 10%, and 950 is missing.
	Masochist.Vitals->SetHealth(50.0f);

	const float NearBefore = Near.Vitals->GetHealth();
	const float EdgeBefore = Edge.Vitals->GetHealth();
	const float OutsideBefore = Outside.Vitals->GetHealth();

	const float Dealt = UCataclysmNova::Step(Masochist.Actor);
	TestEqual(TEXT("and it is eight per cent of the 950 it is missing"),
		Dealt, 76.0f, 0.01f);

	// TWO METRES AND FIVE METRES ARE BOTH INSIDE SIX.
	TestTrue(TEXT("an enemy two metres away is struck"),
		Near.Vitals->GetHealth() < NearBefore);
	TestTrue(TEXT("and one five metres away is struck"),
		Edge.Vitals->GetHealth() < EdgeBefore);

	// NINE IS NOT.
	TestEqual(TEXT("and one nine metres away is not"),
		Outside.Vitals->GetHealth(), OutsideBefore, 0.001f);

	// THE CHARACTER DOES NOT STRIKE ITSELF. `FindEnemiesInSphere` never returns
	// the actor passed to it, and a nova that hurt its own caster would kill a
	// Masochist standing at 5% health outright.
	TestEqual(TEXT("and the character releasing it is unhurt by it"),
		Masochist.Vitals->GetHealth(), 50.0f, 0.001f);

	return true;
}

CATACLYSM_NOVA_TEST(FCataclysmNovaIntervalTest,
	"Cataclysm.Nova.OneComesEveryFiveSecondsAndNoOftener")
{
	using namespace CataclysmNovaTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FNovaCombatant Masochist(World, FVector::ZeroVector);
	FNovaCombatant Enemy(World, FVector(2 * NovaMetre, 0.0f, 0.0f));

	GiveUnstableAura(Masochist);
	Masochist.Vitals->SetHealth(50.0f);

	// THE FIRST NOVA COMES AS SOON AS THE CHARACTER IS HURT ENOUGH, rather than
	// five seconds after arriving there. A negative timestamp means none has
	// ever been released, so there is nothing to wait for.
	TestTrue(TEXT("the first nova is released at once"),
		UCataclysmNova::Step(Masochist.Actor) > 0.0f);

	// AND THE NEXT DOES NOT, ON THE SAME FRAME. The per-character step runs
	// several times a second, so without the interval a Masochist at low health
	// would release one on every one of them.
	TestEqual(TEXT("and a second on the same frame is refused"),
		UCataclysmNova::Step(Masochist.Actor), 0.0f, 0.001f);

	// THE INTERVAL IS FIVE SECONDS FROM THE ONE THAT WAS RELEASED, which is what
	// the timestamp records. Read rather than assumed, because the constant and
	// the recorded time are two different things and this is what ties them.
	//
	// THE SUBTRACTION IS CAST TO A FLOAT ON PURPOSE. `UWorld::GetTimeSeconds`
	// answers a double in Unreal 5, so the difference is a double and
	// `TestEqual` cannot choose between its float and its double overload.
	const float AllowedAt = Masochist.AbilitySystem->NovaAllowedAt();
	const float SecondsUntilTheNext =
		AllowedAt - static_cast<float>(World->GetTimeSeconds());
	TestEqual(TEXT("and the next is due five seconds after the last"),
		SecondsUntilTheNext, UCataclysmNova::IntervalSeconds, 0.01f);

	// AND THE ENEMY TOOK ONE NOVA AND NOT SEVERAL. The two refused calls above
	// dealt nothing, which is what makes the interval a rule rather than a
	// number nobody reads.
	const float AfterOne = Enemy.Vitals->GetHealth();
	UCataclysmNova::Step(Masochist.Actor);
	UCataclysmNova::Step(Masochist.Actor);
	TestEqual(TEXT("and two further steps deal nothing at all"),
		Enemy.Vitals->GetHealth(), AfterOne, 0.001f);

	return true;
}

CATACLYSM_NOVA_TEST(FCataclysmNovaReleasedWithNobodyNearTest,
	"Cataclysm.Nova.ItIsReleasedWithNobodyStandingInIt")
{
	using namespace CataclysmNovaTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// NOBODY ELSE IN THE WORLD AT ALL.
	FNovaCombatant Alone(World, FVector::ZeroVector);
	GiveUnstableAura(Alone);
	Alone.Vitals->SetHealth(50.0f);

	// "YOU RELEASE A NOVA EVERY 5 SECONDS" DOES NOT MAKE THE RELEASE DEPEND ON
	// THERE BEING A TARGET, so one goes out and the interval restarts. The other
	// reading would let a character save novas up while alone and fire one the
	// instant an enemy walked into range, which the sentence does not say.
	TestTrue(TEXT("a nova is released with nobody near"),
		UCataclysmNova::Step(Alone.Actor) > 0.0f);

	TestTrue(TEXT("and the interval started, so the next is not due yet"),
		Alone.AbilitySystem->NovaAllowedAt() > World->GetTimeSeconds());

	TestEqual(TEXT("and a second on the same frame is refused"),
		UCataclysmNova::Step(Alone.Actor), 0.0f, 0.001f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
