// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Half-height and radius of the imp's collision capsule, in centimetres. */
	constexpr float MinionCapsuleRadius = 30.0f;
	constexpr float MinionCapsuleHalfHeight = 45.0f;

	/**
	 * How a minion's blow arrives, for either of the two ways it deals damage.
	 *
	 * A MINION NEVER CRITICALLY STRIKES, and saying so at the call site is the
	 * only way to get that right. Its damage is dealt in its summoner's name --
	 * the two ApplyHit calls below pass `Summoner` as the attacker -- so the
	 * character whose critical strike chance the engine reads is the player.
	 * The design forbids the inheritance: "A minion does not take the summoner's
	 * weapon damage, flat added damage, attack speed, critical strike chance or
	 * multiplier, penetration" (docs/Cataclysm_GDD_v2.md:1747), and minion damage
	 * was fitted at the top of its band precisely because a minion "has no
	 * critical strike layer to compound with" (:1776).
	 *
	 * AND IT PENETRATES NOTHING, which is the other half of that same sentence.
	 * Both penetration stats are read off the attacker in the same place the
	 * critical strike chance is, and a piercing weapon adds a third share of
	 * armour ignored on top, so an imp was cutting into a target's armour and
	 * resistance by whatever its summoner's gear supplied. Issue #659. A minion
	 * has nothing of its own to put in its place -- no type in
	 * `game/Data/MinionTypes.csv` states penetration and a minion carries no
	 * combat attribute set -- so it penetrates zero.
	 */
	FCataclysmHitDelivery MinionDelivery(bool bIsArea)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = bIsArea;
		Delivery.bCannotCriticallyStrike = true;
		Delivery.bCannotPenetrate = true;
		return Delivery;
	}

	/** How fast it walks, in centimetres per second, when its type states
	 *  nothing. Faster than a monster, because Summon Imp is written as "fast
	 *  swarming melee". A type that states a move speed overrides it. */
	constexpr float MinionWalkSpeedCmPerSecond = 500.0f;

	/** Where the imported minion type table lives. */
	const TCHAR* MinionTypeTablePath = TEXT("/Game/Data/DT_MinionTypes.DT_MinionTypes");
}

const UDataTable* ACataclysmMinion::LoadTypeTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, MinionTypeTablePath);
	if (!Table)
	{
		// Loudly, and naming both scripts, because the two failures look the
		// same from here: the workbook never produced the CSV, or the CSV was
		// never imported as an asset.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load %s. It is produced by "
				 "tools/generate_datatable_assets.py from game/Data/"
				 "MinionTypes.csv, which tools/generate_datatables.py produces "
				 "from the Minion Types sheet of "
				 "docs/All_Things_Cataclysm.xlsx."), MinionTypeTablePath);
	}
	return Table;
}

const FCataclysmMinionTypeRow* ACataclysmMinion::FindType(
	const UDataTable* Table, const FString& InTypeName)
{
	if (!Table || InTypeName.IsEmpty())
	{
		return nullptr;
	}

	// MATCHED ON THE ROW NAME, which is what the Minions parameter writes:
	// `Ballista:2` names the Ballista row. The generator's
	// validate_minion_references already refuses a name the sheet does not
	// have, so a miss here means the table is stale rather than the cell wrong.
	const FCataclysmMinionTypeRow* Found = nullptr;
	Table->ForeachRow<FCataclysmMinionTypeRow>(
		TEXT("ACataclysmMinion::FindType"),
		[&](const FName& RowName, const FCataclysmMinionTypeRow& Row)
		{
			if (!Found && RowName.ToString().Equals(InTypeName, ESearchCase::IgnoreCase))
			{
				Found = &Row;
			}
		});

	return Found;
}

ACataclysmMinion::ACataclysmMinion()
{
	// Nothing to do per frame. Its controller thinks on a timer and the
	// character movement component walks it.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// The same brain a monster has. What differs is its side and what its
	// attacks are worth, not how it decides.
	AIControllerClass = ACataclysmEnemyController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(
		TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	VitalAttributes = CreateDefaultSubobject<UCataclysmVitalAttributeSet>(
		TEXT("VitalAttributes"));

	// SMALLER THAN AN ENEMY AND SMALLER THAN THE PLAYER, so that three of them
	// around a fight are recognisable as imps rather than as more monsters.
	GetCapsuleComponent()->InitCapsuleSize(MinionCapsuleRadius, MinionCapsuleHalfHeight);

	GetCharacterMovement()->MaxWalkSpeed = MinionWalkSpeedCmPerSecond;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 640.0f, 0.0f);

	// A stand-in body, for the same reason the player and every enemy have one:
	// this project's Content folder holds no meshes at all, so without it an imp
	// is an invisible capsule and there is no way to tell whether one is there.
	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(RootComponent);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetRelativeScale3D(FVector(
		(MinionCapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(MinionCapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(MinionCapsuleHalfHeight * 2.0f) / ACataclysmCharacterBase::BasicShapeSize));

	// A cone rather than the cylinder the player and enemies use, so an imp is
	// distinguishable from both at a glance. Engine content, found by path, so
	// this adds no asset to the project. A failure here is not fatal: the
	// capsule is still there and still takes damage, it is just invisible.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(
		TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(ConeMesh.Object);
	}
}

UAbilitySystemComponent* ACataclysmMinion::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACataclysmMinion::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}

