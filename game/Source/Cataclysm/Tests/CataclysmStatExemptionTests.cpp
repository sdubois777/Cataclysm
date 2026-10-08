// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
// For the eleven probes that prove every scaled stat is asked for. #1973.
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmChorus.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmFollowThrough.h"
#include "AbilitySystem/CataclysmPotions.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmRisenImps.h"
#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmSecondSelf.h"
#include "AbilitySystem/CataclysmSharedBuffs.h"
#include "AbilitySystem/CataclysmShoulderThrough.h"
#include "AbilitySystem/CataclysmLeech.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmEquipmentComponent.h"
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
#include "Character/CataclysmImpCharacter.h"
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
#include "AbilitySystem/CataclysmTerrain.h"
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

		// THE BODY IS THE READING. An explosion destroys the minion at once,
		// and a quiet death leaves the body for half a second (issue #1528),
		// so the difference between the two deaths is visible without a
		// target at the moment of death.
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
	 * Four landed melee blows from `Holder` on a defender with 1,000 armour, and
	 * what the fourth took compared with the third. Rendering Blows, issue #1515:
	 * the third blow removes the holder's share of the armour, so the fourth
	 * takes more than the third exactly when both of its stats are read.
	 */
	float RendingBlowsFourthOverThird(UWorld* World, const FScopedFighter& Holder)
	{
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		Defender.AbilitySystem->SetNumericAttributeBase(
			Combat::GetArmorAttribute(), 1000.0f);

		FGameplayTagContainer Melee;
		Melee.AddTag(UCataclysmDamageCalculation::MeleeTag());

		float Taken[4] = {};
		for (float& Reading : Taken)
		{
			const float Before = Defender.Health();
			UCataclysmSkillEffects::ApplyHit(Holder.Actor, Defender.Actor, 100.0f, Melee);
			Reading = Before - Defender.Health();
		}
		return Taken[3] - Taken[2];
	}

	/**
	 * `third_melee_hit_armour_removed_percent`, read by
	 * `UCataclysmAbilitySystemComponent::NoteLandedMeleeHitFrom`. Issue #1515,
	 * Rendering Blows. With both stats the fourth blow takes more than the
	 * third; with only the seconds it takes the same.
	 */
	void ProbeRendPercent(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Held(World, 1000.0f);
		GrantFlats(Held.Actor,
			{{FName(UCataclysmAbilitySystemComponent::RendPercentStat), 20.0f},
			 {FName(UCataclysmAbilitySystemComponent::RendSecondsStat), 6.0f}});
		FScopedFighter SecondsOnly(World, 1000.0f);
		GrantFlats(SecondsOnly.Actor,
			{{FName(UCataclysmAbilitySystemComponent::RendSecondsStat), 6.0f}});

		Test.TestEqual(TEXT("without third_melee_hit_armour_removed_percent the "
							"fourth blow takes what the third did"),
					   RendingBlowsFourthOverThird(World, SecondsOnly), 0.0f, 0.01f);
		Test.TestTrue(TEXT("and with it the fourth takes more, so "
						   "NoteLandedMeleeHitFrom really reads it"),
					  RendingBlowsFourthOverThird(World, Held) > 0.01f);
	}

	/**
	 * `third_melee_hit_armour_removed_seconds`, read by the same function. With
	 * both stats the fourth blow takes more than the third; with only the share
	 * it takes the same, because a removal of no seconds is none.
	 */
	void ProbeRendSeconds(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Held(World, 1000.0f);
		GrantFlats(Held.Actor,
			{{FName(UCataclysmAbilitySystemComponent::RendPercentStat), 20.0f},
			 {FName(UCataclysmAbilitySystemComponent::RendSecondsStat), 6.0f}});
		FScopedFighter PercentOnly(World, 1000.0f);
		GrantFlats(PercentOnly.Actor,
			{{FName(UCataclysmAbilitySystemComponent::RendPercentStat), 20.0f}});

		Test.TestEqual(TEXT("without third_melee_hit_armour_removed_seconds the "
							"fourth blow takes what the third did"),
					   RendingBlowsFourthOverThird(World, PercentOnly), 0.0f, 0.01f);
		Test.TestTrue(TEXT("and with it the fourth takes more, so "
						   "NoteLandedMeleeHitFrom really reads it"),
					  RendingBlowsFourthOverThird(World, Held) > 0.01f);
	}

	/**
	 * The speed multiplier of an Imp two metres from `Holder` after one Ground
	 * Down step. Issue #1515: 1 when the step slows nothing.
	 */
	float GroundDownImpSpeed(UWorld* World, const FScopedSwinger& Holder)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmImpCharacter* Imp = World->SpawnActor<ACataclysmImpCharacter>(
			ACataclysmImpCharacter::StaticClass(),
			Holder.Actor->GetActorLocation() + FVector(2 * M, 0, 0),
			FRotator::ZeroRotator, Params);
		if (!Imp)
		{
			return -1.0f;
		}
		UCataclysmDebuffs::GroundDownStep(Holder.Actor, 0.25f);
		const float Speed = Imp->SpeedMultiplier();
		Imp->Destroy();
		return Speed;
	}

	/** Ground Down with both of its stats, or with only `Stat`. */
	void ProbeGroundDown(FAutomationTestBase& Test, const TCHAR* Stat, const TCHAR* Other)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Held(World, FVector::ZeroVector);
		GrantFlats(Held.Actor, {{FName(Stat), 15.0f}, {FName(Other), 15.0f}});
		FScopedSwinger Without(World, FVector(0, 100 * M, 0));
		GrantFlats(Without.Actor, {{FName(Other), 15.0f}});

		Test.TestEqual(FString::Printf(TEXT("without %s a creature near the holder "
											"is not slowed"), Stat),
					   GroundDownImpSpeed(World, Without), 1.0f, 0.001f);
		Test.TestTrue(FString::Printf(TEXT("and with it the creature is slowed, so "
										   "GroundDownStep really reads %s"), Stat),
					  GroundDownImpSpeed(World, Held) < 0.999f);
	}

	/** `enemies_near_slowed_within_metres`, read by `UCataclysmDebuffs::GroundDownStep`. */
	void ProbeGroundDownMetres(FAutomationTestBase& Test)
	{
		ProbeGroundDown(Test, UCataclysmDebuffs::GroundDownMetresStat,
						UCataclysmDebuffs::GroundDownPercentStat);
	}

	/** `enemies_near_slowed_percent`, read by the same function. */
	void ProbeGroundDownPercent(FAutomationTestBase& Test)
	{
		ProbeGroundDown(Test, UCataclysmDebuffs::GroundDownPercentStat,
						UCataclysmDebuffs::GroundDownMetresStat);
	}

	/**
	 * `two_handed_weapon_in_each_hand`, read by
	 * `UCataclysmEquipmentComponent::MayHoldTwoTwoHanded`. Issue #1515, Both
	 * Hands Full. A wearer holding it keeps a greatsword in each hand; one
	 * without it keeps only the second put on.
	 */
	void ProbeBothHandsFull(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Plain(World, 0.0f);
		FScopedFighter Held(World, 0.0f);
		GrantFlats(Held.Actor,
				   {{FName(UCataclysmEquipmentComponent::BothHandsFullStat), 1.0f}});

		const auto WeaponsKept = [](const FScopedFighter& Wearer)
		{
			UCataclysmEquipmentComponent* Equipment =
				NewObject<UCataclysmEquipmentComponent>(Wearer.Actor);
			Equipment->RegisterComponent();
			FCataclysmItem Greatsword;
			Greatsword.Base = FName(TEXT("Weapon_Greatsword"));
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			Equipment->EquipInto(Greatsword, ECataclysmGearSlot::Weapon1,
								 Removed, AlsoRemoved);
			Equipment->EquipInto(Greatsword, ECataclysmGearSlot::Weapon2,
								 Removed, AlsoRemoved);
			return Equipment->NumEquipped();
		};
		Test.TestEqual(TEXT("a wearer without two_handed_weapon_in_each_hand keeps "
							"one greatsword"),
					   WeaponsKept(Plain), 1);
		Test.TestEqual(TEXT("and one holding it keeps both, so MayHoldTwoTwoHanded "
							"really reads it"),
					   WeaponsKept(Held), 2);
	}

	/**
	 * `melee_kill_repeats_attack_every_seconds`, read by
	 * `UCataclysmFollowThrough::NoteMeleeKill`. Issue #1515, Follow Through. A
	 * melee kill by a fighter holding it waits to repeat its strike; one by a
	 * fighter without it does not.
	 */
	void ProbeFollowThrough(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const auto WaitsToRepeat = [&Test](FScopedFighter& Fighter)
		{
			UCataclysmAbilitySystemComponent* System = Fighter.AbilitySystem;
			const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
				UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Heavy,
				/*Level=*/100, Fighter.Actor);
			FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
			UCataclysmSkillTemplate* Strike = Spec
				? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance())
				: nullptr;
			if (!Test.TestNotNull(TEXT("a strike"), Strike))
			{
				return false;
			}
			Strike->SkillName = TEXT("Probe Strike");
			return UCataclysmFollowThrough::NoteMeleeKill(
				Fighter.Actor, FName(TEXT("Probe Strike")));
		};

		FScopedFighter Plain(World, 0.0f);
		FScopedFighter Held(World, 0.0f);
		GrantFlats(Held.Actor, {{FName(UCataclysmFollowThrough::EverySecondsStat), 3.0f}});
		Test.TestFalse(TEXT("a melee kill by a fighter without "
							"melee_kill_repeats_attack_every_seconds earns no repeat"),
					   WaitsToRepeat(Plain));
		Test.TestTrue(TEXT("and one by a fighter holding it waits to repeat, so "
						   "NoteMeleeKill really reads it"),
					  WaitsToRepeat(Held));
	}

	/**
	 * `enemies_cannot_move_away_within_metres`, read by
	 * `UCataclysmDebuffs::NowhereToRunStep`. Issue #1515, Nowhere to Run. An
	 * Imp near a holder is held; near a fighter without the stat it is not.
	 */
	void ProbeNowhereToRun(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const auto HoldsAnImp = [&](FScopedFighter& Fighter)
		{
			ACataclysmImpCharacter* Imp = World->SpawnActor<ACataclysmImpCharacter>(
				ACataclysmImpCharacter::StaticClass(),
				Fighter.Actor->GetActorLocation() + FVector(300.0f, 0.0f, 0.0f),
				FRotator::ZeroRotator, Params);
			if (!Test.TestNotNull(TEXT("an Imp"), Imp))
			{
				return false;
			}
			UCataclysmDebuffs::NowhereToRunStep(Fighter.Actor, 0.25f);
			const bool bHeld = Imp->IsHeld();
			Imp->Destroy();
			return bHeld;
		};

		FScopedFighter Plain(World, 0.0f);
		FScopedFighter Holder(World, 0.0f);
		GrantFlats(Holder.Actor, {{FName(UCataclysmDebuffs::NowhereToRunMetresStat), 8.0f}});
		Test.TestFalse(TEXT("an Imp near a fighter without "
							"enemies_cannot_move_away_within_metres is not held"),
					   HoldsAnImp(Plain));
		Test.TestTrue(TEXT("and one near a fighter holding it is, so "
						   "NowhereToRunStep really reads it"),
					  HoldsAnImp(Holder));
	}

	/**
	 * `moving_into_enemy_pushes_aside`, read by `UCataclysmShoulderThrough::Step`.
	 * Issue #1515, Shoulder Through. A walker holding it pushes the enemy it
	 * walks into; one without it does not.
	 */
	void ProbeShoulderThrough(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		// 60 cm apart, inside the 34 + 34 + 10 that touches.
		FScopedSwinger Plain(World, FVector(0, 100 * M, 0));
		FScopedSwinger PlainsEnemy(World, FVector(0.6f * M, 100 * M, 0));
		FScopedSwinger Held(World, FVector::ZeroVector);
		FScopedSwinger HeldsEnemy(World, FVector(0.6f * M, 0, 0));
		GrantFlats(Held.Actor, {{FName(UCataclysmShoulderThrough::Stat), 1.0f}});

		Test.TestNull(TEXT("a walker without moving_into_enemy_pushes_aside pushes "
						   "nothing"),
					  UCataclysmShoulderThrough::Step(Plain.Actor, FVector::ForwardVector));
		Test.TestTrue(TEXT("and one holding it pushes the enemy it walks into, so "
						   "Step really reads it"),
					  UCataclysmShoulderThrough::Step(Held.Actor, FVector::ForwardVector)
						  == HeldsEnemy.Actor);
	}

	/**
	 * `knockback_suppressed`, read by `UCataclysmSkillEffects::ApplyKnockback`.
	 * Issue #1755, Set Stance. A target holding it is not knocked back; one
	 * without it is.
	 */
	void ProbeSetStance(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Shover(World, FVector::ZeroVector);
		FScopedSwinger Plain(World, FVector(2.0f * M, 0, 0));
		FScopedSwinger Held(World, FVector(0, 2.0f * M, 0));
		GrantFlats(Held.Actor,
				   {{FName(UCataclysmSkillEffects::KnockbackSuppressedStat), 1.0f}});

		Test.TestTrue(TEXT("a target without knockback_suppressed is knocked back"),
					  UCataclysmSkillEffects::ApplyKnockback(Shover.Actor, Plain.Actor,
															 150.0f));
		Test.TestFalse(TEXT("and one holding it is not, so ApplyKnockback really "
							"reads it"),
					   UCataclysmSkillEffects::ApplyKnockback(Shover.Actor, Held.Actor,
															  150.0f));
	}

	/**
	 * `minions_repeat_your_skills`, read by `UCataclysmChorus::Repeat`. Issue
	 * #1515, Chorus. A caster holding it with an imp has the imp repeat a skill's
	 * hit; one without it, with an imp too, does not.
	 */
	void ProbeChorus(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Target(World, 0.0f);
		const auto RepeatsFor = [&Test, World, &Target](FScopedFighter& Caster)
		{
			if (!SummonImp(Test, World, Caster.Actor))
			{
				return -1;
			}
			const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
				UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Heavy,
				/*Level=*/100, Caster.Actor);
			FGameplayAbilitySpec* Spec = Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle);
			const UGameplayAbility* Strike = Spec ? Spec->GetPrimaryInstance() : nullptr;
			if (!Test.TestNotNull(TEXT("a strike"), Strike))
			{
				return -1;
			}
			return UCataclysmChorus::Repeat(Caster.Actor, Target.Actor, 1000.0f, Strike);
		};

		FScopedFighter Plain(World, 0.0f);
		FScopedFighter Held(World, 0.0f);
		GrantFlats(Held.Actor, {{FName(UCataclysmChorus::Stat), 1.0f}});
		Test.TestEqual(TEXT("an imp of a caster without minions_repeat_your_skills "
							"repeats nothing"),
					   RepeatsFor(Plain), 0);
		Test.TestEqual(TEXT("and one of a caster holding it repeats the hit, so "
							"Repeat really reads it"),
					   RepeatsFor(Held), 1);
	}

	/**
	 * `minion_held_longest_becomes_your_equal`, read by
	 * `UCataclysmSecondSelf::Step`. Issue #1515, A Second Self. A summoner
	 * holding it has its imp chosen; one without it, with an imp too, does not.
	 */
	void ProbeSecondSelf(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Plain(World, 0.0f);
		FScopedFighter Held(World, 0.0f);
		GrantFlats(Held.Actor, {{FName(UCataclysmSecondSelf::Stat), 1.0f}});
		if (!SummonImp(Test, World, Plain.Actor) || !SummonImp(Test, World, Held.Actor))
		{
			return;
		}
		Test.TestNull(TEXT("a summoner without minion_held_longest_becomes_your_equal "
						   "chooses no Second Self"),
					  UCataclysmSecondSelf::Step(Plain.Actor));
		Test.TestNotNull(TEXT("and one holding it chooses its imp, so Step really "
							  "reads it"),
						 UCataclysmSecondSelf::Step(Held.Actor));
	}

	/**
	 * `minion_energy_shield_percent_of_yours`, read by
	 * `UCataclysmRegeneration::SharedBloodStep`. Issue #1515, Shared Blood. An
	 * imp summoned by a summoner holding it has a shield; one summoned by a
	 * summoner without it has none.
	 */
	void ProbeSharedBlood(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Plain(World, 0.0f);
		FScopedFighter Held(World, 0.0f);
		for (const FScopedFighter* Summoner : {&Plain, &Held})
		{
			Summoner->AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 500.0f);
		}
		GrantFlats(Held.Actor, {{FName(UCataclysmRegeneration::SharedBloodStat), 20.0f}});

		const auto ShieldOfImpFrom = [&Test, World](const FScopedFighter& Summoner)
		{
			ACataclysmMinion* Imp = SummonImp(Test, World, Summoner.Actor);
			if (!Imp)
			{
				return -1.0f;
			}
			const float Maximum = UCataclysmTargeting::AbilitySystemOf(Imp)->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute());
			Imp->Destroy();
			return Maximum;
		};
		Test.TestEqual(TEXT("an imp of a summoner without "
							"minion_energy_shield_percent_of_yours has no shield"),
					   ShieldOfImpFrom(Plain), 0.0f);
		Test.TestTrue(TEXT("and one of a summoner holding it has one, so "
						   "SharedBloodStep really reads it"),
					  ShieldOfImpFrom(Held) > 0.0f);
	}

	/**
	 * `mitigated_damage_added_to_next_melee_cap_percent`, read by
	 * `UCataclysmAbilitySystemComponent::NoteMitigatedDamage`. Issue #1515,
	 * Nothing Wasted. Two defenders with 1,000 armour take the same blow; only
	 * the one holding the stat stores what the armour removed.
	 */
	void ProbeMitigatedAdded(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Attacker(World, 1000.0f);
		FScopedFighter Plain(World, 0.0f);
		FScopedFighter Held(World, 0.0f);
		for (const FScopedFighter* Defender : {&Plain, &Held})
		{
			Defender->AbilitySystem->SetNumericAttributeBase(
				Combat::GetArmorAttribute(), 1000.0f);
		}
		GrantFlats(Held.Actor,
			{{FName(UCataclysmAbilitySystemComponent::MitigatedAddedCapStat), 100.0f}});

		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Plain.Actor, 100.0f,
										 FGameplayTagContainer());
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Held.Actor, 100.0f,
										 FGameplayTagContainer());

		Test.TestEqual(TEXT("a defender without "
							"mitigated_damage_added_to_next_melee_cap_percent stores nothing"),
					   Plain.AbilitySystem->StoredMitigatedDamageNow(), 0.0f);
		Test.TestTrue(TEXT("and one holding it stores what its armour removed, so "
						   "NoteMitigatedDamage really reads it"),
					  Held.AbilitySystem->StoredMitigatedDamageNow() > 0.0f);
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

	/**
	 * `skill_charges_bonus` is read by
	 * `UCataclysmAbilitySystemComponent::SkillChargesMaximum`. Issue #1833,
	 * skill charges. Two characters, one carrying a flat row of 2: a skill
	 * holds one use on the first and three on the second.
	 */
	void ProbeSkillCharges(FAutomationTestBase& Test)
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
			System->InitAbilityActorInfo(Actor, Actor);
			return System;
		};
		UCataclysmAbilitySystemComponent* Plain = Make();
		UCataclysmAbilitySystemComponent* Charged = Make();

		FCataclysmStatModifier Flat;
		Flat.Bucket = ECataclysmStatBucket::Flat;
		Flat.Source = ECataclysmModifierSource::Enchantment;
		Flat.Value = 2.0f;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(
			FName(UCataclysmAbilitySystemComponent::SkillChargesBonusStat));
		Line.Base = 0.0f;
		Line.Modifiers = {Flat};
		Charged->SetStatInputs(MoveTemp(Inputs));

		Test.TestEqual(TEXT("a character with no row holds one use"),
			Plain->SkillChargesMaximum(FGameplayTagContainer()), 1);
		Test.TestEqual(TEXT("and one carrying a row of 2 holds three"),
			Charged->SkillChargesMaximum(FGameplayTagContainer()), 3);
	}

	/**
	 * `resistance_cap` is read by `UCataclysmDamageCalculation::ResistanceCapOf`.
	 * Issue #1833, the kill counter. Two characters, one carrying a flat row of
	 * 10: the first's cap is 70 and the second's 80.
	 */
	void ProbeResistanceCap(FAutomationTestBase& Test)
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
			System->InitAbilityActorInfo(Actor, Actor);
			return System;
		};
		UCataclysmAbilitySystemComponent* Plain = Make();
		UCataclysmAbilitySystemComponent* Raised = Make();

		FCataclysmStatModifier Flat;
		Flat.Bucket = ECataclysmStatBucket::Flat;
		Flat.Source = ECataclysmModifierSource::Enchantment;
		Flat.Value = 10.0f;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(
			FName(UCataclysmDamageCalculation::ResistanceCapStat));
		Line.Base = 0.0f;
		Line.Modifiers = {Flat};
		Raised->SetStatInputs(MoveTemp(Inputs));

		Test.TestEqual(TEXT("a character with no row is held to 70"),
			UCataclysmDamageCalculation::ResistanceCapOf(Plain), 70.0f, 0.001f);
		Test.TestEqual(TEXT("and one carrying a row of 10 to 80"),
			UCataclysmDamageCalculation::ResistanceCapOf(Raised), 80.0f, 0.001f);
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
	/**
	 * `health_reserved`, scaled by minions held, asked by
	 * `UCataclysmAbilitySystemComponent::HealthReserved`, which every heal's
	 * ceiling and the regeneration step's hold read. Issue #1833: "Each minion
	 * reserves 100-500 hp" is a flat 100-500 scaled by `minions_held`.
	 *
	 * A FLAT ROW RATHER THAN `ScaledBy`'S INCREASE, because a reservation has no
	 * base for an increase to act on: the row states points.
	 */
	void ProbeScaledHealthReserved(FAutomationTestBase& Test)
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
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);

		FCataclysmStatModifier Points;
		Points.Bucket = ECataclysmStatBucket::Flat;
		Points.Source = ECataclysmModifierSource::Enchantment;
		Points.Value = 100.0f;
		Points.Scale = ECataclysmStatScale::PerMinionHeld;
		Points.ScaleStep = 1.0f;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(
			FName(UCataclysmAbilitySystemComponent::HealthReservedStat));
		Line.Base = 0.0f;
		Line.Modifiers = {Points};
		System->SetStatInputs(MoveTemp(Inputs));

		const float Alone = System->UnreservedMaximumHealth();
		ACataclysmMinion* Imp = SummonImp(Test, World, Summoner.Actor);
		if (!Imp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };
		const float Holding = System->UnreservedMaximumHealth();

		Test.TestTrue(
			FString::Printf(TEXT("health_reserved is asked for, so a minion lowers "
								 "the health that may be held: %.2f against %.2f"),
							Holding, Alone),
			Holding < Alone - 0.001f);
	}

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
	 * What one blink that leaves ground gave, for the four stats a persistent area reads. Ruled 2026-10-06.
	 *
	 * A CASTER AT THE ORIGIN WITH AN ENEMY 2 M TO EACH SIDE blinks 8 m. A blink leaves ground where it began and
	 * where it arrived, so two zones; the one where it began covers both enemies, and is swept once here.
	 */
	struct FZoneReading
	{
		bool bMade = false;
		int32 LiveZones = 0;
		float LastsSeconds = -1.0f;
		float TakenByOneEnemy = -1.0f;
		bool bEnemyIsSlowed = false;
		float SlowStatedOnTheEnemy = -1.0f;
		float CreatureSpeedBefore = -1.0f;
		float CreatureSpeedAfter = -1.0f;
		float TakenByTheCreature = -1.0f;
		bool bCreatureCarriesTheSlow = false;
		int32 FoundByTheSweep = -1;
		FName ZonesOwnAilment;
		bool bBurningAfterTheSweep = false;
		int32 BurnsOnTheEnemyAfterTwoSweeps = -1;
		bool bStaggeredByTheFirstSweep = false;
		bool bStaggeredAgainWhileStaying = false;
		bool bStaggeredOnComingBack = false;
		int32 LiveZonesAfterASecondBlink = 0;
	};

	FZoneReading ReadABlinksZones(TFunctionRef<void(TMap<FName, FCataclysmStatInputs>&)> Carry,
								  bool bWithACreature = false, const TCHAR* Element = TEXT("Element.Demonic"))
	{
		FZoneReading Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!World)
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		// NO CRITICAL STRIKES, so two sweeps can be compared. Pinned here by hand: `FPinnedRoll` is declared further down.
		IConsoleVariable* CritRoll = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.CritRoll"));
		const float CritRollWas = CritRoll ? CritRoll->GetFloat() : -1.0f;
		if (CritRoll)
		{
			CritRoll->Set(100.0f, ECVF_SetByConsole);
		}
		ON_SCOPE_EXIT
		{
			if (CritRoll)
			{
				CritRoll->Set(CritRollWas, ECVF_SetByConsole);
			}
		};

		FScopedSwinger Caster(World, FVector::ZeroVector);
		FScopedSwinger Left(World, FVector(0.0f, 200.0f, 0.0f));
		FScopedSwinger Right(World, FVector(0.0f, -200.0f, 0.0f));
		// AND A REAL CREATURE 1.5 M BEHIND THE CASTER, when the caller reads a speed: only a creature has one. It is
		// a third body inside the zone, so the probe that counts enemies does not ask for it.
		ACataclysmEnemyCharacter* Creature = bWithACreature
			? World->SpawnActor<ACataclysmEnemyCharacter>(FVector(-150.0f, 0.0f, 0.0f), FRotator::ZeroRotator)
			: nullptr;
		if (Creature)
		{
			// ON THE MONSTERS' SIDE AND WITH HEALTH, as `SpawnEnemy` in CataclysmApplyStatusTests.cpp and
			// `SpawnEnemyAt` in CataclysmBasicAttackTests.cpp make one. The first version of this probe did neither,
			// and a creature spawned with no health set is counted dead, so no sweep found it.
			Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Creature->SetHealth(1000.0f);
		}
		TMap<FName, FCataclysmStatInputs> Inputs;
		Carry(Inputs);
		if (Inputs.Num() > 0)
		{
			Caster.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		const auto Blink = [&Caster, Element](ECataclysmAbilitySlot Slot) -> bool
		{
			const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
				UCataclysmMovementSkill::StaticClass(), Slot, /*Level=*/100, Caster.Actor);
			FGameplayAbilitySpec* Spec = Handle.IsValid()
				? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmMovementSkill* Slip = Spec ? Cast<UCataclysmMovementSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (!Slip)
			{
				return false;
			}
			Slip->SkillName = TEXT("A blink leaving ground");
			Slip->Params = UCataclysmSkillShapes::ParseParams(
				TEXT("Mode=Blink; Range=8; Radius=3.5; GroundRadius=3.5; GroundDuration=6; GroundPercent=16.7"));
			Slip->SkillTags = UCataclysmSkillShapes::TagsFromCell(
				*FString::Printf(TEXT("Item.Weapon.Wand, %s, Type.AOE.Persistent"), Element));
			return Caster.AbilitySystem->TryActivateAbility(Handle);
		};
		const auto LiveZones = [World, &Caster](ACataclysmGroundZone** OutAtTheOrigin = nullptr) -> int32
		{
			int32 Count = 0;
			for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
			{
				if (IsValid(*It) && It->GetOwner() == Caster.Actor && It->DamagePerTick > 0.0f)
				{
					++Count;
					if (OutAtTheOrigin && It->GetActorLocation().Size2D() < 100.0f)
					{
						*OutAtTheOrigin = *It;
					}
				}
			}
			return Count;
		};

		if (!Blink(ECataclysmAbilitySlot::Movement))
		{
			return Read;
		}
		Read.bMade = true;
		ACataclysmGroundZone* AtTheOrigin = nullptr;
		Read.LiveZones = LiveZones(&AtTheOrigin);
		if (AtTheOrigin)
		{
			Read.LastsSeconds = AtTheOrigin->GetLifeSpan();
			const UAbilitySystemComponent* LeftSystem = UCataclysmTargeting::AbilitySystemOf(Left.Actor);
			const float Before = LeftSystem->GetNumericAttribute(Vital::GetHealthAttribute());
			const UAbilitySystemComponent* CreatureSystem =
				Creature ? UCataclysmTargeting::AbilitySystemOf(Creature) : nullptr;
			const float CreatureHealthBefore =
				CreatureSystem ? CreatureSystem->GetNumericAttribute(Vital::GetHealthAttribute()) : 0.0f;
			if (Creature)
			{
				Read.CreatureSpeedBefore = Creature->CrippleMultiplier();
			}
			AtTheOrigin->Sweep();
			Read.FoundByTheSweep = AtTheOrigin->LastSweepCount;
			if (Creature)
			{
				Read.CreatureSpeedAfter = Creature->CrippleMultiplier();
			}
			if (CreatureSystem)
			{
				// WHETHER THE SWEEP REACHED THE CREATURE AT ALL, so a speed that did not change names its cause.
				Read.TakenByTheCreature =
					CreatureHealthBefore - CreatureSystem->GetNumericAttribute(Vital::GetHealthAttribute());
				Read.bCreatureCarriesTheSlow = CreatureSystem->HasMatchingGameplayTag(UCataclysmDebuffs::CrippleTag());
			}
			Read.TakenByOneEnemy = Before - LeftSystem->GetNumericAttribute(Vital::GetHealthAttribute());
			Read.bEnemyIsSlowed = LeftSystem->HasMatchingGameplayTag(UCataclysmDebuffs::CrippleTag());
			Read.SlowStatedOnTheEnemy =
				UCataclysmSkillEffects::StatedStrengthOn(Left.Actor, UCataclysmDebuffs::CrippleTag());

			// THE ZONE'S OWN AILMENT, AND THE STAGGER ON ENTRY. Ruled 2026-10-06. The stagger is taken off by hand
			// between sweeps, since nothing here moves time: an enemy that stays is not staggered again, and one that
			// leaves for a sweep and comes back is.
			Read.ZonesOwnAilment = AtTheOrigin->OwnAilment;
			Read.bBurningAfterTheSweep = UCataclysmSkillEffects::HasTag(Left.Actor, UCataclysmSkillEffects::BurnTag());
			const FGameplayTag Staggered = UCataclysmSkillEffects::StaggeredTag();
			UAbilitySystemComponent* LeftMutable = UCataclysmTargeting::AbilitySystemOf(Left.Actor);
			const auto ClearTheStagger = [LeftMutable, &Staggered]()
			{
				LeftMutable->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(Staggered));
			};
			Read.bStaggeredByTheFirstSweep = UCataclysmSkillEffects::IsStaggered(Left.Actor);
			ClearTheStagger();
			AtTheOrigin->Sweep();
			Read.bStaggeredAgainWhileStaying = UCataclysmSkillEffects::IsStaggered(Left.Actor);
			// AND HOW MANY BURNS THE ENEMY CARRIES AFTER TWO SWEEPS: the tag is held once for each running burn.
			Read.BurnsOnTheEnemyAfterTwoSweeps = LeftMutable->GetTagCount(UCataclysmSkillEffects::BurnTag());
			ClearTheStagger();
			const FVector Stood = Left.Actor->GetActorLocation();
			Left.Actor->SetActorLocation(Stood + FVector(0.0f, 5000.0f, 0.0f));
			AtTheOrigin->Sweep();
			Left.Actor->SetActorLocation(Stood);
			AtTheOrigin->Sweep();
			Read.bStaggeredOnComingBack = UCataclysmSkillEffects::IsStaggered(Left.Actor);
		}
		Read.LiveZonesAfterASecondBlink = Blink(ECataclysmAbilitySlot::Special) ? LiveZones() : -1;
		return Read;
	}

	/** One flat line of one of the zone stats, on a base of nothing. */
	void CarryFlat(TMap<FName, FCataclysmStatInputs>& Inputs, const TCHAR* Stat, float Value)
	{
		Inputs.FindOrAdd(FName(Stat)).Base = Value;
	}

	/**
	 * `persistent_area_duration` is read by `UCataclysmSkillTemplate::LeaveGroundAlong` (and `LeaveTerrainAlong`)
	 * where the area is left. A caster carrying it at its base of 100 with 50% less leaves a zone that lasts half
	 * as long as a plain caster's.
	 */
	void ProbePersistentAreaDuration(FAutomationTestBase& Test)
	{
		const FZoneReading Plain = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>&) {});
		const FZoneReading Cut = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>& Inputs)
		{
			FCataclysmStatModifier Half;
			Half.Bucket = ECataclysmStatBucket::More;
			Half.Source = ECataclysmModifierSource::Enchantment;
			Half.Value = -50.0f;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::PersistentAreaDurationStat));
			Line.Base = UCataclysmDamageCalculation::NormalPersistentAreaDuration;
			Line.Modifiers = {Half};
		});
		if (!Test.TestTrue(TEXT("both casters left a zone that lasts"), Plain.LastsSeconds > 0.0f && Cut.LastsSeconds > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("the plain caster's zone lasts its stated 6 seconds"), Plain.LastsSeconds, 6.0f, 0.01f);
		Test.TestEqual(TEXT("and the carrying caster's lasts half of that"), Cut.LastsSeconds, 3.0f, 0.01f);
	}

	/**
	 * `zone_damage_per_enemy_inside` is read where the zone is priced and applied by `ACataclysmGroundZone::Sweep`.
	 * With two enemies inside, a zone whose owner carries 20 of it deals each of them 1.4 times a plain zone's sweep.
	 */
	void ProbeZoneDamagePerEnemyInside(FAutomationTestBase& Test)
	{
		const FZoneReading Plain = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>&) {});
		const FZoneReading More = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>& Inputs)
		{
			CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneDamagePerEnemyInsideStat, 20.0f);
		});
		if (!Test.TestTrue(TEXT("both zones' sweeps hurt the enemy"), Plain.TakenByOneEnemy > 0.0f && More.TakenByOneEnemy > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("with two enemies inside, 20 per enemy makes a sweep 1.4 times a plain one"),
			More.TakenByOneEnemy / Plain.TakenByOneEnemy, 1.4f, 0.01f);
	}

	/**
	 * `zone_slow_percent` is read where the zone is left and laid by `ACataclysmGroundZone::Sweep` as the Cripple
	 * debuff. An enemy swept by a carrying caster's zone is slowed; one swept by a plain caster's is not.
	 *
	 * AT THE SIZE THE STAT STATES, AND 20 IS USED BECAUSE THE CRIPPLE ROW'S OWN FIGURE IS 30. The first version of
	 * this probe gave the stat 30 and asked only for the tag, and so passed against a slow that dropped its size.
	 * A cripple that states nothing reads -1 and slows by the row's 30; one stating 20 slows by 20, which is a
	 * speed of 0.8.
	 */
	void ProbeZoneSlowPercent(FAutomationTestBase& Test)
	{
		const FZoneReading Plain = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>&) {}, /*bWithACreature=*/true);
		const FZoneReading Slowing = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>& Inputs)
		{
			CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneSlowPercentStat, 20.0f);
		}, /*bWithACreature=*/true);
		if (!Test.TestTrue(TEXT("both zones' sweeps reached the enemy"),
						   Plain.TakenByOneEnemy > 0.0f && Slowing.TakenByOneEnemy > 0.0f))
		{
			return;
		}
		Test.TestFalse(TEXT("a plain zone slows nobody"), Plain.bEnemyIsSlowed);
		Test.TestTrue(TEXT("and a carrying caster's zone slows the enemy inside"), Slowing.bEnemyIsSlowed);
		Test.TestEqual(TEXT("by the 20 per cent the stat states, and not the Cripple row's 30"),
			Slowing.SlowStatedOnTheEnemy, 20.0f, 0.01f);

		// AND THE RESULT, NOT ONLY THE STATEMENT: a real creature's own speed multiplier, which is what its walk and
		// its attacks are multiplied by. A stated strength nothing read would pass the line above.
		if (!Test.TestEqual(TEXT("a creature in either zone is at its whole speed before the sweep"),
							Plain.CreatureSpeedBefore + Slowing.CreatureSpeedBefore, 2.0f, 0.001f))
		{
			return;
		}
		// THE SWEEP HAS TO HAVE REACHED THE CREATURE, or the speeds below say nothing about a slow.
		if (!Test.TestEqual(TEXT("set-up: each sweep found three bodies, the two plain ones and the creature"),
							Plain.FoundByTheSweep + Slowing.FoundByTheSweep, 6)
			|| !Test.TestTrue(TEXT("set-up: each sweep hurt the creature"),
							  Plain.TakenByTheCreature > 0.0f && Slowing.TakenByTheCreature > 0.0f))
		{
			return;
		}
		Test.TestTrue(TEXT("the creature swept by the carrying caster's zone carries the Cripple tag"),
			Slowing.bCreatureCarriesTheSlow);
		Test.TestEqual(TEXT("a plain zone's sweep leaves the creature at its whole speed"),
			Plain.CreatureSpeedAfter, 1.0f, 0.001f);
		Test.TestEqual(TEXT("and a carrying caster's zone leaves it at 0.8 of its speed"),
			Slowing.CreatureSpeedAfter, 0.8f, 0.001f);
	}

	/**
	 * `zone_staggers_on_entry` is read where the zone is left and acted on by `ACataclysmGroundZone::Sweep`. An enemy
	 * standing in a carrying caster's zone is staggered by the first sweep, not again while it stays, and again when
	 * it has left for a sweep and come back. A plain zone staggers nobody.
	 */
	void ProbeZoneStaggersOnEntry(FAutomationTestBase& Test)
	{
		const FZoneReading Plain = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>&) {});
		const FZoneReading Staggering = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>& Inputs)
		{
			CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneStaggersOnEntryStat, 1.0f);
		});
		if (!Test.TestTrue(TEXT("both zones' sweeps reached the enemy"),
						   Plain.TakenByOneEnemy > 0.0f && Staggering.TakenByOneEnemy > 0.0f))
		{
			return;
		}
		Test.TestFalse(TEXT("a plain zone staggers nobody"),
			Plain.bStaggeredByTheFirstSweep || Plain.bStaggeredOnComingBack);
		if (!Test.TestTrue(TEXT("a carrying caster's zone staggers the enemy its first sweep finds"),
						   Staggering.bStaggeredByTheFirstSweep))
		{
			return;
		}
		Test.TestFalse(TEXT("and not again while it stays inside"), Staggering.bStaggeredAgainWhileStaying);
		Test.TestTrue(TEXT("and again when it has left for a sweep and come back"), Staggering.bStaggeredOnComingBack);
	}

	/**
	 * `zone_applies_own_ailment` is read where the zone is left, and the zone then lays the ailment of its skill's
	 * damage type each sweep. A Demonic skill's zone burns. A zone of a type with no ailment of its own lays none.
	 */
	void ProbeZoneAppliesOwnAilment(FAutomationTestBase& Test)
	{
		// THE ONE PLACE THE MAPPING IS STATED.
		Test.TestEqual(TEXT("Demonic's ailment is Burn"), UCataclysmAilments::AilmentOfDamageType(FName(TEXT("Demonic"))),
			FName(TEXT("Burn")));
		Test.TestEqual(TEXT("War's ailment is Bleed"), UCataclysmAilments::AilmentOfDamageType(FName(TEXT("War"))),
			FName(TEXT("Bleed")));
		Test.TestTrue(TEXT("no other type has one"),
			UCataclysmAilments::AilmentOfDamageType(FName(TEXT("Chaos"))).IsNone()
				&& UCataclysmAilments::AilmentOfDamageType(NAME_None).IsNone());

		const auto Carrying = [](TMap<FName, FCataclysmStatInputs>& Inputs)
		{
			CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAppliesOwnAilmentStat, 1.0f);
		};
		const FZoneReading Plain = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>&) {});
		const FZoneReading Demonic = ReadABlinksZones(Carrying);
		const FZoneReading Chaos = ReadABlinksZones(Carrying, /*bWithACreature=*/false, TEXT("Element.Chaos"));
		if (!Test.TestTrue(TEXT("all three zones' sweeps reached the enemy"),
						   Plain.TakenByOneEnemy > 0.0f && Demonic.TakenByOneEnemy > 0.0f && Chaos.TakenByOneEnemy > 0.0f))
		{
			return;
		}
		Test.TestFalse(TEXT("a plain Demonic zone's sweep sets nobody alight"), Plain.bBurningAfterTheSweep);
		Test.TestEqual(TEXT("a carrying caster's Demonic zone holds Burn"), Demonic.ZonesOwnAilment, FName(TEXT("Burn")));
		Test.TestTrue(TEXT("and its sweep sets the enemy alight"), Demonic.bBurningAfterTheSweep);
		Test.TestEqual(TEXT("and a second sweep leaves it carrying one burn, not two"),
			Demonic.BurnsOnTheEnemyAfterTwoSweeps, 1);
		Test.TestTrue(TEXT("a carrying caster's Chaos zone holds no ailment"), Chaos.ZonesOwnAilment.IsNone());
		Test.TestFalse(TEXT("and its sweep sets nobody alight"), Chaos.bBurningAfterTheSweep);
	}

	/**
	 * `only_one_persistent_area` is read where an area is left. A blink leaves two zones; a caster carrying the
	 * flag is left with one after it, and with one after a second blink.
	 */
	void ProbeOnlyOnePersistentArea(FAutomationTestBase& Test)
	{
		const FZoneReading Plain = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>&) {});
		const FZoneReading OnlyOne = ReadABlinksZones([](TMap<FName, FCataclysmStatInputs>& Inputs)
		{
			CarryFlat(Inputs, UCataclysmDamageCalculation::OnlyOnePersistentAreaStat, 1.0f);
		});
		if (!Test.TestTrue(TEXT("both casters blinked"), Plain.bMade && OnlyOne.bMade))
		{
			return;
		}
		Test.TestEqual(TEXT("a plain caster's blink leaves two zones"), Plain.LiveZones, 2);
		Test.TestEqual(TEXT("and four after a second blink"), Plain.LiveZonesAfterASecondBlink, 4);
		Test.TestEqual(TEXT("a carrying caster is left with one"), OnlyOne.LiveZones, 1);
		Test.TestEqual(TEXT("and with one after a second blink"), OnlyOne.LiveZonesAfterASecondBlink, 1);
	}

	/** One zone a use of a skill left its user: where, how wide, for how long, what a sweep deals and as what type. */
	struct FLeftZone
	{
		FVector At = FVector::ZeroVector;
		float RadiusCm = 0.0f;
		float LastsSeconds = 0.0f;
		float PerSweep = 0.0f;
		FName DamageType;
		float FollowsAtPercent = 0.0f;
	};

	/**
	 * The zones one use of one skill left its user, nearest the origin first. For the two stats by which a row
	 * gives a zone to a skill that states no ground. Ruled 2026-10-06.
	 *
	 * A USER AT THE ORIGIN AND ONE ENEMY 6 M TO ITS SIDE. The enemy is what a curse is laid on. It is outside
	 * every radius the callers state, so nothing else here strikes it.
	 *
	 * `bOutUsed` SAYS THE SKILL RAN, because an empty answer is also what a skill that left nothing gives.
	 */
	TArray<FLeftZone> ZonesLeftByOneUse(TSubclassOf<UCataclysmSkillTemplate> Class, const TCHAR* ParamsCell,
										const TCHAR* TagsCell,
										TFunctionRef<void(TMap<FName, FCataclysmStatInputs>&)> Carry, bool& bOutUsed)
	{
		TArray<FLeftZone> Left;
		bOutUsed = false;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!World)
		{
			return Left;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger User(World, FVector::ZeroVector);
		FScopedSwinger Enemy(World, FVector(0.0f, 600.0f, 0.0f));
		TMap<FName, FCataclysmStatInputs> Inputs;
		Carry(Inputs);
		if (Inputs.Num() > 0)
		{
			User.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		const FGameplayAbilitySpecHandle Handle = User.AbilitySystem->GiveAbilityInSlot(
			Class, ECataclysmAbilitySlot::Movement, /*Level=*/100, User.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? User.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmSkillTemplate* Skill = Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			return Left;
		}
		Skill->SkillName = TEXT("A skill a row may give a zone");
		Skill->Params = UCataclysmSkillShapes::ParseParams(ParamsCell);
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagsCell);
		bOutUsed = User.AbilitySystem->TryActivateAbility(Handle);

		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It) && It->GetOwner() == User.Actor)
			{
				FLeftZone One;
				One.At = It->GetActorLocation();
				One.RadiusCm = It->RadiusCm;
				One.LastsSeconds = It->GetLifeSpan();
				One.PerSweep = It->DamagePerTick;
				One.DamageType = It->DamageType;
				One.FollowsAtPercent = It->FollowsItsOwnerAtPercent;
				Left.Add(One);
			}
		}
		Left.Sort([](const FLeftZone& A, const FLeftZone& B) { return A.At.SizeSquared2D() < B.At.SizeSquared2D(); });
		return Left;
	}

	/**
	 * `zone_at_start_and_end_seconds` is read by a Movement skill, where it began and where it arrived. A blink
	 * that states no ground leaves none; the same blink by a caster carrying 4 leaves two zones lasting 4 seconds.
	 */
	void ProbeZoneAtStartAndEndSeconds(FAutomationTestBase& Test)
	{
		const TCHAR* Blink = TEXT("Mode=Blink; Range=8; Radius=3.5");
		const TCHAR* Tags = TEXT("Item.Weapon.Wand, Element.Demonic, Slot.Movement");
		bool bPlainUsed = false;
		bool bCarryingUsed = false;
		const TArray<FLeftZone> Plain = ZonesLeftByOneUse(UCataclysmMovementSkill::StaticClass(), Blink, Tags,
			[](TMap<FName, FCataclysmStatInputs>&) {}, bPlainUsed);
		const TArray<FLeftZone> Carrying = ZonesLeftByOneUse(UCataclysmMovementSkill::StaticClass(), Blink, Tags,
			[](TMap<FName, FCataclysmStatInputs>& Inputs)
			{
				CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAtStartAndEndSecondsStat, 4.0f);
			}, bCarryingUsed);
		if (!Test.TestTrue(TEXT("both casters blinked"), bPlainUsed && bCarryingUsed))
		{
			return;
		}
		Test.TestEqual(TEXT("a blink that states no ground leaves no zone"), Plain.Num(), 0);
		if (!Test.TestEqual(TEXT("a carrying caster's blink leaves two"), Carrying.Num(), 2))
		{
			return;
		}
		Test.TestTrue(TEXT("one where it began"), Carrying[0].At.Size2D() < 100.0f);
		Test.TestTrue(TEXT("and one where it arrived"), Carrying[1].At.Size2D() > 400.0f);
		Test.TestEqual(TEXT("the first lasting the 4 seconds carried"), Carrying[0].LastsSeconds, 4.0f, 0.01f);
		Test.TestEqual(TEXT("and the second too"), Carrying[1].LastsSeconds, 4.0f, 0.01f);
	}

	/**
	 * `zone_at_impact_seconds` is read where a skill's blow lands. A charge that states no ground leaves none; the
	 * same charge by a user carrying 3 leaves one zone, where it arrived, lasting 3 seconds.
	 */
	void ProbeZoneAtImpactSeconds(FAutomationTestBase& Test)
	{
		const TCHAR* Charge = TEXT("Mode=Charge; Range=8; Radius=1.5");
		const TCHAR* Tags = TEXT("Item.Weapon.Sword, Element.Demonic, Keyword.Charge, Slot.Movement");
		bool bPlainUsed = false;
		bool bCarryingUsed = false;
		const TArray<FLeftZone> Plain = ZonesLeftByOneUse(UCataclysmMovementSkill::StaticClass(), Charge, Tags,
			[](TMap<FName, FCataclysmStatInputs>&) {}, bPlainUsed);
		const TArray<FLeftZone> Carrying = ZonesLeftByOneUse(UCataclysmMovementSkill::StaticClass(), Charge, Tags,
			[](TMap<FName, FCataclysmStatInputs>& Inputs)
			{
				CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAtImpactSecondsStat, 3.0f);
			}, bCarryingUsed);
		if (!Test.TestTrue(TEXT("both users charged"), bPlainUsed && bCarryingUsed))
		{
			return;
		}
		Test.TestEqual(TEXT("a charge that states no ground leaves no zone"), Plain.Num(), 0);
		if (!Test.TestEqual(TEXT("a carrying user's charge leaves one"), Carrying.Num(), 1))
		{
			return;
		}
		Test.TestTrue(TEXT("where it arrived"), Carrying[0].At.Size2D() > 400.0f);
		Test.TestEqual(TEXT("lasting the 3 seconds carried"), Carrying[0].LastsSeconds, 3.0f, 0.01f);
	}

	/**
	 * What one sweep of a zone did to the character who left it and stands in it, for the two flags by which a zone
	 * reaches its owner. The owner's decision of 2026-10-06.
	 *
	 * A STRIKE THAT LEAVES GROUND UNDER ITS USER, 4 m in radius, carrying the skill's curse. The user wears the three
	 * zone rows as well -- a slow of 20, the stagger on entry and the zone's own ailment -- so the zone has every
	 * effect a zone can have. One enemy stands 2 m away, inside it, as the control: it is reached whatever is worn.
	 */
	struct FOwnerReading
	{
		bool bMade = false;
		bool bTheZoneCarriesACurse = false;
		float PerSweep = -1.0f;
		float TakenByTheOwner = -1.0f;
		float TakenByTheEnemy = -1.0f;
		int32 EnemiesFoundByTheSweep = -1;
		bool bOwnerIsSlowed = false;
		bool bOwnerIsStaggered = false;
		bool bOwnerIsBurning = false;
		bool bOwnerIsCursed = false;
		bool bOwnerStaggeredAgainWhileStaying = false;
	};

	FOwnerReading ReadAZoneUnderItsOwner(TFunctionRef<void(TMap<FName, FCataclysmStatInputs>&)> Carry)
	{
		FOwnerReading Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!World)
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		// NO CRITICAL STRIKES, so a sweep deals its stated figure. Pinned by hand: `FPinnedRoll` is declared further down.
		IConsoleVariable* CritRoll = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.CritRoll"));
		const float CritRollWas = CritRoll ? CritRoll->GetFloat() : -1.0f;
		if (CritRoll)
		{
			CritRoll->Set(100.0f, ECVF_SetByConsole);
		}
		ON_SCOPE_EXIT
		{
			if (CritRoll)
			{
				CritRoll->Set(CritRollWas, ECVF_SetByConsole);
			}
		};

		FScopedSwinger User(World, FVector::ZeroVector);
		FScopedSwinger Enemy(World, FVector(200.0f, 0.0f, 0.0f));
		TMap<FName, FCataclysmStatInputs> Inputs;
		CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneSlowPercentStat, 20.0f);
		CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneStaggersOnEntryStat, 1.0f);
		CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAppliesOwnAilmentStat, 1.0f);
		Carry(Inputs);
		User.AbilitySystem->SetStatInputs(MoveTemp(Inputs));

		const FGameplayAbilitySpecHandle Handle = User.AbilitySystem->GiveAbilityInSlot(
			UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Movement, /*Level=*/100, User.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? User.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmSkillTemplate* Skill = Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			return Read;
		}
		Skill->SkillName = TEXT("A strike leaving ground under its user");
		Skill->Params = UCataclysmSkillShapes::ParseParams(
			TEXT("Radius=1; Angle=360; GroundRadius=4; GroundDuration=6; GroundPercent=16.7; EffectDuration=6; Effect=Shred"));
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Item.Weapon.Wand, Element.Demonic, Type.AOE.Persistent"));
		if (!User.AbilitySystem->TryActivateAbility(Handle))
		{
			return Read;
		}

		ACataclysmGroundZone* Zone = nullptr;
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It) && It->GetOwner() == User.Actor)
			{
				Zone = *It;
			}
		}
		if (!Zone)
		{
			return Read;
		}
		Read.bMade = true;
		Read.bTheZoneCarriesACurse = Zone->AppliedEffect.IsValid();
		Read.PerSweep = Zone->DamagePerTick;

		UAbilitySystemComponent* Owner = UCataclysmTargeting::AbilitySystemOf(User.Actor);
		const UAbilitySystemComponent* EnemySystem = UCataclysmTargeting::AbilitySystemOf(Enemy.Actor);
		const float OwnerBefore = Owner->GetNumericAttribute(Vital::GetHealthAttribute());
		const float EnemyBefore = EnemySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		Zone->Sweep();
		Read.EnemiesFoundByTheSweep = Zone->LastSweepCount;
		Read.TakenByTheOwner = OwnerBefore - Owner->GetNumericAttribute(Vital::GetHealthAttribute());
		Read.TakenByTheEnemy = EnemyBefore - EnemySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		Read.bOwnerIsSlowed = Owner->HasMatchingGameplayTag(UCataclysmDebuffs::CrippleTag());
		Read.bOwnerIsStaggered = UCataclysmSkillEffects::IsStaggered(User.Actor);
		Read.bOwnerIsBurning = UCataclysmSkillEffects::HasTag(User.Actor, UCataclysmSkillEffects::BurnTag());
		Read.bOwnerIsCursed = Zone->AppliedEffect.IsValid() && Owner->HasMatchingGameplayTag(Zone->AppliedEffect);

		// AND A SECOND SWEEP WHILE THE OWNER STAYS, with the stagger taken off by hand since nothing here moves time.
		Owner->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(UCataclysmSkillEffects::StaggeredTag()));
		Zone->Sweep();
		Read.bOwnerStaggeredAgainWhileStaying = UCataclysmSkillEffects::IsStaggered(User.Actor);
		return Read;
	}

	/**
	 * `zone_damages_its_owner` is read where the zone is left. A plain owner standing in their own zone takes
	 * nothing from a sweep; a carrying one takes the zone's sweep figure, and none of its effects.
	 */
	void ProbeZoneDamagesItsOwner(FAutomationTestBase& Test)
	{
		const FOwnerReading Plain = ReadAZoneUnderItsOwner([](TMap<FName, FCataclysmStatInputs>&) {});
		const FOwnerReading Hurt = ReadAZoneUnderItsOwner([](TMap<FName, FCataclysmStatInputs>& Inputs)
		{
			CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneDamagesItsOwnerStat, 1.0f);
		});
		if (!Test.TestTrue(TEXT("both strikes left a zone that deals something"),
						   Plain.bMade && Hurt.bMade && Plain.PerSweep > 0.0f && Hurt.PerSweep > 0.0f))
		{
			return;
		}
		Test.TestTrue(TEXT("control: a plain owner's zone damages the enemy standing in it"), Plain.TakenByTheEnemy > 0.0f);
		Test.TestEqual(TEXT("and deals its owner nothing"), Plain.TakenByTheOwner, 0.0f, 0.001f);
		Test.TestEqual(TEXT("a carrying owner takes the zone's sweep figure"), Hurt.TakenByTheOwner, Hurt.PerSweep, 0.01f);
		Test.TestEqual(TEXT("and the sweep still counts one enemy, not two"), Hurt.EnemiesFoundByTheSweep, 1);
		Test.TestFalse(TEXT("and the owner carries none of the zone's effects: not the slow"), Hurt.bOwnerIsSlowed);
		Test.TestFalse(TEXT("nor the stagger"), Hurt.bOwnerIsStaggered);
		Test.TestFalse(TEXT("nor the ailment"), Hurt.bOwnerIsBurning);
		Test.TestFalse(TEXT("nor the curse"), Hurt.bOwnerIsCursed);
	}

	/**
	 * `zone_applies_effects_to_owner` is read where the zone is left. A carrying owner standing in their own zone
	 * is slowed, staggered on entering, set alight and cursed by a sweep, and takes no damage from it.
	 */
	void ProbeZoneAppliesEffectsToOwner(FAutomationTestBase& Test)
	{
		const FOwnerReading Plain = ReadAZoneUnderItsOwner([](TMap<FName, FCataclysmStatInputs>&) {});
		const FOwnerReading Affected = ReadAZoneUnderItsOwner([](TMap<FName, FCataclysmStatInputs>& Inputs)
		{
			CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAppliesEffectsToOwnerStat, 1.0f);
		});
		if (!Test.TestTrue(TEXT("both strikes left a zone that carries a curse"),
						   Plain.bMade && Affected.bMade && Plain.bTheZoneCarriesACurse && Affected.bTheZoneCarriesACurse))
		{
			return;
		}
		Test.TestFalse(TEXT("control: a plain owner is not slowed by their own zone"), Plain.bOwnerIsSlowed);
		Test.TestFalse(TEXT("control: nor staggered"), Plain.bOwnerIsStaggered);
		Test.TestFalse(TEXT("control: nor set alight"), Plain.bOwnerIsBurning);
		Test.TestFalse(TEXT("control: nor cursed"), Plain.bOwnerIsCursed);
		Test.TestTrue(TEXT("a carrying owner is slowed by their own zone"), Affected.bOwnerIsSlowed);
		Test.TestTrue(TEXT("and staggered on entering it"), Affected.bOwnerIsStaggered);
		Test.TestFalse(TEXT("and not again while staying"), Affected.bOwnerStaggeredAgainWhileStaying);
		Test.TestTrue(TEXT("and set alight"), Affected.bOwnerIsBurning);
		Test.TestTrue(TEXT("and cursed"), Affected.bOwnerIsCursed);
		Test.TestEqual(TEXT("and the sweep deals them no damage"), Affected.TakenByTheOwner, 0.0f, 0.001f);
	}

	/**
	 * `zone_follows_owner_percent` is read where the zone is left. A blink that leaves ground leaves two zones that
	 * stay; the same blink by a caster carrying 50 leaves two that follow at 50.
	 */
	void ProbeZoneFollowsOwnerPercent(FAutomationTestBase& Test)
	{
		const TCHAR* Blink = TEXT("Mode=Blink; Range=8; Radius=3.5; GroundRadius=3.5; GroundDuration=6; GroundPercent=16.7");
		const TCHAR* Tags = TEXT("Item.Weapon.Wand, Element.Demonic, Type.AOE.Persistent");
		bool bPlainUsed = false;
		bool bCarryingUsed = false;
		const TArray<FLeftZone> Plain = ZonesLeftByOneUse(UCataclysmMovementSkill::StaticClass(), Blink, Tags,
			[](TMap<FName, FCataclysmStatInputs>&) {}, bPlainUsed);
		const TArray<FLeftZone> Carrying = ZonesLeftByOneUse(UCataclysmMovementSkill::StaticClass(), Blink, Tags,
			[](TMap<FName, FCataclysmStatInputs>& Inputs)
			{
				CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneFollowsOwnerPercentStat, 50.0f);
			}, bCarryingUsed);
		if (!Test.TestTrue(TEXT("both casters blinked and left two zones"),
						   bPlainUsed && bCarryingUsed && Plain.Num() == 2 && Carrying.Num() == 2))
		{
			return;
		}
		Test.TestEqual(TEXT("a plain caster's zone does not follow"), Plain[0].FollowsAtPercent, 0.0f, 0.001f);
		Test.TestEqual(TEXT("a carrying caster's first zone follows at the 50 carried"), Carrying[0].FollowsAtPercent, 50.0f, 0.001f);
		Test.TestEqual(TEXT("and the second too"), Carrying[1].FollowsAtPercent, 50.0f, 0.001f);
	}

	/**
	 * `minions_leave_chaos_pools` is a flag on a commander, read when a minion of theirs is killed. Ruled 2026-10-06.
	 *
	 * TWO COMMANDERS A HUNDRED METRES APART, as `ProbeExplodesOnDeath` has. The plain one loses an imp and is left
	 * no zone. The flagged one loses an imp and a ballista, each of which leaves a pool, and has a third imp
	 * destroyed without being killed -- which is how one whose time ran out or one removed to make room goes --
	 * and that one leaves none.
	 */
	void ProbeMinionsLeaveChaosPools(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger Flagged(World, FVector(0, 100 * M, 0));
		GrantFlat(Flagged.Actor, UCataclysmDamageCalculation::MinionsLeaveChaosPoolsStat, 1.0f);

		const FVector KilledAt(1 * M, 100 * M, 0);
		const FVector MachineAt(30 * M, 100 * M, 0);
		const FVector RemovedAt(60 * M, 100 * M, 0);
		ACataclysmMinion* PlainImp = ACataclysmMinion::Spawn(
			Plain.Actor, FVector(1 * M, 0, 0), /*Lifetime=*/20.0f, /*bBurns=*/false, TEXT("Imp"));
		ACataclysmMinion* Killed = ACataclysmMinion::Spawn(
			Flagged.Actor, KilledAt, /*Lifetime=*/20.0f, /*bBurns=*/false, TEXT("Imp"));
		ACataclysmMinion* Machine = ACataclysmMinion::Spawn(
			Flagged.Actor, MachineAt, /*Lifetime=*/20.0f, /*bBurns=*/false, TEXT("Ballista"));
		ACataclysmMinion* Removed = ACataclysmMinion::Spawn(
			Flagged.Actor, RemovedAt, /*Lifetime=*/20.0f, /*bBurns=*/false, TEXT("Imp"));
		if (!Test.TestTrue(TEXT("set-up: four minions"), PlainImp && Killed && Machine && Removed))
		{
			return;
		}
		ON_SCOPE_EXIT
		{
			for (ACataclysmMinion* One : {PlainImp, Killed, Machine})
			{
				if (IsValid(One))
				{
					One->Destroy();
				}
			}
		};
		const float ImpsOwnBlow = Killed->OwnDamagePerHit;
		const FVector KilledStood = Killed->GetActorLocation();
		const FVector MachineStood = Machine->GetActorLocation();
		const FVector RemovedStood = Removed->GetActorLocation();
		if (!Test.TestTrue(TEXT("set-up: the imp has a blow of its own to take a share of"), ImpsOwnBlow > 0.0f))
		{
			return;
		}

		KillMinion(PlainImp);
		KillMinion(Killed);
		KillMinion(Machine);
		Removed->Destroy();

		const auto PoolsOf = [World](const AActor* Commander)
		{
			TArray<ACataclysmGroundZone*> Found;
			for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
			{
				if (IsValid(*It) && It->GetOwner() == Commander)
				{
					Found.Add(*It);
				}
			}
			return Found;
		};
		const auto PoolAt = [](const TArray<ACataclysmGroundZone*>& Pools, const FVector& Where) -> ACataclysmGroundZone*
		{
			for (ACataclysmGroundZone* Pool : Pools)
			{
				if (FVector::Dist2D(Pool->GetActorLocation(), Where) < 100.0f)
				{
					return Pool;
				}
			}
			return nullptr;
		};

		Test.TestEqual(TEXT("a commander without the flag is left no pool by a killed imp"), PoolsOf(Plain.Actor).Num(), 0);
		const TArray<ACataclysmGroundZone*> Pools = PoolsOf(Flagged.Actor);
		Test.TestEqual(TEXT("a flagged commander is left two pools: the killed imp's and the killed ballista's"), Pools.Num(), 2);
		Test.TestNotNull(TEXT("a killed machine that carries the minion tag leaves one where it stood"), PoolAt(Pools, MachineStood));
		Test.TestNull(TEXT("an imp destroyed without being killed leaves none"), PoolAt(Pools, RemovedStood));
		const ACataclysmGroundZone* Pool = PoolAt(Pools, KilledStood);
		if (!Test.TestNotNull(TEXT("the killed imp leaves one where it stood"), Pool))
		{
			return;
		}
		Test.TestEqual(TEXT("the pool is 3 metres in radius"), Pool->RadiusCm, 300.0f, 0.01f);
		Test.TestEqual(TEXT("and lasts 4 seconds"), Pool->GetLifeSpan(), 4.0f, 0.01f);
		Test.TestEqual(TEXT("and a sweep deals 10 of the imp's own blow"), Pool->DamagePerTick, ImpsOwnBlow * 0.1f, 0.001f);
		Test.TestTrue(TEXT("as Chaos damage"), Pool->DamageType == FName(TEXT("Chaos")));
	}

	/** A damage over time tick of this size, of the ailment whose tag this is, or of none for an invalid tag. */
	FCataclysmIncomingHit TickOf(float Damage, const FGameplayTag& Ailment)
	{
		FCataclysmIncomingHit Tick;
		Tick.Damage = Damage;
		Tick.bIsDamageOverTime = true;
		if (Ailment.IsValid())
		{
			Tick.DamageOverTimeTags.AddTag(Ailment);
			Tick.bIsBleed = Ailment == UCataclysmDebuffs::BleedTag();
		}
		return Tick;
	}

	/** What a tick comes to on this character, with the evasion and block rolls pinned to miss. */
	FCataclysmDamageResult ResolveATick(const FCataclysmIncomingHit& Tick, const UAbilitySystemComponent* Defender)
	{
		return UCataclysmDamageCalculation::Resolve(Tick, Defender, /*Tier=*/1, /*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f);
	}

	/**
	 * One modifier on a stat held at its base of 100, requiring a tag where one is given. For the two stats a
	 * defender is asked about what arrives, `damage_over_time_taken` and `debuff_duration_taken`.
	 */
	void CarryOnAHundred(TMap<FName, FCataclysmStatInputs>& Inputs, const TCHAR* Stat, ECataclysmStatBucket Bucket,
						 float Value, const FGameplayTag& Required = FGameplayTag())
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = Bucket;
		Modifier.Source = ECataclysmModifierSource::Enchantment;
		Modifier.Value = Value;
		if (Required.IsValid())
		{
			Modifier.RequiredTags.AddTag(Required);
		}
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
		Line.Base = 100.0f;
		Line.Modifiers.Add(Modifier);
	}

	/** The longest time left on any effect this character carries that grants the tag, or nought. */
	float SecondsLeftOfTag(const UAbilitySystemComponent* System, const FGameplayTag& Tag)
	{
		float Longest = 0.0f;
		for (const float Seconds : System->GetActiveEffectsTimeRemaining(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
		{
			Longest = FMath::Max(Longest, Seconds);
		}
		return Longest;
	}

	/**
	 * `ailment_immunity` is a flag asked with the tags of the ailment being applied, read by
	 * `UCataclysmSkillEffects::ApplyDamageOverTime`. Ruled 2026-10-06: "Unaffected by bleeding".
	 *
	 * ONE ATTACKER AND TWO TARGETS, the second carrying the flag. The same bleed is applied to each: the plain one
	 * bleeds and the flagged one does not, and the application to the flagged one answers false.
	 */
	void ProbeAilmentImmunity(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
		if (!Test.TestTrue(TEXT("set-up: the bleed tag"), Bleed.IsValid()))
		{
			return;
		}
		FScopedSwinger Attacker(World, FVector::ZeroVector);
		FScopedSwinger Plain(World, FVector(2 * M, 0, 0));
		FScopedSwinger Immune(World, FVector(2 * M, 100 * M, 0));
		GrantFlat(Immune.Actor, UCataclysmDamageCalculation::AilmentImmunityStat, 1.0f);

		const bool bPlainApplied = UCataclysmSkillEffects::ApplyDamageOverTime(
			Attacker.Actor, Plain.Actor, /*DamagePerTick=*/10.0f, /*DurationSeconds=*/5.0f, Bleed);
		const bool bImmuneApplied = UCataclysmSkillEffects::ApplyDamageOverTime(
			Attacker.Actor, Immune.Actor, /*DamagePerTick=*/10.0f, /*DurationSeconds=*/5.0f, Bleed);
		Test.TestTrue(TEXT("control: a bleed applied to a plain character leaves them bleeding"),
					  bPlainApplied && UCataclysmDebuffs::IsBleeding(Plain.AbilitySystem));
		Test.TestFalse(TEXT("and one carrying ailment_immunity is not bleeding, so ApplyDamageOverTime really reads it"),
					   UCataclysmDebuffs::IsBleeding(Immune.AbilitySystem));
		Test.TestFalse(TEXT("and the application to them answers that nothing was applied"), bImmuneApplied);
	}

	/**
	 * `bleed_damage_taken_from_energy_shield` is the percent of a bleed tick the energy shield takes, read by
	 * `UCataclysmDamageCalculation::Resolve`. Ruled 2026-10-06.
	 *
	 * TWO SHIELDED CHARACTERS, the second carrying the stat at 20, each asked about the same bleed tick. The plain
	 * one's shield takes none of it. The other's takes a fifth and their health the rest.
	 */
	void ProbeBleedDamageTakenFromEnergyShield(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger Held(World, FVector(0, 100 * M, 0));
		for (FScopedSwinger* One : {&Plain, &Held})
		{
			One->Set(Vital::GetMaxEnergyShieldAttribute(), 500.0f);
			One->Set(Vital::GetEnergyShieldAttribute(), 500.0f);
		}
		GrantFlat(Held.Actor, UCataclysmDamageCalculation::BleedDamageTakenFromEnergyShieldStat, 20.0f);

		const FCataclysmIncomingHit Bleed = TickOf(100.0f, UCataclysmDebuffs::BleedTag());
		const FCataclysmDamageResult OnPlain = ResolveATick(Bleed, Plain.AbilitySystem);
		const FCataclysmDamageResult OnHeld = ResolveATick(Bleed, Held.AbilitySystem);
		if (!Test.TestTrue(TEXT("set-up: the tick is a bleed and it reaches a plain character's health"),
						   Bleed.bIsBleed && OnPlain.DealtToHealth > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("control: a plain character's shield takes none of a bleed"), OnPlain.AbsorbedByShield, 0.0f, 0.001f);
		Test.TestEqual(TEXT("one carrying bleed_damage_taken_from_energy_shield at 20 has a fifth of it taken from the shield, so Resolve really reads it"),
					   OnHeld.AbsorbedByShield, OnPlain.DealtToHealth * 0.2f, 0.01f);
		Test.TestEqual(TEXT("and the other four fifths from health"), OnHeld.DealtToHealth, OnPlain.DealtToHealth * 0.8f, 0.01f);
	}

	/**
	 * `damage_over_time_taken_from_mana_first` is a flag read by `UCataclysmDamageCalculation::Resolve`, and
	 * `UCataclysmVitalAttributeSet` takes the figure from the mana. Ruled 2026-10-06.
	 *
	 * A TICK DELIVERED AS DAMAGE, to a plain character and to one carrying the flag. The plain one loses health and
	 * no mana. The flagged one loses the same amount of mana and no health.
	 */
	void ProbeDamageOverTimeTakenFromManaFirst(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Attacker(World, FVector::ZeroVector);
		FScopedSwinger Plain(World, FVector(2 * M, 0, 0));
		FScopedSwinger Held(World, FVector(2 * M, 100 * M, 0));
		GrantFlat(Held.Actor, UCataclysmDamageCalculation::DamageOverTimeTakenFromManaFirstStat, 1.0f);

		FCataclysmHitDelivery AsATick;
		AsATick.bIsDamageOverTime = true;
		const float HealthBefore = Plain.Get(Vital::GetHealthAttribute());
		const float ManaBefore = Plain.Get(Vital::GetManaAttribute());
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker.Actor, Plain.Actor, 300.0f, AsATick);
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker.Actor, Held.Actor, 300.0f, AsATick);
		const float PlainLost = HealthBefore - Plain.Get(Vital::GetHealthAttribute());
		if (!Test.TestTrue(TEXT("set-up: the tick takes health from a plain character, and less than the mana held"),
						   PlainLost > 0.0f && PlainLost < ManaBefore))
		{
			return;
		}
		Test.TestEqual(TEXT("control: and none of a plain character's mana"), Plain.Get(Vital::GetManaAttribute()), ManaBefore, 0.001f);
		Test.TestEqual(TEXT("one carrying damage_over_time_taken_from_mana_first loses that much mana instead, so Resolve really reads it"),
					   Held.Get(Vital::GetManaAttribute()), ManaBefore - PlainLost, 0.01f);
		Test.TestEqual(TEXT("and no health"), Held.Get(Vital::GetHealthAttribute()), HealthBefore, 0.001f);
	}

	/**
	 * A player character on its player state, for the potion probes below: only a
	 * player character holds potions. Issue #806.
	 */
	ACataclysmPlayerCharacter* SpawnPotionHolder(UWorld* World)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Character)
		{
			return nullptr;
		}
		Character->SetPlayerState(PlayerState);
		Character->OnRep_PlayerState();
		if (UCataclysmAbilitySystemComponent* System =
				PlayerState->GetCataclysmAbilitySystemComponent())
		{
			System->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000.0f);
			System->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000.0f);
		}
		return Character;
	}

	/**
	 * `curse_death_raises_imp`, asked by `UCataclysmRisenImps::CurserOf`. The
	 * Ritualist's starting node, issue #1479. Two cursers, one holding the flag,
	 * each laying Shred on its own victim, so neither answer can be the other's.
	 */
	void ProbeCurseDeathRaisesImp(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter PlainVictim(World, /*AttackDamage=*/0.0f);
		FScopedFighter Flagged(World, /*AttackDamage=*/0.0f);
		FScopedFighter FlaggedVictim(World, /*AttackDamage=*/0.0f);
		GrantFlat(Flagged.Actor, UCataclysmRisenImps::RisesStat, 1.0f);

		const FGameplayTag Shred = FGameplayTag::RequestGameplayTag(
			FName(TEXT("Status.Debuff.Shred")), /*ErrorIfNotFound=*/false);
		if (!Test.TestTrue(TEXT("set-up: the plain curser lays Shred"),
				UCataclysmSkillEffects::ApplyTagForDuration(
					Plain.Actor, PlainVictim.Actor, Shred, 10.0f))
			|| !Test.TestTrue(TEXT("set-up: and so does the other"),
				UCataclysmSkillEffects::ApplyTagForDuration(
					Flagged.Actor, FlaggedVictim.Actor, Shred, 10.0f)))
		{
			return;
		}

		Test.TestNull(TEXT("a curse laid without curse_death_raises_imp raises nothing"),
					  UCataclysmRisenImps::CurserOf(PlainVictim.Actor));
		Test.TestTrue(TEXT("and one laid holding it names its curser, so CurserOf "
						   "really reads curse_death_raises_imp"),
					  UCataclysmRisenImps::CurserOf(FlaggedVictim.Actor) == Flagged.Actor);
	}

	/**
	 * `melee_arc_full_circle`, asked by `UCataclysmStrikeSkill::ArcDegrees`. Every
	 * Swing Lands, issue #1515. Two fighters, one holding the flag, each with the
	 * same 60-degree melee swing, so neither answer can be the other's.
	 */
	void ProbeMeleeArcFullCircle(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter Flagged(World, /*AttackDamage=*/0.0f);
		GrantFlat(Flagged.Actor, UCataclysmStrikeSkill::MeleeArcFullCircleStat, 1.0f);

		const auto MeleeSwing = [&Test](FScopedFighter& Who) -> UCataclysmStrikeSkill*
		{
			const FGameplayAbilitySpecHandle Handle = Who.AbilitySystem->GiveAbilityInSlot(
				UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Heavy,
				/*Level=*/1, Who.Actor);
			FGameplayAbilitySpec* Spec = Who.AbilitySystem->FindAbilitySpecFromHandle(Handle);
			UCataclysmStrikeSkill* Skill =
				Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=3; Angle=60"));
				Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Melee"));
			}
			Test.TestNotNull(TEXT("set-up: a melee swing is granted"), Skill);
			return Skill;
		};
		const UCataclysmStrikeSkill* PlainSwing = MeleeSwing(Plain);
		const UCataclysmStrikeSkill* FlaggedSwing = MeleeSwing(Flagged);
		if (!PlainSwing || !FlaggedSwing)
		{
			return;
		}

		Test.TestEqual(TEXT("a swing without melee_arc_full_circle keeps its 60 degrees"),
					   PlainSwing->ArcDegrees(), 60.0f, 0.001f);
		Test.TestEqual(TEXT("and one holding it is a full circle, so ArcDegrees really "
							"reads melee_arc_full_circle"),
					   FlaggedSwing->ArcDegrees(), 360.0f, 0.001f);
	}

	/** `potions_forbidden`, asked by `UCataclysmPotions::Drink`. Hard Mode. */
	void ProbePotionsForbidden(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmPlayerCharacter* Plain = SpawnPotionHolder(World);
		ACataclysmPlayerCharacter* Held = SpawnPotionHolder(World);
		if (!Test.TestNotNull(TEXT("a player character"), Plain)
			|| !Test.TestNotNull(TEXT("and another"), Held))
		{
			return;
		}
		GrantFlats(Held, {{FName(UCataclysmPotions::ForbiddenStat), 1.0f}});

		Test.TestEqual(TEXT("a player without potions_forbidden drinks"),
					   UCataclysmPotions::Drink(Plain, 0), ECataclysmPotionRefusal::None);
		Test.TestEqual(TEXT("and one holding it is refused, so Drink really reads it"),
					   UCataclysmPotions::Drink(Held, 0), ECataclysmPotionRefusal::Forbidden);
	}

	/** `potion_kill_charges_less_percent`, asked by `NoteEnemyKilled`. Recession. */
	void ProbePotionKillChargesLess(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmPlayerCharacter* Plain = SpawnPotionHolder(World);
		ACataclysmPlayerCharacter* Held = SpawnPotionHolder(World);
		if (!Test.TestNotNull(TEXT("a player character"), Plain)
			|| !Test.TestNotNull(TEXT("and another"), Held))
		{
			return;
		}
		GrantFlats(Held, {{FName(UCataclysmPotions::KillChargesLessStat), 75.0f}});

		Test.TestEqual(TEXT("an Elite kill adds 3.5 to a player without it"),
					   UCataclysmPotions::NoteEnemyKilled(Plain, 1), 3.5f, 0.001f);
		Test.TestEqual(TEXT("and a quarter of that to one holding 75, so it is read"),
					   UCataclysmPotions::NoteEnemyKilled(Held, 1), 0.875f, 0.001f);
	}

	/** `potion_heal_less_percent_per_drink`, asked by `Drink`. Diminishing Returns. */
	void ProbePotionHealLessPerDrink(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmPlayerCharacter* Plain = SpawnPotionHolder(World);
		ACataclysmPlayerCharacter* Held = SpawnPotionHolder(World);
		UCataclysmAbilitySystemComponent* PlainSystem = Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Plain));
		UCataclysmAbilitySystemComponent* HeldSystem = Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Held));
		if (!Test.TestNotNull(TEXT("a player's ability system"), PlainSystem)
			|| !Test.TestNotNull(TEXT("and another's"), HeldSystem))
		{
			return;
		}
		GrantFlats(Held, {{FName(UCataclysmPotions::HealLessPerDrinkStat), 10.0f}});
		PlainSystem->SetPotionsDrunk(3);
		HeldSystem->SetPotionsDrunk(3);

		UCataclysmPotions::Drink(Plain, 0);
		UCataclysmPotions::Drink(Held, 0);
		Test.TestEqual(TEXT("a fourth drink without it heals 350 of 1000"),
					   PlainSystem->GetPotionHeal().Remaining, 350.0f, 0.01f);
		Test.TestEqual(TEXT("and with 10 a drink, 70% of that, so it is read"),
					   HeldSystem->GetPotionHeal().Remaining, 245.0f, 0.01f);
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

	/**
	 * `resistance_cap`, scaled by the character's kills, asked by
	 * `UCataclysmDamageCalculation::ResistanceCapOf`. Issue #1833, the kill
	 * counter: the shipped row is flat -1 to -4 per 100,000 to 500,000 kills.
	 * A player's ability system, because only a player state counts kills.
	 */
	/**
	 * `skill_locked`, scaled by the row's own stacks, asked by
	 * `UCataclysmSkillTemplate::CanActivateAbility` before any skill that is not
	 * a basic attack. Issue #1833, the two skill-lock negatives: "Killing an
	 * enemy triggers a 1-2 second global cooldown on all your skills" is a flat
	 * 1 scaled by `own_stacks`, granted on a kill.
	 *
	 * TWO CASTERS CARRYING THE SAME LINE, one holding the stack and one not, for
	 * the reason the skill-lock test in CataclysmSkillTemplateTests.cpp gives:
	 * each activates once, so no cooldown can be what refuses the second. The
	 * stack is the only difference between them.
	 */
	void ProbeScaledSkillLocked(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FName Key(TEXT("Probe:skill_locked"));
		const auto Prepare = [&Key](FScopedSwinger& Caster)
		{
			FCataclysmStatModifier Lock;
			Lock.Bucket = ECataclysmStatBucket::Flat;
			Lock.Source = ECataclysmModifierSource::Enchantment;
			Lock.Value = 1.0f;
			Lock.Scale = ECataclysmStatScale::PerOwnStack;
			Lock.ScaleStep = 1.0f;
			Lock.ScaleMaxSteps = 1;
			Lock.StackKey = Key;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(UCataclysmSkillSlots::LockedStat));
			Line.Base = 0.0f;
			Line.Modifiers = {Lock};
			Caster.AbilitySystem->SetStatInputs(MoveTemp(Inputs));

			const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
				UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Support,
				/*Level=*/1, Caster.Actor);
			FGameplayAbilitySpec* Spec = Handle.IsValid()
				? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmSelfBuffSkill* Skill =
				Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->SkillName = TEXT("A probe's buff");
				Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Duration=6"));
			}
			return Skill;
		};

		FScopedSwinger Clean(World, FVector::ZeroVector);
		FScopedSwinger Stacked(World, FVector(0, 100 * M, 0));
		UCataclysmSelfBuffSkill* CleanSkill = Prepare(Clean);
		UCataclysmSelfBuffSkill* StackedSkill = Prepare(Stacked);
		if (!Test.TestNotNull(TEXT("a skill for the caster with no stack"), CleanSkill)
			|| !Test.TestNotNull(TEXT("and for the caster holding one"), StackedSkill))
		{
			return;
		}
		Stacked.AbilitySystem->GrantOwnStack(Key, /*WindowSeconds=*/5.0f, /*Cap=*/1);

		const auto Use = [](FScopedSwinger& Caster, UGameplayAbility* Ability)
		{
			return Caster.AbilitySystem->TryActivateAbility(
				Ability->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		};
		// THE CONTROL FIRST, so the refusal below is evidence of the stack
		// rather than of a skill refused for some other reason.
		Test.TestTrue(TEXT("with no stack held the skill is used"), Use(Clean, CleanSkill));
		Test.TestFalse(TEXT("skill_locked is asked for, so one stack held refuses the same skill"),
					   Use(Stacked, StackedSkill));
	}

	void ProbeScaledResistanceCap(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		UCataclysmAbilitySystemComponent* System =
			State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
		if (!Test.TestNotNull(TEXT("a player's ability system"), System))
		{
			return;
		}
		FCataclysmStatModifier Flat;
		Flat.Bucket = ECataclysmStatBucket::Flat;
		Flat.Source = ECataclysmModifierSource::Enchantment;
		Flat.Value = -4.0f;
		Flat.Scale = ECataclysmStatScale::PerKillOfTheCharacter;
		Flat.ScaleStep = 500000.0f;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(
			FName(UCataclysmDamageCalculation::ResistanceCapStat));
		Line.Base = 0.0f;
		Line.Modifiers = {Flat};
		System->SetStatInputs(MoveTemp(Inputs));

		const float Clean = UCataclysmDamageCalculation::ResistanceCapOf(System);
		State->SetLifetimeKills(1000000);
		const float Killed = UCataclysmDamageCalculation::ResistanceCapOf(System);
		Test.TestTrue(
			FString::Printf(TEXT("resistance_cap is asked for, so a million kills "
								 "lower the cap: %.2f against %.2f"), Killed, Clean),
			Killed < Clean - 0.001f);
	}

	/**
	 * `evasion`, scaled by the deployable machines commanded, asked by
	 * `DefenderStat` in `UCataclysmDamageCalculation::Resolve`. Issue #1833,
	 * deployable Part 3. With the evasion roll pinned at 25, a defender at 20
	 * is hit; with one ballista commanded and a row of 50% increased per
	 * machine it stands at 30, and the same blow is evaded.
	 */
	void ProbeScaledEvasion(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		Defender.AbilitySystem->SetNumericAttributeBase(
			Combat::GetEvasionAttribute(), 20.0f);
		ScaledBy(Defender.Actor, TEXT("evasion"), 50.0f,
				 ECataclysmStatScale::PerDeployableActive, /*Base=*/20.0f);

		FCataclysmIncomingHit Blow;
		Blow.Damage = 100.0f;
		const auto Evaded = [&Blow, &Defender]()
		{
			return UCataclysmDamageCalculation::Resolve(
				Blow, Defender.AbilitySystem, /*Tier=*/1, /*EvasionRoll=*/25.0f,
				/*BlockRoll=*/100.0f).bEvaded;
		};
		const bool bClean = Evaded();
		ACataclysmMinion* Ballista = ACataclysmMinion::Spawn(
			Defender.Actor, FVector(300.0f, 0.0f, 0.0f), /*Lifetime=*/20.0f,
			/*bBurns=*/false, TEXT("Ballista"));
		if (!Test.TestNotNull(TEXT("a ballista commanded"), Ballista))
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(Ballista)) { Ballista->Destroy(); } };
		Test.TestTrue(
			FString::Printf(TEXT("evasion is asked for, so a machine commanded "
								 "turns a hit into an evade: %s then %s"),
							bClean ? TEXT("evaded") : TEXT("hit"),
							Evaded() ? TEXT("evaded") : TEXT("hit")),
			!bClean && Evaded());
	}

	/**
	 * `crowd_control_resistance`, asked by
	 * `UCataclysmSkillEffects::HeldSecondsAfterCrowdControlResistance` on every
	 * crowd control. Issue #1833 group D part 2: the Demon King's Regalia scales
	 * it by its own stacks. Measured with a scale the probe can move, a machine
	 * commanded, as the evasion probe does.
	 */
	void ProbeScaledCrowdControlResistance(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		Defender.AbilitySystem->SetNumericAttributeBase(
			Combat::GetCrowdControlResistanceAttribute(), 40.0f);
		ScaledBy(Defender.Actor, TEXT("crowd_control_resistance"), 100.0f,
				 ECataclysmStatScale::PerDeployableActive, /*Base=*/40.0f);

		const float Clean =
			UCataclysmSkillEffects::HeldSecondsAfterCrowdControlResistance(Defender.Actor, 4.0f);
		ACataclysmMinion* Ballista = ACataclysmMinion::Spawn(
			Defender.Actor, FVector(300.0f, 0.0f, 0.0f), /*Lifetime=*/20.0f,
			/*bBurns=*/false, TEXT("Ballista"));
		if (!Test.TestNotNull(TEXT("a ballista commanded"), Ballista))
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(Ballista)) { Ballista->Destroy(); } };
		const float Commanding =
			UCataclysmSkillEffects::HeldSecondsAfterCrowdControlResistance(Defender.Actor, 4.0f);
		Test.TestTrue(
			FString::Printf(TEXT("crowd control resistance is asked for, so a machine "
								 "commanded shortens a crowd control: %.2f then %.2f"),
							Clean, Commanding),
			Commanding < Clean);
	}

	/**
	 * `block_damage_reduction`, scaled by own stacks, read by
	 * `UCataclysmDamageCalculation::BlockShareOf` at the block step. Issue #1833
	 * group E part 2, "Consecutive blocks within 3 seconds each block 5%-10% more
	 * damage". Two defenders carry the line at its base of 50 with 10 flat per own
	 * stack; one holds a stack. A blocked hit of 100 keeps 50 on the one and 40 on
	 * the other.
	 */
	void ProbeScaledBlockDamageReduction(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FName Key(TEXT("Probe:block_damage_reduction"));
		const auto Prepare = [&Key](FScopedFighter& Defender)
		{
			FCataclysmStatModifier PerStack;
			PerStack.Bucket = ECataclysmStatBucket::Flat;
			PerStack.Source = ECataclysmModifierSource::Enchantment;
			PerStack.Value = 10.0f;
			PerStack.Scale = ECataclysmStatScale::PerOwnStack;
			PerStack.ScaleStep = 1.0f;
			PerStack.ScaleMaxSteps = 7;
			PerStack.StackKey = Key;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line =
				Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::BlockDamageReductionStat));
			Line.Base = UCataclysmDamageCalculation::BlockDamageReduction;
			Line.Modifiers = {PerStack};
			Defender.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
			Defender.AbilitySystem->SetNumericAttributeBase(Combat::GetBlockChanceAttribute(), 100.0f);
		};
		FScopedFighter Clean(World, /*AttackDamage=*/0.0f);
		FScopedFighter Stacked(World, /*AttackDamage=*/0.0f);
		Prepare(Clean);
		Prepare(Stacked);
		Cast<UCataclysmAbilitySystemComponent>(Stacked.AbilitySystem)
			->GrantOwnStack(Key, /*WindowSeconds=*/3.0f, /*Cap=*/7);

		FCataclysmIncomingHit Blow;
		Blow.Damage = 100.0f;
		const auto Kept = [&Blow](FScopedFighter& Defender)
		{
			const FCataclysmDamageResult Result = UCataclysmDamageCalculation::Resolve(
				Blow, Defender.AbilitySystem, /*Tier=*/1, /*EvasionRoll=*/100.0f,
				/*BlockRoll=*/0.0f);
			return Result.bBlocked ? Result.DealtToHealth : -1.0f;
		};
		const float KeptClean = Kept(Clean);
		const float KeptStacked = Kept(Stacked);
		Test.TestEqual(TEXT("a block with no stack keeps half"), KeptClean, 50.0f, 0.01f);
		Test.TestEqual(TEXT("block_damage_reduction is asked for, so a block holding one "
							"stack of ten keeps forty"), KeptStacked, 40.0f, 0.01f);
	}

	/**
	 * One of the three leech stats, scaled by own stacks, read by
	 * `UCataclysmLeech::NoteHit` for every hit that took health. Issue #1833,
	 * "Starvation (10-Piece Bonus)". Two attackers carry the line at a base of
	 * 10 with 100 increased per own stack; one holds a stack. A hit of 1000
	 * queues 100 for the one and 200 for the other, in the stat's own pool.
	 */
	void ProbeScaledLeech(FAutomationTestBase& Test, const TCHAR* Stat, ECataclysmLeechPool Pool)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FName Key(*FString::Printf(TEXT("Probe:%s"), Stat));
		const auto Prepare = [&Key, Stat](FScopedFighter& Attacker)
		{
			FCataclysmStatModifier PerStack;
			PerStack.Bucket = ECataclysmStatBucket::Increased;
			PerStack.Source = ECataclysmModifierSource::Enchantment;
			PerStack.Value = 100.0f;
			PerStack.Scale = ECataclysmStatScale::PerOwnStack;
			PerStack.ScaleStep = 1.0f;
			PerStack.ScaleMaxSteps = 10;
			PerStack.StackKey = Key;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
			Line.Base = 10.0f;
			Line.Modifiers = {PerStack};
			Attacker.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		};
		FScopedFighter Clean(World, /*AttackDamage=*/0.0f);
		FScopedFighter Stacked(World, /*AttackDamage=*/0.0f);
		Prepare(Clean);
		Prepare(Stacked);
		Stacked.AbilitySystem->GrantOwnStack(Key, /*WindowSeconds=*/4.0f, /*Cap=*/10);

		const auto Queued = [Pool](FScopedFighter& Attacker)
		{
			UCataclysmLeech::NoteHit(Attacker.AbilitySystem, 1000.0f, FGameplayTagContainer());
			float Total = 0.0f;
			for (const FCataclysmLeechPayment& Payment : Attacker.AbilitySystem->GetLeechPayments())
			{
				Total += Payment.Pool == Pool ? Payment.Remaining : 0.0f;
			}
			return Total;
		};
		const float QueuedClean = Queued(Clean);
		const float QueuedStacked = Queued(Stacked);
		Test.TestEqual(FString::Printf(TEXT("%s with no stack leeches a tenth of the hit"), Stat),
					   QueuedClean, 100.0f, 0.01f);
		Test.TestEqual(FString::Printf(TEXT("%s is asked for, so one stack of a hundred increased "
											"leeches twice that"), Stat),
					   QueuedStacked, 200.0f, 0.01f);
	}

	void ProbeScaledLifeLeech(FAutomationTestBase& Test)
	{
		ProbeScaledLeech(Test, TEXT("life_leech"), ECataclysmLeechPool::Health);
	}

	void ProbeScaledManaLeech(FAutomationTestBase& Test)
	{
		ProbeScaledLeech(Test, TEXT("mana_leech"), ECataclysmLeechPool::Mana);
	}

	void ProbeScaledEnergyShieldLeech(FAutomationTestBase& Test)
	{
		ProbeScaledLeech(Test, TEXT("energy_shield_leech"), ECataclysmLeechPool::EnergyShield);
	}

	/**
	 * `minion_damage` under a scale, read by `ACataclysmMinion::AttackTarget`
	 * through `UCataclysmCommand::SummonerMultiplierAgainst`. Issue #2284, ruled
	 * 2026-10-07.
	 *
	 * THE SCALE THE DATA PAIRS WITH IT: 30 increased for each OTHER trap the
	 * summoner commands, `traps_active` with an offset of 1, requiring
	 * `Type.Trap`. One trap strikes alone; a second trap is summoned and the
	 * first strikes again. A RATIO TO ITS OWN FIRST BLOW, never a figure: 1.3.
	 * A ballista of the same summoner is the control: its blow is the same
	 * before and after, because the row names traps.
	 *
	 * THE TRAP TAG IS PUT ON BY HAND, so this does not depend on what the
	 * minion types table gives a Spike Trap.
	 */
	void ProbeScaledMinionDamage(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const FGameplayTag TrapTag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap")), /*ErrorIfNotFound=*/false);
		if (!Test.TestTrue(TEXT("minion_damage: the trap tag is registered"), TrapTag.IsValid()))
		{
			return;
		}
		FScopedFighter Summoner(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Target(World, /*AttackDamage=*/0.0f);

		FCataclysmStatModifier PerOtherTrap;
		PerOtherTrap.Bucket = ECataclysmStatBucket::Increased;
		PerOtherTrap.Source = ECataclysmModifierSource::GearAffix;
		PerOtherTrap.Value = 30.0f;
		PerOtherTrap.Scale = ECataclysmStatScale::PerTrapActive;
		PerOtherTrap.ScaleStep = 1.0f;
		PerOtherTrap.ScaleOffset = 1.0f;
		PerOtherTrap.RequiredTags.AddTag(TrapTag);
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(TEXT("minion_damage")));
		Line.Base = 0.0f;
		Line.Modifiers = {PerOtherTrap};
		Summoner.AbilitySystem->SetStatInputs(MoveTemp(Inputs));

		// FOUR METRES APART, so no spawn is refused for standing in another.
		TArray<ACataclysmMinion*> Machines;
		ON_SCOPE_EXIT
		{
			for (ACataclysmMinion* Made : Machines)
			{
				if (IsValid(Made)) { Made->Destroy(); }
			}
		};
		const auto Summon = [&](const TCHAR* Type, float Metres, bool bATrap) -> ACataclysmMinion*
		{
			ACataclysmMinion* Made = ACataclysmMinion::Spawn(
				Summoner.Actor, FVector(Metres * M, 0, 0), /*Lifetime=*/20.0f, /*bBurns=*/false, Type);
			if (Made && bATrap)
			{
				Made->TypeTags.AddTag(TrapTag);
			}
			Machines.Add(Made);
			return Made;
		};
		const auto BlowFrom = [&Target](ACataclysmMinion* Machine)
		{
			const float Before = Target.Health();
			Machine->AttackTarget(Target.Actor);
			return Before - Target.Health();
		};

		ACataclysmMinion* First = Summon(TEXT("SpikeTrap"), 4.0f, /*bATrap=*/true);
		ACataclysmMinion* Ballista = Summon(TEXT("Ballista"), 8.0f, /*bATrap=*/false);
		if (!Test.TestNotNull(TEXT("minion_damage: a spike trap"), First)
			|| !Test.TestNotNull(TEXT("minion_damage: a ballista"), Ballista)
			|| !Test.TestTrue(TEXT("minion_damage: the spike trap is a trap"), First->IsTrap())
			|| !Test.TestFalse(TEXT("minion_damage: the ballista is not"), Ballista->IsTrap()))
		{
			return;
		}
		const float TrapAlone = BlowFrom(First);
		const float BallistaBefore = BlowFrom(Ballista);
		if (!Test.TestTrue(TEXT("minion_damage: one trap alone lands a blow"), TrapAlone > 0.0f)
			|| !Test.TestTrue(TEXT("minion_damage: the ballista lands a blow"), BallistaBefore > 0.0f))
		{
			return;
		}

		ACataclysmMinion* Second = Summon(TEXT("SpikeTrap"), 12.0f, /*bATrap=*/true);
		if (!Test.TestNotNull(TEXT("minion_damage: a second spike trap"), Second))
		{
			return;
		}
		Test.TestEqual(
			TEXT("minion_damage is asked for with the summoner's count of traps, so with one other trap a "
				 "trap's blow is 1.3 times its blow alone: ACataclysmMinion::AttackTarget really reads it"),
			BlowFrom(First) / TrapAlone, 1.3f, 0.001f);
		Test.TestEqual(TEXT("minion_damage: the ballista's blow is unchanged, since the row names traps"),
			BlowFrom(Ballista) / BallistaBefore, 1.0f, 0.001f);
	}

	const TMap<FString, FProbe>& ScaledProbes()
	{
		static const TMap<FString, FProbe> Made = {
			{TEXT("crowd_control_resistance"),   &ProbeScaledCrowdControlResistance},
			{TEXT("skill_locked"),               &ProbeScaledSkillLocked},
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
			{TEXT("health_reserved"),            &ProbeScaledHealthReserved},
			{TEXT("mana_regen"),                 &ProbeScaledManaRegen},
			{TEXT("movement_speed"),             &ProbeScaledMovementSpeed},
			{TEXT("evasion"),                    &ProbeScaledEvasion},
			{TEXT("block_damage_reduction"),     &ProbeScaledBlockDamageReduction},
			{TEXT("resistance_cap"),             &ProbeScaledResistanceCap},
			{TEXT("life_leech"),                 &ProbeScaledLifeLeech},
			{TEXT("mana_leech"),                 &ProbeScaledManaLeech},
			{TEXT("energy_shield_leech"),        &ProbeScaledEnergyShieldLeech},
			{TEXT("minion_damage"),              &ProbeScaledMinionDamage},
		};
		return Made;
	}

	/**
	 * `health_reserved` and `health_reserved_percent`, read by
	 * `UCataclysmAbilitySystemComponent::HealthReserved`. Issue #1833, health
	 * reservation. Each grants its stat on a character with a maximum of 1000
	 * and asserts the unreserved maximum falls: 200 points, and a 20% share.
	 */
	float UnreservedWith(FAutomationTestBase& Test, const TCHAR* Stat, float Value)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return -1.0f;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		UCataclysmAbilitySystemComponent* System = Fighter.AbilitySystem;
		System->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		if (Value > 0.0f)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::Enchantment;
			Flat.Value = Value;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
			System->SetStatInputs(MoveTemp(Inputs));
		}
		return System->UnreservedMaximumHealth();
	}

	void ProbeHealthReserved(FAutomationTestBase& Test)
	{
		const TCHAR* Stat = UCataclysmAbilitySystemComponent::HealthReservedStat;
		Test.TestEqual(TEXT("health_reserved is read: 200 points of 1000 reserved leave 800"),
					   UnreservedWith(Test, Stat, 200.0f), 800.0f, 0.01f);
		Test.TestEqual(TEXT("and nothing granted leaves 1000"),
					   UnreservedWith(Test, Stat, 0.0f), 1000.0f, 0.01f);
	}

	void ProbeHealthReservedPercent(FAutomationTestBase& Test)
	{
		const TCHAR* Stat = UCataclysmAbilitySystemComponent::HealthReservedPercentStat;
		Test.TestEqual(TEXT("health_reserved_percent is read: 20% of 1000 reserved leaves 800"),
					   UnreservedWith(Test, Stat, 20.0f), 800.0f, 0.01f);
		Test.TestEqual(TEXT("and nothing granted leaves 1000"),
					   UnreservedWith(Test, Stat, 0.0f), 1000.0f, 0.01f);
	}

	/**
	 * `skill_duration`, `buff_duration` and `debuff_duration`. Issue #1833, the
	 * durations. The first two are read by
	 * `UCataclysmSkillTemplate::OwnDurationMultiplier` on a Support self-buff
	 * skill, the third by `UCataclysmSkillEffects::DebuffDurationMultiplierOf`.
	 * Each grants 50% increased and asserts the answer is 1.5 against 1.
	 */
	UCataclysmSelfBuffSkill* SupportBuffOn(FScopedFighter& Fighter)
	{
		const FGameplayAbilitySpecHandle Handle = Fighter.AbilitySystem->GiveAbilityInSlot(
			UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Support,
			/*Level=*/1, Fighter.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid()
			? Fighter.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmSelfBuffSkill* Skill =
			Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Slot.Support"));
		}
		return Skill;
	}

	void GrantIncrease(FScopedFighter& Fighter, const TCHAR* Stat, float Percent)
	{
		FCataclysmStatModifier Increase;
		Increase.Bucket = ECataclysmStatBucket::Increased;
		Increase.Source = ECataclysmModifierSource::Enchantment;
		Increase.Value = Percent;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
		Line.Base = 0.0f;
		Line.Modifiers = {Increase};
		Fighter.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	}

	void ProbeSkillDuration(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		UCataclysmSelfBuffSkill* Skill = SupportBuffOn(Fighter);
		if (!Test.TestNotNull(TEXT("a Support buff"), Skill))
		{
			return;
		}
		Test.TestEqual(TEXT("nothing granted: 1"), Skill->OwnDurationMultiplier(false), 1.0f, 0.001f);
		GrantIncrease(Fighter, UCataclysmSkillTemplate::SkillDurationStat, 50.0f);
		Test.TestEqual(TEXT("skill_duration is read: 50% increased gives 1.5"),
					   Skill->OwnDurationMultiplier(false), 1.5f, 0.001f);
	}

	void ProbeBuffDuration(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		UCataclysmSelfBuffSkill* Skill = SupportBuffOn(Fighter);
		if (!Test.TestNotNull(TEXT("a Support buff"), Skill))
		{
			return;
		}
		GrantIncrease(Fighter, UCataclysmSkillTemplate::BuffDurationStat, 50.0f);
		Test.TestEqual(TEXT("buff_duration is read for a buff: 1.5"),
					   Skill->OwnDurationMultiplier(true), 1.5f, 0.001f);
		Test.TestEqual(TEXT("and not for the skill's other durations: 1"),
					   Skill->OwnDurationMultiplier(false), 1.0f, 0.001f);
	}

	void ProbeDebuffDuration(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		Test.TestEqual(TEXT("nothing granted: 1"),
					   UCataclysmSkillEffects::DebuffDurationMultiplierOf(Fighter.AbilitySystem),
					   1.0f, 0.001f);
		GrantIncrease(Fighter, UCataclysmSkillEffects::DebuffDurationStat, 50.0f);
		Test.TestEqual(TEXT("debuff_duration is read: 50% increased gives 1.5"),
					   UCataclysmSkillEffects::DebuffDurationMultiplierOf(Fighter.AbilitySystem),
					   1.5f, 0.001f);
	}

	/**
	 * `skill_range` and `projectile_speed`. Issue #1833, projectiles and range.
	 * Read by `UCataclysmSkillTemplate::ScaledRangeCm` and
	 * `ScaledProjectileSpeed` on a skill given to the fighter, stating ten
	 * metres and twenty metres a second. Each grants 50% increased.
	 */
	void ProbeSkillRange(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		UCataclysmSelfBuffSkill* Skill = SupportBuffOn(Fighter);
		if (!Test.TestNotNull(TEXT("a skill"), Skill))
		{
			return;
		}
		Skill->Params.RangeCm = 1000.0f;
		Test.TestEqual(TEXT("nothing granted: the stated 1000"), Skill->ScaledRangeCm(), 1000.0f, 0.01f);
		GrantIncrease(Fighter, UCataclysmSkillTemplate::SkillRangeStat, 50.0f);
		Test.TestEqual(TEXT("skill_range is read: 50% increased gives 1500"),
					   Skill->ScaledRangeCm(), 1500.0f, 0.01f);
	}

	void ProbeProjectileSpeed(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		UCataclysmSelfBuffSkill* Skill = SupportBuffOn(Fighter);
		if (!Test.TestNotNull(TEXT("a skill"), Skill))
		{
			return;
		}
		Skill->Params.SpeedCmPerSecond = 2000.0f;
		Test.TestEqual(TEXT("nothing granted: the stated 2000"),
					   Skill->ScaledProjectileSpeed(), 2000.0f, 0.01f);
		GrantIncrease(Fighter, UCataclysmSkillTemplate::ProjectileSpeedStat, 50.0f);
		Test.TestEqual(TEXT("projectile_speed is read: 50% increased gives 3000"),
					   Skill->ScaledProjectileSpeed(), 3000.0f, 0.01f);
	}

	/**
	 * `projectile_bounces` and `projectile_pierce_all`, read by `UCataclysmProjectileSkill::BouncesWithRows` and
	 * `PierceWithRows` where the skill fires. Ruled 2026-10-07.
	 *
	 * ONE CHARACTER at the origin holding a projectile skill that states one bounce and no pierce. Asked with
	 * nothing granted, each answers what the skill states. Granted 2 flat, the bounces are 3; granted the flag,
	 * the pierce is the 99 that stands for all.
	 */
	UCataclysmProjectileSkill* ProjectileSkillOn(FScopedSwinger& Holder)
	{
		const FGameplayAbilitySpecHandle Handle = Holder.AbilitySystem->GiveAbilityInSlot(
			UCataclysmProjectileSkill::StaticClass(), ECataclysmAbilitySlot::Special, /*Level=*/1, Holder.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid()
			? Holder.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmProjectileSkill* Skill =
			Spec ? Cast<UCataclysmProjectileSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Range=10; Radius=1; Speed=2000; Bounces=1"));
			Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Spell, Type.Projectile"));
		}
		return Skill;
	}

	void ProbeProjectileBounces(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedSwinger Holder(World, FVector::ZeroVector);
		const UCataclysmProjectileSkill* Skill = ProjectileSkillOn(Holder);
		if (!Test.TestNotNull(TEXT("a projectile skill"), Skill))
		{
			return;
		}
		Test.TestEqual(TEXT("nothing granted: the 1 bounce stated"), Skill->BouncesWithRows(), 1);
		GrantFlat(Holder.Actor, UCataclysmProjectileSkill::ProjectileBouncesStat, 2.0f);
		Test.TestEqual(TEXT("projectile_bounces is read: 2 granted gives 3"), Skill->BouncesWithRows(), 3);
	}

	void ProbeProjectilePierceAll(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedSwinger Holder(World, FVector::ZeroVector);
		const UCataclysmProjectileSkill* Skill = ProjectileSkillOn(Holder);
		if (!Test.TestNotNull(TEXT("a projectile skill"), Skill))
		{
			return;
		}
		Test.TestEqual(TEXT("nothing granted: the pierce of nought stated"), Skill->PierceWithRows(), 0);
		GrantFlat(Holder.Actor, UCataclysmProjectileSkill::ProjectilePierceAllStat, 1.0f);
		Test.TestEqual(TEXT("projectile_pierce_all is read: the flag gives the count that stands for all"),
					   Skill->PierceWithRows(), UCataclysmProjectileSkill::PierceAllCount);
	}

	/**
	 * `minion_range`, read by `ACataclysmMinion::Spawn` at the summoning on the
	 * reach and the notice radius the type row states. Issue #1833, "Gadgets
	 * have 20%-40% increased attack range". Two imps, one from a summoner
	 * granted 40% increased.
	 */
	void ProbeMinionRange(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter Geared(World, /*AttackDamage=*/0.0f);
		Grant(Geared.Actor, TEXT("minion_range"), IncreasePercent);

		ACataclysmMinion* PlainImp = SummonImp(Test, World, Plain.Actor);
		ACataclysmMinion* GearedImp = SummonImp(Test, World, Geared.Actor);
		if (!PlainImp || !GearedImp)
		{
			return;
		}
		ON_SCOPE_EXIT { if (IsValid(PlainImp)) { PlainImp->Destroy(); } };
		ON_SCOPE_EXIT { if (IsValid(GearedImp)) { GearedImp->Destroy(); } };

		const float Multiplier = 1.0f + IncreasePercent / 100.0f;
		Test.TestEqual(TEXT("minion_range is read: the reach is 40% longer"),
					   GearedImp->ReachCm, PlainImp->ReachCm * Multiplier, 0.01f);
		Test.TestEqual(TEXT("and the notice radius with it"),
					   GearedImp->NoticeRadiusCm, PlainImp->NoticeRadiusCm * Multiplier, 0.01f);
	}

	/**
	 * `critical_armor_penetration`, asked in `UCataclysmVitalAttributeSet` where
	 * armour penetration is and added in `UCataclysmDamageCalculation::Resolve`
	 * only once the blow has critically struck. Issue #1833, "Your critical
	 * strikes ignore 20%-40% of enemy armor".
	 *
	 * TWO ATTACKERS strike an armoured defender with the roll pinned so every
	 * blow critically strikes, at a multiplier of 100 so the crit changes no
	 * damage; one carries 100 flat of the stat. Its blow loses nothing to armour.
	 */
	void ProbeCriticalArmorPenetration(FAutomationTestBase& Test)
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
		Roll->Set(0.0f, ECVF_SetByConsole);
		ON_SCOPE_EXIT { Roll->Set(PreviousRoll, ECVF_SetByConsole); };

		FScopedFighter Plain(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Piercing(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		for (const FScopedFighter* Striker : {&Plain, &Piercing})
		{
			Striker->AbilitySystem->SetNumericAttributeBase(Combat::GetCritChanceAttribute(), 100.0f);
			Striker->AbilitySystem->SetNumericAttributeBase(Combat::GetCritMultiplierAttribute(), 100.0f);
		}
		Defender.AbilitySystem->SetNumericAttributeBase(Combat::GetArmorAttribute(), 1000.0f);

		FCataclysmStatModifier All;
		All.Bucket = ECataclysmStatBucket::Flat;
		All.Source = ECataclysmModifierSource::Enchantment;
		All.Value = 100.0f;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line =
			Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::CriticalArmorPenetrationStat));
		Line.Base = 0.0f;
		Line.Modifiers = {All};
		Piercing.AbilitySystem->SetStatInputs(MoveTemp(Inputs));

		FCataclysmDamageResult Kept;
		UCataclysmSkillEffects::ApplyHit(Plain.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), FCataclysmHitDelivery(), &Kept);
		FCataclysmDamageResult Ignored;
		UCataclysmSkillEffects::ApplyHit(Piercing.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), FCataclysmHitDelivery(), &Ignored);

		if (!Test.TestTrue(TEXT("both blows landed and critically struck, and armour took a share of the plain one"),
				Kept.bWasCritical && Ignored.bWasCritical && Kept.RemovedByArmour > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("critical_armor_penetration is read: the carrying attacker's critical blow loses nothing to armour"),
			Ignored.RemovedByArmour, 0.0f, 0.01f);
	}

	/** Replace a character's stat lines with one modifier on one stat, base nought. */
	void GrantOne(UCataclysmAbilitySystemComponent* System, const TCHAR* Stat,
				  ECataclysmStatBucket Bucket, float Value)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::Enchantment;
		Row.Value = Value;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
		Line.Base = 0.0f;
		Line.Modifiers = {Row};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/**
	 * `max_crit_chance`, read over the MaxCritChance attribute where a blow
	 * takes its critical strike chance. Issue #1833, "Your critical strike
	 * chance cannot exceed 30%-50%". Two attackers at 100% chance, the roll
	 * pinned at 50; one carries flat -70, a ceiling of 30, and does not
	 * critically strike.
	 */
	void ProbeMaxCritChance(FAutomationTestBase& Test)
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
		Roll->Set(50.0f, ECVF_SetByConsole);
		ON_SCOPE_EXIT { Roll->Set(PreviousRoll, ECVF_SetByConsole); };

		FScopedFighter Plain(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Capped(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		for (const FScopedFighter* Striker : {&Plain, &Capped})
		{
			Striker->AbilitySystem->SetNumericAttributeBase(Combat::GetCritChanceAttribute(), 100.0f);
			Striker->AbilitySystem->SetNumericAttributeBase(Combat::GetCritMultiplierAttribute(), 200.0f);
		}
		GrantOne(Capped.AbilitySystem, UCataclysmCombatAttributeSet::MaxCritChanceStat,
				 ECataclysmStatBucket::Flat, -70.0f);

		FCataclysmDamageResult Struck;
		UCataclysmSkillEffects::ApplyHit(Plain.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), FCataclysmHitDelivery(), &Struck);
		FCataclysmDamageResult Held;
		UCataclysmSkillEffects::ApplyHit(Capped.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), FCataclysmHitDelivery(), &Held);

		Test.TestTrue(TEXT("the plain attacker at 100% critically strikes on a roll of 50"),
					  Struck.bWasCritical);
		Test.TestFalse(TEXT("max_crit_chance is read: a ceiling of 30 does not critically strike on a roll of 50"),
					   Held.bWasCritical);
	}

	/**
	 * `energy_shield_recharge_ceiling_reduction`, read in `UCataclysmRegeneration::ApplyStep`.
	 * Issue #1833, "Your energy shield cannot recharge above 50% of its
	 * maximum". A shield of 400 of 1000 regenerating 1000 a second: one second
	 * fills it, and flat 50 stops it at 500.
	 */
	void ProbeEnergyShieldRechargeCeiling(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter Capped(World, /*AttackDamage=*/0.0f);
		for (const FScopedFighter* Each : {&Plain, &Capped})
		{
			Each->AbilitySystem->SetNumericAttributeBase(Vital::GetMaxEnergyShieldAttribute(), 1000.0f);
			Each->AbilitySystem->SetNumericAttributeBase(Vital::GetEnergyShieldAttribute(), 400.0f);
			Each->AbilitySystem->SetNumericAttributeBase(Vital::GetEnergyShieldRegenAttribute(), 1000.0f);
		}
		GrantOne(Capped.AbilitySystem, UCataclysmRegeneration::EnergyShieldRechargeCeilingReductionStat,
				 ECataclysmStatBucket::Flat, 50.0f);

		UCataclysmRegeneration::ApplyStep(Plain.Actor, 1.0f, 100.0f);
		UCataclysmRegeneration::ApplyStep(Capped.Actor, 1.0f, 100.0f);

		Test.TestEqual(TEXT("with no ceiling, one second fills the shield"),
					   Plain.AbilitySystem->GetNumericAttribute(Vital::GetEnergyShieldAttribute()),
					   1000.0f, 0.01f);
		Test.TestEqual(TEXT("energy_shield_recharge_ceiling_reduction is read: it stops at half, 500"),
					   Capped.AbilitySystem->GetNumericAttribute(Vital::GetEnergyShieldAttribute()),
					   500.0f, 0.01f);
	}

	/**
	 * `experience_gain`, read by `ACataclysmPlayerState::ExperienceAfterGain`,
	 * which the kill grant calls. Issue #1833, "Kills no longer generate any
	 * experience": removed, a kill worth 100 grants nothing.
	 */
	void ProbeExperienceGain(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmPlayerState* Plain = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerState* Barred = World->SpawnActor<ACataclysmPlayerState>();
		UCataclysmAbilitySystemComponent* BarredSystem = Barred
			? Cast<UCataclysmAbilitySystemComponent>(Barred->GetAbilitySystemComponent())
			: nullptr;
		if (!Test.TestNotNull(TEXT("a player state"), Plain)
			|| !Test.TestNotNull(TEXT("a second one with an ability system"), BarredSystem))
		{
			return;
		}
		// THE ACTOR INFORMATION A LOOKUP READS, as a possessed player's has.
		BarredSystem->InitAbilityActorInfo(Barred, Barred);
		GrantOne(BarredSystem, ACataclysmPlayerState::ExperienceGainStat,
				 ECataclysmStatBucket::Removed, 1.0f);

		Test.TestEqual(TEXT("a kill worth 100 grants 100 to a character without the row"),
					   Plain->ExperienceAfterGain(100), static_cast<int64>(100));
		Test.TestEqual(TEXT("experience_gain is read: removed, it grants nothing"),
					   Barred->ExperienceAfterGain(100), static_cast<int64>(0));
	}

	/**
	 * `mana_cost_as_maximum_mana_percent`, read in
	 * `UCataclysmGameplayAbility::ManaCostFor` with the skill's tags. Issue #1833,
	 * "Movement abilities cost 20%-50% of your maximum mana". 20 flat on a
	 * maximum of 1000 adds 200 to the skill's cost.
	 */
	void ProbeManaCostAsMaximumManaPercent(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		Fighter.AbilitySystem->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), 1000.0f);
		UCataclysmSelfBuffSkill* Skill = SupportBuffOn(Fighter);
		if (!Test.TestNotNull(TEXT("a skill"), Skill))
		{
			return;
		}
		const float Before = Skill->ManaCostFor(Fighter.AbilitySystem);
		GrantOne(Fighter.AbilitySystem, UCataclysmGameplayAbility::ManaCostAsMaximumManaPercentStat,
				 ECataclysmStatBucket::Flat, 20.0f);
		Test.TestEqual(TEXT("mana_cost_as_maximum_mana_percent is read: 20% of 1000 adds 200"),
					   Skill->ManaCostFor(Fighter.AbilitySystem), Before + 200.0f, 0.01f);
	}

	/**
	 * `class_resource_generation` is read by `UCataclysmAbilitySystemComponent::ClassResourceGainScaled` at each
	 * gain of class resource. Ruled 2026-10-07. A character granted 10 for a cast gains 10; carrying the stat at
	 * its base of 100 with 40% increased, the same cast grants 14.
	 */
	void ProbeClassResourceGeneration(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);

		// A CLASS RESOURCE SET FIRST, which the fighter does not carry, for the reason
		// `ProbeScaledMaximumClassResource` gives; and a maximum of 100 for the gain to fit under.
		Fighter.AbilitySystem->AddAttributeSetSubobject(
			NewObject<UCataclysmClassResourceAttributeSet>(Fighter.Actor));
		Fighter.AbilitySystem->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

		// THE SAME FIGHTER READ TWICE, from an empty pool each time: without the stat, then carrying it.
		const auto GainedForACast = [&Fighter, &Pool](bool bCarrying) -> float
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			Inputs.FindOrAdd(FName(UCataclysmFervour::PerCastStat)).Base = 10.0f;
			if (bCarrying)
			{
				FCataclysmStatModifier Faster;
				Faster.Bucket = ECataclysmStatBucket::Increased;
				Faster.Source = ECataclysmModifierSource::Enchantment;
				Faster.Value = 40.0f;
				FCataclysmStatInputs& Line = Inputs.FindOrAdd(
					FName(UCataclysmAbilitySystemComponent::ClassResourceGenerationStat));
				Line.Base = UCataclysmAbilitySystemComponent::NormalClassResourceGeneration;
				Line.Modifiers = {Faster};
			}
			Fighter.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
			Fighter.AbilitySystem->SetNumericAttributeBase(Pool, 0.0f);
			return UCataclysmFervour::GainForCast(Fighter.AbilitySystem);
		};
		const float Plain = GainedForACast(false);
		const float Carrying = GainedForACast(true);
		if (!Test.TestEqual(TEXT("set-up: without the stat a cast grants its 10"), Plain, 10.0f, 0.001f))
		{
			return;
		}
		Test.TestEqual(TEXT("class_resource_generation is read: 40% increased makes the same cast grant 14"),
					   Carrying, 14.0f, 0.001f);
	}

	/** Pins one console variable at the console's priority until it goes out of scope. */
	struct FPinnedRoll
	{
		FPinnedRoll(const TCHAR* Name, float Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}
		~FPinnedRoll()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/**
	 * What one blow from an attacker of a thousand takes from `Defender`, which
	 * blocks every blow: the block roll and the critical strike roll are pinned,
	 * and the defender's block chance is 100.
	 */
	float BlockedBlowOn(FAutomationTestBase& Test, UWorld* World, FScopedFighter& Defender,
		TMap<FName, FCataclysmStatInputs>&& DefenderLines)
	{
		FScopedFighter Attacker(World, /*AttackDamage=*/1000.0f);
		Defender.AbilitySystem->SetNumericAttributeBase(Combat::GetBlockChanceAttribute(), 100.0f);
		if (DefenderLines.Num() > 0)
		{
			Cast<UCataclysmAbilitySystemComponent>(Defender.AbilitySystem)
				->SetStatInputs(MoveTemp(DefenderLines));
		}
		FCataclysmDamageResult Result;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), FCataclysmHitDelivery(), &Result);
		Test.TestTrue(TEXT("the blow was blocked and did not critically strike"),
			Result.bBlocked && !Result.bWasCritical);
		return Result.DealtToHealth;
	}

	/**
	 * `block_damage_reduction` is read by `UCataclysmDamageCalculation::Resolve`
	 * at the block step, through `BlockShareOf`. Issue #1833 group E part 2. A
	 * defender carrying it at its base of 50 plus 25 keeps a quarter of a blocked
	 * blow, half what a plain defender keeps.
	 */
	void ProbeBlockDamageReduction(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedRoll Critical(TEXT("Cataclysm.CritRoll"), 100.0f);
		const FPinnedRoll Block(TEXT("Cataclysm.BlockRoll"), 0.0f);

		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter Carrying(World, /*AttackDamage=*/0.0f);
		FCataclysmStatModifier More;
		More.Bucket = ECataclysmStatBucket::Flat;
		More.Source = ECataclysmModifierSource::Enchantment;
		More.Value = 25.0f;
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Line =
			Lines.FindOrAdd(FName(UCataclysmDamageCalculation::BlockDamageReductionStat));
		Line.Base = UCataclysmDamageCalculation::BlockDamageReduction;
		Line.Modifiers = {More};

		const float Kept = BlockedBlowOn(Test, World, Plain, {});
		const float KeptCarrying = BlockedBlowOn(Test, World, Carrying, MoveTemp(Lines));
		if (!Test.TestTrue(TEXT("the plain blocked blow landed"), Kept > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("a block removing 75 keeps half what a block removing 50 keeps"),
			KeptCarrying, Kept * 0.5f, 0.01f);
	}

	/**
	 * What one blow carrying these tags, from `Attacker`, takes from `Defender`'s health. The critical strike roll is
	 * pinned by the caller. Ruled 2026-10-06, for the two chances a defender rolls against an incoming blow.
	 */
	float TaggedBlowOn(FScopedFighter& Attacker, FScopedFighter& Defender, const TCHAR* TagCell)
	{
		const float Before = Defender.Health();
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 UCataclysmSkillShapes::TagsFromCell(TagCell), FCataclysmHitDelivery());
		return Before - Defender.Health();
	}

	/** Gives a defender this much of one of the two chances, on a base of nothing. */
	void CarryAChance(FScopedFighter& Defender, const TCHAR* Stat, float Chance)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.FindOrAdd(FName(Stat)).Base = Chance;
		Defender.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	}

	/**
	 * `shield_absorbed_damage_added_to_next_attack_cap_percent`, read by
	 * `UCataclysmAbilitySystemComponent::NoteShieldAbsorbedDamage`. Ruled 2026-10-07. Two defenders with a shield of
	 * 40 take the same blow of 100; only the one carrying the stat stores what its shield absorbed.
	 */
	void ProbeShieldAbsorbedAdded(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedRoll Critical(TEXT("Cataclysm.CritRoll"), 100.0f);

		FScopedSwinger Attacker(World, FVector::ZeroVector);
		FScopedSwinger Plain(World, FVector(3 * M, 0, 0));
		FScopedSwinger Held(World, FVector(3 * M, 100 * M, 0));
		GrantFlats(Held.Actor,
			{{FName(UCataclysmAbilitySystemComponent::ShieldAbsorbedAddedCapStat), 100.0f}});
		for (FScopedSwinger* Defender : {&Plain, &Held})
		{
			Defender->Set(Vital::GetMaxEnergyShieldAttribute(), 1000.0f);
			Defender->Set(Vital::GetEnergyShieldAttribute(), 40.0f);
		}

		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Plain.Actor, 100.0f, FGameplayTagContainer());
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Held.Actor, 100.0f, FGameplayTagContainer());

		const float Absorbed = 40.0f - Held.Get(Vital::GetEnergyShieldAttribute());
		if (!Test.TestTrue(TEXT("set-up: the carrying defender's shield absorbed something of the blow"), Absorbed > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("a defender without shield_absorbed_damage_added_to_next_attack_cap_percent stores nothing"),
					   Plain.AbilitySystem->StoredShieldAbsorbedDamageNow(), 0.0f);
		Test.TestEqual(TEXT("and one carrying it stores what its shield absorbed, so NoteShieldAbsorbedDamage really reads it"),
					   Held.AbilitySystem->StoredShieldAbsorbedDamageNow(), Absorbed, 0.01f);
	}

	/**
	 * `spell_absorbed_damage_added_to_next_attack_cap_percent`, read by
	 * `UCataclysmAbilitySystemComponent::NoteSpellAbsorbedDamage`. Ruled 2026-10-07. Two defenders absorb the same
	 * spell, each on a chance of 100; only the one carrying the stat stores what the spell would have dealt.
	 */
	void ProbeSpellAbsorbedAdded(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedRoll Critical(TEXT("Cataclysm.CritRoll"), 100.0f);

		FScopedSwinger Attacker(World, FVector::ZeroVector);
		FScopedSwinger Plain(World, FVector(3 * M, 0, 0));
		FScopedSwinger Held(World, FVector(3 * M, 100 * M, 0));
		GrantFlats(Plain.Actor, {{FName(UCataclysmDamageCalculation::SpellAbsorbChanceStat), 100.0f}});
		GrantFlats(Held.Actor,
			{{FName(UCataclysmDamageCalculation::SpellAbsorbChanceStat), 100.0f},
			 {FName(UCataclysmAbilitySystemComponent::SpellAbsorbedAddedCapStat), 100.0f}});

		const FGameplayTagContainer Spell = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Spell"));
		const float HealthBefore = Held.Get(Vital::GetHealthAttribute());
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Plain.Actor, 100.0f, Spell);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Held.Actor, 100.0f, Spell);

		if (!Test.TestEqual(TEXT("set-up: the carrying defender absorbed the spell and took nothing"),
							Held.Get(Vital::GetHealthAttribute()), HealthBefore, 0.001f))
		{
			return;
		}
		Test.TestEqual(TEXT("a defender without spell_absorbed_damage_added_to_next_attack_cap_percent stores nothing of a spell it absorbs"),
					   Plain.AbilitySystem->StoredSpellAbsorbedDamageNow(), 0.0f);
		Test.TestTrue(TEXT("and one carrying it stores what the spell would have dealt, so NoteSpellAbsorbedDamage really reads it"),
					  Held.AbilitySystem->StoredSpellAbsorbedDamageNow() > 0.0f);
	}

	/**
	 * `spell_absorb_chance` is read by `UCataclysmDamageCalculation::SpellIsAbsorbed`, asked in the vital set beside
	 * the no-damage window. A defender carrying 100 of it takes nothing from a spell a plain defender is hurt by.
	 */
	void ProbeSpellAbsorbChance(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedRoll Critical(TEXT("Cataclysm.CritRoll"), 100.0f);
		FScopedFighter Attacker(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter Carrying(World, /*AttackDamage=*/0.0f);
		CarryAChance(Carrying, UCataclysmDamageCalculation::SpellAbsorbChanceStat, 100.0f);
		Test.TestTrue(TEXT("a spell hurts the plain defender"), TaggedBlowOn(Attacker, Plain, TEXT("Type.Spell")) > 0.0f);
		Test.TestEqual(TEXT("and takes nothing from the carrying one"),
			TaggedBlowOn(Attacker, Carrying, TEXT("Type.Spell")), 0.0f, 0.001f);
	}

	/**
	 * `melee_reflect_chance` is read by `UCataclysmDamageCalculation::MeleeIsReflected`, asked in the same place. A
	 * defender carrying 100 of it takes nothing from a melee blow, and the attacker is hurt.
	 */
	void ProbeMeleeReflectChance(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedRoll Critical(TEXT("Cataclysm.CritRoll"), 100.0f);
		FScopedFighter Attacker(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter Carrying(World, /*AttackDamage=*/0.0f);
		CarryAChance(Carrying, UCataclysmDamageCalculation::MeleeReflectChanceStat, 100.0f);
		Test.TestTrue(TEXT("a melee blow hurts the plain defender"),
			TaggedBlowOn(Attacker, Plain, TEXT("Type.Melee")) > 0.0f);
		const float AttackerBefore = Attacker.Health();
		Test.TestEqual(TEXT("and takes nothing from the carrying one"),
			TaggedBlowOn(Attacker, Carrying, TEXT("Type.Melee")), 0.0f, 0.001f);
		Test.TestTrue(TEXT("and the attacker is hurt by what came back"), AttackerBefore - Attacker.Health() > 0.0f);
	}

	/**
	 * `block_negation_chance` is read by `UCataclysmDamageCalculation::Resolve`
	 * inside the block step. Issue #1833 group E part 2. A defender carrying 100
	 * of it keeps nothing of a blocked blow, with the negation roll pinned at 0.
	 */
	void ProbeBlockNegationChance(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedRoll Critical(TEXT("Cataclysm.CritRoll"), 100.0f);
		const FPinnedRoll Block(TEXT("Cataclysm.BlockRoll"), 0.0f);
		const FPinnedRoll Negation(TEXT("Cataclysm.BlockNegationRoll"), 0.0f);

		FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
		FScopedFighter Carrying(World, /*AttackDamage=*/0.0f);
		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.FindOrAdd(FName(UCataclysmDamageCalculation::BlockNegationChanceStat)).Base = 100.0f;

		const float Kept = BlockedBlowOn(Test, World, Plain, {});
		const float KeptCarrying = BlockedBlowOn(Test, World, Carrying, MoveTemp(Lines));
		Test.TestTrue(*FString::Printf(TEXT("the plain blocked blow kept something: %.1f"), Kept),
			Kept > 0.0f);
		Test.TestEqual(TEXT("the carrying defender kept nothing"), KeptCarrying, 0.0f, 0.001f);
	}

	/**
	 * The More damage an ally `AllyMetres` along X from a player carrying
	 * `Value` of `Stat` holds after one shared-buff step. With `bWithBuff`, the
	 * player first casts an unscoped 4% More buff in the Support slot, with one
	 * enemy burning 3 m away so it grants that 4%. Issue #1833 group E part 4a.
	 */
	float SharedMoreCarried(FAutomationTestBase& Test, const TCHAR* Stat, float Value,
							float AllyMetres, bool bWithBuff)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return -1.0f;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		ACataclysmPlayerCharacter* Wearer = SpawnPotionHolder(World);
		ACataclysmEnemyCharacter* Ally = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(AllyMetres * 100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		UCataclysmAbilitySystemComponent* Mine = Wearer
			? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Wearer)) : nullptr;
		if (!Test.TestNotNull(TEXT("a wearer"), Mine) || !Test.TestNotNull(TEXT("an ally"), Ally))
		{
			return -1.0f;
		}
		Ally->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));

		if (bWithBuff)
		{
			ACataclysmEnemyCharacter* Alight = World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector(-300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
			if (!Test.TestNotNull(TEXT("something to set alight"), Alight))
			{
				return -1.0f;
			}
			Alight->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Alight->SetHealth(100000.0f);
			UCataclysmSkillEffects::ApplyBurn(Wearer, Alight, 100.0f,
				/*bScalesWithInstigator=*/true, /*bBurnIsDesigned=*/true);
			const FGameplayAbilitySpecHandle Handle = Mine->GiveAbilityInSlot(
				UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Support, /*Level=*/100, Wearer);
			FGameplayAbilitySpec* Spec = Mine->FindAbilitySpecFromHandle(Handle);
			UCataclysmSelfBuffSkill* Buff = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (!Test.TestNotNull(TEXT("a Support buff"), Buff))
			{
				return -1.0f;
			}
			Buff->SkillName = TEXT("Burning Wrath");
			Buff->Params = UCataclysmSkillShapes::ParseParams(
				TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning"));
			Mine->TryActivateAbility(Buff->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
			if (!Test.TestEqual(TEXT("set-up: the buff grants its caster 4% More"), Buff->GrantedIncrease, 4.0f))
			{
				return -1.0f;
			}
		}

		if (Value != 0.0f)
		{
			TMap<FName, FCataclysmStatInputs> Lines;
			Lines.FindOrAdd(FName(Stat)).Base = Value;
			Mine->SetStatInputs(MoveTemp(Lines));
		}
		UCataclysmSharedBuffs::Step(Wearer);

		float More = 0.0f;
		if (const UCataclysmAbilitySystemComponent* Its =
				Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Ally)))
		{
			for (const FCataclysmStatModifier& Modifier : Its->GetStatModifiers())
			{
				More += Modifier.Bucket == ECataclysmStatBucket::More ? Modifier.Value : 0.0f;
			}
		}
		return More;
	}

	/**
	 * `self_buff_shared_within_metres` is read by `UCataclysmSharedBuffs::Step`.
	 * Issue #1833 group E part 4a. With 8 of it, an ally 6 m from a player whose
	 * buff grants 4% More carries that 4%; without it, nothing.
	 */
	void ProbeSelfBuffShared(FAutomationTestBase& Test)
	{
		const TCHAR* Stat = UCataclysmAbilitySystemComponent::SelfBuffSharedWithinMetresStat;
		Test.TestEqual(TEXT("without the reach, the ally carries nothing"),
			SharedMoreCarried(Test, Stat, 0.0f, 6.0f, true), 0.0f, 0.001f);
		Test.TestEqual(TEXT("with 8 m of it, the ally at 6 m carries the buff's 4%"),
			SharedMoreCarried(Test, Stat, 8.0f, 6.0f, true), 4.0f, 0.001f);
	}

	/** The same for `support_buff_shared_within_metres`, at 15 m and an ally at 12 m. */
	void ProbeSupportBuffShared(FAutomationTestBase& Test)
	{
		const TCHAR* Stat = UCataclysmAbilitySystemComponent::SupportBuffSharedWithinMetresStat;
		Test.TestEqual(TEXT("without the reach, the ally carries nothing"),
			SharedMoreCarried(Test, Stat, 0.0f, 12.0f, true), 0.0f, 0.001f);
		Test.TestEqual(TEXT("with 15 m of it, the ally at 12 m carries the Support buff's 4%"),
			SharedMoreCarried(Test, Stat, 15.0f, 12.0f, true), 4.0f, 0.001f);
	}

	/** `nearby_allies_more_damage`: with 15 of it, an ally 3 m away carries 15% More. */
	void ProbeNearbyAlliesMoreDamage(FAutomationTestBase& Test)
	{
		const TCHAR* Stat = UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat;
		Test.TestEqual(TEXT("without it, the ally carries nothing"),
			SharedMoreCarried(Test, Stat, 0.0f, 3.0f, false), 0.0f, 0.001f);
		Test.TestEqual(TEXT("with 15 of it, the ally at 3 m carries 15"),
			SharedMoreCarried(Test, Stat, 15.0f, 3.0f, false), 15.0f, 0.001f);
	}

	/**
	 * `necrosis_kill_raises_imp_seconds` is read by
	 * `UCataclysmRisenImps::RiseOnNecrosisKill`. Issue #1833 group E part 4b.
	 * Asked with necrosis as what killed an enemy: nothing rises for a killer
	 * without it, and an imp lasting 7 seconds for one carrying 7.
	 */
	void ProbeNecrosisRiseSeconds(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Killer(World, /*AttackDamage=*/0.0f);
		FScopedFighter Victim(World, /*AttackDamage=*/0.0f);
		FGameplayTagContainer Killing;
		Killing.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Keyword.DoT.Necrosis")), /*ErrorIfNotFound=*/false));

		Test.TestNull(TEXT("without the stat nothing rises"),
			UCataclysmRisenImps::RiseOnNecrosisKill(
				Killer.Actor, Victim.Actor, FVector(300.0f, 0.0f, 0.0f), &Killing));

		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.FindOrAdd(FName(UCataclysmRisenImps::NecrosisRiseSecondsStat)).Base = 7.0f;
		Killer.AbilitySystem->SetStatInputs(MoveTemp(Lines));
		ACataclysmMinion* Risen = UCataclysmRisenImps::RiseOnNecrosisKill(
			Killer.Actor, Victim.Actor, FVector(300.0f, 0.0f, 0.0f), &Killing);
		if (!Test.TestNotNull(TEXT("with 7 of it an imp rises"), Risen))
		{
			return;
		}
		Test.TestEqual(TEXT("and lasts the 7 seconds"), Risen->GetLifeSpan(), 7.0f, 0.01f);
	}

	/**
	 * `minion_resummoned_after_seconds` is read by
	 * `UCataclysmSummonSkill::ScheduleResummon`. Issue #1833 group E part 4b. A
	 * summoner without it starts no wait for a lost minion; one carrying 4 does.
	 */
	void ProbeResummonedAfterSeconds(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Summoner(World, /*AttackDamage=*/0.0f);
		const FGameplayAbilitySpecHandle Handle = Summoner.AbilitySystem->GiveAbilityInSlot(
			UCataclysmSummonSkill::StaticClass(), ECataclysmAbilitySlot::Special, /*Level=*/1,
			Summoner.Actor);
		FGameplayAbilitySpec* Spec = Summoner.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UCataclysmSummonSkill* Skill =
			Spec ? Cast<UCataclysmSummonSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Test.TestNotNull(TEXT("a summon skill"), Skill))
		{
			return;
		}

		Test.TestFalse(TEXT("without the stat no wait is started"),
			UCataclysmSummonSkill::ScheduleResummon(Summoner.Actor, Skill));
		Test.TestEqual(TEXT("and none is held"), Summoner.AbilitySystem->PendingResummons.Num(), 0);

		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.FindOrAdd(FName(UCataclysmSummonSkill::ResummonedAfterSecondsStat)).Base = 4.0f;
		Summoner.AbilitySystem->SetStatInputs(MoveTemp(Lines));
		Test.TestTrue(TEXT("with 4 of it a wait is started"),
			UCataclysmSummonSkill::ScheduleResummon(Summoner.Actor, Skill));
		Test.TestEqual(TEXT("and one is held"), Summoner.AbilitySystem->PendingResummons.Num(), 1);
		Summoner.AbilitySystem->ClearPendingResummons();
	}

	/**
	 * `aura_shares_immunities_with_allies` is read by
	 * `UCataclysmAuraSkill::ShareImmunitiesWithAlliesInside`. Issue #1833 group E
	 * part 4c. An ally 2 m from the caster of an aura naming `Immune=Stun` is
	 * immune to a stun after a pulse when the caster carries the flag, and is
	 * not when it does not.
	 */
	bool AllyIsImmuneAfterAPulse(FAutomationTestBase& Test, bool bWithTheFlag)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedFighter Caster(World, /*AttackDamage=*/0.0f);
		Caster.AbilitySystem->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), 10000.0f);
		Caster.AbilitySystem->SetNumericAttributeBase(Vital::GetManaAttribute(), 10000.0f);
		ACataclysmEnemyCharacter* Ally = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("an ally"), Ally))
		{
			return false;
		}
		Ally->SetOwner(Caster.Actor);
		if (bWithTheFlag)
		{
			TMap<FName, FCataclysmStatInputs> Lines;
			Lines.FindOrAdd(FName(UCataclysmAbilitySystemComponent::AuraSharesImmunitiesStat)).Base = 1.0f;
			Caster.AbilitySystem->SetStatInputs(MoveTemp(Lines));
		}

		const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
			UCataclysmAuraSkill::StaticClass(), ECataclysmAbilitySlot::Aura, /*Level=*/1, Caster.Actor);
		FGameplayAbilitySpec* Spec = Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UCataclysmAuraSkill* Aura = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Test.TestNotNull(TEXT("an aura"), Aura))
		{
			return false;
		}
		Aura->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=4; Interval=1; Immune=Stun"));
		if (!Test.TestTrue(TEXT("set-up: the aura activates"),
				Caster.AbilitySystem->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false)))
		{
			return false;
		}
		Aura->Pulse();
		if (!Test.TestTrue(TEXT("set-up: the aura is still running after its pulse"), Aura->IsActive()))
		{
			return false;
		}
		return UCataclysmSkillTemplate::IsImmuneTo(Ally, TEXT("Stun"));
	}

	void ProbeAuraSharesImmunities(FAutomationTestBase& Test)
	{
		Test.TestFalse(TEXT("without the flag the ally inside is not immune"),
			AllyIsImmuneAfterAPulse(Test, /*bWithTheFlag=*/false));
		Test.TestTrue(TEXT("with it the ally inside is immune to what the aura names"),
			AllyIsImmuneAfterAPulse(Test, /*bWithTheFlag=*/true));
	}

	// ------------------------------------------------------------------------
	// A CONDITIONED ROW IS JUDGED WHERE THE STAT IS USED. Issue #1833, 2026-10-06.
	//
	// A gameplay attribute is worked out with every condition refused, so a row
	// under a condition reaches play only where the consuming code asks the stat
	// pipeline. Each probe below gives one fighter one modifier that holds below
	// half health, reads the engine's own answer at full health and at two
	// fifths, and asserts the two differ. The scaled probes above measure the same
	// ask for the stats they cover; these are the stats the shipped data
	// conditions that had no probe.
	// ------------------------------------------------------------------------

	/** One modifier on one stat that holds only while its fighter is below half health. */
	void ConditionedBelowHalf(FScopedFighter& Fighter, const TCHAR* Stat,
							  ECataclysmStatBucket Bucket, float Value, float Base)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::Enchantment;
		Row.Value = Value;
		Row.Condition = ECataclysmStatCondition::HealthBelowPercent;
		Row.ConditionValue = 50.0f;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
		Line.Base = Base;
		Line.Modifiers = {Row};
		Fighter.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	}

	/** Put a fighter at this share of its health. */
	void AtHealthShare(FScopedFighter& Fighter, float Share)
	{
		Fighter.AbilitySystem->SetNumericAttributeBase(
			Vital::GetHealthAttribute(), TargetHealthPool * Share);
	}

	/** `area_of_effect`, read by `UCataclysmSkillTemplate::AreaOfEffectMultiplier`. */
	void ProbeConditionedAreaOfEffect(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		UCataclysmSelfBuffSkill* Skill = SupportBuffOn(Fighter);
		if (!Test.TestNotNull(TEXT("a skill"), Skill))
		{
			return;
		}
		ConditionedBelowHalf(Fighter, TEXT("area_of_effect"), ECataclysmStatBucket::Increased, 50.0f, 100.0f);
		Test.TestEqual(TEXT("area_of_effect at full health: 1"), Skill->AreaOfEffectMultiplier(), 1.0f, 0.001f);
		AtHealthShare(Fighter, 0.4f);
		Test.TestEqual(TEXT("area_of_effect is asked for, so below half health 50% increased gives 1.5"),
					   Skill->AreaOfEffectMultiplier(), 1.5f, 0.001f);
	}

	/** `cooldown_reduction`, read by `UCataclysmGameplayAbility::CooldownAfterReduction`. */
	void ProbeConditionedCooldownReduction(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		ConditionedBelowHalf(Fighter, TEXT("cooldown_reduction"), ECataclysmStatBucket::Flat, 50.0f, 0.0f);
		const float Full = UCataclysmGameplayAbility::CooldownAfterReduction(Fighter.AbilitySystem, 4.0f);
		AtHealthShare(Fighter, 0.4f);
		const float Hurt = UCataclysmGameplayAbility::CooldownAfterReduction(Fighter.AbilitySystem, 4.0f);
		Test.TestEqual(TEXT("cooldown_reduction at full health: the stated four seconds"), Full, 4.0f, 0.001f);
		Test.TestTrue(FString::Printf(TEXT("cooldown_reduction is asked for, so below half health the cooldown is "
										   "shorter: %.3f against %.3f"), Hurt, Full),
					  Hurt < Full - 0.1f);
	}

	/** `dot_damage`, read by `UCataclysmSkillEffects::DamageOverTimeNumbers` when an effect is applied. */
	void ProbeConditionedDotDamage(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		ConditionedBelowHalf(Fighter, TEXT("dot_damage"), ECataclysmStatBucket::Increased, 50.0f, 100.0f);
		const FGameplayTagContainer Burn(UCataclysmSkillEffects::BurnTag());
		const float Full = UCataclysmSkillEffects::DamageOverTimeNumbers(
			Fighter.AbilitySystem, 100.0f, 5.0f, Burn).DamagePerTick;
		AtHealthShare(Fighter, 0.4f);
		const float Hurt = UCataclysmSkillEffects::DamageOverTimeNumbers(
			Fighter.AbilitySystem, 100.0f, 5.0f, Burn).DamagePerTick;
		if (!Test.TestTrue(TEXT("a tick is priced at something at full health"), Full > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("dot_damage is asked for, so a tick priced below half health is half again"),
					   Hurt / Full, 1.5f, 0.001f);
	}

	/**
	 * `damage_over_time_taken`, read by `UCataclysmDamageCalculation::Resolve` when a tick arrives.
	 *
	 * UNDER `while_moving` AND NOT BELOW HALF HEALTH, because the row this stands for is "you take more damage
	 * over time while moving". The same wearer is asked standing, moving and standing again, so the standing
	 * figure is the control on both sides.
	 */
	void ProbeConditionedDamageOverTimeTaken(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Wearer(World, /*AttackDamage=*/0.0f);
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::More;
		Row.Source = ECataclysmModifierSource::Enchantment;
		Row.Value = 100.0f;
		Row.Condition = ECataclysmStatCondition::WhileMoving;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::DamageOverTimeTakenStat));
		Line.Base = 100.0f;
		Line.Modifiers = {Row};
		Wearer.AbilitySystem->SetStatInputs(MoveTemp(Inputs));

		const FCataclysmIncomingHit Tick = TickOf(100.0f, UCataclysmSkillEffects::BurnTag());
		const auto Taken = [&Tick, &Wearer]()
		{
			return ResolveATick(Tick, Wearer.AbilitySystem).DealtToHealth;
		};
		Wearer.AbilitySystem->NoteDidNotMove();
		const float Standing = Taken();
		if (!Test.TestTrue(TEXT("damage_over_time_taken: a standing wearer takes something from a tick"),
						   Standing > 0.0f))
		{
			return;
		}
		Wearer.AbilitySystem->NoteMovedMetres(1.0f);
		Test.TestEqual(TEXT("damage_over_time_taken is asked for, so the same tick on the wearer moving is doubled"),
					   Taken(), Standing * 2.0f, 0.01f);
		Wearer.AbilitySystem->NoteDidNotMove();
		Test.TestEqual(TEXT("damage_over_time_taken: and standing again the tick is what it was"),
					   Taken(), Standing, 0.01f);
	}

	/** `mana_pool_becomes_health`, read by `UCataclysmSkillTemplate::ManaPoolBecomesHealth`. */
	void ProbeConditionedManaPoolBecomesHealth(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		ConditionedBelowHalf(Fighter, UCataclysmSkillTemplate::ManaPoolBecomesHealthStat,
							 ECataclysmStatBucket::Flat, 1.0f, 0.0f);
		Test.TestFalse(TEXT("mana_pool_becomes_health at full health: no"),
					   UCataclysmSkillTemplate::ManaPoolBecomesHealth(Fighter.AbilitySystem));
		AtHealthShare(Fighter, 0.4f);
		Test.TestTrue(TEXT("mana_pool_becomes_health is asked for, so below half health: yes"),
					  UCataclysmSkillTemplate::ManaPoolBecomesHealth(Fighter.AbilitySystem));
	}

	/** `block_chance`, read by `UCataclysmDamageCalculation::Resolve` at the block step. */
	void ProbeConditionedBlockChance(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		ConditionedBelowHalf(Defender, TEXT("block_chance"), ECataclysmStatBucket::Flat, 100.0f, 0.0f);
		FCataclysmIncomingHit Blow;
		Blow.Damage = 100.0f;
		const auto Blocks = [&Blow, &Defender]()
		{
			return UCataclysmDamageCalculation::Resolve(
				Blow, Defender.AbilitySystem, /*Tier=*/1, /*EvasionRoll=*/100.0f,
				/*BlockRoll=*/50.0f).bBlocked;
		};
		Test.TestFalse(TEXT("block_chance at full health: a roll of 50 is not blocked"), Blocks());
		AtHealthShare(Defender, 0.4f);
		Test.TestTrue(TEXT("block_chance is asked for, so below half health the same roll is blocked"), Blocks());
	}

	/**
	 * What one blow of 100% does to a defender, from a striker that never
	 * critically strikes unless `CritRoll` says so. The three probes below price a
	 * real hit, because that is where these three stats are asked for.
	 */
	FCataclysmDamageResult OneBlow(FScopedFighter& Striker, FScopedFighter& Defender)
	{
		FCataclysmDamageResult Result;
		UCataclysmSkillEffects::ApplyHit(Striker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), FCataclysmHitDelivery(), &Result);
		return Result;
	}

	/** Pin the critical strike roll for a probe's length. A roll of 100 never critically strikes. */
	struct FPinnedCritRoll
	{
		explicit FPinnedCritRoll(float Value)
			: Roll(IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.CritRoll")))
		{
			if (Roll)
			{
				Previous = Roll->GetFloat();
				Roll->Set(Value, ECVF_SetByConsole);
			}
		}
		~FPinnedCritRoll()
		{
			if (Roll)
			{
				Roll->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Roll = nullptr;
		float Previous = 0.0f;
	};

	/** `armor_penetration`, read where a hit is priced, with the striker's state. */
	void ProbeConditionedArmorPenetration(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedCritRoll NeverCritical(100.0f);
		FScopedFighter Full(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Hurt(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		Defender.AbilitySystem->SetNumericAttributeBase(Combat::GetArmorAttribute(), 1000.0f);
		for (FScopedFighter* Striker : {&Full, &Hurt})
		{
			ConditionedBelowHalf(*Striker, TEXT("armor_penetration"), ECataclysmStatBucket::Flat, 100.0f, 0.0f);
		}
		AtHealthShare(Hurt, 0.4f);
		const FCataclysmDamageResult AtFull = OneBlow(Full, Defender);
		const FCataclysmDamageResult WhenHurt = OneBlow(Hurt, Defender);
		if (!Test.TestTrue(TEXT("armour takes a share of the blow from the striker at full health"),
				AtFull.RemovedByArmour > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("armor_penetration is asked for, so the striker below half health loses nothing to armour"),
					   WhenHurt.RemovedByArmour, 0.0f, 0.01f);
	}

	/** `penetration`, read where a hit is priced, with the striker's state. */
	void ProbeConditionedPenetration(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedCritRoll NeverCritical(100.0f);
		FScopedFighter Full(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Hurt(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		Defender.AbilitySystem->SetNumericAttributeBase(
			UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 50.0f);
		for (FScopedFighter* Striker : {&Full, &Hurt})
		{
			ConditionedBelowHalf(*Striker, TEXT("penetration"), ECataclysmStatBucket::Flat, 100.0f, 0.0f);
		}
		AtHealthShare(Hurt, 0.4f);
		const float AtFull = OneBlow(Full, Defender).DealtToHealth;
		const float WhenHurt = OneBlow(Hurt, Defender).DealtToHealth;
		if (!Test.TestTrue(TEXT("the blow from the striker at full health lands for something"), AtFull > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("penetration is asked for, so the striker below half health meets no resistance "
							"and deals twice what fifty resistance let through"),
					   WhenHurt / AtFull, 2.0f, 0.01f);
	}

	/** `crit_multiplier`, read where a hit is priced, with the striker's state. */
	void ProbeConditionedCritMultiplier(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedCritRoll AlwaysCritical(0.0f);
		FScopedFighter Full(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Hurt(World, /*AttackDamage=*/1000.0f);
		FScopedFighter Defender(World, /*AttackDamage=*/0.0f);
		for (FScopedFighter* Striker : {&Full, &Hurt})
		{
			Striker->AbilitySystem->SetNumericAttributeBase(Combat::GetCritChanceAttribute(), 100.0f);
			ConditionedBelowHalf(*Striker, TEXT("crit_multiplier"), ECataclysmStatBucket::Flat, 100.0f, 150.0f);
		}
		AtHealthShare(Hurt, 0.4f);
		const FCataclysmDamageResult AtFull = OneBlow(Full, Defender);
		const FCataclysmDamageResult WhenHurt = OneBlow(Hurt, Defender);
		if (!Test.TestTrue(TEXT("both blows landed and critically struck"),
				AtFull.bWasCritical && WhenHurt.bWasCritical && AtFull.DealtToHealth > 0.0f))
		{
			return;
		}
		Test.TestTrue(FString::Printf(TEXT("crit_multiplier is asked for, so the striker below half health's "
										   "critical strike deals more: %.1f against %.1f"),
						  WhenHurt.DealtToHealth, AtFull.DealtToHealth),
					  WhenHurt.DealtToHealth > AtFull.DealtToHealth * 1.2f);
	}

	/**
	 * `skill_cost_paid_from_health`, read by `UCataclysmGameplayAbility::CostPool`.
	 * Issue #2228. Granted under `health_below` 50, as the one row that states it
	 * is: a cost comes out of mana at full health and out of health below half,
	 * and the mana pool's maximum is what it was either way.
	 */
	void ProbeCostPaidFromHealth(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedFighter Fighter(World, /*AttackDamage=*/0.0f);
		ConditionedBelowHalf(Fighter, UCataclysmGameplayAbility::CostPaidFromHealthStat,
							 ECataclysmStatBucket::Flat, 1.0f, 0.0f);
		Test.TestTrue(TEXT("skill_cost_paid_from_health at full health: a cost comes out of mana"),
					  UCataclysmGameplayAbility::CostPool(Fighter.AbilitySystem) == Vital::GetManaAttribute());
		AtHealthShare(Fighter, 0.4f);
		Test.TestTrue(TEXT("skill_cost_paid_from_health is asked for, so below half health a cost comes out of "
						   "health"),
					  UCataclysmGameplayAbility::CostPool(Fighter.AbilitySystem) == Vital::GetHealthAttribute());
		Test.TestFalse(TEXT("and it is not the stat that converts the mana pool"),
					   UCataclysmSkillTemplate::ManaPoolBecomesHealth(Fighter.AbilitySystem));
	}

	/**
	 * `skill_cost_paid_from_health_when_short`, read by
	 * `UCataclysmGameplayAbility::PoolPaying`. A character with no mana and 1,000
	 * health: a cost of 20 finds no pool to pay it, and holding the stat at 3
	 * health pays, and what is to be charged is 60.
	 */
	void ProbeCostPaidFromHealthWhenShort(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		float Charged = -1.0f;
		const auto Paying = [World, &Charged](bool bHeld)
		{
			FScopedFighter Caster(World, /*AttackDamage=*/0.0f);
			Caster.AbilitySystem->SetNumericAttributeBase(Vital::GetManaAttribute(), 0.0f);
			Caster.AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			Caster.AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
			if (bHeld)
			{
				GrantFlats(Caster.Actor,
					{{FName(UCataclysmGameplayAbility::CostPaidFromHealthWhenShortStat), 3.0f}});
			}
			return UCataclysmGameplayAbility::PoolPaying(Caster.AbilitySystem, 20.0f, &Charged);
		};

		Test.TestFalse(TEXT("with no mana, a plain caster finds nothing to pay 20 from"),
					   Paying(false).IsValid());
		Test.TestTrue(
			TEXT("and one holding skill_cost_paid_from_health_when_short pays it from health, "
				 "so UCataclysmGameplayAbility::PoolPaying really reads it"),
			Paying(true) == Vital::GetHealthAttribute());
		Test.TestEqual(TEXT("at three health for each point of mana: 60 for a cost of 20"),
					   Charged, 60.0f, 0.001f);
	}

	/**
	 * `healing_received`, read by `UCataclysmRegeneration::TopUp`. A character at
	 * 100 of 1,000 health offered a heal of 100 gains 100; one holding the stat
	 * at a flat 50 is offered 150 and gains that.
	 */
	void ProbeHealingReceived(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		const auto Gained = [World](bool bHeld)
		{
			FScopedFighter Healed(World, /*AttackDamage=*/0.0f);
			Healed.AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			Healed.AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 100.0f);
			if (bHeld)
			{
				GrantFlats(Healed.Actor, {{FName(UCataclysmRegeneration::HealingReceivedStat), 50.0f}});
			}
			UCataclysmRegeneration::TopUp(*Healed.AbilitySystem, Vital::GetHealthAttribute(),
										  Vital::GetMaxHealthAttribute(), 100.0f);
			return Healed.AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute()) - 100.0f;
		};

		Test.TestEqual(TEXT("a plain character offered a heal of 100 gains 100"), Gained(false), 100.0f, 0.01f);
		Test.TestEqual(TEXT("and one holding healing_received at a flat 50 gains 150, so "
							"UCataclysmRegeneration::TopUp really reads it"),
					   Gained(true), 150.0f, 0.01f);
	}

	/**
	 * `dot_application_refreshes_others`, read by `UCataclysmSkillEffects` where
	 * a damage over time effect is applied. A poison of ten seconds, four seconds
	 * in, has six left; when its applier then applies a bleed, it still has six,
	 * and ten when the applier holds the stat.
	 */
	void ProbeDotApplicationRefreshesOthers(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FGameplayTag Poison =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Poison")), /*ErrorIfNotFound=*/false);
		const FGameplayTag Bleed =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Bleed")), /*ErrorIfNotFound=*/false);
		if (!Test.TestTrue(TEXT("set-up: the two ailment tags"), Poison.IsValid() && Bleed.IsValid()))
		{
			return;
		}

		const auto PoisonLeft = [World, &Poison, &Bleed](bool bHeld)
		{
			FScopedFighter Applier(World, /*AttackDamage=*/0.0f);
			FScopedFighter Struck(World, /*AttackDamage=*/0.0f);
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Actor, Struck.Actor, 1.0f, 10.0f, Poison,
														/*bScalesWithInstigator=*/false);
			CataclysmTestWorld::RunClock(World, 4.0f);
			if (bHeld)
			{
				GrantFlats(Applier.Actor,
						   {{FName(UCataclysmSkillEffects::DotApplicationRefreshesOthersStat), 1.0f}});
			}
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Actor, Struck.Actor, 1.0f, 10.0f, Bleed,
														/*bScalesWithInstigator=*/false);
			UCataclysmSkillEffects::FRunningAilment Running;
			return UCataclysmSkillEffects::RunningAilmentOn(Struck.Actor, Poison, Running) ? Running.SecondsLeft
																						   : -1.0f;
		};

		Test.TestEqual(TEXT("a plain applier's poison has six seconds left after it applies a bleed"),
					   PoisonLeft(false), 6.0f, 0.1f);
		Test.TestEqual(TEXT("and ten when the applier holds dot_application_refreshes_others, so "
							"UCataclysmSkillEffects really reads it"),
					   PoisonLeft(true), 10.0f, 0.1f);
	}

	/**
	 * `overheal_absorb_percent_of_maximum_health`, read by `UCataclysmAbilitySystemComponent::NoteOverheal` where
	 * `UCataclysmRegeneration::TopUp` finds a heal of health did not all fit. Ruled 2026-10-07. Two characters at
	 * full health of 1,000 are each offered a heal of 100; only the one carrying the stat, at a flat 20, holds a
	 * temporary absorb afterwards, and it holds the 100 that did not fit.
	 *
	 * STANDING: the plain character at the origin, the carrying one 100 m along Y.
	 */
	void ProbeOverhealAbsorb(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger Held(World, FVector(0, 100 * M, 0));
		GrantFlats(Held.Actor,
			{{FName(UCataclysmAbilitySystemComponent::OverhealAbsorbCapStat), 20.0f}});
		for (FScopedSwinger* Healed : {&Plain, &Held})
		{
			Healed->Set(Vital::GetMaxHealthAttribute(), 1000.0f);
			Healed->Set(Vital::GetHealthAttribute(), 1000.0f);
			UCataclysmRegeneration::TopUp(*Healed->AbilitySystem, Vital::GetHealthAttribute(),
										  Vital::GetMaxHealthAttribute(), 100.0f);
		}

		Test.TestEqual(TEXT("a character without overheal_absorb_percent_of_maximum_health holds no temporary "
							"absorb after a heal at full health"),
					   Plain.AbilitySystem->TemporaryAbsorbHeld(), 0.0f);
		Test.TestEqual(TEXT("and one carrying it holds the 100 that did not fit, so NoteOverheal really reads it"),
					   Held.AbilitySystem->TemporaryAbsorbHeld(), 100.0f, 0.01f);
	}

	/**
	 * Grant a flat figure that reaches only a skill carrying the tags of this cell. For a stat asked with a skill's
	 * own tags, where `GrantFlat` above would reach every skill.
	 */
	void GrantFlatRequiring(AActor* Who, const TCHAR* Stat, float Value, const TCHAR* TagCell)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who));
		if (!System)
		{
			return;
		}

		FCataclysmStatModifier Scoped;
		Scoped.Bucket = ECataclysmStatBucket::Flat;
		Scoped.Source = ECataclysmModifierSource::Enchantment;
		Scoped.Value = Value;
		Scoped.RequiredTags = UCataclysmSkillShapes::TagsFromCell(TagCell);

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
		Line.Base = 0.0f;
		Line.Modifiers = {Scoped};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** What one blow took from a defender's energy shield and from its health. */
	struct FShieldAndHealthLost
	{
		float Shield = 0.0f;
		float Health = 0.0f;
	};

	/** One blow of 100% on a defender holding this much energy shield, read off the two attributes. */
	FShieldAndHealthLost BlowOnAShield(FScopedSwinger& Attacker, FScopedSwinger& Defender, float ShieldHeld)
	{
		if (ShieldHeld > 0.0f)
		{
			Defender.Set(Vital::GetMaxEnergyShieldAttribute(), ShieldHeld);
			Defender.Set(Vital::GetEnergyShieldAttribute(), ShieldHeld);
		}
		const float ShieldBefore = Defender.Get(Vital::GetEnergyShieldAttribute());
		const float HealthBefore = Defender.Get(Vital::GetHealthAttribute());
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		FShieldAndHealthLost Lost;
		Lost.Shield = ShieldBefore - Defender.Get(Vital::GetEnergyShieldAttribute());
		Lost.Health = HealthBefore - Defender.Get(Vital::GetHealthAttribute());
		return Lost;
	}

	/**
	 * `energy_shield_damage_taken` is the percent more a character's energy shield loses for each point of a blow
	 * it stops, read by `UCataclysmDamageCalculation::Resolve` at the energy shield step. Ruled 2026-10-07: "Your
	 * energy shield takes 30%-50% increased damage".
	 *
	 * ONE ATTACKER AT THE ORIGIN AND SIX DEFENDERS, three metres apart along X from three metres out. They are three
	 * pairs, a control and a wearer carrying the stat at 40: a pair holding a shield far larger than the blow, a pair
	 * holding seven tenths of the blow, and a pair holding none. Every figure is read against the control's.
	 */
	void ProbeEnergyShieldDamageTaken(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedCritRoll NeverCritical(100.0f);

		FScopedSwinger Attacker(World, FVector::ZeroVector);
		FScopedSwinger ControlWhole(World, FVector(3 * M, 0, 0));
		FScopedSwinger WearerWhole(World, FVector(6 * M, 0, 0));
		FScopedSwinger ControlShort(World, FVector(9 * M, 0, 0));
		FScopedSwinger WearerShort(World, FVector(12 * M, 0, 0));
		FScopedSwinger ControlBare(World, FVector(15 * M, 0, 0));
		FScopedSwinger WearerBare(World, FVector(18 * M, 0, 0));
		for (FScopedSwinger* Wearer : {&WearerWhole, &WearerShort, &WearerBare})
		{
			GrantFlat(Wearer->Actor, UCataclysmDamageCalculation::EnergyShieldDamageTakenStat, 40.0f);
		}

		// A SHIELD THE BLOW CANNOT EMPTY. The control's shield loses the whole blow and its health nothing.
		const FShieldAndHealthLost OnControlWhole = BlowOnAShield(Attacker, ControlWhole, 1000.0f);
		const FShieldAndHealthLost OnWearerWhole = BlowOnAShield(Attacker, WearerWhole, 1000.0f);
		const float Blow = OnControlWhole.Shield;
		if (!Test.TestTrue(TEXT("set-up: the control's shield absorbs the whole of a blow smaller than 700"),
						   Blow > 0.0f && Blow < 700.0f && FMath::IsNearlyZero(OnControlWhole.Health, 0.001f)))
		{
			return;
		}
		Test.TestEqual(TEXT("a wearer's shield loses 40% more than the control's for the same blow, so Resolve "
							"really reads energy_shield_damage_taken"),
					   OnWearerWhole.Shield, Blow * 1.4f, 0.01f);
		Test.TestEqual(TEXT("and the wearer's health takes what the control's took, which is nothing"),
					   OnWearerWhole.Health, OnControlWhole.Health, 0.001f);

		// A SHIELD OF SEVEN TENTHS OF THE BLOW. The control's stops seven tenths. The wearer's is emptied having
		// stopped 0.7 / 1.4, which is half, so half reaches health and never more than the blow held.
		const FShieldAndHealthLost OnControlShort = BlowOnAShield(Attacker, ControlShort, Blow * 0.7f);
		const FShieldAndHealthLost OnWearerShort = BlowOnAShield(Attacker, WearerShort, Blow * 0.7f);
		Test.TestEqual(TEXT("control: a shield of seven tenths of the blow is emptied"),
					   OnControlShort.Shield, Blow * 0.7f, 0.01f);
		Test.TestEqual(TEXT("control: and three tenths of the blow reach health"),
					   OnControlShort.Health, Blow * 0.3f, 0.01f);
		Test.TestEqual(TEXT("the wearer's shield of the same size is emptied too, losing no more than it held"),
					   OnWearerShort.Shield, OnControlShort.Shield, 0.01f);
		Test.TestEqual(TEXT("and it stopped only half the blow, so the other half reaches the wearer's health"),
					   OnWearerShort.Health, Blow * 0.5f, 0.01f);

		// NO SHIELD AT ALL. The stat is about the shield, so a wearer without one is unchanged.
		const FShieldAndHealthLost OnControlBare = BlowOnAShield(Attacker, ControlBare, 0.0f);
		const FShieldAndHealthLost OnWearerBare = BlowOnAShield(Attacker, WearerBare, 0.0f);
		if (Test.TestTrue(TEXT("set-up: with no shield the control's health takes the blow"),
						  OnControlBare.Health > 0.0f))
		{
			Test.TestEqual(TEXT("a wearer with no shield takes what the control takes"),
						   OnWearerBare.Health, OnControlBare.Health, 0.001f);
			Test.TestEqual(TEXT("and loses no shield, having none"), OnWearerBare.Shield, 0.0f, 0.001f);
		}
	}

	/** Health gained over this many quarter-second steps of the leech pay-out. */
	float LeechPaidOverSteps(FScopedSwinger& Who, int32 Steps)
	{
		const float Before = Who.Get(Vital::GetHealthAttribute());
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			UCataclysmLeech::PayOutStep(Who.Actor, 0.25f);
		}
		return Who.Get(Vital::GetHealthAttribute()) - Before;
	}

	/**
	 * `leech_payout_rate` is how fast a payment of leech arrives, read by `UCataclysmLeech::PayoutSecondsFor` where
	 * a payment is made. Ruled 2026-10-07: "50% less tick rate for your leech effects", read as the pay-out taking
	 * twice as long.
	 *
	 * TWO CHARACTERS A HUNDRED METRES APART, each at half health with 10% life leech, each told of one hit that
	 * took 1200. The second carries the stat at its base of 100 with a `more` of -50. Each is paid a quarter second
	 * at a time by calling the pay-out step, which is what the regeneration timer calls.
	 */
	void ProbeLeechPayoutRate(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger Slow(World, FVector(0, 100 * M, 0));
		for (FScopedSwinger* One : {&Plain, &Slow})
		{
			One->Set(Vital::GetLifeLeechAttribute(), 10.0f);
			One->Set(Vital::GetHealthAttribute(), TargetHealthPool * 0.5f);
		}
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			CarryOnAHundred(Inputs, UCataclysmLeech::PayoutRateStat, ECataclysmStatBucket::More, -50.0f);
			Slow.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		UCataclysmLeech::NoteHit(Plain.AbilitySystem, 1200.0f, FGameplayTagContainer());
		UCataclysmLeech::NoteHit(Slow.AbilitySystem, 1200.0f, FGameplayTagContainer());
		if (!Test.TestTrue(TEXT("set-up: each character is owed one payment of leech"),
						   Plain.AbilitySystem->GetLeechPayments().Num() == 1
							   && Slow.AbilitySystem->GetLeechPayments().Num() == 1))
		{
			return;
		}
		Test.TestEqual(TEXT("the payment made under the stat is the same size as the control's"),
					   Slow.AbilitySystem->GetLeechPayments()[0].Remaining,
					   Plain.AbilitySystem->GetLeechPayments()[0].Remaining, 0.001f);

		// THE FIRST QUARTER SECOND.
		const float PlainFirst = LeechPaidOverSteps(Plain, 1);
		const float SlowFirst = LeechPaidOverSteps(Slow, 1);
		if (!Test.TestTrue(TEXT("set-up: the control is paid something in the first step"), PlainFirst > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("a payment made under a more of -50 pays half as much in the first step, so "
							"PayoutSecondsFor really reads leech_payout_rate"),
					   SlowFirst, PlainFirst * 0.5f, 0.01f);

		// BY THREE SECONDS, eleven more steps: the control has been paid in full and owes nothing.
		const float PlainByThree = PlainFirst + LeechPaidOverSteps(Plain, 11);
		const float SlowByThree = SlowFirst + LeechPaidOverSteps(Slow, 11);
		Test.TestEqual(TEXT("control: after 3 seconds nothing is owed"),
					   Plain.AbilitySystem->GetLeechPayments().Num(), 0);
		Test.TestEqual(TEXT("under the stat half the control's total has arrived by 3 seconds"),
					   SlowByThree, PlainByThree * 0.5f, 0.01f);
		Test.TestEqual(TEXT("and the payment is still running"), Slow.AbilitySystem->GetLeechPayments().Num(), 1);

		// BY SIX SECONDS, twelve more: the same total, in twice the time.
		const float SlowBySix = SlowByThree + LeechPaidOverSteps(Slow, 12);
		Test.TestEqual(TEXT("and by 6 seconds the same total as the control was paid in 3"),
					   SlowBySix, PlainByThree, 0.01f);
		Test.TestEqual(TEXT("with nothing owed after it"), Slow.AbilitySystem->GetLeechPayments().Num(), 0);
		Test.TestEqual(TEXT("control: three more seconds pay the control nothing further"),
					   LeechPaidOverSteps(Plain, 12), 0.0f, 0.001f);
	}

	/**
	 * `strike_arc_at_least_degrees`, asked by `UCataclysmStrikeSkill::ArcAtLeastDegrees` with the skill's tags.
	 * Ruled 2026-10-07: "Your heavy attack hits all enemies in a 180 degree arc in front of you".
	 *
	 * TWO CHARACTERS A HUNDRED METRES APART, each with the same 60-degree melee strike tagged as the heavy attack.
	 * The second carries the stat at 180 on a line requiring `Slot.Heavy`. Then the second's strike is retagged as
	 * a strike that is not the heavy attack.
	 */
	void ProbeStrikeArcAtLeastDegrees(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger Carrying(World, FVector(0, 100 * M, 0));
		GrantFlatRequiring(Carrying.Actor, UCataclysmStrikeSkill::StrikeArcAtLeastDegreesStat, 180.0f,
						   TEXT("Slot.Heavy"));

		const auto HeavyStrike = [&Test](FScopedSwinger& Who) -> UCataclysmStrikeSkill*
		{
			const FGameplayAbilitySpecHandle Handle = Who.AbilitySystem->GiveAbilityInSlot(
				UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Heavy,
				/*Level=*/1, Who.Actor);
			FGameplayAbilitySpec* Spec = Who.AbilitySystem->FindAbilitySpecFromHandle(Handle);
			UCataclysmStrikeSkill* Skill =
				Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=3; Angle=60"));
				Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Melee, Slot.Heavy"));
			}
			Test.TestTrue(TEXT("set-up: a melee strike tagged as the heavy attack is granted"),
						  Skill && Skill->SkillTags.Num() == 2);
			return Skill;
		};
		UCataclysmStrikeSkill* PlainStrike = HeavyStrike(Plain);
		UCataclysmStrikeSkill* CarryingStrike = HeavyStrike(Carrying);
		if (!PlainStrike || !CarryingStrike)
		{
			return;
		}

		Test.TestEqual(TEXT("control: a heavy strike without strike_arc_at_least_degrees keeps its 60 degrees"),
					   PlainStrike->ArcDegrees(), 60.0f, 0.001f);
		Test.TestEqual(TEXT("and one carrying it at 180 is 180 degrees wide, so ArcAtLeastDegrees really reads it"),
					   CarryingStrike->ArcDegrees(), 180.0f, 0.001f);

		CarryingStrike->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Melee"));
		Test.TestEqual(TEXT("and the same character's strike that is not the heavy attack keeps its 60"),
					   CarryingStrike->ArcDegrees(), 60.0f, 0.001f);
	}

	/**
	 * `stagger_root_seconds`, read by `UCataclysmSkillEffects::ApplyStagger` from the staggering character once
	 * the stagger has landed. Ruled 2026-10-07: "Enemies you stagger are also briefly rooted for 0.5-1.5 seconds".
	 *
	 * TWO PAIRS A HUNDRED METRES APART, a staggerer at X = 0 and its target two metres along X. The second staggerer
	 * carries the stat at half a second. The root is read as the seconds left on the pin its target carries,
	 * against the seconds left on the stagger the same call laid, which runs for one second.
	 */
	void ProbeStaggerRootSeconds(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger PlainTarget(World, FVector(2 * M, 0, 0));
		FScopedSwinger Carrying(World, FVector(0, 100 * M, 0));
		FScopedSwinger CarryingTarget(World, FVector(2 * M, 100 * M, 0));
		GrantFlat(Carrying.Actor, UCataclysmSkillEffects::StaggerRootSecondsStat, 0.5f);

		const bool bPlainStaggered = UCataclysmSkillEffects::ApplyStagger(Plain.Actor, PlainTarget.Actor);
		const bool bCarryingStaggered = UCataclysmSkillEffects::ApplyStagger(Carrying.Actor, CarryingTarget.Actor);
		const float StaggerLeft =
			SecondsLeftOfTag(CarryingTarget.AbilitySystem, UCataclysmSkillEffects::StaggeredTag());
		if (!Test.TestTrue(TEXT("set-up: both staggers land, and the stagger has seconds to run"),
						   bPlainStaggered && bCarryingStaggered && StaggerLeft > 0.0f))
		{
			return;
		}
		Test.TestEqual(TEXT("control: a target staggered by a character without stagger_root_seconds is not pinned"),
					   SecondsLeftOfTag(PlainTarget.AbilitySystem, UCataclysmSkillEffects::PinnedTag()), 0.0f, 0.001f);
		Test.TestEqual(TEXT("and one staggered by a character carrying it at 0.5 is pinned for half as long as "
							"its one-second stagger, so ApplyStagger really reads it"),
					   SecondsLeftOfTag(CarryingTarget.AbilitySystem, UCataclysmSkillEffects::PinnedTag()),
					   StaggerLeft * 0.5f / UCataclysmSkillEffects::StaggerSeconds, 0.01f);
	}

	/**
	 * What one use of a Movement skill did, for the four stats a worn row states that only that skill reads.
	 * Ruled 2026-10-07.
	 */
	struct FMovementRiderRun
	{
		/** Whether the skill ran. An empty answer is also what a skill that did nothing gives. */
		bool bUsed = false;
		/** Where the user stood afterwards. */
		FVector ArrivedAt = FVector::ZeroVector;
		/** For a walked charge: the way it was walking, read before its first step. */
		FVector WalkingToward = FVector::ZeroVector;
		/** The health each enemy lost, in the order the enemies were given. */
		TArray<float> Lost;
		/** Where each enemy stood afterwards, in the same order. */
		TArray<FVector> EndedAt;
		/** What one plain hit of the asked per cent, with the skill's tags, took from the control enemy. */
		float PlainHitLost = 0.0f;
		/** Set-up: whether the enemy made immune to displacement answered that it was. */
		bool bTheImmuneOneWasImmune = false;
	};

	/**
	 * One use of one Movement skill by a user at the origin facing +X, among enemies at the places given.
	 *
	 * NO ACTOR IS SPAWNED WITHIN 2 M OF ANOTHER BY ANY CALLER. A control enemy stands 30 m to the side at
	 * (0, -30 m), outside everything, and takes one plain hit after the use when `PlainHitPercent` is above nought.
	 * The skill states its own hit as 100 per cent of weapon damage, so nothing here reads the slot table. The
	 * critical strike roll is pinned at 100, which never critically strikes. With no player controller the aimed
	 * point is the user's own facing at the skill's full range.
	 *
	 * `WalkTo` drives a walked charge by hand: the user is put at each point in turn and one step is taken there,
	 * because no timer fires in this world.
	 */
	FMovementRiderRun UseAMovementSkillAmong(const TCHAR* ParamsCell, const TCHAR* TagsCell,
		TFunctionRef<void(TMap<FName, FCataclysmStatInputs>&)> Carry, const TArray<FVector>& EnemiesAt,
		int32 ImmuneToDisplacement = INDEX_NONE, float PlainHitPercent = 0.0f,
		const TArray<FVector>& WalkTo = TArray<FVector>())
	{
		FMovementRiderRun Run;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!World)
		{
			return Run;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FPinnedRoll NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);

		FScopedSwinger User(World, FVector::ZeroVector);
		FScopedSwinger ControlEnemy(World, FVector(0.0f, -3000.0f, 0.0f));
		TArray<TUniquePtr<FScopedSwinger>> Enemies;
		for (const FVector& EnemyAt : EnemiesAt)
		{
			Enemies.Add(MakeUnique<FScopedSwinger>(World, EnemyAt));
		}
		if (Enemies.IsValidIndex(ImmuneToDisplacement))
		{
			Enemies[ImmuneToDisplacement]->AbilitySystem->GrantImmunity(FName(TEXT("Displacement")), User.Actor, 60.0f);
			Run.bTheImmuneOneWasImmune =
				UCataclysmSkillTemplate::IsImmuneTo(Enemies[ImmuneToDisplacement]->Actor, TEXT("Displacement"));
		}

		TMap<FName, FCataclysmStatInputs> Inputs;
		Carry(Inputs);
		if (Inputs.Num() > 0)
		{
			User.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		const FGameplayAbilitySpecHandle Handle = User.AbilitySystem->GiveAbilityInSlot(
			UCataclysmMovementSkill::StaticClass(), ECataclysmAbilitySlot::Movement, /*Level=*/100, User.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? User.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmMovementSkill* Skill = Spec ? Cast<UCataclysmMovementSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			return Run;
		}
		Skill->SkillName = TEXT("A movement skill a row may add to");
		Skill->Params = UCataclysmSkillShapes::ParseParams(ParamsCell);
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagsCell);
		Skill->DamagePercentOverride = 100.0f;

		TArray<float> HealthBefore;
		for (const TUniquePtr<FScopedSwinger>& Enemy : Enemies)
		{
			HealthBefore.Add(Enemy->Get(Vital::GetHealthAttribute()));
		}

		Run.bUsed = User.AbilitySystem->TryActivateAbility(Handle);
		Run.WalkingToward = UCataclysmMovementSkill::AdvanceDirectionFor(User.Actor);
		for (const FVector& Step : WalkTo)
		{
			User.Actor->SetActorLocation(Step);
			Skill->AdvanceOneStep();
		}
		Run.ArrivedAt = User.Actor->GetActorLocation();

		for (int32 Index = 0; Index < Enemies.Num(); ++Index)
		{
			Run.Lost.Add(HealthBefore[Index] - Enemies[Index]->Get(Vital::GetHealthAttribute()));
			Run.EndedAt.Add(Enemies[Index]->Actor->GetActorLocation());
		}
		if (PlainHitPercent > 0.0f)
		{
			const float ControlBefore = ControlEnemy.Get(Vital::GetHealthAttribute());
			UCataclysmSkillEffects::ApplyHit(User.Actor, ControlEnemy.Actor, PlainHitPercent, Skill->SkillTags);
			Run.PlainHitLost = ControlBefore - ControlEnemy.Get(Vital::GetHealthAttribute());
		}
		return Run;
	}

	/** The tags of a movement skill in the Movement slot, which is what each of the four rows is restricted to. */
	const TCHAR* const MovementRiderTags = TEXT("Item.Weapon.Wand, Element.Demonic, Slot.Movement");

	/**
	 * `movement_pulls_nearby_on_arrival` is read by a Movement skill where it arrived. A blink of 8 m with one enemy
	 * 3 m beyond where it lands: with no row the enemy stays; with the flag it ends 1.5 m from the user.
	 */
	void ProbeMovementPullsNearby(FAutomationTestBase& Test)
	{
		const TArray<FVector> OneEnemy = {FVector(1100.0f, 0.0f, 0.0f)};
		const FMovementRiderRun Plain = UseAMovementSkillAmong(TEXT("Mode=Blink; Range=8"), MovementRiderTags,
			[](TMap<FName, FCataclysmStatInputs>&) {}, OneEnemy);
		const FMovementRiderRun Carrying = UseAMovementSkillAmong(TEXT("Mode=Blink; Range=8"), MovementRiderTags,
			[](TMap<FName, FCataclysmStatInputs>& Inputs)
			{
				CarryFlat(Inputs, UCataclysmMovementSkill::PullsNearbyOnArrivalStat, 1.0f);
			}, OneEnemy);
		if (!Test.TestTrue(TEXT("both users blinked"), Plain.bUsed && Carrying.bUsed))
		{
			return;
		}
		Test.TestEqual(TEXT("with no row the enemy stays 3 m from where the blink arrived"),
					   static_cast<float>(FVector::Dist2D(Plain.EndedAt[0], Plain.ArrivedAt)), 300.0f, 1.0f);
		Test.TestEqual(TEXT("movement_pulls_nearby_on_arrival is read: the enemy ends 1.5 m from the user"),
					   static_cast<float>(FVector::Dist2D(Carrying.EndedAt[0], Carrying.ArrivedAt)), 150.0f, 1.0f);
	}

	/**
	 * `movement_path_damage_percent` is read by a charge for what its path crossed. One enemy stands on the line of
	 * an 8 m charge: a user carrying 75 takes more from it than a plain user's charge does.
	 */
	void ProbeMovementPathDamage(FAutomationTestBase& Test)
	{
		const TArray<FVector> OneEnemy = {FVector(300.0f, 0.0f, 0.0f)};
		const TCHAR* Charge = TEXT("Mode=Charge; Range=8; Radius=1.5");
		const FMovementRiderRun Plain = UseAMovementSkillAmong(Charge, MovementRiderTags,
			[](TMap<FName, FCataclysmStatInputs>&) {}, OneEnemy);
		const FMovementRiderRun Carrying = UseAMovementSkillAmong(Charge, MovementRiderTags,
			[](TMap<FName, FCataclysmStatInputs>& Inputs)
			{
				CarryFlat(Inputs, UCataclysmMovementSkill::PathDamagePercentStat, 75.0f);
			}, OneEnemy);
		if (!Test.TestTrue(TEXT("both users charged"), Plain.bUsed && Carrying.bUsed)
			|| !Test.TestTrue(TEXT("set-up: the plain charge's own blow hurt the enemy on its line"), Plain.Lost[0] > 0.0f))
		{
			return;
		}
		Test.TestTrue(TEXT("movement_path_damage_percent is read: the carrying user's charge takes more"),
					  Carrying.Lost[0] > Plain.Lost[0] + 1.0f);
	}

	/**
	 * `movement_random_direction` is read by a Movement skill that goes where the player pointed. With the roll
	 * pinned at 90 degrees a plain user's blink still goes 8 m along +X and a carrying user's goes 8 m along +Y.
	 */
	void ProbeMovementRandomDirection(FAutomationTestBase& Test)
	{
		const FPinnedRoll TowardY(TEXT("Cataclysm.MovementDirectionRoll"), 90.0f);
		const FMovementRiderRun Plain = UseAMovementSkillAmong(TEXT("Mode=Blink; Range=8"), MovementRiderTags,
			[](TMap<FName, FCataclysmStatInputs>&) {}, TArray<FVector>());
		const FMovementRiderRun Carrying = UseAMovementSkillAmong(TEXT("Mode=Blink; Range=8"), MovementRiderTags,
			[](TMap<FName, FCataclysmStatInputs>& Inputs)
			{
				CarryFlat(Inputs, UCataclysmMovementSkill::RandomDirectionStat, 1.0f);
			}, TArray<FVector>());
		if (!Test.TestTrue(TEXT("both users blinked"), Plain.bUsed && Carrying.bUsed))
		{
			return;
		}
		Test.TestTrue(TEXT("with no row the blink goes where it was aimed"),
					  Plain.ArrivedAt.Equals(FVector(800.0f, 0.0f, 0.0f), 1.0));
		Test.TestTrue(TEXT("movement_random_direction is read: the blink goes the rolled way instead"),
					  Carrying.ArrivedAt.Equals(FVector(0.0f, 800.0f, 0.0f), 1.0));
	}

	/**
	 * `movement_explodes_at_both_ends` is read by a Movement skill where it began and where it arrived. A blink
	 * that strikes nobody, with one enemy 2.8 m from where it began: untouched with no row, hurt with the flag.
	 */
	void ProbeMovementExplodesAtBothEnds(FAutomationTestBase& Test)
	{
		const TArray<FVector> OneEnemy = {FVector(0.0f, 280.0f, 0.0f)};
		const FMovementRiderRun Plain = UseAMovementSkillAmong(TEXT("Mode=Blink; Range=8"), MovementRiderTags,
			[](TMap<FName, FCataclysmStatInputs>&) {}, OneEnemy);
		const FMovementRiderRun Carrying = UseAMovementSkillAmong(TEXT("Mode=Blink; Range=8"), MovementRiderTags,
			[](TMap<FName, FCataclysmStatInputs>& Inputs)
			{
				CarryFlat(Inputs, UCataclysmMovementSkill::ExplodesAtBothEndsStat, 1.0f);
			}, OneEnemy);
		if (!Test.TestTrue(TEXT("both users blinked"), Plain.bUsed && Carrying.bUsed))
		{
			return;
		}
		Test.TestEqual(TEXT("with no row the enemy near where the blink began loses nothing"), Plain.Lost[0], 0.0f, 0.001f);
		Test.TestTrue(TEXT("movement_explodes_at_both_ends is read: under the flag it is hurt"), Carrying.Lost[0] > 1.0f);
	}

	/**
	 * `crowd_control_health_ceiling_reduction`, read by
	 * `UCataclysmSkillEffects::CrowdControlRefusedByHealthCeiling` where a stun, a knockdown, a fear or a
	 * displacement is applied. Ruled 2026-10-07: "You cannot apply CC effects to enemies above 50% HP". Two
	 * appliers, the second carrying the stat at a flat 50, each apply a designed stun to a target of their own
	 * at full health. The plain applier's target is stunned; the other's is not, and that application answers
	 * false. The carrying applier then stuns a third target at 40% health, so it is not refusing everything.
	 *
	 * STANDING: the plain applier at the origin and its target 2 m along X. The carrying applier 100 m along Y,
	 * its healthy target 2 m along X from it and its hurt target 2 m the other way.
	 */
	void ProbeCrowdControlHealthCeiling(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger PlainApplier(World, FVector::ZeroVector);
		FScopedSwinger PlainsTarget(World, FVector(2 * M, 0, 0));
		FScopedSwinger Carrier(World, FVector(0, 100 * M, 0));
		FScopedSwinger CarriersHealthy(World, FVector(2 * M, 100 * M, 0));
		FScopedSwinger CarriersHurt(World, FVector(-2 * M, 100 * M, 0));
		GrantFlat(Carrier.Actor, UCataclysmSkillEffects::CrowdControlHealthCeilingStat, 50.0f);
		CarriersHurt.Set(Vital::GetHealthAttribute(), TargetHealthPool * 0.4f);

		const bool bPlainApplied = UCataclysmSkillEffects::ApplyStun(
			PlainApplier.Actor, PlainsTarget.Actor, /*DurationSeconds=*/1.5f, /*DamageDealt=*/0.0f,
			/*bStunIsDesigned=*/true);
		const bool bCarrierApplied = UCataclysmSkillEffects::ApplyStun(
			Carrier.Actor, CarriersHealthy.Actor, /*DurationSeconds=*/1.5f, /*DamageDealt=*/0.0f,
			/*bStunIsDesigned=*/true);
		const bool bCarrierAppliedToHurt = UCataclysmSkillEffects::ApplyStun(
			Carrier.Actor, CarriersHurt.Actor, /*DurationSeconds=*/1.5f, /*DamageDealt=*/0.0f,
			/*bStunIsDesigned=*/true);
		Test.TestTrue(TEXT("control: a plain applier stuns a target at full health"),
					  bPlainApplied && UCataclysmSkillEffects::IsStunned(PlainsTarget.Actor));
		Test.TestFalse(TEXT("and one carrying crowd_control_health_ceiling_reduction at 50 does not, so "
							"CrowdControlRefusedByHealthCeiling really reads it"),
					   bCarrierApplied || UCataclysmSkillEffects::IsStunned(CarriersHealthy.Actor));
		Test.TestTrue(TEXT("and the same applier stuns a target at 40% health"),
					  bCarrierAppliedToHurt && UCataclysmSkillEffects::IsStunned(CarriersHurt.Actor));
	}

	/**
	 * `cannot_walk`, read by `UCataclysmSkillEffects::CannotWalkByARow`, which is what
	 * `ACataclysmPlayerController::PawnCannotWalk` asks for its fifth reason. Ruled 2026-10-08: "You cannot move
	 * while channeling any skill". Two characters, the second carrying the stat at a flat 1: the plain one is not
	 * forbidden to walk and the carrying one is.
	 *
	 * WHAT THIS DOES NOT REACH. That a player's step is then refused: `PawnCannotWalk` runs on a player controller
	 * and no automation test has one. This observes the function that controller calls, and no further.
	 *
	 * STANDING: the plain character at the origin, the carrying one 100 m along Y.
	 */
	void ProbeCannotWalk(FAutomationTestBase& Test)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		FScopedSwinger Plain(World, FVector::ZeroVector);
		FScopedSwinger Carrier(World, FVector(0, 100 * M, 0));
		GrantFlat(Carrier.Actor, UCataclysmSkillEffects::CannotWalkStat, 1.0f);

		Test.TestFalse(TEXT("control: a character with no row is not forbidden to walk"),
					   UCataclysmSkillEffects::CannotWalkByARow(Plain.Actor));
		Test.TestTrue(TEXT("and one carrying cannot_walk at 1 is, so CannotWalkByARow really reads it"),
					  UCataclysmSkillEffects::CannotWalkByARow(Carrier.Actor));
	}

	const TMap<FString, FProbe>& ConditionedProbes()
	{
		static const TMap<FString, FProbe> Made = {
			{TEXT("area_of_effect"),           &ProbeConditionedAreaOfEffect},
			{TEXT("armor_penetration"),        &ProbeConditionedArmorPenetration},
			{TEXT("block_chance"),             &ProbeConditionedBlockChance},
			{TEXT("cooldown_reduction"),       &ProbeConditionedCooldownReduction},
			{TEXT("crit_multiplier"),          &ProbeConditionedCritMultiplier},
			{TEXT("damage_over_time_taken"),   &ProbeConditionedDamageOverTimeTaken},
			{TEXT("dot_damage"),               &ProbeConditionedDotDamage},
			{TEXT("mana_pool_becomes_health"), &ProbeConditionedManaPoolBecomesHealth},
			{TEXT("penetration"),              &ProbeConditionedPenetration},
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
			{TEXT("skill_cost_paid_from_health"), &ProbeCostPaidFromHealth},
			{TEXT("skill_cost_paid_from_health_when_short"), &ProbeCostPaidFromHealthWhenShort},
			{TEXT("healing_received"), &ProbeHealingReceived},
			{TEXT("dot_application_refreshes_others"), &ProbeDotApplicationRefreshesOthers},
			{TEXT("applied_cripple_and_weaken_held_within_metres"), &ProbeAppliedHeldNearby},
			{TEXT("mitigated_damage_added_to_next_melee_cap_percent"), &ProbeMitigatedAdded},
			{TEXT("shield_absorbed_damage_added_to_next_attack_cap_percent"), &ProbeShieldAbsorbedAdded},
			{TEXT("spell_absorbed_damage_added_to_next_attack_cap_percent"), &ProbeSpellAbsorbedAdded},
			{TEXT("minion_energy_shield_percent_of_yours"), &ProbeSharedBlood},
			{TEXT("minions_repeat_your_skills"), &ProbeChorus},
			{TEXT("two_handed_weapon_in_each_hand"), &ProbeBothHandsFull},
			{TEXT("melee_kill_repeats_attack_every_seconds"), &ProbeFollowThrough},
			{TEXT("enemies_cannot_move_away_within_metres"), &ProbeNowhereToRun},
			{TEXT("moving_into_enemy_pushes_aside"), &ProbeShoulderThrough},
			{TEXT("knockback_suppressed"), &ProbeSetStance},
			{TEXT("block_damage_reduction"), &ProbeBlockDamageReduction},
			{TEXT("block_negation_chance"), &ProbeBlockNegationChance},
			{TEXT("spell_absorb_chance"), &ProbeSpellAbsorbChance},
			{TEXT("melee_reflect_chance"), &ProbeMeleeReflectChance},
			{TEXT("self_buff_shared_within_metres"), &ProbeSelfBuffShared},
			{TEXT("support_buff_shared_within_metres"), &ProbeSupportBuffShared},
			{TEXT("nearby_allies_more_damage"), &ProbeNearbyAlliesMoreDamage},
			{TEXT("necrosis_kill_raises_imp_seconds"), &ProbeNecrosisRiseSeconds},
			{TEXT("minion_resummoned_after_seconds"), &ProbeResummonedAfterSeconds},
			{TEXT("aura_shares_immunities_with_allies"), &ProbeAuraSharesImmunities},
			{TEXT("potions_forbidden"), &ProbePotionsForbidden},
			{TEXT("curse_death_raises_imp"), &ProbeCurseDeathRaisesImp},
			{TEXT("melee_arc_full_circle"), &ProbeMeleeArcFullCircle},
			{TEXT("potion_kill_charges_less_percent"), &ProbePotionKillChargesLess},
			{TEXT("potion_heal_less_percent_per_drink"), &ProbePotionHealLessPerDrink},
			{TEXT("minion_held_longest_becomes_your_equal"), &ProbeSecondSelf},
			{TEXT("enemies_near_slowed_within_metres"), &ProbeGroundDownMetres},
			{TEXT("enemies_near_slowed_percent"), &ProbeGroundDownPercent},
			{TEXT("third_melee_hit_armour_removed_percent"), &ProbeRendPercent},
			{TEXT("third_melee_hit_armour_removed_seconds"), &ProbeRendSeconds},
			{TEXT("mana_on_hit"),         &ProbeManaOnHit},
			{TEXT("mana_cost"),           &ProbeManaCost},
			{TEXT("cooldown_lengthening"), &ProbeCooldownLengthening},
			{TEXT("resistance_cap"), &ProbeResistanceCap},
			{TEXT("skill_charges_bonus"), &ProbeSkillCharges},
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
			{TEXT("persistent_area_duration"), &ProbePersistentAreaDuration},
			{TEXT("zone_damage_per_enemy_inside"), &ProbeZoneDamagePerEnemyInside},
			{TEXT("zone_slow_percent"), &ProbeZoneSlowPercent},
			{TEXT("only_one_persistent_area"), &ProbeOnlyOnePersistentArea},
			{TEXT("zone_staggers_on_entry"), &ProbeZoneStaggersOnEntry},
			{TEXT("zone_applies_own_ailment"), &ProbeZoneAppliesOwnAilment},
			{TEXT("zone_at_start_and_end_seconds"), &ProbeZoneAtStartAndEndSeconds},
			{TEXT("zone_at_impact_seconds"), &ProbeZoneAtImpactSeconds},
			{TEXT("movement_pulls_nearby_on_arrival"), &ProbeMovementPullsNearby},
			{TEXT("movement_path_damage_percent"), &ProbeMovementPathDamage},
			{TEXT("movement_random_direction"), &ProbeMovementRandomDirection},
			{TEXT("movement_explodes_at_both_ends"), &ProbeMovementExplodesAtBothEnds},
			{TEXT("zone_damages_its_owner"), &ProbeZoneDamagesItsOwner},
			{TEXT("zone_applies_effects_to_owner"), &ProbeZoneAppliesEffectsToOwner},
			{TEXT("zone_follows_owner_percent"), &ProbeZoneFollowsOwnerPercent},
			{TEXT("minions_leave_chaos_pools"), &ProbeMinionsLeaveChaosPools},
			{TEXT("ailment_immunity"), &ProbeAilmentImmunity},
			{TEXT("bleed_damage_taken_from_energy_shield"), &ProbeBleedDamageTakenFromEnergyShield},
			{TEXT("damage_over_time_taken_from_mana_first"), &ProbeDamageOverTimeTakenFromManaFirst},
			{TEXT("health_reserved"), &ProbeHealthReserved},
			{TEXT("health_reserved_percent"), &ProbeHealthReservedPercent},
			{TEXT("skill_duration"), &ProbeSkillDuration},
			{TEXT("buff_duration"), &ProbeBuffDuration},
			{TEXT("debuff_duration"), &ProbeDebuffDuration},
			{TEXT("skill_range"), &ProbeSkillRange},
			{TEXT("projectile_speed"), &ProbeProjectileSpeed},
			{TEXT("projectile_bounces"), &ProbeProjectileBounces},
			{TEXT("projectile_pierce_all"), &ProbeProjectilePierceAll},
			{TEXT("minion_range"), &ProbeMinionRange},
			{TEXT("critical_armor_penetration"), &ProbeCriticalArmorPenetration},
			{TEXT("max_crit_chance"), &ProbeMaxCritChance},
			{TEXT("energy_shield_recharge_ceiling_reduction"), &ProbeEnergyShieldRechargeCeiling},
			{TEXT("experience_gain"), &ProbeExperienceGain},
			{TEXT("mana_cost_as_maximum_mana_percent"), &ProbeManaCostAsMaximumManaPercent},
			{TEXT("class_resource_generation"), &ProbeClassResourceGeneration},
			{TEXT("overheal_absorb_percent_of_maximum_health"), &ProbeOverhealAbsorb},
			{TEXT("energy_shield_damage_taken"), &ProbeEnergyShieldDamageTaken},
			{TEXT("leech_payout_rate"), &ProbeLeechPayoutRate},
			{TEXT("strike_arc_at_least_degrees"), &ProbeStrikeArcAtLeastDegrees},
			{TEXT("stagger_root_seconds"), &ProbeStaggerRootSeconds},
			{TEXT("crowd_control_health_ceiling_reduction"), &ProbeCrowdControlHealthCeiling},
			{TEXT("cannot_walk"), &ProbeCannotWalk},
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEveryConditionedProbeTest,
	"Cataclysm.StatExemption.AConditionedRowIsJudgedWhereEachOfTheseStatsIsUsed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A row under a condition reaches play only where the code that uses its stat
 * asks the stat pipeline. Issue #1833, 2026-10-06.
 *
 * WHY THIS EXISTS. A gameplay attribute is worked out with every condition
 * refused, so a conditioned row on a stat whose consumer reads the attribute is
 * accepted, built, imported and dead. A resistance row under `health_below`
 * would have been one on 2026-10-05. `tools/generate_datatables.py` now refuses
 * a conditioned row on a stat outside `CONDITIONED_STATS_WITH_AN_ASKER`, and
 * the promise behind each name on that list can only be measured here.
 *
 * THESE EIGHT ARE THE STATS THE SHIPPED DATA CONDITIONS THAT NO OTHER PROBE
 * COVERED. The scaled probes above measure the same ask for the others: a scale
 * and a condition are both judged against the state handed over when the stat
 * is asked for. `tools/tests/test_every_conditioned_stat_has_an_asker.py` holds
 * the generator's list to the three probe tables in this file.
 *
 * EVERY PROBE RUNS, WHATEVER THE DATA HOLDS, so a consumer changed to read its
 * attribute fails here by name.
 */
bool FCataclysmEveryConditionedProbeTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	if (!TestEqual(TEXT("the nine probes are all here"), ConditionedProbes().Num(), 9))
	{
		return false;
	}
	for (const TPair<FString, FProbe>& Probe : ConditionedProbes())
	{
		Probe.Value(*this);
	}
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

// THE TWO CHANCES A DEFENDER ROLLS AGAINST AN INCOMING BLOW. Ruled 2026-10-06: "Spells that hit you have a 15%-30%
// chance to be absorbed dealing no damage" and "Melee attacks that hit you have a 10%-20% chance to be reflected back as
// retaliation damage". Issue #1833.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellAbsorbChanceTest,
	"Cataclysm.StatExemption.ASpellIsAbsorbedOnItsRollAndABlowThatIsNotASpellNever",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSpellAbsorbChanceTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FPinnedRoll Critical(TEXT("Cataclysm.CritRoll"), 100.0f);
	FPinnedRoll Absorb(TEXT("Cataclysm.SpellAbsorbRoll"), 29.0f);
	if (!TestNotNull(TEXT("set-up: the absorb roll can be pinned"), Absorb.Variable))
	{
		return false;
	}
	FScopedFighter Attacker(World, /*AttackDamage=*/1000.0f);
	FScopedFighter Plain(World, /*AttackDamage=*/0.0f);
	FScopedFighter Carrying(World, /*AttackDamage=*/0.0f);
	CarryAChance(Carrying, UCataclysmDamageCalculation::SpellAbsorbChanceStat, 30.0f);

	const float Whole = TaggedBlowOn(Attacker, Plain, TEXT("Type.Spell"));
	if (!TestTrue(TEXT("control: a spell hurts a defender carrying none of the chance"), Whole > 0.0f))
	{
		return false;
	}

	// A ROLL BELOW THE CHANCE ABSORBS THE SPELL.
	if (!TestEqual(TEXT("a roll of 29 against a chance of 30: the spell deals nothing"),
				   TaggedBlowOn(Attacker, Carrying, TEXT("Type.Spell")), 0.0f, 0.001f))
	{
		return false;
	}
	// A ROLL AT THE CHANCE DOES NOT.
	Absorb.Variable->Set(30.0f, ECVF_SetByConsole);
	TestEqual(TEXT("a roll of 30 against a chance of 30: the spell deals the whole"),
			  TaggedBlowOn(Attacker, Carrying, TEXT("Type.Spell")), Whole, 0.01f);
	// A BLOW THAT IS NOT A SPELL IS NEVER ABSORBED, whatever the roll.
	Absorb.Variable->Set(0.0f, ECVF_SetByConsole);
	TestTrue(TEXT("a melee blow is not absorbed"), TaggedBlowOn(Attacker, Carrying, TEXT("Type.Melee")) > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMeleeReflectChanceTest,
	"Cataclysm.StatExemption.AReflectedMeleeHitIsNotTakenAndIsPaidBackWholeAsRetaliationIsPriced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMeleeReflectChanceTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FPinnedRoll Critical(TEXT("Cataclysm.CritRoll"), 100.0f);
	FPinnedRoll Reflect(TEXT("Cataclysm.MeleeReflectRoll"), 0.0f);
	if (!TestNotNull(TEXT("set-up: the reflect roll can be pinned"), Reflect.Variable))
	{
		return false;
	}
	FScopedFighter Attacker(World, /*AttackDamage=*/1000.0f);

	// WHAT RETALIATION AT 100 PER CENT SENDS BACK, which is the price a reflected hit is paid at.
	FScopedFighter Retaliating(World, /*AttackDamage=*/0.0f);
	Retaliating.AbilitySystem->SetNumericAttributeBase(Combat::GetRetaliationAttribute(), 100.0f);
	float AttackerBefore = Attacker.Health();
	const float TakenByTheRetaliator = TaggedBlowOn(Attacker, Retaliating, TEXT("Type.Melee"));
	const float SentBackByRetaliation = AttackerBefore - Attacker.Health();
	if (!TestTrue(TEXT("control: a retaliating defender is hurt and sends something back"),
				  TakenByTheRetaliator > 0.0f && SentBackByRetaliation > 0.0f))
	{
		return false;
	}

	// A REFLECTED HIT: THE DEFENDER TAKES NONE OF IT, AND THE WHOLE OF IT GOES BACK AT THAT PRICE.
	FScopedFighter Reflecting(World, /*AttackDamage=*/0.0f);
	CarryAChance(Reflecting, UCataclysmDamageCalculation::MeleeReflectChanceStat, 20.0f);
	AttackerBefore = Attacker.Health();
	if (!TestEqual(TEXT("the reflecting defender takes none of the melee hit"),
				   TaggedBlowOn(Attacker, Reflecting, TEXT("Type.Melee")), 0.0f, 0.001f))
	{
		return false;
	}
	const float SentBackByTheReflection = AttackerBefore - Attacker.Health();
	TestEqual(TEXT("and sends back what retaliation at 100 per cent sends"), SentBackByTheReflection,
			  SentBackByRetaliation, 0.01f);

	// A ROLL AT THE CHANCE DOES NOT REFLECT, and a spell is never reflected.
	Reflect.Variable->Set(20.0f, ECVF_SetByConsole);
	AttackerBefore = Attacker.Health();
	TestTrue(TEXT("a roll of 20 against a chance of 20: the hit is taken"),
			 TaggedBlowOn(Attacker, Reflecting, TEXT("Type.Melee")) > 0.0f);
	TestEqual(TEXT("and nothing goes back"), AttackerBefore - Attacker.Health(), 0.0f, 0.001f);
	Reflect.Variable->Set(0.0f, ECVF_SetByConsole);
	AttackerBefore = Attacker.Health();
	TestTrue(TEXT("a spell is not reflected"), TaggedBlowOn(Attacker, Reflecting, TEXT("Type.Spell")) > 0.0f);
	TestEqual(TEXT("and nothing goes back for it"), AttackerBefore - Attacker.Health(), 0.0f, 0.001f);

	// A DEFENDER THAT ALSO RETALIATES PAYS ONCE FOR A REFLECTED HIT, not once for each.
	FScopedFighter Both(World, /*AttackDamage=*/0.0f);
	Both.AbilitySystem->SetNumericAttributeBase(Combat::GetRetaliationAttribute(), 100.0f);
	CarryAChance(Both, UCataclysmDamageCalculation::MeleeReflectChanceStat, 20.0f);
	AttackerBefore = Attacker.Health();
	TaggedBlowOn(Attacker, Both, TEXT("Type.Melee"));
	TestEqual(TEXT("a defender that reflects and retaliates sends back the reflection alone"),
			  AttackerBefore - Attacker.Health(), SentBackByTheReflection, 0.01f);
	return true;
}

// TERRAIN'S HALF OF TWO OF THE ZONE STATS. Ruled 2026-10-06: "persistent AOE effects" are ground zones and terrain, so
// `persistent_area_duration` and `only_one_persistent_area` reach a terrain piece too, and the change that added them
// tested only the ground zone's half. Issue #1833.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTerrainReadsTheZoneStatsTest,
	"Cataclysm.StatExemption.ATerrainPieceLastsLessWithTheDurationStatAndANewOneEndsAnEarlierOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTerrainReadsTheZoneStatsTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	/** What two strikes that each raise a wall leave: how long the first lasts, and how many stand after each. */
	struct FRead
	{
		bool bMade = false;
		float FirstLastsSeconds = -1.0f;
		int32 AfterOne = -1;
		int32 AfterTwo = -1;
	};
	const auto TwoWalls = [](TFunctionRef<void(TMap<FName, FCataclysmStatInputs>&)> Carry) -> FRead
	{
		FRead Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!World)
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScopedSwinger Caster(World, FVector::ZeroVector);
		TMap<FName, FCataclysmStatInputs> Inputs;
		Carry(Inputs);
		if (Inputs.Num() > 0)
		{
			Caster.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}
		// UPTHRUST'S OWN SHAPE, without its burn and its launch: a strike that raises a wall for 8 seconds.
		const auto RaiseAWall = [&Caster](ECataclysmAbilitySlot Slot) -> bool
		{
			const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
				UCataclysmStrikeSkill::StaticClass(), Slot, /*Level=*/100, Caster.Actor);
			FGameplayAbilitySpec* Spec = Handle.IsValid()
				? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmStrikeSkill* Strike = Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (!Strike)
			{
				return false;
			}
			Strike->SkillName = TEXT("A strike raising a wall");
			Strike->Params = UCataclysmSkillShapes::ParseParams(
				TEXT("Radius=10; Angle=15; Terrain=Wall; TerrainSize=10; TerrainDuration=8"));
			Strike->SkillTags = UCataclysmSkillShapes::TagsFromCell(
				TEXT("Item.Weapon.Warhammer, Element.Demonic, Type.AOE.Persistent, Type.Melee"));
			return Caster.AbilitySystem->TryActivateAbility(Handle);
		};
		const auto Standing = [World, &Caster](ACataclysmTerrain** OutOne = nullptr) -> int32
		{
			int32 Count = 0;
			for (TActorIterator<ACataclysmTerrain> It(World); It; ++It)
			{
				if (IsValid(*It) && It->GetOwner() == Caster.Actor)
				{
					++Count;
					if (OutOne)
					{
						*OutOne = *It;
					}
				}
			}
			return Count;
		};

		if (!RaiseAWall(ECataclysmAbilitySlot::Heavy))
		{
			return Read;
		}
		Read.bMade = true;
		ACataclysmTerrain* First = nullptr;
		Read.AfterOne = Standing(&First);
		Read.FirstLastsSeconds = First ? First->GetLifeSpan() : -1.0f;
		Read.AfterTwo = RaiseAWall(ECataclysmAbilitySlot::Special) ? Standing() : -1;
		return Read;
	};

	const FRead Plain = TwoWalls([](TMap<FName, FCataclysmStatInputs>&) {});
	const FRead Cut = TwoWalls([](TMap<FName, FCataclysmStatInputs>& Inputs)
	{
		FCataclysmStatModifier Half;
		Half.Bucket = ECataclysmStatBucket::More;
		Half.Source = ECataclysmModifierSource::Enchantment;
		Half.Value = -50.0f;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::PersistentAreaDurationStat));
		Line.Base = UCataclysmDamageCalculation::NormalPersistentAreaDuration;
		Line.Modifiers = {Half};
	});
	const FRead OnlyOne = TwoWalls([](TMap<FName, FCataclysmStatInputs>& Inputs)
	{
		CarryFlat(Inputs, UCataclysmDamageCalculation::OnlyOnePersistentAreaStat, 1.0f);
	});
	if (!TestTrue(TEXT("set-up: each caster raised a wall"), Plain.bMade && Cut.bMade && OnlyOne.bMade)
		|| !TestEqual(TEXT("set-up: a plain caster's two strikes leave one wall, then two"),
					  Plain.AfterOne * 10 + Plain.AfterTwo, 12))
	{
		return false;
	}

	// THE DURATION STAT REACHES TERRAIN.
	TestEqual(TEXT("a plain caster's wall lasts its stated 8 seconds"), Plain.FirstLastsSeconds, 8.0f, 0.01f);
	TestEqual(TEXT("and with 50% less of the duration stat it lasts 4"), Cut.FirstLastsSeconds, 4.0f, 0.01f);

	// AND SO DOES THE ONLY-ONE RULE: a new terrain piece ends the earlier one.
	TestEqual(TEXT("a caster that may hold only one has one wall after the first strike"), OnlyOne.AfterOne, 1);
	TestEqual(TEXT("and still one after the second"), OnlyOne.AfterTwo, 1);
	return true;
}

