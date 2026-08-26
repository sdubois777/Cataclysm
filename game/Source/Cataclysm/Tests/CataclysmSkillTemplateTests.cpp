// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCastEffect.h"
// For the Fervour pool a health cost fills. Issue #954.
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStrikeEffect.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayTagsManager.h"
#include "GameFramework/Actor.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/ScopeExit.h"
// For pinning the critical strike roll in the one test whose subject it is.
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for what the seven shared skill templates actually do.
 *
 * WHAT WAS TRUE BEFORE THESE. Every ability in the project was
 * UCataclysmUndesignedSkill, which occupies a slot and ends immediately, so
 * there was nothing to test and the existing weapon slot tests say so in their
 * own comment. These are the first tests in the project that assert a skill
 * changed anything in the world.
 *
 * WHAT THEY DO NOT COVER, said plainly:
 *
 *   Anything on a timer. Repeating strikes, projectile flight time, aura pulses
 *   and rift spawns all run through the timer manager, and these tests use a
 *   world that is never ticked. Each of those is driven directly instead --
 *   SwingOnce, Land, Pulse, SummonOne are public for exactly this reason -- so
 *   what one repetition DOES is covered and the scheduling of them is not.
 *
 *   The magnitude of a DEBUFF. Subjugate applies the Status.Madness tag, which
 *   is real and is covered, but a debuff that has a number to apply has no
 *   route to apply it. A self buff now does; see the buff tests at the end.
 *
 *   Martyr's Ember's store. It needs a damage-taken hook that does not exist.
 *   Issue #192.
 */

namespace CataclysmSkillTest
{
	constexpr float M = 100.0f;

	/** What the test caster's weapon deals, so damage numbers are exact. */
	constexpr float WeaponDamage = 100.0f;

	static UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}

	/** An actor a sphere overlap can find, carrying health and mana. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, const FVector& Where)
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

			UCataclysmVitalAttributeSet* Vitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(Vitals);

			UCataclysmCombatAttributeSet* Combat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(Combat);

			// FERVOUR, SO A SKILL THAT COSTS HEALTH CAN BE SEEN TO FILL IT.
			// Issue #954. Only the Blood Pyre test below reads it; the set costs
			// the others nothing, and without it a caster has no Fervour pool at
			// all and the whole health-cost path is unreachable from here.
			UCataclysmClassResourceAttributeSet* Resource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(Resource);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// Big enough that nothing in these tests dies part way through and
			// changes what a later assertion sees.
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), WeaponDamage);
		}

		~FScopedFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		float Health() const { return Get(UCataclysmVitalAttributeSet::GetHealthAttribute()); }
		float Mana() const { return Get(UCataclysmVitalAttributeSet::GetManaAttribute()); }
		float Fervour() const
		{
			return Get(
				UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Grant a template into a slot and return its live instance.
	 *
	 * Granted at level 100 so mana costs are the figures the Skill Slots sheet
	 * quotes, rather than the level 1 share of them.
	 */
	template <typename T>
	T* GrantSkill(FScopedFighter& Caster, ECataclysmAbilitySlot Slot,
				  const FString& ParamText, const FString& Name = TEXT("Test Skill"),
				  const FString& TagCell = FString())
	{
		const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
			T::StaticClass(), Slot, /*Level=*/100, Caster.Actor);
		if (!Handle.IsValid())
		{
			return nullptr;
		}

		FGameplayAbilitySpec* Spec = Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		T* Instance = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			Instance->SkillName = Name;
			Instance->Params = UCataclysmSkillShapes::ParseParams(ParamText);

			// The Tags cell, read the same way UCataclysmWeaponSkills reads it,
			// so a test cannot pass with a tag the real path would refuse.
			Instance->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
		}
		return Instance;
	}

	/** Activate a granted ability and report whether it started. */
	bool Activate(FScopedFighter& Caster, UGameplayAbility* Ability)
	{
		return Ability && Caster.AbilitySystem->TryActivateAbility(
			Ability->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
	}

	/** The only patch of burning ground in the world, or null if there is not
	 *  exactly one. */
	ACataclysmGroundZone* TheOnlyGroundZone(UWorld* World)
	{
		ACataclysmGroundZone* Found = nullptr;
		int32 Count = 0;
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			Found = *It;
			++Count;
		}
		return Count == 1 ? Found : nullptr;
	}
}

// --------------------------------------------------------------------------
// Cost and cooldown. This is the bug the templates exposed.
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillCommitsTest,
	"Cataclysm.Skills.UsingASkillSpendsManaAndStartsItsCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillCommitsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(2 * M, 0, 0));

	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const float Cost = Strike->GetManaCost();
	const float Cooldown = Strike->GetBaseCooldown();

	// Both come from the Skill Slots sheet's Heavy row: 15 mana at level 100 and
	// a 1.5 second cooldown. Checked here because a cost of zero would make the
	// rest of this test pass without proving anything.
	TestTrue(FString::Printf(TEXT("The Heavy slot costs mana (%.1f)"), Cost),
		Cost > 0.0f);
	TestTrue(FString::Printf(TEXT("The Heavy slot waits (%.1fs)"), Cooldown),
		Cooldown > 0.0f);

	const float ManaBefore = Caster.Mana();
	TestTrue(TEXT("The strike activates"), Activate(Caster, Strike));

	// ISSUE #155 BUILT ApplyCost AND ApplyCooldown AND NOTHING CALLED THEM. The
	// engine runs them from CommitAbility, and the only ability that existed was
	// the placeholder, which ends immediately and commits nothing. So mana was
	// checked and never spent and cooldowns were checked and never started.
	// Removing CommitAndBegin from the templates fails these two lines.
	TestEqual(TEXT("The mana was spent"), Caster.Mana(), ManaBefore - Cost);

	const FGameplayTag CooldownTag =
		UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("The slot's cooldown tag is present"),
		Caster.AbilitySystem->HasMatchingGameplayTag(CooldownTag));

	// And the cooldown actually refuses a second use.
	TestFalse(TEXT("It cannot be used again while it is waiting"),
		Activate(Caster, Strike));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillRefusedWithoutManaTest,
	"Cataclysm.Skills.ASkillIsRefusedWhenTheManaIsNotThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillRefusedWithoutManaTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(2 * M, 0, 0));

	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	Caster.Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1.0f);

	const float HealthBefore = Enemy.Health();
	TestFalse(TEXT("A skill nobody can pay for does not activate"),
		Activate(Caster, Strike));
	TestEqual(TEXT("And it hits nothing"), Enemy.Health(), HealthBefore);
	TestEqual(TEXT("And it spends nothing"), Caster.Mana(), 1.0f);

	return true;
}

// --------------------------------------------------------------------------
// Strike
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeTest,
	"Cataclysm.Skills.AStrikeHitsInFrontAndNotBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStrikeTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter InFront(World, FVector(2 * M, 0, 0));
	FScopedFighter Behind(World, FVector(-2 * M, 0, 0));
	FScopedFighter TooFar(World, FVector(9 * M, 0, 0));

	// There is no player controller in this world, so AimPoint falls back to the
	// caster's own position and the cone uses the actor's forward vector, which
	// is +X. That is exactly the case being tested.
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=120; Burn=1"),
		TEXT("Molten Cleave"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const float FrontBefore = InFront.Health();
	const float BehindBefore = Behind.Health();
	const float FarBefore = TooFar.Health();

	TestTrue(TEXT("It activates"), Activate(Caster, Strike));
	TestEqual(TEXT("One swing landed"), Strike->SwingsMade, 1);

	TestTrue(TEXT("The enemy in front took damage"), InFront.Health() < FrontBefore);
	TestEqual(TEXT("The enemy behind did not"), Behind.Health(), BehindBefore);
	TestEqual(TEXT("The enemy beyond the radius did not"), TooFar.Health(), FarBefore);

	// The Heavy slot is 250% of weapon damage. With no armor, resistance,
	// evasion or block on the target, all of it reaches health.
	const float Expected = WeaponDamage * 250.0f / 100.0f;
	TestEqual(TEXT("It dealt the Heavy slot's 250% of weapon damage"),
		FrontBefore - InFront.Health(), Expected);

	// BURN IS A SHARE OF THE HIT, so the tag proves the rider fired and the
	// damage over time is separately covered.
	TestTrue(TEXT("The enemy in front is alight"),
		UCataclysmSkillEffects::HasTag(InFront.Actor, UCataclysmSkillEffects::BurnTag()));
	TestFalse(TEXT("The enemy behind is not"),
		UCataclysmSkillEffects::HasTag(Behind.Actor, UCataclysmSkillEffects::BurnTag()));

	return true;
}

