// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSecondSelf.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A Second Self, the Ritualist's `Ritualist_capstone_200` option 1. Issue #1515.
 *
 * "The minion you have held longest becomes your equal: it has your Maximum
 * Health, your Spell Damage and your Area of Effect, and reserves twice the
 * Fervour it would. When it is gone, the next longest-held takes its place."
 *
 * THE STAT IS GIVEN BY HAND, as the row will give it: the option has no row yet.
 * World time is moved by hand between two takings, so "held longest" is a
 * difference the test made rather than one it hoped for.
 */
namespace CataclysmSecondSelfTest
{
	constexpr float M = 100.0f;

	/** The commander: a bare actor with a stat line and the sets it lends. */
	struct FScopedCommander
	{
		FScopedCommander(UWorld* World, const FVector& Where)
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
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 5000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 5000.0f);
			Set(UCataclysmCombatAttributeSet::GetSpellDamageAttribute(), 40.0f);
			Set(UCataclysmCombatAttributeSet::GetAreaOfEffectAttribute(), 150.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
		}

		~FScopedCommander()
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

		/** Hold A Second Self, as its row will. */
		void HoldSecondSelf()
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = 1.0f;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(UCataclysmSecondSelf::Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A creature on the monsters' side, with a maximum of 1000, to be taken. */
	ACataclysmEnemyCharacter* SpawnCreature(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Creature =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		check(Creature);
		Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Creature->SetHealth(1000.0f);
		return Creature;
	}

	float Read(const AActor* Actor, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Actor);
		return Its ? Its->GetNumericAttribute(Attribute) : -1.0f;
	}

