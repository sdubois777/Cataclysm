// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmContagion.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"

/**
 * A debuff passing from the character carrying it to an enemy. Issues #1057
 * and #1058.
 *
 * WHAT THESE GUARD. Two Masochist nodes:
 *
 *   Beacon of Despair     "You radiate an aura that applies a random debuff to
 *                          enemies within 6 metres every 3 seconds. The duration
 *                          of those debuffs is increased by 4% per point."
 *   Contagious Torment    "When a debuff on you deals damage, enemies within 6
 *                          metres have a 1% chance per point to receive a random
 *                          debuff you carry."
 *
 * THE ROLLS ARE PINNED, so a test never depends on a random number. Both
 * functions take the same negative-means-roll parameters
 * `UCataclysmDamageCalculation::Resolve` uses for evasion, blocking and critical
 * strikes. A test that let the chance roll for real would fail one run in
 * twelve and pass the rest, which is worse than no test.
 *
 * THE STATS ARE BUILT THE WAY `UCataclysmPlayerClassStats::ApplyTo` LEAVES
 * THEM -- as stat inputs on the ability system component -- rather than written
 * onto the gameplay attribute, so that the path a test exercises is the path the
 * game takes. Both nodes' rows are `flat` and carry no condition, so the two
 * would agree today; writing the inputs keeps that true if either row ever gains
 * one.
 */
namespace CataclysmContagionTest
{
	/** Centimetres in a metre, so these read like the node text does. */
	constexpr float ContagionMetre = 100.0f;

	/**
	 * A combatant that can carry a debuff or catch one.
	 *
	 * NAMED APART FROM THE HARNESSES IN THE NEIGHBOURING TEST FILES on purpose:
	 * the Unreal unity build concatenates these translation units, so two
	 * structs of one name in two files compile until both are clean and then
	 * collide.
	 *
	 * IT CARRIES COLLISION, because `UCataclysmTargeting::Gather` runs a sphere
	 * overlap on the Pawn object channel and a bare AActor has no collision at
	 * all. Without it the aura would look as though it reached nobody when what
	 * really happened is that nobody was there to be found.
	 */
	struct FContagionCombatant
	{
		explicit FContagionCombatant(UWorld* World,
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

			Vitals->SetMaxHealth(10'000.0f);
			Vitals->SetHealth(10'000.0f);
		}

		~FContagionCombatant()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Put a lasting effect on this character, as anything else would. */
		bool Catch(const FGameplayTag& Tag, float Seconds = 30.0f) const
		{
			return UCataclysmSkillEffects::ApplyTagForDuration(Actor, Actor, Tag,
															   Seconds);
		}

		bool Carries(const FGameplayTag& Tag) const
		{
			return Tag.IsValid()
				&& AbilitySystem->HasMatchingGameplayTag(Tag);
		}

		/**
		 * How long the longest effect running on this character lasts.
		 *
		 * READ OFF THE ACTIVE EFFECT RATHER THAN WAITED OUT. A world built by
		 * `UWorld::CreateWorld` is never ticked, so no duration can expire here
		 * however far the clock is pushed.
		 */
		float LongestEffect() const
		{
			float Longest = 0.0f;
			for (const float Seconds :
				 AbilitySystem->GetActiveEffectsDuration(FGameplayEffectQuery()))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
			return Longest;
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
	};

	/**
	 * Give a character one of the two nodes, as the passive tree delivers it:
	 * a `flat` modifier recorded as a stat input, with no condition.
	 */
	void GiveNode(const FContagionCombatant& Who, const TCHAR* Stat,
				  float PerPoint, int32 Points = 8)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Flat;
		// `PassiveKeystone` IS WHAT EVERY PASSIVE TREE MODIFIER USES, whatever
		// kind of node it came from. The enumerator names the tree rather than
		// the node's kind, and only the More bucket reads it at all.
		Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
		Modifier.Value = PerPoint * Points;

		FCataclysmStatInputs Inputs;
		Inputs.Base = 0.0f;
		Inputs.Modifiers.Add(Modifier);

		// WHOLESALE, WHICH IS WHAT `ApplyTo` DOES. A test giving a character two
		// nodes has to build both entries at once rather than call this twice,
		// and no test here needs both.
		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.Add(FName(Stat), Inputs);
		Who.AbilitySystem->SetStatInputs(MoveTemp(Stats));
	}

	FGameplayTag Tag(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Name), /*ErrorIfNotFound=*/false);
	}
}

