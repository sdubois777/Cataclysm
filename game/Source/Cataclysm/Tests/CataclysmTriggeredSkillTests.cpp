// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmFear.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * `UCataclysmTriggeredSkill`: a skill granted from its row with no key, started free at an aim point and removed when
 * it ends. Issue #41.
 *
 * THE CASTER IS A PLAIN ACTOR, as in the Follow Through tests and for their reason: a player waits for its attack
 * clip before a blow lands, and an automation test world is never ticked.
 */
namespace CataclysmTriggeredSkillTest
{
	constexpr float M = 100.0f;

	/** An actor with an ability system and the four attribute sets a skill of any shape reads. */
	struct FScopedBody
	{
		FScopedBody(UWorld* World, const FVector& Where, float Health)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			// A SPHERE ON THE PAWN CHANNEL, so a skill's search finds it.
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
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
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

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		float Mana() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetManaAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A row as the weapon skill matrix would give it. */
	FCataclysmWeaponSkill ARow(ECataclysmAbilitySlot Slot, const TCHAR* Name, ECataclysmSkillShape Shape,
							   const TCHAR* Params, const TCHAR* Tags)
	{
		FCataclysmWeaponSkill Row;
		Row.Slot = Slot;
		Row.Name = Name;
		Row.Shape = Shape;
		Row.Params = UCataclysmSkillShapes::ParseParams(Params);
		Row.Tags = UCataclysmSkillShapes::TagsFromCell(Tags);
		return Row;
	}

	/** A melee strike reaching 3 metres in a 90 degree arc, so where it is aimed decides what it hits. */
	FCataclysmWeaponSkill ACleaveRow(const TCHAR* Name)
	{
		return ARow(ECataclysmAbilitySlot::Heavy, Name, ECataclysmSkillShape::Strike, TEXT("Radius=3; Angle=90"),
					TEXT("Type.Melee"));
	}

	/** The character's own Heavy skill, granted the ordinary way, with its key. */
	FGameplayAbilitySpecHandle GrantAHeldCleave(const FScopedBody& Caster)
	{
		const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
			UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Heavy, /*Level=*/100, Caster.Actor);
		FGameplayAbilitySpec* Spec = Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		if (UCataclysmSkillTemplate* Skill = Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr)
		{
			UCataclysmWeaponSkills::StampOnto(*Skill, ACleaveRow(TEXT("Held Cleave")));
		}
		return Handle;
	}

	int32 SkillsHeld(const FScopedBody& Caster)
	{
		return Caster.AbilitySystem->GetActivatableAbilities().Num();
	}

	/** How many of the character's skills a key in any slot would find. */
	int32 SkillsAKeyFinds(const FScopedBody& Caster)
	{
		int32 Found = 0;
		for (const FGameplayAbilitySpec& Spec : Caster.AbilitySystem->GetActivatableAbilities())
		{
			for (const ECataclysmAbilitySlot Slot : CataclysmAbilitySlots::All())
			{
				Found += Spec.GetDynamicSpecSourceTags().HasTagExact(CataclysmAbilitySlots::Tag(Slot)) ? 1 : 0;
			}
		}
		return Found;
	}