// THE TARGET STANDS IN ONE OF THE ASKER'S ZONES. Ruled 2026-10-06: "You deal 15%-30% increased damage to enemies
// standing in your persistent AOE zones" is a row carrying the condition `target_in_your_zone`. Issue #1833.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTargetInYourZoneTest,
	"Cataclysm.StatExemption.ABonusForEnemiesStandingInYourZonesReachesOnlyThoseInAZoneYouOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTargetInYourZoneTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	// EVERY BLOW'S CRITICAL STRIKE ROLL IS NOUGHT, so a blow critically strikes exactly when its chance is above
	// nought. The chance is nought on its base and a hundred only while the condition holds, which makes "did it
	// critically strike" the reading of the condition, as the boss test in CataclysmCriticalStrikeTests.cpp does.
	const FPinnedRoll Critical(TEXT("Cataclysm.CritRoll"), 0.0f);

	FScopedSwinger Attacker(World, FVector::ZeroVector);
	FScopedSwinger Stranger(World, FVector(-2000.0f, 0.0f, 0.0f));
	FScopedSwinger InMine(World, FVector(100.0f, 0.0f, 0.0f));
	FScopedSwinger InAStrangers(World, FVector(1000.0f, 0.0f, 0.0f));
	ACataclysmGroundZone* Mine = ACataclysmGroundZone::Spawn(Attacker.Actor, FVector::ZeroVector, 300.0f, 10.0f, 1.0f);
	ACataclysmGroundZone* Theirs =
		ACataclysmGroundZone::Spawn(Stranger.Actor, FVector(1000.0f, 0.0f, 0.0f), 300.0f, 10.0f, 1.0f);
	if (!TestNotNull(TEXT("set-up: the attacker's zone"), Mine) || !TestNotNull(TEXT("set-up: a stranger's zone"), Theirs)
		|| !TestTrue(TEXT("set-up: each enemy stands in the zone meant for it"),
					 Mine->Covers(InMine.Actor->GetActorLocation()) && !Mine->Covers(InAStrangers.Actor->GetActorLocation())
						 && Theirs->Covers(InAStrangers.Actor->GetActorLocation())))
	{
		return false;
	}

	const auto Carry = [&Attacker](ECataclysmStatCondition Condition)
	{
		FCataclysmStatModifier Conditional;
		Conditional.Bucket = ECataclysmStatBucket::Flat;
		Conditional.Source = ECataclysmModifierSource::Enchantment;
		Conditional.Value = 100.0f;
		Conditional.Condition = Condition;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(TEXT("crit_chance")));
		Line.Base = 0.0f;
		Line.Modifiers = {Conditional};
		Attacker.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	};
	const auto Strike = [&Attacker](FScopedSwinger& Target)
	{
		FCataclysmDamageResult Result;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target.Actor, 100.0f, FGameplayTagContainer(),
										 FCataclysmHitDelivery(), &Result);
		return Result;
	};

	// CONTROL: WITH NO ROW, NOTHING CRITICALLY STRIKES, so the readings below are the row's.
	Carry(ECataclysmStatCondition::TargetIsBoss);
	if (!TestFalse(TEXT("control: with a row that asks something else, a blow on the enemy in the zone does not critically strike"),
				   Strike(InMine).bWasCritical))
	{
		return false;
	}

	Carry(ECataclysmStatCondition::TargetStandsInYourZone);
	if (!TestTrue(TEXT("a blow on the enemy standing in the attacker's zone carries the bonus"), Strike(InMine).bWasCritical))
	{
		return false;
	}
	TestFalse(TEXT("a blow on the enemy standing in a stranger's zone does not"), Strike(InAStrangers).bWasCritical);

	// AND IT IS WHERE THE ENEMY STANDS NOW: once it has left the zone, the bonus is gone.
	InMine.Actor->SetActorLocation(FVector(100.0f, 3000.0f, 0.0f));
	TestFalse(TEXT("once the enemy has left the zone, a blow on it does not"), Strike(InMine).bWasCritical);

	// AND A ZONE THAT IS GONE COUNTS FOR NOTHING.
	InMine.Actor->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	Mine->Destroy();
	TestFalse(TEXT("once the zone is gone, a blow on the enemy where it stood does not"), Strike(InMine).bWasCritical);
	return true;
}

