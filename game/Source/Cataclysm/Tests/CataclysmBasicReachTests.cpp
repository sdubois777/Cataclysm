// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A ranged weapon's basic attack reaches its range, and a click aims at the enemy clicked. Ruled 2026-10-16 by the
 * coordinating session (P7 and P7-b, brief 38): `UCataclysmBasicAttack::ReachCmOf` returns a Projectile basic's
 * `ScaledRangeCm()`, so a Wand (`ItemBases.csv`: Range=8.4; Radius=0.6), a Crossbow (Range=10; Radius=0.4), a Staff
 * (Range=7.2; Radius=0.9) and a Two-Handed Crossbow (Range=12; Radius=0.9) fire from their range rather than from their
 * bolt's width; a Strike keeps its radius, the Whip's 3 m. And a click attack on an enemy aims at that enemy.
 *
 * THE RIG IS SKILL GADGETS STACK 2'S, with helpers of this file's own names: a possessed player character with the
 * game's own `ACataclysmPlayerController`, holding one Demonic weapon, given a weapon damage of 100 by hand; enemies of
 * the monsters' side with no defences. THE CLICK IS DRIVEN THROUGH `PressOnAnEnemyForTest`, because a test world has no
 * cursor to trace; and with no cursor the aim of a shot nothing names falls back to the character's facing, +X here.
 * NOTHING WAITS ON A TICK: a projectile is stepped by hand, a twentieth of a second at a time.
 */
namespace CataclysmBasicReachTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FReachWorld
	{
		FReachWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FReachWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A creature of the monsters' side holding 50,000 health, with no armour, evasion, block or resistance. */
	ACataclysmEnemyCharacter* ReachEnemy(UWorld* World, const FVector& Where)
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
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 50000.0f);
		return Made;
	}

	float ReachHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** A possessed player character with the game's own controller, holding one Demonic weapon. */
	struct FReachRig
	{
		FReachRig(UWorld* World, const FVector& Where, const TCHAR* WeaponType)
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
			System = State->GetCataclysmAbilitySystemComponent();
			Slots = Player->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
			if (!System || !Slots)
			{
				return;
			}
			Slots->SetDamageType(TEXT("Demonic"));
			Slots->EquipWeaponType(WeaponType);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		}

		/** The basic attack row the weapon holds, its granted instance, and whether everything was made. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test, const TCHAR* WeaponType, FCataclysmWeaponSkill& Row,
					  UCataclysmSkillTemplate*& Basic) const
		{
			Basic = nullptr;
			const FCataclysmWeaponSkill* Held = Slots ? Slots->GetAvailableSkills().FindByPredicate(
				[](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == ECataclysmAbilitySlot::BasicAttack; }) : nullptr;
			if (System)
			{
				for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
				{
					UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
					if (Skill && Skill->Slot == ECataclysmAbilitySlot::BasicAttack)
					{
						Basic = Skill;
					}
				}
			}
			if (!Player || !Controller || !System || !Held || !Basic)
			{
				Test.AddError(FString::Printf(TEXT("set-up: a possessed player with the game's controller, holding a %s "
					"with a basic attack row and its granted instance. If the row is missing, DT_ItemBases may be older "
					"than game/Data: run tools/generate_datatable_assets.py"), WeaponType));
				return false;
			}
			Row = *Held;
			return true;
		}

		ACataclysmPlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	/**
	 * Step every projectile in the world not in `Done` by a twentieth of a second until each has finished, adding each
	 * to `Done`. How many finished in this call. `Done` is the test's own, because a finished projectile lingers.
	 */
	int32 ReachLandEveryProjectile(UWorld* World, TSet<const ACataclysmProjectile*>& Done)
	{
		int32 Finished = 0;
		for (int32 Round = 0; Round < 200; ++Round)
		{
			TArray<ACataclysmProjectile*> Flying;
			for (TActorIterator<ACataclysmProjectile> It(World); It; ++It)
			{
				if (IsValid(*It) && !Done.Contains(*It))
				{
					Flying.Add(*It);
				}
			}
			if (Flying.IsEmpty())
			{
				break;
			}
			for (ACataclysmProjectile* Shot : Flying)
			{
				if (IsValid(Shot) && !Shot->Step(0.05f))
				{
					Done.Add(Shot);
					++Finished;
				}
			}
		}
		return Finished;
	}
}

