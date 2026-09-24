// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
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
#include "AbilitySystem/CataclysmGameplayAbility.h"
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
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "EngineUtils.h"
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
	 * `minion_duration`, read by `ACataclysmMinion::Spawn` on the lifetime the
	 * summoning states. Issue #1515, the Kept Longer node.
	 *
	 * TWO SUMMONERS, for the reason health gives: the lifetime is set once, at
	 * the summoning. A test world's clock does not move, so a fresh minion's
	 * remaining lifespan is the whole of what it was given.
	 */
	void ProbeDuration(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter Geared(World, /*AttackDamage=*/0.0f);
		Grant(Geared.Actor, TEXT("minion_duration"), IncreasePercent);

		ACataclysmMinion* PlainImp = SummonImp(Test, World, Plain.Actor);
		ACataclysmMinion* GearedImp = SummonImp(Test, World, Geared.Actor);
		if (!PlainImp || !GearedImp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(PlainImp)) { PlainImp->Destroy(); } };
		ON_SCOPE_EXIT { if (IsValid(GearedImp)) { GearedImp->Destroy(); } };

		Test.TestTrue(TEXT("the plain imp has a lifespan at all"),
					  PlainImp->GetLifeSpan() > 0.0f);
		Test.TestTrue(
			TEXT("minion_duration lengthens a minion's lifespan, so "
				 "ACataclysmMinion::Spawn really reads it"),
			GearedImp->GetLifeSpan() > PlainImp->GetLifeSpan() + 0.001f);
	}

	/**
	 * `cripple_and_weaken_duration`, read by `UCataclysmAilments::Apply` where
	 * a Cripple or a Weaken is created. Issue #1515, the Deeper Hurt node.
	 *
	 * THE SAME CRIPPLE FROM TWO APPLIERS, one granted the stat, each on its own
	 * target, and the time left on each compared. A test world's clock does not
	 * move, so a fresh debuff's time left is the whole of what it was given.
	 */
	/**
	 * A summoner holding Summon Imp, with these stats on its line, and the
	 * living count its skill holds after one of its imps dies. Issue #1515.
	 */
	int32 LivingAfterALoss(FAutomationTestBase& Test, UWorld* World,
						   const TMap<FName, float>& Stats)
	{
		FScopedFighter Summoner(World, /*AttackDamage=*/0.0f);
		UCataclysmAbilitySystemComponent* System = Summoner.AbilitySystem;
		const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
			UCataclysmSummonSkill::StaticClass(), ECataclysmAbilitySlot::Special,
			/*Level=*/100, Summoner.Actor);
		FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
		UCataclysmSummonSkill* Skill =
			Spec ? Cast<UCataclysmSummonSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Test.TestNotNull(TEXT("a summon skill"), Skill))
		{
			return -1;
		}
		Skill->Params = UCataclysmSkillShapes::ParseParams(
			TEXT("Count=1; MaxActive=3; Duration=20; Radius=3; Minions=Imp:1"));

		TMap<FName, FCataclysmStatInputs> Inputs;
		for (const TPair<FName, float>& Stat : Stats)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = Stat.Value;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(Stat.Key);
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
		}
		System->SetStatInputs(MoveTemp(Inputs));

		ACataclysmMinion* Imp = Skill->SummonOne();
		UAbilitySystemComponent* ImpSystem = UCataclysmTargeting::AbilitySystemOf(Imp);
		if (!Test.TestNotNull(TEXT("an imp with an ability system"), ImpSystem))
		{
			return -1;
		}
		ImpSystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 0.0f);
		const int32 Living = Skill->LivingMinionCount();
		for (ACataclysmMinion* Left : Skill->Minions)
		{
			if (IsValid(Left))
			{
				Left->Destroy();
			}
		}
		return Living;
	}

	/**
	 * `minion_death_replaced_every_seconds`, read by
	 * `UCataclysmSummonSkill::ReplaceLost` after a commanded death. Issue #1515,
	 * Press-Ganged. A summoner holding it has its dead imp replaced; a plain one
	 * does not.
	 */
	void ProbeReplacedOnDeath(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const int32 Plain = LivingAfterALoss(Test, World, {});
		const int32 Held = LivingAfterALoss(Test, World,
			{{FName(UCataclysmSummonSkill::ReplacedOnDeathStat), 10.0f}});
		Test.TestEqual(TEXT("a plain summoner's dead imp is not replaced"), Plain, 0);
		Test.TestEqual(
			TEXT("minion_death_replaced_every_seconds replaces it, so "
				 "UCataclysmSummonSkill::ReplaceLost really reads it"),
			Held, 1);
	}

	/**
	 * `minion_explosion_replaced_every_seconds`, the same for an imp whose
	 * death is an explosion. Issue #1515, Rekindled.
	 */
	void ProbeReplacedOnExplosion(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FName Explodes(TEXT("minion_explodes_on_death"));
		const int32 Plain = LivingAfterALoss(Test, World, {{Explodes, 1.0f}});
		const int32 Held = LivingAfterALoss(Test, World,
			{{Explodes, 1.0f},
			 {FName(UCataclysmSummonSkill::ReplacedOnExplosionStat), 5.0f}});
		Test.TestEqual(TEXT("a plain summoner's exploded imp is not replaced"),
			Plain, 0);
		Test.TestEqual(
			TEXT("minion_explosion_replaced_every_seconds replaces it, so "
				 "UCataclysmSummonSkill::ReplaceLost really reads it"),
			Held, 1);
	}

	/**
	 * `melee_reach_metres`, read by `UCataclysmSkillTemplate::MeleeReachBonusCm`
	 * into a melee strike's reach. Issue #1515, Overreach. The same melee strike
	 * on two fighters, one granted a flat metre and a half: its reach is longer.
	 */
	void ProbeMeleeReach(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const auto ReachOfAMeleeStrike = [&Test, World](float GrantedMetres)
		{
			FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
			UCataclysmAbilitySystemComponent* System = Fighter.AbilitySystem;
			const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
				UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Special,
				/*Level=*/100, Fighter.Actor);
			FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
			UCataclysmSkillTemplate* Strike = Spec
				? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance())
				: nullptr;
			if (!Test.TestNotNull(TEXT("a strike"), Strike))
			{
				return -1.0f;
			}
			Strike->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=2"));
			Strike->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Melee"));

			if (GrantedMetres > 0.0f)
			{
				FCataclysmStatModifier Flat;
				Flat.Bucket = ECataclysmStatBucket::Flat;
				Flat.Source = ECataclysmModifierSource::PassiveKeystone;
				Flat.Value = GrantedMetres;
				TMap<FName, FCataclysmStatInputs> Inputs;
				FCataclysmStatInputs& Line =
					Inputs.FindOrAdd(FName(UCataclysmSkillTemplate::MeleeReachMetresStat));
				Line.Base = 0.0f;
				Line.Modifiers = {Flat};
				System->SetStatInputs(MoveTemp(Inputs));
			}
			return Strike->ScaledRadiusCm();
		};

		const float Plain = ReachOfAMeleeStrike(0.0f);
		const float Granted = ReachOfAMeleeStrike(1.5f);
		Test.TestTrue(TEXT("the plain strike reaches at all"), Plain > 0.0f);
		Test.TestTrue(
			TEXT("melee_reach_metres lengthens a melee strike, so "
				 "UCataclysmSkillTemplate::MeleeReachBonusCm really reads it"),
			Granted > Plain + 0.001f);
	}

	void ProbeCrippleAndWeakenDuration(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FCataclysmAilmentKind* Cripple =
			UCataclysmAilments::KindNamed(TEXT("Cripple"));
		if (!Test.TestNotNull(TEXT("Cripple is an ailment"), Cripple))
		{
			return;
		}
		const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Cripple->TagName), /*ErrorIfNotFound=*/false);

		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter Geared(World, /*AttackDamage=*/0.0f);
		Grant(Geared.Actor, UCataclysmAilments::CrippleAndWeakenDurationStat,
			  IncreasePercent);
		FScopedFighter PlainTarget(World, /*AttackDamage=*/0.0f);
		FScopedFighter GearedTarget(World, /*AttackDamage=*/0.0f);

		UCataclysmAilments::Apply(Plain.Actor, PlainTarget.Actor, *Cripple,
								  /*Magnitude=*/1.0f);
		UCataclysmAilments::Apply(Geared.Actor, GearedTarget.Actor, *Cripple,
								  /*Magnitude=*/1.0f);

		const auto SecondsLeftOn = [&Tag](const FScopedFighter& Target)
		{
			float Longest = 0.0f;
			for (const float Seconds :
				 Target.AbilitySystem->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
						 FGameplayTagContainer(Tag))))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
			return Longest;
		};

		Test.TestTrue(TEXT("the plain applier's Cripple lasts at all"),
					  SecondsLeftOn(PlainTarget) > 0.0f);
		Test.TestTrue(
			TEXT("cripple_and_weaken_duration lengthens a Cripple its holder "
				 "applies, so UCataclysmAilments::Apply really reads it"),
			SecondsLeftOn(GearedTarget) > SecondsLeftOn(PlainTarget) + 0.001f);
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
	 * Grant several flat figures at once. `GrantFlat` above replaces the whole
	 * stat line, so two stats that only work together must arrive together.
	 */
	void GrantFlats(AActor* Who, const TMap<FName, float>& Stats)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Who));
		if (!System)
		{
			return;
		}

		TMap<FName, FCataclysmStatInputs> Inputs;
		for (const TPair<FName, float>& Stat : Stats)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = Stat.Value;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(Stat.Key);
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
		}
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/**
	 * Shared Ruin's two stats, read by `ACataclysmMinion::DeathBlast`. Issue
	 * #1515. Two summoners a hundred metres apart each lose an imp that does
	 * not explode; a target two metres from each imp. Both hold `Held`, and
	 * only the second holds `Probed` as well: the blast needs both figures, so
	 * the first target must lose nothing and the second something.
	 */
	void ProbeDeathBlastStat(FAutomationTestBase& Test, const TCHAR* Probed,
							 float ProbedValue, const TCHAR* Held, float HeldValue)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Without(World, FVector::ZeroVector);
		FScopedSwinger WithoutTarget(World, FVector(3 * M, 0, 0));
		FScopedSwinger With(World, FVector(0, 100 * M, 0));
		FScopedSwinger WithTarget(World, FVector(3 * M, 100 * M, 0));
		GrantFlats(Without.Actor, {{FName(Held), HeldValue}});
		GrantFlats(With.Actor, {{FName(Held), HeldValue}, {FName(Probed), ProbedValue}});

		ACataclysmMinion* WithoutImp =
			ImpToldItsExplosion(Test, Without.Actor, FVector(1 * M, 0, 0));
		ACataclysmMinion* WithImp =
			ImpToldItsExplosion(Test, With.Actor, FVector(1 * M, 100 * M, 0));
		if (!WithoutImp || !WithImp)
		{
			return;
		}
		ON_SCOPE_EXIT
		{
			for (ACataclysmMinion* Imp : {WithoutImp, WithImp})
			{
				if (IsValid(Imp))
				{
					Imp->Destroy();
				}
			}
		};

		KillMinion(WithoutImp);
		KillMinion(WithImp);

		const FGameplayAttribute Health = Vital::GetHealthAttribute();
		Test.TestEqual(
			FString::Printf(TEXT("with %s alone a dying imp hurts nobody"), Held),
			TargetHealthPool - WithoutTarget.Get(Health), 0.0f, 0.001f);
		Test.TestTrue(
			FString::Printf(
				TEXT("and with %s as well it does, so ACataclysmMinion::DeathBlast "
					 "really reads it"),
				Probed),
			TargetHealthPool - WithTarget.Get(Health) > 0.001f);
	}

	void ProbeDeathBlastPercent(FAutomationTestBase& Test)
	{
		ProbeDeathBlastStat(Test,
			ACataclysmMinion::DeathBlastPercentOfMaximumHealthStat, 20.0f,
			ACataclysmMinion::DeathBlastRadiusMetresStat, 4.0f);
	}

	void ProbeDeathBlastRadius(FAutomationTestBase& Test)
	{
		ProbeDeathBlastStat(Test,
			ACataclysmMinion::DeathBlastRadiusMetresStat, 4.0f,
			ACataclysmMinion::DeathBlastPercentOfMaximumHealthStat, 20.0f);
	}

	/**
	 * `shield_break_destroys_minion_every_seconds`, read in
	 * `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`. Issue #1515,
	 * Sacrificial Ward. Two shielded summoners, each with an imp, take a blow
	 * worth three times the shield: the plain one's shield breaks, and the one
	 * holding the stat keeps its shield and loses its imp instead.
	 */
	void ProbeShieldWard(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const auto ShieldLeft = [&Test, World](bool bHeld, const FVector& Where)
		{
			FScopedFighter Attacker(World, /*AttackDamage=*/0.0f);
			// NO LOCATION TO SET: `FScopedFighter` has no root component, so it
			// stands at the origin. The ward does not look at distance.
			FScopedFighter Summoner(World, /*AttackDamage=*/0.0f);
			Summoner.AbilitySystem->SetNumericAttributeBase(
				Vital::GetMaxEnergyShieldAttribute(), 100.0f);
			Summoner.AbilitySystem->SetNumericAttributeBase(
				Vital::GetEnergyShieldAttribute(), 100.0f);
			if (bHeld)
			{
				GrantFlats(Summoner.Actor,
					{{FName(UCataclysmAbilitySystemComponent::
								ShieldBreakDestroysMinionEverySecondsStat),
					  3.0f}});
			}
			ACataclysmMinion* Imp =
				ImpToldItsExplosion(Test, Summoner.Actor, Where + FVector(1 * M, 0, 0));
			ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };
			UCataclysmSkillEffects::ApplyDirectDamage(
				Attacker.Actor, Summoner.Actor, 300.0f);
			return Summoner.AbilitySystem->GetNumericAttribute(
				Vital::GetEnergyShieldAttribute());
		};

		const float Plain = ShieldLeft(false, FVector::ZeroVector);
		const float Held = ShieldLeft(true, FVector(0, 100 * M, 0));
		Test.TestEqual(TEXT("a plain summoner's shield breaks"), Plain, 0.0f, 0.001f);
		Test.TestEqual(
			TEXT("and one holding shield_break_destroys_minion_every_seconds keeps "
				 "it, so PostGameplayEffectExecute really reads it"),
			Held, 100.0f, 0.001f);
	}

	/**
	 * `skill_cost_paid_from_energy_shield`, read by
	 * `UCataclysmGameplayAbility::PoolPaying`. Issue #1515, Cast from Ward. A
	 * character with no mana and a full shield: a cost of 20 finds no pool to pay
	 * it, and with the stat held the shield pays.
	 */
	void ProbeCostPaidFromShield(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const auto Paying = [World](bool bHeld)
		{
			FScopedFighter Caster(World, /*AttackDamage=*/0.0f);
			Caster.AbilitySystem->SetNumericAttributeBase(Vital::GetManaAttribute(), 0.0f);
			Caster.AbilitySystem->SetNumericAttributeBase(
				Vital::GetMaxEnergyShieldAttribute(), 100.0f);
			Caster.AbilitySystem->SetNumericAttributeBase(
				Vital::GetEnergyShieldAttribute(), 100.0f);
			if (bHeld)
			{
				GrantFlats(Caster.Actor,
					{{FName(UCataclysmGameplayAbility::CostPaidFromEnergyShieldStat), 1.0f}});
			}
			return UCataclysmGameplayAbility::PoolPaying(Caster.AbilitySystem, 20.0f);
		};

		Test.TestFalse(TEXT("with no mana, a plain caster finds nothing to pay 20 from"),
					   Paying(false).IsValid());
		Test.TestTrue(
			TEXT("and one holding skill_cost_paid_from_energy_shield pays it from the "
				 "shield, so UCataclysmGameplayAbility::PoolPaying really reads it"),
			Paying(true) == Vital::GetEnergyShieldAttribute());
	}

	/**
	 * `applied_cripple_and_weaken_held_within_metres`, read by
	 * `UCataclysmDebuffs::HoldAppliedNearbyStep`. Issue #1515, No Second Wind.
	 * Two appliers a hundred metres apart each cripple an enemy two metres away
	 * for three seconds; after a second of the clock and a step, the plain one's
	 * has two left and the one holding the stat still has three.
	 */
	void ProbeAppliedHeldNearby(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger PlainsEnemy(World, FVector(2 * M, 0, 0));
		FScopedSwinger Held(World, FVector(0, 100 * M, 0));
		FScopedSwinger HeldsEnemy(World, FVector(2 * M, 100 * M, 0));
		GrantFlats(Held.Actor,
			{{FName(UCataclysmDebuffs::AppliedHeldWithinMetresStat), 4.0f}});

		const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
		UCataclysmSkillEffects::ApplyTagForDuration(Plain.Actor, PlainsEnemy.Actor,
			Cripple, 3.0f, 30.0f);
		UCataclysmSkillEffects::ApplyTagForDuration(Held.Actor, HeldsEnemy.Actor,
			Cripple, 3.0f, 30.0f);

		World->TimeSeconds += 1.0f;
		UCataclysmDebuffs::HoldAppliedNearbyStep(Plain.Actor, 1.0f);
		UCataclysmDebuffs::HoldAppliedNearbyStep(Held.Actor, 1.0f);

		const auto Left = [&Cripple](const FScopedSwinger& Who)
		{
			FGameplayTagContainer Tags;
			Tags.AddTag(Cripple);
			float Longest = -1.0f;
			for (const float Seconds : Who.AbilitySystem->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Tags)))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
			return Longest;
		};
		Test.TestEqual(TEXT("a plain applier's Cripple runs down to two"),
					   Left(PlainsEnemy), 2.0f, 0.01f);
		Test.TestEqual(
			TEXT("and one holding applied_cripple_and_weaken_held_within_metres keeps "
				 "three, so HoldAppliedNearbyStep really reads it"),
			Left(HeldsEnemy), 3.0f, 0.01f);
	}

	/**
	 * Nothing Stops It's two stats, read in
	 * `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`. Issue #1515.
	 * `Blows` are dealt in turn to a fighter holding `Stats`, and what it has
	 * left after each is answered.
	 */
	TArray<float> HealthAfterBlows(UWorld* World, const TMap<FName, float>& Stats,
								   const TArray<float>& Blows)
	{
		FScopedFighter Attacker(World, /*AttackDamage=*/0.0f);
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		GrantFlats(Defender.Actor, Stats);

		TArray<float> Left;
		for (const float Blow : Blows)
		{
			UCataclysmSkillEffects::ApplyDirectDamage(
				Attacker.Actor, Defender.Actor, Blow);
			Left.Add(Defender.Health());
		}
		return Left;
	}

	/** The interval: above zero, a blow worth twice the pool leaves one health. */
	void ProbeLethalHitSurvived(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const float Lethal = TargetHealthPool * 2.0f;
		const TArray<float> Plain = HealthAfterBlows(World, {}, {Lethal});
		const TArray<float> Held = HealthAfterBlows(World,
			{{FName(UCataclysmAbilitySystemComponent::LethalHitSurvivedEverySecondsStat),
			  20.0f}},
			{Lethal});
		Test.TestEqual(TEXT("a plain fighter is killed by the blow"), Plain[0], 0.0f,
					   0.001f);
		Test.TestEqual(
			TEXT("and one holding lethal_hit_survived_every_seconds is left at one, "
				 "so PostGameplayEffectExecute really reads it"),
			Held[0], 1.0f, 0.001f);
	}

	/**
	 * The window: both fighters survive a lethal blow, and only the one holding
	 * the seconds takes nothing from the blow after it.
	 */
	void ProbeImmuneAfterLethalHit(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FName Every(UCataclysmAbilitySystemComponent::LethalHitSurvivedEverySecondsStat);
		const FName Immune(UCataclysmAbilitySystemComponent::ImmuneAfterLethalHitSecondsStat);
		const TArray<float> Blows = {TargetHealthPool * 2.0f, 0.5f};
		const TArray<float> Plain = HealthAfterBlows(World, {{Every, 20.0f}}, Blows);
		const TArray<float> Held =
			HealthAfterBlows(World, {{Every, 20.0f}, {Immune, 2.0f}}, Blows);
		Test.TestEqual(TEXT("without the window, the second blow hurts"), Plain[1],
					   0.5f, 0.001f);
		Test.TestEqual(
			TEXT("and with damage_immunity_after_lethal_hit_seconds it does not, so "
				 "PostGameplayEffectExecute really reads it"),
			Held[1], 1.0f, 0.001f);
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

	/**
	 * `mana_cost_as_current_health_percent` is read by
	 * `UCataclysmGameplayAbility::ManaCostPaidAsHealthPercent`. Issues #1820 and
	 * #41.
	 *
	 * TWO CHARACTERS, one carrying a flat row of 5 with no condition, each asked
	 * about the same 40-mana skill. The dungeon floor rule writes this row under
	 * `mana_below`; the condition is not what is being probed here, so it is left
	 * off and the reading is asked directly.
	 */
	void ProbeManaCostAsCurrentHealthPercent(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger Converted(World, FVector(0, 100 * M, 0));
		GrantFlat(Converted.Actor,
				  UCataclysmGameplayAbility::ManaCostAsCurrentHealthPercentStat, 5.0f);

		UCataclysmStrikeSkill* PlainSkill = GrantCostingSkill(Plain);
		UCataclysmStrikeSkill* ConvertedSkill = GrantCostingSkill(Converted);
		if (!Test.TestNotNull(TEXT("a skill that costs mana"), PlainSkill)
			|| !Test.TestNotNull(TEXT("and one under the row"), ConvertedSkill))
		{
			return;
		}

		Test.TestEqual(TEXT("a character with no row pays no share of health"),
			PlainSkill->ManaCostPaidAsHealthPercent(Plain.AbilitySystem), 0.0f,
			0.001f);
		Test.TestEqual(TEXT("and one carrying a row of 5 pays 5% of current health"),
			ConvertedSkill->ManaCostPaidAsHealthPercent(Converted.AbilitySystem),
			5.0f, 0.001f);
	}

	/**
	 * `cooldown_lengthening` is read by
	 * `UCataclysmGameplayAbility::CooldownAfterReduction`. Issue #1994.
	 *
	 * TWO CHARACTERS, one carrying a flat row of 100, asked for the same four
	 * second cooldown. A bare actor is enough: the lookup needs only an ability
	 * system with the combat set, which is what decides that a cooldown is
	 * worked out at all.
	 */
	void ProbeCooldownLengthening(FAutomationTestBase& Test)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const auto Make = [World]()
		{
			AActor* Actor = World->SpawnActor<AActor>();
			check(Actor);
			UCataclysmAbilitySystemComponent* System =
				NewObject<UCataclysmAbilitySystemComponent>(Actor);
			System->RegisterComponent();
			UCataclysmCombatAttributeSet* Combat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			System->AddAttributeSetSubobject(Combat);
			System->InitAbilityActorInfo(Actor, Actor);
			return System;
		};
		UCataclysmAbilitySystemComponent* Plain = Make();
		UCataclysmAbilitySystemComponent* Longer = Make();

		FCataclysmStatModifier Flat;
		Flat.Bucket = ECataclysmStatBucket::Flat;
		Flat.Source = ECataclysmModifierSource::Enchantment;
		Flat.Value = 100.0f;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line =
			Inputs.FindOrAdd(FName(UCataclysmSkillSlots::CooldownLengtheningStat));
		Line.Base = 0.0f;
		Line.Modifiers = {Flat};
		Longer->SetStatInputs(MoveTemp(Inputs));

		Test.TestEqual(TEXT("a character with no row keeps a four second cooldown"),
			UCataclysmGameplayAbility::CooldownAfterReduction(Plain, 4.0f),
			4.0f, 0.001f);
		Test.TestEqual(TEXT("and one carrying a row of 100 waits eight"),
			UCataclysmGameplayAbility::CooldownAfterReduction(Longer, 4.0f),
			8.0f, 0.001f);
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

	/**
	 * Behind the Veil's two numbers, granted together.
	 *
	 * TOGETHER BECAUSE `GrantFlat` ABOVE REPLACES THE WHOLE STAT MAP. Calling it
	 * once per stat would leave only the second, and the probe would then
	 * measure a character missing the other half.
	 */
	void GrantTheVeil(AActor* Who, float Metres, float Minimum)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Who));
		if (!System)
		{
			return;
		}

		auto Flat = [](float Value)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::PassiveKeystone;
			Row.Value = Value;

			FCataclysmStatInputs Line;
			Line.Base = 0.0f;
			Line.Modifiers = {Row};
			return Line;
		};

		TMap<FName, FCataclysmStatInputs> Inputs;
		Inputs.Add(FName(TEXT("minions_draw_nearby_enemies_metres")),
				   Flat(Metres));
		Inputs.Add(FName(TEXT("minions_draw_nearby_enemies_minimum")),
				   Flat(Minimum));
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/**
	 * A character on the players' side and a creature standing `Metres` away.
	 *
	 * REAL CHARACTERS AND NOT `FScopedFighter`, because both probes below turn
	 * on the distance between the two, and a bare actor with no root component
	 * stands at the origin whatever it is told.
	 */
	bool AVeilPair(FAutomationTestBase& Test, UWorld* World, float Metres,
				   ACataclysmEnemyCharacter*& Summoner,
				   ACataclysmEnemyCharacter*& Hunting)
	{
		Summoner = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		Hunting = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * M, 0, 0), FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("a summoner"), Summoner)
			|| !Test.TestNotNull(TEXT("a creature hunting it"), Hunting))
		{
			return false;
		}

		Summoner->SetGenericTeamId(
			UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		return true;
	}

	void ProbeMinionsDrawNearbyEnemiesMetres(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmEnemyCharacter* Summoner = nullptr;
		ACataclysmEnemyCharacter* Hunting = nullptr;
		if (!AVeilPair(Test, World, /*Metres=*/8.0f, Summoner, Hunting))
		{
			return;
		}

		ACataclysmMinion* Imp = SummonImp(Test, World, Summoner);
		if (!Imp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

		// THE ONLY THING THAT MOVES IS THE REACH ROW. The creature stands eight
		// metres away throughout, so a reader that ignored this stat would
		// answer the same both times.
		GrantTheVeil(Summoner, /*Metres=*/6.0f, /*Minimum=*/1.0f);
		Test.TestNull(
			TEXT("a reach of six does not reach a creature eight metres away"),
			UCataclysmCommand::MinionDrawingEnemyFrom(Summoner, Hunting));

		GrantTheVeil(Summoner, /*Metres=*/10.0f, /*Minimum=*/1.0f);
		Test.TestTrue(
			TEXT("and a reach of ten does, so "
				 "UCataclysmCommand::MinionDrawingEnemyFrom really reads "
				 "minions_draw_nearby_enemies_metres"),
			UCataclysmCommand::MinionDrawingEnemyFrom(Summoner, Hunting) == Imp);
	}

	void ProbeMinionsDrawNearbyEnemiesMinimum(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmEnemyCharacter* Summoner = nullptr;
		ACataclysmEnemyCharacter* Hunting = nullptr;
		if (!AVeilPair(Test, World, /*Metres=*/4.0f, Summoner, Hunting))
		{
			return;
		}

		ACataclysmMinion* Imp = SummonImp(Test, World, Summoner);
		if (!Imp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

		// AND HERE ONLY THE COUNT MOVES. One minion stands there throughout and
		// the reach is the same both times.
		GrantTheVeil(Summoner, /*Metres=*/10.0f, /*Minimum=*/1.0f);
		Test.TestTrue(
			TEXT("a row asking for one is satisfied by one minion"),
			UCataclysmCommand::MinionDrawingEnemyFrom(Summoner, Hunting) == Imp);

		GrantTheVeil(Summoner, /*Metres=*/10.0f, /*Minimum=*/5.0f);
		Test.TestNull(
			TEXT("and one asking for five is not, so "
				 "UCataclysmCommand::MinionDrawingEnemyFrom really reads "
				 "minions_draw_nearby_enemies_minimum"),
			UCataclysmCommand::MinionDrawingEnemyFrom(Summoner, Hunting));
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
	 * `class_resource`, scaled by maximum mana, asked by
	 * `UCataclysmAbilitySystemComponent::MaximumClassResource`. Issue #1515:
	 * `Ritualist_keystone_d_kC` Vessel is the pairing, and it granted nothing
	 * until that lookup existed.
	 */
	void ProbeScaledMaximumClassResource(FAutomationTestBase& Test)
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

		// A CLASS RESOURCE SET FIRST, which the fighter does not carry: only a
		// player class has a bar. Without it the write below is refused with an
		// engine error and the lookup answers nothing both times -- which is how
		// this probe failed the first time it ran, on 2026-09-23, five days after
		// it was written.
		System->AddAttributeSetSubobject(
			NewObject<UCataclysmClassResourceAttributeSet>(Caster.Actor));

		// A HUNDRED OF EACH: the bar this scales, and the mana it scales by.
		// `Ritualist_keystone_d_kC` Vessel reads "1 Fervour for every 20 maximum
		// mana", so a hundred mana is five steps.
		System->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
			100.0f);
		ScaledBy(Caster.Actor, TEXT("class_resource"), 1.0f,
				 ECataclysmStatScale::PerPointOfMaximumMana, /*Base=*/100.0f);

		// THE READING MOVES FROM NOTHING TO A HUNDRED, so the scale's own answer
		// moves with it rather than the probe reading a figure that was always
		// there.
		System->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 0.0f);
		const float Without = System->MaximumClassResource();
		System->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 100.0f);
		const float With = System->MaximumClassResource();

		Test.TestTrue(
			FString::Printf(TEXT("class_resource is asked for, so maximum mana "
								 "raises the bar: %.2f against %.2f"),
							With, Without),
			With > Without);
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
	 * `damage_taken`, scaled by debuffs carried, asked by `DefenderStat` in
	 * `UCataclysmDamageCalculation` on every blow the defender takes. Issue
	 * #1815: "You take 10%-20% increased damage for each second you have been
	 * in combat, up to 10 stacks" is the first scaled row on this stat.
	 *
	 * DEBUFFS AND NOT SECONDS IN COMBAT, because this probe measures the ask
	 * and not the reading, and debuffs are the reading the probes beside it
	 * already know how to move.
	 */
	void ProbeScaledDamageTaken(FAutomationTestBase& Test)
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
			Combat::GetDamageTakenAttribute(), 100.0f);
		ScaledBy(Defender.Actor, TEXT("damage_taken"), 100.0f,
				 ECataclysmStatScale::PerDebuffCarried, /*Base=*/100.0f);

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
			FString::Printf(TEXT("damage_taken is asked for, so debuffs raise it "
								 "and more lands: %.2f against %.2f"),
							Carrying, Clean),
			Carrying > Clean + 0.001f);
	}

	/**
	 * `crit_chance`, scaled by debuffs carried, asked through `StatForSkill` at
	 * the critical strike site in `UCataclysmVitalAttributeSet` on every blow.
	 * Issue #1815: "Each unique debuff on an enemy increases your crit chance
	 * against them by 5%-10%" is the first scaled row on this stat.
	 *
	 * THE ROLL IS PINNED AT 30, set at the console's own priority and restored
	 * after, so a chance of 20 misses and one of 60 lands. The chance is 20 with
	 * no debuff and 60 with two, at 100% increased a debuff.
	 */
	void ProbeScaledCritChance(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		IConsoleVariable* Roll =
			IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.CritRoll"));
		if (!Test.TestNotNull(TEXT("the critical strike roll can be pinned"), Roll))
		{
			return;
		}
		const float PreviousRoll = Roll->GetFloat();
		Roll->Set(30.0f, ECVF_SetByConsole);
		ON_SCOPE_EXIT { Roll->Set(PreviousRoll, ECVF_SetByConsole); };

		FScopedFighter Attacker(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		ScaledBy(Attacker.Actor, TEXT("crit_chance"), 100.0f,
				 ECataclysmStatScale::PerDebuffCarried, /*Base=*/20.0f);

		FCataclysmDamageResult Clean;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(),
										 FCataclysmHitDelivery(), &Clean);
		GiveTwoDebuffs(Attacker.Actor);
		FCataclysmDamageResult Carrying;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(),
										 FCataclysmHitDelivery(), &Carrying);

		if (!Test.TestTrue(TEXT("both blows landed"),
						   Clean.DealtToHealth > 0.0f && Carrying.DealtToHealth > 0.0f))
		{
			return;
		}
		Test.TestFalse(TEXT("at 20% the pinned roll of 30 is not a critical strike"),
					   Clean.bWasCritical);
		Test.TestTrue(TEXT("crit_chance is asked for, so two debuffs raise it to 60% "
						   "and the same roll is"),
					  Carrying.bWasCritical);
	}

	/**
	 * `max_health`, scaled by debuffs carried, asked by
	 * `UCataclysmAbilitySystemComponent::RefreshLiveMaximumHealth`, which each
	 * regeneration step calls for a character whose `max_health` line moves with
	 * its state. Issue #1815: "Each active minion reduces your maximum HP by
	 * 3%-6%" is the first scaled row on this stat.
	 *
	 * THE ATTRIBUTE IS WHAT MOVES, because every reader of maximum health reads
	 * the attribute; the refresh is what writes the scaled line onto it.
	 */
	void ProbeScaledMaximumHealth(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Character(World, /*AttackDamage=*/0.0f);
		Character.AbilitySystem->SetNumericAttributeBase(
			Vital::GetMaxHealthAttribute(), 1000.0f);
		ScaledBy(Character.Actor, TEXT("max_health"), 100.0f,
				 ECataclysmStatScale::PerDebuffCarried, /*Base=*/1000.0f);

		const auto MaximumOf = [&]()
		{
			return Character.AbilitySystem->GetNumericAttribute(
				Vital::GetMaxHealthAttribute());
		};

		UCataclysmRegeneration::ApplyStep(Character.Actor, 1.0f, 100.0f);
		const float Clean = MaximumOf();
		GiveTwoDebuffs(Character.Actor);
		UCataclysmRegeneration::ApplyStep(Character.Actor, 1.0f, 100.0f);
		const float Carrying = MaximumOf();

		Test.TestTrue(
			FString::Printf(TEXT("max_health is asked for, so two debuffs raise the "
								 "maximum: %.2f against %.2f"), Carrying, Clean),
			Carrying > Clean + 0.001f);
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

	/**
	 * `non_critical_damage` is read beside the critical multiplier in
	 * CataclysmVitalAttributeSet.cpp and applied by
	 * `UCataclysmDamageCalculation::Resolve` when the roll fails. Issue #1686.
	 *
	 * TWO ATTACKERS, the roll pinned so neither critically strikes, one carrying
	 * the stat at its base of 100 with 50% less on it. Its blow must be half.
	 */
	void ProbeNonCriticalDamage(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		IConsoleVariable* Roll =
			IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.CritRoll"));
		if (!Test.TestNotNull(TEXT("the critical strike roll can be pinned"), Roll))
		{
			return;
		}
		const float PreviousRoll = Roll->GetFloat();
		Roll->Set(100.0f, ECVF_SetByConsole);
		ON_SCOPE_EXIT { Roll->Set(PreviousRoll, ECVF_SetByConsole); };

		FScopedFighter Plain(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Carrying(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);

		FCataclysmStatModifier Half;
		Half.Bucket = ECataclysmStatBucket::More;
		Half.Source = ECataclysmModifierSource::Enchantment;
		Half.Value = -50.0f;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line =
			Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::NonCriticalDamageStat));
		Line.Base = UCataclysmDamageCalculation::NormalNonCriticalDamage;
		Line.Modifiers = {Half};
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Carrying.Actor))
			->SetStatInputs(MoveTemp(Inputs));

		FCataclysmDamageResult Clean;
		UCataclysmSkillEffects::ApplyHit(Plain.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(),
										 FCataclysmHitDelivery(), &Clean);
		FCataclysmDamageResult Cut;
		UCataclysmSkillEffects::ApplyHit(Carrying.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(),
										 FCataclysmHitDelivery(), &Cut);

		if (!Test.TestTrue(TEXT("both blows landed and neither critically struck"),
				Clean.DealtToHealth > 0.0f && !Clean.bWasCritical && !Cut.bWasCritical))
		{
			return;
		}
		Test.TestEqual(TEXT("the carrying attacker's non-critical blow is half"),
			Cut.DealtToHealth, Clean.DealtToHealth * 0.5f, 0.01f);
	}

	/**
	 * `projectile_later_hit_damage` is read by `ACataclysmProjectile::HitOne` for
	 * every landed contact after the first. Issue #1686.
	 *
	 * TWO CASTERS each fire a piercing shot through two enemies, the roll pinned
	 * so nothing critically strikes; one carries the stat at its base of 100 with
	 * 50% less on it. Its second contact must be half the plain one's.
	 */
	void ProbeProjectileLaterHitDamage(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

		const auto SecondContact = [&](bool bCarries, float Y)
		{
			FScopedSwinger Caster(World, FVector(0, Y, 0));
			FScopedSwinger Near(World, FVector(2 * M, Y, 0));
			FScopedSwinger Far(World, FVector(4 * M, Y, 0));
			if (bCarries)
			{
				FCataclysmStatModifier Half;
				Half.Bucket = ECataclysmStatBucket::More;
				Half.Source = ECataclysmModifierSource::Enchantment;
				Half.Value = -50.0f;
				TMap<FName, FCataclysmStatInputs> Inputs;
				FCataclysmStatInputs& Line = Inputs.FindOrAdd(
					FName(UCataclysmDamageCalculation::ProjectileLaterHitDamageStat));
				Line.Base = UCataclysmDamageCalculation::NormalProjectileLaterHitDamage;
				Line.Modifiers = {Half};
				Caster.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
			}

			ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
				Caster.Actor, FVector(0, Y, 0), FVector(6 * M, Y, 0),
				/*InRadiusCm=*/100.0f, /*InSpeed=*/2000.0f, /*InPierce=*/99,
				/*bInReturns=*/false, /*InDamagePercent=*/100.0f,
				FGameplayTagContainer(), /*bInBurns=*/false);
			if (!Shot)
			{
				return -1.0f;
			}
			const float Before = Far.Get(Vital::GetHealthAttribute());
			for (int32 Steps = 0; Steps < 200 && !Shot->bFinished; ++Steps)
			{
				Shot->Step(1.0f / 60.0f);
			}
			const float Taken = Before - Far.Get(Vital::GetHealthAttribute());
			Shot->Destroy();
			return Taken;
		};

		const float Plain = SecondContact(false, 0.0f);
		const float Cut = SecondContact(true, 50 * M);
		if (!Test.TestTrue(TEXT("both second contacts landed"), Plain > 0.0f && Cut > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("the carrying caster's second contact is half"),
			Cut, Plain * 0.5f, 0.01f);
	}

	/**
	 * `zone_first_sweep_damage` is read by
	 * `UCataclysmSkillTemplate::LeaveGroundAlong` where a zone is priced, and
	 * handed to the zone as its first sweep's figure. Issue #1686.
	 *
	 * TWO CASTERS, each in its own world, blink and leave ground; one carries
	 * the stat at its base of 100 with 50% less on it. Its zone's first sweep
	 * must be half a tick, where the plain caster's is a whole one.
	 */
	void ProbeZoneFirstSweepDamage(FAutomationTestBase& Test)
	{
		const auto FirstSweepShare = [&Test](bool bCarries) -> float
		{
			UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
			if (!World)
			{
				return -1.0f;
			}
			ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			FScopedSwinger Caster(World, FVector::ZeroVector);
			if (bCarries)
			{
				FCataclysmStatModifier Half;
				Half.Bucket = ECataclysmStatBucket::More;
				Half.Source = ECataclysmModifierSource::Enchantment;
				Half.Value = -50.0f;
				TMap<FName, FCataclysmStatInputs> Inputs;
				FCataclysmStatInputs& Line = Inputs.FindOrAdd(
					FName(UCataclysmDamageCalculation::ZoneFirstSweepDamageStat));
				Line.Base = UCataclysmDamageCalculation::NormalZoneFirstSweepDamage;
				Line.Modifiers = {Half};
				Caster.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
			}

			const FGameplayAbilitySpecHandle Handle =
				Caster.AbilitySystem->GiveAbilityInSlot(
					UCataclysmMovementSkill::StaticClass(),
					ECataclysmAbilitySlot::Movement, /*Level=*/100, Caster.Actor);
			FGameplayAbilitySpec* Spec = Handle.IsValid()
				? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmMovementSkill* Slip = Spec
				? Cast<UCataclysmMovementSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (!Slip)
			{
				return -1.0f;
			}
			Slip->SkillName = TEXT("A blink leaving ground");
			Slip->Params = UCataclysmSkillShapes::ParseParams(
				TEXT("Mode=Blink; Range=8; Radius=3.5; GroundRadius=3.5; "
					 "GroundDuration=6; GroundPercent=16.7"));
			Slip->SkillTags = UCataclysmSkillShapes::TagsFromCell(
				TEXT("Item.Weapon.Wand, Element.Demonic, Type.AOE.Persistent"));
			if (!Caster.AbilitySystem->TryActivateAbility(Handle))
			{
				return -1.0f;
			}

			for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
			{
				if (It->DamagePerTick > 0.0f)
				{
					return It->FirstSweepDamage / It->DamagePerTick;
				}
			}
			return -1.0f;
		};

		const float Plain = FirstSweepShare(false);
		const float Cut = FirstSweepShare(true);
		if (!Test.TestTrue(TEXT("both casters left ground that deals something"),
						   Plain > 0.0f && Cut > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("the plain caster's first sweep is a whole tick"), Plain, 1.0f, 0.001f);
		Test.TestEqual(TEXT("and the carrying caster's is half of one"), Cut, 0.5f, 0.001f);
	}

	/**
	 * `movement_speed`, scaled by debuffs carried, asked by
	 * `ACataclysmPlayerCharacter::RefreshMovementSpeed`. Issue #1833, for
	 * "Each skill use increases your movement speed by 3%-5% for 2 seconds,
	 * stacking up to 5 times".
	 *
	 * A SPAWNED PLAYER CHARACTER AND ITS MOVEMENT COMPONENT, because the
	 * speed a player runs at is what the component holds, and the attribute
	 * never carries a scaled row. The line is written after
	 * `OnRep_PlayerState`, which applies the class's stat line over it.
	 */
	void ProbeScaledMovementSpeed(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		UCataclysmAbilitySystemComponent* System =
			PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
		const UCharacterMovementComponent* Movement =
			Character ? Character->GetCharacterMovement() : nullptr;
		if (!Test.TestNotNull(TEXT("an ability system"), System)
			|| !Test.TestNotNull(TEXT("a movement component"), Movement))
		{
			return;
		}
		Character->SetPlayerState(PlayerState);
		Character->OnRep_PlayerState();

		constexpr float MetresPerSecond = 5.0f;
		System->SetNumericAttributeBase(Combat::GetMovementSpeedAttribute(),
										 MetresPerSecond);
		ScaledBy(Character, TEXT("movement_speed"), 50.0f,
				 ECataclysmStatScale::PerDebuffCarried, MetresPerSecond);

		Character->RefreshMovementSpeed();
		const float Clean = Movement->MaxWalkSpeed;
		GiveTwoDebuffs(Character);
		Character->RefreshMovementSpeed();
		const float Carrying = Movement->MaxWalkSpeed;

		Test.TestTrue(
			FString::Printf(TEXT("movement_speed is asked for, so two debuffs "
								 "raise the speed run at: %.2f against %.2f"),
							Carrying, Clean),
			Clean > 0.0f && Carrying > Clean + 0.001f);
	}

	const TMap<FString, FProbe>& ScaledProbes()
	{
		static const TMap<FString, FProbe> Made = {
			{TEXT("attack_damage"),              &ProbeScaledAttackDamage},
			{TEXT("spell_damage"),               &ProbeScaledSpellDamage},
			{TEXT("attack_speed"),               &ProbeScaledAttackSpeed},
			{TEXT("armor"),                      &ProbeScaledArmour},
			{TEXT("damage_reduction"),           &ProbeScaledDamageReduction},
			{TEXT("damage_taken"),               &ProbeScaledDamageTaken},
			{TEXT("crit_chance"),                &ProbeScaledCritChance},
			{TEXT("max_health"),                 &ProbeScaledMaximumHealth},
			{TEXT("retaliation"),                &ProbeScaledRetaliation},
			{TEXT("health_regen"),               &ProbeScaledHealthRegen},
			{TEXT("fervour_per_second"),         &ProbeScaledFervourPerSecond},
			{TEXT("fervour_from_minions"),       &ProbeScaledFervourFromMinions},
			{TEXT("fervour_per_enemy_in_reach"), &ProbeScaledFervourPerEnemyInReach},
			{TEXT("class_resource"),             &ProbeScaledMaximumClassResource},
			{TEXT("max_energy_shield"),          &ProbeScaledMaximumEnergyShield},
			{TEXT("mana_regen"),                 &ProbeScaledManaRegen},
			{TEXT("movement_speed"),             &ProbeScaledMovementSpeed},
		};
		return Made;
	}

	const TMap<FString, FProbe>& Probes()
	{
		static const TMap<FString, FProbe> Made = {
			{TEXT("minion_attack_speed"), &ProbeAttackSpeed},
			{TEXT("minion_damage"),       &ProbeDamage},
			{TEXT("minion_health"),       &ProbeHealth},
			{TEXT("minion_duration"),     &ProbeDuration},
			{TEXT("cripple_and_weaken_duration"), &ProbeCrippleAndWeakenDuration},
			{TEXT("melee_reach_metres"),  &ProbeMeleeReach},
			{TEXT("minion_death_replaced_every_seconds"), &ProbeReplacedOnDeath},
			{TEXT("minion_explosion_replaced_every_seconds"), &ProbeReplacedOnExplosion},
			{TEXT("minion_death_blast_percent_of_maximum_health"), &ProbeDeathBlastPercent},
			{TEXT("minion_death_blast_radius_metres"), &ProbeDeathBlastRadius},
			{TEXT("lethal_hit_survived_every_seconds"), &ProbeLethalHitSurvived},
			{TEXT("damage_immunity_after_lethal_hit_seconds"), &ProbeImmuneAfterLethalHit},
			{TEXT("shield_break_destroys_minion_every_seconds"), &ProbeShieldWard},
			{TEXT("skill_cost_paid_from_energy_shield"), &ProbeCostPaidFromShield},
			{TEXT("applied_cripple_and_weaken_held_within_metres"), &ProbeAppliedHeldNearby},
			{TEXT("mana_on_hit"),         &ProbeManaOnHit},
			{TEXT("mana_cost"),           &ProbeManaCost},
			{TEXT("cooldown_lengthening"), &ProbeCooldownLengthening},
			{TEXT("mana_cost_as_current_health_percent"),
									&ProbeManaCostAsCurrentHealthPercent},
			{TEXT("minion_explodes_on_death"), &ProbeExplodesOnDeath},
			{TEXT("minion_explosion_damage"),  &ProbeExplosionDamage},
			{TEXT("minion_hits_count_as_yours"), &ProbeHitsCountAsYours},
			{TEXT("minions_draw_nearby_enemies_metres"),
									&ProbeMinionsDrawNearbyEnemiesMetres},
			{TEXT("minions_draw_nearby_enemies_minimum"),
									&ProbeMinionsDrawNearbyEnemiesMinimum},
			{TEXT("non_critical_damage"), &ProbeNonCriticalDamage},
			{TEXT("projectile_later_hit_damage"), &ProbeProjectileLaterHitDamage},
			{TEXT("zone_first_sweep_damage"), &ProbeZoneFirstSweepDamage},
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionDurationFloorTest,
	"Cataclysm.StatExemption.AMinionDurationCutByAHundredPerCentKeepsTheStatedLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A summoner whose `minion_duration` stands at -100% summons a minion that
 * still expires. Issue #1515.
 *
 * WHY THIS GUARD HAS ITS OWN TEST. `ACataclysmMinion::Spawn` multiplies the
 * stated lifetime by one plus the summoner's increases, and at -100% that is
 * nothing. `SetLifeSpan(0)` means "never expires", so without the guard a
 * minion would stay for ever. The guard keeps the stated lifetime instead. No
 * shipped row reduces the duration, which is exactly why nothing else would
 * notice the guard going missing.
 *
 * AND -150% TOO, because the multiplier is floored at zero before it is used:
 * a larger cut must land on the same guard rather than on a negative lifespan.
 */
bool FCataclysmMinionDurationFloorTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	for (const float Cut : {-100.0f, -150.0f})
	{
		FScopedFighter Summoner(World, /*AttackDamage=*/0.0f);
		Grant(Summoner.Actor, TEXT("minion_duration"), Cut);

		ACataclysmMinion* Imp = SummonImp(*this, World, Summoner.Actor);
		if (!Imp)
		{
			return false;
		}
		ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

		// THE SAME TWENTY `SummonImp` STATES, and not zero, which would be a
		// minion that never leaves.
		TestEqual(*FString::Printf(
					  TEXT("at %.0f%% duration the imp keeps the stated lifetime"),
					  Cut),
				  Imp->GetLifeSpan(), 20.0f, 0.01f);
	}
	return true;
}

#endif  // WITH_AUTOMATION_TESTS
