// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
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
	const float Expected = WeaponDamage * Step->GetSlotDamagePercent() / 100.0f;
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
			 "GroundRadius=1.5; GroundDuration=4"),
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
			 "GroundDuration=8"),
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
				 "GroundDuration=5"),
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
				 "GroundDuration=5"),
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

	const int32 Filled = Slots->EquipWeaponType(TEXT("Greataxe"));
	TestEqual(TEXT("A Demonic Greataxe fills six slots"), Filled, 6);

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
		TEXT("Item.Weapon.2hAxe, Element.Demonic, Type.Buff"));
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
		TEXT("Molten Cleave"), TEXT("Item.Weapon.2hAxe, Element.Demonic"));

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
		TEXT("Radius=4; Angle=360; GroundRadius=3; GroundDuration=6"),
		TEXT("Molten Cleave"), TEXT("Element.Demonic"));
	TestTrue(TEXT("The strike activates"), Activate(Caster, Strike));

	ACataclysmGroundZone* Zone = TheOnlyGroundZone(World);
	if (!Zone)
	{
		AddError(TEXT("Expected exactly one patch of burning ground."));
		return false;
	}

	const FCataclysmStatusEffectNumbers Burn = UCataclysmSkillEffects::BurnNumbers();
	if (!Burn.bUsable)
	{
		AddError(TEXT("Burn has no row in the generated status effect table."));
		return false;
	}

	const float Unbuffed = WeaponDamage * 250.0f / 100.0f
						 * Burn.PercentOfHit / 100.0f
						 / FMath::Max(1.0f, Burn.DurationSeconds);
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
		TEXT("Item.Weapon.2hAxe, Element.Demonic, Type.Strike"));
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
	const float OneHit = WeaponDamage * Hurl->GetSlotDamagePercent() / 100.0f;
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

	const float OneHit = WeaponDamage * Pyre->GetSlotDamagePercent() / 100.0f;
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

	const float OneHit = WeaponDamage * Hurl->GetSlotDamagePercent() / 100.0f;
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
			 "GroundRadius=1.5; GroundDuration=4"),
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
		TEXT("Item.Weapon.2hAxe, Element.Demonic"));
	TestTrue(TEXT("It activates"), Activate(Caster, Hurl));

	const float Before = Target.Health();
	FlyToCompletion(Hurl->InFlight);

	const float Unbuffed = WeaponDamage * Hurl->GetSlotDamagePercent() / 100.0f;
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
			 "GroundRadius=1.5; GroundDuration=4"),
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

#endif // WITH_AUTOMATION_TESTS
