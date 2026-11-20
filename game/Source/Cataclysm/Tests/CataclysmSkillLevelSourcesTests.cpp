// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmRuntimeEffect.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmSkillLevels.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A skill's damage that is not its own direct hit earns it experience and grows with its level. Issue #2764, slice 2a,
 * accepted by the coordinating session on 2026-11-19, with the owner's ruling of the same day on flat figures: "Yes,
 * they scale too (Recommended)".
 *
 * WHAT IS CHECKED HERE, through the functions every such effect is laid by, so no gadget, trap, zone or axe has to be
 * built: a skill's burn carries the skill's row on its effect, every tick credits it, and its tick is sized by the
 * skill's level; a burn laid with only a carried row, the skill object gone, is credited and sized the same way; the
 * basic attack's burn and an unscaled burn laid with no skill (a spread copy's shape) carry no row; Void Splinter from a
 * skill is credited and not sized, being a share of health; a blow whose skill object has gone is sized and credited by
 * the row it carries (a gadget's or a trap's late shot, a buried axe's leap); a blow cast in the player's name is
 * credited (F1); and a kill by a skill's burn pays the skill.
 *
 * Registered under `Cataclysm.SkillLevelSources.`, which no other group's name begins with.
 */
namespace CataclysmSkillLevelSourcesTest
{
	using Levels = UCataclysmSkillLevels;

	const FName SourcesMoltenCleave(TEXT("Demonic_Greataxe_Heavy")); // the starting Greataxe's Heavy

	struct FSourcesPlayer
	{
		UWorld* World = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UAbilitySystemComponent* Abilities = nullptr;

		bool IsUsable() const { return World && State && Character && Abilities; }
	};

	/** The world, the player possessed, and its Greataxe's skills granted, as the skill level tests make it. */
	FSourcesPlayer MakeSourcesPlayer()
	{
		FSourcesPlayer Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Out.Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Out.Character);
			Out.Abilities = Out.Character->GetAbilitySystemComponent();
		}
		return Out;
	}

	/** The player's skill template whose row is `Row`, or null. */
	const UCataclysmSkillTemplate* SourcesSkillOfRow(UAbilitySystemComponent* Abilities, FName Row)
	{
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			const UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
			if (Skill && Skill->SkillRow == Row)
			{
				return Skill;
			}
		}
		return nullptr;
	}

	/** The player's basic attack, or null. */
	const UCataclysmSkillTemplate* SourcesBasicAttackOf(UAbilitySystemComponent* Abilities)
	{
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			const UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
			if (Skill && Skill->GetDesignedSlot() == ECataclysmAbilitySlot::BasicAttack)
			{
				return Skill;
			}
		}
		return nullptr;
	}

	/** A creature to lay effects on, apart from the others, at its default figures. */
	ACataclysmEnemyCharacter* SourcesCreature(UWorld* World, float X)
	{
		return World->SpawnActor<ACataclysmEnemyCharacter>(FVector(X, 0.0f, 0.0f), FRotator::ZeroRotator);
	}

	/** The running effect on `Creature` granting `Tag`, or null. */
	const FActiveGameplayEffect* SourcesRunning(ACataclysmEnemyCharacter* Creature, const FGameplayTag& Tag)
	{
		UAbilitySystemComponent* System = Creature ? Creature->GetAbilitySystemComponent() : nullptr;
		if (!System)
		{
			return nullptr;
		}
		const TArray<FActiveGameplayEffectHandle> Handles = System->GetActiveEffects(
			FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag)));
		return Handles.Num() > 0 ? System->GetActiveGameplayEffect(Handles[0]) : nullptr;
	}

	/** The row a running effect credits each tick to: None when it carries none. */
	FName SourcesRowOf(const FActiveGameplayEffect* Running)
	{
		const UCataclysmRuntimeEffect* Laid = Running ? Cast<UCataclysmRuntimeEffect>(Running->Spec.Def.Get()) : nullptr;
		return Laid ? Laid->SkillRowToCredit : NAME_None;
	}

	/** A running damage over time's figure a tick, before the struck creature's defences: -1 when there is none. */
	float SourcesPerTickOf(const FActiveGameplayEffect* Running)
	{
		const UGameplayEffect* Def = Running ? Running->Spec.Def.Get() : nullptr;
		if (!Def)
		{
			return -1.0f;
		}
		for (const FGameplayModifierInfo& Modifier : Def->Modifiers)
		{
			float Figure = 0.0f;
			if (Modifier.Attribute == UCataclysmVitalAttributeSet::GetDamageAttribute()
				&& Modifier.ModifierMagnitude.GetStaticMagnitudeIfPossible(1.0f, Figure))
			{
				return Figure;
			}
		}
		return -1.0f;
	}

	/** The health `Row` has removed from `Creature`: 0 when none. */
	float SourcesCreditedTo(const ACataclysmEnemyCharacter* Creature, FName Row)
	{
		const float* Held = Creature->GetHealthRemovedBySkill().Find(Row);
		return Held ? *Held : 0.0f;
	}
}