// A ROW GIVES A ZONE TO A SKILL THAT STATES NO GROUND. Ruled 2026-10-06: "Your movement ability leaves a persistent
// AOE zone at both start and end locations", "Charge skills leave a persistent AOE zone at the impact point" and
// "Your spells leave a persistent AOE zone at the impact point". Issue #1833.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmARowGivesAZoneTest,
	"Cataclysm.StatExemption.ARowGivesAZoneOnlyToASkillThatStatesNoGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmARowGivesAZoneTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	const auto Nothing = [](TMap<FName, FCataclysmStatInputs>&) {};
	const auto BothEnds4 = [](TMap<FName, FCataclysmStatInputs>& Inputs)
	{
		CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAtStartAndEndSecondsStat, 4.0f);
	};
	const auto Impact3 = [](TMap<FName, FCataclysmStatInputs>& Inputs)
	{
		CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAtImpactSecondsStat, 3.0f);
	};
	const auto BothRows = [](TMap<FName, FCataclysmStatInputs>& Inputs)
	{
		CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAtStartAndEndSecondsStat, 4.0f);
		CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAtImpactSecondsStat, 3.0f);
	};
	const auto BothRowsAndOnlyOne = [](TMap<FName, FCataclysmStatInputs>& Inputs)
	{
		CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAtStartAndEndSecondsStat, 4.0f);
		CarryFlat(Inputs, UCataclysmDamageCalculation::ZoneAtImpactSecondsStat, 3.0f);
		CarryFlat(Inputs, UCataclysmDamageCalculation::OnlyOnePersistentAreaStat, 1.0f);
	};
	UClass* Movement = UCataclysmMovementSkill::StaticClass();
	const TCHAR* MoveTags = TEXT("Item.Weapon.Wand, Element.Demonic, Slot.Movement");
	const TCHAR* ChargeTags = TEXT("Item.Weapon.Sword, Element.Demonic, Keyword.Charge, Slot.Movement");
	const TCHAR* SpellTags = TEXT("Item.Weapon.Wand, Element.Demonic, Type.Spell");
	bool bUsed = false;

	// THE ROW'S ZONE IS THE ZONE A SKILL STATING "1.5 M, 4 SECONDS, 10 OF A HIT" LEAVES. One blink states that
	// ground and wears no row; the other states none and wears the row at 4 seconds.
	const TArray<FLeftZone> Stated = ZonesLeftByOneUse(Movement,
		TEXT("Mode=Blink; Range=8; Radius=3.5; GroundRadius=1.5; GroundDuration=4; GroundPercent=10"), MoveTags, Nothing, bUsed);
	if (!TestTrue(TEXT("set-up: the blink that states ground ran and left two zones"), bUsed && Stated.Num() == 2))
	{
		return false;
	}
	const TArray<FLeftZone> Given = ZonesLeftByOneUse(Movement, TEXT("Mode=Blink; Range=8; Radius=3.5"), MoveTags, BothEnds4, bUsed);
	if (!TestTrue(TEXT("the blink that states none ran"), bUsed)
		|| !TestEqual(TEXT("and under the row it leaves two zones"), Given.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("set-up: the stated ground deals something a sweep"), Stated[0].PerSweep > 0.0f);
	TestEqual(TEXT("the row's zone is 1.5 metres in radius"), Given[0].RadiusCm, 150.0f, 0.01f);
	TestEqual(TEXT("which is the radius the stated ground has"), Given[0].RadiusCm, Stated[0].RadiusCm, 0.01f);
	TestEqual(TEXT("and a sweep of it deals what a sweep of ground stating 10 deals"), Given[0].PerSweep, Stated[0].PerSweep, 0.001f);
	TestEqual(TEXT("and it lasts as long"), Given[0].LastsSeconds, Stated[0].LastsSeconds, 0.01f);
	TestTrue(TEXT("the row's zone carries the skill's own damage type"),
			 Given[0].DamageType == UCataclysmDamageCalculation::DamageTypeFromTags(UCataclysmSkillShapes::TagsFromCell(MoveTags)));
	TestFalse(TEXT("which is a type and not none"), Given[0].DamageType.IsNone());
	TestTrue(TEXT("control: a skill's own ground is handed none, as before"), Stated[0].DamageType.IsNone());

	// A SKILL THAT STATES GROUND KEEPS ITS OWN AND GETS NONE FROM EITHER ROW.
	const TArray<FLeftZone> Kept = ZonesLeftByOneUse(Movement,
		TEXT("Mode=Blink; Range=8; Radius=3.5; GroundRadius=1.5; GroundDuration=6; GroundPercent=10"), MoveTags, BothRows, bUsed);
	if (TestTrue(TEXT("the blink that states ground ran under both rows"), bUsed)
		&& TestEqual(TEXT("and still leaves its own two zones and no more"), Kept.Num(), 2))
	{
		TestEqual(TEXT("each lasting its own stated 6 seconds"), Kept[0].LastsSeconds, 6.0f, 0.01f);
	}

	// A CHARGE SKILL UNDER BOTH ROWS: one zone where it began, and two where it arrived, one from each row.
	const TArray<FLeftZone> Charged = ZonesLeftByOneUse(Movement, TEXT("Mode=Charge; Range=8; Radius=1.5"), ChargeTags, BothRows, bUsed);
	if (TestTrue(TEXT("the charge ran under both rows"), bUsed)
		&& TestEqual(TEXT("and leaves three zones"), Charged.Num(), 3))
	{
		TestTrue(TEXT("one where it began"), Charged[0].At.Size2D() < 100.0f);
		TestEqual(TEXT("lasting the 4 seconds of the start-and-end row"), Charged[0].LastsSeconds, 4.0f, 0.01f);
		TestTrue(TEXT("and two where it arrived"), Charged[1].At.Size2D() > 400.0f && Charged[2].At.Size2D() > 400.0f);
		TestEqual(TEXT("one of those lasting the 3 seconds of the impact row"),
				  FMath::Min(Charged[1].LastsSeconds, Charged[2].LastsSeconds), 3.0f, 0.01f);
		TestEqual(TEXT("and the other the 4 of the start-and-end row"),
				  FMath::Max(Charged[1].LastsSeconds, Charged[2].LastsSeconds), 4.0f, 0.01f);
	}

	// AND WITH "ONLY ONE PERSISTENT AREA" WORN TOO, the ordinary rule ends each earlier one, and one is left.
	const TArray<FLeftZone> OnlyOne = ZonesLeftByOneUse(Movement, TEXT("Mode=Charge; Range=8; Radius=1.5"), ChargeTags, BothRowsAndOnlyOne, bUsed);
	TestTrue(TEXT("the charge ran under both rows and the only-one row"), bUsed);
	TestEqual(TEXT("and is left with one zone"), OnlyOne.Num(), 1);

	// THE IMPACT ROW ON THE OTHER KINDS OF SKILL. A projectile: where its flight ended.
	const TArray<FLeftZone> Shot = ZonesLeftByOneUse(UCataclysmProjectileSkill::StaticClass(),
		TEXT("Range=14; Radius=1.5; Pierce=0; Speed=0"), SpellTags, Impact3, bUsed);
	if (TestTrue(TEXT("the projectile ran"), bUsed) && TestEqual(TEXT("and leaves one zone"), Shot.Num(), 1))
	{
		TestTrue(TEXT("away from its user"), Shot[0].At.Size2D() > 400.0f);
	}
	const TArray<FLeftZone> ShotPlain = ZonesLeftByOneUse(UCataclysmProjectileSkill::StaticClass(),
		TEXT("Range=14; Radius=1.5; Pierce=0; Speed=0"), SpellTags, Nothing, bUsed);
	TestTrue(TEXT("control: the projectile ran with no row"), bUsed);
	TestEqual(TEXT("control: and leaves no zone"), ShotPlain.Num(), 0);

	// A strike: under its user.
	const TArray<FLeftZone> Struck = ZonesLeftByOneUse(UCataclysmStrikeSkill::StaticClass(),
		TEXT("Radius=3; Angle=360"), SpellTags, Impact3, bUsed);
	if (TestTrue(TEXT("the strike ran"), bUsed) && TestEqual(TEXT("and leaves one zone"), Struck.Num(), 1))
	{
		TestTrue(TEXT("under its user"), Struck[0].At.Size2D() < 100.0f);
	}

	// A curse: under the enemy it was laid on, which stands 6 metres to the side.
	const TArray<FLeftZone> Cursed = ZonesLeftByOneUse(UCataclysmDebuffSkill::StaticClass(),
		TEXT("Range=12; MaxTargets=1; EffectDuration=6; Effect=Shred"), SpellTags, Impact3, bUsed);
	if (TestTrue(TEXT("the curse ran"), bUsed) && TestEqual(TEXT("and leaves one zone"), Cursed.Num(), 1))
	{
		TestTrue(TEXT("under the enemy it was laid on"), FVector::Dist2D(Cursed[0].At, FVector(0.0f, 600.0f, 0.0f)) < 100.0f);
	}

	// AND THE START-AND-END ROW IS READ BY A MOVEMENT SKILL ONLY: a strike under it leaves nothing.
	const TArray<FLeftZone> StruckUnderTheOther = ZonesLeftByOneUse(UCataclysmStrikeSkill::StaticClass(),
		TEXT("Radius=3; Angle=360"), SpellTags, BothEnds4, bUsed);
	TestTrue(TEXT("the strike ran under the start-and-end row"), bUsed);
	TestEqual(TEXT("and leaves no zone"), StruckUnderTheOther.Num(), 0);
	return true;
}