/**
 * A skill's own critical strike chance reaches the blow it deals. Issue #657.
 *
 * THE LAST LINK IN THE CHAIN, and the one the other tests cannot reach.
 * `Cataclysm.Crit.*` proves that a hit carrying a chance uses it and that a row
 * stating one produces a skill stating one. Neither proves that a running skill
 * puts its own figure onto the blows it lands, which is what
 * `UCataclysmSkillTemplate::HitTargets` has to do, and every one of the seven
 * shared templates deals its damage through that one function.
 *
 * THE CASTER'S OWN CHANCE IS ZERO ON PURPOSE. If the skill's figure were ignored
 * and the character's read instead, the hit would not critically strike at all
 * and the reading would be the plain 250%.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillSendsItsOwnCritChanceTest,
	"Cataclysm.Skills.ASkillsOwnCriticalStrikeChanceReachesWhatItHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillSendsItsOwnCritChanceTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// EVERY CHANCE ABOVE ZERO BEATS A ROLL OF ZERO, so the roll is decided and
	// this test is not a coin toss. Restored when it leaves scope.
	const CataclysmTestWorld::FScopedCritRoll AlwaysRolls(0.0f);

	// The Heavy slot deals 250% of weapon damage, and a target with no armour,
	// resistance, evasion or block takes all of it.
	const float Plain = WeaponDamage * 250.0f / 100.0f;

	{
		FScopedFighter Caster(World, FVector::ZeroVector);
		FScopedFighter Target(World, FVector(2 * M, 0, 0));

		// THE CHARACTER NEVER CRITICALLY STRIKES. Only the skill does.
		Caster.Set(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
		Caster.Set(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(), 200.0f);

		UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
			Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=120"),
			TEXT("Precise Cut"));
		if (!Strike)
		{
			AddError(TEXT("Could not grant the strike that states a chance."));
			return false;
		}
		Strike->CritChancePercent = 100.0f;

		const float Before = Target.Health();
		TestTrue(TEXT("the skill that states a chance activates"),
			Activate(Caster, Strike));

		// THE MULTIPLIER IS STILL THE CHARACTER'S, which is the design and not an
		// oversight: only the CHANCE is stated by the skill. So 250% doubled.
		TestEqual(TEXT("a skill stating 100% critically strikes, at its "
					   "character's multiplier"),
			Before - Target.Health(), Plain * 2.0f);
	}

	{
		// THE SAME SKILL SAYING NOTHING, which is every skill in the game today,
		// and what makes the reading above a rule rather than damage that changed
		// for some other reason.
		FScopedFighter Caster(World, FVector::ZeroVector);
		FScopedFighter Target(World, FVector(2 * M, 0, 0));

		Caster.Set(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
		Caster.Set(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(), 200.0f);

		UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
			Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=120"),
			TEXT("Wild Swing"));
		if (!Strike)
		{
			AddError(TEXT("Could not grant the strike that states nothing."));
			return false;
		}
		TestEqual(TEXT("a granted skill states no chance until one is stamped on it"),
			Strike->CritChancePercent, -1.0f);

		const float Before = Target.Health();
		TestTrue(TEXT("the skill that states nothing activates"),
			Activate(Caster, Strike));
		TestEqual(TEXT("so it takes its character's 0% and does not critically strike"),
			Before - Target.Health(), Plain);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStrikeSingleTargetTest,
	"Cataclysm.Skills.AStrikeCappedToOneTargetHitsTheNearest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStrikeSingleTargetTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Nearer(World, FVector(1 * M, 0, 0));
	FScopedFighter Farther(World, FVector(2 * M, 0, 0));

	// Searing Hook: "massive damage to a single target, knocks them back 4
	// meters".
	UCataclysmStrikeSkill* Hook = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=2.5; Angle=60; MaxTargets=1; Knockback=4"),
		TEXT("Searing Hook"));
	if (!Hook)
	{
		AddError(TEXT("Could not grant the hook."));
		return false;
	}

	const float NearerBefore = Nearer.Health();
	const float FartherBefore = Farther.Health();
	const FVector NearerWhere = Nearer.Actor->GetActorLocation();

	TestTrue(TEXT("It activates"), Activate(Caster, Hook));

	TestTrue(TEXT("The nearest took damage"), Nearer.Health() < NearerBefore);
	TestEqual(TEXT("The second one did not, because the cap is one"),
		Farther.Health(), FartherBefore);

	// Knocked directly away from the caster, so 4 metres further along +X.
	const FVector Moved = Nearer.Actor->GetActorLocation() - NearerWhere;
	TestTrue(FString::Printf(TEXT("It was knocked back about 4 metres (moved %.0f cm)"),
		Moved.Size()), Moved.Size() > 3.5f * M);
	TestTrue(TEXT("Away from the caster, not towards"), Moved.X > 0.0f);

	return true;
}

/**
 * A skill that is not a Strike can knock back, because knockback is a rider.
 *
 * WHAT WENT WRONG. Issue #626: `Knockback` was a parameter of the `Strike` shape
 * alone, and the code that applied it was written inline in
 * UCataclysmStrikeSkill::SwingOnce. So Shockwave Leap, a Movement skill whose
 * description reads "knocks back all enemies within 5 meters", could not state a
 * distance at all -- the generator refused the parameter on its shape -- and
 * would not have shoved anything if it had.
 *
 * WHY A MOVEMENT SKILL IS THE ONE TESTED. It is the shape the real skill uses,
 * and it reaches the shared hit path by a completely different route from a
 * Strike: it moves the caster first and hits what it arrives among. If knockback
 * had been moved somewhere only a Strike passes through, this is what would say
 * so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKnockbackIsARiderTest,
	"Cataclysm.Skills.AMovementSkillCanKnockBackBecauseKnockbackIsARider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmKnockbackIsARiderTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// Standing where the leap lands, so it is inside the shockwave.
	FScopedFighter Caught(World, FVector(10 * M, 0, 0));

	// Shockwave Leap's cell, verbatim from the Weapon Skills sheet.
	UCataclysmMovementSkill* Leap = GrantSkill<UCataclysmMovementSkill>(
		Caster, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Leap; Range=9; Radius=5; Knockback=3; Effect=Stun; StunSeconds=1"),
		TEXT("Shockwave Leap"));
	if (!Leap)
	{
		AddError(TEXT("Could not grant the leap."));
		return false;
	}

	const FVector CaughtWhere = Caught.Actor->GetActorLocation();
	const float CaughtBefore = Caught.Health();

	TestTrue(TEXT("It activates"), Activate(Caster, Leap));
	TestTrue(TEXT("The enemy it landed among took damage"),
		Caught.Health() < CaughtBefore);

	// THE ASSERTION THE ISSUE IS ABOUT. Before this change a Movement skill could
	// not carry a Knockback at all, and nothing applied one outside a Strike.
	const FVector Moved = Caught.Actor->GetActorLocation() - CaughtWhere;
	TestTrue(FString::Printf(
		TEXT("It was shoved about 3 metres by a Movement skill (moved %.0f cm)"),
		Moved.Size()), Moved.Size() > 2.5f * M);

	// Away from where the caster ENDED UP, which for a leap is where it landed
	// rather than where it started. Getting this wrong would push the target
	// towards the player, which is the opposite of a knockback.
	const FVector AwayFromCaster =
		Caught.Actor->GetActorLocation() - Caster.Actor->GetActorLocation();
	TestTrue(TEXT("and away from the caster rather than towards it"),
		FVector::DotProduct(Moved.GetSafeNormal(),
							AwayFromCaster.GetSafeNormal()) > 0.0f);

	return true;
}

/**
 * Each shove inside the window moves a target half as far as the one before.
 *
 * THE RULE, from "Stun and the Anti-Stun-Lock Rule" in
 * docs/Cataclysm_GDD_v2.md and decided on issue #302: a displacement applied to
 * a target already displaced within the last 5 seconds moves it half as far, and
 * the count resets once 5 seconds pass with no displacement at all. It was
 * stated in the design and implemented nowhere until issue #628.
 *
 * WHAT IT PREVENTS. Without it, three shoves of 4 metres move a target 12 metres
 * and a fourth moves it 4 more, so repeated displacement can hold a target at the
 * far end of a room. With it they move 4, then 2, then 1 -- seven metres in total
 * and nothing worth measuring after that.
 *
 * THE DISTANCES ARE COMPARED TO EACH OTHER RATHER THAN TO WRITTEN NUMBERS, so
 * this says what the rule says: each is half the one before. Changing the stated
 * knockback would not need this test edited.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDiminishingDisplacementTest,
	"Cataclysm.Skills.EachShoveInsideTheWindowMovesHalfAsFar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDiminishingDisplacementTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Target(World, FVector(2 * M, 0, 0));

	UCataclysmAbilitySystemComponent* Pushed = Target.AbilitySystem;
	if (!Pushed)
	{
		AddError(TEXT("The target has no ability system, so it cannot hold a "
					  "displacement count."));
		return false;
	}

	TestEqual(TEXT("It has not been displaced yet"),
		Pushed->DisplacementsInWindow(), 0);

	// A wide ring with a long radius, so the target stays inside it as it is
	// pushed away and every activation finds it again.
	UCataclysmStrikeSkill* Shove = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=20; Angle=360; Knockback=4"), TEXT("A Shove"));
	if (!Shove)
	{
		AddError(TEXT("Could not grant the shove."));
		return false;
	}

	// ACTIVATED ONCE AND THEN SWUNG DIRECTLY, because a Heavy slot skill goes on a
	// 1.5 second cooldown and an automation test cannot wait for it. The first
	// activation is what proves a shove reaches this path through a real ability
	// at all; the repeats are what the halving is a property of.
	float Distances[3] = { 0.0f, 0.0f, 0.0f };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FVector Before = Target.Actor->GetActorLocation();
		if (Index == 0)
		{
			TestTrue(TEXT("The first shove activates as an ability"),
				Activate(Caster, Shove));
		}
		else
		{
			TestTrue(FString::Printf(TEXT("Shove %d hits something"), Index + 1),
				Shove->SwingOnce() > 0);
		}
		Distances[Index] = (Target.Actor->GetActorLocation() - Before).Size();
	}

	TestEqual(TEXT("Three displacements were counted"),
		Pushed->DisplacementsInWindow(), 3);
	TestTrue(FString::Printf(TEXT("The first shove moved it (%.0f cm)"),
		Distances[0]), Distances[0] > 1.0f);

	// Half, then a quarter. A centimetre of slack, because the move is swept and
	// stops short if it meets anything.
	TestTrue(FString::Printf(
		TEXT("The second moved half as far (%.0f then %.0f cm)"),
		Distances[0], Distances[1]),
		FMath::IsNearlyEqual(Distances[1], Distances[0] * 0.5f, 1.0f));
	TestTrue(FString::Printf(
		TEXT("The third moved a quarter as far (%.0f then %.0f cm)"),
		Distances[0], Distances[2]),
		FMath::IsNearlyEqual(Distances[2], Distances[0] * 0.25f, 1.0f));

	// AND IT CANNOT HOLD A TARGET ACROSS A ROOM. Three full shoves would be three
	// times the first; the rule makes the total 1.75 times it.
	const float Total = Distances[0] + Distances[1] + Distances[2];
	TestTrue(FString::Printf(
		TEXT("Three shoves total under twice the first, not three times "
			 "(%.0f vs %.0f cm)"), Total, Distances[0] * 3.0f),
		Total < Distances[0] * 2.0f);

	return true;
}

/**
 * The count resets once the window passes with no displacement.
 *
 * The other half of the rule. Without it a target shoved once would be
 * permanently harder to shove, which would make the halving a punishment for
 * having ever been hit rather than a limit on being hit repeatedly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDisplacementWindowResetsTest,
	"Cataclysm.Skills.TheDisplacementCountResetsAfterTheWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDisplacementWindowResetsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Target(World, FVector(2 * M, 0, 0));

	UCataclysmAbilitySystemComponent* Pushed = Target.AbilitySystem;
	if (!Pushed)
	{
		AddError(TEXT("The target has no ability system."));
		return false;
	}

	// DRIVEN DIRECTLY RATHER THAN THROUGH A SKILL, because the window is five
	// seconds of world time and an automation test cannot wait that long. What
	// is under test here is the rule itself, which the reset is a property of.
	TestEqual(TEXT("The first displacement is the full distance"),
		Pushed->TakeNextDisplacementShare(), 1.0f);
	TestEqual(TEXT("The second is half"),
		Pushed->TakeNextDisplacementShare(), 0.5f);
	TestEqual(TEXT("Two are counted"), Pushed->DisplacementsInWindow(), 2);

	// Past the window. The rule reuses the stun immunity window rather than
	// stating a second number, so this reads that constant instead of writing
	// five again -- and would fail if the two ever stopped being the same.
	World->TimeSeconds +=
		UCataclysmSkillEffects::StunImmunityWindowSeconds + 1.0f;

	TestEqual(TEXT("The count reads zero once the window has passed"),
		Pushed->DisplacementsInWindow(), 0);
	TestEqual(TEXT("and the next displacement is the full distance again"),
		Pushed->TakeNextDisplacementShare(), 1.0f);
	TestEqual(TEXT("counted as the first of a new window"),
		Pushed->DisplacementsInWindow(), 1);

	return true;
}

/**
 * A skill that states no knockback moves nothing.
 *
 * The other half of the test above. Moving knockback into the shared hit path
 * means every skill in the game now runs that code, so a skill with no
 * `Knockback` must still leave its targets exactly where they were. Without this
 * check, a change that shoved on every hit regardless of the parameter would
 * pass everything above.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoKnockbackMovesNothingTest,
	"Cataclysm.Skills.ASkillThatStatesNoKnockbackMovesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNoKnockbackMovesNothingTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Target(World, FVector(2 * M, 0, 0));

	UCataclysmStrikeSkill* Plain = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=4; Angle=180"), TEXT("A Strike With No Knockback"));
	if (!Plain)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const FVector Where = Target.Actor->GetActorLocation();
	const float Before = Target.Health();

	TestTrue(TEXT("It activates"), Activate(Caster, Plain));
	TestTrue(TEXT("The target took damage"), Target.Health() < Before);
	TestTrue(TEXT("and did not move"),
		Target.Actor->GetActorLocation().Equals(Where, 1.0f));

	return true;
}

// --------------------------------------------------------------------------
// Projectile
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPiercingProjectileTest,
	"Cataclysm.Skills.APiercingProjectileHitsEveryoneOnItsLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPiercingProjectileTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter First(World, FVector(3 * M, 0, 0));
	FScopedFighter Second(World, FVector(7 * M, 0, 0));
	FScopedFighter Beside(World, FVector(5 * M, 5 * M, 0));

	// Infernal Lance. Speed of zero means it arrives at once, so no timer is
	// needed and the activation resolves the hit itself.
	UCataclysmProjectileSkill* Lance = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Range=12; Radius=1.5; Pierce=99; Speed=0"), TEXT("Infernal Lance"));
	if (!Lance)
	{
		AddError(TEXT("Could not grant the lance."));
		return false;
	}

	const float FirstBefore = First.Health();
	const float SecondBefore = Second.Health();
	const float BesideBefore = Beside.Health();

	TestTrue(TEXT("It activates"), Activate(Caster, Lance));
	TestEqual(TEXT("A beam lands once, with no flight time"), Lance->Landings, 1);

	TestTrue(TEXT("The first on the line was hit"), First.Health() < FirstBefore);
	TestTrue(TEXT("So was the second, because it pierces"),
		Second.Health() < SecondBefore);
	TestEqual(TEXT("One standing beside the line was not"),
		Beside.Health(), BesideBefore);

	return true;
}

/**
 * A projectile carries the firing skill's critical strike chance. Issue #657.
 *
 * WHY A PROJECTILE NEEDS ITS OWN TEST. It is the one thing that lands after the
 * ability that made it has already ended, so it cannot read anything off the
 * character when it arrives -- by then the character may be using a different
 * skill. Everything else a skill does goes through
 * `UCataclysmSkillTemplate::HitTargets` while the skill is still running.
 *
 * DRIVEN WITH `Step` RATHER THAN BY TICKING. The test world is never ticked, so
 * the projectile is moved by hand, which is what `Step` is public for. The two
 * projectile tests either side of this one use a speed of zero, which never
 * creates a flying projectile at all, so neither covers this path.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileCarriesTheSkillsCritChanceTest,
	"Cataclysm.Skills.AProjectileCarriesItsFiringSkillsCriticalStrikeChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileCarriesTheSkillsCritChanceTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// Every chance above zero beats a roll of zero, so nothing here is random.
	const CataclysmTestWorld::FScopedCritRoll AlwaysRolls(0.0f);

	// Fires one shot at a target three metres away and returns what it took.
	const auto DamageDealtWith = [&](float CritChancePercent) -> float
	{
		FScopedFighter Caster(World, FVector::ZeroVector);
		FScopedFighter Target(World, FVector(3 * M, 0, 0));

		// THE CHARACTER NEVER CRITICALLY STRIKES, so anything above the plain
		// figure can only have come from the number the projectile carried.
		Caster.Set(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
		Caster.Set(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(), 200.0f);

		ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
			Caster.Actor, FVector::ZeroVector, FVector(8 * M, 0, 0),
			/*InRadiusCm=*/100.0f, /*InSpeed=*/1000.0f, /*InPierce=*/0,
			/*bInReturns=*/false, /*InDamagePercent=*/100.0f,
			FGameplayTagContainer(), /*bInBurns=*/false,
			/*InBodyMesh=*/nullptr, /*InFlightSeconds=*/0.0f, CritChancePercent);
		if (!Shot)
		{
			AddError(TEXT("The projectile was not fired."));
			return -1.0f;
		}

		const float Before = Target.Health();

		// Three metres at ten metres a second, so it arrives inside 0.3 s. The
		// loop stops as soon as the target is hurt rather than running on.
		for (int32 Steps = 0; Steps < 20 && Target.Health() >= Before; ++Steps)
		{
			Shot->Step(0.05f);
		}

		return Before - Target.Health();
	};

	// 100% of a weapon damage of 100.
	TestEqual(TEXT("a projectile whose skill states nothing takes the "
				   "character's 0% and does not critically strike"),
		DamageDealtWith(-1.0f), WeaponDamage);

	TestEqual(TEXT("and one whose skill states 100% critically strikes, at the "
				   "character's multiplier"),
		DamageDealtWith(100.0f), WeaponDamage * 2.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLandingProjectileTest,
	"Cataclysm.Skills.AProjectileThatDoesNotPierceHitsWhereItLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLandingProjectileTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	// Blood Pyre with no Range: with no player controller, AimPoint falls back
	// to the caster, so it lands at the caster's feet and hits in its radius.
	// Pierce absent, so it is the landing kind.
	UCataclysmProjectileSkill* Pyre = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Radius=3; Speed=0; HealthCostPercent=8"), TEXT("Blood Pyre"));
	if (!Pyre)
	{
		AddError(TEXT("Could not grant the pyre."));
		return false;
	}

	const float CloseBefore = Close.Health();
	const float CasterHealthBefore = Caster.Health();

	TestTrue(TEXT("It activates"), Activate(Caster, Pyre));

	TestTrue(TEXT("An enemy inside the landing radius was hit"),
		Close.Health() < CloseBefore);

	// "Paying 8% of your current health". A percent of CURRENT, not maximum,
	// which is what makes it unable to kill the caster.
	const float Paid = CasterHealthBefore - Caster.Health();
	TestEqual(TEXT("It cost 8% of the caster's current health"),
		Paid, CasterHealthBefore * 0.08f);

	// AND A CASTER WITH NO FERVOUR GENERATOR GAINED NOTHING FROM PAYING IT,
	// which is every character in the game until a point is spent on one. The
	// test below is the other half. Issue #954.
	TestEqual(TEXT("and a caster with no Fervour generator gained none"),
		Caster.Fervour(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHealthCostFillsFervourTest,
	"Cataclysm.Skills.AHealthCostFillsFervourForACasterWithAGenerator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHealthCostFillsFervourTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// WHY THIS IS HERE RATHER THAN IN CataclysmFervourTests.cpp. That file
	// checks the rule by calling `UCataclysmFervour::GainFromHealthCost`
	// directly, and would go on passing with the line inside
	// `UCataclysmSkillTemplate::PayHealthCost` deleted -- at which point paying
	// health for a skill would generate nothing in a real cast and nothing would
	// report it. This is the only test that goes through the real path, which is
	// activating a skill that charges health. Issue #954.
	//
	// A CASTER OF ITS OWN AND A SINGLE CAST. Casting twice in one test does not
	// work: a skill commits a cooldown, so the second activation is refused and
	// the test would be measuring the refusal.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	// THE MASOCHIST'S RATE FOR HEALTH SPENT AS A COST: 1 Fervour per 1% of
	// maximum health. Its starting node is what grants this in the game.
	Caster.Set(
		UCataclysmClassResourceAttributeSet::GetFervourFromCostAttribute(), 1.0f);

	UCataclysmProjectileSkill* Pyre = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Radius=3; Speed=0; HealthCostPercent=8"), TEXT("Blood Pyre"));
	if (!Pyre)
	{
		AddError(TEXT("Could not grant the pyre."));
		return false;
	}

	const float HealthBefore = Caster.Health();
	const float MaxHealth =
		Caster.Get(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());

	TestTrue(TEXT("It activates"), Activate(Caster, Pyre));

	const float Paid = HealthBefore - Caster.Health();
	if (!TestTrue(FString::Printf(TEXT("it charged health (%.1f)"), Paid),
				  Paid > 0.0f))
	{
		return false;
	}

	// READ OFF WHAT IT REALLY CHARGED rather than assumed, so this measures the
	// conversion rather than restating the cost rule the test above already
	// holds.
	TestEqual(TEXT("and the health it charged became Fervour, 1 per 1% of "
				   "maximum health"),
		Caster.Fervour(), Paid / MaxHealth * 100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAddedHealthCostTest,
	"Cataclysm.Skills.ACharactersOwnHealthCostIsPaidBySkillsThatStateNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAddedHealthCostTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// THE MASOCHIST'S DEEPER CUTS NODE, held at its full ten points: "Your
	// skills also cost 1% of your maximum health per point, in addition to any
	// other cost." Issue #970.
	//
	// THE SKILL HERE STATES NO COST OF ITS OWN, which is every skill in the game
	// except Blood Pyre, and is the whole point of the test. `PayHealthCost`
	// used to return on its first line when the skill's own figure was zero, at
	// which point this node would have done nothing at all.
	//
	// A CASTER OF ITS OWN AND A SINGLE CAST. A skill commits a cooldown, so a
	// second activation is refused and the test would measure the refusal.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	// A ROUND MAXIMUM, so the share is easy to read off the assertion.
	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000.0f);

	// TEN PERCENT OF MAXIMUM HEALTH, which is Deeper Cuts at ten points.
	Caster.Set(
		UCataclysmClassResourceAttributeSet::GetAddedHealthCostAttribute(), 10.0f);

	// AND THE MASOCHIST'S RATE FOR HEALTH SPENT AS A COST, so the second half of
	// the node's sentence -- "This cost generates Fervour like any other" -- is
	// checked rather than assumed.
	Caster.Set(
		UCataclysmClassResourceAttributeSet::GetFervourFromCostAttribute(), 1.0f);

	UCataclysmProjectileSkill* Plain = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Radius=3; Speed=0"), TEXT("A skill with no health cost"));
	if (!Plain)
	{
		AddError(TEXT("Could not grant the skill."));
		return false;
	}

	TestEqual(TEXT("the skill states no health cost of its own"),
		Plain->Params.HealthCostPercent, 0.0f);

	TestTrue(TEXT("It activates"), Activate(Caster, Plain));

	TestEqual(TEXT("and the character's own cost was taken anyway: 10% of a "
				   "maximum of 1000"),
		Caster.Health(), 900.0f, 0.5f);

	// ONE FERVOUR PER 1% OF MAXIMUM HEALTH SPENT, so ten percent is ten.
	TestEqual(TEXT("and it generated Fervour like any other cost"),
		Caster.Fervour(), 10.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTwoHealthCostsAddTest,
	"Cataclysm.Skills.ASkillsOwnHealthCostAndTheCharactersAreAddedNotCompounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTwoHealthCostsAddTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// BOTH COSTS ON ONE CAST. Issue #970. The node says the character's cost is
	// "in addition to any other cost", so the two are summed rather than one
	// being taken from what the other left.
	//
	// THE TWO ARE MEASURED AGAINST DIFFERENT THINGS, which is what makes the
	// arithmetic worth pinning. The skill's own 8% is a share of CURRENT health;
	// the character's 10% is a share of MAXIMUM health.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000.0f);
	Caster.Set(
		UCataclysmClassResourceAttributeSet::GetAddedHealthCostAttribute(), 10.0f);

	UCataclysmProjectileSkill* Pyre = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Radius=3; Speed=0; HealthCostPercent=8"), TEXT("Blood Pyre"));
	if (!Pyre)
	{
		AddError(TEXT("Could not grant the pyre."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Pyre));

	// 8% OF 1000 CURRENT PLUS 10% OF 1000 MAXIMUM, which is 180.
	const float Paid = 1'000.0f - Caster.Health();
	TestEqual(FString::Printf(
		TEXT("both costs were taken and added, and it charged %.1f"), Paid),
		Paid, 180.0f, 0.5f);

	// AND NOT COMPOUNDED, which either order would give as 172: 8% of what is
	// left after 100, or 10% of what is left after 80. The assertion above would
	// accept neither, and this says so out loud.
	TestTrue(FString::Printf(
		TEXT("and not compounded, which would have charged 172 and charged %.1f"),
		Paid),
		!FMath::IsNearlyEqual(Paid, 172.0f, 1.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillRecordsWhatItChargedTest,
	"Cataclysm.Skills.ASkillRecordsWhatItChargedAsAShareOfMaximumHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillRecordsWhatItChargedTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// WHAT THE MASOCHIST'S GRAND TITHE NODE READS. Issue #983. The node asks
	// about "a skill whose health cost is above 10% of your maximum health", and
	// nothing anywhere recorded what a skill had cost.
	//
	// AGAINST MAXIMUM HEALTH WHATEVER EACH HALF WAS MEASURED AGAINST, which is
	// the arithmetic worth pinning. The skill's own 8% is a share of CURRENT
	// health and the character's 10% is a share of MAXIMUM health, and the node
	// asks about maximum.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	// HALF HEALTH, DELIBERATELY, so the two halves of the cost cannot be
	// confused. At full health both would be a share of the same number and a
	// division by the wrong one would give the right answer by accident.
	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 500.0f);
	Caster.Set(
		UCataclysmClassResourceAttributeSet::GetAddedHealthCostAttribute(), 10.0f);

	UCataclysmProjectileSkill* Pyre = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Radius=3; Speed=0; HealthCostPercent=8"), TEXT("Blood Pyre"));
	if (!Pyre)
	{
		AddError(TEXT("Could not grant the pyre."));
		return false;
	}

	// NOTHING RECORDED UNTIL IT IS USED, which is what -1 means and why zero
	// cannot be the sentinel: a skill that was used and charged nothing is a
	// real and common answer.
	TestTrue(TEXT("an unused skill has recorded nothing"),
			 Pyre->LastHealthCostPercentOfMaximum < 0.0f);

	TestTrue(TEXT("It activates"), Activate(Caster, Pyre));

	// 8% OF 500 CURRENT IS 40, PLUS 10% OF 1000 MAXIMUM IS 100, so 140 charged,
	// and 140 of 1000 maximum health is 14%.
	//
	// NOT 18. That is what the figure would be at full health, and it is the
	// number a reader expects, which is exactly why the character is at half.
	//
	// NOT 28. That is 140 measured against CURRENT health, which is the wrong
	// denominator and the one mistake this arithmetic can make.
	TestEqual(FString::Printf(
		TEXT("it recorded 14%% of maximum health, and recorded %.2f"),
		Pyre->LastHealthCostPercentOfMaximum),
		Pyre->LastHealthCostPercentOfMaximum, 14.0f, 0.01f);

	// AND THE COST WAS REALLY TAKEN, so the figure is a record of something that
	// happened rather than of an intention.
	TestEqual(TEXT("and 140 health was really taken"),
			  500.0f - Caster.Health(), 140.0f, 0.5f);

	// ABOVE THE DESIGN'S TEN PER CENT THRESHOLD, which is the point of the whole
	// number. Stated here so that a change to either figure has to face the
	// question rather than quietly making the node inert.
	TestTrue(TEXT("which is above the ten per cent Grand Tithe asks for"),
			 Pyre->LastHealthCostPercentOfMaximum > 10.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLethalHealthCostTest,
	"Cataclysm.Skills.AHealthCostLargerThanHealthLeavesTheCasterAtZeroNotBelow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLethalHealthCostTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// A COST MEASURED AGAINST MAXIMUM HEALTH CAN EXCEED CURRENT HEALTH, which a
	// cost measured against current health never can. Issue #970, and
	// `docs/DECISIONS.md` records the project owner drawing that distinction.
	//
	// WHAT THIS PINS IS THE FLOOR, NOT THE DEATH. Health must not go negative: a
	// character at minus fifty health is one the health bar cannot draw and the
	// death check cannot recognise. Whether a health cost also KILLS is a
	// separate question about which notifications a direct attribute write runs,
	// and it has an issue of its own.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000.0f);

	// TWICE THE CHARACTER'S WHOLE HEALTH POOL. No node reaches this -- Deeper
	// Cuts stops at ten percent -- but the arithmetic has to hold at the edge
	// rather than only in the middle.
	Caster.Set(
		UCataclysmClassResourceAttributeSet::GetAddedHealthCostAttribute(),
		200.0f);

	UCataclysmProjectileSkill* Plain = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Radius=3; Speed=0"), TEXT("A skill with no health cost"));
	if (!Plain)
	{
		AddError(TEXT("Could not grant the skill."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Plain));

	TestEqual(TEXT("the caster is left at exactly no health, not below it"),
		Caster.Health(), 0.0f, 0.01f);

	return true;
}

// --------------------------------------------------------------------------
// A chance for a skill not to go on cooldown at all. Issue #973.
//
// THE MASOCHIST'S THE CATALYST NODE: "While at or below 5% health, your skills
// have a 5% chance per point not to go on cooldown." Eight points, so 40%.
//
// THREE TESTS RATHER THAN ONE, because a skill commits a cooldown and cannot be
// activated twice in one test. Each has its own caster and casts once.
// --------------------------------------------------------------------------

namespace CataclysmSkillTest
{
	/**
	 * A character carrying The Catalyst at its full eight points.
	 *
	 * WRITTEN AS STAT INPUTS RATHER THAN ONTO THE ATTRIBUTE, which is the whole
	 * point of the node. The bonus carries a health condition, so
	 * `UCataclysmPlayerClassStats::ApplyTo` refuses it and the gameplay
	 * attribute stays at zero; only `StatForSkill` can see it.
	 */
	void GiveTheCatalyst(FScopedFighter& Caster, float ChancePerNode)
	{
		FCataclysmStatModifier Conditional;
		Conditional.Bucket = ECataclysmStatBucket::Flat;
		Conditional.Source = ECataclysmModifierSource::PassiveKeystone;
		Conditional.Value = ChancePerNode;
		Conditional.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
		Conditional.ConditionValue = 5.0f;

		FCataclysmStatInputs Inputs;
		Inputs.Base = 0.0f;
		Inputs.Modifiers.Add(Conditional);

		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.Add(FName(TEXT("cooldown_skip_chance")), Inputs);
		Caster.AbilitySystem->SetStatInputs(MoveTemp(Stats));
	}

	/** Whether the slot this skill occupies is on cooldown right now. */
	bool IsOnCooldown(const FScopedFighter& Caster, ECataclysmAbilitySlot Slot)
	{
		const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Slot);
		return Tag.IsValid()
			&& Caster.AbilitySystem->HasMatchingGameplayTag(Tag);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownSkippedTest,
	"Cataclysm.Skills.AWoundedCharacterCanUseASkillWithoutItGoingOnCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCooldownSkippedTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	// FOUR PERCENT OF MAXIMUM HEALTH, which is under the node's threshold.
	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 40.0f);
	GiveTheCatalyst(Caster, 40.0f);

	// THE ROLL IS PINNED SO THE TEST ASSERTS RATHER THAN SAMPLES. Zero beats
	// every chance above zero.
	const CataclysmTestWorld::FScopedCooldownSkipRoll AlwaysSkips(0.0f);

	UCataclysmProjectileSkill* Skill = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special, TEXT("Radius=3; Speed=0"));
	if (!Skill)
	{
		AddError(TEXT("Could not grant the skill."));
		return false;
	}

	if (!TestTrue(TEXT("the skill has a cooldown to skip in the first place"),
				  Skill->GetBaseCooldown() > 0.0f))
	{
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Skill));

	TestFalse(TEXT("and the slot is not on cooldown afterwards"),
		IsOnCooldown(Caster, ECataclysmAbilitySlot::Special));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownNotSkippedAboveThresholdTest,
	"Cataclysm.Skills.AHealthyCharacterWithTheSameNodeStillGoesOnCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCooldownNotSkippedAboveThresholdTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// THE SAME NODE, THE SAME PINNED ROLL, AND ONLY THE HEALTH DIFFERENT. This
	// is what says the health condition is judged at the moment the skill is
	// used, rather than the node simply always applying. Without it the test
	// above would pass just as well for a node with no condition on it.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000.0f);
	GiveTheCatalyst(Caster, 40.0f);

	const CataclysmTestWorld::FScopedCooldownSkipRoll AlwaysSkips(0.0f);

	UCataclysmProjectileSkill* Skill = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special, TEXT("Radius=3; Speed=0"));
	if (!Skill)
	{
		AddError(TEXT("Could not grant the skill."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Skill));

	TestTrue(TEXT("and the slot IS on cooldown, because the character is not "
				  "wounded enough for the node to apply"),
		IsOnCooldown(Caster, ECataclysmAbilitySlot::Special));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCooldownNotSkippedOnALostRollTest,
	"Cataclysm.Skills.AWoundedCharacterThatLosesTheRollStillGoesOnCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCooldownNotSkippedOnALostRollTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// WOUNDED ENOUGH AND HOLDING THE NODE, AND THE ROLL LOST. This is what says
	// the chance is a chance rather than a certainty: without it the two tests
	// above would both pass for an implementation that skipped the cooldown
	// whenever the health condition held, whatever the node's value.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 40.0f);
	GiveTheCatalyst(Caster, 40.0f);

	// A HUNDRED NEVER SKIPS, because the comparison is strictly less than and a
	// chance is capped at 100.
	const CataclysmTestWorld::FScopedCooldownSkipRoll NeverSkips(100.0f);

	UCataclysmProjectileSkill* Skill = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special, TEXT("Radius=3; Speed=0"));
	if (!Skill)
	{
		AddError(TEXT("Could not grant the skill."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Skill));

	TestTrue(TEXT("and the slot IS on cooldown, because the roll was lost"),
		IsOnCooldown(Caster, ECataclysmAbilitySlot::Special));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHealthCostOpensAWindowTest,
	"Cataclysm.Skills.AHealthCostOpensTheWindowAPassiveNodeReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHealthCostOpensAWindowTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// WHY THIS IS HERE AND NOT IN CataclysmStatPipelineTests.cpp. That file
	// checks the rule by handing the pipeline a number of seconds directly, and
	// would go on passing with the `NoteHealthCostPaid` line inside
	// `UCataclysmSkillTemplate::PayHealthCost` deleted -- at which point Blood
	// Rush would never fire in a real cast and nothing would report it. This is
	// the only test that goes through the real path. Issue #962, and the same
	// argument the Fervour test above makes about the same function.
	//
	// A CASTER OF ITS OWN AND A SINGLE CAST. A skill commits a cooldown, so a
	// second activation is refused and the test would measure the refusal.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	// NOTHING HAS HAPPENED YET, ASSERTED BEFORE THE CAST. Without this the check
	// afterwards would pass just as well if the window were open from birth,
	// which is the failure the "never paid one" reading exists to prevent.
	TestEqual(TEXT("a character that has cast nothing has no window"),
		Caster.AbilitySystem->SecondsSinceHealthCostPaid(), -1.0f, 0.001f);

	UCataclysmProjectileSkill* Pyre = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Radius=3; Speed=0; HealthCostPercent=8"), TEXT("Blood Pyre"));
	if (!Pyre)
	{
		AddError(TEXT("Could not grant the pyre."));
		return false;
	}

	const float HealthBefore = Caster.Health();
	TestTrue(TEXT("It activates"), Activate(Caster, Pyre));

	const float Paid = HealthBefore - Caster.Health();
	if (!TestTrue(FString::Printf(TEXT("it charged health (%.1f)"), Paid),
				  Paid > 0.0f))
	{
		return false;
	}

	// THE WINDOW IS OPEN, AND IT OPENED NOW rather than at some earlier time.
	TestEqual(TEXT("paying opens the window, at this instant"),
		Caster.AbilitySystem->SecondsSinceHealthCostPaid(), 0.0f, 0.001f);

	// AND IT AGES BY ITSELF AS TIME PASSES, with nothing else happening. Blood
	// Rush states two seconds, so three is past any window the design uses.
	World->TimeSeconds += 3.0f;
	TestEqual(TEXT("and three seconds later it has been open that long"),
		Caster.AbilitySystem->SecondsSinceHealthCostPaid(), 3.0f, 0.01f);

	// AND THE STATE THE PIPELINE IS HANDED CARRIES IT, which is the join between
	// this timestamp and the conditional bonus that reads it. Checking the
	// timestamp alone would not prove `CurrentConditions` passes it on.
	TestEqual(TEXT("and the state handed to the pipeline says the same"),
		Caster.AbilitySystem->CurrentConditions().SecondsSinceHealthCost,
		3.0f, 0.01f);

	return true;
}

// --------------------------------------------------------------------------
// Movement
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBlinkTest,
	"Cataclysm.Skills.ABlinkHitsBothEndsAndMovesTheCaster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBlinkTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter AtStart(World, FVector(1 * M, 0, 0));

	// Emberstep. With no player controller AimPoint is the caster's own
	// position, so the destination is where it started -- which still proves
	// that both ends are gathered and merged without hitting anyone twice.
	UCataclysmMovementSkill* Step = GrantSkill<UCataclysmMovementSkill>(
		Caster, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Blink; Range=12; Radius=3; Burn=1"), TEXT("Emberstep"));
	if (!Step)
	{
		AddError(TEXT("Could not grant the step."));
		return false;
	}

	const float Before = AtStart.Health();
	TestTrue(TEXT("It activates"), Activate(Caster, Step));

	TestTrue(TEXT("An enemy at the point left behind was hit"),
		AtStart.Health() < Before);

	// AN ENEMY IN BOTH CIRCLES IS HIT ONCE, NOT TWICE. AddUnique is what makes
	// that true, and without it a blink in place would double every hit.
	const float Expected = WeaponDamage * Step->GetDamagePercent() / 100.0f;
	TestEqual(TEXT("And exactly once, not once per end"),
		Before - AtStart.Health(), Expected);

	TestEqual(TEXT("One enemy was recorded as hit"), Step->EnemiesHit, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChargeTest,
	"Cataclysm.Skills.AChargeHitsWhatItRunsThroughAndArrives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmChargeTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	UCataclysmMovementSkill* Rush = GrantSkill<UCataclysmMovementSkill>(
		Caster, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Charge; Range=10; Radius=2"), TEXT("Cinder Rush"));
	if (!Rush)
	{
		AddError(TEXT("Could not grant the rush."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Rush));

	// With no controller the aim point is the caster, so it charges nowhere --
	// which is the honest thing to assert here. What is proved is that the
	// destination was recorded and the move was applied without failing.
	TestEqual(TEXT("The arrival was recorded"),
		Rush->ArrivedAt, Caster.Actor->GetActorLocation());

	return true;
}

// --------------------------------------------------------------------------
// Summon
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSummonCapTest,
	"Cataclysm.Skills.SummoningPastTheCapExplodesTheOldest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSummonCapTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Bystander(World, FVector(1 * M, 0, 0));

	// Summon Imp: "up to 3 imps may be active at once. Summoning a fourth
	// destroys the oldest, which explodes for damage in a 3 meter radius."
	UCataclysmSummonSkill* Summon = GrantSkill<UCataclysmSummonSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Count=1; MaxActive=3; Duration=20; Radius=3; Burn=1"),
		TEXT("Summon Imp"));
	if (!Summon)
	{
		AddError(TEXT("Could not grant the summon."));
		return false;
	}

	// Driven directly rather than through activation, because the cooldown would
	// refuse the second use and this is about the cap, not the cooldown.
	ACataclysmMinion* Oldest = Summon->SummonOne();
	Summon->SummonOne();
	Summon->SummonOne();

	TestEqual(TEXT("Three imps are held"), Summon->LivingMinionCount(), 3);
	TestTrue(TEXT("The first is still alive"), IsValid(Oldest));

	const float BystanderBefore = Bystander.Health();

	Summon->SummonOne();

	// THE CAP IS ENFORCED BEFORE THE SPAWN, so the count never exceeds three
	// even for an instant.
	TestEqual(TEXT("Still three after a fourth"), Summon->LivingMinionCount(), 3);
	TestFalse(TEXT("The oldest was destroyed"), IsValid(Oldest));
	TestTrue(TEXT("And it exploded, hurting a bystander"),
		Bystander.Health() < BystanderBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionAttacksTest,
	"Cataclysm.Skills.AMinionHitsForAShareOfItsSummonersWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMinionAttacksTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(1 * M, 0, 0));

	ACataclysmMinion* Minion = ACataclysmMinion::Spawn(
		Caster.Actor, FVector(1 * M, 0, 0), /*Lifetime=*/20.0f, /*bBurns=*/true);
	if (!Minion)
	{
		AddError(TEXT("Could not spawn the minion."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Minion)) { Minion->Destroy(); } };

	const float Before = Enemy.Health();
	Minion->AttackOnce();

	TestEqual(TEXT("It attacked once"), Minion->AttacksMade, 1);
	TestTrue(TEXT("The enemy took damage"), Enemy.Health() < Before);

	// A SHARE OF THE SUMMONER'S WEAPON, not of its own, which it has none of.
	const float Expected =
		WeaponDamage * ACataclysmMinion::DamagePercentOfSummoner / 100.0f;
	TestEqual(TEXT("It dealt its share of the summoner's weapon damage"),
		Before - Enemy.Health(), Expected);

	TestTrue(TEXT("And set the enemy alight"),
		UCataclysmSkillEffects::HasTag(Enemy.Actor, UCataclysmSkillEffects::BurnTag()));

	return true;
}

// --------------------------------------------------------------------------
// Aura
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraDrainTest,
	"Cataclysm.Skills.AnAuraSwitchesOffWhenTheManaRunsOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAuraDrainTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(3 * M, 0, 0));

	// Conflagration. No Duration, so it is a toggle.
	UCataclysmAuraSkill* Aura = GrantSkill<UCataclysmAuraSkill>(
		Caster, ECataclysmAbilitySlot::Aura, TEXT("Radius=10; Interval=1; Burn=1"),
		TEXT("Conflagration"));
	if (!Aura)
	{
		AddError(TEXT("Could not grant the aura."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Aura));
	TestTrue(TEXT("And is held"), Aura->IsHeld());

	const float PerPulse = Aura->GetManaCost();
	TestTrue(FString::Printf(TEXT("The aura slot drains mana (%.1f a second)"),
		PerPulse), PerPulse > 0.0f);

	const float ManaBefore = Caster.Mana();
	const float EnemyBefore = Enemy.Health();

	Aura->Pulse();

	TestEqual(TEXT("One pulse drained one second's worth"),
		Caster.Mana(), ManaBefore - PerPulse);
	TestTrue(TEXT("And burned an enemy inside it"), Enemy.Health() < EnemyBefore);
	TestEqual(TEXT("One pulse was counted"), Aura->Pulses, 1);

	// ISSUE #36 REQUIRES THE AURA TO SWITCH OFF WHEN THE RESOURCE IS EXHAUSTED.
	Caster.Set(UCataclysmVitalAttributeSet::GetManaAttribute(), PerPulse * 0.5f);
	const float TooLittle = Caster.Mana();
	const float EnemyBeforeLast = Enemy.Health();

	Aura->Pulse();

	TestFalse(TEXT("It switched off"), Aura->IsHeld());
	TestTrue(TEXT("And said why"), Aura->bEndedForLackOfMana);
	TestEqual(TEXT("It took nothing it could not pay"), Caster.Mana(), TooLittle);
	TestEqual(TEXT("And did nothing on the pulse it could not pay for"),
		Enemy.Health(), EnemyBeforeLast);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraTogglesTest,
	"Cataclysm.Skills.PressingAnAuraAgainSwitchesItOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAuraTogglesTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	UCataclysmAuraSkill* Aura = GrantSkill<UCataclysmAuraSkill>(
		Caster, ECataclysmAbilitySlot::Aura, TEXT("Radius=10; Interval=1"),
		TEXT("Conflagration"));
	if (!Aura)
	{
		AddError(TEXT("Could not grant the aura."));
		return false;
	}

	TestTrue(TEXT("The first press switches it on"), Activate(Caster, Aura));
	TestTrue(TEXT("It is held"), Aura->IsHeld());

	// THROUGH THE REAL INPUT PATH, not by calling TryActivateAbility again.
	// UCataclysmAbilitySystemComponent::ProcessAbilityInput sends a press on an
	// already-running ability to AbilitySpecInputPressed rather than activating
	// it a second time, so that is what a second key press actually does and
	// what the toggle has to answer.
	FGameplayAbilitySpec* Spec = Caster.AbilitySystem->FindAbilitySpecFromHandle(
		Aura->GetCurrentAbilitySpecHandle());
	if (!Spec)
	{
		AddError(TEXT("The aura's spec disappeared."));
		return false;
	}

	// The aura slot has NO COOLDOWN, so nothing else would stop the key being
	// pressed to no effect while it runs. Issue #36 requires it to toggle.
	Caster.AbilitySystem->AbilitySpecInputPressed(*Spec);

	TestFalse(TEXT("The second press switches it off"), Aura->IsHeld());
	TestFalse(TEXT("And not for lack of mana"), Aura->bEndedForLackOfMana);

	// And a third press switches it back on, which is what a toggle means.
	TestTrue(TEXT("A third press switches it on again"), Activate(Caster, Aura));
	TestTrue(TEXT("And it is held again"), Aura->IsHeld());

	return true;
}

// --------------------------------------------------------------------------
// Debuff
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDebuffTest,
	"Cataclysm.Skills.ADebuffLastsTwiceAsLongOnABurningTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDebuffTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Target(World, FVector(1 * M, 0, 0));

	// Subjugate: "subjugating an enemy that is already burning makes the madness
	// last twice as long." Madness itself is three seconds, which is what the
	// design document's effect table states: "the enemy attacks anything nearby,
	// friend or foe, for 3 seconds".
	UCataclysmDebuffSkill* Subjugate = GrantSkill<UCataclysmDebuffSkill>(
		Caster, ECataclysmAbilitySlot::Support,
		TEXT("Range=15; MaxTargets=1; Duration=3; Effect=Madness"),
		TEXT("Subjugate"));
	if (!Subjugate)
	{
		AddError(TEXT("Could not grant the debuff."));
		return false;
	}

	// The tag has to exist, or the whole test would pass vacuously with nothing
	// applied. It is generated from the Debuffs sheet, which lists Madness.
	TestTrue(TEXT("Madness has a status tag"),
		UCataclysmSkillShapes::StatusTagFor(TEXT("Madness")).IsValid());

	TestTrue(TEXT("It activates against a target that is not burning"),
		Activate(Caster, Subjugate));
	TestEqual(TEXT("One enemy was taken"), Subjugate->EnemiesAffected, 1);
	TestEqual(TEXT("For Madness's written three seconds"),
		Subjugate->LastDurationApplied, 3.0f);
	TestTrue(TEXT("And the target carries the Madness tag"),
		UCataclysmSkillEffects::HasTag(Target.Actor,
			UCataclysmSkillShapes::StatusTagFor(TEXT("Madness"))));

	// Now set the target alight and use it again. The cooldown refuses a second
	// activation, so the state is reset and the ability driven again.
	Caster.AbilitySystem->RemoveActiveEffectsWithGrantedTags(
		FGameplayTagContainer(UCataclysmSkillSlots::CooldownTag(
			ECataclysmAbilitySlot::Support)));

	UCataclysmSkillEffects::ApplyBurn(Caster.Actor, Target.Actor, /*HitDamage=*/100.0f);
	TestTrue(TEXT("The target is now burning"),
		UCataclysmSkillEffects::HasTag(Target.Actor, UCataclysmSkillEffects::BurnTag()));

	TestTrue(TEXT("It activates again"), Activate(Caster, Subjugate));
	TestEqual(TEXT("And lasts twice as long on a burning enemy"),
		Subjugate->LastDurationApplied, 6.0f);

	return true;
}

// --------------------------------------------------------------------------
// The burning ground a skill leaves behind
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundZoneTest,
	"Cataclysm.Skills.BurningGroundHurtsWhoeverIsStandingInItNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGroundZoneTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Inside(World, FVector(2 * M, 0, 0));
	FScopedFighter Outside(World, FVector(20 * M, 0, 0));

	ACataclysmGroundZone* Zone = ACataclysmGroundZone::Spawn(
		Caster.Actor, FVector::ZeroVector, /*RadiusCm=*/5 * M,
		/*Duration=*/6.0f, /*DamagePerTick=*/10.0f);
	if (!Zone)
	{
		AddError(TEXT("Could not spawn the ground zone."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Zone)) { Zone->Destroy(); } };

	const float InsideBefore = Inside.Health();
	const float OutsideBefore = Outside.Health();

	Zone->Sweep();

	TestEqual(TEXT("It found one enemy inside"), Zone->LastSweepCount, 1);
	TestEqual(TEXT("Which took one tick"), InsideBefore - Inside.Health(), 10.0f);
	TestEqual(TEXT("The one outside took nothing"), Outside.Health(), OutsideBefore);
	TestEqual(TEXT("The caster is not hurt by their own ground"),
		Caster.Health(), 100000.0f);

	// WHO IS INSIDE IS A QUESTION ABOUT NOW, NOT ABOUT WHEN IT WAS CREATED.
	// Standing in it is the cost, so walking out has to stop it.
	Inside.Actor->SetActorLocation(FVector(20 * M, 0, 0));
	const float AfterWalkingOut = Inside.Health();

	Zone->Sweep();

	TestEqual(TEXT("Having walked out, it takes nothing"),
		Inside.Health(), AfterWalkingOut);
	TestEqual(TEXT("And the zone found nobody"), Zone->LastSweepCount, 0);

	// And walking back in starts it again.
	Inside.Actor->SetActorLocation(FVector(2 * M, 0, 0));
	Zone->Sweep();
	TestEqual(TEXT("Walking back in starts it again"), Zone->LastSweepCount, 1);
	TestEqual(TEXT("Three sweeps ran"), Zone->TicksElapsed, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundZoneIsWhereItWasLeftTest,
	"Cataclysm.Skills.BurningGroundIsWhereItWasLeftNotAtTheWorldOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGroundZoneIsWhereItWasLeftTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter NearTheZone(World, FVector(30 * M, 0, 0));
	FScopedFighter NearTheOrigin(World, FVector(1 * M, 0, 0));

	// WELL AWAY FROM THE ORIGIN, WHICH IS THE POINT OF THIS TEST. The existing
	// ground zone test spawns at (0,0,0), so it could not tell the difference
	// between a zone that is where it was put and one that is at the origin. The
	// class had no components at all, an actor with no scene component gets no
	// root component, and an actor with no root component reports its location as
	// the world origin -- so every patch of burning ground in the project swept
	// around (0,0,0). Issue #167.
	const FVector Where(30 * M, 0, 0);
	ACataclysmGroundZone* Zone = ACataclysmGroundZone::Spawn(
		Caster.Actor, Where, /*RadiusCm=*/5 * M,
		/*Duration=*/6.0f, /*DamagePerTick=*/10.0f);
	if (!Zone)
	{
		AddError(TEXT("Could not spawn the ground zone."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Zone)) { Zone->Destroy(); } };

	TestEqual(TEXT("The zone reports the position it was spawned at"),
		Zone->GetActorLocation(), Where);
	TestFalse(TEXT("A zone spawned at a point is not long"), Zone->IsLong());

	const float FarBefore = NearTheZone.Health();
	const float OriginBefore = NearTheOrigin.Health();

	Zone->Sweep();

	TestEqual(TEXT("It found the one standing in it"), Zone->LastSweepCount, 1);
	TestTrue(TEXT("Who took a tick"), NearTheZone.Health() < FarBefore);
	TestEqual(TEXT("And one standing at the world origin took nothing"),
		NearTheOrigin.Health(), OriginBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLongGroundZoneTest,
	"Cataclysm.Skills.BurningGroundCanCoverAPathRatherThanAPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLongGroundZoneTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector(-5 * M, 0, 0));
	FScopedFighter Halfway(World, FVector(6 * M, 0, 0));
	FScopedFighter AtTheFarEnd(World, FVector(12 * M, 0, 0));
	FScopedFighter Beside(World, FVector(6 * M, 5 * M, 0));
	FScopedFighter Beyond(World, FVector(20 * M, 0, 0));

	const FVector Start(0, 0, 0);
	const FVector End(12 * M, 0, 0);

	ACataclysmGroundZone* Zone = ACataclysmGroundZone::SpawnAlong(
		Caster.Actor, Start, End, /*HalfWidthCm=*/1.5f * M,
		/*Duration=*/4.0f, /*DamagePerTick=*/10.0f);
	if (!Zone)
	{
		AddError(TEXT("Could not spawn the long ground zone."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Zone)) { Zone->Destroy(); } };

	TestTrue(TEXT("A zone spawned along a path is long"), Zone->IsLong());
	TestEqual(TEXT("Its near end is where the path started"),
		Zone->GetActorLocation(), Start);
	TestEqual(TEXT("Its far end is where the path ended"), Zone->FarEnd, End);

	Zone->Sweep();

	// THE HALFWAY ONE IS THE WHOLE POINT. With one patch at the far end it stood
	// on ground that was not burning, which is what issue #167 reported.
	TestEqual(TEXT("It found the two standing on the path"), Zone->LastSweepCount, 2);

	Zone->Sweep();
	TestEqual(TEXT("Still two on the second sweep"), Zone->LastSweepCount, 2);

	// Named so a failure says which one is wrong rather than only the count.
	const float Damage = 10.0f;
	TestEqual(TEXT("The one halfway along the path is burning"),
		100000.0f - Halfway.Health(), Damage * 2.0f);
	TestEqual(TEXT("So is the one at the far end"),
		100000.0f - AtTheFarEnd.Health(), Damage * 2.0f);
	TestEqual(TEXT("One five metres to the side took nothing"),
		Beside.Health(), 100000.0f);
	TestEqual(TEXT("One beyond the far end took nothing"),
		Beyond.Health(), 100000.0f);

	// Behind the start is outside too, which is what stops a trail burning
	// ground the caster ran away from.
	TestEqual(TEXT("The caster stands behind the start and is unhurt"),
		Caster.Health(), 100000.0f);

	return true;
}

// --------------------------------------------------------------------------
// Which skills leave a path burning and which leave a patch
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPiercingProjectileTrailTest,
	"Cataclysm.Skills.APiercingProjectileLeavesItsWholeFlightPathBurning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPiercingProjectileTrailTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Halfway(World, FVector(6 * M, 0, 0));
	FScopedFighter Beside(World, FVector(6 * M, 5 * M, 0));

	// Emberhurl's own numbers from the Weapon Skills sheet, with Speed set to
	// zero so it arrives at once and needs no timer, and without Returns so the
	// test is about the ground rather than the second pass.
	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Speed=0; Burn=1; "
			 "GroundRadius=1.5; GroundDuration=4; GroundPercent=25"),
		TEXT("Emberhurl"));
	if (!Hurl)
	{
		AddError(TEXT("Could not grant the hurl."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	ACataclysmGroundZone* Zone = TheOnlyGroundZone(World);
	if (!Zone)
	{
		AddError(TEXT("A throw that leaves ground left no zone, or left more "
					  "than one."));
		return false;
	}

	TestTrue(TEXT("The ground it left covers a path"), Zone->IsLong());
	TestEqual(TEXT("Starting where the throw started"),
		Zone->GetActorLocation(), FVector::ZeroVector);
	TestEqual(TEXT("And ending twelve metres away, where it landed"),
		static_cast<float>(Zone->FarEnd.X), 12.0f * M, 1.0f);

	// Measured from AFTER the throw's own hits, so this is the ground's damage
	// and not the projectile's.
	const float HalfwayBefore = Halfway.Health();
	const float BesideBefore = Beside.Health();

	Zone->Sweep();

	TestTrue(TEXT("An enemy halfway along the flight path is burning"),
		Halfway.Health() < HalfwayBefore);
	TestEqual(TEXT("One standing off the path is not"),
		Beside.Health(), BesideBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLandingProjectileGroundTest,
	"Cataclysm.Skills.AProjectileThatDoesNotPierceLeavesGroundOnlyWhereItLanded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLandingProjectileGroundTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// Blood Pyre. It does not pierce, and its text puts a pyre where it hit, so
	// its ground must stay a patch even though the change made a path possible.
	UCataclysmProjectileSkill* Pyre = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=3; Speed=0; Burn=1; GroundRadius=3; "
			 "GroundDuration=8; GroundPercent=12.5"),
		TEXT("Blood Pyre"));
	if (!Pyre)
	{
		AddError(TEXT("Could not grant the pyre."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Pyre));

	ACataclysmGroundZone* Zone = TheOnlyGroundZone(World);
	if (!Zone)
	{
		AddError(TEXT("A pyre that leaves ground left no zone, or left more "
					  "than one."));
		return false;
	}

	TestFalse(TEXT("The ground it left is a patch, not a path"), Zone->IsLong());
	TestEqual(TEXT("At the point it landed"),
		static_cast<float>(Zone->GetActorLocation().X), 12.0f * M, 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChargeTrailTest,
	"Cataclysm.Skills.AChargeLeavesFireAlongTheWholeRunAndALeapDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmChargeTrailTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	{
		UWorld* World = MakeWorld();
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		FScopedFighter Caster(World, FVector::ZeroVector);

		// Cinder Rush "leaves a trail of fire behind you".
		UCataclysmMovementSkill* Rush = GrantSkill<UCataclysmMovementSkill>(
			Caster, ECataclysmAbilitySlot::Movement,
			TEXT("Mode=Charge; Range=10; Radius=2; Burn=1; GroundRadius=2; "
				 "GroundDuration=5; GroundPercent=20"),
			TEXT("Cinder Rush"));
		if (!Rush)
		{
			AddError(TEXT("Could not grant the rush."));
			return false;
		}

		TestTrue(TEXT("It activates"), Activate(Caster, Rush));

		ACataclysmGroundZone* Zone = TheOnlyGroundZone(World);
		if (!Zone)
		{
			AddError(TEXT("A charge that leaves ground left no zone, or left "
						  "more than one."));
			return false;
		}

		TestTrue(TEXT("A charge leaves ground covering a path"), Zone->IsLong());
		TestEqual(TEXT("Starting where the run started"),
			Zone->GetActorLocation(), FVector::ZeroVector);
		TestEqual(TEXT("And ending where the character arrived"),
			static_cast<float>(Zone->FarEnd.X),
			static_cast<float>(Caster.Actor->GetActorLocation().X), 1.0f);
	}

	{
		UWorld* World = MakeWorld();
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		FScopedFighter Caster(World, FVector::ZeroVector);

		// Infernal Plunge leaves "a pool of lava" where it landed, and nothing
		// under the arc, so a leap must still leave a patch.
		UCataclysmMovementSkill* Plunge = GrantSkill<UCataclysmMovementSkill>(
			Caster, ECataclysmAbilitySlot::Movement,
			TEXT("Mode=Leap; Range=10; Radius=5; Burn=1; GroundRadius=5; "
				 "GroundDuration=5; GroundPercent=20"),
			TEXT("Infernal Plunge"));
		if (!Plunge)
		{
			AddError(TEXT("Could not grant the plunge."));
			return false;
		}

		TestTrue(TEXT("It activates"), Activate(Caster, Plunge));

		ACataclysmGroundZone* Zone = TheOnlyGroundZone(World);
		if (!Zone)
		{
			AddError(TEXT("A leap that leaves ground left no zone, or left more "
						  "than one."));
			return false;
		}

		TestFalse(TEXT("A leap leaves a patch, not a path"), Zone->IsLong());
	}

	return true;
}

// --------------------------------------------------------------------------
// The dispatch: a row's Shape column decides which class is granted
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShapeDispatchTest,
	"Cataclysm.Skills.EquippingAWeaponGrantsTheTemplateItsRowNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmShapeDispatchTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	UCataclysmWeaponSlotsComponent* Slots =
		NewObject<UCataclysmWeaponSlotsComponent>(Caster.Actor);
	Slots->RegisterComponent();
	Slots->SetDamageType(TEXT("Demonic"));

	// SEVEN, NOT SIX, SINCE ISSUE #524. Six skills come from the weapon skill
	// matrix and the seventh is the basic attack, which is read from the
	// Greataxe's own row on the Item Bases sheet because it does not vary by
	// damage type. The ByShape map below deliberately covers only the six matrix
	// slots, because this test is about the Shape column deciding which class is
	// granted, and the basic attack has no matrix row to have a Shape column in.
	const int32 Filled = Slots->EquipWeaponType(TEXT("Greataxe"));
	TestEqual(TEXT("A Demonic Greataxe fills seven slots"), Filled, 7);

	// THE ROW DECIDES THE CLASS. Nothing in C++ names Molten Cleave; the Heavy
	// row's Shape column says Strike, so a Strike is what is granted.
	TMap<ECataclysmAbilitySlot, UClass*> ByShape = {
		{ ECataclysmAbilitySlot::Heavy,    UCataclysmStrikeSkill::StaticClass()     },
		{ ECataclysmAbilitySlot::Ultimate, UCataclysmStrikeSkill::StaticClass()     },
		{ ECataclysmAbilitySlot::Special,  UCataclysmProjectileSkill::StaticClass() },
		{ ECataclysmAbilitySlot::Support,  UCataclysmSelfBuffSkill::StaticClass()   },
		{ ECataclysmAbilitySlot::Movement, UCataclysmMovementSkill::StaticClass()   },
		{ ECataclysmAbilitySlot::Aura,     UCataclysmAuraSkill::StaticClass()       },
	};

	TMap<ECataclysmAbilitySlot, UClass*> Granted;
	TMap<ECataclysmAbilitySlot, FString> Names;
	for (const FGameplayAbilitySpec& Spec : Caster.AbilitySystem->GetActivatableAbilities())
	{
		for (const ECataclysmAbilitySlot Slot : CataclysmAbilitySlots::All())
		{
			if (Spec.GetDynamicSpecSourceTags().HasTagExact(CataclysmAbilitySlots::Tag(Slot)))
			{
				Granted.Add(Slot, Spec.Ability ? Spec.Ability->GetClass() : nullptr);
				if (const UCataclysmSkillTemplate* Template =
						Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance()))
				{
					Names.Add(Slot, Template->SkillName);
				}
			}
		}
	}

	for (const TPair<ECataclysmAbilitySlot, UClass*>& Pair : ByShape)
	{
		UClass* const* Found = Granted.Find(Pair.Key);
		if (!Found)
		{
			AddError(FString::Printf(TEXT("Nothing was granted into slot %d"),
				static_cast<int32>(Pair.Key)));
			continue;
		}
		TestEqual(FString::Printf(TEXT("Slot %d got the class its Shape names"),
			static_cast<int32>(Pair.Key)), *Found, Pair.Value);
	}

	// And the designed name and numbers reached the instance, which is what
	// makes one class stand for many skills.
	const FString* HeavyName = Names.Find(ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("The Heavy slot knows it is Molten Cleave"),
		HeavyName && *HeavyName == TEXT("Molten Cleave"));

	Slots->UnequipWeapon();
	return true;
}

// --------------------------------------------------------------------------
// A self buff's magnitude. Issue #166: the duration was real and the magnitude
// was not, because nothing fed the three-bucket stat pipeline from a skill.
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuffIncreaseAppliesTest,
	"Cataclysm.Skills.ABuffsIncreaseRaisesTheDamageOfSkillsItScopesTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuffIncreaseAppliesTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// Two enemies inside the buff's 15 metre radius, both alight, so Burning
	// Wrath's count is two and its increase is 4% x 2 = 8%.
	FScopedFighter Alight(World, FVector(3 * M, 0, 0));
	FScopedFighter AlsoAlight(World, FVector(4 * M, 0, 0));
	UCataclysmSkillEffects::ApplyBurn(Caster.Actor, Alight.Actor, 100.0f);
	UCataclysmSkillEffects::ApplyBurn(Caster.Actor, AlsoAlight.Actor, 100.0f);

	UCataclysmSelfBuffSkill* Buff = GrantSkill<UCataclysmSelfBuffSkill>(
		Caster, ECataclysmAbilitySlot::Support,
		TEXT("Duration=10; Radius=15; IncreasePerBurning=4"), TEXT("Burning Wrath"),
		TEXT("Item.Weapon.Greataxe, Element.Demonic, Type.Buff"));
	if (!Buff)
	{
		AddError(TEXT("Could not grant the buff."));
		return false;
	}

	TestTrue(TEXT("The buff activates"), Activate(Caster, Buff));
	TestEqual(TEXT("It counted both burning enemies"), Buff->BurningEnemiesAtCast, 2);
	TestEqual(TEXT("It granted 4% for each of them"), Buff->GrantedIncrease, 8.0f);
	TestEqual(TEXT("Scoped to the skill's own element"),
		Buff->GrantedScope.ToString(), FString(TEXT("Element.Demonic")));

	// A Demonic strike carries Element.Demonic, so the increase reaches it.
	FScopedFighter Demonic(World, FVector(2 * M, 0, 0));
	UCataclysmStrikeSkill* DemonicStrike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"),
		TEXT("Molten Cleave"), TEXT("Item.Weapon.Greataxe, Element.Demonic"));

	const float DemonicBefore = Demonic.Health();
	TestTrue(TEXT("The Demonic strike activates"), Activate(Caster, DemonicStrike));

	// The Heavy slot is 250% of weapon damage. With 8% increased on top:
	// 100 x 2.50 x 1.08 = 270.
	const float Unbuffed = WeaponDamage * 250.0f / 100.0f;
	TestEqual(TEXT("The Demonic strike dealt 250% raised by 8%"),
		DemonicBefore - Demonic.Health(), Unbuffed * 1.08f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuffIncreaseIsScopedTest,
	"Cataclysm.Skills.ABuffsIncreaseDoesNotReachAnotherDamageType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuffIncreaseIsScopedTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	FScopedFighter Alight(World, FVector(3 * M, 0, 0));
	UCataclysmSkillEffects::ApplyBurn(Caster.Actor, Alight.Actor, 100.0f);

	UCataclysmSelfBuffSkill* Buff = GrantSkill<UCataclysmSelfBuffSkill>(
		Caster, ECataclysmAbilitySlot::Support,
		TEXT("Duration=10; Radius=15; IncreasePerBurning=4"), TEXT("Burning Wrath"),
		TEXT("Element.Demonic, Type.Buff"));
	TestTrue(TEXT("The buff activates"), Activate(Caster, Buff));
	TestEqual(TEXT("One burning enemy, so 4%"), Buff->GrantedIncrease, 4.0f);

	// A War strike carries Element.War, so a Demonic increase must not reach it.
	// THIS IS THE POINT OF SCOPING BY THE SKILL'S TAGS. A plain attribute could
	// not express it: the character's increased damage would be one number for
	// every skill they own.
	FScopedFighter Target(World, FVector(2 * M, 0, 0));
	UCataclysmStrikeSkill* WarStrike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"),
		TEXT("A War Skill"), TEXT("Item.Weapon.Sword, Element.War"));

	const float Before = Target.Health();
	TestTrue(TEXT("The War strike activates"), Activate(Caster, WarStrike));

	TestEqual(TEXT("The War strike dealt the plain 250%, unraised"),
		Before - Target.Health(), WeaponDamage * 250.0f / 100.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuffIncreaseEndsTest,
	"Cataclysm.Skills.ABuffsIncreaseIsTakenAwayWhenTheBuffEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuffIncreaseEndsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Alight(World, FVector(3 * M, 0, 0));
	UCataclysmSkillEffects::ApplyBurn(Caster.Actor, Alight.Actor, 100.0f);

	UCataclysmSelfBuffSkill* Buff = GrantSkill<UCataclysmSelfBuffSkill>(
		Caster, ECataclysmAbilitySlot::Support,
		TEXT("Duration=10; Radius=15; IncreasePerBurning=4"), TEXT("Burning Wrath"),
		TEXT("Element.Demonic"));
	TestTrue(TEXT("The buff activates"), Activate(Caster, Buff));
	TestEqual(TEXT("The caster carries one modifier while it is up"),
		Caster.AbilitySystem->GetStatModifiers().Num(), 1);

	// The world is never ticked here, so the ten second timer never fires.
	// Ending the ability is the same path the timer takes -- Finish calls
	// EndAbility -- and it is also the path a cancel or a death takes.
	Buff->EndAbility(Buff->GetCurrentAbilitySpecHandle(), Buff->GetCurrentActorInfo(),
					 Buff->GetCurrentActivationInfo(), true, false);

	TestEqual(TEXT("The modifier is gone once it ends"),
		Caster.AbilitySystem->GetStatModifiers().Num(), 0);
	TestEqual(TEXT("And the buff reports granting nothing"),
		Buff->GrantedIncrease, 0.0f);

	// So a hit afterwards is back to the plain figure.
	FScopedFighter Target(World, FVector(2 * M, 0, 0));
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"),
		TEXT("Molten Cleave"), TEXT("Element.Demonic"));

	const float Before = Target.Health();
	TestTrue(TEXT("The strike activates"), Activate(Caster, Strike));
	TestEqual(TEXT("It dealt the unbuffed 250% of weapon damage"),
		Before - Target.Health(), WeaponDamage * 250.0f / 100.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuffWithNothingBurningTest,
	"Cataclysm.Skills.ABuffWithNothingBurningNearbyGrantsNoIncrease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuffWithNothingBurningTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// Present, within the radius, and not on fire. Burning Wrath is written to
	// be worth using only after something has been set alight, so this is the
	// ordinary case rather than a fault.
	FScopedFighter NotAlight(World, FVector(3 * M, 0, 0));

	UCataclysmSelfBuffSkill* Buff = GrantSkill<UCataclysmSelfBuffSkill>(
		Caster, ECataclysmAbilitySlot::Support,
		TEXT("Duration=10; Radius=15; IncreasePerBurning=4"), TEXT("Burning Wrath"),
		TEXT("Element.Demonic"));
	TestTrue(TEXT("The buff still activates"), Activate(Caster, Buff));

	TestEqual(TEXT("Nothing was burning"), Buff->BurningEnemiesAtCast, 0);
	TestEqual(TEXT("So it granted nothing"), Buff->GrantedIncrease, 0.0f);
	TestEqual(TEXT("And added no modifier"),
		Caster.AbilitySystem->GetStatModifiers().Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuffLeavesPricedGroundTest,
	"Cataclysm.Skills.BurningGroundIsPricedWithTheBuffThatWasUpWhenItWasLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuffLeavesPricedGroundTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A patch of burning ground works out what one of its ticks is worth when it
	// is created, and keeps that figure for its whole life. So a buff that was
	// up at the moment it was left has to be in the price, or the increase would
	// apply to the skill and not to what the skill leaves behind.
	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Alight(World, FVector(3 * M, 0, 0));
	UCataclysmSkillEffects::ApplyBurn(Caster.Actor, Alight.Actor, 100.0f);

	UCataclysmSelfBuffSkill* Buff = GrantSkill<UCataclysmSelfBuffSkill>(
		Caster, ECataclysmAbilitySlot::Support,
		TEXT("Duration=10; Radius=15; IncreasePerBurning=4"), TEXT("Burning Wrath"),
		TEXT("Element.Demonic"));
	TestTrue(TEXT("The buff activates"), Activate(Caster, Buff));
	TestEqual(TEXT("It granted 4%"), Buff->GrantedIncrease, 4.0f);

	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=4; Angle=360; GroundRadius=3; GroundDuration=6; "
			 "GroundPercent=16.7"),
		TEXT("Molten Cleave"), TEXT("Element.Demonic"));
	TestTrue(TEXT("The strike activates"), Activate(Caster, Strike));

	ACataclysmGroundZone* Zone = TheOnlyGroundZone(World);
	if (!Zone)
	{
		AddError(TEXT("Expected exactly one patch of burning ground."));
		return false;
	}

	// PRICED FROM THE ROW'S OWN GroundPercent, WHICH IT WAS NOT. Until issue #590
	// this figure came from the Burn status effect's percent and duration, which
	// ignored the patch's own duration entirely. The expectation below is now the
	// stated rule -- 16.7% of the skill's damage per second, so a full six second
	// stay costs one hit -- rather than a second derivation of the old formula.
	const float Unbuffed = WeaponDamage * 250.0f / 100.0f * 16.7f / 100.0f;
	TestEqual(TEXT("A tick is priced with the 4% increase applied"),
		Zone->DamagePerTick, Unbuffed * 1.04f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillElementTagTest,
	"Cataclysm.Skills.ASkillKnowsItsOwnDamageTypeFromItsTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillElementTagTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	UCataclysmStrikeSkill* Demonic = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4"), TEXT("A Demonic Skill"),
		TEXT("Item.Weapon.Greataxe, Element.Demonic, Type.Strike"));
	TestEqual(TEXT("It picks the Element tag out of the row's other tags"),
		Demonic->ElementTag().ToString(), FString(TEXT("Element.Demonic")));

	UCataclysmStrikeSkill* Untagged = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Special, TEXT("Radius=4"), TEXT("No Tags"));
	TestFalse(TEXT("A skill with no tags has no element"),
		Untagged->ElementTag().IsValid());

	return true;
}

