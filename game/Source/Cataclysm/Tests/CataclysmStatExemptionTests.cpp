// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
// For the eleven probes that prove every scaled stat is asked for. #1973.
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmItem.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The promise `UCataclysmPlayerClassStats::StatsWithNoAttribute()` makes, kept.
 * Issues #898, #1025, #1733 and #1791.
 *
 * WHAT THE LIST CLAIMS. The stats on it deliberately have no gameplay attribute,
 * and bespoke code reads them directly instead: the three minion stats for their
 * increases, and `mana_on_hit` for whether it is removed. Several checks stop
 * refusing a row that names them because of that claim: the data generator's
 * vocabulary, `test_every_stat_is_one_the_game_supplies`, and the engine tests
 * that every stat a passive node, an enchantment or an affix grants has an
 * attribute behind it.
 *
 * AN EXEMPTION IS A PROMISE AND A PROMISE CAN GO UNKEPT, WHICH IS ISSUE #1025.
 * `ENGINE_SUPPLIED_BASES` once exempted `damage_to_bleeding_window` and named the
 * code supplying its base. No code ever applied it. The stat's base really was
 * zero, The Breaking Point opened a conversion window of zero seconds and
 * converted nothing for as long as the node existed, and five further nodes that
 * counted on it were starved. **One inert exemption, six dead nodes.** Nothing
 * held the other side of the bargain.
 *
 * THIS IS THAT OTHER SIDE, AND IT HAS TO OBSERVE BEHAVIOUR RATHER THAN EXISTENCE.
 * `test_every_engine_supplied_base_names_code_that_exists` says in its own words
 * why a source search is not enough: "It CANNOT check that the code is ever
 * called, which is exactly what went wrong -- `BaseWindowSeconds` existed the
 * whole time." So every probe below grants the stat and asserts that the reading
 * code's ANSWER CHANGES.
 *
 * THE DISCRIMINATING STEP IS THE MISSING PROBE. A name added to the shared list
 * with nothing reading it fails here, by name, before any probe runs. That is the
 * check #1025 lacked, and adding a name that nothing reads is how this test is
 * proved to work.
 *
 * WHY NOT A CHECK THAT A TEST EXISTS FOR EACH NAME. Because a test existing is
 * not a test covering: a suite can hold a test whose name mentions a stat and
 * whose body never grants it. The probes are here, in this file, where what they
 * measure can be read.
 */