	/** The skill of this name the character holds and that is running, or null. */
	UCataclysmSkillTemplate* TheRunningSkillNamed(const FScopedBody& Caster, const TCHAR* Name,
												  FGameplayAbilitySpecHandle& OutHandle)
	{
		for (const FGameplayAbilitySpec& Spec : Caster.AbilitySystem->GetActivatableAbilities())
		{
			UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
			if (Skill && Spec.IsActive() && Skill->SkillName == Name)
			{
				OutHandle = Spec.Handle;
				return Skill;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTriggeredSkillStrikesTest,
	"Cataclysm.TriggeredSkill.ItStrikesAtItsAimForFreeAndIsGoneAfterwards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTriggeredSkillStrikesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmTriggeredSkillTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// ONE ENEMY IN FRONT FOR THE HELD SKILL, ONE TO EACH SIDE FOR THE TRIGGERED ONE: its aim decides which is hit.
	FScopedBody Caster(World, FVector::ZeroVector, 10000.0f);
	FScopedBody InFront(World, FVector(1 * M, 0, 0), 10000.0f);
	FScopedBody Left(World, FVector(0, 2 * M, 0), 10000.0f);
	FScopedBody Right(World, FVector(0, -2 * M, 0), 10000.0f);
	UCataclysmAbilitySystemComponent* System = Caster.AbilitySystem;

	// THE CHARACTER'S OWN HEAVY SKILL, USED ONCE, so its mana is spent and the Heavy slot's cooldown is running.
	const FGameplayAbilitySpecHandle Held = GrantAHeldCleave(Caster);
	int32 SkillUses = 0;
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	const FDelegateHandle Listening = Events->OnSkillUsed.AddLambda(
		[&SkillUses](const FCataclysmSkillUsedNotice&) { ++SkillUses; });
	ON_SCOPE_EXIT { Events->OnSkillUsed.Remove(Listening); };
	if (!TestTrue(TEXT("set-up: the held skill is used"), System->TryActivateAbility(Held))
		|| !TestTrue(TEXT("set-up: and its cooldown runs"),
					 System->HasMatchingGameplayTag(UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy)))
		|| !TestEqual(TEXT("set-up: and it raised one skill use"), SkillUses, 1))
	{
		return false;
	}
	const float ManaBefore = Caster.Mana();
	const int32 HeldBefore = SkillsHeld(Caster);
	const int32 KeysBefore = SkillsAKeyFinds(Caster);
	const float LeftBefore = Left.Health();

	// TRIGGERED IN THE SAME SLOT, WHILE THAT SLOT'S COOLDOWN RUNS, AIMED LEFT.
	if (!TestTrue(TEXT("the triggered skill starts, though its slot is on cooldown"),
				  UCataclysmTriggeredSkill::Trigger(Caster.Actor, ACleaveRow(TEXT("Triggered Cleave")),
													Left.Actor->GetActorLocation())))
	{
		return false;
	}
	TestTrue(TEXT("it struck the enemy it was aimed at"), Left.Health() < LeftBefore);
	TestEqual(TEXT("and not the one on the other side"), Right.Health(), 10000.0f);
	TestEqual(TEXT("it paid no mana"), Caster.Mana(), ManaBefore);
	TestEqual(TEXT("it raised no skill use"), SkillUses, 1);

	// GONE, AND NOTHING OF THE CHARACTER'S OWN WENT WITH IT.
	TestEqual(TEXT("the character holds as many skills as before"), SkillsHeld(Caster), HeldBefore);
	TestEqual(TEXT("and a key finds as many as before"), SkillsAKeyFinds(Caster), KeysBefore);
	TestNotNull(TEXT("the held skill is still held"), System->FindAbilitySpecFromHandle(Held));
	TestTrue(TEXT("the Heavy slot's cooldown still runs"),
			 System->HasMatchingGameplayTag(UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy)));
	TestFalse(TEXT("so the held skill still waits for it"), System->TryActivateAbility(Held));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTriggeredSkillNoKeyTest,
	"Cataclysm.TriggeredSkill.ARunningOneIsFoundByNoKeyAndIsRemovedWhenItEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTriggeredSkillNoKeyTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmTriggeredSkillTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedBody Caster(World, FVector::ZeroVector, 10000.0f);
	UCataclysmAbilitySystemComponent* System = Caster.AbilitySystem;
	const FGameplayAbilitySpecHandle Held = GrantAHeldCleave(Caster);
	const int32 HeldBefore = SkillsHeld(Caster);
	const int32 KeysBefore = SkillsAKeyFinds(Caster);
	const float ManaBefore = Caster.Mana();

	// A SELF BUFF LASTS, so it is still running after `Trigger` returns. Ashen Edge's own parameters.
	const TCHAR* BuffName = TEXT("Triggered Buff");
	if (!TestTrue(TEXT("the triggered buff starts"),
				  UCataclysmTriggeredSkill::Trigger(
					  Caster.Actor,
					  ARow(ECataclysmAbilitySlot::Support, BuffName, ECataclysmSkillShape::SelfBuff,
						   TEXT("Duration=10; ConsumeRadius=4"), TEXT("Type.Buff")),
					  FVector::ZeroVector)))
	{
		return false;
	}
	FGameplayAbilitySpecHandle Running;
	const UCataclysmSkillTemplate* Buff = TheRunningSkillNamed(Caster, BuffName, Running);
	if (!TestNotNull(TEXT("it is running"), Buff))
	{
		return false;
	}
	TestEqual(TEXT("while it runs the character holds one more skill"), SkillsHeld(Caster), HeldBefore + 1);
	TestEqual(TEXT("but a key finds no more than before"), SkillsAKeyFinds(Caster), KeysBefore);
	TestEqual(TEXT("its slot property is the row's, which is where it finds its figures"),
			  static_cast<int32>(Buff->Slot), static_cast<int32>(ECataclysmAbilitySlot::Support));

	// PRESSING ITS SLOT'S KEY DOES NOT REACH IT. Nothing else is in the Support slot, so nothing is queued.
	System->AbilityInputTagPressed(CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Support));
	const FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Running);
	TestTrue(TEXT("a press of its slot's key leaves it unpressed"), Spec && !Spec->InputPressed);
	TestEqual(TEXT("it paid no mana"), Caster.Mana(), ManaBefore);
	TestFalse(TEXT("and started no cooldown in its slot"),
			  System->HasMatchingGameplayTag(UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Support)));

	// ENDED, IT IS REMOVED. Cancelled here because a test world's clock never runs its ten seconds out.
	System->CancelAbilityHandle(Running);
	TestNull(TEXT("once it ends it is no longer held"), System->FindAbilitySpecFromHandle(Running));
	TestEqual(TEXT("and the character holds as many skills as before"), SkillsHeld(Caster), HeldBefore);
	TestNotNull(TEXT("the held skill is still held"), System->FindAbilitySpecFromHandle(Held));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTriggeredSkillRefusedTest,
	"Cataclysm.TriggeredSkill.ARefusedOneLeavesNothingBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTriggeredSkillRefusedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmTriggeredSkillTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedBody Caster(World, FVector::ZeroVector, 10000.0f);
	FScopedBody Enemy(World, FVector(1 * M, 0, 0), 10000.0f);
	UCataclysmAbilitySystemComponent* System = Caster.AbilitySystem;
	const FGameplayAbilitySpecHandle Held = GrantAHeldCleave(Caster);
	const int32 HeldBefore = SkillsHeld(Caster);

	// A ROW WITH NO SHAPE has no template to run.
	TestFalse(TEXT("a row with no shape does not start"),
			  UCataclysmTriggeredSkill::Trigger(
				  Caster.Actor,
				  ARow(ECataclysmAbilitySlot::Heavy, TEXT("Undesigned"), ECataclysmSkillShape::None, TEXT(""), TEXT("")),
				  Enemy.Actor->GetActorLocation()));
	TestEqual(TEXT("and leaves nothing held"), SkillsHeld(Caster), HeldBefore);

	// A SKILL THAT REFUSES ITSELF: it requires a burning enemy and none burns.
	TestFalse(TEXT("a skill whose requirement is unmet does not start"),
			  UCataclysmTriggeredSkill::Trigger(
				  Caster.Actor,
				  ARow(ECataclysmAbilitySlot::Heavy, TEXT("Needs A Burn"), ECataclysmSkillShape::Strike,
					   TEXT("Radius=8; Angle=360; Burn=1; ConsumeBurn=1; Requires=Burning"), TEXT("Type.Melee")),
				  Enemy.Actor->GetActorLocation()));
	TestEqual(TEXT("and leaves nothing held"), SkillsHeld(Caster), HeldBefore);
	TestEqual(TEXT("and struck nothing"), Enemy.Health(), 10000.0f);

	// INSIDE ANOTHER SKILL'S ACTIVATION the engine defers a grant, so there is no instance to stamp. The held skill's
	// own skill-used notice is sent from inside its activation, which is where a rule reacting to it would stand.
	bool bStartedInside = true;
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	AActor* CasterActor = Caster.Actor;
	const FVector EnemyAt = Enemy.Actor->GetActorLocation();
	const FDelegateHandle Listening = Events->OnSkillUsed.AddLambda(
		[&bStartedInside, CasterActor, EnemyAt](const FCataclysmSkillUsedNotice&)
		{
			bStartedInside = UCataclysmTriggeredSkill::Trigger(CasterActor, ACleaveRow(TEXT("Triggered Inside")), EnemyAt);
		});
	ON_SCOPE_EXIT { Events->OnSkillUsed.Remove(Listening); };
	if (!TestTrue(TEXT("set-up: the held skill is used"), System->TryActivateAbility(Held)))
	{
		return false;
	}
	const float AfterTheHeldBlow = Enemy.Health();
	TestTrue(TEXT("set-up: the held skill struck"), AfterTheHeldBlow < 10000.0f);
	TestFalse(TEXT("a trigger from inside another skill's activation does not start"), bStartedInside);
	TestEqual(TEXT("and no deferred grant arrives afterwards"), SkillsHeld(Caster), HeldBefore);

	// THE SAME ROW, ONCE THAT ACTIVATION IS OVER, STARTS: which is what waiting for the next tick buys.
	Events->OnSkillUsed.Remove(Listening);
	TestTrue(TEXT("the same row starts once the activation is over"),
			 UCataclysmTriggeredSkill::Trigger(Caster.Actor, ACleaveRow(TEXT("Triggered After")), EnemyAt));
	TestTrue(TEXT("and strikes"), Enemy.Health() < AfterTheHeldBlow);
	TestEqual(TEXT("and is gone"), SkillsHeld(Caster), HeldBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTriggeredSkillShareTest,
	"Cataclysm.TriggeredSkill.AShareOfHalfDealsHalfTheDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTriggeredSkillShareTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmTriggeredSkillTest;

	CataclysmTestWorld::SilenceCriticalStrikes();
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE SAME STRIKE TWICE, ONCE AT EACH OF TWO BODIES OF THE SAME HEALTH: whole, then at a share of half.
	FScopedBody Caster(World, FVector::ZeroVector, 10000.0f);
	FScopedBody Left(World, FVector(0, 2 * M, 0), 10000.0f);
	FScopedBody Right(World, FVector(0, -2 * M, 0), 10000.0f);
	if (!TestTrue(TEXT("set-up: the whole strike starts"),
				  UCataclysmTriggeredSkill::Trigger(Caster.Actor, ACleaveRow(TEXT("Whole")), Left.Actor->GetActorLocation())))
	{
		return false;
	}
	const float Whole = 10000.0f - Left.Health();
	if (!TestTrue(FString::Printf(TEXT("set-up: the whole strike dealt damage (%.2f)"), Whole), Whole > 0.0f)
		|| !TestEqual(TEXT("set-up: and did not reach the other body"), Right.Health(), 10000.0f))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the half strike starts"),
				  UCataclysmTriggeredSkill::Trigger(Caster.Actor, ACleaveRow(TEXT("Half")), Right.Actor->GetActorLocation(),
													0.5f)))
	{
		return false;
	}
	TestEqual(TEXT("a share of half deals half of what the whole strike dealt"), 10000.0f - Right.Health(), Whole * 0.5f,
			  0.01f);

	// AND THE SHARE DOES NOT STAY BEHIND: the next free start with no share stated deals the whole again.
	Left.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
	if (!TestTrue(TEXT("set-up: a third strike starts"),
				  UCataclysmTriggeredSkill::Trigger(Caster.Actor, ACleaveRow(TEXT("Whole Again")),
													Left.Actor->GetActorLocation())))
	{
		return false;
	}
	TestEqual(TEXT("a later strike with no share stated deals the whole"), 10000.0f - Left.Health(), Whole, 0.01f);
	return true;
}

