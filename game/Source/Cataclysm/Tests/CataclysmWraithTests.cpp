// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmWraithCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Death's Wraith, 2026-11-26: the design the project owner approved that day. Its numbers are held to the model by
 * `tools/tests/test_wraith_matches_the_model.py`; these hold what the numbers do in a world.
 *
 * Helpers are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWraithTest
{
	using Wraith_t = ACataclysmWraithCharacter;

	/** One frame at sixty frames a second, the slice every charge test advances by. */
	constexpr float WraithTestFrameSeconds = 1.0f / 60.0f;

	ACataclysmWraithCharacter* SpawnWraith(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmWraithCharacter>(ACataclysmWraithCharacter::StaticClass(), Where,
															 FRotator::ZeroRotator, Params);
	}

	/** A plain creature on the players' side, which the Wraith's searches find, put exactly where it is wanted: spawned
	 *  far away and then moved, so two capsules at contact do not shove each other first. */
	ACataclysmEnemyCharacter* WraithTestTarget(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Target = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), FVector(20000.0f, 20000.0f, 0.0f), FRotator::ZeroRotator);
		if (!Target)
		{
			return nullptr;
		}
		Target->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Target->SetHealth(100000.0f);
		Target->SetActorLocation(Where);
		return Target;
	}

	float WraithTestHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* AbilitySystem = UCataclysmTargeting::AbilitySystemOf(Actor);
		return AbilitySystem ? AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute())
							 : -1.0f;
	}

	const FCataclysmEnemyAbility* WraithTestAbilityNamed(const TArray<FCataclysmEnemyAbility>& Abilities,
														 const TCHAR* Name)
	{
		return Abilities.FindByPredicate([Name](const FCataclysmEnemyAbility& One) { return One.Name == FName(Name); });
	}
}

#define CATACLYSM_WRAITH_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_WRAITH_TEST(FCataclysmWraithProfileTest, "Cataclysm.Wraith.ItCarriesItsDesignedProfile")
{
	using namespace CataclysmWraithTest;
	// WHAT THE CONSTRUCTOR SETS, read off a spawned Wraith rather than the header: its archetype row, reach, interval,
	// evasion and walk. THE CONTROL: the walk is not Unreal's default 600, which an enemy that forgot to set it keeps.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmWraithCharacter* Wraith = SpawnWraith(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("a Wraith"), Wraith))
	{
		return false;
	}
	TestEqual(TEXT("it is the Wraith row of EnemyArchetypes"), Wraith->ArchetypeRow, FName(TEXT("Wraith")));
	TestEqual(TEXT("its scythe reaches 1.6 metres"), Wraith->AttackReachCm(), Wraith_t::DesignedMeleeReachCm, 0.01f);
	TestEqual(TEXT("it swings every 1.25 seconds"), Wraith->DesignedSecondsBetweenAttacks(),
			  Wraith_t::DesignedAttackIntervalSeconds, 0.001f);
	TestEqual(TEXT("with 30% evasion"), Wraith->EvasionPercent, Wraith_t::DesignedEvasionPercent, 0.001f);
	const UCharacterMovementComponent* Movement = Wraith->GetCharacterMovement();
	if (!TestNotNull(TEXT("a movement component"), Movement))
	{
		return false;
	}
	TestEqual(TEXT("and walks at 5 metres a second"), Movement->MaxWalkSpeed, Wraith_t::DesignedWalkSpeedCmPerSecond,
			  0.01f);
	TestNotEqual(TEXT("CONTROL: not Unreal's default walk"), Movement->MaxWalkSpeed, 600.0f);
	TestFalse(TEXT("and it is no Reaper"), Wraith->bIsTheReaper);
	return true;
}

