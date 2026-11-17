// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For the real minion whose summoner holds the keystone. Issue #1515.
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The two Ravager keystones that forbid a defence working. Issue #1515.
 *
 *   Ravager_keystone_spine_001  Ironhide
 *       "Your Armor cannot be ignored: armor penetration and piercing weapons
 *        remove none of it."
 *   Ravager_keystone_spine_002  Every Swing Lands
 *       "Your melee attacks cannot be evaded, and your melee arc is a full
 *        circle rather than a cone."
 *
 * ONLY THE FIRST CLAUSE OF EVERY SWING LANDS IS BUILT. The arc belongs with the
 * other nodes that change an attack's shape and is deferred; nothing here
 * asserts anything about it.
 *
 * THE TWO SIT ON OPPOSITE SIDES OF A BLOW, which is the thing to keep straight.
 * Ironhide is read on the DEFENDER and protects its own armour. Every Swing
 * Lands is read on the ATTACKER and refuses the defender's evasion. Every other
 * flag of this kind in the project is read on the character holding it, so the
 * distinctness test below is not a formality.
 *
 * EVASION IS FORCED RATHER THAN ROLLED. A defender at 100 evasion evades every
 * direct attack, because the roll is drawn from [0, 100) and the comparison is
 * `Roll < evasion`. That needs no scoped roll and no pinned random number, and
 * it makes "was it evaded" a question about the rule rather than about luck.
 */
namespace CataclysmForbiddenDefenceTest
{
	using Combat = UCataclysmCombatAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;
	using Effects = UCataclysmSkillEffects;

	/** Health large enough that nothing here approaches the floor at zero. */
	constexpr float PlentyOfHealth = 1'000'000.0f;

	/** Armour worth having, so ignoring it is worth a measurable amount. */
	constexpr float SomeArmour = 500.0f;

	/**
	 * The weapon damage every fighter below carries.
	 *
	 * ARMING IS NOT OPTIONAL. `UCataclysmSkillEffects::ApplyHit` takes a PERCENT
	 * of the attacker's weapon damage and answers zero outright for a caster
	 * with none, so an unarmed attacker would make every reading below a zero
	 * that looks like a defence working.
	 */
	constexpr float WeaponDamage = 1'000.0f;

	/** One whole swing, as a share of that weapon damage. */
	constexpr float FullSwing = 100.0f;

	/** Half the target's armour ignored, which is a difference nothing hides. */
	constexpr float HalfIgnored = 50.0f;

	/** An evasion chance that stops every direct attack. */
	constexpr float AlwaysEvades = 100.0f;

