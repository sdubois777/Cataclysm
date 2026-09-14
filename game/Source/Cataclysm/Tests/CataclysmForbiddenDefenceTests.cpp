// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
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
 * WHY THIS TEST EXISTS AT ALL. A minion's damage is dealt in its summoner's
 * NAME, so the attacker whose attributes are read when the blow is assembled is
 * the player. Four other things are blocked from crossing that way by name --
 * `MinionDelivery` in `CataclysmMinion.cpp` sets `bCannotCriticallyStrike`,
 * `bCannotPenetrate`, `bCarriesNoWeaponSubType` and `bCannotLeech`. This one is
 * blocked by the melee gate instead, because a minion's blow carries no melee
 * tag: both delivery calls in that file pass an empty tag container.
 *
 * SO THE EXCLUSION IS TRUE TODAY BY HOW MINION DAMAGE IS DELIVERED, NOT BY
 * ANYTHING STATING IT, and that is exactly why it is asserted rather than
 * trusted. The design's rule is "a minion reaches its summoner through exactly
 * three channels, and nothing else crosses".
 *
 * WHAT THIS TEST IS AND IS NOT. It sends the blow the way a minion's is sent --
 * from the summoner, with an empty tag container -- rather than spawning a
 * minion and driving its brain. That is a stand-in, and it is named as one: it
 * proves the melee gate holds for the shape a minion's blow has. If somebody
 * ever gives a minion's blow a melee tag, this test keeps passing and the rule
 * breaks, so the comment in `CataclysmVitalAttributeSet.cpp` at the set site
 * carries the warning as well.
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

	// AND THE BLOW SENT THE WAY A MINION'S IS SENT DOES NOT.
	Defender.Heal();
	Effects::ApplyHit(Summoner.Actor, Defender.Actor, FullSwing, NoTags());

	TestEqual(TEXT("a blow carrying no tags, which is how a minion's damage is "
				   "delivered, is still evaded"),
			  Defender.HealthLost(), 0.0f, 0.01f);

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
 * a few characters. The energy-shield keystones had the same shape and a guard
 * proof on exactly this leak was the only one that the single tests could not
 * catch.
 *
 * THE TWO SIT ON OPPOSITE SIDES, so each is given to the character that should
 * NOT benefit, and the defence it does not forbid is checked still working.
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

	// --- IRONHIDE ALONE DOES NOT STOP EVASION WORKING --------------------
	{
		const FScopedFighter Attacker(World);
		const FScopedFighter Defender(World);

		Defender.Write(Combat::GetEvasionAttribute(), AlwaysEvades);
		Defender.Write(Combat::GetArmorPenetrationSuppressedAttribute(), 1.0f);

		Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, MeleeTags());
		TestEqual(TEXT("Ironhide alone leaves the defender's evasion working"),
				  Defender.HealthLost(), 0.0f, 0.01f);
	}

	// --- EVERY SWING LANDS ALONE DOES NOT PROTECT ARMOUR ------------------
	{
		const FScopedFighter Attacker(World);
		const FScopedFighter Defender(World);
		const FScopedFighter Plain(World);

		Defender.Write(Combat::GetArmorAttribute(), SomeArmour);
		Attacker.Write(Combat::GetArmorPenetrationAttribute(), HalfIgnored);
		Attacker.Write(Combat::GetMeleeEvasionSuppressedAttribute(), 1.0f);

		Effects::ApplyHit(Attacker.Actor, Defender.Actor, FullSwing, MeleeTags());
		const float WithPenetration = Defender.HealthLost();

		Defender.Heal();
		Effects::ApplyHit(Plain.Actor, Defender.Actor, FullSwing, MeleeTags());
		const float WithoutPenetration = Defender.HealthLost();

		TestTrue(TEXT("Every Swing Lands alone leaves the attacker's armour "
					  "penetration working, so it still does more than an "
					  "attacker with none"),
				 WithPenetration > WithoutPenetration);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
