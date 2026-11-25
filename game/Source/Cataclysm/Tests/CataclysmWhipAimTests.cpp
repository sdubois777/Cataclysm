// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
// For what a swing waits on when its test ends. Window bb12-w2.
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Whip's swing reaches an enemy beside it, and a skill aims at the cursor. Issue #2870, reported by the owner while
 * playtesting on 2026-11-23: "Some weapons, like the whip, don't actually hit enemies in their range, it's like it only
 * hits them if they're on the edge. Also, is the targeting towards the cursor or is it based on the direction the player
 * is walking? It should be towards the cursor."
 *
 * THE RIG IS `Cataclysm.BasicReach.`'s, with helpers of this file's own names: a possessed player character with the
 * game's own `ACataclysmPlayerController`, holding a Demonic Whip, given a weapon damage of 100 by hand; enemies of the
 * monsters' side with no defences and 50,000 health. Every rig stands 60 m from the next, so no swing reaches another's
 * enemies. EACH PLAYER FACES +X AND IS GIVEN A VELOCITY ALONG +X, walking right, so a swing that followed the facing or
 * the walk would go along +X. THE CURSOR IS GIVEN BY HAND (`ACataclysmPlayerController::PointCursorForTest`), because a
 * test world has no cursor to trace; a rig given none has no cursor, as in play when it points at nothing. EVERY SWING
 * IS FINISHED BY HAND, moving the world's clock with `CataclysmTestWorld::RunClock` until it connects, as
 * `Cataclysm.BasicReach.` does (that file's comment says why the wait is never skipped).
 *
 * THE BASIC ATTACK IS SWUNG NAMING NO ENEMY (`UCataclysmBasicAttack::Swing` with no target), so it aims as a skill on a
 * key does: at the cursor, or along the facing with none. A click on an enemy aims at that enemy (ruled 2026-10-16,
 * P7-b), which `Cataclysm.BasicReach.AClickAttackGoesToTheEnemyClickedAndNotWhereTheAimWouldOtherwiseFall` covers.
 */
namespace CataclysmWhipAimTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every enemy here starts with. */
	constexpr float WhipAimPool = 50000.0f;

	/** How far apart the rigs stand. */
	constexpr float WhipAimApart = 60.0f * M;

	/** A world for one test, destroyed when the test ends. */
	struct FWhipAimWorld
	{
		FWhipAimWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FWhipAimWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side with 50,000 health and no armour, evasion, block or resistance. */
	ACataclysmEnemyCharacter* WhipAimEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(100000.0f);
		Made->SetArmour(0.0f);
		Made->SetAttackDamage(0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), WhipAimPool);
		return Made;
	}

	/** Whether a creature has lost any health. */
	bool WhipAimStruck(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its && Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) < WhipAimPool - 0.01f;
	}

	/** A possessed player character with the game's own controller, holding a Demonic Whip, facing and walking +X. */
	struct FWhipAimRig
	{
		FWhipAimRig(UWorld* World, const FVector& Where)
		{
			// THE PLAYER STATE IS OWNED BY ITS CONTROLLER, as the engine's `AController::InitPlayerState` spawns it in
			// the game. The abilities' actor info finds the player controller only through the player state's owner
			// (`FGameplayAbilityActorInfo::InitFromActor`), and `SetPlayerState` sets none, so without it a skill's
			// `AimPoint` has no controller and never reads the cursor: window bb12-w1 failed both cursor tests on it
			// (2026-11-24).
			Controller = World->SpawnActor<ACataclysmPlayerController>();
			FActorSpawnParameters OwnedByTheController;
			OwnedByTheController.Owner = Controller;
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>(OwnedByTheController);
			Player = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
			if (!State || !Controller || !Player)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Player);
			System = State->GetCataclysmAbilitySystemComponent();
			UCataclysmWeaponSlotsComponent* Slots = Player->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
			if (!System || !Slots)
			{
				return;
			}
			Slots->SetDamageType(TEXT("Demonic"));
			Slots->EquipWeaponType(TEXT("Whip"));
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);

			// WALKING RIGHT, ALONG +X: facing it, and moving along it. Nothing ticks the movement, so the velocity is
			// what the character would report, and the facing is what a swing with no cursor falls back to.
			Player->SetActorRotation(FRotator::ZeroRotator);
			if (UCharacterMovementComponent* Moving = Player->GetCharacterMovement())
			{
				Moving->Velocity = FVector(600.0f, 0.0f, 0.0f);
			}

			for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
			{
				UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
				if (Skill && Skill->Slot == ECataclysmAbilitySlot::BasicAttack)
				{
					Basic = Skill;
					BasicHandle = Spec.Handle;
				}
			}
		}

		/** Whether everything was made and the basic attack is the Whip's 3 m, 45 degree, one-target Strike. */
		bool IsUsable(FAutomationTestBase& Test) const
		{
			if (!Player || !Controller || !System || !Basic)
			{
				Test.AddError(TEXT("set-up: a possessed player with the game's controller, holding a Whip with a basic "
								   "attack granted. If the row is missing, DT_ItemBases may be older than game/Data: run "
								   "tools/generate_datatable_assets.py"));
				return false;
			}
			// AND THE ABILITIES' ACTOR INFO HOLDS THIS CONTROLLER, the one a skill asks where the cursor points. Without
			// it every cursor case fails for a reason the test itself made (window bb12-w1), so it is said here first.
			if (!Test.TestTrue(TEXT("set-up: the abilities' actor info holds the player's controller, which a skill asks "
									"where the cursor points"),
					System->AbilityActorInfo.IsValid() && System->AbilityActorInfo->PlayerController.Get() == Controller))
			{
				return false;
			}
			return Test.TestTrue(*FString::Printf(TEXT("set-up: the whip's basic attack is a Strike of 3 m and 45 "
													   "degrees taking one target (radius %.1f cm, angle %.1f, %d)"),
												  Basic->Params.RadiusCm, Basic->Params.AngleDegrees,
												  Basic->Params.MaxTargets),
				Basic->Shape() == ECataclysmSkillShape::Strike
					&& FMath::IsNearlyEqual(Basic->Params.RadiusCm, 3.0f * M, 0.5f)
					&& FMath::IsNearlyEqual(Basic->Params.AngleDegrees, 45.0f, 0.01f)
					&& Basic->Params.MaxTargets == 1);
		}

		/**
		 * Swing the basic attack naming no enemy, and finish its wait for the clip by moving the clock, for at most
		 * five seconds. Whether it started and connected.
		 */
		bool SwingAndConnect(UWorld* World) const
		{
			if (!UCataclysmBasicAttack::Swing(System))
			{
				return false;
			}
			for (int32 Each = 0; Each < 50 && Basic->IsWaitingForTheSwingToConnect(); ++Each)
			{
				CataclysmTestWorld::RunClock(World, 0.1f);
			}
			return !Basic->IsWaitingForTheSwingToConnect();
		}

		/** Whether the basic attack's spec still counts as active in the ability system. */
		bool IsStrikeActive() const
		{
			const FGameplayAbilitySpec* Spec = System ? System->FindAbilitySpecFromHandle(BasicHandle) : nullptr;
			return Spec && Spec->IsActive();
		}

		/**
		 * What the swing is waiting on, read once: the spec's active count, the ability object's own flag, the swing's
		 * timer, a montage on the ability system or on the mesh, and which way the character faces. Window bb12-w2 asked
		 * whether a swing with the controller in the actor info waits on a turn, a montage or a timer.
		 */
		FString WhatTheSwingWaitsOn() const
		{
			const FGameplayAbilitySpec* Spec = System ? System->FindAbilitySpecFromHandle(BasicHandle) : nullptr;
			const USkeletalMeshComponent* Mesh = Player ? Player->GetMesh() : nullptr;
			const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
			const UAnimMontage* OnTheMesh = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
			const UAnimMontage* OnTheSystem = System ? System->GetCurrentMontage() : nullptr;
			const FVector Facing = Player ? Player->GetActorForwardVector() : FVector::ZeroVector;
			return FString::Printf(TEXT("active count %d, the ability object %s, the swing timer %s, montage on the "
										"ability system %s, montage on the mesh %s, facing (%.3f, %.3f)"),
								   Spec ? static_cast<int32>(Spec->ActiveCount) : -1,
								   Basic && Basic->IsActive() ? TEXT("active") : TEXT("not active"),
								   Basic && Basic->IsWaitingForTheSwingToConnect() ? TEXT("waiting") : TEXT("none"),
								   OnTheSystem ? *OnTheSystem->GetName() : TEXT("none"),
								   OnTheMesh ? *OnTheMesh->GetName() : TEXT("none"), Facing.X, Facing.Y);
		}

		ACataclysmPlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmSkillTemplate* Basic = nullptr;
		FGameplayAbilitySpecHandle BasicHandle;
	};

	/**
	 * After a test's last check, every rig's basic attack ends: the world's clock is run in tenths of a second until no
	 * rig's strike spec counts as active, for at most three seconds of game time, and each rig says how long its strike
	 * stayed active and what it was waiting on when the wait began. Prints "SWING END".
	 *
	 * WHY. Window bb12-w2 (2026-11-24): with the player state owned by its controller, so that the abilities' actor info
	 * holds the controller as it does in the game, all three tests failed on the engine's ensure that clearing the
	 * abilities found "Default__CataclysmStrikeSkill was still active". Either the world was destroyed mid-swing or the
	 * strike never ends; this tells the two apart, and checks the basic attack ends. Nothing here cancels an ability.
	 */
	void EachSwingEnds(FAutomationTestBase& Test, UWorld* World, const TArray<const FWhipAimRig*>& Rigs)
	{
		constexpr float CapSeconds = 3.0f;
		constexpr float StepSeconds = 0.1f;
		TArray<float> EndedAfter;
		EndedAfter.Init(-1.0f, Rigs.Num());
		TArray<FString> WaitingOn;
		for (const FWhipAimRig* Rig : Rigs)
		{
			WaitingOn.Add(Rig->WhatTheSwingWaitsOn());
		}

		float Waited = 0.0f;
		while (true)
		{
			bool bAllEnded = true;
			for (int32 Index = 0; Index < Rigs.Num(); ++Index)
			{
				if (EndedAfter[Index] < 0.0f)
				{
					if (Rigs[Index]->IsStrikeActive())
					{
						bAllEnded = false;
					}
					else
					{
						EndedAfter[Index] = Waited;
					}
				}
			}
			if (bAllEnded || Waited >= CapSeconds - KINDA_SMALL_NUMBER)
			{
				break;
			}
			CataclysmTestWorld::RunClock(World, StepSeconds);
			Waited += StepSeconds;
		}

		for (int32 Index = 0; Index < Rigs.Num(); ++Index)
		{
			const bool bEnded = EndedAfter[Index] >= 0.0f;
			Test.AddInfo(FString::Printf(TEXT("SWING END, rig %d: %s; when the wait began: %s"), Index + 1,
										 bEnded ? *FString::Printf(TEXT("ended %.1f s after the test's last check"),
																	EndedAfter[Index])
												: TEXT("STILL ACTIVE after 3.0 s"),
										 *WaitingOn[Index]));
			Test.TestTrue(*FString::Printf(TEXT("THE SWING ENDED: rig %d's strike was still active for %.1f s after the "
												"test's last check, of at most 3.0"),
										   Index + 1, bEnded ? EndedAfter[Index] : CapSeconds),
						  bEnded);
		}
	}
}