#define CATACLYSM_BASIC_REACH_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. The reach: a Projectile basic's range, a Strike's radius (R1, R2)
// ==========================================================================

CATACLYSM_BASIC_REACH_TEST(FCataclysmBasicReachFigureTest,
	"Cataclysm.BasicReach.ARangedBasicAttackReachesItsRangeAndAStrikeKeepsItsRadius")
{
	using namespace CataclysmBasicReachTest;

	FReachWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;

	// THE CONTROL, READ FIRST: a Whip, whose basic is a 3 m Strike, reaches its radius, before and after the change.
	FReachRig Whip(World, FVector::ZeroVector, TEXT("Whip"));
	FCataclysmWeaponSkill WhipRow;
	UCataclysmSkillTemplate* WhipBasic = nullptr;
	if (!Whip.IsUsable(*this, TEXT("Whip"), WhipRow, WhipBasic)
		|| !TestTrue(TEXT("set-up: the whip's basic attack is a Strike"), WhipRow.Shape == ECataclysmSkillShape::Strike))
	{
		return false;
	}
	const float WhipReach = UCataclysmBasicAttack::ReachCmOf(Whip.System);
	TestEqual(*FString::Printf(TEXT("control: the whip reaches its Strike's radius, %.1f cm: %.1f cm"),
							   WhipRow.Params.RadiusCm, WhipReach),
		WhipReach, WhipRow.Params.RadiusCm, 0.01f);

	// EACH OF THE FOUR RANGED WEAPONS, 60 M APART, AGAINST ITS OWN ROW. No figure is typed in.
	const TCHAR* const Ranged[] = {TEXT("Wand"), TEXT("Crossbow"), TEXT("Staff"), TEXT("2H Crossbow")};
	float Along = 0.0f;
	for (const TCHAR* Weapon : Ranged)
	{
		Along += 60.0f * M;
		FReachRig Rig(World, FVector(Along, 0.0f, 0.0f), Weapon);
		FCataclysmWeaponSkill Row;
		UCataclysmSkillTemplate* Basic = nullptr;
		if (!Rig.IsUsable(*this, Weapon, Row, Basic)
			|| !TestTrue(*FString::Printf(TEXT("set-up: the %s's basic attack is a Projectile with a range past its "
											   "radius"), Weapon),
				   Row.Shape == ECataclysmSkillShape::Projectile && Row.Params.RangeCm > Row.Params.RadiusCm))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("set-up: holding no row, the %s's scaled range is its stated %.1f cm"), Weapon,
								   Row.Params.RangeCm),
			Basic->ScaledRangeCm(), Row.Params.RangeCm, 0.01f);

		const float Reach = UCataclysmBasicAttack::ReachCmOf(Rig.System);
		TestEqual(*FString::Printf(TEXT("the %s's basic reaches its range, %.1f cm: %.1f cm"), Weapon,
								   Basic->ScaledRangeCm(), Reach),
			Reach, Basic->ScaledRangeCm(), 0.01f);
		TestTrue(*FString::Printf(TEXT("and not its bolt's radius, %.1f cm: %.1f cm"), Row.Params.RadiusCm, Reach),
			Reach > Row.Params.RadiusCm + 1.0f);
	}
	return true;
}

// ==========================================================================
// 2. A press within a Wand's range swings; one beyond it does not (R1)
// ==========================================================================

