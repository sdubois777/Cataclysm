// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmBonePyreCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmTargetCandidates.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Death's enemy modifiers on any wearer, the player above all: issue #2899 (2026-11-26), the owner's ruling of
 * 2026-11-25, "Everything that isn't a new ability that would require a button push should work for a player", and Wearer
 * of the Dead lending them, as the owner ruled on 2026-11-26: every Death modifier but Haunting and Final Curse, which are
 * never lent to the player. `Cataclysm.DeathModifiers.` tests each on a creature; this group tests each on a player.
 *
 * THE RIG IS `Cataclysm.DeathModifiers.`'s, rebuilt here under this group's own names so no unity build meets two of one
 * name: a possessed player laid bare (1000 health, no shield, defences, regeneration or leech, no stat line), on the
 * players' side, and creatures of the monsters' side. A player carries a modifier by its own `ModifierRows` in most
 * tests, and by a lending recorded on its ability system in the two that test the lending, since `CarriedBy` reads both.
 * A blow is direct, can be neither evaded nor critical, carries no weapon sub-type and does not leech, so a figure is
 * exact.
 */
namespace CataclysmDeathOnAnyWearerTest
{
	using AnyWearerVital = UCataclysmVitalAttributeSet;

	constexpr float AnyWearerMostHealth = 1000.0f;
	constexpr float AnyWearerMetre = 100.0f;

	/** A possessed player on the players' side, laid bare. */
	struct FAnyWearerPlayer
	{
		explicit FAnyWearerPlayer(UWorld* World, const FVector& Where = FVector::ZeroVector)
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
			Character->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			System = Cast<UCataclysmAbilitySystemComponent>(Character->GetAbilitySystemComponent());
			LayBare();
		}

		/** Moves `Attribute`'s base until its current value reads `Wanted`, as `Cataclysm.DeathModifiers.`'s rig does. */
		void SetCurrent(const FGameplayAttribute& Attribute, float Wanted) const
		{
			if (!System->HasAttributeSetForAttribute(Attribute))
			{
				return;
			}
			for (int32 Try = 0; Try < 4; ++Try)
			{
				const float Gap = Wanted - System->GetNumericAttribute(Attribute);
				if (FMath::IsNearlyZero(Gap, 0.0001f))
				{
					return;
				}
				System->SetNumericAttributeBase(Attribute, System->GetNumericAttributeBase(Attribute) + Gap);
			}
		}

