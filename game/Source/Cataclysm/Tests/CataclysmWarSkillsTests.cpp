// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR WEAPON SKILLS (issue #2972), slice WS1 (2026-11-28): the five on-hit keys W1 to W5, Earthquake's row, and
 * Colossus's share. See the decisions entry "The War weapon skills, slice WS1".
 */
namespace CataclysmWarSkillsTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** A creature with a class resource pool of 100 holding `Held`. */
	struct FWarSkillsCreature
	{
		FWarSkillsCreature(UWorld* World, const FVector& At, float Held = 0.0f)
		{
			Body = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
			System = Body ? Cast<UCataclysmAbilitySystemComponent>(Body->GetAbilitySystemComponent()) : nullptr;
			if (System)
			{
				System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Body));
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												100.0f);
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Held);
				System->SetStatInputs({});
			}
		}
		bool IsUsable() const { return Body && System; }
		ACataclysmEnemyCharacter* Body = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A strike of the War skills' tests, its parameters read from `Text` as the generated table's cell is. */
	UCataclysmStrikeSkill* WarSkillsStrike(UObject* Outer, const FString& Text)
	{
		UCataclysmStrikeSkill* Strike = NewObject<UCataclysmStrikeSkill>(Outer);
		Strike->SkillName = TEXT("War Skills Test Strike");
		Strike->Params = UCataclysmSkillShapes::ParseParams(Text);
		return Strike;
	}
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarSkillsKeysParseTest,
	"Cataclysm.WarSkills.TheFiveOnHitKeysAreReadFromTheCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** W1 to W5 read from one cell, and each one's default when absent: chance 100, magnitude 1, cap 1, the rest nought. */
