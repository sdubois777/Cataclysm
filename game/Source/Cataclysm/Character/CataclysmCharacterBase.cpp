// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "Engine/World.h"
#include "TimerManager.h"

ACataclysmCharacterBase::ACataclysmCharacterBase()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACataclysmCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	// A REPEATING TIMER RATHER THAN A TICK. This is the only per-frame work any
	// of these characters would have and it does not need a frame. The base
	// disables ticking for every character deliberately, and the player pawn's
	// own Tick switches itself off the moment its camera glide settles, so
	// adding regeneration to either would undo that.
	//
	// NOT AN INFINITE PERIODIC GAMEPLAY EFFECT, which is the other shape this
	// could take. Its magnitude would have to be attribute-based, because the
	// amount comes from the character's own HealthRegen, ManaRegen and
	// EnergyShieldRegen, and an effect built at runtime with three
	// attribute-based modifiers is both harder to read and impossible to test
	// without an ability system -- whereas UCataclysmRegeneration::GainPerStep
	// and ShieldMayRefill can be checked with plain numbers.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RegenerationTimer, this,
			&ACataclysmCharacterBase::RegenerationStep,
			UCataclysmRegeneration::StepSeconds, /*bLoop=*/true,
			/*InFirstDelay=*/UCataclysmRegeneration::StepSeconds);
	}
}

void ACataclysmCharacterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RegenerationTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void ACataclysmCharacterBase::RegenerationStep()
{
	UCataclysmRegeneration::ApplyStep(this, UCataclysmRegeneration::StepSeconds,
									  SecondsSinceLastDamage());
}

void ACataclysmCharacterBase::NoteDamageTaken()
{
	const UWorld* World = GetWorld();
	LastDamagedAtSeconds = World ? World->GetTimeSeconds() : 0.0f;
}

bool ACataclysmCharacterBase::IsRegenerating() const
{
	const UWorld* World = GetWorld();
	return World
		&& World->GetTimerManager().IsTimerActive(RegenerationTimer);
}

float ACataclysmCharacterBase::SecondsSinceLastDamage() const
{
	// NEVER HURT ANSWERS WITH A LARGE NUMBER RATHER THAN WITH ZERO. Zero would
	// read as "hurt this instant" and would stop an energy shield ever filling
	// on a character nothing has touched.
	if (LastDamagedAtSeconds < 0.0f)
	{
		return TNumericLimits<float>::Max();
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return TNumericLimits<float>::Max();
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastDamagedAtSeconds);
}

UAbilitySystemComponent* ACataclysmCharacterBase::GetAbilitySystemComponent() const
{
	// Subclasses decide where the component lives.
	return nullptr;
}

void ACataclysmCharacterBase::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;
}

FGenericTeamId ACataclysmCharacterBase::GetGenericTeamId() const
{
	return TeamId;
}

void ACataclysmCharacterBase::InitAbilityActorInfo()
{
	// Subclasses supply the owner and avatar.
}