namespace CataclysmStatExemptionTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	/** Centimetres in a metre, so a case can place a character in metres. */
	constexpr float M = 100.0f;

	/** What each minion probe grants; the mana on hit probe removes its stat
	 *  instead. Not any affix's top roll, so a reading that matched one could
	 *  only have come from the data. */
	constexpr float IncreasePercent = 40.0f;

	/** Small enough that a blow can be read as a difference of two health
	 *  readings. A float near 1,000,000 steps in units of 0.0625 and cannot
	 *  resolve one; 10,000 steps in 0.0009766. Issue #1728. */
	constexpr float TargetHealthPool = 10'000.0f;

	/** A character carrying the attributes a blow needs at both ends. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, float AttackDamage)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResist =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAllResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(
				Vital::GetMaxHealthAttribute(), TargetHealthPool);
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetHealthAttribute(), TargetHealthPool);
			AbilitySystem->SetNumericAttributeBase(
				Combat::GetAttackDamageAttribute(), AttackDamage);
		}

		~FScopedFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Health() const
		{
			const UAbilitySystemComponent* System =
				UCataclysmTargeting::AbilitySystemOf(Actor);
			return System
				? System->GetNumericAttribute(Vital::GetHealthAttribute())
				: 0.0f;
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Give a character the stat line a player would have after equipping gear
	 * granting one of the exempt stats.
	 *
	 * A BASE OF NOTHING, DELIBERATELY. These stats have no base and can have
	 * none. Giving one a base here would let a probe pass against code that read
	 * the stat's VALUE, which is the mistake the whole exemption exists around.
	 */
	void Grant(AActor* Who, const FString& Stat, float Percent)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Who));
		if (!System)
		{
			return;
		}

		FCataclysmStatModifier Increase;
		Increase.Bucket = ECataclysmStatBucket::Increased;
		Increase.Source = ECataclysmModifierSource::GearAffix;
		Increase.Value = Percent;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(*Stat));
		Line.Base = 0.0f;
		Line.Modifiers = {Increase};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	float MaxHealthOf(const ACataclysmMinion* Minion)
	{
		const UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Minion);
		return System
			? System->GetNumericAttribute(Vital::GetMaxHealthAttribute())
			: -1.0f;
	}

	/** Summon an imp and assert it really came from the Imp row, because a
	 *  minion whose row was not found keeps the defaults and falls back to a
	 *  different damage rule entirely. */
	ACataclysmMinion* SummonImp(FAutomationTestBase& Test, UWorld* World,
								AActor* Summoner)
	{
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
			Summoner, FVector(1 * M, 0, 0), /*Lifetime=*/20.0f,
			/*bBurns=*/false, TEXT("Imp"));
		if (!Test.TestNotNull(TEXT("an imp"), Imp))
		{
			return nullptr;
		}
		if (Imp->TypeName != FString(TEXT("Imp")))
		{
			Test.AddError(TEXT("DT_MinionTypes could not supply the Imp row. Run "
							   "tools/generate_datatable_assets.py"));
			return nullptr;
		}
		return Imp;
	}

	/** What a probe does: grant the stat, then show the reading code answers
	 *  differently. It reports its own reason for failing. */
	using FProbe = TFunction<void(FAutomationTestBase&)>;

	void ProbeAttackSpeed(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmEnemyCharacter* Commander =
			World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
		ACataclysmEnemyCharacter* Enemy =
			World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector(6 * M, 0, 0), FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("a commander"), Commander)
			|| !Test.TestNotNull(TEXT("an enemy"), Enemy))
		{
			return;
		}
		Commander->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Enemy->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));

		ACataclysmMinion* Imp = SummonImp(Test, World, Commander);
		if (!Imp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

		const float Plain =
			UCataclysmCommand::AttackIntervalScaleFor(Imp, Enemy);
		Grant(Commander, TEXT("minion_attack_speed"), IncreasePercent);
		const float Geared =
			UCataclysmCommand::AttackIntervalScaleFor(Imp, Enemy);

		Test.TestTrue(
			TEXT("minion_attack_speed shortens a commanded creature's interval, "
				 "so UCataclysmCommand::AttackIntervalScaleFor really reads it"),
			Geared < Plain - 0.001f);
	}

	void ProbeDamage(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Summoner(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Target(World, /*AttackDamage=*/0.0f);

		ACataclysmMinion* Imp = SummonImp(Test, World, Summoner.Actor);
		if (!Imp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

		const float Before = Target.Health();
		Imp->AttackTarget(Target.Actor);
		const float Plain = Before - Target.Health();

		Grant(Summoner.Actor, TEXT("minion_damage"), IncreasePercent);

		const float BeforeGeared = Target.Health();
		Imp->AttackTarget(Target.Actor);
		const float Geared = BeforeGeared - Target.Health();

		// THE FIRST READING IS A SPECIFIC NUMBER RATHER THAN "MORE THAN ZERO",
		// so a build where a minion deals nothing at all fails here rather than
		// passing the comparison below with two zeroes.
		Test.TestTrue(TEXT("the imp deals something without the gear"),
					  Plain > 0.0f);
		Test.TestTrue(
			TEXT("minion_damage raises a minion's blow, so "
				 "ACataclysmMinion::AttackTarget really reads it"),
			Geared > Plain + 0.001f);
	}

	void ProbeHealth(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		// TWO SUMMONERS RATHER THAN ONE MEASURED TWICE, because health is written
		// once at the summoning and cannot be re-read after a change the way a
		// blow can.
		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter Geared(World, /*AttackDamage=*/0.0f);
		Grant(Geared.Actor, TEXT("minion_health"), IncreasePercent);

		ACataclysmMinion* PlainImp = SummonImp(Test, World, Plain.Actor);
		ACataclysmMinion* GearedImp = SummonImp(Test, World, Geared.Actor);
		if (!PlainImp || !GearedImp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(PlainImp)) { PlainImp->Destroy(); } };
		ON_SCOPE_EXIT { if (IsValid(GearedImp)) { GearedImp->Destroy(); } };

		Test.TestTrue(TEXT("the plain imp has health at all"),
					  MaxHealthOf(PlainImp) > 0.0f);
		Test.TestTrue(
			TEXT("minion_health raises a minion's maximum health, so "
				 "ACataclysmMinion::Spawn really reads it"),
			MaxHealthOf(GearedImp) > MaxHealthOf(PlainImp) + 0.001f);
	}

	/**
	 * An actor a skill's sphere overlap can find, carrying health and mana.
	 * Issue #1791.
	 *
	 * THE SHAPE `CataclysmSkillTemplateTests.cpp` GIVES ITS OWN FIGHTER, for the
	 * reason it gives: a strike finds what it hits by overlap, so a fighter with
	 * no collision is never struck and a swing at it never lands.
	 */
	struct FScopedSwinger
	{
		FScopedSwinger(UWorld* World, const FVector& Where)
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

			// Raw pointers on purpose, for the reason `FScopedFighter` gives.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(Vital::GetMaxHealthAttribute(), TargetHealthPool);
			Set(Vital::GetHealthAttribute(), TargetHealthPool);
			Set(Vital::GetMaxManaAttribute(), 1000.0f);
			Set(Vital::GetManaAttribute(), 1000.0f);
			Set(Combat::GetAttackDamageAttribute(), 100.0f);
		}

		~FScopedSwinger()
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

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Grant a flat figure, which is how a flag arrives.
	 *
	 * `Grant` ABOVE MAKES AN INCREASE, which is right for a stat that scales
	 * something and useless for one that is either set or not:
	 * `minion_explodes_on_death` is a flag a passive row sets to one, and an
	 * increase of forty per cent of nothing is still nothing.
	 */
	void GrantFlat(AActor* Who, const FString& Stat, float Value)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Who));
		if (!System)
		{
			return;
		}

		FCataclysmStatModifier Flat;
		Flat.Bucket = ECataclysmStatBucket::Flat;
		Flat.Source = ECataclysmModifierSource::PassiveKeystone;
		Flat.Value = Value;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(*Stat));
		Line.Base = 0.0f;
		Line.Modifiers = {Flat};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/**
	 * Take a stat away, as "You cannot regenerate mana through any means" does.
	 *
	 * FROM AN ENCHANTMENT, because only a source that may grant a More multiplier
	 * may remove a stat: the same removal from a gear affix is ignored, and a
	 * probe granting one would measure the refusal rather than the reader.
	 */
	void Remove(AActor* Who, const FString& Stat)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Who));
		if (!System)
		{
			return;
		}

		FCataclysmStatModifier Removal;
		Removal.Bucket = ECataclysmStatBucket::Removed;
		Removal.Source = ECataclysmModifierSource::Enchantment;
		Removal.Value = 1.0f;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(*Stat));
		Line.Base = 0.0f;
		Line.Modifiers = {Removal};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** A basic swing striking everything around the caster, granted at level 100. */
	UCataclysmStrikeSkill* GrantSwing(FScopedSwinger& Caster)
	{
		const FGameplayAbilitySpecHandle Handle =
			Caster.AbilitySystem->GiveAbilityInSlot(
				UCataclysmStrikeSkill::StaticClass(),
				ECataclysmAbilitySlot::BasicAttack, /*Level=*/100, Caster.Actor);
		FGameplayAbilitySpec* Spec =
			Handle.IsValid()
				? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle)
				: nullptr;
		UCataclysmStrikeSkill* Swing =
			Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Swing)
		{
			Swing->SkillName = TEXT("A basic swing");
			Swing->Params =
				UCataclysmSkillShapes::ParseParams(TEXT("Radius=20; Angle=360"));
		}
		return Swing;
	}

	void ProbeManaOnHit(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		// TWO CASTERS RATHER THAN ONE SWINGING TWICE, as `ProbeHealth` has two
		// summoners: each swings once, so nothing about a second activation can
		// be what the reading measures. A hundred metres apart, so neither swing
		// reaches the other pair.
		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger PlainTarget(World, FVector(2 * M, 0, 0));
		FScopedSwinger Removed(World, FVector(0, 100 * M, 0));
		FScopedSwinger RemovedTarget(World, FVector(2 * M, 100 * M, 0));
		Remove(Removed.Actor, UCataclysmSkillSlots::ManaOnHitStat);

		UCataclysmStrikeSkill* PlainSwing = GrantSwing(Plain);
		UCataclysmStrikeSkill* RemovedSwing = GrantSwing(Removed);
		if (!Test.TestNotNull(TEXT("a plain swing"), PlainSwing)
			|| !Test.TestNotNull(TEXT("a swing under the removal"), RemovedSwing))
		{
			return;
		}

		// THE SLOT REALLY PAYS, checked first, or both readings below would
		// compare nothing with nothing.
		const float Paid = PlainSwing->GetManaOnHit();
		if (!Test.TestTrue(
				FString::Printf(TEXT("the basic slot pays mana on hit: %.2f"), Paid),
				Paid > 0.0f))
		{
			return;
		}

		// SPENT DOWN FIRST, because a payment to a full pool is clamped away.
		const FGameplayAttribute Mana = Vital::GetManaAttribute();
		const FGameplayAttribute Health = Vital::GetHealthAttribute();
		Plain.Set(Mana, 500.0f);
		Removed.Set(Mana, 500.0f);

		const auto Swing = [](FScopedSwinger& Caster, UGameplayAbility* Ability)
		{
			return Caster.AbilitySystem->TryActivateAbility(
				Ability->GetCurrentAbilitySpecHandle(),
				/*bAllowRemoteActivation=*/false);
		};
		Test.TestTrue(TEXT("the plain swing goes off"), Swing(Plain, PlainSwing));
		Test.TestTrue(TEXT("and so does the swing under the removal"),
					  Swing(Removed, RemovedSwing));

		// BOTH LANDED, or "paid nothing" below could be a swing that missed.
		Test.TestTrue(TEXT("the plain swing landed"),
					  PlainTarget.Get(Health) < TargetHealthPool - 0.001f);
		Test.TestTrue(TEXT("and so did the swing under the removal"),
					  RemovedTarget.Get(Health) < TargetHealthPool - 0.001f);

		Test.TestEqual(TEXT("the plain swing paid its mana on hit"),
					   Plain.Get(Mana), 500.0f + Paid, 0.01f);
		Test.TestEqual(
			TEXT("with mana_on_hit removed the same swing paid nothing, so "
				 "UCataclysmSkillTemplate::ApplyManaOnHit really reads it"),
			Removed.Get(Mana), 500.0f, 0.01f);
	}

	/**
	 * The radius a summoning skill states for the explosions these probes use.
	 *
	 * WHAT AN EXPLOSION IS WORTH IS NO LONGER STATED HERE. Since issue #1515
	 * it is the minion's own blow times the share its type row gives, so an
	 * imp spawned with its type carries the figure already and these probes
	 * measure one against the other rather than against a number.
	 */
	constexpr float ProbeExplosionRadiusCm = 300.0f;

	/** An imp of the authored type, told how wide its explosion would be. */
	ACataclysmMinion* ImpToldItsExplosion(FAutomationTestBase& Test,
										  AActor* Summoner, const FVector& Where)
	{
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
			Summoner, Where, /*Lifetime=*/20.0f, /*bBurns=*/false, TEXT("Imp"));
		if (!Test.TestNotNull(TEXT("an imp"), Imp))
		{
			return nullptr;
		}
		Imp->RecordExplosionRadius(ProbeExplosionRadiusCm);
		return Imp;
	}

	/** Write a minion's health to nothing, which is how anything else dies. */
	void KillMinion(ACataclysmMinion* Minion)
	{
		if (UAbilitySystemComponent* System =
				UCataclysmTargeting::AbilitySystemOf(Minion))
		{
			System->SetNumericAttributeBase(Vital::GetHealthAttribute(), 0.0f);
		}
	}

	void ProbeExplodesOnDeath(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		// TWO SUMMONERS A HUNDRED METRES APART, as `ProbeManaOnHit` has: each
		// loses one minion, so nothing about a second death can be what the
		// reading measures.
		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger Flagged(World, FVector(0, 100 * M, 0));
		GrantFlat(Flagged.Actor, TEXT("minion_explodes_on_death"), 1.0f);

		ACataclysmMinion* PlainImp =
			ImpToldItsExplosion(Test, Plain.Actor, FVector(1 * M, 0, 0));
		ACataclysmMinion* FlaggedImp =
			ImpToldItsExplosion(Test, Flagged.Actor, FVector(1 * M, 100 * M, 0));
		if (!PlainImp || !FlaggedImp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(PlainImp)) { PlainImp->Destroy(); } };

		KillMinion(PlainImp);
		KillMinion(FlaggedImp);

		// THE BODY IS THE READING. An explosion destroys the minion, so the
		// difference between the two deaths is visible without a target: one
		// leaves a corpse for the summon cap to count and the other does not.
		Test.TestTrue(
			TEXT("a minion whose summoner lacks the flag leaves its body"),
			IsValid(PlainImp));
		Test.TestFalse(
			TEXT("and one whose summoner has it is destroyed by its own "
				 "explosion, so ACataclysmMinion::HandleDeath really reads "
				 "minion_explodes_on_death"),
			IsValid(FlaggedImp));
	}

	void ProbeExplosionDamage(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger PlainTarget(World, FVector(2 * M, 0, 0));
		FScopedSwinger Raised(World, FVector(0, 100 * M, 0));
		FScopedSwinger RaisedTarget(World, FVector(2 * M, 100 * M, 0));
		Grant(Raised.Actor, TEXT("minion_explosion_damage"), IncreasePercent);

		ACataclysmMinion* PlainImp =
			ImpToldItsExplosion(Test, Plain.Actor, FVector(1 * M, 0, 0));
		ACataclysmMinion* RaisedImp =
			ImpToldItsExplosion(Test, Raised.Actor, FVector(1 * M, 100 * M, 0));
		if (!PlainImp || !RaisedImp)
		{
			return;
		}

		// NO FLAG ON EITHER, because this stat belongs to the explosion rather
		// than to the death: the summon cap sets one off the same way, and this
		// calls what the cap calls.
		const FGameplayAttribute Health = Vital::GetHealthAttribute();
		PlainImp->Explode();
		RaisedImp->Explode();

		const float PlainLost = TargetHealthPool - PlainTarget.Get(Health);
		const float RaisedLost = TargetHealthPool - RaisedTarget.Get(Health);

		// THE PLAIN EXPLOSION MUST HURT FIRST, or "more" below would hold with
		// both of them dealing nothing at all.
		if (!Test.TestTrue(
				FString::Printf(TEXT("the plain explosion hurt its target: %.2f"),
								PlainLost),
				PlainLost > 0.0f))
		{
			return;
		}
		Test.TestTrue(
			FString::Printf(
				TEXT("and the one under minion_explosion_damage hurt more "
					 "(%.2f against %.2f), so ACataclysmMinion::Explode really "
					 "reads it"),
				RaisedLost, PlainLost),
			RaisedLost > PlainLost + 0.001f);
	}

	/**
	 * One probe per exempt stat.
	 *
	 * A NAME WITH NO ENTRY HERE IS THE FAILURE THIS FILE EXISTS FOR. It is not a
	 * gap to be filled by adding an empty probe: an empty probe would pass and
	 * put the exemption straight back into the state issue #1025 describes.
	 */
	/**
	 * Scale a stat, as "Your spells cost 10%-20% less mana" does.
	 *
	 * A MORE MULTIPLIER FROM AN ENCHANTMENT, for the reason `Remove` above gives:
	 * an ordinary affix may not grant one, so a probe that used one would measure
	 * the refusal rather than the reader.
	 */
	void Scale(AActor* Who, const FString& Stat, float Percent)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Who));
		if (!System)
		{
			return;
		}

		FCataclysmStatModifier Multiplier;
		Multiplier.Bucket = ECataclysmStatBucket::More;
		Multiplier.Source = ECataclysmModifierSource::Enchantment;
		Multiplier.Value = Percent;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(*Stat));
		Line.Base = 0.0f;
		Line.Modifiers = {Multiplier};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** A Heavy strike costing 40 mana, a figure no slot in the sheet states. */
	UCataclysmStrikeSkill* GrantCostingSkill(FScopedSwinger& Caster)
	{
		const FGameplayAbilitySpecHandle Handle =
			Caster.AbilitySystem->GiveAbilityInSlot(
				UCataclysmStrikeSkill::StaticClass(),
				ECataclysmAbilitySlot::Heavy, /*Level=*/100, Caster.Actor);
		FGameplayAbilitySpec* Spec =
			Handle.IsValid()
				? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle)
				: nullptr;
		UCataclysmStrikeSkill* Skill =
			Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = TEXT("A skill that costs something");
			Skill->ManaCostOverride = 40.0f;
		}
		return Skill;
	}

	void ProbeManaCost(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		// TWO CHARACTERS RATHER THAN ONE WITH THE ROW ADDED HALFWAY, as the probes
		// above use two: the reading is asked once of each, so nothing about the
		// order can be what it measures.
		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger Cheaper(World, FVector(0, 100 * M, 0));
		Scale(Cheaper.Actor, UCataclysmSkillSlots::ManaCostStat, -50.0f);

		UCataclysmStrikeSkill* PlainSkill = GrantCostingSkill(Plain);
		UCataclysmStrikeSkill* CheaperSkill = GrantCostingSkill(Cheaper);
		if (!Test.TestNotNull(TEXT("a skill that costs mana"), PlainSkill)
			|| !Test.TestNotNull(TEXT("and one under the row"), CheaperSkill))
		{
			return;
		}

		// THE SKILL REALLY COSTS SOMETHING, checked first, or both readings below
		// would compare nothing with nothing.
		const float Base = PlainSkill->GetManaCost();
		if (!Test.TestTrue(
				FString::Printf(TEXT("the skill costs mana: %.1f"), Base),
				Base > 0.0f))
		{
			return;
		}

		Test.TestEqual(TEXT("a character with no row pays the skill's own cost"),
					   PlainSkill->ManaCostFor(Plain.AbilitySystem), Base, 0.01f);
		Test.TestEqual(TEXT("and one carrying a row that halves it pays half"),
					   CheaperSkill->ManaCostFor(Cheaper.AbilitySystem),
					   Base * 0.5f, 0.01f);
	}

	void ProbeHitsCountAsYours(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		// TWO SUMMONERS, ONE HOLDING THE KEYSTONE FLAG, each with its own imp
		// and its own target, so neither reading can be the other's.
		FScopedFighter Plain(World, /*AttackDamage=*/1000.0f);
		FScopedFighter PlainTarget(World, /*AttackDamage=*/0.0f);
		FScopedFighter Flagged(World, /*AttackDamage=*/1000.0f);
		FScopedFighter FlaggedTarget(World, /*AttackDamage=*/0.0f);
		GrantFlat(Flagged.Actor, TEXT("minion_hits_count_as_yours"), 1.0f);

		ACataclysmMinion* PlainImp = SummonImp(Test, World, Plain.Actor);
		ACataclysmMinion* FlaggedImp = SummonImp(Test, World, Flagged.Actor);
		if (!PlainImp || !FlaggedImp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(PlainImp)) { PlainImp->Destroy(); } };
		ON_SCOPE_EXIT { if (IsValid(FlaggedImp)) { FlaggedImp->Destroy(); } };

		PlainImp->AttackTarget(PlainTarget.Actor);
		FlaggedImp->AttackTarget(FlaggedTarget.Actor);

		// THE RECORD THE TARGET KEEPS IS THE READING, rather than a listener:
		// `UCataclysmCombatEvents::NoteBlow` writes the same attacker into the
		// target's last blow as it puts on the notice, and a death reads the
		// killer out of that record.
		UCataclysmAbilitySystemComponent* PlainHit =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(PlainTarget.Actor));
		UCataclysmAbilitySystemComponent* FlaggedHit =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(FlaggedTarget.Actor));
		if (!Test.TestNotNull(TEXT("the plain target records what hit it"), PlainHit)
			|| !Test.TestNotNull(TEXT("and so does the other"), FlaggedHit))
		{
			return;
		}

		// A BLOW MUST HAVE LANDED FIRST, or both readings below would be null
		// and the comparison would hold for the wrong reason.
		if (!Test.TestTrue(TEXT("the plain imp's blow was recorded"),
						   PlainHit->GetLastBlow().DealtBy == PlainImp))
		{
			return;
		}

		Test.TestTrue(
			TEXT("a minion's blow is credited to the minion by default"),
			PlainHit->GetLastBlow().Attacker == PlainImp);
		Test.TestTrue(
			TEXT("and to the summoner that holds the flag, so "
				 "UCataclysmCombatEvents::NoteBlow really reads "
				 "minion_hits_count_as_yours"),
			FlaggedHit->GetLastBlow().Attacker == Flagged.Actor);
	}

	// ------------------------------------------------------------------
	// THE SECOND PROMISE THIS FILE KEEPS, AND IT IS A DIFFERENT ONE. Issue
	// #1973. Above: every stat with no gameplay attribute is read by bespoke
	// code. Below: every stat the shipped data SCALES is asked for through the
	// pipeline. A scaled row is never folded into its attribute -- it is worked
	// out when something asks -- so a scaled row on a stat nothing asks for
	// grants nothing, with no error and no warning. That is what
	// `Ritualist_capstone_200#3` did for as long as it existed.
	// ------------------------------------------------------------------

	/**
	 * A modifier on `Stat` worth `Percent` per unit of `Scale`, over `Base`.
	 *
	 * THE BASE IS NOT OPTIONAL AND THAT COST A BUILD. `StatForSkill`'s third
	 * argument is a FALLBACK, used only when the character has no line for the
	 * stat; once a line exists the pipeline multiplies the LINE'S base. A line
	 * recorded with a base of nothing therefore answers nothing however large
	 * the increase, and every probe reading such a stat measured 0 against 0 --
	 * ten of the twelve, on the first run of this test. Each probe passes the
	 * figure its subject really holds.
	 */
	void ScaledBy(AActor* Who, const FString& Stat, float Percent,
				  ECataclysmStatScale Scale, float Base,
				  float ReachMetres = -1.0f)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Who));
		if (!System)
		{
			return;
		}

		// INCREASED RATHER THAN MORE, because that is the bucket every scaled
		// row in the sheet uses but one, and a bucket a probe invented would
		// measure a path no row takes.
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Increased;
		Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
		Modifier.Value = Percent;
		Modifier.Scale = Scale;
		Modifier.ScaleStep = 1.0f;

		// AND HOW FAR IT LOOKS, WHICH IS NOT DECORATION FOR ONE OF THESE.
		// `WithEnemiesInReach` walks the world only for a modifier that states
		// a reach; one that states none counts nobody, and the probe reading
		// enemies nearby measured 1.000 against 1.000 until this was passed.
		Modifier.ReachMetres = ReachMetres;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(*Stat));
		Line.Base = Base;
		Line.Modifiers = {Modifier};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** Two debuffs on a character, which is what `debuffs_carried` counts. */
	void GiveTwoDebuffs(AActor* Who)
	{
		UCataclysmSkillEffects::ApplyTagForDuration(
			Who, Who, UCataclysmDebuffs::BleedTag(), 30.0f);
		UCataclysmSkillEffects::ApplyTagForDuration(
			Who, Who, UCataclysmDebuffs::WeakenTag(), 30.0f);
	}

	/**
	 * `attack_damage`, scaled by debuffs carried, asked by
	 * `UCataclysmAbilitySystemComponent::AttackDamageIncreasesForSkill`.
	 *
	 * FIVE SHIPPED ROWS PAIR THOSE TWO, which is why this probe uses that scale
	 * rather than whichever is easiest to drive.
	 */
	void ProbeScaledAttackDamage(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Attacker(World, /*AttackDamage=*/1000.0f);
		ScaledBy(Attacker.Actor, TEXT("attack_damage"), 50.0f,
				 ECataclysmStatScale::PerDebuffCarried, /*Base=*/1000.0f);

		const UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Attacker.Actor));
		if (!Test.TestNotNull(TEXT("an ability system"),
							  const_cast<UCataclysmAbilitySystemComponent*>(System)))
		{
			return;
		}

		const float Clean = System->AttackDamageIncreasesForSkill(
			FGameplayTagContainer(), 0.0f, 0.0f, -1.0f, false, nullptr, 0);
		GiveTwoDebuffs(Attacker.Actor);
		const float Carrying = System->AttackDamageIncreasesForSkill(
			FGameplayTagContainer(), 0.0f, 0.0f, -1.0f, false, nullptr, 0);

		Test.TestTrue(
			FString::Printf(TEXT("attack_damage is asked for, so two debuffs "
								 "raise its increases: %.2f against %.2f"),
							Carrying, Clean),
			Carrying > Clean + 0.001f);
	}

	/**
	 * `spell_damage`, scaled by debuffs carried, asked by
	 * `UCataclysmSkillEffects::SpellDamageOf`. Five shipped rows pair them.
	 */
	void ProbeScaledSpellDamage(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Caster(World, /*AttackDamage=*/0.0f);
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Caster.Actor));
		if (!Test.TestNotNull(TEXT("an ability system"), System))
		{
			return;
		}
		System->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetSpellDamageAttribute(), 100.0f);
		ScaledBy(Caster.Actor, TEXT("spell_damage"), 50.0f,
				 ECataclysmStatScale::PerDebuffCarried, /*Base=*/100.0f);

		const float Clean = UCataclysmSkillEffects::SpellDamageOf(
			System, FGameplayTagContainer());
		GiveTwoDebuffs(Caster.Actor);
		const float Carrying = UCataclysmSkillEffects::SpellDamageOf(
			System, FGameplayTagContainer());

		Test.TestTrue(
			FString::Printf(TEXT("spell_damage is asked for, so two debuffs "
								 "raise it: %.2f against %.2f"),
							Carrying, Clean),
			Carrying > Clean + 0.001f);
	}

	/**
	 * `max_energy_shield`, scaled by minions held, asked by
	 * `UCataclysmAbilitySystemComponent::MaximumEnergyShield`. Issue #1973: this
	 * is the pairing that granted nothing until that lookup existed.
	 */
	void ProbeScaledMaximumEnergyShield(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Summoner(World, /*AttackDamage=*/0.0f);
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Summoner.Actor));
		if (!Test.TestNotNull(TEXT("an ability system"), System))
		{
			return;
		}
		System->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 100.0f);
		ScaledBy(Summoner.Actor, TEXT("max_energy_shield"), 25.0f,
				 ECataclysmStatScale::PerMinionHeld, /*Base=*/100.0f);

		const float Alone = System->MaximumEnergyShield();
		ACataclysmMinion* Imp = SummonImp(Test, World, Summoner.Actor);
		if (!Imp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };
		const float Holding = System->MaximumEnergyShield();

		Test.TestTrue(
			FString::Printf(TEXT("max_energy_shield is asked for, so a minion "
								 "raises it: %.2f against %.2f"),
							Holding, Alone),
			Holding > Alone + 0.001f);
	}

	/**
	 * `retaliation`, scaled by health missing, asked by `StatOfRetaliator` in
	 * `CataclysmRetaliation.cpp` and reachable through `AmountFor`.
	 */
	void ProbeScaledRetaliation(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Defender.Actor));
		if (!Test.TestNotNull(TEXT("an ability system"), System))
		{
			return;
		}
		System->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetRetaliationAttribute(), 10.0f);
		System->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		System->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		ScaledBy(Defender.Actor, TEXT("retaliation"), 1.0f,
				 ECataclysmStatScale::PerPercentOfMaximumHealthMissing,
				 /*Base=*/10.0f);

		const float Whole = UCataclysmRetaliation::AmountFor(System, 100.0f);
		System->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), 400.0f);
		const float Hurt = UCataclysmRetaliation::AmountFor(System, 100.0f);

		Test.TestTrue(
			FString::Printf(TEXT("retaliation is asked for, so missing health "
								 "raises it: %.2f against %.2f"), Hurt, Whole),
			Hurt > Whole + 0.001f);
	}

	/**
	 * `attack_speed`, scaled by momentum, asked by
	 * `UCataclysmBasicAttack::SecondsBetweenSwingsFor`.
	 *
	 * THE ANSWER FALLS RATHER THAN RISES, because it is seconds between swings:
	 * more attack speed is less time, so the assertion is the other way round
	 * from every other probe here and says so.
	 */
	void ProbeScaledAttackSpeed(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Swinger(World, /*AttackDamage=*/100.0f);
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Swinger.Actor));
		if (!Test.TestNotNull(TEXT("an ability system"), System))
		{
			return;
		}
		System->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.0f);
		ScaledBy(Swinger.Actor, TEXT("attack_speed"), 50.0f,
				 ECataclysmStatScale::PerStackOfSanguineMomentum, /*Base=*/1.0f);

		const float Still = UCataclysmBasicAttack::SecondsBetweenSwingsFor(System);

		// THE STACK IS GRANTED DIRECTLY, NOT THROUGH THE EVENT THAT USUALLY
		// GRANTS IT. `UCataclysmStacks::NoteHealthCostPaid` refuses unless the
		// character paid a health cost within the stack's own window, which a
		// probe has not: it returned false twice here and the reading never
		// moved. What this case is about is whether the SWING GAP asks for the
		// stat, not how a Masochist earns momentum.
		System->GrantStack(
			ECataclysmStackKind::SanguineMomentum,
			UCataclysmStacks::WindowSecondsFor(ECataclysmStackKind::SanguineMomentum),
			UCataclysmStacks::CapFor(ECataclysmStackKind::SanguineMomentum));
		System->GrantStack(
			ECataclysmStackKind::SanguineMomentum,
			UCataclysmStacks::WindowSecondsFor(ECataclysmStackKind::SanguineMomentum),
			UCataclysmStacks::CapFor(ECataclysmStackKind::SanguineMomentum));

		const float Moving = UCataclysmBasicAttack::SecondsBetweenSwingsFor(System);

		Test.TestTrue(
			FString::Printf(TEXT("attack_speed is asked for, so momentum "
								 "shortens the gap between swings: %.3f "
								 "against %.3f"), Moving, Still),
			Moving < Still - 0.0001f);
	}

	/** A character that can hold Fervour, which the three rate probes need. */
	void GiveAFervourPool(FScopedFighter& Who)
	{
		Who.AbilitySystem->AddAttributeSetSubobject(
			NewObject<UCataclysmClassResourceAttributeSet>(Who.Actor));
		Who.AbilitySystem->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
			100.0f);
		Who.AbilitySystem->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
	}

	/**
	 * `armor`, scaled by debuffs carried, asked by `DefenderStat` in
	 * `CataclysmDamageCalculation.cpp` when a blow is worked out.
	 *
	 * A REAL HIT, BECAUSE THE ASKER IS FILE-LOCAL. Nothing outside that file can
	 * call it, so the probe drives a blow and reads what reached health.
	 */
	void ProbeScaledArmour(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Attacker(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		Defender.AbilitySystem->SetNumericAttributeBase(
			Combat::GetArmorAttribute(), 800.0f);
		ScaledBy(Defender.Actor, TEXT("armor"), 200.0f,
				 ECataclysmStatScale::PerDebuffCarried, /*Base=*/800.0f);

		const float Before = Defender.Health();
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer());
		const float Clean = Before - Defender.Health();
		if (!Test.TestTrue(TEXT("the unshielded blow landed"), Clean > 0.0f))
		{
			return;
		}

		GiveTwoDebuffs(Defender.Actor);
		const float Middle = Defender.Health();
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer());
		const float Carrying = Middle - Defender.Health();

		Test.TestTrue(
			FString::Printf(TEXT("armor is asked for, so debuffs raise it and "
								 "less lands: %.2f against %.2f"),
							Carrying, Clean),
			Carrying < Clean - 0.001f);
	}

	/**
	 * `damage_reduction`, scaled by debuffs carried, asked by the same
	 * `DefenderStat` at a later step of the same blow.
	 *
	 * ITS OWN DEFENDER, NOT THE ARMOUR ONE. A single defender carrying both
	 * scaled lines would still take less when only one of the two reads worked,
	 * so each stat gets its own subject and its own assertion.
	 */
	void ProbeScaledDamageReduction(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Attacker(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		Defender.AbilitySystem->SetNumericAttributeBase(
			Combat::GetDamageReductionAttribute(), 10.0f);
		ScaledBy(Defender.Actor, TEXT("damage_reduction"), 100.0f,
				 ECataclysmStatScale::PerDebuffCarried, /*Base=*/10.0f);

		const float Before = Defender.Health();
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer());
		const float Clean = Before - Defender.Health();
		if (!Test.TestTrue(TEXT("the blow landed"), Clean > 0.0f))
		{
			return;
		}

		GiveTwoDebuffs(Defender.Actor);
		const float Middle = Defender.Health();
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer());
		const float Carrying = Middle - Defender.Health();

		Test.TestTrue(
			FString::Printf(TEXT("damage_reduction is asked for, so debuffs "
								 "raise it and less lands: %.2f against %.2f"),
							Carrying, Clean),
			Carrying < Clean - 0.001f);
	}

	/**
	 * `health_regen`, scaled by debuffs carried, asked by `RateOf` inside
	 * `UCataclysmRegeneration::ApplyStep`.
	 *
	 * THE CHARACTER STARTS HURT, or a full pool would hide the gain behind its
	 * own maximum and both readings would be nothing.
	 */
	void ProbeScaledHealthRegen(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Hurt(World, /*AttackDamage=*/0.0f);
		Hurt.AbilitySystem->SetNumericAttributeBase(
			Vital::GetHealthRegenAttribute(), 10.0f);
		Hurt.AbilitySystem->SetNumericAttributeBase(
			Vital::GetHealthAttribute(), TargetHealthPool / 2.0f);
		ScaledBy(Hurt.Actor, TEXT("health_regen"), 100.0f,
				 ECataclysmStatScale::PerDebuffCarried, /*Base=*/10.0f);

		const float Before = Hurt.Health();
		UCataclysmRegeneration::ApplyStep(Hurt.Actor, 1.0f, 100.0f);
		const float Clean = Hurt.Health() - Before;
		if (!Test.TestTrue(TEXT("a hurt character regenerates something"),
						   Clean > 0.0f))
		{
			return;
		}

		GiveTwoDebuffs(Hurt.Actor);
		const float Middle = Hurt.Health();
		UCataclysmRegeneration::ApplyStep(Hurt.Actor, 1.0f, 100.0f);
		const float Carrying = Hurt.Health() - Middle;

		Test.TestTrue(
			FString::Printf(TEXT("health_regen is asked for, so debuffs raise "
								 "what a step restores: %.3f against %.3f"),
							Carrying, Clean),
			Carrying > Clean + 0.0001f);
	}

	/**
	 * `fervour_per_second`, scaled by debuffs carried, asked inside
	 * `UCataclysmFervour::GainPerSecondStep`.
	 */
	void ProbeScaledFervourPerSecond(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Holder(World, /*AttackDamage=*/0.0f);
		GiveAFervourPool(Holder);
		ScaledBy(Holder.Actor, TEXT("fervour_per_second"), 1.0f,
				 ECataclysmStatScale::PerDebuffCarried, /*Base=*/1.0f);

		const float Clean =
			UCataclysmFervour::GainPerSecondStep(Holder.AbilitySystem, 1.0f);
		GiveTwoDebuffs(Holder.Actor);
		const float Carrying =
			UCataclysmFervour::GainPerSecondStep(Holder.AbilitySystem, 1.0f);

		Test.TestTrue(
			FString::Printf(TEXT("fervour_per_second is asked for, so debuffs "
								 "raise the step's gain: %.3f against %.3f"),
							Carrying, Clean),
			Carrying > Clean + 0.0001f);
	}

	/**
	 * `fervour_from_minions`, scaled by minions held, asked in the same step.
	 */
	void ProbeScaledFervourFromMinions(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Summoner(World, /*AttackDamage=*/0.0f);
		GiveAFervourPool(Summoner);
		ScaledBy(Summoner.Actor, TEXT("fervour_from_minions"), 1.0f,
				 ECataclysmStatScale::PerMinionHeld, /*Base=*/1.0f);

		const float Alone =
			UCataclysmFervour::GainPerSecondStep(Summoner.AbilitySystem, 1.0f);
		ACataclysmMinion* Imp = SummonImp(Test, World, Summoner.Actor);
		if (!Imp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };
		const float Holding =
			UCataclysmFervour::GainPerSecondStep(Summoner.AbilitySystem, 1.0f);

		Test.TestTrue(
			FString::Printf(TEXT("fervour_from_minions is asked for, so a "
								 "minion raises the step's gain: %.3f against "
								 "%.3f"), Holding, Alone),
			Holding > Alone + 0.0001f);
	}

	/**
	 * `fervour_per_enemy_in_reach`, scaled by enemies in reach, asked in the
	 * same step.
	 *
	 * THE CREATURE STANDS WHERE THE CHARACTER DOES, because a bare actor has no
	 * root component and cannot be moved off the origin; nothing here depends on
	 * the distance beyond its being inside reach.
	 */
	void ProbeScaledFervourPerEnemyInReach(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		// A REAL CHARACTER HOLDS THE LINE, NOT A BARE ACTOR. The walk that counts
		// nearby enemies measures from the avatar and refuses anything that is
		// not an `ACataclysmCharacterBase`, so a bare actor counts nobody however
		// many creatures stand on it. That is why this probe alone builds its
		// subject as a character.
		ACataclysmEnemyCharacter* Holder =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
														FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("a character to hold the line"), Holder))
		{
			return;
		}
		Holder->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		ON_SCOPE_EXIT { if (IsValid(Holder)) { Holder->Destroy(); } };

		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				Holder->GetAbilitySystemComponent());
		if (!Test.TestNotNull(TEXT("with an ability system"), System))
		{
			return;
		}
		System->AddAttributeSetSubobject(
			NewObject<UCataclysmClassResourceAttributeSet>(Holder));
		System->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
			100.0f);
		System->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);

		// FIVE METRES, WHICH IS A REACH THE SHIPPED ROWS STATE. A modifier with
		// no reach is skipped by the walk entirely.
		ScaledBy(Holder, TEXT("fervour_per_enemy_in_reach"), 1.0f,
				 ECataclysmStatScale::PerEnemyInReach, /*Base=*/1.0f,
				 /*ReachMetres=*/5.0f);

		const float Alone = UCataclysmFervour::GainPerSecondStep(System, 1.0f);

		ACataclysmEnemyCharacter* Creature =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector(1 * M, 0, 0),
														FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("a hostile creature to stand near"), Creature))
		{
			return;
		}
		Creature->SetGenericTeamId(
			UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		ON_SCOPE_EXIT { if (IsValid(Creature)) { Creature->Destroy(); } };

		const float Crowded = UCataclysmFervour::GainPerSecondStep(System, 1.0f);

		Test.TestTrue(
			FString::Printf(TEXT("fervour_per_enemy_in_reach is asked for, so "
								 "an enemy near raises the step's gain: %.3f "
								 "against %.3f"), Crowded, Alone),
			Crowded > Alone + 0.0001f);
	}

	/**
	 * Every stat the shipped data scales, and the probe that proves the engine
	 * asks for it.
	 *
	 * THE PYTHON SIDE READS THIS BLOCK. `tools/tests/` parses the literals here
	 * and requires them to equal `STATS_WITH_AN_ASKER` in
	 * `tools/generate_datatables.py`, so a stat added to one and not the other
	 * fails. The parse is anchored on this table's opening line and raises if its
	 * shape changes, rather than silently reading nothing.
	 */
	/**
	 * `mana_regen`, scaled by maximum mana, asked by the same `RateOf` inside
	 * `UCataclysmRegeneration::ApplyStep` that the health rate uses.
	 *
	 * IT ARRIVED WITH THE ENCHANTMENT ROWS OF 2026-09-18 and is the twelfth
	 * stat the shipped data scales. This test found it by name on the first run
	 * after that merge, which is what it is for.
	 *
	 * THE POOL STARTS HALF EMPTY, or a full one would hide the gain behind its
	 * own maximum and both readings would be nothing.
	 */
	void ProbeScaledManaRegen(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Caster(World, /*AttackDamage=*/0.0f);
		Caster.AbilitySystem->SetNumericAttributeBase(
			Vital::GetManaRegenAttribute(), 10.0f);
		Caster.AbilitySystem->SetNumericAttributeBase(
			Vital::GetMaxManaAttribute(), 200.0f);
		Caster.AbilitySystem->SetNumericAttributeBase(
			Vital::GetManaAttribute(), 100.0f);
		ScaledBy(Caster.Actor, TEXT("mana_regen"), 1.0f,
				 ECataclysmStatScale::PerPointOfMaximumMana, /*Base=*/10.0f);

		const auto ManaOf = [&Caster]()
		{
			return Caster.AbilitySystem->GetNumericAttribute(
				Vital::GetManaAttribute());
		};

		const float Before = ManaOf();
		UCataclysmRegeneration::ApplyStep(Caster.Actor, 1.0f, 100.0f);
		const float Restored = ManaOf() - Before;

		// THE SCALE IS ALREADY IN THE FIRST READING, because maximum mana is a
		// standing figure rather than something a probe turns on. So this one
		// compares against the SAME step with the scaled line taken away, which
		// is the only way round for a reading no probe can set to nothing.
		Caster.AbilitySystem->SetNumericAttributeBase(
			Vital::GetManaAttribute(), Before);
		Remove(Caster.Actor, TEXT("mana_regen"));
		const float Middle = ManaOf();
		UCataclysmRegeneration::ApplyStep(Caster.Actor, 1.0f, 100.0f);
		const float Unscaled = ManaOf() - Middle;

		Test.TestTrue(
			FString::Printf(TEXT("mana_regen is asked for, so a line scaled by "
								 "maximum mana restores more than none: %.3f "
								 "against %.3f"), Restored, Unscaled),
			Restored > Unscaled + 0.0001f);
	}

	const TMap<FString, FProbe>& ScaledProbes()
	{
		static const TMap<FString, FProbe> Made = {
			{TEXT("attack_damage"),              &ProbeScaledAttackDamage},
			{TEXT("spell_damage"),               &ProbeScaledSpellDamage},
			{TEXT("attack_speed"),               &ProbeScaledAttackSpeed},
			{TEXT("armor"),                      &ProbeScaledArmour},
			{TEXT("damage_reduction"),           &ProbeScaledDamageReduction},
			{TEXT("retaliation"),                &ProbeScaledRetaliation},
			{TEXT("health_regen"),               &ProbeScaledHealthRegen},
			{TEXT("fervour_per_second"),         &ProbeScaledFervourPerSecond},
			{TEXT("fervour_from_minions"),       &ProbeScaledFervourFromMinions},
			{TEXT("fervour_per_enemy_in_reach"), &ProbeScaledFervourPerEnemyInReach},
			{TEXT("max_energy_shield"),          &ProbeScaledMaximumEnergyShield},
			{TEXT("mana_regen"),                 &ProbeScaledManaRegen},
		};
		return Made;
	}

	const TMap<FString, FProbe>& Probes()
	{
		static const TMap<FString, FProbe> Made = {
			{TEXT("minion_attack_speed"), &ProbeAttackSpeed},
			{TEXT("minion_damage"),       &ProbeDamage},
			{TEXT("minion_health"),       &ProbeHealth},
			{TEXT("mana_on_hit"),         &ProbeManaOnHit},
			{TEXT("mana_cost"),           &ProbeManaCost},
			{TEXT("minion_explodes_on_death"), &ProbeExplodesOnDeath},
			{TEXT("minion_explosion_damage"),  &ProbeExplosionDamage},
			{TEXT("minion_hits_count_as_yours"), &ProbeHitsCountAsYours},
		};
		return Made;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStatExemptionIsKeptTest,
	"Cataclysm.StatExemption.EveryStatWithNoAttributeIsActuallyRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every name on the shared exemption list is read by code that changes its
 * answer, and a name with no probe fails by name.
 *
 * THIS IS THE HALF ISSUE #1025 WAS MISSING. The other checks stop refusing these
 * stats; none of them asks whether the claim behind the exemption is true.
 *
 * HOW TO PROVE IT WORKS: add a name to
 * `UCataclysmPlayerClassStats::StatsWithNoAttribute()` that nothing reads, run
 * this group, and watch it name that stat and fail. Then take it out.
 */
bool FCataclysmStatExemptionIsKeptTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	const TArray<FString>& Exempt =
		UCataclysmPlayerClassStats::StatsWithNoAttribute();

	// A LIST THAT EMPTIED ITSELF WOULD PASS EVERY LOOP BELOW, which is what a
	// refactor that lost the contents looks like. The checks that read it would
	// go back to refusing the rows, so this would be reported somewhere --
	// but reported here first, and by name.
	if (!TestTrue(TEXT("the exemption list has names in it"), Exempt.Num() > 0))
	{
		return false;
	}

	for (const FString& Stat : Exempt)
	{
		const FProbe* Probe = Probes().Find(Stat);
		if (!Probe)
		{
			AddError(FString::Printf(
				TEXT("'%s' is exempt from needing a gameplay attribute and no "
					 "probe here reads it. An exemption is a promise that "
					 "bespoke code reads the stat; nothing checks that promise "
					 "but this. Add a probe that grants the stat and asserts "
					 "the reading code's answer changes, or take "
					 "the stat off "
					 "UCataclysmPlayerClassStats::StatsWithNoAttribute(). "
					 "Issue #1025 is what an unkept one costs."),
				*Stat));
			continue;
		}

		(*Probe)(*this);
	}

	AddInfo(FString::Printf(
		TEXT("%d exempt stats, each with a probe that observes its reader"),
		Exempt.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryScaledStatIsAskedForTest,
	"Cataclysm.StatExemption.EveryStatTheDataScalesIsAskedForThroughThePipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every stat the shipped data SCALES is asked for through the pipeline, and a
 * scaled stat with no probe fails by name. Issue #1973.
 *
 * WHAT A SCALED ROW NEEDS THAT AN ORDINARY ONE DOES NOT. A scaled bonus is
 * never folded into its gameplay attribute -- it would be stale the moment the
 * reading moved -- so it reaches play ONLY where the consuming code asks for the
 * stat through the pipeline. Where the code reads the attribute instead, the row
 * is discarded in silence: nothing errors, nothing warns, and the node grants
 * nothing. `Ritualist_capstone_200#3` did exactly that from the day it was
 * written until the day this test's issue was filed.
 *
 * THE STAT SET COMES FROM THE DATA THE GAME LOADS, not from a list here. Every
 * row of the passive effect table and the enchantment effect table carrying a
 * scale contributes its stat, so a NEW scaled row on a stat nothing asks for
 * fails this test by name on the next run, which is the whole point.
 *
 * WHY THE PROBES MEASURE BEHAVIOUR. A source search cannot answer "does anything
 * ask for this stat": measured on 2026-09-17, deriving the answer from the
 * engine's call sites got three of eleven wrong, and two of the three are
 * unfollowable in principle -- one stat has no lookup call at all because its
 * asker finds the stat line and runs the pipeline inline, and another is asked
 * through a lambda that takes the stat as a parameter. So each probe grants a
 * scaled row, moves the reading, and asserts the engine's own answer moves.
 *
 * EACH PROBE USES A SCALE THE SHIPPED DATA REALLY PAIRS WITH ITS STAT. A probe
 * on a convenient reading no row uses could pass while every real row on that
 * stat was dead.
 *
 * WHAT THIS DOES NOT MEASURE, SAID PLAINLY. The eleven stats are paired with
 * fourteen scales in 31 combinations; this proves the STAT is asked, once per
 * stat. A scale's reading also has to be one the asker's own conditions carry,
 * and some readings are filled by a wrapper at particular call sites, so a
 * pairing can be dead while its stat is asked. Surveying all 31 is its own work.
 */
bool FCataclysmEveryScaledStatIsAskedForTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	const UDataTable* Passives = UCataclysmPassiveTree::LoadEffectTable();
	const UDataTable* Enchantments =
		UCataclysmItemModifiers::LoadEnchantmentEffectTable();
	if (!TestNotNull(TEXT("the passive effect table loads"),
					 const_cast<UDataTable*>(Passives))
		|| !TestNotNull(TEXT("the enchantment effect table loads"),
						const_cast<UDataTable*>(Enchantments)))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py "
					  "tools/generate_datatable_assets.py"));
		return false;
	}

	TSet<FString> Scaled;
	Passives->ForeachRow<FCataclysmPassiveEffectRow>(
		TEXT("EveryStatTheDataScales"),
		[&Scaled](const FName&, const FCataclysmPassiveEffectRow& Row)
		{
			if (!Row.Scale.IsEmpty())
			{
				Scaled.Add(Row.Stat);
			}
		});
	Enchantments->ForeachRow<FCataclysmEnchantmentEffectRow>(
		TEXT("EveryStatTheDataScales"),
		[&Scaled](const FName&, const FCataclysmEnchantmentEffectRow& Row)
		{
			if (!Row.Scale.IsEmpty())
			{
				Scaled.Add(Row.Stat);
			}
		});

	// A TABLE THAT LOADED EMPTY WOULD PASS EVERY LOOP BELOW, which is what a
	// stale or half-built asset looks like. Refused here rather than reported as
	// a clean run over nothing.
	if (!TestTrue(TEXT("the shipped data scales some stats"), Scaled.Num() > 0))
	{
		return false;
	}

	for (const FString& Stat : Scaled)
	{
		const FProbe* Probe = ScaledProbes().Find(Stat);
		if (!Probe)
		{
			AddError(FString::Printf(
				TEXT("a shipped row scales '%s' and no probe here proves the "
					 "engine asks for it. A scaled row is never folded into a "
					 "gameplay attribute, so a stat nothing asks for grants "
					 "NOTHING and says nothing. Add a probe that grants the "
					 "stat with a scale the data really pairs with it, moves "
					 "the reading, and asserts the engine's answer changes -- "
					 "or give the stat an asker. Issue #1973 is what one dead "
					 "row cost."),
				*Stat));
			continue;
		}
		(*Probe)(*this);
	}

	// AND THE LIST THE GENERATOR REFUSES BY IS THE SAME SET, which one Python
	// check holds: `tools/tests/` reads the probe table above and requires it to
	// equal `STATS_WITH_AN_ASKER` in `tools/generate_datatables.py`. Stated here
	// so a reader of this test knows where the refusal lives.
	return true;
}

#endif  // WITH_AUTOMATION_TESTS
