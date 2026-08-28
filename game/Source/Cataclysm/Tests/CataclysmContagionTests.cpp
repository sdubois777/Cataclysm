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
 * What a debuff does to the enemies around whoever is carrying it.
 *
 * WHAT THESE GUARD. The four Masochist nodes of the Flagellant branch, which
 * between them finished that tree:
 *
 *   Beacon of Despair     "You radiate an aura that applies a random debuff to
 *                          enemies within 6 metres every 3 seconds. The duration
 *                          of those debuffs is increased by 4% per point."
 *   Contagious Torment    "When a debuff on you deals damage, enemies within 6
 *                          metres have a 1% chance per point to receive a random
 *                          debuff you carry."
 *   Empathic Link         "When an enemy dies, its debuffs have a 2% chance per
 *                          point to pass to a random enemy within 6 metres."
 *   Wound Channeling      "...you deal 1% increased damage per point to enemies
 *                          carrying a debuff you also carry."
 *
 * THREE OF THEM SPREAD ONE AND THE FOURTH COMPARES TWO, which is why Wound
 * Channeling's tests are here rather than beside `UCataclysmDebuffs`: they are
 * about the same branch of the tree and use the same harness.
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

// ---------------------------------------------------------------------------
// Empathic Link
// ---------------------------------------------------------------------------

CATACLYSM_CONTAGION_TEST(FCataclysmContagionOnDeathTest,
	"Cataclysm.Contagion.ADyingCreaturesDebuffsPassToOneOfTheOnesStandingByIt")
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

	// THE KILLER STANDS WELL AWAY FROM THE BODY, deliberately. The node says
	// "within 6 metres" of the dying enemy, not of the character holding it, and
	// a search centred on the player instead would find nobody from here.
	FContagionCombatant Masochist(World, FVector(0.0f, 0.0f, 40 * M));
	FContagionCombatant Dying(World, FVector::ZeroVector);
	FContagionCombatant Beside(World, FVector(2 * M, 0.0f, 0.0f));
	FContagionCombatant Away(World, FVector(9 * M, 0.0f, 0.0f));

	// THE BODY IS CARRYING SOMETHING, and it is the BODY'S debuff that passes.
	// The Masochist carries nothing at all here, which is what says this node
	// reads the dying creature rather than the character holding it.
	Dying.Catch(Bleed);
	TestEqual(TEXT("the Masochist itself is carrying nothing"),
		UCataclysmDebuffs::CountOn(Masochist.AbilitySystem), 0);

	// MARKED DEAD, WHICH IS WHAT `HandleDeath` DOES BEFORE IT REACHES THIS.
	// Without it the corpse is found by its own search and can catch its own
	// debuff back.
	UCataclysmSkillEffects::MarkDead(Dying.Actor);

	// WITHOUT THE NODE, A CERTAIN ROLL STILL PASSES NOTHING.
	TestEqual(TEXT("no points in the node passes nothing on a certain roll"),
		UCataclysmContagion::SpreadOnDeath(Dying.Actor, Masochist.Actor,
										   /*PinnedRoll=*/0.0f), 0);
	TestFalse(TEXT("so the creature beside the body caught nothing"),
			  Beside.Carries(Bleed));

	// EIGHT POINTS OF EMPATHIC LINK, which is 2% a point, so 16%.
	GiveNode(Masochist, UCataclysmContagion::DeathChanceStat, 2.0f);

	// A ROLL ABOVE THE CHANCE MISSES. 50 against 16.
	TestEqual(TEXT("a roll above the chance passes nothing"),
		UCataclysmContagion::SpreadOnDeath(Dying.Actor, Masochist.Actor,
										   /*PinnedRoll=*/50.0f), 0);
	TestFalse(TEXT("and the creature beside the body still caught nothing"),
			  Beside.Carries(Bleed));

	// AND ONE BELOW IT PASSES. 1 against 16. The body carries one debuff, so one
	// passes.
	TestEqual(TEXT("a roll under the chance passes the one debuff it carried"),
		UCataclysmContagion::SpreadOnDeath(Dying.Actor, Masochist.Actor,
										   /*PinnedRoll=*/1.0f), 1);
	TestTrue(TEXT("the creature two metres from the body is now bleeding"),
			 Beside.Carries(Bleed));
	TestFalse(TEXT("and the one nine metres away is not"), Away.Carries(Bleed));

	// AND THE BODY DID NOT CATCH ITS OWN DEBUFF BACK. It is already carrying
	// one, so this asks the count instead: a second application would refresh
	// rather than add, and the count would stay at one either way. What says it
	// was never a candidate is that exactly one creature caught something, which
	// the return value above already asserted.
	TestEqual(TEXT("the body still carries the one debuff it died with"),
		UCataclysmDebuffs::CountOn(Dying.AbilitySystem), 1);

	return true;
}