	struct FScopedFighter
	{
		explicit FScopedFighter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAll =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAll);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Write(Vital::GetMaxHealthAttribute(), PlentyOfHealth);
			Write(Vital::GetHealthAttribute(), PlentyOfHealth);
			Write(Combat::GetAttackDamageAttribute(), WeaponDamage);
		}

		~FScopedFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Write(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Read(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		/** Health back to full, so the next blow is measured on its own. */
		void Heal() const
		{
			Write(Vital::GetHealthAttribute(), PlentyOfHealth);
		}

		/** How much the last blow took off. */
		float HealthLost() const
		{
			return PlentyOfHealth - Read(Vital::GetHealthAttribute());
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/**
	 * A tag container holding `Type.Melee`, the way a weapon skill row does.
	 *
	 * REQUESTED BY NAME RATHER THAN DECLARED NATIVELY, the same reason
	 * `CataclysmMeleeBleedTests.cpp` gives: a native declaration would create
	 * the tag whether or not the vocabulary still lists it, hiding exactly the
	 * disagreement that matters.
	 */
	static FGameplayTagContainer MeleeTags()
	{
		FGameplayTagContainer Tags;
		const FGameplayTag Melee = UCataclysmDamageCalculation::MeleeTag();
		if (Melee.IsValid())
		{
			Tags.AddTag(Melee);
		}
		return Tags;
	}

	/** A blow with no tags at all, which is how a minion's damage is sent. */
	static FGameplayTagContainer NoTags()
	{
		return FGameplayTagContainer();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmIronhideKeepsItsArmourTest,
	"Cataclysm.ForbiddenDefence.IronhideKeepsArmourThatWouldOtherwiseBeIgnored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_keystone_spine_001` Ironhide. Issue #1515.
 *
 * THE READING WITHOUT THE NODE IS HALF THE TEST. Armour penetration working is
 * the rule for every character in the game, so a test that only checked the half
 * with the flag on would pass against a build where penetration never worked at
 * all.
 */
bool FCataclysmIronhideKeepsItsArmourTest::RunTest(const FString&)
{
	using namespace CataclysmForbiddenDefenceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedFighter Attacker(World);
	const FScopedFighter Defender(World);

	Defender.Write(Combat::GetArmorAttribute(), SomeArmour);
	Attacker.Write(Combat::GetArmorPenetrationAttribute(), HalfIgnored);

	// WITHOUT THE KEYSTONE, THE PENETRATION WORKS.
	Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, MeleeTags());
	const float Penetrated = Defender.HealthLost();
	if (!TestTrue(TEXT("the blow did something, which every figure below is a "
					   "difference from"),
				  Penetrated > 0.0f))
	{
		return false;
	}

	// WITH IT, NONE OF THE ARMOUR IS IGNORED, so the same blow does less.
	Defender.Heal();
	Defender.Write(Combat::GetArmorPenetrationSuppressedAttribute(), 1.0f);

	Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, MeleeTags());
	const float Stopped = Defender.HealthLost();

	TestTrue(TEXT("with Ironhide the same blow with the same penetration does "
				  "LESS, because none of the armour is ignored"),
			 Stopped < Penetrated);

	// AND IT IS THE SAME AS AN ATTACKER WITH NO PENETRATION AT ALL, which is
	// what "removes none of it" means. Without this the test would pass against
	// a build that merely reduced penetration rather than refusing it.
	const FScopedFighter Plain(World);
	Defender.Heal();
	Defender.Write(Combat::GetArmorPenetrationSuppressedAttribute(), 0.0f);

	Effects::ApplyHit(Plain.Actor, Defender.Actor, FullSwing, MeleeTags());
	const float NoPenetrationAtAll = Defender.HealthLost();

	TestEqual(TEXT("and it is exactly what an attacker with no penetration does"),
			  Stopped, NoPenetrationAtAll, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEverySwingLandsTest,
	"Cataclysm.ForbiddenDefence.EverySwingLandsCannotBeEvadedInMelee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ravager_keystone_spine_002` Every Swing Lands, first clause. Issue #1515.
 *
 * IT SETS A FLAG THAT ALREADY EXISTED. `FCataclysmIncomingHit::bCannotBeEvaded`
 * was built for the Perfect Aim enemy modifier, and the evasion step already
 * honours it; this node is the first thing to set it from a player's own stat.
 */
bool FCataclysmEverySwingLandsTest::RunTest(const FString&)
{
	using namespace CataclysmForbiddenDefenceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FGameplayTagContainer Melee = MeleeTags();
	if (!TestFalse(TEXT("the vocabulary still has Type.Melee, without which "
						"every reading below would be of the wrong thing"),
				   Melee.IsEmpty()))
	{
		return false;
	}

	const FScopedFighter Attacker(World);
	const FScopedFighter Defender(World);

	Defender.Write(Combat::GetEvasionAttribute(), AlwaysEvades);

	// WITHOUT THE KEYSTONE, A DEFENDER AT FULL EVASION TAKES NOTHING.
	Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, Melee);
	TestEqual(TEXT("without Every Swing Lands a melee blow is evaded entirely"),
			  Defender.HealthLost(), 0.0f, 0.01f);

	// WITH IT, THE SAME BLOW LANDS.
	Attacker.Write(Combat::GetMeleeEvasionSuppressedAttribute(), 1.0f);

	Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, Melee);
	TestTrue(TEXT("with Every Swing Lands the same melee blow lands on the same "
				  "defender"),
			 Defender.HealthLost() > 0.0f);

	// AND TAKING IT AWAY PUTS THE EVASION BACK, which would catch a build that
	// set the flag on the blow once and left it set.
	Attacker.Write(Combat::GetMeleeEvasionSuppressedAttribute(), 0.0f);
	Defender.Heal();

	Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, Melee);
	TestEqual(TEXT("and without it again the blow is evaded"),
			  Defender.HealthLost(), 0.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEverySwingLandsIsMeleeOnlyTest,
	"Cataclysm.ForbiddenDefence.EverySwingLandsLeavesANonMeleeAttackEvadable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The node's own limit. Issue #1515.
 *
 * "Your MELEE attacks cannot be evaded" says nothing about anything else, and a
 * build that read the flag without checking the blow would make a Ravager's
 * every attack unavoidable. That is a much larger keystone than the row states.
 */
bool FCataclysmEverySwingLandsIsMeleeOnlyTest::RunTest(const FString&)
{
	using namespace CataclysmForbiddenDefenceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedFighter Attacker(World);
	const FScopedFighter Defender(World);

	Defender.Write(Combat::GetEvasionAttribute(), AlwaysEvades);
	Attacker.Write(Combat::GetMeleeEvasionSuppressedAttribute(), 1.0f);

	// THE MELEE BLOW LANDS, which is the state this test is a limit on. Without
	// this reading the test would pass against a build where the keystone did
	// nothing at all.
	Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, MeleeTags());
	if (!TestTrue(TEXT("the keystone is working, so a melee blow lands"),
				  Defender.HealthLost() > 0.0f))
	{
		return false;
	}

	// AND A BLOW THAT IS NOT MELEE IS STILL EVADED.
	Defender.Heal();
	Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, NoTags());

	TestEqual(TEXT("a blow that is not melee is still evaded by the same "
				   "defender, from the same attacker, in the same breath"),
			  Defender.HealthLost(), 0.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionBlowStillEvadableTest,
	"Cataclysm.ForbiddenDefence.AMinionsBlowIsStillEvadableWhileItsSummonerHoldsEverySwingLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A summoner's keystone does not cross to its minions. Issue #1515.
 *
 * WHY THIS ONE MATTERS MORE THAN THE FOUR BESIDE IT. Four capabilities are kept
 * from crossing to a minion's blow by name, in `MinionDelivery` in
 * `CataclysmMinion.cpp`: critical strike, penetration, weapon sub-type and
 * leech. THIS ONE IS NAMED NOWHERE. Nothing sets a flag for it, and until issue
 * #1515 it was blocked only by an accident of delivery -- a minion's blow
 * carries no melee tag, and the suppression is read only for a melee blow. This
 * case's own comment said so, and warned that giving a minion's blow a melee tag
 * would break the rule while this test went on passing.
 *
 * IT IS STRUCTURAL NOW, WHICH IS WHAT THE CASE MEASURES. Since 2026-09-17 the
 * minion is the instigator of its own blow, so the suppression is looked for on
 * the MINION, which has no combat attribute set to hold it. A summoner's
 * keystone cannot reach a minion's blow whatever tags that blow carries, and a
 * capability added later is blocked for the same reason rather than by somebody
 * remembering to add a flag. The design's rule is "a minion reaches its summoner
 * through exactly three channels, and nothing else crosses".
 *
 * SO IT SUMMONS A REAL IMP AND TELLS IT TO ATTACK, rather than sending a blow
 * shaped like a minion's from the summoner. The stand-in it used to be could not
 * have caught the fault it warned about.
 */
bool FCataclysmMinionBlowStillEvadableTest::RunTest(const FString&)
{
	using namespace CataclysmForbiddenDefenceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedFighter Summoner(World);
	const FScopedFighter Defender(World);
	const FScopedFighter Unready(World);

	Defender.Write(Combat::GetEvasionAttribute(), AlwaysEvades);
	Summoner.Write(Combat::GetMeleeEvasionSuppressedAttribute(), 1.0f);

	// THE SUMMONER'S OWN MELEE STILL LANDS, so this test is not passing because
	// the keystone is broken.
	Effects::ApplyHit(Summoner.Actor, Defender.Actor, FullSwing, MeleeTags());
	if (!TestTrue(TEXT("the summoner's own melee lands, so the keystone is on"),
				  Defender.HealthLost() > 0.0f))
	{
		return false;
	}

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(200.0f, 0.0f, 0.0f), /*Lifetime=*/20.0f,
		/*bBurns=*/false, /*TypeName=*/TEXT("Imp"));
	if (!TestNotNull(TEXT("a minion of its own"), Imp))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	// THE IMP'S BLOW LANDS ON A DEFENDER THAT DOES NOT EVADE, which is the
	// control this case needs: without it, a minion that dealt nothing at all
	// would pass the reading below and look like the rule holding.
	const float UnreadyBefore = Unready.HealthLost();
	Imp->AttackTarget(Unready.Actor);
	if (!TestTrue(TEXT("the imp's blow lands on a defender with no evasion"),
				  Unready.HealthLost() > UnreadyBefore))
	{
		return false;
	}

	// AND IT IS EVADED BY THE ONE THAT EVADES EVERYTHING, although its summoner
	// holds the keystone that would stop exactly that.
	Defender.Heal();
	Imp->AttackTarget(Defender.Actor);

	TestEqual(TEXT("the imp's blow is still evaded, so its summoner's keystone "
				   "did not reach it"),
			  Defender.HealthLost(), 0.0f, 0.01f);

	// AND THE REASON IS ASSERTED, NOT ONLY THE OUTCOME, because the outcome
	// alone cannot tell the new reason from the old one. A minion's blow carries
	// no melee tag, so it would be evaded here even if the summoner were still
	// the instigator -- which is exactly the weakness the comment above records.
	// What is new is that the suppression is looked for on the MINION, and a
	// minion has no combat attribute set for it to live on. If somebody gives a
	// minion one, this line fails and the case above has to be rethought rather
	// than quietly going on passing.
	if (const UAbilitySystemComponent* ImpSystem =
			UCataclysmTargeting::AbilitySystemOf(Imp))
	{
		TestFalse(TEXT("and the imp has no combat attribute set for a melee "
					   "evasion suppression to be read from at all"),
				  ImpSystem->HasAttributeSetForAttribute(
					  Combat::GetMeleeEvasionSuppressedAttribute()));
	}
	else
	{
		AddError(TEXT("the imp has no ability system to ask."));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmForbiddenDefencesAreDistinctTest,
	"Cataclysm.ForbiddenDefence.TheTwoKeystonesForbidTwoDifferentDefences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The two keystones are two separate rules. Issue #1515.
 *
 * WHAT THIS CATCHES THAT THE TESTS ABOVE DO NOT. Each of those turns one flag on
 * and checks one thing happens. Both still pass if the two flags are wired to
 * one attribute, or if either reads the other's: a build where holding one
 * forbade both defences satisfies every one of them.
 *
 * AND THE RISK IS REAL HERE RATHER THAN THEORETICAL. Both stats are named
 * `..._suppressed`, both are read during one blow, and their accessors differ by
 * a few characters.
 *
 * EACH HALF MAKES TWO CLAIMS, AND THE FIRST VERSION OF THIS TEST MADE ONLY ONE.
 * It asserted that the defence a keystone does NOT forbid still worked, and
 * never that the keystone itself did anything. A guard proof found it: breaking
 * Ironhide outright failed the Ironhide test and left this one passing, because
 * the half holding Ironhide was only checking that the blow was still evaded --
 * which a broken Ironhide does not change.
 *
 * SO A TEST OF THIS SHAPE HAS TO SAY BOTH THINGS PER HALF: this flag works, AND
 * the other defence is untouched. With only the second it cannot tell "two
 * separate rules" from "two rules that both do nothing", which is the exact
 * fault it exists to catch.
 */
bool FCataclysmForbiddenDefencesAreDistinctTest::RunTest(const FString&)
{
	using namespace CataclysmForbiddenDefenceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// --- IRONHIDE ALONE: IT WORKS, AND EVASION IS UNTOUCHED ---------------
	//
	// TWO BLOWS, BECAUSE THE TWO CLAIMS CANNOT SHARE ONE. While the defender is
	// evading everything there is no damage to measure, so the armour claim
	// needs a blow that lands; and a blow that lands says nothing about evasion.
	// The evasion is turned off between them and nothing else changes.
	{
		const FScopedFighter Attacker(World);
		const FScopedFighter Defender(World);
		const FScopedFighter Plain(World);

		Defender.Write(Combat::GetArmorAttribute(), SomeArmour);
		Defender.Write(Combat::GetArmorPenetrationSuppressedAttribute(), 1.0f);
		Attacker.Write(Combat::GetArmorPenetrationAttribute(), HalfIgnored);

		// THE DEFENCE THIS KEYSTONE DOES NOT FORBID IS UNTOUCHED.
		Defender.Write(Combat::GetEvasionAttribute(), AlwaysEvades);
		Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, MeleeTags());
		TestEqual(TEXT("Ironhide alone leaves the defender's evasion working"),
				  Defender.HealthLost(), 0.0f, 0.01f);

		// AND THE KEYSTONE ITSELF WORKS, which the half above cannot say and
		// without which this test cannot tell two separate rules from two rules
		// that both do nothing.
		Defender.Write(Combat::GetEvasionAttribute(), 0.0f);
		Defender.Heal();
		Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, MeleeTags());
		const float AgainstIronhide = Defender.HealthLost();

		Defender.Heal();
		Effects::ApplyHit(Plain.Actor, Defender.Actor, FullSwing, MeleeTags());
		const float FromAnAttackerWithNone = Defender.HealthLost();

		TestEqual(TEXT("and Ironhide itself is working, so an attacker with "
					   "penetration does exactly what one with none does"),
				  AgainstIronhide, FromAnAttackerWithNone, 0.01f);
	}

	// --- EVERY SWING LANDS ALONE: IT WORKS, AND ARMOUR IS UNTOUCHED -------
	//
	// THE DEFENDER EVADES EVERYTHING HERE, which is what makes the first claim
	// a claim at all: a blow landing on a defender with no evasion would say
	// nothing about the keystone. BOTH attackers hold it, so both blows land
	// and the difference between them is penetration and nothing else.
	{
		const FScopedFighter Attacker(World);
		const FScopedFighter NoPenetration(World);
		const FScopedFighter Defender(World);

		Defender.Write(Combat::GetArmorAttribute(), SomeArmour);
		Defender.Write(Combat::GetEvasionAttribute(), AlwaysEvades);

		Attacker.Write(Combat::GetArmorPenetrationAttribute(), HalfIgnored);
		Attacker.Write(Combat::GetMeleeEvasionSuppressedAttribute(), 1.0f);
		NoPenetration.Write(Combat::GetMeleeEvasionSuppressedAttribute(), 1.0f);

		Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, MeleeTags());
		const float WithPenetration = Defender.HealthLost();

		// THE KEYSTONE ITSELF WORKS: a defender at full evasion took damage.
		TestTrue(TEXT("Every Swing Lands itself is working, so a blow lands on a "
					  "defender that evades everything"),
				 WithPenetration > 0.0f);

		Defender.Heal();
		Effects::ApplyHit(NoPenetration.Actor, Defender.Actor, FullSwing,
						  MeleeTags());
		const float WithoutPenetration = Defender.HealthLost();

		// AND THE DEFENCE IT DOES NOT FORBID IS UNTOUCHED.
		TestTrue(TEXT("and it leaves the attacker's armour penetration working, "
					  "so it still does more than an attacker with none"),
				 WithPenetration > WithoutPenetration);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
