// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmRemains.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmBoneGiants.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmDetonation.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

const TCHAR* ACataclysmRemainsMarker::NameText = TEXT("Remains");

const TCHAR* UCataclysmRemains::RemainsLeftStat = TEXT("remains_left");
const TCHAR* UCataclysmRemains::RemainsDurationStat = TEXT("remains_duration");
const TCHAR* UCataclysmRemains::RaiseReachMetresStat = TEXT("remains_raise_reach_metres");
const TCHAR* UCataclysmRemains::ReservePercentLessStat = TEXT("minion_on_remains_reserve_percent_less");
const TCHAR* UCataclysmRemains::KeptOnRaiseChanceStat = TEXT("remains_kept_on_raise_chance");
const TCHAR* UCataclysmRemains::DetonateStat = TEXT("weapon_skills_detonate_remains");
const TCHAR* UCataclysmRemains::DetonationPercentStat = TEXT("remains_detonation_percent_of_maximum_health");
const TCHAR* UCataclysmRemains::IncreasedPerAilmentStat = TEXT("remains_detonation_increased_per_ailment");
const TCHAR* UCataclysmRemains::PassAilmentsStat = TEXT("detonated_remains_pass_ailments");
const TCHAR* UCataclysmRemains::FollowerLeavesChanceStat = TEXT("minion_leaves_remains_chance");
const TCHAR* UCataclysmRemains::RaisedOnRemainsTagName = TEXT("Minion.RaisedOnRemains");
float UCataclysmRemains::PinnedRollForTests = -1.0f;

namespace
{
	/** Engine content, found by path as `ACataclysmTelegraphMarker` finds its own. Named for this file: a unity build
	 *  joins `.cpp` files, so a helper's name in an anonymous namespace must be unique to the module. */
	const TCHAR* RemainsDiscMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* RemainsDiscMaterialPath = TEXT("/Game/Effects/M_TelegraphMarker.M_TelegraphMarker");
	const TCHAR* RemainsDiscColourParameter = TEXT("Colour");
	const TCHAR* RemainsDiscOpacityParameter = TEXT("Opacity");

	/** The Cataclysm ability system of an actor, or null. */
	UCataclysmAbilitySystemComponent* RemainsAbilitySystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	/** `Actor`'s current health, or nought for an actor with no ability system. */
	float RemainsHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Actor);
		return Theirs ? Theirs->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	/** What a dead creature or follower is, read into `Remains` while its body still stands. */
	void RemainsRecordTheDead(ACataclysmRemainsMarker& Remains, AActor* Dead)
	{
		const UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Dead);
		if (Theirs)
		{
			Remains.MaximumHealth = Theirs->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		}
		if (const ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Dead))
		{
			Remains.DamagePerHit = Creature->OwnAttackDamage();
			Remains.RarityStep = Creature->RarityStep;
			Remains.Kind = Creature->ArchetypeRow;
		}
		else if (const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Dead))
		{
			Remains.DamagePerHit = Minion->OwnDamagePerHit;
			Remains.Kind = FName(*Minion->TypeName);
		}

		// EVERY AILMENT IT DIED CARRYING, of the eleven `UCataclysmAilments` names: a damage over time ailment with what
		// it dealt a second and its seconds left (`RunningAilmentOn`), and any other with the seconds its longest
		// application had left. Read now, because the body goes.
		if (!Theirs || !Dead)
		{
			return;
		}
		for (const FCataclysmAilmentKind& Kind : UCataclysmAilments::Kinds())
		{
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(Kind.TagName), /*ErrorIfNotFound=*/false);
			if (!Tag.IsValid() || !Theirs->HasMatchingGameplayTag(Tag))
			{
				continue;
			}
			FCataclysmRemainsAilment& Held = Remains.Ailments.AddDefaulted_GetRef();
			Held.Ailment = Kind.Ailment;
			Held.Tag = Tag;
			UCataclysmSkillEffects::FRunningAilment Running;
			if (UCataclysmSkillEffects::RunningAilmentOn(Dead, Tag, Running))
			{
				Held.DamagePerSecond = Running.DamagePerSecond;
				Held.bShareOfHealth = Running.bShareOfHealth;
				Held.SecondsLeft = Running.SecondsLeft;
				continue;
			}
			const TArray<float> Left = Theirs->GetActiveEffectsTimeRemaining(
				FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag)));
			for (const float Seconds : Left)
			{
				Held.SecondsLeft = FMath::Max(Held.SecondsLeft, Seconds);
			}
		}
	}

	/** Whether `Victim` was an enemy of `Listener`'s, by team, as the player's death listener judges one. */
	bool RemainsIsAnEnemysDeath(const AActor* Listener, const AActor* Victim)
	{
		return Listener && Victim && Victim != Listener
			&& UCataclysmTeams::AttitudeBetween(Listener, Victim) == ETeamAttitude::Hostile;
	}
}

