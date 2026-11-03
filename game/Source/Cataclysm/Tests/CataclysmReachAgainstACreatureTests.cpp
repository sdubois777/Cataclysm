// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmDroppedItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Issue #2706: a creature as wide as a Brute is still within a Brute's reach when the two touch.
 *
 * WHAT WAS WRONG, FOUND BY READING. Every melee reach is designed against the PLAYER's body: the Brute's, the
 * Hellhound's and the Abyssal Warden's 90 cm is their own 48 cm radius plus the player's 42, which is exactly where the
 * two touch. The brain compared that bare figure with the distance between the two centres, so against another 48 cm
 * body -- touching at 96 -- a thrall, a maddened creature, or any creature fighting another pressed against it and never
 * swung. The swing's own landing test (issue #783) and Famished Beasts' "fight rather than eat" made the same
 * comparison, and Famished Beasts also left out the 2 cm contact tolerance.
 *
 * WHAT THESE GUARD, ruled 2026-11-03 by the coordinating session: one reach, `ACataclysmEnemyController::
 * ReachAgainstCm`, the designed reach plus how much wider the target is than the player, never shorter; and one test,
 * `IsInReach`, at the chase, the swing order, the swing's landing and Famished Beasts. Against the player nothing
 * moves, and the tests that say so are the controls.
 *
 * NO WORLD IS TICKED, as everywhere in the brain's tests. The clock is moved by hand, a swing that waits is landed by
 * calling `LandPendingBasicAttack`, and nothing walks: a chase is read from what `Think` answers. Every distance is
 * on the floor plane, centre to centre; every body stands at the same height.
 *
 * WHERE EACH FIXTURE'S STARTING STATE COMES FROM. A plain creature's body is `EnemyCapsuleRadius`, 48 cm, at the
 * Common rarity's scale of 1, and each test states it before using it. Its health is written to 100,000 by `SetHealth`
 * and its evasion, block chance and energy shield to nought after that, so no blow is missed or soaked by chance;
 * critical strikes are silenced by `MakeWorldThatHasBegunPlay`. A creature's abilities start ready and are spent by
 * `SpendReachTestAbilities` before any swing is asked for, the way `Cataclysm.AI.` spends them. The player is possessed
 * by the game's own controller and given 10,000 health, the rig `Cataclysm.BasicReach.` uses.
 */
namespace CataclysmReachAgainstACreatureTest
{
	/** Centimetres in a metre. */
	constexpr float ReachTestMetre = 100.0f;

	/** What every creature under test hits for, so a blow is unmistakable. */
	constexpr float ReachTestSwingerDamage = 35.0f;

	/** A creature of the given class where it is put, overlapping or not, hitting for `ReachTestSwingerDamage`. */
	template <typename TCreature>
	TCreature* SpawnReachTestCreature(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TCreature* Creature =
			World->SpawnActor<TCreature>(TCreature::StaticClass(), Where, FRotator::ZeroRotator, Params);
		if (Creature)
		{
			Creature->SetAttackDamage(ReachTestSwingerDamage);
		}
		return Creature;
	}

	/**
	 * A plain creature of the given side where it is put: 48 cm across, as every enemy body is, deals nothing, and
	 * nothing can make a blow at it miss or be soaked.
	 */
	static ACataclysmEnemyCharacter* SpawnReachTestBody(UWorld* World, const FVector& Where, ECataclysmTeam Team)
	{
		ACataclysmEnemyCharacter* Body = SpawnReachTestCreature<ACataclysmEnemyCharacter>(World, Where);
		if (!Body)
		{
			return nullptr;
		}
		Body->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
		Body->SetAttackDamage(0.0f);
		Body->SetHealth(100000.0f);
		if (UAbilitySystemComponent* System = Body->GetAbilitySystemComponent())
		{
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
		}
		return Body;
	}

