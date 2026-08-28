// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For the stat saying how long a lasting effect on this character runs.
// Issue #1033.
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Counting the harmful effects a character is under. Issue #962.
 *
 * WHAT IS BEING PINNED. Four Masochist nodes grant a bonus "for each unique
 * debuff on you" and a fifth applies "While you are Bleeding", so the count and
 * the named kind are both load-bearing numbers rather than diagnostics. A count
 * that is one too high hands every one of those nodes more than it promises, and
 * nothing at run time would report it: the arithmetic runs and the number is
 * simply larger.
 *
 * THE TESTS BREAK IN BOTH DIRECTIONS, WHICH IS THE POINT. A test that only
 * asserts "a bleeding character counts one" passes against a build that counts
 * every tag anything ever attached, so each case below has a partner asserting
 * that something is NOT counted. `State.StunImmune` is the sharpest of those: it
 * is attached to the target by the same call that stuns it, and it protects the
 * character.
 *
 * A PLAIN ACTOR AND NOT A CHARACTER, the choice `CataclysmDamageConversionTests`
 * makes and for the reason it gives: a character brings a movement component, a
 * regeneration timer and a death path with it, and none of that is what these
 * are about.
 */
namespace CataclysmDebuffTest
{
	using Debuffs = UCataclysmDebuffs;
	using Effects = UCataclysmSkillEffects;

	FGameplayTag TagNamed(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Name), /*ErrorIfNotFound=*/false);
	}

	/** An actor with an ability system, which is all a tag needs to sit on. */
	struct FScopedCarrier
	{
		explicit FScopedCarrier(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointer on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);

			// AND A COMBAT SET, WHICH IS WHERE THE DURATION STAT LIVES. Issue
			// #1033. It changes nothing for the counting tests above: the stat
			// starts at 100, which means unchanged, and a carrier without the
			// set would have its durations left alone anyway.
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000.0f);
		}

		~FScopedCarrier()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/**
		 * Attach a lasting effect granting this tag, the way a real one does.
		 *
		 * THROUGH `ApplyTagForDuration` AND NOT BY WRITING THE TAG, deliberately.
		 * Writing it with `AddLooseGameplayTag` would test a route nothing in the
		 * game uses; every lasting effect this project applies arrives as a
		 * gameplay effect granting a tag through a
		 * `UTargetTagsGameplayEffectComponent`, and the count has to read that.
		 */
		bool Attach(const FGameplayTag& Tag, float Seconds = 30.0f) const
		{
			return Effects::ApplyTagForDuration(Actor, Actor, Tag, Seconds);
		}

		int32 Count() const { return Debuffs::CountOn(AbilitySystem); }

		/** Give this character the stat two Masochist nodes grant. #1033. */
		void SetDurationStat(float Percent) const
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetDebuffDurationTakenAttribute(),
				Percent);
		}

		/**
		 * How long the longest effect running on this character lasts.
		 *
		 * READ OFF THE ACTIVE EFFECT RATHER THAN WAITED OUT. A world built by
		 * `UWorld::CreateWorld` is never ticked, so no duration can expire here
		 * however far the clock is pushed -- the same reason the expiry test
		 * above removes its effect instead of waiting.
		 *
		 * THE LONGEST RATHER THAN THE ONLY ONE, because applying a stun also
		 * applies a longer stun immunity beside it, and the caller wants the
		 * one it asked for. Each test here uses a fresh carrier so nothing else
		 * is running on it.
		 */
		float LongestEffect() const
		{
			float Longest = 0.0f;
			for (float Seconds :
				 AbilitySystem->GetActiveEffectsDuration(FGameplayEffectQuery()))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
			return Longest;
		}

		bool IsBleeding() const { return Debuffs::IsBleeding(AbilitySystem); }

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