// A ZONE FOLLOWS THE CHARACTER WHO LEFT IT. Ruled 2026-10-06: "Your persistent AOE zones follow you as you move at
// 50% of your movement speed". Issue #1833.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAFollowingZoneTest,
	"Cataclysm.StatExemption.AFollowingZoneWalksToItsOwnerAtAShareOfTheirSpeedAndStopsThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAFollowingZoneTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE OWNER IS A CREATURE, because only a character has a walking speed to read. It walks 400 cm a second.
	ACataclysmEnemyCharacter* Owner =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a creature to own the zones"), Owner)
		|| !TestNotNull(TEXT("set-up: it has a movement component"), Owner->GetCharacterMovement()))
	{
		return false;
	}
	Owner->GetCharacterMovement()->MaxWalkSpeed = 400.0f;
	TestEqual(TEXT("set-up: the speed a zone reads for it is that figure"), ACataclysmGroundZone::WalkSpeedOf(Owner), 400.0f, 0.001f);

	ACataclysmGroundZone* Zone = ACataclysmGroundZone::Spawn(Owner, FVector::ZeroVector, 200.0f, 10.0f, 1.0f);
	ACataclysmGroundZone* Staying = ACataclysmGroundZone::Spawn(Owner, FVector::ZeroVector, 200.0f, 10.0f, 1.0f);
	ACataclysmGroundZone* Lane = ACataclysmGroundZone::SpawnAlong(
		Owner, FVector::ZeroVector, FVector(0.0f, 500.0f, 0.0f), 100.0f, 10.0f, 1.0f);
	if (!TestNotNull(TEXT("set-up: a zone"), Zone) || !TestNotNull(TEXT("set-up: a second zone"), Staying)
		|| !TestNotNull(TEXT("set-up: a lane"), Lane))
	{
		return false;
	}
	Zone->FollowItsOwnerAt(50.0f);
	Lane->FollowItsOwnerAt(50.0f);
	const FVector LaneShape = Lane->FarEnd - Lane->GetActorLocation();
	const FVector ZoneBegan = Zone->GetActorLocation();

	// WITH ITS OWNER STANDING ON IT, IT HAS NOWHERE TO GO.
	Zone->FollowStep(1.0f);
	TestEqual(TEXT("a following zone under its owner does not move"), Zone->TravelledCm, 0.0f, 0.01f);

	// THE OWNER GOES 10 METRES AWAY. One second at half of 400 is 2 metres toward them.
	Owner->SetActorLocation(FVector(1000.0f, 0.0f, Owner->GetActorLocation().Z));
	Zone->FollowStep(1.0f);
	Staying->FollowStep(1.0f);
	Lane->FollowStep(1.0f);
	TestEqual(TEXT("after one second the zone has moved 2 metres"), Zone->TravelledCm, 200.0f, 0.5f);
	TestEqual(TEXT("toward its owner"), static_cast<float>(Zone->GetActorLocation().X - ZoneBegan.X), 200.0f, 0.5f);
	TestEqual(TEXT("control: a zone that was not told to follow has not moved"), Staying->TravelledCm, 0.0f, 0.01f);
	TestEqual(TEXT("the lane's near end has moved 2 metres too"), Lane->TravelledCm, 200.0f, 0.5f);
	TestTrue(TEXT("and the lane has kept its shape"), (Lane->FarEnd - Lane->GetActorLocation()).Equals(LaneShape, 0.5));

	// AND IT STOPS ON REACHING THEM: ten more seconds would carry it 20 metres, and it has 8 to go.
	Zone->FollowStep(10.0f);
	TestEqual(TEXT("it stops where its owner stands"), static_cast<float>(Zone->GetActorLocation().X), 1000.0f, 0.5f);
	TestEqual(TEXT("having moved 10 metres in all and no further"), Zone->TravelledCm, 1000.0f, 0.5f);

	// AN OWNER THAT IS NOT A CHARACTER HAS NO SPEED, so its zone stays.
	FScopedSwinger Plain(World, FVector(0.0f, -3000.0f, 0.0f));
	ACataclysmGroundZone* OfAPlainActor =
		ACataclysmGroundZone::Spawn(Plain.Actor, FVector(500.0f, -3000.0f, 0.0f), 200.0f, 10.0f, 1.0f);
	if (TestNotNull(TEXT("set-up: a zone owned by an actor that is no character"), OfAPlainActor))
	{
		OfAPlainActor->FollowItsOwnerAt(50.0f);
		OfAPlainActor->FollowStep(1.0f);
		TestEqual(TEXT("the speed read for an owner that is no character is nought"),
				  ACataclysmGroundZone::WalkSpeedOf(Plain.Actor), 0.0f, 0.001f);
		TestEqual(TEXT("so its zone has not moved"), OfAPlainActor->TravelledCm, 0.0f, 0.01f);
	}
	return true;
}