// `using namespace` inside each test, never at file scope: a unity build concatenates this file with others.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesBurnTest,
	"Cataclysm.SkillLevelSources.ASkillsBurnIsCreditedToItAndSizedByItsLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A burn Molten Cleave lays carries Molten Cleave's row on its effect, so each tick credits the skill; and its tick is
 * sized by the skill's level, read once when it is laid: level 3's is 1.0567 squared times level 1's (the owner's ruling
 * of 2026-11-19). The figures are the effects' own, before the creature's defences, so nothing but the level differs.
 */
bool FCataclysmSkillLevelSourcesBurnTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	FSourcesPlayer Player = MakeSourcesPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	const UCataclysmSkillTemplate* Heavy = Player.IsUsable() ? SourcesSkillOfRow(Player.Abilities, SourcesMoltenCleave) : nullptr;
	ACataclysmEnemyCharacter* AtOne = Player.World ? SourcesCreature(Player.World, 300.0f) : nullptr;
	ACataclysmEnemyCharacter* AtThree = Player.World ? SourcesCreature(Player.World, 600.0f) : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player holding Molten Cleave, and two creatures"), Heavy && AtOne && AtThree))
	{
		return false;
	}
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	Player.State->SetLevelAndExperience(4, 0);

	const bool bLaidAtOne = UCataclysmSkillEffects::ApplyDamageOverTime(Player.Character, AtOne, 10.0f, 3.0f, Burn,
		/*bScalesWithInstigator=*/true, /*DealtBy=*/nullptr, Heavy);
	Player.State->SetSkillExperience(SourcesMoltenCleave, Levels::ExperienceToReach(3));
	const int32 LevelThen = Player.State->SkillLevelOf(SourcesMoltenCleave);
	const bool bLaidAtThree = UCataclysmSkillEffects::ApplyDamageOverTime(Player.Character, AtThree, 10.0f, 3.0f, Burn,
		/*bScalesWithInstigator=*/true, /*DealtBy=*/nullptr, Heavy);
	const float TickAtOne = SourcesPerTickOf(SourcesRunning(AtOne, Burn));
	const float TickAtThree = SourcesPerTickOf(SourcesRunning(AtThree, Burn));
	const FName RowAtOne = SourcesRowOf(SourcesRunning(AtOne, Burn));
	CataclysmTestWorld::RunClock(Player.World, 3.5f);
	const float Credited = SourcesCreditedTo(AtOne, SourcesMoltenCleave);

	AddInfo(FString::Printf(TEXT("SKILL BURN: tick at level 1 %.4f, at level %d %.4f (x%.4f); row %s; credited %.3f"),
							TickAtOne, LevelThen, TickAtThree, TickAtOne > 0.0f ? TickAtThree / TickAtOne : 0.0f,
							*RowAtOne.ToString(), Credited));
	TestTrue(TEXT("set-up: both burns were laid and have a tick"), bLaidAtOne && bLaidAtThree && TickAtOne > 0.0f);
	TestEqual(TEXT("set-up: the skill stood at level 3 for the second"), LevelThen, 3);
	TestEqual(TEXT("THE BURN CARRIES MOLTEN CLEAVE'S ROW"), RowAtOne, SourcesMoltenCleave);
	TestEqual(TEXT("LEVEL 3'S TICK IS 1.0567 SQUARED TIMES LEVEL 1'S"), TickAtThree / TickAtOne,
			  static_cast<float>(Levels::DamageFactor(3)), 1.0e-4f);
	TestTrue(TEXT("AND ITS TICKS CREDITED MOLTEN CLEAVE"), Credited > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesCarriedRowTest,
	"Cataclysm.SkillLevelSources.ABurnLaidWithACarriedRowIsCreditedAndSized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A burn laid with no skill object and Molten Cleave's row handed in -- a projectile's, a buried axe's or a trap's
 * share's, laid after a one-use grant was removed -- carries the row and is sized at the skill's level, against a burn
 * laid with neither, which carries none and keeps its figure.
 */
bool FCataclysmSkillLevelSourcesCarriedRowTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	FSourcesPlayer Player = MakeSourcesPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	ACataclysmEnemyCharacter* Carried = Player.World ? SourcesCreature(Player.World, 300.0f) : nullptr;
	ACataclysmEnemyCharacter* Neither = Player.World ? SourcesCreature(Player.World, 600.0f) : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player and two creatures"), Player.IsUsable() && Carried && Neither))
	{
		return false;
	}
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	Player.State->SetLevelAndExperience(4, 0);
	Player.State->SetSkillExperience(SourcesMoltenCleave, Levels::ExperienceToReach(3));

	UCataclysmSkillEffects::ApplyDamageOverTime(Player.Character, Carried, 10.0f, 3.0f, Burn,
		/*bScalesWithInstigator=*/true, /*DealtBy=*/nullptr, /*Skill=*/nullptr, /*DamageType=*/NAME_None,
		/*bIsConvertedDamage=*/false, /*SkillTagsWithNoSkill=*/nullptr, /*SkillRowWithNoSkill=*/SourcesMoltenCleave);
	UCataclysmSkillEffects::ApplyDamageOverTime(Player.Character, Neither, 10.0f, 3.0f, Burn,
		/*bScalesWithInstigator=*/true);
	const float TickCarried = SourcesPerTickOf(SourcesRunning(Carried, Burn));
	const float TickNeither = SourcesPerTickOf(SourcesRunning(Neither, Burn));
	const FName RowCarried = SourcesRowOf(SourcesRunning(Carried, Burn));
	const FName RowNeither = SourcesRowOf(SourcesRunning(Neither, Burn));
	CataclysmTestWorld::RunClock(Player.World, 3.5f);

	AddInfo(FString::Printf(TEXT("CARRIED ROW BURN: tick %.4f with the row, %.4f with neither (x%.4f); rows %s and %s; "
								 "credited %.3f"),
							TickCarried, TickNeither, TickNeither > 0.0f ? TickCarried / TickNeither : 0.0f,
							*RowCarried.ToString(), *RowNeither.ToString(),
							SourcesCreditedTo(Carried, SourcesMoltenCleave)));
	TestTrue(TEXT("set-up: both burns have a tick"), TickCarried > 0.0f && TickNeither > 0.0f);
	TestEqual(TEXT("THE BURN WITH THE ROW CARRIES IT"), RowCarried, SourcesMoltenCleave);
	TestEqual(TEXT("AND IS SIZED AT THE SKILL'S LEVEL 3"), TickCarried / TickNeither,
			  static_cast<float>(Levels::DamageFactor(3)), 1.0e-4f);
	TestTrue(TEXT("CONTROL: a burn with neither carries no row"), RowNeither.IsNone());
	TestTrue(TEXT("AND THE CARRIED ROW'S TICKS CREDITED MOLTEN CLEAVE"),
			 SourcesCreditedTo(Carried, SourcesMoltenCleave) > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesNoRowTest,
	"Cataclysm.SkillLevelSources.TheBasicAttacksBurnAndAnUnscaledCopyCarryNoRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The basic attack does not level (the owner's ruling QC): its burn carries no row and keeps its figure with a skill at
 * level 3. And a burn laid unscaled with no skill, the shape of a spread copy, carries no row and credits nothing.
 */
bool FCataclysmSkillLevelSourcesNoRowTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	FSourcesPlayer Player = MakeSourcesPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	const UCataclysmSkillTemplate* Basic = Player.IsUsable() ? SourcesBasicAttackOf(Player.Abilities) : nullptr;
	ACataclysmEnemyCharacter* BasicAtOne = Player.World ? SourcesCreature(Player.World, 300.0f) : nullptr;
	ACataclysmEnemyCharacter* BasicAtThree = Player.World ? SourcesCreature(Player.World, 600.0f) : nullptr;
	ACataclysmEnemyCharacter* Copied = Player.World ? SourcesCreature(Player.World, 900.0f) : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player with its basic attack, and three creatures"),
				  Basic && BasicAtOne && BasicAtThree && Copied))
	{
		return false;
	}
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	Player.State->SetLevelAndExperience(4, 0);

	UCataclysmSkillEffects::ApplyDamageOverTime(Player.Character, BasicAtOne, 10.0f, 3.0f, Burn,
		/*bScalesWithInstigator=*/true, /*DealtBy=*/nullptr, Basic);
	Player.State->SetSkillExperience(SourcesMoltenCleave, Levels::ExperienceToReach(3));
	UCataclysmSkillEffects::ApplyDamageOverTime(Player.Character, BasicAtThree, 10.0f, 3.0f, Burn,
		/*bScalesWithInstigator=*/true, /*DealtBy=*/nullptr, Basic);
	UCataclysmSkillEffects::ApplyDamageOverTime(Player.Character, Copied, 10.0f, 3.0f, Burn,
		/*bScalesWithInstigator=*/false);
	const float TickOne = SourcesPerTickOf(SourcesRunning(BasicAtOne, Burn));
	const float TickThree = SourcesPerTickOf(SourcesRunning(BasicAtThree, Burn));
	const FName BasicRow = SourcesRowOf(SourcesRunning(BasicAtOne, Burn));
	const FName CopiedRow = SourcesRowOf(SourcesRunning(Copied, Burn));
	CataclysmTestWorld::RunClock(Player.World, 3.5f);
	const int32 RowsCredited = BasicAtOne->GetHealthRemovedBySkill().Num() + BasicAtThree->GetHealthRemovedBySkill().Num()
		+ Copied->GetHealthRemovedBySkill().Num();

	AddInfo(FString::Printf(TEXT("NO ROW: the basic attack's burn ticks %.4f and %.4f with a skill at level 1 and 3, row "
								 "%s; an unscaled copy's row %s; rows credited %d"),
							TickOne, TickThree, *BasicRow.ToString(), *CopiedRow.ToString(), RowsCredited));
	TestTrue(TEXT("set-up: the basic attack's burns have a tick"), TickOne > 0.0f && TickThree > 0.0f);
	TestTrue(TEXT("THE BASIC ATTACK'S BURN CARRIES NO ROW"), BasicRow.IsNone());
	TestEqual(TEXT("AND KEEPS ITS FIGURE"), TickThree, TickOne, 1.0e-4f);
	TestTrue(TEXT("AN UNSCALED BURN WITH NO SKILL CARRIES NO ROW"), CopiedRow.IsNone());
	TestEqual(TEXT("AND NONE OF THEM CREDITS A SKILL"), RowsCredited, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesVoidSplinterTest,
	"Cataclysm.SkillLevelSources.AVoidSplinterFromASkillIsCreditedAndNotSized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Void Splinter a skill lays carries the skill's row and credits it (F4: until slice 2a its skill was dropped), and it
 * is not sized by the level: it is a share of the creature's current health, which sizes itself. Two creatures at the
 * same health lose the same to it with the skill at level 1 and at level 3.
 */
bool FCataclysmSkillLevelSourcesVoidSplinterTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	FSourcesPlayer Player = MakeSourcesPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	const UCataclysmSkillTemplate* Heavy = Player.IsUsable() ? SourcesSkillOfRow(Player.Abilities, SourcesMoltenCleave) : nullptr;
	ACataclysmEnemyCharacter* AtOne = Player.World ? SourcesCreature(Player.World, 300.0f) : nullptr;
	ACataclysmEnemyCharacter* AtThree = Player.World ? SourcesCreature(Player.World, 600.0f) : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player holding Molten Cleave, and two creatures"), Heavy && AtOne && AtThree))
	{
		return false;
	}
	const FGameplayTag Splinter = UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Keyword.DoT.VoidSplinter")), /*ErrorIfNotFound=*/false);
	Player.State->SetLevelAndExperience(4, 0);
	const auto HealthOf = [](ACataclysmEnemyCharacter* Creature)
	{
		return Creature->GetAbilitySystemComponent()->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	};
	const float StartOne = HealthOf(AtOne);
	const float StartThree = HealthOf(AtThree);

	const bool bLaidAtOne = UCataclysmSkillEffects::ApplyShareOfHealthOverTime(Player.Character, AtOne, 0.01f, 3.0f,
		Splinter, /*bScalesWithInstigator=*/true, Heavy);
	Player.State->SetSkillExperience(SourcesMoltenCleave, Levels::ExperienceToReach(3));
	const bool bLaidAtThree = UCataclysmSkillEffects::ApplyShareOfHealthOverTime(Player.Character, AtThree, 0.01f, 3.0f,
		Splinter, /*bScalesWithInstigator=*/true, Heavy);
	const FName Row = SourcesRowOf(SourcesRunning(AtOne, Splinter));
	CataclysmTestWorld::RunClock(Player.World, 3.5f);
	const float LostOne = StartOne - HealthOf(AtOne);
	const float LostThree = StartThree - HealthOf(AtThree);

	AddInfo(FString::Printf(TEXT("SKILL SPLINTER: row %s; lost %.4f at level 1 and %.4f at level 3; credited %.4f"),
							*Row.ToString(), LostOne, LostThree, SourcesCreditedTo(AtOne, SourcesMoltenCleave)));
	TestTrue(TEXT("set-up: the tag is known and both were laid"), Splinter.IsValid() && bLaidAtOne && bLaidAtThree);
	TestTrue(TEXT("set-up: the creatures started at the same health and lost some"),
			 FMath::IsNearlyEqual(StartOne, StartThree) && LostOne > 0.0f);
	TestEqual(TEXT("VOID SPLINTER FROM A SKILL CARRIES ITS ROW"), Row, SourcesMoltenCleave);
	TestTrue(TEXT("AND CREDITS IT"), SourcesCreditedTo(AtOne, SourcesMoltenCleave) > 0.0f);
	TestEqual(TEXT("AND IS NOT SIZED BY THE LEVEL"), LostThree, LostOne, 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesLateBlowTest,
	"Cataclysm.SkillLevelSources.ABlowWhoseSkillHasGoneIsSizedAndCreditedByItsRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A blow that names no skill object and carries Molten Cleave's row -- a gadget's or a trap's late shot, a buried axe's
 * leap (F3) -- is priced at the skill's level, as the design document's row "What a level does" says of anything priced
 * from the skill's percentage, and credits the skill. Level 3's blow sends 1.0567 squared times level 1's.
 */
bool FCataclysmSkillLevelSourcesLateBlowTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	FSourcesPlayer Player = MakeSourcesPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	ACataclysmEnemyCharacter* AtOne = Player.World ? SourcesCreature(Player.World, 300.0f) : nullptr;
	ACataclysmEnemyCharacter* AtThree = Player.World ? SourcesCreature(Player.World, 600.0f) : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player and two creatures"), Player.IsUsable() && AtOne && AtThree))
	{
		return false;
	}
	Player.State->SetLevelAndExperience(4, 0);
	const auto StrikeWithTheRow = [&Player](ACataclysmEnemyCharacter* Target, FCataclysmDamageResult& Out)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.SkillRowWithNoSkill = SourcesMoltenCleave;
		Delivery.bCannotCriticallyStrike = true;
		return UCataclysmSkillEffects::ApplyHit(Player.Character, Target, 100.0f, FGameplayTagContainer(), Delivery, &Out);
	};

	FCataclysmDamageResult First;
	const float SentAtOne = StrikeWithTheRow(AtOne, First);
	Player.State->SetSkillExperience(SourcesMoltenCleave, Levels::ExperienceToReach(3));
	FCataclysmDamageResult Second;
	const float SentAtThree = StrikeWithTheRow(AtThree, Second);

	AddInfo(FString::Printf(TEXT("LATE BLOW: sent %.3f at level 1 and %.3f at level 3 (x%.4f); credited %.3f of %.3f"),
							SentAtOne, SentAtThree, SentAtOne > 0.0f ? SentAtThree / SentAtOne : 0.0f,
							SourcesCreditedTo(AtOne, SourcesMoltenCleave), First.DealtToHealth));
	TestTrue(TEXT("set-up: the blow landed and took health"), SentAtOne > 0.0f && First.DealtToHealth > 0.0f);
	TestEqual(TEXT("A BLOW CARRYING ONLY THE ROW IS SIZED AT THE SKILL'S LEVEL"), SentAtThree / SentAtOne,
			  static_cast<float>(Levels::DamageFactor(3)), 1.0e-4f);
	TestEqual(TEXT("AND CREDITS THE SKILL WITH WHAT IT TOOK"), SourcesCreditedTo(AtOne, SourcesMoltenCleave),
			  First.DealtToHealth, 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesCastByTest,
	"Cataclysm.SkillLevelSources.ABlowCastInThePlayersNameIsCredited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A skill's blow cast in the player's name by something else -- `CastBy` set, as a gadget's or a minion's copy carries
 * it -- is credited to the skill. Until slice 2a a blow with `CastBy` set credited nothing (F1); the design document's
 * earning row shares a kill among the skills that hurt it, and this is that skill's damage.
 */
bool FCataclysmSkillLevelSourcesCastByTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	FSourcesPlayer Player = MakeSourcesPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	const UCataclysmSkillTemplate* Heavy = Player.IsUsable() ? SourcesSkillOfRow(Player.Abilities, SourcesMoltenCleave) : nullptr;
	ACataclysmEnemyCharacter* Struck = Player.World ? SourcesCreature(Player.World, 300.0f) : nullptr;
	AActor* CastFrom = Player.World ? Player.World->SpawnActor<AActor>(FVector(0.0f, 200.0f, 0.0f), FRotator::ZeroRotator)
									: nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player holding Molten Cleave, a creature and an actor to cast from"),
				  Heavy && Struck && CastFrom))
	{
		return false;
	}
	FCataclysmHitDelivery Delivery;
	Delivery.Skill = Heavy;
	Delivery.CastBy = CastFrom;
	Delivery.bCannotCriticallyStrike = true;
	FCataclysmDamageResult Result;
	const float Sent = UCataclysmSkillEffects::ApplyHit(Player.Character, Struck, 100.0f, FGameplayTagContainer(), Delivery,
														&Result);

	AddInfo(FString::Printf(TEXT("CAST BY: sent %.3f, took %.3f, credited %.3f"), Sent, Result.DealtToHealth,
							SourcesCreditedTo(Struck, SourcesMoltenCleave)));
	TestTrue(TEXT("set-up: the blow landed and took health"), Sent > 0.0f && Result.DealtToHealth > 0.0f);
	TestEqual(TEXT("A BLOW CAST IN THE PLAYER'S NAME CREDITS ITS SKILL"), SourcesCreditedTo(Struck, SourcesMoltenCleave),
			  Result.DealtToHealth, 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesBurnKillTest,
	"Cataclysm.SkillLevelSources.AKillByASkillsBurnPaysTheSkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A creature a skill's burn kills pays that skill its whole worth: every tick is credited before a death is handled
 * (slice 1b's place), and the killing tick with it. A Boss, so it is worth something here, at 5 health, which the
 * burn's first tick takes.
 */
bool FCataclysmSkillLevelSourcesBurnKillTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	FSourcesPlayer Player = MakeSourcesPlayer();
	ON_SCOPE_EXIT
	{
		if (Player.World)
		{
			Player.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	const UCataclysmSkillTemplate* Heavy = Player.IsUsable() ? SourcesSkillOfRow(Player.Abilities, SourcesMoltenCleave) : nullptr;
	ACataclysmEnemyCharacter* Burnt = Player.World ? SourcesCreature(Player.World, 300.0f) : nullptr;
	UAbilitySystemComponent* BurntsSystem = Burnt ? Burnt->GetAbilitySystemComponent() : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player holding Molten Cleave, and a creature"), Heavy && BurntsSystem))
	{
		return false;
	}
	// A BOSS, as the skill level death tests make one. The rarity first, then the health, because the rarity sets the
	// creature's starting figures again.
	constexpr int32 BossStep = 4;
	const int32 Worth = UCataclysmEnemyScore::ScoreFor(UCataclysmEnemyScore::FloorIn(Player.World), BossStep);
	Burnt->SetRarityStep(BossStep);
	BurntsSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 5.0f);
	BurntsSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 5.0f);

	const bool bLaid = UCataclysmSkillEffects::ApplyDamageOverTime(Player.Character, Burnt, 10.0f, 3.0f,
		UCataclysmSkillEffects::BurnTag(), /*bScalesWithInstigator=*/true, /*DealtBy=*/nullptr, Heavy);
	CataclysmTestWorld::RunClock(Player.World, 3.5f);
	const bool bKilled = UCataclysmSkillEffects::IsDead(Burnt);
	const double Paid = Player.State->GetSkillExperience(SourcesMoltenCleave);

	AddInfo(FString::Printf(TEXT("BURN KILL: worth %d; killed %s; paid %.3f"), Worth, bKilled ? TEXT("yes") : TEXT("NO"),
							Paid));
	TestTrue(TEXT("set-up: a Boss is worth something here, and the burn was laid"), Worth > 0 && bLaid);
	TestTrue(TEXT("set-up: the burn killed it"), bKilled);
	TestEqual(TEXT("A KILL BY A SKILL'S BURN PAYS THE SKILL THE CREATURE'S WHOLE WORTH"), Paid,
			  static_cast<double>(Worth), 1.0e-3);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
