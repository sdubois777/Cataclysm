// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmFollowThrough.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Components/SphereComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmSkillBar.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"
#include "TimerManager.h"

/**
 * Follow Through, `Ravager_keystone_b_kB`: "Killing an enemy with a melee attack
 * immediately repeats that attack at no cost, no more than once every 3
 * seconds." Issue #1515.
 *
 * THE STAT IS GIVEN BY HAND, as its row will give it: the keystone has no row
 * yet, so none of these can see a missing or wrong one. The rows change has to
 * add a test that wears the real row.
 *
 * THE KILLER IN MOST OF THESE IS A PLAIN ACTOR, NOT THE PLAYER, and on purpose.
 * A player that plays an attack animation waits for it before its blow lands,
 * and an automation test world is never ticked, so with the Paragon art
 * present a real player's strike would never land here. A plain actor never
 * animates. `NoteMeleeKill` is called for it exactly as the player's kill hook
 * calls it, and `ThePlayersKillHookRecordsTheRepeat` checks that hook on its
 * own.
 */
namespace CataclysmFollowThroughTest
{
	constexpr float M = 100.0f;
	const TCHAR* CleaveName = TEXT("Test Cleave");

	/** An actor with an ability system, health, mana and the combat set. */
	struct FScopedBody
	{
		FScopedBody(UWorld* World, const FVector& Where, float Health)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			// A SPHERE ON THE PAWN CHANNEL, SO A SKILL'S SEARCH FINDS IT. A plain
			// scene component has no collision, and the strike's cone search is
			// an overlap: the first run of these tests found nothing to hit.
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
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Health);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 200.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 200.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FScopedBody()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		float Health() const { return Get(UCataclysmVitalAttributeSet::GetHealthAttribute()); }
		float Mana() const { return Get(UCataclysmVitalAttributeSet::GetManaAttribute()); }

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** Follow Through's interval given by hand, as its row will give it. */
	void HoldFollowThrough(UCataclysmAbilitySystemComponent* System, float Seconds)
	{
		FCataclysmStatModifier Flat;
		Flat.Bucket = ECataclysmStatBucket::Flat;
		Flat.Source = ECataclysmModifierSource::PassiveKeystone;
		Flat.Value = Seconds;
		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line =
			Inputs.FindOrAdd(FName(UCataclysmFollowThrough::EverySecondsStat));
		Line.Base = 0.0f;
		Line.Modifiers = {Flat};
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** A melee strike named `CleaveName` in a slot, reaching 3 metres in a
	 *  90 degree arc, so where it is aimed decides what it hits. */
	UCataclysmSkillTemplate* GrantCleave(UCataclysmAbilitySystemComponent* System,
										 AActor* Owner, ECataclysmAbilitySlot Slot,
										 FGameplayAbilitySpecHandle& OutHandle)
	{
		OutHandle = System->GiveAbilityInSlot(UCataclysmStrikeSkill::StaticClass(), Slot,
											  /*Level=*/100, Owner);
		FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(OutHandle);
		UCataclysmSkillTemplate* Skill =
			Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = CleaveName;
			Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=3; Angle=90"));
			Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Melee"));
		}
		return Skill;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowThroughRepeatsTest,
	"Cataclysm.FollowThrough.AMeleeKillRepeatsTheAttackAtTheNearestEnemyAndPaysNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A cleave facing forward kills the enemy in front. The repeat is aimed at the
 * nearest living enemy in reach, to the side, and not at a farther one on the
 * other side. It pays no mana, waits for no cooldown, spends no next-use charge
 * and raises no skill_use; it spends the clock, and the line says so. The next
 * ordinary use still waits for the cooldown the first one started.
 */
bool FCataclysmFollowThroughRepeatsTest::RunTest(const FString&)
{
	using namespace CataclysmFollowThroughTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBody Killer(World, FVector::ZeroVector, 10000.0f);
	FScopedBody InFront(World, FVector(1 * M, 0, 0), 1.0f);
	FScopedBody Nearer(World, FVector(0, 2 * M, 0), 10000.0f);
	FScopedBody Farther(World, FVector(0, -2.5f * M, 0), 10000.0f);
	UCataclysmAbilitySystemComponent* System = Killer.AbilitySystem;

	FGameplayAbilitySpecHandle Handle;
	UCataclysmSkillTemplate* Cleave =
		GrantCleave(System, Killer.Actor, ECataclysmAbilitySlot::Heavy, Handle);
	if (!TestNotNull(TEXT("the cleave is granted"), Cleave))
	{
		return false;
	}
	HoldFollowThrough(System, 3.0f);

	int32 SkillUses = 0;
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("the combat events exist"), Events))
	{
		return false;
	}
	Events->OnSkillUsed.AddLambda(
		[&SkillUses](const FCataclysmSkillUsedNotice&) { ++SkillUses; });