// A PLAYER READS CRIPPLE AS A CREATURE DOES. Issue #2273. Until that issue only `ACataclysmEnemyCharacter` read it: a
// player who carried Cripple walked at full speed and swung at full rate. THIS IS THE REPRODUCTION, and it fails on
// the code before the fix at "walks at four fifths".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAPlayerReadsCrippleTest,
	"Cataclysm.StatExemption.APlayerWhoCarriesCrippleWalksSwingsAndThrowsSlowerAsACreatureDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAPlayerReadsCrippleTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
	const FCataclysmAilmentKind* CrippleKind = UCataclysmAilments::KindNamed(TEXT("Cripple"));
	FScopedSwinger Source(World, FVector(0.0f, 9000.0f, 0.0f));
	ACataclysmPlayerCharacter* Player = SpawnPotionHolder(World);
	UCataclysmAbilitySystemComponent* System =
		Player ? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player)) : nullptr;
	if (!TestTrue(TEXT("set-up: the tag, the ailment, a player and its ability system"),
				  Cripple.IsValid() && CrippleKind && Player && System && Player->GetCharacterMovement()))
	{
		return false;
	}
	const auto Walk = [Player]() { return Player->GetCharacterMovement()->MaxWalkSpeed; };
	const auto TakeCrippleOff = [System, &Cripple]()
	{
		System->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(Cripple));
	};

	// A THROWN SKILL WHOSE INTERVAL FOLLOWS ATTACK SPEED, to read the seconds between its throws.
	const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
		UCataclysmProjectileSkill::StaticClass(), ECataclysmAbilitySlot::Heavy, /*Level=*/1, Player);
	FGameplayAbilitySpec* Spec = Handle.IsValid() ? System->FindAbilitySpecFromHandle(Handle) : nullptr;
	UCataclysmProjectileSkill* Thrown = Spec ? Cast<UCataclysmProjectileSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("set-up: a thrown skill on the player"), Thrown))
	{
		return false;
	}
	Thrown->Params = UCataclysmSkillShapes::ParseParams(
		TEXT("Range=10; Radius=1; Speed=0; Interval=0.5; ScalesWithAttackSpeed=1"));

	// AN ATTACK SPEED OF 1 A SECOND, WHICH A WORN WEAPON WOULD SUPPLY. The attribute starts at nought and this player
	// wears none, so without this its seconds between swings is nought and nothing below can be read. The first run
	// of this test, on 2026-10-07, stopped here for that reason.
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.0f);

	const float WalkBefore = Walk();
	const float SwingBefore = UCataclysmBasicAttack::SecondsBetweenSwingsFor(System);
	const float ThrowBefore = Thrown->SecondsBetweenThrows();
	const bool bWalks = TestTrue(TEXT("set-up: the player walks at some speed"), WalkBefore > 0.0f);
	const bool bSwings = TestTrue(TEXT("set-up: the player swings at some rate"), SwingBefore > 0.0f);
	const bool bThrows = TestTrue(TEXT("set-up: the player throws at some rate"), ThrowBefore > 0.0f);
	if (!bWalks || !bSwings || !bThrows)
	{
		return false;
	}
	TestEqual(TEXT("control: with no Cripple the share kept is the whole"),
			  UCataclysmSkillEffects::CrippleMultiplierOn(Player), 1.0f, 0.0001f);

	// CRIPPLE STATING 20. Gaining the tag is heard, and the walking speed is four fifths at once.
	UCataclysmSkillEffects::ApplyTagForDuration(Source.Actor, Player, Cripple, 4.0f, 20.0f);
	if (!TestTrue(TEXT("set-up: the player carries Cripple"), System->HasMatchingGameplayTag(Cripple)))
	{
		return false;
	}
	TestEqual(TEXT("a player carrying a Cripple of 20 walks at four fifths of their speed"), Walk(), WalkBefore * 0.8f, 0.5f);
	TestEqual(TEXT("and waits a quarter longer between swings"),
			  UCataclysmBasicAttack::SecondsBetweenSwingsFor(System), SwingBefore / 0.8f, 0.001f);
	TestEqual(TEXT("and a quarter longer between throws"), Thrown->SecondsBetweenThrows(), ThrowBefore / 0.8f, 0.001f);

	// LOSING THE TAG IS HEARD TOO.
	TakeCrippleOff();
	TestEqual(TEXT("with the Cripple gone the player walks at their full speed again"), Walk(), WalkBefore, 0.5f);
	TestEqual(TEXT("and swings at their full rate"), UCataclysmBasicAttack::SecondsBetweenSwingsFor(System), SwingBefore, 0.001f);

	// AT THE ROW'S OWN FIGURE, 30, which is what the ailment lays at its ordinary size.
	UCataclysmAilments::Apply(Source.Actor, Player, *CrippleKind, /*Magnitude=*/1.0f);
	if (TestTrue(TEXT("set-up: the player carries the ailment's Cripple"), System->HasMatchingGameplayTag(Cripple)))
	{
		TestEqual(TEXT("a player carrying the row's Cripple walks at seven tenths"), Walk(), WalkBefore * 0.7f, 0.5f);
		TestEqual(TEXT("and swings at seven tenths of the rate"),
				  UCataclysmBasicAttack::SecondsBetweenSwingsFor(System), SwingBefore / 0.7f, 0.001f);
		TestEqual(TEXT("and throws at seven tenths of the rate"), Thrown->SecondsBetweenThrows(), ThrowBefore / 0.7f, 0.001f);
	}
	TakeCrippleOff();

	// A CREATURE'S FIGURE IS WHAT IT WAS: four fifths under a Cripple of 20, read through the same function.
	ACataclysmEnemyCharacter* Creature =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(3000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Creature->SetHealth(1000.0f);
		TestEqual(TEXT("control: a creature with no Cripple keeps the whole"), Creature->CrippleMultiplier(), 1.0f, 0.0001f);
		UCataclysmSkillEffects::ApplyTagForDuration(Source.Actor, Creature, Cripple, 4.0f, 20.0f);
		TestEqual(TEXT("a creature carrying a Cripple of 20 keeps four fifths, as before"), Creature->CrippleMultiplier(), 0.8f, 0.0001f);
		TestEqual(TEXT("and the shared function answers the same for it"),
				  UCataclysmSkillEffects::CrippleMultiplierOn(Creature), Creature->CrippleMultiplier(), 0.0001f);
	}

	// A PLAYER WHOSE MOVEMENT SPEED NOTHING MAY LOWER IS NOT SLOWED ON FOOT BY CRIPPLE, AND STILL SWINGS SLOWER. The
	// flag is the Ravager's (`movement_speed_reduction_suppressed`). Ruled 2026-10-06.
	// THE FIRST PLAYER IS MOVED AWAY FIRST: the helper spawns every player at the origin, and two characters are not
	// spawned on one spot.
	Player->SetActorLocation(FVector(-6000.0f, 0.0f, Player->GetActorLocation().Z));
	ACataclysmPlayerCharacter* Unslowed = SpawnPotionHolder(World);
	UCataclysmAbilitySystemComponent* UnslowedSystem =
		Unslowed ? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Unslowed)) : nullptr;
	if (TestTrue(TEXT("set-up: a second player"), Unslowed && UnslowedSystem && Unslowed->GetCharacterMovement()))
	{
		GrantFlat(Unslowed, ACataclysmPlayerCharacter::MovementSpeedReductionSuppressedStat, 1.0f);
		UnslowedSystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.0f);
		const float FullWalk = Unslowed->GetCharacterMovement()->MaxWalkSpeed;
		const float FullSwing = UCataclysmBasicAttack::SecondsBetweenSwingsFor(UnslowedSystem);
		UCataclysmSkillEffects::ApplyTagForDuration(Source.Actor, Unslowed, Cripple, 4.0f, 20.0f);
		TestTrue(TEXT("set-up: the second player carries Cripple"), UnslowedSystem->HasMatchingGameplayTag(Cripple));
		TestEqual(TEXT("a player whose movement speed nothing may lower walks at full speed under Cripple"),
				  Unslowed->GetCharacterMovement()->MaxWalkSpeed, FullWalk, 0.5f);
		TestEqual(TEXT("and still waits a quarter longer between swings"),
				  UCataclysmBasicAttack::SecondsBetweenSwingsFor(UnslowedSystem), FullSwing / 0.8f, 0.001f);
	}
	return true;
}

