// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRuntimeEffect.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmSkillLevels.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesBurstTest,
	"Cataclysm.SkillLevelSources.ABurstOfAnAilmentsRemainingDamageIsCreditedToItsSkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An ailment's remaining damage paid at once credits the skill that laid it, as its ticks would have (the coordinating
 * session's judgement 2). The burn is laid as `ASkillsBurnIsCreditedToItAndSizedByItsLevel` lays it; the burst is
 * dealt as `Cataclysm.RemainingDamage.AfterRealTicksTheBurstDealsWhatIsLeftAndEndsTheEffect` deals it
 * (`CataclysmRemainingDamageTests.cpp`), with the owner the effect's instigator, the player character.
 */
bool FCataclysmSkillLevelSourcesBurstTest::RunTest(const FString&)
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
	if (!TestTrue(TEXT("set-up: a possessed player holding Molten Cleave, and a creature"), Heavy && Burnt))
	{
		return false;
	}
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	Player.State->SetLevelAndExperience(4, 0);
	const auto HealthOf = [](ACataclysmEnemyCharacter* Creature)
	{
		return Creature->GetAbilitySystemComponent()->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	};

	const bool bLaid = UCataclysmSkillEffects::ApplyDamageOverTime(Player.Character, Burnt, 10.0f, 5.0f, Burn,
		/*bScalesWithInstigator=*/true, /*DealtBy=*/nullptr, Heavy);
	CataclysmTestWorld::RunClock(Player.World, 1.5f);
	const float CreditedBefore = SourcesCreditedTo(Burnt, SourcesMoltenCleave);
	const float HealthBefore = HealthOf(Burnt);
	const int32 Burst = UCataclysmSkillEffects::DealRemainingDamageOverTime(Player.Character, Burnt, 100.0f, Burn,
		/*bEndEach=*/true);
	const float TookByTheBurst = HealthBefore - HealthOf(Burnt);
	const float CreditedByTheBurst = SourcesCreditedTo(Burnt, SourcesMoltenCleave) - CreditedBefore;

	AddInfo(FString::Printf(TEXT("SKILL BURST: %d burst; credited %.3f by the ticks before, then %.3f by the burst, which "
								 "took %.3f"),
							Burst, CreditedBefore, CreditedByTheBurst, TookByTheBurst));
	TestTrue(TEXT("set-up: the burn was laid, ticked, and one effect burst"), bLaid && CreditedBefore > 0.0f && Burst == 1);
	TestTrue(TEXT("set-up: the burst took health"), TookByTheBurst > 0.0f);
	TestEqual(TEXT("THE BURST CREDITS THE BURN'S SKILL WITH WHAT IT TOOK"), CreditedByTheBurst, TookByTheBurst, 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesHitAndBurnTest,
	"Cataclysm.SkillLevelSources.AKillIsSplitBetweenOneSkillsHitAndAnothersBurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A creature hurt by one skill's hit and killed by another's burn pays each skill its share of the creature's health,
 * end to end through `ACataclysmEnemyCharacter::HandleDeath`: the shape of the fault slice 1b fixed, with a tick as the
 * killing blow. The hit is struck as `Cataclysm.SkillLevels.ASkillsKillingBlowIsCountedInTheDeathsSplit` strikes it (no
 * critical); the burn kills as `AKillByASkillsBurnPaysTheSkill` kills. A first creature measures the hit's figure, W; the
 * second, a Boss so it is worth something, is given W + 15 health, so Molten Cleave's hit leaves it 15 and Emberhurl's
 * burn takes the rest. Molten Cleave's share is W / (W + 15) of the worth and Emberhurl's 15 / (W + 15).
 */
bool FCataclysmSkillLevelSourcesHitAndBurnTest::RunTest(const FString&)
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
	const FName EmberhurlRow(TEXT("Demonic_Greataxe_Special"));
	const UCataclysmSkillTemplate* Heavy = Player.IsUsable() ? SourcesSkillOfRow(Player.Abilities, SourcesMoltenCleave) : nullptr;
	const UCataclysmSkillTemplate* Special = Player.IsUsable() ? SourcesSkillOfRow(Player.Abilities, EmberhurlRow) : nullptr;
	ACataclysmEnemyCharacter* Measure = Player.World ? SourcesCreature(Player.World, 300.0f) : nullptr;
	ACataclysmEnemyCharacter* Split = Player.World ? SourcesCreature(Player.World, 600.0f) : nullptr;
	UAbilitySystemComponent* SplitsSystem = Split ? Split->GetAbilitySystemComponent() : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player holding Molten Cleave and Emberhurl, and two creatures"),
				  Heavy && Special && Measure && SplitsSystem))
	{
		return false;
	}
	const auto StrikeOnce = [&Player, Heavy](ACataclysmEnemyCharacter* Target, FCataclysmDamageResult& Out)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.Skill = Heavy;
		Delivery.bCannotCriticallyStrike = true;
		UCataclysmSkillEffects::ApplyHit(Player.Character, Target, 100.0f, FGameplayTagContainer(), Delivery, &Out);
	};

	// A BOSS, as the death tests make one. The rarity first, then the health, because the rarity sets the creature's
	// starting figures again; and both creatures are Bosses, so the hit's figure on one is its figure on the other.
	constexpr int32 BossStep = 4;
	const int32 Worth = UCataclysmEnemyScore::ScoreFor(UCataclysmEnemyScore::FloorIn(Player.World), BossStep);
	Measure->SetRarityStep(BossStep);
	Split->SetRarityStep(BossStep);
	FCataclysmDamageResult Measured;
	StrikeOnce(Measure, Measured);
	const float Hit = Measured.DealtToHealth;
	constexpr float Left = 15.0f;
	SplitsSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Hit + Left);
	SplitsSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Hit + Left);

	FCataclysmDamageResult Struck;
	StrikeOnce(Split, Struck);
	const bool bAliveAfterTheHit = !UCataclysmSkillEffects::IsDead(Split);
	const bool bLaid = UCataclysmSkillEffects::ApplyDamageOverTime(Player.Character, Split, 10.0f, 5.0f,
		UCataclysmSkillEffects::BurnTag(), /*bScalesWithInstigator=*/true, /*DealtBy=*/nullptr, Special);
	CataclysmTestWorld::RunClock(Player.World, 5.5f);
	const bool bKilled = UCataclysmSkillEffects::IsDead(Split);
	const double PaidHeavy = Player.State->GetSkillExperience(SourcesMoltenCleave);
	const double PaidSpecial = Player.State->GetSkillExperience(EmberhurlRow);
	// THE EXPECTED SPLIT FROM WHAT EACH SKILL WAS CREDITED, as `ACataclysmPlayerState::GrantSkillExperience` splits it:
	// each credit over the larger of the maximum health and all that was credited. So health regained during the burn,
	// which its ticks then took again, moves the expected figures with the paid ones.
	const double CreditedHeavy = SourcesCreditedTo(Split, SourcesMoltenCleave);
	const double CreditedSpecial = SourcesCreditedTo(Split, EmberhurlRow);
	const double Over = FMath::Max(static_cast<double>(Hit + Left), CreditedHeavy + CreditedSpecial);

	AddInfo(FString::Printf(TEXT("HIT AND BURN: worth %d; the hit took %.3f of %.3f; credited %.3f to Molten Cleave and "
								 "%.3f to Emberhurl; killed %s; Molten Cleave paid %.3f, Emberhurl %.3f, together %.3f"),
							Worth, Struck.DealtToHealth, Hit + Left, CreditedHeavy, CreditedSpecial,
							bKilled ? TEXT("yes") : TEXT("NO"), PaidHeavy, PaidSpecial, PaidHeavy + PaidSpecial));
	TestTrue(TEXT("set-up: a Boss is worth something here, and the hit has a figure"), Worth > 0 && Hit > 0.0f);
	TestEqual(TEXT("set-up: the hit took the same on the second Boss"), Struck.DealtToHealth, Hit, 1.0e-3f);
	TestTrue(TEXT("set-up: the hit left it alive, and the burn was laid and killed it"), bAliveAfterTheHit && bLaid && bKilled);
	TestEqual(TEXT("MOLTEN CLEAVE WAS CREDITED ITS HIT"), CreditedHeavy, static_cast<double>(Hit), 1.0e-3);
	TestTrue(TEXT("EMBERHURL WAS CREDITED ALL ITS TICKS TOOK, THE KILLING TICK INCLUDED"),
			 CreditedSpecial >= static_cast<double>(Left) - 1.0e-3);
	TestEqual(TEXT("MOLTEN CLEAVE IS PAID ITS SHARE OF THE WORTH"), PaidHeavy,
			  static_cast<double>(Worth) * CreditedHeavy / Over, 1.0e-2);
	TestEqual(TEXT("EMBERHURL IS PAID ITS SHARE"), PaidSpecial, static_cast<double>(Worth) * CreditedSpecial / Over, 1.0e-2);
	TestEqual(TEXT("AND THE TWO ADD UP TO THE WHOLE WORTH"), PaidHeavy + PaidSpecial, static_cast<double>(Worth), 1.0e-2);
	return true;
}

