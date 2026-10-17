// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmVoidAura.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The void aura, the Null Emperor set's third layer. Ruled 2026-10-17: the coordinating session's O9, recorded in
 * `docs/DECISIONS.md`. "While you have at least 10 stacks of Void, you gain an aura of void energy that has a 50% chance
 * to consume enemy projectiles": an enemy's shot in flight that comes within 3 m of the wearer is rolled once, at 50%,
 * and on success ends with no blast and no hit.
 *
 * THE ROWS ARE MADE BY HAND: the six-piece's action, which gives the stacks their name, and the ten-piece's flat 50 of
 * `void_aura_projectile_consume_chance`. No authored row carries the stat; the enchantment session writes it above
 * this layer, with its real-row test wearing ten pieces.
 *
 * THE SCENE. A wearer is a creature of the players' side, as `Cataclysm.Silence.Void.` reads a gain; a firer is a
 * creature of the monsters' side; each shot is fired by hand with `ACataclysmProjectile::Fire` and stepped by hand at a
 * sixtieth of a second, as `Cataclysm.ProjectileCount.` steps one. The roll is pinned with `Cataclysm.VoidAuraConsumeRoll`,
 * the critical strike roll with `Cataclysm.CritRoll` at 100 after the world is made, and every wearer's evasion is
 * nought, so a shot that reaches a wearer lands. Every figure is a health lost, read against the health before the
 * shot. Separate scenes stand 40 m apart; everything in a scene stands at least a metre from everything else.
 */
namespace CataclysmVoidAuraTest
{
	constexpr float AuraM = 100.0f;
	constexpr float AuraPool = 100000.0f;
	constexpr float AuraScenesApart = 40.0f * AuraM;

	/** A flat shot's speed: 20 cm a step at a sixtieth of a second. */
	constexpr float AuraShotSpeed = 1200.0f;

	/** A shot's blast radius where it stops, so a blast that went off would reach a bystander 1.5 m off. */
	constexpr float AuraBlastRadiusCm = 200.0f;

	/** The key the hand-made six-piece row keeps its stacks under. */
	const TCHAR* const AuraVoidKey = TEXT("AuraTest:kept_stacks_free_next_ability:hit_dealt");