// ==========================================================================
// The actor
// ==========================================================================

FLinearColor ACataclysmRemainsMarker::NameColour()
{
	return FLinearColor(0.93f, 0.89f, 0.78f, 1.0f);
}

ACataclysmRemainsMarker::ACataclysmRemainsMarker()
{
	// NOTHING TICKS AND NOTHING COLLIDES: it lies there until it is used or its lifespan ends.
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Disc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Disc"));
	Disc->SetupAttachment(RootComponent);
	Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Disc->SetCastShadow(false);
	// ONLY ITS OWNER SEES IT: Bone Fuse's "which only you see". The owner is the player it was left for.
	Disc->SetOnlyOwnerSee(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> FoundDisc(RemainsDiscMeshPath);
	if (FoundDisc.Succeeded())
	{
		DiscMesh = FoundDisc.Object;
		Disc->SetStaticMesh(DiscMesh);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FoundMaterial(RemainsDiscMaterialPath);
	if (FoundMaterial.Succeeded())
	{
		DiscMaterial = FoundMaterial.Object;
	}
	Disc->SetRelativeScale3D(FVector(DiscWidthCm / ACataclysmCharacterBase::BasicShapeSize,
									 DiscWidthCm / ACataclysmCharacterBase::BasicShapeSize,
									 DiscHeightCm / ACataclysmCharacterBase::BasicShapeSize));
}

void ACataclysmRemainsMarker::ShowDisc(float FloorBelowCm)
{
	if (!Disc)
	{
		return;
	}
	Disc->SetRelativeLocation(FVector(0.0f, 0.0f, -FloorBelowCm + DiscHeightCm * 0.5f));
	if (DiscMaterial)
	{
		if (UMaterialInstanceDynamic* Paint = Disc->CreateDynamicMaterialInstance(0, DiscMaterial))
		{
			Paint->SetVectorParameterValue(RemainsDiscColourParameter, NameColour());
			Paint->SetScalarParameterValue(RemainsDiscOpacityParameter, 1.0f);
		}
	}
}

// ==========================================================================
// Leaving remains
// ==========================================================================

FGameplayTag UCataclysmRemains::RaisedOnRemainsTag()
{
	return FGameplayTag::RequestGameplayTag(FName(RaisedOnRemainsTagName), /*ErrorIfNotFound=*/false);
}

float UCataclysmRemains::StatOf(const AActor* Player, const TCHAR* Stat)
{
	const UCataclysmAbilitySystemComponent* Theirs = RemainsAbilitySystemOf(Player);
	return Theirs ? Theirs->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f) : 0.0f;
}

float UCataclysmRemains::Roll()
{
	return PinnedRollForTests >= 0.0f ? PinnedRollForTests : FMath::FRandRange(0.0f, 100.0f);
}

ACataclysmRemainsMarker* UCataclysmRemains::LeaveFor(AActor* Player, AActor* Dead, const FVector& Where,
											   bool bEvenWithoutTheFlag)
{
	if (!IsValid(Player) || !Dead)
	{
		return nullptr;
	}
	UWorld* Into = Player->GetWorld();
	const UCataclysmAbilitySystemComponent* Theirs = RemainsAbilitySystemOf(Player);
	if (!Into || !Theirs || (!bEvenWithoutTheFlag && StatOf(Player, RemainsLeftStat) <= 0.0f))
	{
		return nullptr;
	}

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// OWNED BY THE PLAYER, which is what `SetOnlyOwnerSee` reads.
	Parameters.Owner = Player;
	ACataclysmRemainsMarker* Left = Into->SpawnActor<ACataclysmRemainsMarker>(Where, FRotator::ZeroRotator, Parameters);
	if (!Left)
	{
		return nullptr;
	}
	Left->LeftFor = Player;
	Left->LeftAtSeconds = Into->GetTimeSeconds();
	RemainsRecordTheDead(*Left, Dead);

	// ON THE FLOOR UNDER THE BODY: the death is told where the body's middle was.
	const ACharacter* Body = Cast<ACharacter>(Dead);
	const float Below = Body && Body->GetCapsuleComponent() ? Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
															: 0.0f;
	Left->ShowDisc(Below);

	// GATHERED REMAINS: "Remains last 25% longer per point". Increased, on a base of `BaseSeconds`.
	const float Lengthened = Theirs->MultiplierForStatAgainst(FName(RemainsDurationStat), FGameplayTagContainer(),
															   /*Target=*/nullptr);
	Left->SetLifeSpan(ACataclysmRemainsMarker::BaseSeconds * FMath::Max(1.0f, Lengthened));

	UE_LOG(LogCataclysm, Verbose, TEXT("REMAINS: %s left for %s, %.0f maximum health, %d ailment(s), %.1f s."),
		   *Left->Kind.ToString(), *Player->GetName(), Left->MaximumHealth, Left->Ailments.Num(), Left->GetLifeSpan());
	return Left;
}

void UCataclysmRemains::HearDeath(AActor* Player, const FCataclysmDeathNotice& Notice)
{
	if (!IsValid(Player) || !Notice.Victim)
	{
		return;
	}
	if (!RemainsIsAnEnemysDeath(Player, Notice.Victim))
	{
		return;
	}

	// THE REMAINS FIRST, so the Bone Colossus below can be raised on them or fed them as they land.
	ACataclysmRemainsMarker* Left = LeaveFor(Player, Notice.Victim, Notice.Location);
	if (Left)
	{
		UCataclysmBoneGiants::RemainsLanded(Player, Left);
	}

	// AND THE ABOMINATION'S TALLY, for a kill of this player's or of anything it commands.
	UCataclysmBoneGiants::NoteDeath(Player, Notice);
}

ACataclysmRemainsMarker* UCataclysmRemains::LeaveForAFollower(AActor* Commander, AActor* Follower, const FVector& Where)
{
	if (!IsValid(Commander) || !Follower)
	{
		return nullptr;
	}
	const ACataclysmMinion* AsMinion = Cast<ACataclysmMinion>(Follower);
	if (AsMinion && AsMinion->bIsMachine)
	{
		return nullptr;
	}
	const float Chance = StatOf(Commander, FollowerLeavesChanceStat);
	if (Chance <= 0.0f || Roll() >= Chance)
	{
		return nullptr;
	}
	ACataclysmRemainsMarker* Left = LeaveFor(Commander, Follower, Where, /*bEvenWithoutTheFlag=*/true);
	if (Left)
	{
		UCataclysmBoneGiants::RemainsLanded(Commander, Left);
	}
	return Left;
}

// ==========================================================================
// Finding them
// ==========================================================================

TArray<ACataclysmRemainsMarker*> UCataclysmRemains::OwnRemainsNear(const AActor* Player, const FVector& Place, float RadiusCm)
{
	TArray<ACataclysmRemainsMarker*> Found;
	const UWorld* World = Player ? Player->GetWorld() : nullptr;
	if (!World || RadiusCm < 0.0f)
	{
		return Found;
	}
	for (TActorIterator<ACataclysmRemainsMarker> It(World); It; ++It)
	{
		ACataclysmRemainsMarker* Remains = *It;
		if (IsValid(Remains) && Remains->LeftFor.Get() == Player
			&& FVector::Dist2D(Remains->GetActorLocation(), Place) <= RadiusCm)
		{
			Found.Add(Remains);
		}
	}
	Found.Sort([&Place](const ACataclysmRemainsMarker& A, const ACataclysmRemainsMarker& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), Place) < FVector::DistSquared2D(B.GetActorLocation(), Place);
	});
	return Found;
}