ACataclysmMinion* ACataclysmMinion::Spawn(AActor* InSummoner, const FVector& Location,
										  float Lifetime, bool bBurns,
										  const FString& InTypeName,
										  float HealthPercent)
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

	// ITS OWN NUMBERS, IF IT WAS TOLD WHAT IT IS. Before issue #622 every minion
	// carried one set of compile-time constants, so a ballista and an imp were
	// the same creature with a different name in the prose. A minion spawned
	// without a type keeps those defaults, which is what the tests that predate
	// this rely on.
	if (const FCataclysmMinionTypeRow* Type = FindType(LoadTypeTable(), InTypeName))
	{
		Minion->TypeName = InTypeName;
		Minion->ReachCm = Type->ReachCm;
		Minion->NoticeRadiusCm = Type->NoticeRadiusCm;
		Minion->AttackIntervalSeconds = Type->AttackIntervalSeconds;

		// THE MOVE SPEED IS WRITTEN IN METRES PER SECOND and Unreal walks in
		// centimetres, the same conversion the shape parameters make.
		// A ZERO IS NOT A MISSING NUMBER HERE: it is what makes a turret, a
		// ballista and a spike trap stay where they are put, which is the whole
		// behavioural difference between the Summon shape and the Deployable
		// shape. Issue #621.
		Minion->bStaysWhereItIsPut = Type->MoveSpeed <= 0.0f;
		if (UCharacterMovementComponent* Movement = Minion->GetCharacterMovement())
		{
			Movement->MaxWalkSpeed = Type->MoveSpeed * 100.0f;
		}
	}
	else if (!InTypeName.IsEmpty())
	{
		// Named something the table does not have. Loud, because the generator
		// refuses an unknown name, so reaching here means the imported asset is
		// older than the sheet -- and the symptom is a ballista that behaves
		// like an imp, which nothing else would report.
		UE_LOG(LogCataclysm, Warning,
			TEXT("No minion type named '%s'. It was spawned carrying the "
				 "defaults instead. Run tools/generate_datatable_assets.py."),
			*InTypeName);
	}

	// HEALTH IS DELIBERATELY NOT SET FROM THE TYPE. The table states BaseHealth
	// and HealthPerLevel, and applying them needs the summoner's level, which
	// nothing in this module can read yet. Issue #340 holds that half, together
	// with the damage model. HealthPercent is accepted so a caller need not know
	// that, and is recorded rather than silently dropped.
	Minion->DeployedHealthPercent = HealthPercent;

	// The summoner's side, not one of its own. A Ritualist's imps must be
	// friendly to a second player in the party, not merely to the Ritualist,
	// and ownership alone cannot say that.
	Minion->SetGenericTeamId(UCataclysmTeams::TeamOf(InSummoner));

	Minion->SetLifeSpan(Lifetime);

	return Minion;
}

void ACataclysmMinion::AttackOnce()
{
	// Nearest first, one target. Used by tests and by anything that wants one
	// swing without a controller; the ordinary case is the controller calling
	// AttackTarget with what it chose.
	const TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
		GetWorld(), this, GetActorLocation(), ReachCm, /*MaxTargets=*/1);
	if (Nearby.IsEmpty())
	{
		return;
	}

	AttackTarget(Nearby[0]);
}

void ACataclysmMinion::AttackTarget(AActor* Target)
{
	if (!IsValid(Summoner) || !IsValid(Target))
	{
		return;
	}

	// Damage comes from the SUMMONER's weapon, not the minion's own, which it
	// has none of. So a Ritualist's imps get stronger as the Ritualist does,
	// which is how every minion in the genre scales.
	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Summoner, Target, DamagePercentOfSummoner, FGameplayTagContainer(),
		MinionDelivery(/*bIsArea=*/false));

	if (bBurnsWhatItHits && Dealt > 0.0f)
	{
		UCataclysmSkillEffects::ApplyBurn(Summoner, Target, Dealt);
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
			// AREA DAMAGE: an explosion swept a sphere. The melee attack
			// above is a single blow and stays evadable. Issue #513.
			const float Dealt = UCataclysmSkillEffects::ApplyHit(
				Summoner, Target, DamagePercent, FGameplayTagContainer(),
				MinionDelivery(/*bIsArea=*/true));
			if (bBurnsWhatItHits && Dealt > 0.0f)
			{
				UCataclysmSkillEffects::ApplyBurn(Summoner, Target, Dealt);
			}
		}
	}

	Destroy();
}