CATACLYSM_CONTAGION_TEST(FCataclysmContagionOnDeathPerDebuffTest,
	"Cataclysm.Contagion.EachDebuffOnTheBodyIsRolledForSeparately")
{
	using namespace CataclysmContagionTest;

	// THE SHAPE THAT MAKES THIS NODE DIFFERENT FROM ITS TWO NEIGHBOURS. Beacon
	// of Despair picks one debuff and gives it to everybody; Contagious Torment
	// rolls once per enemy; this rolls once per DEBUFF, because the sentence
	// says "its debuffs have a 2% chance per point to pass".
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	constexpr float M = ContagionMetre;
	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Burn = Tag(TEXT("Keyword.DoT.Burn"));

	FContagionCombatant Masochist(World, FVector(0.0f, 0.0f, 40 * M));
	FContagionCombatant Dying(World, FVector::ZeroVector);
	FContagionCombatant Beside(World, FVector(2 * M, 0.0f, 0.0f));

	// TWO DEBUFFS ON THE BODY, AND A STUN THAT CANNOT TRAVEL. The stun is here
	// so the count below is not simply "everything it carried": a body carrying
	// three things passes two, because `State.Stunned` names no status effect
	// row.
	Dying.Catch(Bleed);
	Dying.Catch(Burn);
	Dying.Catch(Tag(TEXT("State.Stunned")));
	TestEqual(TEXT("the body carries three debuffs"),
		UCataclysmDebuffs::CountOn(Dying.AbilitySystem), 3);

	UCataclysmSkillEffects::MarkDead(Dying.Actor);
	GiveNode(Masochist, UCataclysmContagion::DeathChanceStat, 2.0f);

	TestEqual(TEXT("two of the three pass, the stun being unable to"),
		UCataclysmContagion::SpreadOnDeath(Dying.Actor, Masochist.Actor,
										   /*PinnedRoll=*/1.0f), 2);
	TestTrue(TEXT("the neighbour caught the bleed"), Beside.Carries(Bleed));
	TestTrue(TEXT("and the burn"), Beside.Carries(Burn));
	TestFalse(TEXT("and is not stunned"),
			  Beside.Carries(Tag(TEXT("State.Stunned"))));

	return true;
}

// ---------------------------------------------------------------------------
// Wound Channeling
// ---------------------------------------------------------------------------

CATACLYSM_CONTAGION_TEST(FCataclysmContagionSharedDebuffTest,
	"Cataclysm.Contagion.SharingADebuffIsExactAndNotByTheParentBranch")
{
	using namespace CataclysmContagionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Burn = Tag(TEXT("Keyword.DoT.Burn"));

	FContagionCombatant Masochist(World);
	FContagionCombatant Bleeding(World, FVector(2 * ContagionMetre, 0.0f, 0.0f));
	FContagionCombatant Burning(World, FVector(4 * ContagionMetre, 0.0f, 0.0f));
	FContagionCombatant Unhurt(World, FVector(6 * ContagionMetre, 0.0f, 0.0f));

	Bleeding.Catch(Bleed);
	Burning.Catch(Burn);

	// NEITHER SIDE CARRYING ANYTHING SHARES NOTHING, rather than sharing
	// vacuously.
	TestFalse(TEXT("a character carrying nothing shares nothing"),
		UCataclysmDebuffs::ShareADebuff(Masochist.AbilitySystem, Unhurt.Actor));
	TestFalse(TEXT("nor with a bleeding enemy, while it carries nothing itself"),
		UCataclysmDebuffs::ShareADebuff(Masochist.AbilitySystem,
										Bleeding.Actor));

	Masochist.Catch(Bleed);

	TestTrue(TEXT("a bleeding character shares with a bleeding enemy"),
		UCataclysmDebuffs::ShareADebuff(Masochist.AbilitySystem,
										Bleeding.Actor));

	// THE ASSERTION THE WHOLE FUNCTION EXISTS FOR. Bleeding and burning are both
	// `Keyword.DoT`, and the engine counts a tag against its parents, so any
	// looser comparison would call these two a match and pay the node for any
	// two harmful effects at all.
	TestFalse(TEXT("but a bleeding character does not share with a burning one"),
		UCataclysmDebuffs::ShareADebuff(Masochist.AbilitySystem,
										Burning.Actor));

	TestFalse(TEXT("nor with an enemy carrying nothing"),
		UCataclysmDebuffs::ShareADebuff(Masochist.AbilitySystem, Unhurt.Actor));

	return true;
}

