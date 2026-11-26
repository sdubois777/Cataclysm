// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmBoneGiants.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRemains.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"

const TCHAR* UCataclysmBoneGiants::AbominationRisesStat = TEXT("abomination_rises");
const TCHAR* UCataclysmBoneGiants::AbominationKillPercentLessStat = TEXT("abomination_kill_percent_less");
const TCHAR* UCataclysmBoneGiants::BoneColossusStat = TEXT("bone_colossus");
const TCHAR* UCataclysmBoneGiants::LoneRevenantHeldStat = TEXT("lone_revenant_held");
const TCHAR* UCataclysmBoneGiants::GiantTypeName = TEXT("BarrowGuard");

namespace
{
	/** Named for this file, for the unity build's reason `CataclysmRemains.cpp` gives. */
	UAbilitySystemComponent* BoneGiantsAbilitySystemOf(const AActor* Actor)
	{
		return UCataclysmTargeting::AbilitySystemOf(Actor);
	}

	float BoneGiantsMaximumHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* Its = BoneGiantsAbilitySystemOf(Actor);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : 0.0f;
	}

	float BoneGiantsHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* Its = BoneGiantsAbilitySystemOf(Actor);
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	/** Write a giant's maximum and current health, the maximum first for the reason `ACataclysmMinion::Spawn` gives. */
	void BoneGiantsWriteHealth(ACataclysmMinion* Giant, float Maximum, float Current)
	{
		UAbilitySystemComponent* Its = BoneGiantsAbilitySystemOf(Giant);
		if (!Its || Maximum <= 0.0f)
		{
			return;
		}
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Maximum);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(),
									 FMath::Clamp(Current, 0.0f, Maximum));
	}

	/** A dead creature's blow: a creature's attack damage, a minion's damage a hit. */
	float BoneGiantsBlowOf(const AActor* Dead)
	{
		if (const ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Dead))
		{
			return Creature->OwnAttackDamage();
		}
		if (const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Dead))
		{
			return Minion->OwnDamagePerHit;
		}
		return 0.0f;
	}

	bool BoneGiantsIsAlive(const ACataclysmMinion* Giant)
	{
		return IsValid(Giant) && !UCataclysmSkillEffects::IsDead(Giant);
	}

	/** Lone Revenant's "400% more", as a multiplier: five with the flag, one without. */
	float BoneGiantsLoneRevenantMultiplier(const AActor* Player)
	{
		return UCataclysmRemains::StatOf(Player, UCataclysmBoneGiants::LoneRevenantHeldStat) > 0.0f
			? 1.0f + UCataclysmBoneGiants::LoneRevenantMorePercent / 100.0f
			: 1.0f;
	}
}

UCataclysmBoneGiants* UCataclysmBoneGiants::In(const UWorld* World)
{
	return World ? World->GetSubsystem<UCataclysmBoneGiants>() : nullptr;
}

FCataclysmGiantsOfAPlayer& UCataclysmBoneGiants::TallyOf(const AActor* Player)
{
	return Players.FindOrAdd(TWeakObjectPtr<const AActor>(Player));
}

const FCataclysmGiantsOfAPlayer* UCataclysmBoneGiants::FindTallyOf(const AActor* Player) const
{
	return Players.Find(TWeakObjectPtr<const AActor>(Player));
}

// ==========================================================================
// The floor's tally
// ==========================================================================

void UCataclysmBoneGiants::BeginFloor(UWorld* World)
{
	if (World)
	{
		EndTheFloor(*World);
	}
}

void UCataclysmBoneGiants::NoteStartedWith(AActor* Creature)
{
	UCataclysmBoneGiants* Giants = Creature ? In(Creature->GetWorld()) : nullptr;
	if (!Giants || Giants->StartedWith.Contains(Creature))
	{
		return;
	}
	Giants->StartedWith.Add(Creature);
	++Giants->StartedCount;
}

int32 UCataclysmBoneGiants::StartedWithCount(const UWorld* World)
{
	const UCataclysmBoneGiants* Giants = In(World);
	return Giants ? Giants->StartedCount : 0;
}

bool UCataclysmBoneGiants::IsAGiant(const ACataclysmMinion* Minion)
{
	if (!IsValid(Minion))
	{
		return false;
	}
	const UCataclysmBoneGiants* Giants = In(Minion->GetWorld());
	const FCataclysmGiantsOfAPlayer* Tally = Giants ? Giants->FindTallyOf(Minion->Summoner.Get()) : nullptr;
	return Tally && (Tally->Abomination.Get() == Minion || Tally->Colossus.Get() == Minion);
}

