// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For the stat saying how long a lasting effect on this character runs.
// Issue #1033.
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
// For a target whose resistance a Shred or an Abyssal Aura cuts. Issue #1503.
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Counting the harmful effects a character is under. Issue #962.
 *
 * WHAT IS BEING PINNED. Seven Masochist nodes grant a bonus "for each unique
 * debuff on you" and another applies "While you are Bleeding", so the count and
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
		/**
		 * @param bResists  also give it the eight per-type resistances, for the
		 *                  tests at the end of this file that cut one. Issue
		 *                  #1503.
		 */
		explicit FScopedCarrier(UWorld* World, bool bResists = false)
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

			// AND THE RESISTANCES ONLY WHEN ASKED. Every test above the ones for
			// issue #1503 was written against a carrier holding none.
			if (bResists)
			{
				AbilitySystem->AddAttributeSetSubobject(
					NewObject<UCataclysmResistanceAttributeSet>(Actor));
			}

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

		/**
		 * How long the effect granting this tag has left to run. Issue #1070.
		 *
		 * REMAINING RATHER THAN THE WHOLE DURATION, which `LongestEffect` above
		 * answers. Holding a debuff still does not change how long it was ever
		 * meant to last; it changes how much of that is left.
		 *
		 * THE CLOCK DOES NOT MOVE IN THIS WORLD, so the remaining time reads as
		 * the whole duration until something pushes the effect's start out. That
		 * is what makes the checks below able to fail: with the hold deleted the
		 * number does not move at all.
		 */
		float RemainingOn(const FGameplayTag& Tag) const
		{
			FGameplayTagContainer Wanted;
			Wanted.AddTag(Tag);

			float Longest = 0.0f;
			for (float Seconds : AbilitySystem->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Wanted)))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
			return Longest;
		}

		/**
		 * Take Ceaseless Penance: record the stat line its row produces.
		 * Issue #1070.
		 *
		 * A RECORDED STAT LINE AND NOT A WRITE TO THE ATTRIBUTE, and that is
		 * forced rather than a preference. The row carries `health_above 50`,
		 * and a conditional modifier is never folded into a gameplay attribute,
		 * so writing the attribute would exercise a route the game does not
		 * have. This is the shape `UCataclysmPassiveTree::AccumulateInto`
		 * really builds from that row.
		 */
		void TakeCeaselessPenance() const
		{
			FCataclysmStatModifier Flag;
			Flag.Bucket = ECataclysmStatBucket::Flat;
			Flag.Source = ECataclysmModifierSource::PassiveKeystone;
			Flag.Value = 1.0f;
			Flag.Condition = ECataclysmStatCondition::HealthAbovePercent;
			Flag.ConditionValue = 50.0f;

			FCataclysmStatInputs Inputs;
			Inputs.Base = 0.0f;
			Inputs.Modifiers.Add(Flag);

			TMap<FName, FCataclysmStatInputs> Stats;
			Stats.Add(FName(Debuffs::DoNotExpireStat), Inputs);
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		/** Put health at this share of the thousand the carrier starts with. */
		void MoveHealthTo(float Share) const
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(),
				1'000.0f * Share);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * The gameplay effect definitions behind every effect running on a
	 * component. Issue #1501.
	 *
	 * THE DEFINITION AND NOT THE ACTIVE EFFECT, because the fault these are for
	 * is about how long a definition object lives: two effects that should have
	 * been two objects were one, and building the second destroyed the first.
	 *
	 * AN EMPTY QUERY MATCHES EVERYTHING. `FGameplayEffectQuery::Matches` fails a
	 * filter only when that filter was set, so a default query returns the whole
	 * list rather than nothing.
	 */
	TArray<const UGameplayEffect*> EffectDefinitionsOn(
		const UAbilitySystemComponent* AbilitySystem)
	{
		TArray<const UGameplayEffect*> Definitions;
		if (!AbilitySystem)
		{
			return Definitions;
		}

		for (const FActiveGameplayEffectHandle& Handle :
				 AbilitySystem->GetActiveEffects(FGameplayEffectQuery()))
		{
			Definitions.Add(AbilitySystem->GetGameplayEffectCDO(Handle));
		}
		return Definitions;
	}

	/** How many gameplay effects are running on a component at all. */
	int32 EffectCountOn(const UAbilitySystemComponent* AbilitySystem)
	{
		return EffectDefinitionsOn(AbilitySystem).Num();
	}

	/**
	 * The definition behind the one effect on a component, or null when it
	 * carries none or more than one.
	 *
	 * NULL RATHER THAN THE FIRST OF SEVERAL, so a test that expected one effect
	 * and found two says so instead of quietly reading whichever came first.
	 */
	const UGameplayEffect* SoleEffectDefinitionOn(
		const UAbilitySystemComponent* AbilitySystem)
	{
		const TArray<const UGameplayEffect*> Definitions =
			EffectDefinitionsOn(AbilitySystem);
		return Definitions.Num() == 1 ? Definitions[0] : nullptr;
	}

	/**
	 * What an effect's single modifier states, at level one.
	 *
	 * A NEGATIVE ANSWER MEANS THE QUESTION DID NOT APPLY: no effect, or not
	 * exactly one modifier, or a magnitude that is not a plain number. Every
	 * effect these tests build states one plain number, so a negative answer
	 * here is a failure rather than a value.
	 */
	float SoleModifierMagnitudeOf(const UGameplayEffect* Effect)
	{
		if (!Effect || Effect->Modifiers.Num() != 1)
		{
			return -1.0f;
		}

		float Magnitude = -1.0f;
		if (!Effect->Modifiers[0].ModifierMagnitude
				 .GetStaticMagnitudeIfPossible(1.0f, Magnitude))
		{
			return -1.0f;
		}
		return Magnitude;
	}
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
	// side effect: adding a branch here changes what seven passive nodes are
	// worth to every character in the game.
	//
	// IT DID ITS JOB ON 2026-09-04. Adding `Status.Debuff` for issue #1145 failed
	// this test and nothing else in the whole suite, which is exactly what it
	// exists to do. The third entry below is that decision written down, rather
	// than a number relaxed to make a failure go away.
	const FGameplayTagContainer Roots = Debuffs::DebuffRoots();
	TestEqual(TEXT("there are three debuff roots"), Roots.Num(), 3);
	TestTrue(TEXT("damage over time is one of them"),
		Roots.HasTagExact(
			UCataclysmDamageCalculation::DamageOverTimeTag()));
	TestTrue(TEXT("being stunned is another"),
		Roots.HasTagExact(UCataclysmSkillEffects::StunnedTag()));

	// AND EVERY NAMED CURSE, AS ONE BRANCH. The twenty-seven effects from the
	// Debuffs sheet are `Status.Debuff.*` since issue #1145, so this one entry
	// takes all of them and cannot take a buff.
	const FGameplayTag NamedCurses = UGameplayTagsManager::Get()
		.RequestGameplayTag(FName(TEXT("Status.Debuff")),
							/*ErrorIfNotFound=*/false);
	TestTrue(TEXT("the vocabulary has a branch for the named curses"),
		NamedCurses.IsValid());
	TestTrue(TEXT("and it is the third root"), Roots.HasTagExact(NamedCurses));

	TestFalse(TEXT("stun immunity is not a root"),
		Roots.HasTagExact(UCataclysmSkillEffects::StunImmuneTag()));

	// THE BUFFS ARE NOT, AND THAT IS THE WHOLE REASON THE BRANCH WAS SPLIT.
	// `Status` used to be one flat branch holding the eighteen buffs beside the
	// twenty-seven curses, so a root naming it would have paid those seven nodes
	// for carrying the Commander buff a Succubus grants its allies.
	const FGameplayTag Blessings = UGameplayTagsManager::Get()
		.RequestGameplayTag(FName(TEXT("Status.Buff")),
							/*ErrorIfNotFound=*/false);
	TestTrue(TEXT("the vocabulary has a branch for the blessings too"),
		Blessings.IsValid());
	TestFalse(TEXT("and it is not a root"), Roots.HasTagExact(Blessings));

	// NEITHER IS THE PARENT OF ALL THREE. `Status` still exists, because Unreal
	// implies a parent for every branch under it, and naming it would take the
	// buffs straight back in.
	const FGameplayTag Everything = UGameplayTagsManager::Get()
		.RequestGameplayTag(FName(TEXT("Status")), /*ErrorIfNotFound=*/false);
	TestFalse(TEXT("the whole Status branch is not a root"),
		Roots.HasTagExact(Everything));

	// AND NEITHER IS THE DAMAGE OVER TIME BRANCH UNDER `Status`, which would
	// double-count against `Keyword.DoT` above: the eight damage over time
	// effects have a tag under each of the two.
	const FGameplayTag StatusDoT = UGameplayTagsManager::Get()
		.RequestGameplayTag(FName(TEXT("Status.DoT")), /*ErrorIfNotFound=*/false);
	TestFalse(TEXT("and the Status damage over time branch is not either"),
		Roots.HasTagExact(StatusDoT));

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

// ---------------------------------------------------------------------------
// Ceaseless Penance: debuffs that stop counting down
// ---------------------------------------------------------------------------

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffHoldTest,
	"Cataclysm.Debuffs.CeaselessPenanceHoldsADebuffStillAboveHalfHealth")
{
	using namespace CataclysmDebuffTest;

	// ISSUE #1070: "Debuffs on you no longer expire while you are above 50%
	// health." The reading the project owner chose on 2026-08-28 is that the
	// countdown stops rather than that the effect is re-applied, so what is
	// checked here is the time REMAINING standing still.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Character(World);
	const FGameplayTag Bleed = Debuffs::BleedTag();
	if (!TestTrue(TEXT("the vocabulary has the bleed tag"), Bleed.IsValid()))
	{
		return false;
	}

	constexpr float Step = 0.25f;

	TestTrue(TEXT("a thirty second bleed can be attached"),
		Character.Attach(Bleed, 30.0f));
	TestEqual(TEXT("and it has thirty seconds left"),
		Character.RemainingOn(Bleed), 30.0f, 0.01f);

	// WITHOUT THE OPTION NOTHING IS HELD, AND THAT HALF COMES FIRST. A hold
	// applied to every character in the game would pass every check below, so
	// this is what says the rest is the option's doing.
	TestEqual(TEXT("a character without the option holds nothing"),
		Debuffs::HoldStep(Character.Actor, Step), 0);
	TestEqual(TEXT("and its bleed still has thirty seconds left"),
		Character.RemainingOn(Bleed), 30.0f, 0.01f);

	// AND WITH IT, ABOVE HALF HEALTH, THE TIME LEFT STOPS MOVING. The carrier
	// is at full health, so the row's `health_above 50` condition holds.
	Character.TakeCeaselessPenance();

	TestEqual(TEXT("one debuff is held"),
		Debuffs::HoldStep(Character.Actor, Step), 1);
	TestEqual(TEXT("and its end moved out by exactly the step"),
		Character.RemainingOn(Bleed), 30.0f + Step, 0.01f);

	// FOUR MORE STEPS ARE FOUR MORE QUARTER SECONDS, which is what says this
	// accumulates rather than being a single one-off push.
	for (int32 Held = 0; Held < 4; ++Held)
	{
		Debuffs::HoldStep(Character.Actor, Step);
	}
	TestEqual(TEXT("five steps moved it out by five quarter seconds"),
		Character.RemainingOn(Bleed), 30.0f + 5.0f * Step, 0.01f);

	// AND THE WHOLE DURATION IS UNTOUCHED, which is what separates this reading
	// from the one that re-applies the effect. A re-application would show a
	// duration of thirty and a remaining time of thirty; this shows a duration
	// of thirty and a remaining time above it.
	TestEqual(TEXT("the effect still says it lasts thirty seconds"),
		Character.LongestEffect(), 30.0f, 0.01f);

	// AND AT OR BELOW HALF HEALTH THE HOLD STOPS. Nothing was respecced and no
	// stat was written: health moved and that is all, which is what a condition
	// on the row means.
	const float BeforeFalling = Character.RemainingOn(Bleed);
	Character.MoveHealthTo(0.5f);

	TestEqual(TEXT("at exactly half health nothing is held"),
		Debuffs::HoldStep(Character.Actor, Step), 0);
	TestEqual(TEXT("and the time left did not move"),
		Character.RemainingOn(Bleed), BeforeFalling, 0.01f);

	// AND HEALING BACK ABOVE IT STARTS AGAIN, which says the rule is a state
	// rather than a crossing that happens once.
	Character.MoveHealthTo(0.51f);
	TestEqual(TEXT("just above half health it is held again"),
		Debuffs::HoldStep(Character.Actor, Step), 1);
	TestEqual(TEXT("and the time left moved again"),
		Character.RemainingOn(Bleed), BeforeFalling + Step, 0.01f);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffHoldLeavesOtherEffectsTest,
	"Cataclysm.Debuffs.HoldingDebuffsStillLeavesAnythingElseCountingDown")
{
	using namespace CataclysmDebuffTest;

	// THE HALF THAT SAYS WHAT IS NOT HELD. Issue #1070. The option says
	// "debuffs on you", so an effect that is not one has to go on counting down.
	// Without this a build that pushed every active effect on the character
	// would pass the test above, and a Masochist's own buffs would never end.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Character(World);
	Character.TakeCeaselessPenance();

	const FGameplayTag Bleed = Debuffs::BleedTag();
	// `Keyword.Leech` IS ATTACHED TO HEALING BY THE FERVOUR RULES and is in no
	// debuff branch, which is what the counting test above already relies on.
	const FGameplayTag Helpful = TagNamed(TEXT("Keyword.Leech"));
	if (!TestTrue(TEXT("the vocabulary has both tags"),
				  Bleed.IsValid() && Helpful.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("a bleed can be attached"), Character.Attach(Bleed, 30.0f));
	TestTrue(TEXT("and something helpful beside it"),
		Character.Attach(Helpful, 30.0f));

	TestEqual(TEXT("one of the two is a debuff and is held"),
		Debuffs::HoldStep(Character.Actor, 0.25f), 1);

	TestEqual(TEXT("the bleed's end moved out"),
		Character.RemainingOn(Bleed), 30.25f, 0.01f);
	TestEqual(TEXT("and the helpful effect's did not"),
		Character.RemainingOn(Helpful), 30.0f, 0.01f);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmDebuffHoldRefusalsTest,
	"Cataclysm.Debuffs.NothingIsHeldForACorpseOrForNoTimePassing")
{
	using namespace CataclysmDebuffTest;

	// THE TWO REFUSALS `HoldStep` OPENS WITH. Issue #1070. Neither is reachable
	// from the per-character step in ordinary play, and both are the kind of
	// guard that reads as working while doing nothing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Character(World);
	Character.TakeCeaselessPenance();

	const FGameplayTag Bleed = Debuffs::BleedTag();
	if (!TestTrue(TEXT("the vocabulary has the bleed tag"), Bleed.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("a bleed can be attached"), Character.Attach(Bleed, 30.0f));

	// A STEP OF NO TIME HOLDS NOTHING. A negative one would push every effect's
	// start BACKWARDS, which would make the option shorten debuffs instead of
	// stopping them.
	TestEqual(TEXT("a step of nothing holds nothing"),
		Debuffs::HoldStep(Character.Actor, 0.0f), 0);
	TestEqual(TEXT("a negative step holds nothing"),
		Debuffs::HoldStep(Character.Actor, -1.0f), 0);
	TestEqual(TEXT("and the time left is untouched by either"),
		Character.RemainingOn(Bleed), 30.0f, 0.01f);

	// AND NOTHING AT ALL IS NOT A CHARACTER.
	TestEqual(TEXT("no actor holds nothing"),
		Debuffs::HoldStep(nullptr, 0.25f), 0);

	// AND A CORPSE IS SKIPPED. A creature is destroyed on the step AFTER it
	// dies, so the per-character step really does run once on a dead one.
	UCataclysmSkillEffects::MarkDead(Character.Actor);
	TestEqual(TEXT("a dead character holds nothing"),
		Debuffs::HoldStep(Character.Actor, 0.25f), 0);
	TestEqual(TEXT("and its debuff's time left did not move"),
		Character.RemainingOn(Bleed), 30.0f, 0.01f);

	return true;
}

// ---------------------------------------------------------------------------
// A gameplay effect built at run time must outlive the next one built like it
// ---------------------------------------------------------------------------

CATACLYSM_DEBUFF_TEST(FCataclysmSecondPinLeavesTheFirstAloneTest,
	"Cataclysm.Debuffs.PinningASecondTargetLeavesTheFirstPinsEffectAlone")
{
	using namespace CataclysmDebuffTest;

	/**
	 * Issue #1501, the crash that ended two of the project owner's sessions on
	 * 2026-09-08. Both callstacks ended in `SetNumericAttributeBase` at
	 * `CataclysmPlayerClassStats.cpp:947`, reached from two different player
	 * actions, and read a different invalid address each time.
	 *
	 * WHAT WAS WRONG. Every gameplay effect this project builds at run time was
	 * given a FIXED object name in one shared outer -- `ApplyPin` asked for
	 * `NewObject<UGameplayEffect>(GetTransientPackage(), "CataclysmStatus_Pinned")`
	 * every time it pinned anything. Asking Unreal for an object whose name is
	 * already taken does not produce a second object. `StaticAllocateObject` in
	 * `UObjectGlobals.cpp` destroys the existing one in place -- it calls
	 * `Obj->~UObject()` -- and constructs the new one at the same address,
	 * under a comment reading "Replace an existing object without affecting the
	 * original's address or index".
	 *
	 * WHY THAT CRASHED SOMEWHERE ELSE ENTIRELY. When a lasting effect carrying
	 * attribute modifiers is applied, the engine keeps RAW POINTERS into that
	 * effect's `Modifiers` array: `GameplayEffect.cpp` hands
	 * `&ModInfo.SourceTags` and `&ModInfo.TargetTags` to the target's attribute
	 * aggregator, which stores them as bare `const FGameplayTagRequirements*`
	 * and owns nothing. Destroying the effect freed that array while the first
	 * target's aggregator still pointed into it. Nothing read those pointers
	 * until an attribute was recalculated -- and recalculating is what
	 * `SetNumericAttributeBase` does, so the crash landed on whoever next
	 * clicked a passive node or changed a piece of gear.
	 *
	 * WHY PINNING IS THE CASE THAT REACHES IT SOONEST. A pinning skill pins
	 * every target its blow landed on, one after another, in the loop
	 * `UCataclysmSkillTemplate` runs over its targets. So the second pin
	 * follows the first inside one skill activation, and the Spear's Skewer
	 * holds a whole line at once.
	 *
	 * WHAT IS ASSERTED, AND WHY IT IS NOT THE CRASH. A test that reads the freed
	 * memory does not fail reliably: the allocator usually hands the same block
	 * straight back, the read succeeds, and the test proves nothing. What is
	 * asserted instead is the condition the crash needs -- two pins must be two
	 * separate effect objects, and each target's effect must still describe that
	 * target's own pin. Both of those failed before the fix, every run.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FScopedCarrier First(World);
	const FScopedCarrier Second(World);

	// TWO DIFFERENT SIZES, so each target's effect can be told from the other's
	// by what it says rather than by where it sits. Thirty is the Spear's
	// Impale, which is the only row that states a figure.
	constexpr float FirstIncrease = 30.0f;
	constexpr float SecondIncrease = 10.0f;

	if (!TestTrue(TEXT("the first target is pinned"),
			Effects::ApplyPin(Attacker.Actor, First.Actor,
							  /*DurationSeconds=*/10.0f, FirstIncrease)))
	{
		return false;
	}

	const UGameplayEffect* FirstPin =
		SoleEffectDefinitionOn(First.AbilitySystem);
	if (!TestNotNull(TEXT("and it carries exactly one gameplay effect"),
					 FirstPin))
	{
		return false;
	}

	if (!TestTrue(TEXT("the second target is pinned"),
			Effects::ApplyPin(Attacker.Actor, Second.Actor,
							  /*DurationSeconds=*/10.0f, SecondIncrease)))
	{
		return false;
	}

	const UGameplayEffect* SecondPin =
		SoleEffectDefinitionOn(Second.AbilitySystem);
	if (!TestNotNull(TEXT("and so does the second target"), SecondPin))
	{
		return false;
	}

	// THE WHOLE TEST. Before issue #1501 these two were one object: the second
	// call destroyed the first effect and was built at its address, so the
	// first target's attribute aggregator was left pointing into freed memory.
	TestTrue(TEXT("the two pins are two separate gameplay effect objects"),
			 FirstPin != SecondPin);

	// AND THE SAME FAULT READ FROM THE OTHER SIDE. With one shared object the
	// first target's effect described the second target's pin, because there
	// was only ever one set of modifiers and the newer application wrote it.
	TestEqual(TEXT("the first target's pin still states its own increase"),
			  SoleModifierMagnitudeOf(FirstPin), FirstIncrease, 0.01f);
	TestEqual(TEXT("and the second target's pin states its own"),
			  SoleModifierMagnitudeOf(SecondPin), SecondIncrease, 0.01f);

	// AND BOTH TARGETS ACTUALLY TAKE WHAT THEIR OWN PIN SAYS. The Damage Taken
	// stat reads 100 when nothing has touched it, and
	// `UCataclysmDamageCalculation` divides it by 100, so a target pinned by
	// Impale reads 130 and takes 1.3 times what it otherwise would.
	const FGameplayAttribute Taken =
		UCataclysmCombatAttributeSet::GetDamageTakenAttribute();
	TestEqual(TEXT("the first target takes its own increase"),
			  First.AbilitySystem->GetNumericAttribute(Taken),
			  100.0f + FirstIncrease, 0.01f);
	TestEqual(TEXT("and the second takes its own"),
			  Second.AbilitySystem->GetNumericAttribute(Taken),
			  100.0f + SecondIncrease, 0.01f);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmRecalculatingWhileTwoArePinnedTest,
	"Cataclysm.Debuffs.RecalculatingAStatWhileTwoTargetsArePinnedKeepsBothPins")
{
	using namespace CataclysmDebuffTest;

	/**
	 * The path issue #1501 crashed on, walked deliberately.
	 *
	 * `SetNumericAttributeBase` is not a plain write. It sets the attribute
	 * aggregator's base value, which makes the engine re-evaluate every
	 * modifier standing on that attribute, which reads the tag requirements
	 * each modifier was registered with. Those are the pointers the old code
	 * left dangling. `UCataclysmPlayerClassStats::ApplyTo` calls it once per
	 * stat, which is why any full recalculation -- spending a passive point,
	 * changing a piece of gear -- was enough to crash the game.
	 *
	 * THIS IS NOT THE TEST THAT FAILED BEFORE THE FIX, and saying so matters.
	 * Reading freed memory is not reliably a crash: the block is usually handed
	 * straight back by the allocator and the read succeeds. The test above is
	 * the one that failed every run. This one holds the path itself, so that a
	 * later change that breaks the evaluation is caught here rather than by the
	 * project owner.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FScopedCarrier First(World);
	const FScopedCarrier Second(World);

	constexpr float FirstIncrease = 30.0f;
	constexpr float SecondIncrease = 10.0f;

	TestTrue(TEXT("the first target is pinned"),
		Effects::ApplyPin(Attacker.Actor, First.Actor,
						  /*DurationSeconds=*/10.0f, FirstIncrease));
	TestTrue(TEXT("and then the second"),
		Effects::ApplyPin(Attacker.Actor, Second.Actor,
						  /*DurationSeconds=*/10.0f, SecondIncrease));

	// THE RECALCULATION, ON THE VERY STAT BOTH PINS MODIFY. Writing the base
	// back to the value it already held is enough: the write is what forces the
	// re-evaluation, not the value changing.
	const FGameplayAttribute Taken =
		UCataclysmCombatAttributeSet::GetDamageTakenAttribute();
	First.AbilitySystem->SetNumericAttributeBase(Taken, 100.0f);
	Second.AbilitySystem->SetNumericAttributeBase(Taken, 100.0f);

	TestEqual(TEXT("the first target still takes its own increase afterwards"),
			  First.AbilitySystem->GetNumericAttribute(Taken),
			  100.0f + FirstIncrease, 0.01f);
	TestEqual(TEXT("and the second still takes its own"),
			  Second.AbilitySystem->GetNumericAttribute(Taken),
			  100.0f + SecondIncrease, 0.01f);

	return true;
}

