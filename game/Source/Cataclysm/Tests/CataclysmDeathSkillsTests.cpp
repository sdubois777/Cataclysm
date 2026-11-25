// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBuriedWeapon.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmRisingCurse.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
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
// For every shot in the world, which a rack's spirits are. Slice DS3.
#include "EngineUtils.h"
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
		/** Slice DS2's keys: N1 `Permanent`, N3 `Detonates`, N4 `CommandStrike` and N11 `MinionsLayEffect`. */
		bool bPermanent = false;
		bool bDetonates = false;
		bool bCommandStrike = false;
		bool bMinionsLayEffect = false;
	};

	/** One of the 33 rows waiting on code or a ruling, by weapon, slot and name. */
	struct FDeathRowWaiting
	{
		const TCHAR* Weapon;
		ECataclysmAbilitySlot Slot;
		const TCHAR* Name;
	};

	/**
	 * Slice DS3's keys on one of the nine rows it writes whole, read back off the generated row: N6 `KillFervour`, N7
	 * `ScalingSource=Fervour` with its rate and ceiling, N8 `HoldsFervour`, N10 `DurationPer` and `MaxDuration`, and
	 * N5 `OnDeath=Rise` with `RisesAs`, beside the on-death keys the rows already had.
	 */
	struct FDeathRowDS3Keys
	{
		const TCHAR* Weapon;
		ECataclysmAbilitySlot Slot;
		float KillFervour;
		const TCHAR* ScalingSource;
		float MoreDamagePer;
		float MaxDamagePercent;
		float DurationPer;
		float MaxDuration;
		bool bHoldsFervour;
		const TCHAR* OnDeath;
		float OnDeathRangeMetres;
		const TCHAR* RisesAs;
	};

	/** Step every shot in the world until it has finished, as `DeathSkillFlyToTheEnd` steps one. Slice DS3. */
	void DeathSkillFlyEveryShot(UWorld* World)
	{
		TArray<ACataclysmProjectile*> Shots;
		for (TActorIterator<ACataclysmProjectile> It(World); It; ++It)
		{
			Shots.Add(*It);
		}
		for (ACataclysmProjectile* Shot : Shots)
		{
			DeathSkillFlyToTheEnd(Shot);
		}
	}

	/** The Fervour anything with an ability system holds now; nought for anything without one. Slice DS3. */
	float DeathSkillFervourOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute())
					  : 0.0f;
	}

	/** Write anything's Fervour, by its base, which is how a test fills or empties a pool. Slice DS3. */
	void DeathSkillSetFervour(const AActor* Actor, float Fervour)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor))
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Fervour);
		}
	}

	/**
	 * Kill a creature by the route the game takes, its health written to nought, which runs
	 * `ACataclysmEnemyCharacter::HandleDeath` and so `UCataclysmSkillEffects::MarkDead`. Slice DS3: a death nobody's blow
	 * dealt, as a burn or a rule deals one.
	 */
	void DeathSkillKillCreature(const FDeathSkillCreature& Creature)
	{
		Creature.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
	}

	/** Health a creature has lost from the 100000 `FDeathSkillCreature` gives it. Slice DS3. */
	float DeathSkillHealthLost(const FDeathSkillCreature& Creature)
	{
		return 100000.0f - DeathSkillHealthOf(Creature.Actor);
	}
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
 * The Death rows written whole, each read through `UCataclysmWeaponSkills::SkillsFor` (the function a weapon grants
 * from), granted the template its shape names in its designed slot and stamped as a weapon stamps it, and read back off
 * the template: its name, shape, radius, range, damage percent, named effect and magnitude, ailment, taunt and reserve,
 * and slice DS2's four keys, as `death_weapon_skills_draft.csv` and `death_crossbow_skills_draft.csv` state them.
 *
 * TWELVE FROM SLICE DS1 AND TEN FROM SLICE DS2: Gravebell, Barrow Guard, Barrow Leap, Exhume, Open Grave, Marrow Lance,
 * Bone Choir, Grave Return, Severing Rush, and None Spared, which waited on a ruling and not on code. The four ranges the
 * coordinating session's ruling of 2026-11-24 brought down to 14 m are read here as 14: None Spared's among them, and
 * Coffin Nail's, Soul Lance's and Pale Rider's when their code lands.
 *
 * AND NINE FROM SLICE DS3, which built N5, N6, N7, N8 and N10: Cold Blood, Danse Macabre, Death Warrant, Death Rattle,
 * Grim Swath, Hungering Soul, Restless Spirits, Last Toll and Pale Rider (at its 14 m), with their new keys read off
 * the generated row: `KillFervour`, `ScalingSource=Fervour` with its rate and ceiling, `HoldsFervour`, `DurationPer` and
 * `MaxDuration`, and `OnDeath=Rise` with `RisesAs`.
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
		// SLICE DS2.
		{TEXT("Greatsword"), ESlot::Heavy, TEXT("Gravebell"), EShape::Strike, 4.5f, 0.0f, -1.0f, TEXT(""), 0.0f,
		 TEXT("Necrosis"), false, 0.0f, /*bPermanent=*/false, /*bDetonates=*/true, /*bCommandStrike=*/true},
		{TEXT("Greatsword"), ESlot::Ultimate, TEXT("Barrow Guard"), EShape::Summon, 5.0f, 0.0f, 300.0f, TEXT("Cripple"),
		 30.0f, TEXT(""), true, 10.0f, /*bPermanent=*/true},
		{TEXT("Greatsword"), ESlot::Movement, TEXT("Barrow Leap"), EShape::Movement, 4.0f, 10.0f, -1.0f, TEXT("Cripple"),
		 30.0f, TEXT(""), false, 0.0f, /*bPermanent=*/false, /*bDetonates=*/true},
		{TEXT("Greataxe"), ESlot::Ultimate, TEXT("Exhume"), EShape::Summon, 2.5f, 4.0f, 400.0f, TEXT(""), 0.0f, TEXT(""),
		 false, 15.0f, /*bPermanent=*/true},
		{TEXT("Greataxe"), ESlot::Movement, TEXT("Open Grave"), EShape::Movement, 5.0f, 9.0f, -1.0f, TEXT(""), 0.0f,
		 TEXT(""), false, 0.0f, /*bPermanent=*/false, /*bDetonates=*/true},
		{TEXT("Staff"), ESlot::Heavy, TEXT("Marrow Lance"), EShape::Projectile, 1.2f, 14.0f, -1.0f, TEXT(""), 0.0f,
		 TEXT("Necrosis"), false, 0.0f, /*bPermanent=*/false, /*bDetonates=*/true},
		{TEXT("Staff"), ESlot::Ultimate, TEXT("Bone Choir"), EShape::Summon, 5.0f, 12.0f, 300.0f, TEXT("Cripple"), 20.0f,
		 TEXT(""), false, 10.0f, /*bPermanent=*/true, /*bDetonates=*/false, /*bCommandStrike=*/false,
		 /*bMinionsLayEffect=*/true},
		{TEXT("Wand"), ESlot::Movement, TEXT("Grave Return"), EShape::Movement, 3.0f, 14.0f, -1.0f, TEXT(""), 0.0f,
		 TEXT(""), false, 0.0f, /*bPermanent=*/false, /*bDetonates=*/true},
		{TEXT("Fist"), ESlot::Movement, TEXT("Severing Rush"), EShape::Movement, 2.0f, 12.0f, -1.0f, TEXT(""), 0.0f,
		 TEXT("Bleed"), false, 0.0f},
		{TEXT("Crossbow"), ESlot::Ultimate, TEXT("None Spared"), EShape::Projectile, 0.4f, 14.0f, 35.0f, TEXT(""), 0.0f,
		 TEXT(""), false, 0.0f},
		// SLICE DS3.
		{TEXT("Sword"), ESlot::Support, TEXT("Cold Blood"), EShape::SelfBuff, 0.0f, 0.0f, -1.0f, TEXT(""), 0.0f, TEXT(""),
		 false, 0.0f},
		{TEXT("Sword"), ESlot::Ultimate, TEXT("Danse Macabre"), EShape::Movement, 0.0f, 10.0f, 30.0f, TEXT(""), 0.0f,
		 TEXT(""), false, 0.0f},
		{TEXT("Sword"), ESlot::Special, TEXT("Death Warrant"), EShape::Debuff, 1.5f, 15.0f, -1.0f, TEXT("Cripple"), 30.0f,
		 TEXT(""), false, 0.0f},
		{TEXT("Greataxe"), ESlot::Support, TEXT("Death Rattle"), EShape::Debuff, 5.0f, 12.0f, -1.0f, TEXT("Cripple"),
		 20.0f, TEXT(""), false, 0.0f},
		{TEXT("Greataxe"), ESlot::Heavy, TEXT("Grim Swath"), EShape::Strike, 4.0f, 0.0f, 200.0f, TEXT(""), 0.0f, TEXT(""),
		 false, 0.0f},
		{TEXT("Wand"), ESlot::Heavy, TEXT("Hungering Soul"), EShape::Projectile, 1.0f, 14.0f, -1.0f, TEXT(""), 0.0f,
		 TEXT("Necrosis"), false, 0.0f},
		{TEXT("Wand"), ESlot::Special, TEXT("Restless Spirits"), EShape::Projectile, 1.0f, 12.0f, 25.0f, TEXT(""), 0.0f,
		 TEXT("Necrosis"), false, 0.0f},
		{TEXT("Whip"), ESlot::Ultimate, TEXT("Last Toll"), EShape::Strike, 12.0f, 0.0f, 350.0f, TEXT(""), 0.0f,
		 TEXT("Necrosis"), false, 0.0f},
		{TEXT("Spear"), ESlot::Ultimate, TEXT("Pale Rider"), EShape::Movement, 2.5f, 14.0f, 300.0f, TEXT("Cripple"), 50.0f,
		 TEXT(""), false, 0.0f},
	};
	TestEqual(TEXT("set-up: thirty-one rows are written whole, twelve by slice DS1, ten by DS2 and nine by DS3"),
			  static_cast<int32>(UE_ARRAY_COUNT(Rows)), 31);

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
		TestTrue(*FString::Printf(TEXT("%s: permanent or not, as drafted"), *Where),
				 Granted->Params.bPermanent == Drafted.bPermanent);
		TestTrue(*FString::Printf(TEXT("%s: detonates or not, as drafted"), *Where),
				 Granted->Params.bDetonates == Drafted.bDetonates);
		TestTrue(*FString::Printf(TEXT("%s: commands a strike or not, as drafted"), *Where),
				 Granted->Params.bCommandStrike == Drafted.bCommandStrike);
		TestTrue(*FString::Printf(TEXT("%s: its minions lay its effect or not, as drafted"), *Where),
				 Granted->Params.bMinionsLayEffect == Drafted.bMinionsLayEffect);
		TestTrue(*FString::Printf(TEXT("%s: the whole cell read"), *Where), Granted->Params.bValid);
	}

	// NONE SPARED'S CERTAIN CRITICAL STRIKE, the owner's ruling of 2026-11-24, and its 14 m since the coordinating
	// session's ruling of the same day: a crossbow may not out-range the Corrupted Sentinel's 14 m reach (issue #352).
	const FCataclysmWeaponSkill NoneSpared = DeathRowOf(TEXT("Crossbow"), ESlot::Ultimate);
	TestEqual(TEXT("None Spared states a critical strike chance of 100"), NoneSpared.CritChancePercent, 100.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("and twelve bolts"), NoneSpared.Params.Count, 12);
	TestEqual(TEXT("each at the next enemy in turn"), NoneSpared.Params.TargetMode, FString(TEXT("All")));

	// SLICE DS3'S KEYS ON THE NINE ROWS IT WRITES WHOLE, as `death_weapon_skills_draft.csv` states them, Death
	// Rattle's `RisesAs=Husk` slice DS3's own (see `docs/DECISIONS.md`).
	const FDeathRowDS3Keys Keyed[] = {
		{TEXT("Sword"), ESlot::Support, 0.0f, TEXT("Fervour"), 0.25f, 0.0f, 0.0f, 0.0f, true, TEXT(""), 0.0f, TEXT("")},
		{TEXT("Sword"), ESlot::Ultimate, 5.0f, TEXT("Kill"), 0.0f, 0.0f, 0.5f, 8.0f, false, TEXT(""), 0.0f, TEXT("")},
		{TEXT("Sword"), ESlot::Special, 10.0f, TEXT(""), 0.0f, 0.0f, 0.0f, 0.0f, false, TEXT("SpreadDebuff"), 10.0f,
		 TEXT("")},
		{TEXT("Greataxe"), ESlot::Support, 0.0f, TEXT(""), 0.0f, 0.0f, 0.0f, 0.0f, false, TEXT("Rise"), 0.0f,
		 TEXT("Husk")},
		{TEXT("Greataxe"), ESlot::Heavy, 2.0f, TEXT("Fervour"), 0.5f, 350.0f, 0.0f, 0.0f, false, TEXT(""), 0.0f,
		 TEXT("")},
		{TEXT("Wand"), ESlot::Heavy, 2.0f, TEXT(""), 0.0f, 0.0f, 0.0f, 0.0f, false, TEXT("Leap"), 8.0f, TEXT("")},
		{TEXT("Wand"), ESlot::Special, 1.0f, TEXT(""), 0.0f, 0.0f, 0.0f, 0.0f, false, TEXT(""), 0.0f, TEXT("")},
		{TEXT("Whip"), ESlot::Ultimate, 5.0f, TEXT(""), 0.0f, 0.0f, 0.0f, 0.0f, false, TEXT(""), 0.0f, TEXT("")},
		{TEXT("Spear"), ESlot::Ultimate, 5.0f, TEXT(""), 0.0f, 0.0f, 0.0f, 0.0f, false, TEXT(""), 0.0f, TEXT("")},
	};
	for (const FDeathRowDS3Keys& Drafted : Keyed)
	{
		const FCataclysmWeaponSkill Row = DeathRowOf(Drafted.Weapon, Drafted.Slot);
		const FString Where = FString::Printf(TEXT("%s, Death %s"), *Row.Name, Drafted.Weapon);
		TestEqual(*FString::Printf(TEXT("%s: Fervour for a kill"), *Where), Row.Params.KillFervour, Drafted.KillFervour,
				  DeathSkillAllowance);
		TestEqual(*FString::Printf(TEXT("%s: what it scales by"), *Where), Row.Params.ScalingSource,
				  FString(Drafted.ScalingSource));
		TestEqual(*FString::Printf(TEXT("%s: more damage a unit"), *Where), Row.Params.MoreDamagePer,
				  Drafted.MoreDamagePer, DeathSkillAllowance);
		TestEqual(*FString::Printf(TEXT("%s: its damage ceiling"), *Where), Row.Params.MaxDamagePercent,
				  Drafted.MaxDamagePercent, DeathSkillAllowance);
		TestEqual(*FString::Printf(TEXT("%s: seconds a death adds"), *Where), Row.Params.DurationPer, Drafted.DurationPer,
				  DeathSkillAllowance);
		TestEqual(*FString::Printf(TEXT("%s: its time's ceiling"), *Where), Row.Params.MaxDuration, Drafted.MaxDuration,
				  DeathSkillAllowance);
		TestTrue(*FString::Printf(TEXT("%s: holds Fervour or not, as drafted"), *Where),
				 Row.Params.bHoldsFervour == Drafted.bHoldsFervour);
		TestEqual(*FString::Printf(TEXT("%s: what its death does"), *Where), Row.Params.OnDeath, FString(Drafted.OnDeath));
		TestEqual(*FString::Printf(TEXT("%s: how far it reaches on a death"), *Where), Row.Params.OnDeathRangeCm,
				  Drafted.OnDeathRangeMetres * DeathSkillMetre, DeathSkillAllowance);
		TestEqual(*FString::Printf(TEXT("%s: what rises"), *Where), Row.Params.RisesAs, FString(Drafted.RisesAs));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsWaitingRowsArePlaceholdersTest,
	"Cataclysm.DeathSkills.EveryDeathRowWaitingOnCodeIsGrantedAsTheDesignedPlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Death rows written with their name, description and numbers and NO shape, because the code they need is not
 * built (`tools/reapply_death_skills_workbook.py` names what each waits on): 33 after slice DS1, 23 after slice DS2,
 * which built N1, N2, N3, N4, N11 and N16 and saw None Spared's range ruled, and 14 after slice DS3, which built N5,
 * N6, N7, N8 and N10 (Soul Snare waits on N12 too, its wisp being Grave Mouth's). Each is offered by its weapon
 * (`SkillsFor` keeps a named row), carries its description, and has no shape, so `TemplateFor` answers null and the
 * weapon slots grant `UCataclysmUndesignedSkill`, which says what it will do and does nothing yet. THE CONTROL: across
 * every Death weapon and the aura the table holds 51 named Death rows, of which 37 have a shape: the Dagger's five,
 * the aura and the thirty-one the three slices write whole.
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
		{TEXT("Sword"), ESlot::Heavy, TEXT("Mercy Stroke")},
		{TEXT("Staff"), ESlot::Movement, TEXT("Wraithwalk")},
		{TEXT("Staff"), ESlot::Special, TEXT("Grave Mouth")},
		{TEXT("Wand"), ESlot::Support, TEXT("Soul Snare")},
		{TEXT("Wand"), ESlot::Ultimate, TEXT("Requiem")},
		{TEXT("Fist"), ESlot::Support, TEXT("Hemlock")},
		{TEXT("Fist"), ESlot::Heavy, TEXT("Gravetouch")},
		{TEXT("Fist"), ESlot::Ultimate, TEXT("Quietus")},
		{TEXT("Whip"), ESlot::Support, TEXT("Death Grip")},
		{TEXT("Spear"), ESlot::Support, TEXT("Grim Reach")},
		{TEXT("Spear"), ESlot::Special, TEXT("Soul Lance")},
		{TEXT("Crossbow"), ESlot::Support, TEXT("Carrion Crows")},
		{TEXT("Crossbow"), ESlot::Heavy, TEXT("Coffin Nail")},
		{TEXT("Crossbow"), ESlot::Special, TEXT("Effigy")},
	};
	TestEqual(TEXT("set-up: 14 rows wait"), static_cast<int32>(UE_ARRAY_COUNT(Waiting)), 14);

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
	TestEqual(TEXT("control: of which 37 have a shape, the 31 of the three slices, the Dagger's five and the aura"),
			  Shaped, 37);
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

	ACataclysmMinion* Stays = Lasting->SummonOne();
	ACataclysmMinion* Replacement = Lasting->SummonReplacementAt(FVector(2.0f * DeathSkillMetre, 0.0f, 0.0f));
	ACataclysmMinion* Passes = Passing->SummonOne();
	ON_SCOPE_EXIT { DeathSkillClear({Stays, Replacement, Passes}); };
	if (!TestNotNull(TEXT("set-up: the permanent raise rose"), Stays)
		|| !TestNotNull(TEXT("set-up: and so did its replacement"), Replacement)
		|| !TestNotNull(TEXT("set-up: and the control's imp"), Passes))
	{
		return false;
	}

	TestEqual(TEXT("the permanent raise has no lifespan: it stands until it is destroyed"), Stays->GetLifeSpan(), 0.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("and neither has a replacement of it"), Replacement->GetLifeSpan(), 0.0f, DeathSkillAllowance);
	TestTrue(TEXT("and it holds its reserve while it stands"), Stays->ReservedFervour > 0.0f);
	TestEqual(TEXT("control: a raise without the key has its row's 8 seconds"), Passes->GetLifeSpan(), 8.0f,
			  DeathSkillAllowance);
	return true;
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

	const float NearBefore = DeathSkillHealthOf(Near.Actor);
	const float FarBefore = DeathSkillHealthOf(Far.Actor);
	TestEqual(TEXT("its blow struck the one enemy within 5 m"), Rising->StrikeAsItRises(), 1);
	TestEqual(TEXT("and records it"), Rising->StruckAsItRose, 1);
	TestTrue(TEXT("the enemy 3 m off lost health"), DeathSkillHealthOf(Near.Actor) < NearBefore);
	TestEqual(TEXT("and is Crippled at the row's 30"), UCataclysmSkillEffects::StatedStrengthOn(Near.Actor, Cripple),
			  30.0f, DeathSkillAllowance);
	TestEqual(TEXT("for the row's 4 seconds"), DeathSkillSecondsLeft(Near.Actor, Cripple), 4.0f, DeathSkillAllowance);
	TestEqual(TEXT("the enemy 7 m off, beyond the radius, lost nothing"), DeathSkillHealthOf(Far.Actor), FarBefore,
			  DeathSkillAllowance);
	TestFalse(TEXT("and is not Crippled"), Far.AbilitySystem->HasMatchingGameplayTag(Cripple));

	// THE CONTROL: the same cell, no damage of its own.
	FDeathSkillCreature ControlSummoner(ControlWorld, FVector::ZeroVector, ECataclysmTeam::Players);
	ControlSummoner.Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), DeathSkillWeaponDamage);
	FDeathSkillCreature ControlNear(ControlWorld, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Quiet = GrantDeathSkill<UCataclysmSummonSkill>(
		ControlSummoner.AbilitySystem, ControlSummoner.Actor, ECataclysmAbilitySlot::Ultimate, Cell,
		TEXT("A raise stating no damage of its own"));
	if (!TestNotNull(TEXT("set-up: the control raise is granted"), Quiet))
	{
		return false;
	}
	const float ControlBefore = DeathSkillHealthOf(ControlNear.Actor);
	TestFalse(TEXT("control: a row stating no damage of its own does not strike as it rises"), Quiet->StrikesAsItRises());
	TestEqual(TEXT("control: so its blow struck nobody"), Quiet->StrikeAsItRises(), 0);
	TestEqual(TEXT("control: the enemy 3 m off lost nothing"), DeathSkillHealthOf(ControlNear.Actor), ControlBefore,
			  DeathSkillAllowance);
	TestFalse(TEXT("control: and is not Crippled"), ControlNear.AbilitySystem->HasMatchingGameplayTag(Cripple));
	return true;
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

	// THE CONTROL: nobody within reach.
	FDeathSkillCreature ControlSummoner(ControlWorld, FVector::ZeroVector, ECataclysmTeam::Players);
	ControlSummoner.Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), DeathSkillWeaponDamage);
	FDeathSkillCreature OutOfReach(ControlWorld, FVector(6.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Unanswered = GrantDeathSkill<UCataclysmSummonSkill>(
		ControlSummoner.AbilitySystem, ControlSummoner.Actor, ECataclysmAbilitySlot::Ultimate, Cell,
		TEXT("A raise with nobody to hew"));
	if (!TestNotNull(TEXT("set-up: the control raise is granted"), Unanswered))
	{
		return false;
	}
	Unanswered->DamagePercentOverride = 400.0f;
	TestEqual(TEXT("control: with nobody within 4 m the blow strikes nobody"), Unanswered->StrikeAsItRises(), 0);
	ACataclysmMinion* RisenAnyway = Unanswered->SummonOne();
	ON_SCOPE_EXIT { DeathSkillClear({RisenAnyway}); };
	if (TestNotNull(TEXT("control: and the raise still rises"), RisenAnyway))
	{
		TestTrue(TEXT("control: where its rift was, the origin, and not at the enemy out of reach"),
				 FVector::Dist2D(RisenAnyway->GetActorLocation(), FVector::ZeroVector) < 1.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsSeveringRushTest,
	"Cataclysm.DeathSkills.SeveringRushOpensABleedOnTheEnemyItsChargeCrossesAndNoneBesideIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Fist's Severing Rush, its own row: "Rush 12 meters through everything in your way, knocking each enemy 2 meters
 * aside and opening a Bleed on it." N16: its `Ailment=Bleed`, which the owner's set allows since 2026-11-24, laid through
 * `UCataclysmSkillTemplate::ApplyStatedAilmentTo` as Nightshade's Poison is. The charge runs 12 m along +X: the enemy
 * 6 m along it and half a metre off the line is struck and bleeds (`Keyword.DoT.Bleed`, the tag a bleed is laid with);
 * THE CONTROL, the enemy 6 m along and 4 m off the line, beyond the charge's 2 m, is not struck and does not bleed.
 */
bool FCataclysmDeathSkillsSeveringRushTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Fist"), ECataclysmAbilitySlot::Movement);
	const FGameplayTag Bleed = DeathSkillTagNamed(TEXT("Keyword.DoT.Bleed"));
	if (!TestEqual(TEXT("set-up: the Death Fist's Movement row is Severing Rush. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Severing Rush")))
		|| !TestEqual(TEXT("set-up: it states a Bleed"), Row.Params.Ailment, FString(TEXT("Bleed")))
		|| !TestTrue(TEXT("set-up: the vocabulary has the bleed's tag"), Bleed.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillFighter Crossed(World, FVector(6.0f * DeathSkillMetre, 0.5f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter Beside(World, FVector(6.0f * DeathSkillMetre, 4.0f * DeathSkillMetre, 0.0f));
	UCataclysmMovementSkill* Rush = GrantDeathRow<UCataclysmMovementSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Rush))
	{
		return false;
	}

	TestTrue(TEXT("the rush activates"), ActivateDeathSkill(Caster.AbilitySystem, Rush));
	TestEqual(TEXT("it struck the one enemy on its line"), Rush->EnemiesHit, 1);
	TestTrue(TEXT("which bleeds"), Crossed.Carries(Bleed));
	TestFalse(TEXT("control: the enemy 4 m off the line, beyond the charge's 2 m, does not"), Beside.Carries(Bleed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsBarrowGuardTest,
	"Cataclysm.DeathSkills.BarrowGuardRaisesTwoPermanentTauntingGuardsInABurstThatCripplesWithinFiveMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Greatsword's Barrow Guard, its own row: "call up two Barrow Guards, skeleton warriors that fight beside you until
 * they are destroyed and draw the fight onto themselves: every enemy within 5 meters of a guard turns on it ... They rise
 * in a burst of grave-cold: every enemy within 5 meters takes 300% weapon damage and is Crippled, moving and attacking
 * 30% slower, for 4 seconds." N1, N2 and C3 together, raised as a press raises them -- the rising blow, then one
 * `SummonOne` for each of the row's `Count` -- at a summoner that has not aimed, so at its feet. The enemy 4 m off is
 * struck, Crippled at 30 for 4 s and taunted by a guard; THE CONTROL, the enemy 7 m off, is none of them. Two guards of
 * the BarrowGuard kind stand with no lifespan, each taunting within the row's 5 m and holding some of its summoner's
 * Fervour.
 */
bool FCataclysmDeathSkillsBarrowGuardTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Greatsword"), ECataclysmAbilitySlot::Ultimate);
	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestEqual(TEXT("set-up: the Death Greatsword's Ultimate row is Barrow Guard. If empty, DT_WeaponSkills is older "
						"than the CSV"), Row.Name, FString(TEXT("Barrow Guard")))
		|| !TestEqual(TEXT("set-up: it raises two"), Row.Params.Count, 2)
		|| !TestTrue(TEXT("set-up: the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Summoner.Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), DeathSkillWeaponDamage);
	FDeathSkillCreature Near(World, FVector(4.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Far(World, FVector(0.0f, 7.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Guard = GrantDeathRow<UCataclysmSummonSkill>(Summoner.AbilitySystem, Summoner.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Guard))
	{
		return false;
	}

	const float NearBefore = DeathSkillHealthOf(Near.Actor);
	const float FarBefore = DeathSkillHealthOf(Far.Actor);
	TestEqual(TEXT("the burst struck the one enemy within 5 m"), Guard->StrikeAsItRises(), 1);
	TestTrue(TEXT("the enemy 4 m off lost health"), DeathSkillHealthOf(Near.Actor) < NearBefore);
	TestEqual(TEXT("and is Crippled at 30"), UCataclysmSkillEffects::StatedStrengthOn(Near.Actor, Cripple), 30.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("for 4 seconds"), DeathSkillSecondsLeft(Near.Actor, Cripple), 4.0f, DeathSkillAllowance);

	TArray<ACataclysmMinion*> Guards;
	ON_SCOPE_EXIT { DeathSkillClear(Guards); };
	for (int32 Index = 0; Index < Row.Params.Count; ++Index)
	{
		Guards.Add(Guard->SummonOne());
	}
	bool bNearTauntedByAGuard = false;
	for (const ACataclysmMinion* Risen : Guards)
	{
		if (!TestNotNull(TEXT("a guard rose. If not, DT_MinionTypes is older than the CSV"), Risen))
		{
			return false;
		}
		TestEqual(TEXT("of the BarrowGuard kind"), Risen->TypeName, FString(TEXT("BarrowGuard")));
		TestEqual(TEXT("standing until it is destroyed"), Risen->GetLifeSpan(), 0.0f, DeathSkillAllowance);
		TestEqual(TEXT("taunting within the row's 5 m"), Risen->TauntRadiusCm, 5.0f * DeathSkillMetre,
				  DeathSkillAllowance);
		TestTrue(TEXT("and holding some of its summoner's Fervour"), Risen->ReservedFervour > 0.0f);
		bNearTauntedByAGuard = bNearTauntedByAGuard || Near.TauntedBy() == Risen;
	}
	TestEqual(TEXT("two guards stand"), Guard->LivingMinionCount(), 2);
	TestTrue(TEXT("and the enemy 4 m off is taunted by one of them"), bNearTauntedByAGuard);
	TestEqual(TEXT("control: the enemy 7 m off lost nothing"), DeathSkillHealthOf(Far.Actor), FarBefore,
			  DeathSkillAllowance);
	TestFalse(TEXT("control: is not Crippled"), Far.AbilitySystem->HasMatchingGameplayTag(Cripple));
	TestNull(TEXT("control: and is not taunted"), Far.TauntedBy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsExhumeTest,
	"Cataclysm.DeathSkills.ExhumeHewsTheEnemyWithinFourMetresAndRaisesAPermanentBoneReaperAtItsFeet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Greataxe's Exhume, its own row: "Hew one enemy within 4 meters for 400% weapon damage and drag a Bone Reaper up out
 * of the ground at its feet. The Reaper fights for you until it is destroyed ... and reserves 15 Fervour while it
 * stands." N1 and N2 as one blow. The enemy 3 m off is hewn and the Reaper rises at its feet, of the BoneReaper kind,
 * with no lifespan and holding some of its summoner's Fervour; THE CONTROL, the enemy 6 m off, beyond the 4 m, is not
 * struck.
 */
bool FCataclysmDeathSkillsExhumeTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Greataxe"), ECataclysmAbilitySlot::Ultimate);
	if (!TestEqual(TEXT("set-up: the Death Greataxe's Ultimate row is Exhume. If empty, DT_WeaponSkills is older than the "
						"CSV"), Row.Name, FString(TEXT("Exhume")))
		|| !TestEqual(TEXT("set-up: it hews one"), Row.Params.MaxTargets, 1))
	{
		return false;
	}

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Summoner.Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), DeathSkillWeaponDamage);
	FDeathSkillCreature Hewn(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Beyond(World, FVector(0.0f, 6.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Exhume = GrantDeathRow<UCataclysmSummonSkill>(Summoner.AbilitySystem, Summoner.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Exhume))
	{
		return false;
	}

	const float HewnBefore = DeathSkillHealthOf(Hewn.Actor);
	const float BeyondBefore = DeathSkillHealthOf(Beyond.Actor);
	TestEqual(TEXT("it hewed one enemy"), Exhume->StrikeAsItRises(), 1);
	TestTrue(TEXT("the enemy 3 m off lost health"), DeathSkillHealthOf(Hewn.Actor) < HewnBefore);
	TestEqual(TEXT("control: the enemy 6 m off, beyond the 4 m, lost nothing"), DeathSkillHealthOf(Beyond.Actor),
			  BeyondBefore, DeathSkillAllowance);

	ACataclysmMinion* Reaper = Exhume->SummonOne();
	ON_SCOPE_EXIT { DeathSkillClear({Reaper}); };
	if (!TestNotNull(TEXT("a Reaper rose. If not, DT_MinionTypes is older than the CSV"), Reaper))
	{
		return false;
	}
	TestEqual(TEXT("of the BoneReaper kind"), Reaper->TypeName, FString(TEXT("BoneReaper")));
	TestTrue(TEXT("at the hewn enemy's feet"),
			 FVector::Dist2D(Reaper->GetActorLocation(), Hewn.Actor->GetActorLocation()) < 1.0f);
	TestEqual(TEXT("standing until it is destroyed"), Reaper->GetLifeSpan(), 0.0f, DeathSkillAllowance);
	TestTrue(TEXT("and holding some of its summoner's Fervour"), Reaper->ReservedFervour > 0.0f);
	return true;
}

// ==========================================================================
// Slice DS2, change B. N3: the caster's minions in the skill's area explode. N4: a strike orders everything its caster
// commands. N11: a summon's minions lay its effect with their blows.
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsDetonatingSwingTest,
	"Cataclysm.DeathSkills.ADetonatingSwingExplodesTheCastersMinionsInsideItsArcAndNoOthers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N3 through a strike (`Detonates=1`, `UCataclysmSkillTemplate::DetonateOwnMinionsWhere`): a 4 m, 90 degree swing along
 * +X explodes the caster's minion 2 m ahead, and that minion's blast strikes the enemy 2.6 m from it, which stands
 * outside the swing's arc, so only the explosion reached it. The caster's minion 2 m behind (outside the arc), its minion
 * 6 m ahead (beyond the reach) and another summoner's minion 2 m ahead and half a metre aside (inside the arc, not the
 * caster's) all stand. THE CONTROL: the same swing without `Detonates`, by a second caster 100 m along +Y, leaves its own
 * minion 2 m ahead standing. Each caster is on the players' side, as a player is, so its minions are its allies.
 */
bool FCataclysmDeathSkillsDetonatingSwingTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDeathSkillCreature PlayersSide(World, FVector(0.0f, -100.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Players);
	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	DeathSkillSideWithThePlayers(Caster, PlayersSide);
	FDeathSkillFighter Blasted(World, FVector(1.5f * DeathSkillMetre, 2.5f * DeathSkillMetre, 0.0f));
	ACataclysmMinion* Inside = DeathSkillMinionOf(Caster.Actor, FVector(2.0f * DeathSkillMetre, 0.0f, 0.0f));
	ACataclysmMinion* Behind = DeathSkillMinionOf(Caster.Actor, FVector(-2.0f * DeathSkillMetre, 0.0f, 0.0f));
	ACataclysmMinion* Beyond = DeathSkillMinionOf(Caster.Actor, FVector(6.0f * DeathSkillMetre, 0.0f, 0.0f));
	ACataclysmMinion* NotTheCasters =
		DeathSkillMinionOf(PlayersSide.Actor, FVector(2.0f * DeathSkillMetre, 0.5f * DeathSkillMetre, 0.0f));
	ON_SCOPE_EXIT { DeathSkillClear({Inside, Behind, Beyond, NotTheCasters}); };
	if (!TestNotNull(TEXT("set-up: the minion inside the arc"), Inside) || !TestNotNull(TEXT("set-up: behind"), Behind)
		|| !TestNotNull(TEXT("set-up: beyond"), Beyond) || !TestNotNull(TEXT("set-up: another's"), NotTheCasters)
		|| !TestTrue(TEXT("set-up: the caster's minion is its ally"), UCataclysmTargeting::IsFriendlyTo(Inside, Caster.Actor))
		|| !TestTrue(TEXT("set-up: and the enemy beside it is the minion's enemy"),
					 UCataclysmTargeting::IsHostileTo(Blasted.Actor, Inside)))
	{
		return false;
	}

	UCataclysmStrikeSkill* Swing = GrantDeathSkill<UCataclysmStrikeSkill>(
		Caster.AbilitySystem, Caster.Actor, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=90; Detonates=1"),
		TEXT("A detonating swing"));
	if (!TestNotNull(TEXT("set-up: the swing is granted"), Swing))
	{
		return false;
	}

	const float BlastedBefore = Blasted.Health();
	TestEqual(TEXT("the swing reaches no enemy of its own"), Swing->SwingOnce(), 0);
	TestEqual(TEXT("and explodes one minion"), Swing->MinionsDetonated, 1);
	TestFalse(TEXT("the caster's minion 2 m ahead, inside the arc, is gone"), IsValid(Inside));
	TestTrue(TEXT("its blast struck the enemy beside it, outside the arc"), Blasted.Health() < BlastedBefore);
	TestTrue(TEXT("the caster's minion 2 m behind, outside the arc, stands"), IsValid(Behind));
	TestTrue(TEXT("its minion 6 m ahead, beyond the reach, stands"), IsValid(Beyond));
	TestTrue(TEXT("and another summoner's minion inside the arc stands: only the caster's explode"),
			 IsValid(NotTheCasters));

	// THE CONTROL: the same swing without the key.
	FDeathSkillFighter Plain(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	DeathSkillSideWithThePlayers(Plain, PlayersSide);
	ACataclysmMinion* PlainInside =
		DeathSkillMinionOf(Plain.Actor, FVector(2.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f));
	ON_SCOPE_EXIT { DeathSkillClear({PlainInside}); };
	UCataclysmStrikeSkill* PlainSwing = GrantDeathSkill<UCataclysmStrikeSkill>(
		Plain.AbilitySystem, Plain.Actor, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=90"),
		TEXT("A swing that explodes nothing"));
	if (!TestNotNull(TEXT("set-up: the control minion"), PlainInside)
		|| !TestNotNull(TEXT("set-up: the control swing"), PlainSwing))
	{
		return false;
	}
	PlainSwing->SwingOnce();
	TestEqual(TEXT("control: a swing without the key explodes nothing"), PlainSwing->MinionsDetonated, 0);
	TestTrue(TEXT("control: and its minion inside the arc stands"), IsValid(PlainInside));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsCommandingSwingTest,
	"Cataclysm.DeathSkills.ASwingThatCommandsOrdersEveryCreatureItsCasterCommandsAndOneThatDoesNotOrdersNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N4 through a strike (`CommandStrike=1` on a Strike, `UCataclysmSkillTemplate::OrderEveryCommandedStrikeOn`): a swing
 * that strikes the enemy 2 m ahead orders the caster's minion 20 m away onto it, wherever it stands, so the minion swings
 * once (`AttacksMade`) and the swing counts one creature ordered. THE CONTROL: the same swing without the key, by a
 * second caster 100 m along +Y, orders nobody and its minion does not swing. Compel's own order, through a projectile, is
 * `Cataclysm.Command.CompelOrdersEverythingCommandedOntoOneEnemy`, unchanged by the order's move to the base class.
 */
bool FCataclysmDeathSkillsCommandingSwingTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDeathSkillCreature PlayersSide(World, FVector(0.0f, -100.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Players);
	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	DeathSkillSideWithThePlayers(Caster, PlayersSide);
	FDeathSkillFighter Struck(World, FVector(2.0f * DeathSkillMetre, 0.0f, 0.0f));
	ACataclysmMinion* Distant = DeathSkillMinionOf(Caster.Actor, FVector(0.0f, 20.0f * DeathSkillMetre, 0.0f));
	ON_SCOPE_EXIT { DeathSkillClear({Distant}); };
	UCataclysmStrikeSkill* Swing = GrantDeathSkill<UCataclysmStrikeSkill>(
		Caster.AbilitySystem, Caster.Actor, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3; Angle=120; CommandStrike=1"),
		TEXT("A swing that commands"));
	if (!TestNotNull(TEXT("set-up: the distant minion"), Distant) || !TestNotNull(TEXT("set-up: the swing"), Swing))
	{
		return false;
	}

	const int32 Before = Distant->AttacksMade;
	TestEqual(TEXT("the swing struck the enemy 2 m ahead"), Swing->SwingOnce(), 1);
	TestEqual(TEXT("and ordered one creature onto it"), Swing->CommandedStrikes, 1);
	TestEqual(TEXT("the caster's minion 20 m away swung once, wherever it stands"), Distant->AttacksMade, Before + 1);

	// THE CONTROL: the same swing without the key.
	FDeathSkillFighter Plain(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	DeathSkillSideWithThePlayers(Plain, PlayersSide);
	FDeathSkillFighter PlainStruck(World, FVector(2.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f));
	ACataclysmMinion* PlainDistant =
		DeathSkillMinionOf(Plain.Actor, FVector(0.0f, 120.0f * DeathSkillMetre, 0.0f));
	ON_SCOPE_EXIT { DeathSkillClear({PlainDistant}); };
	UCataclysmStrikeSkill* PlainSwing = GrantDeathSkill<UCataclysmStrikeSkill>(
		Plain.AbilitySystem, Plain.Actor, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3; Angle=120"),
		TEXT("A swing that commands nobody"));
	if (!TestNotNull(TEXT("set-up: the control minion"), PlainDistant)
		|| !TestNotNull(TEXT("set-up: the control swing"), PlainSwing))
	{
		return false;
	}
	const int32 PlainBefore = PlainDistant->AttacksMade;
	TestEqual(TEXT("control: the plain swing struck its enemy too"), PlainSwing->SwingOnce(), 1);
	TestEqual(TEXT("control: and ordered nobody"), PlainSwing->CommandedStrikes, 0);
	TestEqual(TEXT("control: so its minion did not swing"), PlainDistant->AttacksMade, PlainBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsMinionsLayEffectTest,
	"Cataclysm.DeathSkills.AMinionTaughtTheRowsEffectLaysItWithItsBlowAndOneNotTaughtLaysNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N11 (`MinionsLayEffect=1`; `UCataclysmSummonSkill::RecordTheSkillOn` teaches it and `ACataclysmMinion::AttackTarget`
 * lays it): a summon stating `Effect=Cripple; EffectMagnitude=20; EffectDuration=2; MinionsLayEffect=1` raises an imp
 * taught the Cripple, for 2 seconds at 20, and the imp's blow on the enemy 1 m off Cripples it at 20 for 2 s. THE
 * CONTROL: the same cell without the key raises an imp taught nothing, whose blow on its own enemy lays no Cripple.
 */
bool FCataclysmDeathSkillsMinionsLayEffectTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestTrue(TEXT("set-up: the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FDeathSkillCreature Struck(World, FVector(1.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature ControlStruck(World, FVector(0.0f, 50.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Teaching = GrantDeathSkill<UCataclysmSummonSkill>(
		Summoner.AbilitySystem, Summoner.Actor, ECataclysmAbilitySlot::Ultimate,
		TEXT("Count=1; Radius=3; Minions=Imp:1; FervourReserve=3; Effect=Cripple; EffectMagnitude=20; EffectDuration=2; "
			 "MinionsLayEffect=1"), TEXT("A raise whose minions Cripple"));
	UCataclysmSummonSkill* Plain = GrantDeathSkill<UCataclysmSummonSkill>(
		Summoner.AbilitySystem, Summoner.Actor, ECataclysmAbilitySlot::Special,
		TEXT("Count=1; Radius=3; Minions=Imp:1; FervourReserve=3; Effect=Cripple; EffectMagnitude=20; EffectDuration=2"),
		TEXT("A raise whose minions lay nothing"));
	if (!TestNotNull(TEXT("set-up: the teaching raise"), Teaching) || !TestNotNull(TEXT("set-up: the control"), Plain))
	{
		return false;
	}

	ACataclysmMinion* Taught = Teaching->SummonOne();
	ACataclysmMinion* Untaught = Plain->SummonOne();
	ON_SCOPE_EXIT { DeathSkillClear({Taught, Untaught}); };
	if (!TestNotNull(TEXT("set-up: the taught imp rose"), Taught)
		|| !TestNotNull(TEXT("set-up: the control imp rose"), Untaught)
		|| !TestEqual(TEXT("the taught imp was taught one effect"), Taught->EffectTagsItsBlowsLay.Num(), 1)
		|| !TestEqual(TEXT("with its seconds beside it"), Taught->EffectSecondsItsBlowsLay.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("the Cripple"), Taught->EffectTagsItsBlowsLay[0] == Cripple);
	TestEqual(TEXT("for 2 seconds"), Taught->EffectSecondsItsBlowsLay[0], 2.0f, DeathSkillAllowance);
	TestEqual(TEXT("at 20"), Taught->EffectMagnitudeItsBlowsLay, 20.0f, DeathSkillAllowance);

	Taught->AttackTarget(Struck.Actor);
	TestTrue(TEXT("its blow Crippled the enemy it struck"), Struck.AbilitySystem->HasMatchingGameplayTag(Cripple));
	TestEqual(TEXT("at 20"), UCataclysmSkillEffects::StatedStrengthOn(Struck.Actor, Cripple), 20.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("for 2 seconds"), DeathSkillSecondsLeft(Struck.Actor, Cripple), 2.0f, DeathSkillAllowance);

	// THE CONTROL: an imp taught nothing.
	TestEqual(TEXT("control: the imp of a row without the key was taught nothing"),
			  Untaught->EffectTagsItsBlowsLay.Num(), 0);
	const int32 SwungBefore = Untaught->AttacksMade;
	Untaught->AttackTarget(ControlStruck.Actor);
	TestEqual(TEXT("control: set-up: its blow was dealt"), Untaught->AttacksMade, SwungBefore + 1);
	TestFalse(TEXT("control: and laid no Cripple"), ControlStruck.AbilitySystem->HasMatchingGameplayTag(Cripple));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsGravebellTest,
	"Cataclysm.DeathSkills.GravebellExplodesTheMinionsInItsArcAndOrdersTheRestOntoTheEnemyItStruck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Greatsword's Gravebell, its own row: "Swing the greatsword through a wide arc like a tolling bell, giving Necrosis
 * to everything it strikes. Your dead answer the bell: every minion of yours the arc passes through explodes, and every
 * other creature you command strikes the first enemy the arc hit, wherever it is standing." N3 and N4 in one swing. The
 * enemy 2 m ahead is struck and carries Necrosis; the caster's minion 3 m ahead and half a metre aside, inside the 4.5 m,
 * 140 degree arc, explodes; its minion 20 m away is ordered onto the enemy and swings, and is the one creature ordered,
 * since the exploded one is gone before the order.
 */
bool FCataclysmDeathSkillsGravebellTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Greatsword"), ECataclysmAbilitySlot::Heavy);
	const FGameplayTag Necrosis = DeathSkillTagNamed(TEXT("Keyword.DoT.Necrosis"));
	if (!TestEqual(TEXT("set-up: the Death Greatsword's Heavy row is Gravebell. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Gravebell")))
		|| !TestTrue(TEXT("set-up: the vocabulary has Necrosis's tag"), Necrosis.IsValid()))
	{
		return false;
	}

	FDeathSkillCreature PlayersSide(World, FVector(0.0f, -100.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Players);
	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	DeathSkillSideWithThePlayers(Caster, PlayersSide);
	FDeathSkillFighter Struck(World, FVector(2.0f * DeathSkillMetre, 0.0f, 0.0f));
	ACataclysmMinion* InTheArc =
		DeathSkillMinionOf(Caster.Actor, FVector(3.0f * DeathSkillMetre, 0.5f * DeathSkillMetre, 0.0f));
	ACataclysmMinion* Away = DeathSkillMinionOf(Caster.Actor, FVector(0.0f, 20.0f * DeathSkillMetre, 0.0f));
	ON_SCOPE_EXIT { DeathSkillClear({InTheArc, Away}); };
	UCataclysmStrikeSkill* Bell = GrantDeathRow<UCataclysmStrikeSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the minion in the arc"), InTheArc) || !TestNotNull(TEXT("set-up: the one away"), Away)
		|| !TestNotNull(TEXT("set-up: the row is granted"), Bell))
	{
		return false;
	}

	const int32 Before = Away->AttacksMade;
	TestEqual(TEXT("the swing struck the enemy 2 m ahead"), Bell->SwingOnce(), 1);
	TestTrue(TEXT("which carries Necrosis"), Struck.Carries(Necrosis));
	TestEqual(TEXT("one minion exploded"), Bell->MinionsDetonated, 1);
	TestFalse(TEXT("the one inside the arc"), IsValid(InTheArc));
	TestEqual(TEXT("one creature was ordered"), Bell->CommandedStrikes, 1);
	TestEqual(TEXT("the minion 20 m away, which swung once"), Away->AttacksMade, Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsBarrowLeapTest,
	"Cataclysm.DeathSkills.BarrowLeapCripplesAndExplodesTheCastersMinionsWithinFourMetresOfItsLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Greatsword's Barrow Leap, its own row: "Leap to a point up to 10 meters away and land in a burst of grave-cold,
 * Crippling every enemy within 4 meters so it moves and attacks 30% slower for 3 seconds. Every minion of yours within it
 * explodes as you land." N3 at a leap's arrival, with N22. It lands 10 m along +X: the enemy 1 m beyond is Crippled at
 * 30 for 3 s; the caster's minion 3 m to the side of the landing explodes; its minion 7 m to the side, beyond the 4 m,
 * stands; and another summoner's minion a metre and a half from the landing stands, not being the caster's.
 */
bool FCataclysmDeathSkillsBarrowLeapTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Greatsword"), ECataclysmAbilitySlot::Movement);
	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestEqual(TEXT("set-up: the Death Greatsword's Movement row is Barrow Leap. If empty, DT_WeaponSkills is older "
						"than the CSV"), Row.Name, FString(TEXT("Barrow Leap")))
		|| !TestTrue(TEXT("set-up: the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	FDeathSkillCreature PlayersSide(World, FVector(0.0f, -100.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Players);
	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	DeathSkillSideWithThePlayers(Caster, PlayersSide);
	FDeathSkillFighter Landed(World, FVector(11.0f * DeathSkillMetre, 0.0f, 0.0f));
	ACataclysmMinion* Near =
		DeathSkillMinionOf(Caster.Actor, FVector(10.0f * DeathSkillMetre, 3.0f * DeathSkillMetre, 0.0f));
	ACataclysmMinion* Far =
		DeathSkillMinionOf(Caster.Actor, FVector(10.0f * DeathSkillMetre, 7.0f * DeathSkillMetre, 0.0f));
	ACataclysmMinion* Others =
		DeathSkillMinionOf(PlayersSide.Actor, FVector(9.0f * DeathSkillMetre, 1.0f * DeathSkillMetre, 0.0f));
	ON_SCOPE_EXIT { DeathSkillClear({Near, Far, Others}); };
	UCataclysmMovementSkill* Leap = GrantDeathRow<UCataclysmMovementSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the near minion"), Near) || !TestNotNull(TEXT("set-up: the far one"), Far)
		|| !TestNotNull(TEXT("set-up: another's"), Others) || !TestNotNull(TEXT("set-up: the row is granted"), Leap))
	{
		return false;
	}

	TestTrue(TEXT("the leap activates"), ActivateDeathSkill(Caster.AbilitySystem, Leap));
	TestTrue(TEXT("the enemy 1 m beyond the landing is Crippled"), Landed.Carries(Cripple));
	TestEqual(TEXT("at 30"), UCataclysmSkillEffects::StatedStrengthOn(Landed.Actor, Cripple), 30.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("for 3 seconds"), DeathSkillSecondsLeft(Landed.Actor, Cripple), 3.0f, DeathSkillAllowance);
	TestEqual(TEXT("one minion exploded"), Leap->MinionsDetonated, 1);
	TestFalse(TEXT("the caster's, 3 m from the landing"), IsValid(Near));
	TestTrue(TEXT("its minion 7 m from the landing stands"), IsValid(Far));
	TestTrue(TEXT("and another summoner's minion beside the landing stands"), IsValid(Others));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsMarrowLanceTest,
	"Cataclysm.DeathSkills.MarrowLanceExplodesTheCastersMinionsOnTheLineItFlewAndNoneBesideIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Staff's Marrow Lance, its own row: "Drive a lance of bone up out of the ground along a line up to 14 meters long,
 * giving Necrosis to everything it passes. Every minion of yours on the line explodes." N3 through a projectile that
 * flies (`Speed=2400`): as the lance finishes, the caster's minions within its 1.2 m of the line it flew explode
 * (`UCataclysmProjectileSkill::OnProjectileFinished`; a judgement of slice DS2 that a flying lance detonates once, as it
 * finishes). Fired along +X: nothing has exploded while it is in the air; then the enemy 5 m along is struck and carries
 * Necrosis, the caster's minion 7 m along and half a metre aside explodes, and its minion 7 m along and 3 m aside stands.
 */
bool FCataclysmDeathSkillsMarrowLanceTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Staff"), ECataclysmAbilitySlot::Heavy);
	const FGameplayTag Necrosis = DeathSkillTagNamed(TEXT("Keyword.DoT.Necrosis"));
	if (!TestEqual(TEXT("set-up: the Death Staff's Heavy row is Marrow Lance. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Marrow Lance")))
		|| !TestTrue(TEXT("set-up: the vocabulary has Necrosis's tag"), Necrosis.IsValid()))
	{
		return false;
	}

	FDeathSkillCreature PlayersSide(World, FVector(0.0f, -100.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Players);
	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	DeathSkillSideWithThePlayers(Caster, PlayersSide);
	FDeathSkillFighter Passed(World, FVector(5.0f * DeathSkillMetre, 0.0f, 0.0f));
	ACataclysmMinion* OnTheLine =
		DeathSkillMinionOf(Caster.Actor, FVector(7.0f * DeathSkillMetre, 0.5f * DeathSkillMetre, 0.0f));
	ACataclysmMinion* Beside =
		DeathSkillMinionOf(Caster.Actor, FVector(7.0f * DeathSkillMetre, 3.0f * DeathSkillMetre, 0.0f));
	ON_SCOPE_EXIT { DeathSkillClear({OnTheLine, Beside}); };
	UCataclysmProjectileSkill* Lance = GrantDeathRow<UCataclysmProjectileSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the minion on the line"), OnTheLine) || !TestNotNull(TEXT("set-up: beside"), Beside)
		|| !TestNotNull(TEXT("set-up: the row is granted"), Lance))
	{
		return false;
	}

	if (!TestTrue(TEXT("the lance is thrown"), ActivateDeathSkill(Caster.AbilitySystem, Lance)))
	{
		return false;
	}
	ACataclysmProjectile* Shot = Lance->InFlight.Get();
	if (!TestNotNull(TEXT("and is in the air, since it has a speed"), Shot))
	{
		return false;
	}
	TestEqual(TEXT("nothing has exploded while it flies"), Lance->MinionsDetonated, 0);
	TestTrue(TEXT("and the minion on the line still stands"), IsValid(OnTheLine));

	DeathSkillFlyToTheEnd(Shot);
	TestTrue(TEXT("set-up: the lance has finished"), !IsValid(Shot) || Shot->bFinished);
	TestTrue(TEXT("the enemy 5 m along carries Necrosis"), Passed.Carries(Necrosis));
	TestEqual(TEXT("one minion exploded"), Lance->MinionsDetonated, 1);
	TestFalse(TEXT("the caster's, half a metre off the line"), IsValid(OnTheLine));
	TestTrue(TEXT("and its minion 3 m off the line stands"), IsValid(Beside));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsGraveReturnTest,
	"Cataclysm.DeathSkills.GraveReturnExplodesTheCastersMinionsWithinThreeMetresOfTheShadeItReturnsTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Wand's Grave Return, its own row: "Leave a shade of yourself where you stand. For 8 seconds you may return to it
 * from anywhere within 14 meters. As you arrive it bursts, throwing every enemy within 3 meters 3 meters back, and every
 * minion of yours within it explodes too." N3 at a return. The first press leaves the shade at the origin and explodes
 * nothing; the caster is moved 10 m along +X; the second press returns it to the origin, where the caster's minion
 * nearly 2 m from the shade explodes and its minion 5 m from it, beyond the 3 m, stands.
 */
bool FCataclysmDeathSkillsGraveReturnTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Wand"), ECataclysmAbilitySlot::Movement);
	if (!TestEqual(TEXT("set-up: the Death Wand's Movement row is Grave Return. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Grave Return")))
		|| !TestTrue(TEXT("set-up: it is a return"), Row.Params.MovementMode == ECataclysmMovementMode::Recall))
	{
		return false;
	}

	FDeathSkillCreature PlayersSide(World, FVector(0.0f, -100.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Players);
	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	DeathSkillSideWithThePlayers(Caster, PlayersSide);
	ACataclysmMinion* Near =
		DeathSkillMinionOf(Caster.Actor, FVector(1.5f * DeathSkillMetre, 1.0f * DeathSkillMetre, 0.0f));
	ACataclysmMinion* Far = DeathSkillMinionOf(Caster.Actor, FVector(0.0f, 5.0f * DeathSkillMetre, 0.0f));
	ON_SCOPE_EXIT { DeathSkillClear({Near, Far}); };
	UCataclysmMovementSkill* Return = GrantDeathRow<UCataclysmMovementSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the near minion"), Near) || !TestNotNull(TEXT("set-up: the far one"), Far)
		|| !TestNotNull(TEXT("set-up: the row is granted"), Return))
	{
		return false;
	}

	TestTrue(TEXT("the first press activates"), ActivateDeathSkill(Caster.AbilitySystem, Return));
	TestEqual(TEXT("and explodes nothing: it only leaves the shade"), Return->MinionsDetonated, 0);
	TestTrue(TEXT("so the minion beside it still stands"), IsValid(Near));

	Caster.Actor->SetActorLocation(FVector(10.0f * DeathSkillMetre, 0.0f, 0.0f));
	TestTrue(TEXT("the second press activates"), ActivateDeathSkill(Caster.AbilitySystem, Return));
	TestTrue(TEXT("and returns the caster to the shade"),
			 FVector::Dist2D(Caster.Actor->GetActorLocation(), FVector::ZeroVector) < 1.0f * DeathSkillMetre);
	TestEqual(TEXT("one minion exploded"), Return->MinionsDetonated, 1);
	TestFalse(TEXT("the caster's, within 3 m of the shade"), IsValid(Near));
	TestTrue(TEXT("and its minion 5 m from the shade stands"), IsValid(Far));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsBoneChoirTest,
	"Cataclysm.DeathSkills.BoneChoirRaisesTwoPermanentMagesWhoseBurstAndBlowsCrippleByTwenty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Staff's Bone Choir, its own row: "Call two Skeletal Mages up out of the ground ... in a burst of grave-cold that
 * deals 300% weapon damage within 5 meters. They fight for you until they are destroyed and keep their distance, and the
 * burst and every bolt they throw Cripple what they hit, 20% slower for 2 seconds." N1, N2 and N11 together, raised as a
 * press raises them at a summoner that has not aimed (a skill never pressed holds its rift at the origin, where it
 * stands). The burst strikes the enemy 3 m off and Cripples it at 20 for 2 s; the enemy 7 m off is not struck, until a
 * mage's blow lands on it and Cripples it at 20 for 2 s. Two mages of the SkeletalMage kind stand with no lifespan.
 */
bool FCataclysmDeathSkillsBoneChoirTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Staff"), ECataclysmAbilitySlot::Ultimate);
	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestEqual(TEXT("set-up: the Death Staff's Ultimate row is Bone Choir. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Bone Choir")))
		|| !TestEqual(TEXT("set-up: it raises two"), Row.Params.Count, 2)
		|| !TestTrue(TEXT("set-up: the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	FDeathSkillCreature Summoner(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Summoner.Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), DeathSkillWeaponDamage);
	FDeathSkillCreature Near(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Far(World, FVector(0.0f, 7.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmSummonSkill* Choir = GrantDeathRow<UCataclysmSummonSkill>(Summoner.AbilitySystem, Summoner.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Choir))
	{
		return false;
	}

	const float NearBefore = DeathSkillHealthOf(Near.Actor);
	TestEqual(TEXT("the burst struck the one enemy within 5 m"), Choir->StrikeAsItRises(), 1);
	TestTrue(TEXT("the enemy 3 m off lost health"), DeathSkillHealthOf(Near.Actor) < NearBefore);
	TestEqual(TEXT("and is Crippled at 20"), UCataclysmSkillEffects::StatedStrengthOn(Near.Actor, Cripple), 20.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("for 2 seconds"), DeathSkillSecondsLeft(Near.Actor, Cripple), 2.0f, DeathSkillAllowance);
	TestFalse(TEXT("the enemy 7 m off is not, yet"), Far.AbilitySystem->HasMatchingGameplayTag(Cripple));

	TArray<ACataclysmMinion*> Mages;
	ON_SCOPE_EXIT { DeathSkillClear(Mages); };
	for (int32 Index = 0; Index < Row.Params.Count; ++Index)
	{
		Mages.Add(Choir->SummonOne());
	}
	for (const ACataclysmMinion* Mage : Mages)
	{
		if (!TestNotNull(TEXT("a mage rose. If not, DT_MinionTypes is older than the CSV"), Mage))
		{
			return false;
		}
		TestEqual(TEXT("of the SkeletalMage kind"), Mage->TypeName, FString(TEXT("SkeletalMage")));
		TestEqual(TEXT("standing until it is destroyed"), Mage->GetLifeSpan(), 0.0f, DeathSkillAllowance);
		TestEqual(TEXT("and taught the row's one effect"), Mage->EffectTagsItsBlowsLay.Num(), 1);
	}
	TestEqual(TEXT("two mages stand"), Choir->LivingMinionCount(), 2);

	Mages[0]->AttackTarget(Far.Actor);
	TestTrue(TEXT("a mage's blow Crippled the enemy 7 m off"), Far.AbilitySystem->HasMatchingGameplayTag(Cripple));
	TestEqual(TEXT("at 20"), UCataclysmSkillEffects::StatedStrengthOn(Far.Actor, Cripple), 20.0f, DeathSkillAllowance);
	TestEqual(TEXT("for 2 seconds"), DeathSkillSecondsLeft(Far.Actor, Cripple), 2.0f, DeathSkillAllowance);
	return true;
}

// ==========================================================================
// Slice DS3, change A. N6: Fervour paid for a kill. N7: scaling by the Fervour held. N8: a buff that holds Fervour from
// decay.
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsKillFervourByTheBlowTest,
	"Cataclysm.DeathSkills.AStrikeStatingKillFervourPaysItForEachEnemyItsBlowKillsAndNothingForOneLeftStanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N6 by the blow (`UCataclysmKillFervour::PayForTheKillingBlow`, from `UCataclysmSkillEffects::MarkDead`): a ring swing
 * stating `KillFervour=2` reaches three creatures 2 m off, kills the two of 1 health and leaves the one of 100000
 * standing, and its caster, at nought Fervour, then holds 4: 2 for each kill and nothing for the one left standing. THE
 * CONTROL, 100 m along +Y: the same swing stating no `KillFervour` kills a creature of 1 health and pays nothing.
 */
bool FCataclysmDeathSkillsKillFervourByTheBlowTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillCreature FirstWeak(World, FVector(2.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature SecondWeak(World, FVector(-2.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Sturdy(World, FVector(0.0f, 2.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	FirstWeak.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	SecondWeak.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	DeathSkillSetFervour(Caster.Actor, 0.0f);
	UCataclysmStrikeSkill* Swing = GrantDeathSkill<UCataclysmStrikeSkill>(
		Caster.AbilitySystem, Caster.Actor, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3; Angle=360; KillFervour=2"),
		TEXT("A paying swing"));
	if (!TestNotNull(TEXT("set-up: the swing is granted"), Swing)
		|| !TestEqual(TEXT("set-up: the caster holds no Fervour"), DeathSkillFervourOf(Caster.Actor), 0.0f,
					  DeathSkillAllowance))
	{
		return false;
	}

	TestEqual(TEXT("the swing reaches all three"), Swing->SwingOnce(), 3);
	TestTrue(TEXT("the first creature of 1 health died"), UCataclysmSkillEffects::IsDead(FirstWeak.Actor));
	TestTrue(TEXT("and the second"), UCataclysmSkillEffects::IsDead(SecondWeak.Actor));
	TestFalse(TEXT("the creature of 100000 stands"), UCataclysmSkillEffects::IsDead(Sturdy.Actor));
	TestTrue(TEXT("though it was struck"), DeathSkillHealthLost(Sturdy) > 0.0f);
	TestEqual(TEXT("two kills at 2 each: the caster holds 4, and nothing for the one left standing"),
			  DeathSkillFervourOf(Caster.Actor), 4.0f, DeathSkillAllowance);

	// THE CONTROL: the same swing stating no KillFervour.
	FDeathSkillFighter Plain(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	FDeathSkillCreature PlainWeak(World, FVector(2.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f),
								  ECataclysmTeam::Monsters);
	PlainWeak.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	DeathSkillSetFervour(Plain.Actor, 0.0f);
	UCataclysmStrikeSkill* PlainSwing = GrantDeathSkill<UCataclysmStrikeSkill>(
		Plain.AbilitySystem, Plain.Actor, ECataclysmAbilitySlot::Heavy, TEXT("Radius=3; Angle=360"),
		TEXT("A swing that pays nothing"));
	if (!TestNotNull(TEXT("set-up: the control swing is granted"), PlainSwing))
	{
		return false;
	}
	PlainSwing->SwingOnce();
	TestTrue(TEXT("control: its creature of 1 health died"), UCataclysmSkillEffects::IsDead(PlainWeak.Actor));
	TestEqual(TEXT("control: and its caster holds no Fervour for it"), DeathSkillFervourOf(Plain.Actor), 0.0f,
			  DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsKillFervourUnderACurseTest,
	"Cataclysm.DeathSkills.ADeathWhileAPayingCurseHoldsPaysItsCasterAndTheCursePassedOnPaysAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N6 through a curse's mark (`UCataclysmCurseSpread::SpreadFromDying`): a curse stating `Effect=Cripple; OnDeath=
 * SpreadDebuff; OnDeathRange=10; KillFervour=10` -- Death Warrant's cell, cast from the Support slot, whose curse strikes
 * no blow -- is laid on the nearest creature, 5 m off. Its death pays the caster 10 and passes the Cripple to the
 * creature 3 m from it, whose own death pays 10 more: 20. THE CONTROL, 100 m along +Y: a creature warranted the same way
 * whose Cripple is cleansed before it dies -- the warrant no longer holds -- pays nothing.
 */
bool FCataclysmDeathSkillsKillFervourUnderACurseTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestTrue(TEXT("set-up: the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}
	const TCHAR* const Warrant = TEXT("Range=15; Radius=1.5; MaxTargets=1; EffectDuration=10; Effect=Cripple; "
									  "EffectMagnitude=30; OnDeath=SpreadDebuff; OnDeathRange=10; KillFervour=10");

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillCreature Marked(World, FVector(5.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Next(World, FVector(5.0f * DeathSkillMetre, 3.0f * DeathSkillMetre, 0.0f),
							 ECataclysmTeam::Monsters);
	DeathSkillSetFervour(Caster.Actor, 0.0f);
	UCataclysmDebuffSkill* Curse = GrantDeathSkill<UCataclysmDebuffSkill>(
		Caster.AbilitySystem, Caster.Actor, ECataclysmAbilitySlot::Support, Warrant, TEXT("A paying warrant"));
	if (!TestNotNull(TEXT("set-up: the curse is granted"), Curse)
		|| !TestTrue(TEXT("the warrant is signed"), ActivateDeathSkill(Caster.AbilitySystem, Curse))
		|| !TestTrue(TEXT("the nearest creature, 5 m off, is warranted"),
					 Marked.AbilitySystem->HasMatchingGameplayTag(Cripple))
		|| !TestFalse(TEXT("and the one 3 m beyond it is not yet: the row takes one"),
					  Next.AbilitySystem->HasMatchingGameplayTag(Cripple)))
	{
		return false;
	}

	DeathSkillKillCreature(Marked);
	if (!TestTrue(TEXT("set-up: the warranted creature died"), UCataclysmSkillEffects::IsDead(Marked.Actor)))
	{
		return false;
	}
	TestEqual(TEXT("its death under the warrant paid the caster 10"), DeathSkillFervourOf(Caster.Actor), 10.0f,
			  DeathSkillAllowance);
	TestTrue(TEXT("and the warrant passed to the creature 3 m from it"), Next.AbilitySystem->HasMatchingGameplayTag(Cripple));

	DeathSkillKillCreature(Next);
	TestTrue(TEXT("set-up: that creature died too"), UCataclysmSkillEffects::IsDead(Next.Actor));
	TestEqual(TEXT("and its death under the passed warrant paid 10 more"), DeathSkillFervourOf(Caster.Actor), 20.0f,
			  DeathSkillAllowance);

	// THE CONTROL: a warrant lifted before the death.
	FDeathSkillFighter Lifter(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	FDeathSkillCreature Lifted(World, FVector(5.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f),
							   ECataclysmTeam::Monsters);
	DeathSkillSetFervour(Lifter.Actor, 0.0f);
	UCataclysmDebuffSkill* LiftedCurse = GrantDeathSkill<UCataclysmDebuffSkill>(
		Lifter.AbilitySystem, Lifter.Actor, ECataclysmAbilitySlot::Support, Warrant, TEXT("A warrant lifted"));
	if (!TestNotNull(TEXT("set-up: the control curse is granted"), LiftedCurse)
		|| !TestTrue(TEXT("set-up: the control warrant is signed"), ActivateDeathSkill(Lifter.AbilitySystem, LiftedCurse))
		|| !TestTrue(TEXT("set-up: on the control creature"), Lifted.AbilitySystem->HasMatchingGameplayTag(Cripple)))
	{
		return false;
	}
	Lifted.AbilitySystem->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(Cripple));
	if (!TestFalse(TEXT("set-up: its Cripple is cleansed"), Lifted.AbilitySystem->HasMatchingGameplayTag(Cripple)))
	{
		return false;
	}
	DeathSkillKillCreature(Lifted);
	TestTrue(TEXT("set-up: the control creature died"), UCataclysmSkillEffects::IsDead(Lifted.Actor));
	TestEqual(TEXT("control: a death after the warrant was lifted pays nothing"), DeathSkillFervourOf(Lifter.Actor),
			  0.0f, DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsKillFervourForEachHostTest,
	"Cataclysm.DeathSkills.ASoulStatingKillFervourPaysForEachHostItLeavesDeadAndGivesEachNewHostItsAilment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N6 through a buried weapon (`UCataclysmBuriedWeapon::LeapFromDying`), and the stated ailment its leap lays: a shot
 * stating `Ailment=Necrosis; OnDeath=Leap; OnDeathRange=8; KillFervour=2` -- Hungering Soul's cell -- lodges in the
 * creature 5 m along +X. That host's death pays 2, and the soul leaps into the creature 4 m from it, striking it and
 * giving it Necrosis; that one's death pays 2 more, 4 in all. THE CONTROL, 100 m along +Y: the same shot stating neither
 * `KillFervour` nor `Ailment`, Harrower's shape, pays nothing for its host's death, and its leap strikes the next
 * creature without giving it Necrosis.
 */
bool FCataclysmDeathSkillsKillFervourForEachHostTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Necrosis = DeathSkillTagNamed(TEXT("Keyword.DoT.Necrosis"));
	if (!TestTrue(TEXT("set-up: the vocabulary has Necrosis's tag"), Necrosis.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillCreature Host(World, FVector(5.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Next(World, FVector(5.0f * DeathSkillMetre, 4.0f * DeathSkillMetre, 0.0f),
							 ECataclysmTeam::Monsters);
	DeathSkillSetFervour(Caster.Actor, 0.0f);
	UCataclysmProjectileSkill* Soul = GrantDeathSkill<UCataclysmProjectileSkill>(
		Caster.AbilitySystem, Caster.Actor, ECataclysmAbilitySlot::Heavy,
		TEXT("Range=14; Radius=1; Speed=2600; Ailment=Necrosis; OnDeath=Leap; OnDeathRange=8; KillFervour=2"),
		TEXT("A hungering shot"));
	if (!TestNotNull(TEXT("set-up: the shot is granted"), Soul)
		|| !TestTrue(TEXT("the shot is loosed"), ActivateDeathSkill(Caster.AbilitySystem, Soul)))
	{
		return false;
	}
	DeathSkillFlyToTheEnd(Soul->InFlight.Get());

	const UCataclysmBuriedWeapon* Lodged = Host.Actor->FindComponentByClass<UCataclysmBuriedWeapon>();
	if (!TestNotNull(TEXT("the soul lodged in the creature 5 m along"), Lodged)
		|| !TestEqual(TEXT("carrying the row's 2 for each host it leaves dead"), Lodged->KillFervour, 2.0f,
					  DeathSkillAllowance)
		|| !TestEqual(TEXT("and its ailment"), Lodged->StatedAilment, FString(TEXT("Necrosis"))))
	{
		return false;
	}
	TestFalse(TEXT("set-up: the creature 4 m from it carries no Necrosis yet"),
			  Next.AbilitySystem->HasMatchingGameplayTag(Necrosis));
	TestEqual(TEXT("set-up: nothing is paid while the host lives"), DeathSkillFervourOf(Caster.Actor), 0.0f,
			  DeathSkillAllowance);

	DeathSkillKillCreature(Host);
	TestTrue(TEXT("set-up: the host died"), UCataclysmSkillEffects::IsDead(Host.Actor));
	TestEqual(TEXT("the host it left dead paid 2"), DeathSkillFervourOf(Caster.Actor), 2.0f, DeathSkillAllowance);
	TestTrue(TEXT("the soul struck the creature 4 m from it"), DeathSkillHealthLost(Next) > 0.0f);
	TestTrue(TEXT("and gave it Necrosis"), Next.AbilitySystem->HasMatchingGameplayTag(Necrosis));
	TestNotNull(TEXT("and lodged in it"), Next.Actor->FindComponentByClass<UCataclysmBuriedWeapon>());

	DeathSkillKillCreature(Next);
	TestTrue(TEXT("set-up: the second host died"), UCataclysmSkillEffects::IsDead(Next.Actor));
	TestEqual(TEXT("and paid 2 more: 4 in all"), DeathSkillFervourOf(Caster.Actor), 4.0f, DeathSkillAllowance);

	// THE CONTROL: Harrower's shape, which states neither.
	FDeathSkillFighter Plain(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	FDeathSkillCreature PlainHost(World, FVector(5.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f),
								  ECataclysmTeam::Monsters);
	FDeathSkillCreature PlainNext(World, FVector(5.0f * DeathSkillMetre, 104.0f * DeathSkillMetre, 0.0f),
								  ECataclysmTeam::Monsters);
	DeathSkillSetFervour(Plain.Actor, 0.0f);
	UCataclysmProjectileSkill* PlainShot = GrantDeathSkill<UCataclysmProjectileSkill>(
		Plain.AbilitySystem, Plain.Actor, ECataclysmAbilitySlot::Heavy,
		TEXT("Range=14; Radius=1; Speed=2600; OnDeath=Leap; OnDeathRange=8"), TEXT("A plain leaping shot"));
	if (!TestNotNull(TEXT("set-up: the control shot is granted"), PlainShot)
		|| !TestTrue(TEXT("set-up: the control shot is loosed"), ActivateDeathSkill(Plain.AbilitySystem, PlainShot)))
	{
		return false;
	}
	DeathSkillFlyToTheEnd(PlainShot->InFlight.Get());
	if (!TestNotNull(TEXT("set-up: the control weapon lodged"),
					 PlainHost.Actor->FindComponentByClass<UCataclysmBuriedWeapon>()))
	{
		return false;
	}
	DeathSkillKillCreature(PlainHost);
	TestEqual(TEXT("control: its host's death pays nothing"), DeathSkillFervourOf(Plain.Actor), 0.0f,
			  DeathSkillAllowance);
	TestTrue(TEXT("control: its leap struck the next creature"), DeathSkillHealthLost(PlainNext) > 0.0f);
	TestFalse(TEXT("control: without giving it Necrosis"), PlainNext.AbilitySystem->HasMatchingGameplayTag(Necrosis));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsStrikeScalesByFervourTest,
	"Cataclysm.DeathSkills.AStrikeScalingByFervourDealsMoreForEachPointHeldUpToItsCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N7 on a strike (`UCataclysmSkillTemplate::ScalingUnits`): a ring swing of 200% stating `MoreDamagePer=0.5;
 * ScalingSource=Fervour; MaxDamagePercent=320` -- Grim Swath's rate, under a ceiling low enough to reach -- strikes a
 * creature half again as hard at 100 Fervour as at none (300% against 200%), and at 150 Fervour for its 320% ceiling,
 * where its rate alone would make 350%. THE CONTROL: the same swing stating no scaling, at 100 Fervour, strikes as hard
 * as the scaling one at none. Read as ratios of the health each swing took, so no figure of the damage pipeline beyond
 * the skill's own percent is assumed; each swing is a fresh caster and creature, 100 m apart.
 */
bool FCataclysmDeathSkillsStrikeScalesByFervourTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const TCHAR* const Scaling = TEXT("Radius=3; Angle=360; MoreDamagePer=0.5; ScalingSource=Fervour; MaxDamagePercent=320");
	const auto LostAt = [World](float HeldFervour, const TCHAR* SwingCell, float AcrossMetres) -> float
	{
		FDeathSkillFighter Swinger(World, FVector(0.0f, AcrossMetres * DeathSkillMetre, 0.0f));
		FDeathSkillCreature Struck(World, FVector(2.0f * DeathSkillMetre, AcrossMetres * DeathSkillMetre, 0.0f),
								   ECataclysmTeam::Monsters);
		DeathSkillSetFervour(Swinger.Actor, HeldFervour);
		UCataclysmStrikeSkill* Swing = GrantDeathSkill<UCataclysmStrikeSkill>(
			Swinger.AbilitySystem, Swinger.Actor, ECataclysmAbilitySlot::Heavy, SwingCell,
			TEXT("A swing scaling by Fervour"));
		if (!Swing)
		{
			return -1.0f;
		}
		Swing->DamagePercentOverride = 200.0f;
		Swing->SwingOnce();
		return DeathSkillHealthLost(Struck);
	};

	const float AtNone = LostAt(0.0f, Scaling, 0.0f);
	if (!TestTrue(TEXT("set-up: the swing at no Fervour struck"), AtNone > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("at 100 Fervour it strikes half again as hard: 300% against 200%"),
			  LostAt(100.0f, Scaling, 100.0f) / AtNone, 1.5f, DeathSkillAllowance);
	TestEqual(TEXT("at 150 Fervour it strikes for its ceiling, 320% against 200%, not the 350% its rate makes"),
			  LostAt(150.0f, Scaling, 200.0f) / AtNone, 1.6f, DeathSkillAllowance);
	TestEqual(TEXT("control: the same swing stating no scaling, at 100 Fervour, strikes as the scaling one at none"),
			  LostAt(100.0f, TEXT("Radius=3; Angle=360"), 300.0f) / AtNone, 1.0f, DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsBuffScalesByFervourTest,
	"Cataclysm.DeathSkills.ASelfBuffScalingByFervourGrantsMoreDamageForWhatIsHeldAndFollowsItWhileItRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N7 on a self buff (`UCataclysmSelfBuffSkill::ScalingCount`, `FervourChanged`): a buff stating `Duration=6;
 * MoreDamagePer=0.25; ScalingSource=Fervour` -- Cold Blood's rate -- goes up at 100 Fervour granting 25% more damage and
 * follows the pool while it runs: at 40, 10%. Cancelled, it grants nothing, and the pool rising to 80 afterwards gives it
 * nothing back. THE CONTROL: the same buff scaling by `Burning`, at 100 Fervour with nothing alight, grants nothing.
 */
bool FCataclysmDeathSkillsBuffScalesByFervourTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDeathSkillFighter Holder(World, FVector::ZeroVector);
	DeathSkillSetFervour(Holder.Actor, 100.0f);
	UCataclysmSelfBuffSkill* Blood = GrantDeathSkill<UCataclysmSelfBuffSkill>(
		Holder.AbilitySystem, Holder.Actor, ECataclysmAbilitySlot::Support,
		TEXT("Duration=6; MoreDamagePer=0.25; ScalingSource=Fervour"), TEXT("A buff scaling by Fervour"));
	if (!TestNotNull(TEXT("set-up: the buff is granted"), Blood)
		|| !TestTrue(TEXT("the buff goes up"), ActivateDeathSkill(Holder.AbilitySystem, Blood)))
	{
		return false;
	}
	TestEqual(TEXT("at 100 Fervour it grants 25% more damage"), Blood->GrantedIncrease, 25.0f, DeathSkillAllowance);

	DeathSkillSetFervour(Holder.Actor, 40.0f);
	TestEqual(TEXT("and follows the pool down to 40: 10%"), Blood->GrantedIncrease, 10.0f, DeathSkillAllowance);

	Holder.AbilitySystem->CancelAbilityHandle(Blood->GetCurrentAbilitySpecHandle());
	TestFalse(TEXT("set-up: the buff is cancelled"), Blood->IsActive());
	TestEqual(TEXT("cancelled, it grants nothing"), Blood->GrantedIncrease, 0.0f, DeathSkillAllowance);
	DeathSkillSetFervour(Holder.Actor, 80.0f);
	TestEqual(TEXT("and the pool rising afterwards gives it nothing back"), Blood->GrantedIncrease, 0.0f,
			  DeathSkillAllowance);

	// THE CONTROL: the same rate counting burning enemies, of which there are none.
	FDeathSkillFighter Other(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	DeathSkillSetFervour(Other.Actor, 100.0f);
	UCataclysmSelfBuffSkill* Burning = GrantDeathSkill<UCataclysmSelfBuffSkill>(
		Other.AbilitySystem, Other.Actor, ECataclysmAbilitySlot::Support,
		TEXT("Duration=6; Radius=15; MoreDamagePer=0.25; ScalingSource=Burning"), TEXT("A buff counting fires"));
	if (!TestNotNull(TEXT("set-up: the control buff is granted"), Burning)
		|| !TestTrue(TEXT("set-up: the control buff goes up"), ActivateDeathSkill(Other.AbilitySystem, Burning)))
	{
		return false;
	}
	TestEqual(TEXT("control: counting fires, at 100 Fervour and nothing alight, it grants nothing"),
			  Burning->GrantedIncrease, 0.0f, DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsBuffHoldsFervourTest,
	"Cataclysm.DeathSkills.ASelfBuffThatHoldsFervourStopsItsDecayAndTheDecayReturnsWhenItEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N8 (`UCataclysmFervour::DecayStep`, `UCataclysmSkillTemplate::ARunningBuffHoldsFervour`): a character given the
 * Ravager's decay -- 5 a second, with nothing hostile within its 4 m -- loses 5 of its 50 Fervour in a second's step,
 * which is THE CONTROL that the decay is real; with a buff stating `Duration=6; HoldsFervour=1` running, the next step
 * takes nothing; and once that buff is cancelled, the step after takes 5 again. The character is a creature on the
 * players' side, since the decay asks a character and a bare actor is not one.
 */
bool FCataclysmDeathSkillsBuffHoldsFervourTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDeathSkillCreature Holder(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Holder.Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
	Holder.Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
	DeathSkillSetFervour(Holder.Actor, 50.0f);
	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.FindOrAdd(FName(UCataclysmFervour::DecayPerSecondStat)).Base = 5.0f;
	Stats.FindOrAdd(FName(UCataclysmFervour::DecayGraceMetresStat)).Base = 4.0f;
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	UCataclysmFervour::DecayStep(Holder.Actor, 1.0f);
	if (!TestEqual(TEXT("control: out of contact, a second's step takes 5 of the 50"), DeathSkillFervourOf(Holder.Actor),
				   45.0f, DeathSkillAllowance))
	{
		return false;
	}

	UCataclysmSelfBuffSkill* Holding = GrantDeathSkill<UCataclysmSelfBuffSkill>(
		Holder.AbilitySystem, Holder.Actor, ECataclysmAbilitySlot::Support, TEXT("Duration=6; HoldsFervour=1"),
		TEXT("A buff that holds Fervour"));
	if (!TestNotNull(TEXT("set-up: the buff is granted"), Holding)
		|| !TestTrue(TEXT("the buff goes up"), ActivateDeathSkill(Holder.AbilitySystem, Holding)))
	{
		return false;
	}
	TestTrue(TEXT("a running buff holds the Fervour"), UCataclysmSkillTemplate::ARunningBuffHoldsFervour(Holder.Actor));
	UCataclysmFervour::DecayStep(Holder.Actor, 1.0f);
	TestEqual(TEXT("while it runs, a second's step takes nothing"), DeathSkillFervourOf(Holder.Actor), 45.0f,
			  DeathSkillAllowance);

	Holder.AbilitySystem->CancelAbilityHandle(Holding->GetCurrentAbilitySpecHandle());
	TestFalse(TEXT("cancelled, it holds nothing"), UCataclysmSkillTemplate::ARunningBuffHoldsFervour(Holder.Actor));
	UCataclysmFervour::DecayStep(Holder.Actor, 1.0f);
	TestEqual(TEXT("and the next step takes 5 again"), DeathSkillFervourOf(Holder.Actor), 40.0f, DeathSkillAllowance);
	return true;
}

// ==========================================================================
// Slice DS3, change B. N10: a flicker its deaths lengthen, re-gathering its circle. N5: a curse under which the dying
// rise.
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsFlickerLengthenedByDeathsTest,
	"Cataclysm.DeathSkills.AFlickerItsDeathsLengthenGainsHalfASecondAndItsFervourForEachDeathToItsCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N10 and N6 on a flicker (`UCataclysmMovementSkill::NoteADeathWhileItRuns`, told by `UCataclysmSkillTemplate::NoteKill`
 * as Butcher's Heat is): a flicker stating `Duration=4; Untargetable=1; DurationPer=0.5; ScalingSource=Kill;
 * MaxDuration=5; KillFervour=5` -- Danse Macabre's cell under a ceiling of 5 so it is reached in two -- runs 4 s and
 * leaves its caster untargetable for 4. Each death it is told of pays 5 Fervour and adds half a second, the dance and
 * the untargetability alike: 4.5, then 5; a third death pays 5 and adds nothing, the ceiling reached. THE CONTROL, 100 m
 * along +Y: a flicker stating none of the keys (Everywhere at Once's shape) is not told, runs its 4 s, and pays nothing.
 */
bool FCataclysmDeathSkillsFlickerLengthenedByDeathsTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Untargetable = UCataclysmSkillEffects::UntargetableTag();
	FDeathSkillFighter Dancer(World, FVector::ZeroVector);
	FDeathSkillFighter Partner(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f));
	DeathSkillSetFervour(Dancer.Actor, 0.0f);
	UCataclysmMovementSkill* Dance = GrantDeathSkill<UCataclysmMovementSkill>(
		Dancer.AbilitySystem, Dancer.Actor, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Flicker; Range=10; Duration=4; Interval=0.33; Untargetable=1; DurationPer=0.5; ScalingSource=Kill; "
			 "MaxDuration=5; KillFervour=5"),
		TEXT("A dance its deaths lengthen"));
	if (!TestTrue(TEXT("set-up: the untargetable tag"), Untargetable.IsValid())
		|| !TestNotNull(TEXT("set-up: the flicker is granted"), Dance)
		|| !TestTrue(TEXT("the dance begins"), ActivateDeathSkill(Dancer.AbilitySystem, Dance)))
	{
		return false;
	}
	TestEqual(TEXT("it runs 4 s"), Dance->FlickerSecondsInAll, 4.0f, DeathSkillAllowance);
	TestEqual(TEXT("and its dancer is untargetable for 4"), DeathSkillSecondsLeft(Dancer.Actor, Untargetable), 4.0f,
			  DeathSkillAllowance);

	TestEqual(TEXT("a death is told to the running dance"), UCataclysmSkillTemplate::NoteKill(Dancer.Actor), 1);
	TestEqual(TEXT("it pays 5 Fervour"), DeathSkillFervourOf(Dancer.Actor), 5.0f, DeathSkillAllowance);
	TestEqual(TEXT("and adds half a second: 4.5"), Dance->FlickerSecondsInAll, 4.5f, DeathSkillAllowance);
	TestEqual(TEXT("and the untargetability with it"), DeathSkillSecondsLeft(Dancer.Actor, Untargetable), 4.5f,
			  DeathSkillAllowance);

	UCataclysmSkillTemplate::NoteKill(Dancer.Actor);
	TestEqual(TEXT("a second death pays 5 more"), DeathSkillFervourOf(Dancer.Actor), 10.0f, DeathSkillAllowance);
	TestEqual(TEXT("and reaches the ceiling of 5 s"), Dance->FlickerSecondsInAll, 5.0f, DeathSkillAllowance);

	UCataclysmSkillTemplate::NoteKill(Dancer.Actor);
	TestEqual(TEXT("a third pays 5 more, the ceiling being on the time alone"), DeathSkillFervourOf(Dancer.Actor), 15.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("and adds nothing past the ceiling"), Dance->FlickerSecondsInAll, 5.0f, DeathSkillAllowance);
	TestEqual(TEXT("three deaths were heard"), Dance->DeathsDuringTheFlicker, 3);

	// THE CONTROL: a flicker stating none of the keys.
	FDeathSkillFighter Plain(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	FDeathSkillFighter PlainPartner(World, FVector(3.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f));
	DeathSkillSetFervour(Plain.Actor, 0.0f);
	UCataclysmMovementSkill* PlainDance = GrantDeathSkill<UCataclysmMovementSkill>(
		Plain.AbilitySystem, Plain.Actor, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Flicker; Range=10; Duration=4; Interval=0.33; Untargetable=1"), TEXT("A plain flicker"));
	if (!TestNotNull(TEXT("set-up: the control flicker is granted"), PlainDance)
		|| !TestTrue(TEXT("set-up: the control flicker begins"), ActivateDeathSkill(Plain.AbilitySystem, PlainDance)))
	{
		return false;
	}
	TestEqual(TEXT("control: a death is told to nothing"), UCataclysmSkillTemplate::NoteKill(Plain.Actor), 0);
	TestEqual(TEXT("control: it runs its 4 s"), PlainDance->FlickerSecondsInAll, 4.0f, DeathSkillAllowance);
	TestEqual(TEXT("control: and pays nothing"), DeathSkillFervourOf(Plain.Actor), 0.0f, DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsFlickerGathersAFreshCircleTest,
	"Cataclysm.DeathSkills.AFlickerItsDeathsLengthenGathersAFreshCircleOnceItsFirstHasFallen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N10's "re-gathering its circle" (`UCataclysmMovementSkill::FlickerOnce`, `GatherAFreshCircle`): a flicker its deaths
 * lengthen arrives at the one creature within its 10 m, 3 m along +X, which then dies; a second creature comes within
 * reach afterwards, 6 m from where the dancer stands, and the next arrival gathers a fresh circle and strikes it. THE
 * CONTROL, 100 m along +Y: a flicker stating none of the keys keeps the circle it gathered at the cast, and the next
 * arrival, finding nobody standing in it, goes nowhere and strikes the newcomer not at all.
 */
bool FCataclysmDeathSkillsFlickerGathersAFreshCircleTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDeathSkillFighter Dancer(World, FVector::ZeroVector);
	FDeathSkillCreature First(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmMovementSkill* Dance = GrantDeathSkill<UCataclysmMovementSkill>(
		Dancer.AbilitySystem, Dancer.Actor, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Flicker; Range=10; Duration=4; Interval=0.33; DurationPer=0.5; ScalingSource=Kill; MaxDuration=8"),
		TEXT("A dance that moves on"));
	if (!TestNotNull(TEXT("set-up: the flicker is granted"), Dance)
		|| !TestTrue(TEXT("the dance begins"), ActivateDeathSkill(Dancer.AbilitySystem, Dance))
		|| !TestEqual(TEXT("its first arrival is at the one creature in reach"), Dance->Arrivals, 1))
	{
		return false;
	}
	DeathSkillKillCreature(First);
	FDeathSkillCreature Second(World, FVector(3.0f * DeathSkillMetre, 6.0f * DeathSkillMetre, 0.0f),
							   ECataclysmTeam::Monsters);

	Dance->FlickerOnce();
	TestEqual(TEXT("with its circle fallen, the next arrival gathered a fresh one"), Dance->FreshCirclesGathered, 1);
	TestEqual(TEXT("and arrived"), Dance->Arrivals, 2);
	TestTrue(TEXT("at the newcomer, 6 m from where the dancer stood, and struck it"), DeathSkillHealthLost(Second) > 0.0f);

	// THE CONTROL: a flicker stating none of the keys.
	FDeathSkillFighter Plain(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	FDeathSkillCreature PlainFirst(World, FVector(3.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f),
								   ECataclysmTeam::Monsters);
	UCataclysmMovementSkill* PlainDance = GrantDeathSkill<UCataclysmMovementSkill>(
		Plain.AbilitySystem, Plain.Actor, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Flicker; Range=10; Duration=4; Interval=0.33"), TEXT("A flicker that keeps its circle"));
	if (!TestNotNull(TEXT("set-up: the control flicker is granted"), PlainDance)
		|| !TestTrue(TEXT("set-up: the control flicker begins"), ActivateDeathSkill(Plain.AbilitySystem, PlainDance))
		|| !TestEqual(TEXT("set-up: its first arrival"), PlainDance->Arrivals, 1))
	{
		return false;
	}
	DeathSkillKillCreature(PlainFirst);
	FDeathSkillCreature PlainSecond(World, FVector(3.0f * DeathSkillMetre, 106.0f * DeathSkillMetre, 0.0f),
									ECataclysmTeam::Monsters);
	PlainDance->FlickerOnce();
	TestEqual(TEXT("control: it gathers no fresh circle"), PlainDance->FreshCirclesGathered, 0);
	TestEqual(TEXT("control: and makes no arrival"), PlainDance->Arrivals, 1);
	TestEqual(TEXT("control: so the newcomer is untouched"), DeathSkillHealthLost(PlainSecond), 0.0f,
			  DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsRisingCurseTest,
	"Cataclysm.DeathSkills.ACreatureDyingUnderARisingCurseRisesAsItsKindReadFromTheRowThatRaisesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * N5 (`UCataclysmRisingCurse`): a Death curse stating `Effect=Cripple; EffectMagnitude=20; OnDeath=Rise; RisesAs=Husk`
 * -- Death Rattle's cell -- is laid on a creature 11 m off, and its death raises a husk where it fell, for the caster,
 * read from Shallow Graves' row: lasting its 8 s, reserving its 3 Fervour, exploding and taunting within its 4 m, so the
 * creature 2 m beyond, which the curse did not reach (13 m off), turns on it. THE CONTROLS: a creature under the same
 * curse whose Cripple is cleansed before it dies raises nothing -- "while it rattles" -- and a creature under the same
 * Cripple from a curse stating no `OnDeath=Rise` raises nothing.
 *
 * READS `DT_WeaponSkills` AND `DT_MinionTypes` for Shallow Graves and the Husk, which slice DS1 wrote; the set-up line
 * says so when the table is older than the CSV.
 */
bool FCataclysmDeathSkillsRisingCurseTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCataclysmWeaponSkill Raiser;
	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	const FGameplayTag Death = DeathSkillTagNamed(TEXT("Element.Death"));
	if (!TestTrue(TEXT("set-up: a Death Summon row raises the Husk with a Duration. If not, DT_WeaponSkills is older "
					   "than the CSV"), UCataclysmRisingCurse::FindTheRowThatRaises(TEXT("Death"), TEXT("Husk"), Raiser))
		|| !TestEqual(TEXT("set-up: it is Shallow Graves"), Raiser.Name, FString(TEXT("Shallow Graves")))
		|| !TestTrue(TEXT("set-up: the Cripple curse and the Death element"), Cripple.IsValid() && Death.IsValid()))
	{
		return false;
	}

	const TCHAR* const Rattle = TEXT("Range=12; Radius=5; MaxTargets=8; EffectDuration=8; Effect=Cripple; "
									 "EffectMagnitude=20; OnDeath=Rise; RisesAs=Husk");
	const auto GrantTheCurse = [](const FDeathSkillFighter& Cursing, const TCHAR* CurseCell, const TCHAR* CurseName)
	{
		UCataclysmDebuffSkill* Granted = GrantDeathSkill<UCataclysmDebuffSkill>(
			Cursing.AbilitySystem, Cursing.Actor, ECataclysmAbilitySlot::Support, CurseCell, CurseName);
		if (Granted)
		{
			// A DEATH CURSE, as its row's `Element.Death` tag makes it: the kind is read from a Death Summon row.
			Granted->SkillTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Element.Death")), false));
		}
		return Granted;
	};

	FDeathSkillCreature PlayersSide(World, FVector(0.0f, -100.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Players);
	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	DeathSkillSideWithThePlayers(Caster, PlayersSide);
	FDeathSkillCreature Rattled(World, FVector(11.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Beyond(World, FVector(13.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmDebuffSkill* Curse = GrantTheCurse(Caster, Rattle, TEXT("A rising curse"));
	TArray<ACataclysmMinion*> Risen;
	ON_SCOPE_EXIT { DeathSkillClear(Risen); };
	if (!TestNotNull(TEXT("set-up: the curse is granted"), Curse)
		|| !TestTrue(TEXT("the curse is laid"), ActivateDeathSkill(Caster.AbilitySystem, Curse))
		|| !TestTrue(TEXT("on the creature 11 m off"), Rattled.AbilitySystem->HasMatchingGameplayTag(Cripple))
		|| !TestFalse(TEXT("and not the one 13 m off, beyond its 12 m"), Beyond.AbilitySystem->HasMatchingGameplayTag(Cripple)))
	{
		return false;
	}
	const FVector DiedAt = Rattled.Actor->GetActorLocation();

	DeathSkillKillCreature(Rattled);
	Risen = UCataclysmCommand::MinionsOfTypeCommandedBy(Caster.Actor, TEXT("Husk"));
	if (!TestEqual(TEXT("its death raised one husk for the caster. If none, DT_MinionTypes is older than the CSV"),
				   Risen.Num(), 1))
	{
		return false;
	}
	ACataclysmMinion* Husk = Risen[0];
	TestTrue(TEXT("where the creature fell"), FVector::Dist2D(Husk->GetActorLocation(), DiedAt) < 1.0f);
	TestEqual(TEXT("lasting Shallow Graves' 8 s"), Husk->GetLifeSpan(), 8.0f, DeathSkillAllowance);
	TestEqual(TEXT("reserving its 3 Fervour"), Husk->ReservedFervour, 3.0f, DeathSkillAllowance);
	TestEqual(TEXT("taunting within its 4 m"), Husk->TauntRadiusCm, 4.0f * DeathSkillMetre, DeathSkillAllowance);
	TestTrue(TEXT("so the creature 2 m beyond turns on it"), UCataclysmTaunt::TaunterOf(Beyond.Actor) == Husk);

	// CONTROL: the same curse, cleansed before the death.
	FDeathSkillCreature PlayersSideToo(World, FVector(0.0f, 300.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Players);
	FDeathSkillFighter Lifter(World, FVector(0.0f, 100.0f * DeathSkillMetre, 0.0f));
	DeathSkillSideWithThePlayers(Lifter, PlayersSideToo);
	FDeathSkillCreature Lifted(World, FVector(11.0f * DeathSkillMetre, 100.0f * DeathSkillMetre, 0.0f),
							   ECataclysmTeam::Monsters);
	UCataclysmDebuffSkill* LiftedCurse = GrantTheCurse(Lifter, Rattle, TEXT("A rising curse lifted"));
	if (!TestNotNull(TEXT("set-up: the control curse is granted"), LiftedCurse)
		|| !TestTrue(TEXT("set-up: the control curse is laid"), ActivateDeathSkill(Lifter.AbilitySystem, LiftedCurse))
		|| !TestTrue(TEXT("set-up: on the control creature"), Lifted.AbilitySystem->HasMatchingGameplayTag(Cripple)))
	{
		return false;
	}
	Lifted.AbilitySystem->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(Cripple));
	DeathSkillKillCreature(Lifted);
	TestEqual(TEXT("control: a creature whose curse was cleansed raises nothing"),
			  UCataclysmCommand::MinionsOfTypeCommandedBy(Lifter.Actor, TEXT("Husk")).Num(), 0);

	// CONTROL: the same Cripple from a curse stating no rise.
	FDeathSkillFighter Plain(World, FVector(0.0f, 200.0f * DeathSkillMetre, 0.0f));
	DeathSkillSideWithThePlayers(Plain, PlayersSideToo);
	FDeathSkillCreature PlainCursed(World, FVector(11.0f * DeathSkillMetre, 200.0f * DeathSkillMetre, 0.0f),
									ECataclysmTeam::Monsters);
	UCataclysmDebuffSkill* PlainCurse = GrantTheCurse(
		Plain, TEXT("Range=12; Radius=5; MaxTargets=8; EffectDuration=8; Effect=Cripple; EffectMagnitude=20"),
		TEXT("A curse that raises nothing"));
	if (!TestNotNull(TEXT("set-up: the plain curse is granted"), PlainCurse)
		|| !TestTrue(TEXT("set-up: the plain curse is laid"), ActivateDeathSkill(Plain.AbilitySystem, PlainCurse))
		|| !TestTrue(TEXT("set-up: on its creature"), PlainCursed.AbilitySystem->HasMatchingGameplayTag(Cripple)))
	{
		return false;
	}
	DeathSkillKillCreature(PlainCursed);
	TestEqual(TEXT("control: a creature under a curse stating no rise raises nothing"),
			  UCataclysmCommand::MinionsOfTypeCommandedBy(Plain.Actor, TEXT("Husk")).Num(), 0);
	return true;
}

// ==========================================================================
// Slice DS3, change C: the nine rows its pieces complete, each cast from its own generated row
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsDeathWarrantTest,
	"Cataclysm.DeathSkills.DeathWarrantCripplesOneEnemyTenSecondsAndItsDeathPaysTenAndPassesTheWarrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Sword's Death Warrant, its own row: "Sign an enemy's death warrant from up to 15 meters away. For 10 seconds it is
 * Crippled, moving and attacking 30% slower. If it dies while the warrant holds, you gain 10 Fervour and the warrant
 * passes to the nearest enemy within 10 meters." The nearest creature, 5 m off, is Crippled at 30 for 10 s; THE
 * CONTROL, the creature 4 m beyond it, is not, the row taking one. The warranted creature's death pays 10 and the
 * warrant passes to that creature.
 */
bool FCataclysmDeathSkillsDeathWarrantTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Sword"), ECataclysmAbilitySlot::Special);
	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestEqual(TEXT("set-up: the Death Sword's Special row is Death Warrant. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Death Warrant")))
		|| !TestTrue(TEXT("set-up: the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillCreature Warranted(World, FVector(5.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature NextNearest(World, FVector(5.0f * DeathSkillMetre, 4.0f * DeathSkillMetre, 0.0f),
									ECataclysmTeam::Monsters);
	DeathSkillSetFervour(Caster.Actor, 0.0f);
	UCataclysmDebuffSkill* Warrant = GrantDeathRow<UCataclysmDebuffSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Warrant)
		|| !TestTrue(TEXT("the warrant is signed"), ActivateDeathSkill(Caster.AbilitySystem, Warrant)))
	{
		return false;
	}
	TestTrue(TEXT("the nearest creature is Crippled"), Warranted.AbilitySystem->HasMatchingGameplayTag(Cripple));
	TestEqual(TEXT("at the row's 30"), UCataclysmSkillEffects::StatedStrengthOn(Warranted.Actor, Cripple), 30.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("for the row's 10 seconds"), DeathSkillSecondsLeft(Warranted.Actor, Cripple), 10.0f,
			  DeathSkillAllowance);
	TestFalse(TEXT("control: the creature 4 m beyond it is not: the row takes one"),
			  NextNearest.AbilitySystem->HasMatchingGameplayTag(Cripple));

	DeathSkillKillCreature(Warranted);
	TestTrue(TEXT("set-up: the warranted creature died"), UCataclysmSkillEffects::IsDead(Warranted.Actor));
	TestEqual(TEXT("its death under the warrant paid 10 Fervour"), DeathSkillFervourOf(Caster.Actor), 10.0f,
			  DeathSkillAllowance);
	TestTrue(TEXT("and the warrant passed to the nearest enemy within 10 m of it"),
			 NextNearest.AbilitySystem->HasMatchingGameplayTag(Cripple));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsHungeringSoulTest,
	"Cataclysm.DeathSkills.HungeringSoulLodgesWithNecrosisAndLeapsEightMetresOnItsHostsDeathPayingTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Wand's Hungering Soul, its own row: "It lodges in it, giving it Necrosis, and when its host dies it tears free and
 * flies into the nearest living enemy within 8 meters, again and again until nothing is left in reach. Every host it
 * leaves dead gives you 2 Fervour." The soul lodges in the creature 5 m along +X and gives it Necrosis; that host's
 * death pays 2, and the soul leaps 6 m into the next and gives it Necrosis. THE CONTROL: the creature 9 m beyond that
 * one, 15 m from the first host, is out of every reach and carries nothing.
 */
bool FCataclysmDeathSkillsHungeringSoulTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Wand"), ECataclysmAbilitySlot::Heavy);
	const FGameplayTag Necrosis = DeathSkillTagNamed(TEXT("Keyword.DoT.Necrosis"));
	if (!TestEqual(TEXT("set-up: the Death Wand's Heavy row is Hungering Soul. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Hungering Soul")))
		|| !TestTrue(TEXT("set-up: the vocabulary has Necrosis's tag"), Necrosis.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillCreature Host(World, FVector(5.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Next(World, FVector(5.0f * DeathSkillMetre, 6.0f * DeathSkillMetre, 0.0f),
							 ECataclysmTeam::Monsters);
	FDeathSkillCreature OutOfReach(World, FVector(5.0f * DeathSkillMetre, 15.0f * DeathSkillMetre, 0.0f),
								   ECataclysmTeam::Monsters);
	DeathSkillSetFervour(Caster.Actor, 0.0f);
	UCataclysmProjectileSkill* Soul = GrantDeathRow<UCataclysmProjectileSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Soul)
		|| !TestTrue(TEXT("the soul is loosed"), ActivateDeathSkill(Caster.AbilitySystem, Soul)))
	{
		return false;
	}
	DeathSkillFlyToTheEnd(Soul->InFlight.Get());
	TestTrue(TEXT("its host, 5 m along, carries Necrosis"), Host.AbilitySystem->HasMatchingGameplayTag(Necrosis));
	if (!TestNotNull(TEXT("and the soul lodged in it"), Host.Actor->FindComponentByClass<UCataclysmBuriedWeapon>()))
	{
		return false;
	}

	DeathSkillKillCreature(Host);
	TestEqual(TEXT("the host it left dead paid 2"), DeathSkillFervourOf(Caster.Actor), 2.0f, DeathSkillAllowance);
	TestTrue(TEXT("the soul leapt 6 m into the next, within its 8, and gave it Necrosis"),
			 Next.AbilitySystem->HasMatchingGameplayTag(Necrosis));
	TestFalse(TEXT("control: the creature 15 m from the host is out of reach and carries nothing"),
			  OutOfReach.AbilitySystem->HasMatchingGameplayTag(Necrosis));
	TestEqual(TEXT("control: nor was it struck"), DeathSkillHealthLost(OutOfReach), 0.0f, DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsRestlessSpiritsTest,
	"Cataclysm.DeathSkills.RestlessSpiritsFlyAtTheNearestWithNecrosisAndAKillPaysOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Wand's Restless Spirits, its own row: "Let six restless spirits loose over 2 seconds. Each flies at the nearest
 * enemy within 12 meters for 25% weapon damage and gives it Necrosis, and every enemy a spirit kills gives you 1
 * Fervour." The first spirit flies at the nearest creature, 4 m off and of 1 health, and kills it: 1 Fervour. The next
 * flies at the nearest still standing, 8 m off, and gives it Necrosis; THE CONTROL, it lives, and nothing more is paid.
 * Each spirit is thrown by hand (`ThrowOne`), as the rack's timer would throw it.
 */
bool FCataclysmDeathSkillsRestlessSpiritsTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Wand"), ECataclysmAbilitySlot::Special);
	const FGameplayTag Necrosis = DeathSkillTagNamed(TEXT("Keyword.DoT.Necrosis"));
	if (!TestEqual(TEXT("set-up: the Death Wand's Special row is Restless Spirits. If empty, DT_WeaponSkills is older "
						"than the CSV"), Row.Name, FString(TEXT("Restless Spirits")))
		|| !TestTrue(TEXT("set-up: the vocabulary has Necrosis's tag"), Necrosis.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillCreature Weak(World, FVector(4.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Sturdy(World, FVector(8.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	Weak.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	DeathSkillSetFervour(Caster.Actor, 0.0f);
	UCataclysmProjectileSkill* Spirits =
		GrantDeathRow<UCataclysmProjectileSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Spirits))
	{
		return false;
	}

	TestTrue(TEXT("the first spirit is let loose"), Spirits->ThrowOne());
	DeathSkillFlyEveryShot(World);
	TestTrue(TEXT("it killed the nearest creature, of 1 health"), UCataclysmSkillEffects::IsDead(Weak.Actor));
	TestEqual(TEXT("which paid 1 Fervour"), DeathSkillFervourOf(Caster.Actor), 1.0f, DeathSkillAllowance);

	TestTrue(TEXT("the second spirit is let loose"), Spirits->ThrowOne());
	DeathSkillFlyEveryShot(World);
	TestTrue(TEXT("at the nearest still standing, 8 m off, which carries Necrosis"),
			 Sturdy.AbilitySystem->HasMatchingGameplayTag(Necrosis));
	TestFalse(TEXT("control: it lives"), UCataclysmSkillEffects::IsDead(Sturdy.Actor));
	TestEqual(TEXT("control: so nothing more is paid"), DeathSkillFervourOf(Caster.Actor), 1.0f, DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsLastTollTest,
	"Cataclysm.DeathSkills.LastTollDragsTheTwelveMetreRingInWithNecrosisAndPaysFiveForEachKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Whip's Last Toll, its own row: "Crack the whip in a ring 12 meters wide. Every enemy it catches is dragged to your
 * feet for 350% weapon damage, given Necrosis and knocked down for 1.5 seconds, and every enemy it kills gives you 5
 * Fervour." The ring kills two creatures of 1 health, 5 m and 7 m off on either side: 10 Fervour. A creature of 100000,
 * 9 m off, is dragged in and given Necrosis. THE CONTROL, 13 m off: beyond the ring, untouched. Swung by hand
 * (`SwingOnce`), so the Ultimate's 50 Fervour is not spent and only what the kills pay is counted.
 */
bool FCataclysmDeathSkillsLastTollTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Whip"), ECataclysmAbilitySlot::Ultimate);
	const FGameplayTag Necrosis = DeathSkillTagNamed(TEXT("Keyword.DoT.Necrosis"));
	if (!TestEqual(TEXT("set-up: the Death Whip's Ultimate row is Last Toll. If empty, DT_WeaponSkills is older than the "
						"CSV"), Row.Name, FString(TEXT("Last Toll")))
		|| !TestTrue(TEXT("set-up: the vocabulary has Necrosis's tag"), Necrosis.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	FDeathSkillCreature WeakAhead(World, FVector(5.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature WeakBehind(World, FVector(-7.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Sturdy(World, FVector(0.0f, 9.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Beyond(World, FVector(0.0f, -13.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Monsters);
	WeakAhead.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	WeakBehind.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	DeathSkillSetFervour(Caster.Actor, 0.0f);
	UCataclysmStrikeSkill* Toll = GrantDeathRow<UCataclysmStrikeSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Toll))
	{
		return false;
	}

	Toll->SwingOnce();
	TestTrue(TEXT("the creature of 1 health 5 m ahead died"), UCataclysmSkillEffects::IsDead(WeakAhead.Actor));
	TestTrue(TEXT("and the one 7 m behind"), UCataclysmSkillEffects::IsDead(WeakBehind.Actor));
	TestEqual(TEXT("two kills at 5 each: 10 Fervour"), DeathSkillFervourOf(Caster.Actor), 10.0f, DeathSkillAllowance);
	TestTrue(TEXT("the creature of 100000, 9 m off, was dragged in"),
			 FVector::Dist2D(Sturdy.Actor->GetActorLocation(), FVector::ZeroVector) < 8.5f * DeathSkillMetre);
	TestTrue(TEXT("and given Necrosis"), Sturdy.AbilitySystem->HasMatchingGameplayTag(Necrosis));
	TestEqual(TEXT("control: the creature 13 m off, beyond the ring, is untouched"), DeathSkillHealthLost(Beyond), 0.0f,
			  DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsPaleRiderTest,
	"Cataclysm.DeathSkills.PaleRiderCripplesWhatItRunsThroughByFiftyAndAKillPaysFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Spear's Pale Rider, its own row: "You charge 14 meters in 1.5 seconds and nothing turns you aside: everything in
 * your path is run through for 300% weapon damage and Crippled, moving and attacking 50% slower for 4 seconds. Every
 * enemy it kills gives you 5 Fervour." The ride is begun from 60 Fervour, the Ultimate's 50 spent; a step 1.5 m along
 * runs through a creature of 1 health, which dies (5 Fervour: 15 held), and one of 100000 beside it, Crippled at 50 for
 * 4 s. THE CONTROL: a creature 5 m off the path, beyond its 2.5 m, is not run through.
 */
bool FCataclysmDeathSkillsPaleRiderTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Spear"), ECataclysmAbilitySlot::Ultimate);
	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestEqual(TEXT("set-up: the Death Spear's Ultimate row is Pale Rider. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Pale Rider")))
		|| !TestTrue(TEXT("set-up: the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	FDeathSkillFighter Rider(World, FVector::ZeroVector);
	FDeathSkillCreature Weak(World, FVector(1.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Sturdy(World, FVector(1.0f * DeathSkillMetre, 1.5f * DeathSkillMetre, 0.0f),
							   ECataclysmTeam::Monsters);
	FDeathSkillCreature OffThePath(World, FVector(1.0f * DeathSkillMetre, 5.0f * DeathSkillMetre, 0.0f),
								   ECataclysmTeam::Monsters);
	Weak.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	DeathSkillSetFervour(Rider.Actor, 60.0f);
	UCataclysmMovementSkill* Ride = GrantDeathRow<UCataclysmMovementSkill>(Rider.AbilitySystem, Rider.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Ride)
		|| !TestTrue(TEXT("the ride begins"), ActivateDeathSkill(Rider.AbilitySystem, Ride))
		|| !TestEqual(TEXT("set-up: the Ultimate's 50 Fervour is spent"), DeathSkillFervourOf(Rider.Actor), 10.0f,
					  DeathSkillAllowance))
	{
		return false;
	}

	Rider.Actor->SetActorLocation(Rider.Actor->GetActorLocation() + FVector(150.0f, 0.0f, 0.0f));
	Ride->AdvanceOneStep();
	TestTrue(TEXT("the creature of 1 health it ran through died"), UCataclysmSkillEffects::IsDead(Weak.Actor));
	TestEqual(TEXT("which paid 5: 15 held"), DeathSkillFervourOf(Rider.Actor), 15.0f, DeathSkillAllowance);
	TestTrue(TEXT("the one beside it is Crippled"), Sturdy.AbilitySystem->HasMatchingGameplayTag(Cripple));
	TestEqual(TEXT("at the row's 50"), UCataclysmSkillEffects::StatedStrengthOn(Sturdy.Actor, Cripple), 50.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("for the row's 4 seconds"), DeathSkillSecondsLeft(Sturdy.Actor, Cripple), 4.0f, DeathSkillAllowance);
	TestFalse(TEXT("control: the creature 5 m off the path is not run through"),
			  OffThePath.AbilitySystem->HasMatchingGameplayTag(Cripple));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsDanseMacabreTest,
	"Cataclysm.DeathSkills.DanseMacabreDancesFourSecondsAndEachDeathPaysFiveAndAddsHalfASecondToEight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Sword's Danse Macabre, its own row: "Dance with death for 4 seconds ... Every enemy that dies during the dance
 * gives you 5 Fervour and adds half a second to it, up to 8 seconds in all." Begun from 60 Fervour, the Ultimate's 50
 * spent, it runs 4 s and arrives at the one enemy within its 10 m. Eight deaths carry it to its 8 s; THE CONTROL, a ninth
 * adds nothing, the ceiling reached, and still pays its 5: 10 and 45 is 55. The deaths are told as the enemy death path
 * tells them (`UCataclysmSkillTemplate::NoteKill`).
 */
bool FCataclysmDeathSkillsDanseMacabreTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Sword"), ECataclysmAbilitySlot::Ultimate);
	if (!TestEqual(TEXT("set-up: the Death Sword's Ultimate row is Danse Macabre. If empty, DT_WeaponSkills is older "
						"than the CSV"), Row.Name, FString(TEXT("Danse Macabre"))))
	{
		return false;
	}

	FDeathSkillFighter Dancer(World, FVector::ZeroVector);
	FDeathSkillFighter Partner(World, FVector(3.0f * DeathSkillMetre, 0.0f, 0.0f));
	DeathSkillSetFervour(Dancer.Actor, 60.0f);
	UCataclysmMovementSkill* Dance = GrantDeathRow<UCataclysmMovementSkill>(Dancer.AbilitySystem, Dancer.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Dance)
		|| !TestTrue(TEXT("the dance begins"), ActivateDeathSkill(Dancer.AbilitySystem, Dance))
		|| !TestEqual(TEXT("set-up: the Ultimate's 50 Fervour is spent"), DeathSkillFervourOf(Dancer.Actor), 10.0f,
					  DeathSkillAllowance))
	{
		return false;
	}
	TestEqual(TEXT("it runs the row's 4 s"), Dance->FlickerSecondsInAll, 4.0f, DeathSkillAllowance);
	TestEqual(TEXT("and arrived at the one enemy within its 10 m"), Dance->Arrivals, 1);

	for (int32 Death = 0; Death < 8; ++Death)
	{
		UCataclysmSkillTemplate::NoteKill(Dancer.Actor);
	}
	TestEqual(TEXT("eight deaths carry it to its 8 s"), Dance->FlickerSecondsInAll, 8.0f, DeathSkillAllowance);
	UCataclysmSkillTemplate::NoteKill(Dancer.Actor);
	TestEqual(TEXT("control: a ninth adds nothing past the ceiling"), Dance->FlickerSecondsInAll, 8.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("and each of the nine paid 5: 55 held"), DeathSkillFervourOf(Dancer.Actor), 55.0f,
			  DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsGrimSwathTest,
	"Cataclysm.DeathSkills.GrimSwathMowsHalfAgainAsHardAtAHundredFervourToItsCeilingAndAKillPaysTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Greataxe's Grim Swath, its own row: "Mow a wide swath in front of you for 200% weapon damage, 1% more for every 2
 * Fervour you hold, to at most 350%. Every enemy it kills gives you 2 more Fervour." Each swing a fresh caster and
 * creature 2 m ahead, 100 m apart: at 100 Fervour it strikes half again as hard as at none (300% against 200%), and at
 * 150 for its 350% ceiling, 1.75 times. A swing from no Fervour that kills a creature of 1 health pays 2; THE CONTROL is
 * the swing at none, whose 200% the others are read against.
 */
bool FCataclysmDeathSkillsGrimSwathTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Greataxe"), ECataclysmAbilitySlot::Heavy);
	if (!TestEqual(TEXT("set-up: the Death Greataxe's Heavy row is Grim Swath. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Grim Swath"))))
	{
		return false;
	}

	const auto LostAt = [World, &Row](float HeldFervour, float AcrossMetres) -> float
	{
		FDeathSkillFighter Mower(World, FVector(0.0f, AcrossMetres * DeathSkillMetre, 0.0f));
		FDeathSkillCreature Mown(World, FVector(2.0f * DeathSkillMetre, AcrossMetres * DeathSkillMetre, 0.0f),
								 ECataclysmTeam::Monsters);
		DeathSkillSetFervour(Mower.Actor, HeldFervour);
		UCataclysmStrikeSkill* Swath = GrantDeathRow<UCataclysmStrikeSkill>(Mower.AbilitySystem, Mower.Actor, Row);
		if (!Swath)
		{
			return -1.0f;
		}
		Swath->SwingOnce();
		return DeathSkillHealthLost(Mown);
	};

	const float AtNone = LostAt(0.0f, 0.0f);
	if (!TestTrue(TEXT("control: the swath at no Fervour strikes for its 200%"), AtNone > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("at 100 Fervour, half again as hard: 300% against 200%"), LostAt(100.0f, 100.0f) / AtNone, 1.5f,
			  DeathSkillAllowance);
	TestEqual(TEXT("at 150 Fervour, its 350% ceiling: 1.75 times"), LostAt(150.0f, 200.0f) / AtNone, 1.75f,
			  DeathSkillAllowance);

	FDeathSkillFighter Reaper(World, FVector(0.0f, 300.0f * DeathSkillMetre, 0.0f));
	FDeathSkillCreature Weak(World, FVector(2.0f * DeathSkillMetre, 300.0f * DeathSkillMetre, 0.0f),
							 ECataclysmTeam::Monsters);
	Weak.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	DeathSkillSetFervour(Reaper.Actor, 0.0f);
	UCataclysmStrikeSkill* Killing = GrantDeathRow<UCataclysmStrikeSkill>(Reaper.AbilitySystem, Reaper.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted to the reaper"), Killing))
	{
		return false;
	}
	Killing->SwingOnce();
	TestTrue(TEXT("its swath killed the creature of 1 health"), UCataclysmSkillEffects::IsDead(Weak.Actor));
	TestEqual(TEXT("which paid 2 Fervour"), DeathSkillFervourOf(Reaper.Actor), 2.0f, DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsColdBloodTest,
	"Cataclysm.DeathSkills.ColdBloodGrantsAPercentMoreForEachFourFervourHeldForSixSecondsAndHoldsItFromDecay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Sword's Cold Blood, its own row: "Let your blood run cold for 6 seconds. While it lasts your Fervour does not
 * decay, and your hits deal 1% more damage for every 4 Fervour you hold." A character given the Ravager's decay -- THE
 * CONTROL: a second's step out of contact takes 5 of its 100 -- casts it at 100 Fervour: it runs 6 s, grants 25% more
 * damage, and the next step takes nothing.
 */
bool FCataclysmDeathSkillsColdBloodTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Sword"), ECataclysmAbilitySlot::Support);
	if (!TestEqual(TEXT("set-up: the Death Sword's Support row is Cold Blood. If empty, DT_WeaponSkills is older than "
						"the CSV"), Row.Name, FString(TEXT("Cold Blood"))))
	{
		return false;
	}

	FDeathSkillCreature Holder(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Holder.Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
	Holder.Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.FindOrAdd(FName(UCataclysmFervour::DecayPerSecondStat)).Base = 5.0f;
	Stats.FindOrAdd(FName(UCataclysmFervour::DecayGraceMetresStat)).Base = 4.0f;
	Holder.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	DeathSkillSetFervour(Holder.Actor, 100.0f);
	UCataclysmFervour::DecayStep(Holder.Actor, 1.0f);
	if (!TestEqual(TEXT("control: out of contact, a second's step takes 5"), DeathSkillFervourOf(Holder.Actor), 95.0f,
				   DeathSkillAllowance))
	{
		return false;
	}

	DeathSkillSetFervour(Holder.Actor, 100.0f);
	UCataclysmSelfBuffSkill* Blood = GrantDeathRow<UCataclysmSelfBuffSkill>(Holder.AbilitySystem, Holder.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted"), Blood)
		|| !TestTrue(TEXT("the blood runs cold"), ActivateDeathSkill(Holder.AbilitySystem, Blood)))
	{
		return false;
	}
	TestEqual(TEXT("for the row's 6 seconds"), Blood->TotalDuration, 6.0f, DeathSkillAllowance);
	TestEqual(TEXT("at 100 Fervour it grants 25% more damage"), Blood->GrantedIncrease, 25.0f, DeathSkillAllowance);
	UCataclysmFervour::DecayStep(Holder.Actor, 1.0f);
	TestEqual(TEXT("and while it runs, a second's step takes nothing"), DeathSkillFervourOf(Holder.Actor), 100.0f,
			  DeathSkillAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathSkillsDeathRattleTest,
	"Cataclysm.DeathSkills.DeathRattleCripplesWithinTwelveMetresByTwentyAndTheRattledDeadRiseAsHusks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Greataxe's Death Rattle, its own row: "For 8 seconds each is Crippled, moving and attacking 20% slower, and any
 * that dies while it rattles rises at once as a husk, the same husk Shallow Graves raises ... Each husk lasts 8 seconds
 * and reserves Fervour while it stands." Two creatures, 6 m and about 6.3 m off, are Crippled at 20 for 8 s, and THE
 * CONTROL 13 m off, beyond the curse's 12 m, is not. The first's death raises a husk where it fell, lasting 8 s,
 * reserving Shallow Graves' 3 and taunting within its 4 m; the second still stands, so one husk stands.
 */
bool FCataclysmDeathSkillsDeathRattleTest::RunTest(const FString&)
{
	using namespace CataclysmDeathSkillsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = DeathRowOf(TEXT("Greataxe"), ECataclysmAbilitySlot::Support);
	const FGameplayTag Cripple = DeathSkillStatus(TEXT("Cripple"));
	if (!TestEqual(TEXT("set-up: the Death Greataxe's Support row is Death Rattle. If empty, DT_WeaponSkills is older "
						"than the CSV"), Row.Name, FString(TEXT("Death Rattle")))
		|| !TestTrue(TEXT("set-up: the Cripple curse"), Cripple.IsValid()))
	{
		return false;
	}

	FDeathSkillCreature PlayersSide(World, FVector(0.0f, -100.0f * DeathSkillMetre, 0.0f), ECataclysmTeam::Players);
	FDeathSkillFighter Caster(World, FVector::ZeroVector);
	DeathSkillSideWithThePlayers(Caster, PlayersSide);
	FDeathSkillCreature First(World, FVector(6.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FDeathSkillCreature Second(World, FVector(6.0f * DeathSkillMetre, 2.0f * DeathSkillMetre, 0.0f),
							   ECataclysmTeam::Monsters);
	FDeathSkillCreature Beyond(World, FVector(13.0f * DeathSkillMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	UCataclysmDebuffSkill* Rattle = GrantDeathRow<UCataclysmDebuffSkill>(Caster.AbilitySystem, Caster.Actor, Row);
	TArray<ACataclysmMinion*> Husks;
	ON_SCOPE_EXIT { DeathSkillClear(Husks); };
	if (!TestNotNull(TEXT("set-up: the row is granted"), Rattle)
		|| !TestTrue(TEXT("the bones are rattled"), ActivateDeathSkill(Caster.AbilitySystem, Rattle)))
	{
		return false;
	}
	TestTrue(TEXT("the creature 6 m off is Crippled"), First.AbilitySystem->HasMatchingGameplayTag(Cripple));
	TestEqual(TEXT("at the row's 20"), UCataclysmSkillEffects::StatedStrengthOn(First.Actor, Cripple), 20.0f,
			  DeathSkillAllowance);
	TestEqual(TEXT("for the row's 8 seconds"), DeathSkillSecondsLeft(First.Actor, Cripple), 8.0f, DeathSkillAllowance);
	TestTrue(TEXT("and so is the one beside it, up to 8 being taken"),
			 Second.AbilitySystem->HasMatchingGameplayTag(Cripple));
	TestFalse(TEXT("control: the creature 13 m off, beyond the 12 m, is not"),
			  Beyond.AbilitySystem->HasMatchingGameplayTag(Cripple));

	const FVector DiedAt = First.Actor->GetActorLocation();
	DeathSkillKillCreature(First);
	Husks = UCataclysmCommand::MinionsOfTypeCommandedBy(Caster.Actor, TEXT("Husk"));
	if (!TestEqual(TEXT("its death raised one husk for the caster, the other still standing. If none, DT_MinionTypes "
						"is older than the CSV"), Husks.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("where the creature fell"), FVector::Dist2D(Husks[0]->GetActorLocation(), DiedAt) < 1.0f);
	TestEqual(TEXT("lasting 8 seconds"), Husks[0]->GetLifeSpan(), 8.0f, DeathSkillAllowance);
	TestEqual(TEXT("reserving Shallow Graves' 3 Fervour"), Husks[0]->ReservedFervour, 3.0f, DeathSkillAllowance);
	TestEqual(TEXT("and taunting within its 4 m"), Husks[0]->TauntRadiusCm, 4.0f * DeathSkillMetre, DeathSkillAllowance);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