// ==========================================================================
// A character that cannot act uses no skill, by any route. Issue #1627, ruled 2026-10-27 by the coordinating session:
// the check is in `UCataclysmSkillTemplate::CanActivateAbility`, and it refuses a stunned, knocked-down or feared
// avatar unless the use names another actor casting it (`FreeRepeatCastBy`). The design's crowd-control table: Stun and
// Knockdown, "The target cannot act at all"; Fear, "cannot attack or use a skill". One caster per case, 30 m apart.
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTriggeredSkillStunnedCasterTest,
	"Cataclysm.TriggeredSkill.AStunnedCharacterStartsNoSkillAndAnUnstunnedOneDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTriggeredSkillStunnedCasterTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmTriggeredSkillTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedBody Free(World, FVector::ZeroVector, 10000.0f);
	FScopedBody FreeEnemy(World, FVector(1 * M, 0, 0), 10000.0f);
	FScopedBody Stunned(World, FVector(30 * M, 0, 0), 10000.0f);
	FScopedBody StunnedEnemy(World, FVector(31 * M, 0, 0), 10000.0f);
	const FGameplayAbilitySpecHandle FreeHeld = GrantAHeldCleave(Free);
	const FGameplayAbilitySpecHandle StunnedHeld = GrantAHeldCleave(Stunned);

	// THE CONTROL FIRST: unstunned, a free start and the character's own skill both start.
	TestTrue(TEXT("control: an unstunned character's free start starts"),
			 UCataclysmTriggeredSkill::Trigger(Free.Actor, ACleaveRow(TEXT("Free Cleave")),
											   FreeEnemy.Actor->GetActorLocation()));
	TestTrue(TEXT("control: and strikes"), FreeEnemy.Health() < 10000.0f);
	TestTrue(TEXT("control: and its own held skill is used"), Free.AbilitySystem->TryActivateAbility(FreeHeld));

	// A DESIGNED STUN, AS A SKILL LAYS ONE, by the enemy in front of it.
	if (!TestTrue(TEXT("set-up: the designed stun lands"),
				  UCataclysmSkillEffects::ApplyStun(StunnedEnemy.Actor, Stunned.Actor, /*DurationSeconds=*/2.0f,
													/*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true))
		|| !TestTrue(TEXT("set-up: and the character cannot act"), UCataclysmSkillEffects::CannotAct(Stunned.Actor)))
	{
		return false;
	}
	const int32 HeldBefore = SkillsHeld(Stunned);
	TestFalse(TEXT("a stunned character's free start does not start"),
			  UCataclysmTriggeredSkill::Trigger(Stunned.Actor, ACleaveRow(TEXT("Stunned Cleave")),
												StunnedEnemy.Actor->GetActorLocation()));
	TestEqual(TEXT("and struck nothing"), StunnedEnemy.Health(), 10000.0f);
	TestEqual(TEXT("and leaves nothing held"), SkillsHeld(Stunned), HeldBefore);
	TestFalse(TEXT("and its own held skill is refused too, as the key press refuses it"),
			  Stunned.AbilitySystem->TryActivateAbility(StunnedHeld));
	TestEqual(TEXT("so its enemy still has its whole health"), StunnedEnemy.Health(), 10000.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTriggeredSkillKnockedDownOrFearedTest,
	"Cataclysm.TriggeredSkill.AKnockedDownOrFearedCharacterStartsNoSkillAndAPinnedOneDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTriggeredSkillKnockedDownOrFearedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmTriggeredSkillTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE TAG EACH STATE'S OWN FUNCTION LAYS, PUT ON BY HAND: `UCataclysmFear::ApplyFear` refuses anything that is not a
	// character, and these casters are plain actors. The check reads the tags and nothing else (`CannotAct`,
	// `IsFeared`). A PIN IS THE CONTROL: it stops movement and takes no skill away, which issue #1627 says must survive.
	FScopedBody Pinned(World, FVector::ZeroVector, 10000.0f);
	FScopedBody PinnedEnemy(World, FVector(1 * M, 0, 0), 10000.0f);
	FScopedBody KnockedDown(World, FVector(30 * M, 0, 0), 10000.0f);
	FScopedBody KnockedDownEnemy(World, FVector(31 * M, 0, 0), 10000.0f);
	FScopedBody Feared(World, FVector(60 * M, 0, 0), 10000.0f);
	FScopedBody FearedEnemy(World, FVector(61 * M, 0, 0), 10000.0f);
	Pinned.AbilitySystem->AddLooseGameplayTag(UCataclysmSkillEffects::PinnedTag());
	KnockedDown.AbilitySystem->AddLooseGameplayTag(UCataclysmSkillEffects::KnockedDownTag());
	Feared.AbilitySystem->AddLooseGameplayTag(UCataclysmFear::FearedTag());
	if (!TestTrue(TEXT("set-up: the pinned character is pinned and may act"),
				  UCataclysmSkillEffects::IsPinned(Pinned.Actor) && !UCataclysmSkillEffects::CannotAct(Pinned.Actor)
					  && !UCataclysmFear::IsFeared(Pinned.Actor))
		|| !TestTrue(TEXT("set-up: the knocked-down one cannot act"), UCataclysmSkillEffects::CannotAct(KnockedDown.Actor))
		|| !TestTrue(TEXT("set-up: the feared one is feared"), UCataclysmFear::IsFeared(Feared.Actor)))
	{
		return false;
	}

	TestTrue(TEXT("control: a pinned character's free start starts"),
			 UCataclysmTriggeredSkill::Trigger(Pinned.Actor, ACleaveRow(TEXT("Pinned Cleave")),
											   PinnedEnemy.Actor->GetActorLocation()));
	TestTrue(TEXT("control: and strikes"), PinnedEnemy.Health() < 10000.0f);
	TestFalse(TEXT("a knocked-down character's free start does not start"),
			  UCataclysmTriggeredSkill::Trigger(KnockedDown.Actor, ACleaveRow(TEXT("Knocked Down Cleave")),
												KnockedDownEnemy.Actor->GetActorLocation()));
	TestEqual(TEXT("and struck nothing"), KnockedDownEnemy.Health(), 10000.0f);
	TestFalse(TEXT("a feared character's free start does not start"),
			  UCataclysmTriggeredSkill::Trigger(Feared.Actor, ACleaveRow(TEXT("Feared Cleave")),
												FearedEnemy.Actor->GetActorLocation()));
	TestEqual(TEXT("and struck nothing"), FearedEnemy.Health(), 10000.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTriggeredSkillCastByAnotherTest,
	"Cataclysm.TriggeredSkill.AFreeStartCastByAnotherActorIsNotStoppedByItsCastersStun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTriggeredSkillCastByAnotherTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmTriggeredSkillTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A GADGET, A TRAP OR A TYRANT'S MINION CASTS ITS PLAYER'S SKILL BY NAMING ITSELF, and is not stopped by its player's
	// stun (Q1, ruled 2026-10-27). The actor named here stands 5 m from the stunned caster, with an enemy 1 m in front
	// of it; the caster's own enemy stands 1 m in front of the caster, and each strike reaches 3 m.
	FScopedBody Caster(World, FVector::ZeroVector, 10000.0f);
	FScopedBody CastersEnemy(World, FVector(1 * M, 0, 0), 10000.0f);
	FScopedBody CastingFor(World, FVector(0, 5 * M, 0), 10000.0f);
	FScopedBody ItsEnemy(World, FVector(1 * M, 5 * M, 0), 10000.0f);
	if (!TestTrue(TEXT("set-up: the designed stun lands"),
				  UCataclysmSkillEffects::ApplyStun(CastersEnemy.Actor, Caster.Actor, /*DurationSeconds=*/2.0f,
													/*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true))
		|| !TestTrue(TEXT("set-up: and the caster cannot act"), UCataclysmSkillEffects::CannotAct(Caster.Actor)))
	{
		return false;
	}

	// THE CONTROL: the same caster, stunned, casting in its own name.
	TestFalse(TEXT("control: cast in its own name, the stunned caster's free start does not start"),
			  UCataclysmTriggeredSkill::Trigger(Caster.Actor, ACleaveRow(TEXT("Own Cleave")),
												CastersEnemy.Actor->GetActorLocation()));
	TestEqual(TEXT("control: and struck nothing"), CastersEnemy.Health(), 10000.0f);

	const FVector From = CastingFor.Actor->GetActorLocation();
	TestTrue(TEXT("cast by another actor from its place, the stunned caster's skill starts"),
			 UCataclysmTriggeredSkill::Trigger(Caster.Actor, ACleaveRow(TEXT("Cast For It")),
											   ItsEnemy.Actor->GetActorLocation(), /*DamageShare=*/1.0f,
											   /*bPaysCost=*/false, &From, /*CastBy=*/CastingFor.Actor));
	TestTrue(TEXT("and strikes the enemy in front of that actor"), ItsEnemy.Health() < 10000.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTriggeredSkillChargeSelfStunTest,
	"Cataclysm.TriggeredSkill.AChargeSkillsOwnStunStopsTheFreeStartThatFollowsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTriggeredSkillChargeSelfStunTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmTriggeredSkillTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// "After using a charge skill you are briefly stunned for 0.5-1 second": the row as the item builds it, on
	// `skill_end` asking for `Keyword.Charge`, as `Cataclysm.StatExemption.ARowLaysAStatusOnItsOwnWearer` builds it. A
	// charge ends inside its own activation in an unticked world, so its stun is on before anything follows it, as it is
	// before a repeat made on the next tick. THE FREE START STANDS FOR THAT REPEAT: `MakePendingRepeat` ends in the same
	// `Trigger`. A blink, which does not carry the keyword, is the control.
	FCataclysmPoolAction Stun;
	Stun.Event = FName(TEXT("skill_end"));
	Stun.ApplyStatus = ECataclysmApplyStatus::Seconds;
	Stun.bStatusOnTheWearer = true;
	Stun.StatusName = TEXT("Stun");
	Stun.Percent = 0.75f;
	Stun.TriggerKey = FName(TEXT("a charge stuns its user"));
	Stun.RequiredTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Keyword.Charge"));
	const auto UseAMove = [&Stun](FScopedBody& Mover, const TCHAR* ParamsCell, const TCHAR* TagsCell) -> bool
	{
		Mover.Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
		Mover.Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		Mover.AbilitySystem->SetPoolActions({Stun});
		const FGameplayAbilitySpecHandle Handle = Mover.AbilitySystem->GiveAbilityInSlot(
			UCataclysmMovementSkill::StaticClass(), ECataclysmAbilitySlot::Movement, /*Level=*/100, Mover.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? Mover.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmSkillTemplate* Skill = Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			return false;
		}
		Skill->SkillName = TEXT("A move that may stun its user");
		Skill->Params = UCataclysmSkillShapes::ParseParams(ParamsCell);
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagsCell);
		return Mover.AbilitySystem->TryActivateAbility(Handle);
	};
	FScopedBody Blinker(World, FVector(0, 30 * M, 0), 10000.0f);
	FScopedBody Charger(World, FVector(0, 60 * M, 0), 10000.0f);
	if (!TestTrue(TEXT("set-up: the blink ran"), UseAMove(Blinker, TEXT("Mode=Blink; Range=8; Radius=3.5"),
		TEXT("Item.Weapon.Wand, Element.Demonic, Slot.Movement")))
		|| !TestTrue(TEXT("set-up: the charge ran"), UseAMove(Charger, TEXT("Mode=Charge; Range=8; Radius=1.5"),
		TEXT("Item.Weapon.Sword, Element.Demonic, Keyword.Charge, Slot.Movement"))))
	{
		return false;
	}

	TestFalse(TEXT("control: the blink leaves its user able to act"), UCataclysmSkillEffects::CannotAct(Blinker.Actor));
	TestTrue(TEXT("control: so the free start that follows it starts"),
			 UCataclysmTriggeredSkill::Trigger(Blinker.Actor, ACleaveRow(TEXT("After The Blink")),
											   Blinker.Actor->GetActorLocation() + FVector(1 * M, 0, 0)));
	if (!TestTrue(TEXT("set-up: the charge's own row stunned its user"), UCataclysmSkillEffects::IsStunned(Charger.Actor)))
	{
		return false;
	}
	TestFalse(TEXT("the free start that follows the charge does not start"),
			  UCataclysmTriggeredSkill::Trigger(Charger.Actor, ACleaveRow(TEXT("After The Charge")),
												Charger.Actor->GetActorLocation() + FVector(1 * M, 0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTriggeredSkillStunnedAuraTest,
	"Cataclysm.TriggeredSkill.ARunningAuraKeepsPulsingWhileItsCasterIsStunned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTriggeredSkillStunnedAuraTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmTriggeredSkillTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// UNCHANGED BY THE CHECK: it refuses an activation, and a running aura's pulse is not one. The aura is switched on
	// unstunned; the stun lands while it runs.
	FScopedBody Caster(World, FVector::ZeroVector, 10000.0f);
	FScopedBody Enemy(World, FVector(2 * M, 0, 0), 10000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
	Caster.Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
	const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
		UCataclysmAuraSkill::StaticClass(), ECataclysmAbilitySlot::Aura, /*Level=*/1, Caster.Actor);
	FGameplayAbilitySpec* Spec = Handle.IsValid() ? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
	UCataclysmAuraSkill* Aura = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("set-up: the aura is granted"), Aura))
	{
		return false;
	}
	Aura->SkillName = TEXT("Test Aura");
	Aura->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=4; Interval=1"));
	if (!TestTrue(TEXT("set-up: it is switched on"),
				  Caster.AbilitySystem->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false)))
	{
		return false;
	}

	Aura->Pulse();
	const float AfterTheFirst = Enemy.Health();
	TestTrue(TEXT("control: its first pulse strikes the enemy inside it"), AfterTheFirst < 10000.0f);

	if (!TestTrue(TEXT("set-up: the designed stun lands"),
				  UCataclysmSkillEffects::ApplyStun(Enemy.Actor, Caster.Actor, /*DurationSeconds=*/2.0f,
													/*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true))
		|| !TestTrue(TEXT("set-up: and the caster cannot act"), UCataclysmSkillEffects::CannotAct(Caster.Actor)))
	{
		return false;
	}
	TestTrue(TEXT("the aura still runs once its caster is stunned"), Aura->IsActive());
	Aura->Pulse();
	TestEqual(TEXT("and pulses again"), Aura->Pulses, 2);
	TestTrue(TEXT("striking the enemy again"), Enemy.Health() < AfterTheFirst);
	TestTrue(TEXT("and runs on after it"), Aura->IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTriggeredSkillStunnedBasicAttackTest,
	"Cataclysm.TriggeredSkill.AStunnedCharactersBasicAttackIsRefusedByItsOwnCheckAndByTheSkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTriggeredSkillStunnedBasicAttackTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmTriggeredSkillTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE BASIC ATTACK IS NOT EXEMPT FROM THE CHECK. Every caller of `UCataclysmBasicAttack::Swing` in the game asks
	// `MaySwing` first, which is the same question, so that refusal is unchanged; the check refuses a swing started by
	// any other route as well. Granted as `Cataclysm.Skills.ABasicAttackSurvivesALockOnEverySkill` grants it.
	FScopedBody Free(World, FVector::ZeroVector, 10000.0f);
	FScopedBody FreeTarget(World, FVector(2 * M, 0, 0), 10000.0f);
	FScopedBody Stunned(World, FVector(30 * M, 0, 0), 10000.0f);
	FScopedBody StunnedTarget(World, FVector(32 * M, 0, 0), 10000.0f);
	const auto GrantTheBasicAttack = [](const FScopedBody& Swinger) -> bool
	{
		const FGameplayAbilitySpecHandle Handle = Swinger.AbilitySystem->GiveAbilityInSlot(
			UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::BasicAttack, /*Level=*/100, Swinger.Actor);
		FGameplayAbilitySpec* Spec = Swinger.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UCataclysmStrikeSkill* Swing = Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Swing)
		{
			return false;
		}
		Swing->SkillName = TEXT("Basic Attack");
		Swing->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=2.4; Angle=120; MaxTargets=1"));
		return true;
	};
	if (!TestTrue(TEXT("set-up: both are granted a basic attack"), GrantTheBasicAttack(Free) && GrantTheBasicAttack(Stunned)))
	{
		return false;
	}
	constexpr float ReachCm = 240.0f;

	TestTrue(TEXT("control: an unstunned character may swing"), UCataclysmBasicAttack::MaySwing(Free.Actor));
	TestTrue(TEXT("control: and swings"), UCataclysmBasicAttack::Swing(Free.AbilitySystem));

	if (!TestTrue(TEXT("set-up: the designed stun lands"),
				  UCataclysmSkillEffects::ApplyStun(StunnedTarget.Actor, Stunned.Actor, /*DurationSeconds=*/2.0f,
													/*DamageDealt=*/0.0f, /*bStunIsDesigned=*/true)))
	{
		return false;
	}
	TestFalse(TEXT("a stunned character may not swing, by the basic attack's own check"),
			  UCataclysmBasicAttack::MaySwing(Stunned.Actor));
	TestFalse(TEXT("so it does not swing at what stands in its reach"),
			  UCataclysmBasicAttack::ShouldSwingNow(Stunned.Actor, ReachCm));
	TestFalse(TEXT("and a swing started past that check is refused by the skill"),
			  UCataclysmBasicAttack::Swing(Stunned.AbilitySystem));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
