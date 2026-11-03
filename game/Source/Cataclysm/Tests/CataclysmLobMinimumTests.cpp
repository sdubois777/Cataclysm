// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Issue #2716: a lobbed attack's marked circle never covers the caster's own body, at any rung.
 *
 * WHAT WAS WRONG, FOUND BY READING. Three abilities mark a circle where they land and refuse a target nearer than
 * `marked radius + the caster's body radius`, so the circle never covers the ground the caster stands on (issue #475,
 * on the Brute): the Brute's Rip and Toss (210 + 48 = 258 cm), the Gatekeeper's Soulfall (300 + 48 = 348) and the
 * Corrupted Sentinel's Brimstone Mortar (300 + 48 = 348). The body in that sum was the UNSCALED 48 cm, and a rarer
 * creature is bigger: `SetRarityStep` scales the whole actor, capsule included, by the rung's `BodyScale`
 * (game/Data/EnemyRarities.csv). An Elite's body is 57.6 cm, a Cataclysm Boss's 119.44, so between the old minimum and
 * the new the circle covered the caster's own feet again.
 *
 * WHAT THESE GUARD, ruled 2026-11-03 by the coordinating session: each minimum is the designed margin (the marked
 * radius) plus the caster's body AS IT STANDS, so a Common caster's minimum is exactly what it was. The maximum is not
 * changed.
 *
 * HOW THE CIRCLE IS READ. Through the brain, as the game draws it: `Think` begins the lob, `ShowWindUpMarker` draws an
 * `ACataclysmTelegraphMarker` circle at the aim point, and the test measures, on the floor plane, how far the circle's
 * nearest edge is from the caster's centre. The circle covers the caster's body when that is less than the body's
 * radius. Each such test has two halves, each in a world of its own: the CASE, a target half way between the old
 * minimum and the new, where the circle would cover the caster's body; and the CONTROL, a target 2 cm beyond the new
 * minimum, where the lob must still be thrown and its circle clear the body. The control computes its distance from
 * the ability's marked radius and the capsule, not from the minimum under test, so it holds before the fix and after.
 *
 * WHERE EACH FIXTURE'S STARTING STATE COMES FROM. Every creature spawns at the origin facing +X, at the Common rung
 * (scale 1) with every ability ready and full health, so a Gatekeeper is in phase 1 (asserted); the rung is then set
 * with `SetRarityStep`, which re-applies the starting attributes, health included. Its body is read back from its
 * capsule, and the rung's scale from the rarity table, before any distance is placed. The target is a plain
 * `ACataclysmEnemyCharacter` of the players' side (48 cm, Common), given 100,000 health and no damage, put on +X at the
 * distance under test, so the caster faces it and a directional ability needs no turn. No world is ticked: the clock
 * is moved by hand. The Brute's Stomp (0 to 350 cm, ahead of the throw in its list) would be chosen at every distance
 * these use, so it is spent first: one pass begins it, the clock passes its wind-up, one pass lands it, and the clock
 * passes the Brute's attack interval (1.2 s), well inside the Stomp's 8 s cooldown; the target is put back after,
 * because the Stomp shoves what it catches.
 */
namespace CataclysmLobMinimumTest
{
	/** Centimetres in a metre. */
	constexpr float LobTestMetre = 100.0f;

	/** How far beyond the new minimum the CONTROL stands its target. */
	constexpr float LobTestControlMarginCm = 2.0f;

	/** Which side of the new minimum a scene stands its target. */
	enum class ELobTestWhere : uint8
	{
		/** Half way between the minimum the unscaled body gave and the one the body as it stands gives. */
		InsideTheGrownBody,

		/** `LobTestControlMarginCm` beyond the marked radius plus the body as it stands. */
		JustClearOfTheGrownBody,
	};

	/** What one scene saw after the pass that chooses. */
	struct FLobTestSeen
	{
		/** False when the set-up failed; it has already said why. */
		bool bReady = false;

		/** The caster's body as it stands, read from its capsule. */
		float CasterRadiusCm = 0.0f;

		/** Where the target was stood, centre to centre on the floor plane. */
		float DistanceCm = 0.0f;

