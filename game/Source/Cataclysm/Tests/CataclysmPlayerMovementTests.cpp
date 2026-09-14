// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
// For the health a bonus can be made to depend on. Issue #959.
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
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

#endif // WITH_AUTOMATION_TESTS