void UCataclysmRemains::DistancesOfOwnRemains(const AActor* Player, float WithinCm, TArray<float>& OutMetres)
{
	OutMetres.Reset();
	if (!Player)
	{
		return;
	}
	const FVector From = Player->GetActorLocation();
	for (const ACataclysmRemainsMarker* Remains : OwnRemainsNear(Player, From, WithinCm))
	{
		OutMetres.Add(FVector::Dist2D(Remains->GetActorLocation(), From) / 100.0f);
	}
}

void UCataclysmRemains::RemainsToName(const AActor* Viewer, const FVector& Standing, float Range,
									  TArray<ACataclysmRemainsMarker*>& Out)
{
	Out = OwnRemainsNear(Viewer, Standing, Range);
}

// ==========================================================================
// Raising on them
// ==========================================================================

ACataclysmRemainsMarker* UCataclysmRemains::ChooseToRaiseOn(const AActor* Summoner, const FVector& RisePoint)
{
	const float Metres = StatOf(Summoner, RaiseReachMetresStat);
	if (Metres <= 0.0f)
	{
		return nullptr;
	}
	const TArray<ACataclysmRemainsMarker*> Near = OwnRemainsNear(Summoner, RisePoint, Metres * 100.0f);
	return Near.IsEmpty() ? nullptr : Near[0];
}