// --------------------------------------------------------------------------
// A projectile that occupies space while it flies. Issue #164: a Speed used to
// be a delay, so who was hit was decided entirely by where everyone stood at
// the moment of impact.
// --------------------------------------------------------------------------

namespace CataclysmProjectileTest
{
	using namespace CataclysmSkillTest;

	/** One frame at sixty a second, which is what the flight is stepped in. */
	constexpr float Frame = 1.0f / 60.0f;

	/**
	 * Step a projectile until it finishes, or until it plainly never will.
	 *
	 * The test world is built with UWorld::CreateWorld and is never ticked, so
	 * nothing moves on its own. ACataclysmProjectile::Step is public for exactly
	 * this reason, in the same way SwingOnce and Pulse are.
	 *
	 * @return how many frames it took
	 */
	int32 FlyToCompletion(ACataclysmProjectile* Projectile, int32 MaxFrames = 600)
	{
		int32 Frames = 0;
		while (Projectile && !Projectile->bFinished && Frames < MaxFrames)
		{
			Projectile->Step(Frame);
			++Frames;
		}
		return Frames;
	}

	/** Step it forward by a number of frames, stopping early if it finishes. */
	void FlyFor(ACataclysmProjectile* Projectile, int32 Frames)
	{
		for (int32 Index = 0; Index < Frames && Projectile && !Projectile->bFinished; ++Index)
		{
			Projectile->Step(Frame);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileIsAnActorTest,
	"Cataclysm.Skills.AProjectileWithASpeedIsAnActorThatMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileIsAnActorTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// Aimed forward. With no player controller AimPoint falls back to the
	// caster's own position, so AimedPointWithin uses the actor's facing, +X.
	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Speed=1800"), TEXT("Emberhurl"));
	if (!Hurl)
	{
		AddError(TEXT("Could not grant the throw."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	ACataclysmProjectile* Projectile = Hurl->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("A skill with a speed should have put a projectile in the world."));
		return false;
	}

	// IT HAS NOT ARRIVED YET, which is the whole point. Before this the hit was
	// resolved on a timer and nothing existed in between.
	TestEqual(TEXT("It starts at the caster"),
		Projectile->GetActorLocation(), Caster.Actor->GetActorLocation());
	TestFalse(TEXT("And has not finished"), Projectile->bFinished);

	// One frame at 1800 centimetres per second is 30 centimetres.
	Projectile->Step(Frame);
	const float Moved = FVector::Dist(Projectile->GetActorLocation(),
									  Caster.Actor->GetActorLocation());
	TestEqual(TEXT("One frame moves it speed times the frame"), Moved, 1800.0f * Frame, 0.5f);
	TestFalse(TEXT("It is still flying"), Projectile->bFinished);

	// Twelve metres at 1800 per second is two thirds of a second, or 40 frames.
	const int32 Frames = FlyToCompletion(Projectile);
	TestTrue(TEXT("It finished within a reasonable number of frames"), Frames < 100);
	TestTrue(TEXT("And it finished"), Projectile->bFinished);
	TestEqual(TEXT("The skill counted the landing"), Hurl->Landings, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileHitsWhoStepsInTest,
	"Cataclysm.Skills.AProjectileHitsSomeoneWhoStepsIntoItsPathMidFlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileHitsWhoStepsInTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// THIS IS THE CASE ISSUE #164 NAMED FIRST. Someone standing well off the
	// line when the throw happens, who walks onto it while the projectile is on
	// its way. Under the old delay there was nothing in the air to touch them.
	FScopedFighter Wanderer(World, FVector(6 * M, 10 * M, 0));

	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Speed=1800"), TEXT("Emberhurl"));
	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	ACataclysmProjectile* Projectile = Hurl->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("No projectile was fired."));
		return false;
	}

	// Let it get to about two metres, then put the wanderer at six metres,
	// which is still ahead of it.
	FlyFor(Projectile, 7);
	TestTrue(TEXT("It is still short of the wanderer"),
		Projectile->GetActorLocation().X < 6 * M);

	Wanderer.Actor->SetActorLocation(FVector(6 * M, 0, 0));
	const float Before = Wanderer.Health();

	FlyToCompletion(Projectile);
	TestTrue(TEXT("The wanderer was hit by the projectile passing through"),
		Wanderer.Health() < Before);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileMissesWhoStepsOutTest,
	"Cataclysm.Skills.AProjectileMissesSomeoneWhoLeavesBeforeItArrives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileMissesWhoStepsOutTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// THE OTHER HALF OF THE SAME CASE. Standing on the line when the throw
	// happens, and gone by the time the projectile gets there. Dodging is only
	// possible because the projectile occupies space over time.
	FScopedFighter Dodger(World, FVector(9 * M, 0, 0));

	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Speed=1800"), TEXT("Emberhurl"));
	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	ACataclysmProjectile* Projectile = Hurl->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("No projectile was fired."));
		return false;
	}

