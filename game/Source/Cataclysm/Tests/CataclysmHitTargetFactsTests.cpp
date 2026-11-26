// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * Facts of the target of a hit (the Death trees' slice C6b1, 2026-11-25): struck from behind (From Behind), at full health
 * and not yet struck (Opening Strike), carrying your Poison (Venomed Prey), and beyond metres (Hit and Run). Four tests
 * ask the stat pipeline against a real creature with rows given by hand; one reads the real rows.
 */

namespace CataclysmHitTargetFactsTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float FactMetre = 100.0f;

	/** A character on the players' side at `Where`, facing along +X. */
	UCataclysmAbilitySystemComponent* AskerAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Holder = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (!Holder)
		{
			return nullptr;
		}
		Holder->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Holder->SetAttackDamage(0.0f);
		return Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent());
	}

	/** A creature of the monsters' side at `Where`, facing along +X, with 1,000 health. */
	ACataclysmEnemyCharacter* CreatureAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(1000.0f);
		}
		return Made;
	}

	FCataclysmStatModifier FactRow(ECataclysmStatBucket Bucket, float Value, ECataclysmStatCondition Condition,
								   float ConditionValue = 0.0f)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Condition = Condition;
		Row.ConditionValue = ConditionValue;
		return Row;
	}

	/** One stat line at `Base` with `Rows`, and nothing else. */
	void GiveLine(UCataclysmAbilitySystemComponent* System, const TCHAR* Stat, float Base,
				  TArray<FCataclysmStatModifier> Rows)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Line = Lines.Add(FName(Stat));
		Line.Base = Base;
		Line.Modifiers = MoveTemp(Rows);
		System->SetStatInputs(MoveTemp(Lines));
	}

	/** The stat against `Target`, at `DistanceMetres` (-1 for not known), with `Tags`. */
	float Against(const UCataclysmAbilitySystemComponent* System, const TCHAR* Stat, const AActor* Target,
				  float DistanceMetres = -1.0f, const FGameplayTagContainer& Tags = FGameplayTagContainer())
	{
		return System->StatForSkill(FName(Stat), Tags, 0.0f, -1.0f, FCataclysmBlowContext(), -1.0f, DistanceMetres, false,
									Target);
	}

	bool LayAilmentOf(AActor* Applier, AActor* Carrier, const TCHAR* Ailment)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(Ailment), /*ErrorIfNotFound=*/false);
		return Tag.IsValid()
			&& UCataclysmSkillEffects::ApplyDamageOverTime(Applier, Carrier, 1.0f, 10.0f, Tag,
														   /*bScalesWithInstigator=*/false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHitTargetFactsBehindTest,
	"Cataclysm.HitTargetFacts.FromBehindHoldsInTheRearArcAndForARearHitsSkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * From Behind's row, 3% increased crit chance on a base of 10 when struck from behind: against a creature facing +X, an
 * asker 3 metres behind it (at -X) reads 10.3; one 3 metres in front reads 10. THE RULE: the one in front, marked as
 * striking from behind by a `RearHits=1` skill, reads 10.3, and 10 again once the mark is cleared.
 */
bool FCataclysmHitTargetFactsBehindTest::RunTest(const FString&)
{
	using namespace CataclysmHitTargetFactsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const float M = FactMetre;
	ACataclysmEnemyCharacter* Creature = CreatureAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Behind = AskerAt(World, FVector(-3 * M, 0, 0));
	UCataclysmAbilitySystemComponent* InFront = AskerAt(World, FVector(3 * M, 0, 0));
	if (!Creature || !TestNotNull(TEXT("set-up: an asker behind"), Behind)
		|| !TestNotNull(TEXT("set-up: an asker in front"), InFront))
	{
		return false;
	}
	for (UCataclysmAbilitySystemComponent* Each : {Behind, InFront})
	{
		GiveLine(Each, TEXT("crit_chance"), 10.0f,
				 {FactRow(ECataclysmStatBucket::Increased, 3.0f, ECataclysmStatCondition::TargetStruckFromBehind)});
	}
	TestEqual(TEXT("from behind: 10.3"), Against(Behind, TEXT("crit_chance"), Creature), 10.3f, 0.001f);
	TestEqual(TEXT("control: in front, 10"), Against(InFront, TEXT("crit_chance"), Creature), 10.0f, 0.001f);
	InFront->SetStrikingFromBehindByRule(true);
	TestEqual(TEXT("in front, under a RearHits=1 skill's mark: 10.3"), Against(InFront, TEXT("crit_chance"), Creature),
			  10.3f, 0.001f);
	InFront->SetStrikingFromBehindByRule(false);
	TestEqual(TEXT("and 10 once the mark is cleared"), Against(InFront, TEXT("crit_chance"), Creature), 10.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHitTargetFactsOpeningTest,
	"Cataclysm.HitTargetFacts.OpeningStrikeHoldsOnAFirstHitOnACreatureAtFullHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Opening Strike's crit row, flat 100 under `target_not_yet_struck_by_you` and `target_at_full_health`: 100 against a
 * creature at full health this asker has not struck. THE CONTROLS: 0 against a creature at 999 of 1,000, and 0 against
 * one at full health the asker has already struck.
 */
bool FCataclysmHitTargetFactsOpeningTest::RunTest(const FString&)
{
	using namespace CataclysmHitTargetFactsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const float M = FactMetre;
	UCataclysmAbilitySystemComponent* System = AskerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Fresh = CreatureAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Scratched = CreatureAt(World, FVector(-3 * M, 0, 0));
	ACataclysmEnemyCharacter* Struck = CreatureAt(World, FVector(0, 3 * M, 0));
	if (!TestNotNull(TEXT("set-up: an asker"), System) || !Fresh || !Scratched || !Struck)
	{
		AddError(TEXT("set-up: three creatures"));
		return false;
	}
	FCataclysmStatModifier Row =
		FactRow(ECataclysmStatBucket::Flat, 100.0f, ECataclysmStatCondition::TargetNotYetStruckByYou);
	Row.Condition2 = ECataclysmStatCondition::TargetAtFullHealth;
	GiveLine(System, TEXT("crit_chance"), 0.0f, {Row});
	if (UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Scratched))
	{
		Theirs->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 999.0f);
	}
	if (UCataclysmAbilitySystemComponent* Theirs =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Struck)))
	{
		Theirs->NoteStruckBy(System, /*bCritical=*/false);
	}

	TestEqual(TEXT("a first hit at full health: 100"), Against(System, TEXT("crit_chance"), Fresh), 100.0f, 0.001f);
	TestEqual(TEXT("control: at 999 of 1,000, 0"), Against(System, TEXT("crit_chance"), Scratched), 0.0f, 0.001f);
	TestEqual(TEXT("control: at full health, already struck, 0"), Against(System, TEXT("crit_chance"), Struck), 0.0f,
			  0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHitTargetFactsPoisonTest,
	"Cataclysm.HitTargetFacts.VenomedPreyCountsYourPoisonAndNotYourBleed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Venomed Prey's row, 2% increased attack damage on a base of 100 against a creature carrying your Poison: 102 against
 * one this asker poisoned. THE CONTROLS: 100 against one this asker made bleed, and 100 against one another character
 * poisoned.
 */
bool FCataclysmHitTargetFactsPoisonTest::RunTest(const FString&)
{
	using namespace CataclysmHitTargetFactsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const float M = FactMetre;
	UCataclysmAbilitySystemComponent* System = AskerAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Other = AskerAt(World, FVector(0, 20 * M, 0));
	ACataclysmEnemyCharacter* Poisoned = CreatureAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Bleeding = CreatureAt(World, FVector(-3 * M, 0, 0));
	ACataclysmEnemyCharacter* TheirPoison = CreatureAt(World, FVector(0, 3 * M, 0));
	if (!TestNotNull(TEXT("set-up: an asker"), System) || !TestNotNull(TEXT("set-up: another"), Other) || !Poisoned
		|| !Bleeding || !TheirPoison)
	{
		AddError(TEXT("set-up: three creatures"));
		return false;
	}
	AActor* Mine = System->GetAvatarActor();
	if (!TestTrue(TEXT("set-up: the asker poisons one"), LayAilmentOf(Mine, Poisoned, TEXT("Keyword.DoT.Poison")))
		|| !TestTrue(TEXT("set-up: and bleeds another"), LayAilmentOf(Mine, Bleeding, TEXT("Keyword.DoT.Bleed")))
		|| !TestTrue(TEXT("set-up: another character poisons a third"),
					 LayAilmentOf(Other->GetAvatarActor(), TheirPoison, TEXT("Keyword.DoT.Poison"))))
	{
		return false;
	}
	GiveLine(System, TEXT("attack_damage"), 100.0f,
			 {FactRow(ECataclysmStatBucket::Increased, 2.0f, ECataclysmStatCondition::TargetCarriesYourPoison)});
	TestEqual(TEXT("your Poison: 102"), Against(System, TEXT("attack_damage"), Poisoned), 102.0f, 0.001f);
	TestEqual(TEXT("control: your Bleed alone, 100"), Against(System, TEXT("attack_damage"), Bleeding), 100.0f, 0.001f);
	TestEqual(TEXT("control: another character's Poison, 100"), Against(System, TEXT("attack_damage"), TheirPoison),
			  100.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHitTargetFactsBeyondTest,
	"Cataclysm.HitTargetFacts.HitAndRunHoldsStrictlyBeyondFourMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Hit and Run's row, 3% increased Bleed damage beyond 4 metres, on a base of 100, asked with the Bleed's tags as a tick
 * asks it: 103 at 5 metres. THE CONTROLS: 100 at exactly 4 metres (strictly more than), 100 at 3, and 100 when the
 * distance is not known.
 */
bool FCataclysmHitTargetFactsBeyondTest::RunTest(const FString&)
{
	using namespace CataclysmHitTargetFactsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = AskerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = CreatureAt(World, FVector(5 * FactMetre, 0, 0));
	const FGameplayTag Bleed = FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Bleed")), false);
	if (!TestNotNull(TEXT("set-up: an asker"), System) || !Creature || !TestTrue(TEXT("set-up: Bleed"), Bleed.IsValid()))
	{
		return false;
	}
	FCataclysmStatModifier Row =
		FactRow(ECataclysmStatBucket::Increased, 3.0f, ECataclysmStatCondition::TargetBeyondMetres, 4.0f);
	Row.RequiredTags.AddTag(Bleed);
	GiveLine(System, TEXT("dot_damage"), 100.0f, {Row});
	const FGameplayTagContainer Tags(Bleed);
	TestEqual(TEXT("at 5 metres: 103"), Against(System, TEXT("dot_damage"), Creature, 5.0f, Tags), 103.0f, 0.001f);
	TestEqual(TEXT("control: at exactly 4, 100"), Against(System, TEXT("dot_damage"), Creature, 4.0f, Tags), 100.0f,
			  0.001f);
	TestEqual(TEXT("control: at 3, 100"), Against(System, TEXT("dot_damage"), Creature, 3.0f, Tags), 100.0f, 0.001f);
	TestEqual(TEXT("control: not known, 100"), Against(System, TEXT("dot_damage"), Creature, -1.0f, Tags), 100.0f,
			  0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHitTargetFactsRealRowsTest,
	"Cataclysm.HitTargetFacts.TheFourNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The four nodes' real rows, read from the passive effects table: each stat, value and condition. */
bool FCataclysmHitTargetFactsRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	struct FExpected
	{
		const TCHAR* Node;
		int32 Option;
		const TCHAR* Stat;
		const TCHAR* Condition;
		float Value;
		int32 Count;
	};
	const FExpected Expected[] = {
		{TEXT("Shadow_basic_b_b0"), 0, TEXT("crit_chance"), TEXT("target_struck_from_behind"), 3.0f, 1},
		{TEXT("Shadow_capstone_25"), 1, TEXT("crit_chance"), TEXT("target_not_yet_struck_by_you"), 100.0f, 1},
		{TEXT("Shadow_capstone_25"), 1, TEXT("attack_damage"), TEXT("target_not_yet_struck_by_you"), 50.0f, 1},
		{TEXT("Shadow_basic_a_a2"), 0, TEXT("attack_damage"), TEXT("target_carries_your_poison"), 2.0f, 1},
		{TEXT("Shadow_basic_a_b2"), 0, TEXT("dot_damage"), TEXT("target_beyond_metres"), 3.0f, 1},
	};
	for (const FExpected& Want : Expected)
	{
		int32 Found = 0;
		for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(Want.Node)))
		{
			if (Row->Option != Want.Option || Row->Stat != Want.Stat)
			{
				continue;
			}
			++Found;
			TestEqual(*FString::Printf(TEXT("%s %s: the condition"), Want.Node, Want.Stat), Row->Condition,
					  FString(Want.Condition));
			TestEqual(*FString::Printf(TEXT("%s %s: the value"), Want.Node, Want.Stat), Row->ValuePerPoint, Want.Value,
					  0.001f);
		}
		TestEqual(*FString::Printf(TEXT("%s %s: rows"), Want.Node, Want.Stat), Found, Want.Count);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
