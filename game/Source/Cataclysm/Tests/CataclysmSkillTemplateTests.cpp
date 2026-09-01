// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCastEffect.h"
// For the Fervour pool a health cost fills. Issue #954.
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
// For health owed and the share of a cost taken later. Issue #991.
#include "AbilitySystem/CataclysmHealthDebt.h"
// For the Fervour a cast itself grants. Issue #1051.
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
// For the flag saying a character pays with health where others pay with
// mana. Issue #1067.
#include "AbilitySystem/CataclysmSkillTemplate.h"
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

	// TEN PERCENT OF MAXIMUM HEALTH, a round share of the pool chosen so the
	// arithmetic below is exact.
	//
	// IT WAS DEEPER CUTS AT TEN POINTS AND IS NO LONGER. That node charged 1% of
	// maximum health a point until issue #1107 lowered it to 0.25%, so ten
	// points now add 2.5% and no node on its own reaches ten. Exsanguinate's 15%
	// of CURRENT health passes it for a character above two thirds of its pool.
	// The figure is kept because what this test measures is the arithmetic of a
	// cost, not which node supplied it.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCurrentHealthCostTest,
	"Cataclysm.Skills.ACharacterCanAddAHealthCostMeasuredAgainstCurrentHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCurrentHealthCostTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// THE MASOCHIST'S EXSANGUINATE KEYSTONE: "Every skill costs an additional
	// 15% of your current health". Issue #986.
	//
	// A SECOND STAT AND NOT A LARGER FIRST ONE, and the whole test is about why.
	// The character's other added cost is a share of MAXIMUM health and can
	// kill; this one is a share of CURRENT health and cannot. `docs/DECISIONS.md`
	// records the project owner drawing that line, quoting this very number.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	// HALF HEALTH, so a share of current health and a share of maximum health
	// are different numbers and cannot be confused.
	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 500.0f);
	Caster.Set(
		UCataclysmClassResourceAttributeSet::GetAddedHealthCostOfCurrentAttribute(),
		15.0f);

	// A SKILL THAT STATES NO HEALTH COST OF ITS OWN, which is every skill in
	// the game except Blood Pyre. Without the keystone it would charge no
	// health at all, so every figure below is the keystone's doing.
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3"), TEXT("Strike"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	// 15% OF 500 CURRENT IS 75.
	//
	// NOT 150. That is 15% of MAXIMUM health, which is the other stat, and it is
	// the one number this test exists to rule out.
	const float Paid = 500.0f - Caster.Health();
	TestEqual(FString::Printf(
		TEXT("it charged 15%% of current health, which is 75, and charged %.1f"),
		Paid),
		Paid, 75.0f, 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCurrentHealthCostCannotKillTest,
	"Cataclysm.Skills.ACostTakenFromCurrentHealthLeavesAtLeastOneHealthBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCurrentHealthCostCannotKillTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// EXSANGUINATE STATES THE FLOOR OUTRIGHT: "A cost taken from current health
	// cannot reduce it below 1." Issue #986.
	//
	// THE ARITHMETIC NEARLY GUARANTEES IT ALREADY, and that is why the floor is
	// worth a test rather than being assumed. A share of current health
	// approaches zero without reaching it, so no number of casts empties the bar
	// in exact arithmetic -- but a float does reach zero, and a rule that holds
	// in algebra and fails in single precision is not a rule.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	// A CHARACTER ALREADY ON ALMOST NOTHING, and a cost far larger than the
	// design's own. 90% of half a point of health would leave 0.05 behind, which
	// is above zero and below the floor, so only the floor can produce the
	// expected answer.
	//
	// MAXIMUM HEALTH IS 1 SO THAT THIS TEST CANNOT SEE THE MEASURE. A share of
	// current health and a share of maximum health both come to less than the
	// floor swallows here, so what this character pays depends on the floor and
	// on nothing else. The test above is the one that watches the measure, and
	// keeping the two separable is what lets a broken build say which is wrong.
	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.5f);
	Caster.Set(
		UCataclysmClassResourceAttributeSet::GetAddedHealthCostOfCurrentAttribute(),
		90.0f);

	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3"), TEXT("Strike"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	// ALREADY BELOW THE FLOOR, SO NOTHING IS TAKEN. The floor is "cannot reduce
	// it below 1", not "sets it to 1": a character under the floor is not healed
	// up to it.
	TestEqual(TEXT("a character already below the floor pays nothing"),
			  Caster.Health(), 0.5f, 0.01f);

	// AND A COST WELL CLEAR OF THE FLOOR IS NOT FLOORED, so the floor cannot be
	// a clamp that fires on every cast. 90% of 100 is 90, leaving 10, and the
	// floor would only have bitten below 1.
	FScopedFighter Second(World, FVector(4 * M, 0, 0));
	//
	// AT FULL HEALTH, so a share of current and a share of maximum are the same
	// number and this half cannot see the measure either.
	Second.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	Second.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100.0f);
	Second.Set(
		UCataclysmClassResourceAttributeSet::GetAddedHealthCostOfCurrentAttribute(),
		90.0f);

	UCataclysmStrikeSkill* Another = GrantSkill<UCataclysmStrikeSkill>(
		Second, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3"), TEXT("Strike"));
	if (!Another)
	{
		AddError(TEXT("Could not grant the second strike."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Second, Another));
	TestEqual(TEXT("a cost well clear of the floor is not floored"),
			  Second.Health(), 10.0f, 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeferredShareArithmeticTest,
	"Cataclysm.Skills.WhatShareOfAHealthCostIsTakenLaterIsPlainArithmetic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeferredShareArithmeticTest::RunTest(const FString&)
{
	// PURE ARITHMETIC AND NO ABILITY SYSTEM, which is why this function exists
	// apart from the one that reads the attribute. Issue #991.
	using Debt = UCataclysmHealthDebt;

	// THE MASOCHIST'S DEFERRED PAYMENT NODE at one point and at its full ten:
	// "10% per point of the health a skill costs is not taken when the skill is
	// used." Ten points defers the whole cost.
	TestEqual(TEXT("one point defers a tenth"),
			  Debt::AmountDeferred(200.0f, 10.0f), 20.0f, 0.001f);
	TestEqual(TEXT("ten points defer the whole cost"),
			  Debt::AmountDeferred(200.0f, 100.0f), 200.0f, 0.001f);

	// A CHARACTER WITHOUT THE NODE DEFERS NOTHING, which is every character in
	// the game until a point is spent there.
	TestEqual(TEXT("no share defers nothing"),
			  Debt::AmountDeferred(200.0f, 0.0f), 0.0f, 0.001f);
	TestEqual(TEXT("and no cost defers nothing"),
			  Debt::AmountDeferred(0.0f, 100.0f), 0.0f, 0.001f);

	// NEVER MORE THAN THE COST, however the share arrives. The attribute is
	// already held between 0 and 100 when it is written; this is what happens if
	// a figure reaches here anyway, and handing health back would be worse than
	// clamping.
	TestEqual(TEXT("a share above a hundred still defers only the cost"),
			  Debt::AmountDeferred(200.0f, 250.0f), 200.0f, 0.001f);
	TestEqual(TEXT("and a negative share defers nothing"),
			  Debt::AmountDeferred(200.0f, -50.0f), 0.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeferredHealthCostTest,
	"Cataclysm.Skills.APartOfAHealthCostCanBeTakenLaterInsteadOfNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeferredHealthCostTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	// THE MASOCHIST'S DEFERRED PAYMENT NODE, END TO END. Issue #991.
	//
	// WHAT THIS PROVES THAT THE ARITHMETIC TEST DOES NOT. That the share reaches
	// the place a cost is worked out, that what is not taken is recorded as owed,
	// and that the character really keeps the health in the meantime.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	Caster.Set(Vital::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(Vital::GetHealthAttribute(), 1'000.0f);

	// A COST OF A HUNDRED: ten per cent of maximum health, through the node that
	// adds a cost to every skill. Half of it is deferred.
	Caster.Set(Resource::GetAddedHealthCostAttribute(), 10.0f);
	Caster.Set(Resource::GetDeferredHealthCostShareAttribute(), 50.0f);

	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3"), TEXT("Strike"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	// HALF TAKEN NOW.
	TestEqual(TEXT("half the cost was taken at once"),
			  1'000.0f - Caster.Health(), 50.0f, 0.5f);

	// AND HALF OWED.
	TestEqual(TEXT("and the other half is owed"),
			  Caster.Get(Resource::GetHealthOwedAttribute()), 50.0f, 0.5f);

	// THE DEBT IS NOT DUE YET, so a step now takes nothing. Three seconds have
	// not passed; the world's clock has not moved at all.
	TestEqual(TEXT("a debt that is not due yet is not taken"),
			  UCataclysmHealthDebt::DrainIfDue(
				  Caster.Actor, UCataclysmHealthDebt::DrainSeconds),
			  0.0f, 0.001f);
	TestEqual(TEXT("and the health is still there"), Caster.Health(), 950.0f,
			  0.5f);

	// AND THREE SECONDS LATER IT IS TAKEN. Nothing else changes: no point is
	// spent, no stat rewritten, no skill used. The clock moves and that is all.
	World->TimeSeconds += UCataclysmHealthDebt::DelaySeconds + 0.1f;
	TestEqual(TEXT("once due, the whole debt is taken"),
			  UCataclysmHealthDebt::DrainIfDue(
				  Caster.Actor, UCataclysmHealthDebt::DrainSeconds),
			  50.0f, 0.5f);
	TestEqual(TEXT("and the health is gone"), Caster.Health(), 900.0f, 0.5f);
	TestEqual(TEXT("and nothing is owed any more"),
			  Caster.Get(Resource::GetHealthOwedAttribute()), 0.0f, 0.001f);

	// AND IT IS NOT TAKEN TWICE. A settled debt clears its due time, so every
	// later step finds nothing.
	TestEqual(TEXT("a settled debt is not taken again"),
			  UCataclysmHealthDebt::DrainIfDue(
				  Caster.Actor, UCataclysmHealthDebt::DrainSeconds),
			  0.0f, 0.001f);
	TestEqual(TEXT("and the health is unchanged"), Caster.Health(), 900.0f,
			  0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoDeferralTakesTheWholeCostTest,
	"Cataclysm.Skills.ACharacterWithoutTheNodePaysItsWholeHealthCostAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNoDeferralTakesTheWholeCostTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	// EVERY CHARACTER IN THE GAME UNTIL A POINT IS SPENT IN DEFERRED PAYMENT.
	// Issue #991. Without this the test above would pass just as well if the
	// whole cost were always deferred, which would change what every existing
	// health cost does.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	Caster.Set(Vital::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(Vital::GetHealthAttribute(), 1'000.0f);
	Caster.Set(Resource::GetAddedHealthCostAttribute(), 10.0f);

	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3"), TEXT("Strike"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	TestEqual(TEXT("the whole cost was taken at once"),
			  1'000.0f - Caster.Health(), 100.0f, 0.5f);
	TestEqual(TEXT("and nothing is owed"),
			  Caster.Get(Resource::GetHealthOwedAttribute()), 0.0f, 0.001f);

	// AND NOTHING FALLS DUE LATER EITHER, so a character without the node never
	// acquires a due time it would then carry for the rest of its life.
	World->TimeSeconds += UCataclysmHealthDebt::DelaySeconds + 0.1f;
	TestEqual(TEXT("and nothing falls due later"),
			  UCataclysmHealthDebt::DrainIfDue(
				  Caster.Actor, UCataclysmHealthDebt::DrainSeconds),
			  0.0f, 0.001f);
	TestEqual(TEXT("and the health is unchanged"), Caster.Health(), 900.0f,
			  0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCompoundInterestTest,
	"Cataclysm.Skills.DamageCanGrowWithTheHealthACharacterOwes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCompoundInterestTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	// THE MASOCHIST'S COMPOUND INTEREST NODE, END TO END. Issue #994: "+1%
	// increased damage per point for every 5% of your maximum health you
	// currently owe", at its full eight points.
	//
	// WHAT THIS PROVES THAT THE PIPELINE TEST DOES NOT. That the amount owed
	// really reaches the reading the pipeline is handed, through
	// `UCataclysmAbilitySystemComponent::CurrentConditions`, and that casting a
	// skill is what puts it there.
	//
	// THE WHOLE COST IS DEFERRED, WHICH IS WHAT LETS THIS TEST SEE THE
	// DIFFERENCE. The character's health does not move at all, so a bonus
	// reading MISSING health would be worth nothing here and only a bonus
	// reading what is OWED can produce the figure below. The two states are one
	// substitution apart in the code and would otherwise be indistinguishable.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	Caster.Set(Vital::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(Vital::GetHealthAttribute(), 1'000.0f);

	FCataclysmStatModifier CompoundInterest;
	CompoundInterest.Bucket = ECataclysmStatBucket::Increased;
	CompoundInterest.Source = ECataclysmModifierSource::PassiveKeystone;
	CompoundInterest.Value = 8.0f;
	CompoundInterest.Scale =
		ECataclysmStatScale::PerPercentOfMaximumHealthOwed;
	CompoundInterest.ScaleStep = 5.0f;

	FCataclysmStatInputs Inputs;
	Inputs.Base = 0.0f;
	Inputs.Modifiers.Add(CompoundInterest);

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("attack_damage")), Inputs);
	Caster.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	const FGameplayTagContainer NoTags;

	// OWING NOTHING IS WORTH NOTHING, which is where every character starts.
	TestEqual(TEXT("a character that owes nothing gets nothing"),
			  Caster.AbilitySystem->AttackDamageIncreasesForSkill(NoTags),
			  0.0f, 0.0001f);

	// A COST OF TWO HUNDRED -- twenty per cent of maximum health -- all of it
	// deferred.
	Caster.Set(Resource::GetAddedHealthCostAttribute(), 20.0f);
	Caster.Set(Resource::GetDeferredHealthCostShareAttribute(), 100.0f);

	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3"), TEXT("Strike"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	TestEqual(TEXT("the whole cost was deferred"),
			  Caster.Get(Resource::GetHealthOwedAttribute()), 200.0f, 0.5f);
	TestEqual(TEXT("so the health did not move"), Caster.Health(), 1'000.0f,
			  0.5f);

	// TWENTY PER CENT OWED IS FOUR WHOLE STEPS OF FIVE, and eight per point is
	// thirty-two percentage points. The function answers a fraction.
	TestEqual(TEXT("owing a fifth of maximum health is +32% damage"),
			  Caster.AbilitySystem->AttackDamageIncreasesForSkill(NoTags),
			  0.32f, 0.0001f);

	// AND IT GOES AWAY WHEN THE DEBT IS PAID. Nothing else changes: the clock
	// moves, the debt settles, and the bonus goes with it. Without this the
	// bonus could be a one-way ratchet and every assertion above would still
	// pass.
	World->TimeSeconds += UCataclysmHealthDebt::DelaySeconds + 0.1f;
	TestEqual(TEXT("the debt falls due and is taken"),
			  UCataclysmHealthDebt::DrainIfDue(
				  Caster.Actor, UCataclysmHealthDebt::DrainSeconds),
			  200.0f, 0.5f);
	TestEqual(TEXT("and owing nothing again is worth nothing again"),
			  Caster.AbilitySystem->AttackDamageIncreasesForSkill(NoTags),
			  0.0f, 0.0001f);

	// AND THE CHARACTER IS NOW A FIFTH DOWN ON HEALTH AND STILL GETS NOTHING,
	// which is the other half of telling the two readings apart: the state that
	// WOULD pay a missing-health bonus pays this one nothing at all.
	TestEqual(TEXT("the health went instead"), Caster.Health(), 800.0f, 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRollingDebtReachesTheCostTest,
	"Cataclysm.Skills.PayingAHealthCostPushesAnOutstandingDebtOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRollingDebtReachesTheCostTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	// THE MASOCHIST'S ROLLING DEBT NODE, WIRED INTO THE PLACE A COST IS PAID.
	// Issue #995. `Cataclysm.HealthDebt.*` proves the rule itself; this proves
	// that `UCataclysmSkillTemplate::PayHealthCost` calls it, and that it calls
	// it BEFORE this cast's own deferral is added.
	//
	// THE ORDER IS THE WHOLE POINT OF THE SECOND HALF. Called after the
	// deferral, the FIRST cast of a fight would find its own debt outstanding
	// and extend itself, which is a different node.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	Caster.Set(Vital::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(Vital::GetHealthAttribute(), 1'000.0f);

	// A COST OF FIFTY, ALL DEFERRED, AND THE NODE AT ITS FULL SIX POINTS.
	Caster.Set(Resource::GetAddedHealthCostAttribute(), 5.0f);
	Caster.Set(Resource::GetDeferredHealthCostShareAttribute(), 100.0f);
	Caster.Set(Resource::GetHealthDebtDelayExtensionAttribute(), 3.0f);

	// TWO SKILLS IN TWO SLOTS, BECAUSE A SKILL CANNOT BE CAST TWICE IN ONE
	// TEST: activating commits a cooldown and the second attempt is refused.
	UCataclysmStrikeSkill* First = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3"), TEXT("Strike"));
	UCataclysmStrikeSkill* Second = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Special, TEXT("Radius=3"),
		TEXT("Second strike"));
	if (!First || !Second)
	{
		AddError(TEXT("Could not grant both skills."));
		return false;
	}

	TestTrue(TEXT("the first activates"), Activate(Caster, First));
	TestEqual(TEXT("fifty is owed"),
			  Caster.Get(Resource::GetHealthOwedAttribute()), 50.0f, 0.5f);

	// THE FIRST CAST EXTENDED NOTHING, because nothing was owed when it paid.
	// This is what the ordering guards.
	TestEqual(TEXT("the first cast pushed nothing out"),
			  Caster.AbilitySystem->HealthDebtExtensionApplied(), 0.0f, 0.001f);

	// A SECOND CAST, TWO SECONDS IN, WHILE THE FIRST DEBT IS STILL OUTSTANDING.
	World->TimeSeconds += 2.0f;
	TestTrue(TEXT("the second activates"), Activate(Caster, Second));

	TestEqual(TEXT("the debt accumulated"),
			  Caster.Get(Resource::GetHealthOwedAttribute()), 100.0f, 0.5f);
	TestEqual(TEXT("and the second cast pushed the debt out by three seconds"),
			  Caster.AbilitySystem->HealthDebtExtensionApplied(), 3.0f, 0.001f);

	// SO PAST THE ORDINARY DELAY NOTHING IS TAKEN.
	World->TimeSeconds += 1.5f;
	TestEqual(TEXT("past the ordinary delay the debt is still not due"),
			  UCataclysmHealthDebt::DrainIfDue(
				  Caster.Actor, UCataclysmHealthDebt::DrainSeconds),
			  0.0f, 0.001f);
	TestEqual(TEXT("and the health is untouched"), Caster.Health(), 1'000.0f,
			  0.5f);

	// AND IT FALLS DUE AT SIX SECONDS.
	World->TimeSeconds += 2.6f;
	TestEqual(TEXT("once the extended delay passes the whole debt is taken"),
			  UCataclysmHealthDebt::DrainIfDue(
				  Caster.Actor, UCataclysmHealthDebt::DrainSeconds),
			  100.0f, 0.5f);
	TestEqual(TEXT("and the health goes with it"), Caster.Health(), 900.0f,
			  0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMomentumReachesTheCostTest,
	"Cataclysm.Skills.PayingAHealthCostSoonAfterTheLastBuildsAStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMomentumReachesTheCostTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	// THE MASOCHIST'S SANGUINE MOMENTUM NODE, WIRED INTO THE PLACE A COST IS
	// PAID. Issue #1002. `Cataclysm.Stacks.*` proves the chain rule itself; this
	// proves that `UCataclysmSkillTemplate::PayHealthCost` runs it, and that it
	// runs it BEFORE the component's own timestamp is moved to now.
	//
	// THE ORDER IS THE WHOLE POINT OF THE FIRST ASSERTION. Run afterwards, every
	// payment would be nought seconds after itself, and the FIRST health cost of
	// a fight would grant a stack -- which is a different node.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	Caster.Set(Vital::GetMaxHealthAttribute(), 10'000.0f);
	Caster.Set(Vital::GetHealthAttribute(), 10'000.0f);

	// A COST OF ONE PER CENT OF MAXIMUM HEALTH, so the casts below cannot kill
	// the caster and nothing else about them can change what is measured.
	Caster.Set(Resource::GetAddedHealthCostAttribute(), 1.0f);

	// TWO SKILLS IN TWO SLOTS, BECAUSE A SKILL CANNOT BE CAST TWICE IN ONE
	// TEST: activating commits a cooldown and the second attempt is refused.
	UCataclysmStrikeSkill* First = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3"), TEXT("Strike"));
	UCataclysmStrikeSkill* Second = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Special, TEXT("Radius=3"),
		TEXT("Second strike"));
	if (!First || !Second)
	{
		AddError(TEXT("Could not grant both skills."));
		return false;
	}

	TestTrue(TEXT("the first activates"), Activate(Caster, First));
	TestEqual(TEXT("the first cost of a fight builds no stack"),
			  UCataclysmStacks::Held(Caster.AbilitySystem,
									 ECataclysmStackKind::SanguineMomentum), 0);

	// A SECOND CAST TWO SECONDS LATER IS INSIDE THE THREE-SECOND CHAIN.
	World->TimeSeconds += 2.0f;
	TestTrue(TEXT("the second activates"), Activate(Caster, Second));
	TestEqual(TEXT("a cost two seconds after the last builds one"),
			  UCataclysmStacks::Held(Caster.AbilitySystem,
									 ECataclysmStackKind::SanguineMomentum), 1);

	// AND THE STATE THE PIPELINE IS HANDED CARRIES IT, which is the join
	// between the cast that happened and the bonus that reads it.
	TestEqual(TEXT("and the pipeline is told about it"),
			  Caster.AbilitySystem->CurrentConditions().SanguineMomentumStacks,
			  1);

	// AND IT LAPSES THREE SECONDS AFTER THAT CAST, with nothing else happening.
	World->TimeSeconds += UCataclysmStacks::WindowSecondsFor(
		ECataclysmStackKind::SanguineMomentum) + 0.1f;
	TestEqual(TEXT("three seconds later the chain is broken and it is gone"),
			  UCataclysmStacks::Held(Caster.AbilitySystem,
									 ECataclysmStackKind::SanguineMomentum), 0);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackCostsNoHealthTest,
	"Cataclysm.Skills.TheBasicAttackPaysNoHealthCostBecauseItIsAutomatic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackCostsNoHealthTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// WHAT WENT WRONG. Issue #1110. `PayHealthCost` charged every skill
	// activation and had no slot check, and the basic attack is a skill in a
	// slot like any other. It is also AUTOMATIC: `ACataclysmPlayerCharacter`
	// swings it at the weapon's attack speed whenever an enemy is in reach, and
	// the design says "The basic attack is on no key... Nothing the player
	// presses triggers it."
	//
	// So a Masochist holding Exsanguinate, which charges 15% of CURRENT health a
	// skill, paid that on every automatic swing. The project owner reported on
	// 2026-08-31: "I used my teleport a few times, then pressed e once, and
	// instantly died. Nothing had hit me." At a Fist's 1.45 swings a second the
	// automatic attack alone emptied a full health bar in about six seconds.
	//
	// TWO CASTERS AND NOT ONE, because a skill commits a cooldown when it is
	// used, so a second activation on the same caster is refused and the failure
	// names the activation rather than the health.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Automatic(World, FVector::ZeroVector);
	FScopedFighter Pressed(World, FVector(4 * M, 0, 0));

	// EXSANGUINATE, ON BOTH. 15% of current health per skill, which is the
	// keystone's own figure.
	for (const FScopedFighter* Who : {&Automatic, &Pressed})
	{
		Who->AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
		Who->AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000.0f);
		Who->AbilitySystem->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetAddedHealthCostOfCurrentAttribute(),
			15.0f);
	}

	UCataclysmProjectileSkill* Swing = GrantSkill<UCataclysmProjectileSkill>(
		Automatic, ECataclysmAbilitySlot::BasicAttack,
		TEXT("Radius=3; Speed=0"), TEXT("The automatic basic attack"));
	UCataclysmProjectileSkill* Button = GrantSkill<UCataclysmProjectileSkill>(
		Pressed, ECataclysmAbilitySlot::Special,
		TEXT("Radius=3; Speed=0"), TEXT("A skill on a key"));
	if (!TestNotNull(TEXT("a basic attack was granted"), Swing)
		|| !TestNotNull(TEXT("and a skill on a key"), Button))
	{
		return false;
	}

	// THE CONTROL COMES FIRST, because without it this test would pass just as
	// well if the health cost machinery had stopped working altogether. The same
	// keystone on the same pool, in a slot the player presses, still charges its
	// 15% of current health.
	TestTrue(TEXT("the skill on a key activates"), Activate(Pressed, Button));
	TestEqual(TEXT("and it charged fifteen per cent of current health"),
			  Pressed.Health(), 850.0f, 0.01f);

	// AND THE BASIC ATTACK CHARGES NOTHING AT ALL.
	TestTrue(TEXT("the basic attack activates"), Activate(Automatic, Swing));
	TestEqual(TEXT("and the caster's health did not move"),
			  Automatic.Health(), 1'000.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRockBottomCostTest,
	"Cataclysm.Skills.RockBottomLeavesOneHealthAndOwesTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRockBottomCostTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// THE FIRST SENTENCE OF ROCK BOTTOM. Issue #1069: "A health cost can never
	// reduce you below 1 health; anything you cannot pay becomes health debt
	// instead."
	//
	// THE SAME KIND OF COST THE TEST ABOVE USES, deliberately. That one shows
	// what happens without the option -- the character is emptied -- and this
	// shows the option changing exactly that outcome. Two tests on one shape of
	// cost is what says the option is doing the work.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Close(World, FVector(2 * M, 0, 0));

	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100.0f);

	// ONE AND A HALF TIMES THE WHOLE POOL, measured against MAXIMUM health,
	// which is the half of a cost the design allows to kill.
	Caster.Set(
		UCataclysmClassResourceAttributeSet::GetAddedHealthCostAttribute(),
		150.0f);
	Caster.Set(
		UCataclysmClassResourceAttributeSet
			::GetUnpayableHealthCostBecomesDebtAttribute(),
		1.0f);

	UCataclysmProjectileSkill* Plain = GrantSkill<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Radius=3; Speed=0"), TEXT("A skill with no health cost"));
	if (!Plain)
	{
		AddError(TEXT("Could not grant the skill."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Plain));

	// NINETY-NINE OF THE HUNDRED AND FIFTY IS PAID, which is everything above
	// the floor.
	TestEqual(TEXT("the caster is left on exactly one health"),
		Caster.Health(), 1.0f, 0.01f);

	// AND THE FIFTY-ONE IT COULD NOT PAY IS OWED. Nothing about the cost is
	// forgiven: the character still owes every point of it, which is what
	// separates this option from a discount.
	const float Owed = Caster.AbilitySystem->GetNumericAttribute(
		UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute());
	TestEqual(TEXT("and owes the fifty-one it could not pay"), Owed, 51.0f,
		0.01f);

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

	// THE KEY COMES UP BEFORE IT GOES DOWN AGAIN, WHICH THIS TEST USED TO SKIP.
	// Issue #1114. A second press in the running game is always preceded by a
	// release, and the aura now requires one: without it, a "press" is another
	// frame of the press that started the aura, because
	// `ACataclysmPlayerController::Input_AbilitySlotPressed` fires every frame
	// the key is held. Leaving the release out made this test describe an input
	// sequence a player cannot produce.
	Caster.AbilitySystem->AbilitySpecInputReleased(*Spec);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraHeldKeyDoesNotRestartItTest,
	"Cataclysm.Skills.HoldingTheAuraKeyDoesNotRestartItOrChargeAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAuraHeldKeyDoesNotRestartItTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	// WHAT WENT WRONG. Issue #1114.
	// `ACataclysmPlayerController::Input_AbilitySlotPressed` is bound to
	// `ETriggerEvent::Triggered`, which fires EVERY FRAME the key is held. On a
	// slot with a cooldown that repeats the cast, which issue #1016's comment
	// says is wanted. The aura slot has NO cooldown, so holding the key switched
	// the aura off on one frame and
	// `UCataclysmAbilitySystemComponent::ProcessAbilityInput` started it again
	// on the next -- and every start pays a full health cost.
	//
	// The project owner pressed the key once on 2026-08-31. The log recorded
	// five costs of about 253 health inside 117 milliseconds, and it killed
	// them.
	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// EXSANGUINATE, SO EVERY ACTIVATION COSTS SOMETHING VISIBLE. 15% of current
	// health a skill, on a round pool. Without a cost this test could only see
	// that the aura stayed on, and the health is the part that killed somebody.
	Caster.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000.0f);
	Caster.Set(
		UCataclysmClassResourceAttributeSet::GetAddedHealthCostOfCurrentAttribute(),
		15.0f);

	UCataclysmAuraSkill* Aura = GrantSkill<UCataclysmAuraSkill>(
		Caster, ECataclysmAbilitySlot::Aura, TEXT("Radius=10; Interval=1"),
		TEXT("Conflagration"));
	if (!Aura)
	{
		AddError(TEXT("Could not grant the aura."));
		return false;
	}

	TestTrue(TEXT("the key goes down and the aura comes on"),
			 Activate(Caster, Aura));
	TestTrue(TEXT("and it is held"), Aura->IsHeld());

	// ONE ACTIVATION, ONE COST. This is the baseline the repeats below are
	// measured against.
	const float AfterOnePress = Caster.Health();
	TestEqual(TEXT("one activation charged fifteen per cent"), AfterOnePress,
			  850.0f, 0.01f);

	FGameplayAbilitySpec* Spec = Caster.AbilitySystem->FindAbilitySpecFromHandle(
		Aura->GetCurrentAbilitySpecHandle());
	if (!Spec)
	{
		AddError(TEXT("The aura's spec disappeared."));
		return false;
	}

	// TEN MORE FRAMES OF THE SAME PRESS, WITH NO RELEASE BETWEEN THEM. This is
	// what holding the key looks like from the ability's side. Ten rather than
	// two, because the fault compounded: five frames was enough to kill a
	// character with 1,449 health left.
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Caster.AbilitySystem->AbilitySpecInputPressed(*Spec);
	}

	TestTrue(TEXT("the aura is still on after ten held frames"), Aura->IsHeld());
	TestEqual(TEXT("and not one of them charged anything"), Caster.Health(),
			  AfterOnePress, 0.01f);

	// AND THE TOGGLE STILL WORKS, which is the half that stops this being a fix
	// that simply disables the feature. Issue #36 requires the aura to switch
	// off on a second press.
	Caster.AbilitySystem->AbilitySpecInputReleased(*Spec);
	Caster.AbilitySystem->AbilitySpecInputPressed(*Spec);

	TestFalse(TEXT("a press after a release switches it off"), Aura->IsHeld());
	TestFalse(TEXT("and not for lack of mana"), Aura->bEndedForLackOfMana);

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
		TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning"), TEXT("Burning Wrath"),
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

// --------------------------------------------------------------------------
// THE BUFF'S NUMBER MULTIPLIES ON ITS OWN, and this is the only test that can
// tell. Until 2026-09-01 Burning Wrath wrote its number into the additive
// bucket, where it was summed with every gear affix the character wore and the
// total multiplied in once. It now writes the multiplicative bucket, because
// "4% more fire damage" is section VI's wording for a multiplier that applies
// separately from that sum.
//
// A SINGLE MODIFIER ON A STAT CANNOT SHOW WHICH BUCKET IT IS IN. A lone 4%
// gives 1.04x from either one, so the five tests around this one hold equally
// under both spellings and not one of them would notice the bucket going back.
// A second modifier is what separates them -- here an ordinary 50% increase of
// the kind a gear affix rolls:
//
//     additive, what it used to do:   2.50 x (1 + (50 + 4)/100)  = 3.85
//     multiplicative, what it does:   2.50 x (1 + 50/100) x 1.04 = 3.90
//
// So the damage assertion below moves by 5% of weapon damage if the bucket is
// ever changed back, which neither a count of modifiers nor a reading of
// GrantedIncrease would do.
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBuffGrantsMoreNotIncreasedTest,
	"Cataclysm.Skills.ABuffsNumberMultipliesRatherThanJoiningTheSumOfIncreases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBuffGrantsMoreNotIncreasedTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// One burning enemy inside the fifteen metre radius, so the buff's number
	// is a single 4 rather than a multiple of it.
	FScopedFighter Alight(World, FVector(3 * M, 0, 0));
	UCataclysmSkillEffects::ApplyBurn(Caster.Actor, Alight.Actor, 100.0f);

	// THE SECOND MODIFIER, which is the whole point of this test. It is scoped
	// to the same element the buff scopes to, so both reach the same strike.
	FCataclysmStatModifier Affix;
	Affix.Bucket = ECataclysmStatBucket::Increased;
	Affix.Source = ECataclysmModifierSource::GearAffix;
	Affix.Value = 50.0f;
	Affix.RequiredTags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Element.Demonic")), /*ErrorIfNotFound=*/false));
	Caster.AbilitySystem->AddStatModifier(Affix);

	UCataclysmSelfBuffSkill* Buff = GrantSkill<UCataclysmSelfBuffSkill>(
		Caster, ECataclysmAbilitySlot::Support,
		TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning"),
		TEXT("Burning Wrath"), TEXT("Element.Demonic"));
	if (!Buff)
	{
		AddError(TEXT("Could not grant the buff."));
		return false;
	}

	TestTrue(TEXT("The buff activates"), Activate(Caster, Buff));
	TestEqual(TEXT("It granted 4% for the one burning enemy"),
		Buff->GrantedIncrease, 4.0f);

	// Read the bucket directly as well as through the damage, so that a failure
	// says which of the two things went wrong rather than only that a number
	// moved.
	const TArray<FCataclysmStatModifier>& Modifiers =
		Caster.AbilitySystem->GetStatModifiers();
	TestEqual(TEXT("The affix and the buff are both on the caster"),
		Modifiers.Num(), 2);

	const FCataclysmStatModifier* FromBuff = Modifiers.FindByPredicate(
		[](const FCataclysmStatModifier& Modifier)
		{ return Modifier.Source == ECataclysmModifierSource::SkillBuff; });
	if (!FromBuff)
	{
		AddError(TEXT("The buff put no modifier of its own on the caster."));
		return false;
	}

	TestEqual(TEXT("The buff's modifier is in the multiplicative bucket"),
		static_cast<int32>(FromBuff->Bucket),
		static_cast<int32>(ECataclysmStatBucket::More));
	TestEqual(TEXT("And it carries the 4% it reported granting"),
		FromBuff->Value, 4.0f);

	FScopedFighter Target(World, FVector(2 * M, 0, 0));
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"),
		TEXT("Molten Cleave"), TEXT("Element.Demonic"));

	const float Before = Target.Health();
	TestTrue(TEXT("The strike activates"), Activate(Caster, Strike));

	// The Heavy slot is 250% of weapon damage, the affix's 50% applies once,
	// and the buff's 4% multiplies after it. Were the buff additive the second
	// bracket would be 1.54 and this would read 385 rather than 390.
	const float Unbuffed = WeaponDamage * 250.0f / 100.0f;
	TestEqual(TEXT("The increase applied once and the buff multiplied after it"),
		Before - Target.Health(), Unbuffed * 1.5f * 1.04f);

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
		TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning"), TEXT("Burning Wrath"),
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
		TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning"), TEXT("Burning Wrath"),
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
		TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning"), TEXT("Burning Wrath"),
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
		TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning"), TEXT("Burning Wrath"),
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

// --------------------------------------------------------------------------
// The Last Drop: the first option of The Final Vow. Issue #1051.
//
// "While below 20% health your skills cost no health, and every skill you cast
// grants 10 Fervour."
// --------------------------------------------------------------------------

namespace CataclysmSkillTest
{
	/**
	 * The Last Drop as the passive tree delivers it: two flat rows under one
	 * condition, which is the first STRICTLY-below health threshold in the game.
	 *
	 * BUILT AS `UCataclysmPlayerClassStats::ApplyTo` LEAVES IT rather than
	 * written onto the two gameplay attributes. Both rows carry a condition, so
	 * `ApplyTo` refuses them and both attributes stay at zero for a character
	 * holding the option. A test that wrote the attributes would be testing a
	 * state the game never produces.
	 */
	void GiveTheLastDrop(FScopedFighter& Who, float FervourPerCast = 10.0f)
	{
		const auto BelowTwenty = [](float Value)
		{
			FCataclysmStatModifier Modifier;
			Modifier.Bucket = ECataclysmStatBucket::Flat;
			Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
			Modifier.Value = Value;
			Modifier.Condition = ECataclysmStatCondition::HealthBelowPercent;
			Modifier.ConditionValue = 20.0f;
			return Modifier;
		};

		FCataclysmStatInputs Suppressed;
		Suppressed.Modifiers.Add(BelowTwenty(1.0f));

		FCataclysmStatInputs PerCast;
		PerCast.Modifiers.Add(BelowTwenty(FervourPerCast));

		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.Add(FName(UCataclysmSkillTemplate::HealthCostSuppressedStat),
				  Suppressed);
		Stats.Add(FName(UCataclysmFervour::PerCastStat), PerCast);
		Who.AbilitySystem->SetStatInputs(MoveTemp(Stats));
	}

	/**
	 * A caster with a real health cost, standing at a stated share of health.
	 *
	 * A SHARE OF MAXIMUM HEALTH AND NOT OF CURRENT, so the cost is the same
	 * absolute number wherever the character is standing. Two of the casters
	 * below sit at 19% and 20%, and a share of current health would make them
	 * pay different amounts for a reason unrelated to what is being checked.
	 */
	void StandAtShareOfHealthWithACost(FScopedFighter& Who, float SharePercent)
	{
		Who.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
		Who.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(),
				1'000.0f * SharePercent / 100.0f);
		Who.Set(UCataclysmClassResourceAttributeSet::GetAddedHealthCostAttribute(),
				10.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTheLastDropSuppressesTheCostTest,
	"Cataclysm.Skills.TheLastDropMakesASkillCostNoHealthBelowTheThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheLastDropSuppressesTheCostTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THREE CASTERS AND NOT ONE, because a skill cannot be cast twice in a test:
	// casting commits its cooldown, so a second activation is refused and the
	// failure names the activation rather than what was being measured.
	FScopedFighter Below(World, FVector::ZeroVector);
	FScopedFighter Exactly(World, FVector(4 * M, 0, 0));
	FScopedFighter Without(World, FVector(8 * M, 0, 0));

	// TEN PER CENT OF A THOUSAND MAXIMUM HEALTH IS A COST OF A HUNDRED. It was
	// the Masochist's Deeper Cuts node at its full ten points until issue #1107
	// lowered that node to 0.25% a point; Exsanguinate's 15% of current health
	// is what reaches this share now, for a character above two thirds of its
	// pool.
	StandAtShareOfHealthWithACost(Below, 19.0f);
	StandAtShareOfHealthWithACost(Exactly, 20.0f);
	StandAtShareOfHealthWithACost(Without, 19.0f);

	GiveTheLastDrop(Below);
	GiveTheLastDrop(Exactly);
	// `Without` deliberately gets nothing.

	const auto CastOnce = [&](FScopedFighter& Who, const TCHAR* Name) -> bool
	{
		UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
			Who, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3"), Name);
		return Strike && Activate(Who, Strike);
	};

	if (!TestTrue(TEXT("all three casters activate"),
				  CastOnce(Below, TEXT("Below")) && CastOnce(Exactly, TEXT("Exactly"))
					  && CastOnce(Without, TEXT("Without"))))
	{
		return false;
	}

	// BELOW A FIFTH OF ITS HEALTH, THE SKILL COSTS NOTHING.
	TestEqual(TEXT("a caster below 20% health with the option pays nothing"),
			  Below.Health(), 190.0f, 0.01f);

	// AT EXACTLY A FIFTH, IT PAYS IN FULL. This is the whole reason
	// `health_below` exists beside `health_at_or_below`: the node says "below",
	// and a character sitting on the number is not below it. Delivering the
	// option with the other predicate would make this caster pay nothing too,
	// and nothing at run time would report it.
	TestEqual(TEXT("and one at exactly 20% health pays the whole cost"),
			  Exactly.Health(), 100.0f, 0.01f);

	// AND A CASTER WITHOUT THE OPTION PAYS WHEREVER IT IS STANDING, which is
	// what makes the first reading evidence of the option rather than of the
	// cost being broken.
	TestEqual(TEXT("and one below 20% health without the option pays in full"),
			  Without.Health(), 90.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTheLastDropGrantsFervourTest,
	"Cataclysm.Skills.TheLastDropGrantsFervourForASkillThatCostNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTheLastDropGrantsFervourTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Below(World, FVector::ZeroVector);
	FScopedFighter Without(World, FVector(4 * M, 0, 0));
	FScopedFighter Neither(World, FVector(8 * M, 0, 0));

	StandAtShareOfHealthWithACost(Below, 19.0f);
	StandAtShareOfHealthWithACost(Without, 19.0f);
	StandAtShareOfHealthWithACost(Neither, 19.0f);
	GiveTheLastDrop(Below);

	// A RATE THAT FILLS FERVOUR FROM A COST, ON TWO OF THE THREE. This is the
	// discriminating half. `Below` pays nothing, so this rate gives it nothing
	// and every point of Fervour it ends with came from the cast itself;
	// `Without` pays in full, so this is where its Fervour comes from. `Neither`
	// has no rate and no option, so it must end with none at all.
	Below.Set(UCataclysmClassResourceAttributeSet::GetFervourFromCostAttribute(),
			  1.0f);
	Without.Set(
		UCataclysmClassResourceAttributeSet::GetFervourFromCostAttribute(), 1.0f);

	const auto CastOnce = [&](FScopedFighter& Who, const TCHAR* Name) -> bool
	{
		UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
			Who, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3"), Name);
		return Strike && Activate(Who, Strike);
	};

	if (!TestTrue(TEXT("all three casters activate"),
				  CastOnce(Below, TEXT("Below"))
					  && CastOnce(Without, TEXT("Without"))
					  && CastOnce(Neither, TEXT("Neither"))))
	{
		return false;
	}

	// TEN FERVOUR FOR THE CAST, AND NONE FOR THE COST, because there was no
	// cost. The grant sits OUTSIDE the branch guarded on the cost being above
	// zero in `PayHealthCost`, and it has to: this option's other clause makes
	// the cost zero, so a grant inside that branch would never fire for the one
	// character that has the option.
	TestEqual(TEXT("a caster with the option gains ten Fervour for the cast"),
			  Below.Fervour(), 10.0f, 0.01f);

	// AND ITS HEALTH DID NOT MOVE, which is what says those ten came from the
	// cast rather than from a cost that was quietly paid.
	TestEqual(TEXT("and paid no health for it"), Below.Health(), 190.0f, 0.01f);

	// A CASTER WITHOUT THE OPTION PAYS HEALTH, so its Fervour came the ordinary
	// way. A hundred health is 10% of its maximum and the rate is one per
	// percent, so it also ends on ten -- which is why the health readings rather
	// than the Fervour readings are what tell the two rules apart here.
	TestEqual(TEXT("and one without it pays health for its Fervour"),
			  Without.Health(), 90.0f, 0.01f);

	// AND ONE WITH NEITHER GAINS NOTHING, which is what proves the first caster
	// did not simply gain Fervour for casting regardless of the option.
	TestEqual(TEXT("a caster with neither the option nor a rate gains nothing"),
			  Neither.Fervour(), 0.0f, 0.001f);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillWaterToBloodCostTest,
	"Cataclysm.Skills.WaterToBloodPaysASkillsCostOutOfHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The last clause of the Masochist's Water to Blood. Issue #1067.
 *
 * "...and every ability costs health instead of mana."
 *
 * THE OTHER THREE CLAUSES ARE A STAT LINE and are covered by
 * `Cataclysm.Passives.WaterToBloodTradesTheManaPoolForHealthOnARealCharacter`.
 * This one is a property of an ACTIVATION, so it needs a granted skill and a
 * caster that really casts it.
 *
 * THE SAME NUMBER OUT OF A DIFFERENT POOL. The option converts the pool, not the
 * price, so the health taken is the skill's own mana cost.
 *
 * THE FLAG IS SET AS A STAT INPUT AND NOT ON THE ATTRIBUTE, because
 * `UCataclysmSkillTemplate::ManaPoolBecomesHealth` asks through `StatForSkill`
 * with a fallback of zero rather than reading the attribute. Writing the
 * attribute would leave the answer at zero and this test would pass while
 * measuring nothing. That is how `UCataclysmPlayerClassStats::ApplyTo` really
 * leaves it: the row is in `StatToAttribute`, so its inputs are recorded.
 */
bool FCataclysmSkillWaterToBloodCostTest::RunTest(const FString&)
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
	if (!TestTrue(FString::Printf(TEXT("The Heavy slot costs something (%.1f)"),
								  Cost),
				  Cost > 0.0f))
	{
		return false;
	}

	// THE OPTION, AS THE PASSIVE TREE DELIVERS IT: a flat 1 on the flag, with no
	// condition. See the header for why it is a stat input rather than an
	// attribute.
	FCataclysmStatModifier Traded;
	Traded.Bucket = ECataclysmStatBucket::Flat;
	Traded.Source = ECataclysmModifierSource::PassiveKeystone;
	Traded.Value = 1.0f;

	FCataclysmStatInputs Inputs;
	Inputs.Base = 0.0f;
	Inputs.Modifiers.Add(Traded);

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(UCataclysmSkillTemplate::ManaPoolBecomesHealthStat), Inputs);
	Caster.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	// THE MANA IS STILL THERE IN THIS TEST, deliberately. The stat line is what
	// empties it, and this test does not build one; leaving it full is what says
	// the cost came out of health because of the flag rather than because there
	// was no mana to take.
	const float ManaBefore = Caster.Mana();
	if (!TestTrue(TEXT("the caster still has mana to spend"), ManaBefore > Cost))
	{
		return false;
	}

	const float HealthBefore =
		Caster.AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute());

	TestTrue(TEXT("The strike activates"), Activate(Caster, Strike));

	// THE HEADLINE. The cost came out of health.
	TestEqual(TEXT("the health was spent"),
			  Caster.AbilitySystem->GetNumericAttribute(
				  UCataclysmVitalAttributeSet::GetHealthAttribute()),
			  HealthBefore - Cost, 0.01f);

	// AND THE MANA WAS NOT TOUCHED, which is the half that says the cost moved
	// rather than being taken twice.
	TestEqual(TEXT("and the mana was left alone"), Caster.Mana(), ManaBefore,
			  0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillWaterToBloodRefusalTest,
	"Cataclysm.Skills.WaterToBloodRefusesASkillThatWouldEmptyTheCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A skill paid for in health must not be able to kill the caster. Issue #1067.
 *
 * WHY IT IS ASKED AS STRICTLY MORE THAN, WHERE MANA ASKS FOR AT LEAST. A cost
 * that took a character to exactly zero health would kill it, and no skill
 * should be able to do that by being cast. The design gives its own health costs
 * their own floor rules, and Rock Bottom -- the first option of the next
 * capstone, which is not built -- is the node that says what happens at the
 * bottom. Until then the cast is refused rather than fatal.
 */
bool FCataclysmSkillWaterToBloodRefusalTest::RunTest(const FString&)
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

	FCataclysmStatModifier Traded;
	Traded.Bucket = ECataclysmStatBucket::Flat;
	Traded.Source = ECataclysmModifierSource::PassiveKeystone;
	Traded.Value = 1.0f;

	FCataclysmStatInputs Inputs;
	Inputs.Base = 0.0f;
	Inputs.Modifiers.Add(Traded);

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(UCataclysmSkillTemplate::ManaPoolBecomesHealthStat), Inputs);
	Caster.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	// EXACTLY THE COST, WHICH WOULD LEAVE NOTHING. The mana pool is left full so
	// that a refusal cannot be blamed on it.
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), Cost);

	const float HealthBefore =
		Caster.AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float EnemyBefore = Enemy.Health();

	TestFalse(TEXT("a skill that would take the last of the health is refused"),
			  Activate(Caster, Strike));
	TestEqual(TEXT("and it takes nothing"),
			  Caster.AbilitySystem->GetNumericAttribute(
				  UCataclysmVitalAttributeSet::GetHealthAttribute()),
			  HealthBefore, 0.01f);
	TestEqual(TEXT("and hits nothing"), Enemy.Health(), EnemyBefore, 0.01f);

	// AND ONE POINT MORE IS ENOUGH, which is what says the boundary is where the
	// comment says it is rather than somewhere nearby.
	Caster.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), Cost + 1.0f);
	TestTrue(TEXT("with one point of health more it casts"),
			 Activate(Caster, Strike));

	return true;
}