	/** Health now, or -1 for something with no ability system. */
	static float ReachTestHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Who);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** The radius of a character's body as it stands, or -1 for an actor with no capsule. */
	static float ReachTestRadiusOf(const AActor* Who)
	{
		const UCapsuleComponent* Capsule = Who ? Cast<UCapsuleComponent>(Who->GetRootComponent()) : nullptr;
		return Capsule ? Capsule->GetScaledCapsuleRadius() : -1.0f;
	}

	/** Put an actor at a distance along +X from `From`, at its own height. */
	static void PlaceReachTestActor(AActor* Who, const AActor* From, float DistanceCm)
	{
		const FVector Origin = From->GetActorLocation();
		Who->SetActorLocation(FVector(Origin.X + DistanceCm, Origin.Y, Who->GetActorLocation().Z));
	}

	/** The floor-plane distance between two actors. */
	static float ReachTestApart(const AActor* One, const AActor* Other)
	{
		return static_cast<float>(FVector::Dist2D(One->GetActorLocation(), Other->GetActorLocation()));
	}

	/** The creature's brain, or null. */
	static ACataclysmEnemyController* ReachTestBrainOf(const APawn* Creature)
	{
		return Creature ? Cast<ACataclysmEnemyController>(Creature->GetController()) : nullptr;
	}

	/**
	 * Run a creature's abilities to completion so only its ordinary swing is left, and then wait out its attack
	 * interval. The shape of `SpendAbilities` in `CataclysmEnemyBehaviourTests.cpp`, for the reasons recorded there:
	 * it asks `ChooseAbility` before it acts, so it never performs the ordinary swing itself; and it puts every actor
	 * back where it stood, because the Brute's stomp shoves what it catches.
	 *
	 * @return how many abilities it used
	 */
	static int32 SpendReachTestAbilities(UWorld* World, ACataclysmEnemyController* Brain, float DistanceCm)
	{
		const ACataclysmCharacterBase* Driven = Brain ? Cast<ACataclysmCharacterBase>(Brain->GetPawn()) : nullptr;
		if (!Driven)
		{
			return 0;
		}

		TMap<AActor*, FVector> Placed;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Placed.Add(*It, It->GetActorLocation());
		}

		int32 Used = 0;
		for (int32 Pass = 0; Pass < 8; ++Pass)
		{
			const int32 Chosen = Brain->ChooseAbility(DistanceCm);
			if (Chosen == INDEX_NONE)
			{
				break;
			}
			const TArray<FCataclysmEnemyAbility> Abilities = Driven->EnemyAbilities();
			const float WindUp = Abilities.IsValidIndex(Chosen) ? Abilities[Chosen].WindUpSeconds : 1.4f;

			Brain->Think();                           // starts the wind-up
			World->TimeSeconds += WindUp + 0.05;      // just past it
			Brain->Think();                           // lands it
			++Used;
		}

		// LONGER THAN THE INTERVAL, so the comparison is not asked to decide a tie.
		World->TimeSeconds += Driven->SecondsBetweenAttacks() + 0.05;

		for (const TPair<AActor*, FVector>& Pair : Placed)
		{
			if (IsValid(Pair.Key) && !Pair.Key->GetActorLocation().Equals(Pair.Value, 0.01))
			{
				Pair.Key->SetActorLocation(Pair.Value);
			}
		}
		return Used;
	}

	/** Land a swing the brain just ordered: the clock moved just past the moment it is due. Nothing if none waits. */
	static void LandReachTestSwing(UWorld* World, ACataclysmEnemyCharacter* Swinger)
	{
		if (!World || !Swinger || !Swinger->HasPendingBasicAttack())
		{
			return;
		}
		World->TimeSeconds += FMath::Max(0.0, Swinger->PendingBasicAttackLandsAtSeconds - World->GetTimeSeconds())
			+ 0.01;
		Swinger->LandPendingBasicAttack();
	}

	/** A player possessed by the game's own controller, with health to lose. The rig `Cataclysm.BasicReach.` uses. */
	struct FReachTestPlayer
	{
		FReachTestPlayer(UWorld* World, const FVector& Where)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<ACataclysmPlayerController>();
			Player = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
			if (!State || !Controller || !Player)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Player);
			if (UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent())
			{
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			}
		}

		ACataclysmPlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Player = nullptr;
	};

	/**
	 * The whole of one creature taken as a thrall and pressed against a creature exactly as wide as itself: it swings
	 * rather than chasing, at that creature, once, and the blow lands. Returns false when the set-up failed, having
	 * said why.
	 */
	template <typename TCreature>
	bool CheckAThrallSwingsAtABodyAsWideAsItself(FAutomationTestBase& Test, const TCHAR* Name)
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		// THE COMMANDER TEN METRES OFF, ON THE PLAYERS' SIDE, and the creature it fights on the monsters' side in
		// contact with the thrall. Nothing about where the commander stands enters: a thrall with a target fights it.
		ACataclysmEnemyCharacter* Commander =
			SpawnReachTestBody(World, FVector(0.0f, 10.0f * ReachTestMetre, 0.0f), ECataclysmTeam::Players);
		TCreature* Thrall = SpawnReachTestCreature<TCreature>(World, FVector::ZeroVector);
		ACataclysmEnemyCharacter* Fought = SpawnReachTestBody(World, FVector(5.0f * ReachTestMetre, 0.0f, 0.0f),
															  ECataclysmTeam::Monsters);
		ACataclysmEnemyController* Brain = ReachTestBrainOf(Thrall);
		if (!Test.TestNotNull(TEXT("set-up: a commander"), Commander)
			|| !Test.TestNotNull(*FString::Printf(TEXT("set-up: a %s"), Name), Thrall)
			|| !Test.TestNotNull(TEXT("set-up: a creature to fight"), Fought)
			|| !Test.TestNotNull(TEXT("set-up: with a brain"), Brain)
			|| !Test.TestTrue(TEXT("set-up: the commander takes it"), UCataclysmCommand::Subjugate(Commander, Thrall)))
		{
			return false;
		}

		// STATED RATHER THAN ASSUMED: the two are as wide as each other, and they touch.
		const float Contact = ReachTestRadiusOf(Thrall) + ReachTestRadiusOf(Fought);
		PlaceReachTestActor(Fought, Thrall, Contact);
		if (!Test.TestEqual(TEXT("set-up: the creature it fights is as wide as it is"),
				ReachTestRadiusOf(Fought), ReachTestRadiusOf(Thrall), 0.001f)
			|| !Test.TestEqual(TEXT("set-up: they touch, 96 cm apart"), ReachTestApart(Thrall, Fought), 96.0f, 0.01f)
			|| !Test.TestEqual(TEXT("set-up: its designed reach is the 90 cm it touches the player at"),
				Thrall->AttackReachCm(), 90.0f, 0.001f))
		{
			return false;
		}

		SpendReachTestAbilities(World, Brain, Contact);

		const float Before = ReachTestHealthOf(Fought);
		const int32 OrderedBefore = Brain->AttacksOrdered;
		Test.TestEqual(*FString::Printf(TEXT("a thrall %s pressed against a creature as wide as itself swings rather "
											 "than chasing"), Name),
			static_cast<int32>(Brain->Think()), static_cast<int32>(ECataclysmBrainAction::Attacking));
		Test.TestTrue(TEXT("and the creature it touches is what it is fighting"), Brain->CurrentTarget.Get() == Fought);
		Test.TestEqual(TEXT("and it ordered one swing"), Brain->AttacksOrdered - OrderedBefore, 1);

		// THE WORDS CARRY NO FIGURE, so a failure reads the same in every run: the stomp spent above has already taken
		// an amount of health that depends on the creature's own numbers.
		LandReachTestSwing(World, Thrall);
		Test.TestTrue(TEXT("and the blow lands: the creature it touches loses health"),
			ReachTestHealthOf(Fought) < Before);
		return true;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// The figure
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReachAgainstABodyFigureTest,
	"Cataclysm.ReachAgainstACreature.TheReachIsTheDesignedOnePlusHowMuchWiderTheTargetIsThanThePlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReachAgainstABodyFigureTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReachAgainstACreatureTest;
	using Controller_t = ACataclysmEnemyController;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// EVERYTHING FAR APART. Nothing here is asked how far it stands from anything: the figure is a property of the
	// two bodies, and nothing thinks.
	const FReachTestPlayer Rig(World, FVector(0.0f, 0.0f, 0.0f));
	ACataclysmBruteCharacter* Brute =
		SpawnReachTestCreature<ACataclysmBruteCharacter>(World, FVector(0.0f, 20.0f * ReachTestMetre, 0.0f));
	ACataclysmHellhoundCharacter* Hellhound =
		SpawnReachTestCreature<ACataclysmHellhoundCharacter>(World, FVector(0.0f, 40.0f * ReachTestMetre, 0.0f));
	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnReachTestCreature<ACataclysmAbyssalWardenCharacter>(World, FVector(0.0f, 60.0f * ReachTestMetre, 0.0f));
	ACataclysmEnemyCharacter* Wide =
		SpawnReachTestBody(World, FVector(0.0f, 80.0f * ReachTestMetre, 0.0f), ECataclysmTeam::Players);
	ACataclysmImpCharacter* Narrow =
		SpawnReachTestCreature<ACataclysmImpCharacter>(World, FVector(0.0f, 100.0f * ReachTestMetre, 0.0f));
	AActor* NoBody = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("set-up: a possessed player"), Rig.Player) || !TestNotNull(TEXT("set-up: a Brute"), Brute)
		|| !TestNotNull(TEXT("set-up: a Hellhound"), Hellhound) || !TestNotNull(TEXT("set-up: a Warden"), Warden)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Wide) || !TestNotNull(TEXT("set-up: an Imp"), Narrow)
		|| !TestNotNull(TEXT("set-up: an actor with no body"), NoBody))
	{
		return false;
	}

	// THE BODIES, STATED: the player's is the one the function reads, and it is the capsule the player stands in.
	TestEqual(TEXT("set-up: the player's body is the radius the reach is measured against"),
		ReachTestRadiusOf(Rig.Player), ACataclysmPlayerCharacter::CapsuleRadiusCm(), 0.001f);
	TestEqual(TEXT("set-up: and it is 42 cm"), ACataclysmPlayerCharacter::CapsuleRadiusCm(), 42.0f, 0.001f);
	TestEqual(TEXT("set-up: a plain creature is 48 cm across"), ReachTestRadiusOf(Wide), 48.0f, 0.001f);
	TestEqual(TEXT("set-up: an Imp is 30 cm across"), ReachTestRadiusOf(Narrow), 30.0f, 0.001f);
	TestEqual(TEXT("set-up: an actor with no root has no body"), ReachTestRadiusOf(NoBody), -1.0f);

	// CONTROL: AGAINST THE PLAYER, EXACTLY THE DESIGNED REACH, for each of the three creatures the issue names.
	TestEqual(TEXT("CONTROL: against the player the Brute reaches its designed 90 cm"),
		Controller_t::ReachAgainstCm(Brute, Rig.Player), ACataclysmBruteCharacter::DesignedMeleeReachCm, 0.001f);
	TestEqual(TEXT("CONTROL: and so does the Hellhound"),
		Controller_t::ReachAgainstCm(Hellhound, Rig.Player), ACataclysmHellhoundCharacter::DesignedMeleeReachCm,
		0.001f);
	TestEqual(TEXT("CONTROL: and so does the Abyssal Warden"),
		Controller_t::ReachAgainstCm(Warden, Rig.Player), ACataclysmAbyssalWardenCharacter::DesignedMeleeReachCm,
		0.001f);

	// AGAINST A BODY 48 CM ACROSS, 6 CM FURTHER: 90 + (48 - 42), where the two touch.
	TestEqual(TEXT("against a body 48 cm across the Brute reaches 96 cm"),
		Controller_t::ReachAgainstCm(Brute, Wide), 96.0f, 0.001f);
	TestEqual(TEXT("and so does the Hellhound"), Controller_t::ReachAgainstCm(Hellhound, Wide), 96.0f, 0.001f);
	TestEqual(TEXT("and so does the Abyssal Warden"), Controller_t::ReachAgainstCm(Warden, Wide), 96.0f, 0.001f);

	// AGAINST A NARROWER BODY, NEVER SHORTER: an Imp is 12 cm narrower than the player, and the reach stays 90.
	TestEqual(TEXT("against an Imp, narrower than the player, the Brute's reach is not shortened"),
		Controller_t::ReachAgainstCm(Brute, Narrow), ACataclysmBruteCharacter::DesignedMeleeReachCm, 0.001f);

	// WITH NO BODY TO READ, THE DESIGNED REACH.
	TestEqual(TEXT("against an actor with no body, the designed reach"),
		Controller_t::ReachAgainstCm(Brute, NoBody), ACataclysmBruteCharacter::DesignedMeleeReachCm, 0.001f);
	TestEqual(TEXT("and against nothing at all"),
		Controller_t::ReachAgainstCm(Brute, nullptr), ACataclysmBruteCharacter::DesignedMeleeReachCm, 0.001f);
	TestEqual(TEXT("no attacker reaches nothing"), Controller_t::ReachAgainstCm(nullptr, Wide), 0.0f);
	TestFalse(TEXT("and nothing is within the reach of no attacker"), Controller_t::IsInReach(nullptr, Wide));
	TestFalse(TEXT("nor is nothing within a Brute's"), Controller_t::IsInReach(Brute, nullptr));

	// A SUMMONED MINION'S REACH IS LEFT AS IT IS: a range set against enemies, whose own "within its reach" the brain
	// must keep agreeing with. A real Imp of the minion table, summoned by the plain creature on the players' side.
	ACataclysmMinion* Minion = ACataclysmMinion::Spawn(Wide, FVector(0.0f, 120.0f * ReachTestMetre, 0.0f),
		/*Lifetime=*/20.0f, /*bBurns=*/false, TEXT("Imp"));
	if (TestNotNull(TEXT("set-up: a summoned minion"), Minion))
	{
		ON_SCOPE_EXIT { if (IsValid(Minion)) { Minion->Destroy(); } };
		TestEqual(TEXT("a summoned minion's reach against a body 48 cm across is its own reach, unchanged"),
			Controller_t::ReachAgainstCm(Minion, Brute), Minion->AttackReachCm(), 0.001f);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// The swing, ordered and landed, by a thrall
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReachThrallBruteTest,
	"Cataclysm.ReachAgainstACreature.AThrallBrutePressedAgainstACreatureAsWideAsItselfSwingsAndItsBlowLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReachThrallBruteTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReachAgainstACreatureTest;
	return CheckAThrallSwingsAtABodyAsWideAsItself<ACataclysmBruteCharacter>(*this, TEXT("Brute"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReachThrallHellhoundTest,
	"Cataclysm.ReachAgainstACreature.AThrallHellhoundPressedAgainstACreatureAsWideAsItselfBites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReachThrallHellhoundTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReachAgainstACreatureTest;

	// ITS BITE IS LANDED AT ONCE (`MaulStrikeSeconds` is unmeasured, issue #783), so for it only the brain's order is
	// in question; Hellrush starts at 622.5 cm, so at contact it has no ability to spend.
	return CheckAThrallSwingsAtABodyAsWideAsItself<ACataclysmHellhoundCharacter>(*this, TEXT("Hellhound"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReachThrallWardenTest,
	"Cataclysm.ReachAgainstACreature.AThrallWardenPressedAgainstACreatureAsWideAsItselfSwingsAndItsBlowLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReachThrallWardenTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReachAgainstACreatureTest;

	// MOLTEN ROAR REACHES FROM ITS FEET, so it is spent first; Stampede starts at 232.4 cm.
	return CheckAThrallSwingsAtABodyAsWideAsItself<ACataclysmAbyssalWardenCharacter>(*this, TEXT("Abyssal Warden"));
}

// ---------------------------------------------------------------------------------------------------------------
// Where the reach ends: against the player, where it always did; against a wider body, 6 cm further
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReachAgainstThePlayerTest,
	"Cataclysm.ReachAgainstACreature.ABruteSwingsAtThePlayerFromExactlyTheDistanceItAlwaysDid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReachAgainstThePlayerTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReachAgainstACreatureTest;

	// THE CONTROL FOR EVERYTHING ELSE IN THIS FILE. The Brute's reach against the player is its designed 90 cm plus
	// the 2 cm contact tolerance, 92, and it must be exactly that after the change as it was before: half a centimetre
	// inside it swings, half a centimetre outside it chases.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnReachTestCreature<ACataclysmBruteCharacter>(World, FVector::ZeroVector);
	const FReachTestPlayer Rig(World, FVector(20.0f * ReachTestMetre, 0.0f, 0.0f));
	ACataclysmEnemyController* Brain = ReachTestBrainOf(Brute);
	if (!TestNotNull(TEXT("set-up: a Brute"), Brute) || !TestNotNull(TEXT("set-up: with a brain"), Brain)
		|| !TestNotNull(TEXT("set-up: a possessed player"), Rig.Player))
	{
		return false;
	}

	const float Edge =
		ACataclysmBruteCharacter::DesignedMeleeReachCm + ACataclysmEnemyController::ContactToleranceCm;
	PlaceReachTestActor(Rig.Player, Brute, Edge - 0.5f);
	if (!TestEqual(TEXT("set-up: the player stands half a centimetre inside 92 cm"),
			ReachTestApart(Brute, Rig.Player), 91.5f, 0.01f))
	{
		return false;
	}
	SpendReachTestAbilities(World, Brain, Edge - 0.5f);

	TestEqual(TEXT("CONTROL: half a centimetre inside its designed reach and the tolerance, it swings at the player"),
		static_cast<int32>(Brain->Think()), static_cast<int32>(ECataclysmBrainAction::Attacking));
	TestTrue(TEXT("CONTROL: and the player is what it swings at"), Brain->CurrentTarget.Get() == Rig.Player);

	PlaceReachTestActor(Rig.Player, Brute, Edge + 0.5f);
	TestEqual(TEXT("CONTROL: half a centimetre outside, it chases"),
		static_cast<int32>(Brain->Think()), static_cast<int32>(ECataclysmBrainAction::Chasing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReachAgainstAWideBodyTest,
	"Cataclysm.ReachAgainstACreature.AgainstACreatureAsWideAsItselfABrutesReachEndsSixCentimetresFurtherOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReachAgainstAWideBodyTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReachAgainstACreatureTest;

	// THE SAME TWO STEPS AS THE CONTROL ABOVE, 6 CM FURTHER OUT: 96 + 2 = 98. A monster's Brute against a plain
	// creature of the players' side, so no subjugation enters; the thrall tests above are the case the issue names.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnReachTestCreature<ACataclysmBruteCharacter>(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Wide =
		SpawnReachTestBody(World, FVector(20.0f * ReachTestMetre, 0.0f, 0.0f), ECataclysmTeam::Players);
	ACataclysmEnemyController* Brain = ReachTestBrainOf(Brute);
	if (!TestNotNull(TEXT("set-up: a Brute"), Brute) || !TestNotNull(TEXT("set-up: with a brain"), Brain)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Wide)
		|| !TestEqual(TEXT("set-up: it is 48 cm across"), ReachTestRadiusOf(Wide), 48.0f, 0.001f))
	{
		return false;
	}

	PlaceReachTestActor(Wide, Brute, 97.5f);
	SpendReachTestAbilities(World, Brain, 97.5f);

	TestEqual(TEXT("half a centimetre inside 98 cm, it swings at a creature as wide as itself"),
		static_cast<int32>(Brain->Think()), static_cast<int32>(ECataclysmBrainAction::Attacking));

	PlaceReachTestActor(Wide, Brute, 98.5f);
	TestEqual(TEXT("CONTROL: half a centimetre outside, it chases: the reach grew by the 6 cm and no more"),
		static_cast<int32>(Brain->Think()), static_cast<int32>(ECataclysmBrainAction::Chasing));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// The swing's landing (issue #783's test)
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReachLandingTest,
	"Cataclysm.ReachAgainstACreature.ASwingLandsOnACreatureAsWideAsItselfThatIsPressedAgainstIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReachLandingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReachAgainstACreatureTest;

	// TWO BRUTES, EACH SWINGING ONCE, ten metres apart so neither is near the other's target. Each swing is ordered
	// at 80 cm, inside any reach, so it must still be in reach when it lands; the target then stands where it will
	// be when the blow arrives. Nothing thinks: `AttackTarget` is the order, as `Cataclysm.EnemyHitTiming.` gives it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FVector Apart(0.0f, 10.0f * ReachTestMetre, 0.0f);
	ACataclysmBruteCharacter* Pressed = SpawnReachTestCreature<ACataclysmBruteCharacter>(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Touching = SpawnReachTestBody(World, FVector(80.0f, 0.0f, 0.0f), ECataclysmTeam::Players);
	ACataclysmBruteCharacter* Control = SpawnReachTestCreature<ACataclysmBruteCharacter>(World, Apart);
	ACataclysmEnemyCharacter* Stepped =
		SpawnReachTestBody(World, Apart + FVector(80.0f, 0.0f, 0.0f), ECataclysmTeam::Players);
	if (!TestNotNull(TEXT("set-up: a Brute"), Pressed) || !TestNotNull(TEXT("set-up: its target"), Touching)
		|| !TestNotNull(TEXT("set-up: a second Brute"), Control) || !TestNotNull(TEXT("set-up: its target"), Stepped)
		|| !TestEqual(TEXT("set-up: the target is 48 cm across"), ReachTestRadiusOf(Touching), 48.0f, 0.001f))
	{
		return false;
	}

	const float FullTouching = ReachTestHealthOf(Touching);
	const float FullStepped = ReachTestHealthOf(Stepped);
	Pressed->AttackTarget(Touching);
	Control->AttackTarget(Stepped);
	if (!TestTrue(TEXT("set-up: the first swing waits for its strike"), Pressed->HasPendingBasicAttack())
		|| !TestTrue(TEXT("set-up: and so does the second"), Control->HasPendingBasicAttack()))
	{
		return false;
	}

	// PUSHED APART TO CONTACT, 96 cm, which is what a collision solver does to two bodies ordered to stand at 80; and
	// the control's half a centimetre beyond 98.
	PlaceReachTestActor(Touching, Pressed, 96.0f);
	PlaceReachTestActor(Stepped, Control, 98.5f);
	World->TimeSeconds += ACataclysmBruteCharacter::SwingStrikeSeconds + 0.01;

	TestTrue(TEXT("the blow lands on a creature as wide as itself that is pressed against it"),
		Pressed->LandPendingBasicAttack());
	TestTrue(TEXT("and takes health"), ReachTestHealthOf(Touching) < FullTouching);

	TestFalse(TEXT("CONTROL: half a centimetre beyond its reach against that body, the blow is missed"),
		Control->LandPendingBasicAttack());
	TestEqual(TEXT("CONTROL: and the creature that stepped out is untouched"),
		ReachTestHealthOf(Stepped), FullStepped, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Famished Beasts: fight rather than eat, when what it would fight is within its reach
// ---------------------------------------------------------------------------------------------------------------

namespace CataclysmReachAgainstACreatureTest
{
	/** A drop of gear where it is put, as `Cataclysm.DungeonModifierEffects.FamishedBeasts*` places one. */
	static ACataclysmDroppedItem* SpawnReachTestDrop(UWorld* World, const FVector& At)
	{
		ACataclysmDroppedItem* Drop = World->SpawnActor<ACataclysmDroppedItem>(At, FRotator::ZeroRotator);
		if (Drop)
		{
			Drop->Item.Base = FName(TEXT("Greataxe"));
		}
		return Drop;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReachFamishedWideTest,
	"Cataclysm.ReachAgainstACreature.AHungryBrutePressedAgainstACreatureAsWideAsItselfFightsRatherThanEats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReachFamishedWideTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReachAgainstACreatureTest;

	// THE FLOOR RULE'S FLAG IS WRITTEN BY HAND, as the game mode's beat writes it, and only after the abilities are
	// spent: a hungry creature that walks to a drop never winds up, so the spending has to happen before it is hungry.
	// The drop is five metres behind it, inside the 10 m it seeks within.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnReachTestCreature<ACataclysmBruteCharacter>(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Wide =
		SpawnReachTestBody(World, FVector(20.0f * ReachTestMetre, 0.0f, 0.0f), ECataclysmTeam::Players);
	ACataclysmDroppedItem* Drop = SpawnReachTestDrop(World, FVector(-5.0f * ReachTestMetre, 0.0f, 0.0f));
	ACataclysmEnemyController* Brain = ReachTestBrainOf(Brute);
	if (!TestNotNull(TEXT("set-up: a Brute"), Brute) || !TestNotNull(TEXT("set-up: with a brain"), Brain)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Wide) || !TestNotNull(TEXT("set-up: a drop"), Drop))
	{
		return false;
	}

	const float Contact = ReachTestRadiusOf(Brute) + ReachTestRadiusOf(Wide);
	PlaceReachTestActor(Wide, Brute, Contact);
	if (!TestEqual(TEXT("set-up: they touch, 96 cm apart"), ReachTestApart(Brute, Wide), 96.0f, 0.01f))
	{
		return false;
	}
	SpendReachTestAbilities(World, Brain, Contact);
	Brute->bSeeksDropsForTheFloorRule = true;

	TestEqual(TEXT("a hungry Brute pressed against a creature as wide as itself swings rather than walking to the drop"),
		static_cast<int32>(Brain->Think()), static_cast<int32>(ECataclysmBrainAction::Attacking));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReachFamishedPlayerTest,
	"Cataclysm.ReachAgainstACreature.AHungryBruteTouchingThePlayerFightsAndOneStepBeyondItEats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmReachFamishedPlayerTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmReachAgainstACreatureTest;

	// THE TOLERANCE FAMISHED BEASTS LEFT OUT. A Brute pressed against the player stands a hair past its 90 cm --
	// `ContactToleranceCm` records why -- so 91 cm is touching, and the swing's own test calls it in reach. Until
	// issue #2706 this rule compared the bare 90 and walked off to eat. 93 cm is beyond the tolerance, and there it
	// still eats: the CONTROL.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnReachTestCreature<ACataclysmBruteCharacter>(World, FVector::ZeroVector);
	const FReachTestPlayer Rig(World, FVector(20.0f * ReachTestMetre, 0.0f, 0.0f));
	ACataclysmDroppedItem* Drop = SpawnReachTestDrop(World, FVector(-5.0f * ReachTestMetre, 0.0f, 0.0f));
	ACataclysmEnemyController* Brain = ReachTestBrainOf(Brute);
	if (!TestNotNull(TEXT("set-up: a Brute"), Brute) || !TestNotNull(TEXT("set-up: with a brain"), Brain)
		|| !TestNotNull(TEXT("set-up: a possessed player"), Rig.Player) || !TestNotNull(TEXT("set-up: a drop"), Drop))
	{
		return false;
	}

	PlaceReachTestActor(Rig.Player, Brute, 91.0f);
	SpendReachTestAbilities(World, Brain, 91.0f);
	Brute->bSeeksDropsForTheFloorRule = true;

	TestEqual(TEXT("a hungry Brute 91 cm from the player, inside its reach and the tolerance, swings rather than eats"),
		static_cast<int32>(Brain->Think()), static_cast<int32>(ECataclysmBrainAction::Attacking));

	PlaceReachTestActor(Rig.Player, Brute, 93.0f);
	TestEqual(TEXT("CONTROL: at 93 cm, beyond them, it walks to the drop"),
		static_cast<int32>(Brain->Think()), static_cast<int32>(ECataclysmBrainAction::SeekingADrop));
	return true;
}

#endif  // WITH_AUTOMATION_TESTS