#define CATACLYSM_WHIP_AIM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. The Whip strikes an enemy a metre ahead and half a metre to the side
// ==========================================================================

CATACLYSM_WHIP_AIM_TEST(FCataclysmWhipAimBesideTest,
	"Cataclysm.WhipAim.TheWhipStrikesAnEnemyOneMetreAheadAndHalfAMetreToTheSideOfItsAim")
{
	using namespace CataclysmWhipAimTest;

	// THREE WHIP PLAYERS, EACH SWINGING ALONG +X WITH NO CURSOR. THE CONTROL's enemy stands 1 m straight ahead. THE
	// CASE's stands 1 m ahead and 0.5 m to the side: its centre is 26.6 degrees off the aim, outside the 22.5 either side
	// of the swing, and its 48 cm body is squarely inside it. THE EDGE's stands 1 m ahead and 1.5 m to the side, 56
	// degrees off, its body a metre clear of the swing's edge.
	FWhipAimWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector ControlHome(0.0f, 0.0f, 0.0f);
	const FVector CaseHome(WhipAimApart, 0.0f, 0.0f);
	const FVector EdgeHome(2.0f * WhipAimApart, 0.0f, 0.0f);
	FWhipAimRig Control(World, ControlHome);
	FWhipAimRig Case(World, CaseHome);
	FWhipAimRig Edge(World, EdgeHome);
	if (!Control.IsUsable(*this) || !Case.IsUsable(*this) || !Edge.IsUsable(*this))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Ahead = WhipAimEnemy(World, ControlHome + FVector(1.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Beside = WhipAimEnemy(World, CaseHome + FVector(1.0f * M, 0.5f * M, 0.0f));
	ACataclysmEnemyCharacter* Clear = WhipAimEnemy(World, EdgeHome + FVector(1.0f * M, 1.5f * M, 0.0f));
	if (!TestTrue(TEXT("set-up: three enemies"), Ahead && Beside && Clear))
	{
		return false;
	}
	if (!TestFalse(TEXT("set-up: the case's enemy's centre is outside the swing's cone, by the centre's own test"),
			UCataclysmTargeting::IsInCone(CaseHome, FVector::ForwardVector, Beside->GetActorLocation(),
										  Case.Basic->Params.RadiusCm, Case.Basic->Params.AngleDegrees))
		|| !TestTrue(*FString::Printf(TEXT("set-up: and its body is %.1f cm across, a creature's capsule"),
									  2.0f * UCataclysmTargeting::BodyRadiusOf(Beside)),
			   UCataclysmTargeting::BodyRadiusOf(Beside) > 40.0f))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: the rig swings along +X and strikes what stands straight ahead.
	if (!TestTrue(TEXT("set-up: the control's swing started and connected"), Control.SwingAndConnect(World)))
	{
		return false;
	}
	TestTrue(TEXT("control: the swing struck the enemy 1 m straight ahead"), WhipAimStruck(Ahead));

	// THE CASE.
	if (!TestTrue(TEXT("set-up: the case's swing started and connected"), Case.SwingAndConnect(World)))
	{
		return false;
	}
	TestTrue(TEXT("THE WHIP STRUCK THE ENEMY 1 M AHEAD AND 0.5 M TO THE SIDE OF ITS AIM, whose body is inside the swing"),
		WhipAimStruck(Beside));

	// AND THE SWING STILL HAS AN EDGE.
	if (!TestTrue(TEXT("set-up: the edge's swing started and connected"), Edge.SwingAndConnect(World)))
	{
		return false;
	}
	TestFalse(TEXT("and an enemy 1 m ahead and 1.5 m to the side, its body a metre clear of the swing's edge, is not"),
		WhipAimStruck(Clear));

	// AND EVERY SWING ENDS, before the world is destroyed.
	EachSwingEnds(*this, World, {&Control, &Case, &Edge});
	return true;
}

// ==========================================================================
// 2. The swing goes where the cursor is, not where the character walks
// ==========================================================================

CATACLYSM_WHIP_AIM_TEST(FCataclysmWhipAimFollowsTheCursorTest,
	"Cataclysm.WhipAim.ASwingGoesTowardTheCursorAndNotTheWayTheCharacterWalks")
{
	using namespace CataclysmWhipAimTest;

	// TWO WHIP PLAYERS, EACH FACING AND WALKING +X, each with an enemy 2 m along +X, the way it walks, and one 2 m along
	// -Y, to its left. THE CONTROL has no cursor, which is a cursor pointing at nothing. THE CASE's cursor is on the floor
	// 2.5 m along -Y, to its left, with no creature under it.
	FWhipAimWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector ControlHome(0.0f, 0.0f, 0.0f);
	const FVector CaseHome(WhipAimApart, 0.0f, 0.0f);
	FWhipAimRig Control(World, ControlHome);
	FWhipAimRig Case(World, CaseHome);
	if (!Control.IsUsable(*this) || !Case.IsUsable(*this))
	{
		return false;
	}
	ACataclysmEnemyCharacter* ControlWalked = WhipAimEnemy(World, ControlHome + FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* ControlLeft = WhipAimEnemy(World, ControlHome + FVector(0.0f, -2.0f * M, 0.0f));
	ACataclysmEnemyCharacter* CaseWalked = WhipAimEnemy(World, CaseHome + FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* CaseLeft = WhipAimEnemy(World, CaseHome + FVector(0.0f, -2.0f * M, 0.0f));
	if (!TestTrue(TEXT("set-up: four enemies"), ControlWalked && ControlLeft && CaseWalked && CaseLeft)
		|| !TestTrue(TEXT("set-up: both players walk along +X"),
			   Control.Player->GetVelocity().X > 0.0f && Case.Player->GetVelocity().X > 0.0f))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: with the cursor on nothing, the swing goes the way the character faces.
	FVector Nowhere;
	TestFalse(TEXT("control: a test world's cursor points at nothing"),
		Control.Controller->AimPointUnderCursor(Nowhere));
	if (!TestTrue(TEXT("set-up: the control's swing started and connected"), Control.SwingAndConnect(World)))
	{
		return false;
	}
	TestTrue(TEXT("control: with no cursor the swing struck the enemy the way the character walks"),
		WhipAimStruck(ControlWalked));
	TestFalse(TEXT("control: and not the one to its left"), WhipAimStruck(ControlLeft));

	// THE CASE: the cursor on the floor to the left.
	Case.Controller->PointCursorForTest(nullptr, CaseHome + FVector(0.0f, -2.5f * M, 0.0f));
	if (!TestTrue(TEXT("set-up: the case's swing started and connected"), Case.SwingAndConnect(World)))
	{
		return false;
	}
	TestTrue(TEXT("THE SWING WENT TOWARD THE CURSOR: it struck the enemy to the left"), WhipAimStruck(CaseLeft));
	TestFalse(TEXT("and not the one the character walks toward"), WhipAimStruck(CaseWalked));
	TestTrue(*FString::Printf(TEXT("and the character turned to the cursor: facing (%.3f, %.3f)"),
							  Case.Player->GetActorForwardVector().X, Case.Player->GetActorForwardVector().Y),
		FVector::DotProduct(Case.Player->GetActorForwardVector(), FVector(0.0f, -1.0f, 0.0f)) > 0.99f);
	Case.Controller->ForgetCursorForTest();

	// AND EVERY SWING ENDS, before the world is destroyed.
	EachSwingEnds(*this, World, {&Control, &Case});
	return true;
}

// ==========================================================================
// 3. A cursor on an enemy aims at the enemy, not at the floor behind it
// ==========================================================================

CATACLYSM_WHIP_AIM_TEST(FCataclysmWhipAimAtTheEnemyUnderTheCursorTest,
	"Cataclysm.WhipAim.ACursorOnAnEnemyAimsAtTheEnemyAndNotAtTheFloorBehindIt")
{
	using namespace CataclysmWhipAimTest;

	// TWO WHIP PLAYERS, EACH WITH AN ENEMY 1 M ALONG +Y, to its right on screen. The camera looks along +X from sixty
	// degrees up, so a cursor on that enemy's body passes through it (creatures ignore the visibility channel) and meets
	// the floor further up the screen, along +X. THE FLOOR POINT GIVEN IS 1.5 M ALONG +X OF THE ENEMY'S FEET, a cursor on
	// a tall creature's head: aimed there, the swing's nearer edge passes 56 cm from the enemy's centre, more than its
	// 48 cm body, so the floor point alone misses it. THE CONTROL's cursor is that floor point with no creature under it;
	// THE CASE's is the same floor point with the enemy under it.
	FWhipAimWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector ControlHome(0.0f, 0.0f, 0.0f);
	const FVector CaseHome(WhipAimApart, 0.0f, 0.0f);
	FWhipAimRig Control(World, ControlHome);
	FWhipAimRig Case(World, CaseHome);
	if (!Control.IsUsable(*this) || !Case.IsUsable(*this))
	{
		return false;
	}
	const FVector Right(0.0f, 1.0f * M, 0.0f);
	const FVector Behind(1.5f * M, 1.0f * M, 0.0f);
	ACataclysmEnemyCharacter* ControlEnemy = WhipAimEnemy(World, ControlHome + Right);
	ACataclysmEnemyCharacter* CaseEnemy = WhipAimEnemy(World, CaseHome + Right);
	ACataclysmEnemyCharacter* Bystander = WhipAimEnemy(World, CaseHome + FVector(0.0f, -20.0f * M, 0.0f));
	if (!TestTrue(TEXT("set-up: three enemies"), ControlEnemy && CaseEnemy && Bystander))
	{
		return false;
	}
	if (!TestFalse(TEXT("set-up: aimed at the floor point, the swing's cone does not reach the enemy's body"),
			UCataclysmTargeting::IsActorInCone(ControlHome, Behind.GetSafeNormal2D(), ControlEnemy,
											   Control.Basic->Params.RadiusCm, Control.Basic->Params.AngleDegrees)))
	{
		return false;
	}

	// WHAT THE CONTROLLER ANSWERS, read directly: the enemy's own place under it, the floor otherwise.
	FVector Aimed;
	Case.Controller->PointCursorForTest(CaseEnemy, CaseHome + Behind);
	TestTrue(TEXT("with an enemy under the cursor the aim is that enemy's place"),
		Case.Controller->AimPointUnderCursor(Aimed) && Aimed.Equals(CaseEnemy->GetActorLocation(), 0.01f));
	Case.Controller->PointCursorForTest(Case.Player, CaseHome + Behind);
	TestTrue(TEXT("with the player's own body under it, the floor point: the player is no enemy"),
		Case.Controller->AimPointUnderCursor(Aimed) && Aimed.Equals(CaseHome + Behind, 0.01f));
	Case.Controller->PointCursorForTest(nullptr, CaseHome + Behind);
	TestTrue(TEXT("with nothing under it, the floor point"),
		Case.Controller->AimPointUnderCursor(Aimed) && Aimed.Equals(CaseHome + Behind, 0.01f));

	// THE CONTROL, READ FIRST: the cursor on the floor point alone, the swing goes past the enemy.
	Control.Controller->PointCursorForTest(nullptr, ControlHome + Behind);
	if (!TestTrue(TEXT("set-up: the control's swing started and connected"), Control.SwingAndConnect(World)))
	{
		return false;
	}
	TestFalse(TEXT("control: aimed at the floor behind the enemy, the swing goes past it"), WhipAimStruck(ControlEnemy));
	Control.Controller->ForgetCursorForTest();

	// THE CASE: the same floor point, with the enemy under the cursor.
	Case.Controller->PointCursorForTest(CaseEnemy, CaseHome + Behind);
	if (!TestTrue(TEXT("set-up: the case's swing started and connected"), Case.SwingAndConnect(World)))
	{
		return false;
	}
	TestTrue(TEXT("WITH THE ENEMY UNDER THE CURSOR THE SWING IS AIMED AT IT AND STRIKES IT"), WhipAimStruck(CaseEnemy));

	// AND A CORPSE UNDER THE CURSOR IS NO ENEMY: the floor point is the aim again.
	UCataclysmSkillEffects::MarkDead(Bystander);
	Case.Controller->PointCursorForTest(Bystander, CaseHome + Behind);
	TestTrue(TEXT("with a corpse under the cursor, the floor point"),
		Case.Controller->AimPointUnderCursor(Aimed) && Aimed.Equals(CaseHome + Behind, 0.01f));
	Case.Controller->ForgetCursorForTest();

	// AND EVERY SWING ENDS, before the world is destroyed.
	EachSwingEnds(*this, World, {&Control, &Case});
	return true;
}

#undef CATACLYSM_WHIP_AIM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
