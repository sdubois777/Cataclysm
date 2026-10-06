// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
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

#endif // WITH_AUTOMATION_TESTS
