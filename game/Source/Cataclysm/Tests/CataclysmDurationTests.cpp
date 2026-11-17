// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The durations a character's own stats lengthen. Issue #1833, ruled 2026-09-30
 * under the owner's delegation: `skill_duration` lengthens every duration a
 * skill sets, `buff_duration` a self-buff skill's buff, and `debuff_duration`
 * every debuff the character places on an enemy.
 *
 * EACH TEST WRITES THE STAT LINES DIRECTLY, because what is under test is what
 * the engine does with the stats. The rows are measured in
 * CataclysmEnchantmentEffectTests.cpp, where a real enchantment is worn.
 */
namespace CataclysmDurationTest
{
	/** A bare actor with an ability system and the sets a tag and a tick need. */
	struct FFighter
	{
		explicit FFighter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		/** Replace the stat lines with one increase to this stat. */
		void Increase(const TCHAR* Stat, float Percent) const
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Increased;
			Row.Source = ECataclysmModifierSource::Enchantment;
			Row.Value = Percent;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Row};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		/** A self-buff skill granted in this slot, carrying that slot's tag. */
		UCataclysmSelfBuffSkill* Buff(ECataclysmAbilitySlot Slot, const TCHAR* SlotTag) const
		{
			const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
				UCataclysmSelfBuffSkill::StaticClass(), Slot, /*Level=*/1, Actor);
			FGameplayAbilitySpec* Spec =
				Handle.IsValid() ? AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmSelfBuffSkill* Skill =
				Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(SlotTag);
			}
			return Skill;
		}

		/** The longest time left on anything granting this tag, or nought. */
		float SecondsLeftOn(const FGameplayTag& Tag) const
		{
			float Longest = 0.0f;
			for (const float Seconds : AbilitySystem->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
			return Longest;
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDebuffDurationTest,
	"Cataclysm.Durations.ADebuffOnAnEnemyLastsLongerAndATagOnYourselfDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * With 100% increased `debuff_duration`, a five-second stun placed on an enemy
 * lasts ten, and the same tag placed on the instigator itself lasts five.
 * `UCataclysmSkillEffects::ApplyTagForDuration` lengthens only a tag on a
 * target hostile to the instigator: "debuffs you apply to enemies". Two actors
 * with no team are hostile to each other, and an actor is friendly to itself.
 */
bool FCataclysmDebuffDurationTest::RunTest(const FString&)
{
	using namespace CataclysmDurationTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FFighter Caster(World);
	FFighter Enemy(World);
	const FGameplayTag Stun = UCataclysmSkillEffects::StunnedTag();
	Caster.Increase(UCataclysmSkillEffects::DebuffDurationStat, 100.0f);

	TestTrue(TEXT("a stun lands on the enemy"),
			 UCataclysmSkillEffects::ApplyTagForDuration(Caster.Actor, Enemy.Actor, Stun, 5.0f));
	TestEqual(TEXT("100% increased debuff duration: the enemy's five seconds last ten"),
			  Enemy.SecondsLeftOn(Stun), 10.0f, 0.05f);

	TestTrue(TEXT("the same tag lands on the caster itself"),
			 UCataclysmSkillEffects::ApplyTagForDuration(Caster.Actor, Caster.Actor, Stun, 5.0f));
	TestEqual(TEXT("and on itself it lasts five: a tag on yourself is no debuff you applied"),
			  Caster.SecondsLeftOn(Stun), 5.0f, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDotDebuffDurationTest,
	"Cataclysm.Durations.DebuffDurationLengthensDamageOverTimeInOneBucket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A damage over time effect of four seconds lasts eight with 100% increased
 * `debuff_duration`, and ten with 50% more from `dot_duration` beside it: the
 * two sum in one bucket, 1 + 1.0 + 0.5, rather than multiplying to 3.
 */
bool FCataclysmDotDebuffDurationTest::RunTest(const FString&)
{
	using namespace CataclysmDurationTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FFighter Caster(World);
	const auto Lasts = [&Caster]()
	{
		return UCataclysmSkillEffects::DamageOverTimeNumbers(
			Caster.AbilitySystem, 100.0f, 4.0f, FGameplayTagContainer()).DurationSeconds;
	};
	TestEqual(TEXT("nothing granted: four seconds"), Lasts(), 4.0f, 0.001f);

	Caster.Increase(UCataclysmSkillEffects::DebuffDurationStat, 100.0f);
	TestEqual(TEXT("100% increased debuff duration: eight"), Lasts(), 8.0f, 0.001f);

	FCataclysmStatModifier Debuff;
	Debuff.Bucket = ECataclysmStatBucket::Increased;
	Debuff.Source = ECataclysmModifierSource::Enchantment;
	Debuff.Value = 100.0f;
	FCataclysmStatModifier Dot = Debuff;
	Dot.Value = 50.0f;
	TMap<FName, FCataclysmStatInputs> Inputs;
	Inputs.FindOrAdd(FName(UCataclysmSkillEffects::DebuffDurationStat)).Modifiers = {Debuff};
	FCataclysmStatInputs& DotLine = Inputs.FindOrAdd(FName(TEXT("dot_duration")));
	DotLine.Base = 100.0f;
	DotLine.Modifiers = {Dot};
	Caster.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	TestEqual(TEXT("and 50% more dot duration beside it: ten, one bucket"), Lasts(), 10.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDurationTest,
	"Cataclysm.Durations.SkillDurationLengthensTheSkillsItsTagsReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `skill_duration` scoped to `Slot.Support` lengthens a Support skill's own
 * durations and leaves a Special skill's alone; `buff_duration` lengthens a
 * buff and not the skill's other durations. `OwnDurationMultiplier` is what
 * the buff, the effect, the mark and the terrain each read.
 */
bool FCataclysmSkillDurationTest::RunTest(const FString&)
{
	using namespace CataclysmDurationTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FFighter Caster(World);
	UCataclysmSelfBuffSkill* Support = Caster.Buff(ECataclysmAbilitySlot::Support, TEXT("Slot.Support"));
	UCataclysmSelfBuffSkill* Special = Caster.Buff(ECataclysmAbilitySlot::Special, TEXT("Slot.Special"));
	if (!TestNotNull(TEXT("a Support buff"), Support) || !TestNotNull(TEXT("a Special buff"), Special))
	{
		return false;
	}

	FCataclysmStatModifier Row;
	Row.Bucket = ECataclysmStatBucket::Increased;
	Row.Source = ECataclysmModifierSource::Enchantment;
	Row.Value = 100.0f;
	Row.RequiredTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Slot.Support"));
	TMap<FName, FCataclysmStatInputs> Inputs;
	Inputs.FindOrAdd(FName(UCataclysmSkillTemplate::SkillDurationStat)).Modifiers = {Row};
	Caster.AbilitySystem->SetStatInputs(MoveTemp(Inputs));

	TestEqual(TEXT("a Support skill's own durations are doubled"),
			  Support->OwnDurationMultiplier(false), 2.0f, 0.001f);
	TestEqual(TEXT("and its buff too"), Support->OwnDurationMultiplier(true), 2.0f, 0.001f);
	TestEqual(TEXT("a Special skill's are not"), Special->OwnDurationMultiplier(false), 1.0f, 0.001f);

	Caster.Increase(UCataclysmSkillTemplate::BuffDurationStat, 50.0f);
	TestEqual(TEXT("50% increased buff duration: a buff 1.5"),
			  Support->OwnDurationMultiplier(true), 1.5f, 0.001f);
	TestEqual(TEXT("and the skill's other durations 1"),
			  Support->OwnDurationMultiplier(false), 1.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