CATACLYSM_WRAITH_TEST(FCataclysmWraithAbilitiesTest, "Cataclysm.Wraith.ItsDashAndItsGrabCoverTheGroundBeyondItsReach")
{
	using namespace CataclysmWraithTest;
	// TWO ABILITIES BESIDE ITS SWING, IN BANDS THAT MEET AND DO NOT OVERLAP: Soul Grab from Reap's 1.6 metres to 4, the
	// dash from 4 to 8. Both marked, the dash as a lane and the grab as a ring.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmWraithCharacter* Wraith = SpawnWraith(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("a Wraith"), Wraith))
	{
		return false;
	}
	const TArray<FCataclysmEnemyAbility> Abilities = Wraith->EnemyAbilities();
	TestEqual(TEXT("two abilities beside its swing, three in all"), Abilities.Num(), 2);
	const FCataclysmEnemyAbility* Dash = WraithTestAbilityNamed(Abilities, TEXT("Phase Dash"));
	const FCataclysmEnemyAbility* Grab = WraithTestAbilityNamed(Abilities, TEXT("Soul Grab"));
	if (!TestNotNull(TEXT("Phase Dash"), Dash) || !TestNotNull(TEXT("Soul Grab"), Grab))
	{
		return false;
	}
	TestEqual(TEXT("Soul Grab begins where Reap ends"), Grab->MinRangeCm, Wraith->AttackReachCm(), 0.01f);
	TestEqual(TEXT("and the dash where Soul Grab ends"), Dash->MinRangeCm, Grab->MaxRangeCm, 0.01f);
	TestEqual(TEXT("the dash reaches 8 metres"), Dash->MaxRangeCm, Wraith_t::PhaseDashRangeCm, 0.01f);
	TestTrue(TEXT("the dash is a marked lane"),
			 Dash->Shape == ECataclysmSkillShape::Movement && Dash->MarkerRadiusCm >= 100.0f && Dash->WindUpSeconds > 0.0f);
	TestTrue(TEXT("the grab is a marked ring"),
			 Grab->Shape == ECataclysmSkillShape::Strike && Grab->MarkerRadiusCm == Wraith_t::SoulGrabRadiusCm
			 && Grab->WindUpSeconds > 0.0f);
	return true;
}

CATACLYSM_WRAITH_TEST(FCataclysmWraithDashTest, "Cataclysm.Wraith.PhaseDashRunsThroughThePlayerAndOutTheOtherSide")
{
	using namespace CataclysmWraithTest;
	// A TARGET 6 METRES AHEAD, IN THE LANE. The dash aimed at the lane's far end runs its whole 8 metres, through the
	// target, and ends 2 metres beyond it; the target is hit once and NOT SHOVED (it goes through, not into).
	// THE CONTROL: a second target 2 metres off the lane's side is neither hit nor moved.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmWraithCharacter* Wraith = SpawnWraith(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* InTheLane = WraithTestTarget(World, FVector(600.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* OffTheLane = WraithTestTarget(World, FVector(300.0f, 300.0f, 0.0f));
	if (!TestNotNull(TEXT("a Wraith"), Wraith) || !TestNotNull(TEXT("a target in the lane"), InTheLane)
		|| !TestNotNull(TEXT("a target off it"), OffTheLane))
	{
		return false;
	}
	// A BLOW WORTH SOMETHING, set as the Hellhound's tests set theirs: a fresh creature's attack is not a design.
	Wraith->SetAttackDamage(50.0f);
	const FVector InLaneStood = InTheLane->GetActorLocation();
	const FVector OffLaneStood = OffTheLane->GetActorLocation();
	const float InLaneBefore = WraithTestHealthOf(InTheLane);
	const float OffLaneBefore = WraithTestHealthOf(OffTheLane);

	Wraith->UseEnemyAbility(Wraith_t::PhaseDashAbility, InTheLane, FVector(Wraith_t::PhaseDashRangeCm, 0.0f, 0.0f));
	if (!TestTrue(TEXT("the dash started"), Wraith->IsCharging()))
	{
		return false;
	}
	int32 Frames = 0;
	while (Wraith->IsCharging() && Frames < 400)
	{
		Wraith->AdvanceCharge(WraithTestFrameSeconds);
		++Frames;
	}
	const float PerFrame = Wraith_t::PhaseDashSpeedCmPerSecond * WraithTestFrameSeconds;
	TestTrue(*FString::Printf(TEXT("it ran its 8 metres: %.1f cm"), Wraith->ChargeTravelledCm),
			 FMath::Abs(Wraith->ChargeTravelledCm - Wraith_t::PhaseDashRangeCm) <= PerFrame);
	TestTrue(*FString::Printf(TEXT("and came out beyond the target: x=%.1f against its %.1f"),
							  static_cast<float>(Wraith->GetActorLocation().X), static_cast<float>(InLaneStood.X)),
			 Wraith->GetActorLocation().X > InLaneStood.X + 150.0);
	TestTrue(*FString::Printf(TEXT("the target in the lane was hit: %.1f to %.1f"), InLaneBefore,
							  WraithTestHealthOf(InTheLane)),
			 WraithTestHealthOf(InTheLane) < InLaneBefore);
	TestTrue(TEXT("and not shoved"),
			 FVector::Dist2D(InTheLane->GetActorLocation(), InLaneStood) < 1.0);
	TestEqual(TEXT("CONTROL: the target off the lane took nothing"), WraithTestHealthOf(OffTheLane), OffLaneBefore,
			  0.01f);
	TestTrue(TEXT("CONTROL: and did not move"), FVector::Dist2D(OffTheLane->GetActorLocation(), OffLaneStood) < 1.0);
	return true;
}

