// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
// For the health a bonus can be made to depend on. Issue #959.
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
// For a hostile body to stand near, which is what the Unstoppable clause
// counts. Issue #1515.
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"

/**
 * Tests for how fast the player walks.
 *
 * WHAT THESE GUARD. Issue #391. ACataclysmPlayerCharacter never touched
 * MaxWalkSpeed, so the player ran at Unreal's engine default of 600 cm/s while
 * the design gives the three Demonic classes 460, 350 and 400. Nothing noticed
 * for as long as the project has existed, because a character that moves is a
 * character that looks like it is working.
 *
 * The second failure it guards is subtler and is the reason the pawn reads an
 * attribute rather than holding a number. Movement speed has a suffix affix,
 * four boot implicits, an attribute that scales it and several enchantments. A
 * pawn that took its speed once at spawn would leave every one of those moving
 * a number that changed nothing.
 *
 * WHY THE ATTRIBUTE TEST DOES NOT USE 4.0. That is what the attribute already
 * starts at AND what the constructor writes, so a pawn that ignored the
 * attribute entirely would still read 400 and the test would pass. Every
 * assertion below therefore uses a speed the constructor does not produce.
 */

namespace CataclysmPlayerMovementTest
{
	/** A world that has begun play, so a spawned player state gets its ability
	 *  system component initialised and its attribute sets registered. */
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/**
	 * UCharacterMovementComponent's own default, in centimetres per second.
	 *
	 * Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp
	 * sets `MaxWalkSpeed = 600.f` in the constructor. Written here as a literal
	 * because it is the value the fault produced, and a test that compared
	 * against the engine's live default would stop meaning anything if Epic ever
	 * changed it.
	 */
	constexpr float EngineDefaultWalkSpeedCmPerSecond = 600.0f;

	/** Ravager and Ritualist movement speed from game/Data/ClassStats.csv, in
	 *  metres per second. Neither is the shared Default line, which is what the
	 *  constructor writes, so either one distinguishes the attribute path from
	 *  the constructor. */
	constexpr float RavagerMetresPerSecond = 4.6f;
	constexpr float RitualistMetresPerSecond = 3.5f;

	/** Centimetres in a metre, so a case can place a body in metres. */
	constexpr float M = 100.0f;

	/** The reach the Unstoppable row names. */
	constexpr float FourMetres = 4.0f;