		void LayBare() const
		{
			if (!System)
			{
				return;
			}
			System->SetStatInputs(TMap<FName, FCataclysmStatInputs>());
			SetCurrent(AnyWearerVital::GetMaxHealthAttribute(), AnyWearerMostHealth);
			SetCurrent(AnyWearerVital::GetHealthAttribute(), AnyWearerMostHealth);
			SetCurrent(AnyWearerVital::GetMaxEnergyShieldAttribute(), 0.0f);
			SetCurrent(AnyWearerVital::GetEnergyShieldAttribute(), 0.0f);
			SetCurrent(AnyWearerVital::GetEnergyShieldRegenAttribute(), 0.0f);
			SetCurrent(AnyWearerVital::GetHealthRegenAttribute(), 0.0f);
			SetCurrent(AnyWearerVital::GetLifeLeechAttribute(), 0.0f);
			SetCurrent(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			SetCurrent(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			SetCurrent(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			SetCurrent(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 0.0f);
			SetCurrent(UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute(), 0.0f);
			SetCurrent(UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute(), 0.0f);
		}

		bool IsUsable() const { return Character && System; }
		float Health() const { return System->GetNumericAttribute(AnyWearerVital::GetHealthAttribute()); }
		void SetHealth(float NewHealth) const
		{
			System->SetNumericAttributeBase(AnyWearerVital::GetHealthAttribute(), NewHealth);
		}

		ACataclysmPlayerState* State = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A creature of the monsters' side with 1000 health and an attack damage of 20 that cannot dodge. */
	ACataclysmEnemyCharacter* AnyWearerCreatureAt(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), Where, FRotator::ZeroRotator, Spawn);
		if (!Creature)
		{
			return nullptr;
		}
		Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Creature->SetHealth(AnyWearerMostHealth);
		Creature->SetAttackDamage(20.0f);
		if (UAbilitySystemComponent* System = Creature->GetAbilitySystemComponent())
		{
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(AnyWearerVital::GetEnergyShieldAttribute(), 0.0f);
		}
		return Creature;
	}

	float AnyWearerHealthOf(const AActor* Character)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Character);
		return System ? System->GetNumericAttribute(AnyWearerVital::GetHealthAttribute()) : -1.0f;
	}

	void AnyWearerSetHealthOf(const AActor* Character, float NewHealth)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Character))
		{
			System->SetNumericAttributeBase(AnyWearerVital::GetHealthAttribute(), NewHealth);
		}
	}

	/** A direct blow that can be neither evaded nor critical, carries no weapon sub-type and does not leech; what it took
	 *  from `Struck`'s health. */
	float AnyWearerBlowTook(AActor* From, AActor* Struck, float Damage, bool bTick = false)
	{
		const float Before = AnyWearerHealthOf(Struck);
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotBeEvaded = true;
		Delivery.bCannotCriticallyStrike = true;
		Delivery.bCarriesNoWeaponSubType = true;
		Delivery.bCannotLeech = true;
		Delivery.bIsDamageOverTime = bTick;
		UCataclysmSkillEffects::ApplyDirectDamage(From, Struck, Damage, Delivery);
		return Before - AnyWearerHealthOf(Struck);
	}

	void AnyWearerSteps(AActor* Character, int32 Steps)
	{
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			UCataclysmEnemyModifiers::TimedStep(Character, 0.25f);
		}
	}

	void AnyWearerCarries(ACataclysmCharacterBase* Wearer, const TCHAR* Row)
	{
		Wearer->ModifierRows.AddUnique(FName(Row));
	}

	/** The zones in the world that cut healing. */
	TArray<ACataclysmGroundZone*> AnyWearerDecayZones(UWorld* World)
	{
		TArray<ACataclysmGroundZone*> Zones;
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It) && It->HealingLessPercentInside > 0.0f)
			{
				Zones.Add(*It);
			}
		}
		return Zones;
	}

	/** The pyres in the world still to explode. */
	TArray<ACataclysmBonePyreCharacter*> AnyWearerPyres(UWorld* World)
	{
		TArray<ACataclysmBonePyreCharacter*> Pyres;
		for (TActorIterator<ACataclysmBonePyreCharacter> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->bPyreSpent && !UCataclysmSkillEffects::IsDead(*It))
			{
				Pyres.Add(*It);
			}
		}
		return Pyres;
	}

	/** A stat line holding one row under `wearing_enemy_modifier`: Wearer of the Dead's vow, as option 1 gives it. */
	void AnyWearerHoldsTheVow(UCataclysmAbilitySystemComponent* System)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = 100.0f;
		Row.Condition = ECataclysmStatCondition::WearingEnemyModifier;
		Row.ConditionValue = static_cast<float>(UCataclysmFervour::WornEnemyModifierPlace(FName(TEXT("Generic_Overpowered"))));
		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.Add(FName(TEXT("crit_chance"))).Modifiers = {Row};
		System->SetStatInputs(MoveTemp(Lines));
	}
}

#define CATACLYSM_DEATH_ON_ANY_WEARER_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The player's blows: Grave Touch, Soul Eater, Mortal Reminder
// ---------------------------------------------------------------------------