// --------------------------------------------------------------------------
// Consuming burn, the conditions on a cast, and a skill's own damage scaling.
// Issue #37, for the Sword and Fist sets.
//
// WHAT THESE COVER AND WHAT THEY DO NOT. Each drives a skill in a world that is
// never ticked, so what a burn does over time is not covered here -- that is in
// CataclysmDamageOverTimeTests. What is covered is whether the fire is present
// or absent once the skill has finished with it, where a conditional move
// arrives, and what the blow was worth.
// --------------------------------------------------------------------------

namespace CataclysmSkillTest
{
	/** Put a target alight, the way any damaging skill would. */
	void SetAlight(FScopedFighter& By, FScopedFighter& Target)
	{
		UCataclysmSkillEffects::ApplyBurn(By.Actor, Target.Actor, WeaponDamage);
	}

	/** Whether this target carries the burn tag right now. */
	bool IsAlight(const FScopedFighter& Target)
	{
		return UCataclysmSkillEffects::HasTag(Target.Actor,
											  UCataclysmSkillEffects::BurnTag());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumeBurnTest,
	"Cataclysm.Skills.AStrikeThatConsumesBurnPutsTheFireOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumeBurnTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Alight(World, FVector(2 * M, 0, 0));
	FScopedFighter Cold(World, FVector(3 * M, 0, 0));

	SetAlight(Caster, Alight);
	TestTrue(TEXT("The enemy starts alight"), IsAlight(Alight));
	TestFalse(TEXT("The other does not"), IsAlight(Cold));

	// NO `Burn` ON THE SKILL, DELIBERATELY. Every Sword row states both, and a
	// skill that consumes and then relights leaves the tag exactly where it was,
	// so this test could not tell consuming from doing nothing. The relighting
	// is the next test's subject.
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=6; Angle=360; ConsumeBurn=1"), TEXT("Touch Off"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const float ColdBefore = Cold.Health();
	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	TestFalse(TEXT("The fire on the burning enemy was put out"), IsAlight(Alight));
	TestFalse(TEXT("And the enemy that was never alight is still not"),
		IsAlight(Cold));
	TestTrue(TEXT("Both were still hit"), Cold.Health() < ColdBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumeLeftAloneTest,
	"Cataclysm.Skills.AStrikeThatDoesNotConsumeLeavesTheFireAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumeLeftAloneTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Alight(World, FVector(2 * M, 0, 0));

	SetAlight(Caster, Alight);

	// THE CONTROL FOR THE TEST ABOVE, and the same parameters without
	// `ConsumeBurn`. Without this, that test would pass just as well against a
	// burn that had expired or never applied.
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=6; Angle=360"),
		TEXT("Molten Cleave"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	TestTrue(TEXT("It activates"), Activate(Caster, Strike));
	TestTrue(TEXT("The enemy is still alight"), IsAlight(Alight));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumeThenRelightTest,
	"Cataclysm.Skills.ConsumingHappensBeforeTheBlowThatSetsAlightAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumeThenRelightTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(2 * M, 0, 0));

	SetAlight(Caster, Enemy);

	// QUENCH'S OWN SENTENCE: "any enemy already alight has their fire consumed
	// ... and the whole arc is set alight anew behind the blade". Both halves
	// have to be true at once, and the order is what makes them so.
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=6; Angle=360; Burn=1; ConsumeBurn=1; MoreDamagePer=50; "
			 "ScalingSource=Consume"),
		TEXT("Quench"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const float Before = Enemy.Health();
	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	TestTrue(TEXT("The enemy is alight again after the blow"), IsAlight(Enemy));

	// AND THE FIRE REALLY WAS SPENT ON THE WAY THROUGH, which the tag alone
	// cannot show. The blow is worth 50% more only to an enemy whose burn was
	// consumed, so the damage is the evidence that consuming happened first.
	const float Plain = WeaponDamage * 250.0f / 100.0f;
	TestEqual(TEXT("And it was struck as a consumed enemy, for 50% more"),
		Before - Enemy.Health(), Plain * 1.5f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumeScalesPerTargetTest,
	"Cataclysm.Skills.QuenchHitsAConsumedEnemyHarderThanOneThatWasNotAlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumeScalesPerTargetTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Alight(World, FVector(2 * M, 0, 0));
	FScopedFighter Cold(World, FVector(3 * M, 0, 0));

	SetAlight(Caster, Alight);

	// `ScalingSource=Consume` IS THE ONE SOURCE ASKED OF EACH TARGET rather than
	// of the whole use, so one swing deals two different figures.
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=6; Angle=360; Burn=1; ConsumeBurn=1; MoreDamagePer=50; "
			 "ScalingSource=Consume"),
		TEXT("Quench"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const float AlightBefore = Alight.Health();
	const float ColdBefore = Cold.Health();

	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	// THE HEAVY SLOT IS 250% OF WEAPON DAMAGE, so the enemy that was never
	// alight takes exactly that and the one whose fire was spent takes half as
	// much again.
	const float Plain = WeaponDamage * 250.0f / 100.0f;
	TestEqual(TEXT("The enemy that was not alight took the plain blow"),
		ColdBefore - Cold.Health(), Plain, 0.01f);
	TestEqual(TEXT("The one whose fire was consumed took 50% more"),
		AlightBefore - Alight.Health(), Plain * 1.5f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumedCountScalesTest,
	"Cataclysm.Skills.ExtinctionRisesWithEveryOtherFirePutOutAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumedCountScalesTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// TWO CASTERS AND NOT ONE CAST TWICE. A cast commits a cooldown, so a second
	// activation on one caster is refused and the test would be measuring the
	// refusal rather than the scaling.
	FScopedFighter Alone(World, FVector::ZeroVector);
	FScopedFighter OneFire(World, FVector(2 * M, 0, 0));

	FScopedFighter Crowded(World, FVector(0, 40 * M, 0));
	FScopedFighter FireA(World, FVector(2 * M, 40 * M, 0));
	FScopedFighter FireB(World, FVector(3 * M, 40 * M, 0));
	FScopedFighter FireC(World, FVector(4 * M, 40 * M, 0));

	SetAlight(Alone, OneFire);
	SetAlight(Crowded, FireA);
	SetAlight(Crowded, FireB);
	SetAlight(Crowded, FireC);

	// EXTINCTION'S OWN PARAMETERS WITHOUT ITS CEILING, which is the next test's
	// subject and would hide the rise being measured here.
	const TCHAR* Params = TEXT("Radius=15; Angle=360; Burn=1; ConsumeBurn=1; "
							   "IncreasedDamagePer=15; ScalingSource=Consumed");

	UCataclysmStrikeSkill* One = GrantSkill<UCataclysmStrikeSkill>(
		Alone, ECataclysmAbilitySlot::Ultimate, Params, TEXT("Extinction"));
	UCataclysmStrikeSkill* Three = GrantSkill<UCataclysmStrikeSkill>(
		Crowded, ECataclysmAbilitySlot::Ultimate, Params, TEXT("Extinction"));
	if (!One || !Three)
	{
		AddError(TEXT("Could not grant the strikes."));
		return false;
	}

	const float OneBefore = OneFire.Health();
	const float ThreeBefore = FireA.Health();

	TestTrue(TEXT("The lone cast activates"), Activate(Alone, One));
	TestTrue(TEXT("The crowded cast activates"), Activate(Crowded, Three));

	// THE ULTIMATE SLOT IS 400% OF WEAPON DAMAGE. One fire put out is worth
	// nothing, because the row reads "for every OTHER enemy consumed"; three
	// fires is two others, so thirty percentage points.
	const float Base = WeaponDamage * 400.0f / 100.0f;
	TestEqual(TEXT("One fire put out is the plain blow"),
		OneBefore - OneFire.Health(), Base, 0.01f);
	TestEqual(TEXT("Three fires put out is 30% more"),
		ThreeBefore - FireA.Health(), Base * 1.30f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaxDamagePercentTest,
	"Cataclysm.Skills.AScalingSkillStopsAtItsStatedCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaxDamagePercentTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter FireA(World, FVector(2 * M, 0, 0));
	FScopedFighter FireB(World, FVector(3 * M, 0, 0));
	FScopedFighter FireC(World, FVector(4 * M, 0, 0));
	FScopedFighter FireD(World, FVector(5 * M, 0, 0));

	SetAlight(Caster, FireA);
	SetAlight(Caster, FireB);
	SetAlight(Caster, FireC);
	SetAlight(Caster, FireD);

	// FOUR FIRES IS THREE OTHERS, so forty-five percentage points would take the
	// Ultimate slot's 400% to 580%. Extinction states a ceiling of 500%.
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Ultimate,
		TEXT("Radius=15; Angle=360; Burn=1; ConsumeBurn=1; "
			 "IncreasedDamagePer=15; ScalingSource=Consumed; MaxDamagePercent=500"),
		TEXT("Extinction"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const float Before = FireA.Health();
	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	const float Uncapped = WeaponDamage * 400.0f / 100.0f * 1.45f;
	const float Capped = WeaponDamage * 500.0f / 100.0f;

	// THE CEILING HAS TO BIND FOR THIS TEST TO MEAN ANYTHING. Stated rather than
	// assumed, because a cap above what the skill could reach is a check that
	// cannot fail.
	TestTrue(FString::Printf(
			TEXT("The ceiling binds: uncapped %.0f is above the cap %.0f"),
			Uncapped, Capped),
		Uncapped > Capped);
	TestEqual(TEXT("The blow lands for the stated ceiling"),
		Before - FireA.Health(), Capped, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHealthMissingScalesTest,
	"Cataclysm.Skills.SearingHookHitsHarderTheMoreHealthTheCasterIsMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHealthMissingScalesTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Whole(World, FVector::ZeroVector);
	FScopedFighter WholeTarget(World, FVector(2 * M, 0, 0));

	FScopedFighter Hurt(World, FVector(0, 40 * M, 0));
	FScopedFighter HurtTarget(World, FVector(2 * M, 40 * M, 0));

	// HALF THE CASTER'S HEALTH GONE, which the row prices at fifty percentage
	// points: "1% increased damage for every 1% of your maximum health you are
	// currently missing". The harness gives every fighter 100000 of each.
	Hurt.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 50000.0f);

	const TCHAR* Params = TEXT("Radius=2.5; Angle=60; MaxTargets=1; Burn=1; "
							   "IncreasedDamagePer=1; ScalingSource=HealthMissing");

	UCataclysmStrikeSkill* Full = GrantSkill<UCataclysmStrikeSkill>(
		Whole, ECataclysmAbilitySlot::Heavy, Params, TEXT("Searing Hook"));
	UCataclysmStrikeSkill* Half = GrantSkill<UCataclysmStrikeSkill>(
		Hurt, ECataclysmAbilitySlot::Heavy, Params, TEXT("Searing Hook"));
	if (!Full || !Half)
	{
		AddError(TEXT("Could not grant the strikes."));
		return false;
	}

	const float WholeBefore = WholeTarget.Health();
	const float HurtBefore = HurtTarget.Health();

	TestTrue(TEXT("The unhurt caster's hook activates"), Activate(Whole, Full));
	TestTrue(TEXT("The hurt caster's hook activates"), Activate(Hurt, Half));

	const float Base = WeaponDamage * 250.0f / 100.0f;
	TestEqual(TEXT("At full health it is the plain blow"),
		WholeBefore - WholeTarget.Health(), Base, 0.01f);
	TestEqual(TEXT("At half health it is 50% more"),
		HurtBefore - HurtTarget.Health(), Base * 1.5f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUncountedScalingSourceTest,
	"Cataclysm.Skills.ASkillNamingAnUncountedScalingSourceDealsItsPlainDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUncountedScalingSourceTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(2 * M, 0, 0));

	// SEVEN OF THE ELEVEN SOURCES ARE COUNTED BY NOTHING -- Kill, Second, Meter,
	// HitTaken, Bounce, Pierced and Pinned. A skill naming one has to deal its
	// plain damage rather than a figure taken from whatever happened to be to
	// hand. The Greatsword's Buried Fire is a real row that does.
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=6; Angle=360; Burn=1; MoreDamagePer=12; ScalingSource=Second"),
		TEXT("Buried Fire"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const float Before = Enemy.Health();
	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	TestEqual(TEXT("It dealt the Heavy slot's 250% and nothing more"),
		Before - Enemy.Health(), WeaponDamage * 250.0f / 100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRequiresBurningTest,
	"Cataclysm.Skills.ASkillRequiringSomethingAlightIsRefusedWhenNothingIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRequiresBurningTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Enemy(World, FVector(2 * M, 0, 0));

	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Radius=8; Angle=360; Burn=1; ConsumeBurn=1; Requires=Burning"),
		TEXT("Touch Off"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const float ManaBefore = Caster.Mana();
	const float HealthBefore = Enemy.Health();

	TestFalse(TEXT("With nothing alight it does not activate"),
		Activate(Caster, Strike));
	TestEqual(TEXT("And it spends no mana"), Caster.Mana(), ManaBefore);
	TestEqual(TEXT("And it hits nothing"), Enemy.Health(), HealthBefore);

	// AND THE SAME SKILL WORKS THE MOMENT SOMETHING IS ALIGHT, which is what
	// says the refusal was this condition and not the cost, the cooldown or a
	// missing target. A refused activation commits no cooldown, so this second
	// attempt is not blocked by the first.
	SetAlight(Caster, Enemy);
	TestTrue(TEXT("With an enemy alight it activates"), Activate(Caster, Strike));
	TestTrue(TEXT("And it hits"), Enemy.Health() < HealthBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRequiresTargetTest,
	"Cataclysm.Skills.ASkillRequiringATargetIsRefusedWithNobodyInReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRequiresTargetTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);

	// THE ONLY ENEMY IS BEYOND THE STATED RANGE. Twelve metres is what the Axe's
	// Emberhaul reaches; this one stands at twenty.
	FScopedFighter TooFar(World, FVector(20 * M, 0, 0));

	UCataclysmMovementSkill* Haul = GrantSkill<UCataclysmMovementSkill>(
		Caster, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Charge; Range=12; Radius=1.5; Burn=1; Requires=Target"),
		TEXT("Emberhaul"));
	if (!Haul)
	{
		AddError(TEXT("Could not grant the movement skill."));
		return false;
	}

	const float ManaBefore = Caster.Mana();
	TestFalse(TEXT("With nobody in reach it does not activate"),
		Activate(Caster, Haul));
	TestEqual(TEXT("And it spends no mana"), Caster.Mana(), ManaBefore);
	TestEqual(TEXT("And the caster has not moved"),
		Caster.Actor->GetActorLocation(), FVector::ZeroVector);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTravelsToTheBurningEnemyTest,
	"Cataclysm.Skills.FlashpointArrivesAtABurningEnemyRatherThanTheCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTravelsToTheBurningEnemyTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE CONTROL, ON ITS OWN CASTER AND WITH THE SAME SKILL MINUS THE
	// CONDITION. There is no player controller in this world, so the aim falls
	// back to the caster's own position: a movement skill that goes where the
	// player pointed does not move at all here. Without this, the test below
	// would pass just as well against a skill that always travelled to the
	// nearest enemy.
	{
		FScopedFighter Unconditional(World, FVector(0, 80 * M, 0));
		FScopedFighter ItsFire(World, FVector(0, 89 * M, 0));
		SetAlight(Unconditional, ItsFire);

		UCataclysmMovementSkill* Plain = GrantSkill<UCataclysmMovementSkill>(
			Unconditional, ECataclysmAbilitySlot::Movement,
			TEXT("Mode=Charge; Range=14; Radius=3; Burn=1"), TEXT("Cinder Rush"));
		if (!Plain)
		{
			AddError(TEXT("Could not grant the control movement skill."));
			return false;
		}

		TestTrue(TEXT("The control activates"), Activate(Unconditional, Plain));
		TestEqual(
			TEXT("With no condition it stays where it was, because it aims at "
				 "its own feet"),
			static_cast<float>(Plain->ArrivedAt.Y), 80.0f * M, 1.0f);
	}

	FScopedFighter Caster(World, FVector::ZeroVector);

	// THE BURNING ENEMY IS NINE METRES AWAY ALONG Y. The other stands nearer in
	// a different direction and well off the path to it, so it is neither
	// travelled to nor barrelled through.
	FScopedFighter Nearer(World, FVector(3 * M, -4 * M, 0));
	FScopedFighter Alight(World, FVector(0, 9 * M, 0));

	SetAlight(Caster, Alight);

	UCataclysmMovementSkill* Dart = GrantSkill<UCataclysmMovementSkill>(
		Caster, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Charge; Range=14; Radius=3; Burn=1; Requires=Burning; "
			 "ConsumeBurn=1"),
		TEXT("Flashpoint"));
	if (!Dart)
	{
		AddError(TEXT("Could not grant the movement skill."));
		return false;
	}

	const float AlightBefore = Alight.Health();
	const float NearerBefore = Nearer.Health();

	TestTrue(TEXT("It activates, because something is alight"),
		Activate(Caster, Dart));

	// AT THE BURNING ENEMY, not at the nearer one and not at the caster's own
	// feet. Compared on the ground plane, because the arrival keeps the caster's
	// own height.
	//
	// CAST TO float BECAUSE FVector HOLDS doubles IN UNREAL 5, and TestEqual has
	// overloads for both which the compiler cannot choose between when the
	// tolerance is a float.
	const FVector Reached = Alight.Actor->GetActorLocation();
	TestEqual(TEXT("It arrived at the burning enemy, along X"),
		static_cast<float>(Dart->ArrivedAt.X), static_cast<float>(Reached.X), 1.0f);
	TestEqual(TEXT("It arrived at the burning enemy, along Y"),
		static_cast<float>(Dart->ArrivedAt.Y), static_cast<float>(Reached.Y), 1.0f);

	TestTrue(TEXT("And the enemy it reached was hit"),
		Alight.Health() < AlightBefore);

	// AND THE ONE THAT WAS NEVER ALIGHT WAS NEITHER REACHED NOR PASSED. A charge
	// hits everything in its path, so this only means anything because that
	// enemy stands five metres from the line and the path is three metres wide.
	TestEqual(TEXT("The enemy that was never alight was left alone"),
		Nearer.Health(), NearerBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumeSpreadsFireTest,
	"Cataclysm.Skills.ConsumingAFireSpreadsItToEnemiesStandingNearby",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumeSpreadsFireTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Alight(World, FVector(4 * M, 0, 0));

	// TWO METRES FROM THE BURNING ENEMY AND OUTSIDE THE SWING. The strike
	// reaches five metres from the caster and this one stands at six, so the
	// only thing that can set it alight is the fire spreading from the enemy
	// whose burn was consumed.
	FScopedFighter Bystander(World, FVector(6 * M, 0, 0));
	FScopedFighter FarOff(World, FVector(12 * M, 0, 0));

	SetAlight(Caster, Alight);

	// NO `Burn` ON THE SKILL, so nothing but the spread can light anything.
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Special,
		TEXT("Radius=5; Angle=360; ConsumeBurn=1; ConsumeRadius=3"),
		TEXT("Touch Off"));
	if (!Strike)
	{
		AddError(TEXT("Could not grant the strike."));
		return false;
	}

	const float BystanderBefore = Bystander.Health();
	TestTrue(TEXT("It activates"), Activate(Caster, Strike));

	TestTrue(TEXT("The bystander outside the swing is alight"), IsAlight(Bystander));
	TestEqual(TEXT("And took no damage, because a spread is fire and not a blow"),
		Bystander.Health(), BystanderBefore);
	TestFalse(TEXT("An enemy beyond the spread is not alight"), IsAlight(FarOff));

	// THE ENEMY WHOSE FIRE IT WAS IS NOT RELIT BY ITS OWN SPREAD. This skill
	// states no `Burn`, so nothing else could put the tag back; were the spread
	// to include its own centre, consuming would cost the player nothing.
	TestFalse(TEXT("The consumed enemy is not set alight by its own spread"),
		IsAlight(Alight));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeldBuffWidensSpreadTest,
	"Cataclysm.Skills.AHeldSelfBuffWidensWhatConsumingSpreads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeldBuffWidensSpreadTest::RunTest(const FString&)
{
	using namespace CataclysmSkillTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE CONTROL FIRST AND ON ITS OWN CASTER, because a strike cannot be cast
	// twice: the first use commits its cooldown. Without it this test would pass
	// against a spread that was four metres wide all along.
	{
		FScopedFighter Unbuffed(World, FVector(0, 40 * M, 0));
		FScopedFighter ItsFire(World, FVector(4 * M, 40 * M, 0));
		FScopedFighter ItsBystander(World, FVector(7.5f * M, 40 * M, 0));
		SetAlight(Unbuffed, ItsFire);

		UCataclysmStrikeSkill* Plain = GrantSkill<UCataclysmStrikeSkill>(
			Unbuffed, ECataclysmAbilitySlot::Heavy,
			TEXT("Radius=5; Angle=360; ConsumeBurn=1"), TEXT("Quench"));
		if (!Plain)
		{
			AddError(TEXT("Could not grant the control strike."));
			return false;
		}

		TestTrue(TEXT("The control strike activates"), Activate(Unbuffed, Plain));
		TestFalse(
			TEXT("With no buff held, consuming spreads nothing to the bystander"),
			IsAlight(ItsBystander));
	}

	FScopedFighter Caster(World, FVector::ZeroVector);
	FScopedFighter Alight(World, FVector(4 * M, 0, 0));

	// THREE AND A HALF METRES FROM THE BURNING ENEMY. The strike below states no
	// spread of its own, so nothing reaches this bystander unless Ashen Edge's
	// four metres is being read off the buff the caster is holding.
	FScopedFighter Bystander(World, FVector(7.5f * M, 0, 0));

	SetAlight(Caster, Alight);

	// ASHEN EDGE CONSUMES NOTHING ITSELF. All it does is last ten seconds and
	// state a spread, which is how a row says "while this is up, what my other
	// skills consume also spreads".
	UCataclysmSelfBuffSkill* Edge = GrantSkill<UCataclysmSelfBuffSkill>(
		Caster, ECataclysmAbilitySlot::Support,
		TEXT("Duration=10; ConsumeRadius=4"), TEXT("Ashen Edge"));
	UCataclysmStrikeSkill* Strike = GrantSkill<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=5; Angle=360; ConsumeBurn=1"), TEXT("Quench"));
	if (!Edge || !Strike)
	{
		AddError(TEXT("Could not grant the two skills."));
		return false;
	}

	TestTrue(TEXT("Ashen Edge activates"), Activate(Caster, Edge));
	TestTrue(TEXT("And it is still running"), Edge->IsActive());

	TestTrue(TEXT("The strike activates"), Activate(Caster, Strike));
	TestTrue(TEXT("With the buff held, the fire spreads four metres"),
		IsAlight(Bystander));

	return true;
}
#endif // WITH_AUTOMATION_TESTS
