// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Dungeon/CataclysmFloorContents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Reading a floor into a record and putting it back. Issue #751.
 *
 * WHAT THIS IS FOR. The project owner set the rule on 2026-08-20: the game saves
 * itself so that a Hardcore character cannot leave a losing boss fight by
 * closing the game. `docs/Save_System_Design.md` section 6 turns that into a
 * table -- a boss keeps every point taken off it, nothing resumes mid-blow --
 * and everything below is one row of that table.
 *
 * THE ONE PROPERTY THAT MATTERS MOST is that damage survives the round trip. A
 * restore that put every creature back at full health would look like it worked,
 * would pass a test that only counted creatures, and would hand the player back
 * exactly the escape this feature closes.
 */
namespace CataclysmSaveFloorTest
{
	/** An undressed creature, which is what the base class spawns as. */
	static ACataclysmEnemyCharacter* SpawnCreature(UWorld* World, const FVector& At,
												   float MaxHealth, int32 RarityStep)
	{
		ACataclysmEnemyCharacter* Creature =
			World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (!Creature)
		{
			return nullptr;
		}

		Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Creature->SetHealth(MaxHealth);
		Creature->SetRarityStep(RarityStep);
		return Creature;
	}

	/** Write a creature's current health without going through a hit. */
	static bool WoundTo(AActor* Actor, float Health)
	{
		UAbilitySystemComponent* AbilitySystem = const_cast<UAbilitySystemComponent*>(
			UCataclysmTargeting::AbilitySystemOf(Actor));
		if (!AbilitySystem)
		{
			return false;
		}

		const FGameplayAttribute HealthAttribute =
			UCataclysmVitalAttributeSet::GetHealthAttribute();
		if (!AbilitySystem->HasAttributeSetForAttribute(HealthAttribute))
		{
			return false;
		}

		AbilitySystem->SetNumericAttributeBase(HealthAttribute, Health);
		return true;
	}

	static float MaxHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		return AbilitySystem
			? AbilitySystem->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute())
			: 0.0f;
	}

	static float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		return AbilitySystem
			? AbilitySystem->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealthAttribute())
			: 0.0f;
	}

	static int32 CountCreatures(UWorld* World)
	{
		int32 Found = 0;
		for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++Found;
			}
		}
		return Found;
	}

	static int32 CountDrops(UWorld* World)
	{
		int32 Found = 0;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++Found;
			}
		}
		return Found;
	}
}

/**
 * A creature is read with everything the design says a restore needs.
 *
 * WHICH IS THE LEFT-HAND COLUMN OF SECTION 6'S TABLE and nothing else: where it
 * stands, which way it faces, what it is, its rung on the rarity ladder, the
 * modifiers it carries, and the health it has.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveGatherReadsACreature,
	"Cataclysm.SaveGather.ACreatureIsReadWithItsPlaceRarityModifiersAndHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveGatherReadsACreature::RunTest(const FString&)
{
	using namespace CataclysmSaveFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to gather from"), World))
	{
		return false;
	}

	const FVector Where(250.0f, -125.0f, 90.0f);
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World, Where, 800.0f, /*Rarity=*/4);
	if (!Creature)
	{
		AddError(TEXT("no creature to read"));
		World->DestroyWorld(false);
		return false;
	}

	Creature->SetActorRotation(FRotator(0.0f, 135.0f, 0.0f));
	Creature->ModifierRows = { FName(TEXT("Demonic_Hellfire_Aura")) };
	WoundTo(Creature, 200.0f);

	const FCataclysmSavedCreature Saved = FCataclysmSaveGather::CreatureFrom(*Creature);

	TestEqual(TEXT("its rung on the rarity ladder"), Saved.RarityStep, 4);
	TestEqual(TEXT("its archetype"), Saved.ArchetypeRow, Creature->ArchetypeRow);
	TestEqual(TEXT("how many modifiers it carries"), Saved.ModifierRows.Num(), 1);
	TestEqual(TEXT("and which one"), Saved.ModifierRows[0],
		FName(TEXT("Demonic_Hellfire_Aura")));
	TestEqual(TEXT("where it was standing"), Saved.Location, Where);
	TestEqual(TEXT("which way it faced"), Saved.Yaw, 135.0f);

	// THE POINT OF THE WHOLE RECORD. Not its maximum health, which the creature
	// would work out again for itself, but the health it had left.
	TestEqual(TEXT("the health it had left, not the health it started with"),
		Saved.Health, 200.0f);

	World->DestroyWorld(false);
	return true;
}

