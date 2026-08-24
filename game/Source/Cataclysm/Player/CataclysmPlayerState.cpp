// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPlayerState.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Net/UnrealNetwork.h"

ACataclysmPlayerState::ACataclysmPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Mixed replication: the owning client receives full gameplay effect data,
	// while other clients see only the resulting tags and cues. Full is wasteful
	// for a player-controlled actor and Minimal loses information the owner's
	// own interface needs.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Created as default subobjects rather than granted at runtime, because a
	// player has all five for its whole lifetime. An attribute set added later
	// does not retroactively replicate to clients that already have the
	// component, so the ones every player always carries are created here.
	VitalAttributes = CreateDefaultSubobject<UCataclysmVitalAttributeSet>(TEXT("VitalAttributes"));
	PrimaryAttributes = CreateDefaultSubobject<UCataclysmPrimaryAttributeSet>(TEXT("PrimaryAttributes"));
	CombatAttributes = CreateDefaultSubobject<UCataclysmCombatAttributeSet>(TEXT("CombatAttributes"));
	ResistanceAttributes = CreateDefaultSubobject<UCataclysmResistanceAttributeSet>(TEXT("ResistanceAttributes"));
	ClassResourceAttributes = CreateDefaultSubobject<UCataclysmClassResourceAttributeSet>(TEXT("ClassResourceAttributes"));

	// APlayerState replicates at 1 Hz by default, which is fine for a score but
	// far too slow for health bars and cooldowns driven off attributes.
	SetNetUpdateFrequency(100.0f);
}

UAbilitySystemComponent* ACataclysmPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACataclysmPlayerState::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACataclysmPlayerState, SpentAttributePoints);
}

int32 ACataclysmPlayerState::AttributePointsAvailable() const
{
	// THE LEVEL AND NOTHING ELSE. `docs/Cataclysm_GDD_v2.md` names two sources,
	// "1 attribute point per level" and the Maw, which "consumes items and
	// enemies for Attribute points". The Maw does not exist, so a term for it
	// here would be a number nobody chose. Issue #50.
	return UCataclysmPlayerClassStats::ChosenLevel();
}

int32 ACataclysmPlayerState::AttributePointsUnspent() const
{
	return AttributePointsAvailable() - SpentAttributePoints.Total();
}

bool ACataclysmPlayerState::SpendAttributePoints(const FString& Attribute,
												 int32 Count, FString& OutReason)
{
	if (Count <= 0)
	{
		OutReason = FString::Printf(
			TEXT("%d is not a number of points to spend. Cataclysm."
				 "ResetAttributePoints is how they are taken back."), Count);
		return false;
	}

	// THE NAME IS CHECKED BEFORE THE COUNT, so a mistyped attribute never reads
	// as "you do not have enough points", which would send somebody looking for
	// a problem they do not have.
	FCataclysmAttributePoints Wanted = SpentAttributePoints;
	if (!Wanted.AddTo(Attribute, Count))
	{
		OutReason = FString::Printf(
			TEXT("%s is not one of the eight attributes. They are %s."),
			*Attribute,
			*FString::Join(FCataclysmAttributePoints::Names(), TEXT(", ")));
		return false;
	}

	const int32 Unspent = AttributePointsUnspent();
	if (Count > Unspent)
	{
		// REFUSED WHOLE RATHER THAN PARTLY SPENT. Spending three of a requested
		// forty and reporting success is the shape of failure somebody notices
		// an hour later, when a character sheet does not match what they typed.
		OutReason = FString::Printf(
			TEXT("that would spend %d points and only %d are unspent. A "
				 "character has one for every level and is level %d."),
			Count, Unspent, AttributePointsAvailable());
		return false;
	}

	SpentAttributePoints = Wanted;
	return true;
}

void ACataclysmPlayerState::ResetAttributePoints()
{
	SpentAttributePoints = FCataclysmAttributePoints();
}