CATACLYSM_WRAITH_TEST(FCataclysmWraithSoulGrabTest, "Cataclysm.Wraith.SoulGrabHitsAndPullsWhoeverIsInTheRing")
{
	using namespace CataclysmWraithTest;
	// A TARGET 3 METRES AWAY, INSIDE THE 4 METRE RING: Soul Grab hits it and pulls it to within Reap's reach.
	// THE CONTROL: a target 5 metres away, outside the ring, is neither hit nor moved.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmWraithCharacter* Wraith = SpawnWraith(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Inside = WraithTestTarget(World, FVector(300.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Outside = WraithTestTarget(World, FVector(0.0f, 500.0f, 0.0f));
	if (!TestNotNull(TEXT("a Wraith"), Wraith) || !TestNotNull(TEXT("a target inside"), Inside)
		|| !TestNotNull(TEXT("a target outside"), Outside))
	{
		return false;
	}
	// A BLOW WORTH SOMETHING, set as the Hellhound's tests set theirs: a fresh creature's attack is not a design.
	Wraith->SetAttackDamage(50.0f);
	const float InsideBefore = WraithTestHealthOf(Inside);
	const float OutsideBefore = WraithTestHealthOf(Outside);
	const FVector OutsideStood = Outside->GetActorLocation();

	Wraith->UseEnemyAbility(Wraith_t::SoulGrabAbility, Inside, Inside->GetActorLocation());
	TestEqual(TEXT("it caught one"), Wraith->LastSoulGrabCaught, 1);
	TestTrue(*FString::Printf(TEXT("the one inside was hit: %.1f to %.1f"), InsideBefore, WraithTestHealthOf(Inside)),
			 WraithTestHealthOf(Inside) < InsideBefore);
	const float Gap = static_cast<float>(FVector::Dist2D(Inside->GetActorLocation(), Wraith->GetActorLocation()));
	TestTrue(*FString::Printf(TEXT("and pulled to within Reap's reach: %.1f cm"), Gap),
			 Gap <= Wraith_t::DesignedMeleeReachCm);
	TestEqual(TEXT("CONTROL: the one outside took nothing"), WraithTestHealthOf(Outside), OutsideBefore, 0.01f);
	TestTrue(TEXT("CONTROL: and was not moved"), FVector::Dist2D(Outside->GetActorLocation(), OutsideStood) < 1.0);
	return true;
}