float UCataclysmRemains::ReserveOnRemains(const AActor* Summoner, float Reserve)
{
	const float Less = FMath::Clamp(StatOf(Summoner, ReservePercentLessStat), 0.0f, 100.0f);
	return FMath::Max(0.0f, Reserve * (1.0f - Less / 100.0f));
}

bool UCataclysmRemains::RaiseOn(AActor* Summoner, ACataclysmMinion* Minion, ACataclysmRemainsMarker* Remains)
{
	if (!IsValid(Summoner) || !IsValid(Minion) || !IsValid(Remains) || Remains->LeftFor.Get() != Summoner)
	{
		return false;
	}

	// ITS TAG, AND ITS HEALTH TAKEN AGAIN WITH IT, so Raised on Bones' `minion_health` rows (which require the tag)
	// reach the health `ACataclysmMinion::Spawn` set once, at the summoning, without it. The share is the ratio of the
	// two multipliers, so every other row is counted once.
	const FGameplayTag Tag = RaisedOnRemainsTag();
	if (Tag.IsValid() && !Minion->TypeTags.HasTagExact(Tag))
	{
		const float Without = UCataclysmCommand::SummonerMultiplierFor(Summoner, TEXT("minion_health"), Minion->TypeTags);
		Minion->TypeTags.AddTag(Tag);
		const float With = UCataclysmCommand::SummonerMultiplierFor(Summoner, TEXT("minion_health"), Minion->TypeTags);
		UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Minion);
		if (Its && Without > 0.0f && With > 0.0f && !FMath::IsNearlyEqual(With, Without))
		{
			const float Share = With / Without;
			const float Maximum = Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) * Share;
			const float Current = Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) * Share;
			// MAXIMUM FIRST, for the reason `ACataclysmMinion::Spawn` gives.
			Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Maximum);
			Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Current);
		}
	}

	// THE EVENT, naming the minion: Bone Fuse's 3 Fervour and Grave Robber's 5 are rows on it.
	if (UCataclysmAbilitySystemComponent* Theirs = RemainsAbilitySystemOf(Summoner))
	{
		Theirs->ActOnEvent(FName(TEXT("minion_raised_on_remains")), &Minion->TypeTags, 0.0f, true, Minion);
	}

	// TWO FROM ONE: "a 4% chance per point not to consume them".
	const float Kept = StatOf(Summoner, KeptOnRaiseChanceStat);
	const bool bKept = Kept > 0.0f && Roll() < Kept;
	UE_LOG(LogCataclysm, Log, TEXT("RAISED ON REMAINS: %s rose on the remains of %s for %s; the remains were %s."),
		   *Minion->GetName(), *Remains->Kind.ToString(), *Summoner->GetName(), bKept ? TEXT("kept") : TEXT("consumed"));
	if (bKept)
	{
		return false;
	}
	Consume(Summoner, Remains);
	return true;
}