	const float Before = Dodger.Health();

	// Two metres in, still well short of nine.
	FlyFor(Projectile, 7);
	TestTrue(TEXT("It has not reached the dodger yet"),
		Projectile->GetActorLocation().X < 9 * M);

	Dodger.Actor->SetActorLocation(FVector(9 * M, 10 * M, 0));

	FlyToCompletion(Projectile);
	TestEqual(TEXT("The dodger took nothing, because it had moved"),
		Dodger.Health(), Before);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileHitsEachOnceTest,
	"Cataclysm.Skills.APiercingProjectileHitsEachEnemyOnceAsItPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileHitsEachOnceTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Near(World, FVector(3 * M, 0, 0));
	FScopedFighter Far(World, FVector(7 * M, 0, 0));
	FScopedFighter Beside(World, FVector(5 * M, 5 * M, 0));

	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Speed=1800"), TEXT("Emberhurl"));
	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	ACataclysmProjectile* Projectile = Hurl->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("No projectile was fired."));
		return false;
	}

	const float NearBefore = Near.Health();
	const float FarBefore = Far.Health();
	const float BesideBefore = Beside.Health();

	FlyToCompletion(Projectile);

	// ONCE EACH, NOT ONCE PER STEP. A projectile is inside a character for
	// several frames at these speeds, so without remembering who it has already
	// touched it would hit them on every one of them. The Special slot deals a
	// fixed percent of weapon damage, so the exact figure proves the count.
	const float OneHit = WeaponDamage * Hurl->GetDamagePercent() / 100.0f;
	TestEqual(TEXT("The near enemy was hit exactly once"),
		NearBefore - Near.Health(), OneHit);
	TestEqual(TEXT("So was the far one, because it pierces"),
		FarBefore - Far.Health(), OneHit);
	TestEqual(TEXT("One standing beside the line was not hit"),
		Beside.Health(), BesideBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileStopsAtFirstTest,
	"Cataclysm.Skills.AProjectileThatDoesNotPierceStopsAtTheFirstEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileStopsAtFirstTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter First(World, FVector(4 * M, 0, 0));
	FScopedFighter Behind(World, FVector(10 * M, 0, 0));

	// Blood Pyre. No Pierce, so it is the landing kind: it stops at the first
	// thing it touches and goes off there.
	UCataclysmProjectileSkill* Pyre = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=3; Speed=1400"), TEXT("Blood Pyre"));
	TestTrue(TEXT("It activates"), Activate(Caster, Pyre));

	ACataclysmProjectile* Projectile = Pyre->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("No projectile was fired."));
		return false;
	}

	const float FirstBefore = First.Health();
	const float BehindBefore = Behind.Health();

	FlyToCompletion(Projectile);

	TestTrue(TEXT("The first enemy was hit"), First.Health() < FirstBefore);
	TestEqual(TEXT("The one further along the line was not, because it stopped"),
		Behind.Health(), BehindBefore);

	// AND IT STOPPED THERE, not at the twelve metres it was aimed at. That is
	// what makes the pyre appear against the enemy rather than past them.
	TestTrue(TEXT("It stopped at roughly the first enemy, not at its aimed range"),
		FMath::Abs(Projectile->GetActorLocation().X - 4 * M) < 1.5f * M);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileDetonatesTest,
	"Cataclysm.Skills.AProjectileThatDoesNotPierceHitsEveryoneWhereItStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileDetonatesTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Struck(World, FVector(6 * M, 0, 0));

	// Standing two metres to the side of the one that stops it, which is inside
	// the three metre blast and not on the line the projectile travelled.
	FScopedFighter Nearby(World, FVector(6 * M, 2 * M, 0));

	UCataclysmProjectileSkill* Pyre = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=3; Speed=1400"), TEXT("Blood Pyre"));
	TestTrue(TEXT("It activates"), Activate(Caster, Pyre));

	ACataclysmProjectile* Projectile = Pyre->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("No projectile was fired."));
		return false;
	}

	const float StruckBefore = Struck.Health();
	const float NearbyBefore = Nearby.Health();

	FlyToCompletion(Projectile);

	const float OneHit = WeaponDamage * Pyre->GetDamagePercent() / 100.0f;
	TestEqual(TEXT("The one it stopped on took exactly one hit, not two"),
		StruckBefore - Struck.Health(), OneHit);
	TestEqual(TEXT("And so did the one standing beside them, from the blast"),
		NearbyBefore - Nearby.Health(), OneHit);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileReturnsTest,
	"Cataclysm.Skills.AReturningProjectileComesBackAndHitsAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileReturnsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter OnTheLine(World, FVector(5 * M, 0, 0));

	// Emberhurl "hits once going out and once returning to your hand".
	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Returns=1; Speed=1800"),
		TEXT("Emberhurl"));
	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	ACataclysmProjectile* Projectile = Hurl->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("No projectile was fired."));
		return false;
	}

	const float Before = OnTheLine.Health();

	FlyToCompletion(Projectile);

	TestTrue(TEXT("It turned round"), Projectile->bReturning);

	const float OneHit = WeaponDamage * Hurl->GetDamagePercent() / 100.0f;
	TestEqual(TEXT("The enemy on the line was hit twice, once each way"),
		Before - OnTheLine.Health(), OneHit * 2.0f);

	// And it ended up back where it was thrown from.
	TestTrue(TEXT("It came back to the caster"),
		FVector::Dist(Projectile->GetActorLocation(), Projectile->StartedAt) < M);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileBurnsRealPathTest,
	"Cataclysm.Skills.APiercingProjectileBurnsThePathItActuallyTravelled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileBurnsRealPathTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Speed=1800; Burn=1; "
			 "GroundRadius=1.5; GroundDuration=4; GroundPercent=25"),
		TEXT("Emberhurl"));
	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	ACataclysmProjectile* Projectile = Hurl->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("No projectile was fired."));
		return false;
	}

	FlyToCompletion(Projectile);

	ACataclysmGroundZone* Zone = TheOnlyGroundZone(World);
	if (!Zone)
	{
		AddError(TEXT("Expected exactly one patch of burning ground."));
		return false;
	}

	TestTrue(TEXT("The burning ground covers a path, not a point"), Zone->IsLong());
	TestTrue(TEXT("It starts where the throw did"),
		FVector::Dist(Zone->GetActorLocation(), Caster.Actor->GetActorLocation()) < M);
	TestTrue(TEXT("And ends where the projectile actually stopped"),
		FVector::Dist(Zone->FarEnd, Projectile->GetActorLocation()) < M);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileHitsAreScaledTest,
	"Cataclysm.Skills.AProjectileInFlightIsStillScaledByTheCastersBuffs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileHitsAreScaledTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A projectile deals its own damage rather than calling back into the
	// ability, so it has to carry the firing skill's tags with it or the
	// caster's scoped modifiers would stop applying the moment the shape
	// changed. Issue #166 built those modifiers; this checks they survive here.
	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Target(World, FVector(5 * M, 0, 0));

	FCataclysmStatModifier Increase;
	Increase.Bucket = ECataclysmStatBucket::Increased;
	Increase.Source = ECataclysmModifierSource::SkillBuff;
	Increase.Value = 50.0f;
	Increase.RequiredTags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Element.Demonic")), /*ErrorIfNotFound=*/false));
	Caster.AbilitySystem->AddStatModifier(Increase);

	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Speed=1800"), TEXT("Emberhurl"),
		TEXT("Item.Weapon.Greataxe, Element.Demonic"));
	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	const float Before = Target.Health();
	FlyToCompletion(Hurl->InFlight);

	const float Unbuffed = WeaponDamage * Hurl->GetDamagePercent() / 100.0f;
	TestEqual(TEXT("The hit carried the 50% increase scoped to its element"),
		Before - Target.Health(), Unbuffed * 1.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileBlockedByWallTest,
	"Cataclysm.Skills.AWallStopsAProjectileBeforeItReachesWhatIsBehindIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileBlockedByWallTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Sheltering(World, FVector(8 * M, 0, 0));

	// A solid wall at five metres, blocking the visibility channel. That is the
	// channel the flight traces against, because it is the one that answers
	// whether something solid stands between two points.
	AActor* Wall = World->SpawnActor<AActor>(FVector(5 * M, 0, 0), FRotator::ZeroRotator);
	if (!Wall)
	{
		AddError(TEXT("Could not spawn the wall."));
		return false;
	}
	UBoxComponent* Solid = NewObject<UBoxComponent>(Wall);
	Solid->InitBoxExtent(FVector(20.0f, 5 * M, 5 * M));
	Solid->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Solid->SetCollisionObjectType(ECC_WorldStatic);
	Solid->SetCollisionResponseToAllChannels(ECR_Block);
	Wall->SetRootComponent(Solid);
	Solid->RegisterComponent();
	Wall->SetActorLocation(FVector(5 * M, 0, 0));

	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Speed=1800"), TEXT("Emberhurl"));
	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	ACataclysmProjectile* Projectile = Hurl->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("No projectile was fired."));
		return false;
	}

	const float Before = Sheltering.Health();
	FlyToCompletion(Projectile);

	TestTrue(TEXT("It reports that geometry stopped it"),
		Projectile->bBlockedByGeometry);
	TestTrue(TEXT("It stopped at about the wall, not at its aimed range"),
		FMath::Abs(Projectile->GetActorLocation().X - 5 * M) < 0.5f * M);
	TestEqual(TEXT("The enemy behind the wall took nothing"),
		Sheltering.Health(), Before);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileNotBlockedByPawnsTest,
	"Cataclysm.Skills.AnEnemyIsNotCoverForTheEnemyBehindThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileNotBlockedByPawnsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// GUARDS THE WALL TEST ABOVE. If the flight traced against a channel that
	// characters blocked, every piercing projectile would stop at the first
	// enemy and Pierce would mean nothing. What a projectile passes through is
	// decided by Pierce; what stops it is geometry.
	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter InFront(World, FVector(3 * M, 0, 0));
	FScopedFighter Behind(World, FVector(7 * M, 0, 0));

	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Speed=1800"), TEXT("Emberhurl"));
	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	const float BehindBefore = Behind.Health();
	FlyToCompletion(Hurl->InFlight);

	TestTrue(TEXT("The one behind another enemy was still hit"),
		Behind.Health() < BehindBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileSweepsTest,
	"Cataclysm.Skills.AProjectileHitsWhatItPassedThroughNotOnlyWhereItLanded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileSweepsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// EXACTLY IN THE MIDDLE OF ONE STEP, AND WIDER THAN THE PROJECTILE IS. A
	// step is capped at a tenth of a second, which at Blood Pyre's 1400
	// centimetres per second is 140 centimetres. This enemy sits 70 centimetres
	// along, which is further than the projectile's 40 centimetre body from both
	// ends of that step. So a projectile that only asked where it had arrived
	// would pass straight through them and carry on to twelve metres.
	//
	// THIS IS THE CASE THE WHOLE CHANGE IS FOR: what a projectile hits is what it
	// PASSED THROUGH, not what happens to be standing where it stopped.
	//
	// It is written with a skill that does not pierce because that is where the
	// sweep does real work. A piercing skill is written Radius=1.5, and a body a
	// metre and a half wide is wider than half of any step it can take, so it
	// cannot pass over anybody in the first place.
	FScopedFighter Midstep(World, FVector(0.7f * M, 0, 0));

	UCataclysmProjectileSkill* Pyre = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=3; Speed=1400"), TEXT("Blood Pyre"));
	TestTrue(TEXT("It activates"), Activate(Caster, Pyre));

	ACataclysmProjectile* Projectile = Pyre->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("No projectile was fired."));
		return false;
	}

	TestTrue(TEXT("It starts further from the enemy than its own body"),
		0.7f * M > ACataclysmProjectile::DefaultBodyRadiusCm);

	const float Before = Midstep.Health();

	// One step of a tenth of a second: 0 to 140 centimetres in a single move.
	Projectile->Step(0.1f);

	TestTrue(TEXT("It hit the enemy the step passed over"),
		Midstep.Health() < Before);
	TestTrue(TEXT("And stopped there rather than flying on to its aimed range"),
		Projectile->GetActorLocation().X < 2.0f * M);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReturningProjectileGroundTest,
	"Cataclysm.Skills.AReturningProjectileBurnsItsFlightPathNotTheCastersFeet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReturningProjectileGroundTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using namespace CataclysmProjectileTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// EMBERHURL ENDS WHERE IT STARTED, because it "returns to your hand". So the
	// burning ground it leaves cannot be measured from where it was fired to
	// where it finished: those are the same point, and the result would be a
	// patch at the caster instead of the flight path the description promises.
	// It is measured to the furthest the projectile got.
	UCataclysmProjectileSkill* Hurl = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Range=12; Radius=1.5; Pierce=99; Returns=1; Speed=1800; Burn=1; "
			 "GroundRadius=1.5; GroundDuration=4; GroundPercent=25"),
		TEXT("Emberhurl"));
	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	ACataclysmProjectile* Projectile = Hurl->InFlight;
	if (!Projectile)
	{
		AddError(TEXT("No projectile was fired."));
		return false;
	}

	FlyToCompletion(Projectile);
	TestTrue(TEXT("It came back"), Projectile->bReturning);
	TestEqual(TEXT("And counted as two landings, once each way"), Hurl->Landings, 2);

	ACataclysmGroundZone* Zone = TheOnlyGroundZone(World);
	if (!Zone)
	{
		AddError(TEXT("Expected exactly one patch of burning ground."));
		return false;
	}

	TestTrue(TEXT("The burning ground covers a path, not a point"), Zone->IsLong());
	TestTrue(TEXT("It reaches roughly the twelve metres the throw covered"),
		FVector::Dist(Zone->GetActorLocation(), Zone->FarEnd) > 11 * M);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSlotScopedModifierTest,
	"Cataclysm.Skills.AModifierScopedToASlotReachesOnlyTheSkillInThatSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSlotScopedModifierTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// WHAT ISSUE #156 WAS ABOUT. The design says increases are scoped by tag and
	// lists the ability slot tags among the scopes, so an affix reading
	// "increased Heavy Attack damage" is a modifier scoped to Slot.Heavy. Only
	// Slot.Movement and Slot.Ultimate appeared on any skill row, so such a
	// modifier applied to nothing and nothing reported it. Every row now carries
	// its own slot tag, derived from the Slot column by the generator.
	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter HeavyTarget(World, FVector(2 * M, 0, 0));
	FScopedFighter SpecialTarget(World, FVector(-2 * M, 0, 0));

	FCataclysmStatModifier Increase;
	Increase.Bucket = ECataclysmStatBucket::Increased;
	Increase.Source = ECataclysmModifierSource::GearAffix;
	Increase.Value = 100.0f;
	Increase.RequiredTags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Slot.Heavy")), /*ErrorIfNotFound=*/false));
	TestTrue(TEXT("Slot.Heavy is a registered tag"),
		Increase.RequiredTags.Num() == 1);
	Caster.AbilitySystem->AddStatModifier(Increase);

	// Both hit in a full ring, so the only difference between them is the slot
	// tag each carries.
	UCataclysmStrikeSkill* Heavy = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"),
		TEXT("A Heavy Skill"), TEXT("Element.Demonic, Slot.Heavy"));
	UCataclysmStrikeSkill* Special = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Special, TEXT("Radius=4; Angle=360"),
		TEXT("A Special Skill"), TEXT("Element.Demonic, Slot.Special"));

	// Only the Heavy one runs first, and only the enemy in front of it exists to
	// be hit, so the two figures can be read apart.
	SpecialTarget.Actor->SetActorLocation(FVector(0, 50 * M, 0));
	const float HeavyBefore = HeavyTarget.Health();
	TestTrue(TEXT("The Heavy skill activates"), Activate(Caster, Heavy));
	const float HeavyDealt = HeavyBefore - HeavyTarget.Health();

	HeavyTarget.Actor->SetActorLocation(FVector(0, 50 * M, 0));
	SpecialTarget.Actor->SetActorLocation(FVector(2 * M, 0, 0));
	const float SpecialBefore = SpecialTarget.Health();
	TestTrue(TEXT("The Special skill activates"), Activate(Caster, Special));
	const float SpecialDealt = SpecialBefore - SpecialTarget.Health();

	TestEqual(TEXT("The Heavy skill got the doubled damage"),
		HeavyDealt, WeaponDamage * Heavy->GetDamagePercent() / 100.0f * 2.0f);
	TestEqual(TEXT("The Special skill got the plain damage"),
		SpecialDealt, WeaponDamage * Special->GetDamagePercent() / 100.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShippedSlotTagsTest,
	"Cataclysm.Data.EverySkillTheTableOffersCarriesItsOwnSlotTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmShippedSlotTagsTest::RunTest(const FString&)
{
	// THE REAL DATA, not a hand-written cell. tools/tests/test_slot_tags.py
	// checks the generated CSV and
	// Cataclysm.Data.EveryGeneratedTableHasAnAssetThatMatchesIt checks the asset
	// matches that CSV. This is the last link in the chain: a row read out of the
	// shipped DataTable arrives with its slot tag attached, which is what makes a
	// modifier scoped to Slot.Heavy reach a Heavy skill. Issue #156.
	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!Table)
	{
		AddError(TEXT("Could not load the generated weapon skill table."));
		return false;
	}

	// One weapon of each kind the Demonic vertical slice uses, so every slot the
	// design fills is represented.
	const TCHAR* Weapons[] = { TEXT("Greataxe"), TEXT("Fist"), TEXT("Staff") };

	int32 Checked = 0;
	for (const TCHAR* Weapon : Weapons)
	{
		for (const FCataclysmWeaponSkill& Skill :
				UCataclysmWeaponSkills::SkillsFor(Table, Weapon, TEXT("Demonic")))
		{
			bool bFoundOne = false;
			for (const FGameplayTag& Tag : Skill.Tags)
			{
				if (Tag.ToString().StartsWith(TEXT("Slot.")))
				{
					bFoundOne = true;
					++Checked;
					break;
				}
			}
			TestTrue(FString::Printf(
				TEXT("'%s' carries a slot tag"), *Skill.Name), bFoundOne);
		}
	}

	// Guards the loop itself. If SkillsFor returned nothing the assertions above
	// would all pass without testing anything.
	TestTrue(TEXT("Some skills were actually checked"), Checked >= 10);

	return true;
}

