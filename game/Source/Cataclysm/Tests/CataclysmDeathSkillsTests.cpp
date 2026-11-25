// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTaunt.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Death weapon skills, slice DS1. Issue #2885: the owner approved the 40 drafted Death skills and the five Death
 * one-handed Crossbow skills on 2026-11-24.
 *
 * THREE MECHANISMS AND THE ROWS THEY MAKE WORK.
 *
 *   A skill row's Cripple slows by the size its row states (`UCataclysmSkillEffects::ApplyNamedEffectOnly`), found while
 *   entering the rows: the size was dropped, and every Cripple a row laid was the Cripple row's own 30. Grave Muster
 *   and Nightshade state 20.
 *
 *   N22, a movement lays its row's `Effect` (`UCataclysmMovementSkill::LayTheRowsEffectsOn`): until this slice only the
 *   Strike, Projectile, Aura and Debuff templates called `ApplyNamedEffectsTo`, so a Movement row's `Effect=Cripple`
 *   was accepted by the generator and then dropped. Each of the move's three blows is shown laying it -- an arrival, a
 *   step of a walked charge and a flicker's arrival -- beside a CONTROL that lays nothing, and the one row that states
 *   an effect and leaves its own ground (the Demonic Wand's Foul Wake) is shown keeping its curse in the ground.
 *
 *   C3, a taunt (`Taunts=1`; `ACataclysmMinion::BeginTaunting`, `RowTauntStep`): a minion a Summon row raises, or a
 *   machine a Deployable row places, taunts every enemy within the row's `Radius` of it as it appears and once a beat
 *   after. Within the radius and not beyond; a Boss refused and the rung below taunted; renewed each beat; ended when
 *   the taunter falls; a placed machine's beat is its row's `Interval`.
 *
 *   The rows, read from the generated table: the twelve this slice writes whole carry their drafted numbers through
 *   the template each is granted, the 33 waiting on code (or, for None Spared, on a ruling) are granted as the
 *   placeholder, and the four rows the two mechanisms make work are cast: Pale Stride, Pale Vault, Parting Shot and
 *   Shallow Graves.
 *
 * THE ROW TESTS READ `DT_WeaponSkills` AND `DT_MinionTypes`, so they need the asset step after the CSVs change
 * (`tools/generate_datatable_assets.py`); each says so in its set-up line when the table is older than the CSV.
 *
 * NO CLOCK RUNS. A taunt's beat is driven by `ACataclysmMinion::RowTauntStep` with the world's seconds moved by hand,
 * as the regeneration step would call it, and a walked charge and a flicker by `AdvanceOneStep` and `FlickerOnce`, as
 * `CataclysmSkillTemplateTests.cpp` drives them.
 *
 * WRITTEN WITHOUT A BUILD. Every figure below is reasoned from the code; the run that proves them is registered in
 * `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmDeathSkillsTest
{
	/** Centimetres in a metre. */
	constexpr float DeathSkillMetre = 100.0f;

	/** What a bare fighter's weapon deals. */
	constexpr float DeathSkillWeaponDamage = 100.0f;

	/** How far a float figure may fall from one it was built to equal. */
	constexpr float DeathSkillAllowance = 0.01f;

	/**
	 * A bare actor a sphere search can find, holding the attribute sets a skill reads and pays from: the shape of
	 * `FScopedFighter` in `CataclysmSkillTemplateTests.cpp`, under its own name so the two files can share a unity build.
	 * It belongs to no team, which reads as hostile to every other body, so it casts and is struck by movement skills;
	 * a minion's team is its summoner's, so the taunt tests use `FDeathSkillCreature` instead.
	 */
	struct FDeathSkillFighter
	{
		FDeathSkillFighter(UWorld* World, const FVector& Where)
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
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), DeathSkillWeaponDamage);
		}

		~FDeathSkillFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		bool Carries(const FGameplayTag& Tag) const
		{
			return Tag.IsValid() && AbilitySystem->HasMatchingGameplayTag(Tag);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * A real creature with a team and a brain, as Brute's Heart's taunt tests build one: a summoner on the players'
	 * side, its enemies on the monsters', a Boss. A minion takes its summoner's team, so the minions of one summoner
	 * are not each other's enemies and taunt only the monsters. THE SETTERS FIRST AND THE ATTRIBUTES AFTER, because a
	 * setter re-runs the creature's starting attributes. A class resource pool of 150 and 1000 mana, so a summon has
	 * room to reserve.
	 */
	struct FDeathSkillCreature
	{
		FDeathSkillCreature(UWorld* World, const FVector& Where, ECataclysmTeam Team, int32 RarityStep = 0)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
			if (RarityStep > 0)
			{
				Actor->SetRarityStep(RarityStep);
			}
			Actor->SetHealth(100000.0f);
			Actor->SetArmour(0.0f);
			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(Actor->GetAbilitySystemComponent());
			check(AbilitySystem);
			if (!AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>())
			{
				AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			}
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
			Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(), 0.0f);
			Set(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}

		~FDeathSkillCreature()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		/** Who taunts this creature now, or null. */
		AActor* TauntedBy() const
		{
			return UCataclysmTaunt::TaunterOf(Actor);
		}

		/** The seconds left on the taunt on this creature, or nought. */
		float TauntLeft() const
		{
			return UCataclysmTaunt::SecondsLeftOn(Actor);
		}

		/** What this creature's brain would attack now. */
		AActor* Chooses() const
		{
			const ACataclysmEnemyController* Brain = Cast<ACataclysmEnemyController>(Actor->GetController());
			return Brain ? Brain->ChooseTarget() : nullptr;
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** Grant a template into a slot at level 100, its cell parsed as the game parses a row's. */
	template <typename T>
	T* GrantDeathSkill(UCataclysmAbilitySystemComponent* AbilitySystem, AActor* Owner, ECataclysmAbilitySlot Slot,
					   const FString& ParamText, const FString& Name)
	{
		const FGameplayAbilitySpecHandle Handle =
			AbilitySystem->GiveAbilityInSlot(T::StaticClass(), Slot, /*Level=*/100, Owner);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		T* Instance = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			Instance->SkillName = Name;
			Instance->Params = UCataclysmSkillShapes::ParseParams(ParamText);
		}
		return Instance;
	}

	/** Activate a granted ability and report whether it started. */
	bool ActivateDeathSkill(UAbilitySystemComponent* AbilitySystem, UGameplayAbility* Ability)
	{
		return AbilitySystem && Ability
			&& AbilitySystem->TryActivateAbility(Ability->GetCurrentAbilitySpecHandle(),
												 /*bAllowRemoteActivation=*/false);
	}

	/** The tag a skill cell writing `Effect=<Name>` lays, or an invalid one this build does not know. */
	FGameplayTag DeathSkillStatus(const TCHAR* Name)
	{
		return UCataclysmSkillShapes::StatusTagFor(Name);
	}

	/** The longest time left on any running effect granting `Granted` on `Actor`, in seconds. */
	float DeathSkillSecondsLeft(const AActor* Actor, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		float Longest = 0.0f;
		if (System && Granted.IsValid())
		{
			for (const float Seconds : System->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Granted))))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
		}
		return Longest;
	}

	/** One generated Death row, by its weapon and the slot it was designed for, or an empty one. */
	FCataclysmWeaponSkill DeathRowOf(const TCHAR* WeaponType, ECataclysmAbilitySlot Slot)
	{
		for (const FCataclysmWeaponSkill& Skill :
			 UCataclysmWeaponSkills::SkillsFor(UCataclysmWeaponSkills::LoadGeneratedTable(), WeaponType, TEXT("Death")))
		{
			if (UCataclysmWeaponSkills::DesignedSlotOf(Skill) == Slot)
			{
				return Skill;
			}
		}
		return FCataclysmWeaponSkill();
	}

	/** A generated row granted to `Owner` in its designed slot at level 100, stamped as a weapon's grant stamps it. */
	template <typename T>
	T* GrantDeathRow(UCataclysmAbilitySystemComponent* AbilitySystem, AActor* Owner, const FCataclysmWeaponSkill& Row)
	{
		const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
			T::StaticClass(), UCataclysmWeaponSkills::DesignedSlotOf(Row), /*Level=*/100, Owner);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		T* Instance = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			UCataclysmWeaponSkills::StampOnto(*Instance, Row);
		}
		return Instance;
	}

	/** Write a minion's health to nothing, which is how anything else dies. */
	void DeathSkillKill(ACataclysmMinion* Minion)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Minion))
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
		}
	}

	/** Destroy every minion still standing at the end of a case. */
	void DeathSkillClear(const TArray<ACataclysmMinion*>& Minions)
	{
		for (ACataclysmMinion* Minion : Minions)
		{
			if (IsValid(Minion))
			{
				Minion->Destroy();
			}
		}
	}

	/** Health now, of anything with an ability system; nought for anything without one. */
	float DeathSkillHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	/** A gameplay tag by its name, or an invalid one this build does not know. */
	FGameplayTag DeathSkillTagNamed(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/**
	 * Put a bare fighter on the players' side, slice DS2: a creature on that side becomes its owner, and
	 * `UCataclysmTeams::TeamOf` walks the owner chain, as burning ground takes the side of whoever left it. So the
	 * minions it raises take the players' team (`ACataclysmMinion::Spawn`) and are its allies rather than its enemies,
	 * which a teamless fighter's would be, and its skills strike a teamless fighter as an enemy.
	 */
	void DeathSkillSideWithThePlayers(const FDeathSkillFighter& Fighter, const FDeathSkillCreature& OnThePlayersSide)
	{
		Fighter.Actor->SetOwner(OnThePlayersSide.Actor);
	}

	/**
	 * A minion of `Summoner`'s, an Imp, at `Where`, told the 3 m explosion radius Summon Imp states, as a summon skill
	 * tells what it raises (`RecordExplosionRadius`). Slice DS2's detonation and command tests place one where they
	 * need it rather than raising it through a skill.
	 */
	ACataclysmMinion* DeathSkillMinionOf(AActor* Summoner, const FVector& Where)
	{
		ACataclysmMinion* Minion =
			ACataclysmMinion::Spawn(Summoner, Where, /*Lifetime=*/20.0f, /*bBurns=*/false, FString(TEXT("Imp")));
		if (Minion)
		{
			Minion->RecordExplosionRadius(3.0f * DeathSkillMetre);
		}
		return Minion;
	}

	/** Step a projectile until it has finished, at sixty steps a second and for at most ten seconds. */
	void DeathSkillFlyToTheEnd(ACataclysmProjectile* Shot)
	{
		for (int32 Steps = 0; IsValid(Shot) && Steps < 600 && !Shot->bFinished; ++Steps)
		{
			Shot->Step(1.0f / 60.0f);
		}
	}

	/** One of the twelve rows this slice writes whole, and the numbers its template must carry. */
	struct FDeathRowDrafted
	{
		const TCHAR* Weapon;
		ECataclysmAbilitySlot Slot;
		const TCHAR* Name;
		ECataclysmSkillShape Shape;
		float RadiusMetres;
		float RangeMetres;
		/** The row's own Damage Percent, or -1 when it states none and takes its slot's. */
		float DamagePercent;
		/** The row's `Effect`, empty for none. */
		const TCHAR* Effect;
		float EffectMagnitude;
		/** The row's `Ailment`, empty for none. */
		const TCHAR* Ailment;
		bool bTaunts;
		float FervourReserve;
	};

	/** One of the 33 rows waiting on code or a ruling, by weapon, slot and name. */
	struct FDeathRowWaiting
	{
		const TCHAR* Weapon;
		ECataclysmAbilitySlot Slot;
		const TCHAR* Name;
	};
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a `using
// namespace` at file scope reaches the other files concatenated with this one.

// ==========================================================================
// N22: a movement lays its row's Effect
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsArrivalLaysEffectTest,
	"Cataclysm.DeathSkills.AMovementRowsEffectIsLaidOnWhatItsArrivalStrikesAndAMoveStatingNoneLaysNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N22 at an arrival: a leap stating `Effect=Cripple; EffectMagnitude=30; EffectDuration=4` (Pale Vault's cell) lands 10 m
 * along +X, where an enemy 1 m beyond the landing is struck and Crippled for the stated 4 s, and one 5 m from the
 * landing, beyond the 3 m radius, is not. THE CONTROL, 100 m away along +Y: the same leap stating no effect strikes the
 * same enemy (its health falls) and lays no Cripple. Before N22 the first half failed: the blow landed and the Cripple
 * was dropped. The slow's size is the next test's subject, so this one asks only that it is laid.
 */
bool FCataclysmDeathSkillsArrivalLaysEffectTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestTrue(TEXT("set-up: the vocabulary has the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillFighter Landed(World, FVector(11.0f * DeathSkillMetre, 0.0f, 0.0f));
	FDeathSkillFighter Beyond(World, FVector(10.0f * DeathSkillMetre, 5.0f * DeathSkillMetre, 0.0f));

	UCataclysmMovementSkill* Leap = GrantDeathSkill<UCataclysmMovementSkill>(
		Caster.AbilitySystem, Caster.Actor, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Leap; Range=10; Radius=3; Effect=Cripple; EffectMagnitude=30; EffectDuration=4"),
		TEXT("A leap that Cripples"));
	if (!TestNotNull(TEXT("set-up: the leap is granted"), Leap))
	{
		return false;
	}

	TestTrue(TEXT("the leap activates"), ActivateDeathSkill(Caster.AbilitySystem, Leap));
	TestEqual(TEXT("it struck the one enemy within 3 m of where it landed"), Leap->EnemiesHit, 1);
	TestTrue(TEXT("and that enemy carries the row's Cripple"), Landed.Carries(Cripple));
	TestEqual(TEXT("for the row's 4 seconds"), DeathSkillSecondsLeft(Landed.Actor, Cripple), 4.0f, DeathSkillAllowance);
	TestFalse(TEXT("the enemy 5 m from the landing, beyond the radius, carries none"), Beyond.Carries(Cripple));

	// THE CONTROL: the same leap stating no effect strikes and lays nothing.
	FDeathSkillFighter Plain(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter PlainLanded(World, FVector(11.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f));
	UCataclysmMovementSkill* PlainLeap = GrantDeathSkill<UCataclysmMovementSkill>(
		Plain.AbilitySystem, Plain.Actor, ECataclysmAbilitySlot::Movement, TEXT("Mode=Leap; Range=10; Radius=3"),
		TEXT("A leap that names no effect"));
	if (!TestNotNull(TEXT("set-up: the control leap is granted"), PlainLeap))
	{
		return false;
	}
	const float PlainBefore = PlainLanded.Health();
	TestTrue(TEXT("control: the plain leap activates"), ActivateDeathSkill(Plain.AbilitySystem, PlainLeap));
	TestTrue(TEXT("control: it struck the enemy at its landing"), PlainLanded.Health() < PlainBefore);
	TestFalse(TEXT("control: and laid no Cripple, since its row names none"), PlainLanded.Carries(Cripple));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsWalkAndFlickerLayEffectTest,
	"Cataclysm.DeathSkills.AWalkedChargeAndAFlickerLayTheirRowsEffectOnEachEnemyTheirBlowsReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N22 at the other two places a move strikes: a step of a walked charge (`AdvanceOneStep`, the Staff's Wraithwalk and
 * the Spear's Pale Rider) and a flicker's arrival (`FlickerOnce`).
 *
 * THE WALK: a charge stating a duration and `Effect=Cripple; EffectMagnitude=30; EffectDuration=3` has struck nothing
 * when it goes up, so the enemy 1 m ahead is not yet Crippled (THE CONTROL); the test moves the walker 1.5 m, as the
 * movement component would, and one step strikes and Cripples it.
 *
 * THE FLICKER, 50 m away along +Y: the first arrival is at once, on the enemy 3 m off, which is Crippled; the enemy 6 m
 * off is not yet (THE CONTROL); the next arrival reaches and Cripples it.
 */
bool FCataclysmDeathSkillsWalkAndFlickerLayEffectTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestTrue(TEXT("set-up: the vocabulary has the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	// ---- The walk ---------------------------------------------------------
	FDeathSkillFighter Walker(World, FVector::ZeroVector);
	FDeathSkillFighter OnThePath(World, FVector(1.0f * DeathSkillMetre, 0.0f, 0.0f));
	UCataclysmMovementSkill* Walk = GrantDeathSkill<UCataclysmMovementSkill>(
		Walker.AbilitySystem, Walker.Actor, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Charge; Range=12; Duration=1; Radius=1.5; Effect=Cripple; EffectMagnitude=30; EffectDuration=3"),
		TEXT("A walked charge that Cripples"));
	if (!TestNotNull(TEXT("set-up: the walked charge is granted"), Walk))
	{
		return false;
	}
	TestTrue(TEXT("the walked charge activates"), ActivateDeathSkill(Walker.AbilitySystem, Walk));
	TestEqual(TEXT("it has taken no step yet"), Walk->StepsTaken, 0);
	TestFalse(TEXT("control: so the enemy ahead is not yet Crippled"), OnThePath.Carries(Cripple));

	Walker.Actor->SetActorLocation(Walker.Actor->GetActorLocation() + FVector(150.0f, 0.0f, 0.0f));
	Walk->AdvanceOneStep();
	TestEqual(TEXT("one step was taken"), Walk->StepsTaken, 1);
	TestEqual(TEXT("and it struck the enemy it passed"), Walk->EnemiesHit, 1);
	TestTrue(TEXT("which now carries the row's Cripple"), OnThePath.Carries(Cripple));
	TestEqual(TEXT("for the row's 3 seconds"), DeathSkillSecondsLeft(OnThePath.Actor, Cripple), 3.0f,
			  DeathSkillAllowance);

	// ---- The flicker -------------------------------------------------------
	FDeathSkillFighter Flickerer(World, FVector(0.0f, 50.0f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter First(World, FVector(3.0f * DeathSkillMetre, 50.0f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter Second(World, FVector(6.0f * DeathSkillMetre, 50.0f * DeathSkillMetre, 0.0f));
	UCataclysmMovementSkill* Flicker = GrantDeathSkill<UCataclysmMovementSkill>(
		Flickerer.AbilitySystem, Flickerer.Actor, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Flicker; Range=10; Duration=4; Interval=0.33; Effect=Cripple; EffectMagnitude=30; EffectDuration=3"),
		TEXT("A flicker that Cripples"));
	if (!TestNotNull(TEXT("set-up: the flicker is granted"), Flicker))
	{
		return false;
	}
	TestTrue(TEXT("the flicker activates"), ActivateDeathSkill(Flickerer.AbilitySystem, Flicker));
	TestEqual(TEXT("it found the two enemies within its 10 m"), Flicker->EnemiesHit, 2);
	TestEqual(TEXT("and arrived once at once"), Flicker->Arrivals, 1);
	TestTrue(TEXT("the enemy it arrived at, 3 m off, carries the row's Cripple"), First.Carries(Cripple));
	TestFalse(TEXT("control: the enemy 6 m off, not yet reached, carries none"), Second.Carries(Cripple));

	Flicker->FlickerOnce();
	TestEqual(TEXT("a second arrival was made"), Flicker->Arrivals, 2);
	TestTrue(TEXT("and the enemy it reached carries the Cripple now"), Second.Carries(Cripple));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsOwnGroundKeepsTheCurseTest,
	"Cataclysm.DeathSkills.AMovementThatLeavesItsOwnGroundLaysItsEffectThroughThatGroundAndNotWithItsBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N22's judgement of 2026-11-24: a Movement row that leaves ground of its own lays its named effect through that
 * ground, as the Demonic Wand's Foul Wake always has ("the ground you fled burns for 6 seconds and strips the Demonic
 * resistance of anything that walks into it"), and its blow lays none. Foul Wake's cell, verbatim: an 8 m blink along
 * +X strikes and sets alight the enemy 1 m past where it arrives, which carries no Shred, because the ground carries it
 * and no ground has swept in a world that does not tick. THE CONTROL, 100 m away along +Y: the same blink stating no
 * ground lays the Shred with its blow.
 */
bool FCataclysmDeathSkillsOwnGroundKeepsTheCurseTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Shred = DeathSkillStatus(TEXT("Shred"));
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestTrue(TEXT("set-up: the vocabulary has the Shred curse and the burn"), Shred.IsValid() && Burn.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Fleer(World, FVector::ZeroVector);
	FDeathSkillFighter AtArrival(World, FVector(9.0f * DeathSkillMetre, 0.0f, 0.0f));
	UCataclysmMovementSkill* FoulWake = GrantDeathSkill<UCataclysmMovementSkill>(
		Fleer.AbilitySystem, Fleer.Actor, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Blink; Range=8; Radius=3.5; Burn=1; GroundRadius=3.5; GroundDuration=6; GroundPercent=16.7; "
			 "Effect=Shred"),
		TEXT("Foul Wake"));
	if (!TestNotNull(TEXT("set-up: Foul Wake's cell is granted"), FoulWake))
	{
		return false;
	}
	TestTrue(TEXT("the blink activates"), ActivateDeathSkill(Fleer.AbilitySystem, FoulWake));
	TestTrue(TEXT("its blow reached the enemy at the end it arrived at, which is alight"), AtArrival.Carries(Burn));
	TestFalse(TEXT("and carries no Shred: the row's ground carries the curse, not its blow"), AtArrival.Carries(Shred));

	// THE CONTROL: no ground of its own, so the blow lays the curse.
	FDeathSkillFighter Bare(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter BareArrival(World, FVector(9.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f));
	UCataclysmMovementSkill* NoGround = GrantDeathSkill<UCataclysmMovementSkill>(
		Bare.AbilitySystem, Bare.Actor, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Blink; Range=8; Radius=3.5; Burn=1; Effect=Shred"), TEXT("Foul Wake with no ground"));
	if (!TestNotNull(TEXT("set-up: the control blink is granted"), NoGround))
	{
		return false;
	}
	TestTrue(TEXT("control: the blink with no ground activates"), ActivateDeathSkill(Bare.AbilitySystem, NoGround));
	TestTrue(TEXT("control: its blow reached the enemy at its arrival"), BareArrival.Carries(Burn));
	TestTrue(TEXT("control: and laid the Shred, since no ground of its own carries it"), BareArrival.Carries(Shred));
	return true;
}

// ==========================================================================
// A skill row's Cripple slows by the size its row states
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsCrippleSizeTest,
	"Cataclysm.DeathSkills.ASkillRowsCrippleSlowsByTheMagnitudeItStatesHeldToTheCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Found while entering the Death rows, 2026-11-24 (issue #2885): `UCataclysmSkillEffects::ApplyNamedEffectOnly` laid a
 * Cripple, whose row moves no stat, as its tag alone and dropped the size, so a skill row's `EffectMagnitude` on a
 * Cripple slowed by the Cripple row's own 30 whatever it said. Grave Muster and Nightshade state 20.
 *
 * Three strikes, each alone 100 m from the others, each with a 4 m ring: one stating 20 slows its enemy by 20 (it keeps
 * 0.8 of its speed); THE CONTROL stating no magnitude states nothing, so its enemy is slowed by the row's 30 (0.7); and
 * one stating 95 is held to the Cripple row's cap of 80 (0.2). Then Grave Muster's own row: one pulse of the aura slows
 * the enemy 5 m off by its 20 and leaves the one 10 m off, beyond its 8 m, alone.
 */
bool FCataclysmDeathSkillsCrippleSizeTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestTrue(TEXT("set-up: the vocabulary has the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	// ---- 20, as Grave Muster and Nightshade state --------------------------
	FDeathSkillFighter Twenty(World, FVector::ZeroVector);
	FDeathSkillFighter SlowedByTwenty(World, FVector(2.0f * DeathSkillMetre, 0.0f, 0.0f));
	UCataclysmStrikeSkill* AtTwenty = GrantDeathSkill<UCataclysmStrikeSkill>(
		Twenty.AbilitySystem, Twenty.Actor, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=4; Angle=360; Effect=Cripple; EffectMagnitude=20; EffectDuration=4"), TEXT("A Cripple of 20"));
	if (!TestNotNull(TEXT("set-up: the strike of 20 is granted"), AtTwenty))
	{
		return false;
	}
	TestTrue(TEXT("the strike of 20 activates"), ActivateDeathSkill(Twenty.AbilitySystem, AtTwenty));
	TestTrue(TEXT("its enemy is Crippled"), SlowedByTwenty.Carries(Cripple));
	TestEqual(TEXT("by the stated 20"), UCataclysmSkillEffects::StatedStrengthOn(SlowedByTwenty.Actor, Cripple), 20.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("so it keeps 0.8 of its speed"), UCataclysmSkillEffects::CrippleMultiplierOn(SlowedByTwenty.Actor),
			  0.8f, DeathSkillAllowance);

	// ---- THE CONTROL: no magnitude stated ----------------------------------
	FDeathSkillFighter Unsized(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter SlowedByTheRow(World, FVector(2.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f));
	UCataclysmStrikeSkill* AtTheRow = GrantDeathSkill<UCataclysmStrikeSkill>(
		Unsized.AbilitySystem, Unsized.Actor, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=4; Angle=360; Effect=Cripple; EffectDuration=4"), TEXT("A Cripple stating no size"));
	if (!TestNotNull(TEXT("set-up: the strike stating no size is granted"), AtTheRow))
	{
		return false;
	}
	TestTrue(TEXT("control: the strike stating no size activates"), ActivateDeathSkill(Unsized.AbilitySystem, AtTheRow));
	TestTrue(TEXT("control: its enemy is Crippled"), SlowedByTheRow.Carries(Cripple));
	TestEqual(TEXT("control: and the application states no size of its own"),
			  UCataclysmSkillEffects::StatedStrengthOn(SlowedByTheRow.Actor, Cripple), -1.0f, DeathSkillAllowance);
	TestEqual(TEXT("control: so the Cripple row's 30 slows it, and it keeps 0.7"),
			  UCataclysmSkillEffects::CrippleMultiplierOn(SlowedByTheRow.Actor), 0.7f, DeathSkillAllowance);

	// ---- 95, held to the row's cap of 80 -----------------------------------
	FDeathSkillFighter Oversized(World, FVector(0.0f, 200.0f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter SlowedToTheCap(World, FVector(2.0f * DeathSkillMetre, 200.0f * DeathSkillMetre, 0.0f));
	UCataclysmStrikeSkill* AtTheCap = GrantDeathSkill<UCataclysmStrikeSkill>(
		Oversized.AbilitySystem, Oversized.Actor, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=4; Angle=360; Effect=Cripple; EffectMagnitude=95; EffectDuration=4"), TEXT("A Cripple of 95"));
	if (!TestNotNull(TEXT("set-up: the strike of 95 is granted"), AtTheCap))
	{
		return false;
	}
	TestTrue(TEXT("the strike of 95 activates"), ActivateDeathSkill(Oversized.AbilitySystem, AtTheCap));
	TestEqual(TEXT("its Cripple is held to the row's cap of 80"),
			  UCataclysmSkillEffects::StatedStrengthOn(SlowedToTheCap.Actor, Cripple), 80.0f, DeathSkillAllowance);

	// ---- Grave Muster's own row --------------------------------------------
	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Greatsword"), ECataclysmAbilitySlot::Support);
	if (!TestEqual(TEXT("set-up: the Death Greatsword's Support row is Grave Muster. If empty, DT_WeaponSkills is older "
						"than the CSV"), Row.Name, FString(TEXT("Grave Muster"))))
	{
		return false;
	}
	FDeathSkillFighter Mustering(World, FVector(0.0f, 300.0f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter WithinMuster(World, FVector(5.0f * DeathSkillMetre, 300.0f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter BeyondMuster(World, FVector(10.0f * DeathSkillMetre, 300.0f * DeathSkillMetre, 0.0f));
	UCataclysmAuraSkill* Muster = GrantDeathRow<UCataclysmAuraSkill>(Mustering.AbilitySystem, Mustering.Actor, Row);
	if (!TestNotNull(TEXT("set-up: Grave Muster is granted"), Muster))
	{
		return false;
	}
	TestTrue(TEXT("Grave Muster activates"), ActivateDeathSkill(Mustering.AbilitySystem, Muster));
	TestEqual(TEXT("one pulse finds the one enemy within its 8 m"), Muster->Pulse(), 1);
	TestEqual(TEXT("which it slows by its row's 20"), UCataclysmSkillEffects::StatedStrengthOn(WithinMuster.Actor, Cripple),
			  20.0f, DeathSkillAllowance);
	TestFalse(TEXT("control: and the enemy 10 m off is not Crippled"), BeyondMuster.Carries(Cripple));
	return true;
}

// ==========================================================================
// C3: a minion or a placed machine its row tells to taunt
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsSummonTauntsWithinItsRadiusTest,
	"Cataclysm.DeathSkills.ATauntingSummonsMinionTauntsTheEnemiesWithinItsRowsRadiusAndNoneBeyond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * C3 through `UCataclysmSummonSkill::SummonOne`: a summon stating `Radius=4; Taunts=1` raises an imp at its summoner's
 * feet (a skill never pressed holds its rift at the origin, where the summoner stands), and as it rises it taunts the
 * enemy 3 m off, inside the row's 4 m, for 3 s; that enemy's brain now chooses the imp. The enemy 7 m off is not
 * taunted. THE CONTROL, in a world of its own so its rift is at the origin too: the same cell without `Taunts` raises
 * an imp that taunts nobody and records no radius.
 */
bool FCataclysmDeathSkillsSummonTauntsWithinItsRadiusTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	UWorld* ControlWorld = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World) || !TestNotNull(TEXT("set-up: a control world"), ControlWorld))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		ControlWorld->DestroyWorld(/*bInformEngineOfWorld=*/false);
	};

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FDeathSkillCreature Near(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Far(World, FVector(0.0f, 7.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Taunting = GrantDeathSkill<UCataclysmSummonSkill>(
		Summoner.AbilitySystem, Summoner.Actor, ECataclysmAbilitySlot::Special,
		TEXT("Count=1; Duration=8; Radius=4; Minions=Imp:1; FervourReserve=3; Taunts=1"), TEXT("A taunting raise"));
	if (!TestNotNull(TEXT("set-up: the taunting summon is granted"), Taunting)
		|| !TestTrue(TEXT("set-up: its cell reads Taunts"), Taunting->Params.bTaunts))
	{
		return false;
	}

	ACataclysmMinion* Imp = Taunting->SummonOne();
	ON_SCOPE_EXIT { DeathSkillClear({Imp}); };
	if (!TestNotNull(TEXT("set-up: an imp rose"), Imp))
	{
		return false;
	}
	TestEqual(TEXT("it taunts within the row's own 4 m"), Imp->TauntRadiusCm, 4.0f * DeathSkillMetre,
			  DeathSkillAllowance);
	TestEqual(TEXT("once a second"), Imp->TauntEverySeconds, ACataclysmMinion::RowTauntEverySeconds,
			  DeathSkillAllowance);
	TestEqual(TEXT("and its first beat, as it rose, landed one taunt"), Imp->TauntsLanded, 1);
	TestTrue(TEXT("on the enemy 3 m off, taunted by the imp"), Near.TauntedBy() == Imp);
	TestEqual(TEXT("for 3 s"), Near.TauntLeft(), ACataclysmMinion::RowTauntSeconds, DeathSkillAllowance);
	TestTrue(TEXT("whose brain now chooses the imp"), Near.Chooses() == Imp);
	TestNull(TEXT("and the enemy 7 m off, beyond the 4 m, is not taunted"), Far.TauntedBy());

	// THE CONTROL: the same cell without `Taunts`.
	FDeathSkillCreature ControlSummoner(ControlWorld, FVector::ZeroVector, ECataclysmTeam::Players);
	FDeathSkillCreature ControlNear(ControlWorld, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f),
									ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Plain = GrantDeathSkill<UCataclysmSummonSkill>(
		ControlSummoner.AbilitySystem, ControlSummoner.Actor, ECataclysmAbilitySlot::Special,
		TEXT("Count=1; Duration=8; Radius=4; Minions=Imp:1; FervourReserve=3"), TEXT("A raise that taunts nothing"));
	ACataclysmMinion* PlainImp = Plain ? Plain->SummonOne() : nullptr;
	ON_SCOPE_EXIT { DeathSkillClear({PlainImp}); };
	if (!TestNotNull(TEXT("set-up: the control imp rose"), PlainImp))
	{
		return false;
	}
	TestEqual(TEXT("control: it records no taunt radius"), PlainImp->TauntRadiusCm, 0.0f, DeathSkillAllowance);
	TestEqual(TEXT("control: and landed no taunt"), PlainImp->TauntsLanded, 0);
	TestNull(TEXT("control: so the enemy 3 m off is not taunted"), ControlNear.TauntedBy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsTauntRefusesABossTest,
	"Cataclysm.DeathSkills.ATauntingMinionLeavesABossAloneAndTauntsTheRungBelow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * C3 and `UCataclysmTaunt`: "Bosses are not fooled." THE RUNG BELOW IS THE CONTROL: a Herald 3 m from the imp is taunted
 * by it, and a Boss 3 m off on the other side is not, so the first beat lands one taunt.
 */
bool FCataclysmDeathSkillsTauntRefusesABossTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FDeathSkillCreature Herald(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters,
							   ACataclysmEnemyCharacter::FirstBossRarityStep - 1);
	FDeathSkillCreature Boss(World, FVector(-3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters,
							 ACataclysmEnemyCharacter::FirstBossRarityStep);
	if (!TestFalse(TEXT("set-up: the Herald is the rung below a boss"), Herald.Actor->IsBoss())
		|| !TestTrue(TEXT("set-up: the Boss is a boss"), Boss.Actor->IsBoss()))
	{
		return false;
	}

	UCataclysmSummonSkill* Taunting = GrantDeathSkill<UCataclysmSummonSkill>(
		Summoner.AbilitySystem, Summoner.Actor, ECataclysmAbilitySlot::Special,
		TEXT("Count=1; Duration=8; Radius=4; Minions=Imp:1; FervourReserve=3; Taunts=1"), TEXT("A taunting raise"));
	ACataclysmMinion* Imp = Taunting ? Taunting->SummonOne() : nullptr;
	ON_SCOPE_EXIT { DeathSkillClear({Imp}); };
	if (!TestNotNull(TEXT("set-up: an imp rose"), Imp))
	{
		return false;
	}
	TestTrue(TEXT("control: the Herald is taunted, by the imp"), Herald.TauntedBy() == Imp);
	TestNull(TEXT("and the Boss is not taunted"), Boss.TauntedBy());
	TestEqual(TEXT("so the first beat landed one taunt"), Imp->TauntsLanded, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsTauntRenewsAndEndsTest,
	"Cataclysm.DeathSkills.ATauntingMinionTauntsAgainEachSecondAndItsTauntEndsWhenItFalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * C3's beat, through the replacement path (`UCataclysmSummonSkill::SummonReplacementAt`, which Press-Ganged, Rekindled
 * and a re-summon take): the first beat as it appears leaves the enemy 3 m off 3 s. Half a second on, a step lands
 * nothing (THE CONTROL: not due) and 2.5 s are left; a second after the first beat it lands again and 3 s are left.
 * Then the minion dies: the taunt ends at once (`UCataclysmTaunt`, Q1) and a corpse beats no more.
 */
bool FCataclysmDeathSkillsTauntRenewsAndEndsTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FDeathSkillCreature Enemy(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Taunting = GrantDeathSkill<UCataclysmSummonSkill>(
		Summoner.AbilitySystem, Summoner.Actor, ECataclysmAbilitySlot::Special,
		TEXT("Count=1; Duration=8; Radius=4; Minions=Imp:1; FervourReserve=3; Taunts=1"), TEXT("A taunting raise"));
	ACataclysmMinion* Imp = Taunting ? Taunting->SummonReplacementAt(FVector::ZeroVector) : nullptr;
	ON_SCOPE_EXIT { DeathSkillClear({Imp}); };
	if (!TestNotNull(TEXT("set-up: a replacement imp rose"), Imp))
	{
		return false;
	}

	TestTrue(TEXT("the replacement taunts the enemy 3 m off as it appears"), Enemy.TauntedBy() == Imp);
	TestEqual(TEXT("for 3 s"), Enemy.TauntLeft(), ACataclysmMinion::RowTauntSeconds, DeathSkillAllowance);

	World->TimeSeconds += 0.5f;
	TestEqual(TEXT("control: half a second on, a step lands nothing, the beat not being due"),
			  ACataclysmMinion::RowTauntStep(Imp), 0);
	TestEqual(TEXT("and 2.5 s are left"), Enemy.TauntLeft(), 2.5f, DeathSkillAllowance);

	World->TimeSeconds += 0.5f;
	TestEqual(TEXT("a second after the first beat, a step lands the taunt again"),
			  ACataclysmMinion::RowTauntStep(Imp), 1);
	TestEqual(TEXT("and its 3 s are whole again"), Enemy.TauntLeft(), ACataclysmMinion::RowTauntSeconds,
			  DeathSkillAllowance);

	DeathSkillKill(Imp);
	if (!TestTrue(TEXT("set-up: the imp died"), UCataclysmSkillEffects::IsDead(Imp)))
	{
		return false;
	}
	TestNull(TEXT("the taunt ended the moment its taunter fell"), Enemy.TauntedBy());
	World->TimeSeconds += 1.0f;
	TestEqual(TEXT("and a corpse beats no more"), ACataclysmMinion::RowTauntStep(Imp), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsDeployableTauntsEachIntervalTest,
	"Cataclysm.DeathSkills.ATauntingDeployableTauntsWithinItsRadiusEachIntervalItsRowStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * C3 through `UCataclysmDeployableSkill::DeployOne`, the Death Crossbow's Effigy's half (the Effigy itself waits on the
 * Siphon, C4): a machine placed by a row stating `Radius=8; Interval=2; Taunts=1` records 8 m and a 2 s beat, and
 * taunts the enemy 3 m off as it is placed. A second on, a step lands nothing (THE CONTROL: its row's beat is 2 s, not
 * a summon's 1 s); two seconds on, it lands again.
 */
bool FCataclysmDeathSkillsDeployableTauntsEachIntervalTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDeathSkillCreature Placer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FDeathSkillCreature Enemy(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmDeployableSkill* Deploy = GrantDeathSkill<UCataclysmDeployableSkill>(
		Placer.AbilitySystem, Placer.Actor, ECataclysmAbilitySlot::Special,
		TEXT("Count=1; Duration=5; Interval=2; Radius=8; Minions=BoltTurret:1; Taunts=1"), TEXT("A taunting machine"));
	ACataclysmMinion* Machine = Deploy ? Deploy->DeployOne(TEXT("BoltTurret")) : nullptr;
	ON_SCOPE_EXIT { DeathSkillClear({Machine}); };
	if (!TestNotNull(TEXT("set-up: a machine was placed"), Machine))
	{
		return false;
	}
	TestEqual(TEXT("it taunts within its row's 8 m"), Machine->TauntRadiusCm, 8.0f * DeathSkillMetre,
			  DeathSkillAllowance);
	TestEqual(TEXT("each 2 s, its row's Interval"), Machine->TauntEverySeconds, 2.0f, DeathSkillAllowance);
	TestTrue(TEXT("and it taunted the enemy 3 m off as it was placed"), Enemy.TauntedBy() == Machine);

	World->TimeSeconds += 1.0f;
	TestEqual(TEXT("control: a second on, a step lands nothing"), ACataclysmMinion::RowTauntStep(Machine), 0);
	World->TimeSeconds += 1.0f;
	TestEqual(TEXT("two seconds on, a step lands the taunt again"), ACataclysmMinion::RowTauntStep(Machine), 1);
	TestEqual(TEXT("whose 3 s are whole"), Enemy.TauntLeft(), ACataclysmMinion::RowTauntSeconds, DeathSkillAllowance);
	return true;
}

// ==========================================================================
// The rows, read from the generated table
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsBuiltRowsCarryTheirNumbersTest,
	"Cataclysm.DeathSkills.EveryRowThisSliceWritesWholeIsGrantedItsTemplateWithItsDraftedNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The twelve Death rows slice DS1 writes whole, each read through `UCataclysmWeaponSkills::SkillsFor` (the function a
 * weapon grants from), granted the template its shape names in its designed slot and stamped as a weapon stamps it,
 * and read back off the template: its name, shape, radius, range, damage percent, named effect and magnitude, ailment,
 * taunt and reserve, as `death_weapon_skills_draft.csv` and `death_crossbow_skills_draft.csv` state them.
 */
bool FCataclysmDeathSkillsBuiltRowsCarryTheirNumbersTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World)
		|| !TestNotNull(TEXT("set-up: the weapon skill table loads"), UCataclysmWeaponSkills::LoadGeneratedTable()))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	using EShape = ECataclysmSkillShape;
	using ESlot = ECataclysmAbilitySlot;
	const FDeathRowDrafted Rows[] = {
		{TEXT("Greatsword"), ESlot::Support, TEXT("Grave Muster"), EShape::Aura, 8.0f, 0.0f, -1.0f, TEXT("Cripple"), 20.0f,
		 TEXT(""), false, 0.0f},
		{TEXT("Greatsword"), ESlot::Special, TEXT("Wailing Host"), EShape::Summon, 3.0f, 10.0f, -1.0f, TEXT(""), 0.0f,
		 TEXT(""), false, 3.0f},
		{TEXT("Greataxe"), ESlot::Special, TEXT("Shallow Graves"), EShape::Summon, 4.0f, 10.0f, -1.0f, TEXT(""), 0.0f,
		 TEXT(""), true, 3.0f},
		{TEXT("Staff"), ESlot::Support, TEXT("Grasping Dead"), EShape::Projectile, 5.0f, 14.0f, -1.0f, TEXT(""), 0.0f,
		 TEXT(""), false, 0.0f},
		{TEXT("Fist"), ESlot::Special, TEXT("Nightshade"), EShape::Projectile, 2.0f, 12.0f, -1.0f, TEXT("Cripple"), 20.0f,
		 TEXT("Poison"), false, 0.0f},
		{TEXT("Whip"), ESlot::Heavy, TEXT("Soul Lash"), EShape::Strike, 5.0f, 0.0f, -1.0f, TEXT(""), 0.0f,
		 TEXT("Necrosis"), false, 0.0f},
		{TEXT("Whip"), ESlot::Special, TEXT("Gallows Chain"), EShape::Projectile, 0.5f, 14.0f, -1.0f, TEXT("Cripple"),
		 30.0f, TEXT(""), false, 0.0f},
		{TEXT("Whip"), ESlot::Movement, TEXT("Soul Herd"), EShape::Movement, 6.0f, 12.0f, -1.0f, TEXT(""), 0.0f, TEXT(""),
		 false, 0.0f},
		{TEXT("Spear"), ESlot::Heavy, TEXT("Reaping Arc"), EShape::Strike, 7.0f, 0.0f, 200.0f, TEXT(""), 0.0f,
		 TEXT("Necrosis"), false, 0.0f},
		{TEXT("Sword"), ESlot::Movement, TEXT("Pale Stride"), EShape::Movement, 1.5f, 12.0f, -1.0f, TEXT("Cripple"), 30.0f,
		 TEXT(""), false, 0.0f},
		{TEXT("Spear"), ESlot::Movement, TEXT("Pale Vault"), EShape::Movement, 3.0f, 10.0f, 150.0f, TEXT("Cripple"), 30.0f,
		 TEXT(""), false, 0.0f},
		{TEXT("Crossbow"), ESlot::Movement, TEXT("Parting Shot"), EShape::Movement, 10.0f, 10.0f, -1.0f, TEXT("Cripple"),
		 30.0f, TEXT(""), false, 0.0f},
	};
	TestEqual(TEXT("set-up: twelve rows are written whole"), static_cast<int32>(UE_ARRAY_COUNT(Rows)), 12);

	FDeathSkillFighter Holder(World, FVector::ZeroVector);
	for (const FDeathRowDrafted& Drafted : Rows)
	{
		const FString Where = FString::Printf(TEXT("%s, Death %s"), Drafted.Name, Drafted.Weapon);
		const FCataclysmWeaponSkill Row = DeathRowOf(Drafted.Weapon, Drafted.Slot);
		if (!TestEqual(*FString::Printf(TEXT("%s: the row is found by its weapon and slot. If empty, DT_WeaponSkills is "
											 "older than the CSV"), *Where), Row.Name, FString(Drafted.Name))
			|| !TestTrue(*FString::Printf(TEXT("%s: its shape is the drafted one"), *Where), Row.Shape == Drafted.Shape))
		{
			continue;
		}

		const TSubclassOf<UCataclysmGameplayAbility> Template = UCataclysmWeaponSkills::TemplateFor(Row.Shape);
		if (!TestTrue(*FString::Printf(TEXT("%s: its shape has a template, so it is granted working"), *Where),
					  Template.Get() != nullptr))
		{
			continue;
		}
		const FGameplayAbilitySpecHandle Handle = Holder.AbilitySystem->GiveAbilityInSlot(
			Template, UCataclysmWeaponSkills::DesignedSlotOf(Row), /*Level=*/100, Holder.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? Holder.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmSkillTemplate* Granted = Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s: the template is granted"), *Where), Granted))
		{
			continue;
		}
		UCataclysmWeaponSkills::StampOnto(*Granted, Row);

		TestEqual(*FString::Printf(TEXT("%s: name"), *Where), Granted->SkillName, FString(Drafted.Name));
		TestEqual(*FString::Printf(TEXT("%s: radius"), *Where), Granted->Params.RadiusCm,
				  Drafted.RadiusMetres * DeathSkillMetre, DeathSkillAllowance);
		TestEqual(*FString::Printf(TEXT("%s: range"), *Where), Granted->Params.RangeCm,
				  Drafted.RangeMetres * DeathSkillMetre, DeathSkillAllowance);
		if (Drafted.DamagePercent >= 0.0f)
		{
			TestEqual(*FString::Printf(TEXT("%s: its own damage percent"), *Where), Granted->GetDamagePercent(),
					  Drafted.DamagePercent, DeathSkillAllowance);
		}
		TestEqual(*FString::Printf(TEXT("%s: named effect"), *Where), Granted->Params.Effect, FString(Drafted.Effect));
		TestEqual(*FString::Printf(TEXT("%s: effect magnitude"), *Where), Granted->Params.EffectMagnitude,
				  Drafted.EffectMagnitude, DeathSkillAllowance);
		TestEqual(*FString::Printf(TEXT("%s: ailment"), *Where), Granted->Params.Ailment, FString(Drafted.Ailment));
		TestTrue(*FString::Printf(TEXT("%s: taunts or not, as drafted"), *Where), Granted->Params.bTaunts == Drafted.bTaunts);
		TestEqual(*FString::Printf(TEXT("%s: Fervour reserved each"), *Where), Granted->Params.FervourReserve,
				  Drafted.FervourReserve, DeathSkillAllowance);
		TestTrue(*FString::Printf(TEXT("%s: the whole cell read"), *Where), Granted->Params.bValid);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsWaitingRowsArePlaceholdersTest,
	"Cataclysm.DeathSkills.EveryDeathRowWaitingOnCodeIsGrantedAsTheDesignedPlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The 33 Death rows slice DS1 writes with their name, description and numbers and NO shape, because the code they need
 * is not built, or, for None Spared, because a ruling on the Corrupted Sentinel's reach is not made
 * (`tools/reapply_death_skills_workbook.py` names what each waits on). Each is offered by its weapon (`SkillsFor`
 * keeps a named row), carries its description, and has no shape, so `TemplateFor` answers null and the weapon slots
 * grant `UCataclysmUndesignedSkill`, which says what it will do and does nothing yet. None Spared carries its numbers
 * already: a critical strike chance of 100, the owner's ruling of 2026-11-24 that its critical strikes are certain.
 * THE CONTROL: across every Death weapon and the aura the table holds 51 named Death rows, of which 18 have a shape:
 * the Dagger's five, the aura and the twelve this slice writes whole.
 */
bool FCataclysmDeathSkillsWaitingRowsArePlaceholdersTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill table loads"), Table))
	{
		return false;
	}

	using ESlot = ECataclysmAbilitySlot;
	const FDeathRowWaiting Waiting[] = {
		{TEXT("Sword"), ESlot::Support, TEXT("Cold Blood")},
		{TEXT("Sword"), ESlot::Heavy, TEXT("Mercy Stroke")},
		{TEXT("Sword"), ESlot::Ultimate, TEXT("Danse Macabre")},
		{TEXT("Sword"), ESlot::Special, TEXT("Death Warrant")},
		{TEXT("Greatsword"), ESlot::Heavy, TEXT("Gravebell")},
		{TEXT("Greatsword"), ESlot::Ultimate, TEXT("Barrow Guard")},
		{TEXT("Greatsword"), ESlot::Movement, TEXT("Barrow Leap")},
		{TEXT("Greataxe"), ESlot::Support, TEXT("Death Rattle")},
		{TEXT("Greataxe"), ESlot::Heavy, TEXT("Grim Swath")},
		{TEXT("Greataxe"), ESlot::Ultimate, TEXT("Exhume")},
		{TEXT("Greataxe"), ESlot::Movement, TEXT("Open Grave")},
		{TEXT("Staff"), ESlot::Heavy, TEXT("Marrow Lance")},
		{TEXT("Staff"), ESlot::Ultimate, TEXT("Bone Choir")},
		{TEXT("Staff"), ESlot::Movement, TEXT("Wraithwalk")},
		{TEXT("Staff"), ESlot::Special, TEXT("Grave Mouth")},
		{TEXT("Wand"), ESlot::Support, TEXT("Soul Snare")},
		{TEXT("Wand"), ESlot::Heavy, TEXT("Hungering Soul")},
		{TEXT("Wand"), ESlot::Ultimate, TEXT("Requiem")},
		{TEXT("Wand"), ESlot::Movement, TEXT("Grave Return")},
		{TEXT("Wand"), ESlot::Special, TEXT("Restless Spirits")},
		{TEXT("Fist"), ESlot::Support, TEXT("Hemlock")},
		{TEXT("Fist"), ESlot::Heavy, TEXT("Gravetouch")},
		{TEXT("Fist"), ESlot::Ultimate, TEXT("Quietus")},
		{TEXT("Fist"), ESlot::Movement, TEXT("Severing Rush")},
		{TEXT("Whip"), ESlot::Support, TEXT("Death Grip")},
		{TEXT("Whip"), ESlot::Ultimate, TEXT("Last Toll")},
		{TEXT("Spear"), ESlot::Support, TEXT("Grim Reach")},
		{TEXT("Spear"), ESlot::Ultimate, TEXT("Pale Rider")},
		{TEXT("Spear"), ESlot::Special, TEXT("Soul Lance")},
		{TEXT("Crossbow"), ESlot::Support, TEXT("Carrion Crows")},
		{TEXT("Crossbow"), ESlot::Heavy, TEXT("Coffin Nail")},
		{TEXT("Crossbow"), ESlot::Special, TEXT("Effigy")},
		{TEXT("Crossbow"), ESlot::Ultimate, TEXT("None Spared")},
	};
	TestEqual(TEXT("set-up: 33 rows wait"), static_cast<int32>(UE_ARRAY_COUNT(Waiting)), 33);

	for (const FDeathRowWaiting& Row : Waiting)
	{
		const FCataclysmWeaponSkill Skill = DeathRowOf(Row.Weapon, Row.Slot);
		const FString Where = FString::Printf(TEXT("%s, Death %s"), Row.Name, Row.Weapon);
		TestEqual(*FString::Printf(TEXT("%s: offered by its weapon under its name. If empty, DT_WeaponSkills is older "
										 "than the CSV"), *Where), Skill.Name, FString(Row.Name));
		TestFalse(*FString::Printf(TEXT("%s: it carries its description"), *Where), Skill.Description.IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s: it has no shape yet"), *Where), Skill.Shape == ECataclysmSkillShape::None);
		TestTrue(*FString::Printf(TEXT("%s: so no template, and the placeholder is granted"), *Where),
				 UCataclysmWeaponSkills::TemplateFor(Skill.Shape).Get() == nullptr);
	}

	int32 Named = 0;
	int32 Shaped = 0;
	for (const FCataclysmWeaponSkill& Skill : UCataclysmWeaponSkills::SkillsOfDamageType(Table, TEXT("Death")))
	{
		++Named;
		Shaped += Skill.Shape != ECataclysmSkillShape::None ? 1 : 0;
	}
	TestEqual(TEXT("control: 51 named Death rows, the 45 of issue #2885, the Dagger's five and the aura"), Named, 51);
	TestEqual(TEXT("control: of which 18 have a shape, the 12 of this slice, the Dagger's five and the aura"), Shaped,
			  18);

	// NONE SPARED'S NUMBERS ARE ENTERED WITH IT, the certain critical strike among them.
	const FCataclysmWeaponSkill NoneSpared = DeathRowOf(TEXT("Crossbow"), ESlot::Ultimate);
	TestEqual(TEXT("None Spared states a critical strike chance of 100"), NoneSpared.CritChancePercent, 100.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("and 35% weapon damage a bolt"), NoneSpared.DamagePercent, 35.0f, DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsPaleStrideTest,
	"Cataclysm.DeathSkills.PaleStrideCripplesTheFirstEnemyItReachesAndGivesTheStrideBackOnAKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Sword's Pale Stride, its own row: "Lunge at the first enemy within 12 meters and cut it as you arrive, Crippling
 * it so it moves and attacks 30% slower for 3 seconds. If the cut kills, the stride is yours again at once."
 *
 * The first enemy 4 m along +X is reached and Crippled at 30 for 3 s; a second enemy beside it, 1 m off the line at
 * the far end, is within the charge's 1.5 m but is not cut, since the row takes one (`MaxTargets=1`). THE CONTROL FOR
 * THE REFUND: the stride that left its enemy standing is waiting on its cooldown, and a stride that cut down an enemy
 * of 1 health, 50 m away along +Y, is not.
 */
bool FCataclysmDeathSkillsPaleStrideTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Sword"), ECataclysmAbilitySlot::Movement);
	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	const FGameplayTag Waiting = UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Movement);
	if (!TestEqual(TEXT("set-up: the Death Sword's Movement row is Pale Stride. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Pale Stride")))
		|| !TestTrue(TEXT("set-up: the Cripple curse and the Movement cooldown tag"), Cripple.IsValid() && Waiting.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillFighter FirstReached(World, FVector(4.0f * DeathSkillMetre, 0.0f, 0.0f));
	FDeathSkillFighter Beside(World, FVector(4.0f * DeathSkillMetre, 1.0f * DeathSkillMetre, 0.0f));
	UCataclysmMovementSkill* Stride = GrantDeathRow<UCataclysmMovementSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Stride))
	{
		return false;
	}
	TestTrue(TEXT("the stride activates"), ActivateDeathSkill(Caster.AbilitySystem, Stride));
	TestEqual(TEXT("it cut one enemy"), Stride->EnemiesHit, 1);
	TestTrue(TEXT("the first enemy it reached is Crippled"), FirstReached.Carries(Cripple));
	TestEqual(TEXT("at the row's 30"), UCataclysmSkillEffects::StatedStrengthOn(FirstReached.Actor, Cripple), 30.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("for the row's 3 seconds"), DeathSkillSecondsLeft(FirstReached.Actor, Cripple), 3.0f,
			  DeathSkillAllowance);
	TestFalse(TEXT("and the enemy beside it is not: the row cuts one"), Beside.Carries(Cripple));
	TestTrue(TEXT("control: the enemy lived, so the stride waits on its cooldown"),
			 Caster.AbilitySystem->HasMatchingGameplayTag(Waiting));

	FDeathSkillFighter Killer(World, FVector(0.0f, 50.0f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter Weak(World, FVector(4.0f * DeathSkillMetre, 50.0f * DeathSkillMetre, 0.0f));
	Weak.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	UCataclysmMovementSkill* KillingStride =
		GrantDeathRow<UCataclysmMovementSkill>(Killer.AbilitySystem, Killer.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted to the second caster"), KillingStride))
	{
		return false;
	}
	TestTrue(TEXT("the second stride activates"), ActivateDeathSkill(Killer.AbilitySystem, KillingStride));
	TestTrue(TEXT("and its cut killed the enemy of 1 health"), UCataclysmSkillEffects::IsDead(Weak.Actor)
		|| Weak.Health() <= 0.0f);
	TestFalse(TEXT("so the stride is given back at once: no cooldown"),
			  Killer.AbilitySystem->HasMatchingGameplayTag(Waiting));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsPaleVaultTest,
	"Cataclysm.DeathSkills.PaleVaultStrikesAndCripplesWithinThreeMetresOfItsLandingAndNotAtFour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Spear's Pale Vault, its own row: "Vault on the spear to a point up to 10 meters away and land point first,
 * striking every enemy within 3 meters for 150% weapon damage and Crippling it, moving and attacking 30% slower for 4
 * seconds." The vault lands 10 m along +X; the enemy 2 m past the landing is struck and Crippled at 30 for 4 s, and
 * THE CONTROL 4 m past it, beyond the 3 m, is neither struck nor Crippled.
 */
bool FCataclysmDeathSkillsPaleVaultTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Spear"), ECataclysmAbilitySlot::Movement);
	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestEqual(TEXT("set-up: the Death Spear's Movement row is Pale Vault. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Pale Vault")))
		|| !TestTrue(TEXT("set-up: the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillFighter Within(World, FVector(12.0f * DeathSkillMetre, 0.0f, 0.0f));
	FDeathSkillFighter Outside(World, FVector(14.0f * DeathSkillMetre, 0.0f, 0.0f));
	UCataclysmMovementSkill* Vault = GrantDeathRow<UCataclysmMovementSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Vault))
	{
		return false;
	}
	const float WithinBefore = Within.Health();
	const float OutsideBefore = Outside.Health();
	TestTrue(TEXT("the vault activates"), ActivateDeathSkill(Caster.AbilitySystem, Vault));
	TestEqual(TEXT("it struck one enemy"), Vault->EnemiesHit, 1);
	TestTrue(TEXT("the enemy 2 m past the landing was struck"), Within.Health() < WithinBefore);
	TestTrue(TEXT("and is Crippled"), Within.Carries(Cripple));
	TestEqual(TEXT("at the row's 30"), UCataclysmSkillEffects::StatedStrengthOn(Within.Actor, Cripple), 30.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("for the row's 4 seconds"), DeathSkillSecondsLeft(Within.Actor, Cripple), 4.0f, DeathSkillAllowance);
	TestEqual(TEXT("control: the enemy 4 m past it lost nothing"), Outside.Health(), OutsideBefore, DeathSkillAllowance);
	TestFalse(TEXT("control: and is not Crippled"), Outside.Carries(Cripple));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsPartingShotTest,
	"Cataclysm.DeathSkills.PartingShotCripplesTheNearestEnemyToItsLandingAndNoOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Death Crossbow's Parting Shot, its own row: "Leap to a point up to 10 meters away and loose a bolt as you land at
 * the nearest enemy within 10 meters, Crippling it so it moves and attacks 30% slower for 3 seconds." The leap lands 10
 * m along +X; the enemy 3 m from the landing is shot and Crippled at 30 for 3 s, and THE CONTROL, an enemy 6 m from the
 * landing and so also within the 10 m, is not, since the row takes the nearest one (`MaxTargets=1`).
 */
bool FCataclysmDeathSkillsPartingShotTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Crossbow"), ECataclysmAbilitySlot::Movement);
	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestEqual(TEXT("set-up: the Death Crossbow's Movement row is Parting Shot. If empty, DT_WeaponSkills is older "
						"than the CSV"), Row.Name, FString(TEXT("Parting Shot")))
		|| !TestTrue(TEXT("set-up: the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillFighter Nearest(World, FVector(13.0f * DeathSkillMetre, 0.0f, 0.0f));
	FDeathSkillFighter Further(World, FVector(10.0f * DeathSkillMetre, 6.0f * DeathSkillMetre, 0.0f));
	UCataclysmMovementSkill* Shot = GrantDeathRow<UCataclysmMovementSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Shot))
	{
		return false;
	}
	TestTrue(TEXT("the leap activates"), ActivateDeathSkill(Caster.AbilitySystem, Shot));
	TestEqual(TEXT("it shot one enemy"), Shot->EnemiesHit, 1);
	TestTrue(TEXT("the enemy 3 m from the landing is Crippled"), Nearest.Carries(Cripple));
	TestEqual(TEXT("at the row's 30"), UCataclysmSkillEffects::StatedStrengthOn(Nearest.Actor, Cripple), 30.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("for the row's 3 seconds"), DeathSkillSecondsLeft(Nearest.Actor, Cripple), 3.0f, DeathSkillAllowance);
	TestFalse(TEXT("control: the enemy 6 m from it is not"), Further.Carries(Cripple));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsShallowGravesTest,
	"Cataclysm.DeathSkills.ShallowGravesRaisesFourHusksAndTheEnemyNearThemIsTauntedByOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Greataxe's Shallow Graves, its own row: "Four husks claw out of shallow graves ... Every enemy within 4 meters of a
 * husk turns on it and attacks it until it falls; Bosses are not fooled. Each lasts 8 seconds and reserves Fervour while
 * it stands." Raised as a press raises them, one `SummonOne` for each of the row's `Count`, at a summoner that has not
 * aimed, so at its feet. Four husks of the `Husk` kind, each taunting within the row's 4 m and holding its 3 Fervour;
 * the enemy 3 m off is taunted by one of them and by no other, since a second taunter is refused while the first
 * holds it, so the four first beats land one taunt between them. THE CONTROL: the enemy 6 m off is not taunted.
 */
bool FCataclysmDeathSkillsShallowGravesTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Greataxe"), ECataclysmAbilitySlot::Special);
	if (!TestEqual(TEXT("set-up: the Death Greataxe's Special row is Shallow Graves. If empty, DT_WeaponSkills is older "
						"than the CSV"), Row.Name, FString(TEXT("Shallow Graves")))
		|| !TestEqual(TEXT("set-up: it raises four"), Row.Params.Count, 4))
	{
		return false;
	}

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FDeathSkillCreature Near(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Far(World, FVector(0.0f, 6.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Graves = GrantDeathRow<UCataclysmSummonSkill>(Summoner.AbilitySystem, Summoner.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Graves))
	{
		return false;
	}

	TArray<ACataclysmMinion*> Husks;
	ON_SCOPE_EXIT { DeathSkillClear(Husks); };
	for (int32 Index = 0; Index < Row.Params.Count; ++Index)
	{
		Husks.Add(Graves->SummonOne());
	}

	int32 Landed = 0;
	bool bNearTauntedByAHusk = false;
	for (const ACataclysmMinion* Husk : Husks)
	{
		if (!TestNotNull(TEXT("a husk rose. If not, DT_MinionTypes is older than the CSV"), Husk))
		{
			return false;
		}
		TestEqual(TEXT("of the Husk kind"), Husk->TypeName, FString(TEXT("Husk")));
		TestEqual(TEXT("taunting within the row's 4 m"), Husk->TauntRadiusCm, 4.0f * DeathSkillMetre,
				  DeathSkillAllowance);
		TestTrue(TEXT("and holding some of its summoner's Fervour"), Husk->ReservedFervour > 0.0f);
		Landed += Husk->TauntsLanded;
		bNearTauntedByAHusk = bNearTauntedByAHusk || Near.TauntedBy() == Husk;
	}
	TestEqual(TEXT("four husks stand"), Graves->LivingMinionCount(), 4);
	TestTrue(TEXT("the enemy 3 m off is taunted by one of the husks"), bNearTauntedByAHusk);
	TestEqual(TEXT("and by that one alone: the four first beats landed one taunt"), Landed, 1);
	TestNull(TEXT("control: the enemy 6 m off is not taunted"), Far.TauntedBy());
	return true;
}

// ==========================================================================
// Slice DS2, change A. N1: a raise that stands until destroyed. N2: a raise that strikes as it rises. N16: Bleed on a
// row, which is the generator's alone, so its test is the row that states it.
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsPermanentRaiseTest,
	"Cataclysm.DeathSkills.APermanentRaiseStandsWithNoLifespanAndARaiseWithoutTheKeyLastsItsDuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N1 (`Permanent=1`, read in `UCataclysmSummonSkill::RecordTheSkillOn`): a summon stating it raises an imp with no
 * lifespan, which the engine reads as one that never expires, and a replacement of it (`SummonReplacementAt`, the route
 * Press-Ganged, a re-summon and a spawn at a kill take) has none either; the raise holds its reserve while it stands.
 * THE CONTROL: the same cell stating `Duration=8` and no `Permanent` raises an imp with its 8 seconds to live.
 */
bool FCataclysmDeathSkillsPermanentRaiseTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	UCataclysmSummonSkill* Lasting = GrantDeathSkill<UCataclysmSummonSkill>(
		Summoner.AbilitySystem, Summoner.Actor, ECataclysmAbilitySlot::Ultimate,
		TEXT("Count=1; Radius=3; Minions=Imp:1; FervourReserve=3; Permanent=1"), TEXT("A raise that stays"));
	UCataclysmSummonSkill* Passing = GrantDeathSkill<UCataclysmSummonSkill>(
		Summoner.AbilitySystem, Summoner.Actor, ECataclysmAbilitySlot::Special,
		TEXT("Count=1; Duration=8; Radius=3; Minions=Imp:1; FervourReserve=3"), TEXT("A raise that passes"));
	if (!TestNotNull(TEXT("set-up: the permanent raise is granted"), Lasting)
		|| !TestNotNull(TEXT("set-up: the control raise is granted"), Passing)
		|| !TestTrue(TEXT("set-up: the first cell reads Permanent"), Lasting->Params.bPermanent)
		|| !TestFalse(TEXT("set-up: the control's does not"), Passing->Params.bPermanent))
	{
		return false;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsRisingBurstTest,
	"Cataclysm.DeathSkills.ARaiseStatingItsDamageStrikesAndCripplesTheRingItRisesInAndOneStatingNoneStrikesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N2 as a ring (`UCataclysmSummonSkill::StrikeAsItRises`): a summon whose row states 300% of its own (stamped as a
 * weapon stamps a row's figure, `DamagePercentOverride`) and `Effect=Cripple; EffectMagnitude=30; EffectDuration=4`
 * strikes every enemy within its 5 m of where it rises -- a skill never pressed holds its rift at the origin, where its
 * summoner stands -- so the enemy 3 m off loses health and is Crippled at 30 for 4 s, and the enemy 7 m off is neither.
 * THE CONTROL, in a world of its own: the same cell with no damage of its own does not strike as it rises.
 */
bool FCataclysmDeathSkillsRisingBurstTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	UWorld* ControlWorld = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World) || !TestNotNull(TEXT("set-up: a control world"), ControlWorld))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		ControlWorld->DestroyWorld(/*bInformEngineOfWorld=*/false);
	};

	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestTrue(TEXT("set-up: the vocabulary has the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}
	const TCHAR* const Cell =
		TEXT("Count=1; Radius=5; Minions=Imp:1; FervourReserve=3; Effect=Cripple; EffectMagnitude=30; EffectDuration=4");

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Summoner.Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), DeathSkillWeaponDamage);
	FDeathSkillCreature Near(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Far(World, FVector(0.0f, 7.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Rising = GrantDeathSkill<UCataclysmSummonSkill>(
		Summoner.AbilitySystem, Summoner.Actor, ECataclysmAbilitySlot::Ultimate, Cell, TEXT("A raise that strikes"));
	if (!TestNotNull(TEXT("set-up: the raise is granted"), Rising))
	{
		return false;
	}
	Rising->DamagePercentOverride = 300.0f;
	if (!TestTrue(TEXT("set-up: a row stating its own damage strikes as it rises"), Rising->StrikesAsItRises()))
	{
		return false;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsHewingRaiseTest,
	"Cataclysm.DeathSkills.ARaiseThatHewsOneEnemyStrikesTheNearestInReachAndRisesAtItsFeet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N2 as one blow (`MaxTargets=1`, Exhume's cell): of the enemies within the row's 4 m of the summoner, the one nearest
 * where it points (with no cursor, its own place) is struck -- the enemy 3 m off, and not the one 3.5 m off on the other
 * side -- and what rises rises at its feet. The enemy 6 m off, beyond the range, is not struck. THE CONTROL, in a world
 * of its own with no enemy within reach: the blow strikes nobody and the raise rises where its rift was, a judgement of
 * slice DS2 (a skill never pressed holds its rift at the origin).
 */
bool FCataclysmDeathSkillsHewingRaiseTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	UWorld* ControlWorld = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World) || !TestNotNull(TEXT("set-up: a control world"), ControlWorld))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		ControlWorld->DestroyWorld(/*bInformEngineOfWorld=*/false);
	};

	const TCHAR* const Cell = TEXT("Range=4; Radius=2.5; MaxTargets=1; Count=1; Minions=Imp:1; FervourReserve=3");

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Summoner.Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), DeathSkillWeaponDamage);
	FDeathSkillCreature Hewn(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Other(World, FVector(-3.5f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Beyond(World, FVector(0.0f, 6.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Hewing = GrantDeathSkill<UCataclysmSummonSkill>(
		Summoner.AbilitySystem, Summoner.Actor, ECataclysmAbilitySlot::Ultimate, Cell, TEXT("A raise that hews"));
	if (!TestNotNull(TEXT("set-up: the raise is granted"), Hewing))
	{
		return false;
	}
	Hewing->DamagePercentOverride = 400.0f;

	const float HewnBefore = DeathSkillHealthOf(Hewn.Actor);
	const float OtherBefore = DeathSkillHealthOf(Other.Actor);
	const float BeyondBefore = DeathSkillHealthOf(Beyond.Actor);
	TestEqual(TEXT("it hewed one enemy"), Hewing->StrikeAsItRises(), 1);
	TestTrue(TEXT("the nearest within 4 m, 3 m off, lost health"), DeathSkillHealthOf(Hewn.Actor) < HewnBefore);
	TestEqual(TEXT("the one 3.5 m off on the other side lost nothing: the row hews one"), DeathSkillHealthOf(Other.Actor),
			  OtherBefore, DeathSkillAllowance);
	TestEqual(TEXT("and the one 6 m off, beyond the range, nothing"), DeathSkillHealthOf(Beyond.Actor), BeyondBefore,
			  DeathSkillAllowance);

	ACataclysmMinion* Risen = Hewing->SummonOne();
	ON_SCOPE_EXIT { DeathSkillClear({Risen}); };
	if (TestNotNull(TEXT("something rose"), Risen))
	{
		TestTrue(TEXT("at the hewn enemy's feet"),
				 FVector::Dist2D(Risen->GetActorLocation(), Hewn.Actor->GetActorLocation()) < 1.0f);
	}

#endif // WITH_AUTOMATION_TESTS