void UCataclysmRemains::Consume(AActor* Player, ACataclysmRemainsMarker* Remains)
{
	if (!IsValid(Remains))
	{
		return;
	}
	Remains->Destroy();
	// PICKED CLEAN: "Consuming remains, by raising a minion on them or by detonating them, restores 1% of your maximum
	// health for every 3 points." Rows on this event.
	if (UCataclysmAbilitySystemComponent* Theirs = RemainsAbilitySystemOf(Player))
	{
		Theirs->ActOnEvent(FName(TEXT("remains_consumed")));
	}
}

// ==========================================================================
// Detonating them
// ==========================================================================

int32 UCataclysmRemains::DetonateWhere(AActor* Caster, TFunctionRef<bool(const AActor&)> IsInside)
{
	if (!IsValid(Caster) || StatOf(Caster, DetonateStat) <= 0.0f)
	{
		return 0;
	}
	const UWorld* World = Caster->GetWorld();
	if (!World)
	{
		return 0;
	}
	TArray<TWeakObjectPtr<ACataclysmRemainsMarker>> Caught;
	for (TActorIterator<ACataclysmRemainsMarker> It(World); It; ++It)
	{
		ACataclysmRemainsMarker* Remains = *It;
		if (IsValid(Remains) && Remains->LeftFor.Get() == Caster && IsInside(*Remains))
		{
			Caught.Add(Remains);
		}
	}
	int32 Detonated = 0;
	for (const TWeakObjectPtr<ACataclysmRemainsMarker>& One : Caught)
	{
		if (ACataclysmRemainsMarker* Remains = One.Get(); IsValid(Remains))
		{
			Detonate(Caster, Remains);
			++Detonated;
		}
	}
	if (!Caught.IsEmpty())
	{
		UE_LOG(LogCataclysm, Log, TEXT("REMAINS DETONATED: %s detonated %d remains within a skill's reach."),
			   *Caster->GetName(), Detonated);
	}
	return Detonated;
}

float UCataclysmRemains::DetonationDamage(const AActor* Caster, const ACataclysmRemainsMarker* Remains)
{
	if (!Remains)
	{
		return 0.0f;
	}
	const float Percent = FMath::Max(0.0f, StatOf(Caster, DetonationPercentStat));
	const float PerAilment = FMath::Max(0.0f, StatOf(Caster, IncreasedPerAilmentStat));
	const float Increased = PerAilment * static_cast<float>(Remains->Ailments.Num());
	return Remains->MaximumHealth * Percent / 100.0f * (1.0f + Increased / 100.0f);
}