int32 UCataclysmBoneGiants::KilledOfStartBy(const AActor* Player)
{
	const UCataclysmBoneGiants* Giants = Player ? In(Player->GetWorld()) : nullptr;
	const FCataclysmGiantsOfAPlayer* Tally = Giants ? Giants->FindTallyOf(Player) : nullptr;
	return Tally ? Tally->KilledOfStart : 0;
}

int32 UCataclysmBoneGiants::KillsNeededFor(const AActor* Player)
{
	const int32 Started = Player ? StartedWithCount(Player->GetWorld()) : 0;
	if (Started <= 0)
	{
		return MAX_int32;
	}
	// MANY MADE ONE: "each point takes 2 off the percentage ... from 50% down to 34% at 8 points". Rounded up, so "at
	// least 50%" of 7 is 4.
	const float Percent = FMath::Clamp(
		AbominationKillPercent - UCataclysmRemains::StatOf(Player, AbominationKillPercentLessStat), 0.0f, 100.0f);
	return FMath::Max(1, FMath::CeilToInt(static_cast<float>(Started) * Percent / 100.0f - KINDA_SMALL_NUMBER));
}

void UCataclysmBoneGiants::NoteDeath(AActor* Player, const FCataclysmDeathNotice& Notice)
{
	UCataclysmBoneGiants* Giants = Player ? In(Player->GetWorld()) : nullptr;
	if (!Giants || !Notice.Victim)
	{
		return;
	}
	// A KILL OF THE PLAYER'S OR OF ANYTHING IT COMMANDS, the Abomination's own included.
	const bool bTheirs = Notice.Killer
		&& (Notice.Killer == Player || UCataclysmCommand::CommanderOf(Notice.Killer) == Player);
	if (!bTheirs)
	{
		return;
	}

	FCataclysmGiantsOfAPlayer& Tally = Giants->TallyOf(Player);
	const float Health = BoneGiantsMaximumHealthOf(Notice.Victim);
	const float Blow = BoneGiantsBlowOf(Notice.Victim);
	Tally.SummedHealth += Health;
	Tally.SummedDamage += Blow;
	if (Giants->StartedWith.Contains(Notice.Victim))
	{
		++Tally.KilledOfStart;
	}

	// GROWN BY EACH KILL AFTER IT ROSE, by the same shares, Lone Revenant's included.
	if (ACataclysmMinion* Standing = Tally.Abomination.Get(); BoneGiantsIsAlive(Standing))
	{
		const float Revenant = BoneGiantsLoneRevenantMultiplier(Player);
		const float HealthRows = UCataclysmCommand::SummonerMultiplierFor(Player, TEXT("minion_health"),
																		  Standing->TypeTags);
		const float Added = Health * AbominationHealthSharePercent / 100.0f * FMath::Max(0.0f, HealthRows) * Revenant;
		BoneGiantsWriteHealth(Standing, BoneGiantsMaximumHealthOf(Standing) + Added, BoneGiantsHealthOf(Standing) + Added);
		Standing->OwnDamagePerHit += Blow * AbominationDamageSharePercent / 100.0f * Revenant;
		return;
	}

	if (!Tally.bAbominationRose && UCataclysmRemains::StatOf(Player, AbominationRisesStat) > 0.0f
		&& Tally.KilledOfStart >= KillsNeededFor(Player))
	{
		RaiseAbomination(Player, Notice.Location);
	}
}

// ==========================================================================
// The Abomination
// ==========================================================================