		/** What the brain was winding up after the pass, or INDEX_NONE. */
		int32 Winding = INDEX_NONE;

		/** How many circles (not lanes) were drawn. */
		int32 Circles = 0;

		/** The least, over every circle, of its centre's distance from the caster less its radius. */
		float NearestCircleEdgeCm = TNumericLimits<float>::Max();
	};

	/** The radius of a character's body as it stands, or -1 with no capsule. */
	static float LobTestScaledRadiusOf(const ACataclysmCharacterBase* Who)
	{
		const UCapsuleComponent* Capsule = Who ? Who->GetCapsuleComponent() : nullptr;
		return Capsule ? Capsule->GetScaledCapsuleRadius() : -1.0f;
	}

	/** The radius of a character's body as designed, before any scale, or -1 with no capsule. */
	static float LobTestUnscaledRadiusOf(const ACataclysmCharacterBase* Who)
	{
		const UCapsuleComponent* Capsule = Who ? Who->GetCapsuleComponent() : nullptr;
		return Capsule ? Capsule->GetUnscaledCapsuleRadius() : -1.0f;
	}

	/** Put an actor at a distance along +X from `From`, at its own height. */
	static void PlaceLobTestTarget(AActor* Who, const AActor* From, float DistanceCm)
	{
		const FVector Origin = From->GetActorLocation();
		Who->SetActorLocation(FVector(Origin.X + DistanceCm, Origin.Y, Who->GetActorLocation().Z));
	}

	/** Count the circles drawn in the world and how near the nearest edge of any comes to the caster's centre. */
	static void ReadLobTestCircles(UWorld* World, const AActor* Caster, FLobTestSeen& Seen)
	{
		for (TActorIterator<ACataclysmTelegraphMarker> It(World); It; ++It)
		{
			if (!IsValid(*It) || It->IsLane())
			{
				continue;
			}
			++Seen.Circles;
			const float Edge = static_cast<float>(FVector::Dist2D(It->GetActorLocation(), Caster->GetActorLocation()))
				- It->RadiusCm;
			Seen.NearestCircleEdgeCm = FMath::Min(Seen.NearestCircleEdgeCm, Edge);
		}
	}

	/**
	 * One scene in a world of its own: a creature of the given class at the given rung, a target where `Where` says,
	 * the ability `SpendFirst` spent beforehand when it is not INDEX_NONE, and one thinking pass. Reports what the
	 * brain began and the circles it drew. Set-up failures are reported to `Test` and leave `bReady` false.
	 */
	template <typename TCreature>
	FLobTestSeen LookAtTheLob(FAutomationTestBase& Test, const TCHAR* Name, int32 Rung, int32 LobIndex,
		int32 SpendFirst, ELobTestWhere Where)
	{
		FLobTestSeen Seen;

		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Test.TestNotNull(TEXT("set-up: a world"), World))
		{
			return Seen;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TCreature* Caster =
			World->SpawnActor<TCreature>(TCreature::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		ACataclysmEnemyCharacter* Target = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), FVector(0.0f, 50.0f * LobTestMetre, 0.0f), FRotator::ZeroRotator,
			Params);
		ACataclysmEnemyController* Brain =
			Caster ? Cast<ACataclysmEnemyController>(Caster->GetController()) : nullptr;
		if (!Test.TestNotNull(*FString::Printf(TEXT("set-up: a %s"), Name), Caster)
			|| !Test.TestNotNull(TEXT("set-up: something for it to attack"), Target)
			|| !Test.TestNotNull(TEXT("set-up: with a brain"), Brain))
		{
			return Seen;
		}
		Target->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Target->SetHealth(100000.0f);
		Target->SetAttackDamage(0.0f);

