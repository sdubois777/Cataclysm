// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmBonePyreCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Eight of War's nine enemy modifiers, written 2026-11-25 ahead of their window on the owner's rulings of 2026-11-24
 * (the Death and War plan's Q10 to Q12). One test each, each with a control where the modifier is absent. 1 On 1 is
 * not built and has no test. The rows are `game/Data/EnemyModifiers.csv`; every figure is in
 * `UCataclysmEnemyModifiers` and `docs/DECISIONS.md`.
 *
 * THE RIG IS THE DEATH MODIFIER TESTS': a possessed player with 1000 health and nothing that could turn a blow aside,
 * and creatures a few metres off. A test blow can be neither evaded nor critical, so a figure is exact, and every
 * figure that depends on the creature's own numbers is read off it rather than typed.
 *
 * BATTLE CRY READS ITS ROW. `Debuff_Battle_Cry` in `game/Data/StatusEffects.csv` names the eight resistances it
 * moves, so its test needs `DT_StatusEffects` rebuilt from that CSV first; the window's editor step does it.
 */
namespace CataclysmWarModifiersTest
{
	using Vital = UCataclysmVitalAttributeSet;

	constexpr float WarModifiersMostHealth = 1000.0f;

	/** The player's eight resistances, which Battle Cry cuts. */
	TArray<FGameplayAttribute> WarPlayerResistances()
	{
		return {
			UCataclysmResistanceAttributeSet::GetWarResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetPestilenceResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetFamineResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetCelestialResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetChaosResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetVoidResistanceAttribute(),
		};
	}

	/** A player the game finds as "the player": a player state, a controller possessing a pawn. */
	struct FWarModifiersPlayer
	{
		explicit FWarModifiersPlayer(UWorld* World, const FVector& Where = FVector::ZeroVector)
		{
			State = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<APlayerController>(APlayerController::StaticClass());
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			System = Cast<UCataclysmAbilitySystemComponent>(Character->GetAbilitySystemComponent());
			Expose();
		}

		/**
		 * 1000 health of 1000 and nothing that could turn a blow aside or shrink it: no energy shield, armour, evasion,
		 * block, damage reduction or resistance, and no regeneration. Its own attack damage 100, for its projectile.
		 */
		void Expose() const
		{
			if (!System)
			{
				return;
			}
			System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), WarModifiersMostHealth);
			System->SetNumericAttributeBase(Vital::GetHealthAttribute(), WarModifiersMostHealth);
			System->SetNumericAttributeBase(Vital::GetMaxEnergyShieldAttribute(), 0.0f);
			System->SetNumericAttributeBase(Vital::GetEnergyShieldAttribute(), 0.0f);
			System->SetNumericAttributeBase(Vital::GetHealthRegenAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
			for (const FGameplayAttribute& Resistance : WarPlayerResistances())
			{
				if (System->HasAttributeSetForAttribute(Resistance))
				{
					System->SetNumericAttributeBase(Resistance, 0.0f);
				}
			}
		}

		bool IsUsable() const { return Character && System; }
		float Read(const FGameplayAttribute& Attribute) const { return System->GetNumericAttribute(Attribute); }
		float Health() const { return Read(Vital::GetHealthAttribute()); }

		ACataclysmPlayerState* State = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A plain creature of the monsters' side with 1000 health that cannot dodge, carrying `Row` when one is named. */
	ACataclysmEnemyCharacter* WarCreatureAt(UWorld* World, const FVector& Where, const TCHAR* Row)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), Where, FRotator::ZeroRotator, Spawn);
		if (!Creature)
		{
			return nullptr;
		}
		if (Row)
		{
			Creature->ModifierRows.Add(FName(Row));
		}
		// THE ROW FIRST, THEN THE SETTERS, so the stat block each setter writes is the one the row changes.
		Creature->SetHealth(WarModifiersMostHealth);
		Creature->SetAttackDamage(20.0f);
		if (UAbilitySystemComponent* System = Creature->GetAbilitySystemComponent())
		{
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		}
		return Creature;
	}

	/** A blow that can be neither evaded nor critical, and what it took from `Struck`'s health. */
	float WarBlowTook(AActor* From, AActor* Struck, float Damage)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Struck);
		if (!System)
		{
			return -1.0f;
		}
		const float Before = System->GetNumericAttribute(Vital::GetHealthAttribute());
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotBeEvaded = true;
		Delivery.bCannotCriticallyStrike = true;
		UCataclysmSkillEffects::ApplyDirectDamage(From, Struck, Damage, Delivery);
		return Before - System->GetNumericAttribute(Vital::GetHealthAttribute());
	}

	/** What one of `Firer`'s projectiles, fired from five metres off, took from `Struck`'s health. */
	float WarProjectileTook(AActor* Firer, AActor* Struck)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Struck);
		if (!System || !Firer)
		{
			return -1.0f;
		}
		const float Before = System->GetNumericAttribute(Vital::GetHealthAttribute());
		const FVector At = Struck->GetActorLocation();
		const FVector From = At - FVector(500.0f, 0.0f, 0.0f);
		ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
			Firer, From, At + FVector(500.0f, 0.0f, 0.0f), /*InRadiusCm=*/50.0f, /*InSpeed=*/1200.0f,
			/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f, FGameplayTagContainer(),
			/*bInBurns=*/false, /*InBodyMesh=*/nullptr, /*InFlightSeconds=*/0.0f, /*InCritChancePercent=*/0.0f);
		for (int32 Step = 0; Shot && Step < 100 && Shot->Step(0.05f); ++Step)
		{
		}
		return Before - System->GetNumericAttribute(Vital::GetHealthAttribute());
	}

	/** Steps of the per-character step on `Character`'s modifiers that act on a timer, a quarter second each. */
	void WarSteps(AActor* Character, int32 Steps)
	{
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			UCataclysmEnemyModifiers::TimedStep(Character, 0.25f);
		}
	}

	/** Steps of the aura step, a quarter second each: four of them are one pulse. */
	void WarAuraSteps(AActor* Character, int32 Steps)
	{
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			UCataclysmEnemyModifiers::AuraStep(Character, 0.25f);
		}
	}

	FGameplayTag WarStatus(const TCHAR* Name) { return UCataclysmSkillShapes::StatusTagFor(Name); }

	float WarRead(const ACataclysmEnemyCharacter* Creature, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* System = Creature ? Creature->GetAbilitySystemComponent() : nullptr;
		return System ? System->GetNumericAttribute(Attribute) : -1.0f;
	}
}