	/** Pins the void aura's roll until it goes out of scope. Under the chance consumes. */
	struct FScopedAuraRoll
	{
		explicit FScopedAuraRoll(float Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.VoidAuraConsumeRoll"));
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		~FScopedAuraRoll()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		void Repin(float Roll) const
		{
			if (Variable)
			{
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/** A creature of a side with a great deal of health and no evasion, spawned 300 m away and moved there. */
	ACataclysmEnemyCharacter* AuraSpawnCreature(UWorld* World, ECataclysmTeam Side, const FVector& Where,
												float AttackDamage = 0.0f)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), FVector(30000.0f, 30000.0f, 0.0f), FRotator::ZeroRotator, Params);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(Side));
			Made->SetHealth(AuraPool);
			Made->SetAttackDamage(AttackDamage);
			Made->SetActorLocation(Where);
			if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Made))
			{
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			}
		}
		return Made;
	}

	UCataclysmAbilitySystemComponent* AuraSystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	float AuraHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	/**
	 * Wear the six-piece's row holding `Void` of its stacks, and with `bTenPiece` the ten-piece's flat 50 as well.
	 * False when the creature has no ability system or the count did not take.
	 */
	bool AuraWear(const AActor* Wearer, int32 Void, bool bTenPiece)
	{
		UCataclysmAbilitySystemComponent* System = AuraSystemOf(Wearer);
		if (!System)
		{
			return false;
		}
		FCataclysmPoolAction SixPiece;
		SixPiece.Event = FName(TEXT("hit_dealt"));
		SixPiece.Pool = FName(UCataclysmAbilitySystemComponent::KeptStacksFreeNextAbilityAction);
		SixPiece.Percent = 100.0f;
		SixPiece.bKeptStacksFreeNextAbility = true;
		SixPiece.KeptStackKey = FName(AuraVoidKey);
		SixPiece.KeptStackCap = 10;
		SixPiece.StatusName = TEXT("Void");
		SixPiece.Condition = ECataclysmStatCondition::TargetIsSilenced;
		SixPiece.TriggerKey = FName(AuraVoidKey);
		System->SetPoolActions({SixPiece});
		for (int32 Each = 0; Each < Void; ++Each)
		{
			System->GainKeptStack(FName(AuraVoidKey), 10);
		}
		if (bTenPiece)
		{
			FCataclysmStatModifier TenPiece;
			TenPiece.Bucket = ECataclysmStatBucket::Flat;
			TenPiece.Source = ECataclysmModifierSource::Enchantment;
			TenPiece.Value = 50.0f;
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(UCataclysmVoidAura::ConsumeChanceStat));
			Line.Base = 0.0f;
			Line.Modifiers = {TenPiece};
			System->SetStatInputs(MoveTemp(Inputs));
		}
		return System->KeptStacksHeldNamed(TEXT("Void")) == Void;
	}

	/** A flat shot of 100% of the firer's damage, which does not pierce and blasts in `AuraBlastRadiusCm` where it stops. */
	ACataclysmProjectile* AuraFireFlat(AActor* Firer, const FVector& From, const FVector& To)
	{
		return ACataclysmProjectile::Fire(Firer, From, To, AuraBlastRadiusCm, AuraShotSpeed, /*InPierce=*/0,
										  /*bInReturns=*/false, /*InDamagePercent=*/100.0f, FGameplayTagContainer(),
										  /*bInBurns=*/false);
	}

	/** Step a shot until it finishes, at most ten seconds of steps. */
	void AuraFly(ACataclysmProjectile* Shot)
	{
		for (int32 Steps = 0; Shot && Steps < 600 && !Shot->bFinished; ++Steps)
		{
			Shot->Step(1.0f / 60.0f);
		}
	}

	/** Step a shot until a void aura has rolled for it or it finishes, at most ten seconds of steps. */
	void AuraFlyUntilRolled(ACataclysmProjectile* Shot)
	{
		for (int32 Steps = 0; Shot && Steps < 600 && !Shot->bFinished && !Shot->bVoidAuraRolled; ++Steps)
		{
			Shot->Step(1.0f / 60.0f);
		}
	}

	/** One scene: a wearer at `Origin`, a firer 8 m along -X, and a bystander of the wearer's side 1.5 m along +Y. */
	struct FAuraScene
	{
		FAuraScene(UWorld* World, const FVector& InOrigin)
			: Origin(InOrigin)
		{
			Wearer = AuraSpawnCreature(World, ECataclysmTeam::Players, Origin);
			Firer = AuraSpawnCreature(World, ECataclysmTeam::Monsters, Origin - FVector(8.0f * AuraM, 0.0f, 0.0f),
									  /*AttackDamage=*/100.0f);
			Bystander = AuraSpawnCreature(World, ECataclysmTeam::Players, Origin + FVector(0.0f, 1.5f * AuraM, 0.0f));
		}

		bool IsWhole() const { return Wearer && Firer && Bystander; }

		/** The firer's flat shot at the wearer. */
		ACataclysmProjectile* FireAtTheWearer() const
		{
			return AuraFireFlat(Firer, Firer->GetActorLocation(), Origin);
		}

		FVector Origin;
		ACataclysmEnemyCharacter* Wearer = nullptr;
		ACataclysmEnemyCharacter* Firer = nullptr;
		ACataclysmEnemyCharacter* Bystander = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidAuraConsumesAShotTest,
	"Cataclysm.Silence.Aura.AnEnemyShotEnteringTheAuraAtTenVoidIsConsumedAndDeliversNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three scenes: a wearer of both rows at nine Void, one at ten, and one at ten wearing the six-piece's row alone. Each
 * firer shoots at its wearer from 8 m, with the roll pinned at 10, under the 50. At nine and without the ten-piece the
 * shot is never rolled and lands, blasting the bystander too. At ten it is rolled and consumed inside 3 m, before it
 * reaches the wearer, and nobody loses anything.
 */
bool FCataclysmVoidAuraConsumesAShotTest::RunTest(const FString&)
{
	using namespace CataclysmVoidAuraTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	const FScopedAuraRoll Roll(10.0f);
	if (!TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}

	FAuraScene AtNine(World, FVector::ZeroVector);
	FAuraScene AtTen(World, FVector(0.0f, AuraScenesApart, 0.0f));
	FAuraScene SixPieceOnly(World, FVector(0.0f, 2.0f * AuraScenesApart, 0.0f));
	if (!TestTrue(TEXT("set-up: three scenes"), AtNine.IsWhole() && AtTen.IsWhole() && SixPieceOnly.IsWhole())
		|| !TestTrue(TEXT("set-up: nine Void and the ten-piece"), AuraWear(AtNine.Wearer, 9, true))
		|| !TestTrue(TEXT("set-up: ten Void and the ten-piece"), AuraWear(AtTen.Wearer, 10, true))
		|| !TestTrue(TEXT("set-up: ten Void and the six-piece alone"), AuraWear(SixPieceOnly.Wearer, 10, false)))
	{
		return false;
	}

	// CONTROL: AT NINE.
	const float NineBefore = AuraHealthOf(AtNine.Wearer);
	const float NineBystanderBefore = AuraHealthOf(AtNine.Bystander);
	ACataclysmProjectile* NineShot = AtNine.FireAtTheWearer();
	if (!TestNotNull(TEXT("set-up: the shot at nine"), NineShot))
	{
		return false;
	}
	AuraFly(NineShot);
	TestFalse(TEXT("control: at nine Void the aura is not up"), UCataclysmVoidAura::IsUp(AuraSystemOf(AtNine.Wearer)));
	TestFalse(TEXT("control: so the shot is never rolled"), NineShot->bVoidAuraRolled);
	TestFalse(TEXT("control: nor consumed"), NineShot->bConsumedByAVoidAura);
	const float NineLost = NineBefore - AuraHealthOf(AtNine.Wearer);
	TestTrue(*FString::Printf(TEXT("control: and the wearer loses health: %.2f"), NineLost), NineLost > 0.0f);
	TestTrue(TEXT("control: and its blast reaches the bystander 1.5 m off"),
			 NineBystanderBefore - AuraHealthOf(AtNine.Bystander) > 0.0f);

	// CONTROL: TEN VOID WITHOUT THE TEN-PIECE.
	const float SixBefore = AuraHealthOf(SixPieceOnly.Wearer);
	ACataclysmProjectile* SixShot = SixPieceOnly.FireAtTheWearer();
	if (!TestNotNull(TEXT("set-up: the shot at the six-piece's wearer"), SixShot))
	{
		return false;
	}
	AuraFly(SixShot);
	TestFalse(TEXT("control: ten Void with no ten-piece is no aura, so the shot is never rolled"),
			  SixShot->bVoidAuraRolled);
	TestEqual(TEXT("control: and the wearer loses what the wearer at nine lost"),
			  SixBefore - AuraHealthOf(SixPieceOnly.Wearer), NineLost, 0.01f);

	// AT TEN.
	const float TenBefore = AuraHealthOf(AtTen.Wearer);
	const float TenBystanderBefore = AuraHealthOf(AtTen.Bystander);
	TestTrue(TEXT("at ten Void with the ten-piece the aura is up"), UCataclysmVoidAura::IsUp(AuraSystemOf(AtTen.Wearer)));
	ACataclysmProjectile* TenShot = AtTen.FireAtTheWearer();
	if (!TestNotNull(TEXT("set-up: the shot at ten"), TenShot))
	{
		return false;
	}
	AuraFly(TenShot);
	TestTrue(TEXT("the shot is rolled"), TenShot->bVoidAuraRolled);
	TestTrue(TEXT("and, the roll under 50, consumed"), TenShot->bConsumedByAVoidAura);
	TestTrue(TEXT("and it has ended"), TenShot->bFinished);
	const float EndedCm = FVector::Dist2D(TenShot->GetActorLocation(), AtTen.Origin);
	TestTrue(*FString::Printf(TEXT("inside the 3 m and short of the wearer: it ended %.1f cm off"), EndedCm),
			 EndedCm <= UCataclysmVoidAura::RadiusCm + 0.5f && EndedCm > 100.0f);
	TestEqual(TEXT("it struck nobody"), TenShot->EnemiesHit, 0);
	TestEqual(TEXT("the wearer loses nothing"), TenBefore - AuraHealthOf(AtTen.Wearer), 0.0f, 0.01f);
	TestEqual(TEXT("and no blast reaches the bystander"),
			  TenBystanderBefore - AuraHealthOf(AtTen.Bystander), 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidAuraRollsOnceTest,
	"Cataclysm.Silence.Aura.AFailedRollIsNotRolledAgainAsTheShotFliesOnAndItHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A wearer at ten and a control wearer at nine. The roll is pinned at 80, over the 50, until the shot has been rolled,
 * and then at 10, under it, for the rest of its flight. Rolled once, it is not consumed and lands for what the
 * control's shot dealt.
 */
bool FCataclysmVoidAuraRollsOnceTest::RunTest(const FString&)
{
	using namespace CataclysmVoidAuraTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	const FScopedAuraRoll Roll(80.0f);
	if (!TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}

	FAuraScene Control(World, FVector::ZeroVector);
	FAuraScene Subject(World, FVector(0.0f, AuraScenesApart, 0.0f));
	if (!TestTrue(TEXT("set-up: two scenes"), Control.IsWhole() && Subject.IsWhole())
		|| !TestTrue(TEXT("set-up: nine Void"), AuraWear(Control.Wearer, 9, true))
		|| !TestTrue(TEXT("set-up: ten Void"), AuraWear(Subject.Wearer, 10, true)))
	{
		return false;
	}

	const float ControlBefore = AuraHealthOf(Control.Wearer);
	ACataclysmProjectile* ControlShot = Control.FireAtTheWearer();
	if (!TestNotNull(TEXT("set-up: the control's shot"), ControlShot))
	{
		return false;
	}
	AuraFly(ControlShot);
	const float ControlLost = ControlBefore - AuraHealthOf(Control.Wearer);
	TestTrue(*FString::Printf(TEXT("control: at nine the shot lands: %.2f"), ControlLost), ControlLost > 0.0f);

	const float Before = AuraHealthOf(Subject.Wearer);
	ACataclysmProjectile* Shot = Subject.FireAtTheWearer();
	if (!TestNotNull(TEXT("set-up: the shot at ten"), Shot))
	{
		return false;
	}
	Shot->Step(1.0f / 60.0f);
	TestFalse(TEXT("control: 8 m off, outside the 3 m, it has not been rolled"), Shot->bVoidAuraRolled);
	AuraFlyUntilRolled(Shot);
	TestTrue(TEXT("inside the 3 m it is rolled"), Shot->bVoidAuraRolled);
	TestFalse(TEXT("and, the roll over 50, not consumed"), Shot->bConsumedByAVoidAura);
	TestFalse(TEXT("and still flying"), Shot->bFinished);

	Roll.Repin(10.0f);
	AuraFly(Shot);
	TestFalse(TEXT("with the roll then under 50 it is still not consumed: it is never rolled again"),
			  Shot->bConsumedByAVoidAura);
	TestEqual(TEXT("and it lands for what the control's shot dealt"), Before - AuraHealthOf(Subject.Wearer),
			  ControlLost, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidAuraOwnSideTest,
	"Cataclysm.Silence.Aura.TheWearersOwnShotAndAnAllysAreNeverConsumed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * One wearer at ten, with the roll pinned at 0, which passes any chance above nought. Control: an enemy's shot at it is
 * consumed. Then the wearer's own shot, fired from where it stands at a creature 6 m along +X, and an ally's, fired from
 * 2 m beside the wearer, inside its aura, at the same creature: neither is rolled, and each lands. The ally stands off
 * the enemy's lane, so the control's shot meets nobody before the aura.
 */
bool FCataclysmVoidAuraOwnSideTest::RunTest(const FString&)
{
	using namespace CataclysmVoidAuraTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	const FScopedAuraRoll Roll(0.0f);
	if (!TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}

	FAuraScene Scene(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Ally = AuraSpawnCreature(World, ECataclysmTeam::Players,
													   FVector(0.0f, -2.0f * AuraM, 0.0f), /*AttackDamage=*/100.0f);
	ACataclysmEnemyCharacter* Target = AuraSpawnCreature(World, ECataclysmTeam::Monsters,
														 FVector(6.0f * AuraM, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: the scene, an ally and a target"), Scene.IsWhole() && Ally && Target)
		|| !TestTrue(TEXT("set-up: ten Void and the ten-piece"), AuraWear(Scene.Wearer, 10, true)))
	{
		return false;
	}
	Scene.Wearer->SetAttackDamage(100.0f);

	ACataclysmProjectile* EnemyShot = Scene.FireAtTheWearer();
	if (!TestNotNull(TEXT("set-up: the enemy's shot"), EnemyShot))
	{
		return false;
	}
	AuraFly(EnemyShot);
	TestTrue(TEXT("control: an enemy's shot at the wearer is consumed"), EnemyShot->bConsumedByAVoidAura);

	const float BeforeOwn = AuraHealthOf(Target);
	ACataclysmProjectile* OwnShot = AuraFireFlat(Scene.Wearer, Scene.Origin, Target->GetActorLocation());
	if (!TestNotNull(TEXT("set-up: the wearer's own shot"), OwnShot))
	{
		return false;
	}
	AuraFly(OwnShot);
	TestFalse(TEXT("the wearer's own shot, inside its own aura from the start, is never rolled"),
			  OwnShot->bVoidAuraRolled);
	TestFalse(TEXT("nor consumed"), OwnShot->bConsumedByAVoidAura);
	TestTrue(TEXT("and it lands on the creature 6 m off"), BeforeOwn - AuraHealthOf(Target) > 0.0f);

	const float BeforeAlly = AuraHealthOf(Target);
	ACataclysmProjectile* AllyShot = AuraFireFlat(Ally, Ally->GetActorLocation(), Target->GetActorLocation());
	if (!TestNotNull(TEXT("set-up: the ally's shot"), AllyShot))
	{
		return false;
	}
	AuraFly(AllyShot);
	TestFalse(TEXT("an ally's shot fired inside the wearer's aura is never rolled"), AllyShot->bVoidAuraRolled);
	TestFalse(TEXT("nor consumed"), AllyShot->bConsumedByAVoidAura);
	TestTrue(TEXT("and it lands on the creature"), BeforeAlly - AuraHealthOf(Target) > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidAuraReachTest,
	"Cataclysm.Silence.Aura.AShotPassingFourMetresOffIsNotConsumed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two scenes, each a wearer at ten with the roll pinned at 0. In each, a firer 8 m along -X shoots along a lane beside
 * the wearer at a creature of the wearer's side 8 m along +X in the same lane. Control: a lane 2.5 m off is inside the
 * 3 m, so the shot is consumed and the creature loses nothing. A lane 4 m off is outside it: the shot is never rolled
 * and lands.
 */
bool FCataclysmVoidAuraReachTest::RunTest(const FString&)
{
	using namespace CataclysmVoidAuraTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	const FScopedAuraRoll Roll(0.0f);
	if (!TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}

	struct FLane
	{
		ACataclysmEnemyCharacter* Wearer = nullptr;
		ACataclysmEnemyCharacter* Firer = nullptr;
		ACataclysmEnemyCharacter* Struck = nullptr;
		FVector From;
		FVector To;
	};
	const auto MakeLane = [World](const FVector& Origin, float OffCm)
	{
		FLane Lane;
		Lane.From = Origin + FVector(-8.0f * AuraM, OffCm, 0.0f);
		Lane.To = Origin + FVector(8.0f * AuraM, OffCm, 0.0f);
		Lane.Wearer = AuraSpawnCreature(World, ECataclysmTeam::Players, Origin);
		Lane.Firer = AuraSpawnCreature(World, ECataclysmTeam::Monsters, Lane.From, /*AttackDamage=*/100.0f);
		Lane.Struck = AuraSpawnCreature(World, ECataclysmTeam::Players, Lane.To);
		return Lane;
	};
	const FLane Inside = MakeLane(FVector::ZeroVector, 2.5f * AuraM);
	const FLane Outside = MakeLane(FVector(0.0f, AuraScenesApart, 0.0f), 4.0f * AuraM);
	if (!TestTrue(TEXT("set-up: two lanes"), Inside.Wearer && Inside.Firer && Inside.Struck && Outside.Wearer
				  && Outside.Firer && Outside.Struck)
		|| !TestTrue(TEXT("set-up: ten Void and the ten-piece, inside"), AuraWear(Inside.Wearer, 10, true))
		|| !TestTrue(TEXT("set-up: ten Void and the ten-piece, outside"), AuraWear(Outside.Wearer, 10, true)))
	{
		return false;
	}

	const float InsideBefore = AuraHealthOf(Inside.Struck);
	ACataclysmProjectile* InsideShot = AuraFireFlat(Inside.Firer, Inside.From, Inside.To);
	if (!TestNotNull(TEXT("set-up: the shot 2.5 m off"), InsideShot))
	{
		return false;
	}
	AuraFly(InsideShot);
	TestTrue(TEXT("control: a shot passing 2.5 m off the wearer is consumed"), InsideShot->bConsumedByAVoidAura);
	TestEqual(TEXT("control: and the creature at the lane's end loses nothing"),
			  InsideBefore - AuraHealthOf(Inside.Struck), 0.0f, 0.01f);

	const float OutsideBefore = AuraHealthOf(Outside.Struck);
	ACataclysmProjectile* OutsideShot = AuraFireFlat(Outside.Firer, Outside.From, Outside.To);
	if (!TestNotNull(TEXT("set-up: the shot 4 m off"), OutsideShot))
	{
		return false;
	}
	AuraFly(OutsideShot);
	TestFalse(TEXT("a shot passing 4 m off is never rolled"), OutsideShot->bVoidAuraRolled);
	TestFalse(TEXT("nor consumed"), OutsideShot->bConsumedByAVoidAura);
	TestTrue(TEXT("and it lands on the creature at the lane's end"), OutsideBefore - AuraHealthOf(Outside.Struck) > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidAuraLobTest,
	"Cataclysm.Silence.Aura.ALobbedBlastIsConsumedInFlightAndDealsNothingOnTheGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A lob, fired as the Corrupted Sentinel fires its mortar: a flight time and no speed, here a quarter-arc over 8 m, with
 * a 2 m blast where it lands, aimed at the wearer's place. Control, at nine Void: it lands and its blast takes the
 * wearer's and the bystander's health. At ten, with the roll pinned at 10: it is consumed in the air with ground still
 * to cover, and nobody loses anything.
 */
bool FCataclysmVoidAuraLobTest::RunTest(const FString&)
{
	using namespace CataclysmVoidAuraTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	const FScopedAuraRoll Roll(10.0f);
	if (!TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}

	FAuraScene AtNine(World, FVector::ZeroVector);
	FAuraScene AtTen(World, FVector(0.0f, AuraScenesApart, 0.0f));
	if (!TestTrue(TEXT("set-up: two scenes"), AtNine.IsWhole() && AtTen.IsWhole())
		|| !TestTrue(TEXT("set-up: nine Void"), AuraWear(AtNine.Wearer, 9, true))
		|| !TestTrue(TEXT("set-up: ten Void"), AuraWear(AtTen.Wearer, 10, true)))
	{
		return false;
	}
	const float FlightSeconds = ACataclysmProjectile::LobFlightSecondsFor(0.25f, 8.0f * AuraM);
	const auto Lob = [FlightSeconds](const FAuraScene& Scene)
	{
		return ACataclysmProjectile::Fire(Scene.Firer, Scene.Firer->GetActorLocation(), Scene.Origin,
										  AuraBlastRadiusCm, /*InSpeed=*/0.0f, /*InPierce=*/0, /*bInReturns=*/false,
										  /*InDamagePercent=*/100.0f, FGameplayTagContainer(), /*bInBurns=*/false,
										  /*InBodyMesh=*/nullptr, FlightSeconds);
	};

	const float NineBefore = AuraHealthOf(AtNine.Wearer);
	const float NineBystanderBefore = AuraHealthOf(AtNine.Bystander);
	ACataclysmProjectile* NineLob = Lob(AtNine);
	if (!TestNotNull(TEXT("set-up: the lob at nine"), NineLob))
	{
		return false;
	}
	TestTrue(TEXT("set-up: it lobs"), NineLob->FlightSeconds > 0.0f);
	AuraFly(NineLob);
	TestFalse(TEXT("control: at nine the lob is not consumed"), NineLob->bConsumedByAVoidAura);
	TestTrue(TEXT("control: it lands and the wearer loses health"), NineBefore - AuraHealthOf(AtNine.Wearer) > 0.0f);
	TestTrue(TEXT("control: and so does the bystander"), NineBystanderBefore - AuraHealthOf(AtNine.Bystander) > 0.0f);

	const float TenBefore = AuraHealthOf(AtTen.Wearer);
	const float TenBystanderBefore = AuraHealthOf(AtTen.Bystander);
	ACataclysmProjectile* TenLob = Lob(AtTen);
	if (!TestNotNull(TEXT("set-up: the lob at ten"), TenLob))
	{
		return false;
	}
	AuraFly(TenLob);
	TestTrue(TEXT("at ten the lob is consumed"), TenLob->bConsumedByAVoidAura);
	TestTrue(*FString::Printf(TEXT("in the air, with ground still to cover: %.1f cm"), TenLob->RemainingRangeCm),
			 TenLob->RemainingRangeCm > 100.0f);
	TestEqual(TEXT("the wearer loses nothing"), TenBefore - AuraHealthOf(AtTen.Wearer), 0.0f, 0.01f);
	TestEqual(TEXT("and nor does the bystander: no blast on the ground"),
			  TenBystanderBefore - AuraHealthOf(AtTen.Bystander), 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidAuraEndsWithTheSpendTest,
	"Cataclysm.Silence.Aura.SpendingTheVoidEndsTheAuraAndTheNextEnemyShotHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * One wearer at ten, with the roll pinned at 10. Control: a shot is consumed. Then the free next ability is spent, as
 * `UCataclysmSkillTemplate::CommitAndBegin` spends it: all ten Void go, the aura is down, and the next shot lands.
 */
bool FCataclysmVoidAuraEndsWithTheSpendTest::RunTest(const FString&)
{
	using namespace CataclysmVoidAuraTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	const FScopedAuraRoll Roll(10.0f);
	if (!TestNotNull(TEXT("set-up: the roll can be pinned"), Roll.Variable))
	{
		return false;
	}

	FAuraScene Scene(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: the scene"), Scene.IsWhole())
		|| !TestTrue(TEXT("set-up: ten Void and the ten-piece"), AuraWear(Scene.Wearer, 10, true)))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* System = AuraSystemOf(Scene.Wearer);

	const float FirstBefore = AuraHealthOf(Scene.Wearer);
	ACataclysmProjectile* First = Scene.FireAtTheWearer();
	if (!TestNotNull(TEXT("set-up: the first shot"), First))
	{
		return false;
	}
	AuraFly(First);
	TestTrue(TEXT("control: at ten the first shot is consumed"), First->bConsumedByAVoidAura);
	TestEqual(TEXT("control: and the wearer loses nothing"), FirstBefore - AuraHealthOf(Scene.Wearer), 0.0f, 0.01f);

	TestEqual(TEXT("the free next ability is spent, at twice"), System->SpendFreeNextAbility(), 2.0f, 0.0001f);
	TestEqual(TEXT("and leaves no Void"), System->KeptStacksHeldNamed(TEXT("Void")), 0);
	TestFalse(TEXT("so the aura is down"), UCataclysmVoidAura::IsUp(System));

	const float SecondBefore = AuraHealthOf(Scene.Wearer);
	ACataclysmProjectile* Second = Scene.FireAtTheWearer();
	if (!TestNotNull(TEXT("set-up: the second shot"), Second))
	{
		return false;
	}
	AuraFly(Second);
	TestFalse(TEXT("the next shot is never rolled"), Second->bVoidAuraRolled);
	TestFalse(TEXT("nor consumed"), Second->bConsumedByAVoidAura);
	TestTrue(TEXT("and it lands"), SecondBefore - AuraHealthOf(Scene.Wearer) > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidAuraLineTest,
	"Cataclysm.Silence.Aura.TheLineSaysTheAuraIsUpAtTenAndNotAtNine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The words on the line above the player's bars. The entry first, as `KeptStacksEntry` makes it; then on living
 * wearers through `KeptStacksTextFor`: one at ten without the ten-piece, one at nine with it, and one at ten with it.
 */
bool FCataclysmVoidAuraLineTest::RunTest(const FString&)
{
	using namespace CataclysmVoidAuraTest;
	TestEqual(TEXT("control: the entry at ten with no aura is the six-piece's"),
			  UCataclysmCombatOverlay::KeptStacksEntry(TEXT("Void"), 10, 10, true, 100.0f),
			  FString(TEXT("Void 10/10: next ability free, 100% more damage")));
	TestEqual(TEXT("the entry at ten with the aura up ends \", void aura\""),
			  UCataclysmCombatOverlay::KeptStacksEntry(TEXT("Void"), 10, 10, true, 100.0f, true),
			  FString(TEXT("Void 10/10: next ability free, 100% more damage, void aura")));

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* TenNoAura = AuraSpawnCreature(World, ECataclysmTeam::Players, FVector::ZeroVector);
	ACataclysmEnemyCharacter* AtNine = AuraSpawnCreature(World, ECataclysmTeam::Players,
														 FVector(0.0f, AuraScenesApart, 0.0f));
	ACataclysmEnemyCharacter* AtTen = AuraSpawnCreature(World, ECataclysmTeam::Players,
														FVector(0.0f, 2.0f * AuraScenesApart, 0.0f));
	if (!TestTrue(TEXT("set-up: three wearers"), TenNoAura && AtNine && AtTen)
		|| !TestTrue(TEXT("set-up: ten Void and the six-piece alone"), AuraWear(TenNoAura, 10, false))
		|| !TestTrue(TEXT("set-up: nine Void and the ten-piece"), AuraWear(AtNine, 9, true))
		|| !TestTrue(TEXT("set-up: ten Void and the ten-piece"), AuraWear(AtTen, 10, true)))
	{
		return false;
	}
	TestEqual(TEXT("control: ten Void without the ten-piece reads no aura"),
			  UCataclysmCombatOverlay::KeptStacksTextFor(TenNoAura),
			  FString(TEXT("Void 10/10: next ability free, 100% more damage")));
	TestEqual(TEXT("nine Void with the ten-piece reads no aura"), UCataclysmCombatOverlay::KeptStacksTextFor(AtNine),
			  FString(TEXT("Void 9/10")));
	TestEqual(TEXT("ten Void with the ten-piece reads the aura up"), UCataclysmCombatOverlay::KeptStacksTextFor(AtTen),
			  FString(TEXT("Void 10/10: next ability free, 100% more damage, void aura")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