bool FCataclysmWarSkillsKeysParseTest::RunTest(const FString&)
{
	FString Error;
	const FCataclysmSkillShapeParams Stated = UCataclysmSkillShapes::ParseParams(
		TEXT("Radius=3; Ailment=Bleed; AilmentChance=30; AilmentMagnitude=2; ArmourRemoved=20; ArmourRemovedDuration=8; "
			 "ArmourRemovedCap=0; DamageTakenIncreased=40; DamageTakenIncreasedDuration=6; Stagger=0.5"),
		&Error);
	TestTrue(TEXT("the cell reads with no error"), Error.IsEmpty());
	TestEqual(TEXT("AilmentChance"), Stated.AilmentChancePercent, 30.0f);
	TestEqual(TEXT("AilmentMagnitude"), Stated.AilmentMagnitude, 2.0f);
	TestEqual(TEXT("ArmourRemoved"), Stated.ArmourRemovedPercent, 20.0f);
	TestEqual(TEXT("ArmourRemovedDuration"), Stated.ArmourRemovedSeconds, 8.0f);
	TestEqual(TEXT("ArmourRemovedCap"), Stated.ArmourRemovedCap, 0);
	TestEqual(TEXT("DamageTakenIncreased"), Stated.DamageTakenIncreasedPercent, 40.0f);
	TestEqual(TEXT("DamageTakenIncreasedDuration"), Stated.DamageTakenIncreasedSeconds, 6.0f);
	TestEqual(TEXT("Stagger"), Stated.StaggerSeconds, 0.5f);
	const FCataclysmSkillShapeParams Plain = UCataclysmSkillShapes::ParseParams(TEXT("Radius=3"));
	TestEqual(TEXT("control: no AilmentChance is always"), Plain.AilmentChancePercent, 100.0f);
	TestEqual(TEXT("control: no AilmentMagnitude is one"), Plain.AilmentMagnitude, 1.0f);
	TestEqual(TEXT("control: no ArmourRemovedCap is one stack"), Plain.ArmourRemovedCap, 1);
	TestEqual(TEXT("control: no Stagger is none"), Plain.StaggerSeconds, 0.0f);
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarSkillsMarksTest,
	"Cataclysm.WarSkills.ALandedBlowLaysTheArmourRemovedTheDamageTakenAndTheStagger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `ApplyStatedHitMarksTo`, on two targets: one struck by a strike stating none of the marks (the control), one by a strike
 * stating Earthquake's armour removed and stagger and Warlord's Decree's damage taken. Then a second blow of a capped
 * strike leaves one stack, and of an uncapped one (Annihilator's `ArmourRemovedCap=0`) two.
 */
bool FCataclysmWarSkillsMarksTest::RunTest(const FString&)
{
	using namespace CataclysmWarSkillsTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FWarSkillsCreature Player(World, FVector::ZeroVector);
	FWarSkillsCreature Plain(World, FVector(300.0f, 0.0f, 0.0f));
	FWarSkillsCreature Marked(World, FVector(-300.0f, 0.0f, 0.0f));
	FWarSkillsCreature Spun(World, FVector(0.0f, 300.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: four creatures"),
				  Player.IsUsable() && Plain.IsUsable() && Marked.IsUsable() && Spun.IsUsable()))
	{
		return false;
	}
	WarSkillsStrike(Player.Body, TEXT("Radius=3"))->ApplyStatedHitMarksTo(Player.Body, Plain.Body);
	TestEqual(TEXT("control: a strike stating no mark removes no armour"), Plain.System->ArmourRemovedPercentNow(), 0.0f);
	TestEqual(TEXT("control: and adds no damage taken"),
			  UCataclysmAbilitySystemComponent::PlacedDamageTakenPercentOn(Plain.System), 0.0f);
	TestFalse(TEXT("control: and staggers nothing"), UCataclysmSkillEffects::IsStaggered(Plain.Body));

	UCataclysmStrikeSkill* Quake = WarSkillsStrike(Player.Body,
		TEXT("Radius=6; ArmourRemoved=20; ArmourRemovedDuration=6; Stagger=0.5; DamageTakenIncreased=40; "
			 "DamageTakenIncreasedDuration=8"));
	Quake->ApplyStatedHitMarksTo(Player.Body, Marked.Body);
	TestEqual(TEXT("ArmourRemoved=20 removes 20% of its armour"), Marked.System->ArmourRemovedPercentNow(), 20.0f, 0.001f);
	TestEqual(TEXT("DamageTakenIncreased=40 adds 40% damage taken"),
			  UCataclysmAbilitySystemComponent::PlacedDamageTakenPercentOn(Marked.System), 40.0f, 0.001f);
	TestTrue(TEXT("Stagger=0.5 leaves it Staggered"), UCataclysmSkillEffects::IsStaggered(Marked.Body));
	Quake->ApplyStatedHitMarksTo(Player.Body, Marked.Body);
	TestEqual(TEXT("a second blow of the same strike holds one stack, 20%"), Marked.System->ArmourRemovedPercentNow(),
			  20.0f, 0.001f);

	UCataclysmStrikeSkill* Annihilator =
		WarSkillsStrike(Player.Body, TEXT("Radius=3.5; ArmourRemoved=5; ArmourRemovedDuration=6; ArmourRemovedCap=0"));
	Annihilator->SkillName = TEXT("War Skills Test Spin");
	Annihilator->ApplyStatedHitMarksTo(Player.Body, Spun.Body);
	Annihilator->ApplyStatedHitMarksTo(Player.Body, Spun.Body);
	TestEqual(TEXT("ArmourRemovedCap=0: two revolutions remove 10%"), Spun.System->ArmourRemovedPercentNow(), 10.0f,
			  0.001f);
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarSkillsAilmentChanceTest,
	"Cataclysm.WarSkills.AnAilmentChanceRollsAndARollAboveItLaysNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `AilmentChance=30` with the roll pinned at 50 lays no bleed and at 10 lays one; with no chance stated a roll pinned at
 * 99 still lays it (the control: a row with no chance is always). `Cataclysm.SkillAilmentRoll`, at console priority.
 * The target's health is asked for the bleed (`DealtToHealth` 10).
 */
bool FCataclysmWarSkillsAilmentChanceTest::RunTest(const FString&)
{
	using namespace CataclysmWarSkillsTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	IConsoleVariable* Roll = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.SkillAilmentRoll"));
	if (!TestNotNull(TEXT("set-up: the roll's console variable"), Roll))
	{
		return false;
	}
	ON_SCOPE_EXIT { Roll->Set(-1.0f, ECVF_SetByConsole); };
	FWarSkillsCreature Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: the player"), Player.IsUsable()))
	{
		return false;
	}
	UCataclysmStrikeSkill* Chancy = WarSkillsStrike(Player.Body, TEXT("Radius=3; Ailment=Bleed; AilmentChance=30"));
	UCataclysmStrikeSkill* Always = WarSkillsStrike(Player.Body, TEXT("Radius=3; Ailment=Bleed"));
	const auto Lays = [World, &Player](UCataclysmStrikeSkill* Strike, float Pinned, float Y)
	{
		IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.SkillAilmentRoll"))->Set(Pinned, ECVF_SetByConsole);
		FWarSkillsCreature Target(World, FVector(300.0f, Y, 0.0f));
		return Target.IsUsable() && Strike->ApplyStatedAilmentTo(Player.Body, Target.Body, /*DealtToHealth=*/10.0f);
	};
	TestFalse(TEXT("AilmentChance=30 with the roll at 50 lays nothing"), Lays(Chancy, 50.0f, 300.0f));
	TestTrue(TEXT("and with the roll at 10 lays the bleed"), Lays(Chancy, 10.0f, 600.0f));
	TestTrue(TEXT("control: no AilmentChance lays it with the roll at 99"), Lays(Always, 99.0f, 900.0f));
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarSkillsColossusTest,
	"Cataclysm.WarSkills.ColossusFindsEarthquakeAndScalesItByTheFervourHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Colossus's share, 25 at an empty pool rising in a straight line to 100 at a full one: 0.25, 0.625 and 1 at 0, 50 and
 * 100 Fervour. And the row it casts, found whatever weapon is held: `War_Warhammer_Heavy`, a Strike stating its 6 m, its
 * stagger and its armour removed.
 */
bool FCataclysmWarSkillsColossusTest::RunTest(const FString&)
{
	using namespace CataclysmWarSkillsTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FWarSkillsCreature Empty(World, FVector::ZeroVector, 0.0f);
	FWarSkillsCreature Half(World, FVector(300.0f, 0.0f, 0.0f), 50.0f);
	FWarSkillsCreature Full(World, FVector(600.0f, 0.0f, 0.0f), 100.0f);
	if (!TestTrue(TEXT("set-up: three creatures"), Empty.IsUsable() && Half.IsUsable() && Full.IsUsable()))
	{
		return false;
	}
	TestEqual(TEXT("an empty pool casts at 25%"), Empty.System->ColossusShare(25.0f), 0.25f, 0.0001f);
	TestEqual(TEXT("half a pool at 62.5%"), Half.System->ColossusShare(25.0f), 0.625f, 0.0001f);
	TestEqual(TEXT("a full pool at 100%"), Full.System->ColossusShare(25.0f), 1.0f, 0.0001f);

	FCataclysmWeaponSkill Earthquake;
	if (!TestTrue(TEXT("Earthquake is found by its row's name"),
				  UCataclysmTriggeredSkill::FindSkillRowNamed(
					  FName(UCataclysmAbilitySystemComponent::ColossusSkillRow), TEXT("War"), Earthquake)))
	{
		return false;
	}
	TestEqual(TEXT("it is a Strike"), Earthquake.Shape, ECataclysmSkillShape::Strike);
	TestEqual(TEXT("of 6 metres"), Earthquake.Params.RadiusCm, 600.0f, 0.01f);
	TestEqual(TEXT("staggering for 0.5 s"), Earthquake.Params.StaggerSeconds, 0.5f, 0.0001f);
	TestEqual(TEXT("removing 20% armour"), Earthquake.Params.ArmourRemovedPercent, 20.0f, 0.0001f);
	FCataclysmWeaponSkill Missing;
	TestFalse(TEXT("control: a name no row has is not found"),
			  UCataclysmTriggeredSkill::FindSkillRowNamed(FName(TEXT("War_No_Such_Row")), TEXT("War"), Missing));
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarSkillsBackwardTest,
	"Cataclysm.WarSkills.ABackwardBlinkLandsItsRangeAwayFromTheAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * W12, Recoil Dash (accepted 2026-11-28): `Backward=1` is read from the cell, and a backward move from the origin aimed
 * 5 m east lands 6 m WEST, further from the aim than it started; aimed at its own feet, it goes away from where it
 * faces. Recoil Dash's own row states it. THE CONTROL: a row with no `Backward` reads false.
 */
bool FCataclysmWarSkillsBackwardTest::RunTest(const FString&)
{
	TestTrue(TEXT("Backward=1 is read"), UCataclysmSkillShapes::ParseParams(TEXT("Mode=Blink; Range=6; Backward=1")).bBackward);
	TestFalse(TEXT("control: no Backward is a forward blink"),
			  UCataclysmSkillShapes::ParseParams(TEXT("Mode=Blink; Range=6")).bBackward);
	const FVector Start(0.0f, 0.0f, 50.0f);
	const FVector Aimed(500.0f, 0.0f, 0.0f);
	const FVector Landed = UCataclysmMovementSkill::BackwardDestination(Start, Aimed, 600.0f, FVector::ForwardVector);
	TestEqual(TEXT("aimed 5 m east, it lands 6 m west"), Landed, FVector(-600.0f, 0.0f, 50.0f), 0.01f);
	TestTrue(TEXT("further from the aim than it started"),
			 FVector::Dist2D(Landed, Aimed) > FVector::Dist2D(Start, Aimed));
	TestEqual(TEXT("aimed at its own feet, it goes away from its facing"),
			  UCataclysmMovementSkill::BackwardDestination(Start, Start, 600.0f, FVector::ForwardVector),
			  FVector(-600.0f, 0.0f, 50.0f), 0.01f);

	FCataclysmWeaponSkill RecoilDash;
	if (TestTrue(TEXT("Recoil Dash's row is found"),
				 UCataclysmTriggeredSkill::FindSkillRowNamed(FName(TEXT("War_2H_Crossbow_Movement")), TEXT("War"),
															 RecoilDash)))
	{
		TestTrue(TEXT("and states Backward"), RecoilDash.Params.bBackward);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
