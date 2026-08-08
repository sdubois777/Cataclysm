// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
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
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (World)
		{
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}
		return World;
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

#endif // WITH_AUTOMATION_TESTS