// ---------------------------------------------------------------------------
// One effect at a time per target, which the repair must not cost
// ---------------------------------------------------------------------------

CATACLYSM_DEBUFF_TEST(FCataclysmSecondPinOnOneTargetTest,
	"Cataclysm.Debuffs.PinningOneTargetTwiceStillLeavesItOnePin")
{
	using namespace CataclysmDebuffTest;

	/**
	 * The half of issue #1501 that is easy to break while fixing the other.
	 *
	 * The design requires that a second application of the same effect refresh
	 * the first rather than add a second. Until this fix that happened by
	 * accident: the engine compares `ActiveEffect.Spec.Def == Spec.Def`, a raw
	 * pointer comparison, and rebuilding the object at the same address is what
	 * made it true. Giving each effect its own name -- which is what stops the
	 * crash -- also stops those pointers matching, so the rule now has to be
	 * kept deliberately.
	 *
	 * COUNTED AS EFFECTS AND NOT AS TAGS, which is the whole point of the test.
	 * `UCataclysmDebuffs::CountOn` counts distinct tags, so two pins running at
	 * once would still count as one debuff and nothing would report it. What
	 * would be wrong is two effects both adding to Damage Taken, so the stat is
	 * asserted as well as the count.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FScopedCarrier Held(World);

	TestTrue(TEXT("the target is pinned"),
		Effects::ApplyPin(Attacker.Actor, Held.Actor,
						  /*DurationSeconds=*/10.0f, /*Increase=*/30.0f));
	TestEqual(TEXT("and carries one effect"),
			  EffectCountOn(Held.AbilitySystem), 1);

	TestTrue(TEXT("the same target is pinned again"),
		Effects::ApplyPin(Attacker.Actor, Held.Actor,
						  /*DurationSeconds=*/10.0f, /*Increase=*/10.0f));

	// ONE, NOT TWO. A second pin replaces the first rather than joining it.
	TestEqual(TEXT("and still carries exactly one effect"),
			  EffectCountOn(Held.AbilitySystem), 1);

	TestTrue(TEXT("and is still pinned"),
			 Effects::HasTag(Held.Actor, Effects::PinnedTag()));

	// THE FIGURE THE SECOND PIN CARRIES IS NOT ASSERTED HERE. Which of two
	// pins' increases should win is a separate question with its own test
	// below, and it is not part of the rule this one is for.
	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmStrongerPinStandsTest,
	"Cataclysm.Debuffs.PinningATargetAgainKeepsTheStrongerPinsIncrease")
{
	using namespace CataclysmDebuffTest;

	/**
	 * Which of two applications of one effect decides the figure. Issue #1503.
	 *
	 * THE STRONGER ONE. The project owner ruled on 2026-09-09 that the strongest
	 * application of a lasting effect wins: "A 10% Shred can never overwrite a
	 * 30% Shred." `docs/DECISIONS.md` carries the ruling. A pin carrying a
	 * damage taken increase is the same kind of effect, so a 10% pin arriving on
	 * a target pinned for 30% leaves the 30% standing.
	 *
	 * THREE ANSWERS HAVE BEEN MEASURED ON THIS TEST, AND ONLY THE LAST WAS
	 * CHOSEN. Before issue #1501 the first application's figure was frozen,
	 * because every application was built at one address, and the target read
	 * 130. The crash repair in #1505 made the newer application's figure apply,
	 * and it read 110. The ruling makes it 130 again, now as a decision rather
	 * than as a consequence of how the effects were built.
	 *
	 * THE 50% PIN THAT FOLLOWS READS 150 UNDER BOTH OF THE LAST TWO RULES,
	 * because 50 is the strongest and also the newest. It is kept as the half
	 * that says a stronger application replaces a weaker one, rather than the
	 * first one standing for ever. It is not evidence of which rule is in force.
	 *
	 * WHETHER THE RULING ALSO REACHES DAMAGE OVER TIME IS NOT DECIDED. A burn's
	 * per-tick figure lives on the applied effect rather than in an attribute,
	 * and a later application still replaces it.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FScopedCarrier Held(World);

	const FGameplayAttribute Taken =
		UCataclysmCombatAttributeSet::GetDamageTakenAttribute();

	TestTrue(TEXT("the target is pinned for thirty per cent"),
		Effects::ApplyPin(Attacker.Actor, Held.Actor,
						  /*DurationSeconds=*/10.0f, /*Increase=*/30.0f));
	TestEqual(TEXT("and takes that much more"),
			  Held.AbilitySystem->GetNumericAttribute(Taken), 130.0f, 0.01f);

	TestTrue(TEXT("and is then pinned for ten"),
		Effects::ApplyPin(Attacker.Actor, Held.Actor,
						  /*DurationSeconds=*/10.0f, /*Increase=*/10.0f));

	// THIRTY, NOT TEN. The weaker pin's figure is refused.
	TestEqual(TEXT("and still takes the stronger pin's increase"),
			  Held.AbilitySystem->GetNumericAttribute(Taken), 130.0f, 0.01f);

	// AND A STRONGER PIN MOVES IT UP, which is the half that says the stronger
	// application wins rather than whichever arrived first.
	TestTrue(TEXT("and is then pinned for fifty"),
		Effects::ApplyPin(Attacker.Actor, Held.Actor,
						  /*DurationSeconds=*/10.0f, /*Increase=*/50.0f));
	TestEqual(TEXT("and takes that much more"),
			  Held.AbilitySystem->GetNumericAttribute(Taken), 150.0f, 0.01f);
	TestEqual(TEXT("and still carries exactly one effect"),
			  EffectCountOn(Held.AbilitySystem), 1);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmSecondBleedOnOneTargetTest,
	"Cataclysm.Debuffs.BleedingOneTargetTwiceStillLeavesItOneBleed")
{
	using namespace CataclysmDebuffTest;

	/**
	 * The same rule for the other path that applies a lasting effect.
	 *
	 * `ApplyDamageOverTime` is periodic, so it registers no attribute modifiers
	 * and cannot crash the way the pin did. It shares the fixed-name fault and
	 * the accidental stacking that came with it, so it has to be held to the
	 * same rule: a second bleed refreshes the first rather than adding a second
	 * one ticking alongside it. Issue #1062 states that rule; the test it added
	 * counts tags, and two bleeds at once would still count as one tag.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FScopedCarrier Bleeding(World);

	const FGameplayTag Bleed = Debuffs::BleedTag();
	if (!TestTrue(TEXT("the vocabulary has the bleed tag"), Bleed.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("a bleed is applied"),
		Effects::ApplyDamageOverTime(Attacker.Actor, Bleeding.Actor,
									 /*DamagePerTick=*/10.0f,
									 /*DurationSeconds=*/10.0f, Bleed));
	TestEqual(TEXT("and the target carries one effect"),
			  EffectCountOn(Bleeding.AbilitySystem), 1);

	TestTrue(TEXT("and then a second bleed"),
		Effects::ApplyDamageOverTime(Attacker.Actor, Bleeding.Actor,
									 /*DamagePerTick=*/10.0f,
									 /*DurationSeconds=*/10.0f, Bleed));
	TestEqual(TEXT("and it still carries exactly one"),
			  EffectCountOn(Bleeding.AbilitySystem), 1);
	TestTrue(TEXT("and is still bleeding"), Bleeding.IsBleeding());

	return true;
}

