// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBuriedWeapon.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/OutputDevice.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Harrower's axe leaps from a dead host to the nearest living enemy within 10 m, and on, until nothing is in reach.
 * Issue #2872, reported by the owner while playtesting on 2026-11-24: "I don't think the axe skill, Harrower, is actually
 * working." `Demonic_Axe_Special` in game/Data/WeaponSkills.csv: "Bury a burning axe in an enemy up to 10 meters away,
 * setting them alight. The axe stays where it lands. When that enemy dies it tears free and buries itself in the
 * nearest living enemy within 10 meters, and it goes on doing so until nothing is left in reach."
 *
 * THE LEAP WAS TESTED BEFORE, BUT NEVER FROM A THROW: `Cataclysm.Skills.ABuriedAxeTearsFreeAndStrikesTheNextEnemy`
 * buries the axe with `BuryInStruck` by hand. Here the first test throws Harrower from the Special slot of a player
 * holding a Demonic Axe, as play does, and every death is `UCataclysmSkillEffects::MarkDead`, the one place a death is
 * recorded, which is where the axe tears free.
 *
 * THE RIG IS `Cataclysm.BasicReach.`'s, with helpers of this file's own names: a possessed player character with the
 * game's own `ACataclysmPlayerController`, holding a Demonic Axe, given a weapon damage of 100 by hand, facing +X. A test
 * world has no cursor, so the throw goes along the facing, its 10 m. Enemies of the monsters' side with no defences and
 * 50,000 health unless a case says otherwise. THE THROW'S WAIT FOR ITS CLIP IS FINISHED BY HAND by moving the clock, as
 * `Cataclysm.BasicReach.` does, and the axe in flight is stepped a twentieth of a second at a time. EVERY LINE
 * `LogCataclysm` writes is captured, so the lines a burial, a leap and a stop say are counted.
 */
namespace CataclysmHarrowerLeapsTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The health every enemy here starts with, unless a case says otherwise. */
	constexpr float HarrowerLeapsPool = 50000.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FHarrowerLeapsWorld
	{
		FHarrowerLeapsWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FHarrowerLeapsWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** Every line `LogCataclysm` writes while this is alive. */
	struct FHarrowerLeapsLogLines : public FOutputDevice
	{
		FHarrowerLeapsLogLines()
		{
			if (GLog)
			{
				GLog->AddOutputDevice(this);
			}
		}
		virtual ~FHarrowerLeapsLogLines()
		{
			if (GLog)
			{
				GLog->RemoveOutputDevice(this);
			}
		}
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == FName(TEXT("LogCataclysm")))
			{
				Lines.Add(FString(Text));
			}
		}

		/**
		 * How many lines carry the buried weapon's prefix and contain `Needle`. Flushes first: the log buffers lines
		 * written from a thread that is not the primary one.
		 */
		int32 Count(const TCHAR* Needle)
		{
			if (GLog)
			{
				GLog->Flush();
			}
			int32 Found = 0;
			for (const FString& Line : Lines)
			{
				Found += Line.Contains(UCataclysmBuriedWeapon::LogPrefix, ESearchCase::CaseSensitive)
					&& Line.Contains(Needle, ESearchCase::CaseSensitive) ? 1 : 0;
			}
			return Found;
		}

		TArray<FString> Lines;
	};

	/** A creature of the monsters' side with `Health` and no armour, evasion, block or resistance. */
	ACataclysmEnemyCharacter* HarrowerLeapsEnemy(UWorld* World, const FVector& Where, float Health = HarrowerLeapsPool)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(Health);
		Made->SetArmour(0.0f);
		Made->SetAttackDamage(0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Health);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		return Made;
	}

	/** What a creature has lost since it was made at `Health`. */
	float HarrowerLeapsTaken(const AActor* Who, float Health = HarrowerLeapsPool)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? Health - Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** Whether a creature is burning. */
	bool HarrowerLeapsAlight(const AActor* Who)
	{
		return UCataclysmSkillEffects::HasTag(Who, UCataclysmSkillEffects::BurnTag());
	}

	/** The weapon buried in a creature, or null. */
	const UCataclysmBuriedWeapon* HarrowerLeapsWeaponIn(const AActor* Who)
	{
		return Who ? Who->FindComponentByClass<UCataclysmBuriedWeapon>() : nullptr;
	}

	/** A possessed player character with the game's own controller, holding a Demonic Axe, facing +X. */
	struct FHarrowerLeapsRig
	{
		FHarrowerLeapsRig(UWorld* World, const FVector& Where)
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
			UCataclysmWeaponSlotsComponent* Slots = Player->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
			if (!System || !Slots)
			{
				return;
			}
			Slots->SetDamageType(TEXT("Demonic"));
			Slots->EquipWeaponType(TEXT("Axe"));
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
			{
				UCataclysmProjectileSkill* Skill = Cast<UCataclysmProjectileSkill>(Spec.GetPrimaryInstance());
				if (Skill && Skill->Slot == ECataclysmAbilitySlot::Special)
				{
					Harrower = Skill;
					HarrowerHandle = Spec.Handle;
				}
			}
		}

		/** Whether everything was made and the Special is Harrower as its row writes it. Reports why not. */
		bool IsUsable(FAutomationTestBase& Test) const
		{
			if (!Player || !Controller || !System || !Harrower)
			{
				Test.AddError(TEXT("set-up: a possessed player with the game's controller, holding a Demonic Axe whose "
								   "Special is a projectile. If the row is missing, DT_WeaponSkills may be older than "
								   "game/Data: run tools/generate_datatable_assets.py"));
				return false;
			}
			return Test.TestTrue(*FString::Printf(TEXT("set-up: the Special is Harrower, OnDeath=Leap, OnDeathRange=10, "
													   "Burn=1 (%s, %s, %.1f cm, burns %d)"),
												  *Harrower->SkillName, *Harrower->Params.OnDeath,
												  Harrower->Params.OnDeathRangeCm, Harrower->Params.bBurns ? 1 : 0),
				Harrower->SkillName == TEXT("Harrower")
					&& Harrower->Params.OnDeath.Equals(TEXT("Leap"), ESearchCase::IgnoreCase)
					&& FMath::IsNearlyEqual(Harrower->Params.OnDeathRangeCm, 10.0f * M, 0.5f)
					&& Harrower->Params.bBurns);
		}

		/**
		 * Throw Harrower, finish its wait for the clip by moving the clock, and step every axe in the air until each has
		 * finished. Whether the throw started, released and landed.
		 */
		bool Throw(UWorld* World) const
		{
			if (!System->TryActivateAbility(HarrowerHandle, /*bAllowRemoteActivation=*/true))
			{
				return false;
			}
			for (int32 Each = 0; Each < 50 && Harrower->IsWaitingForTheSwingToConnect(); ++Each)
			{
				CataclysmTestWorld::RunClock(World, 0.1f);
			}
			if (Harrower->IsWaitingForTheSwingToConnect())
			{
				return false;
			}
			TSet<const ACataclysmProjectile*> Done;
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
					}
				}
			}
			return Done.Num() > 0;
		}

		ACataclysmPlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		UCataclysmProjectileSkill* Harrower = nullptr;
		FGameplayAbilitySpecHandle HarrowerHandle;
	};
}

#define CATACLYSM_HARROWER_LEAPS_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// 1. A thrown axe leaps to a second enemy, then a third, and stops with nothing in reach
// ==========================================================================

