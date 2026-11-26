// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmContagion.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * Marks left by a death (the Death trees' slice C4c2, 2026-11-25): the Soul Collector's Grim Wake, "Enemies within 3
 * metres of an enemy you kill take 2% increased damage from you per point for 3 seconds", and the Shadow's Blood Debt,
 * "When an enemy bleeding from you dies, the damage its Bleed had left to deal is dealt over 2 seconds to the strongest
 * enemy within 10 metres". Four tests give rows by hand, one goes through a real player's kill handler on the scene
 * `Cataclysm.Swarm.` builds, and one reads the real rows.
 */

namespace CataclysmKillMarksTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	constexpr float MarkMetre = 100.0f;

	/** A character on the players' side with health, able to be given stat lines and action rows. */
	UCataclysmAbilitySystemComponent* MarkerAt(UWorld* World, const FVector& Where)
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

	/** A creature of the monsters' side with this maximum health, this much left, and this rarity rung. */
	ACataclysmEnemyCharacter* MarkCreature(UWorld* World, const FVector& At, float Maximum, float Left, int32 Rung = 0)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(Maximum);
			// THE RUNG ALONE, as `WithTargetState` reads it. `SetRarityStep` would also scale the creature and roll its
			// modifiers, which nothing here asks about.
			Made->RarityStep = Rung;
			if (UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Made))
			{
				Theirs->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Left);
			}
		}
		return Made;
	}

	/** Grim Wake's row on one damage line: 2 increased while the target stood within 3 metres of a kill of yours. */
	FCataclysmStatModifier GrimWakeRow()
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Increased;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = 2.0f;
		Row.Condition = ECataclysmStatCondition::TargetNearYourKillWithinSeconds;
		Row.ConditionValue = 3.0f;
		Row.ReachMetres = 3.0f;
		return Row;
	}

	/** One flat row of a stat line. */
	FCataclysmStatModifier MarkFlat(float Value)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		return Row;
	}

	/** Attack damage at a base of 100 with Grim Wake's row, and Death's Toll when asked. */
	void WearGrimWake(UCataclysmAbilitySystemComponent* System, bool bDeathsToll = false)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Attack = Lines.Add(FName(TEXT("attack_damage")));
		Attack.Base = 100.0f;
		Attack.Modifiers = {GrimWakeRow()};
		Lines.Add(FName(TEXT("spell_damage"))).Base = 100.0f;
		if (bDeathsToll)
		{
			Lines.Add(FName(UCataclysmFervour::FollowerKillsCountAsYoursStat)).Modifiers = {MarkFlat(1.0f)};
		}
		System->SetStatInputs(MoveTemp(Lines));
	}

	/** The asker's attack damage against `Target`, as a blow reads it. */
	float AttackAgainst(const UCataclysmAbilitySystemComponent* System, const AActor* Target)
	{
		return System->StatForSkill(FName(TEXT("attack_damage")), FGameplayTagContainer(), 0.0f, -1.0f,
									FCataclysmBlowContext(), -1.0f, -1.0f, false, Target);
	}

	/** How long ago `Marked` stood near a kill of `Killer`'s, or -1. */
	float SecondsSinceMarked(const AActor* Marked, const UAbilitySystemComponent* Killer)
	{
		const UCataclysmAbilitySystemComponent* Theirs =
			Cast<const UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Marked));
		return Theirs ? Theirs->SecondsSinceNearKillBy(Killer) : -2.0f;
	}

	/** Blood Debt's row: on `afflicted_death` under `Keyword.DoT.Bleed`, over 2 seconds. */
	FCataclysmPoolAction BloodDebtRow()
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("afflicted_death"));
		Action.Percent = 2.0f;
		Action.bPassBleedLeftToStrongest = true;
		Action.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Bleed"))));
		Action.TriggerKey = FName(TEXT("Passive_Shadow_keystone_a_kB:pass_bleed_left_to_strongest:afflicted_death"));
		return Action;
	}

	/** A real ten-second ailment on `Carrier` laid by `Applier`, at 5 a tick and unscaled, so it cannot kill. */
	bool LayMarkAilment(AActor* Applier, AActor* Carrier, const TCHAR* AilmentTag)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(AilmentTag), /*ErrorIfNotFound=*/false);
		return Applier && Carrier && Tag.IsValid()
			&& UCataclysmSkillEffects::ApplyDamageOverTime(Applier, Carrier, /*DamagePerTick=*/5.0f,
														   /*DurationSeconds=*/10.0f, Tag,
														   /*bScalesWithInstigator=*/false);
	}

	/** A real player on a real player state, as `Cataclysm.Swarm.` builds one. */
	struct FMarkScene
	{
		ACataclysmPlayerCharacter* Player = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	bool BuildMarkScene(FAutomationTestBase& Test, UWorld* World, FMarkScene& Out)
	{
		Out.State = World->SpawnActor<ACataclysmPlayerState>();
		Out.Player = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("set-up: a player state"), Out.State)
			|| !Test.TestNotNull(TEXT("set-up: a player"), Out.Player))
		{
			return false;
		}
		Out.Player->SetPlayerState(Out.State);
		Out.Player->OnRep_PlayerState();
		Out.System = Out.State->GetCataclysmAbilitySystemComponent();
		if (!Test.TestNotNull(TEXT("set-up: the player's ability system"), Out.System))
		{
			return false;
		}
		Out.System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		Out.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		Out.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		Out.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		return true;
	}

	/** One blow of the player's at 100%, never a critical strike, in the name of a test strike. */
	void MarkBlow(const FMarkScene& Scene, AActor* Target)
	{
		UCataclysmStrikeSkill* Swing = NewObject<UCataclysmStrikeSkill>(Scene.Player);
		Swing->SkillName = TEXT("Test Swing");
		Swing->SkillTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Strike"))));
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = 0.0f;
		Delivery.Skill = Swing;
		UCataclysmSkillEffects::ApplyHit(Scene.Player, Target, /*DamagePercent=*/100.0f, Swing->SkillTags, Delivery);
	}

	bool IsMarkDead(const AActor* Who)
	{
		return !IsValid(Who) || UCataclysmSkillEffects::IsDead(Who);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillMarksNearTest,
	"Cataclysm.KillMarks.GrimWakeRaisesYourDamageAgainstTheEnemiesNearYourKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character holding Grim Wake's row, and no other row asking about the target, marks the enemies within its row's 3
 * metres of a kill: its attack damage of 100 is 102 against a creature 2 metres from the body. THE CONTROLS: 100 against
 * a creature 4 metres off; the slain creature itself is not marked; and a creature marked only by ANOTHER killer's kill,
 * holding the same row, gives this character 100, since "from you" is the character's own kill.
 */
bool FCataclysmKillMarksNearTest::RunTest(const FString&)
{
	using namespace CataclysmKillMarksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = MarkerAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Someone = MarkerAt(World, FVector(0.0f, 30.0f * MarkMetre, 0.0f));
	const float M = MarkMetre;
	ACataclysmEnemyCharacter* Slain = MarkCreature(World, FVector(6 * M, 0, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* Near = MarkCreature(World, FVector(8 * M, 0, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* Far = MarkCreature(World, FVector(10 * M, 0, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* TheirSlain = MarkCreature(World, FVector(6 * M, 20 * M, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* TheirNear = MarkCreature(World, FVector(8 * M, 20 * M, 0), 1000.0f, 1000.0f);
	if (!TestNotNull(TEXT("set-up: a killer"), System) || !TestNotNull(TEXT("set-up: another killer"), Someone)
		|| !Slain || !Near || !Far || !TheirSlain || !TheirNear)
	{
		AddError(TEXT("set-up: five creatures"));
		return false;
	}
	WearGrimWake(System);
	WearGrimWake(Someone);

	TestEqual(TEXT("set-up: nobody killed yet, 100 against the near one"), AttackAgainst(System, Near), 100.0f, 0.001f);
	UCataclysmFervour::MarkNearAKill(System, Slain, Slain->GetActorLocation());
	UCataclysmFervour::MarkNearAKill(Someone, TheirSlain, TheirSlain->GetActorLocation());

	TestEqual(TEXT("2 metres from your kill: 102"), AttackAgainst(System, Near), 102.0f, 0.001f);
	TestEqual(TEXT("control: 4 metres from your kill: 100"), AttackAgainst(System, Far), 100.0f, 0.001f);
	TestEqual(TEXT("control: the slain creature is not marked"), SecondsSinceMarked(Slain, System), -1.0f, 0.001f);
	TestTrue(TEXT("set-up: the other killer's kill marked its near creature"), SecondsSinceMarked(TheirNear, Someone) >= 0.0f);
	TestEqual(TEXT("control: near another killer's kill: 100 from you"), AttackAgainst(System, TheirNear), 100.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillMarksClockTest,
	"Cataclysm.KillMarks.GrimWakesMarkLastsItsThreeSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The mark lasts the row's 3 seconds: 102 at 2.5 seconds after the kill. THE CONTROL: 100 at 3.5 seconds. And a second
 * kill near the same creature marks it afresh: 102 again.
 */
bool FCataclysmKillMarksClockTest::RunTest(const FString&)
{
	using namespace CataclysmKillMarksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = MarkerAt(World, FVector::ZeroVector);
	const float M = MarkMetre;
	ACataclysmEnemyCharacter* Slain = MarkCreature(World, FVector(6 * M, 0, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* Near = MarkCreature(World, FVector(8 * M, 0, 0), 1000.0f, 1000.0f);
	if (!TestNotNull(TEXT("set-up: a killer"), System) || !Slain || !Near)
	{
		AddError(TEXT("set-up: two creatures"));
		return false;
	}
	WearGrimWake(System);

	UCataclysmFervour::MarkNearAKill(System, Slain, Slain->GetActorLocation());
	RunClock(World, 2.5f);
	TestEqual(TEXT("2.5 seconds on: 102"), AttackAgainst(System, Near), 102.0f, 0.001f);
	RunClock(World, 1.0f);
	TestEqual(TEXT("control: 3.5 seconds on: 100"), AttackAgainst(System, Near), 100.0f, 0.001f);
	UCataclysmFervour::MarkNearAKill(System, Slain, Slain->GetActorLocation());
	TestEqual(TEXT("a second kill marks it afresh: 102"), AttackAgainst(System, Near), 102.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillMarksHandlerTest,
	"Cataclysm.KillMarks.GrimWakeMarksThroughTheRealKillHandlerAndForAFollowersKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path, on the scene `Cataclysm.Swarm.` builds. A real player holding Grim Wake's row kills a creature with a
 * blow: a creature 2 metres from the body is marked, and the player's attack damage against it is 102. Its real imp's
 * kill, under Death's Toll, marks a creature 2 metres from that body too, as ruled. THE CONTROLS: a creature 4 metres
 * from the player's kill is not marked; and the imp's kill without Death's Toll marks nothing.
 */
bool FCataclysmKillMarksHandlerTest::RunTest(const FString&)
{
	using namespace CataclysmKillMarksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	FMarkScene Scene;
	if (!BuildMarkScene(*this, World, Scene))
	{
		return false;
	}

	const float M = MarkMetre;
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Scene.Player, FVector(2 * M, 2 * M, 0), /*Lifetime=*/60.0f,
													/*bBurns=*/false, TEXT("Imp"));
	ACataclysmEnemyCharacter* Slain = MarkCreature(World, FVector(6 * M, 0, 0), 1000.0f, 1.0f);
	ACataclysmEnemyCharacter* Near = MarkCreature(World, FVector(8 * M, 0, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* Far = MarkCreature(World, FVector(10 * M, 0, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* ControlSlain = MarkCreature(World, FVector(2 * M, -6 * M, 0), 1000.0f, 1.0f);
	ACataclysmEnemyCharacter* ControlNear = MarkCreature(World, FVector(2 * M, -8 * M, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* ImpSlain = MarkCreature(World, FVector(2 * M, 12 * M, 0), 1000.0f, 1.0f);
	ACataclysmEnemyCharacter* ImpNear = MarkCreature(World, FVector(2 * M, 14 * M, 0), 1000.0f, 1000.0f);
	if (!TestNotNull(TEXT("set-up: the player's imp"), Imp) || !Slain || !Near || !Far || !ControlSlain || !ControlNear
		|| !ImpSlain || !ImpNear)
	{
		AddError(TEXT("set-up: seven creatures"));
		return false;
	}

	WearGrimWake(Scene.System);
	MarkBlow(Scene, Slain);
	if (!TestTrue(TEXT("set-up: the player's blow killed the first"), IsMarkDead(Slain)))
	{
		return false;
	}
	TestTrue(TEXT("2 metres from the player's kill: marked"), SecondsSinceMarked(Near, Scene.System) >= 0.0f);
	TestEqual(TEXT("and the player's attack damage against it is 102"), AttackAgainst(Scene.System, Near), 102.0f,
			  0.001f);
	TestEqual(TEXT("control: 4 metres off: not marked"), SecondsSinceMarked(Far, Scene.System), -1.0f, 0.001f);

	Imp->AttackTarget(ControlSlain);
	if (!TestTrue(TEXT("set-up: the imp killed a creature"), IsMarkDead(ControlSlain)))
	{
		return false;
	}
	TestEqual(TEXT("control: without Death's Toll the imp's kill marks nothing"),
			  SecondsSinceMarked(ControlNear, Scene.System), -1.0f, 0.001f);

	WearGrimWake(Scene.System, /*bDeathsToll=*/true);
	Imp->AttackTarget(ImpSlain);
	if (!TestTrue(TEXT("set-up: the imp killed another"), IsMarkDead(ImpSlain)))
	{
		return false;
	}
	TestTrue(TEXT("with Death's Toll the imp's kill marks the creature 2 metres off"),
			 SecondsSinceMarked(ImpNear, Scene.System) >= 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillMarksBloodDebtTest,
	"Cataclysm.KillMarks.BloodDebtPassesTheBleedLeftToTheStrongestEnemyNear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Blood Debt through the game's own announcement of an afflicted death. A creature carries the holder's ten-second Bleed
 * at 5 a tick, whose damage left is held to four seconds of movement: 20. When it dies, an Elite 5 metres off is given a
 * Bleed of the holder's holding the same 20, over 2 seconds. THE CONTROLS: a Common creature 3 metres off, nearer and
 * weaker, gets none; a Boss 12 metres off, beyond the 10, gets none; and a second creature carrying only the holder's
 * Poison passes nothing to an Elite beside it, the row asking for a Bleed.
 */
bool FCataclysmKillMarksBloodDebtTest::RunTest(const FString&)
{
	using namespace CataclysmKillMarksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = MarkerAt(World, FVector::ZeroVector);
	const float M = MarkMetre;
	ACataclysmEnemyCharacter* Dead = MarkCreature(World, FVector(5 * M, 0, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* Common = MarkCreature(World, FVector(5 * M, 3 * M, 0), 1000.0f, 1000.0f, 0);
	ACataclysmEnemyCharacter* Elite = MarkCreature(World, FVector(5 * M, -5 * M, 0), 1000.0f, 1000.0f, 1);
	ACataclysmEnemyCharacter* Boss = MarkCreature(World, FVector(5 * M, 12 * M, 0), 1000.0f, 1000.0f, 4);
	ACataclysmEnemyCharacter* Poisoned = MarkCreature(World, FVector(5 * M, 40 * M, 0), 1000.0f, 1000.0f);
	ACataclysmEnemyCharacter* PoisonedElite = MarkCreature(World, FVector(5 * M, 43 * M, 0), 1000.0f, 1000.0f, 1);
	if (!TestNotNull(TEXT("set-up: a holder"), System) || !Dead || !Common || !Elite || !Boss || !Poisoned
		|| !PoisonedElite)
	{
		AddError(TEXT("set-up: six creatures"));
		return false;
	}
	AActor* Holder = System->GetAvatarActor();
	System->SetPoolActions({BloodDebtRow()});
	if (!TestTrue(TEXT("set-up: the holder bleeds the first"), LayMarkAilment(Holder, Dead, TEXT("Keyword.DoT.Bleed")))
		|| !TestTrue(TEXT("set-up: and poisons another"),
					 LayMarkAilment(Holder, Poisoned, TEXT("Keyword.DoT.Poison"))))
	{
		return false;
	}
	const float Left = UCataclysmDebuffs::BleedLeftLaidBy(Dead, Holder);
	TestEqual(TEXT("set-up: the Bleed has 20 left"), Left, 20.0f, 0.001f);

	UCataclysmContagion::AnnounceAfflictedDeath(Dead);
	TestTrue(TEXT("the Elite 5 metres off now bleeds from the holder"),
			 UCataclysmDebuffs::CarriesPoisonOrBleedLaidBy(Elite, Holder));
	TestEqual(TEXT("holding the 20 left"), UCataclysmDebuffs::BleedLeftLaidBy(Elite, Holder), 20.0f, 0.001f);
	TestFalse(TEXT("control: the Common one, nearer and weaker, does not"),
			  UCataclysmDebuffs::CarriesPoisonOrBleedLaidBy(Common, Holder));
	TestFalse(TEXT("control: the Boss 12 metres off, beyond the 10, does not"),
			  UCataclysmDebuffs::CarriesPoisonOrBleedLaidBy(Boss, Holder));

	UCataclysmContagion::AnnounceAfflictedDeath(Poisoned);
	TestFalse(TEXT("control: a death carrying only the holder's Poison passes nothing"),
			  UCataclysmDebuffs::CarriesPoisonOrBleedLaidBy(PoisonedElite, Holder));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillMarksStrongestTest,
	"Cataclysm.KillMarks.TheStrongestIsRarityThenHealthThenNearness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Strongest", as ruled: the highest rarity rung, then the most current health, then the nearest. Among two Commons the
 * one with 800 health left beats the nearer one with 500; between two Commons with 800 each the nearer is chosen; an
 * Elite with 100 left beats both. THE CONTROLS: the body itself is never chosen, and nothing within reach answers null.
 */
bool FCataclysmKillMarksStrongestTest::RunTest(const FString&)
{
	using namespace CataclysmKillMarksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = MarkerAt(World, FVector::ZeroVector);
	const float M = MarkMetre;
	ACataclysmEnemyCharacter* Body = MarkCreature(World, FVector(5 * M, 0, 0), 1000.0f, 1000.0f, 3);
	ACataclysmEnemyCharacter* Weaker = MarkCreature(World, FVector(5 * M, 2 * M, 0), 1000.0f, 500.0f);
	ACataclysmEnemyCharacter* Healthier = MarkCreature(World, FVector(5 * M, 6 * M, 0), 1000.0f, 800.0f);
	ACataclysmEnemyCharacter* FartherTwin = MarkCreature(World, FVector(5 * M, -8 * M, 0), 1000.0f, 800.0f);
	ACataclysmEnemyCharacter* Lonely = MarkCreature(World, FVector(5 * M, 60 * M, 0), 1000.0f, 1000.0f);
	if (!TestNotNull(TEXT("set-up: an asker"), System) || !Body || !Weaker || !Healthier || !FartherTwin || !Lonely)
	{
		AddError(TEXT("set-up: five creatures"));
		return false;
	}
	AActor* Asker = System->GetAvatarActor();
	const float Reach = UCataclysmDebuffs::BloodDebtReachCm;

	TestTrue(TEXT("more health beats nearer, and the nearer of two equal is kept"),
			 UCataclysmDebuffs::StrongestEnemyNear(Body, Asker, Reach) == Healthier);
	ACataclysmEnemyCharacter* Elite = MarkCreature(World, FVector(5 * M, -9 * M, 0), 1000.0f, 100.0f, 1);
	if (!TestNotNull(TEXT("set-up: an Elite"), Elite))
	{
		return false;
	}
	TestTrue(TEXT("a higher rung beats any health"), UCataclysmDebuffs::StrongestEnemyNear(Body, Asker, Reach) == Elite);
	TestTrue(TEXT("control: nothing within reach of the lonely one"),
			 UCataclysmDebuffs::StrongestEnemyNear(Lonely, Asker, Reach) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillMarksRealRowsTest,
	"Cataclysm.KillMarks.TheTwoNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The two nodes' real rows, read from the passive effects table: Grim Wake's 2 increased attack and spell damage per
 * point within 3 seconds of standing within 3 metres of your kill; Blood Debt's pass of a Bleed's damage left over 2
 * seconds, on an afflicted death carrying a Bleed.
 */
bool FCataclysmKillMarksRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}

	int32 Damage = 0;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("SoulCollector_basic_b_a2"))))
	{
		++Damage;
		TestTrue(TEXT("Grim Wake: attack or spell damage"),
				 Row->Stat == TEXT("attack_damage") || Row->Stat == TEXT("spell_damage"));
		TestEqual(TEXT("Grim Wake: increased"), Row->ValueKind, FString(TEXT("increased")));
		TestEqual(TEXT("Grim Wake: 2 per point"), Row->ValuePerPoint, 2.0f, 0.001f);
		TestEqual(TEXT("Grim Wake: near your kill"), Row->Condition,
				  FString(TEXT("target_near_your_kill_within_seconds")));
		TestEqual(TEXT("Grim Wake: for 3 seconds"), Row->ConditionValue, 3.0f, 0.001f);
		TestEqual(TEXT("Grim Wake: within 3 metres"), Row->ReachMetres, 3.0f, 0.001f);
	}
	TestEqual(TEXT("Grim Wake: two rows"), Damage, 2);

	int32 Debts = 0;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("Shadow_keystone_a_kB"))))
	{
		++Debts;
		TestEqual(TEXT("Blood Debt: the pass"), Row->Action, FString(TEXT("pass_bleed_left_to_strongest")));
		TestEqual(TEXT("Blood Debt: on an afflicted death"), Row->ActionEvent, FString(TEXT("afflicted_death")));
		TestEqual(TEXT("Blood Debt: carrying a Bleed"), Row->RequiredTags, FString(TEXT("Keyword.DoT.Bleed")));
		TestEqual(TEXT("Blood Debt: over 2 seconds"), Row->ValuePerPoint, 2.0f, 0.001f);
	}
	TestEqual(TEXT("Blood Debt: one row"), Debts, 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
