// Copyright Stephen Dubois. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * What a character commands, and the four Staff skills built on it.
 *
 * WHY THIS IS ITS OWN FILE RATHER THAN MORE OF `CataclysmSkillTemplateTests.cpp`.
 * That file's test fighter is a bare `AActor` with an ability system bolted on,
 * which is enough for almost everything and is not enough here. Three of these
 * tests need a real `ACataclysmEnemyCharacter`: subjugation changes which side a
 * creature is on and keeps its body, a boss refusal reads the rarity only that
 * class carries, and an order is obeyed by the controller that only a real
 * character has.
 *
 * SO BOTH KINDS APPEAR BELOW, AND THE DIFFERENCE IS DELIBERATE. The commander is
 * a bare actor, because it needs a class resource pool and skills granted into
 * slots and never needs to be found by a search. Everything it commands, and
 * everything it takes, is a real character, because those are what get walked,
 * driven and counted.
 */
namespace CataclysmCommandTest
{
	/** Metres, so the tests read the way the design document does. */
	constexpr float M = 100.0f;

	/** What the Ritualist's row in `game/Data/ClassStats.csv` gives it. */
	constexpr float RitualistPool = 150.0f;

	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/**
	 * The commander: an actor that can hold skills and a resource pool.
	 *
	 * A BARE ACTOR, MATCHING `CataclysmSkillTemplateTests.cpp`. It is never
	 * searched for, only searched from, so it does not have to be a character.
	 */
	struct FScopedCaster
	{
		FScopedCaster(UWorld* World, const FVector& Where,
					  float Pool = RitualistPool)
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

			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmClassResourceAttributeSet>(Actor));

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

			// THE RITUALIST'S OWN POOL BY DEFAULT, so the thrall cap the tests
			// check is the number the design states rather than one invented
			// here. A test that wants a smaller army asks for a smaller pool.
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
				Pool);
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
				Pool);
		}

		~FScopedCaster()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A real creature: something that can be taken, driven and counted. */
	struct FScopedCreature
	{
		FScopedCreature(UWorld* World, const FVector& Where,
						ECataclysmTeam Team = ECataclysmTeam::Monsters,
						float Health = 1000.0f)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
				Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));

			// `SetHealth` sets the MAXIMUM, which is what a health threshold is
			// a share of. A thousand makes half of it a round five hundred.
			Actor->SetHealth(Health);
			Actor->SetAttackDamage(50.0f);
		}

		~FScopedCreature()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		float Health() const
		{
			const UAbilitySystemComponent* AbilitySystem =
				UCataclysmTargeting::AbilitySystemOf(Actor);
			return AbilitySystem
				? AbilitySystem->GetNumericAttribute(
					  UCataclysmVitalAttributeSet::GetHealthAttribute())
				: 0.0f;
		}

		void SetHealthTo(float Value)
		{
			if (UAbilitySystemComponent* AbilitySystem =
					UCataclysmTargeting::AbilitySystemOf(Actor))
			{
				AbilitySystem->SetNumericAttributeBase(
					UCataclysmVitalAttributeSet::GetHealthAttribute(), Value);
			}
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
	};

	template <typename T>
	T* GrantSkill(FScopedCaster& Caster, ECataclysmAbilitySlot Slot,
				  const FString& ParamText, const FString& Name,
				  const FString& TagCell = FString())
	{
		const FGameplayAbilitySpecHandle Handle =
			Caster.AbilitySystem->GiveAbilityInSlot(
				T::StaticClass(), Slot, /*Level=*/100, Caster.Actor);
		if (!Handle.IsValid())
		{
			return nullptr;
		}

		FGameplayAbilitySpec* Spec =
			Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		T* Instance = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			Instance->SkillName = Name;
			Instance->Params = UCataclysmSkillShapes::ParseParams(ParamText);
			Instance->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
		}
		return Instance;
	}

	static bool Activate(FScopedCaster& Caster, UGameplayAbility* Ability)
	{
		return Ability && Caster.AbilitySystem->TryActivateAbility(
			Ability->GetCurrentAbilitySpecHandle(),
			/*bAllowRemoteActivation=*/false);
	}
}