// --------------------------------------------------------------------------
// That the swing is still drawn at all
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEverySwingAsksForAnArc,
	"Cataclysm.Effects.EverySwingAsksForAnArc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEverySwingAsksForAnArc::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// THIS IS THE TEST THE WHOLE SHAPE DEPENDS ON, and it lives in this file
	// rather than beside the other NS_Strike_Arc tests because the harness that
	// can grant and drive a real skill is here.
	//
	// WHAT IT REPAIRS. NS_Strike_Arc could be authored perfectly, every asset
	// test above it could pass, and a melee swing could still draw nothing --
	// which is exactly what UCataclysmClassStats did before issue #807: it
	// worked, it had tests, and no code path reached it. Deleting the
	// UCataclysmStrikeEffect::PlayAt call from UCataclysmStrikeSkill::SwingOnce
	// fails this and nothing else in the suite.
	//
	// IT COUNTS ASKS RATHER THAN ARCS. No test in this project can observe a
	// Niagara component: the automation command passes -nullrhi and Niagara
	// refuses to create one when FApp::CanEverRender() is false. Issue #559.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const int32 Before = UCataclysmStrikeEffect::TimesAsked;

	// SWUNG WITH NOTHING IN RANGE, ON PURPOSE. A swing that misses still
	// happened, and the arc is drawn whether or not anything was hit -- the
	// opposite of the impact burst, which refuses to draw for a blow that
	// connected with nothing. A player who saw nothing when they hit thin air
	// would read it as the button not working.
	const int32 Hit = Strike->SwingOnce();
	TestEqual(TEXT("nothing was in range, so this is a swing at thin air"),
		Hit, 0);

	TestEqual(TEXT("a swing asks for exactly one arc"),
		UCataclysmStrikeEffect::TimesAsked, Before + 1);

	// AND A REPEATING STRIKE ASKS AGAIN EACH TIME. Pyroclasm swings every 0.2
	// seconds for three seconds; an arc drawn once at the start and never again
	// would leave fourteen of its fifteen swings invisible.
	Strike->SwingOnce();
	Strike->SwingOnce();
	TestEqual(TEXT("three swings ask for three arcs"),
		UCataclysmStrikeEffect::TimesAsked, Before + 3);

	return true;
}