// ==========================================================================
// Slice 2b: the minions a skill summons and the thralls it takes. Issue #2764.
// ==========================================================================

namespace CataclysmSkillLevelSourcesTest
{
	const FName SourcesSummonImp(TEXT("Demonic_Staff_Special")); // the Staff's Special
	const FName SourcesSubjugate(TEXT("Demonic_Staff_Ultimate")); // the Staff's Ultimate
	const FName SourcesCompel(TEXT("Demonic_Staff_Heavy")); // the Staff's Heavy
	constexpr float SourcesM = 100.0f;

	/**
	 * A possessed player at `Where` in `World` holding a Demonic Staff and the skills it grants, each with its row, as
	 * `Cataclysm.BasicReach`'s `FReachRig` equips a weapon (`CataclysmBasicReachTests.cpp`), with that rig's figures; at
	 * character level 4, so a skill can reach level 3; and a pool of 150 Fervour, a Ritualist's, for what Summon Imp and
	 * Subjugate reserve.
	 */
	FSourcesPlayer SourcesStaffPlayerIn(UWorld* World, const FVector& Where)
	{
		FSourcesPlayer Out;
		Out.World = World;
		Out.State = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!Out.State || !Controller || !Character)
		{
			return Out;
		}
		Controller->SetPlayerState(Out.State);
		Controller->Possess(Character);
		UCataclysmAbilitySystemComponent* System = Out.State->GetCataclysmAbilitySystemComponent();
		UCataclysmWeaponSlotsComponent* Slots = Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
		if (!System || !Slots)
		{
			return Out;
		}
		Slots->SetDamageType(TEXT("Demonic"));
		Slots->EquipWeaponType(TEXT("Staff"));
		System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
		Out.State->SetLevelAndExperience(4, 0);
		Out.Character = Character;
		Out.Abilities = System;
		return Out;
	}

	/** The player's summon skill whose row is `Row`, or null: Summon Imp or Subjugate. */
	UCataclysmSummonSkill* SourcesSummonSkillOfRow(UAbilitySystemComponent* Abilities, FName Row)
	{
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			UCataclysmSummonSkill* Skill = Cast<UCataclysmSummonSkill>(Spec.GetPrimaryInstance());
			if (Skill && Skill->SkillRow == Row)
			{
				return Skill;
			}
		}
		return nullptr;
	}

	/**
	 * A creature to be struck, with nothing between a blow and its health but the blow's own resolution: no evasion and
	 * no block, as `Cataclysm.CombatEvents.AMinionsBlowPutsTheMinionInCombatAndNotItsSummoner` sets its creature, and no
	 * armour, so two blows' figures compare as they were dealt. 50,000 health unless a test says otherwise.
	 */
	ACataclysmEnemyCharacter* SourcesStruckCreature(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetHealth(50'000.0f);
			Made->SetArmour(0.0f);
			UAbilitySystemComponent* System = Made->GetAbilitySystemComponent();
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		}
		return Made;
	}

	/** A creature's health now. */
	float SourcesHealthOf(const ACataclysmEnemyCharacter* Creature)
	{
		return Creature->GetAbilitySystemComponent()->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	}

	/** What one swing of `Imp` took from `Creature`, as the combat event test above deals it (`AttackTarget`). */
	float SourcesSwingOf(ACataclysmMinion* Imp, ACataclysmEnemyCharacter* Creature)
	{
		const float Before = SourcesHealthOf(Creature);
		Imp->AttackTarget(Creature);
		return Before - SourcesHealthOf(Creature);
	}

	/** What one blow by `Striker` at 100% of its own damage took from `Creature`, as the thrall gear test's `BlowFrom`. */
	float SourcesBlowFrom(AActor* Striker, ACataclysmEnemyCharacter* Creature)
	{
		const float Before = SourcesHealthOf(Creature);
		UCataclysmSkillEffects::ApplyHit(Striker, Creature, 100.0f);
		return Before - SourcesHealthOf(Creature);
	}

	/**
	 * A creature at Common with the thrall gear test's figures (`Cataclysm.MinionGear.
	 * AThrallHitsHarderForItsCommandersIncreasedMinionDamage`): 1,000 health and 100 attack damage; then hurt to 520, as
	 * `Cataclysm.Command.SubjugateTakesAnEnemyTheBlowLeftBelowHalfHealth` hurts its creature, so Subjugate's own blow
	 * carries it under half.
	 */
	ACataclysmEnemyCharacter* SourcesCreatureToTake(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetHealth(1'000.0f);
			Made->SetAttackDamage(100.0f);
			Made->GetAbilitySystemComponent()->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(),
																	  520.0f);
		}
		return Made;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesImpBlowTest,
	"Cataclysm.SkillLevelSources.ASummonedImpsBlowCreditsSummonImpAndIsScaledByItsLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An imp Summon Imp summons credits the skill with what its swing takes, and its swing grows with the skill's level,
 * READ ONCE WHEN IT WAS SUMMONED: the owner's ruling of 2026-11-19, "Yes, they scale too", and judgement 4. Issue
 * #2764, slice 2b. The imps are summoned on the real character as
 * `Cataclysm.Passives.PressGangedAndRekindledRowsReplaceARealRitualistsLostImp` summons one (`SummonOne`), from the
 * Staff's own granted Summon Imp with its row; each swing is dealt as
 * `Cataclysm.CombatEvents.AMinionsBlowPutsTheMinionInCombatAndNotItsSummoner` deals it. The first imp is summoned at
 * level 1, the skill raised to level 3, and a second summoned: the second's swing takes 1.0567 squared times the
 * first's, and the first's next swing, after the level, takes what its first did.
 */
