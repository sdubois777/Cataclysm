// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Damage taken from one source: melee attacks, ranged attacks, spells and
 * bosses. Issue #666.
 *
 * THE PROJECT OWNER'S RULING, 2026-09-11: "Each is its own 'more' or 'less'
 * multiplier on damage taken, used only for hits from that source. The 75% cap
 * on flat damage reduction does not limit it, and like every 'less' it can
 * remove at most 99% of the damage. Hits will need to record whether they are
 * ranged or a spell; today they record only melee."
 *
 * TWO LEVELS ARE TESTED. `Resolve` given a hand-built hit checks the arithmetic
 * and the four conditions. `ApplyHit` from a real attacker checks that the facts
 * travel: from a skill's tags onto the damage effect, and from the effect onto
 * the defender's hit. A build that forgot one of those steps would pass the
 * first level and do nothing in play.
 *
 * EVERY ROW TAKES 50% LESS, so a hit that meets its row is exactly half of one
 * that does not. The end-to-end tests compare two hits from the same attacker
 * rather than naming a number, so armour, resistance and the attacker's own
 * damage cancel out and only the row is measured.
 */
namespace CataclysmDamageBySourceTest
{
	using Combat = UCataclysmCombatAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	/** A bare actor holding every attribute set, usable as either side. */
	struct FScopedFighter
	{
		explicit FScopedFighter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAll =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAll);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// LARGE ENOUGH THAT NOTHING HERE APPROACHES DEATH, so the floor
			// `Resolve` puts on the health step never reports the health left
			// instead of the hit.
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetMaxHealthAttribute(), 1'000'000.0f);
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetHealthAttribute(), 1'000'000.0f);

			// Enough weapon damage that `ApplyHit` has something to deal.
			AbilitySystem->SetNumericAttributeBase(
				Combat::GetAttackDamageAttribute(), 100.0f);
		}

		~FScopedFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** The whole of a hand-built hit, with evasion and block pinned off. */
		float Taken(const FCataclysmIncomingHit& Hit) const
		{
			return UCataclysmDamageCalculation::Resolve(
				Hit, AbilitySystem, /*Tier=*/1, /*EvasionRoll=*/100.0f,
				/*BlockRoll=*/100.0f).DealtToHealth;
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/**
	 * The same, for a condition that compares a number. -25 at 6 metres is
	 * Standing Apart: "You take 25% less damage from enemies more than 6 metres
	 * away from you".
	 */
	FCataclysmStatModifier RowAtThreshold(ECataclysmStatCondition Condition,
										  float Value, float Threshold)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::More;
		Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
		Modifier.Value = Value;
		Modifier.Condition = Condition;
		Modifier.ConditionValue = Threshold;
		return Modifier;
	}

	/** One "more" row on damage taken, with a condition. -50 is 50% less. */
	FCataclysmStatModifier Row(ECataclysmStatCondition Condition, float Value)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::More;
		Modifier.Source = ECataclysmModifierSource::Enchantment;
		Modifier.Value = Value;
		Modifier.Condition = Condition;
		return Modifier;
	}

	/**
	 * Record a damage taken stat line holding these rows, the way
	 * `UCataclysmPlayerClassStats::ApplyTo` records one for a real player, and
	 * which the damage taken step asks for on every hit.
	 */
	void TakeDamageThrough(UCataclysmAbilitySystemComponent* AbilitySystem,
						   const TArray<FCataclysmStatModifier>& Rows)
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Taken =
			Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::DamageTakenStat));
		Taken.Base = UCataclysmDamageCalculation::NormalDamageTaken;
		Taken.Modifiers = Rows;
		AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	}

	/** A 400 point hit carrying these facts and nothing else. */
	FCataclysmIncomingHit HitOf(bool bMelee, bool bRanged, bool bSpell,
								bool bFromBoss = false)
	{
		FCataclysmIncomingHit Hit;
		Hit.Damage = 400.0f;
		Hit.bIsMelee = bMelee;
		Hit.bIsRanged = bRanged;
		Hit.bIsSpell = bSpell;
		Hit.bFromBoss = bFromBoss;
		return Hit;
	}

	/** A blow that may not critically strike, so two hits can be compared. */
	FCataclysmHitDelivery NoCritical()
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotCriticallyStrike = true;
		return Delivery;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one. See CataclysmForcedMovementTests.cpp.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageBySourceEachRowTest,
	"Cataclysm.DamageBySource.EachSourceRowMeetsOnlyHitsFromItsSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmDamageBySourceEachRowTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDamageBySourceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// ONE DEFENDER PER ROW, AND EVERY KIND OF HIT AGAINST EACH. A build that
	// read one fact for another fails on the line naming both; a build that
	// granted a row to every hit fails on the hit that says nothing.
	struct FSource
	{
		const TCHAR* What;
		ECataclysmStatCondition Condition;
	};
	const FSource Sources[] = {
		{ TEXT("melee attacks"), ECataclysmStatCondition::HitIsMeleeAttack },
		{ TEXT("ranged attacks"), ECataclysmStatCondition::HitIsRangedAttack },
		{ TEXT("spells"), ECataclysmStatCondition::HitIsSpell },
		{ TEXT("bosses"), ECataclysmStatCondition::OpponentIsBoss },
	};

	struct FKind
	{
		const TCHAR* What;
		FCataclysmIncomingHit Hit;
		ECataclysmStatCondition Meets;
	};
	const FKind Kinds[] = {
		{ TEXT("a melee hit"), HitOf(true, false, false),
		  ECataclysmStatCondition::HitIsMeleeAttack },
		{ TEXT("a ranged hit"), HitOf(false, true, false),
		  ECataclysmStatCondition::HitIsRangedAttack },
		{ TEXT("a spell"), HitOf(false, false, true),
		  ECataclysmStatCondition::HitIsSpell },
		{ TEXT("a boss's hit"), HitOf(false, false, false, true),
		  ECataclysmStatCondition::OpponentIsBoss },
		// `Always` is no row's condition, so this hit meets none of them.
		{ TEXT("a hit that says nothing"), HitOf(false, false, false),
		  ECataclysmStatCondition::Always },
	};

	for (const FSource& Source : Sources)
	{
		const FScopedFighter Defender(World);
		TakeDamageThrough(Defender.AbilitySystem, { Row(Source.Condition, -50.0f) });

		for (const FKind& Kind : Kinds)
		{
			const float Expected = Kind.Meets == Source.Condition ? 200.0f : 400.0f;
			TestEqual(FString::Printf(TEXT("50%% less from %s, against %s"),
									  Source.What, Kind.What),
				Defender.Taken(Kind.Hit), Expected, 0.01f);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageBySourceSpellIsNotAnAttackTest,
	"Cataclysm.DamageBySource.ASpellIsNotAMeleeOrRangedAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmDamageBySourceSpellIsNotAnAttackTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDamageBySourceTest;

	// A LABELLED JUDGEMENT, RECORDED IN docs/DECISIONS.md. The rows say "melee
	// attacks", "ranged attacks" and "spells", and Path of Exile draws the line
	// the same way: a skill is an attack or a spell, and projectile is a tag
	// either can carry. So a spell that fires a projectile is a spell, and not a
	// ranged attack. Two Demonic weapon skills, the heavy Wand and Staff, are
	// that shape.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	{
		const FScopedFighter Defender(World);
		TakeDamageThrough(Defender.AbilitySystem,
			{ Row(ECataclysmStatCondition::HitIsRangedAttack, -50.0f) });
		TestEqual(TEXT("a ranged attack meets the ranged row"),
			Defender.Taken(HitOf(false, true, false)), 200.0f, 0.01f);
		TestEqual(TEXT("a spell that fires a projectile does not"),
			Defender.Taken(HitOf(false, true, true)), 400.0f, 0.01f);
	}
	{
		const FScopedFighter Defender(World);
		TakeDamageThrough(Defender.AbilitySystem,
			{ Row(ECataclysmStatCondition::HitIsMeleeAttack, -50.0f) });
		TestEqual(TEXT("a melee attack meets the melee row"),
			Defender.Taken(HitOf(true, false, false)), 200.0f, 0.01f);
		TestEqual(TEXT("a spell struck in melee does not"),
			Defender.Taken(HitOf(true, false, true)), 400.0f, 0.01f);
	}
	{
		const FScopedFighter Defender(World);
		TakeDamageThrough(Defender.AbilitySystem,
			{ Row(ECataclysmStatCondition::HitIsSpell, -50.0f) });
		TestEqual(TEXT("and a spell that fires a projectile meets the spell row"),
			Defender.Taken(HitOf(false, true, true)), 200.0f, 0.01f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageBySourceSheetTest,
	"Cataclysm.DamageBySource.TheCharacterSheetShowsNoRowThatNeedsAHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmDamageBySourceSheetTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDamageBySourceTest;

	// THE CHARACTER SHEET ASKS WITH NO HIT IN HAND, which is every caller of
	// `StatForSkill` but the damage taken step. Every fact of an absent blow is
	// false, so a row that asks about the hit is left out of the sheet's figure,
	// the same way a low-health bonus is left out at full health.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedFighter Defender(World);
	TakeDamageThrough(Defender.AbilitySystem,
		{ Row(ECataclysmStatCondition::HitIsSpell, -50.0f) });
	const FName Taken(UCataclysmDamageCalculation::DamageTakenStat);

	TestEqual(TEXT("with no hit in hand, damage taken is the normal hundred"),
		Defender.AbilitySystem->StatForSkill(Taken, FGameplayTagContainer(), 0.0f),
		UCataclysmDamageCalculation::NormalDamageTaken, 0.01f);

	FCataclysmBlowContext Spell;
	Spell.bIsSpell = true;
	TestEqual(TEXT("and with a spell's hit in hand it is fifty"),
		Defender.AbilitySystem->StatForSkill(Taken, FGameplayTagContainer(), 0.0f,
											 /*SkillHealthCostPercent=*/-1.0f, Spell),
		50.0f, 0.01f);

	TestFalse(TEXT("the conditions built with no hit in hand know no source"),
		Defender.AbilitySystem->CurrentConditions().Blow.bIsSpell);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageBySourceCapTest,
	"Cataclysm.DamageBySource.ASourceRowIsOutsideTheDamageReductionCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmDamageBySourceCapTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDamageBySourceTest;

	// THE OWNER'S WORDS: "The 75% cap on flat damage reduction does not limit
	// it." A defender at the cap who takes 50% less from spells takes an eighth
	// of a spell. Had the row been added into damage reduction instead, the cap
	// would have held the spell at a quarter, the same as any other hit.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedFighter Defender(World);
	Defender.AbilitySystem->SetNumericAttributeBase(
		Combat::GetDamageReductionAttribute(), 75.0f);
	TakeDamageThrough(Defender.AbilitySystem,
		{ Row(ECataclysmStatCondition::HitIsSpell, -50.0f) });

	TestEqual(TEXT("75% damage reduction leaves a quarter of a melee hit"),
		Defender.Taken(HitOf(true, false, false)), 100.0f, 0.01f);
	TestEqual(TEXT("and a spell loses half of what is left"),
		Defender.Taken(HitOf(false, false, true)), 50.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageBySourceMultiplyTest,
	"Cataclysm.DamageBySource.TwoSourceRowsMultiply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmDamageBySourceMultiplyTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDamageBySourceTest;

	// "EACH IS ITS OWN MULTIPLIER." A boss's spell meets both rows, and the two
	// multiply: 0.5 x 1.2 = 0.6 of the hit, not 1 - 0.5 + 0.2 = 0.7.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedFighter Defender(World);
	TakeDamageThrough(Defender.AbilitySystem,
		{ Row(ECataclysmStatCondition::HitIsSpell, -50.0f),
		  Row(ECataclysmStatCondition::OpponentIsBoss, 20.0f) });

	TestEqual(TEXT("a boss's spell is 400 x 0.5 x 1.2"),
		Defender.Taken(HitOf(false, false, true, true)), 240.0f, 0.01f);
	TestEqual(TEXT("a boss's melee hit is 400 x 1.2"),
		Defender.Taken(HitOf(true, false, false, true)), 480.0f, 0.01f);
	TestEqual(TEXT("anything else's spell is 400 x 0.5"),
		Defender.Taken(HitOf(false, false, true)), 200.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageBySourceTickTest,
	"Cataclysm.DamageBySource.ADamageOverTimeTickIsNotAHitFromItsSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmDamageBySourceTickTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDamageBySourceTest;

	// "USED ONLY FOR HITS FROM THAT SOURCE." A damage over time tick is not a
	// hit, which is also why it can neither be evaded nor critically strike, so
	// a burn a spell left behind is not shrunk by less damage from spells.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedFighter Defender(World);
	TakeDamageThrough(Defender.AbilitySystem,
		{ Row(ECataclysmStatCondition::HitIsSpell, -50.0f) });

	FCataclysmIncomingHit Tick = HitOf(false, false, true);
	Tick.bIsDamageOverTime = true;

	TestEqual(TEXT("a spell's hit is halved"),
		Defender.Taken(HitOf(false, false, true)), 200.0f, 0.01f);
	TestEqual(TEXT("and a tick carrying the same fact is not"),
		Defender.Taken(Tick), 400.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageBySourceSkillTagsTest,
	"Cataclysm.DamageBySource.ASkillsTagsReachTheDefenderAsItsSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmDamageBySourceSkillTagsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDamageBySourceTest;

	// THE WHOLE JOURNEY, which is what the tests above cannot see. A skill's tags
	// are used where the blow is built and stop there; only what `ApplyTypedSpec`
	// puts on the damage effect reaches the defender. Until issue #666 that was
	// area, damage over time and melee, so a spell and a projectile both arrived
	// as nothing in particular.
	//
	// EACH CASE COMPARES TWO HITS FROM ONE ATTACKER: one on a defender carrying
	// the row and one on a defender carrying nothing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedFighter Attacker(World);
	const FScopedFighter Control(World);

	const auto Struck = [&Attacker](const FScopedFighter& Target, const TCHAR* Cell)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target.Actor, 100.0f,
			UCataclysmSkillShapes::TagsFromCell(Cell), NoCritical(), &Resolved);
		return Resolved.DealtToHealth;
	};

	struct FCase
	{
		const TCHAR* What;
		ECataclysmStatCondition Row;
		const TCHAR* Cell;
		bool bMeets;
	};
	const FCase Cases[] = {
		{ TEXT("a spell reaches the defender as a spell"),
		  ECataclysmStatCondition::HitIsSpell, TEXT("Type.Spell"), true },
		{ TEXT("and so does a spell that fires a projectile"),
		  ECataclysmStatCondition::HitIsSpell, TEXT("Type.Spell, Type.Projectile"), true },
		{ TEXT("a projectile reaches it as a ranged attack"),
		  ECataclysmStatCondition::HitIsRangedAttack, TEXT("Type.Projectile"), true },
		{ TEXT("and so does a skill tagged ranged"),
		  ECataclysmStatCondition::HitIsRangedAttack, TEXT("Type.Ranged"), true },
		{ TEXT("but a spell that fires a projectile is not a ranged attack"),
		  ECataclysmStatCondition::HitIsRangedAttack, TEXT("Type.Spell, Type.Projectile"), false },
		{ TEXT("a melee strike reaches it as a melee attack"),
		  ECataclysmStatCondition::HitIsMeleeAttack, TEXT("Type.Strike, Type.Melee"), true },
		{ TEXT("and a projectile is not one"),
		  ECataclysmStatCondition::HitIsMeleeAttack, TEXT("Type.Projectile"), false },
	};

	for (const FCase& Case : Cases)
	{
		const FScopedFighter Defender(World);
		TakeDamageThrough(Defender.AbilitySystem, { Row(Case.Row, -50.0f) });

		const float Plain = Struck(Control, Case.Cell);
		if (!TestTrue(FString::Printf(TEXT("%s: the hit lands for something"),
									  Case.What), Plain > 0.0f))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s (%s)"), Case.What, Case.Cell),
			Struck(Defender, Case.Cell), Case.bMeets ? Plain * 0.5f : Plain, 0.01f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageBySourceBossTest,
	"Cataclysm.DamageBySource.AHitFromABossSaysSoAndAHeraldsDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmDamageBySourceBossTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDamageBySourceTest;

	// A BOSS IS WHAT `ACataclysmEnemyCharacter::IsBoss` SAYS: the Boss and
	// Cataclysm Boss rarities. A Herald, one rung below, is not, which is the
	// line the stun rule already uses. The fact is read off the damage effect's
	// causer when the hit lands, so this drives a real hit from each.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const auto Spawn = [World](const FVector& Where)
	{
		return World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
	};
	ACataclysmEnemyCharacter* Boss = Spawn(FVector(0.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Herald = Spawn(FVector(1000.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Control = Spawn(FVector(0.0f, 1000.0f, 0.0f));
	ACataclysmEnemyCharacter* Defender = Spawn(FVector(1000.0f, 1000.0f, 0.0f));
	if (!TestNotNull(TEXT("a boss"), Boss) || !TestNotNull(TEXT("a Herald"), Herald)
		|| !TestNotNull(TEXT("a control"), Control)
		|| !TestNotNull(TEXT("a defender"), Defender))
	{
		return false;
	}

	Boss->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
	Herald->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep - 1);
	TestTrue(TEXT("the first boss rung is a boss"), Boss->IsBoss());
	TestFalse(TEXT("and the rung below it is not"), Herald->IsBoss());
	Boss->SetAttackDamage(100.0f);
	Herald->SetAttackDamage(100.0f);

	// ON THE PLAYER'S SIDE, AS A PLAYER WOULD BE, with health enough for every
	// hit here.
	for (ACataclysmEnemyCharacter* Target : { Control, Defender })
	{
		Target->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Target->SetHealth(1'000'000.0f);
	}
	UCataclysmAbilitySystemComponent* Guarded =
		Cast<UCataclysmAbilitySystemComponent>(Defender->GetAbilitySystemComponent());
	if (!TestNotNull(TEXT("the defender's ability system"), Guarded))
	{
		return false;
	}
	TakeDamageThrough(Guarded, { Row(ECataclysmStatCondition::OpponentIsBoss, -50.0f) });

	const auto Struck = [](ACataclysmEnemyCharacter* From, ACataclysmEnemyCharacter* Target)
	{
		FCataclysmDamageResult Resolved;
		UCataclysmSkillEffects::ApplyHit(From, Target, 100.0f, FGameplayTagContainer(),
										 NoCritical(), &Resolved);
		return Resolved.DealtToHealth;
	};

	const float BossPlain = Struck(Boss, Control);
	const float HeraldPlain = Struck(Herald, Control);
	if (!TestTrue(TEXT("both hits land for something"),
				  BossPlain > 0.0f && HeraldPlain > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("a boss's hit meets the row"),
		Struck(Boss, Defender), BossPlain * 0.5f, 0.01f);
	TestEqual(TEXT("a Herald's hit does not"),
		Struck(Herald, Defender), HeraldPlain, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageBySourceTagsExistTest,
	"Cataclysm.DamageBySource.TheSourceTagsExistInTheVocabulary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCataclysmDamageBySourceTagsExistTest::RunTest(const FString& Parameters)
{
	// REQUESTED BY NAME, SO A LOST TAG WOULD FAIL IN SILENCE. Each is asked for
	// with ErrorIfNotFound false, so a vocabulary that had lost one would return
	// an invalid tag, nothing would be put on the damage effect, and the fact
	// would stop reaching the defender with no error anywhere.
	// `Cataclysm.DamageType.TheTwoDeliveryTagsExistInTheVocabulary` guards area
	// and damage over time the same way.
	TestTrue(TEXT("Type.Melee is a known tag"),
		UCataclysmDamageCalculation::MeleeTag().IsValid());
	TestTrue(TEXT("Type.Ranged is a known tag"),
		UCataclysmDamageCalculation::RangedTag().IsValid());
	TestTrue(TEXT("Type.Projectile is a known tag"),
		UCataclysmDamageCalculation::ProjectileTag().IsValid());
	TestTrue(TEXT("Type.Spell is a known tag"),
		UCataclysmSkillEffects::SpellTag().IsValid());
	TestNotEqual(TEXT("and ranged and projectile are two tags"),
		UCataclysmDamageCalculation::RangedTag(),
		UCataclysmDamageCalculation::ProjectileTag());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDamageBySourceDistanceTest,
	"Cataclysm.DamageBySource.ADistantAttackerTakesLessThroughTheWholePipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Standing Apart, through the real damage pipeline rather than through a
 * predicate on its own.
 *
 * WHY THIS EXISTS BESIDE THE TWO TESTS THAT ALREADY COVER THE CONDITION. Those
 * ask `ConditionHolds` directly, and one asks what a row's modifier carries.
 * Both would still pass if the distance never reached a hit at all. This is the
 * only test that resolves a real hit and reads what arrived, so it is the one
 * that fails if the reading is dropped anywhere between the hit being built and
 * the damage taken step asking for it.
 *
 * FOUR READINGS, AND THE THREE THAT CHANGE NOTHING ARE THE POINT. A test that
 * only showed the distant case would pass against a build that reduced every
 * hit.
 */
bool FCataclysmDamageBySourceDistanceTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDamageBySourceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedFighter Defender(World);

	// THE NODE'S OWN NUMBERS: 25% less damage, beyond 6 metres.
	TakeDamageThrough(Defender.AbilitySystem,
		{ RowAtThreshold(ECataclysmStatCondition::OpponentBeyondMetres,
						 -25.0f, 6.0f) });

	// A HIT OF 400 WITH NO DISTANCE ON IT AT ALL, which is what every hit in the
	// game carried before this reading existed. It must take the whole 400.
	FCataclysmIncomingHit Unknown = HitOf(true, false, false);
	const float Full = Defender.Taken(Unknown);
	TestEqual(TEXT("a hit with no distance on it takes the whole four hundred"),
		Full, 400.0f, 0.01f);

	// FROM BEYOND SIX METRES, A QUARTER LESS. This is the only reading here that
	// changes, and it is the sentence the node writes.
	FCataclysmIncomingHit FromAfar = Unknown;
	FromAfar.OpponentDistanceMetres = 10.0f;
	TestEqual(TEXT("and one from ten metres takes three hundred"),
		Defender.Taken(FromAfar), 300.0f, 0.01f);

	// FROM INSIDE SIX METRES, NOTHING CHANGES.
	FCataclysmIncomingHit FromClose = Unknown;
	FromClose.OpponentDistanceMetres = 2.0f;
	TestEqual(TEXT("and one from two metres takes the whole four hundred"),
		Defender.Taken(FromClose), 400.0f, 0.01f);

	// AND EXACTLY ON THE THRESHOLD, NOTHING CHANGES, because the node writes
	// "more than". A character standing at precisely six metres is not beyond
	// six. `Cataclysm.StatPipeline.ADistanceThresholdIsStrictAndAnUnknownDistanceRefuses`
	// checks the same boundary on the predicate alone; this checks a player
	// standing there really does take the full hit.
	FCataclysmIncomingHit OnTheLine = Unknown;
	OnTheLine.OpponentDistanceMetres = 6.0f;
	TestEqual(TEXT("and one from exactly six metres takes the whole four hundred"),
		Defender.Taken(OnTheLine), 400.0f, 0.01f);

	// AND A TICK FROM TEN METRES TAKES THE WHOLE FOUR HUNDRED, which is the
	// judgement `docs/DECISIONS.md` records rather than a limitation: a tick
	// hands a row no distance, so the row grants nothing for it.
	FCataclysmIncomingHit Tick = FromAfar;
	Tick.bIsDamageOverTime = true;
	TestEqual(TEXT("a tick from ten metres is not reduced at all"),
		Defender.Taken(Tick), 400.0f, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
