// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCastEffect.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmProjectileEffect.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "NiagaraSystem.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the quick cast: a flat bolt fired half a second after the creature
 * decides to, with no marker on the ground. Issue #2808, from the owner's
 * playtest of 2026-11-21: "sometimes enemies should be able to just shoot a
 * fireball at you as long as you can see it".
 *
 * WHAT THEY HOLD. The two quick casts -- the Succubus's Soulfire and the
 * Corrupted Sentinel's Siege Bolt -- leave at 0.5 seconds and not before, draw
 * no marker, and carry the 0.5 metre radius that makes them possible to step
 * around in flight. THE CONTROLS are every other enemy ability: their wind-ups
 * are the figures they had before, held here as literals, and they still draw a
 * marker -- the Sentinel's own mortar is drawn live, beside its bolt.
 *
 * WHAT NO TEST HERE CAN SEE: the bolt on screen. The automation command passes
 * -nullrhi (issue #650). The clock is moved by hand and the landing is asked
 * for by hand, as `ACataclysmEnemyCharacter::Tick` asks for it every frame in
 * play, because no automation test ticks a world.
 */

namespace CataclysmQuickCastTest
{
	constexpr float M = 100.0f;

	/** The quick cast, from the header both creatures' constants must equal. */
	constexpr float QuickCast = FCataclysmEnemyAbility::QuickCastSeconds;

	static void AdvanceWorldClock(UWorld* World, double Seconds)
	{
		// A world built by UWorld::CreateWorld is never ticked, so its clock
		// moves only when a test moves it. See CataclysmEnemyBehaviourTests.cpp.
		World->TimeSeconds += Seconds;
	}

	template <typename T>
	static T* Spawn(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(T::StaticClass(), Where, FRotator::ZeroRotator, Params);
	}

	/** Something on the players' side with health to lose. Spawned far away and then moved, because two capsules
	 *  created at contact distance push each other apart. */
	static ACataclysmEnemyCharacter* SpawnTarget(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Target = Spawn<ACataclysmEnemyCharacter>(World, FVector(30000.0f, 30000.0f, 0.0f));
		if (Target)
		{
			Target->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Target->SetHealth(100000.0f);
			Target->SetAttackDamage(0.0f);
			Target->SetActorLocation(Where);
		}
		return Target;
	}

	/** The ability of this name in a creature's list, or null. */
	static const FCataclysmEnemyAbility* Named(const TArray<FCataclysmEnemyAbility>& Abilities, const TCHAR* Name)
	{
		return Abilities.FindByPredicate([Name](const FCataclysmEnemyAbility& A) { return A.Name == FName(Name); });
	}

	/**
	 * Holds a quick cast from its decision to its release: winding up with no marker; nothing fired a hair before
	 * half a second; the bolt fired at half a second, with the 0.5 metre radius. `Fired` reads the creature's last
	 * shot, which is null until the bolt leaves.
	 */
	static void HoldTheQuickCast(FAutomationTestBase& Test, UWorld* World, ACataclysmEnemyController* Brain,
								 int32 Expected, float RadiusCm, TFunctionRef<ACataclysmProjectile*()> Fired)
	{
		Test.TestEqual(TEXT("it is winding up the quick cast"), Brain->WindingUpAbility, Expected);
		Test.TestNull(TEXT("**and draws no marker on the ground**"), Brain->WindUpMarker.Get());
		Test.TestNull(TEXT("and has fired nothing yet"), Fired());

		AdvanceWorldClock(World, QuickCast - 0.01);
		Test.TestFalse(TEXT("a hundredth of a second short of half a second it has not landed"),
			Brain->LandWindUpIfDue());
		Test.TestNull(TEXT("so nothing is fired yet"), Fired());

		AdvanceWorldClock(World, 0.01);
		Test.TestTrue(TEXT("**at half a second it lands**"), Brain->LandWindUpIfDue());
		Test.TestEqual(TEXT("and the wind-up is over"), Brain->WindingUpAbility, static_cast<int32>(INDEX_NONE));

		ACataclysmProjectile* Bolt = Fired();
		if (Test.TestNotNull(TEXT("**the bolt is fired**"), Bolt))
		{
			Test.TestEqual(TEXT("with the 0.5 metre radius that can be stepped around in flight"), Bolt->RadiusCm,
				RadiusCm);
			Test.TestEqual(TEXT("which is 0.5 metres"), RadiusCm, 50.0f);
		}
	}
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmQuickCastSoulfireLeavesAtHalfASecond,
	"Cataclysm.QuickCast.SoulfireIsFiredHalfASecondAfterItDecidesWithNoMarker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuickCastSoulfireLeavesAtHalfASecond::RunTest(const FString&)
{
	using namespace CataclysmQuickCastTest;
	using Succubus_t = ACataclysmSuccubusCharacter;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	Succubus_t* Succubus = Spawn<Succubus_t>(World, FVector::ZeroVector);
	// SIX METRES OUT, inside its 8 metre range, straight ahead so it need not turn.
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, FVector(6.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyController* Brain = Succubus ? Cast<ACataclysmEnemyController>(Succubus->GetController()) : nullptr;
	if (!TestNotNull(TEXT("a Succubus"), Succubus) || !TestNotNull(TEXT("something to shoot"), Target)
		|| !TestNotNull(TEXT("its brain"), Brain))
	{
		return false;
	}

	TestEqual(TEXT("Soulfire's wind-up is the quick cast"), Succubus_t::SoulfireWindUpSeconds, QuickCast);
	TestEqual(TEXT("which is half a second"), QuickCast, 0.5f);

	// THE CURSE COMES FIRST, by its order in the list, and lands at once. Its 10 second cooldown then leaves Soulfire
	// the only ability, once the 2.6 second attack interval has passed.
	Brain->Think();
	AdvanceWorldClock(World, Succubus_t::DesignedAttackIntervalSeconds + 0.1);
	if (!TestEqual(TEXT("it begins a cast"), static_cast<int32>(Brain->Think()),
			static_cast<int32>(ECataclysmBrainAction::WindingUp)))
	{
		return false;
	}

	HoldTheQuickCast(*this, World, Brain, Succubus_t::SoulfireAbility, Succubus_t::SoulfireRadiusCm,
		[Succubus]() { return Succubus->LastShotFired.Get(); });
	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmQuickCastSiegeBoltLeavesAtHalfASecond,
	"Cataclysm.QuickCast.SiegeBoltIsFiredHalfASecondAfterItDecidesAndTheMortarStillMarks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuickCastSiegeBoltLeavesAtHalfASecond::RunTest(const FString&)
{
	using namespace CataclysmQuickCastTest;
	using Sentinel_t = ACataclysmCorruptedSentinelCharacter;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	Sentinel_t* Sentinel = Spawn<Sentinel_t>(World, FVector::ZeroVector);
	// THREE METRES OUT: inside the mortar's 3.48 metre minimum, so the bolt is what it chooses.
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyController* Brain = Sentinel ? Cast<ACataclysmEnemyController>(Sentinel->GetController()) : nullptr;
	if (!TestNotNull(TEXT("a Sentinel"), Sentinel) || !TestNotNull(TEXT("something to shoot"), Target)
		|| !TestNotNull(TEXT("its brain"), Brain))
	{
		return false;
	}

	TestEqual(TEXT("Siege Bolt's wind-up is the quick cast"), Sentinel_t::SiegeBoltWindUpSeconds, QuickCast);

	if (!TestEqual(TEXT("it begins a cast"), static_cast<int32>(Brain->Think()),
			static_cast<int32>(ECataclysmBrainAction::WindingUp)))
	{
		return false;
	}
	HoldTheQuickCast(*this, World, Brain, Sentinel_t::SiegeBoltAbility, Sentinel_t::SiegeBoltRadiusCm,
		[Sentinel]() { return Sentinel->LastShotFired.Get(); });

	// THE CONTROL, ON THE SAME CREATURE: its mortar is an area and keeps its marker. Six metres out, past the
	// mortar's minimum, once the attack interval has passed.
	Target->SetActorLocation(FVector(6.0f * M, 0.0f, Target->GetActorLocation().Z));
	AdvanceWorldClock(World, Sentinel_t::DesignedAttackIntervalSeconds + 0.1);
	TestEqual(TEXT("it begins the mortar"), static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestEqual(TEXT("and it is the mortar"), Brain->WindingUpAbility,
		static_cast<int32>(Sentinel_t::BrimstoneMortarAbility));
	TestNotNull(TEXT("**which draws its circle, as an area does**"), Brain->WindUpMarker.Get());
	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmQuickCastOnlyTheTwoBoltsAreQuick,
	"Cataclysm.QuickCast.OnlyTheTwoBoltsAreQuickAndEveryAreaKeepsItsWindUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuickCastOnlyTheTwoBoltsAreQuick::RunTest(const FString&)
{
	using namespace CataclysmQuickCastTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// EVERY ENEMY ABILITY'S RELEASE TIME, AS A LITERAL. An attack lands at its wind-up's own length since issue
	// #2808, so this is when each is released. The areas, lobs and charges are the figures they had before the
	// issue, which the owner chose to keep on 2026-11-21: "Keep them as they are".
	//
	// AND ITS COOLDOWN, AS A LITERAL. The same issue made both bolts repeat every second by shortening the two
	// creatures' attack intervals, which gate every ability; each other ability keeps its own cooldown, so it comes
	// no more often. That is the control for the faster bolts.
	struct FCase
	{
		const TCHAR* Ability;
		float ReleasedAt;
		bool bQuick;
		float Cooldown;
	};
	struct FCreature
	{
		const TCHAR* What;
		ACataclysmCharacterBase* Body;
		TArray<FCase> Cases;
	};
	const FVector Apart(40.0f * M, 0.0f, 0.0f);
	TArray<FCreature> Creatures = {
		{TEXT("Succubus"), Spawn<ACataclysmSuccubusCharacter>(World, Apart * 1.0f),
			{{TEXT("Soulfire"), 0.5f, true, 0.0f}, {TEXT("Wither the Living"), 0.0f, false, 10.0f}}},
		{TEXT("Corrupted Sentinel"), Spawn<ACataclysmCorruptedSentinelCharacter>(World, Apart * 2.0f),
			{{TEXT("Siege Bolt"), 0.5f, true, 0.0f}, {TEXT("Brimstone Mortar"), 1.2571f, false, 8.0f}}},
		{TEXT("Brute"), Spawn<ACataclysmBruteCharacter>(World, Apart * 3.0f),
			{{TEXT("Stomp"), 1.4f, false, 8.0f}, {TEXT("Rip and Toss"), 1.0f, false, 12.0f}}},
		{TEXT("Abyssal Warden"), Spawn<ACataclysmAbyssalWardenCharacter>(World, Apart * 4.0f),
			{{TEXT("Molten Roar"), 2.0f, false, 12.0f}, {TEXT("Stampede"), 0.83f, false, 5.0f}}},
		{TEXT("Gatekeeper"), Spawn<ACataclysmGatekeeperCharacter>(World, Apart * 5.0f),
			{{TEXT("Soul Harvest"), 2.0f, false, 20.0f}, {TEXT("Call the Damned"), 0.0f, false, 10.0f},
			 {TEXT("Soulfall"), 1.2571f, false, 10.0f}, {TEXT("Dread Cleave"), 0.9714f, false, 0.0f}}},
		{TEXT("Hellhound"), Spawn<ACataclysmHellhoundCharacter>(World, Apart * 6.0f),
			{{TEXT("Hellrush"), 0.83f, false, 5.0f}}},
	};

	int32 Quick = 0;
	for (const FCreature& Creature : Creatures)
	{
		if (!TestNotNull(FString::Printf(TEXT("a %s"), Creature.What), Creature.Body))
		{
			return false;
		}
		const TArray<FCataclysmEnemyAbility> Abilities = Creature.Body->EnemyAbilities();
		TestEqual(FString::Printf(TEXT("the %s has the abilities this table names"), Creature.What),
			Abilities.Num(), Creature.Cases.Num());
		for (const FCase& Case : Creature.Cases)
		{
			const FCataclysmEnemyAbility* Ability = Named(Abilities, Case.Ability);
			if (!TestNotNull(FString::Printf(TEXT("the %s's %s"), Creature.What, Case.Ability), Ability))
			{
				continue;
			}
			TestEqual(FString::Printf(TEXT("the %s's %s is released %.4f s after it decides"), Creature.What,
				Case.Ability, Case.ReleasedAt), Ability->WindUpSeconds, Case.ReleasedAt, 0.0001f);
			TestEqual(FString::Printf(TEXT("and %s a marker"), Case.bQuick ? TEXT("draws no") : TEXT("is not denied")),
				static_cast<int32>(Ability->bDrawsNoMarker), static_cast<int32>(Case.bQuick));
			TestEqual(FString::Printf(TEXT("and keeps its %.0f s cooldown"), Case.Cooldown), Ability->CooldownSeconds,
				Case.Cooldown, 0.0001f);
			Quick += Ability->bDrawsNoMarker ? 1 : 0;
		}
	}
	TestEqual(TEXT("**exactly two abilities in the game are quick casts**"), Quick, 2);
	return true;
}

// --------------------------------------------------------------------------
// Seen in flight, and every second. Issue #2808, the owner's two requirements
// of 2026-11-21: "Make sure those attacks can actually be seen. Like a flaming
// fireball that's obvious to the player", and "give those quick enemy ranged
// attacks a 1 second cooldown. They should be a basic attack sort of thing."
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmQuickCastIsDrawnLargerBrighterAndNeverCulled,
	"Cataclysm.QuickCast.EachBoltIsDrawnLargerBrighterAndNeverCulled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuickCastIsDrawnLargerBrighterAndNeverCulled::RunTest(const FString&)
{
	using namespace CataclysmQuickCastTest;
	using Effect_t = UCataclysmProjectileEffect;
	using Sentinel_t = ACataclysmCorruptedSentinelCharacter;
	using Succubus_t = ACataclysmSuccubusCharacter;

	// WHAT THIS CAN SEE, AND WHAT IT CANNOT. No test can see a Niagara effect (-nullrhi, issue #559), so this reads
	// `LookFor`, which is what `AttachTo` sets on the effect; tools/tests/test_quick_cast.py reads that `AttachTo`
	// passes each answer on.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	Succubus_t* Succubus = Spawn<Succubus_t>(World, FVector::ZeroVector);
	Sentinel_t* Sentinel = Spawn<Sentinel_t>(World, FVector(0.0f, 20.0f * M, 0.0f));
	Succubus_t* Thrall = Spawn<Succubus_t>(World, FVector(0.0f, -20.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, FVector(6.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("a Succubus"), Succubus) || !TestNotNull(TEXT("a Sentinel"), Sentinel)
		|| !TestNotNull(TEXT("a second Succubus"), Thrall) || !TestNotNull(TEXT("a target"), Target))
	{
		return false;
	}
	const FVector Aim = Target->GetActorLocation();

	// THE TWO QUICK CASTS.
	struct FBolt
	{
		const TCHAR* What;
		ACataclysmProjectile* Shot;
	};
	Succubus->UseEnemyAbility(Succubus_t::SoulfireAbility, Target, Aim);
	ACataclysmProjectile* Soulfire = Succubus->LastShotFired.Get();
	Sentinel->UseEnemyAbility(Sentinel_t::SiegeBoltAbility, Target, Aim);
	ACataclysmProjectile* SiegeBolt = Sentinel->LastShotFired.Get();
	for (const FBolt& Bolt : {FBolt{TEXT("Soulfire"), Soulfire}, FBolt{TEXT("Siege Bolt"), SiegeBolt}})
	{
		if (!TestNotNull(FString::Printf(TEXT("%s is fired"), Bolt.What), Bolt.Shot))
		{
			continue;
		}
		const FCataclysmProjectileLook Look = Effect_t::LookFor(Bolt.Shot);
		TestTrue(FString::Printf(TEXT("%s is a quick cast"), Bolt.What), Bolt.Shot->bQuickCast);
		TestEqual(FString::Printf(TEXT("%s is drawn at its 0.5 m blast radius"), Bolt.What), Look.Scale, 0.5f,
			0.0001f);
		TestEqual(TEXT("four times brighter"), Look.ColourMultiplier, 4.0f, 0.0001f);
		TestEqual(TEXT("with three times the trail"), Look.IntensityMultiplier, 3.0f, 0.0001f);
		TestFalse(FString::Printf(TEXT("**and %s is never culled, a hostile fired it**"), Bolt.What),
			Look.bMayBeCulled);
	}

	// CONTROL: THE SENTINEL'S MORTAR, AN ORDINARY ENEMY PROJECTILE. Its own look, and still never culled.
	Sentinel->UseEnemyAbility(Sentinel_t::BrimstoneMortarAbility, Target, Aim);
	ACataclysmProjectile* Shell = Sentinel->LastShotFired.Get();
	if (TestNotNull(TEXT("the mortar fires a shell"), Shell))
	{
		const FCataclysmProjectileLook Look = Effect_t::LookFor(Shell);
		TestFalse(TEXT("the shell is not a quick cast"), Shell->bQuickCast);
		TestEqual(TEXT("so it keeps the look of its 40 cm body"), Look.Scale, Effect_t::ScaleFor(Shell->BodyRadiusCm),
			0.0001f);
		TestEqual(TEXT("and its colour"), Look.ColourMultiplier, 1.0f, 0.0001f);
		TestEqual(TEXT("and its trail"), Look.IntensityMultiplier, 1.0f, 0.0001f);
		TestFalse(TEXT("and a hostile's shell is never culled either"), Look.bMayBeCulled);
	}

	// CONTROL: THE SAME BOLT FROM THE PLAYER'S SIDE, a thrall. Drawn as a quick cast, and kept to the budget as the
	// player's own effects are.
	Thrall->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Thrall->UseEnemyAbility(Succubus_t::SoulfireAbility, Target, Aim);
	ACataclysmProjectile* Theirs = Thrall->LastShotFired.Get();
	if (TestNotNull(TEXT("a thrall fires Soulfire"), Theirs))
	{
		TestTrue(TEXT("**a bolt on the player's side may be culled**"), Effect_t::LookFor(Theirs).bMayBeCulled);
		TestEqual(TEXT("though it is still drawn as a quick cast"), Effect_t::LookFor(Theirs).Scale, 0.5f, 0.0001f);
	}
	TestTrue(TEXT("and nothing at all keeps the budget"), Effect_t::MayBeCulled(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmQuickCastAsksForTheCastFlare,
	"Cataclysm.QuickCast.AQuickCastAsksForTheCastFlareAndAMarkedAttackDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuickCastAsksForTheCastFlare::RunTest(const FString&)
{
	using namespace CataclysmQuickCastTest;
	using Sentinel_t = ACataclysmCorruptedSentinelCharacter;

	// `TimesAsked` IS WHAT A TEST CAN SEE: the flare itself never spawns under -nullrhi.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	Sentinel_t* Sentinel = Spawn<Sentinel_t>(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyController* Brain = Sentinel ? Cast<ACataclysmEnemyController>(Sentinel->GetController()) : nullptr;
	if (!TestNotNull(TEXT("a Sentinel"), Sentinel) || !TestNotNull(TEXT("a target"), Target)
		|| !TestNotNull(TEXT("its brain"), Brain))
	{
		return false;
	}

	const int32 Before = UCataclysmCastEffect::TimesAsked;
	TestEqual(TEXT("it begins the bolt"), static_cast<int32>(Brain->Think()),
		static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestEqual(TEXT("**the quick cast asks for the cast flare once**"), UCataclysmCastEffect::TimesAsked, Before + 1);
	TestEqual(TEXT("in the creature's own damage type"), UCataclysmCastEffect::LastDamageTypeAsked,
		FName(TEXT("Demonic")));

	// CONTROL: the mortar, which draws a marker instead, asks for no flare.
	AdvanceWorldClock(World, QuickCast);
	Brain->LandWindUpIfDue();
	Target->SetActorLocation(FVector(6.0f * M, 0.0f, Target->GetActorLocation().Z));
	AdvanceWorldClock(World, Sentinel_t::DesignedAttackIntervalSeconds + 0.1);
	const int32 BeforeMortar = UCataclysmCastEffect::TimesAsked;
	TestEqual(TEXT("it begins the mortar"), Brain->WindingUpAbility == INDEX_NONE
		? static_cast<int32>(Brain->Think()) : -1, static_cast<int32>(ECataclysmBrainAction::WindingUp));
	TestEqual(TEXT("and it is the mortar"), Brain->WindingUpAbility, static_cast<int32>(Sentinel_t::BrimstoneMortarAbility));
	TestEqual(TEXT("which asks for no flare"), UCataclysmCastEffect::TimesAsked, BeforeMortar);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmQuickCastRepeatsEverySecond,
	"Cataclysm.QuickCast.EachBoltRepeatsEverySecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuickCastRepeatsEverySecond::RunTest(const FString&)
{
	using namespace CataclysmQuickCastTest;
	using Sentinel_t = ACataclysmCorruptedSentinelCharacter;
	using Succubus_t = ACataclysmSuccubusCharacter;

	TestEqual(TEXT("the Sentinel attacks every second"), Sentinel_t::DesignedAttackIntervalSeconds, 1.0f);
	TestEqual(TEXT("and so does the Succubus"), Succubus_t::DesignedAttackIntervalSeconds, 1.0f);

	// THROUGH THE BRAIN: a bolt lands, and the next one begins one second later and not a hundredth before.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	Sentinel_t* Sentinel = Spawn<Sentinel_t>(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Target = SpawnTarget(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyController* Brain = Sentinel ? Cast<ACataclysmEnemyController>(Sentinel->GetController()) : nullptr;
	if (!TestNotNull(TEXT("a Sentinel"), Sentinel) || !TestNotNull(TEXT("a target"), Target)
		|| !TestNotNull(TEXT("its brain"), Brain))
	{
		return false;
	}

	for (int32 Shot = 1; Shot <= 3; ++Shot)
	{
		if (!TestEqual(FString::Printf(TEXT("bolt %d begins"), Shot), static_cast<int32>(Brain->Think()),
				static_cast<int32>(ECataclysmBrainAction::WindingUp)))
		{
			return false;
		}
		AdvanceWorldClock(World, QuickCast);
		TestTrue(FString::Printf(TEXT("bolt %d lands"), Shot), Brain->LandWindUpIfDue());

		// A HUNDREDTH SHORT OF A SECOND AFTER IT LANDED: the interval still holds the next.
		AdvanceWorldClock(World, Sentinel_t::DesignedAttackIntervalSeconds - 0.01);
		Brain->Think();
		TestEqual(FString::Printf(TEXT("nothing begins a hundredth short of a second after bolt %d"), Shot),
			Brain->WindingUpAbility, static_cast<int32>(INDEX_NONE));
		// PAST THE SECOND BY A HUNDREDTH, not onto it: the interval test is a float comparison of clocks the test
		// moved in pieces, and landing exactly on it would decide this by rounding.
		AdvanceWorldClock(World, 0.02);
	}
	TestEqual(TEXT("**three bolts, one a second**"), Brain->AbilitiesUsed, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmQuickCastReadsTheTrailFromTheAsset,
	"Cataclysm.QuickCast.TheDenserTrailIsAMultipleOfTheAssetsOwnDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuickCastReadsTheTrailFromTheAsset::RunTest(const FString&)
{
	// THE DEFAULT IS READ, NOT GUESSED. Loading the asset works under -nullrhi, as
	// `Cataclysm.Effects.ProjectileBodyExposesTheStandardParameterBlock` shows; spawning it does not.
	const UNiagaraSystem* System =
		LoadObject<UNiagaraSystem>(nullptr, UCataclysmProjectileEffect::SystemAssetPath);
	if (!TestNotNull(TEXT("NS_Proj_Body loads"), System))
	{
		return false;
	}
	const float Default = UCataclysmProjectileEffect::DefaultIntensityOf(System);
	AddInfo(FString::Printf(TEXT("NS_Proj_Body's own User.Intensity default reads %.4f, so a quick cast's trail is "
								 "%.4f."), Default, Default * UCataclysmProjectileEffect::QuickCastIntensityMultiplier));
	TestTrue(TEXT("the asset's default is a usable figure"), Default > 0.0f);
	TestEqual(TEXT("and nothing to read answers one"), UCataclysmProjectileEffect::DefaultIntensityOf(nullptr), 1.0f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