// ---------------------------------------------------------------------------
// The strongest application wins. Issue #1503.
// ---------------------------------------------------------------------------

CATACLYSM_DEBUFF_TEST(FCataclysmWeakerPinRefreshesTest,
	"Cataclysm.Debuffs.AWeakerPinRefreshesTheStrongerPinsDurationAndNeverShortensIt")
{
	using namespace CataclysmDebuffTest;

	/**
	 * The third of the project owner's rulings of 2026-09-09, from
	 * `docs/DECISIONS.md`: "A weaker application still refreshes the duration.
	 * Its figures are refused; its timing is not." Without it, keeping an effect
	 * running would need its strongest source every time.
	 *
	 * NEVER SHORTER, WHICH THE RULING DOES NOT SAY AND IS A JUDGEMENT. A weaker
	 * application lasting less than the running one has left leaves the running
	 * one's time alone. The ruling's own reason is keeping an effect running,
	 * and cutting one short would work against that. The same entry records the
	 * judgement.
	 *
	 * THE CLOCK DOES NOT MOVE IN THIS WORLD, so a fresh effect's remaining time
	 * is its whole duration, and any other figure means something moved it.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FGameplayAttribute Taken =
		UCataclysmCombatAttributeSet::GetDamageTakenAttribute();

	// A STRONG PIN WITH TWO SECONDS TO RUN, THEN A WEAK ONE STATING NINE.
	const FScopedCarrier Soon(World);
	TestTrue(TEXT("a target is pinned for thirty per cent for two seconds"),
		Effects::ApplyPin(Attacker.Actor, Soon.Actor,
						  /*DurationSeconds=*/2.0f, /*Increase=*/30.0f));
	TestTrue(TEXT("and then for ten per cent for nine seconds"),
		Effects::ApplyPin(Attacker.Actor, Soon.Actor,
						  /*DurationSeconds=*/9.0f, /*Increase=*/10.0f));
	TestEqual(TEXT("it still takes the stronger pin's increase"),
			  Soon.AbilitySystem->GetNumericAttribute(Taken), 130.0f, 0.01f);
	TestEqual(TEXT("and has the weaker pin's nine seconds left"),
			  Soon.RemainingOn(Effects::PinnedTag()), 9.0f, 0.01f);
	TestEqual(TEXT("in exactly one effect"),
			  EffectCountOn(Soon.AbilitySystem), 1);

	// AND THE OTHER WAY ROUND: A STRONG PIN WITH NINE SECONDS TO RUN, THEN A
	// WEAK ONE STATING TWO.
	const FScopedCarrier Later(World);
	TestTrue(TEXT("another target is pinned for thirty per cent for nine seconds"),
		Effects::ApplyPin(Attacker.Actor, Later.Actor,
						  /*DurationSeconds=*/9.0f, /*Increase=*/30.0f));
	TestTrue(TEXT("and then for ten per cent for two seconds"),
		Effects::ApplyPin(Attacker.Actor, Later.Actor,
						  /*DurationSeconds=*/2.0f, /*Increase=*/10.0f));
	TestEqual(TEXT("it still takes the stronger pin's increase"),
			  Later.AbilitySystem->GetNumericAttribute(Taken), 130.0f, 0.01f);
	TestEqual(TEXT("and keeps its own nine seconds rather than the two"),
			  Later.RemainingOn(Effects::PinnedTag()), 9.0f, 0.01f);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmPinWithoutIncreaseTest,
	"Cataclysm.Debuffs.APinStatingNoIncreaseLeavesTheIncreaseOfOneThatDoes")
{
	using namespace CataclysmDebuffTest;

	/**
	 * Only one pin in the game states an increase, and the others must not take
	 * it away.
	 *
	 * The Spear's Impale states that a pinned target takes 30% more damage. Nail
	 * Down, Skewer, Thicket and the terrain hold in `CataclysmTerrain.cpp` pin a
	 * target and state nothing. A pin stating nothing states zero, which is
	 * weaker than a pin stating 30, so under the 2026-09-09 ruling it cannot
	 * replace Impale's. It still refreshes the duration, which is the ruling's
	 * third part.
	 *
	 * A pin stating nothing is granted as a bare tag, and granting a tag takes
	 * off whatever already grants it. So before this change, holding an impaled
	 * target with any of the other four took Impale's increase off it.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FScopedCarrier Held(World);
	const FGameplayAttribute Taken =
		UCataclysmCombatAttributeSet::GetDamageTakenAttribute();

	TestTrue(TEXT("the target is pinned for thirty per cent for three seconds"),
		Effects::ApplyPin(Attacker.Actor, Held.Actor,
						  /*DurationSeconds=*/3.0f, /*Increase=*/30.0f));
	TestTrue(TEXT("and then held for eight seconds by a pin stating nothing"),
		Effects::ApplyPin(Attacker.Actor, Held.Actor,
						  /*DurationSeconds=*/8.0f, /*Increase=*/0.0f));

	TestEqual(TEXT("it still takes the first pin's increase"),
			  Held.AbilitySystem->GetNumericAttribute(Taken), 130.0f, 0.01f);
	TestEqual(TEXT("for the eight seconds the second pin states"),
			  Held.RemainingOn(Effects::PinnedTag()), 8.0f, 0.01f);
	TestTrue(TEXT("and it is still pinned"),
			 Effects::HasTag(Held.Actor, Effects::PinnedTag()));

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmShredAgainTest,
	"Cataclysm.Debuffs.ShreddingATargetAgainDoesNotGiveItsResistanceBack")
{
	using namespace CataclysmDebuffTest;

	/**
	 * The same Shred twice leaves the target where the first one put it.
	 *
	 * FOUND BY READING WHILE BUILDING ISSUE #1503. `ApplyNamedEffect` worked
	 * out how much resistance a new Shred takes while the running Shred was
	 * still on the target, and only then took the running one off. Measured on
	 * 2026-09-11 before the change: a target at 40, cut to 10 by a 30 Shred,
	 * was cut again by min(30, 10) = 10, given the first 30 back, and read 30.
	 * Applying the same curse again undid two thirds of it.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FGameplayTag Shred = TagNamed(TEXT("Status.Debuff.Shred"));
	if (!TestTrue(TEXT("Status.Debuff.Shred is a gameplay tag"), Shred.IsValid()))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FScopedCarrier Target(World, /*bResists=*/true);
	const FGameplayAttribute Demonic =
		UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute();
	Target.AbilitySystem->SetNumericAttributeBase(Demonic, 40.0f);

	TestTrue(TEXT("a Demonic Shred of thirty lands"),
		Effects::ApplyNamedEffect(Attacker.Actor, Target.Actor, Shred,
								  /*DurationSeconds=*/6.0f, /*Magnitude=*/30.0f,
								  FName(TEXT("Demonic"))));
	TestEqual(TEXT("and takes the target's Demonic resistance from 40 to 10"),
			  Target.AbilitySystem->GetNumericAttribute(Demonic), 10.0f, 0.01f);

	TestTrue(TEXT("the same Shred lands again"),
		Effects::ApplyNamedEffect(Attacker.Actor, Target.Actor, Shred,
								  /*DurationSeconds=*/6.0f, /*Magnitude=*/30.0f,
								  FName(TEXT("Demonic"))));
	TestEqual(TEXT("and the target is still at 10"),
			  Target.AbilitySystem->GetNumericAttribute(Demonic), 10.0f, 0.01f);
	TestEqual(TEXT("carrying exactly one effect"),
			  EffectCountOn(Target.AbilitySystem), 1);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmStrongerShredTest,
	"Cataclysm.Debuffs.AStrongerShredReplacesAWeakerOneAgainstTheWholeResistance")
{
	using namespace CataclysmDebuffTest;

	/**
	 * The worked example in the 2026-09-09 entry of `docs/DECISIONS.md`, on one
	 * resistance rather than two. Issue #1503.
	 *
	 * A target at 40 takes a 30 Shred and is left at 10. A 50 Shred then states
	 * more, so it wins. It is sized against the 40 the target has without the
	 * Shred it replaces, not against the 10 left after it, so it takes all 40.
	 * The entry: "A 50% Shred beats a 30% Shred whatever state the target is
	 * in."
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FGameplayTag Shred = TagNamed(TEXT("Status.Debuff.Shred"));
	if (!TestTrue(TEXT("Status.Debuff.Shred is a gameplay tag"), Shred.IsValid()))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FScopedCarrier Target(World, /*bResists=*/true);
	const FGameplayAttribute Demonic =
		UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute();
	Target.AbilitySystem->SetNumericAttributeBase(Demonic, 40.0f);

	TestTrue(TEXT("a Demonic Shred of thirty lands"),
		Effects::ApplyNamedEffect(Attacker.Actor, Target.Actor, Shred,
								  /*DurationSeconds=*/6.0f, /*Magnitude=*/30.0f,
								  FName(TEXT("Demonic"))));
	TestEqual(TEXT("and leaves the target at 10"),
			  Target.AbilitySystem->GetNumericAttribute(Demonic), 10.0f, 0.01f);

	TestTrue(TEXT("a Demonic Shred of fifty lands"),
		Effects::ApplyNamedEffect(Attacker.Actor, Target.Actor, Shred,
								  /*DurationSeconds=*/6.0f, /*Magnitude=*/50.0f,
								  FName(TEXT("Demonic"))));
	TestEqual(TEXT("and takes all 40, leaving nothing"),
			  Target.AbilitySystem->GetNumericAttribute(Demonic), 0.0f, 0.01f);
	TestEqual(TEXT("carrying exactly one effect"),
			  EffectCountOn(Target.AbilitySystem), 1);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmWeakerShredTest,
	"Cataclysm.Debuffs.AWeakerShredLeavesTheStrongerFigureAndRefreshesItsDuration")
{
	using namespace CataclysmDebuffTest;

	/**
	 * The owner's own example, "A 10% Shred can never overwrite a 30% Shred",
	 * with the third ruling beside it. Issue #1503.
	 *
	 * The 30 Shred has two seconds to run when a 10 Shred arrives stating six.
	 * The target stays at 10, and the 30 Shred now has six seconds to run.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FGameplayTag Shred = TagNamed(TEXT("Status.Debuff.Shred"));
	if (!TestTrue(TEXT("Status.Debuff.Shred is a gameplay tag"), Shred.IsValid()))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FScopedCarrier Target(World, /*bResists=*/true);
	const FGameplayAttribute Demonic =
		UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute();
	Target.AbilitySystem->SetNumericAttributeBase(Demonic, 40.0f);

	TestTrue(TEXT("a Demonic Shred of thirty lands for two seconds"),
		Effects::ApplyNamedEffect(Attacker.Actor, Target.Actor, Shred,
								  /*DurationSeconds=*/2.0f, /*Magnitude=*/30.0f,
								  FName(TEXT("Demonic"))));
	TestEqual(TEXT("and leaves the target at 10"),
			  Target.AbilitySystem->GetNumericAttribute(Demonic), 10.0f, 0.01f);

	TestTrue(TEXT("a Demonic Shred of ten lands stating six seconds"),
		Effects::ApplyNamedEffect(Attacker.Actor, Target.Actor, Shred,
								  /*DurationSeconds=*/6.0f, /*Magnitude=*/10.0f,
								  FName(TEXT("Demonic"))));
	TestEqual(TEXT("and the target is still at 10"),
			  Target.AbilitySystem->GetNumericAttribute(Demonic), 10.0f, 0.01f);
	TestEqual(TEXT("and the stronger Shred has the weaker one's six seconds left"),
			  Target.RemainingOn(Shred), 6.0f, 0.01f);
	TestEqual(TEXT("in exactly one effect"),
			  EffectCountOn(Target.AbilitySystem), 1);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmWeakerAuraTest,
	"Cataclysm.Debuffs.AWeakerAbyssalAuraLeavesBothOfTheStrongerOnesCuts")
{
	using namespace CataclysmDebuffTest;

	/**
	 * An effect cutting two resistances keeps the stronger application whole.
	 * Issue #1503.
	 *
	 * Abyssal Aura cuts Demonic and War resistance together. The second
	 * 2026-09-09 ruling compares two applications of such an effect by the sum
	 * of what each states across its stats, and applies the winner entire.
	 *
	 * THIS DOES NOT PROVE THE SUM, and the entry says no test can. Every effect
	 * in the game states one figure for all of its stats, so the sum is that
	 * figure times the number of stats, and comparing sums orders two
	 * applications exactly as comparing the figures does. What this proves is
	 * that the weaker application changes neither stat, rather than one of them.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FGameplayTag Aura = TagNamed(TEXT("Status.Debuff.AbyssalAura"));
	if (!TestTrue(TEXT("Status.Debuff.AbyssalAura is a gameplay tag"),
				  Aura.IsValid()))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);
	const FScopedCarrier Target(World, /*bResists=*/true);
	const FGameplayAttribute Demonic =
		UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute();
	const FGameplayAttribute War =
		UCataclysmResistanceAttributeSet::GetWarResistanceAttribute();
	Target.AbilitySystem->SetNumericAttributeBase(Demonic, 40.0f);
	Target.AbilitySystem->SetNumericAttributeBase(War, 40.0f);

	TestTrue(TEXT("an aura cutting twenty five lands"),
		Effects::ApplyNamedEffect(Attacker.Actor, Target.Actor, Aura,
								  /*DurationSeconds=*/2.0f, /*Magnitude=*/25.0f));
	TestEqual(TEXT("and leaves Demonic at 15"),
			  Target.AbilitySystem->GetNumericAttribute(Demonic), 15.0f, 0.01f);
	TestEqual(TEXT("and War at 15"),
			  Target.AbilitySystem->GetNumericAttribute(War), 15.0f, 0.01f);

	TestTrue(TEXT("an aura cutting ten lands"),
		Effects::ApplyNamedEffect(Attacker.Actor, Target.Actor, Aura,
								  /*DurationSeconds=*/2.0f, /*Magnitude=*/10.0f));
	TestEqual(TEXT("and Demonic is still at 15"),
			  Target.AbilitySystem->GetNumericAttribute(Demonic), 15.0f, 0.01f);
	TestEqual(TEXT("and War is still at 15"),
			  Target.AbilitySystem->GetNumericAttribute(War), 15.0f, 0.01f);
	TestEqual(TEXT("in exactly one effect"),
			  EffectCountOn(Target.AbilitySystem), 1);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmWeakerBleedTest,
	"Cataclysm.Debuffs.AWeakerBleedLeavesTheStrongerOneAndOnlyLengthensIt")
{
	using namespace CataclysmDebuffTest;

	/**
	 * The strongest application wins for damage over time as well. Issue #1503.
	 *
	 * The 2026-09-09 entry of `docs/DECISIONS.md` lists the helpers that apply
	 * damage over time among what it affects, and its precedent is Path of
	 * Exile's ignite, where only the strongest one deals damage. The
	 * coordinating session decided on 2026-09-11 that it covers bleeds,
	 * poisons, burns, diseases and necrosis.
	 *
	 * READ OFF THE RUNNING EFFECT'S ONE MODIFIER, which is the damage one tick
	 * deals. Nothing ticks in this world, so the figure on the effect is the
	 * evidence of which application is running.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FGameplayTag Bleed = Debuffs::BleedTag();
	if (!TestTrue(TEXT("the vocabulary has the bleed tag"), Bleed.IsValid()))
	{
		return false;
	}

	const FScopedCarrier Attacker(World);

	// A STRONG BLEED WITH TWO SECONDS TO RUN, THEN A WEAK ONE STATING NINE.
	const FScopedCarrier Soon(World);
	TestTrue(TEXT("a bleed of 20 a tick lands for two seconds"),
		Effects::ApplyDamageOverTime(Attacker.Actor, Soon.Actor,
									 /*DamagePerTick=*/20.0f,
									 /*DurationSeconds=*/2.0f, Bleed));
	TestTrue(TEXT("and then one of 10 a tick for nine"),
		Effects::ApplyDamageOverTime(Attacker.Actor, Soon.Actor,
									 /*DamagePerTick=*/10.0f,
									 /*DurationSeconds=*/9.0f, Bleed));
	TestEqual(TEXT("the running bleed still deals 20 a tick"),
			  SoleModifierMagnitudeOf(SoleEffectDefinitionOn(Soon.AbilitySystem)),
			  20.0f, 0.01f);
	TestEqual(TEXT("and has the weaker one's nine seconds left"),
			  Soon.RemainingOn(Bleed), 9.0f, 0.01f);

	// A WEAK ONE STATING LESS TIME THAN THE STRONG ONE HAS LEFT CHANGES NOTHING,
	// AND A STRONGER ONE THEN REPLACES IT.
	const FScopedCarrier Later(World);
	TestTrue(TEXT("a bleed of 20 a tick lands for nine seconds"),
		Effects::ApplyDamageOverTime(Attacker.Actor, Later.Actor,
									 /*DamagePerTick=*/20.0f,
									 /*DurationSeconds=*/9.0f, Bleed));
	TestTrue(TEXT("and then one of 10 a tick for two"),
		Effects::ApplyDamageOverTime(Attacker.Actor, Later.Actor,
									 /*DamagePerTick=*/10.0f,
									 /*DurationSeconds=*/2.0f, Bleed));
	TestEqual(TEXT("it still deals 20 a tick"),
			  SoleModifierMagnitudeOf(SoleEffectDefinitionOn(Later.AbilitySystem)),
			  20.0f, 0.01f);
	TestEqual(TEXT("and keeps its own nine seconds rather than the two"),
			  Later.RemainingOn(Bleed), 9.0f, 0.01f);

	TestTrue(TEXT("a bleed of 30 a tick lands for three seconds"),
		Effects::ApplyDamageOverTime(Attacker.Actor, Later.Actor,
									 /*DamagePerTick=*/30.0f,
									 /*DurationSeconds=*/3.0f, Bleed));
	TestEqual(TEXT("and replaces it, dealing 30 a tick"),
			  SoleModifierMagnitudeOf(SoleEffectDefinitionOn(Later.AbilitySystem)),
			  30.0f, 0.01f);
	TestEqual(TEXT("for its own three seconds"),
			  Later.RemainingOn(Bleed), 3.0f, 0.01f);
	TestEqual(TEXT("in exactly one effect"),
			  EffectCountOn(Later.AbilitySystem), 1);

	return true;
}