#define CATACLYSM_DEBUFF_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// What is counted
// ---------------------------------------------------------------------------

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffCountTest,
	"Cataclysm.Debuffs.EachDistinctHarmfulEffectCountsOnce")
{
	using namespace CataclysmDebuffTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Character(World);

	// A CHARACTER NOTHING HAS TOUCHED CARRIES NOTHING. This is where every
	// character starts, and it is what makes the four scaling nodes a reward for
	// being hurt rather than a flat bonus.
	TestEqual(TEXT("an untouched character carries no debuffs"),
		Character.Count(), 0);

	const FGameplayTag Bleed = Debuffs::BleedTag();
	const FGameplayTag Burn = Effects::BurnTag();
	if (!TestTrue(TEXT("the vocabulary has Bleed and Burn"),
				  Bleed.IsValid() && Burn.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("a bleed can be attached"), Character.Attach(Bleed));
	TestEqual(TEXT("bleeding is one debuff"), Character.Count(), 1);

	// TWO KINDS AT ONCE IS TWO, which is what makes the nodes worth taking.
	TestTrue(TEXT("a burn can be attached too"), Character.Attach(Burn));
	TestEqual(TEXT("bleeding and burning is two debuffs"), Character.Count(), 2);

	// AND THE SAME KIND TWICE IS STILL ONE, WHICH IS WHAT "UNIQUE" MEANS. Every
	// lasting effect this project applies is aggregated by target and limited to
	// a single stack, so a second bleed refreshes the first. A count that grew
	// here would let a character standing in two burning patches claim the bonus
	// for two separate ailments.
	TestTrue(TEXT("a second bleed can be attached"), Character.Attach(Bleed));
	TestEqual(TEXT("a second bleed is still one bleed"), Character.Count(), 2);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffParentTagTest,
	"Cataclysm.Debuffs.ATagIsCountedOnceAndNotAlsoAsItsParents")
{
	using namespace CataclysmDebuffTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Character(World);

	const FGameplayTag Bleed = Debuffs::BleedTag();
	const FGameplayTag AnyDamageOverTime =
		UCataclysmDamageCalculation::DamageOverTimeTag();
	if (!TestTrue(TEXT("the vocabulary has both"),
				  Bleed.IsValid() && AnyDamageOverTime.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("a bleed can be attached"), Character.Attach(Bleed));

	// THE ENGINE COUNTS A TAG AGAINST ITS PARENTS, and that is exactly the trap
	// this test exists for. A character carrying `Keyword.DoT.Bleed` answers yes
	// to `Keyword.DoT` as well, so a count built by asking about every known
	// debuff tag would report one bleed as two -- and as three the moment the
	// branch grew another level. Both halves are asserted so that neither the
	// engine rule nor the counting rule can change without this failing.
	TestTrue(TEXT("a bleeding character answers yes to Keyword.DoT"),
		Character.AbilitySystem->HasMatchingGameplayTag(AnyDamageOverTime));
	TestEqual(TEXT("and still carries exactly one debuff"),
		Character.Count(), 1);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffIgnoresHelpfulTagsTest,
	"Cataclysm.Debuffs.SomethingThatHelpsTheCharacterIsNotCounted")
{
	using namespace CataclysmDebuffTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Character(World);

	// STUN IMMUNITY IS THE CASE THAT DECIDED THE SHAPE OF THE WHOLE COUNTER.
	// `UCataclysmSkillEffects::ApplyStun` attaches `State.Stunned` and
	// `State.StunImmune` to the same target in the same call, and the second is
	// a PROTECTION: it is what stops the character being stunned again at once.
	// A counter that took everything a hit left behind would report one stun as
	// two debuffs and hand all four scaling nodes double what they promise.
	const FGameplayTag Stunned = Effects::StunnedTag();
	const FGameplayTag Immune = Effects::StunImmuneTag();
	if (!TestTrue(TEXT("the vocabulary has both stun tags"),
				  Stunned.IsValid() && Immune.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("stun immunity can be attached"), Character.Attach(Immune));
	TestEqual(TEXT("being immune to stun is not a debuff"),
		Character.Count(), 0);

	TestTrue(TEXT("and being stunned can be attached"),
		Character.Attach(Stunned));
	TestEqual(TEXT("a stunned and stun-immune character carries one debuff"),
		Character.Count(), 1);

	// AND A KEYWORD THAT IS NOT ABOUT HARM AT ALL. `Keyword.Leech` and
	// `Keyword.Regeneration` are attached to HEALING by the Fervour rules, and
	// they share a branch prefix with nothing here. This pins that the roots are
	// matched by branch rather than by any looser rule that would sweep in a
	// sibling.
	const FGameplayTag Leech = TagNamed(TEXT("Keyword.Leech"));
	if (TestTrue(TEXT("the vocabulary has Keyword.Leech"), Leech.IsValid()))
	{
		TestTrue(TEXT("leech can be attached"), Character.Attach(Leech));
		TestEqual(TEXT("and it is not a debuff"), Character.Count(), 1);
	}

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffExpiryTest,
	"Cataclysm.Debuffs.AnEffectThatHasRunOutIsNoLongerCounted")
{
	using namespace CataclysmDebuffTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Character(World);

	const FGameplayTag Bleed = Debuffs::BleedTag();
	if (!TestTrue(TEXT("the vocabulary has Bleed"), Bleed.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("a two second bleed can be attached"),
		Character.Attach(Bleed, 2.0f));
	TestEqual(TEXT("it counts while it runs"), Character.Count(), 1);

	// REMOVED RATHER THAN WAITED OUT, AND THE COMMENT IS THE POINT. A world built
	// by `UWorld::CreateWorld` is never ticked, so a duration cannot expire by
	// itself here however far the clock is pushed; the engine takes a tag off
	// when it removes the effect, and only a tick removes it. Taking the effect
	// away directly is the same removal by a different trigger, and it proves
	// what this is for: the count needs nothing cancelled and no bookkeeping of
	// its own, because the tag list IS the state.
	const int32 Removed = Effects::RemoveEffectsGranting(Character.Actor, Bleed);
	TestEqual(TEXT("one effect was removed"), Removed, 1);
	TestEqual(TEXT("and the character carries nothing again"),
		Character.Count(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// The named kind, which is a different question from the count
// ---------------------------------------------------------------------------

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffBleedingIsItsOwnQuestionTest,
	"Cataclysm.Debuffs.BeingBleedingIsNotTheSameQuestionAsCarryingADebuff")
{
	using namespace CataclysmDebuffTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Character(World);

	TestFalse(TEXT("an untouched character is not bleeding"),
		Character.IsBleeding());

	// A DEBUFF THAT IS NOT BLEEDING ANSWERS NO, which is the direction that
	// matters. Thirst for Pain says "While you are Bleeding", and a build that
	// answered it from the debuff count would grant the bonus to a character who
	// had merely been stunned.
	const FGameplayTag Stunned = Effects::StunnedTag();
	if (TestTrue(TEXT("the vocabulary has State.Stunned"), Stunned.IsValid()))
	{
		TestTrue(TEXT("a stun can be attached"), Character.Attach(Stunned));
		TestEqual(TEXT("it counts as a debuff"), Character.Count(), 1);
		TestFalse(TEXT("and the character is still not Bleeding"),
			Character.IsBleeding());
	}

	// A BURN IS DAMAGE OVER TIME AND IS STILL NOT BLEEDING, the same argument one
	// level further in. The two are siblings under `Keyword.DoT`, so a build that
	// asked about the parent would answer yes to a burning character.
	const FGameplayTag Burn = Effects::BurnTag();
	if (TestTrue(TEXT("the vocabulary has Burn"), Burn.IsValid()))
	{
		TestTrue(TEXT("a burn can be attached"), Character.Attach(Burn));
		TestFalse(TEXT("a burning character is not Bleeding"),
			Character.IsBleeding());
	}

	// AND A BLEED ANSWERS YES.
	const FGameplayTag Bleed = Debuffs::BleedTag();
	if (TestTrue(TEXT("the vocabulary has Bleed"), Bleed.IsValid()))
	{
		TestTrue(TEXT("a bleed can be attached"), Character.Attach(Bleed));
		TestTrue(TEXT("and now the character is Bleeding"),
			Character.IsBleeding());
	}

	return true;
}

// ---------------------------------------------------------------------------
// What the counter refuses to guess
// ---------------------------------------------------------------------------

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffNoAbilitySystemTest,
	"Cataclysm.Debuffs.NothingToAskMeansNoDebuffsRatherThanAGuess")
{
	using namespace CataclysmDebuffTest;

	TestEqual(TEXT("no ability system carries no debuffs"),
		Debuffs::CountOn(nullptr), 0);
	TestFalse(TEXT("and is not Bleeding"), Debuffs::IsBleeding(nullptr));
	TestEqual(TEXT("and no actor carries none either"),
		Debuffs::CountOnActor(nullptr), 0);

	// THE ROOTS ARE THE LIST AND THE LIST IS SHORT ON PURPOSE. Pinning the
	// membership is what makes a change to it somebody's decision rather than a
	// side effect: adding a branch here changes what four passive nodes are worth
	// to every character in the game.
	const FGameplayTagContainer Roots = Debuffs::DebuffRoots();
	TestEqual(TEXT("there are two debuff roots"), Roots.Num(), 2);
	TestTrue(TEXT("damage over time is one of them"),
		Roots.HasTagExact(
			UCataclysmDamageCalculation::DamageOverTimeTag()));
	TestTrue(TEXT("and being stunned is the other"),
		Roots.HasTagExact(UCataclysmSkillEffects::StunnedTag()));
	TestFalse(TEXT("stun immunity is not a root"),
		Roots.HasTagExact(UCataclysmSkillEffects::StunImmuneTag()));

	return true;
}

// ---------------------------------------------------------------------------
// How long a lasting harmful effect runs on the character it is put on
// ---------------------------------------------------------------------------

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffDurationArithmeticTest,
	"Cataclysm.Debuffs.TheTargetsOwnStatDecidesHowLongAnEffectLasts")
{
	using namespace CataclysmDebuffTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Character(World);

	// A HUNDRED MEANS UNCHANGED, which is what every character in the game holds.
	TestEqual(TEXT("a character with the ordinary figure changes nothing"),
		Debuffs::DurationOn(Character.AbilitySystem, 10.0f), 10.0f, 0.001f);

	// AND THE TWO NODES THAT MOVE IT BOTH LENGTHEN. Symphony of Pain at its full
	// eight points is 116, and Vessel of Plagues is 150.
	Character.SetDurationStat(116.0f);
	TestEqual(TEXT("Symphony of Pain at eight points makes ten seconds 11.6"),
		Debuffs::DurationOn(Character.AbilitySystem, 10.0f), 11.6f, 0.001f);

	Character.SetDurationStat(150.0f);
	TestEqual(TEXT("and Vessel of Plagues makes it fifteen"),
		Debuffs::DurationOn(Character.AbilitySystem, 10.0f), 15.0f, 0.001f);

	// AND BOTH TOGETHER SUM RATHER THAN MULTIPLYING, because both rows join the
	// increases bucket. 100 + 16 + 50 is 166, not 100 x 1.16 x 1.50.
	Character.SetDurationStat(166.0f);
	TestEqual(TEXT("and holding both is a sum of the two, not a product"),
		Debuffs::DurationOn(Character.AbilitySystem, 10.0f), 16.6f, 0.001f);

	// A STAT OF NOTHING ENDS AN EFFECT AT ONCE. Nothing today lowers this stat,
	// and the arithmetic is checked anyway because the value is what decides
	// whether the caller applies the effect at all.
	Character.SetDurationStat(0.0f);
	TestEqual(TEXT("a figure of nothing leaves no duration"),
		Debuffs::DurationOn(Character.AbilitySystem, 10.0f), 0.0f, 0.001f);

	// AND A NEGATIVE FIGURE IS FLOORED AT NOTHING RATHER THAN INVERTING. A
	// negative duration would make the caller refuse the effect outright, which
	// would read as immunity rather than as a very short effect.
	Character.SetDurationStat(-50.0f);
	TestEqual(TEXT("and a negative one is floored rather than inverted"),
		Debuffs::DurationOn(Character.AbilitySystem, 10.0f), 0.0f, 0.001f);

	// NO CHARACTER AT ALL CHANGES NOTHING, which is the answer for anything with
	// no combat attribute set. A target that cannot hold the stat must not be a
	// target that shortens everything put on it.
	TestEqual(TEXT("nothing to ask leaves the duration alone"),
		Debuffs::DurationOn(nullptr, 10.0f), 10.0f, 0.001f);

	// AND AN EFFECT OF NO LENGTH STAYS THAT WAY.
	TestEqual(TEXT("and no duration stays no duration"),
		Debuffs::DurationOn(Character.AbilitySystem, 0.0f), 0.0f, 0.001f);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffBothApplyPathsTest,
	"Cataclysm.Debuffs.BothWaysOfApplyingALastingEffectHonourTheTargetsStat")
{
	using namespace CataclysmDebuffTest;

	// THIS IS THE HALF ISSUE #1033 ASKED FOR BY NAME. Two functions put a lasting
	// harmful effect on a character, and a build honouring one and not the other
	// would lengthen a stun and not a burn, or the reverse, with nothing
	// reporting it. Both are checked here against the same figure.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);

	// ONE CARRIER PER PATH, so each holds exactly one active effect and the
	// duration read back cannot be the other path's.
	const FScopedCarrier Stunned(World);
	const FScopedCarrier Burning(World);
	const FScopedCarrier Untouched(World);

	Stunned.SetDurationStat(150.0f);
	Burning.SetDurationStat(150.0f);

	const FGameplayTag Stun = Effects::StunnedTag();
	if (!TestTrue(TEXT("the vocabulary has Stunned"), Stun.IsValid()))
	{
		return false;
	}

	// THE PATH THAT APPLIES A TAG: stuns, stun immunity and the skill templates.
	TestTrue(TEXT("a ten second stun can be applied"),
		Effects::ApplyTagForDuration(Attacker.Actor, Stunned.Actor, Stun, 10.0f));
	TestEqual(TEXT("and it really runs for fifteen"),
		Stunned.LongestEffect(), 15.0f, 0.01f);

	// THE PATH THAT APPLIES DAMAGE OVER TIME: burning, and the Bleeding that
	// dropping below half health creates.
	//
	// THE ATTACKER LEAVES THE LENGTH ALONE, which is what keeps this reading
	// about the DEFENDER. Its own duration stat holds the ordinary 100, so the
	// fifteen below is the defender's doing and not a product of the two.
	TestTrue(TEXT("a ten second burn can be applied"),
		Effects::ApplyDamageOverTime(Attacker.Actor, Burning.Actor,
			/*DamagePerTick=*/10.0f, /*DurationSeconds=*/10.0f,
			Effects::BurnTag()));
	TestEqual(TEXT("and it really runs for fifteen too"),
		Burning.LongestEffect(), 15.0f, 0.01f);

	// AND A CHARACTER WITHOUT THE STAT GETS THE LENGTH THE ATTACKER SENT, which
	// is what says the two readings above are the option and not a change to
	// every effect in the game.
	TestTrue(TEXT("a ten second stun on an ordinary character"),
		Effects::ApplyTagForDuration(Attacker.Actor, Untouched.Actor, Stun,
									 10.0f));
	TestEqual(TEXT("runs for the ten seconds it was sent for"),
		Untouched.LongestEffect(), 10.0f, 0.01f);

	return true;
}

#undef CATACLYSM_DEBUFF_TEST

#endif // WITH_AUTOMATION_TESTS