/**
 * A creature that is already dead is not written into the record.
 *
 * A CORPSE IS AN ACTOR FOR AS LONG AS ITS DEATH CLIP RUNS -- up to four seconds,
 * see `UCataclysmEnemyDeath::LongestCorpseSeconds` -- so a floor gathered a
 * moment after a kill still has one standing in it. Writing it down would put it
 * back on its feet when the floor was restored, which is this feature running
 * backwards: the player would kill a boss, quit, and find it alive.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveGatherSkipsTheDead,
	"Cataclysm.SaveGather.ACreatureAlreadyDeadIsNotWrittenIntoTheRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveGatherSkipsTheDead::RunTest(const FString&)
{
	using namespace CataclysmSaveFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to gather from"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Living =
		SpawnCreature(World, FVector(100.0f, 0.0f, 90.0f), 500.0f, 0);
	ACataclysmEnemyCharacter* Dying =
		SpawnCreature(World, FVector(200.0f, 0.0f, 90.0f), 500.0f, 0);

	if (!Living || !Dying)
	{
		AddError(TEXT("two creatures were needed and were not spawned"));
		World->DestroyWorld(false);
		return false;
	}

	UCataclysmSkillEffects::MarkDead(Dying);

	// BOTH ARE STILL IN THE WORLD. If the corpse had already gone the check
	// below would pass for the wrong reason.
	TestEqual(TEXT("both creatures are still actors in the world"),
		CountCreatures(World), 2);

	const FCataclysmSavedFloor Floor = FCataclysmSaveGather::FloorFrom(
		*World, FName(TEXT("Sandbox")), /*Floor=*/1, FGuid::NewGuid());

	TestEqual(TEXT("only the living one reached the record"), Floor.Creatures.Num(), 1);
	if (Floor.Creatures.Num() == 1)
	{
		TestEqual(TEXT("and it is the one that is still standing"),
			Floor.Creatures[0].Location, Living->GetActorLocation());
	}

	World->DestroyWorld(false);
	return true;
}