CATACLYSM_CONTAGION_TEST(FCataclysmContagionSharedDebuffDamageTest,
	"Cataclysm.Contagion.SharingADebuffIsWorthTheNodesOwnPercentage")
{
	using namespace CataclysmContagionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Burn = Tag(TEXT("Keyword.DoT.Burn"));

	FContagionCombatant Masochist(World);
	FContagionCombatant Bleeding(World, FVector(2 * ContagionMetre, 0.0f, 0.0f));
	FContagionCombatant Burning(World, FVector(4 * ContagionMetre, 0.0f, 0.0f));

	Masochist.Catch(Bleed);
	Bleeding.Catch(Bleed);
	Burning.Catch(Burn);

	// WITHOUT THE NODE IT IS WORTH NOTHING EVEN WHERE A DEBUFF IS SHARED, which
	// is every character in the game.
	TestEqual(TEXT("no points in the node is no bonus"),
		UCataclysmDebuffs::DamageAgainstSharedDebuff(Masochist.AbilitySystem,
													 Bleeding.Actor),
		0.0f, 0.0001f);

	// EIGHT POINTS OF WOUND CHANNELING, which is 1% a point, so 8%.
	GiveNode(Masochist, UCataclysmDebuffs::SharedDebuffDamageStat, 1.0f);

	// A FRACTION AND NOT A PERCENTAGE, because the caller adds it into the
	// increases bracket, which is a sum of fractions. Eight per cent is 0.08.
	TestEqual(TEXT("eight points against a shared debuff is 0.08"),
		UCataclysmDebuffs::DamageAgainstSharedDebuff(Masochist.AbilitySystem,
													 Bleeding.Actor),
		0.08f, 0.0001f);

	TestEqual(TEXT("and nothing against an enemy suffering something else"),
		UCataclysmDebuffs::DamageAgainstSharedDebuff(Masochist.AbilitySystem,
													 Burning.Actor),
		0.0f, 0.0001f);

	return true;
}