CATACLYSM_DEATH_ON_ANY_WEARER_TEST(FCataclysmDeathOnAnyWearerBlowTest,
	"Cataclysm.DeathOnAnyWearer.APlayerWearingGraveTouchAndSoulEaterStrikesAsACreatureWearingThemDoes")
{
	using namespace CataclysmDeathOnAnyWearerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FAnyWearerPlayer Player(World);
	ACataclysmEnemyCharacter* Struck = AnyWearerCreatureAt(World, FVector(5.0f * AnyWearerMetre, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a creature"), Struck))
	{
		return false;
	}
	const auto TookAt = [&](float CreatureHealth)
	{
		AnyWearerSetHealthOf(Struck, CreatureHealth);
		return AnyWearerBlowTook(Player.Character, Struck, 100.0f);
	};

	// THE CONTROL: without the row the player's blow is the same either side of half health.
	const float PlainAbove = TookAt(600.0f);
	const float PlainBelow = TookAt(400.0f);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the blow took health (%.2f)"), PlainAbove), PlainAbove > 1.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: without Grave Touch the blow is the same either side of the line"), PlainBelow / PlainAbove,
			  1.0f, 0.01f);
	AnyWearerSetHealthOf(Struck, 400.0f);
	const float PlainTick = AnyWearerBlowTook(Player.Character, Struck, 100.0f, /*bTick=*/true);
	TestTrue(*FString::Printf(TEXT("set-up: a tick took health (%.2f)"), PlainTick), PlainTick > 1.0f);

	AnyWearerCarries(Player.Character, UCataclysmEnemyModifiers::GraveTouchRow);
	TestEqual(TEXT("wearing Grave Touch, above the line the blow is unchanged"), TookAt(600.0f) / PlainAbove, 1.0f, 0.01f);
	TestEqual(TEXT("and below it, half again"), TookAt(400.0f) / PlainAbove, 1.5f, 0.01f);
	AnyWearerSetHealthOf(Struck, 400.0f);
	TestEqual(TEXT("a tick below the line is no larger than a plain one"),
			  AnyWearerBlowTook(Player.Character, Struck, 100.0f, /*bTick=*/true), PlainTick, 0.01f);

	// SOUL EATER: the creature's shield takes the plain blow; the Soul Eater's passes it by to health.
	UAbilitySystemComponent* Theirs = Struck->GetAbilitySystemComponent();
	const auto Shielded = [Theirs, Struck]()
	{
		Theirs->SetNumericAttributeBase(AnyWearerVital::GetMaxEnergyShieldAttribute(), 500.0f);
		Theirs->SetNumericAttributeBase(AnyWearerVital::GetEnergyShieldAttribute(), 500.0f);
		AnyWearerSetHealthOf(Struck, AnyWearerMostHealth);
	};
	Player.Character->ModifierRows.Reset();
	Shielded();
	const float PlainToHealth = AnyWearerBlowTook(Player.Character, Struck, 100.0f);
	const float PlainToShield = 500.0f - Theirs->GetNumericAttribute(AnyWearerVital::GetEnergyShieldAttribute());
	TestTrue(*FString::Printf(TEXT("control: the plain blow took %.2f of the creature's shield"), PlainToShield),
			 PlainToShield > 1.0f);
	TestEqual(TEXT("control: and none of its health"), PlainToHealth, 0.0f, 0.01f);

	AnyWearerCarries(Player.Character, UCataclysmEnemyModifiers::SoulEaterRow);
	Shielded();
	const float EaterToHealth = AnyWearerBlowTook(Player.Character, Struck, 100.0f);
	TestEqual(TEXT("wearing Soul Eater, the blow leaves the whole shield"),
			  Theirs->GetNumericAttribute(AnyWearerVital::GetEnergyShieldAttribute()), 500.0f, 0.01f);
	TestEqual(TEXT("and takes from health what the plain blow took from the shield"), EaterToHealth, PlainToShield, 0.01f);
	return true;
}

