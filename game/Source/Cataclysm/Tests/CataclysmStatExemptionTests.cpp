// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
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
 * increases, and `mana_on_hit` for whether it is removed. Three checks stop refusing
 * a passive row or an affix naming them because of that claim: the data
 * generator's vocabulary, `test_every_stat_is_one_the_game_supplies`, and
 * `Cataclysm.Passives.EveryStatAPassiveNodeGrantsHasAnAttributeBehindIt`.
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

	/** What every probe grants. Not any affix's top roll, so a reading that
	 *  matched one could only have come from the data. */
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
	 * One probe per exempt stat.
	 *
	 * A NAME WITH NO ENTRY HERE IS THE FAILURE THIS FILE EXISTS FOR. It is not a
	 * gap to be filled by adding an empty probe: an empty probe would pass and
	 * put the exemption straight back into the state issue #1025 describes.
	 */
	const TMap<FString, FProbe>& Probes()
	{
		static const TMap<FString, FProbe> Made = {
			{TEXT("minion_attack_speed"), &ProbeAttackSpeed},
			{TEXT("minion_damage"),       &ProbeDamage},
			{TEXT("minion_health"),       &ProbeHealth},
			{TEXT("mana_on_hit"),         &ProbeManaOnHit},
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
 * THIS IS THE HALF ISSUE #1025 WAS MISSING. The other three checks stop refusing
 * these stats; none of them asks whether the claim behind the exemption is true.
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
	// refactor that lost the contents looks like. The three checks that read it
	// would go back to refusing the rows, so this would be reported somewhere --
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

#endif  // WITH_AUTOMATION_TESTS