/**
 * Every creature class claims its own archetype, and a name nobody claims is
 * refused rather than substituted.
 *
 * WHY A NAME NOBODY CLAIMS MATTERS. A creature removed from the game leaves its
 * name in every save file that ever held one. Substituting a different creature
 * would put a monster in front of the player that was never there; leaving it
 * out is the honest answer and is what the restore does.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveArchetypesMapToClasses,
	"Cataclysm.SaveApply.EveryCreatureClassClaimsItsOwnArchetypeAndAStrangerIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveArchetypesMapToClasses::RunTest(const FString&)
{
	const TArray<FName> Known = FCataclysmSaveApply::KnownArchetypes();

	if (!TestTrue(TEXT("at least one creature class was found at all"), Known.Num() > 0))
	{
		return false;
	}

	// EVERY NAME MAPS BACK TO A CLASS THAT CLAIMS IT. A map built by asking each
	// class what it is cannot drift from the classes it maps, and this is what
	// says so rather than assuming it.
	for (const FName Archetype : Known)
	{
		const TSubclassOf<ACataclysmEnemyCharacter> Class =
			FCataclysmSaveApply::ClassForArchetype(Archetype);
		if (!Class)
		{
			AddError(FString::Printf(TEXT("'%s' is listed as known and maps to nothing"),
				*Archetype.ToString()));
			return false;
		}

		const ACataclysmEnemyCharacter* Default =
			Class.GetDefaultObject();
		if (!Default || Default->ArchetypeRow != Archetype)
		{
			AddError(FString::Printf(
				TEXT("'%s' maps to %s, which calls itself '%s'"),
				*Archetype.ToString(), *Class->GetName(),
				Default ? *Default->ArchetypeRow.ToString() : TEXT("nothing")));
			return false;
		}
	}

	// THE TWO CREATURES THAT HAVE ART ARE THE TWO THIS PROJECT ACTUALLY FIGHTS,
	// so they are named rather than left to the sweep above, which would pass
	// with both of them missing.
	TestTrue(TEXT("the Brute is reachable by name"),
		FCataclysmSaveApply::ClassForArchetype(FName(TEXT("Brute"))) != nullptr);
	TestTrue(TEXT("the Abyssal Warden is reachable by name"),
		FCataclysmSaveApply::ClassForArchetype(FName(TEXT("Abyssal_Warden"))) != nullptr);

	TestTrue(TEXT("a name no class claims maps to nothing"),
		FCataclysmSaveApply::ClassForArchetype(
			FName(TEXT("A_Creature_That_Was_Deleted"))) == nullptr);

	return true;
}

/**
 * A creature comes back with the damage that had been done to it.
 *
 * **THIS IS THE TEST THE WHOLE FEATURE EXISTS FOR.** The project owner's rule of
 * 2026-08-20 is that a Hardcore character cannot leave a losing boss fight by
 * closing the game, and section 6 turns it into one sentence: "A boss keeps
 * every point taken off it." A restore that put the creature back at full health
 * would count the same, look the same, and give the escape straight back.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveCreatureKeepsItsDamage,
	"Cataclysm.SaveApply.ACreatureComesBackWithTheDamageItHadTakenKept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveCreatureKeepsItsDamage::RunTest(const FString&)
{
	using namespace CataclysmSaveFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	constexpr float DesignedHealth = 1000.0f;
	constexpr float WhatWasLeft = 137.5f;

	ACataclysmEnemyCharacter* Boss =
		SpawnCreature(World, FVector(400.0f, 300.0f, 90.0f), DesignedHealth,
					  /*Rarity=*/4);
	if (!Boss || !WoundTo(Boss, WhatWasLeft))
	{
		AddError(TEXT("a wounded boss was needed and could not be made"));
		World->DestroyWorld(false);
		return false;
	}

	// READ OFF THE CREATURE RATHER THAN ASSUMED FROM WHAT IT WAS GIVEN, and
	// since issue #848 those are two different numbers. A spawner passes the
	// design model's COMMON figure and the creature's rarity multiplies it, so a
	// Boss given 1000 stands at about 11,700. What this test is about is that a
	// save and a restore give back what the creature ACTUALLY had, so comparing
	// against the designed input would be testing the wrong thing -- and it
	// would have to be updated again every time the rarity table is re-tuned.
	const float FullHealth = MaxHealthOf(Boss);
	if (!TestTrue(TEXT("a Boss is scaled above the figure it was designed from"),
			FullHealth > DesignedHealth))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FCataclysmSavedFloor Saved = FCataclysmSaveGather::FloorFrom(
		*World, FName(TEXT("Sandbox")), 1, FGuid::NewGuid());

	const FCataclysmFloorRestored Restored = FCataclysmSaveApply::FloorInto(*World, Saved);

	TestEqual(TEXT("the boss that was there was removed first"), Restored.Removed, 1);
	TestEqual(TEXT("and one creature was put back"), Restored.Creatures, 1);
	TestEqual(TEXT("nothing was refused"), Restored.Refused, 0);
	TestEqual(TEXT("there is one creature in the world"), CountCreatures(World), 1);

	ACataclysmEnemyCharacter* Back = nullptr;
	for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			Back = *It;
			break;
		}
	}

	if (!TestNotNull(TEXT("the restored boss"), Back))
	{
		World->DestroyWorld(false);
		return false;
	}

	TestEqual(TEXT("it kept every point taken off it"), HealthOf(Back), WhatWasLeft);

	// AND IT KNOWS WHAT FULL WOULD BE. Without the maximum coming back too, the
	// vital attribute set clamps the health above down to whatever the class
	// defaults to, and the damage is silently undone. That is exactly what this
	// test caught before FCataclysmSavedCreature carried a maximum.
	TestEqual(TEXT("and it knows what full health would be"),
		MaxHealthOf(Back), FullHealth);
	TestEqual(TEXT("it is still a boss"), Back->RarityStep, 4);
	TestEqual(TEXT("it is standing where it was"), Back->GetActorLocation(),
		FVector(400.0f, 300.0f, 90.0f));

	// AND IT IS NOT THE SAME ACTOR. The floor was cleared and rebuilt, so a test
	// reading the health off the original object would pass whatever the restore
	// did.
	TestTrue(TEXT("it is a new actor rather than the one that was there"),
		Back != Boss);

	World->DestroyWorld(false);
	return true;
}