int32 UCataclysmRemains::Detonate(AActor* Caster, ACataclysmRemainsMarker* Remains)
{
	if (!IsValid(Caster) || !IsValid(Remains) || Remains->LeftFor.Get() != Caster)
	{
		return 0;
	}
	UWorld* World = Caster->GetWorld();
	const FVector At = Remains->GetActorLocation();

	// WHO IS STRUCK, read before the blast, by the search every skill uses: the blast's own search finds the same.
	const float Damage = DetonationDamage(Caster, Remains);
	TArray<AActor*> Struck;
	// AND EACH ONE'S HEALTH BEFORE IT, so the event below carries what the blast took from it, as `minion_hit` carries
	// `DealtToHealth`: a row answering it with an ailment is then judged on the blow's real size. Window dfb-w1 found the
	// event raised with nought, which no ailment-carrying row could ever pass.
	TArray<float> HealthBefore;
	int32 Sent = 0;
	if (World && Damage > 0.0f)
	{
		Struck = UCataclysmTargeting::FindEnemiesInSphere(World, Caster, At, DetonationRadiusCm);
		for (const AActor* Enemy : Struck)
		{
			HealthBefore.Add(RemainsHealthOf(Enemy));
		}
		// AS ONE BLOW OF THE CASTER'S, sized by `Damage` at 100 per cent, unscaled by its increases: The Ferryman's
		// burst's shape, a detonation sized by the dead creature's health.
		Sent = UCataclysmSkillEffects::ExplodeForOverkill(Caster, /*Victim=*/nullptr, At, Damage, 100.0f,
														  DetonationRadiusCm);
	}

	UCataclysmAbilitySystemComponent* Theirs = RemainsAbilitySystemOf(Caster);
	const bool bPasses = StatOf(Caster, PassAilmentsStat) > 0.0f;
	for (int32 Index = 0; Index < Struck.Num(); ++Index)
	{
		AActor* Enemy = Struck[Index];
		if (!IsValid(Enemy) || UCataclysmSkillEffects::IsDead(Enemy))
		{
			continue;
		}
		// GRAVE SPORES: "a 6% chance per point to inflict Necrosis on each enemy they hit". A row on this event, carrying
		// the health the blast took from this enemy.
		if (Theirs)
		{
			const float Dealt = FMath::Max(0.0f, HealthBefore[Index] - RemainsHealthOf(Enemy));
			Theirs->ActOnEvent(FName(TEXT("remains_detonation_hit")), nullptr, Dealt, true, Enemy);
		}
		// PLAGUE PIT: "pass every ailment their creature died with, for the time it had left, to each enemy the blast
		// hits". A damage over time ailment as a spread copy, the caster its applier; any other through the ailment's
		// own application, with its seconds left where that application takes seconds (Cripple's).
		if (bPasses)
		{
			for (const FCataclysmRemainsAilment& Held : Remains->Ailments)
			{
				if (Held.SecondsLeft <= 0.0f)
				{
					continue;
				}
				if (Held.DamagePerSecond > 0.0f)
				{
					UCataclysmSkillEffects::FRunningAilment Copy;
					Copy.Ailment = Held.Tag;
					Copy.DamagePerSecond = Held.DamagePerSecond;
					Copy.bShareOfHealth = Held.bShareOfHealth;
					Copy.SecondsLeft = Held.SecondsLeft;
					Copy.Applier = Caster;
					UCataclysmSkillEffects::ApplySpreadCopy(Caster, Enemy, Copy);
				}
				else if (const FCataclysmAilmentKind* Kind = UCataclysmAilments::KindNamed(Held.Ailment))
				{
					UCataclysmAilments::Apply(Caster, Enemy, *Kind, UCataclysmAilments::NormalMagnitude,
											  /*Skill=*/nullptr, NAME_None, Held.SecondsLeft);
				}
			}
		}
	}

	// AND BURSTING RANKS COUNTS IT: "for each detonation of yours in the last 2 seconds, of a minion or of remains".
	// Slice C's `NoteDetonation`, wired at the bundle's assembly (2026-11-26): the caster's `minion_detonated` event, with
	// no tags, as a remains has none.
	UCataclysmDetonation::NoteDetonation(Caster);

	UE_LOG(LogCataclysm, Log, TEXT("REMAINS DETONATION: %s detonated the remains of %s for %.1f, %d enemy(ies) struck."),
		   *Caster->GetName(), *Remains->Kind.ToString(), Damage, Sent);
	Consume(Caster, Remains);
	return Sent;
}
