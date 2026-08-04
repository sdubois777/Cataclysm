// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Engine/World.h"
#include "TimerManager.h"

ACataclysmMinion::ACataclysmMinion()
{
	// It attacks on a timer, a second apart. A tick would ask sixty times as
	// often for the same answer.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(
		TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	VitalAttributes = CreateDefaultSubobject<UCataclysmVitalAttributeSet>(
		TEXT("VitalAttributes"));
}

UAbilitySystemComponent* ACataclysmMinion::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACataclysmMinion::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;
}

FGenericTeamId ACataclysmMinion::GetGenericTeamId() const
{
	return TeamId;
}

void ACataclysmMinion::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AttackTimer, this, &ACataclysmMinion::AttackOnce,
			AttackIntervalSeconds, /*bLoop=*/true,
			/*InFirstDelay=*/AttackIntervalSeconds);
	}
}

ACataclysmMinion* ACataclysmMinion::Spawn(AActor* InSummoner, const FVector& Location,
										  float Lifetime, bool bBurns)
{
	if (!IsValid(InSummoner) || Lifetime <= 0.0f)
	{
		return nullptr;
	}

	UWorld* World = InSummoner->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	// OWNED BY THE SUMMONER, and that is load-bearing rather than tidiness.
	// UCataclysmTeams::TeamOf follows the owner chain, so ownership is what
	// keeps a summon on its summoner's side on a client, where the team assigned
	// below is a server-side value that is not itself replicated.
	SpawnParams.Owner = InSummoner;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACataclysmMinion* Minion = World->SpawnActor<ACataclysmMinion>(
		ACataclysmMinion::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (!Minion)
	{
		return nullptr;
	}

	Minion->Summoner = InSummoner;
	Minion->bBurnsWhatItHits = bBurns;

	// The summoner's side, not one of its own. A Ritualist's imps must be
	// friendly to a second player in the party, not merely to the Ritualist,
	// and ownership alone cannot say that.
	Minion->SetGenericTeamId(UCataclysmTeams::TeamOf(InSummoner));

	Minion->SetLifeSpan(Lifetime);

	return Minion;
}

void ACataclysmMinion::AttackOnce()
{
	if (!IsValid(Summoner))
	{
		return;
	}

	// Nearest first, one target. It does not choose; it hits what is closest,
	// which is what "attacks the nearest enemy" means with no AI to do better.
	const TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
		GetWorld(), this, GetActorLocation(), ReachCm, /*MaxTargets=*/1);
	if (Nearby.IsEmpty())
	{
		return;
	}

	// Damage comes from the SUMMONER's weapon, not the minion's own, which it
	// has none of. So a Ritualist's imps get stronger as the Ritualist does,
	// which is how every minion in the genre scales.
	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Summoner, Nearby[0], DamagePercentOfSummoner);

	if (bBurnsWhatItHits && Dealt > 0.0f)
	{
		UCataclysmSkillEffects::ApplyBurn(Summoner, Nearby[0], Dealt);
	}

	++AttacksMade;
}

void ACataclysmMinion::Explode(float RadiusCm, float DamagePercent)
{
	if (IsValid(Summoner) && RadiusCm > 0.0f && DamagePercent > 0.0f)
	{
		const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), this, GetActorLocation(), RadiusCm);

		for (AActor* Target : Caught)
		{
			const float Dealt = UCataclysmSkillEffects::ApplyHit(
				Summoner, Target, DamagePercent);
			if (bBurnsWhatItHits && Dealt > 0.0f)
			{
				UCataclysmSkillEffects::ApplyBurn(Summoner, Target, Dealt);
			}
		}
	}

	Destroy();
}