#define CATACLYSM_CONTAGION_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Which effect a tag names
// ---------------------------------------------------------------------------

CATACLYSM_CONTAGION_TEST(FCataclysmContagionRowForTagTest,
	"Cataclysm.Contagion.ATagNamesTheStatusEffectRowItCameFrom")
{
	using namespace CataclysmContagionTest;
	using Effects = UCataclysmSkillEffects;

	// THE BRANCH DOES NOT DECIDE THE EFFECT, WHICH IS THE POINT OF THE RULE.
	// `Status.Bleed` and `Keyword.DoT.Bleed` are the same effect declared under
	// two vocabularies, and both have to find the one row.
	TestEqual(TEXT("the damage over time branch finds the bleed row"),
		Effects::StatusEffectRowForTag(Tag(TEXT("Keyword.DoT.Bleed"))),
		FName(TEXT("DoT_Bleed")));
	TestEqual(TEXT("and so does the status branch"),
		Effects::StatusEffectRowForTag(Tag(TEXT("Status.Bleed"))),
		FName(TEXT("DoT_Bleed")));

	// A NAME OF TWO WORDS, which is what the reduction rule is really for. The
	// row is `DoT_Void_Splinter`, its effect name is "Void Splinter", and the
	// tag is `VoidSplinter`. All three spell it differently.
	TestEqual(TEXT("a two-word effect name reduces to one tag segment"),
		Effects::StatusEffectRowForTag(Tag(TEXT("Keyword.DoT.VoidSplinter"))),
		FName(TEXT("DoT_Void_Splinter")));

	// A PURE DEBUFF, so the lookup is not quietly limited to the damage over
	// time sheet.
	TestEqual(TEXT("a debuff that is not damage over time is found too"),
		Effects::StatusEffectRowForTag(Tag(TEXT("Status.Cripple"))),
		FName(TEXT("Debuff_Cripple")));

	// AND THE ONE THAT DELIBERATELY FINDS NOTHING. A stunned character carries
	// `State.Stunned`, and the row is `Debuff_Stun`, whose name reduces to
	// `Stun`. So a character cannot pass its own stun on. See the header of
	// `UCataclysmContagion` for why that is the right outcome.
	TestEqual(TEXT("being stunned names no row, so a stun does not spread"),
		Effects::StatusEffectRowForTag(Tag(TEXT("State.Stunned"))), NAME_None);

	// A TAG THAT IS NOT AN EFFECT AT ALL, and an invalid one.
	TestEqual(TEXT("a keyword that is not an effect names no row"),
		Effects::StatusEffectRowForTag(Tag(TEXT("Keyword.Leech"))), NAME_None);
	TestEqual(TEXT("and neither does an invalid tag"),
		Effects::StatusEffectRowForTag(FGameplayTag()), NAME_None);

	return true;
}

// ---------------------------------------------------------------------------
// Which debuffs a character is carrying
// ---------------------------------------------------------------------------