CATACLYSM_CONTAGION_TEST(FCataclysmContagionSharedDebuffReachesARealHitTest,
	"Cataclysm.Contagion.ARealHitIsBiggerAgainstAnEnemySharingADebuff")
{
	using namespace CataclysmContagionTest;

	// THE SECOND TEST HERE THAT PROVES A WIRING RATHER THAN A FUNCTION. The
	// three above call `UCataclysmDebuffs` directly, which says what the
	// comparison answers and nothing about whether a blow ever asks it. This
	// one strikes a real blow and reads what it was worth.
	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Burn = Tag(TEXT("Keyword.DoT.Burn"));

	FContagionCombatant Masochist(World);
	FContagionCombatant Bleeding(World, FVector(2 * ContagionMetre, 0.0f, 0.0f));
	FContagionCombatant Burning(World, FVector(4 * ContagionMetre, 0.0f, 0.0f));

	// SOMETHING TO SWING WITH. A character with no weapon damage deals nothing
	// however many increases it carries, and `ApplyHit` says so in the log.
	Masochist.Combat->SetAttackDamage(1'000.0f);

	Masochist.Catch(Bleed);
	Bleeding.Catch(Bleed);
	Burning.Catch(Burn);

	// AREA, SO EVASION CANNOT MAKE THIS FAIL ONE RUN IN TWENTY.
	FCataclysmHitDelivery Unevadable;
	Unevadable.bIsArea = true;

	const float BeforeShared = UCataclysmSkillEffects::ApplyHit(
		Masochist.Actor, Bleeding.Actor, 100.0f, FGameplayTagContainer(),
		Unevadable);
	const float BeforeOther = UCataclysmSkillEffects::ApplyHit(
		Masochist.Actor, Burning.Actor, 100.0f, FGameplayTagContainer(),
		Unevadable);

	// THE TWO BLOWS ARE THE SAME SIZE WITHOUT THE NODE, which is the baseline
	// everything below is measured against. Asserted rather than assumed,
	// because the two targets differ in what they are suffering from and this
	// test would read as evidence of the node if they differed for any reason.
	if (!TestEqual(TEXT("without the node both blows are the same size"),
				   BeforeShared, BeforeOther, 0.01f))
	{
		return false;
	}
	if (!TestTrue(TEXT("and both are a real number rather than zero"),
				  BeforeShared > 0.0f))
	{
		return false;
	}

	GiveNode(Masochist, UCataclysmDebuffs::SharedDebuffDamageStat, 1.0f);

	const float AfterShared = UCataclysmSkillEffects::ApplyHit(
		Masochist.Actor, Bleeding.Actor, 100.0f, FGameplayTagContainer(),
		Unevadable);
	const float AfterOther = UCataclysmSkillEffects::ApplyHit(
		Masochist.Actor, Burning.Actor, 100.0f, FGameplayTagContainer(),
		Unevadable);

	// EIGHT PER CENT MORE AGAINST THE ONE SHARING A DEBUFF.
	TestEqual(*FString::Printf(
				  TEXT("the blow against the bleeding enemy grew from %.1f to "
					   "%.1f, which is eight per cent"),
				  BeforeShared, AfterShared),
			  AfterShared, BeforeShared * 1.08f, BeforeShared * 0.001f);

	// AND NOTHING AGAINST THE OTHER, which is what says the bonus is decided by
	// the target rather than being an increase the character carries everywhere.
	TestEqual(TEXT("and the blow against the burning enemy did not change"),
			  AfterOther, BeforeOther, BeforeOther * 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// Two different damage over time effects at once
// ---------------------------------------------------------------------------

CATACLYSM_CONTAGION_TEST(FCataclysmContagionTwoDamageOverTimesTest,
	"Cataclysm.Contagion.ACharacterCanBleedAndBurnAtTheSameTime")
{
	using namespace CataclysmContagionTest;

	/**
	 * Issue #1062, found by the Empathic Link test below failing on its second
	 * debuff.
	 *
	 * WHAT WAS WRONG. `UCataclysmSkillEffects::ApplyDamageOverTime` built every
	 * effect under one name, `CataclysmDamageOverTime`, while
	 * `MakeSingleStackTagged` set a stack limit of one aggregated by target. The
	 * single-stack rule -- which is right, and is the design's own -- therefore
	 * applied ACROSS different effects instead of within one: setting a
	 * character alight while it was bleeding replaced the bleed, and both
	 * applications reported success.
	 *
	 * IT IS HERE RATHER THAN IN `CataclysmDebuffTests.cpp` because that file's
	 * "bleeding and burning counts as two" test uses `ApplyTagForDuration`,
	 * which never had the fault, and moving it would make that file's harness
	 * carry an attacker it does not otherwise need.
	 *
	 * THE SAME EFFECT TWICE IS STILL ONE STACK, which is the half that must not
	 * break while the other is fixed.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to stand in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Burn = Tag(TEXT("Keyword.DoT.Burn"));

	FContagionCombatant Attacker(World);
	FContagionCombatant Suffering(World, FVector(2 * ContagionMetre, 0.0f, 0.0f));

	TestTrue(TEXT("a bleed is applied"),
		UCataclysmSkillEffects::ApplyDamageOverTime(
			Attacker.Actor, Suffering.Actor, /*DamagePerTick=*/10.0f,
			/*DurationSeconds=*/10.0f, Bleed));
	TestTrue(TEXT("and then a burn"),
		UCataclysmSkillEffects::ApplyDamageOverTime(
			Attacker.Actor, Suffering.Actor, /*DamagePerTick=*/10.0f,
			/*DurationSeconds=*/10.0f, Burn));

	// BOTH, WHICH IS THE POINT. Before issue #1062 the second replaced the
	// first, so one of these two was false and the count below was 1.
	TestTrue(TEXT("the character is bleeding"), Suffering.Carries(Bleed));
	TestTrue(TEXT("and burning"), Suffering.Carries(Burn));
	TestEqual(TEXT("and carries two debuffs, which eleven nodes are paid for"),
		UCataclysmDebuffs::CountOn(Suffering.AbilitySystem), 2);

	// AND THE SINGLE-STACK RULE STILL HOLDS WITHIN ONE EFFECT. A second bleed
	// refreshes the first rather than adding a second, which the design requires
	// of everything a player applies. Without this half the repair would have
	// traded one fault for a worse one.
	TestTrue(TEXT("a second bleed is applied"),
		UCataclysmSkillEffects::ApplyDamageOverTime(
			Attacker.Actor, Suffering.Actor, /*DamagePerTick=*/10.0f,
			/*DurationSeconds=*/10.0f, Bleed));
	TestEqual(TEXT("and the character still carries exactly two"),
		UCataclysmDebuffs::CountOn(Suffering.AbilitySystem), 2);

	return true;
}
#endif // WITH_AUTOMATION_TESTS