// A ROW LAYS A STATUS ON ITS OWN WEARER. Ruled 2026-10-06, for five drawbacks: "Taking a hit has a 15%-25% chance to
// trigger a random negative status effect on you", "Critical strikes have a 20%-35% chance to trigger a random debuff
// on you", "Every 15 seconds a random debuff is applied to you", "After using a charge skill you are briefly stunned
// for 0.5-1 second" and "When you apply a DOT, 1-4 stacks are applied to you". Issue #1833.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStatusOnTheWearerTest,
	"Cataclysm.StatExemption.ARowLaysAStatusOnItsOwnWearer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStatusOnTheWearerTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// WHICH DEBUFF THE RANDOM PICK GIVES IS PINNED BY ITS PLACE IN THE POOL: Madness, Cripple, Weaken, Shred, Stun.
	IConsoleVariable* Pick = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.RandomDebuffPick"));
	if (!TestNotNull(TEXT("set-up: the pin for the random pick"), Pick))
	{
		return false;
	}
	const int32 PickWas = Pick->GetInt();
	ON_SCOPE_EXIT { Pick->Set(PickWas, ECVF_SetByConsole); };

	const auto Row = [](const TCHAR* Event, ECataclysmApplyStatus Kind, const TCHAR* Status, float Value)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.ApplyStatus = Kind;
		Action.bStatusOnTheWearer = true;
		Action.StatusName = Status;
		Action.Percent = Value;
		Action.TriggerKey = FName(TEXT("a status on the wearer"));
		return Action;
	};
	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();

	// A HIT TAKEN, A CHANCE OF 20. With the roll pinned under it the wearer carries the debuff picked; with the
	// roll pinned over it, nothing. No other character is named by the event, and none is needed.
	{
		FScopedSwinger Wearer(World, FVector::ZeroVector);
		Wearer.AbilitySystem->SetPoolActions({Row(TEXT("hit_taken"), ECataclysmApplyStatus::Chance, TEXT("Random Debuff"), 20.0f)});
		Pick->Set(1, ECVF_SetByConsole);
		{
			const FPinnedRoll Misses(TEXT("Cataclysm.StatusRoll"), 99.0f);
			Wearer.AbilitySystem->ActOnEvent(FName(TEXT("hit_taken")), nullptr, 0.0f, /*bLanded=*/true);
			TestFalse(TEXT("control: with the roll over the chance, a hit taken lays nothing on the wearer"),
					  Wearer.AbilitySystem->HasMatchingGameplayTag(Cripple));
		}
		{
			const FPinnedRoll Comes(TEXT("Cataclysm.StatusRoll"), 0.0f);
			Wearer.AbilitySystem->ActOnEvent(FName(TEXT("hit_taken")), nullptr, 0.0f, /*bLanded=*/true);
			TestTrue(TEXT("with the roll under the chance, a hit taken lays the debuff picked on the wearer"),
					 Wearer.AbilitySystem->HasMatchingGameplayTag(Cripple));
		}
	}

	// THE PICK THAT IS A STUN LANDS WITH NO BLOW AT ALL, which is the rule of a tenth not being asked: the event
	// carries no damage, and a stun from a blow that dealt none is refused.
	{
		FScopedSwinger Wearer(World, FVector(0.0f, 3000.0f, 0.0f));
		Wearer.AbilitySystem->SetPoolActions({Row(TEXT("hit_taken"), ECataclysmApplyStatus::Chance, TEXT("Random Debuff"), 100.0f)});
		Pick->Set(4, ECVF_SetByConsole);
		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("hit_taken")), nullptr, 0.0f, /*bLanded=*/true);
		TestTrue(TEXT("a random debuff that is a stun stuns the wearer though no blow dealt anything"),
				 UCataclysmSkillEffects::IsStunned(Wearer.Actor));
	}

	// AFTER A CHARGE SKILL, A STUN FOR THE ROW'S SECONDS. The row waits on the skill ending and asks for the charge
	// keyword. A charge stuns its user once it has ended; a blink, which does not carry the keyword, does not.
	const auto UseAMove = [World, &Row](const FVector& At, const TCHAR* ParamsCell, const TCHAR* TagsCell, bool& bOutUsed) -> bool
	{
		FScopedSwinger Wearer(World, At);
		FCataclysmPoolAction Stun = Row(TEXT("skill_end"), ECataclysmApplyStatus::Seconds, TEXT("Stun"), 0.75f);
		Stun.RequiredTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Keyword.Charge"));
		Wearer.AbilitySystem->SetPoolActions({Stun});
		const FGameplayAbilitySpecHandle Handle = Wearer.AbilitySystem->GiveAbilityInSlot(
			UCataclysmMovementSkill::StaticClass(), ECataclysmAbilitySlot::Movement, /*Level=*/100, Wearer.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? Wearer.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmSkillTemplate* Skill = Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			bOutUsed = false;
			return false;
		}
		Skill->SkillName = TEXT("A move that may stun its user");
		Skill->Params = UCataclysmSkillShapes::ParseParams(ParamsCell);
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagsCell);
		bOutUsed = Wearer.AbilitySystem->TryActivateAbility(Handle);
		return UCataclysmSkillEffects::IsStunned(Wearer.Actor);
	};
	bool bCharged = false;
	bool bBlinked = false;
	const bool bStunnedAfterACharge = UseAMove(FVector(0.0f, 6000.0f, 0.0f), TEXT("Mode=Charge; Range=8; Radius=1.5"),
		TEXT("Item.Weapon.Sword, Element.Demonic, Keyword.Charge, Slot.Movement"), bCharged);
	const bool bStunnedAfterABlink = UseAMove(FVector(0.0f, 9000.0f, 0.0f), TEXT("Mode=Blink; Range=8; Radius=3.5"),
		TEXT("Item.Weapon.Wand, Element.Demonic, Slot.Movement"), bBlinked);
	if (TestTrue(TEXT("set-up: the charge and the blink both ran"), bCharged && bBlinked))
	{
		TestTrue(TEXT("a wearer is stunned once their charge skill has ended"), bStunnedAfterACharge);
		TestFalse(TEXT("control: and not after a skill that does not carry the charge keyword"), bStunnedAfterABlink);
	}

	// WHEN THE WEARER APPLIES A DAMAGE OVER TIME TO ANOTHER, THE SAME IS LAID ON THE WEARER ONCE, AT THE ROW'S MULTIPLE
	// OF ITS ORDINARY SIZE. No ailment stacks, so the row's "1-4 stacks" is a size. A wearer whose row rolled 4 takes
	// four times the damage a second from the bleed laid on them that a wearer whose row rolled 1 takes: the two
	// are compared, so a size that is ignored fails here.
	{
		const FCataclysmAilmentKind* Bleed = UCataclysmAilments::KindNamed(TEXT("Bleed"));
		const FGameplayTag BleedTag = UCataclysmDebuffs::BleedTag();
		const auto BleedLaidOnAWearerWhoseRowRolled = [&](float Rolled, const FVector& At, bool& bOutEnemyBleeds) -> float
		{
			FScopedSwinger Wearer(World, At);
			FScopedSwinger Enemy(World, At + FVector(200.0f, 0.0f, 0.0f));
			Wearer.AbilitySystem->SetPoolActions({Row(TEXT("dot_applied"), ECataclysmApplyStatus::Sized, TEXT("Applied DoT"), Rolled)});
			UCataclysmAilments::Apply(Wearer.Actor, Enemy.Actor, *Bleed, /*Magnitude=*/1.0f);
			bOutEnemyBleeds = Enemy.AbilitySystem->HasMatchingGameplayTag(BleedTag);
			return UCataclysmSkillEffects::StatedStrengthOn(Wearer.Actor, BleedTag);
		};
		if (TestNotNull(TEXT("set-up: the bleed"), Bleed))
		{
			FScopedSwinger Plain(World, FVector(0.0f, 21000.0f, 0.0f));
			TestTrue(TEXT("control: a character nothing was laid on states no bleed"),
					 UCataclysmSkillEffects::StatedStrengthOn(Plain.Actor, BleedTag) < 0.0f);
			bool bOneBled = false;
			bool bFourBled = false;
			const float AtOne = BleedLaidOnAWearerWhoseRowRolled(1.0f, FVector(0.0f, 12000.0f, 0.0f), bOneBled);
			const float AtFour = BleedLaidOnAWearerWhoseRowRolled(4.0f, FVector(0.0f, 15000.0f, 0.0f), bFourBled);
			TestTrue(TEXT("set-up: each wearer's enemy bleeds"), bOneBled && bFourBled);
			if (TestTrue(TEXT("a wearer whose row rolled 1 carries a bleed of their own, having applied one to another"), AtOne > 0.0f))
			{
				TestEqual(TEXT("and a wearer whose row rolled 4 carries one four times as large"), AtFour, AtOne * 4.0f, AtOne * 0.001f);
			}
		}
	}

	// AND A CLEANSE REMOVES WHAT THE ROW LAID, since the cleanse keeps only converted damage. Ruled 2026-10-06.
	{
		FScopedSwinger Wearer(World, FVector(0.0f, 18000.0f, 0.0f));
		Wearer.AbilitySystem->SetPoolActions({Row(TEXT("hit_taken"), ECataclysmApplyStatus::Chance, TEXT("Random Debuff"), 100.0f)});
		Pick->Set(1, ECVF_SetByConsole);
		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("hit_taken")), nullptr, 0.0f, /*bLanded=*/true);
		if (TestTrue(TEXT("set-up: the wearer carries the debuff their own row laid"), Wearer.AbilitySystem->HasMatchingGameplayTag(Cripple)))
		{
			UCataclysmDebuffs::Cleanse(Wearer.Actor);
			TestFalse(TEXT("a cleanse takes a debuff the wearer's own row laid off the wearer"),
					  Wearer.AbilitySystem->HasMatchingGameplayTag(Cripple));
		}
	}
	return true;
}