// --------------------------------------------------------------------------
// The burst at the caster, which every skill asks for
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAsksForACastBurstTest,
	"Cataclysm.Effects.EverySkillAsksForACastBurstWhenItGoesOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillAsksForACastBurstTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// A STRIKE STANDS IN FOR ALL EIGHT SHAPES. The call is in
	// UCataclysmSkillTemplate::CommitAndBegin, which every shape calls first, so
	// one of them exercising it exercises the line all of them share.
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3; Angle=90"),
		TEXT("Cinderslash"), TEXT("Element.Demonic, Type.Strike"));
	if (!TestNotNull(TEXT("a granted strike"), Strike))
	{
		return false;
	}

	const int32 Before = UCataclysmCastEffect::TimesAsked;
	UCataclysmCastEffect::LastDamageTypeAsked = NAME_None;

	if (!TestTrue(TEXT("the skill activates"), Activate(Caster, Strike)))
	{
		return false;
	}

	// THE WHOLE POINT. Before 2026-08-22 a skill began with nothing happening at
	// the caster at all: the bolt and the hit burst existed and the third of the
	// three systems had never been built. Issue #811.
	TestEqual(TEXT("using a skill asks for a burst at the caster"),
		UCataclysmCastEffect::TimesAsked, Before + 1);

	// AND IN THE SKILL'S OWN COLOUR, which is the other half. A player's hits
	// carry no damage type, so without the skill's Element tag reaching the
	// effect this would be None and the burst would draw the authored white.
	// Issue #803.
	TestEqual(TEXT("and asks for it in the damage type the skill row names"),
		UCataclysmCastEffect::LastDamageTypeAsked, FName(TEXT("Demonic")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRefusedSkillDrawsNoBurstTest,
	"Cataclysm.Effects.ASkillThatNeverStartsDrawsNoCastBurst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRefusedSkillDrawsNoBurstTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3; Angle=90"),
		TEXT("Cinderslash"), TEXT("Element.Demonic, Type.Strike"));
	if (!TestNotNull(TEXT("a granted strike"), Strike))
	{
		return false;
	}

	// ONE MANA, WHICH IS HOW
	// Cataclysm.Skills.ASkillIsRefusedWhenTheManaIsNotThere makes a skill refuse
	// itself: the Heavy slot costs more than that.
	Caster.Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1.0f);

	const int32 Before = UCataclysmCastEffect::TimesAsked;
	TestFalse(TEXT("the skill is refused"), Activate(Caster, Strike));

	// A SKILL NOBODY CAN PAY FOR DRAWS NOTHING. A flash on a skill that never
	// fired would read as a bug and would tell the player a skill went off when
	// it did not.
	//
	// WHAT THIS DOES NOT PROVE, AND IT WAS WRITTEN BELIEVING IT DID. It does not
	// show that the call sits PAST CommitAbility inside CommitAndBegin. Moving
	// the call above the commit still passes this test, which was measured with
	// prove_cast.py rather than assumed: the ability system checks the cost in
	// TryActivateAbility and refuses the activation before the skill's body runs
	// at all, so nothing inside CommitAndBegin executes either way. The
	// placement is still right -- CommitAbility can refuse for reasons the
	// earlier check did not see -- but no test in this suite distinguishes it.
	TestEqual(TEXT("a skill that could not pay its cost draws nothing"),
		UCataclysmCastEffect::TimesAsked, Before);

	return true;
}