#define CATACLYSM_WAR_MODIFIER_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Change W1: the creature's own figures. Armored Juggernaut, Battle Scarred, Shield Wall.
// ---------------------------------------------------------------------------

CATACLYSM_WAR_MODIFIER_TEST(FCataclysmWarModifierArmoredJuggernautTest,
	"Cataclysm.WarModifiers.ArmoredJuggernautHasHalfAgainItsOwnArmourAndAnUnarmouredKindNone")
{
	using namespace CataclysmWarModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Armoured =
		WarCreatureAt(World, FVector(500.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::ArmoredJuggernautRow);
	ACataclysmEnemyCharacter* Plain = WarCreatureAt(World, FVector(-500.0f, 0.0f, 0.0f), nullptr);
	ACataclysmEnemyCharacter* Bare =
		WarCreatureAt(World, FVector(0.0f, 500.0f, 0.0f), UCataclysmEnemyModifiers::ArmoredJuggernautRow);
	if (!TestNotNull(TEXT("set-up: an Armored Juggernaut"), Armoured) || !TestNotNull(TEXT("set-up: a plain creature"), Plain)
		|| !TestNotNull(TEXT("set-up: an unarmoured carrier"), Bare))
	{
		return false;
	}

	// THE SAME DESIGNED ARMOUR ON THE TWO, AND NONE ON THE THIRD, written through the setter every spawner uses.
	Armoured->SetArmour(200.0f);
	Plain->SetArmour(200.0f);
	Bare->SetArmour(0.0f);
	const FGameplayAttribute Armour = UCataclysmCombatAttributeSet::GetArmorAttribute();
	const float PlainArmour = WarRead(Plain, Armour);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the plain creature holds %.2f armour"), PlainArmour), PlainArmour > 1.0f))
	{
		return false;
	}
	TestEqual(TEXT("the carrier holds half again the plain creature's armour"), WarRead(Armoured, Armour),
			  PlainArmour * 1.5f, 0.01f);
	TestEqual(TEXT("a kind designed with no armour gains none"), WarRead(Bare, Armour), 0.0f, 0.01f);

	// AND IT DOES NOT COMPOUND: the stat block is written again every time a spawner sets anything.
	Armoured->ApplyStartingAttributes();
	TestEqual(TEXT("writing the stat block again leaves it at half again"), WarRead(Armoured, Armour),
			  PlainArmour * 1.5f, 0.01f);
	TestEqual(TEXT("and the pure figure says the same"),
			  UCataclysmEnemyModifiers::ArmourMultiplier({FName(UCataclysmEnemyModifiers::ArmoredJuggernautRow)}), 1.5f);
	TestEqual(TEXT("and one without the row"), UCataclysmEnemyModifiers::ArmourMultiplier(TArray<FName>()), 1.0f);
	return true;
}

CATACLYSM_WAR_MODIFIER_TEST(FCataclysmWarModifierBattleScarredTest,
	"Cataclysm.WarModifiers.BattleScarredDoublesItsCriticalChanceAndAddsThirtyResistance")
{
	using namespace CataclysmWarModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FWarModifiersPlayer Player(World);
	ACataclysmEnemyCharacter* Scarred =
		WarCreatureAt(World, FVector(500.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::BattleScarredRow);
	ACataclysmEnemyCharacter* Plain = WarCreatureAt(World, FVector(-500.0f, 0.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Battle Scarred"), Scarred)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}

	const FGameplayAttribute Crit = UCataclysmCombatAttributeSet::GetCritChanceAttribute();
	const FGameplayAttribute Resist = UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute();
	const float PlainCrit = WarRead(Plain, Crit);
	const float PlainResist = WarRead(Plain, Resist);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the plain creature's critical chance is %.2f"), PlainCrit),
				  PlainCrit > 0.0f && PlainResist >= 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the carrier's critical strike chance is double its kind's"), WarRead(Scarred, Crit), PlainCrit * 2.0f,
			  0.01f);
	TestEqual(TEXT("and its resistance is thirty more"), WarRead(Scarred, Resist), PlainResist + 30.0f, 0.01f);

	// AND THE RESISTANCE IS MET: the player's blow takes 70% of what it takes from the plain creature, when that one
	// resists nothing.
	if (FMath::IsNearlyZero(PlainResist))
	{
		const float OnScarred = WarBlowTook(Player.Character, Scarred, 100.0f);
		const float OnPlain = WarBlowTook(Player.Character, Plain, 100.0f);
		if (TestTrue(*FString::Printf(TEXT("set-up: the plain blow took %.2f"), OnPlain), OnPlain > 1.0f))
		{
			TestEqual(TEXT("so a blow on the carrier takes 70% of the plain one"), OnScarred / OnPlain, 0.7f, 0.01f);
		}
	}

	// OVERPOWERED STILL WINS: its certain critical strike is written after.
	Scarred->ModifierRows.Add(FName(UCataclysmEnemyModifiers::OverpoweredRow));
	Scarred->ApplyStartingAttributes();
	TestEqual(TEXT("with Overpowered too, it always crits"), WarRead(Scarred, Crit), 100.0f, 0.01f);
	return true;
}

CATACLYSM_WAR_MODIFIER_TEST(FCataclysmWarModifierShieldWallTest,
	"Cataclysm.WarModifiers.ShieldWallTakesAQuarterOfAProjectileAndEveryOtherBlowWhole")
{
	using namespace CataclysmWarModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FWarModifiersPlayer Player(World, FVector(0.0f, -2000.0f, 0.0f));
	ACataclysmEnemyCharacter* Walled =
		WarCreatureAt(World, FVector(0.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::ShieldWallRow);
	ACataclysmEnemyCharacter* Plain = WarCreatureAt(World, FVector(0.0f, 1500.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Shield Wall"), Walled)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}

	TestEqual(TEXT("the share a projectile keeps on the carrier is a quarter"),
			  UCataclysmEnemyModifiers::ShareOfProjectileDamageTaken(Walled), 0.25f);
	TestEqual(TEXT("and on the plain creature, all of it"), UCataclysmEnemyModifiers::ShareOfProjectileDamageTaken(Plain),
			  1.0f);

	// A REAL PROJECTILE OF THE PLAYER'S AT EACH, from five metres off.
	const float OnPlain = WarProjectileTook(Player.Character, Plain);
	const float OnWalled = WarProjectileTook(Player.Character, Walled);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the projectile took %.2f from the plain creature"), OnPlain),
				  OnPlain > 1.0f))
	{
		return false;
	}
	TestEqual(TEXT("a projectile takes a quarter as much from the carrier"), OnWalled / OnPlain, 0.25f, 0.01f);

	// THE CONTROL: A BLOW THAT IS NOT A PROJECTILE takes as much from either.
	const float StruckWalled = WarBlowTook(Player.Character, Walled, 100.0f);
	const float StruckPlain = WarBlowTook(Player.Character, Plain, 100.0f);
	TestTrue(*FString::Printf(TEXT("set-up: the blow took %.2f"), StruckPlain), StruckPlain > 1.0f);
	TestEqual(TEXT("a blow that is not a projectile takes as much from the carrier"), StruckWalled, StruckPlain, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// Change W2: its allies. Commander, Drums of War, Phalanx.
// ---------------------------------------------------------------------------

CATACLYSM_WAR_MODIFIER_TEST(FCataclysmWarModifierCommanderTest,
	"Cataclysm.WarModifiers.CommanderGrantsTheCommanderBuffToAlliesWithinEightMetres")
{
	using namespace CataclysmWarModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Commander =
		WarCreatureAt(World, FVector::ZeroVector, UCataclysmEnemyModifiers::CommanderRow);
	ACataclysmEnemyCharacter* Near = WarCreatureAt(World, FVector(600.0f, 0.0f, 0.0f), nullptr);
	ACataclysmEnemyCharacter* Far = WarCreatureAt(World, FVector(0.0f, 1000.0f, 0.0f), nullptr);
	// THE CONTROL: A CREATURE CARRYING ANOTHER MODIFIER, so its aura step runs at all, with an ally beside it.
	ACataclysmEnemyCharacter* Other =
		WarCreatureAt(World, FVector(-3000.0f, 0.0f, 0.0f), UCataclysmEnemyModifiers::HellfireAuraRow);
	ACataclysmEnemyCharacter* BesideOther = WarCreatureAt(World, FVector(-2700.0f, 0.0f, 0.0f), nullptr);
	if (!TestNotNull(TEXT("set-up: a Commander"), Commander) || !TestNotNull(TEXT("set-up: an ally near"), Near)
		|| !TestNotNull(TEXT("set-up: an ally far"), Far) || !TestNotNull(TEXT("set-up: another carrier"), Other)
		|| !TestNotNull(TEXT("set-up: its ally"), BesideOther))
	{
		return false;
	}
	const float Inspired = 1.0f + ACataclysmEnemyCharacter::CommanderIncreasePercent / 100.0f;

	WarAuraSteps(Commander, 3);
	WarAuraSteps(Other, 4);
	TestEqual(TEXT("three quarters of a second in, no pulse yet"), Near->CommanderMultiplier(), 1.0f, 0.001f);
	TestEqual(TEXT("a creature carrying another aura inspires nobody"), BesideOther->CommanderMultiplier(), 1.0f, 0.001f);

	WarAuraSteps(Commander, 1);
	TestEqual(TEXT("a second in, the ally six metres off is 20% faster"), Near->CommanderMultiplier(), Inspired, 0.001f);
	TestEqual(TEXT("and its speed multiplier says so"), Near->SpeedMultiplier(), Inspired, 0.001f);
	TestEqual(TEXT("the ally ten metres off is not"), Far->CommanderMultiplier(), 1.0f, 0.001f);
	TestEqual(TEXT("nor is the Commander itself"), Commander->CommanderMultiplier(), 1.0f, 0.001f);

	// IT LAPSES WITHIN TWO SECONDS OF THE ALLY LEAVING: moved far off, and the clock run past the grant.
	Near->SetActorLocation(FVector(6000.0f, 0.0f, 0.0f));
	CataclysmTestWorld::RunClock(World, 2.5f);
	TestEqual(TEXT("two and a half seconds after it left, the ally is back to its own speed"), Near->CommanderMultiplier(),
			  1.0f, 0.001f);
	return true;
}

CATACLYSM_WAR_MODIFIER_TEST(FCataclysmWarModifierDrumsOfWarTest,
	"Cataclysm.WarModifiers.DrumsOfWarGrantsHalfAgainSpeedToAlliesWithinFourMetres")
{
	using namespace CataclysmWarModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Drummer =
		WarCreatureAt(World, FVector::ZeroVector, UCataclysmEnemyModifiers::DrumsOfWarRow);
	ACataclysmEnemyCharacter* Near = WarCreatureAt(World, FVector(300.0f, 0.0f, 0.0f), nullptr);
	// SIX METRES: INSIDE COMMANDER'S EIGHT AND OUTSIDE THE DRUMS' FOUR, which is the owner's ruling.
	ACataclysmEnemyCharacter* Beyond = WarCreatureAt(World, FVector(0.0f, 600.0f, 0.0f), nullptr);
	if (!TestNotNull(TEXT("set-up: a Drums of War"), Drummer) || !TestNotNull(TEXT("set-up: an ally near"), Near)
		|| !TestNotNull(TEXT("set-up: an ally beyond"), Beyond))
	{
		return false;
	}
	const float Drummed = 1.0f + ACataclysmEnemyCharacter::DrumsOfWarIncreasePercent / 100.0f;
	TestEqual(TEXT("the reach is half Commander's"), UCataclysmEnemyModifiers::DrumsOfWarReachCm,
			  UCataclysmEnemyModifiers::CommanderReachCm * 0.5f, 0.01f);

	WarAuraSteps(Drummer, 3);
	TestEqual(TEXT("before the first pulse, nothing"), Near->SpeedMultiplier(), 1.0f, 0.001f);
	WarAuraSteps(Drummer, 1);
	TestEqual(TEXT("a second in, the ally three metres off moves and attacks half again as fast"),
			  Near->SpeedMultiplier(), Drummed, 0.001f);
	TestEqual(TEXT("through the Drums of War buff"), Near->DrumsOfWarMultiplier(), Drummed, 0.001f);
	TestEqual(TEXT("and none of it is Commander"), Near->CommanderMultiplier(), 1.0f, 0.001f);
	TestTrue(TEXT("and the status line names it"),
			 UCataclysmCombatOverlay::WarModifierTextFor(Near).Contains(TEXT("Drums of War")));
	TestEqual(TEXT("the ally six metres off gains nothing"), Beyond->SpeedMultiplier(), 1.0f, 0.001f);
	TestEqual(TEXT("nor does the drummer itself"), Drummer->SpeedMultiplier(), 1.0f, 0.001f);
	return true;
}

CATACLYSM_WAR_MODIFIER_TEST(FCataclysmWarModifierPhalanxTest,
	"Cataclysm.WarModifiers.PhalanxTakesTenPercentLessForEveryOtherCreatureWithinFiveMetres")
{
	using namespace CataclysmWarModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FWarModifiersPlayer Player(World, FVector(0.0f, -3000.0f, 0.0f));
	ACataclysmEnemyCharacter* Braced = WarCreatureAt(World, FVector::ZeroVector, UCataclysmEnemyModifiers::PhalanxRow);
	ACataclysmEnemyCharacter* Plain = WarCreatureAt(World, FVector(5000.0f, 0.0f, 0.0f), nullptr);
	ACataclysmEnemyCharacter* One = WarCreatureAt(World, FVector(300.0f, 0.0f, 0.0f), nullptr);
	ACataclysmEnemyCharacter* Two = WarCreatureAt(World, FVector(-300.0f, 0.0f, 0.0f), nullptr);
	ACataclysmEnemyCharacter* Three = WarCreatureAt(World, FVector(0.0f, 300.0f, 0.0f), nullptr);
	ACataclysmEnemyCharacter* Outside = WarCreatureAt(World, FVector(0.0f, 900.0f, 0.0f), nullptr);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Phalanx"), Braced)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain) || !TestNotNull(TEXT("set-up: three allies"), One)
		|| !TestNotNull(TEXT("set-up: three allies"), Two) || !TestNotNull(TEXT("set-up: three allies"), Three)
		|| !TestNotNull(TEXT("set-up: one outside"), Outside))
	{
		return false;
	}

	// AND A FLOOR-RULE OBJECT OF ITS SIDE BESIDE IT, which is no ally in the row's sense.
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ACataclysmBonePyreCharacter* Object = World->SpawnActor<ACataclysmBonePyreCharacter>(
		ACataclysmBonePyreCharacter::StaticClass(), FVector(0.0f, -300.0f, 0.0f), FRotator::ZeroRotator, Spawn);
	if (Object)
	{
		Object->SetGenericTeamId(UCataclysmTeams::TeamOf(Braced));
	}

	TestEqual(TEXT("the pure figure: three allies are 30"), UCataclysmEnemyModifiers::PhalanxDamageReductionFor(3), 30.0f,
			  0.01f);

	WarSteps(Braced, 1);
	TestEqual(TEXT("it counts the three creatures within five metres and nothing else"), Braced->PhalanxAlliesCounted, 3);
	TestEqual(TEXT("and holds 30 damage reduction"),
			  WarRead(Braced, UCataclysmCombatAttributeSet::GetDamageReductionAttribute()), 30.0f, 0.01f);
	TestTrue(TEXT("and the status line says so"),
			 UCataclysmCombatOverlay::WarModifierTextFor(Braced).Contains(TEXT("Phalanx -30%")));

	const float OnPlain = WarBlowTook(Player.Character, Plain, 100.0f);
	const float OnBraced = WarBlowTook(Player.Character, Braced, 100.0f);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the plain blow took %.2f"), OnPlain), OnPlain > 1.0f))
	{
		return false;
	}
	TestEqual(TEXT("so a blow on it takes 70% of the plain one"), OnBraced / OnPlain, 0.7f, 0.01f);

	// PULL TWO AWAY AND IT IS 10.
	One->SetActorLocation(FVector(4000.0f, 4000.0f, 0.0f));
	Two->SetActorLocation(FVector(-4000.0f, 4000.0f, 0.0f));
	WarSteps(Braced, 1);
	TestEqual(TEXT("with two pulled away it counts one"), Braced->PhalanxAlliesCounted, 1);
	const float OnBracedAlone = WarBlowTook(Player.Character, Braced, 100.0f);
	TestEqual(TEXT("and a blow takes 90% of the plain one"), OnBracedAlone / OnPlain, 0.9f, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// Change W3: attacks on the player. Battle Cry, Artillery Barrage.
// ---------------------------------------------------------------------------

CATACLYSM_WAR_MODIFIER_TEST(FCataclysmWarModifierBattleCryTest,
	"Cataclysm.WarModifiers.BattleCryCutsEveryResistanceByTwentyFiveForFiveSecondsEveryEightSeconds")
{
	using namespace CataclysmWarModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FWarModifiersPlayer Player(World, FVector(300.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Shouter = WarCreatureAt(World, FVector::ZeroVector, UCataclysmEnemyModifiers::BattleCryRow);
	ACataclysmEnemyCharacter* Plain =
		WarCreatureAt(World, FVector(300.0f, 300.0f, 0.0f), UCataclysmEnemyModifiers::GraveTouchRow);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a Battle Cry"), Shouter)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}
	const FGameplayTag Cry = WarStatus(TEXT("Battle Cry"));
	if (!TestTrue(TEXT("set-up: the Battle Cry status tag exists"), Cry.IsValid()))
	{
		return false;
	}

	// FORTY IN EVERY RESISTANCE, AND TEN IN ONE, so the cut shows and so does its floor.
	const TArray<FGameplayAttribute> Resistances = WarPlayerResistances();
	for (const FGameplayAttribute& Resistance : Resistances)
	{
		Player.System->SetNumericAttributeBase(Resistance, 40.0f);
	}
	Player.System->SetNumericAttributeBase(UCataclysmResistanceAttributeSet::GetVoidResistanceAttribute(), 10.0f);

	// THE CONTROL: a creature carrying another modifier, stepped well past eight seconds, never shouts.
	WarSteps(Plain, 40);
	TestFalse(TEXT("a plain creature never shouts"), UCataclysmSkillEffects::HasTag(Player.Character, Cry));

	WarSteps(Shouter, 31);
	TestFalse(TEXT("7.75 seconds in, no shout yet"), UCataclysmSkillEffects::HasTag(Player.Character, Cry));
	WarSteps(Shouter, 1);
	TestTrue(TEXT("8 seconds in, the player carries Battle Cry"), UCataclysmSkillEffects::HasTag(Player.Character, Cry));
	for (const FGameplayAttribute& Resistance : Resistances)
	{
		const bool bVoid = Resistance == UCataclysmResistanceAttributeSet::GetVoidResistanceAttribute();
		TestEqual(*FString::Printf(TEXT("%s is cut by 25, never below nought (needs DT_StatusEffects rebuilt from "
										"game/Data/StatusEffects.csv)"), *Resistance.GetName()),
				  Player.Read(Resistance), bVoid ? 0.0f : 15.0f, 0.01f);
	}
	TestTrue(TEXT("and the status line names it"),
			 UCataclysmCombatOverlay::WarModifierTextFor(Player.Character).Contains(TEXT("Battle Cry")));
	TestEqual(TEXT("and the clock started again"), Shouter->SecondsSinceBattleCry, 0.0f, 0.001f);

	// WITH NOBODY IN REACH THE CLOCK WAITS, AND THE SHOUT COMES THE STEP SOMEBODY IS.
	Player.Character->SetActorLocation(FVector(3000.0f, 0.0f, 0.0f));
	WarSteps(Shouter, 40);
	TestTrue(*FString::Printf(TEXT("ten seconds with nobody near, it is still waiting (%.2f s)"),
							  Shouter->SecondsSinceBattleCry),
			 Shouter->SecondsSinceBattleCry >= UCataclysmEnemyModifiers::BattleCryIntervalSeconds);
	Player.Character->SetActorLocation(FVector(300.0f, 0.0f, 0.0f));
	WarSteps(Shouter, 1);
	TestEqual(TEXT("and shouts the step the player is back"), Shouter->SecondsSinceBattleCry, 0.0f, 0.001f);

	// FIVE SECONDS AFTER THE LAST SHOUT IT IS GONE.
	CataclysmTestWorld::RunClock(World, 5.5f);
	TestFalse(TEXT("five and a half seconds on, Battle Cry has lapsed"),
			  UCataclysmSkillEffects::HasTag(Player.Character, Cry));
	TestEqual(TEXT("and the War resistance is forty again"),
			  Player.Read(UCataclysmResistanceAttributeSet::GetWarResistanceAttribute()), 40.0f, 0.01f);
	return true;
}

CATACLYSM_WAR_MODIFIER_TEST(FCataclysmWarModifierArtilleryBarrageTest,
	"Cataclysm.WarModifiers.ArtilleryBarrageDrawsThreeCirclesNearThePlayerThatLandInASecondAndAHalf")
{
	using namespace CataclysmWarModifiersTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FVector Standing(800.0f, 0.0f, 0.0f);
	const FWarModifiersPlayer Player(World, Standing);
	ACataclysmEnemyCharacter* Gunner =
		WarCreatureAt(World, FVector::ZeroVector, UCataclysmEnemyModifiers::ArtilleryBarrageRow);
	ACataclysmEnemyCharacter* Plain =
		WarCreatureAt(World, FVector(0.0f, 600.0f, 0.0f), UCataclysmEnemyModifiers::GraveTouchRow);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: an Artillery Barrage"), Gunner)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}
	const float ShellDamage =
		WarRead(Gunner, UCataclysmCombatAttributeSet::GetAttackDamageAttribute()) * 1.5f;
	if (!TestTrue(*FString::Printf(TEXT("set-up: a shell is worth %.2f"), ShellDamage), ShellDamage > 1.0f))
	{
		return false;
	}

	// THE CONTROL: a creature carrying another modifier calls nothing.
	WarSteps(Plain, 30);
	TestFalse(TEXT("a plain creature calls no barrage"), UCataclysmEnemyModifiers::IsArtilleryBarrageInTheAir(Plain));
	TestEqual(TEXT("and marks no ground"), Plain->ArtilleryShellsAt.Num(), 0);

	WarSteps(Gunner, 23);
	TestFalse(TEXT("5.75 seconds in, nothing called"), UCataclysmEnemyModifiers::IsArtilleryBarrageInTheAir(Gunner));
	WarSteps(Gunner, 1);
	if (!TestTrue(TEXT("6 seconds in, a volley is in the air"), UCataclysmEnemyModifiers::IsArtilleryBarrageInTheAir(Gunner))
		|| !TestEqual(TEXT("of three shells"), Gunner->ArtilleryShellsAt.Num(), 3))
	{
		return false;
	}
	for (const FVector& Shell : Gunner->ArtilleryShellsAt)
	{
		TestTrue(*FString::Printf(TEXT("each within four metres of the player: %.0f cm"), FVector::Dist2D(Shell, Standing)),
				 FVector::Dist2D(Shell, Standing) <= 400.0f + 1.0f);
	}
	TestTrue(TEXT("and the status line says a barrage is coming"),
			 UCataclysmCombatOverlay::WarModifierTextFor(Gunner).Contains(TEXT("Barrage")));

	// THE PLAYER STEPS ONTO THE FIRST CIRCLE. A second and a half after the call, it lands.
	const FVector OnTheFirst(Gunner->ArtilleryShellsAt[0].X, Gunner->ArtilleryShellsAt[0].Y, Standing.Z);
	Player.Character->SetActorLocation(OnTheFirst);
	const float Before = Player.Health();
	WarSteps(Gunner, 5);
	TestEqual(TEXT("1.25 seconds after the call, nothing has landed"), Player.Health(), Before, 0.01f);
	WarSteps(Gunner, 1);
	TestFalse(TEXT("1.5 seconds after the call, the volley has landed"),
			  UCataclysmEnemyModifiers::IsArtilleryBarrageInTheAir(Gunner));
	const float Shells = (Before - Player.Health()) / ShellDamage;
	TestTrue(*FString::Printf(TEXT("the player under the first circle took %.3f shells: a whole number, one to three"),
							  Shells),
			 Shells > 0.99f && Shells < 3.01f && FMath::Abs(Shells - FMath::RoundToFloat(Shells)) < 0.01f);
	TestEqual(TEXT("and the circles are gone"), Gunner->ArtilleryShellsAt.Num(), 0);

	// THE NEXT VOLLEY, SIX SECONDS AFTER THE LAST WAS CALLED: the clock ran through the warning, so 4.5 seconds after it
	// landed. THE PLAYER WALKS TWELVE METRES OFF: nothing lands on them.
	Player.Character->SetActorLocation(Standing);
	WarSteps(Gunner, 17);
	TestFalse(TEXT("4.25 seconds after the landing, nothing called"),
			  UCataclysmEnemyModifiers::IsArtilleryBarrageInTheAir(Gunner));
	WarSteps(Gunner, 1);
	if (!TestTrue(TEXT("six seconds after the last call, another volley"),
				  UCataclysmEnemyModifiers::IsArtilleryBarrageInTheAir(Gunner)))
	{
		return false;
	}
	Player.Character->SetActorLocation(Standing + FVector(0.0f, 1200.0f, 0.0f));
	const float BeforeAway = Player.Health();
	WarSteps(Gunner, 6);
	TestEqual(TEXT("a player who walked out of every circle takes nothing"), Player.Health(), BeforeAway, 0.01f);

	// AND A CALLER KILLED IN THE WARNING CALLS ITS VOLLEY OFF.
	Player.Character->SetActorLocation(Standing);
	WarSteps(Gunner, 18);
	if (!TestTrue(TEXT("a third volley"), UCataclysmEnemyModifiers::IsArtilleryBarrageInTheAir(Gunner)))
	{
		return false;
	}
	const FVector OnTheThird(Gunner->ArtilleryShellsAt[0].X, Gunner->ArtilleryShellsAt[0].Y, Standing.Z);
	Player.Character->SetActorLocation(OnTheThird);
	Gunner->HandleDeath();
	TestEqual(TEXT("killed, it has no shells in the air"), Gunner->ArtilleryShellsAt.Num(), 0);
	const float BeforeDead = Player.Health();
	WarSteps(Gunner, 6);
	TestEqual(TEXT("and nothing lands"), Player.Health(), BeforeDead, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