// DAMAGE OVER TIME ON THE WEARER, SCOPED BY AILMENT. Ruled 2026-10-06, for seven drawbacks: "Bleed effects applied to
// you deal 30%-50% increased damage", "Bleeding on you lasts 50%-100% longer", "DoTs on you have 100%-300% more duration", "DoTs
// on you tick twice as fast while moving", "Unaffected by bleeding", "10%-20% of bleed damage you take is taken from
// your energy shield instead of your health" and "DoTs deal damage to your mana pool first". Issue #1833.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageOverTimeOnTheWearerByAilmentTest,
	"Cataclysm.StatExemption.DamageOverTimeOnTheWearerIsScopedByAilment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDamageOverTimeOnTheWearerByAilmentTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Bleed = UCataclysmDebuffs::BleedTag();
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
	const FGameplayTag AnyDamageOverTime = UCataclysmDamageCalculation::DamageOverTimeTag();
	if (!TestTrue(TEXT("set-up: the bleed, burn, Cripple and damage over time tags"),
				  Bleed.IsValid() && Burn.IsValid() && Cripple.IsValid() && AnyDamageOverTime.IsValid()))
	{
		return false;
	}
	const TCHAR* TakenStat = UCataclysmDamageCalculation::DamageOverTimeTakenStat;
	const TCHAR* DurationStat = TEXT("debuff_duration_taken");

	const FCataclysmIncomingHit BleedTick = TickOf(100.0f, Bleed);
	const FCataclysmIncomingHit BurnTick = TickOf(100.0f, Burn);
	const FCataclysmIncomingHit BareTick = TickOf(100.0f, FGameplayTag());
	const auto Taken = [](const FCataclysmIncomingHit& Tick, const FScopedSwinger& Who)
	{
		return ResolveATick(Tick, Who.AbilitySystem).DealtToHealth;
	};

	// THE CONTROL: a character carrying no row. Every figure below is compared with what this one takes.
	FScopedSwinger Plain(World, FVector::ZeroVector);
	const float PlainBleed = Taken(BleedTick, Plain);
	const float PlainBurn = Taken(BurnTick, Plain);
	const float PlainBare = Taken(BareTick, Plain);
	if (!TestTrue(TEXT("set-up: a plain character takes each of the three ticks"),
				  PlainBleed > 0.0f && PlainBurn > 0.0f && PlainBare > 0.0f))
	{
		return false;
	}

	// AN EXISTING ROW WITH NO REQUIRED TAGS READS AS IT DID. "DoTs deal double damage to you" is
	// `damage_over_time_taken` more 100 with none: it doubles a bleed tick, a burn tick and a tick with no ailment.
	{
		FScopedSwinger Wearer(World, FVector(0.0f, 3000.0f, 0.0f));
		TMap<FName, FCataclysmStatInputs> Inputs;
		CarryOnAHundred(Inputs, TakenStat, ECataclysmStatBucket::More, 100.0f);
		Wearer.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		TestEqual(TEXT("a row with no required tags doubles a bleed tick"), Taken(BleedTick, Wearer), PlainBleed * 2.0f, 0.01f);
		TestEqual(TEXT("and a burn tick alike"), Taken(BurnTick, Wearer), PlainBurn * 2.0f, 0.01f);
		TestEqual(TEXT("and a tick with no ailment alike"), Taken(BareTick, Wearer), PlainBare * 2.0f, 0.01f);
	}

	// A ROW REQUIRING THE BLEED TAG CHANGES A BLEED AND NOTHING ELSE. "Bleed effects applied to you deal 30%-50%
	// increased damage", at 50.
	{
		FScopedSwinger Wearer(World, FVector(0.0f, 6000.0f, 0.0f));
		TMap<FName, FCataclysmStatInputs> Inputs;
		CarryOnAHundred(Inputs, TakenStat, ECataclysmStatBucket::Increased, 50.0f, Bleed);
		Wearer.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		TestEqual(TEXT("a row requiring the bleed tag raises a bleed tick by half"), Taken(BleedTick, Wearer), PlainBleed * 1.5f, 0.01f);
		TestEqual(TEXT("control: and leaves a burn tick as it was"), Taken(BurnTick, Wearer), PlainBurn, 0.01f);
		TestEqual(TEXT("control: and a tick with no ailment as it was"), Taken(BareTick, Wearer), PlainBare, 0.01f);

		// A HIT BUILT WITH THE BOOLEAN ALONE IS STILL ASKED AS A BLEED.
		FCataclysmIncomingHit MarkedOnly;
		MarkedOnly.Damage = 100.0f;
		MarkedOnly.bIsDamageOverTime = true;
		MarkedOnly.bIsBleed = true;
		TestEqual(TEXT("a tick marked a bleed and carrying no tags meets the bleed row too"), Taken(MarkedOnly, Wearer), PlainBleed * 1.5f, 0.01f);
	}

	// A ROW REQUIRING THE PARENT REACHES EVERY DAMAGE OVER TIME, one with no ailment included.
	{
		FScopedSwinger Wearer(World, FVector(0.0f, 9000.0f, 0.0f));
		TMap<FName, FCataclysmStatInputs> Inputs;
		CarryOnAHundred(Inputs, TakenStat, ECataclysmStatBucket::More, 100.0f, AnyDamageOverTime);
		Wearer.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		TestEqual(TEXT("a row requiring the damage over time parent doubles a bleed tick"), Taken(BleedTick, Wearer), PlainBleed * 2.0f, 0.01f);
		TestEqual(TEXT("and a burn tick"), Taken(BurnTick, Wearer), PlainBurn * 2.0f, 0.01f);
		TestEqual(TEXT("and a tick with no ailment"), Taken(BareTick, Wearer), PlainBare * 2.0f, 0.01f);
	}

	// HOW LONG AN EFFECT LASTS ON THE CHARACTER, asked with the tags of what is applied. A damage over time passes
	// its own tag and the parent; Cripple passes its own.
	FGameplayTagContainer AsABleed(Bleed);
	AsABleed.AddTag(AnyDamageOverTime);
	FGameplayTagContainer AsABurn(Burn);
	AsABurn.AddTag(AnyDamageOverTime);
	const FGameplayTagContainer AsCripple(Cripple);
	TestEqual(TEXT("control: ten seconds last ten on a plain character"),
			  UCataclysmDebuffs::DurationOn(Plain.AbilitySystem, 10.0f, AsABleed), 10.0f, 0.001f);

	// AN EXISTING ROW WITH NO REQUIRED TAGS READS AS IT DID: "Debuffs applied to you last 30%-50% longer", at 50,
	// lengthens a bleed and a Cripple alike, and a caller that passes no tags.
	{
		FScopedSwinger Wearer(World, FVector(0.0f, 12000.0f, 0.0f));
		TMap<FName, FCataclysmStatInputs> Inputs;
		CarryOnAHundred(Inputs, DurationStat, ECataclysmStatBucket::Increased, 50.0f);
		Wearer.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		TestEqual(TEXT("a duration row with no required tags lengthens a bleed by half"),
				  UCataclysmDebuffs::DurationOn(Wearer.AbilitySystem, 10.0f, AsABleed), 15.0f, 0.001f);
		TestEqual(TEXT("and a Cripple alike"), UCataclysmDebuffs::DurationOn(Wearer.AbilitySystem, 10.0f, AsCripple), 15.0f, 0.001f);
		TestEqual(TEXT("and an effect asked about with no tags alike"), UCataclysmDebuffs::DurationOn(Wearer.AbilitySystem, 10.0f), 15.0f, 0.001f);
	}

	// "BLEEDING ON YOU LASTS 50%-100% LONGER" AND "DoTs ON YOU HAVE 100%-300% MORE DURATION", each at its most: an increase of
	// 100 requiring the bleed tag and 300 more requiring the parent. A bleed lasts 8 times as long, a burn 4 times,
	// and a Cripple as long as it did.
	{
		FScopedSwinger Wearer(World, FVector(0.0f, 15000.0f, 0.0f));
		TMap<FName, FCataclysmStatInputs> Inputs;
		CarryOnAHundred(Inputs, DurationStat, ECataclysmStatBucket::Increased, 100.0f, Bleed);
		CarryOnAHundred(Inputs, DurationStat, ECataclysmStatBucket::More, 300.0f, AnyDamageOverTime);
		Wearer.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		TestEqual(TEXT("with both rows a bleed lasts 8 times as long"),
				  UCataclysmDebuffs::DurationOn(Wearer.AbilitySystem, 10.0f, AsABleed), 80.0f, 0.01f);
		TestEqual(TEXT("a burn 4 times as long, meeting only the row for every damage over time"),
				  UCataclysmDebuffs::DurationOn(Wearer.AbilitySystem, 10.0f, AsABurn), 40.0f, 0.01f);
		TestEqual(TEXT("control: and a Cripple as long as it did"),
				  UCataclysmDebuffs::DurationOn(Wearer.AbilitySystem, 10.0f, AsCripple), 10.0f, 0.001f);
	}

	// AND THE PLACES THAT APPLY AN EFFECT PASS ITS TAGS. A bleed, a burn and a Cripple are laid on a plain target and
	// on one carrying an increase of 100 that requires the bleed tag. Time does not pass in this world, so the time
	// left on each is how long it was applied for.
	{
		FScopedSwinger Attacker(World, FVector(0.0f, 18000.0f, 0.0f));
		FScopedSwinger Unchanged(World, FVector(200.0f, 18000.0f, 0.0f));
		FScopedSwinger Wearer(World, FVector(400.0f, 18000.0f, 0.0f));
		TMap<FName, FCataclysmStatInputs> Inputs;
		CarryOnAHundred(Inputs, DurationStat, ECataclysmStatBucket::Increased, 100.0f, Bleed);
		Wearer.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		for (FScopedSwinger* Target : {&Unchanged, &Wearer})
		{
			UCataclysmSkillEffects::ApplyDamageOverTime(Attacker.Actor, Target->Actor, /*DamagePerTick=*/10.0f, /*DurationSeconds=*/5.0f, Bleed);
			UCataclysmSkillEffects::ApplyDamageOverTime(Attacker.Actor, Target->Actor, /*DamagePerTick=*/10.0f, /*DurationSeconds=*/5.0f, Burn);
			UCataclysmSkillEffects::ApplyTagForDuration(Attacker.Actor, Target->Actor, Cripple, /*DurationSeconds=*/5.0f);
		}
		const float BleedLasts = SecondsLeftOfTag(Unchanged.AbilitySystem, Bleed);
		const float BurnLasts = SecondsLeftOfTag(Unchanged.AbilitySystem, Burn);
		const float CrippleLasts = SecondsLeftOfTag(Unchanged.AbilitySystem, Cripple);
		if (TestTrue(TEXT("set-up: the plain target carries all three for some time"),
					 BleedLasts > 0.0f && BurnLasts > 0.0f && CrippleLasts > 0.0f))
		{
			TestEqual(TEXT("a bleed applied to the wearer lasts twice as long as on the plain target"),
					  SecondsLeftOfTag(Wearer.AbilitySystem, Bleed), BleedLasts * 2.0f, 0.05f);
			TestEqual(TEXT("control: a burn applied to the wearer lasts as long as on the plain target"),
					  SecondsLeftOfTag(Wearer.AbilitySystem, Burn), BurnLasts, 0.05f);
			TestEqual(TEXT("control: and a Cripple too"), SecondsLeftOfTag(Wearer.AbilitySystem, Cripple), CrippleLasts, 0.05f);
		}
	}

	// "UNAFFECTED BY BLEEDING": a flag requiring the bleed tag. A bleed is refused and its applier's `dot_applied`
	// event is not raised; a burn is applied and raises it; and converted damage, which arrives as a bleed the
	// character lays on themselves, is not refused.
	{
		FScopedSwinger Attacker(World, FVector(0.0f, 21000.0f, 0.0f));
		FScopedSwinger Wearer(World, FVector(200.0f, 21000.0f, 0.0f));
		FCataclysmStatModifier Immune;
		Immune.Bucket = ECataclysmStatBucket::Flat;
		Immune.Source = ECataclysmModifierSource::Enchantment;
		Immune.Value = 1.0f;
		Immune.RequiredTags.AddTag(Bleed);
		TMap<FName, FCataclysmStatInputs> Inputs;
		Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::AilmentImmunityStat)).Modifiers.Add(Immune);
		Wearer.AbilitySystem->SetStatInputs(MoveTemp(Inputs));

		// THE APPLIER CARRIES A ROW THAT LAYS CRIPPLE ON THEM EACH TIME THEY APPLY A DAMAGE OVER TIME, which is how
		// the event being raised is observed.
		FCataclysmPoolAction OnApplying;
		OnApplying.Event = FName(TEXT("dot_applied"));
		OnApplying.ApplyStatus = ECataclysmApplyStatus::Chance;
		OnApplying.bStatusOnTheWearer = true;
		OnApplying.StatusName = TEXT("Cripple");
		OnApplying.Percent = 100.0f;
		OnApplying.TriggerKey = FName(TEXT("observes dot_applied"));
		Attacker.AbilitySystem->SetPoolActions({OnApplying});

		const bool bBleedApplied = UCataclysmSkillEffects::ApplyDamageOverTime(
			Attacker.Actor, Wearer.Actor, /*DamagePerTick=*/10.0f, /*DurationSeconds=*/5.0f, Bleed);
		TestFalse(TEXT("a bleed applied to a character unaffected by bleeding is not applied"),
				  bBleedApplied || UCataclysmDebuffs::IsBleeding(Wearer.AbilitySystem));
		TestFalse(TEXT("and its applier's dot_applied event is not raised"), Attacker.AbilitySystem->HasMatchingGameplayTag(Cripple));

		const bool bBurnApplied = UCataclysmSkillEffects::ApplyDamageOverTime(
			Attacker.Actor, Wearer.Actor, /*DamagePerTick=*/10.0f, /*DurationSeconds=*/5.0f, Burn);
		TestTrue(TEXT("control: a burn is applied to the same character"),
				 bBurnApplied && Wearer.AbilitySystem->HasMatchingGameplayTag(Burn));
		TestTrue(TEXT("control: and that application raises the applier's dot_applied event"),
				 Attacker.AbilitySystem->HasMatchingGameplayTag(Cripple));

		const bool bConvertedApplied = UCataclysmSkillEffects::ApplyDamageOverTime(
			Wearer.Actor, Wearer.Actor, /*DamagePerTick=*/10.0f, /*DurationSeconds=*/5.0f, Bleed,
			/*bScalesWithInstigator=*/false, /*DealtBy=*/nullptr, /*Skill=*/nullptr, NAME_None, /*bIsConvertedDamage=*/true);
		TestTrue(TEXT("converted damage arriving as a bleed is not refused"),
				 bConvertedApplied && UCataclysmDebuffs::IsBleeding(Wearer.AbilitySystem));
	}

	// "10%-20% OF BLEED DAMAGE YOU TAKE IS TAKEN FROM YOUR ENERGY SHIELD": the share is taken as far as the shield has
	// it, and every tick that is not a bleed meets the shield whole, as it did.
	{
		FScopedSwinger Unchanged(World, FVector(0.0f, 24000.0f, 0.0f));
		FScopedSwinger Wearer(World, FVector(200.0f, 24000.0f, 0.0f));
		for (FScopedSwinger* One : {&Unchanged, &Wearer})
		{
			One->Set(Vital::GetMaxEnergyShieldAttribute(), 500.0f);
			One->Set(Vital::GetEnergyShieldAttribute(), 500.0f);
		}
		GrantFlat(Wearer.Actor, UCataclysmDamageCalculation::BleedDamageTakenFromEnergyShieldStat, 20.0f);
		const FCataclysmDamageResult BurnOnUnchanged = ResolveATick(BurnTick, Unchanged.AbilitySystem);
		const FCataclysmDamageResult BurnOnWearer = ResolveATick(BurnTick, Wearer.AbilitySystem);
		TestTrue(TEXT("set-up: a shield takes a burn tick"), BurnOnUnchanged.AbsorbedByShield > 0.0f);
		TestEqual(TEXT("control: the wearer's shield takes the same of a burn tick as a plain character's"),
				  BurnOnWearer.AbsorbedByShield, BurnOnUnchanged.AbsorbedByShield, 0.001f);
		TestEqual(TEXT("control: and their health the same"), BurnOnWearer.DealtToHealth, BurnOnUnchanged.DealtToHealth, 0.001f);

		Wearer.Set(Vital::GetEnergyShieldAttribute(), 5.0f);
		const FCataclysmDamageResult OnALowShield = ResolveATick(BleedTick, Wearer.AbilitySystem);
		TestEqual(TEXT("a shield holding 5 takes 5 of a bleed tick whose fifth is more"), OnALowShield.AbsorbedByShield, 5.0f, 0.001f);
		TestEqual(TEXT("and health takes all the rest"), OnALowShield.DealtToHealth, PlainBleed - 5.0f, 0.01f);
	}

	// "DoTs DEAL DAMAGE TO YOUR MANA POOL FIRST": a tick larger than the mana held empties it and the rest reaches
	// health; a hit takes no mana; and a skill cost the mana left cannot pay finds no pool to pay it.
	{
		FScopedSwinger Attacker(World, FVector(0.0f, 27000.0f, 0.0f));
		FScopedSwinger Unchanged(World, FVector(200.0f, 27000.0f, 0.0f));
		FScopedSwinger Wearer(World, FVector(400.0f, 27000.0f, 0.0f));
		GrantFlat(Wearer.Actor, UCataclysmDamageCalculation::DamageOverTimeTakenFromManaFirstStat, 1.0f);
		const float HealthBefore = Wearer.Get(Vital::GetHealthAttribute());
		const float ManaBefore = Wearer.Get(Vital::GetManaAttribute());

		UCataclysmSkillEffects::ApplyDirectDamage(Attacker.Actor, Wearer.Actor, 300.0f);
		TestEqual(TEXT("control: a hit takes none of the wearer's mana"), Wearer.Get(Vital::GetManaAttribute()), ManaBefore, 0.001f);
		const float HealthAfterTheHit = Wearer.Get(Vital::GetHealthAttribute());
		TestTrue(TEXT("control: and the wearer can pay a cost of 40 while they hold their mana"),
				 UCataclysmGameplayAbility::PoolPaying(Wearer.AbilitySystem, 40.0f).IsValid());

		FCataclysmHitDelivery AsATick;
		AsATick.bIsDamageOverTime = true;
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker.Actor, Unchanged.Actor, 1500.0f, AsATick);
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker.Actor, Wearer.Actor, 1500.0f, AsATick);
		const float UnchangedLost = HealthBefore - Unchanged.Get(Vital::GetHealthAttribute());
		if (TestTrue(TEXT("set-up: the tick takes more from a plain character's health than the mana the wearer holds"),
					 UnchangedLost > ManaBefore))
		{
			TestEqual(TEXT("a tick larger than the mana held leaves the wearer no mana"), Wearer.Get(Vital::GetManaAttribute()), 0.0f, 0.001f);
			TestEqual(TEXT("and the rest of it is taken from health"),
					  HealthAfterTheHit - Wearer.Get(Vital::GetHealthAttribute()), UnchangedLost - ManaBefore, 0.01f);
			TestFalse(TEXT("and a cost of 40 then finds no pool to pay it, as for any character short of mana"),
					  UCataclysmGameplayAbility::PoolPaying(Wearer.AbilitySystem, 40.0f).IsValid());
		}
	}
	return true;
}