CATACLYSM_HARROWER_LEAPS_TEST(FCataclysmHarrowerLeapsChainTest,
	"Cataclysm.HarrowerLeaps.AThrownAxeLeapsToASecondEnemyThenAThirdAndStopsWhenNoneIsInReach")
{
	using namespace CataclysmHarrowerLeapsTest;

	// AN AXE PLAYER AT THE ORIGIN FACING +X. THE HOST stands 4 m along X, in the throw's path. THE SECOND 6 m beyond it
	// along Y, THE THIRD 6 m beyond that, 12 m from the host, and THE FAR ONE 13 m beyond the third, outside the 10 m
	// from every body the axe reaches. None of the last three is in the throw's path, which is a metre wide along X.
	FHarrowerLeapsWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FHarrowerLeapsLogLines Log;
	FHarrowerLeapsRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Host = HarrowerLeapsEnemy(World, FVector(4.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Second = HarrowerLeapsEnemy(World, FVector(4.0f * M, 6.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Third = HarrowerLeapsEnemy(World, FVector(4.0f * M, 12.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Far = HarrowerLeapsEnemy(World, FVector(4.0f * M, 25.0f * M, 0.0f));
	if (!TestTrue(TEXT("set-up: four enemies"), Host && Second && Third && Far))
	{
		return false;
	}

	// THE THROW.
	if (!TestTrue(TEXT("set-up: Harrower was thrown, released and landed"), Rig.Throw(World)))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("the throw struck the host: %.1f"), HarrowerLeapsTaken(Host)),
		HarrowerLeapsTaken(Host) > 0.0f);
	TestTrue(TEXT("and set it alight"), HarrowerLeapsAlight(Host));
	const UCataclysmBuriedWeapon* InHost = HarrowerLeapsWeaponIn(Host);
	if (!TestNotNull(TEXT("the axe is buried in the host"), InHost))
	{
		return false;
	}
	TestEqual(TEXT("named for the skill that threw it"), InHost->SkillName, FString(TEXT("Harrower")));
	TestEqual(TEXT("one line says it is buried"), Log.Count(TEXT("is buried in")), 1);
	TestEqual(TEXT("control: nobody else was struck by the throw"),
		HarrowerLeapsTaken(Second) + HarrowerLeapsTaken(Third) + HarrowerLeapsTaken(Far), 0.0f, 0.001f);
	const bool bDrawn = InHost->DrawnWeapon != nullptr;
	const TWeakObjectPtr<UStaticMeshComponent> DrawnInHost = InHost->DrawnWeapon.Get();

	// THE HOST DIES: THE AXE LEAPS TO THE SECOND, 6 M AWAY.
	TestTrue(TEXT("set-up: the host dies"), UCataclysmSkillEffects::MarkDead(Host));
	TestTrue(*FString::Printf(TEXT("THE AXE LEAPT TO THE SECOND ENEMY, 6 M FROM THE HOST: %.1f"),
							  HarrowerLeapsTaken(Second)),
		HarrowerLeapsTaken(Second) > 0.0f);
	TestTrue(TEXT("and set it alight"), HarrowerLeapsAlight(Second));
	const UCataclysmBuriedWeapon* InSecond = HarrowerLeapsWeaponIn(Second);
	TestTrue(TEXT("it is buried in the second, one leap on"), InSecond && InSecond->Leaps == 1);
	TestNull(TEXT("and is out of the host"), HarrowerLeapsWeaponIn(Host));
	TestEqual(TEXT("one line says it tore free"), Log.Count(TEXT("tore free of")), 1);
	TestEqual(TEXT("control: the third and the far one are untouched"),
		HarrowerLeapsTaken(Third) + HarrowerLeapsTaken(Far), 0.0f, 0.001f);

	// THE SECOND DIES: IT LEAPS TO THE THIRD, 6 M ON.
	TestTrue(TEXT("set-up: the second dies"), UCataclysmSkillEffects::MarkDead(Second));
	TestTrue(*FString::Printf(TEXT("THE AXE LEAPT ON TO THE THIRD ENEMY: %.1f"), HarrowerLeapsTaken(Third)),
		HarrowerLeapsTaken(Third) > 0.0f);
	TestTrue(TEXT("and set it alight"), HarrowerLeapsAlight(Third));
	const UCataclysmBuriedWeapon* InThird = HarrowerLeapsWeaponIn(Third);
	TestTrue(TEXT("it is buried in the third, two leaps on"), InThird && InThird->Leaps == 2);
	TestNull(TEXT("and is out of the second"), HarrowerLeapsWeaponIn(Second));
	TestEqual(TEXT("two lines say it tore free"), Log.Count(TEXT("tore free of")), 2);
	TestEqual(TEXT("control: the far one is untouched"), HarrowerLeapsTaken(Far), 0.0f, 0.001f);

	// THE THIRD DIES: NOTHING LIVING IS WITHIN 10 M OF IT, SO THE AXE STAYS.
	TestTrue(TEXT("set-up: the third dies"), UCataclysmSkillEffects::MarkDead(Third));
	TestEqual(TEXT("WITH NOTHING IN REACH THE AXE STOPS: the far one, 13 m away, is untouched"),
		HarrowerLeapsTaken(Far), 0.0f, 0.001f);
	TestTrue(TEXT("and it stays in the third"), HarrowerLeapsWeaponIn(Third) == InThird);
	TestEqual(TEXT("one line says it stays"), Log.Count(TEXT("stays in")), 1);
	TestEqual(TEXT("and no third line says it tore free"), Log.Count(TEXT("tore free of")), 2);

	// AND WHAT IS DRAWN FOLLOWS IT, WHERE THE ART IS ON THE MACHINE.
	if (bDrawn)
	{
		TestFalse(TEXT("the axe drawn in the host was taken out of it"), DrawnInHost.IsValid());
		TestTrue(TEXT("and one is drawn in the third, attached to it"),
			InThird && InThird->DrawnWeapon && InThird->DrawnWeapon->GetOwner() == Third
				&& InThird->DrawnWeapon->GetAttachParent() == Third->GetRootComponent());
	}
	else
	{
		CataclysmTestSkip::ReportSkippedHalf(*this, TEXT("the axe's mesh did not load, so nothing is drawn and where it "
			"is drawn is not checked; that the axe leaps, strikes and burns is. game/Content/Medieval_Weapons/ is not on "
			"this machine; see game/docs/weapon-source-assets.md."));
	}
	return true;
}

// ==========================================================================
// 2. A leap that kills its new host tears free again and goes on
// ==========================================================================

CATACLYSM_HARROWER_LEAPS_TEST(FCataclysmHarrowerLeapsKillGoesOnTest,
	"Cataclysm.HarrowerLeaps.ALeapThatKillsItsNewHostTearsFreeAgainAndGoesOn")
{
	using namespace CataclysmHarrowerLeapsTest;

	// AN AXE PLAYER AT THE ORIGIN. THE HOST, 20 m along Y. THE DOOMED, of 10 health, 4 m east of the host, the nearest to
	// it; THE NEXT 4 m east of the doomed and 8 m from the host; THE FAR ONE 17 m east of the next. The axe is buried in
	// the host as a landing throw buries it, by Harrower's own `BuryInStruck`.
	FHarrowerLeapsWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	UWorld* World = Scope.World;
	FHarrowerLeapsLogLines Log;
	FHarrowerLeapsRig Rig(World, FVector::ZeroVector);
	if (!Rig.IsUsable(*this))
	{
		return false;
	}
	constexpr float Brittle = 10.0f;
	const FVector North(0.0f, 20.0f * M, 0.0f);
	ACataclysmEnemyCharacter* Host = HarrowerLeapsEnemy(World, North);
	ACataclysmEnemyCharacter* Doomed = HarrowerLeapsEnemy(World, North + FVector(4.0f * M, 0.0f, 0.0f), Brittle);
	ACataclysmEnemyCharacter* Next = HarrowerLeapsEnemy(World, North + FVector(8.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Far = HarrowerLeapsEnemy(World, North + FVector(25.0f * M, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: four enemies"), Host && Doomed && Next && Far)
		|| !TestEqual(TEXT("set-up: the axe is buried in the host"),
			   Rig.Harrower->BuryInStruck(TArray<AActor*>{static_cast<AActor*>(Host)}), 1))
	{
		return false;
	}

	TestTrue(TEXT("set-up: the host dies"), UCataclysmSkillEffects::MarkDead(Host));
	TestTrue(TEXT("set-up: the axe leapt to the doomed, 4 m away, and its blow killed it"),
		UCataclysmSkillEffects::IsDead(Doomed));
	TestTrue(*FString::Printf(TEXT("THE AXE TORE FREE OF THE BODY ITS OWN BLOW KILLED AND STRUCK THE NEXT, 4 M ON: %.1f"),
							  HarrowerLeapsTaken(Next)),
		HarrowerLeapsTaken(Next) > 0.0f);
	TestTrue(TEXT("and set it alight"), HarrowerLeapsAlight(Next));
	const UCataclysmBuriedWeapon* InNext = HarrowerLeapsWeaponIn(Next);
	TestTrue(TEXT("it is buried in the next, two leaps on"), InNext && InNext->Leaps == 2);
	TestNull(TEXT("and in neither body behind it"), HarrowerLeapsWeaponIn(Doomed));
	TestNull(TEXT("nor the host"), HarrowerLeapsWeaponIn(Host));
	TestEqual(TEXT("two lines say it tore free"), Log.Count(TEXT("tore free of")), 2);
	TestEqual(TEXT("and the first says its blow killed what it struck"), Log.Count(TEXT("the blow killed it")), 1);
	TestEqual(TEXT("control: the far one, 17 m from the next, is untouched"), HarrowerLeapsTaken(Far), 0.0f, 0.001f);
	return true;
}

#undef CATACLYSM_HARROWER_LEAPS_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
