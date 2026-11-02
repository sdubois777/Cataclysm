// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBuriedWeapon.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTether.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A damage-over-time effect, and a ground zone's or a tether's blow, carry a skill's tags without the skill. Issue
 * #2683, ruled by the coordinating session 2026-11-01: J1, the tags are identity only and never reach a burn's
 * pricing; J2, a zone's own ailment carries its zone's skill's tags and no skill name; J3, a burn whose skill goes
 * mid-burn and a projectile's burn whose firing skill has gone keep their tags. THE TAGS LIVE ON THE EFFECT
 * (`UCataclysmRuntimeEffect::SkillTagsWithNoSkill`), and `UCataclysmSkillEffects::SkillTagsOfABlowWithNoSkill` reads
 * them off a tick's `Spec.Def`. A GROUP OF ITS OWN, `Cataclysm.NoSkillDotTags.`: `Cataclysm.NoSkillBlowTags.` is
 * #2475's two instant blows, and a prefix with a trailing dot keeps the two apart.
 *
 * THE CASTER IS A BARE ACTOR AT THE ORIGIN holding an ability system, the combat and vital attribute sets, a Demonic
 * weapon slot and a weapon damage of 100, as `Cataclysm.CombatEvents.` builds one. EVERY CREATURE is of the monsters'
 * side with no armour, evasion, block or resistance. A SKILL IS GRANTED INTO A SLOT AND NAMED AND TAGGED BY HAND, as
 * its row would be, and A SKILL "GOES" THE WAY A ONE-USE GRANT GOES IN PLAY: `ClearAbility` on its handle, as
 * `UCataclysmTriggeredSkill` removes its grant. The engine marks the removed instance as garbage
 * (`UAbilitySystemComponent::OnRemoveAbility`, `MarkAbilityAsGarbage`), so every weak pointer to it is stale at once,
 * with no garbage collection; each test asserts that at set-up. NOTHING WAITS ON A TICK: a zone is swept, a line
 * checked and a projectile stepped by hand, and one tick of a damage-over-time effect is run with
 * `ExecutePeriodicEffectsGrantingForTests`, as `Cataclysm.CombatEvents.ATethersBurnNamesTheSkillThatBoundIt` runs it. A
 * KILL IS A TICK AT ONE POINT OF HEALTH. Helper types and functions are named after the subject, a damage over time
 * that names no skill.
 */
namespace CataclysmNoSkillDotTagsTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every creature here starts with: no blow or tick here comes near it. */
	constexpr float NoSkillDotEnemyHealth = 1000000.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FNoSkillDotWorld
	{
		FNoSkillDotWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FNoSkillDotWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A bare actor holding an ability system, the attributes a blow reads and a Demonic weapon slot. */
	struct FNoSkillDotCaster
	{
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * `Cataclysm.CombatEvents.`'s armed actor, under this file's own names. IT STANDS AT THE ORIGIN: a bare actor has no
	 * root component.
	 */
	FNoSkillDotCaster NoSkillDotMakeCaster(UWorld* World)
	{
		FNoSkillDotCaster Made;
		Made.Actor = World->SpawnActor<AActor>();
		if (!Made.Actor)
		{
			return Made;
		}
		Made.AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Made.Actor);
		Made.AbilitySystem->RegisterComponent();
		Made.AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Made.Actor));
		Made.AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Made.Actor));
		Made.AbilitySystem->InitAbilityActorInfo(Made.Actor, Made.Actor);

		UCataclysmWeaponSlotsComponent* Slots = NewObject<UCataclysmWeaponSlotsComponent>(Made.Actor);
		Slots->RegisterComponent();
		Slots->SetDamageType(TEXT("Demonic"));

		Made.AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		return Made;
	}

	/** A creature of the monsters' side with no armour, evasion, block or resistance. */
	ACataclysmEnemyCharacter* NoSkillDotEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(NoSkillDotEnemyHealth);
		Made->SetArmour(0.0f);
		Made->SetAttackDamage(0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), NoSkillDotEnemyHealth);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), NoSkillDotEnemyHealth);
		return Made;
	}

	/** The creature's own ability system, or null. */
	UCataclysmAbilitySystemComponent* NoSkillDotSystemOf(AActor* Creature)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Creature));
	}

	/** One point of health left, so its next tick kills it. */
	void NoSkillDotLeaveOnePoint(AActor* Creature)
	{
		if (UCataclysmAbilitySystemComponent* Its = NoSkillDotSystemOf(Creature))
		{
			Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
		}
	}

	/** A tag by name, or an invalid one. */
	FGameplayTag NoSkillDotTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/** Tags written the way a row writes them, separated by commas. */
	FGameplayTagContainer NoSkillDotTagsListed(const TCHAR* Listed)
	{
		TArray<FString> Names;
		FString(Listed).ParseIntoArray(Names, TEXT(","), /*InCullEmpty=*/true);
		FGameplayTagContainer Tags;
		for (FString& Name : Names)
		{
			Name.TrimStartAndEndInline();
			Tags.AddTag(NoSkillDotTag(*Name));
		}
		return Tags;
	}

	/** Whether every tag these tests name exists. */
	bool NoSkillDotTagsExist()
	{
		for (const TCHAR* Name : {TEXT("Item.Weapon.Axe"), TEXT("Item.Weapon.Greataxe"), TEXT("Item.Weapon.Whip"),
								  TEXT("Element.Demonic"), TEXT("Type.Projectile"), TEXT("Keyword.DoT.Burn"),
								  TEXT("Trigger.OnDeath"), TEXT("Type.Ranged"), TEXT("Type.Melee"),
								  TEXT("Type.Strike"), TEXT("Slot.Special")})
		{
			if (!NoSkillDotTag(Name).IsValid())
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * Grants a skill into a slot and names and tags it the way its row would, as `Cataclysm.CombatEvents.` does. Returns
	 * the granted instance, which is where a real skill keeps its name, tags and shape parameters.
	 */
	template <typename TSkill>
	TSkill* NoSkillDotGrant(const FNoSkillDotCaster& User, ECataclysmAbilitySlot Slot, const TCHAR* Name,
							const TCHAR* Tags, const TCHAR* Params, FGameplayAbilitySpecHandle& OutHandle)
	{
		OutHandle = User.AbilitySystem->GiveAbilityInSlot(TSkill::StaticClass(), Slot, /*Level=*/100, User.Actor);
		FGameplayAbilitySpec* Spec = User.AbilitySystem->FindAbilitySpecFromHandle(OutHandle);
		TSkill* Skill = Spec ? Cast<TSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = Name;
			Skill->SkillTags = NoSkillDotTagsListed(Tags);
			Skill->Params = UCataclysmSkillShapes::ParseParams(Params);
		}
		return Skill;
	}

	/** What one hit notice said, copied while it was handed out. */
	struct FNoSkillDotHit
	{
		const AActor* Attacker = nullptr;
		const AActor* Target = nullptr;
		bool bDamageOverTime = false;
		FName SkillName;
		bool bHadSkillTags = false;
		FGameplayTagContainer SkillTags;
	};

	/** What one death notice said, copied while it was handed out. */
	struct FNoSkillDotDeath
	{
		const AActor* Victim = nullptr;
		const AActor* Killer = nullptr;
		bool bByDamageOverTime = false;
		FName SkillName;
		bool bHadSkillTags = false;
		FGameplayTagContainer SkillTags;
	};

	/**
	 * Every hit and death notice a world hands out while this is alive. THE TAG CONTAINERS ARE COPIED WHILE THE NOTICE
	 * IS HANDED OUT, because its pointers are valid only then.
	 */
	struct FNoSkillDotHeard
	{
		explicit FNoSkillDotHeard(UWorld* World)
			: Events(UCataclysmCombatEvents::In(World))
		{
			if (!Events)
			{
				return;
			}
			HitHandle = Events->OnHit.AddLambda([this](const FCataclysmHitNotice& Notice)
			{
				FNoSkillDotHit& One = Hits.AddDefaulted_GetRef();
				One.Attacker = Notice.Attacker;
				One.Target = Notice.Target;
				One.bDamageOverTime = Notice.bDamageOverTime;
				One.SkillName = Notice.SkillName;
				One.bHadSkillTags = Notice.SkillTags != nullptr;
				if (Notice.SkillTags)
				{
					One.SkillTags = *Notice.SkillTags;
				}
			});
			DeathHandle = Events->OnDeath.AddLambda([this](const FCataclysmDeathNotice& Notice)
			{
				FNoSkillDotDeath& One = Deaths.AddDefaulted_GetRef();
				One.Victim = Notice.Victim;
				One.Killer = Notice.Killer;
				One.bByDamageOverTime = Notice.bByDamageOverTime;
				One.SkillName = Notice.KillingSkillName;
				One.bHadSkillTags = Notice.KillingSkillTags != nullptr;
				if (Notice.KillingSkillTags)
				{
					One.SkillTags = *Notice.KillingSkillTags;
				}
			});
		}
		~FNoSkillDotHeard()
		{
			if (Events)
			{
				Events->OnHit.Remove(HitHandle);
				Events->OnDeath.Remove(DeathHandle);
			}
		}

		/** How many death notices named `Victim`. */
		int32 DeathsOf(const AActor* Victim) const
		{
			int32 Found = 0;
			for (const FNoSkillDotDeath& One : Deaths)
			{
				Found += One.Victim == Victim ? 1 : 0;
			}
			return Found;
		}

		/** The last death notice naming `Victim`, or an empty one. */
		FNoSkillDotDeath DeathOf(const AActor* Victim) const
		{
			for (int32 Index = Deaths.Num() - 1; Index >= 0; --Index)
			{
				if (Deaths[Index].Victim == Victim)
				{
					return Deaths[Index];
				}
			}
			return FNoSkillDotDeath();
		}

		UCataclysmCombatEvents* Events = nullptr;
		TArray<FNoSkillDotHit> Hits;
		TArray<FNoSkillDotDeath> Deaths;
		FDelegateHandle HitHandle;
		FDelegateHandle DeathHandle;
	};

	/**
	 * Runs one tick of every burn on `Target` and returns the one hit notice it sent, or an empty one. Reports through
	 * `Test` and returns false when the tick did not run exactly one burn or announced other than exactly one hit on
	 * `Target` from `Attacker` as damage over time.
	 */
	bool NoSkillDotTickTheBurn(FAutomationTestBase& Test, const TCHAR* What, FNoSkillDotHeard& Heard,
							   const AActor* Attacker, AActor* Target, FNoSkillDotHit& OutHit)
	{
		UCataclysmAbilitySystemComponent* Its = NoSkillDotSystemOf(Target);
		const int32 HitsBefore = Heard.Hits.Num();
		const int32 Ran = Its ? Its->ExecutePeriodicEffectsGrantingForTests(UCataclysmSkillEffects::BurnTag()) : 0;
		if (!Test.TestEqual(*FString::Printf(TEXT("set-up: %s ran one burn"), What), Ran, 1)
			|| !Test.TestEqual(*FString::Printf(TEXT("set-up: %s announced one hit"), What),
							   Heard.Hits.Num(), HitsBefore + 1))
		{
			return false;
		}
		OutHit = Heard.Hits.Last();
		return Test.TestTrue(*FString::Printf(TEXT("set-up: %s is the caster's damage over time on its target"), What),
							 OutHit.Attacker == Attacker && OutHit.Target == Target && OutHit.bDamageOverTime);
	}

	/**
	 * Leaves `Victim` one point of health, runs one tick of its burn, and returns the one death notice naming it.
	 * Reports through `Test` and returns false when the tick did not kill it or no single notice was sent.
	 */
	bool NoSkillDotKillWithATick(FAutomationTestBase& Test, const TCHAR* What, FNoSkillDotHeard& Heard,
								 const AActor* Killer, AActor* Victim, FNoSkillDotDeath& OutDeath)
	{
		NoSkillDotLeaveOnePoint(Victim);
		UCataclysmAbilitySystemComponent* Its = NoSkillDotSystemOf(Victim);
		const int32 Ran = Its ? Its->ExecutePeriodicEffectsGrantingForTests(UCataclysmSkillEffects::BurnTag()) : 0;
		if (!Test.TestEqual(*FString::Printf(TEXT("set-up: %s ran one burn"), What), Ran, 1)
			|| !Test.TestTrue(*FString::Printf(TEXT("set-up: %s killed its victim"), What),
							  UCataclysmSkillEffects::IsDead(Victim))
			|| !Test.TestEqual(*FString::Printf(TEXT("set-up: %s sent one death notice"), What),
							   Heard.DeathsOf(Victim), 1))
		{
			return false;
		}
		OutDeath = Heard.DeathOf(Victim);
		return Test.TestTrue(*FString::Printf(TEXT("set-up: %s is credited to the caster, by damage over time"), What),
							 OutDeath.Killer == Killer && OutDeath.bByDamageOverTime);
	}

	/** Every creature this test made, or false with the index of the first that was not. */
	bool NoSkillDotAllMade(FAutomationTestBase& Test, const TArray<ACataclysmEnemyCharacter*>& Made)
	{
		for (int32 Index = 0; Index < Made.Num(); ++Index)
		{
			if (!Test.TestNotNull(*FString::Printf(TEXT("set-up: creature %d"), Index), Made[Index]))
			{
				return false;
			}
		}
		return true;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. A buried weapon's burn carries its throwing skill's tags, and a burn laid with neither carries none
// ==========================================================================

CATACLYSM_TEST(FCataclysmNoSkillDotBuriedBurnTest,
	"Cataclysm.NoSkillDotTags.ABuriedWeaponsBurnCarriesTheThrowingSkillsTagsAndNoNameToItsTicksAndItsKill")
{
	using namespace CataclysmNoSkillDotTagsTest;

	// AN AXE WITH HARROWER'S TAGS (`Demonic_Axe_Special` in `game/Data/WeaponSkills.csv`) AND `Burn=1` IS BURIED IN A
	// HOST 20 M NORTH OF THE CASTER, AND LEAPS to the nearest living enemy, 4 m east of the host, as
	// `Cataclysm.BuriedWeaponKills.` buries and leaps one; its leap sets that enemy alight. THE CONTROL is a burn laid on
	// a creature 20 m south with `ApplyBurn` and nothing else: no skill and no tags.
	FNoSkillDotWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	if (!TestTrue(TEXT("set-up: every tag these tests name exists"), NoSkillDotTagsExist()))
	{
		return false;
	}
	FNoSkillDotCaster Caster = NoSkillDotMakeCaster(World);
	ACataclysmEnemyCharacter* Host = NoSkillDotEnemy(World, FVector(0.0f, 20.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Struck = NoSkillDotEnemy(World, FVector(4.0f * M, 20.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Plain = NoSkillDotEnemy(World, FVector(0.0f, -20.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a caster"), Caster.Actor) || !NoSkillDotAllMade(*this, {Host, Struck, Plain}))
	{
		return false;
	}
	FNoSkillDotHeard Heard(World);
	if (!TestNotNull(TEXT("set-up: the world's combat events"), Heard.Events))
	{
		return false;
	}
	const FGameplayTag Ranged = NoSkillDotTag(TEXT("Type.Ranged"));
	const FGameplayTag Special = NoSkillDotTag(TEXT("Slot.Special"));
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();

	if (!TestNotNull(TEXT("set-up: the axe is buried in the host with Harrower's tags, and burns"),
			UCataclysmBuriedWeapon::BuryIn(Host, Caster.Actor, 10.0f * M, 100.0f,
				NoSkillDotTagsListed(TEXT("Item.Weapon.Axe, Element.Demonic, Type.Projectile, Keyword.DoT.Burn, "
										  "Trigger.OnDeath, Type.Ranged, Slot.Special")),
				/*bInBurns=*/true))
		|| !TestTrue(TEXT("set-up: the axe leaps from its host"), UCataclysmBuriedWeapon::LeapFromDying(Host))
		|| !TestTrue(TEXT("set-up: and its leap set the enemy 4 m east alight"), UCataclysmSkillEffects::HasTag(Struck, Burn))
		|| !TestFalse(TEXT("set-up: without killing it"), UCataclysmSkillEffects::IsDead(Struck)))
	{
		return false;
	}

	// ONE TICK: ITS NOTICE NAMES THE THROWING SKILL'S TAGS AND NO SKILL.
	FNoSkillDotHit Tick;
	if (!NoSkillDotTickTheBurn(*this, TEXT("the buried weapon's burn"), Heard, Caster.Actor, Struck, Tick))
	{
		return false;
	}
	TestTrue(TEXT("the buried weapon's burn's tick names no skill"), Tick.SkillName.IsNone());
	TestTrue(TEXT("the buried weapon's burn's tick carries skill tags"), Tick.bHadSkillTags);
	TestTrue(TEXT("the buried weapon's burn's tick carries Type.Ranged, the throwing skill's"), Tick.SkillTags.HasTagExact(Ranged));

	// AND ITS KILL CARRIES THEM TO THE CASTER'S ROWS, as the leap's own kill does (#2465), and names no skill.
	FNoSkillDotDeath Kill;
	if (!NoSkillDotKillWithATick(*this, TEXT("the buried weapon's burn"), Heard, Caster.Actor, Struck, Kill))
	{
		return false;
	}
	TestTrue(TEXT("the buried weapon's burn's kill names no skill"), Kill.SkillName.IsNone());
	TestTrue(TEXT("the buried weapon's burn's kill carries skill tags"), Kill.bHadSkillTags);
	TestTrue(TEXT("the buried weapon's burn's kill carries Type.Ranged, which a ranged kill row reads"),
			 Kill.SkillTags.HasTagExact(Ranged));
	TestTrue(TEXT("the buried weapon's burn's kill carries Slot.Special, the slot a cooldown refund clears"),
			 Kill.SkillTags.HasTagExact(Special));

	// THE CONTROL: A BURN LAID WITH NO SKILL AND NO TAGS CARRIES NONE, on its tick or its kill.
	if (!TestTrue(TEXT("control set-up: a bare burn was laid"),
			UCataclysmSkillEffects::ApplyBurn(Caster.Actor, Plain, /*HitDamage=*/0.0f,
											  /*bScalesWithInstigator=*/true, /*bBurnIsDesigned=*/true)))
	{
		return false;
	}
	FNoSkillDotHit PlainTick;
	if (!NoSkillDotTickTheBurn(*this, TEXT("the bare burn"), Heard, Caster.Actor, Plain, PlainTick))
	{
		return false;
	}
	TestTrue(TEXT("control: the bare burn's tick names no skill"), PlainTick.SkillName.IsNone());
	TestFalse(TEXT("control: the bare burn's tick carries no skill tags"), PlainTick.bHadSkillTags);
	FNoSkillDotDeath PlainKill;
	if (!NoSkillDotKillWithATick(*this, TEXT("the bare burn"), Heard, Caster.Actor, Plain, PlainKill))
	{
		return false;
	}
	TestTrue(TEXT("control: the bare burn's kill names no skill"), PlainKill.SkillName.IsNone());
	TestFalse(TEXT("control: the bare burn's kill carries no skill tags"), PlainKill.bHadSkillTags);
	return true;
}

// ==========================================================================
// 2. A ground zone's own ailment carries the zone's skill's tags and no name, and a bare zone's carries none (J2)
// ==========================================================================

CATACLYSM_TEST(FCataclysmNoSkillDotZoneAilmentTest,
	"Cataclysm.NoSkillDotTags.AGroundZonesOwnAilmentCarriesTheZonesSkillTagsAndNoNameAndABareZonesCarriesNone")
{
	using namespace CataclysmNoSkillDotTagsTest;

	// A DEMONIC STRIKE WHOSE ROW LEAVES GROUND, cast by a caster wearing "Persistent AOE zones apply a DoT to enemies
	// standing in them" (`zone_applies_own_ailment`, a flat 1, written by hand as the item builds it), as
	// `Cataclysm.CombatEvents.AGroundZonesSweepNamesTheSkillThatLeftIt` casts one. Its zone's own ailment is Burn. A
	// VICTIM 1 M IN FRONT, inside the strike and the ground. THE CONTROL is a zone made by `ACataclysmGroundZone::Spawn`
	// alone, 20 m along Y, given the same ailment by hand, with a bystander in it.
	FNoSkillDotWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	if (!TestTrue(TEXT("set-up: every tag these tests name exists"), NoSkillDotTagsExist()))
	{
		return false;
	}
	FNoSkillDotCaster Caster = NoSkillDotMakeCaster(World);
	ACataclysmEnemyCharacter* Victim = NoSkillDotEnemy(World, FVector(1.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Bystander = NoSkillDotEnemy(World, FVector(0.0f, 20.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a caster"), Caster.Actor) || !NoSkillDotAllMade(*this, {Victim, Bystander}))
	{
		return false;
	}
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::ZoneAppliesOwnAilmentStat)).Base = 1.0f;
		Caster.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	}
	const FGameplayTag Melee = NoSkillDotTag(TEXT("Type.Melee"));
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();

	FGameplayAbilitySpecHandle Handle;
	UCataclysmStrikeSkill* Trail = NoSkillDotGrant<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::BasicAttack, TEXT("Molten Trail"),
		TEXT("Item.Weapon.Greataxe, Element.Demonic, Type.Strike, Type.Melee"),
		TEXT("Radius=2.4; Angle=120; MaxTargets=1; GroundRadius=3; GroundDuration=4; GroundPercent=25"), Handle);
	if (!TestNotNull(TEXT("set-up: a strike that leaves ground"), Trail)
		|| !TestTrue(TEXT("set-up: the strike started"),
			Caster.AbilitySystem->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false)))
	{
		return false;
	}
	TArray<ACataclysmGroundZone*> Zones;
	for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
	{
		Zones.Add(*It);
	}
	if (!TestEqual(TEXT("set-up: the strike left one zone"), Zones.Num(), 1))
	{
		return false;
	}
	ACataclysmGroundZone* Zone = Zones[0];
	if (!TestEqual(TEXT("set-up: the zone's own ailment is Burn"), Zone->OwnAilment, FName(TEXT("Burn")))
		|| !TestTrue(TEXT("set-up: the zone keeps the skill that left it"), Zone->SkillThatLeftIt.Get() == Trail)
		|| !TestFalse(TEXT("set-up: the victim is not alight before the sweep"), UCataclysmSkillEffects::HasTag(Victim, Burn)))
	{
		return false;
	}

	FNoSkillDotHeard Heard(World);
	if (!TestNotNull(TEXT("set-up: the world's combat events"), Heard.Events))
	{
		return false;
	}
	Zone->Sweep();
	if (!TestTrue(TEXT("set-up: the sweep set the victim alight"), UCataclysmSkillEffects::HasTag(Victim, Burn)))
	{
		return false;
	}

	// ONE TICK OF THE ZONE'S OWN AILMENT: ITS NOTICE NAMES THE ZONE'S SKILL'S TAGS, AND NO SKILL (J2).
	FNoSkillDotHit Tick;
	if (!NoSkillDotTickTheBurn(*this, TEXT("the zone's own ailment"), Heard, Caster.Actor, Victim, Tick))
	{
		return false;
	}
	TestTrue(TEXT("the zone's own ailment's tick names no skill"), Tick.SkillName.IsNone());
	TestTrue(TEXT("the zone's own ailment's tick carries skill tags"), Tick.bHadSkillTags);
	TestTrue(TEXT("the zone's own ailment's tick carries Type.Melee, the zone's skill's"), Tick.SkillTags.HasTagExact(Melee));

	FNoSkillDotDeath Kill;
	if (!NoSkillDotKillWithATick(*this, TEXT("the zone's own ailment"), Heard, Caster.Actor, Victim, Kill))
	{
		return false;
	}
	TestTrue(TEXT("the zone's own ailment's kill names no skill"), Kill.SkillName.IsNone());
	TestTrue(TEXT("the zone's own ailment's kill carries skill tags"), Kill.bHadSkillTags);
	TestTrue(TEXT("the zone's own ailment's kill carries Type.Melee, which a melee kill row reads"),
			 Kill.SkillTags.HasTagExact(Melee));

	// THE CONTROL: A ZONE NO SKILL LEFT, GIVEN THE SAME AILMENT, LAYS ONE THAT CARRIES NO TAGS.
	ACataclysmGroundZone* Bare = ACataclysmGroundZone::Spawn(
		Caster.Actor, FVector(0.0f, 20.0f * M, 0.0f), /*RadiusCm=*/1.5f * M, /*Duration=*/4.0f, /*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("control set-up: a zone no skill left"), Bare))
	{
		return false;
	}
	Bare->OwnAilment = FName(TEXT("Burn"));
	Bare->Sweep();
	if (!TestTrue(TEXT("control set-up: its sweep set the bystander alight"), UCataclysmSkillEffects::HasTag(Bystander, Burn)))
	{
		return false;
	}
	FNoSkillDotHit BareTick;
	if (!NoSkillDotTickTheBurn(*this, TEXT("the bare zone's ailment"), Heard, Caster.Actor, Bystander, BareTick))
	{
		return false;
	}
	TestFalse(TEXT("control: the bare zone's ailment's tick carries no skill tags"), BareTick.bHadSkillTags);
	FNoSkillDotDeath BareKill;
	if (!NoSkillDotKillWithATick(*this, TEXT("the bare zone's ailment"), Heard, Caster.Actor, Bystander, BareKill))
	{
		return false;
	}
	TestTrue(TEXT("control: the bare zone's ailment's kill names no skill"), BareKill.SkillName.IsNone());
	TestFalse(TEXT("control: the bare zone's ailment's kill carries no skill tags"), BareKill.bHadSkillTags);
	return true;
}

// ==========================================================================
// 3. A zone whose skill has gone keeps its tags on its sweep and its kill
// ==========================================================================

CATACLYSM_TEST(FCataclysmNoSkillDotStaleZoneTest,
	"Cataclysm.NoSkillDotTags.AZoneWhoseSkillHasGoneKeepsItsTagsOnItsSweepAndItsKill")
{
	using namespace CataclysmNoSkillDotTagsTest;

	// THE STRIKE OF TEST 2, WITH NO ROW, LEAVES ITS ZONE; THEN ITS GRANT IS REMOVED, as a one-use grant is removed
	// after the use. Its sweeps then name no skill and carry the tags it left the zone with (#2683, as a projectile
	// whose firing skill has gone does, #2475). A SWEEP IS DIRECT DAMAGE, NOT A DAMAGE-OVER-TIME EFFECT: it is
	// `FCataclysmHitDelivery::SkillTagsWithNoSkill` that carries them here.
	FNoSkillDotWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	if (!TestTrue(TEXT("set-up: every tag these tests name exists"), NoSkillDotTagsExist()))
	{
		return false;
	}
	FNoSkillDotCaster Caster = NoSkillDotMakeCaster(World);
	ACataclysmEnemyCharacter* Victim = NoSkillDotEnemy(World, FVector(1.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a caster"), Caster.Actor) || !NoSkillDotAllMade(*this, {Victim}))
	{
		return false;
	}
	const FGameplayTag Melee = NoSkillDotTag(TEXT("Type.Melee"));

	FGameplayAbilitySpecHandle Handle;
	UCataclysmStrikeSkill* Trail = NoSkillDotGrant<UCataclysmStrikeSkill>(
		Caster, ECataclysmAbilitySlot::BasicAttack, TEXT("Molten Trail"),
		TEXT("Item.Weapon.Greataxe, Element.Demonic, Type.Strike, Type.Melee"),
		TEXT("Radius=2.4; Angle=120; MaxTargets=1; GroundRadius=3; GroundDuration=4; GroundPercent=25"), Handle);
	if (!TestNotNull(TEXT("set-up: a strike that leaves ground"), Trail)
		|| !TestTrue(TEXT("set-up: the strike started"),
			Caster.AbilitySystem->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false)))
	{
		return false;
	}
	TArray<ACataclysmGroundZone*> Zones;
	for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
	{
		Zones.Add(*It);
	}
	if (!TestEqual(TEXT("set-up: the strike left one zone"), Zones.Num(), 1))
	{
		return false;
	}
	ACataclysmGroundZone* Zone = Zones[0];
	if (!TestTrue(TEXT("set-up: the zone deals damage"), Zone->DamagePerTick > 0.0f)
		|| !TestTrue(TEXT("set-up: the zone keeps the skill that left it"), Zone->SkillThatLeftIt.Get() == Trail))
	{
		return false;
	}

	// THE GRANT IS REMOVED, AND THE SKILL IS GONE.
	const TWeakObjectPtr<UCataclysmStrikeSkill> Left(Trail);
	Caster.AbilitySystem->ClearAbility(Handle);
	Trail = nullptr;
	if (!TestTrue(TEXT("set-up: the removed skill's instance is stale"), Left.IsStale())
		|| !TestTrue(TEXT("set-up: the zone outlives it"), IsValid(Zone))
		|| !TestFalse(TEXT("set-up: and the zone's pointer to it answers nothing"), Zone->SkillThatLeftIt.IsValid()))
	{
		return false;
	}

	FNoSkillDotHeard Heard(World);
	if (!TestNotNull(TEXT("set-up: the world's combat events"), Heard.Events))
	{
		return false;
	}
	const int32 HitsBefore = Heard.Hits.Num();
	Zone->Sweep();
	if (!TestEqual(TEXT("set-up: the sweep announced one hit"), Heard.Hits.Num(), HitsBefore + 1)
		|| !TestTrue(TEXT("set-up: the caster's area damage over time on the victim"),
			Heard.Hits.Last().Attacker == Caster.Actor && Heard.Hits.Last().Target == Victim
				&& Heard.Hits.Last().bDamageOverTime))
	{
		return false;
	}
	const FNoSkillDotHit Sweep = Heard.Hits.Last();
	TestTrue(TEXT("the sweep of a zone whose skill has gone names no skill"), Sweep.SkillName.IsNone());
	TestTrue(TEXT("the sweep of a zone whose skill has gone carries skill tags"), Sweep.bHadSkillTags);
	TestTrue(TEXT("the sweep of a zone whose skill has gone carries Type.Melee, the skill's"), Sweep.SkillTags.HasTagExact(Melee));

	NoSkillDotLeaveOnePoint(Victim);
	Zone->Sweep();
	if (!TestTrue(TEXT("set-up: the second sweep killed the victim"), UCataclysmSkillEffects::IsDead(Victim))
		|| !TestEqual(TEXT("set-up: and sent one death notice"), Heard.DeathsOf(Victim), 1))
	{
		return false;
	}
	const FNoSkillDotDeath Kill = Heard.DeathOf(Victim);
	TestTrue(TEXT("set-up: the death is credited to the caster, by damage over time"),
			 Kill.Killer == Caster.Actor && Kill.bByDamageOverTime);
	TestTrue(TEXT("the kill by a zone whose skill has gone names no skill"), Kill.SkillName.IsNone());
	TestTrue(TEXT("the kill by a zone whose skill has gone carries skill tags"), Kill.bHadSkillTags);
	TestTrue(TEXT("the kill by a zone whose skill has gone carries Type.Melee, which a melee kill row reads"),
			 Kill.SkillTags.HasTagExact(Melee));
	return true;
}

// ==========================================================================
// 4 and 5. A line whose skill has gone, and a burn whose skill goes mid-burn, keep their tags
// ==========================================================================

namespace CataclysmNoSkillDotTagsTest
{
	/** A caster, the line its Whip skill bound, the creature touching the line, and the skill's grant. */
	struct FNoSkillDotLine
	{
		FNoSkillDotCaster Caster;
		ACataclysmEnemyCharacter* Touching = nullptr;
		ACataclysmTether* Line = nullptr;
		UCataclysmProjectileSkill* Lash = nullptr;
		FGameplayAbilitySpecHandle Handle;
	};

	/**
	 * `Cataclysm.CombatEvents.ATethersBurnNamesTheSkillThatBoundIt`'s line: two ends three metres from the caster, one
	 * along X and one along Y, and a creature 1.2 m off the line's middle, on the far side from the caster, which the
	 * line's check sets alight. Every creature is more than 2 m from every other, and the ends are 4.24 m apart, inside
	 * the five metres, so the check drags nobody. Reports through `Test` and returns false when any of it was not made.
	 */
	bool NoSkillDotBindALine(FAutomationTestBase& Test, UWorld* World, FNoSkillDotLine& Out)
	{
		Out.Caster = NoSkillDotMakeCaster(World);
		ACataclysmEnemyCharacter* OneEnd = NoSkillDotEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
		ACataclysmEnemyCharacter* OtherEnd = NoSkillDotEnemy(World, FVector(0.0f, 3.0f * M, 0.0f));
		Out.Touching = NoSkillDotEnemy(World, FVector(2.35f * M, 2.35f * M, 0.0f));
		if (!Test.TestNotNull(TEXT("set-up: a caster"), Out.Caster.Actor)
			|| !NoSkillDotAllMade(Test, {OneEnd, OtherEnd, Out.Touching}))
		{
			return false;
		}
		Out.Lash = NoSkillDotGrant<UCataclysmProjectileSkill>(
			Out.Caster, ECataclysmAbilitySlot::Special, TEXT("Burning Line"),
			TEXT("Item.Weapon.Whip, Element.Demonic, Type.Projectile, Type.Ranged"),
			TEXT("Range=12; Radius=1.5; Speed=2000; TetherTargets=2; TetherLength=5; TetherDuration=8"), Out.Handle);
		if (!Test.TestNotNull(TEXT("set-up: a skill that binds a tether"), Out.Lash))
		{
			return false;
		}
		Out.Line = Out.Lash->BindTether(Out.Caster.Actor);
		if (!Test.TestNotNull(TEXT("set-up: the skill bound a line"), Out.Line))
		{
			return false;
		}
		const bool bBoundTheEnds = (Out.Line->First.Get() == OneEnd && Out.Line->Second.Get() == OtherEnd)
			|| (Out.Line->First.Get() == OtherEnd && Out.Line->Second.Get() == OneEnd);
		return Test.TestTrue(TEXT("set-up: between the two ends"), bBoundTheEnds);
	}

	/** Removes the line's skill's grant, as a one-use grant is removed, and reports whether the skill is gone. */
	bool NoSkillDotRemoveTheLinesSkill(FAutomationTestBase& Test, FNoSkillDotLine& Bound)
	{
		const TWeakObjectPtr<UCataclysmProjectileSkill> Was(Bound.Lash);
		Bound.Caster.AbilitySystem->ClearAbility(Bound.Handle);
		Bound.Lash = nullptr;
		return Test.TestTrue(TEXT("set-up: the removed skill's instance is stale"), Was.IsStale())
			&& Test.TestTrue(TEXT("set-up: the line outlives it"), IsValid(Bound.Line));
	}
}

CATACLYSM_TEST(FCataclysmNoSkillDotStaleLineTest,
	"Cataclysm.NoSkillDotTags.ALineWhoseSkillHasGoneKeepsItsTagsOnItsBurnAndItsKill")
{
	using namespace CataclysmNoSkillDotTagsTest;

	// THE LINE IS BOUND, ITS SKILL'S GRANT IS REMOVED, AND ONLY THEN DOES THE LINE SET ANYBODY ALIGHT: the burn is laid
	// with no skill and the tags the line kept (#2683), so it is priced with no skill (J1) and its ticks and kill carry
	// the tags and no name.
	FNoSkillDotWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	if (!TestTrue(TEXT("set-up: every tag these tests name exists"), NoSkillDotTagsExist()))
	{
		return false;
	}
	FNoSkillDotLine Bound;
	if (!NoSkillDotBindALine(*this, World, Bound) || !NoSkillDotRemoveTheLinesSkill(*this, Bound))
	{
		return false;
	}
	const FGameplayTag Ranged = NoSkillDotTag(TEXT("Type.Ranged"));
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();

	FNoSkillDotHeard Heard(World);
	if (!TestNotNull(TEXT("set-up: the world's combat events"), Heard.Events))
	{
		return false;
	}
	Bound.Line->Check();
	if (!TestEqual(TEXT("set-up: the check set one alight"), Bound.Line->LastCheckLit, 1)
		|| !TestTrue(TEXT("set-up: the one touching the line"), UCataclysmSkillEffects::HasTag(Bound.Touching, Burn)))
	{
		return false;
	}

	FNoSkillDotHit Tick;
	if (!NoSkillDotTickTheBurn(*this, TEXT("the line's burn"), Heard, Bound.Caster.Actor, Bound.Touching, Tick))
	{
		return false;
	}
	TestTrue(TEXT("the burn of a line whose skill has gone names no skill on its tick"), Tick.SkillName.IsNone());
	TestTrue(TEXT("the burn of a line whose skill has gone carries skill tags on its tick"), Tick.bHadSkillTags);
	TestTrue(TEXT("the burn of a line whose skill has gone carries Type.Ranged on its tick"), Tick.SkillTags.HasTagExact(Ranged));

	FNoSkillDotDeath Kill;
	if (!NoSkillDotKillWithATick(*this, TEXT("the line's burn"), Heard, Bound.Caster.Actor, Bound.Touching, Kill))
	{
		return false;
	}
	TestTrue(TEXT("the kill by the burn of a line whose skill has gone names no skill"), Kill.SkillName.IsNone());
	TestTrue(TEXT("the kill by the burn of a line whose skill has gone carries skill tags"), Kill.bHadSkillTags);
	TestTrue(TEXT("the kill by the burn of a line whose skill has gone carries Type.Ranged, which a ranged kill row reads"),
			 Kill.SkillTags.HasTagExact(Ranged));
	return true;
}

CATACLYSM_TEST(FCataclysmNoSkillDotMidBurnTest,
	"Cataclysm.NoSkillDotTags.ABurnWhoseSkillGoesMidBurnKeepsItsTagsOnItsKill")
{
	using namespace CataclysmNoSkillDotTagsTest;

	// THE LINE SETS THE CREATURE ALIGHT WHILE ITS SKILL IS THERE, so the burn names the skill (#1585); ITS GRANT IS THEN
	// REMOVED MID-BURN, and the burn's next tick names no skill and carries the skill's tags, which its effect kept when
	// it was laid (#2683, J3).
	FNoSkillDotWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	if (!TestTrue(TEXT("set-up: every tag these tests name exists"), NoSkillDotTagsExist()))
	{
		return false;
	}
	FNoSkillDotLine Bound;
	if (!NoSkillDotBindALine(*this, World, Bound))
	{
		return false;
	}
	const FGameplayTag Ranged = NoSkillDotTag(TEXT("Type.Ranged"));
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();

	FNoSkillDotHeard Heard(World);
	if (!TestNotNull(TEXT("set-up: the world's combat events"), Heard.Events))
	{
		return false;
	}
	Bound.Line->Check();
	if (!TestEqual(TEXT("set-up: the check set one alight"), Bound.Line->LastCheckLit, 1)
		|| !TestTrue(TEXT("set-up: the one touching the line"), UCataclysmSkillEffects::HasTag(Bound.Touching, Burn)))
	{
		return false;
	}
	FNoSkillDotHit WhileThere;
	if (!NoSkillDotTickTheBurn(*this, TEXT("the burn's first tick"), Heard, Bound.Caster.Actor, Bound.Touching, WhileThere)
		|| !TestEqual(TEXT("set-up: while the skill is there its burn's tick names it"),
					  WhileThere.SkillName.ToString(), FString(TEXT("Burning Line"))))
	{
		return false;
	}

	if (!NoSkillDotRemoveTheLinesSkill(*this, Bound))
	{
		return false;
	}
	FNoSkillDotHit AfterItWent;
	if (!NoSkillDotTickTheBurn(*this, TEXT("the burn's second tick"), Heard, Bound.Caster.Actor, Bound.Touching, AfterItWent))
	{
		return false;
	}
	TestTrue(TEXT("a tick after the skill went names no skill"), AfterItWent.SkillName.IsNone());
	TestTrue(TEXT("a tick after the skill went carries skill tags"), AfterItWent.bHadSkillTags);
	TestTrue(TEXT("a tick after the skill went carries Type.Ranged, the skill's"), AfterItWent.SkillTags.HasTagExact(Ranged));

	FNoSkillDotDeath Kill;
	if (!NoSkillDotKillWithATick(*this, TEXT("the burn's third tick"), Heard, Bound.Caster.Actor, Bound.Touching, Kill))
	{
		return false;
	}
	TestTrue(TEXT("the kill after the skill went names no skill"), Kill.SkillName.IsNone());
	TestTrue(TEXT("the kill after the skill went carries skill tags"), Kill.bHadSkillTags);
	TestTrue(TEXT("the kill after the skill went carries Type.Ranged, which a ranged kill row reads"),
			 Kill.SkillTags.HasTagExact(Ranged));
	return true;
}

// ==========================================================================
// 6. A projectile's burn whose firing skill has gone keeps its tags (J3)
// ==========================================================================

CATACLYSM_TEST(FCataclysmNoSkillDotStaleShotBurnTest,
	"Cataclysm.NoSkillDotTags.AShotWhoseSkillHasGoneKeepsItsTagsOnItsBurnsKill")
{
	using namespace CataclysmNoSkillDotTagsTest;

	// A BURNING SHOT IS FIRED BY A GRANTED SKILL AT A CREATURE 3 M EAST, AND THE SKILL'S GRANT IS REMOVED WHILE IT
	// FLIES, as a trap's rack axe lands after the trap's one-use grant is removed (#2475). Its hit carried the tags it
	// was fired with since #2475; its burn carries them too (#2683, J3).
	FNoSkillDotWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	if (!TestTrue(TEXT("set-up: every tag these tests name exists"), NoSkillDotTagsExist()))
	{
		return false;
	}
	FNoSkillDotCaster Caster = NoSkillDotMakeCaster(World);
	ACataclysmEnemyCharacter* Target = NoSkillDotEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a caster"), Caster.Actor) || !NoSkillDotAllMade(*this, {Target}))
	{
		return false;
	}
	const FGameplayTag Ranged = NoSkillDotTag(TEXT("Type.Ranged"));
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();

	FGameplayAbilitySpecHandle Handle;
	UCataclysmProjectileSkill* Ember = NoSkillDotGrant<UCataclysmProjectileSkill>(
		Caster, ECataclysmAbilitySlot::Special, TEXT("Ember Shot"),
		TEXT("Item.Weapon.Whip, Element.Demonic, Type.Projectile, Type.Ranged"),
		TEXT("Range=12; Radius=0.5; Speed=2000"), Handle);
	if (!TestNotNull(TEXT("set-up: a projectile skill"), Ember))
	{
		return false;
	}
	const FGameplayTagContainer FiredWith = Ember->SkillTags;
	ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
		Caster.Actor, FVector::ZeroVector, FVector(10.0f * M, 0.0f, 0.0f),
		/*InRadiusCm=*/50.0f, /*InSpeed=*/2000.0f, /*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FiredWith, /*bInBurns=*/true, /*InBodyMesh=*/nullptr, /*InFlightSeconds=*/0.0f,
		/*InCritChancePercent=*/0.0f, /*InSkillHealthCostPercent=*/-1.0f, Ember);
	if (!TestNotNull(TEXT("set-up: a burning shot in flight"), Shot))
	{
		return false;
	}

	const TWeakObjectPtr<UCataclysmProjectileSkill> Fired(Ember);
	Caster.AbilitySystem->ClearAbility(Handle);
	Ember = nullptr;
	if (!TestTrue(TEXT("set-up: the removed skill's instance is stale"), Fired.IsStale()))
	{
		return false;
	}

	FNoSkillDotHeard Heard(World);
	if (!TestNotNull(TEXT("set-up: the world's combat events"), Heard.Events))
	{
		return false;
	}
	for (int32 Frame = 0; Frame < 600 && !Shot->bFinished; ++Frame)
	{
		Shot->Step(1.0f / 60.0f);
	}
	if (!TestTrue(TEXT("set-up: the shot finished its flight"), Shot->bFinished)
		|| !TestTrue(TEXT("set-up: and set the creature alight"), UCataclysmSkillEffects::HasTag(Target, Burn))
		|| !TestFalse(TEXT("set-up: without killing it"), UCataclysmSkillEffects::IsDead(Target)))
	{
		return false;
	}

	FNoSkillDotHit Tick;
	if (!NoSkillDotTickTheBurn(*this, TEXT("the shot's burn"), Heard, Caster.Actor, Target, Tick))
	{
		return false;
	}
	TestTrue(TEXT("the burn of a shot whose skill has gone names no skill on its tick"), Tick.SkillName.IsNone());
	TestTrue(TEXT("the burn of a shot whose skill has gone carries skill tags on its tick"), Tick.bHadSkillTags);
	TestTrue(TEXT("the burn of a shot whose skill has gone carries Type.Ranged on its tick"), Tick.SkillTags.HasTagExact(Ranged));

	FNoSkillDotDeath Kill;
	if (!NoSkillDotKillWithATick(*this, TEXT("the shot's burn"), Heard, Caster.Actor, Target, Kill))
	{
		return false;
	}
	TestTrue(TEXT("the kill by the burn of a shot whose skill has gone names no skill"), Kill.SkillName.IsNone());
	TestTrue(TEXT("the kill by the burn of a shot whose skill has gone carries skill tags"), Kill.bHadSkillTags);
	TestTrue(TEXT("the kill by the burn of a shot whose skill has gone carries Type.Ranged, which a ranged kill row reads"),
			 Kill.SkillTags.HasTagExact(Ranged));
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