// --------------------------------------------------------------------------
// Two of the four one-site stats of 2026-10-07, each as a test of its own
// --------------------------------------------------------------------------
//
// EACH RUNS THE PROBE OF ITS STAT AND NOTHING ELSE. The probe is also run by
// `EveryStatWithNoAttributeIsActuallyRead` above; it is given a test of its own
// so that a break of its one site fails a test that names the sentence. The
// other two stats have theirs in `CataclysmPassiveTreeTests.cpp` (the arc) and
// `CataclysmEnemyBehaviourTests.cpp` (the root).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnergyShieldDamageTakenTest,
	"Cataclysm.OneSiteStats.AWearersEnergyShieldLosesMoreForTheSameBlowAndItsHealthTakesNoMoreThanTheBlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "Your energy shield takes 30%-50% increased damage". See `ProbeEnergyShieldDamageTaken` for where each stands. */
bool FCataclysmEnergyShieldDamageTakenTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;
	ProbeEnergyShieldDamageTaken(*this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLeechPayoutRateTest,
	"Cataclysm.OneSiteStats.ALeechPaymentMadeUnderTheRowPaysHalfAsFastAndTheSameTotalInTwiceTheTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "50% less tick rate for your leech effects". See `ProbeLeechPayoutRate` for where each stands. */
bool FCataclysmLeechPayoutRateTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;
	ProbeLeechPayoutRate(*this);
	return true;
}

// FOUR THINGS A WORN ROW MAKES A MOVEMENT SKILL DO. Ruled 2026-10-07: "Your movement abilities pull all nearby
// enemies to you on arrival", "Your movement ability deals 50%-100% of your weapon damage to all enemies along its
// path", "Your movement abilities now move you in a random direction" and "Your movement abilities cause an explosion
// at the starting and end locations". Each through a real Movement skill, against a user without the stat. In every
// case the user begins at the origin facing +X and, with no cursor, the skill is aimed 8 m along +X.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMovementPullRowTest,
	"Cataclysm.StatExemption.AMovementSkillPullsNearbyEnemiesToStandOneAndAHalfMetresFromItsUser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMovementPullRowTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	// A BLINK OF 8 M THAT STRIKES NOBODY, so it arrives at (8 m, 0). Five enemies, placed from where it arrives:
	//   0  3 m beyond it, at (11 m, 0)          pulled
	//   1  4.5 m to its side, at (8 m, 4.5 m)   pulled
	//   2  8 m to its other side, at (8 m, -8 m)  out of reach
	//   3  3 m to that side, at (8 m, -3 m)     in reach and immune to displacement
	//   4  1 m short of it, at (7 m, 0)         already nearer than 1.5 m
	const TArray<FVector> Five = {FVector(1100.0f, 0.0f, 0.0f), FVector(800.0f, 450.0f, 0.0f),
		FVector(800.0f, -800.0f, 0.0f), FVector(800.0f, -300.0f, 0.0f), FVector(700.0f, 0.0f, 0.0f)};
	const TCHAR* Blink = TEXT("Mode=Blink; Range=8");
	const FMovementRiderRun Plain = UseAMovementSkillAmong(Blink, MovementRiderTags,
		[](TMap<FName, FCataclysmStatInputs>&) {}, Five, /*ImmuneToDisplacement=*/3);
	const FMovementRiderRun Pulling = UseAMovementSkillAmong(Blink, MovementRiderTags,
		[](TMap<FName, FCataclysmStatInputs>& Inputs)
		{
			CarryFlat(Inputs, UCataclysmMovementSkill::PullsNearbyOnArrivalStat, 1.0f);
		}, Five, /*ImmuneToDisplacement=*/3);
	if (!TestTrue(TEXT("set-up: both users blinked"), Plain.bUsed && Pulling.bUsed)
		|| !TestTrue(TEXT("set-up: both arrived 8 m along +X"),
					 Plain.ArrivedAt.Equals(FVector(800.0f, 0.0f, 0.0f), 1.0) && Pulling.ArrivedAt.Equals(Plain.ArrivedAt, 1.0))
		|| !TestTrue(TEXT("set-up: the fourth enemy is immune to displacement in both"),
					 Plain.bTheImmuneOneWasImmune && Pulling.bTheImmuneOneWasImmune))
	{
		return false;
	}

	// CONTROL: WITH NO ROW NOBODY MOVES, so every move below is the row's.
	for (int32 Index = 0; Index < Five.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("control: with no row enemy %d stays where it stood"), Index),
				 Plain.EndedAt[Index].Equals(Five[Index], 0.5));
	}

	TestEqual(TEXT("the enemy 3 m away ends 1.5 m from the user"),
			  static_cast<float>(FVector::Dist2D(Pulling.EndedAt[0], Pulling.ArrivedAt)), 150.0f, 1.0f);
	TestTrue(TEXT("on the line it was hauled along"), Pulling.EndedAt[0].Equals(FVector(950.0f, 0.0f, 0.0f), 1.0));
	TestEqual(TEXT("the enemy 4.5 m away ends 1.5 m from the user"),
			  static_cast<float>(FVector::Dist2D(Pulling.EndedAt[1], Pulling.ArrivedAt)), 150.0f, 1.0f);
	TestTrue(TEXT("the enemy 8 m away does not move"), Pulling.EndedAt[2].Equals(Five[2], 0.5));
	TestTrue(TEXT("the enemy immune to displacement does not move"), Pulling.EndedAt[3].Equals(Five[3], 0.5));
	TestTrue(TEXT("the enemy already nearer than 1.5 m does not move"), Pulling.EndedAt[4].Equals(Five[4], 0.5));
	for (int32 Index = 0; Index < Five.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("the pull takes no health from enemy %d"), Index), Pulling.Lost[Index], 0.0f, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChargePathRowTest,
	"Cataclysm.StatExemption.AChargeDealsTheRowsHitOnceToEachEnemyItsPathCrossed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmChargePathRowTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	const auto Nothing = [](TMap<FName, FCataclysmStatInputs>&) {};
	const auto Path75 = [](TMap<FName, FCataclysmStatInputs>& Inputs)
	{
		CarryFlat(Inputs, UCataclysmMovementSkill::PathDamagePercentStat, 75.0f);
	};

	// A CHARGE OF 8 M ALONG +X, 1.5 M TO EACH SIDE. Enemy 0 stands on the line at (3 m, 0); enemy 1 stands 1 m off
	// it at (6 m, 1 m), inside the half-width; enemy 2 stands 4 m off it at (4 m, 4 m), beside the path.
	const TArray<FVector> Three = {FVector(300.0f, 0.0f, 0.0f), FVector(600.0f, 100.0f, 0.0f), FVector(400.0f, 400.0f, 0.0f)};
	const TCHAR* Charge = TEXT("Mode=Charge; Range=8; Radius=1.5");
	const FMovementRiderRun Plain = UseAMovementSkillAmong(Charge, MovementRiderTags, Nothing, Three, INDEX_NONE, 75.0f);
	const FMovementRiderRun Carrying = UseAMovementSkillAmong(Charge, MovementRiderTags, Path75, Three, INDEX_NONE, 75.0f);
	if (!TestTrue(TEXT("set-up: both users charged"), Plain.bUsed && Carrying.bUsed)
		|| !TestTrue(TEXT("set-up: a plain hit of 75 per cent takes something from the control enemy"), Carrying.PlainHitLost > 1.0f)
		|| !TestTrue(TEXT("set-up: the plain charge's own blow hurt both enemies on its path"),
					 Plain.Lost[0] > 1.0f && Plain.Lost[1] > 1.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: the two users' plain hits are the same"), Plain.PlainHitLost, Carrying.PlainHitLost, 0.01f);
	TestEqual(TEXT("control: with no row the enemy beside the path loses nothing"), Plain.Lost[2], 0.0f, 0.001f);

	// WHAT THE ROW ADDS IS ONE PLAIN HIT OF 75 PER CENT, to each. Twice would be double this figure.
	TestEqual(TEXT("the enemy on the line loses one plain hit of 75 per cent more than under a plain charge"),
			  Carrying.Lost[0] - Plain.Lost[0], Carrying.PlainHitLost, 0.01f);
	TestEqual(TEXT("and so does the enemy inside the half-width"),
			  Carrying.Lost[1] - Plain.Lost[1], Carrying.PlainHitLost, 0.01f);
	TestEqual(TEXT("the enemy beside the path loses nothing"), Carrying.Lost[2], 0.0f, 0.001f);

	// A LEAP HAS NO PATH. The same enemies under a leap of 8 m that strikes nobody: the row adds nothing.
	const FMovementRiderRun Leaping = UseAMovementSkillAmong(TEXT("Mode=Leap; Range=8"), MovementRiderTags, Path75, Three);
	if (TestTrue(TEXT("the carrying user leapt"), Leaping.bUsed))
	{
		TestEqual(TEXT("an enemy under a leap's arc loses nothing to the row"), Leaping.Lost[0], 0.0f, 0.001f);
	}

	// A WALKED CHARGE, DRIVEN BY HAND IN TWO STEPS OF 4 M. One enemy at (4 m, 1 m) lies on the line of both steps.
	// It is struck by the first and remembered, so the row's hit reaches it once.
	const TArray<FVector> OneOnBothSteps = {FVector(400.0f, 100.0f, 0.0f)};
	const TArray<FVector> TwoSteps = {FVector(400.0f, 0.0f, 0.0f), FVector(800.0f, 0.0f, 0.0f)};
	const TCHAR* Walked = TEXT("Mode=Charge; Range=8; Radius=1.5; Duration=1");
	const FMovementRiderRun PlainWalk = UseAMovementSkillAmong(Walked, MovementRiderTags, Nothing, OneOnBothSteps, INDEX_NONE, 75.0f, TwoSteps);
	const FMovementRiderRun CarryingWalk = UseAMovementSkillAmong(Walked, MovementRiderTags, Path75, OneOnBothSteps, INDEX_NONE, 75.0f, TwoSteps);
	if (TestTrue(TEXT("both users began the walked charge"), PlainWalk.bUsed && CarryingWalk.bUsed)
		&& TestTrue(TEXT("set-up: the plain walk's own blow hurt the enemy"), PlainWalk.Lost[0] > 1.0f))
	{
		TestEqual(TEXT("over two steps the row adds one plain hit of 75 per cent, not two"),
				  CarryingWalk.Lost[0] - PlainWalk.Lost[0], CarryingWalk.PlainHitLost, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRandomDirectionRowTest,
	"Cataclysm.StatExemption.ARowSendsAnAimedMovementSkillInARolledDirectionAndLeavesATargetedOneAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRandomDirectionRowTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	const auto Nothing = [](TMap<FName, FCataclysmStatInputs>&) {};
	const auto Random = [](TMap<FName, FCataclysmStatInputs>& Inputs)
	{
		CarryFlat(Inputs, UCataclysmMovementSkill::RandomDirectionStat, 1.0f);
	};
	const TArray<FVector> Nobody;
	const TCHAR* Blink = TEXT("Mode=Blink; Range=8");
	// For the targeted skill: one enemy 5 m away behind the user, at (-3 m, -4 m).
	const TArray<FVector> OneBehind = {FVector(-300.0f, -400.0f, 0.0f)};
	const TCHAR* Hauled = TEXT("Mode=Charge; Range=12; Radius=1.5; Requires=Target");
	const TCHAR* Walked = TEXT("Mode=Charge; Range=8; Radius=1.5; Duration=1");

	FMovementRiderRun Aimed;
	FMovementRiderRun TowardY;
	FMovementRiderRun TargetedPlain;
	FMovementRiderRun TargetedCarrying;
	FMovementRiderRun WalkPlain;
	FMovementRiderRun WalkCarrying;
	{
		// THE ROLL IS PINNED AT 90 DEGREES, which is +Y. The control wears no row under the same pin.
		const FPinnedRoll Ninety(TEXT("Cataclysm.MovementDirectionRoll"), 90.0f);
		Aimed = UseAMovementSkillAmong(Blink, MovementRiderTags, Nothing, Nobody);
		TowardY = UseAMovementSkillAmong(Blink, MovementRiderTags, Random, Nobody);
		TargetedPlain = UseAMovementSkillAmong(Hauled, MovementRiderTags, Nothing, OneBehind);
		TargetedCarrying = UseAMovementSkillAmong(Hauled, MovementRiderTags, Random, OneBehind);
		WalkPlain = UseAMovementSkillAmong(Walked, MovementRiderTags, Nothing, Nobody);
		WalkCarrying = UseAMovementSkillAmong(Walked, MovementRiderTags, Random, Nobody);
	}
	FMovementRiderRun TowardMinusX;
	{
		// AND AT 180 DEGREES, which is -X.
		const FPinnedRoll OneEighty(TEXT("Cataclysm.MovementDirectionRoll"), 180.0f);
		TowardMinusX = UseAMovementSkillAmong(Blink, MovementRiderTags, Random, Nobody);
	}
	if (!TestTrue(TEXT("set-up: every user's skill ran"),
				  Aimed.bUsed && TowardY.bUsed && TowardMinusX.bUsed && TargetedPlain.bUsed && TargetedCarrying.bUsed
					  && WalkPlain.bUsed && WalkCarrying.bUsed))
	{
		return false;
	}

	TestTrue(TEXT("control: with no row the blink ends where it was aimed, 8 m along +X"),
			 Aimed.ArrivedAt.Equals(FVector(800.0f, 0.0f, 0.0f), 1.0));
	TestTrue(TEXT("with the roll at 90 the carrying user ends 8 m along +Y"),
			 TowardY.ArrivedAt.Equals(FVector(0.0f, 800.0f, 0.0f), 1.0));
	TestTrue(TEXT("with the roll at 180 the carrying user ends 8 m along -X"),
			 TowardMinusX.ArrivedAt.Equals(FVector(-800.0f, 0.0f, 0.0f), 1.0));
	TestTrue(TEXT("the two rolls end in two different places"), FVector::Dist2D(TowardY.ArrivedAt, TowardMinusX.ArrivedAt) > 400.0);
	TestEqual(TEXT("the first is as far from the start as the aimed blink"),
			  static_cast<float>(TowardY.ArrivedAt.Size2D()), static_cast<float>(Aimed.ArrivedAt.Size2D()), 1.0f);
	TestEqual(TEXT("and so is the second"),
			  static_cast<float>(TowardMinusX.ArrivedAt.Size2D()), static_cast<float>(Aimed.ArrivedAt.Size2D()), 1.0f);

	// A SKILL THAT TRAVELS TO A CREATURE IS UNCHANGED: it arrives at the enemy with the row as without.
	TestTrue(TEXT("control: with no row the targeted skill arrives at its enemy"),
			 FVector::Dist2D(TargetedPlain.ArrivedAt, OneBehind[0]) < 1.0);
	TestTrue(TEXT("and with the row and the roll at 90 it arrives at the same enemy"),
			 FVector::Dist2D(TargetedCarrying.ArrivedAt, OneBehind[0]) < 1.0);

	// AND A WALKED CHARGE TAKES THE ROLLED DIRECTION WHEN IT BEGINS.
	TestTrue(TEXT("control: with no row a walked charge walks the way it was aimed, +X"),
			 WalkPlain.WalkingToward.Equals(FVector(1.0f, 0.0f, 0.0f), 0.001));
	TestTrue(TEXT("with the row and the roll at 90 it walks toward +Y"),
			 WalkCarrying.WalkingToward.Equals(FVector(0.0f, 1.0f, 0.0f), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEndExplosionsRowTest,
	"Cataclysm.StatExemption.AMovementSkillExplodesWhereItBeganAndWhereItArrivedForATenthOfItsHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEndExplosionsRowTest::RunTest(const FString&)
{
	using namespace CataclysmStatExemptionTest;

	// A LEAP OF 8 M WHOSE OWN BLOW REACHES 2 M AROUND WHERE IT LANDS, at (8 m, 0). Four enemies:
	//   0  2.8 m from where it began, at (0, 2.8 m)        outside the leap's own blow, inside the first explosion
	//   1  2.8 m from where it lands, at (8 m, 2.8 m)      outside the leap's own blow, inside the second explosion
	//   2  5 m from both, at (4 m, 3 m)                    outside everything
	//   3  2 m from where it lands, at (8 m, -2 m)         THE CONTROL ENEMY: it takes the leap's own blow
	const TArray<FVector> Four = {FVector(0.0f, 280.0f, 0.0f), FVector(800.0f, 280.0f, 0.0f),
		FVector(400.0f, 300.0f, 0.0f), FVector(800.0f, -200.0f, 0.0f)};
	const TCHAR* Leap = TEXT("Mode=Leap; Range=8; Radius=2");
	const FMovementRiderRun Plain = UseAMovementSkillAmong(Leap, MovementRiderTags,
		[](TMap<FName, FCataclysmStatInputs>&) {}, Four);
	const FMovementRiderRun Exploding = UseAMovementSkillAmong(Leap, MovementRiderTags,
		[](TMap<FName, FCataclysmStatInputs>& Inputs)
		{
			CarryFlat(Inputs, UCataclysmMovementSkill::ExplodesAtBothEndsStat, 1.0f);
		}, Four);
	if (!TestTrue(TEXT("set-up: both users leapt"), Plain.bUsed && Exploding.bUsed)
		|| !TestTrue(TEXT("set-up: the plain leap's own blow hurt the control enemy"), Plain.Lost[3] > 10.0f))
	{
		return false;
	}
	const float TheSkillsHit = Plain.Lost[3];

	// CONTROL: WITH NO ROW THE OTHER THREE LOSE NOTHING, so every loss below is the row's.
	TestEqual(TEXT("control: with no row the enemy near the start loses nothing"), Plain.Lost[0], 0.0f, 0.001f);
	TestEqual(TEXT("control: nor the enemy near the end"), Plain.Lost[1], 0.0f, 0.001f);
	TestEqual(TEXT("control: nor the enemy 5 m from both"), Plain.Lost[2], 0.0f, 0.001f);

	TestEqual(TEXT("the enemy within 3 m of the start loses a tenth of the skill's hit"),
			  Exploding.Lost[0], TheSkillsHit / 10.0f, 0.01f);
	TestEqual(TEXT("the enemy within 3 m of the end loses a tenth of the skill's hit"),
			  Exploding.Lost[1], TheSkillsHit / 10.0f, 0.01f);
	TestEqual(TEXT("the enemy 5 m from both loses nothing"), Exploding.Lost[2], 0.0f, 0.001f);
	TestEqual(TEXT("the enemy the leap struck loses that hit and a tenth of it"),
			  Exploding.Lost[3], TheSkillsHit * 1.1f, 0.01f);
	return true;
}

#endif  // WITH_AUTOMATION_TESTS