	float MaxHealthOf(const AActor* Actor)
	{
		return Read(Actor, UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	}

	float HealthOf(const AActor* Actor)
	{
		return Read(Actor, UCataclysmVitalAttributeSet::GetHealthAttribute());
	}

	/** An imp for `Summoner`, failing the test when the type table cannot supply one. */
	ACataclysmMinion* SpawnMinion(FAutomationTestBase& Test, AActor* Summoner,
								  const FVector& Where, const TCHAR* Type = TEXT("Imp"))
	{
		ACataclysmMinion* Minion = ACataclysmMinion::Spawn(
			Summoner, Where, /*Lifetime=*/60.0f, /*bBurns=*/false, Type);
		if (!Test.TestNotNull(TEXT("a minion"), Minion))
		{
			return nullptr;
		}
		if (Minion->TypeName != FString(Type))
		{
			Test.AddError(TEXT("DT_MinionTypes could not supply the row. Run "
							   "tools/generate_datatable_assets.py"));
			return nullptr;
		}
		return Minion;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSecondSelfOlderThrallTest,
	"Cataclysm.SecondSelf.TheOlderThrallIsChosenAndGivenYourHealthAndSpellDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two thralls, the farther one taken a second earlier: the farther one is
 * chosen. It takes the commander's 5000 maximum keeping its 40% share, the
 * commander's 40 spell damage, and follows the maximum when it moves. The
 * nearer one keeps its own.
 */
bool FCataclysmSecondSelfOlderThrallTest::RunTest(const FString&)
{
	using namespace CataclysmSecondSelfTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCommander Commander(World, FVector::ZeroVector);
	Commander.HoldSecondSelf();
	ACataclysmEnemyCharacter* Older = SpawnCreature(World, FVector(6.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Newer = SpawnCreature(World, FVector(3.0f * M, 0.0f, 0.0f));

	TestTrue(TEXT("the farther creature is taken first"),
			 UCataclysmCommand::Subjugate(Commander.Actor, Older));
	World->TimeSeconds += 1.0;
	TestTrue(TEXT("and the nearer one a second later"),
			 UCataclysmCommand::Subjugate(Commander.Actor, Newer));

	UAbilitySystemComponent* OlderSystem = UCataclysmTargeting::AbilitySystemOf(Older);
	if (!TestNotNull(TEXT("the older thrall's ability system"), OlderSystem))
	{
		return false;
	}
	OlderSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 400.0f);

	TestTrue(TEXT("the one held longest is chosen, not the nearer"),
			 UCataclysmSecondSelf::Step(Commander.Actor) == Older);
	TestEqual(TEXT("it has the commander's maximum health"), MaxHealthOf(Older), 5000.0f, 0.01f);
	TestEqual(TEXT("and keeps its 40% share of it, so the rise is no heal"),
			  HealthOf(Older), 2000.0f, 0.01f);
	TestEqual(TEXT("and the commander's spell damage"),
			  Read(Older, UCataclysmCombatAttributeSet::GetSpellDamageAttribute()), 40.0f, 0.01f);
	TestEqual(TEXT("the nearer thrall keeps its own maximum"), MaxHealthOf(Newer), 1000.0f, 0.01f);
	TestTrue(TEXT("\"Second Self\" is said over the chosen one"),
			 UCataclysmCombatOverlay::StatusLineFor(Older).Contains(TEXT("Second Self")));
	TestTrue(TEXT("and not over the other"),
			 UCataclysmCombatOverlay::SecondSelfTextFor(Newer).IsEmpty());

	Commander.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 6000.0f);
	UCataclysmSecondSelf::Step(Commander.Actor);
	TestEqual(TEXT("when the commander's maximum moves, the chosen one's follows"),
			  MaxHealthOf(Older), 6000.0f, 0.01f);
	TestEqual(TEXT("still at 40%"), HealthOf(Older), 2400.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSecondSelfNextTakesItsPlaceTest,
	"Cataclysm.SecondSelf.WhenItIsGoneTheNextLongestHeldTakesItsPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSecondSelfNextTakesItsPlaceTest::RunTest(const FString&)
{
	using namespace CataclysmSecondSelfTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCommander Commander(World, FVector::ZeroVector);
	Commander.HoldSecondSelf();
	ACataclysmEnemyCharacter* First = SpawnCreature(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Second = SpawnCreature(World, FVector(6.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Third = SpawnCreature(World, FVector(9.0f * M, 0.0f, 0.0f));
	UCataclysmCommand::Subjugate(Commander.Actor, First);
	World->TimeSeconds += 1.0;
	UCataclysmCommand::Subjugate(Commander.Actor, Second);
	World->TimeSeconds += 1.0;
	UCataclysmCommand::Subjugate(Commander.Actor, Third);

	TestTrue(TEXT("the first taken is chosen"), UCataclysmSecondSelf::Step(Commander.Actor) == First);
	First->Destroy();
	TestTrue(TEXT("when it is gone, the next longest-held is"),
			 UCataclysmSecondSelf::Step(Commander.Actor) == Second);
	TestEqual(TEXT("and it has the commander's maximum"), MaxHealthOf(Second), 5000.0f, 0.01f);
	TestEqual(TEXT("the third is untouched"), MaxHealthOf(Third), 1000.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSecondSelfMachineTest,
	"Cataclysm.SecondSelf.ADeployableIsNeverChosenHoweverLongItIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A Bolt Turret deployed before an imp is summoned: the imp is chosen. */
bool FCataclysmSecondSelfMachineTest::RunTest(const FString&)
{
	using namespace CataclysmSecondSelfTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCommander Commander(World, FVector::ZeroVector);
	Commander.HoldSecondSelf();
	ACataclysmMinion* Turret =
		SpawnMinion(*this, Commander.Actor, FVector(2.0f * M, 0.0f, 0.0f), TEXT("BoltTurret"));
	World->TimeSeconds += 5.0;
	ACataclysmMinion* Imp = SpawnMinion(*this, Commander.Actor, FVector(9.0f * M, 0.0f, 0.0f));
	if (!Turret || !Imp)
	{
		return false;
	}

	TestTrue(TEXT("the turret's row says it is a machine"), Turret->bIsMachine);
	TestFalse(TEXT("the imp's says it is not"), Imp->bIsMachine);
	TestTrue(TEXT("the turret was held five seconds longer"),
			 Turret->CommandedSinceSeconds < Imp->CommandedSinceSeconds);
	TestTrue(TEXT("and still the imp is chosen"), UCataclysmSecondSelf::Step(Commander.Actor) == Imp);
	TestFalse(TEXT("the turret is not a Second Self"), UCataclysmSecondSelf::IsSecondSelf(Turret));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSecondSelfAreaTest,
	"Cataclysm.SecondSelf.TheChosenImpExplodesOverYourAreaOfEffectAndAnotherDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two imps with a 3 metre explosion, the commander at 150% area of effect, and
 * an enemy 4 metres from each: the chosen imp's explosion reaches 4.5 metres
 * and strikes its enemy, and the other's reaches 3 and does not. The imps have
 * no combat set, so spell damage reaches neither.
 */
bool FCataclysmSecondSelfAreaTest::RunTest(const FString&)
{
	using namespace CataclysmSecondSelfTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCommander Commander(World, FVector::ZeroVector);
	Commander.HoldSecondSelf();
	const FVector ChosenAt(-15.0f * M, 0.0f, 0.0f);
	const FVector OtherAt(-15.0f * M, 20.0f * M, 0.0f);
	ACataclysmMinion* Chosen = SpawnMinion(*this, Commander.Actor, ChosenAt);
	World->TimeSeconds += 1.0;
	ACataclysmMinion* Other = SpawnMinion(*this, Commander.Actor, OtherAt);
	// PLAIN ACTORS ON NO SIDE, which every side counts as hostile.
	FScopedCommander NearChosen(World, ChosenAt + FVector(4.0f * M, 0.0f, 0.0f));
	FScopedCommander NearOther(World, OtherAt + FVector(4.0f * M, 0.0f, 0.0f));
	if (!Chosen || !Other)
	{
		return false;
	}
	Chosen->RecordExplosionRadius(3.0f * M);
	Other->RecordExplosionRadius(3.0f * M);

	TestTrue(TEXT("the older imp is chosen"), UCataclysmSecondSelf::Step(Commander.Actor) == Chosen);
	TestEqual(TEXT("its areas are the commander's 150%"),
			  UCataclysmSecondSelf::AreaMultiplierFor(Chosen), 1.5f, 0.001f);
	TestEqual(TEXT("the other imp's are its own"),
			  UCataclysmSecondSelf::AreaMultiplierFor(Other), 1.0f, 0.001f);
	TestFalse(TEXT("an imp has no combat set, so the spell damage clause reaches nothing"),
			  Chosen->GetAbilitySystemComponent()->HasAttributeSetForAttribute(
				  UCataclysmCombatAttributeSet::GetSpellDamageAttribute()));

	const float ChosenEnemyBefore = HealthOf(NearChosen.Actor);
	const float OtherEnemyBefore = HealthOf(NearOther.Actor);
	Chosen->Explode();
	Other->Explode();
	TestTrue(TEXT("the chosen imp's explosion reached the enemy 4 metres away"),
			 HealthOf(NearChosen.Actor) < ChosenEnemyBefore);
	TestEqual(TEXT("the other imp's did not"), HealthOf(NearOther.Actor), OtherEnemyBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSecondSelfReserveTest,
	"Cataclysm.SecondSelf.AChosenThrallCountsTwiceSoAHundredAndFiftyFervourHoldsFourNotFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSecondSelfReserveTest::RunTest(const FString&)
{
	using namespace CataclysmSecondSelfTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const auto ThrallsTaken = [World](FScopedCommander& Commander, float Along)
	{
		int32 Taken = 0;
		for (int32 Index = 0; Index < 7; ++Index)
		{
			if (!UCataclysmCommand::HasRoomForAnotherThrall(Commander.Actor, 30.0f))
			{
				break;
			}
			ACataclysmEnemyCharacter* Creature =
				SpawnCreature(World, FVector((3 + Index) * M, Along, 0.0f));
			Taken += UCataclysmCommand::Subjugate(Commander.Actor, Creature) ? 1 : 0;
		}
		return Taken;
	};

	FScopedCommander Plain(World, FVector(0.0f, 30.0f * M, 0.0f));
	TestEqual(TEXT("without the option, 150 Fervour at 30 each holds five"),
			  ThrallsTaken(Plain, 30.0f * M), 5);

	FScopedCommander Holder(World, FVector::ZeroVector);
	Holder.HoldSecondSelf();
	TestEqual(TEXT("with it, the chosen thrall counts twice, so four"),
			  ThrallsTaken(Holder, 0.0f), 4);
	TestEqual(TEXT("and four are held"), UCataclysmCommand::ThrallCountOf(Holder.Actor), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSecondSelfNeedsTheOptionTest,
	"Cataclysm.SecondSelf.WithoutTheOptionNothingIsChosenOrChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSecondSelfNeedsTheOptionTest::RunTest(const FString&)
{
	using namespace CataclysmSecondSelfTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCommander Commander(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Thrall = SpawnCreature(World, FVector(3.0f * M, 0.0f, 0.0f));
	TestTrue(TEXT("a creature is taken"), UCataclysmCommand::Subjugate(Commander.Actor, Thrall));

	TestNull(TEXT("a commander without the option chooses nothing"),
			 UCataclysmSecondSelf::Step(Commander.Actor));
	TestEqual(TEXT("so the thrall keeps its own maximum"), MaxHealthOf(Thrall), 1000.0f, 0.01f);
	TestFalse(TEXT("and is not a Second Self"), UCataclysmSecondSelf::IsSecondSelf(Thrall));
	TestEqual(TEXT("and claims no second share"),
			  UCataclysmSecondSelf::ExtraThrallShares(Commander.Actor), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