CATACLYSM_DEATH_ON_ANY_WEARER_TEST(FCataclysmDeathOnAnyWearerMortalReminderTest,
	"Cataclysm.DeathOnAnyWearer.APlayerWearingMortalReminderHoldsACreaturesHealingToWhatItsHitLeft")
{
	using namespace CataclysmDeathOnAnyWearerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FAnyWearerPlayer Player(World);
	ACataclysmEnemyCharacter* Struck = AnyWearerCreatureAt(World, FVector(5.0f * AnyWearerMetre, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a creature"), Struck))
	{
		return false;
	}
	UAbilitySystemComponent* Theirs = Struck->GetAbilitySystemComponent();
	const auto Heal = [Theirs](float Gain)
	{
		UCataclysmRegeneration::TopUp(*Theirs, AnyWearerVital::GetHealthAttribute(), AnyWearerVital::GetMaxHealthAttribute(),
									  Gain);
	};

	// THE CONTROL: a plain player's hit sets no ceiling on the creature.
	AnyWearerBlowTook(Player.Character, Struck, 50.0f);
	TestTrue(TEXT("control: a plain hit sets no ceiling"), UCataclysmEnemyModifiers::RecoveryCeilingOn(*Theirs) < 0.0f);

	AnyWearerCarries(Player.Character, UCataclysmEnemyModifiers::MortalReminderRow);
	AnyWearerSetHealthOf(Struck, AnyWearerMostHealth);
	const float Took = AnyWearerBlowTook(Player.Character, Struck, 50.0f);
	const float Left = AnyWearerHealthOf(Struck);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the hit took %.2f"), Took), Took > 1.0f))
	{
		return false;
	}
	TestEqual(TEXT("the creature's ceiling is the health the player's hit left"),
			  UCataclysmEnemyModifiers::RecoveryCeilingOn(*Theirs), Left, 0.01f);
	AnyWearerSetHealthOf(Struck, Left - 300.0f);
	Heal(500.0f);
	TestEqual(TEXT("so a heal of 500 stops at the ceiling"), AnyWearerHealthOf(Struck), Left, 0.01f);

	// AND THE PLAYER'S OWN BLOW ON ITSELF CAPS NOTHING.
	AnyWearerBlowTook(Player.Character, Player.Character, 50.0f);
	TestTrue(TEXT("the player's blow on itself sets no ceiling on the player"),
			 UCataclysmEnemyModifiers::RecoveryCeilingOn(*Player.System) < 0.0f);

	World->TimeSeconds += 4.1;
	TestTrue(TEXT("4.1 seconds on, the creature's ceiling has gone"),
			 UCataclysmEnemyModifiers::RecoveryCeilingOn(*Theirs) < 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
// What the player loses: Soul Tether, Funeral Pyre
// ---------------------------------------------------------------------------

CATACLYSM_DEATH_ON_ANY_WEARER_TEST(FCataclysmDeathOnAnyWearerSoulTetherTest,
	"Cataclysm.DeathOnAnyWearer.APlayerWearingSoulTetherSendsATenthOfWhatACreatureTookBackToIt")
{
	using namespace CataclysmDeathOnAnyWearerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FAnyWearerPlayer Player(World);
	ACataclysmEnemyCharacter* Striker = AnyWearerCreatureAt(World, FVector(5.0f * AnyWearerMetre, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a creature"), Striker))
	{
		return false;
	}
	const FGameplayTag Tether = UCataclysmSkillShapes::StatusTagFor(TEXT("Soul Tether"));
	const auto StillToCome = [Striker, &Tether]()
	{
		UCataclysmSkillEffects::FRunningAilment Running;
		return UCataclysmSkillEffects::RunningAilmentOn(Striker, Tether, Running)
			? Running.DamagePerSecond * Running.SecondsLeft
			: 0.0f;
	};

	// THE CONTROL: a creature's blow on a plain player tethers nothing.
	AnyWearerBlowTook(Striker, Player.Character, 200.0f);
	TestFalse(TEXT("control: a plain player's loss tethers nothing"), UCataclysmSkillEffects::HasTag(Striker, Tether));

	AnyWearerCarries(Player.Character, UCataclysmEnemyModifiers::SoulTetherRow);
	Player.SetHealth(AnyWearerMostHealth);
	const float Lost = AnyWearerBlowTook(Striker, Player.Character, 200.0f);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the blow took %.2f"), Lost), Lost > 1.0f))
	{
		return false;
	}
	TestTrue(TEXT("the creature that struck carries Soul Tether"), UCataclysmSkillEffects::HasTag(Striker, Tether));
	TestEqual(TEXT("and a tenth of what the player lost is to come to it"), StillToCome(), Lost * 0.1f, 0.05f);
	return true;
}

CATACLYSM_DEATH_ON_ANY_WEARER_TEST(FCataclysmDeathOnAnyWearerFuneralPyreTest,
	"Cataclysm.DeathOnAnyWearer.APlayerWearingFuneralPyreRaisesAPyreOnItsSideThatExplodesOnCreatures")
{
	using namespace CataclysmDeathOnAnyWearerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FAnyWearerPlayer Player(World);
	ACataclysmEnemyCharacter* Striker = AnyWearerCreatureAt(World, FVector(5.0f * AnyWearerMetre, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a creature"), Striker))
	{
		return false;
	}
	Player.SetCurrent(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 20.0f);

	// THE CONTROL: a creature's hit on a plain player raises no pyre.
	AnyWearerBlowTook(Striker, Player.Character, 50.0f);
	TestEqual(TEXT("control: a plain player raises no pyre"), AnyWearerPyres(World).Num(), 0);

	AnyWearerCarries(Player.Character, UCataclysmEnemyModifiers::FuneralPyreRow);
	AnyWearerBlowTook(Striker, Player.Character, 50.0f);
	const TArray<ACataclysmBonePyreCharacter*> Pyres = AnyWearerPyres(World);
	if (!TestEqual(TEXT("wearing Funeral Pyre, the hit raises one pyre"), Pyres.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("on the player's side"),
			 UCataclysmTeams::TeamOf(Pyres[0]) == UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	TestEqual(TEXT("worth three of the player's hits"), Pyres[0]->PyreExplosionDamage, 60.0f, 0.01f);

	const float StrikerBefore = AnyWearerHealthOf(Striker);
	const float PlayerBefore = Player.Health();
	AnyWearerSteps(Pyres[0], 12);
	TestTrue(*FString::Printf(TEXT("three seconds later it explodes on the creature that struck (%.2f taken)"),
							  StrikerBefore - AnyWearerHealthOf(Striker)),
			 AnyWearerHealthOf(Striker) < StrikerBefore - 1.0f);
	TestEqual(TEXT("and not on the player whose side it is on"), Player.Health(), PlayerBefore, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// The player's own clocks: Spectral Shift, Shadow Step, Lingering Decay
// ---------------------------------------------------------------------------

CATACLYSM_DEATH_ON_ANY_WEARER_TEST(FCataclysmDeathOnAnyWearerSpectralShiftTest,
	"Cataclysm.DeathOnAnyWearer.APlayerWearingSpectralShiftIsOutOfExistenceFromFifteenSecondsForFive")
{
	using namespace CataclysmDeathOnAnyWearerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FAnyWearerPlayer Player(World);
	ACataclysmEnemyCharacter* Striker = AnyWearerCreatureAt(World, FVector(5.0f * AnyWearerMetre, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a creature"), Striker))
	{
		return false;
	}

	// THE CONTROL: a plain player never shifts.
	AnyWearerSteps(Player.Character, 60);
	TestFalse(TEXT("control: a plain player 15 seconds in is not phased"),
			  UCataclysmEnemyModifiers::IsPhasedOut(Player.Character));

	AnyWearerCarries(Player.Character, UCataclysmEnemyModifiers::SpectralShiftRow);
	AnyWearerSteps(Player.Character, 59);
	TestFalse(TEXT("wearing it, 14.75 seconds in, still here"), UCataclysmEnemyModifiers::IsPhasedOut(Player.Character));
	Player.SetHealth(AnyWearerMostHealth);
	TestTrue(TEXT("and a creature's blow lands"), AnyWearerBlowTook(Striker, Player.Character, 100.0f) > 1.0f);

	AnyWearerSteps(Player.Character, 1);
	TestTrue(TEXT("15 seconds in, out of existence"), UCataclysmEnemyModifiers::IsPhasedOut(Player.Character));
	TestEqual(TEXT("and a creature's blow takes nothing"), AnyWearerBlowTook(Striker, Player.Character, 100.0f), 0.0f,
			  0.01f);
	TestTrue(TEXT("and the status line says Phased"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Player.Character).Contains(TEXT("Phased")));

	AnyWearerSteps(Player.Character, 20);
	TestFalse(TEXT("20 seconds in, back"), UCataclysmEnemyModifiers::IsPhasedOut(Player.Character));
	TestTrue(TEXT("and a blow lands again"), AnyWearerBlowTook(Striker, Player.Character, 100.0f) > 1.0f);

	// NO LONGER WORN, THE CLOCK STARTS AGAIN FROM NOUGHT, so the next lending runs its full fifteen seconds.
	AnyWearerSteps(Player.Character, 10);
	Player.Character->ModifierRows.Reset();
	AnyWearerSteps(Player.Character, 1);
	TestEqual(TEXT("no longer worn, its clock is back at nought"), Player.Character->SecondsSinceSpectralShift, 0.0f,
			  0.001f);
	return true;
}

CATACLYSM_DEATH_ON_ANY_WEARER_TEST(FCataclysmDeathOnAnyWearerShadowStepTest,
	"Cataclysm.DeathOnAnyWearer.APlayerWearingShadowStepIsPassedOverByACreaturesSearchUntilItUsesASkill")
{
	using namespace CataclysmDeathOnAnyWearerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FAnyWearerPlayer Player(World);
	ACataclysmEnemyCharacter* Searcher = AnyWearerCreatureAt(World, FVector(3.0f * AnyWearerMetre, 0.0f, 0.0f));
	UCataclysmTargetCandidates* Candidates = UCataclysmTargetCandidates::In(World);
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a creature"), Searcher)
		|| !TestNotNull(TEXT("set-up: the world has the target lists"), Candidates))
	{
		return false;
	}
	const auto Found = [Candidates, Searcher]()
	{
		return Candidates->NearestHostile(Searcher, Searcher->GetActorLocation(), 10.0f * AnyWearerMetre);
	};
	AActor* const Them = Player.Character;

	TestEqual(TEXT("control: a plain player is found"), Found(), Them);

	AnyWearerCarries(Player.Character, UCataclysmEnemyModifiers::ShadowStepRow);
	AnyWearerSteps(Player.Character, 1);
	TestTrue(TEXT("wearing it and not attacking, the player is hidden by Shadow Step"),
			 UCataclysmEnemyModifiers::IsHiddenByShadowStep(Player.Character));
	TestNull(TEXT("so the creature's search finds nothing"), Found());
	TestFalse(TEXT("but the player is still drawn on its own screen"), Player.Character->IsHidden());
	TestTrue(TEXT("and the status line says Hidden"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Player.Character).Contains(TEXT("Hidden")));

	// AN ATTACK: any skill the player uses, as `UCataclysmCombatEvents::NoteSkillUsed` announces it.
	UCataclysmCombatEvents::NoteSkillUsed(Player.Character, TEXT("A test skill"), FGameplayTagContainer(),
										  ECataclysmAbilitySlot::BasicAttack);
	TestEqual(TEXT("as it attacks, it is found"), Found(), Them);
	AnyWearerSteps(Player.Character, 3);
	TestEqual(TEXT("three quarters of a second after, still found"), Found(), Them);
	AnyWearerSteps(Player.Character, 1);
	TestNull(TEXT("a second after, hidden again"), Found());
	return true;
}

CATACLYSM_DEATH_ON_ANY_WEARER_TEST(FCataclysmDeathOnAnyWearerLingeringDecayTest,
	"Cataclysm.DeathOnAnyWearer.APlayerWearingLingeringDecayLeavesZonesThatHalveACreaturesHealing")
{
	using namespace CataclysmDeathOnAnyWearerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A CREATURE TWO METRES FROM THE PLAYER, inside a three metre zone at the player's feet, and one twenty metres off.
	const FAnyWearerPlayer Player(World);
	ACataclysmEnemyCharacter* Near = AnyWearerCreatureAt(World, FVector(2.0f * AnyWearerMetre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Far = AnyWearerCreatureAt(World, FVector(0.0f, 20.0f * AnyWearerMetre, 0.0f));
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a near creature"), Near)
		|| !TestNotNull(TEXT("set-up: a far creature"), Far))
	{
		return false;
	}
	const auto Restored = [](const AActor* Healed)
	{
		AnyWearerSetHealthOf(Healed, 100.0f);
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Healed))
		{
			UCataclysmRegeneration::TopUp(*System, AnyWearerVital::GetHealthAttribute(),
										  AnyWearerVital::GetMaxHealthAttribute(), 100.0f);
		}
		return AnyWearerHealthOf(Healed) - 100.0f;
	};

	// THE CONTROL: a plain player leaves no zone.
	AnyWearerSteps(Player.Character, 20);
	TestEqual(TEXT("control: a plain player leaves no zone in 5 seconds"), AnyWearerDecayZones(World).Num(), 0);

	AnyWearerCarries(Player.Character, UCataclysmEnemyModifiers::LingeringDecayRow);
	AnyWearerSteps(Player.Character, 19);
	TestEqual(TEXT("wearing it, 4.75 seconds in, no zone"), AnyWearerDecayZones(World).Num(), 0);
	AnyWearerSteps(Player.Character, 1);
	const TArray<ACataclysmGroundZone*> Zones = AnyWearerDecayZones(World);
	if (!TestEqual(TEXT("5 seconds in, one zone"), Zones.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("which reaches the creatures' side"), static_cast<int32>(Zones[0]->HealingLessReachesTeamId),
			  static_cast<int32>(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters).GetId()));
	TestEqual(TEXT("the near creature inside it is cut by 50"),
			  UCataclysmEnemyModifiers::LingeringDecayHealingLessPercentAt(Near), 50.0f, 0.01f);
	TestEqual(TEXT("so a heal of 100 restores 50"), Restored(Near), 50.0f, 0.01f);
	TestEqual(TEXT("control: the far creature is healed in full"), Restored(Far), 100.0f, 0.01f);
	TestEqual(TEXT("and the player standing in its own zone is not cut"),
			  UCataclysmEnemyModifiers::LingeringDecayHealingLessPercentAt(Player.Character), 0.0f, 0.01f);
	TestEqual(TEXT("so the player's heal of 100 restores 100"), Restored(Player.Character), 100.0f, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// The one that acts at a death and is lent: Undying Will
// ---------------------------------------------------------------------------

CATACLYSM_DEATH_ON_ANY_WEARER_TEST(FCataclysmDeathOnAnyWearerUndyingWillTest,
	"Cataclysm.DeathOnAnyWearer.UndyingWillLentToThePlayerRaisesItOnceAtHalfHealthWithItsBlowsHalfAgain")
{
	using namespace CataclysmDeathOnAnyWearerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FAnyWearerPlayer Player(World);
	ACataclysmEnemyCharacter* Killer = AnyWearerCreatureAt(World, FVector(5.0f * AnyWearerMetre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Struck = AnyWearerCreatureAt(World, FVector(-5.0f * AnyWearerMetre, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a killer"), Killer)
		|| !TestNotNull(TEXT("set-up: a creature to strike"), Struck))
	{
		return false;
	}
	const FName UndyingWill(UCataclysmEnemyModifiers::UndyingWillRow);
	const auto PlayersBlow = [&]()
	{
		AnyWearerSetHealthOf(Struck, AnyWearerMostHealth);
		return AnyWearerBlowTook(Player.Character, Struck, 100.0f);
	};

	// THE CONTROLS: a player not wearing it does not rise, and a creature wearing it answers no here (it rises as a new
	// creature the dungeon places).
	TestFalse(TEXT("control: a player not wearing it does not rise"),
			  UCataclysmEnemyModifiers::RiseThroughUndyingWill(Player.Character));
	Killer->ModifierRows.Add(UndyingWill);
	TestFalse(TEXT("control: a creature wearing it does not rise here"),
			  UCataclysmEnemyModifiers::RiseThroughUndyingWill(Killer));
	Killer->ModifierRows.Reset();
	const float BlowBefore = PlayersBlow();
	if (!TestTrue(*FString::Printf(TEXT("set-up: the player's blow took %.2f"), BlowBefore), BlowBefore > 1.0f))
	{
		return false;
	}

	// LENT, AS WEARER OF THE DEAD LENDS IT: recorded by its row on the player's ability system.
	Player.System->NoteLentModifier(UndyingWill, UCataclysmFervour::WornEnemyModifierSeconds);
	TestTrue(TEXT("set-up: the player wears the lent Undying Will"),
			 UCataclysmEnemyModifiers::CarriedBy(Player.Character, UCataclysmEnemyModifiers::UndyingWillRow));
	TestEqual(TEXT("before it rises, the player's blow is unchanged"), PlayersBlow() / BlowBefore, 1.0f, 0.01f);

	AnyWearerBlowTook(Killer, Player.Character, 100000.0f);
	TestFalse(TEXT("a blow far past its health does not kill it"), UCataclysmSkillEffects::IsDead(Player.Character));
	TestEqual(TEXT("it rises at half its maximum health"), Player.Health(),
			  AnyWearerMostHealth * UCataclysmEnemyModifiers::UndyingWillHealthShare, 0.5f);
	TestTrue(TEXT("and the status line says Undying"),
			 UCataclysmCombatOverlay::DeathModifierTextFor(Player.Character).Contains(TEXT("Undying")));
	TestEqual(TEXT("and its blows deal half again"), PlayersBlow() / BlowBefore,
			  1.0f + UCataclysmEnemyModifiers::UndyingWillDamageIncreasedPercent / 100.0f, 0.01f);
	TestFalse(TEXT("ONCE: while it still wears it, it does not rise again"),
			  UCataclysmEnemyModifiers::RiseThroughUndyingWill(Player.Character));

	// THE LENDING RUNS OUT: the next step makes the rising ready again and takes the damage away.
	World->TimeSeconds += UCataclysmFervour::WornEnemyModifierSeconds + 0.5f;
	AnyWearerSteps(Player.Character, 1);
	TestFalse(TEXT("20.5 seconds on, it no longer wears it"),
			  UCataclysmEnemyModifiers::CarriedBy(Player.Character, UCataclysmEnemyModifiers::UndyingWillRow));
	TestEqual(TEXT("and its blows are as they were"), PlayersBlow() / BlowBefore, 1.0f, 0.01f);
	TestFalse(TEXT("and it is no longer marked risen"), Player.Character->bRoseThroughUndyingWill);
	TestFalse(TEXT("and its rising is ready for the next lending"), Player.Character->bUndyingWillSpent);
	return true;
}

// ---------------------------------------------------------------------------
// Wearer of the Dead's lending, and the two never lent
// ---------------------------------------------------------------------------

CATACLYSM_DEATH_ON_ANY_WEARER_TEST(FCataclysmDeathOnAnyWearerLendingTest,
	"Cataclysm.DeathOnAnyWearer.WearerOfTheDeadLendsAnElitesDeathModifiersButNeverHauntingOrFinalCurse")
{
	using namespace CataclysmDeathOnAnyWearerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FAnyWearerPlayer Player(World);
	ACataclysmEnemyCharacter* Elite = AnyWearerCreatureAt(World, FVector(5.0f * AnyWearerMetre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Common = AnyWearerCreatureAt(World, FVector(-5.0f * AnyWearerMetre, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !Elite || !Common)
	{
		AddError(TEXT("set-up: two creatures"));
		return false;
	}
	// THE RUNG AND THE ROWS ALONE, as `Cataclysm.WornModifiers.` sets them: `SetRarityStep` would draw more of its own.
	const TArray<FName> Carried = {FName(UCataclysmEnemyModifiers::GraveTouchRow),
								   FName(UCataclysmEnemyModifiers::UndyingWillRow),
								   FName(UCataclysmEnemyModifiers::HauntingRow),
								   FName(UCataclysmEnemyModifiers::FinalCurseRow)};
	Elite->RarityStep = 1;
	Elite->ModifierRows = Carried;
	Common->RarityStep = 0;
	Common->ModifierRows = Carried;
	AActor* const Wearer = Player.Character;
	const auto Wears = [Wearer](const TCHAR* Row) { return UCataclysmEnemyModifiers::CarriedBy(Wearer, Row); };

	// THE CONTROLS: without the vow nothing is lent; with it, a Common creature lends nothing.
	TestEqual(TEXT("control: a player without the vow is lent nothing"),
			  UCataclysmFervour::WearTheModifiersOf(Player.System, Elite), 0);
	TestFalse(TEXT("control: and wears no Grave Touch"), Wears(UCataclysmEnemyModifiers::GraveTouchRow));
	AnyWearerHoldsTheVow(Player.System);
	TestEqual(TEXT("control: a Common creature lends nothing"), UCataclysmFervour::WearTheModifiersOf(Player.System, Common),
			  0);

	TestEqual(TEXT("the Elite lends two of its four: Grave Touch and Undying Will"),
			  UCataclysmFervour::WearTheModifiersOf(Player.System, Elite), 2);
	TestTrue(TEXT("the player wears Grave Touch"), Wears(UCataclysmEnemyModifiers::GraveTouchRow));
	TestTrue(TEXT("and Undying Will"), Wears(UCataclysmEnemyModifiers::UndyingWillRow));
	TestFalse(TEXT("never Haunting"), Wears(UCataclysmEnemyModifiers::HauntingRow));
	TestFalse(TEXT("never Final Curse"), Wears(UCataclysmEnemyModifiers::FinalCurseRow));
	TestEqual(TEXT("its own list is untouched: the lending is on its ability system"),
			  Player.Character->ModifierRows.Num(), 0);
	const FString Line = UCataclysmFervour::WornModifiersLine(Player.System);
	AddInfo(FString::Printf(TEXT("WORN LINE: %s"), *Line));
	TestEqual(TEXT("the screen shows both with their seconds"), Line,
			  FString(TEXT("Worn: Grave Touch 20s, Undying Will 20s")));

	World->TimeSeconds += 19.5;
	TestTrue(TEXT("19.5 seconds on, still worn"), Wears(UCataclysmEnemyModifiers::GraveTouchRow));
	World->TimeSeconds += 1.0;
	TestFalse(TEXT("20.5 seconds on, no longer"), Wears(UCataclysmEnemyModifiers::GraveTouchRow));
	TestTrue(TEXT("and the line is empty"), UCataclysmFervour::WornModifiersLine(Player.System).IsEmpty());
	return true;
}

CATACLYSM_DEATH_ON_ANY_WEARER_TEST(FCataclysmDeathOnAnyWearerFinalCurseSideTest,
	"Cataclysm.DeathOnAnyWearer.AFinalCurseWearerOnThePlayersSideCursesNobodyAtItsDeath")
{
	using namespace CataclysmDeathOnAnyWearerTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FAnyWearerPlayer Player(World);
	ACataclysmEnemyCharacter* Thrall = AnyWearerCreatureAt(World, FVector(5.0f * AnyWearerMetre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Foe = AnyWearerCreatureAt(World, FVector(-5.0f * AnyWearerMetre, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a player"), Player.IsUsable()) || !TestNotNull(TEXT("set-up: a thrall"), Thrall)
		|| !TestNotNull(TEXT("set-up: a foe"), Foe))
	{
		return false;
	}
	const FGameplayTag Curse = UCataclysmSkillShapes::StatusTagFor(TEXT("Final Curse"));
	Thrall->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	AnyWearerCarries(Thrall, UCataclysmEnemyModifiers::FinalCurseRow);
	AnyWearerCarries(Foe, UCataclysmEnemyModifiers::FinalCurseRow);

	Thrall->HandleDeath();
	TestFalse(TEXT("a wearer of the player's own side curses nobody as it dies"),
			  UCataclysmSkillEffects::HasTag(Player.Character, Curse));
	Foe->HandleDeath();
	TestTrue(TEXT("CONTROL: a wearer of the monsters' side curses the player"),
			 UCataclysmSkillEffects::HasTag(Player.Character, Curse));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