/**
 * A record holding a creature with no health left is refused.
 *
 * THE GATHER NEVER WRITES ONE, so a record holding one came from a file
 * somebody edited. Spawning it would put a corpse on the floor that dies again
 * on its first tick, which is a worse answer than leaving it out.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRefusesACreatureWithNoHealth,
	"Cataclysm.SaveApply.ARecordHoldingACreatureWithNoHealthLeftIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveRefusesACreatureWithNoHealth::RunTest(const FString&)
{
	using namespace CataclysmSaveFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	FCataclysmSavedFloor Floor;
	Floor.Dungeon = FName(TEXT("Sandbox"));
	Floor.Floor = 1;

	FCataclysmSavedCreature Corpse;
	Corpse.ArchetypeRow = NAME_None;
	Corpse.Location = FVector(100.0f, 0.0f, 90.0f);
	Corpse.Health = 0.0f;
	Floor.Creatures.Add(Corpse);

	FCataclysmSavedCreature Alive = Corpse;
	Alive.Location = FVector(200.0f, 0.0f, 90.0f);
	Alive.Health = 50.0f;
	Floor.Creatures.Add(Alive);

	// THE REFUSAL IS LOGGED AS A WARNING RATHER THAN AN ERROR, so it does not
	// have to be declared here. A warning does not fail an automation test:
	// `FAutomationTestBase::bElevateLogWarningsToErrors` is false.

	const FCataclysmFloorRestored Restored = FCataclysmSaveApply::FloorInto(*World, Floor);

	TestEqual(TEXT("the living one was put back"), Restored.Creatures, 1);
	TestEqual(TEXT("and the one with no health left was refused"), Restored.Refused, 1);
	TestEqual(TEXT("so one creature is on the floor"), CountCreatures(World), 1);
	TestFalse(TEXT("and the restore reports that it was not everything"),
		Restored.IsEverything(Floor));

	World->DestroyWorld(false);
	return true;
}

/**
 * Clearing the floor takes everything section 6 says is not restored.
 *
 * NOT ONLY THE CREATURES. Section 6's right-hand column names projectiles and
 * ground effects already in flight, and any wind-up in progress -- which is what
 * a telegraph marker draws. A projectile left flying across a floor that has
 * just been rebuilt is worse than one that never existed, because it arrives
 * from a fight that is no longer happening.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveClearingTakesEverythingNotRestored,
	"Cataclysm.SaveApply.ClearingTheFloorTakesEverythingSectionSixDoesNotRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveClearingTakesEverythingNotRestored::RunTest(const FString&)
{
	using namespace CataclysmSaveFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	// COUNTED RATHER THAN ASSUMED. A spawn the world refuses would otherwise
	// make the total below wrong for a reason that has nothing to do with
	// clearing, and the failure would point at the wrong thing.
	int32 Placed = 0;
	Placed += SpawnCreature(World, FVector(100.0f, 0.0f, 90.0f), 100.0f, 0) ? 1 : 0;
	Placed += World->SpawnActor<ACataclysmDroppedItem>(
		FVector(150.0f, 0.0f, 90.0f), FRotator::ZeroRotator) ? 1 : 0;
	Placed += World->SpawnActor<ACataclysmProjectile>(
		FVector(200.0f, 0.0f, 120.0f), FRotator::ZeroRotator) ? 1 : 0;
	Placed += World->SpawnActor<ACataclysmGroundZone>(
		FVector(250.0f, 0.0f, 90.0f), FRotator::ZeroRotator) ? 1 : 0;
	Placed += World->SpawnActor<ACataclysmTelegraphMarker>(
		FVector(300.0f, 0.0f, 90.0f), FRotator::ZeroRotator) ? 1 : 0;

	if (!TestEqual(TEXT("one of each of the five kinds was placed"), Placed, 5))
	{
		World->DestroyWorld(false);
		return false;
	}

	const int32 Cleared = UCataclysmFloorContents::ClearTheFloor(*World);

	TestEqual(TEXT("all five were taken"), Cleared, Placed);
	TestEqual(TEXT("no creature is left"), CountCreatures(World), 0);
	TestEqual(TEXT("no drop is left"), CountDrops(World), 0);

	int32 InFlight = 0;
	for (TActorIterator<ACataclysmProjectile> It(World); It; ++It)
	{
		InFlight += IsValid(*It) ? 1 : 0;
	}
	for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
	{
		InFlight += IsValid(*It) ? 1 : 0;
	}
	for (TActorIterator<ACataclysmTelegraphMarker> It(World); It; ++It)
	{
		InFlight += IsValid(*It) ? 1 : 0;
	}

	TestEqual(TEXT("nothing is left in flight, on the ground, or telegraphed"),
		InFlight, 0);

	World->DestroyWorld(false);
	return true;
}

/**
 * A floor read into a record and put back is the same floor.
 *
 * THE WHOLE ROUND TRIP IN ONE TEST, and it compares records rather than actors:
 * gather, clear and rebuild, gather again, and the two records have to agree
 * creature for creature and drop for drop. That is stronger than counting,
 * because it catches a restore that puts back the right NUMBER of creatures with
 * the wrong health, the wrong rarity or in the wrong place.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveFloorRoundTrip,
	"Cataclysm.SaveApply.AFloorGatheredAndPutBackIsTheSameFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveFloorRoundTrip::RunTest(const FString&)
{
	using namespace CataclysmSaveFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	struct FPlanned
	{
		FVector Where;
		float MaxHealth;
		float Left;
		int32 Rarity;
	};

	const FPlanned Planned[] = {
		{ FVector(100.0f, 100.0f, 90.0f), 500.0f, 500.0f, 0 },
		{ FVector(-250.0f, 75.0f, 90.0f), 1000.0f, 62.5f, 4 },
		{ FVector(0.0f, -400.0f, 90.0f), 250.0f, 125.0f, 2 },
	};

	for (const FPlanned& One : Planned)
	{
		ACataclysmEnemyCharacter* Creature =
			SpawnCreature(World, One.Where, One.MaxHealth, One.Rarity);
		if (!Creature || !WoundTo(Creature, One.Left))
		{
			AddError(TEXT("the floor could not be set up"));
			World->DestroyWorld(false);
			return false;
		}
	}

	// AND SOME REAL DROPS, rolled the way a kill rolls them, so the drops in
	// this test are the shapes the game actually produces rather than ones
	// invented here.
	FRandomStream Stream(/*InSeed=*/20260820);
	const int32 Dropped = UCataclysmDropSpawner::SpawnDropsFor(
		World, /*EnemyRarityStep=*/5, /*MagicFind=*/0.0f,
		UCataclysmDropRoll::BaselineLootQuantity, FVector(50.0f, 50.0f, 90.0f),
		Stream);

	if (!TestTrue(TEXT("a Cataclysm Boss put something on the floor"), Dropped > 0))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FGuid CharacterId = FGuid::NewGuid();
	const FCataclysmSavedFloor Before = FCataclysmSaveGather::FloorFrom(
		*World, FName(TEXT("Hell_On_Earth")), /*Floor=*/6, CharacterId);

	TestEqual(TEXT("three creatures were read"), Before.Creatures.Num(), 3);
	TestEqual(TEXT("and every drop"), Before.GroundItems.Num(), Dropped);

	const FCataclysmFloorRestored Restored = FCataclysmSaveApply::FloorInto(*World, Before);
	TestTrue(TEXT("everything in the record was put back"), Restored.IsEverything(Before));

	const FCataclysmSavedFloor After = FCataclysmSaveGather::FloorFrom(
		*World, FName(TEXT("Hell_On_Earth")), /*Floor=*/6, CharacterId);

	if (!TestEqual(TEXT("the same number of creatures"),
				   After.Creatures.Num(), Before.Creatures.Num()))
	{
		World->DestroyWorld(false);
		return false;
	}

	// COMPARED BY WHERE THEY STAND rather than by the order they were iterated
	// in, because nothing promises a rebuilt floor is walked in the order it was
	// built.
	for (const FCataclysmSavedCreature& Was : Before.Creatures)
	{
		const FCataclysmSavedCreature* Now = After.Creatures.FindByPredicate(
			[&Was](const FCataclysmSavedCreature& Candidate)
			{
				return Candidate.Location.Equals(Was.Location, 1.0);
			});

		if (!Now)
		{
			AddError(FString::Printf(
				TEXT("nothing came back standing at %s"), *Was.Location.ToString()));
			World->DestroyWorld(false);
			return false;
		}

		TestEqual(FString::Printf(TEXT("the health of the creature at %s"),
					*Was.Location.ToString()), Now->Health, Was.Health);
		TestEqual(FString::Printf(TEXT("what full health is for the creature at %s"),
					*Was.Location.ToString()), Now->MaxHealth, Was.MaxHealth);
		TestEqual(FString::Printf(TEXT("the rarity of the creature at %s"),
					*Was.Location.ToString()), Now->RarityStep, Was.RarityStep);
		TestEqual(FString::Printf(TEXT("the archetype of the creature at %s"),
					*Was.Location.ToString()), Now->ArchetypeRow, Was.ArchetypeRow);
	}

	TestEqual(TEXT("the same number of drops"),
		After.GroundItems.Num(), Before.GroundItems.Num());

	World->DestroyWorld(false);
	return true;
}