CATACLYSM_CONTAGION_TEST(FCataclysmContagionTagsOnTest,
	"Cataclysm.Contagion.TheDebuffsACharacterCarriesAreListedNotJustCounted")
{
	using namespace CataclysmContagionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FContagionCombatant Carrier(World);

	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Burn = Tag(TEXT("Keyword.DoT.Burn"));
	const FGameplayTag Leech = Tag(TEXT("Keyword.Leech"));
	if (!TestTrue(TEXT("the vocabulary still has the three tags used here"),
				  Bleed.IsValid() && Burn.IsValid() && Leech.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("a character carrying nothing lists nothing"),
		UCataclysmDebuffs::TagsOn(Carrier.AbilitySystem).Num(), 0);

	Carrier.Catch(Bleed);
	Carrier.Catch(Burn);
	// AND SOMETHING THAT IS NOT A DEBUFF, so the list is shown to be filtered
	// rather than being every tag the character happens to hold.
	Carrier.Catch(Leech);

	const FGameplayTagContainer Listed =
		UCataclysmDebuffs::TagsOn(Carrier.AbilitySystem);

	TestTrue(TEXT("the bleed is listed"), Listed.HasTagExact(Bleed));
	TestTrue(TEXT("and the burn"), Listed.HasTagExact(Burn));
	TestFalse(TEXT("and the leech keyword is not, being no debuff"),
			  Listed.HasTagExact(Leech));

	// THE TWO ANSWERS AGREE, WHICH IS WHY `CountOn` NOW READS THIS LIST. Eleven
	// nodes pay the character per debuff carried and two spread one; the pair
	// disagreeing would be a defect nothing at run time would report.
	TestEqual(TEXT("the count is the length of the list"),
		UCataclysmDebuffs::CountOn(Carrier.AbilitySystem), Listed.Num());
	TestEqual(TEXT("and that is two"), Listed.Num(), 2);

	// NO ABILITY SYSTEM IS AN EMPTY LIST rather than a crash.
	TestEqual(TEXT("no ability system carries nothing"),
		UCataclysmDebuffs::TagsOn(nullptr).Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Putting one on somebody else
// ---------------------------------------------------------------------------

CATACLYSM_CONTAGION_TEST(FCataclysmContagionSpreadOneTest,
	"Cataclysm.Contagion.SpreadingPutsTheDesignedEffectOnTheTarget")
{
	using namespace CataclysmContagionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Stunned = Tag(TEXT("State.Stunned"));
	if (!TestTrue(TEXT("the vocabulary still has both tags used here"),
				  Bleed.IsValid() && Stunned.IsValid()))
	{
		return false;
	}

	// WHAT THE SHEET SAYS BLEEDING IS, READ RATHER THAN ASSUMED. Re-authoring
	// the DoT_Bleed row is how this test would quietly stop measuring anything.
	const FCataclysmStatusEffectNumbers Bleeding =
		UCataclysmSkillEffects::BleedNumbers();
	if (!TestTrue(TEXT("the bleed row states a duration and an amount"),
				  Bleeding.bUsable))
	{
		return false;
	}

	{
		FContagionCombatant Spreader(World);
		FContagionCombatant Victim(World, FVector(2 * ContagionMetre, 0.0f, 0.0f));

		TestTrue(TEXT("a bleed can be put on somebody"),
			UCataclysmContagion::SpreadOne(Spreader.Actor, Victim.Actor, Bleed));
		TestTrue(TEXT("and the target is now carrying it"),
				 Victim.Carries(Bleed));

		// THE ROW'S OWN DURATION, because Contagious Torment's sentence says
		// nothing about how long what it spreads lasts.
		TestEqual(TEXT("for the duration the sheet states"),
			Victim.LongestEffect(), Bleeding.DurationSeconds, 0.01f);
	}

	{
		// AND BEACON OF DESPAIR'S BONUS LENGTHENS IT. Eight points at 4% each is
		// 32% longer, which is what the node promises at full investment.
		FContagionCombatant Spreader(World);
		FContagionCombatant Victim(World, FVector(2 * ContagionMetre, 0.0f, 0.0f));

		TestTrue(TEXT("a lengthened bleed can be put on somebody"),
			UCataclysmContagion::SpreadOne(Spreader.Actor, Victim.Actor, Bleed,
										   /*ExtraDurationPercent=*/32.0f));
		TestEqual(TEXT("and it runs 32% longer than the sheet states"),
			Victim.LongestEffect(), Bleeding.DurationSeconds * 1.32f, 0.01f);
	}

	{
		// A STUN GOES NOWHERE, and it is the tag lookup that stops it rather
		// than a check written here. See the header of `UCataclysmContagion`.
		FContagionCombatant Spreader(World);
		FContagionCombatant Victim(World, FVector(2 * ContagionMetre, 0.0f, 0.0f));

		TestFalse(TEXT("a stun cannot be spread"),
			UCataclysmContagion::SpreadOne(Spreader.Actor, Victim.Actor,
										   Stunned));
		TestFalse(TEXT("so the target is not stunned"),
				  Victim.Carries(Stunned));
	}

	return true;
}

CATACLYSM_CONTAGION_TEST(FCataclysmContagionPickTest,
	"Cataclysm.Contagion.OnlyADebuffThatCouldLandIsEverChosen")
{
	using namespace CataclysmContagionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Stunned = Tag(TEXT("State.Stunned"));

	FContagionCombatant Carrier(World);

	TestFalse(TEXT("a character carrying nothing offers nothing"),
		UCataclysmContagion::PickSpreadable(Carrier.AbilitySystem).IsValid());

	// STUNNED AND ONLY STUNNED. `UCataclysmDebuffs` counts the stun as a debuff,
	// so this character carries one, and none of them can be passed on.
	Carrier.Catch(Stunned);
	TestEqual(TEXT("being stunned is still carrying a debuff"),
		UCataclysmDebuffs::CountOn(Carrier.AbilitySystem), 1);
	TestFalse(TEXT("but there is nothing to spread"),
		UCataclysmContagion::PickSpreadable(Carrier.AbilitySystem).IsValid());

	// AND NOW BLEEDING AS WELL. THE UNSPREADABLE ONE IS DROPPED BEFORE THE ROLL,
	// not after it, so the answer is the bleed every time rather than half the
	// time. Asked repeatedly with no pinned index, so a filter applied after the
	// roll would fail this within a few attempts rather than one run in many.
	Carrier.Catch(Bleed);
	for (int32 Attempt = 0; Attempt < 20; ++Attempt)
	{
		const FGameplayTag Chosen =
			UCataclysmContagion::PickSpreadable(Carrier.AbilitySystem);
		if (!TestEqual(
				*FString::Printf(
					TEXT("attempt %d chooses the bleed and not the stun"),
					Attempt),
				Chosen, Bleed))
		{
			return false;
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Beacon of Despair
// ---------------------------------------------------------------------------

CATACLYSM_CONTAGION_TEST(FCataclysmContagionAuraTest,
	"Cataclysm.Contagion.TheAuraReachesEveryEnemyInRangeAndNoneOutsideIt")
{
	using namespace CataclysmContagionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	constexpr float M = ContagionMetre;
	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));

	FContagionCombatant Masochist(World, FVector::ZeroVector);
	FContagionCombatant Near(World, FVector(2 * M, 0.0f, 0.0f));
	FContagionCombatant Edge(World, FVector(5 * M, 0.0f, 0.0f));
	FContagionCombatant Outside(World, FVector(9 * M, 0.0f, 0.0f));

	// CARRYING SOMETHING TO SPREAD, but no node yet.
	Masochist.Catch(Bleed);

	// WITHOUT THE NODE NOTHING HAPPENS AND THE CLOCK IS NEVER TOUCHED, which is
	// every character in the game. Asserted first, so everything below is
	// evidence of the node rather than of anything else the step does.
	TestEqual(TEXT("a character with no points in the node applies nothing"),
			  UCataclysmContagion::AuraStep(Masochist.Actor), 0);
	TestTrue(TEXT("and its aura clock was never started"),
			 Masochist.AbilitySystem->AuraAllowedAt() < 0.0f);
	TestFalse(TEXT("so nobody caught anything"), Near.Carries(Bleed));

	// EIGHT POINTS OF BEACON OF DESPAIR, which is 4% a point.
	GiveNode(Masochist, UCataclysmContagion::AuraDurationStat, 4.0f);

	TestEqual(TEXT("the aura reaches the two enemies inside six metres"),
			  UCataclysmContagion::AuraStep(Masochist.Actor), 2);
	TestTrue(TEXT("the near one caught it"), Near.Carries(Bleed));
	TestTrue(TEXT("and so did the one at five metres"), Edge.Carries(Bleed));
	TestFalse(TEXT("and the one at nine metres did not"),
			  Outside.Carries(Bleed));

	// LENGTHENED BY WHAT THE NODE PROMISES. Eight points at 4% is 32%.
	const FCataclysmStatusEffectNumbers Bleeding =
		UCataclysmSkillEffects::BleedNumbers();
	if (TestTrue(TEXT("the bleed row states a duration"), Bleeding.bUsable))
	{
		TestEqual(TEXT("and the bleed it applied runs 32% longer"),
				  Near.LongestEffect(), Bleeding.DurationSeconds * 1.32f, 0.01f);
	}

	// THE INTERVAL IS KEPT. This runs several times a second in the game, so a
	// second call must do nothing at all.
	TestEqual(TEXT("a second pulse in the same instant applies nothing"),
			  UCataclysmContagion::AuraStep(Masochist.Actor), 0);
	TestTrue(TEXT("and the next is three seconds away"),
			 Masochist.AbilitySystem->AuraAllowedAt()
				 >= World->GetTimeSeconds()
					 + UCataclysmContagion::AuraIntervalSeconds - 0.01f);

	return true;
}

CATACLYSM_CONTAGION_TEST(FCataclysmContagionAuraWithNothingToSpreadTest,
	"Cataclysm.Contagion.AnAuraOnACharacterCarryingNothingStillKeepsItsBeat")
{
	using namespace CataclysmContagionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FContagionCombatant Masochist(World, FVector::ZeroVector);
	FContagionCombatant Near(World, FVector(2 * ContagionMetre, 0.0f, 0.0f));

	GiveNode(Masochist, UCataclysmContagion::AuraDurationStat, 4.0f);

	// NOTHING TO SPREAD, SO NOTHING SPREADS. A Masochist that is not hurt yet
	// carries no debuff, and this is the ordinary state rather than a fault.
	TestEqual(TEXT("a character carrying no debuff applies none"),
			  UCataclysmContagion::AuraStep(Masochist.Actor), 0);

	// AND THE BEAT STARTED ANYWAY. Without this the aura would fire the instant
	// the character caught fire rather than on its next three second beat, and a
	// character in and out of a burning patch would pulse far faster than the
	// node says.
	TestTrue(TEXT("but the three second clock started"),
			 Masochist.AbilitySystem->AuraAllowedAt() > World->GetTimeSeconds());

	return true;
}

// ---------------------------------------------------------------------------
// Contagious Torment
// ---------------------------------------------------------------------------

CATACLYSM_CONTAGION_TEST(FCataclysmContagionTormentTest,
	"Cataclysm.Contagion.ATickSpreadsToANearbyEnemyWhenTheRollSucceeds")
{
	using namespace CataclysmContagionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	constexpr float M = ContagionMetre;
	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));

	FContagionCombatant Masochist(World, FVector::ZeroVector);
	FContagionCombatant Near(World, FVector(2 * M, 0.0f, 0.0f));
	FContagionCombatant Outside(World, FVector(9 * M, 0.0f, 0.0f));

	Masochist.Catch(Bleed);

	// WITHOUT THE NODE, A ROLL OF ZERO STILL SPREADS NOTHING. Zero beats every
	// chance there is, so this says the refusal is the missing node rather than
	// an unlucky roll.
	TestEqual(TEXT("no points in the node spreads nothing on a certain roll"),
		UCataclysmContagion::SpreadOnDebuffDamage(Masochist.Actor,
												  /*PinnedRoll=*/0.0f), 0);
	TestFalse(TEXT("so the near enemy caught nothing"), Near.Carries(Bleed));

	// EIGHT POINTS OF CONTAGIOUS TORMENT, which is 1% a point, so 8%.
	GiveNode(Masochist, UCataclysmContagion::TormentChanceStat, 1.0f);

	// A ROLL ABOVE THE CHANCE MISSES. 50 against 8.
	TestEqual(TEXT("a roll above the chance spreads nothing"),
		UCataclysmContagion::SpreadOnDebuffDamage(Masochist.Actor,
												  /*PinnedRoll=*/50.0f), 0);
	TestFalse(TEXT("and the near enemy still caught nothing"),
			  Near.Carries(Bleed));

	// AND ONE BELOW IT LANDS. 1 against 8.
	TestEqual(TEXT("a roll under the chance spreads to the enemy in range"),
		UCataclysmContagion::SpreadOnDebuffDamage(Masochist.Actor,
												  /*PinnedRoll=*/1.0f), 1);
	TestTrue(TEXT("the near enemy is now bleeding"), Near.Carries(Bleed));
	TestFalse(TEXT("and the one at nine metres is not"),
			  Outside.Carries(Bleed));

	// AT THE ROW'S OWN DURATION, because this node's sentence says nothing about
	// lengthening what it spreads. Only Beacon of Despair does.
	const FCataclysmStatusEffectNumbers Bleeding =
		UCataclysmSkillEffects::BleedNumbers();
	if (TestTrue(TEXT("the bleed row states a duration"), Bleeding.bUsable))
	{
		TestEqual(TEXT("and it is not lengthened"), Near.LongestEffect(),
				  Bleeding.DurationSeconds, 0.01f);
	}

	// NO INTERVAL OF ITS OWN. Every tick rolls again, which is what "when a
	// debuff on you deals damage" says, so an immediate second call works where
	// the aura's second pulse does not.
	TestEqual(TEXT("a second tick rolls again with no waiting"),
		UCataclysmContagion::SpreadOnDebuffDamage(Masochist.Actor,
												  /*PinnedRoll=*/1.0f), 1);

	return true;
}


CATACLYSM_CONTAGION_TEST(FCataclysmContagionRealTickTest,
	"Cataclysm.Contagion.ARealDamageOverTimeTickSpreadsWithoutAnybodyCallingIt")
{
	using namespace CataclysmContagionTest;

	// THE ONE TEST HERE THAT CALLS NOTHING IN `UCataclysmContagion`. Every other
	// test in this file calls `SpreadOnDebuffDamage` itself, which proves what
	// the function does and says nothing about whether the game ever reaches it.
	// Issue #1054 was exactly that mistake: twenty tests of the passive tree all
	// performed the step the game was missing, so a spent point changed nothing
	// and every one of them passed.
	//
	// SO THIS ONE LANDS A REAL BLOW MARKED AS DAMAGE OVER TIME and then looks at
	// the bystander. `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` is
	// the only place that knows a tick landed, and this is what says the call is
	// really in it.
	//
	// A DIRECT HIT WITH THE FLAG SET RATHER THAN A REAL BURN LEFT TO TICK. A
	// world built by `UWorld::CreateWorld` is never ticked, so a periodic effect
	// applied here would never fire. What arrives at the attribute set is the
	// same either way: one execution carrying the damage over time tag.
	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	constexpr float M = ContagionMetre;
	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));

	FContagionCombatant Masochist(World, FVector::ZeroVector);
	FContagionCombatant Bystander(World, FVector(2 * M, 0.0f, 0.0f));

	// THE MASOCHIST IS BLEEDING, so it has something to pass on.
	Masochist.Catch(Bleed);

	// AND HOLDS CONTAGIOUS TORMENT AT A CERTAINTY RATHER THAN AT 8%. The roll
	// inside the attribute set cannot be pinned from here -- nothing passes a
	// roll through `PostGameplayEffectExecute` -- so the chance is set to 100 and
	// the node's real 1% a point is what the pinned-roll test above measures.
	// This test is about the wiring and not about the arithmetic.
	GiveNode(Masochist, UCataclysmContagion::TormentChanceStat,
			 /*PerPoint=*/100.0f, /*Points=*/1);

	TestFalse(TEXT("the bystander is not bleeding to begin with"),
			  Bystander.Carries(Bleed));

	// A BLOW ON THE MASOCHIST, MARKED AS DAMAGE OVER TIME. This is what a burn
	// tick is by the time it reaches the attribute set.
	FCataclysmHitDelivery AsATick;
	AsATick.bIsDamageOverTime = true;
	// AREA AS WELL, so evasion cannot make this test fail one run in twenty.
	AsATick.bIsArea = true;

	TestTrue(TEXT("the tick took health off the Masochist"),
		UCataclysmSkillEffects::ApplyDirectDamage(Bystander.Actor,
												  Masochist.Actor, 100.0f,
												  AsATick));

	// AND THE BYSTANDER CAUGHT IT WITHOUT THIS TEST ASKING FOR ANY OF IT.
	TestTrue(TEXT("the enemy standing two metres away is now bleeding"),
			 Bystander.Carries(Bleed));

	// THE CONTROL, AND IT IS NOT DECORATION. Without it this test would pass
	// just as happily if every hit spread a debuff, node or no node.
	FContagionCombatant Ordinary(World, FVector(0.0f, 4 * M, 0.0f));
	FContagionCombatant Untouched(World, FVector(0.0f, 6 * M, 0.0f));
	Ordinary.Catch(Bleed);

	TestTrue(TEXT("a character with no points in the node still takes the tick"),
		UCataclysmSkillEffects::ApplyDirectDamage(Untouched.Actor,
												  Ordinary.Actor, 100.0f,
												  AsATick));
	TestFalse(TEXT("and nothing spreads from it"), Untouched.Carries(Bleed));

	// AND AN ORDINARY BLOW SPREADS NOTHING EITHER, which is the other half of
	// the sentence: "when a debuff on you deals damage", not when anything does.
	FCataclysmHitDelivery AsAnOrdinaryHit;
	AsAnOrdinaryHit.bIsArea = true;

	FContagionCombatant Struck(World, FVector(0.0f, 0.0f, 8 * M));
	FContagionCombatant Nearby(World, FVector(0.0f, 2 * M, 8 * M));
	Struck.Catch(Bleed);
	GiveNode(Struck, UCataclysmContagion::TormentChanceStat,
			 /*PerPoint=*/100.0f, /*Points=*/1);

	TestTrue(TEXT("an ordinary blow lands on the character holding the node"),
		UCataclysmSkillEffects::ApplyDirectDamage(Nearby.Actor, Struck.Actor,
												  100.0f, AsAnOrdinaryHit));
	TestFalse(TEXT("and it spreads nothing, not being damage over time"),
			  Nearby.Carries(Bleed));

	return true;
}
#endif // WITH_AUTOMATION_TESTS