	const float ManaBefore = Killer.Mana();
	if (!TestTrue(TEXT("the cleave is used"), System->TryActivateAbility(Handle)))
	{
		return false;
	}
	// A PLAIN ACTOR DOES NOT MARK ITSELF DEAD; only a character's own death
	// handling does. So the test checks the blow took the enemy to no health,
	// then marks it dead as that handling would, so the repeat's search for the
	// nearest LIVING enemy passes over it. The real path -- a character's death
	// reaching Follow Through's kill hook -- is covered by
	// `Cataclysm.FollowThrough.ThePlayersKillHookRecordsTheRepeat`.
	if (!TestTrue(TEXT("and takes the enemy in front to no health, which every "
					   "figure below depends on"),
				  InFront.Health() <= 0.0f))
	{
		return false;
	}
	UCataclysmSkillEffects::MarkDead(InFront.Actor);
	const float ManaAfterTheUse = Killer.Mana();
	TestTrue(TEXT("the use paid mana"), ManaAfterTheUse < ManaBefore);
	TestEqual(TEXT("and raised skill_use once"), SkillUses, 1);
	TestTrue(TEXT("and started the Heavy slot's cooldown"),
			 System->HasMatchingGameplayTag(
				 UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy)));
	TestEqual(TEXT("the side enemies were outside the forward arc"),
			  Nearer.Health(), 10000.0f);

	// THE KILL, AS THE PLAYER'S HOOK RECORDS IT, AND A CHARGE HELD FOR LATER.
	if (!TestTrue(TEXT("the melee kill earns a repeat"),
				  UCataclysmFollowThrough::NoteMeleeKill(Killer.Actor, FName(CleaveName))))
	{
		return false;
	}
	System->GrantNextUseCharge(FName(TEXT("TestCharge")), /*bAttack=*/true, 50.0f, 1);

	if (!TestTrue(TEXT("the repeat is made once the killing use has ended"),
				  UCataclysmFollowThrough::MakePendingRepeat(Killer.Actor)))
	{
		return false;
	}
	TestTrue(TEXT("it struck the nearest living enemy"), Nearer.Health() < 10000.0f);
	TestEqual(TEXT("and not the farther one behind the character"),
			  Farther.Health(), 10000.0f);
	TestEqual(TEXT("it paid no mana"), Killer.Mana(), ManaAfterTheUse);
	TestEqual(TEXT("raised no skill_use"), SkillUses, 1);
	TestEqual(TEXT("and spent no next-use charge"),
			  System->NextUseChargesHeld(FName(TEXT("TestCharge"))), 1);
	TestTrue(TEXT("it spent the clock"), System->FollowThroughSecondsLeft() > 2.9f);
	TestEqual(TEXT("which the line above the skill bar shows"),
			  UCataclysmSkillBar::FollowThroughLine(System->FollowThroughSecondsLeft()),
			  FString(TEXT("Follow Through 3s")));
	TestFalse(TEXT("nothing waits any more"), System->PendingFollowThrough().IsValid());

	TestFalse(TEXT("and the next ordinary use still waits for the cooldown the "
				   "first one started"),
			  System->TryActivateAbility(Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowThroughNoTargetTest,
	"Cataclysm.FollowThrough.NoEnemyInReachMeansNoRepeatAndTheClockIsUnspent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Ruled 2026-09-24: with nobody left in reach there is no repeat, and the clock
 *  is left for the next melee kill. */
bool FCataclysmFollowThroughNoTargetTest::RunTest(const FString&)
{
	using namespace CataclysmFollowThroughTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBody Killer(World, FVector::ZeroVector, 10000.0f);
	FScopedBody InFront(World, FVector(1 * M, 0, 0), 1.0f);
	FScopedBody OutOfReach(World, FVector(0, 10 * M, 0), 10000.0f);
	FGameplayAbilitySpecHandle Handle;
	if (!TestNotNull(TEXT("the cleave is granted"),
					 GrantCleave(Killer.AbilitySystem, Killer.Actor,
								 ECataclysmAbilitySlot::Heavy, Handle)))
	{
		return false;
	}
	HoldFollowThrough(Killer.AbilitySystem, 3.0f);

	Killer.AbilitySystem->TryActivateAbility(Handle);
	// No health left, then marked dead as a character's death would mark it:
	// see the test above for why a plain actor needs that done for it. The real
	// path is covered by `Cataclysm.FollowThrough.ThePlayersKillHookRecordsTheRepeat`.
	const bool bNoHealthLeft = InFront.Health() <= 0.0f;
	UCataclysmSkillEffects::MarkDead(InFront.Actor);
	if (!TestTrue(TEXT("the enemy in front is taken to no health"), bNoHealthLeft)
		|| !TestTrue(TEXT("and the kill earns a repeat"),
					 UCataclysmFollowThrough::NoteMeleeKill(Killer.Actor, FName(CleaveName))))
	{
		return false;
	}

	TestFalse(TEXT("with nobody in reach no repeat is made"),
			  UCataclysmFollowThrough::MakePendingRepeat(Killer.Actor));
	TestEqual(TEXT("the enemy out of reach is untouched"), OutOfReach.Health(), 10000.0f);
	TestEqual(TEXT("the clock is unspent"),
			  Killer.AbilitySystem->FollowThroughSecondsLeft(), 0.0f);
	TestTrue(TEXT("so the next melee kill may still repeat"),
			 Killer.AbilitySystem->MayFollowThrough());
	TestFalse(TEXT("and nothing waits"),
			  Killer.AbilitySystem->PendingFollowThrough().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowThroughWhatCountsTest,
	"Cataclysm.FollowThrough.OnlyYourOwnMeleeKillOutsideTheWaitAndOutsideAMovementSkillCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The kill is this character's own and not a tick; the keystone is held; the
 * clock allows it; and the killing skill is not in the Movement slot, which
 * spends nothing (ruled 2026-09-24).
 */
bool FCataclysmFollowThroughWhatCountsTest::RunTest(const FString&)
{
	using namespace CataclysmFollowThroughTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedBody Killer(World, FVector::ZeroVector, 10000.0f);
	FScopedBody Someone(World, FVector(5 * M, 0, 0), 10000.0f);

	FCataclysmDeathNotice Notice;
	Notice.Killer = Killer.Actor;
	Notice.bIsMelee = true;
	TestTrue(TEXT("a melee kill by this character counts"),
			 UCataclysmFollowThrough::IsOwnMeleeKill(Notice, Killer.Actor));
	Notice.bByDamageOverTime = true;
	TestFalse(TEXT("a tick does not"),
			  UCataclysmFollowThrough::IsOwnMeleeKill(Notice, Killer.Actor));
	Notice.bByDamageOverTime = false;
	Notice.bIsMelee = false;
	TestFalse(TEXT("nor a kill that is not melee"),
			  UCataclysmFollowThrough::IsOwnMeleeKill(Notice, Killer.Actor));
	Notice.bIsMelee = true;
	Notice.Killer = Someone.Actor;
	TestFalse(TEXT("nor somebody else's kill"),
			  UCataclysmFollowThrough::IsOwnMeleeKill(Notice, Killer.Actor));

	FGameplayAbilitySpecHandle Handle;
	GrantCleave(Killer.AbilitySystem, Killer.Actor, ECataclysmAbilitySlot::Heavy, Handle);
	TestFalse(TEXT("without the keystone a melee kill earns nothing"),
			  UCataclysmFollowThrough::NoteMeleeKill(Killer.Actor, FName(CleaveName)));

	HoldFollowThrough(Killer.AbilitySystem, 3.0f);
	Killer.AbilitySystem->NoteFollowedThrough(3.0f);
	TestFalse(TEXT("within 3 seconds of a repeat, a melee kill earns nothing, so a "
				   "repeat's own kill cannot chain"),
			  UCataclysmFollowThrough::NoteMeleeKill(Killer.Actor, FName(CleaveName)));

	FScopedBody Mover(World, FVector(0, 5 * M, 0), 10000.0f);
	GrantCleave(Mover.AbilitySystem, Mover.Actor, ECataclysmAbilitySlot::Movement, Handle);
	HoldFollowThrough(Mover.AbilitySystem, 3.0f);
	TestFalse(TEXT("a kill by a Movement-slot skill earns nothing"),
			  UCataclysmFollowThrough::NoteMeleeKill(Mover.Actor, FName(CleaveName)));
	TestTrue(TEXT("and spends nothing"), Mover.AbilitySystem->MayFollowThrough());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowThroughHookTest,
	"Cataclysm.FollowThrough.ThePlayersKillHookRecordsTheRepeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The player's own death listener is what calls `NoteMeleeKill`. A melee kill
 * announced for the player leaves a repeat waiting on its ability system; a
 * tick does not.
 */
bool FCataclysmFollowThroughHookTest::RunTest(const FString&)
{
	using namespace CataclysmFollowThroughTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!State || !Controller || !Player)
	{
		AddError(TEXT("A possessed player could not be built."));
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Player);
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("the player's ability system"), System)
		|| !TestNotNull(TEXT("the combat events"), Events))
	{
		return false;
	}

	FGameplayAbilitySpecHandle Handle;
	GrantCleave(System, Player, ECataclysmAbilitySlot::Heavy, Handle);
	HoldFollowThrough(System, 3.0f);

	FScopedBody Victim(World, FVector(1 * M, 0, 0), 1.0f);
	FCataclysmDeathNotice Notice;
	Notice.Victim = Victim.Actor;
	Notice.Killer = Player;
	Notice.KillingSkillName = FName(CleaveName);
	Notice.bIsMelee = true;
	Notice.bByDamageOverTime = true;
	Events->OnDeath.Broadcast(Notice);
	TestFalse(TEXT("a tick announced for the player leaves nothing waiting"),
			  System->PendingFollowThrough().IsValid());

	Notice.bByDamageOverTime = false;
	Events->OnDeath.Broadcast(Notice);
	TestTrue(TEXT("a melee kill announced for the player leaves a repeat waiting"),
			 System->PendingFollowThrough().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowThroughTimerTest,
	"Cataclysm.FollowThrough.ABasicAttackKillRepeatsOnTheNextFrameAndKeepsTheSwingInterval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path play takes, on a real player with the game's own controller:
 * a basic attack swung through `TrySwingAt`, a melee kill announced for it, and
 * the next-frame timer making the repeat. Ruled 2026-09-24: the repeat of a
 * basic attack is one extra swing that ignores the swing interval, and the next
 * ordinary swing keeps its timing -- so the controller's record of the last
 * swing is unchanged, and an ordinary swing is still refused.
 *
 * ONE TIMER TICK, AND IT IS ALL A TEST GETS. `FTimerManager::Tick` runs once per
 * engine frame (`LastTickedFrame == GFrameCounter`), and a test runs inside one.
 * That is enough when the killing swing has already landed. With the Paragon art
 * present the player's swing waits for its animation instead, which would need a
 * second tick, so that half is reported as skipped rather than failed.
 */
bool FCataclysmFollowThroughTimerTest::RunTest(const FString&)
{
	using namespace CataclysmFollowThroughTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerController* Controller = World->SpawnActor<ACataclysmPlayerController>();
	ACataclysmPlayerCharacter* Player = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!State || !Controller || !Player)
	{
		AddError(TEXT("A player with the game's own controller could not be built."));
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Player);
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("the player's ability system"), System)
		|| !TestNotNull(TEXT("the combat events"), Events))
	{
		return false;
	}

	// THE BASIC ATTACK ITS WEAPON GAVE IT, found by slot.
	UCataclysmSkillTemplate* Basic = nullptr;
	for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
	{
		UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
		if (Skill && Skill->Slot == ECataclysmAbilitySlot::BasicAttack)
		{
			Basic = Skill;
		}
	}
	const float ReachCm = UCataclysmBasicAttack::ReachCmOf(System);
	if (!TestNotNull(TEXT("the player has a basic attack"), Basic)
		|| !TestTrue(TEXT("which reaches somewhere"), ReachCm > 0.0f))
	{
		return false;
	}
	HoldFollowThrough(System, 3.0f);

	FScopedBody Enemy(World, FVector(ReachCm * 0.5f, 0, 0), 10000.0f);
	if (!TestTrue(TEXT("an ordinary swing at the enemy starts"),
				  Controller->TrySwingAtForTest(Enemy.Actor)))
	{
		return false;
	}
	if (Basic->IsWaitingForTheSwingToConnect())
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the player's swing waits for its animation, and a test world's "
				 "timer manager ticks once, so the repeat after it cannot be seen here"));
		return true;
	}
	const float SwungAt = Controller->LastSwingSecondsForTest();
	const float HealthAfterTheSwing = Enemy.Health();
	if (!TestTrue(TEXT("the swing struck the enemy"), HealthAfterTheSwing < 10000.0f))
	{
		return false;
	}

	FScopedBody Victim(World, FVector(0, 50 * M, 0), 1.0f);
	FCataclysmDeathNotice Notice;
	Notice.Victim = Victim.Actor;
	Notice.Killer = Player;
	Notice.KillingSkillName = FName(*Basic->SkillName);
	Notice.bIsMelee = true;
	Events->OnDeath.Broadcast(Notice);
	if (!TestTrue(TEXT("the kill hook left a repeat waiting"),
				  System->PendingFollowThrough().IsValid()))
	{
		return false;
	}

	World->GetTimerManager().Tick(0.1f);
	TestTrue(TEXT("the next-frame timer made the repeat, which struck the enemy again"),
			 Enemy.Health() < HealthAfterTheSwing);
	TestTrue(TEXT("and spent the clock"), System->FollowThroughSecondsLeft() > 2.9f);
	TestEqual(TEXT("the controller's last swing is still the ordinary one"),
			  Controller->LastSwingSecondsForTest(), SwungAt);
	TestFalse(TEXT("so an ordinary swing now is still refused by its interval"),
			  Controller->TrySwingAtForTest(Enemy.Actor));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFollowThroughLineTest,
	"Cataclysm.FollowThrough.TheLineAboveTheSkillBarCountsTheWaitInWholeSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Approved 2026-09-24: "Follow Through 2s" while the clock runs, rounded up,
 *  and nothing when a repeat may be made now. */
bool FCataclysmFollowThroughLineTest::RunTest(const FString&)
{
	TestEqual(TEXT("three seconds reads 3"), UCataclysmSkillBar::FollowThroughLine(3.0f),
			  FString(TEXT("Follow Through 3s")));
	TestEqual(TEXT("2.2 rounds up to 3"), UCataclysmSkillBar::FollowThroughLine(2.2f),
			  FString(TEXT("Follow Through 3s")));
	TestEqual(TEXT("the last tenth still reads 1"),
			  UCataclysmSkillBar::FollowThroughLine(0.1f), FString(TEXT("Follow Through 1s")));
	TestEqual(TEXT("and nothing when it is ready"),
			  UCataclysmSkillBar::FollowThroughLine(0.0f), FString());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