	/** A hostile body, for a row that counts who is standing near. */
	static ACataclysmEnemyCharacter* SpawnHostile(UWorld* World,
												  const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
														FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(1'000'000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	/**
	 * One modifier, with every field stated.
	 *
	 * EVERY FIELD, INCLUDING THE ONES A CASE DOES NOT USE, so a reader of a case
	 * sees the whole row rather than the difference from a default they have to
	 * go and look up. `CataclysmEnemiesInReachTests.cpp` says the same and for
	 * the same reason.
	 */
	static FCataclysmStatModifier Row(ECataclysmStatBucket Bucket, float Value,
									  ECataclysmStatCondition Condition,
									  float ConditionValue, float ReachMetres)
	{
		FCataclysmStatModifier Made;
		Made.Bucket = Bucket;
		Made.Source = ECataclysmModifierSource::PassiveKeystone;
		Made.Value = Value;
		Made.Condition = Condition;
		Made.ConditionValue = ConditionValue;
		Made.Scale = ECataclysmStatScale::Fixed;
		Made.ScaleStep = 0.0f;
		Made.ReachMetres = ReachMetres;
		return Made;
	}

	/** A row that applies always and is worth its value. */
	static FCataclysmStatModifier Always(ECataclysmStatBucket Bucket, float Value)
	{
		return Row(Bucket, Value, ECataclysmStatCondition::Always, 0.0f,
				   /*ReachMetres=*/-1.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPlayerWalksAtADesignedSpeed,
	"Cataclysm.Player.WalksAtADesignedSpeedRatherThanTheEngineDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerWalksAtADesignedSpeed::RunTest(const FString&)
{
	using namespace CataclysmPlayerMovementTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player character spawned"), Character))
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!TestNotNull(TEXT("movement component"), Movement))
	{
		return false;
	}

	TestEqual(TEXT("a player with no ability system walks at the designed default"),
		Movement->MaxWalkSpeed,
		ACataclysmPlayerCharacter::DefaultWalkSpeedCmPerSecond);

	// SAID SEPARATELY, because this is the fault rather than a restatement of
	// the line above. A pawn that never assigned MaxWalkSpeed would report 600
	// and every judgement about closing and escaping made against it would be
	// wrong; that was the state of the project until issue #391 was fixed.
	TestNotEqual(TEXT("and not at the engine's own default"),
		Movement->MaxWalkSpeed, EngineDefaultWalkSpeedCmPerSecond);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPlayerFollowsTheMovementSpeedAttribute,
	"Cataclysm.Player.MovementSpeedFollowsTheAttribute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerFollowsTheMovementSpeedAttribute::RunTest(const FString&)
{
	using namespace CataclysmPlayerMovementTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("player state spawned"), PlayerState))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState->GetCataclysmAbilitySystemComponent();
	if (!TestNotNull(TEXT("ability system component"), AbilitySystem))
	{
		return false;
	}

	const FGameplayAttribute Speed =
		UCataclysmCombatAttributeSet::GetMovementSpeedAttribute();
	if (!TestTrue(TEXT("the combat attribute set is registered"),
		AbilitySystem->HasAttributeSetForAttribute(Speed)))
	{
		return false;
	}

	// SET BEFORE THE PAWN IS WIRED UP, so that what the pawn reads at
	// initialisation differs from what its constructor wrote. Without this the
	// attribute already holds 4.0, the constructor already wrote 400, and a pawn
	// that ignored the attribute completely would pass.
	AbilitySystem->SetNumericAttributeBase(Speed, RavagerMetresPerSecond);

	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player character spawned"), Character))
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!TestNotNull(TEXT("movement component"), Movement))
	{
		return false;
	}

	TestEqual(TEXT("before the player state arrives it holds the designed default"),
		Movement->MaxWalkSpeed,
		ACataclysmPlayerCharacter::DefaultWalkSpeedCmPerSecond);

	// The client path. OnRep_PlayerState is what runs when the player state
	// replicates, and it calls the same InitAbilityActorInfo the server reaches
	// from PossessedBy. Driven directly because a test world has no controller
	// to possess with and no network to replicate over.
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	TestEqual(TEXT("once the ability system is up it takes the attribute's value"),
		Movement->MaxWalkSpeed,
		RavagerMetresPerSecond * ACataclysmPlayerCharacter::CentimetresPerMetre);

	// AND KEEPS FOLLOWING IT. This is the assertion that makes gear, passives
	// and enchantments able to change how fast the player moves. Without the
	// change delegate the value above would still be right and this would fail.
	AbilitySystem->SetNumericAttributeBase(Speed, RitualistMetresPerSecond);

	TestEqual(TEXT("and follows it when something changes it afterwards"),
		Movement->MaxWalkSpeed,
		RitualistMetresPerSecond * ACataclysmPlayerCharacter::CentimetresPerMetre);

	// A ZERO IS REFUSED RATHER THAN WRITTEN. An ability system holding no combat
	// attribute set reports zero rather than failing, and a MaxWalkSpeed of zero
	// is a character who cannot move at all with nothing on screen saying why.
	AbilitySystem->SetNumericAttributeBase(Speed, 0.0f);

	TestEqual(TEXT("a movement speed of zero leaves the last usable speed alone"),
		Movement->MaxWalkSpeed,
		RitualistMetresPerSecond * ACataclysmPlayerCharacter::CentimetresPerMetre);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPlayerSpeedFollowsAHealthCondition,
	"Cataclysm.Player.MovementSpeedFollowsABonusThatDependsOnHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerSpeedFollowsAHealthCondition::RunTest(const FString&)
{
	using namespace CataclysmPlayerMovementTest;

	// THE MASOCHIST'S DESPERATE MEASURES NODE: "While at or below 50% health,
	// +1% increased Movement Speed per point", held at its full ten points.
	// Issue #959.
	//
	// WHY THIS TEST EXISTS ALONGSIDE THE PIPELINE ONES. A conditional bonus is
	// deliberately never written onto the gameplay attribute, so the delegate
	// that normally re-tells the movement component a speed does not fire when
	// health crosses the threshold. Something has to notice, and only this test
	// goes through the thing that does. Every test in
	// CataclysmStatPipelineTests.cpp would go on passing with that connection
	// deleted, and the player would simply never speed up.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ability system component"), AbilitySystem))
	{
		return false;
	}

	const FGameplayAttribute Speed =
		UCataclysmCombatAttributeSet::GetMovementSpeedAttribute();
	AbilitySystem->SetNumericAttributeBase(Speed, RavagerMetresPerSecond);

	// A HUNDRED HEALTH OUT OF A HUNDRED, so the share is the figure itself.
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 100.0f);

	// THE STAT'S INPUTS AS `UCataclysmPlayerClassStats::ApplyTo` WOULD LEAVE
	// THEM. Written directly rather than by spending a passive point, because
	// this test is about whether the pawn notices a conditional bonus and not
	// about how one gets onto the character.
	FCataclysmStatInputs Inputs;
	Inputs.Base = RavagerMetresPerSecond;

	FCataclysmStatModifier Conditional;
	Conditional.Bucket = ECataclysmStatBucket::Increased;
	Conditional.Source = ECataclysmModifierSource::PassiveKeystone;
	Conditional.Value = 10.0f;
	Conditional.Condition = ECataclysmStatCondition::HealthAtOrBelowPercent;
	Conditional.ConditionValue = 50.0f;
	Inputs.Modifiers.Add(Conditional);

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("movement_speed")), Inputs);
	AbilitySystem->SetStatInputs(MoveTemp(Stats));

	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	const UCharacterMovementComponent* Movement =
		Character ? Character->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("movement component"), Movement))
	{
		return false;
	}

	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	const float Plain =
		RavagerMetresPerSecond * ACataclysmPlayerCharacter::CentimetresPerMetre;

	TestEqual(TEXT("at full health the bonus is not applied"),
		Movement->MaxWalkSpeed, Plain);

	// HEALTH CROSSES THE THRESHOLD AND NOTHING WRITES THE SPEED ATTRIBUTE.
	// `HealthChanged` is the only thing that can notice, and this is what says
	// the pawn is listening to it.
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 40.0f);
	Character->HealthChanged();

	TestEqual(TEXT("below the threshold it speeds up by a tenth"),
		Movement->MaxWalkSpeed, Plain * 1.1f, 0.01f);

	// AND BACK OFF AGAIN WHEN THE CHARACTER HEALS. A bonus that came on and
	// never went off would be a bonus a player keeps by taking one hit.
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 90.0f);
	Character->HealthChanged();

	TestEqual(TEXT("and back to its plain speed when healed"),
		Movement->MaxWalkSpeed, Plain, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPlayerSpeedFollowsAFullClassResource,
	"Cataclysm.Player.MovementSpeedFollowsABonusThatDependsOnTheClassResource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerSpeedFollowsAFullClassResource::RunTest(const FString&)
{
	using namespace CataclysmPlayerMovementTest;

	// "WHEN YOUR CLASS RESOURCE IS FULL YOUR MOVEMENT SPEED IS INCREASED BY
	// 15%-30%" -- a shipped enchantment row that never reached the character.
	// Issue #1825.
	//
	// NOTHING IS CALLED BY HAND HERE, AND THAT IS THE WHOLE POINT. The sibling
	// test above drives `HealthChanged()` itself, because health reaches the
	// pawn through an explicit call chain that a test world never runs; it
	// therefore proves the pawn responds WHEN TOLD, and would keep passing with
	// every binding deleted. The class resource is a gameplay attribute, so
	// writing it fires the change delegate in a test world exactly as in play.
	// This test writes the attribute and reads the movement component, with no
	// call in between, so it fails when nothing is listening -- which is the
	// defect.
	//
	// AND IT IS THE ONLY TEST THAT CAN. `UCataclysmStatPipeline` resolves this
	// modifier correctly with or without the binding, so every pipeline test
	// passes either way. What was broken was the delivery of a correct answer
	// to the thing that moves the character.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ability system component"), AbilitySystem))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetMovementSpeedAttribute(),
		RavagerMetresPerSecond);

	// A HUNDRED POINT POOL HOLDING FORTY, so the condition is false to begin
	// with and the first assertion is about a bonus that is genuinely refused
	// rather than one that is simply absent.
	const FGameplayAttribute Held =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const FGameplayAttribute Maximum =
		UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute();
	AbilitySystem->SetNumericAttributeBase(Maximum, 100.0f);
	AbilitySystem->SetNumericAttributeBase(Held, 40.0f);

	FCataclysmStatInputs Inputs;
	Inputs.Base = RavagerMetresPerSecond;

	FCataclysmStatModifier Conditional;
	Conditional.Bucket = ECataclysmStatBucket::Increased;
	Conditional.Source = ECataclysmModifierSource::GearAffix;
	Conditional.Value = 20.0f;
	Conditional.Condition = ECataclysmStatCondition::ClassResourceAtMaximum;
	Inputs.Modifiers.Add(Conditional);

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("movement_speed")), Inputs);
	AbilitySystem->SetStatInputs(MoveTemp(Stats));

	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	const UCharacterMovementComponent* Movement =
		Character ? Character->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("movement component"), Movement))
	{
		return false;
	}

	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	const float Plain =
		RavagerMetresPerSecond * ACataclysmPlayerCharacter::CentimetresPerMetre;

	TestEqual(TEXT("a pool that is not full leaves the speed alone"),
		Movement->MaxWalkSpeed, Plain, 0.01f);

	// THE POOL FILLS AND NOTHING WRITES THE SPEED ATTRIBUTE.
	AbilitySystem->SetNumericAttributeBase(Held, 100.0f);

	TestEqual(TEXT("filling the pool speeds the character up by a fifth"),
		Movement->MaxWalkSpeed, Plain * 1.2f, 0.01f);

	// AND SPENDING IT TAKES THE BONUS BACK. A bonus that came on and never went
	// off would be one a player keeps for the rest of a run by filling the bar
	// once.
	AbilitySystem->SetNumericAttributeBase(Held, 40.0f);

	TestEqual(TEXT("and spending it returns the character to its plain speed"),
		Movement->MaxWalkSpeed, Plain, 0.01f);

	// AND THE MAXIMUM COUNTS TOO, WHICH THE HELD VALUE ALONE CANNOT SHOW.
	// `ClassResourceAtMaximum` compares the two, so a maximum falling to meet a
	// held value that never moved makes the pool full. Binding only the pool
	// would pass every assertion above and fail this one. The Crowned thrall
	// lowering a summoner's Fervour reserve is that case in play.
	AbilitySystem->SetNumericAttributeBase(Maximum, 40.0f);

	TestEqual(TEXT("and a maximum falling to meet the pool fills it too"),
		Movement->MaxWalkSpeed, Plain * 1.2f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPlayerSpeedFollowsTheRealResourcePath,
	"Cataclysm.Player.MovementSpeedFollowsTheClassResourceMovedTheWayPlayMovesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerSpeedFollowsTheRealResourcePath::RunTest(const FString&)
{
	using namespace CataclysmPlayerMovementTest;

	// THE SAME CLAIM AS THE TEST ABOVE, MADE THROUGH THE PATH PLAY USES.
	// Issue #1825.
	//
	// WHY A SECOND TEST RATHER THAN A SECOND ASSERTION. The test above writes
	// the pool with `SetNumericAttributeBase`. Nothing in play does: every gain
	// and every spend goes through `UCataclysmFervour::Move`, which writes with
	// `ApplyModToAttribute`. Those are different routes into the attribute, and
	// a change delegate firing for one says nothing about the other. A test
	// that drives a stat by a route the game never takes is the same fault as
	// one that calls its own handler by hand -- it passes while the thing it is
	// named for never happens.
	//
	// READING THE ENGINE SAYS THIS SHOULD PASS, AND THE READING IS NOT THE
	// PROOF. `ApplyModToAttribute` reaches `SetAttributeBaseValue`, whose two
	// branches -- with an aggregator and without -- both end at
	// `InternalUpdateNumericalAttribute`, which broadcasts
	// `AttributeValueChangeDelegates`. That is the delegate
	// `InitAbilityActorInfo` binds. This test is what holds that true through
	// an engine upgrade, when nobody will re-read those four functions.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ability system component"), AbilitySystem))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetMovementSpeedAttribute(),
		RavagerMetresPerSecond);

	// FIVE HUNDRED HEALTH AND A RATE OF ONE, so a point of Fervour arrives for
	// every one per cent of maximum health lost: losing all five hundred fills
	// a hundred-point pool exactly. `UCataclysmFervour::Move` refuses a
	// character with no generator at all, so the rate is what makes this a
	// character that can fill its bar.
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 500.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 500.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetFervourFromDamageAttribute(),
		1.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetFervourLostToHealingAttribute(), 1.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
		100.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);

	FCataclysmStatInputs Inputs;
	Inputs.Base = RavagerMetresPerSecond;

	FCataclysmStatModifier Conditional;
	Conditional.Bucket = ECataclysmStatBucket::Increased;
	Conditional.Source = ECataclysmModifierSource::GearAffix;
	Conditional.Value = 20.0f;
	Conditional.Condition = ECataclysmStatCondition::ClassResourceAtMaximum;
	Inputs.Modifiers.Add(Conditional);

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("movement_speed")), Inputs);
	AbilitySystem->SetStatInputs(MoveTemp(Stats));

	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	const UCharacterMovementComponent* Movement =
		Character ? Character->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("movement component"), Movement))
	{
		return false;
	}

	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	const float Plain =
		RavagerMetresPerSecond * ACataclysmPlayerCharacter::CentimetresPerMetre;

	TestEqual(TEXT("an empty pool leaves the speed alone"),
		Movement->MaxWalkSpeed, Plain, 0.01f);

	// THE POOL FILLS THE WAY PLAY FILLS IT. Nothing is called on the character.
	const float Gained = UCataclysmFervour::GainFromDamage(
		AbilitySystem, /*HealthLost=*/500.0f, FGameplayTagContainer());

	// ASSERT THE STATE THIS TEST BUILT BEFORE ASSERTING WHAT FOLLOWS FROM IT.
	// A rate that had stopped generating would leave the pool empty, the speed
	// unchanged, and the assertion below passing for the wrong reason -- it
	// would read as "the delegate did not fire" when nothing had happened at
	// all.
	if (!TestEqual(TEXT("the real path filled the bar"), Gained, 100.0f, 0.01f)
		|| !TestEqual(TEXT("and the pool is at its maximum"),
			AbilitySystem->GetNumericAttribute(
				UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()),
			100.0f, 0.01f))
	{
		return false;
	}

	TestEqual(TEXT("a pool filled through ApplyModToAttribute speeds the "
				   "character up by a fifth"),
		Movement->MaxWalkSpeed, Plain * 1.2f, 0.01f);

	// AND SPENDING IT THE SAME WAY TAKES THE BONUS BACK.
	const float Lost = UCataclysmFervour::RemoveForHealing(
		AbilitySystem, /*HealthRestored=*/300.0f, FGameplayTagContainer());

	if (!TestEqual(TEXT("the real path drained the bar"), Lost, -60.0f, 0.01f))
	{
		return false;
	}

	TestEqual(TEXT("and the character returns to its plain speed"),
		Movement->MaxWalkSpeed, Plain, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmClassResourceWindowsOpenOnCrossings,
	"Cataclysm.Player.TheClassResourceWindowsOpenOnACrossingAndNotWhileThePoolSitsThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClassResourceWindowsOpenOnCrossings::RunTest(const FString&)
{
	using namespace CataclysmPlayerMovementTest;

	// THE TWO WINDOWS A THRESHOLD CROSSING OPENS. Issue #1815.
	//
	// AN EVENT IS NOT A STATE, AND THAT IS THE WHOLE TEST.
	// `class_resource_at_maximum` holds for as long as the bar is full.
	// `seconds_after_resource_full` opens at the moment it fills and then AGES
	// while the bar sits there. A stamp written on every change rather than on
	// a crossing would re-open the window on every point gained at maximum, and
	// it would never age at all -- which no assertion about the window merely
	// being open could tell apart.
	//
	// DRIVEN THROUGH `UCataclysmFervour`, the path play uses, for the reason the
	// test above it gives: a delegate firing for `SetNumericAttributeBase` says
	// nothing about `ApplyModToAttribute`.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ability system component"), AbilitySystem))
	{
		return false;
	}

	const FGameplayAttribute Held =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const FGameplayAttribute Maximum =
		UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute();

	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 500.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 500.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetFervourFromDamageAttribute(), 1.0f);
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmClassResourceAttributeSet::GetFervourLostToHealingAttribute(), 1.0f);
	AbilitySystem->SetNumericAttributeBase(Maximum, 100.0f);
	AbilitySystem->SetNumericAttributeBase(Held, 0.0f);

	// THE PAWN IS WHAT CARRIES THE BINDING, so the windows cannot open without
	// one even though the clocks live on the ability system component.
	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a character"), Character))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	TestEqual(TEXT("an empty pool has opened no full window"),
		AbilitySystem->SecondsSinceClassResourceFull(), -1.0f, 0.001f);
	TestEqual(TEXT("and starting empty is not the pool REACHING zero"),
		AbilitySystem->SecondsSinceClassResourceEmptied(), -1.0f, 0.001f);

	// HALF FULL IS NOT FULL.
	UCataclysmFervour::GainFromDamage(AbilitySystem, /*HealthLost=*/250.0f,
									  FGameplayTagContainer());
	if (!TestEqual(TEXT("the pool holds fifty"),
				   AbilitySystem->GetNumericAttribute(Held), 50.0f, 0.01f))
	{
		return false;
	}
	TestEqual(TEXT("a half-full pool opens no window"),
		AbilitySystem->SecondsSinceClassResourceFull(), -1.0f, 0.001f);

	// AND FILLING IT OPENS ONE.
	UCataclysmFervour::GainFromDamage(AbilitySystem, /*HealthLost=*/250.0f,
									  FGameplayTagContainer());
	if (!TestEqual(TEXT("the pool is now at its maximum"),
				   AbilitySystem->GetNumericAttribute(Held), 100.0f, 0.01f))
	{
		return false;
	}
	TestEqual(TEXT("filling the pool opens the full window, now"),
		AbilitySystem->SecondsSinceClassResourceFull(), 0.0f, 0.001f);

	// SITTING AT FULL DOES NOT RE-STAMP, WHICH IS THE ASSERTION THE WHOLE
	// CROSSING RULE EXISTS FOR. Time is advanced, and the pool is then
	// disturbed twice without ever ceasing to be full.
	//
	// THE FIRST DISTURBANCE PROVES NOTHING AND IS KEPT FOR WHAT IT DOCUMENTS.
	// `UCataclysmFervour::Move` returns early when the change it would write is
	// nearly zero, so a gain at a pool already at its maximum writes no
	// attribute and never reaches the handler at all. Measured by guard proof on
	// 2026-09-14: with the crossing test neutralised, this assertion still
	// passed, which is why the step below had to be written.
	World->TimeSeconds += 2.0f;
	UCataclysmFervour::GainFromDamage(AbilitySystem, /*HealthLost=*/100.0f,
									  FGameplayTagContainer());
	TestEqual(TEXT("a gain at maximum is refused and opens nothing"),
		AbilitySystem->SecondsSinceClassResourceFull(), 2.0f, 0.01f);

	// THE SECOND DISTURBANCE IS THE ONE THAT SEPARATES THEM: the MAXIMUM falls
	// onto a pool that does not move. That does reach the handler, which is bound
	// to the maximum attribute as well as to the pool, and the character is full
	// before it and full after it. A stamp written on every change puts the
	// reading back to zero here; a crossing leaves it aged.
	//
	// AND IT IS A REAL CASE rather than one invented for the test: the Crowned
	// thrall lowers a summoner's Fervour reserve.
	AbilitySystem->SetNumericAttributeBase(Maximum, 80.0f);
	if (!TestEqual(TEXT("the maximum has fallen to eighty"),
				   AbilitySystem->GetNumericAttribute(Maximum), 80.0f, 0.01f))
	{
		return false;
	}
	if (!TestTrue(TEXT("and the pool is still at or above it, so still full"),
				  AbilitySystem->GetNumericAttribute(Held) >= 80.0f))
	{
		return false;
	}
	TestEqual(TEXT("a maximum falling onto a full pool does not re-open it"),
		AbilitySystem->SecondsSinceClassResourceFull(), 2.0f, 0.01f);

	// AND PUT BACK, so the steps below start from the hundred they were written
	// against.
	AbilitySystem->SetNumericAttributeBase(Maximum, 100.0f);

	// SPENDING TO ZERO OPENS THE OTHER ONE.
	UCataclysmFervour::RemoveForHealing(AbilitySystem, /*HealthRestored=*/500.0f,
										FGameplayTagContainer());
	if (!TestEqual(TEXT("the pool is empty"),
				   AbilitySystem->GetNumericAttribute(Held), 0.0f, 0.01f))
	{
		return false;
	}
	TestEqual(TEXT("reaching zero opens the empty window, now"),
		AbilitySystem->SecondsSinceClassResourceEmptied(), 0.0f, 0.001f);

	// AND THE FULL WINDOW IS LEFT ALONE BY IT, still ageing from its own
	// crossing rather than being cleared or re-stamped by an unrelated one.
	TestEqual(TEXT("and the full window keeps ageing rather than being cleared"),
		AbilitySystem->SecondsSinceClassResourceFull(), 2.0f, 0.01f);

	// AND REFILLING STAMPS AGAIN, so the crossing rule has not simply stopped
	// the window ever opening a second time.
	World->TimeSeconds += 5.0f;
	UCataclysmFervour::GainFromDamage(AbilitySystem, /*HealthLost=*/500.0f,
									  FGameplayTagContainer());
	TestEqual(TEXT("refilling from empty opens the full window again"),
		AbilitySystem->SecondsSinceClassResourceFull(), 0.0f, 0.001f);

	// AND THE STATE THE PIPELINE IS HANDED CARRIES BOTH.
	const FCataclysmStatConditions State = AbilitySystem->CurrentConditions();
	TestEqual(TEXT("the pipeline is told the full reading"),
		State.SecondsSinceClassResourceFull, 0.0f, 0.001f);
	TestEqual(TEXT("and the empty reading"),
		State.SecondsSinceClassResourceEmpty, 5.0f, 0.01f);

	// AND A RESPAWN AT AN ALREADY-EMPTY POOL OPENS NEITHER WINDOW. A write that
	// starts at zero and ends at zero is the only shape that separates "the pool
	// REACHED zero" from "the pool IS at zero", and it is not reachable through
	// `UCataclysmFervour`, which refuses to write a change of nothing.
	//
	// IT IS THE ROUTE A RESPAWN TAKES, in that order.
	// `ACataclysmPlayerCharacter::Revive` calls `ClearWhatDeathEnds` first, which
	// forgets every window, and then writes the pool to zero directly. A
	// character that died with an empty pool is written zero over zero, and
	// `FActiveGameplayEffectsContainer::InternalUpdateNumericalAttribute`
	// broadcasts with no equality test in it, so that write does reach the
	// handler.
	//
	// WITHOUT THE "WAS IT ABOVE ZERO BEFORE" TEST, such a character would stand
	// up with the window that says it has just emptied its class resource open,
	// which is the thing the forgetting exists to prevent.
	UCataclysmFervour::RemoveForHealing(AbilitySystem, /*HealthRestored=*/500.0f,
										FGameplayTagContainer());
	if (!TestEqual(TEXT("the pool is empty again"),
				   AbilitySystem->GetNumericAttribute(Held), 0.0f, 0.01f))
	{
		return false;
	}
	AbilitySystem->ClearWhatDeathEnds();
	if (!TestEqual(TEXT("a respawn has forgotten the window that emptying opened"),
				   AbilitySystem->SecondsSinceClassResourceEmptied(), -1.0f, 0.001f))
	{
		return false;
	}
	AbilitySystem->SetNumericAttributeBase(Held, 0.0f);
	TestEqual(TEXT("and writing zero over zero does not open the empty window"),
		AbilitySystem->SecondsSinceClassResourceEmptied(), -1.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmAPoolThatCannotHoldAnythingIsNotFull,
	"Cataclysm.Player.APoolWithAMaximumOfZeroOpensNoFullWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAPoolThatCannotHoldAnythingIsNotFull::RunTest(const FString&)
{
	using namespace CataclysmPlayerMovementTest;

	// THE STAMP MUST APPLY `ClassResourceAtMaximum`'S OWN RULE, NOT A SECOND
	// OPINION ON IT. Issue #1815. That predicate refuses a maximum of zero, and
	// says why: every enemy and every character built without a class resource
	// sits at zero of zero, and each would otherwise satisfy a bonus written for
	// a full bar.
	//
	// IF THE STAMP DID NOT AGREE, the window and the condition would disagree
	// about the same pool: the window would open and the condition would refuse
	// it, so the row would do nothing and nothing would say why.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ability system component"), AbilitySystem))
	{
		return false;
	}

	const FGameplayAttribute Held =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const FGameplayAttribute Maximum =
		UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute();

	ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a character"), Character))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	// A BAR THAT CANNOT HOLD ANYTHING. Held and maximum are both zero, so a
	// stamp comparing only the two readings would call it full.
	AbilitySystem->SetNumericAttributeBase(Maximum, 100.0f);
	AbilitySystem->SetNumericAttributeBase(Held, 0.0f);
	AbilitySystem->SetNumericAttributeBase(Maximum, 0.0f);

	TestEqual(TEXT("a maximum of zero opens no full window"),
		AbilitySystem->SecondsSinceClassResourceFull(), -1.0f, 0.001f);

	// AND THE CONDITION AGREES, which is the point: the two read the same pool
	// and must answer alike about it.
	TestFalse(TEXT("and the condition refuses it too"),
		UCataclysmStatPipeline::ConditionHolds(
			ECataclysmStatCondition::ClassResourceAtMaximum,
			/*Value=*/0.0f, AbilitySystem->CurrentConditions()));

	return true;
}


// ---------------------------------------------------------------------------
// Nothing may lower this character's speed. Issue #1515.
//
// TWO KEYSTONES, ONE STAT, AND THEY DIFFER ONLY IN THEIR ROW'S CONDITION.
// `Ravager_keystone_d_kA` Relentless says "Your Movement Speed cannot be
// reduced by any effect". The third clause of `Ravager_keystone_spine_003`
// Unstoppable says the same while an enemy is within four metres.
//
// WHAT IS BEING SLOWED IS A REAL THING RATHER THAN A TEST INVENTION. Two built
// dungeon rules lower this stat today -- Singularity Wells and Grasping
// Tentacles -- and both do it as a "less" multiplier through the same pipeline
// these rows go through.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPlayerSpeedIgnoresAReduction,
	"Cataclysm.Player.MovementSpeedReductionIsDroppedWhileTheNodeIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Relentless, and the control that says the reduction works without it.
 *
 * THE READING WITHOUT THE NODE IS HALF THE TEST. Being slowed is the rule for
 * every character in the game, so a test that only checked the half with the
 * node would pass against a build where nothing could slow anybody.
 */
bool FCataclysmPlayerSpeedIgnoresAReduction::RunTest(const FString&)
{
	using namespace CataclysmPlayerMovementTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ability system component"), AbilitySystem))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetMovementSpeedAttribute(),
		RavagerMetresPerSecond);

	// HALF SPEED, THE SHAPE A DUNGEON RULE USES. A "less" multiplier reaches the
	// More bucket as a negative percentage, which is what
	// DungeonModifierEffectsAddLess writes.
	FCataclysmStatInputs Inputs;
	Inputs.Base = RavagerMetresPerSecond;
	Inputs.Modifiers.Add(Always(ECataclysmStatBucket::More, -50.0f));

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("movement_speed")), Inputs);
	AbilitySystem->SetStatInputs(MoveTemp(Stats));

	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
													 FRotator::ZeroRotator);
	const UCharacterMovementComponent* Movement =
		Character ? Character->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("movement component"), Movement))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	const float Plain =
		RavagerMetresPerSecond * ACataclysmPlayerCharacter::CentimetresPerMetre;

	// WITHOUT THE NODE THE REDUCTION WORKS, which is every character in the game.
	if (!TestEqual(TEXT("without the node a half-speed effect halves the speed"),
				   Movement->MaxWalkSpeed, Plain * 0.5f, 0.01f))
	{
		return false;
	}

	// WITH IT, THE SAME REDUCTION DOES NOTHING.
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::
			GetMovementSpeedReductionSuppressedAttribute(), 1.0f);
	Character->RefreshMovementSpeed();

	TestEqual(TEXT("with the node the same effect leaves the speed alone"),
			  Movement->MaxWalkSpeed, Plain, 0.01f);

	// AND TAKING IT AWAY PUTS THE REDUCTION BACK, which would catch a build that
	// dropped the modifier once and left it dropped.
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::
			GetMovementSpeedReductionSuppressedAttribute(), 0.0f);
	Character->RefreshMovementSpeed();

	TestEqual(TEXT("and without it again the speed is halved"),
			  Movement->MaxWalkSpeed, Plain * 0.5f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPlayerSpeedKeepsItsEarnedIncrease,
	"Cataclysm.Player.MovementSpeedKeepsAnEarnedIncreaseWhileDroppingAReduction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The node drops the reduction and keeps the bonus, which a floor would not.
 *
 * WHY THIS IS THE TEST THAT MATTERS. The obvious way to build "cannot be
 * reduced" is to floor the answer at the character's own speed. That passes the
 * test above and is WRONG: a character carrying a node worth +20% and standing
 * in a well worth -40% would be put back to their plain speed, losing the bonus
 * they earned along with the reduction they are meant to ignore. The row says
 * the reduction does not apply, not that the increase does not either.
 *
 * THE FIGURES ARE CHOSEN SO THE TWO ANSWERS CANNOT COINCIDE. A floor gives
 * exactly the plain speed; dropping the reduction gives plain x 1.2. They
 * differ by a fifth, which no tolerance here could hide.
 */
bool FCataclysmPlayerSpeedKeepsItsEarnedIncrease::RunTest(const FString&)
{
	using namespace CataclysmPlayerMovementTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ability system component"), AbilitySystem))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetMovementSpeedAttribute(),
		RavagerMetresPerSecond);

	// A NODE WORTH A FIFTH MORE, AND A HAZARD WORTH TWO FIFTHS LESS.
	FCataclysmStatInputs Inputs;
	Inputs.Base = RavagerMetresPerSecond;
	Inputs.Modifiers.Add(Always(ECataclysmStatBucket::Increased, 20.0f));
	Inputs.Modifiers.Add(Always(ECataclysmStatBucket::More, -40.0f));

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("movement_speed")), Inputs);
	AbilitySystem->SetStatInputs(MoveTemp(Stats));

	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
													 FRotator::ZeroRotator);
	const UCharacterMovementComponent* Movement =
		Character ? Character->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("movement component"), Movement))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	const float Plain =
		RavagerMetresPerSecond * ACataclysmPlayerCharacter::CentimetresPerMetre;

	// BOTH APPLY WITHOUT THE NODE: a fifth more, then two fifths of that taken.
	if (!TestEqual(TEXT("without the node both the bonus and the reduction apply"),
				   Movement->MaxWalkSpeed, Plain * 1.2f * 0.6f, 0.01f))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::
			GetMovementSpeedReductionSuppressedAttribute(), 1.0f);
	Character->RefreshMovementSpeed();

	// THE BONUS SURVIVES AND THE REDUCTION DOES NOT.
	TestEqual(TEXT("with the node the earned fifth survives and the reduction "
				   "is dropped"),
			  Movement->MaxWalkSpeed, Plain * 1.2f, 0.01f);

	// SAID A SECOND WAY, BECAUSE THIS IS THE CLAIM THE WHOLE TEST EXISTS FOR.
	TestTrue(TEXT("and the answer is above the plain speed, which a floor at the "
				  "character's own speed could never be"),
			 Movement->MaxWalkSpeed > Plain + 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmPlayerSpeedIgnoresAReductionOnlyWhileCrowded,
	"Cataclysm.Player.MovementSpeedReductionIsDroppedOnlyWhileAnEnemyIsNear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Unstoppable form: the same stat, granted ONLY by a conditioned row.
 *
 * NOTHING WRITES THE ATTRIBUTE HERE, DELIBERATELY. A conditioned row is never
 * folded into a gameplay attribute, so a build that read the flag off the
 * attribute would find zero, drop nothing, and this clause would silently do
 * nothing while the two tests above went on passing. This is the only test that
 * can tell those two builds apart.
 *
 * THE BODY IS SPAWNED RATHER THAN THE COUNT STATED. Nothing here tells the
 * pipeline how many enemies are near or how far away they are; a hostile
 * character is placed and the game measures. A test that supplies the missing
 * step proves nothing.
 */
bool FCataclysmPlayerSpeedIgnoresAReductionOnlyWhileCrowded::RunTest(const FString&)
{
	using namespace CataclysmPlayerMovementTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("ability system component"), AbilitySystem))
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetMovementSpeedAttribute(),
		RavagerMetresPerSecond);

	// THE HAZARD, AND THE FLAG GRANTED ONLY WHILE AN ENEMY IS WITHIN FOUR
	// METRES. Two stat lines, because the flag is its own stat.
	FCataclysmStatInputs Speed;
	Speed.Base = RavagerMetresPerSecond;
	Speed.Modifiers.Add(Always(ECataclysmStatBucket::More, -50.0f));

	FCataclysmStatInputs Flag;
	Flag.Base = 0.0f;
	Flag.Modifiers.Add(Row(ECataclysmStatBucket::Flat, 1.0f,
						   ECataclysmStatCondition::EnemiesInReachAtLeast, 1.0f,
						   FourMetres));

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("movement_speed")), Speed);
	Stats.Add(FName(ACataclysmPlayerCharacter::MovementSpeedReductionSuppressedStat),
			  Flag);
	AbilitySystem->SetStatInputs(MoveTemp(Stats));

	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
													 FRotator::ZeroRotator);
	const UCharacterMovementComponent* Movement =
		Character ? Character->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("movement component"), Movement))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	const float Plain =
		RavagerMetresPerSecond * ACataclysmPlayerCharacter::CentimetresPerMetre;

	// ALONE, THE ROW GRANTS NOTHING AND THE REDUCTION STANDS.
	if (!TestEqual(TEXT("alone, the conditioned row grants nothing and the "
						"reduction still halves the speed"),
				   Movement->MaxWalkSpeed, Plain * 0.5f, 0.01f))
	{
		return false;
	}

	// TEN METRES AWAY IS NOT WITHIN FOUR, and this reading is the control. A
	// build that counted every hostile character whatever the distance would
	// pass a test that only checked "alone" against "crowded".
	SpawnHostile(World, FVector(10.0f * M, 0.0f, 0.0f));
	Character->RefreshMovementSpeed();

	if (!TestEqual(TEXT("a body ten metres away is not within four, so the "
						"reduction still stands"),
				   Movement->MaxWalkSpeed, Plain * 0.5f, 0.01f))
	{
		return false;
	}

	// AND TWO METRES AWAY, ON A DIFFERENT AXIS so that a spawn refused for
	// overlapping another body cannot quietly move a character somewhere else.
	SpawnHostile(World, FVector(0.0f, 2.0f * M, 0.0f));
	Character->RefreshMovementSpeed();

	TestEqual(TEXT("one within four metres drops the reduction entirely"),
			  Movement->MaxWalkSpeed, Plain, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmMovementSuppressionStatNameIsKnown,
	"Cataclysm.Player.TheMovementSuppressionStatNameIsTheOneTheMapKnows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The name this code asks for is the name the data is written against.
 *
 * WHY A TEST RATHER THAN CARE. The stat name appears in this constant, in the
 * key of `UCataclysmPlayerClassStats::StatToAttribute`, and in the `Stat` column
 * of the rows. A character that disagreed by ONE CHARACTER would be granted
 * both nodes and read neither, and nothing anywhere would say so: the pipeline
 * would answer the fallback, both nodes would do nothing, and every test above
 * that writes the attribute directly would still pass.
 */
bool FCataclysmMovementSuppressionStatNameIsKnown::RunTest(const FString&)
{
	const FString Name(
		ACataclysmPlayerCharacter::MovementSpeedReductionSuppressedStat);
	const TMap<FString, FGameplayAttribute>& Map =
		UCataclysmPlayerClassStats::StatToAttribute();

	const FGameplayAttribute* Found = Map.Find(Name);
	if (!TestNotNull(*FString::Printf(
			TEXT("the stat name '%s' is a key in the stat-name map"), *Name),
					 Found))
	{
		return false;
	}

	TestTrue(TEXT("and it names the movement-speed-reduction-suppressed "
				  "attribute"),
			 *Found == UCataclysmCombatAttributeSet::
				 GetMovementSpeedReductionSuppressedAttribute());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