CATACLYSM_WRAITH_TEST(FCataclysmWraithReapTest, "Cataclysm.Wraith.ReapCatchesWhatIsInItsArcAndNothingBehindIt")
{
	using namespace CataclysmWraithTest;
	// TWO TARGETS IN THE SCYTHE'S ARC IN FRONT: its target and a second beside it, both struck. THE CONTROL: a third
	// within reach but BEHIND the Wraith is not.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmWraithCharacter* Wraith = SpawnWraith(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Ahead = WraithTestTarget(World, FVector(120.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Beside = WraithTestTarget(World, FVector(110.0f, 60.0f, 0.0f));
	ACataclysmEnemyCharacter* Behind = WraithTestTarget(World, FVector(-120.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("a Wraith"), Wraith) || !TestNotNull(TEXT("a target ahead"), Ahead)
		|| !TestNotNull(TEXT("one beside it"), Beside) || !TestNotNull(TEXT("one behind"), Behind))
	{
		return false;
	}
	// A BLOW WORTH SOMETHING, set as the Hellhound's tests set theirs: a fresh creature's attack is not a design.
	Wraith->SetAttackDamage(50.0f);
	const float AheadBefore = WraithTestHealthOf(Ahead);
	const float BesideBefore = WraithTestHealthOf(Beside);
	const float BehindBefore = WraithTestHealthOf(Behind);

	Wraith->AttackTarget(Ahead);
	TestEqual(TEXT("it caught two"), Wraith->LastReapCaught, 2);
	TestTrue(TEXT("its target was struck"), WraithTestHealthOf(Ahead) < AheadBefore);
	TestTrue(TEXT("and the one beside it in the arc"), WraithTestHealthOf(Beside) < BesideBefore);
	TestEqual(TEXT("CONTROL: the one behind it was not"), WraithTestHealthOf(Behind), BehindBefore, 0.01f);
	return true;
}

CATACLYSM_WRAITH_TEST(FCataclysmWraithReaperTest, "Cataclysm.Wraith.AsTheReaperItWalksSlowlyAndNeitherDashesNorGrabs")
{
	using namespace CataclysmWraithTest;
	// THE REAPER'S FORM (the owner's ruling that the Wraith art plays The Reaper, kept to the row's "slowly"): slower than
	// the slowest class's 3.5 metres a second, and no dash or pull. THE CONTROL: before it, the same Wraith walks at 5
	// and has both.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmWraithCharacter* Wraith = SpawnWraith(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("a Wraith"), Wraith))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: an ordinary Wraith has its dash and its grab"), Wraith->EnemyAbilities().Num(), 2);
	TestTrue(TEXT("CONTROL: and walks faster than any class"), Wraith->GetCharacterMovement()->MaxWalkSpeed > 460.0f);

	Wraith->BecomeTheReaper();
	TestTrue(TEXT("it is the Reaper"), Wraith->bIsTheReaper);
	TestEqual(TEXT("with neither"), Wraith->EnemyAbilities().Num(), 0);
	TestTrue(*FString::Printf(TEXT("walking slower than the slowest class: %.1f cm/s"),
							  Wraith->GetCharacterMovement()->MaxWalkSpeed),
			 Wraith->GetCharacterMovement()->MaxWalkSpeed < 350.0f);
	return true;
}

CATACLYSM_WRAITH_TEST(FCataclysmWraithDressedTest, "Cataclysm.Wraith.ItWearsItsBodyHelmAndScytheAndHidesThePlaceholder")
{
	using namespace CataclysmWraithTest;
	// THE OWNER'S WRAITH PACK, WHICH ONLY THE MAIN CHECKOUT HAS. With it: the body, Helm1 following its pose, the scythe
	// on the `scythe` bone, its clips, and the placeholder hidden. Without it: the placeholder kept, the half reported.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasNotBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmWraithCharacter* Wraith = SpawnWraith(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("a Wraith"), Wraith) || !TestNotNull(TEXT("a placeholder to hide"), Wraith->PlaceholderBody.Get()))
	{
		return false;
	}
	const bool bDressed = Wraith->ResolveBody(/*bIncludeAnimation=*/true);
	TestEqual(TEXT("the placeholder is hidden exactly when the body resolved"), Wraith->PlaceholderBody->IsVisible(),
			  !bDressed);
	if (!bDressed)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the owner's Wraith pack is not present (only the main checkout has it), so what was checked is that the "
				 "placeholder is KEPT; the body, helm, scythe and clips were not loaded."));
		return true;
	}
	TestNotNull(TEXT("it wears its body"), Wraith->GetMesh()->GetSkeletalMeshAsset());
	TestNotNull(TEXT("and its helm"), Wraith->Helm ? Wraith->Helm->GetSkeletalMeshAsset() : nullptr);
	TestNotNull(TEXT("and its scythe"), Wraith->Scythe ? Wraith->Scythe->GetStaticMesh() : nullptr);
	TestEqual(TEXT("on the scythe bone"), Wraith->Scythe ? Wraith->Scythe->GetAttachSocketName() : NAME_None,
			  FName(Wraith_t::ScytheBoneName));
	TestEqual(TEXT("two swings to alternate"), Wraith->ReapAnimations.Num(), 2);
	TestNotNull(TEXT("its standing clip"), Wraith->IdleAnimation.Get());
	TestNotNull(TEXT("its flying clip"), Wraith->FlyAnimation.Get());
	TestNotNull(TEXT("its dash clip"), Wraith->PhaseDashAnimation.Get());
	TestNotNull(TEXT("its grab clip"), Wraith->SoulGrabAnimation.Get());
	TestEqual(TEXT("two death clips"), Wraith->DeathAnimations.Num(), 2);
	if (Wraith->ReapAnimations.Num() == 2 && Wraith->ReapAnimations[0])
	{
		TestEqual(TEXT("a swing is the 1.6667 seconds the header records"), Wraith->ReapAnimations[0]->GetPlayLength(),
				  Wraith_t::ReapAnimationSeconds, 0.01f);
	}
	return true;
}