bool FCataclysmSkillLevelSourcesImpBlowTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FSourcesPlayer Player = SourcesStaffPlayerIn(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Summon = Player.IsUsable() ? SourcesSummonSkillOfRow(Player.Abilities, SourcesSummonImp) : nullptr;
	ACataclysmEnemyCharacter* AtOne = SourcesStruckCreature(World, FVector(3.0f * SourcesM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* AtThree = SourcesStruckCreature(World, FVector(6.0f * SourcesM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Later = SourcesStruckCreature(World, FVector(9.0f * SourcesM, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a possessed player holding the Staff's Summon Imp, and three creatures. If the skill is "
					   "missing, DT_WeaponSkills may be older than game/Data/WeaponSkills.csv"),
				  Summon && AtOne && AtThree && Later))
	{
		return false;
	}

	ACataclysmMinion* First = Summon->SummonOne();
	Player.State->SetSkillExperience(SourcesSummonImp, Levels::ExperienceToReach(3));
	ACataclysmMinion* Second = Summon->SummonOne();
	if (!TestTrue(TEXT("set-up: two imps summoned"), First && Second))
	{
		return false;
	}
	const float TookAtOne = SourcesSwingOf(First, AtOne);
	const float TookAtThree = SourcesSwingOf(Second, AtThree);
	const float TookLater = SourcesSwingOf(First, Later);

	AddInfo(FString::Printf(TEXT("IMP BLOW: rows %s and %s; factors %.4f and %.4f; a swing took %.3f at level 1 and %.3f at "
								 "level 3 (x%.4f); the first imp's after the level %.3f; credited %.3f and %.3f"),
							*First->SummoningSkillRow.ToString(), *Second->SummoningSkillRow.ToString(),
							First->LevelFactorOnCreatures, Second->LevelFactorOnCreatures, TookAtOne, TookAtThree,
							TookAtOne > 0.0f ? TookAtThree / TookAtOne : 0.0f, TookLater,
							SourcesCreditedTo(AtOne, SourcesSummonImp), SourcesCreditedTo(AtThree, SourcesSummonImp)));
	TestTrue(TEXT("set-up: each swing took health"), TookAtOne > 0.0f && TookAtThree > 0.0f && TookLater > 0.0f);
	TestEqual(TEXT("AN IMP SUMMON IMP SUMMONED CARRIES ITS ROW"), First->SummoningSkillRow, SourcesSummonImp);
	TestEqual(TEXT("THE LEVEL 3 IMP'S SWING IS 1.0567 SQUARED TIMES THE LEVEL 1 IMP'S"), TookAtThree / TookAtOne,
			  static_cast<float>(Levels::DamageFactor(3)), 1.0e-3f);
	TestEqual(TEXT("AN IMP SUMMONED BEFORE THE LEVEL KEEPS THE FACTOR IT WAS SUMMONED WITH"), TookLater, TookAtOne, 1.0e-3f);
	TestEqual(TEXT("THE LEVEL 1 IMP'S SWING IS CREDITED TO SUMMON IMP"), SourcesCreditedTo(AtOne, SourcesSummonImp),
			  TookAtOne, 1.0e-3f);
	TestEqual(TEXT("AND THE LEVEL 3 IMP'S"), SourcesCreditedTo(AtThree, SourcesSummonImp), TookAtThree, 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesUnmadeMinionTest,
	"Cataclysm.SkillLevelSources.AMinionNoSkillMadeCreditsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A minion no skill made credits nothing and keeps factor 1, though its summoner holds Summon Imp at level 3: the
 * coordinating session's judgement 3 (accepted 2026-11-19), "minions no skill made credit nothing". Issue #2764, slice
 * 2b. The imp is spawned directly, as `Cataclysm.CombatEvents.AMinionsBlowPutsTheMinionInCombatAndNotItsSummoner`
 * spawns its imp: the way a risen imp, a Warhound or Tyrant's Minion is made, with no summon skill behind it.
 */
bool FCataclysmSkillLevelSourcesUnmadeMinionTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FSourcesPlayer Player = SourcesStaffPlayerIn(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Struck = SourcesStruckCreature(World, FVector(4.0f * SourcesM, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a possessed player holding a Staff, and a creature"),
				  Player.IsUsable() && SourcesSummonSkillOfRow(Player.Abilities, SourcesSummonImp) && Struck))
	{
		return false;
	}
	Player.State->SetSkillExperience(SourcesSummonImp, Levels::ExperienceToReach(3));
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Player.Character, FVector(3.0f * SourcesM, 0.0f, 0.0f),
													/*Lifetime=*/20.0f, /*bBurns=*/false, /*TypeName=*/TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: an imp spawned with no skill"), Imp))
	{
		return false;
	}
	const float Took = SourcesSwingOf(Imp, Struck);

	AddInfo(FString::Printf(TEXT("UNMADE MINION: row %s, factor %.4f; its swing took %.3f; rows credited %d"),
							*Imp->SummoningSkillRow.ToString(), Imp->LevelFactorOnCreatures, Took,
							Struck->GetHealthRemovedBySkill().Num()));
	TestTrue(TEXT("set-up: its swing took health"), Took > 0.0f);
	TestTrue(TEXT("A MINION NO SKILL MADE CARRIES NO ROW"), Imp->SummoningSkillRow.IsNone());
	TestEqual(TEXT("AND NO LEVEL FACTOR"), Imp->LevelFactorOnCreatures, 1.0f);
	TestEqual(TEXT("AND ITS SWING CREDITS NO SKILL"), Struck->GetHealthRemovedBySkill().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesThrallTest,
	"Cataclysm.SkillLevelSources.AThrallsBlowCreditsSubjugateAndIsScaledByItsLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A thrall Subjugate took credits the skill with what its blows take from a creature, and its blows grow with the
 * skill's level, read once at the take: judgement 3, "Subjugate's thralls credit Subjugate", and the owner's ruling of
 * 2026-11-19. Issue #2764, slice 2b. Each creature is taken as
 * `Cataclysm.Command.SubjugateTakesAnEnemyTheBlowLeftBelowHalfHealth` takes its wounded one -- hurt to 520 of 1,000,
 * so the skill's own blow carries it under half -- by the Staff's own Subjugate with its row, calling `Possess` as the
 * skill's activation does; each thrall's blow is struck as
 * `Cataclysm.MinionGear.AThrallHitsHarderForItsCommandersIncreasedMinionDamage` strikes it, never a critical strike.
 * The first is taken at level 1, the skill raised to level 3, and a second taken; then each strikes a creature of its
 * own. The second's blow takes 1.0567 squared times the first's, the first keeping its level 1 factor after the level.
 */
bool FCataclysmSkillLevelSourcesThrallTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FSourcesPlayer Player = SourcesStaffPlayerIn(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Take = Player.IsUsable() ? SourcesSummonSkillOfRow(Player.Abilities, SourcesSubjugate) : nullptr;
	ACataclysmEnemyCharacter* TakenAtOne = SourcesCreatureToTake(World, FVector(3.0f * SourcesM, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a possessed player holding the Staff's Subjugate, and a wounded creature"),
				  Take && TakenAtOne))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: Subjugate takes the first creature"), Take->Possess()))
	{
		return false;
	}
	Player.State->SetSkillExperience(SourcesSubjugate, Levels::ExperienceToReach(3));
	// THE SECOND CREATURE ONLY NOW, so the first take had one enemy within the skill's range to choose.
	ACataclysmEnemyCharacter* TakenAtThree = SourcesCreatureToTake(World, FVector(0.0f, 3.0f * SourcesM, 0.0f));
	if (!TestTrue(TEXT("set-up: Subjugate takes the second creature"), TakenAtThree && Take->Possess()))
	{
		return false;
	}
	// AND THE CREATURES THEY STRIKE, past the skill's 15 m, after both takes.
	ACataclysmEnemyCharacter* StruckByOne = SourcesStruckCreature(World, FVector(40.0f * SourcesM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* StruckByThree = SourcesStruckCreature(World, FVector(60.0f * SourcesM, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: two creatures to strike"), StruckByOne && StruckByThree))
	{
		return false;
	}
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	const float TookByOne = SourcesBlowFrom(TakenAtOne, StruckByOne);
	const float TookByThree = SourcesBlowFrom(TakenAtThree, StruckByThree);

	AddInfo(FString::Printf(TEXT("THRALL BLOW: rows %s and %s; factors %.4f and %.4f; a blow took %.3f at level 1 and %.3f "
								 "at level 3 (x%.4f); credited %.3f and %.3f"),
							*TakenAtOne->TakenBySkillRow.ToString(), *TakenAtThree->TakenBySkillRow.ToString(),
							TakenAtOne->TakenLevelFactorOnCreatures, TakenAtThree->TakenLevelFactorOnCreatures, TookByOne,
							TookByThree, TookByOne > 0.0f ? TookByThree / TookByOne : 0.0f,
							SourcesCreditedTo(StruckByOne, SourcesSubjugate),
							SourcesCreditedTo(StruckByThree, SourcesSubjugate)));
	TestTrue(TEXT("set-up: both are commanded by the player"),
			 UCataclysmCommand::CommanderOf(TakenAtOne) == Player.Character
				 && UCataclysmCommand::CommanderOf(TakenAtThree) == Player.Character);
	TestTrue(TEXT("set-up: each blow took health"), TookByOne > 0.0f && TookByThree > 0.0f);
	TestEqual(TEXT("A THRALL SUBJUGATE TOOK CARRIES ITS ROW"), TakenAtOne->TakenBySkillRow, SourcesSubjugate);
	TestEqual(TEXT("THE LEVEL 3 THRALL'S BLOW IS 1.0567 SQUARED TIMES THE LEVEL 1 THRALL'S"), TookByThree / TookByOne,
			  static_cast<float>(Levels::DamageFactor(3)), 1.0e-3f);
	TestEqual(TEXT("THE LEVEL 1 THRALL'S BLOW IS CREDITED TO SUBJUGATE"), SourcesCreditedTo(StruckByOne, SourcesSubjugate),
			  TookByOne, 1.0e-3f);
	TestEqual(TEXT("AND THE LEVEL 3 THRALL'S"), SourcesCreditedTo(StruckByThree, SourcesSubjugate), TookByThree, 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesHitAndImpTest,
	"Cataclysm.SkillLevelSources.AKillIsSplitBetweenAHitSkillAndAMinionSkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A creature hurt by one skill's hit and killed by another skill's imp pays each skill its share of the creature's
 * health, end to end through `ACataclysmEnemyCharacter::HandleDeath`. Issue #2764, slice 2b. The shape of
 * `AKillIsSplitBetweenOneSkillsHitAndAnothersBurn` above, with the Staff's Compel striking as Molten Cleave strikes
 * there and Summon Imp's imp, summoned and swinging as `ASummonedImpsBlowCreditsSummonImpAndIsScaledByItsLevel` does,
 * in place of the burn. Both creatures are Bosses, so the kill is worth something; the second is given W + 15 health, W
 * being the hit's figure on the first, so Compel's hit leaves it 15 and the imp's swing takes the rest.
 */
bool FCataclysmSkillLevelSourcesHitAndImpTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FSourcesPlayer Player = SourcesStaffPlayerIn(World, FVector::ZeroVector);
	const UCataclysmSkillTemplate* Heavy = Player.IsUsable() ? SourcesSkillOfRow(Player.Abilities, SourcesCompel) : nullptr;
	UCataclysmSummonSkill* Summon = Player.IsUsable() ? SourcesSummonSkillOfRow(Player.Abilities, SourcesSummonImp) : nullptr;
	ACataclysmEnemyCharacter* Measure = SourcesStruckCreature(World, FVector(3.0f * SourcesM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Split = SourcesStruckCreature(World, FVector(6.0f * SourcesM, 0.0f, 0.0f));
	ACataclysmMinion* Imp = Summon ? Summon->SummonOne() : nullptr;
	if (!TestTrue(TEXT("set-up: a possessed player holding the Staff's Compel and Summon Imp, an imp, and two creatures"),
				  Heavy && Imp && Measure && Split))
	{
		return false;
	}
	const auto StrikeOnce = [&Player, Heavy](ACataclysmEnemyCharacter* Target, FCataclysmDamageResult& Out)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.Skill = Heavy;
		Delivery.bCannotCriticallyStrike = true;
		UCataclysmSkillEffects::ApplyHit(Player.Character, Target, 100.0f, FGameplayTagContainer(), Delivery, &Out);
	};
	// BOSSES, as the death tests make them. The rarity first, because it sets the creature's starting figures again;
	// then no evasion, block or armour once more, and the health.
	constexpr int32 BossStep = 4;
	const int32 Worth = UCataclysmEnemyScore::ScoreFor(UCataclysmEnemyScore::FloorIn(World), BossStep);
	for (ACataclysmEnemyCharacter* Boss : {Measure, Split})
	{
		Boss->SetRarityStep(BossStep);
		Boss->SetArmour(0.0f);
		UAbilitySystemComponent* System = Boss->GetAbilitySystemComponent();
		System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
	}
	FCataclysmDamageResult Measured;
	StrikeOnce(Measure, Measured);
	const float Hit = Measured.DealtToHealth;
	constexpr float Left = 15.0f;
	UAbilitySystemComponent* SplitsSystem = Split->GetAbilitySystemComponent();
	SplitsSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Hit + Left);
	SplitsSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Hit + Left);

	FCataclysmDamageResult Struck;
	StrikeOnce(Split, Struck);
	const bool bAliveAfterTheHit = !UCataclysmSkillEffects::IsDead(Split);
	Imp->AttackTarget(Split);
	const bool bKilled = UCataclysmSkillEffects::IsDead(Split);
	const double PaidHeavy = Player.State->GetSkillExperience(SourcesCompel);
	const double PaidImp = Player.State->GetSkillExperience(SourcesSummonImp);
	// THE EXPECTED SPLIT FROM WHAT EACH SKILL WAS CREDITED, as `ACataclysmPlayerState::GrantSkillExperience` splits it.
	const double CreditedHeavy = SourcesCreditedTo(Split, SourcesCompel);
	const double CreditedImp = SourcesCreditedTo(Split, SourcesSummonImp);
	const double Over = FMath::Max(static_cast<double>(Hit + Left), CreditedHeavy + CreditedImp);

	AddInfo(FString::Printf(TEXT("HIT AND IMP: worth %d; the hit took %.3f of %.3f; credited %.3f to Compel and %.3f to "
								 "Summon Imp; killed %s; Compel paid %.3f, Summon Imp %.3f, together %.3f"),
							Worth, Struck.DealtToHealth, Hit + Left, CreditedHeavy, CreditedImp,
							bKilled ? TEXT("yes") : TEXT("NO"), PaidHeavy, PaidImp, PaidHeavy + PaidImp));
	TestTrue(TEXT("set-up: a Boss is worth something here, and the hit has a figure"), Worth > 0 && Hit > 0.0f);
	TestEqual(TEXT("set-up: the hit took the same on the second Boss"), Struck.DealtToHealth, Hit, 1.0e-3f);
	TestTrue(TEXT("set-up: the hit left it alive, and the imp's swing killed it"), bAliveAfterTheHit && bKilled);
	TestEqual(TEXT("COMPEL WAS CREDITED ITS HIT"), CreditedHeavy, static_cast<double>(Hit), 1.0e-3);
	TestTrue(TEXT("SUMMON IMP WAS CREDITED WHAT ITS IMP'S KILLING SWING TOOK"),
			 CreditedImp >= static_cast<double>(Left) - 1.0e-3);
	TestEqual(TEXT("COMPEL IS PAID ITS SHARE OF THE WORTH"), PaidHeavy, static_cast<double>(Worth) * CreditedHeavy / Over,
			  1.0e-2);
	TestEqual(TEXT("SUMMON IMP IS PAID ITS SHARE"), PaidImp, static_cast<double>(Worth) * CreditedImp / Over, 1.0e-2);
	TestEqual(TEXT("AND THE TWO ADD UP TO THE WHOLE WORTH"), PaidHeavy + PaidImp, static_cast<double>(Worth), 1.0e-2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillLevelSourcesCapExplosionTest,
	"Cataclysm.SkillLevelSources.TheSummonCapsExplosionCreditsSummonImpAndAPassivesDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The explosion the summon cap sets off credits Summon Imp, and one a passive sets off credits nothing; both are sized
 * by the level the imp was summoned at. Issue #2764, slice 2b, ruled 2026-11-20 (Q-B, option b): "Summoning a fourth
 * destroys the oldest, which explodes for damage in a 3 meter radius" is Summon Imp's own row, while
 * `minion_explodes_on_death` is a passive's, and judgement 2 says damage a passive adds credits nothing; "scaling follows
 * the figure", a share of the imp's own blow. The explosions are dealt by `Explode`, with the summoning skill's stated
 * radius, as `Cataclysm.MinionDeath.TheExplosionTheSummonCapSetsOffTakesTheSameStat` deals them: the cap's by the cap
 * itself, a summon past three destroying the oldest, and the passive's by the call `HandleDeath` makes under
 * `minion_explodes_on_death`. Three imps are summoned at level 1 and the skill raised to level 3; the fourth summon
 * destroys a level 1 imp, the fifth and sixth the other two, and the seventh the level 3 imp the fourth made. The fifth's
 * imp, at level 3, is then moved beside a second creature and explodes as the passive explodes it.
 */
bool FCataclysmSkillLevelSourcesCapExplosionTest::RunTest(const FString&)
{
	using namespace CataclysmSkillLevelSourcesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FSourcesPlayer Player = SourcesStaffPlayerIn(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Summon = Player.IsUsable() ? SourcesSummonSkillOfRow(Player.Abilities, SourcesSummonImp) : nullptr;
	// THE NEAR CREATURE WITHIN THE 3 M EVERY IMP SUMMONED HERE EXPLODES OVER: the summon is not activated, so each imp
	// is made where the player stands. The far one past every such explosion's reach.
	ACataclysmEnemyCharacter* Near = SourcesStruckCreature(World, FVector(2.0f * SourcesM, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Far = SourcesStruckCreature(World, FVector(30.0f * SourcesM, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a possessed player holding the Staff's Summon Imp, and two creatures"),
				  Summon && Near && Far))
	{
		return false;
	}
	const float NearAtStart = SourcesHealthOf(Near);
	for (int32 Each = 0; Each < 3; ++Each)
	{
		if (!TestNotNull(TEXT("set-up: an imp summoned at level 1"), Summon->SummonOne()))
		{
			return false;
		}
	}
	Player.State->SetSkillExperience(SourcesSummonImp, Levels::ExperienceToReach(3));
	ACataclysmMinion* Made = nullptr;
	const auto SummonPastTheCap = [&Made, Summon, Near]()
	{
		const float Before = SourcesHealthOf(Near);
		Made = Summon->SummonOne();
		return Before - SourcesHealthOf(Near);
	};
	const float CapAtOne = SummonPastTheCap(); // the fourth: the first level 1 imp explodes
	SummonPastTheCap(); // the fifth: the second
	ACataclysmMinion* Fifth = Made;
	SummonPastTheCap(); // the sixth: the third
	const float CapAtThree = SummonPastTheCap(); // the seventh: the level 3 imp the fourth made
	if (!TestTrue(TEXT("set-up: every summon past the cap made an imp, and the fifth's is still there"),
				  Made && IsValid(Fifth)))
	{
		return false;
	}
	const float NearLost = NearAtStart - SourcesHealthOf(Near);

	Fifth->SetActorLocation(FVector(29.0f * SourcesM, 0.0f, 0.0f));
	const float FarBefore = SourcesHealthOf(Far);
	Fifth->Explode();
	const float TookByThePassive = FarBefore - SourcesHealthOf(Far);

	AddInfo(FString::Printf(TEXT("CAP EXPLOSION: a level 1 imp's took %.3f and a level 3 imp's %.3f (x%.4f); the near "
								 "creature lost %.3f and Summon Imp was credited %.3f; the passive's took %.3f and %d rows "
								 "were credited"),
							CapAtOne, CapAtThree, CapAtOne > 0.0f ? CapAtThree / CapAtOne : 0.0f, NearLost,
							SourcesCreditedTo(Near, SourcesSummonImp), TookByThePassive,
							Far->GetHealthRemovedBySkill().Num()));
	TestTrue(TEXT("set-up: the cap's explosions and the passive's took health"),
			 CapAtOne > 0.0f && CapAtThree > 0.0f && TookByThePassive > 0.0f);
	TestEqual(TEXT("THE CAP'S EXPLOSIONS ARE CREDITED TO SUMMON IMP"), SourcesCreditedTo(Near, SourcesSummonImp), NearLost,
			  1.0e-2f);
	TestEqual(TEXT("THE PASSIVE'S EXPLOSION CREDITS NOTHING"), Far->GetHealthRemovedBySkill().Num(), 0);
	TestEqual(TEXT("A LEVEL 3 IMP'S EXPLOSION IS 1.0567 SQUARED TIMES A LEVEL 1 IMP'S"), CapAtThree / CapAtOne,
			  static_cast<float>(Levels::DamageFactor(3)), 1.0e-3f);
	TestEqual(TEXT("AND THE PASSIVE'S IS SIZED BY ITS IMP'S LEVEL AS THE CAP'S IS"), TookByThePassive, CapAtThree, 1.0e-2f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