// ==========================================================================
// What a character commands
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCommandFindsBothKindsTest,
	"Cataclysm.Command.WhatACharacterCommandsCoversMinionsAndThralls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCommandFindsBothKindsTest::RunTest(const FString&)
{
	// THE QUESTION THREE STAFF ROWS ASK AND NOTHING COULD ANSWER. Each summon
	// skill kept its own private list of minions, and a subjugated enemy is not a
	// minion at all, so no list could ever have held both.
	using namespace CataclysmCommandTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster Commander(World, FVector::ZeroVector);

	// A CREATURE NOBODY COMMANDS, standing in the middle of everything, so a
	// search that answered "every character nearby" would be caught.
	FScopedCreature Stranger(World, FVector(2 * M, 0, 0));

	TestEqual(TEXT("a character that commands nothing commands nothing"),
		UCataclysmCommand::ThingsCommandedBy(Commander.Actor).Num(), 0);

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Commander.Actor, FVector(3 * M, 0, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/true);
	if (!Imp)
	{
		AddError(TEXT("Could not spawn the minion."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	TestEqual(TEXT("a summoned minion is commanded"),
		UCataclysmCommand::ThingsCommandedBy(Commander.Actor).Num(), 1);

	FScopedCreature Taken(World, FVector(4 * M, 0, 0));
	TestTrue(TEXT("an enemy can be taken"),
		UCataclysmCommand::Subjugate(Commander.Actor, Taken.Actor));

	TestEqual(TEXT("and a thrall is commanded alongside the minion"),
		UCataclysmCommand::ThingsCommandedBy(Commander.Actor).Num(), 2);

	// AND ONLY ONE OF THE TWO COUNTS AGAINST THE POOL. A minion was made and a
	// thrall was taken, and the reserve is stated per thrall.
	TestEqual(TEXT("one of the two is a thrall"),
		UCataclysmCommand::ThrallCountOf(Commander.Actor), 1);

	// THE RANGE IS READ. Vesselstep states "up to 14 meters".
	TestEqual(TEXT("a two metre search finds neither"),
		UCataclysmCommand::ThingsCommandedBy(Commander.Actor, 2 * M).Num(), 0);
	TestEqual(TEXT("and a five metre search finds both"),
		UCataclysmCommand::ThingsCommandedBy(Commander.Actor, 5 * M).Num(), 2);

	// AND NOBODY ELSE'S CREATURES ARE ANYBODY'S. The stranger has been standing
	// nearer than either of them throughout.
	FScopedCaster Other(World, FVector(0, 30 * M, 0));
	TestEqual(TEXT("another character commands none of them"),
		UCataclysmCommand::ThingsCommandedBy(Other.Actor).Num(), 0);

	return true;
}

// ==========================================================================
// Subjugate
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSubjugateTakesTheWeakTest,
	"Cataclysm.Command.SubjugateTakesAnEnemyTheBlowLeftBelowHalfHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSubjugateTakesTheWeakTest::RunTest(const FString&)
{
	// THE STAFF'S SUBJUGATE: "if the blow leaves it below half health you take it
	// permanently." Its `Possess`, `FervourReserve` and `HealthThresholdPercent`
	// were parsed and read by nothing until 2026-09-02.
	using namespace CataclysmCommandTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const TCHAR* const Row =
		TEXT("Range=15; MaxTargets=1; Radius=15; Burn=1; Possess=1; "
			 "FervourReserve=30; HealthThresholdPercent=50");

	// THE CONTROL FIRST, ON A HEALTHY CREATURE, AND ON ITS OWN CASTER. A skill
	// commits its cooldown when it fires, so a second cast of one instance would
	// be refused by that rather than by the threshold under test.
	FScopedCaster First(World, FVector::ZeroVector);
	FScopedCreature Healthy(World, FVector(3 * M, 0, 0));

	UCataclysmSummonSkill* Try = GrantSkill<UCataclysmSummonSkill>(
		First, ECataclysmAbilitySlot::Ultimate, Row, TEXT("Subjugate"));
	if (!Try)
	{
		AddError(TEXT("Could not grant Subjugate."));
		return false;
	}

	TestTrue(TEXT("it activates against a healthy enemy"), Activate(First, Try));
	TestFalse(TEXT("and takes nothing"), Try->bTookIt);
	TestEqual(TEXT("so nothing is commanded"),
		UCataclysmCommand::ThingsCommandedBy(First.Actor).Num(), 0);

	// THE BLOW STILL LANDED, which is what "IF THE BLOW LEAVES IT" says: the
	// damage is not conditional on the taking.
	TestTrue(TEXT("but the blow still landed"), Healthy.Health() < 1000.0f);
	TestTrue(TEXT("and still set it alight"),
		UCataclysmSkillEffects::HasTag(Healthy.Actor,
									   UCataclysmSkillEffects::BurnTag()));

	// AND NOW A CREATURE THE BLOW WILL LEAVE UNDER HALF. Hurt to just above the
	// threshold first, so the skill's own damage is what carries it under and the
	// test is not simply handing it a weak target.
	FScopedCaster Second(World, FVector(0, 30 * M, 0));
	FScopedCreature Wounded(World, FVector(3 * M, 30 * M, 0));
	Wounded.SetHealthTo(520.0f);

	UCataclysmSummonSkill* Take = GrantSkill<UCataclysmSummonSkill>(
		Second, ECataclysmAbilitySlot::Ultimate, Row, TEXT("Subjugate"));
	if (!Take)
	{
		AddError(TEXT("Could not grant the second Subjugate."));
		return false;
	}

	TestTrue(TEXT("it activates against a wounded enemy"),
		Activate(Second, Take));
	TestTrue(TEXT("and takes it"), Take->bTookIt);

	TestEqual(TEXT("so the caster now commands it"),
		UCataclysmCommand::ThingsCommandedBy(Second.Actor).Num(), 1);
	TestEqual(TEXT("and it counts as a thrall rather than a minion"),
		UCataclysmCommand::ThrallCountOf(Second.Actor), 1);

	// IT KEPT ITS BODY, which is what "keeps its own abilities" rests on. A
	// thrall that had been destroyed and replaced with a minion would fail this.
	TestTrue(TEXT("the thrall is still the creature it was"),
		IsValid(Wounded.Actor));
	TestFalse(TEXT("and is no longer an enemy of its new commander"),
		UCataclysmTargeting::IsHostileTo(Wounded.Actor, Second.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSubjugateRefusesABossTest,
	"Cataclysm.Command.ABossCannotBeTaken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSubjugateRefusesABossTest::RunTest(const FString&)
{
	// "BOSSES CANNOT BE TAKEN", which the row states outright.
	using namespace CataclysmCommandTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster Commander(World, FVector::ZeroVector);

	// THE CONTROL IS A CREATURE ONE RARITY STEP BELOW A BOSS, so the refusal
	// below is about being a boss rather than about being rare.
	FScopedCreature NearlyABoss(World, FVector(3 * M, 0, 0));
	NearlyABoss.Actor->SetRarityStep(
		ACataclysmEnemyCharacter::FirstBossRarityStep - 1);
	NearlyABoss.SetHealthTo(100.0f);

	TestTrue(TEXT("a creature one step below a boss can be taken"),
		UCataclysmCommand::Subjugate(Commander.Actor, NearlyABoss.Actor));

	FScopedCreature Boss(World, FVector(5 * M, 0, 0));
	Boss.Actor->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
	Boss.SetHealthTo(100.0f);

	TestFalse(TEXT("and a boss cannot, however hurt it is"),
		UCataclysmCommand::Subjugate(Commander.Actor, Boss.Actor));

	TestEqual(TEXT("so only the first is commanded"),
		UCataclysmCommand::ThrallCountOf(Commander.Actor), 1);
	TestTrue(TEXT("and the boss is still hostile"),
		UCataclysmTargeting::IsHostileTo(Boss.Actor, Commander.Actor));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmThrallCapIsThePoolTest,
	"Cataclysm.Command.TheArmyIsOnlyAsLargeAsTheResourcePool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmThrallCapIsThePoolTest::RunTest(const FString&)
{
	// "HOLDING A THRALL RESERVES 30 FERVOUR, SO YOUR ARMY IS ONLY AS LARGE AS
	// YOUR POOL." The Ritualist's pool is 150 in `game/Data/ClassStats.csv`, so
	// five, and this checks the arithmetic rather than the number: a pool of 90
	// holds three.
	using namespace CataclysmCommandTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A NINETY POINT POOL, so the cap arrives after three rather than after five
	// and the test spawns three creatures instead of six.
	FScopedCaster Commander(World, FVector::ZeroVector, /*Pool=*/90.0f);

	TestTrue(TEXT("an empty pool holder has room for one"),
		UCataclysmCommand::HasRoomForAnotherThrall(Commander.Actor, 30.0f));

	TArray<FScopedCreature*> Held;
	ON_SCOPE_EXIT { for (FScopedCreature* One : Held) { delete One; } };

	for (int32 Index = 0; Index < 3; ++Index)
	{
		FScopedCreature* Creature =
			new FScopedCreature(World, FVector((3 + Index) * M, 0, 0));
		Held.Add(Creature);

		TestTrue(FString::Printf(TEXT("thrall %d fits"), Index + 1),
			UCataclysmCommand::HasRoomForAnotherThrall(Commander.Actor, 30.0f));
		TestTrue(FString::Printf(TEXT("and thrall %d is taken"), Index + 1),
			UCataclysmCommand::Subjugate(Commander.Actor, Creature->Actor));
	}

	TestEqual(TEXT("three thralls are held"),
		UCataclysmCommand::ThrallCountOf(Commander.Actor), 3);

	// NINETY DIVIDED BY THIRTY IS THREE, so there is no room for a fourth.
	TestFalse(TEXT("and a fourth does not fit in a ninety point pool"),
		UCataclysmCommand::HasRoomForAnotherThrall(Commander.Actor, 30.0f));

	// THE CAP IS THE POOL AND NOT A NUMBER IN CODE. Raising the pool raises the
	// army, which is the sentence's whole point: every point of maximum resource
	// a passive tree grants is progress toward another thrall.
	Commander.Set(
		UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
		120.0f);
	TestTrue(TEXT("a larger pool makes room for a fourth"),
		UCataclysmCommand::HasRoomForAnotherThrall(Commander.Actor, 30.0f));

	return true;
}

// ==========================================================================
// Quarry
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmQuarryOrdersTheArmyTest,
	"Cataclysm.Command.AQuarryOrdersCommandedCreaturesOntoIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuarryOrdersTheArmyTest::RunTest(const FString&)
{
	// THE STAFF'S QUARRY: "mark an enemy as your quarry for 12 seconds.
	// Everything you command breaks off and attacks it." The mark landed and
	// lasted, and `Status.Debuff.Quarry` was read by nothing at all, so it ordered
	// nobody anywhere.
	using namespace CataclysmCommandTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster Commander(World, FVector::ZeroVector);

	// THE MARKED ENEMY IS THE FURTHER OF THE TWO, which is what makes this a
	// test of the order rather than of the search. Without the mark the minion
	// attacks whatever is nearest, and that is the other one.
	FScopedCreature Nearer(World, FVector(3 * M, 0, 0));
	FScopedCreature Marked(World, FVector(9 * M, 0, 0));

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Commander.Actor, FVector(2 * M, 0, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/false);
	if (!Imp)
	{
		AddError(TEXT("Could not spawn the minion."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(Imp->GetController());
	if (!Brain)
	{
		AddError(TEXT("The minion has no brain to drive."));
		return false;
	}

	// THE CONTROL: with no mark, the minion goes for whatever is nearest.
	TestTrue(TEXT("with no mark it chooses the nearest enemy"),
		Brain->ChooseTarget() == Nearer.Actor);
	TestNull(TEXT("and its commander has marked nothing"),
		UCataclysmCommand::QuarryOf(Commander.Actor));

	// NOW THE MARK, APPLIED THE WAY THE DEBUFF SHAPE APPLIES IT.
	TestTrue(TEXT("the mark lands"),
		UCataclysmSkillEffects::ApplyNamedEffect(
			Commander.Actor, Marked.Actor, UCataclysmCommand::QuarryTag(),
			/*DurationSeconds=*/12.0f, /*Magnitude=*/0.0f, NAME_None));

	TestTrue(TEXT("the commander's quarry is the marked enemy"),
		UCataclysmCommand::QuarryOf(Commander.Actor) == Marked.Actor);
	TestTrue(TEXT("and the minion is ordered onto it"),
		UCataclysmCommand::OrderedTargetFor(Imp) == Marked.Actor);

	// "BREAKS OFF" IS THE WORD THE ROW USES, so the order has to beat the
	// nearer enemy rather than be weighed against it.
	TestTrue(TEXT("so it breaks off the nearer one and takes the mark"),
		Brain->ChooseTarget() == Marked.Actor);

	// AND NOBODY ELSE'S CREATURES ARE ORDERED. A mark belongs to whoever applied
	// it.
	FScopedCaster Other(World, FVector(0, 30 * M, 0));
	ACataclysmMinion* NotTheirs = ACataclysmMinion::Spawn(
		Other.Actor, FVector(2 * M, 30 * M, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/false);
	if (NotTheirs)
	{
		ON_SCOPE_EXIT { if (IsValid(NotTheirs)) { NotTheirs->Destroy(); } };
		TestNull(TEXT("another character's minion takes no order from it"),
			UCataclysmCommand::OrderedTargetFor(NotTheirs));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmQuarryHurriesTheArmyTest,
	"Cataclysm.Command.AQuarryMakesCommandedCreaturesSwingFasterAtIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuarryHurriesTheArmyTest::RunTest(const FString&)
{
	// "GAINING 30% ATTACK SPEED WHILE THE MARK HOLDS." The figure lives in the
	// Debuffs sheet of `docs/All_Things_Cataclysm.xlsx`, where Shred's ten
	// already does, so moving the balance number needs no build.
	using namespace CataclysmCommandTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster Commander(World, FVector::ZeroVector);
	FScopedCreature Marked(World, FVector(5 * M, 0, 0));
	FScopedCreature Unmarked(World, FVector(7 * M, 0, 0));

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Commander.Actor, FVector(2 * M, 0, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/false);
	if (!Imp)
	{
		AddError(TEXT("Could not spawn the minion."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	// THE CONTROL: no mark anywhere, so nothing is hurried.
	TestEqual(TEXT("with no mark the interval is unchanged"),
		UCataclysmCommand::AttackIntervalScaleFor(Imp, Marked.Actor), 1.0f,
		0.001f);

	TestTrue(TEXT("the mark lands"),
		UCataclysmSkillEffects::ApplyNamedEffect(
			Commander.Actor, Marked.Actor, UCataclysmCommand::QuarryTag(),
			/*DurationSeconds=*/12.0f, /*Magnitude=*/0.0f, NAME_None));

	// A SHORTER INTERVAL, AND NOT SMALLER BY THE SAME PERCENTAGE. "30% attack
	// speed" is 30% more swings in the same time, which is an interval of
	// 1 / 1.30 -- about 0.769 -- rather than 0.70. The two differ by enough that
	// this cannot pass under the wrong arithmetic.
	TestEqual(TEXT("swinging at the mark takes 1 / 1.30 of the interval"),
		UCataclysmCommand::AttackIntervalScaleFor(Imp, Marked.Actor),
		1.0f / 1.30f, 0.001f);

	// AND THE BONUS IS FOR HITTING THE MARK, NOT FOR THE MARK EXISTING.
	TestEqual(TEXT("swinging at something else is unchanged"),
		UCataclysmCommand::AttackIntervalScaleFor(Imp, Unmarked.Actor), 1.0f,
		0.001f);

	return true;
}

// ==========================================================================
// Compel and Vesselstep
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCompelOrdersAStrikeTest,
	"Cataclysm.Command.CompelOrdersEverythingCommandedOntoOneEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCompelOrdersAStrikeTest::RunTest(const FString&)
{
	// THE STAFF'S COMPEL: "everything you command strikes that same enemy at
	// once, wherever it happens to be standing." Its `CommandStrike=1` was
	// parsed and read by nothing until 2026-09-02.
	using namespace CataclysmCommandTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedCaster Commander(World, FVector::ZeroVector);

	// FOURTEEN METRES AWAY, WHICH IS WHERE THE BOLT ACTUALLY GOES. With no
	// player controller there is no cursor to aim at, so
	// `UCataclysmSkillTemplate::AimedPointWithin` sends the shot the full range
	// in the caster's facing direction. A target placed near the caster is not
	// hit at all, which is what the first run of this test found.
	FScopedCreature Struck(World, FVector(14 * M, 0, 0));

	// TWENTY METRES AWAY, WHICH IS THE POINT OF THE TEST. "Wherever it happens
	// to be standing" says reaching the target is not the player's problem, so a
	// minion far out of its own reach still has to be ordered.
	ACataclysmMinion* Distant = ACataclysmMinion::Spawn(
		Commander.Actor, FVector(0, 20 * M, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/false);
	if (!Distant)
	{
		AddError(TEXT("Could not spawn the minion."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Distant)) { Distant->Destroy(); } };

	UCataclysmProjectileSkill* Compel = GrantSkill<UCataclysmProjectileSkill>(
		Commander, ECataclysmAbilitySlot::Heavy,
		TEXT("Range=14; Radius=5; Pierce=0; Speed=0; Burn=1; CommandStrike=1"),
		TEXT("Compel"));
	if (!Compel)
	{
		AddError(TEXT("Could not grant Compel."));
		return false;
	}

	const int32 Before = Distant->AttacksMade;

	TestTrue(TEXT("it activates"), Activate(Commander, Compel));

	TestEqual(TEXT("one commanded creature was ordered"),
		Compel->CommandedStrikes, 1);
	TestEqual(TEXT("and it swung once more than before"),
		Distant->AttacksMade, Before + 1);

	// AND IT SWUNG AT THE ENEMY THE BOLT HIT, twenty-odd metres from where it
	// was standing.
	TestTrue(TEXT("the enemy the bolt hit lost health to both"),
		Struck.Health() < 1000.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVesselstepTradesPlacesTest,
	"Cataclysm.Command.AVesselstepTradesPlacesWithACommandedCreature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmVesselstepTradesPlacesTest::RunTest(const FString&)
{
	// THE STAFF'S VESSELSTEP: "trade places with a creature you command up to 14
	// meters away." Until 2026-09-02 `Mode=Swap` had no branch and silently ran
	// the leap code, so the caster moved to the aimed point and the creature
	// stayed where it was. Issue #1139.
	using namespace CataclysmCommandTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE CONTROL FIRST, AND ON ITS OWN CASTER: commanding nothing, the skill is
	// refused outright rather than blinking to the cursor.
	FScopedCaster Alone(World, FVector::ZeroVector);

	UCataclysmMovementSkill* NoPartner = GrantSkill<UCataclysmMovementSkill>(
		Alone, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Swap; Range=14; Radius=3; Burn=1"), TEXT("Vesselstep"));
	if (!NoPartner)
	{
		AddError(TEXT("Could not grant Vesselstep."));
		return false;
	}

	TestFalse(TEXT("commanding nothing, the trade is refused"),
		Activate(Alone, NoPartner));
	const float StayedAt =
		static_cast<float>(Alone.Actor->GetActorLocation().X);
	TestEqual(TEXT("and the caster did not move"), StayedAt, 0.0f, 1.0f);

	// AND NOW WITH A CREATURE TO TRADE WITH, on its own caster so no cooldown
	// from the refusal above can be what decides the next activation.
	FScopedCaster Commander(World, FVector(0, 30 * M, 0));

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Commander.Actor, FVector(8 * M, 30 * M, 0), /*Lifetime=*/20.0f,
		/*bBurns=*/false);
	if (!Imp)
	{
		AddError(TEXT("Could not spawn the minion."));
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	UCataclysmMovementSkill* Vesselstep = GrantSkill<UCataclysmMovementSkill>(
		Commander, ECataclysmAbilitySlot::Movement,
		TEXT("Mode=Swap; Range=14; Radius=3; Burn=1"), TEXT("Vesselstep"));
	if (!Vesselstep)
	{
		AddError(TEXT("Could not grant the second Vesselstep."));
		return false;
	}

	const float CasterWas =
		static_cast<float>(Commander.Actor->GetActorLocation().X);
	const float ImpWas = static_cast<float>(Imp->GetActorLocation().X);

	TestTrue(TEXT("with a creature to trade with, it fires"),
		Activate(Commander, Vesselstep));

	TestTrue(TEXT("it traded with the minion"),
		Vesselstep->SwappedWith == Imp);

	// BOTH MOVED, AND EACH TO WHERE THE OTHER WAS. A skill that moved only the
	// caster would be a blink, which is what this row silently was.
	TestEqual(TEXT("the caster is where the creature was"),
		static_cast<float>(Commander.Actor->GetActorLocation().X), ImpWas, 1.0f);
	TestEqual(TEXT("and the creature is where the caster was"),
		static_cast<float>(Imp->GetActorLocation().X), CasterWas, 1.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