CATACLYSM_WRAITH_TEST(FCataclysmWraithAppearsTest, "Cataclysm.Wraith.ARaisedWraithAppearsBeforeItActs")
{
	using namespace CataclysmWraithTest;
	// A WRAITH A RULE RAISED (`bRisenFromTheDead`, set after its spawn as every rule sets it) appears on its next tick
	// and does not swing until the appearance's 1.96 seconds have passed; then its swing lands. THE CONTROL: an ordinary
	// Wraith does not appear and its first swing lands at once.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmWraithCharacter* Risen = SpawnWraith(World, FVector::ZeroVector);
	ACataclysmWraithCharacter* Ordinary = SpawnWraith(World, FVector(0.0f, 3000.0f, 0.0f));
	ACataclysmEnemyCharacter* RisensTarget = WraithTestTarget(World, FVector(120.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* OrdinarysTarget = WraithTestTarget(World, FVector(120.0f, 3000.0f, 0.0f));
	if (!TestNotNull(TEXT("a raised Wraith"), Risen) || !TestNotNull(TEXT("an ordinary one"), Ordinary)
		|| !TestNotNull(TEXT("a target for each"), RisensTarget) || !TestNotNull(TEXT("and the other"), OrdinarysTarget))
	{
		return false;
	}
	Risen->SetAttackDamage(50.0f);
	Ordinary->SetAttackDamage(50.0f);

	Risen->bRisenFromTheDead = true;
	Risen->AppearIfRisen();
	Ordinary->AppearIfRisen();
	TestTrue(TEXT("the raised Wraith appears"), Risen->bAppeared && Risen->IsAppearing());
	TestFalse(TEXT("CONTROL: the ordinary one does not"), Ordinary->bAppeared || Ordinary->IsAppearing());
	if (Risen->AppearAnimation)
	{
		TestTrue(TEXT("playing Ghost_Appear"), Risen->LastPlayedAnimation == Risen->AppearAnimation);
	}
	else
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the owner's Wraith pack is not present, so the appearance's clip was not loaded; its window, which "
				 "does not depend on the clip, was checked."));
	}

	const float RisensBefore = WraithTestHealthOf(RisensTarget);
	const float OrdinarysBefore = WraithTestHealthOf(OrdinarysTarget);
	Risen->AttackTarget(RisensTarget);
	Ordinary->AttackTarget(OrdinarysTarget);
	TestEqual(TEXT("while it appears, its swing does not land"), WraithTestHealthOf(RisensTarget), RisensBefore, 0.01f);
	TestTrue(TEXT("CONTROL: the ordinary Wraith's lands at once"), WraithTestHealthOf(OrdinarysTarget) < OrdinarysBefore);

	World->TimeSeconds += Wraith_t::AppearSeconds + 0.1f;
	TestFalse(TEXT("its appearance is over"), Risen->IsAppearing());
	Risen->AppearIfRisen();
	TestFalse(TEXT("and it appears once, not again"), Risen->IsAppearing());
	Risen->AttackTarget(RisensTarget);
	TestTrue(TEXT("then its swing lands"), WraithTestHealthOf(RisensTarget) < RisensBefore);
	return true;
}

#undef CATACLYSM_WRAITH_TEST

#endif // WITH_AUTOMATION_TESTS