		// THE RUNG, AND THE BODY IT GIVES, STATED BEFORE ANYTHING IS PLACED.
		Caster->SetRarityStep(Rung);
		const float Scale = UCataclysmEnemyRarity::BodyScaleForStep(UCataclysmEnemyRarity::LoadEnemyRarityTable(), Rung);
		const float Unscaled = LobTestUnscaledRadiusOf(Caster);
		Seen.CasterRadiusCm = LobTestScaledRadiusOf(Caster);
		const TArray<FCataclysmEnemyAbility> Abilities = Caster->EnemyAbilities();
		if (!Test.TestEqual(TEXT("set-up: the rung is what was asked for"), Caster->RarityStep, Rung)
			|| !Test.TestTrue(TEXT("set-up: the rung makes the body bigger"), Scale > 1.0f)
			|| !Test.TestEqual(TEXT("set-up: the body as it stands is the designed body at the rung's scale"),
				Seen.CasterRadiusCm, Unscaled * Scale, 0.01f)
			|| !Test.TestEqual(TEXT("set-up: it is in its first phase"), Caster->CurrentPhase(), 1)
			|| !Test.TestTrue(TEXT("set-up: the lob is one of its abilities"), Abilities.IsValidIndex(LobIndex))
			|| !Test.TestTrue(TEXT("set-up: and it arcs onto where it is aimed, so it marks a circle there"),
				Abilities[LobIndex].bArcsOntoItsTarget)
			|| !Test.TestTrue(TEXT("set-up: and the circle is big enough to be drawn"),
				Abilities[LobIndex].MarkerRadiusCm >= ACataclysmTelegraphMarker::SmallestUsefulRadiusCm))
		{
			return Seen;
		}

		// THE DISTANCE, FROM THE MARKED RADIUS AND THE CAPSULE, never from the minimum under test.
		const float Marked = Abilities[LobIndex].MarkerRadiusCm;
		Seen.DistanceCm = Where == ELobTestWhere::InsideTheGrownBody
			? Marked + Unscaled + (Seen.CasterRadiusCm - Unscaled) / 2.0f
			: Marked + Seen.CasterRadiusCm + LobTestControlMarginCm;
		PlaceLobTestTarget(Target, Caster, Seen.DistanceCm);

		// THE ABILITY AHEAD OF THE LOB, SPENT, so the lob is what the pass under test is asked about.
		if (SpendFirst != INDEX_NONE)
		{
			if (!Test.TestTrue(TEXT("set-up: the ability ahead of the lob is one of its abilities"),
					Abilities.IsValidIndex(SpendFirst)))
			{
				return Seen;
			}
			Brain->Think();
			if (!Test.TestEqual(TEXT("set-up: the first pass begins the ability ahead of the lob"),
					Brain->WindingUpAbility, SpendFirst))
			{
				return Seen;
			}
			World->TimeSeconds += Abilities[SpendFirst].WindUpSeconds + 0.1;
			Brain->Think();
			World->TimeSeconds += Caster->SecondsBetweenAttacks() + 0.05;
			PlaceLobTestTarget(Target, Caster, Seen.DistanceCm);
			if (!Test.TestEqual(TEXT("set-up: and it has landed"), Brain->WindingUpAbility,
					static_cast<int32>(INDEX_NONE))
				|| !Test.TestFalse(TEXT("set-up: and it is cooling down"), Brain->IsAbilityReady(SpendFirst))
				|| !Test.TestTrue(TEXT("set-up: and the lob is ready"), Brain->IsAbilityReady(LobIndex)))
			{
				return Seen;
			}
		}

		if (!Test.TestEqual(TEXT("set-up: the target stands where it was put"),
				static_cast<float>(FVector::Dist2D(Caster->GetActorLocation(), Target->GetActorLocation())),
				Seen.DistanceCm, 0.01f))
		{
			return Seen;
		}