CATACLYSM_BASIC_REACH_TEST(FCataclysmBasicReachPressTest,
	"Cataclysm.BasicReach.APressOnAnEnemyWithinAWandsRangeSwingsAndOneBeyondItDoesNot")
{
	using namespace CataclysmBasicReachTest;

	// A WAND PLAYER AT THE ORIGIN FACING +X: an enemy 5 m along X, beyond the bolt's 0.6 m and inside the 8.4 m range,
	// and one 10 m along -Y, beyond it. A WHIP PLAYER 60 M AWAY, an enemy 2 m from it, inside its 3 m.
	FReachWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FReachRig Whip(World, FVector(60.0f * M, 0.0f, 0.0f), TEXT("Whip"));
	FReachRig Wand(World, FVector::ZeroVector, TEXT("Wand"));
	FCataclysmWeaponSkill WhipRow;
	FCataclysmWeaponSkill WandRow;
	UCataclysmSkillTemplate* WhipBasic = nullptr;
	UCataclysmSkillTemplate* WandBasic = nullptr;
	if (!Whip.IsUsable(*this, TEXT("Whip"), WhipRow, WhipBasic) || !Wand.IsUsable(*this, TEXT("Wand"), WandRow, WandBasic))
	{
		return false;
	}
	ACataclysmEnemyCharacter* WhipEnemy = ReachEnemy(World, FVector(62.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Near = ReachEnemy(World, FVector(5.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Far = ReachEnemy(World, FVector(0.0f, -10.0f * M, 0.0f));
	if (!TestTrue(TEXT("set-up: three enemies"), WhipEnemy && Near && Far)
		|| !TestTrue(*FString::Printf(TEXT("set-up: the wand's range, %.1f cm, lies between 5 m and 10 m"),
									  WandRow.Params.RangeCm),
			   WandRow.Params.RangeCm > 5.0f * M && WandRow.Params.RangeCm < 10.0f * M))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: the rig swings at all. A whip press on an enemy inside its Strike's reach starts a swing.
	const float WhipNeverSwung = Whip.Controller->LastSwingSecondsForTest();
	Whip.Controller->PressOnAnEnemyForTest(WhipEnemy);
	Whip.Controller->ReleaseTheButtonForTest();
	TestTrue(TEXT("control: a whip press on an enemy 2 m away starts a swing"),
		Whip.Controller->LastSwingSecondsForTest() > WhipNeverSwung);

	// THE CONTROL OUT OF RANGE: an enemy 10 m away, beyond the wand's range, starts no swing, before and after.
	const float NeverSwung = Wand.Controller->LastSwingSecondsForTest();
	Wand.Controller->PressOnAnEnemyForTest(Far);
	Wand.Controller->ReleaseTheButtonForTest();
	TestEqual(TEXT("control: a wand press on an enemy 10 m away, beyond its range, starts no swing"),
		Wand.Controller->LastSwingSecondsForTest(), NeverSwung, 0.0001f);

	// THE CASE: an enemy 5 m away, inside the range, is swung at from where the player stands.
	Wand.Controller->PressOnAnEnemyForTest(Near);
	Wand.Controller->ReleaseTheButtonForTest();
	if (!TestTrue(TEXT("a wand press on an enemy 5 m away, inside its range, starts a swing"),
			Wand.Controller->LastSwingSecondsForTest() > NeverSwung))
	{
		return false;
	}

	// AND THE BOLT LANDS FROM 5 M, unless the swing waits on an animation a test world does not run.
	if (WandBasic->IsWaitingForTheSwingToConnect())
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the wand's swing waits for its animation, which a test world does not run, so whether its bolt "
				 "lands is not checked; the swing starting is"));
		return true;
	}
	TSet<const ACataclysmProjectile*> Done;
	ReachLandEveryProjectile(World, Done);
	TestTrue(*FString::Printf(TEXT("the bolt struck the enemy 5 m away: %.1f of 50000 left"), ReachHealthOf(Near)),
		ReachHealthOf(Near) < 50000.0f);
	TestEqual(TEXT("and not the one beyond the range"), ReachHealthOf(Far), 50000.0f, 0.001f);
	return true;
}

// ==========================================================================
// 3. A click's shot goes to the enemy clicked, not where the aim would otherwise fall (R4)
// ==========================================================================

CATACLYSM_BASIC_REACH_TEST(FCataclysmBasicReachAimTest,
	"Cataclysm.BasicReach.AClickedShotGoesToTheEnemyClickedAndNotWhereTheAimWouldOtherwiseFall")
{
	using namespace CataclysmBasicReachTest;

	// TWO WAND PLAYERS 60 M APART, EACH FACING +X, EACH WITH AN ENEMY 5 M ALONG X: the line a shot nothing names falls
	// back to, since a test world has no cursor. The first also has an enemy 5 m along Y, which it clicks.
	FReachWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	const FVector ControlHome(60.0f * M, 0.0f, 0.0f);
	FReachRig Control(World, ControlHome, TEXT("Wand"));
	FReachRig Rig(World, FVector::ZeroVector, TEXT("Wand"));
	FCataclysmWeaponSkill ControlRow;
	FCataclysmWeaponSkill Row;
	UCataclysmSkillTemplate* ControlBasic = nullptr;
	UCataclysmSkillTemplate* Basic = nullptr;
	if (!Control.IsUsable(*this, TEXT("Wand"), ControlRow, ControlBasic) || !Rig.IsUsable(*this, TEXT("Wand"), Row, Basic))
	{
		return false;
	}
	ACataclysmEnemyCharacter* ControlAhead = ReachEnemy(World, ControlHome + FVector(5.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Ahead = ReachEnemy(World, FVector(5.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Clicked = ReachEnemy(World, FVector(0.0f, 5.0f * M, 0.0f));
	if (!TestTrue(TEXT("set-up: three enemies"), ControlAhead && Ahead && Clicked)
		|| !TestTrue(TEXT("set-up: the player faces +X"),
			   Rig.Player->GetActorForwardVector().Equals(FVector::ForwardVector, 0.001f)))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: a swing naming no enemy fires along the facing, and its bolt strikes the enemy ahead.
	if (!TestTrue(TEXT("control: a basic attack swung with no enemy named starts"),
			UCataclysmBasicAttack::Swing(Control.System)))
	{
		return false;
	}
	if (ControlBasic->IsWaitingForTheSwingToConnect())
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the wand's swing waits for its animation, which a test world does not run, so where a shot lands "
				 "is not checked here at all"));
		return true;
	}
	TSet<const ACataclysmProjectile*> Done;
	ReachLandEveryProjectile(World, Done);
	TestTrue(*FString::Printf(TEXT("control: with no enemy named the bolt struck the enemy ahead: %.1f of 50000 left"),
							  ReachHealthOf(ControlAhead)),
		ReachHealthOf(ControlAhead) < 50000.0f);

	// THE CASE: a click on the enemy along Y, with the aim (the facing) on the enemy ahead.
	const float NeverSwung = Rig.Controller->LastSwingSecondsForTest();
	Rig.Controller->PressOnAnEnemyForTest(Clicked);
	Rig.Controller->ReleaseTheButtonForTest();
	if (!TestTrue(TEXT("set-up: the click on the enemy 5 m away, inside the wand's range, starts a swing"),
			Rig.Controller->LastSwingSecondsForTest() > NeverSwung))
	{
		return false;
	}
	ReachLandEveryProjectile(World, Done);
	TestTrue(*FString::Printf(TEXT("the click's bolt struck the enemy clicked: %.1f of 50000 left"), ReachHealthOf(Clicked)),
		ReachHealthOf(Clicked) < 50000.0f);
	TestEqual(TEXT("and not the enemy ahead, where the aim would otherwise fall"), ReachHealthOf(Ahead), 50000.0f, 0.001f);
	return true;
}

#undef CATACLYSM_BASIC_REACH_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