/**
 * A drop put back off a record is described the same way the spawner describes
 * one.
 *
 * WHY THIS IS ITS OWN TEST. A drop's printed name, its colour, its rarity and a
 * material's tier are all worked out from what the drop is, and the record does
 * not hold any of them -- deliberately, because a persisted name would be the
 * name the item had before somebody renamed it in the design workbook. So there
 * are two pieces of code that describe a drop: the spawner, inline, with tables
 * it loaded once for a whole kill, and `ACataclysmDroppedItem::DescribeItself`.
 * **This is what keeps their answers together.**
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRestoredDropReadsTheSame,
	"Cataclysm.SaveApply.ARestoredDropIsDescribedTheSameWayASpawnedOneIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveRestoredDropReadsTheSame::RunTest(const FString&)
{
	using namespace CataclysmSaveFloorTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	FRandomStream Stream(/*InSeed=*/20260821);
	const int32 Dropped = UCataclysmDropSpawner::SpawnDropsFor(
		World, /*EnemyRarityStep=*/5, /*MagicFind=*/0.0f,
		UCataclysmDropRoll::BaselineLootQuantity, FVector::ZeroVector, Stream);

	if (!TestTrue(TEXT("a kill put something on the floor"), Dropped > 0))
	{
		World->DestroyWorld(false);
		return false;
	}

	// WHAT THE SPAWNER SAID ABOUT EACH DROP, keyed by where it landed.
	struct FDescription
	{
		FString Name;
		FLinearColor Colour;
		ECataclysmRarity Rarity;
		int32 MaterialTier;
	};

	TMap<FString, FDescription> BySpawner;
	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		const ACataclysmDroppedItem* Drop = *It;
		if (!IsValid(Drop))
		{
			continue;
		}
		BySpawner.Add(Drop->GetActorLocation().ToString(),
					  { Drop->DisplayName, Drop->NameColour, Drop->Rarity,
						Drop->MaterialTier });
	}

	const FCataclysmSavedFloor Saved = FCataclysmSaveGather::FloorFrom(
		*World, FName(TEXT("Sandbox")), 1, FGuid::NewGuid());
	FCataclysmSaveApply::FloorInto(*World, Saved);

	int32 Compared = 0;
	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		const ACataclysmDroppedItem* Drop = *It;
		if (!IsValid(Drop))
		{
			continue;
		}

		const FDescription* Was = BySpawner.Find(Drop->GetActorLocation().ToString());
		if (!Was)
		{
			AddError(FString::Printf(TEXT("a drop came back at %s where none was"),
				*Drop->GetActorLocation().ToString()));
			World->DestroyWorld(false);
			return false;
		}

		TestEqual(TEXT("the name a restored drop is given"), Drop->DisplayName, Was->Name);
		TestEqual(TEXT("the colour it is drawn in"), Drop->NameColour, Was->Colour);
		TestEqual(TEXT("the rarity its border is drawn for"),
			static_cast<int32>(Drop->Rarity), static_cast<int32>(Was->Rarity));
		TestEqual(TEXT("and a material's tier"), Drop->MaterialTier, Was->MaterialTier);
		++Compared;
	}

	// A NAME THAT IS EMPTY ON BOTH SIDES WOULD PASS EVERY CHECK ABOVE, so at
	// least one drop has to have been given a real name by both routes.
	TestEqual(TEXT("every drop was compared"), Compared, Dropped);

	bool bAnyNamed = false;
	for (const TPair<FString, FDescription>& Pair : BySpawner)
	{
		bAnyNamed |= !Pair.Value.Name.IsEmpty();
	}
	TestTrue(TEXT("at least one drop has a real name to compare"), bAnyNamed);

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