ACataclysmMinion* UCataclysmBoneGiants::RaiseAbomination(AActor* Player, const FVector& Where)
{
	UCataclysmBoneGiants* Giants = Player ? In(Player->GetWorld()) : nullptr;
	const UCataclysmAbilitySystemComponent* Pool =
		Cast<UCataclysmAbilitySystemComponent>(BoneGiantsAbilitySystemOf(Player));
	if (!Giants || !Pool)
	{
		return nullptr;
	}
	FCataclysmGiantsOfAPlayer& Tally = Giants->TallyOf(Player);

	// ROOM, MADE BY RELEASING THE OLDEST, but only when it can be made: what stays (the machines) and the 100 must fit.
	if (!UCataclysmCommand::HasRoomToReserve(Player, AbominationReserve))
	{
		const TArray<ACataclysmCharacterBase*> Releasable = UCataclysmCommand::ReleasableOldestFirst(Player);
		float Staying = 0.0f;
		for (AActor* Follower : UCataclysmCommand::ThingsCommandedBy(Player))
		{
			const ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Follower);
			if (Body && !Releasable.Contains(Body))
			{
				Staying += Body->ReservedFervour;
			}
		}
		if (Staying + AbominationReserve > Pool->MaximumClassResource())
		{
			UE_LOG(LogCataclysm, Log, TEXT("ABOMINATION REFUSED: %s's pool of %.0f cannot hold its 100 Fervour."),
				   *Player->GetName(), Pool->MaximumClassResource());
			return nullptr;
		}
		for (ACataclysmCharacterBase* Oldest : Releasable)
		{
			if (UCataclysmCommand::HasRoomToReserve(Player, AbominationReserve))
			{
				break;
			}
			if (IsValid(Oldest))
			{
				UE_LOG(LogCataclysm, Log, TEXT("RELEASED: '%s' released '%s', which held %.0f Fervour back, to raise its "
											   "Abomination."),
					   *Player->GetName(), *Oldest->GetName(), Oldest->ReservedFervour);
				Oldest->Destroy();
			}
		}
	}

	ACataclysmMinion* Abomination = ACataclysmMinion::Spawn(Player, Where, /*Lifetime=*/60.0f, /*bBurns=*/false,
															 GiantTypeName);
	if (!Abomination)
	{
		return nullptr;
	}
	// IT STANDS UNTIL THE FLOOR IS LEFT: the engine's lifespan of nought never expires, as a permanent raise's.
	Abomination->SetLifeSpan(0.0f);
	Abomination->SetActorScale3D(FVector(AbominationDrawnScale));

	const float Revenant = BoneGiantsLoneRevenantMultiplier(Player);
	const float HealthRows = UCataclysmCommand::SummonerMultiplierFor(Player, TEXT("minion_health"),
																	  Abomination->TypeTags);
	const float Health = Tally.SummedHealth * AbominationHealthSharePercent / 100.0f * FMath::Max(0.0f, HealthRows)
		* Revenant;
	BoneGiantsWriteHealth(Abomination, Health, Health);
	Abomination->OwnDamagePerHit = Tally.SummedDamage * AbominationDamageSharePercent / 100.0f * Revenant;

	UCataclysmCommand::HoldReserve(Player, Abomination, AbominationReserve);
	Tally.Abomination = Abomination;
	Tally.bAbominationRose = true;
	UE_LOG(LogCataclysm, Log,
		   TEXT("ABOMINATION: %s's Abomination rose after %d of %d starting creatures, %.0f health, %.1f a blow%s."),
		   *Player->GetName(), Tally.KilledOfStart, Giants->StartedCount, Health, Abomination->OwnDamagePerHit,
		   Revenant > 1.0f ? TEXT(", with Lone Revenant") : TEXT(""));
	return Abomination;
}

ACataclysmMinion* UCataclysmBoneGiants::AbominationOf(const AActor* Player)
{
	const UCataclysmBoneGiants* Giants = Player ? In(Player->GetWorld()) : nullptr;
	const FCataclysmGiantsOfAPlayer* Tally = Giants ? Giants->FindTallyOf(Player) : nullptr;
	ACataclysmMinion* Standing = Tally ? Tally->Abomination.Get() : nullptr;
	return BoneGiantsIsAlive(Standing) ? Standing : nullptr;
}

// ==========================================================================
// The Bone Colossus
// ==========================================================================

ACataclysmMinion* UCataclysmBoneGiants::ColossusOf(const AActor* Player)
{
	const UCataclysmBoneGiants* Giants = Player ? In(Player->GetWorld()) : nullptr;
	const FCataclysmGiantsOfAPlayer* Tally = Giants ? Giants->FindTallyOf(Player) : nullptr;
	ACataclysmMinion* Standing = Tally ? Tally->Colossus.Get() : nullptr;
	return BoneGiantsIsAlive(Standing) ? Standing : nullptr;
}