// --------------------------------------------------------------------------
// Area of effect, which nothing read until issue #895
// --------------------------------------------------------------------------

/**
 * A CASTER'S AREA OF EFFECT WIDENS THE SKILLS IT SHOULD AND NO OTHERS. #895.
 *
 * WHAT WAS WRONG. The `AreaOfEffect` attribute existed, was clamped, was
 * replicated, and was given 100 by the shared Default line of
 * game/Data/ClassStats.csv, and no code in the project read it. Every skill in
 * the game used the radius its data stated, so `Stat_Increased_area_of_effect`
 * was worth nothing.
 *
 * WHAT DECIDES WHETHER A RADIUS IS AN AREA AT ALL. The design settles it by the
 * tags rather than by the shape: `Type.AOE.PointBlank` and `Type.AOE.Aura` are
 * "the two tags that make a skill's hit area damage". A Strike's radius is how
 * far it reaches and a Projectile's is how wide the bolt is, and widening those
 * is not what the affix says it does.
 *
 * A HUNDRED MEANS UNCHANGED. The design gives area of effect and the three
 * damage over time stats a baseline of 100 rather than zero, "because they are
 * percentages of whatever the skill or the effect itself does".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAreaOfEffectWidensAnAreaTest,
	"Cataclysm.Skills.AreaOfEffectWidensAnAreaSkillAndNotAReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAreaOfEffectWidensAnAreaTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// THREE SKILLS: two carrying an area tag and one carrying none. All three
	// state the same radius so the answers below are comparable.
	//
	// A PERSISTENT ONE IS HERE ON PURPOSE. It is the tag that would be missed by
	// scoping this on whether the skill's BLOW is area damage, which
	// UCataclysmSkillEffects::IsAreaDamage answers and which deliberately leaves
	// Persistent out. 26 of the 63 skill rows carrying an area tag carry that
	// one.
	UCataclysmStrikeSkill* Burst = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"),
		TEXT("Burst"), TEXT("Type.AOE.PointBlank"));
	UCataclysmStrikeSkill* Trail = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Movement, TEXT("Radius=4; Angle=360"),
		TEXT("Trail"), TEXT("Type.AOE.Persistent"));
	UCataclysmStrikeSkill* Reach = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Special, TEXT("Radius=4; Angle=90"),
		TEXT("Reach"), TEXT("Type.Strike, Type.Melee"));

	if (!Burst || !Trail || !Reach)
	{
		AddError(TEXT("Could not grant the three skills."));
		return false;
	}

	const float Stated = Burst->Params.RadiusCm;
	if (!TestTrue(TEXT("the skills state a radius at all"), Stated > 0.0f))
	{
		return false;
	}

	// A CHARACTER SITTING ON THE BASELINE CHANGES NOTHING, asserted first so the
	// figures below are evidence of the stat rather than of the reading.
	Caster.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAreaOfEffectAttribute(), 100.0f);

	TestEqual(TEXT("at the 100 baseline an area skill uses its stated radius"),
		Burst->ScaledRadiusCm(), Stated, 0.01f);
	TestEqual(TEXT("and so does one that leaves a persistent area"),
		Trail->ScaledRadiusCm(), Stated, 0.01f);
	TestEqual(TEXT("and so does a skill whose radius is only a reach"),
		Reach->ScaledRadiusCm(), Stated, 0.01f);

	// AND FIFTY PER CENT MORE WIDENS THE AREA SKILL BY HALF.
	Caster.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAreaOfEffectAttribute(), 150.0f);

	TestEqual(TEXT("150 widens an area skill by half"),
		Burst->ScaledRadiusCm(), Stated * 1.5f, 0.01f);

	// EVERY AREA TAG AND NOT ONLY THE TWO THAT MAKE A HIT EVADABLE. This is the
	// case the first version of this got wrong.
	TestEqual(TEXT("and widens one that leaves a persistent area just the same"),
		Trail->ScaledRadiusCm(), Stated * 1.5f, 0.01f);

	// AND LEAVES THE REACH ALONE, which is the half that stops this affix
	// lengthening a sword swing.
	TestEqual(TEXT("and leaves a skill that is not an area exactly as it was"),
		Reach->ScaledRadiusCm(), Stated, 0.01f);

	return true;
}

/**
 * A SCOPED AREA OF EFFECT BONUS WIDENS THE SKILLS IT NAMES AND NO OTHERS.
 * Issue #943.
 *
 * THIS IS THE REAL ENTRY POINT AND THAT IS THE WHOLE REASON IT EXISTS.
 * `Cataclysm.Passives.ATagScopedNodeReachesOnlyTheSkillsItNames` proves
 * `UCataclysmAbilitySystemComponent::StatForSkill` gives the right answer, and
 * it would go on passing if `UCataclysmSkillTemplate::AreaOfEffectMultiplier`
 * were put back to reading the plain gameplay attribute -- which is the half
 * that makes a trap actually wider in the game. A system covered everywhere
 * except where it is used has bitten this project before.
 *
 * NO PASSIVE TREE AND NO SABOTEUR HERE. The stat inputs are set directly, so
 * this measures the reading rather than the authoring and keeps working however
 * `game/Data/PassiveEffects.csv` is re-authored. The project owner put the
 * Saboteur outside the vertical slice on 2026-08-25; issue #946 records that.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAreaOfEffectScopedByTagTest,
	"Cataclysm.Skills.AScopedAreaOfEffectBonusWidensOnlyTheSkillsItNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAreaOfEffectScopedByTagTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// TWO AREA SKILLS STATING THE SAME RADIUS, differing only in the trap tag,
	// so any difference below is the scoping and nothing else. Both carry an
	// area tag, because a skill whose radius is only a reach is left alone by
	// area of effect either way and would prove nothing here.
	UCataclysmStrikeSkill* Trap = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Special, TEXT("Radius=4; Angle=360"),
		TEXT("Snare"), TEXT("Type.AOE.PointBlank, Type.Trap"));
	UCataclysmStrikeSkill* NotATrap = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"),
		TEXT("Burst"), TEXT("Type.AOE.PointBlank"));

	if (!Trap || !NotATrap)
	{
		AddError(TEXT("Could not grant the two skills."));
		return false;
	}

	const float Stated = Trap->Params.RadiusCm;
	if (!TestTrue(TEXT("the skills state a radius at all"), Stated > 0.0f))
	{
		return false;
	}

	const FGameplayTag TrapTag = UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("the trap tag is declared"), TrapTag.IsValid()))
	{
		return false;
	}

	// THE ATTRIBUTE STAYS AT THE BASELINE, and that is deliberate. It is what a
	// character sheet shows, and a bonus scoped to traps must not appear there.
	// Were the scoped modifier being folded into the attribute instead, the
	// second assertion below would fail.
	Caster.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAreaOfEffectAttribute(), 100.0f);

	// NINETY PER CENT MORE AREA, FOR TRAPS ONLY. The same shape a passive node
	// produces, set here directly rather than earned, so this does not depend on
	// which nodes happen to be authored.
	FCataclysmStatModifier ForTraps;
	ForTraps.Bucket = ECataclysmStatBucket::Increased;
	ForTraps.Source = ECataclysmModifierSource::PassiveKeystone;
	ForTraps.Value = 90.0f;
	ForTraps.RequiredTags.AddTag(TrapTag);

	FCataclysmStatInputs Inputs;
	Inputs.Base = 100.0f;
	Inputs.Modifiers.Add(ForTraps);

	TMap<FName, FCataclysmStatInputs> All;
	All.Add(FName(TEXT("area_of_effect")), Inputs);
	Caster.AbilitySystem->SetStatInputs(MoveTemp(All));

	TestEqual(TEXT("the trap is widened by ninety per cent"),
		Trap->ScaledRadiusCm(), Stated * 1.9f, 0.01f);

	// WITHOUT THIS THE TEST WOULD PASS IF SCOPING WERE IGNORED ALTOGETHER, which
	// would widen every area skill in the game rather than the traps.
	TestEqual(TEXT("and an area skill that is not a trap is left exactly as it was"),
		NotATrap->ScaledRadiusCm(), Stated, 0.01f);

	return true;
}

/**
 * BURNING GROUND WIDENS WHATEVER LEFT IT. Issue #895.
 *
 * ALWAYS, UNLIKE A SKILL'S OWN RADIUS. The design: "A zone's own damage is area
 * damage, decided where the zone deals it." So the ground a Strike leaves is an
 * area even though the Strike itself is one sword blow that evasion applies to.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAreaOfEffectWidensGroundTest,
	"Cataclysm.Skills.AreaOfEffectWidensTheGroundASkillLeavesWhateverTheSkillIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAreaOfEffectWidensGroundTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// A SKILL THAT IS NOT AN AREA AND LEAVES GROUND THAT IS. The tags say it is
	// one sword blow; the patch it leaves burning is area damage regardless.
	UCataclysmStrikeSkill* Slash = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=4; Angle=90; GroundRadius=6; GroundDuration=5; GroundPercent=20"),
		TEXT("Slash"), TEXT("Type.Strike, Type.Melee"));

	if (!Slash)
	{
		AddError(TEXT("Could not grant the skill."));
		return false;
	}

	const float StatedGround = Slash->Params.GroundRadiusCm;
	const float StatedRadius = Slash->Params.RadiusCm;
	if (!TestTrue(TEXT("the skill states a ground radius"), StatedGround > 0.0f))
	{
		return false;
	}

	Caster.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAreaOfEffectAttribute(), 100.0f);
	TestEqual(TEXT("at the 100 baseline the ground is the size it states"),
		Slash->ScaledGroundRadiusCm(), StatedGround, 0.01f);

	Caster.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAreaOfEffectAttribute(), 150.0f);

	TestEqual(TEXT("150 widens the ground by half even for a skill that is not an area"),
		Slash->ScaledGroundRadiusCm(), StatedGround * 1.5f, 0.01f);

	// AND THE SWORD BLOW ITSELF IS STILL THE SIZE IT WAS, which is what makes
	// the two rules different rather than one rule applied twice.
	TestEqual(TEXT("while the blow itself keeps its stated reach"),
		Slash->ScaledRadiusCm(), StatedRadius, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