		Brain->Think();
		Seen.Winding = Brain->WindingUpAbility;
		ReadLobTestCircles(World, Caster, Seen);
		Seen.bReady = true;
		return Seen;
	}

	/**
	 * The whole of one creature at one rung: the CASE, inside the grown body, where it must not begin the lob and no
	 * circle it draws may cover its body; and the CONTROL, just clear of it, where it begins the lob and the circle
	 * clears its body.
	 */
	template <typename TCreature>
	bool CheckTheLobNeverMarksItsOwnBody(FAutomationTestBase& Test, const TCHAR* Name, const TCHAR* Lob, int32 Rung,
		int32 LobIndex, int32 SpendFirst)
	{
		const FLobTestSeen Inside =
			LookAtTheLob<TCreature>(Test, Name, Rung, LobIndex, SpendFirst, ELobTestWhere::InsideTheGrownBody);
		if (Inside.bReady)
		{
			Test.TestTrue(*FString::Printf(TEXT("a %s at rung %d does not begin %s at a target inside its marked "
												 "circle plus its body as it stands"), Name, Rung, Lob),
				Inside.Winding != LobIndex);
			Test.TestTrue(*FString::Printf(TEXT("and no circle it marks covers its own body (nearest edge %.2f cm from "
												 "its centre, its body %.2f cm)"),
					Inside.Circles > 0 ? Inside.NearestCircleEdgeCm : -1.0f, Inside.CasterRadiusCm),
				Inside.Circles == 0 || Inside.NearestCircleEdgeCm >= Inside.CasterRadiusCm - 0.01f);
		}

		const FLobTestSeen Clear =
			LookAtTheLob<TCreature>(Test, Name, Rung, LobIndex, SpendFirst, ELobTestWhere::JustClearOfTheGrownBody);
		if (Clear.bReady)
		{
			Test.TestEqual(*FString::Printf(TEXT("CONTROL: 2 cm beyond its marked circle plus its body as it stands, "
												  "the %s begins %s"), Name, Lob),
				Clear.Winding, LobIndex);
			Test.TestEqual(TEXT("CONTROL: and marks one circle"), Clear.Circles, 1);
			Test.TestTrue(*FString::Printf(TEXT("CONTROL: and the circle clears its body (nearest edge %.2f cm from its "
												 "centre, its body %.2f cm)"),
					Clear.Circles > 0 ? Clear.NearestCircleEdgeCm : -1.0f, Clear.CasterRadiusCm),
				Clear.Circles > 0 && Clear.NearestCircleEdgeCm >= Clear.CasterRadiusCm - 0.01f);
		}
		return Inside.bReady && Clear.bReady;
	}

	/**
	 * One creature's lob, rung by rung, against the ruling: its minimum is its marked radius plus its body as it
	 * stands, its maximum the designed one and still beyond the minimum, and at the Common rung the minimum is exactly
	 * the designed figure. `Elite` and `Boss` are the expected minimums at the Elite and Cataclysm Boss rungs.
	 */
	static void CheckLobTestFigures(FAutomationTestBase& Test, ACataclysmEnemyCharacter* Caster, const TCHAR* Name,
		int32 LobIndex, float DesignedMinimumCm, float DesignedMaximumCm, float Elite, float Boss)
	{
		const int32 BossRung = ACataclysmDungeonGameMode::CataclysmBossRung;

		// CONTROL: COMMON, EXACTLY AS BEFORE, to the last bit.
		Caster->SetRarityStep(0);
		const TArray<FCataclysmEnemyAbility> AtCommon = Caster->EnemyAbilities();
		if (!Test.TestTrue(*FString::Printf(TEXT("set-up: the %s's lob is one of its abilities"), Name),
				AtCommon.IsValidIndex(LobIndex)))
		{
			return;
		}
		Test.TestEqual(*FString::Printf(TEXT("set-up: a Common %s's body is its designed one"), Name),
			LobTestScaledRadiusOf(Caster), LobTestUnscaledRadiusOf(Caster), 0.0f);
		Test.TestEqual(*FString::Printf(TEXT("CONTROL: a Common %s's minimum is exactly the designed %.0f cm"), Name,
				DesignedMinimumCm),
			AtCommon[LobIndex].MinRangeCm, DesignedMinimumCm, 0.0f);

		for (int32 Rung = 0; Rung <= BossRung; ++Rung)
		{
			Caster->SetRarityStep(Rung);
			const TArray<FCataclysmEnemyAbility> Abilities = Caster->EnemyAbilities();
			const FCataclysmEnemyAbility& Lob = Abilities[LobIndex];
			const float Body = LobTestScaledRadiusOf(Caster);

			// THE RULING, AND WITH IT THE GUARANTEE: at the minimum, the circle's nearest edge is the body's edge.
			Test.TestEqual(*FString::Printf(TEXT("a %s at rung %d: its minimum is its marked circle plus its body as "
												  "it stands"), Name, Rung),
				Lob.MinRangeCm, Lob.MarkerRadiusCm + Body, 0.01f);
			Test.TestEqual(*FString::Printf(TEXT("a %s at rung %d: its maximum is the designed one"), Name, Rung),
				Lob.MaxRangeCm, DesignedMaximumCm, 0.0f);
			Test.TestTrue(*FString::Printf(TEXT("a %s at rung %d: and still beyond its minimum"), Name, Rung),
				Lob.MaxRangeCm > Lob.MinRangeCm);
			if (Rung == 1)
			{
				Test.TestEqual(*FString::Printf(TEXT("an Elite %s's minimum is %.2f cm"), Name, Elite), Lob.MinRangeCm,
					Elite, 0.01f);
			}
			if (Rung == BossRung)
			{
				Test.TestEqual(*FString::Printf(TEXT("a Cataclysm Boss %s's minimum is %.2f cm"), Name, Boss),
					Lob.MinRangeCm, Boss, 0.01f);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// The figures
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLobMinimumFiguresTest,
	"Cataclysm.LobMinimum.EachLobsMinimumIsItsCirclePlusItsBodyAsItStandsAtEveryRung",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLobMinimumFiguresTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmLobMinimumTest;

	// EVERYTHING FAR APART, AND NOTHING THINKS: the figures are properties of each creature's own list.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmBruteCharacter* Brute = World->SpawnActor<ACataclysmBruteCharacter>(
		ACataclysmBruteCharacter::StaticClass(), FVector(0.0f, 20.0f * LobTestMetre, 0.0f), FRotator::ZeroRotator,
		Params);
	ACataclysmGatekeeperCharacter* Gatekeeper = World->SpawnActor<ACataclysmGatekeeperCharacter>(
		ACataclysmGatekeeperCharacter::StaticClass(), FVector(0.0f, 40.0f * LobTestMetre, 0.0f), FRotator::ZeroRotator,
		Params);
	ACataclysmCorruptedSentinelCharacter* Sentinel = World->SpawnActor<ACataclysmCorruptedSentinelCharacter>(
		ACataclysmCorruptedSentinelCharacter::StaticClass(), FVector(0.0f, 60.0f * LobTestMetre, 0.0f),
		FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("set-up: a Brute"), Brute) || !TestNotNull(TEXT("set-up: a Gatekeeper"), Gatekeeper)
		|| !TestNotNull(TEXT("set-up: a Corrupted Sentinel"), Sentinel))
	{
		return false;
	}

	// THE SCALES, STATED: the Elite and Cataclysm Boss rungs' `BodyScale`, which the figures below are built from
	// (48 x 1.2 = 57.6; 48 x 2.48832 = 119.44).
	const UDataTable* Rarities = UCataclysmEnemyRarity::LoadEnemyRarityTable();
	TestEqual(TEXT("set-up: the Elite rung scales a body by 1.2"),
		UCataclysmEnemyRarity::BodyScaleForStep(Rarities, 1), 1.2f, 0.0001f);
	TestEqual(TEXT("set-up: the Cataclysm Boss rung by 2.48832"),
		UCataclysmEnemyRarity::BodyScaleForStep(Rarities, ACataclysmDungeonGameMode::CataclysmBossRung), 2.48832f,
		0.0001f);

	CheckLobTestFigures(*this, Brute, TEXT("Brute"), ACataclysmBruteCharacter::RockThrowAbility,
		ACataclysmBruteCharacter::RockThrowMinimumRangeCm, ACataclysmBruteCharacter::RockThrowRangeCm, 267.6f,
		329.44f);
	CheckLobTestFigures(*this, Gatekeeper, TEXT("Gatekeeper"), ACataclysmGatekeeperCharacter::SoulfallAbility,
		ACataclysmGatekeeperCharacter::SoulfallMinimumRangeCm, ACataclysmGatekeeperCharacter::SoulfallRangeCm, 357.6f,
		419.44f);
	CheckLobTestFigures(*this, Sentinel, TEXT("Corrupted Sentinel"),
		ACataclysmCorruptedSentinelCharacter::BrimstoneMortarAbility,
		ACataclysmCorruptedSentinelCharacter::BrimstoneMortarMinimumRangeCm,
		ACataclysmCorruptedSentinelCharacter::BrimstoneMortarRangeCm, 357.6f, 419.44f);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// The circle, as the brain draws it
// ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLobMinimumEliteBruteTest,
	"Cataclysm.LobMinimum.AnEliteBruteNeverMarksItsRockOverItsOwnBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLobMinimumEliteBruteTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmLobMinimumTest;
	return CheckTheLobNeverMarksItsOwnBody<ACataclysmBruteCharacter>(*this, TEXT("Brute"), TEXT("Rip and Toss"), 1,
		ACataclysmBruteCharacter::RockThrowAbility, ACataclysmBruteCharacter::StompAbility);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLobMinimumBossBruteTest,
	"Cataclysm.LobMinimum.ACataclysmBossBruteNeverMarksItsRockOverItsOwnBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLobMinimumBossBruteTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmLobMinimumTest;
	return CheckTheLobNeverMarksItsOwnBody<ACataclysmBruteCharacter>(*this, TEXT("Brute"), TEXT("Rip and Toss"),
		ACataclysmDungeonGameMode::CataclysmBossRung, ACataclysmBruteCharacter::RockThrowAbility,
		ACataclysmBruteCharacter::StompAbility);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLobMinimumEliteGatekeeperTest,
	"Cataclysm.LobMinimum.AnEliteGatekeeperNeverMarksSoulfallOverItsOwnBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLobMinimumEliteGatekeeperTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmLobMinimumTest;

	// NOTHING TO SPEND: in phase 1 Soul Harvest (phase 3) and Call the Damned (phase 2) are skipped, Soulfall comes
	// before Dread Cleave, and Dread Cleave reaches only 200 cm.
	return CheckTheLobNeverMarksItsOwnBody<ACataclysmGatekeeperCharacter>(*this, TEXT("Gatekeeper"), TEXT("Soulfall"),
		1, ACataclysmGatekeeperCharacter::SoulfallAbility, INDEX_NONE);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLobMinimumBossGatekeeperTest,
	"Cataclysm.LobMinimum.ACataclysmBossGatekeeperNeverMarksSoulfallOverItsOwnBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLobMinimumBossGatekeeperTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmLobMinimumTest;
	return CheckTheLobNeverMarksItsOwnBody<ACataclysmGatekeeperCharacter>(*this, TEXT("Gatekeeper"), TEXT("Soulfall"),
		ACataclysmDungeonGameMode::CataclysmBossRung, ACataclysmGatekeeperCharacter::SoulfallAbility, INDEX_NONE);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLobMinimumEliteSentinelTest,
	"Cataclysm.LobMinimum.AnEliteSentinelNeverMarksItsMortarOverItsOwnBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLobMinimumEliteSentinelTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmLobMinimumTest;

	// NOTHING TO SPEND: Brimstone Mortar comes first in its list. Refused, it falls to Siege Bolt, whose lane is not a
	// circle.
	return CheckTheLobNeverMarksItsOwnBody<ACataclysmCorruptedSentinelCharacter>(*this, TEXT("Corrupted Sentinel"),
		TEXT("Brimstone Mortar"), 1, ACataclysmCorruptedSentinelCharacter::BrimstoneMortarAbility, INDEX_NONE);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLobMinimumBossSentinelTest,
	"Cataclysm.LobMinimum.ACataclysmBossSentinelNeverMarksItsMortarOverItsOwnBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLobMinimumBossSentinelTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmLobMinimumTest;
	return CheckTheLobNeverMarksItsOwnBody<ACataclysmCorruptedSentinelCharacter>(*this, TEXT("Corrupted Sentinel"),
		TEXT("Brimstone Mortar"), ACataclysmDungeonGameMode::CataclysmBossRung,
		ACataclysmCorruptedSentinelCharacter::BrimstoneMortarAbility, INDEX_NONE);
}

#endif // WITH_AUTOMATION_TESTS