void UCataclysmBoneGiants::RemainsLanded(AActor* Player, ACataclysmRemainsMarker* Remains)
{
	UCataclysmBoneGiants* Giants = Player ? In(Player->GetWorld()) : nullptr;
	if (!Giants || !IsValid(Remains) || Remains->LeftFor.Get() != Player
		|| UCataclysmRemains::StatOf(Player, BoneColossusStat) <= 0.0f)
	{
		return;
	}
	FCataclysmGiantsOfAPlayer& Tally = Giants->TallyOf(Player);
	ACataclysmMinion* Colossus = ColossusOf(Player);

	// FED: remains landing within reach of it.
	if (Colossus)
	{
		if (FVector::Dist2D(Colossus->GetActorLocation(), Remains->GetActorLocation()) > ColossusFeedReachCm)
		{
			return;
		}
		const float Added = Remains->MaximumHealth * ColossusFedHealthSharePercent / 100.0f;
		BoneGiantsWriteHealth(Colossus, BoneGiantsMaximumHealthOf(Colossus) + Added, BoneGiantsHealthOf(Colossus) + Added);
		UE_LOG(LogCataclysm, Log, TEXT("BONE COLOSSUS FED: %s's Colossus took the remains of %s, +%.0f health."),
			   *Player->GetName(), *Remains->Kind.ToString(), Added);
		Remains->Destroy();
		StepColossusOf(Player);
		return;
	}

	// OR RAISED ON THEM, the first remains of a floor or the first after one was killed, when its 20 fit.
	if (!UCataclysmCommand::HasRoomToReserve(Player, ColossusReserve))
	{
		return;
	}
	ACataclysmMinion* Risen = ACataclysmMinion::Spawn(Player, Remains->GetActorLocation(), /*Lifetime=*/60.0f,
													   /*bBurns=*/false, GiantTypeName);
	if (!Risen)
	{
		return;
	}
	Risen->SetLifeSpan(0.0f);
	UCataclysmCommand::HoldReserve(Player, Risen, ColossusReserve);
	Tally.Colossus = Risen;
	Tally.ColossusStartHealth = BoneGiantsMaximumHealthOf(Risen);
	Tally.ColossusStartDamage = Risen->OwnDamagePerHit;
	UE_LOG(LogCataclysm, Log, TEXT("BONE COLOSSUS: %s's Colossus rose on the remains of %s, %.0f health."),
		   *Player->GetName(), *Remains->Kind.ToString(), Tally.ColossusStartHealth);
	Remains->Destroy();
	Giants->StartColossusStep();
}

void UCataclysmBoneGiants::StepColossusOf(const AActor* Player)
{
	const UCataclysmBoneGiants* Giants = Player ? In(Player->GetWorld()) : nullptr;
	const FCataclysmGiantsOfAPlayer* Tally = Giants ? Giants->FindTallyOf(Player) : nullptr;
	ACataclysmMinion* Colossus = ColossusOf(Player);
	if (!Tally || !Colossus || Tally->ColossusStartHealth <= 0.0f)
	{
		return;
	}
	// ITS MASS IS ITS HEALTH: its blow follows it, and its size follows the cube root of it, as a body's volume.
	const float Mass = FMath::Max(0.0f, BoneGiantsHealthOf(Colossus) / Tally->ColossusStartHealth);
	Colossus->OwnDamagePerHit = Tally->ColossusStartDamage * Mass;
	Colossus->SetActorScale3D(FVector(FMath::Clamp(FMath::Pow(Mass, 1.0f / 3.0f), ColossusSmallestScale,
													ColossusLargestScale)));
}

void UCataclysmBoneGiants::StartColossusStep()
{
	UWorld* World = GetWorld();
	if (!World || World->GetTimerManager().IsTimerActive(ColossusStepTimer))
	{
		return;
	}
	World->GetTimerManager().SetTimer(ColossusStepTimer,
									  FTimerDelegate::CreateUObject(this, &UCataclysmBoneGiants::StepEveryColossus),
									  ColossusStepSeconds, /*bLoop=*/true);
}

void UCataclysmBoneGiants::StepEveryColossus()
{
	TArray<const AActor*> Stepping;
	for (const TPair<TWeakObjectPtr<const AActor>, FCataclysmGiantsOfAPlayer>& Each : Players)
	{
		if (const AActor* Player = Each.Key.Get(); Player && BoneGiantsIsAlive(Each.Value.Colossus.Get()))
		{
			Stepping.Add(Player);
		}
	}
	for (const AActor* Player : Stepping)
	{
		StepColossusOf(Player);
	}
	if (Stepping.IsEmpty())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ColossusStepTimer);
		}
	}
}

// ==========================================================================
// The floor left
// ==========================================================================

int32 UCataclysmBoneGiants::EndTheFloor(UWorld& World)
{
	UCataclysmBoneGiants* Giants = In(&World);
	if (!Giants)
	{
		return 0;
	}
	int32 Ended = 0;
	for (TPair<TWeakObjectPtr<const AActor>, FCataclysmGiantsOfAPlayer>& Each : Giants->Players)
	{
		for (ACataclysmMinion* Giant : {Each.Value.Abomination.Get(), Each.Value.Colossus.Get()})
		{
			if (IsValid(Giant))
			{
				Giant->Destroy();
				++Ended;
			}
		}
	}
	Giants->Players.Reset();
	Giants->StartedWith.Reset();
	Giants->StartedCount = 0;
	World.GetTimerManager().ClearTimer(Giants->ColossusStepTimer);
	return Ended;
}