CATACLYSM_DEBUFF_TEST(FCataclysmFasterBleedTest,
	"Cataclysm.Debuffs.TwoBleedsAreComparedByTheDamageEachDealsASecond")
{
	using namespace CataclysmDebuffTest;

	/**
	 * Damage over time is compared by the damage it deals each second, which is
	 * a judgement. Issue #1503, and `docs/DECISIONS.md` records it.
	 *
	 * Per second rather than per tick, because the attacker's Damage over Time
	 * Frequency makes the same damage a tick worth more each second. Not by the
	 * total, because how long it runs is refreshed separately under the third
	 * ruling.
	 *
	 * THE CASE WHERE THE TWO READINGS DISAGREE. The second bleed deals less a
	 * tick and ticks twice as often, so it deals 30 for every 20 the first
	 * deals. Compared per tick it would lose; compared per second it wins.
	 */
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	const FGameplayTag Bleed = Debuffs::BleedTag();
	if (!TestTrue(TEXT("the vocabulary has the bleed tag"), Bleed.IsValid()))
	{
		return false;
	}

	const FScopedCarrier Slow(World);
	const FScopedCarrier Fast(World);
	// TWICE THE FREQUENCY, SO HALF THE GAP BETWEEN TICKS. The stat reads 100
	// when nothing has raised it, which is what the class line gives.
	Fast.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetDotFrequencyAttribute(), 200.0f);
	const FScopedCarrier Target(World);

	TestTrue(TEXT("a bleed of 20 a tick lands"),
		Effects::ApplyDamageOverTime(Slow.Actor, Target.Actor,
									 /*DamagePerTick=*/20.0f,
									 /*DurationSeconds=*/5.0f, Bleed));
	TestTrue(TEXT("and then one of 15 a tick from an attacker ticking twice as often"),
		Effects::ApplyDamageOverTime(Fast.Actor, Target.Actor,
									 /*DamagePerTick=*/15.0f,
									 /*DurationSeconds=*/5.0f, Bleed));

	TestEqual(TEXT("the faster bleed replaced the slower one"),
			  SoleModifierMagnitudeOf(SoleEffectDefinitionOn(Target.AbilitySystem)),
			  15.0f, 0.01f);
	TestEqual(TEXT("in exactly one effect"),
			  EffectCountOn(Target.AbilitySystem), 1);

	return true;
}

#undef CATACLYSM_DEBUFF_TEST

#endif // WITH_AUTOMATION_TESTS
