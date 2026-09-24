// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For what a blow resolved to, so a burn is refused on an evaded one.
// Issue #1156.
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyController.h"
// For the level a minion's own health and damage are raised by, and for the
// fallback when the summoner has no player state. Issue #340.
#include "Character/CataclysmPlayerClassStats.h"
#include "GameFramework/Pawn.h"
#include "Player/CataclysmPlayerState.h"
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
	 *
	 * AND IT CARRIES NO WEAPON SUB-TYPE, which is the third thing that crossed by
	 * the same route. The weapon a hit is credited to is read off the effect
	 * causer, which is this summoner, so a sword made an imp deal 10% more to
	 * health and a wand made it strip 10% more energy shield. No sentence of the
	 * design names sub-types; the general rule blocks them, because a minion
	 * reaches its summoner through exactly three channels and this is not one of
	 * them. Issue #676.
	 */
	/**
	 * The level a minion's own base health and damage are raised by.
	 *
	 * THE SUMMONER'S, NOT THE MINION'S. A minion has no level of its own; the
	 * design's phrase is "the type's own base, raised by the summoner's
	 * level".
	 *
	 * THE FALLBACK IS NOT A CORNER CASE. A character's level lives on its
	 * player state, and an ENEMY summoner has none -- so every minion summoned
	 * by anything but a player takes this path.
	 * `UCataclysmEquipmentComponent` asks the same question the same way.
	 *
	 * AND WHAT IT FALLS BACK TO IS A PREVIEW CONTROL, WHICH IS FILED RATHER
	 * THAN FIXED HERE. `UCataclysmPlayerClassStats::ChosenLevel` is
	 * documented as "which level the console variable asks for", meant for
	 * previewing class stats in the editor rather than describing anything
	 * in the world. Nothing in the game reaches it today: both places that
	 * create a minion are player weapon skills, at
	 * `CataclysmSkillTemplates.cpp` lines 3022 and 3263, and a player pawn
	 * has a player state. Issue #1702 carries it, and it has no fix yet
	 * because an enemy has no level of its own to use instead.
	 */
	int32 LevelOfSummoner(const AActor* Summoner)
	{
		if (const APawn* Pawn = Cast<const APawn>(Summoner))
		{
			if (const ACataclysmPlayerState* State =
					Pawn->GetPlayerState<ACataclysmPlayerState>())
			{
				return State->GetCharacterLevel();
			}
		}
		return UCataclysmPlayerClassStats::ChosenLevel();
	}

	/**
	 * A base figure raised by a level, which is how the minion type table is
	 * read.
	 *
	 * `Base + PerLevel * Level`, AND THE ARITHMETIC IS NOT A CHOICE. The other
	 * reading, `Base + PerLevel * (Level - 1)`, is what the simulation's
	 * character model uses for a PLAYER -- and the minion table's numbers were
	 * authored against this one. `tools/tests/test_minion_stat_blocks.py`
	 * asserts three imps out-damage their summoner's basic attack at every
	 * difficulty tier; at tier 1 that is 410 a second against the summoner's
	 * 382 under this arithmetic and 378 under the other, so the other fails.
	 * Every tier from 2 upward holds either way, which is why it had to be
	 * measured rather than argued.
	 *
	 * THE TWO CONVENTIONS DIFFER BY ONE LEVEL'S WORTH AND NOTHING SAYS THEY
	 * ARE MEANT TO. Issue #1700 carries it, rather than quietly aligning them,
	 * because changing either is a balance change nobody asked for. Everything
	 * about a minion uses this form -- `tools/tests/test_minion_stat_blocks.py`
	 * lines 49 and 54 have since before this code was written -- and everything
	 * about a player uses the other, in `CataclysmClassStats.cpp`,
	 * `CataclysmSkillSlots.cpp` and `sim/cataclysm_sim/character.py`.
	 */
	float RaisedByLevel(float Base, float PerLevel, int32 Level)
	{
		return Base + PerLevel * static_cast<float>(Level);
	}

	/**
	 * What a summoner's gear and passives do to one of its minions' own figures,
	 * as a multiplier. 1.25 is twenty-five per cent more. Issue #898.
	 *
	 * A MODIFIER THAT NAMES MINIONS IS NOT A FOURTH CHANNEL. The decision of
	 * 2026-08-06 lists three channels and then says, in the next sentence,
	 * "Everything else is blocked unless a modifier names minions", and the
	 * owner's reversal it records has three parts of which the third is "minion
	 * affixes exist on gear on top of that". `minion_damage` and `minion_health`
	 * are the case that qualifier exists for. `AttackTarget` used to quote the
	 * three-channel sentence without it, which read as forbidding this.
	 *
	 * THE INCREASES AND NOT THE STAT'S VALUE. Neither stat has a base and neither
	 * can have one -- a minion's damage and health come from its own row in
	 * `game/Data/MinionTypes.csv`, raised by its summoner's level -- so asking
	 * for the value would return zero however much gear was worn.
	 * `UCataclysmAbilitySystemComponent::IncreasesForStat` exists for that.
	 *
	 * A SUM RATHER THAN A SEPARATE MULTIPLIER, AND THE DESIGN REQUIRES IT. The
	 * same entry says "an attribute's contribution and an affix's contribution
	 * add. They cannot multiply each other", and gives the reason: every
	 * catastrophic minion scaling failure in the survey behind that decision was
	 * multiplicative. `IncreasesForStat` returns the SUM of the increases, so
	 * when the attribute channel is built it lands in the same bucket and adds.
	 *
	 * A REDUCTION IS KEPT AND ONLY THE RESULT IS FLOORED. Ten rows of
	 * `game/Data/PassiveEffects.csv` already carry a negative value, so a node
	 * reducing minion damage is a thing the data can express. Clamping the
	 * increases at zero would discard a designed drawback in silence; flooring
	 * the multiplier only stops a figure below -100% turning into negative
	 * damage or negative health.
	 *
	 * AN EMPTY TAG CONTAINER, for the reason `CataclysmCommand.cpp` records: all
	 * four minion affix rows carry no scope tags, the affix table has no column
	 * for them, and a minion carries no gameplay tags to test a narrower one
	 * against.
	 */
	float SummonerMultiplierFor(const AActor* Summoner, const TCHAR* Stat)
	{
		const UCataclysmAbilitySystemComponent* Theirs =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Summoner));
		if (!Theirs)
		{
			// NO STAT LINE, WHICH IS ORDINARY RATHER THAN A FAULT: an enemy
			// summoner is never given one, and a player's is empty until the
			// first refresh. Nothing recorded means nothing added.
			return 1.0f;
		}

		return FMath::Max(0.0f, 1.0f + Theirs->IncreasesForStat(
			FName(Stat), FGameplayTagContainer()));
	}

	/**
	 * What one of the summoner's stats stands at, or nothing.
	 *
	 * THE SIBLING OF `SummonerMultiplierFor` ABOVE, and it reads a flag rather
	 * than a multiplier: `minion_explodes_on_death` is a stat a passive row
	 * sets to one, so what matters is whether it is above zero. Issue #1515.
	 *
	 * `StatForSkill` RATHER THAN THE ATTRIBUTE, because the stat has no
	 * gameplay attribute at all -- it is one of the names in
	 * `UCataclysmPlayerClassStats::StatsWithNoAttribute` -- so the recorded
	 * stat line is the only place its value exists. Nothing for a summoner
	 * with no stat line, which is ordinary: an enemy summoner never has one.
	 */
	float SummonerStat(const AActor* Summoner, const TCHAR* Stat)
	{
		const UCataclysmAbilitySystemComponent* Theirs =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Summoner));
		return Theirs
			? Theirs->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f)
			: 0.0f;
	}

	FCataclysmHitDelivery MinionDelivery(ACataclysmMinion* Minion, bool bIsArea)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = bIsArea;

		// AND THE MINION IS NAMED AS WHAT DEALT IT. Issue #41, slice 4. It goes
		// on the effect as the source object, which the hit and death notices
		// read to say a minion struck and which none of the rules that work out
		// damage reads, so every exclusion below works exactly as it did.
		//
		// AND SINCE ISSUE #1515 IT DECIDES WHOSE THE BLOW IS. The notice's
		// attacker used to be the instigator for every blow, which made a
		// minion's hit and kill the summoner's; `UCataclysmCombatEvents::
		// NoteBlow` now reads this field, asks
		// `ACataclysmMinion::HitsCountAsTheSummoners`, and credits the summoner
		// only when it holds the Conduit keystone.
		Delivery.DealtBy = Minion;

		// THESE THREE ARE NO LONGER THE ONLY THING STOPPING A SUMMONER'S NUMBERS.
		// Issue #1515. Until 2026-09-17 a minion struck in its summoner's name,
		// so a critical strike chance, a penetration figure and a weapon
		// sub-type were all read off the summoner unless a flag said otherwise,
		// and a reading added later crossed to every minion in the game until
		// somebody remembered to add a flag for it. The minion is its own
		// instigator now: it carries no combat attribute set and no weapon, so
		// those readings find nothing of its own. The flags are kept because
		// they state the rule rather than leaving it to what a minion happens
		// not to have, and because issue #340 may one day give a minion figures
		// of its own -- at which point "a minion never critically strikes" has
		// to go on being true.
		Delivery.bCannotCriticallyStrike = true;
		Delivery.bCannotPenetrate = true;
		Delivery.bCarriesNoWeaponSubType = true;

		// AND NO LEECH, WHICH IS NOW A DIFFERENT QUESTION FROM THE THREE ABOVE.
		// Leech is read off the attacker's VITAL attribute set, and a minion has
		// one -- its leech figures exist and are zero -- so "a minion does not
		// take its summoner's leech" is true of its own accord since the
		// instigator changed. What this flag now forbids is a minion leeching
		// from figures of ITS OWN, which the design does not ask for and no data
		// can produce today. It is kept so that this change moves no behaviour,
		// and the decision to keep it is recorded rather than assumed. Issue
		// #895.
		Delivery.bCannotLeech = true;

		// THE RETALIATION EXCLUSION IS GONE, AND THAT IS A DECISION RATHER THAN
		// AN OVERSIGHT. The project owner decided on 2026-09-18 that a minion
		// takes the retaliation its own blow provokes, as the thing that swung
		// would in any other case. The exclusion existed to stop a summoner
		// standing well away from the fight taking damage for its minion's blow,
		// and that reason went with the instigator: retaliation is paid back to
		// whoever dealt the blow, and a minion now deals its own. Minion builds
		// lose minions to reflecting enemies, which the owner accepted.
		// `Cataclysm.Retaliation.AMinionTakesTheRetaliationItsOwnBlowProvokes`
		// is the case that holds it.

		// AND IT CARRIES NO CHANCE TO APPLY AN AILMENT. The chances are worked
		// out from the attacker when the blow is built, so this stopped a
		// summoner's chance to bleed from gear reaching every imp. The design
		// names "chance to apply an ailment" among what does not cross, and the
		// flag says so rather than resting on a minion having no such chances of
		// its own. Issue #899.
		Delivery.bCarriesNoAilmentChance = true;

		// AND IT REPORTS NO DISTANCE TO ITS TARGET, the seventh. Issue #1596.
		//
		// THIS ONE IS NOT LIKE THE SIX ABOVE AND THE DIFFERENCE IS WORTH SAYING.
		// Each of those stops a CAPABILITY of the summoner's from crossing -- its
		// critical strike, its penetration, its weapon, its leech, its ailment
		// chances -- and in each case the number that would have crossed was
		// simply the wrong number. Here the number is defensible: a distance
		// measured on this blow is the summoner's distance to the minion's
		// target, and "enemies within 5 meters of you" is a question about where
		// the WEARER stands, so that is arguably what the sentence asks for.
		//
		// IT IS REFUSED ON A DIFFERENT GROUND: a player's conditional damage
		// bonus should not reach a minion's blow at all. Path of Exile treats a
		// minion's actions as separate from its summoner's, and Last Epoch's own
		// documentation says a character's modifiers do not apply unless minions
		// are specified. `docs/DECISIONS.md` carries the sources and the ruling.
		//
		// AND IT IS SAID HERE RATHER THAN LEFT TO HAPPEN BY ITSELF. A row scoped
		// to a skill tag already cannot match this blow, because the two ApplyHit
		// calls below pass an empty tag container -- but that is a coincidence
		// rather than a safeguard, and it would vanish the moment a minion's blow
		// carried its skill's tags.
		Delivery.bCarriesNoTargetDistance = true;

		// AND IT REPORTS NO STATE OF ITS TARGET EITHER, the eighth. Issue #45.
		//
		// THE SAME GROUND AS THE SEVENTH AND A WEAKER CASE FOR MEASURING IT. The
		// distance above at least reports the wrong end of the blow; whether this
		// minion's target is staggered is a fact about that target and is exactly
		// the same whoever struck it, so there is no wrong number here to point
		// at. It is refused purely because a player's conditional damage bonus
		// should not reach a minion's blow -- which is the real reason the
		// seventh is refused as well, stated there in full.
		//
		// SET TOGETHER WITH THE SEVENTH AND ALWAYS WILL BE. If a third reading of
		// the target appears, the three should become one flag.
		Delivery.bCarriesNoTargetState = true;
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

bool ACataclysmMinion::HitsCountAsTheSummoners(const ACataclysmMinion* Minion)
{
	// THE SUMMONER IS ASKED, NOT THE MINION, because the keystone is the
	// summoner's and a minion carries no passive tree of its own. The header
	// says what the flag means and what it leaves alone.
	return IsValid(Minion)
		&& SummonerStat(Minion->Summoner,
						TEXT("minion_hits_count_as_yours")) > 0.0f;
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

		// AND ITS OWN HEALTH AND DAMAGE, RAISED BY THE SUMMONER'S LEVEL. Issue
		// #340. The decision of 2026-08-06 reversed the rule this file used to
		// carry -- a minion deals its OWN damage, not a share of its summoner's
		// weapon -- and the four columns behind it have been in
		// `game/Data/MinionTypes.csv` since, read by nothing.
		//
		// THE LEVEL IS READ ONCE. It cannot change while this minion exists.
		const int32 Level = LevelOfSummoner(InSummoner);
		Minion->OwnDamagePerHit =
			RaisedByLevel(Type->BaseDamage, Type->DamagePerLevel, Level);

		// AND WHAT ITS DEATH IS WORTH, AS A SHARE OF THAT BLOW. Issue #1515.
		// The type row states it, so an explosion is the minion's own figure
		// rather than the summoning skill's percentage of the summoner's
		// weapon. Only the radius still comes from the skill.
		Minion->ExplosionPercentOfOwnDamage = Type->ExplosionPercentOfOwnDamage;

		// MAXIMUM FIRST, THEN CURRENT, and the order is not incidental: the
		// vital attribute set clamps health to the maximum in
		// `PreAttributeChange`, so raising the current value first would clamp
		// it straight back down to the old maximum.
		// `ACataclysmEnemyCharacter` sets a creature's health the same way and
		// records the same reason.
		//
		// HEALTH IS TAKEN ONCE, AT THE SUMMONING, AND THAT IS A SNAPSHOT. Issue
		// #898. Damage is read fresh at every blow and attack speed at every
		// swing, so this is the one minion figure that does not follow a gear
		// change. It is a pool: the maximum is written to an attribute here and
		// nothing re-runs this when equipment or passives change.
		//
		// AND THAT IS THE CASE THE DESIGN ALREADY NAMES. The decision of
		// 2026-09-13, section "Minions update live rather than snapshotting",
		// records the owner's ruling that minions should update "anytime
		// gear/passives/skills change", calls it "Not built, and not urgent by
		// the same ruling", and says where it bites: "A per-swing read is live
		// by construction, so this constrains health, which is set once at
		// spawn, and not a stat asked for at the moment of a blow."
		//
		// So damage and attack speed satisfy that ruling by the way they are
		// read, and this line is the one place it is outstanding. Deferred by
		// the ruling rather than missed.
		//
		// A MULTIPLIER OF ZERO LEAVES THE MINION ITS DEFAULT HEALTH rather than
		// being born dead, because the guard below already refuses a figure of
		// zero -- which it was written for a type row that states no health. No
		// shipped data reaches -100% minion health; this says what would happen
		// rather than adding a floor nobody chose.
		const float OwnHealth =
			RaisedByLevel(Type->BaseHealth, Type->HealthPerLevel, Level)
			* SummonerMultiplierFor(InSummoner, TEXT("minion_health"));
		if (Minion->AbilitySystemComponent && OwnHealth > 0.0f)
		{
			Minion->AbilitySystemComponent->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), OwnHealth);
			Minion->AbilitySystemComponent->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), OwnHealth);
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

	// HEALTH FROM THE TYPE IS SET ABOVE, WHERE THE ROW IS IN HAND. This one is
	// the share a DEPLOYED machine was told to start at, which is a different
	// number: Iron Fortress deploys a ballista at a stated percentage. It is
	// recorded and not yet applied -- issue #340's remaining half.
	Minion->DeployedHealthPercent = HealthPercent;

	// The summoner's side, not one of its own. A Ritualist's imps must be
	// friendly to a second player in the party, not merely to the Ritualist,
	// and ownership alone cannot say that.
	Minion->SetGenericTeamId(UCataclysmTeams::TeamOf(InSummoner));

	// THE LIFETIME THE SKILL STATES, RAISED BY THE SUMMONER'S `minion_duration`.
	// Issue #1515: `Ritualist_basic_b_c0` Kept Longer, "+3% increased duration of
	// what you summon per point". Everything summoned comes through here, a
	// deployed machine as well as a summoned minion, and the stat belongs to the
	// summoner, so both are "what you summon".
	//
	// FIXED AT THE SUMMONING, like health above and for the reason given there:
	// the owner's ruling that minions follow later gear and passive changes is
	// recorded as not built, and this is a second figure it covers.
	//
	// A MULTIPLIER OF ZERO OR LESS KEEPS THE STATED LIFETIME, and this guard is
	// not the one health has for the same case. `SetLifeSpan(0)` means "never
	// expires", so a reduction of a hundred per cent would otherwise make a
	// minion permanent. No shipped row reduces the duration; this says what
	// would happen rather than leaving it to the engine's meaning of zero.
	const float Duration =
		Lifetime * SummonerMultiplierFor(InSummoner, TEXT("minion_duration"));
	Minion->SetLifeSpan(Duration > 0.0f ? Duration : Lifetime);

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

	// ITS OWN DAMAGE, NOT A SHARE OF ITS SUMMONER'S WEAPON. Issue #340. This
	// file dealt 30% of the summoner's weapon until the decision of 2026-08-06
	// reversed that: "a minion reaches its summoner through three channels and
	// nothing else: its side, its base health and damage raised by the
	// summoner's level, and increased damage from one primary attribute
	// declared per minion type. **Everything else is blocked unless a modifier
	// names minions.**"
	//
	// THAT LAST SENTENCE USED TO BE MISSING FROM THIS COMMENT, and without it
	// the rule reads as forbidding minion gear. It does not: the same entry
	// summarises the owner's reversal in three parts, the third being "minion
	// affixes exist on gear on top of that", and it names an existing
	// minion-inheritance enchantment as a deliberate exception. A stat called
	// `minion_damage` is the case the qualifier is for. Issue #898.
	//
	// `ApplyDirectDamage` RATHER THAN `ApplyHit`, AND THAT IS THE WHOLE POINT.
	// `ApplyHit` computes weapon damage times a percentage and runs the
	// CASTER'S stat modifiers over it before handing the result to this same
	// function. Going through it would let EVERY increase the summoner carries
	// reach a minion's blow -- their increased attack damage, their increased
	// fire damage, all of it -- and that is the fourth channel the rule blocks.
	// The multiplier applied below is not: it comes from one stat that names
	// minions and nothing else.
	//
	// THE THIRD CHANNEL IS STILL NOT BUILT. `game/Data/MinionScaling.csv` names
	// one primary attribute per minion type and is read by nothing in the
	// engine. Issue #898 carries it -- "three models of minion scaling
	// disagree" -- and it is blocked on the same thing a scoped modifier is: it
	// matches on `RequiresTag` and a minion carries no gameplay tags. So after
	// this change two of the three channels work and the attribute one does
	// not.
	//
	// IT IS NOT A PATH INVENTED FOR MINIONS. `ACataclysmGroundZone` uses the
	// same one in `ACataclysmGroundZone::Sweep` for a damaging area on the
	// floor: a zone deals the figure it was built with to everything standing
	// in it, and no stat of the caster's is read at the moment it ticks.
	// `CataclysmNova.cpp`, `CataclysmRetaliation.cpp`,
	// `CataclysmSkillTemplates.cpp` and `CataclysmEnemyModifiers.cpp` are the
	// other four callers.
	//
	// THE SUMMONER IS STILL THE INSTIGATOR. The Conduit keystone reads "damage
	// dealt by your minions counts as damage you dealt, for every effect of
	// yours that asks", and which side the blow belongs to is decided the same
	// way. Making the minion the instigator would be a smaller change to write
	// and would break both.
	//
	// THE DEFENDER'S MITIGATION STILL APPLIES. This is not
	// `ReduceHealthDirectly`, which bypasses every layer; evasion, block,
	// armour and resistance all run, and `Resolved` reports what they made of
	// it.
	//
	// A MINION WITH NO TYPE FALLS BACK TO THE OLD SHARE, AND THE GAME CAN
	// REACH THAT. `CataclysmSkillTemplates.cpp:3260` produces an empty type
	// name whenever a summoning skill's shape parameters name no minion
	// kind, and the deployable template at line 3022 passes its own name
	// through the same way. No shipped skill row leaves it empty today --
	// `test_every_demonic_minion_skill_produces_a_type_the_table_defines`
	// in `tools/tests/test_minion_stat_blocks.py` holds that -- so this is
	// what a mis-authored row degrades to, not a shape only tests reach.
	// Three tests older than the minion type table also summon one.
	FCataclysmDamageResult Resolved;
	float Dealt = 0.0f;
	if (OwnDamagePerHit > 0.0f)
	{
		// AND THE SUMMONER'S INCREASED MINION DAMAGE ON TOP, WHICH IS NEW. Issue
		// #898. Until this, `game/Data/Affixes.csv` granted `minion_damage` and
		// nothing in the engine read it.
		//
		// HERE RATHER THAN AT THE SUMMONING, so that it is not a snapshot. A
		// player who changes gear sees this blow change on the next swing, the
		// same way `UCataclysmCommand::AttackIntervalScaleFor` already reads
		// minion attack speed fresh. Only health is frozen, because it is a
		// pool; `Spawn` records why.
		//
		// THE BRANCH STILL TESTS THE TYPE ROW'S OWN FIGURE, NOT THIS ONE. A
		// multiplier of zero must not send a typed minion down the typeless
		// fallback below, which deals a share of the summoner's weapon and is a
		// different rule entirely.
		const float Damage = OwnDamagePerHit
			* SummonerMultiplierFor(Summoner, TEXT("minion_damage"));

		// THE MINION IS THE INSTIGATOR OF ITS OWN BLOW, SINCE ISSUE #1515. It
		// was the summoner until 2026-09-17, which is why everything read off
		// "the attacker" while a blow resolves had to be blocked by name. A
		// minion carries no combat attribute set and no weapon, so the same
		// readings now find nothing of its own rather than everything of its
		// summoner's, and a reading added later is blocked because it finds
		// nothing rather than because somebody remembered the list.
		UCataclysmSkillEffects::ApplyDirectDamage(
			this, Target, Damage,
			MinionDelivery(this, /*bIsArea=*/false), &Resolved);
		Dealt = Damage;
	}
	// AND NOTHING AT ALL WITHOUT A TYPE ROW, SINCE ISSUE #1515. A minion used
	// to fall back to 30% of its SUMMONER'S weapon damage here, which the
	// owner ruled a bug on 2026-09-17: a minion's blow carries the minion's
	// own numbers. `tools/generate_datatables.py` now refuses a summoning row
	// that names no minion type, so a minion with no stat block cannot be
	// summoned by shipped data at all, and one made any other way swings for
	// nothing rather than for its summoner.

	// AND THE BURN TAKES NONE OF THE SUMMONER'S DAMAGE OVER TIME STATS, for the
	// same reason its blow takes no critical strike, no penetration, no weapon
	// sub-type and no leech: it is applied with the summoner as the instigator,
	// and the design names damage over time among what a minion does not take
	// from its summoner. Issue #895.
	// A DESIGNED BURN, because `bBurnsWhatItHits` comes from the minion's own
	// row in the Minion Types sheet rather than from a chance on hit. Issue
	// #917: a designed ailment applies whether or not the blow hurt.
	//
	// AN EVADED SWING SETS NOTHING ALIGHT. Issue #1156, decided on 2026-09-04.
	// This one blow is evadable -- it is a single strike rather than area
	// damage, which the explosion below records -- so the test is worth making
	// here and would be worth nothing there.
	if (bBurnsWhatItHits && !Resolved.bEvaded)
	{
		// THE MINION SETS THE FIRE, AS IT DEALT THE BLOW. Ruled on 2026-09-17
		// under the project owner's delegation, with the instigator change
		// above: a burn a minion's swing applies is the minion's, or a minion's
		// swing and the fire it starts would belong to different characters.
		UCataclysmSkillEffects::ApplyBurn(this, Target, Dealt,
										  /*bScalesWithInstigator=*/false,
										  /*bBurnIsDesigned=*/true, /*DealtBy=*/this);
	}

	++AttacksMade;
}

void ACataclysmMinion::HandleDeath()
{
	// THE TAG AND NOTHING ELSE. Issue #1518. Every reader that asks whether a
	// character is alive asks `UCataclysmSkillEffects::IsDead`, which reads this
	// tag, and before this override a minion never took it: the base class's
	// `HandleDeath` is empty and this class did not override it, so a minion at
	// zero health went on answering "alive" for ever.
	//
	// `MarkDead` REFUSES A SECOND TIME, which is what makes a death happen once
	// however many writes at zero health reach it -- a burn ticking on a body,
	// two blows in the same frame.
	//
	// IT DOES NOT REMOVE THE ACTOR, deliberately, and that is the difference
	// between this and the enemy's. `Spawn` gave every minion a lifespan and
	// that is still what takes it out of the level, so the summon cap, the
	// spawning path and every test that counts minions behave exactly as they
	// did. Removing the body sooner is a separate change with its own issue.
	//
	// EXCEPT WHERE THE EXPLOSION BELOW REMOVES IT, since issue #1515. A
	// minion whose summoner carries `minion_explodes_on_death` is destroyed
	// by `Explode`, exactly as one the summon cap evicts already is, so that
	// minion leaves no body and stops counting toward the cap at once. Every
	// minion whose summoner has not taken that keystone behaves as before.
	// What the cap does with a body that is dead and still present is
	// https://github.com/sdubois777/Cataclysm/issues/1957 and is not changed
	// here.
	UCataclysmSkillEffects::MarkDead(this);

	// AND IT MAY BLOW UP ON THE WAY OUT. Issue #1515.
	// `Ritualist_keystone_b_kB` Every One Bursts: "Every minion explodes when
	// it dies, as one destroyed to make room for another does, with the radius
	// and damage of the skill that brought it."
	//
	// THE SUMMONER'S STAT DECIDES, NOT THE MINION'S. What the player chose is
	// on the player, and `Explode` already reads the summoner for everything
	// else the blow needs.
	//
	// BOTH FIGURES MUST BE THERE. A minion nobody told what its explosion is
	// leaves a body, because `Explode` destroys the actor whether or not it
	// finds anything to hurt, and a deployed ballista that vanished silently
	// on death would be a change to the deployable shape rather than to this
	// keystone.
	//
	// THE FERVOUR A SUMMONER GAINS FOR LOSING A MINION IS ALREADY SAFE. It is
	// granted before this runs, and `CataclysmVitalAttributeSet.cpp` says why:
	// a death handler may remove the actor, and the code that finds the
	// commander cannot walk the ownership chain of one that is leaving.
	if (ExplodesOnDeath())
	{
		Explode();
	}
}

bool ACataclysmMinion::ExplodesOnDeath() const
{
	return ExplosionRadiusCm > 0.0f && ExplosionPercentOfOwnDamage > 0.0f
		&& SummonerStat(Summoner, TEXT("minion_explodes_on_death")) > 0.0f;
}

void ACataclysmMinion::RecordExplosionRadius(float RadiusCm)
{
	ExplosionRadiusCm = RadiusCm;
}

void ACataclysmMinion::Explode()
{
	// WHAT IT IS WORTH IS ITS OWN BLOW, NOT ITS SUMMONER'S WEAPON. Issue
	// #1515. `game/Data/MinionTypes.csv` states the share -- the owner's
	// figure of 2026-09-17 is three of its own blows for every kind -- and
	// until then the caller passed the summoning skill's damage percentage,
	// which `ApplyHit` read against the summoner's weapon.
	//
	// THE SUMMONER'S INCREASED MINION DAMAGE IS IN THE BLOW ALREADY, because
	// the blow this is a share of is the one `AttackTarget` deals, and that
	// reads `minion_damage` fresh at every swing. Reading it again here would
	// count it twice.
	const float OwnBlow = OwnDamagePerHit;
	const float Damage = OwnBlow * ExplosionPercentOfOwnDamage / 100.0f;

	if (IsValid(Summoner) && ExplosionRadiusCm > 0.0f && Damage > 0.0f)
	{
		// THE SUMMONER'S OWN STAT ON THE EXPLOSION'S DAMAGE. Issue #1515.
		// `Ritualist_basic_b_a2` Volatile: "+3% increased damage of the
		// explosion a minion leaves per point."
		//
		// HERE RATHER THAN AT EITHER CALLER, so both causes of an explosion
		// take it: the summon cap destroying the oldest, and a death under
		// `minion_explodes_on_death`. The sentence names the explosion rather
		// than what caused it.
		//
		// AREA OF EFFECT IS STILL NOT TAKEN, and that is unchanged: the
		// radius is the skill's stated figure, as the caller's comment in
		// `CataclysmSkillTemplates.cpp` records for issues #910 and #340.
		// This stat is the damage and only the damage.
		const float Scaled = Damage
			* SummonerMultiplierFor(Summoner, TEXT("minion_explosion_damage"));

		const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), this, GetActorLocation(), ExplosionRadiusCm);

		for (AActor* Target : Caught)
		{
			// AREA DAMAGE: an explosion swept a sphere. The melee attack
			// above is a single blow and stays evadable. Issue #513.
			// AN AMOUNT, NOT A PERCENTAGE, SINCE ISSUE #1515: `ApplyHit` reads a
			// percentage against the INSTIGATOR'S weapon, which is the summoner's
			// and is exactly what a minion's blow must not use. `ApplyDirectDamage`
			// takes the figure as it stands, which is how `AttackTarget` deals a
			// typed minion's swing.
			//
			// AND THE MINION IS THE INSTIGATOR HERE TOO, for the reason its
			// swing gives above: an explosion is the minion's, not its
			// summoner's, and the summoner's own numbers must not be what the
			// blow is resolved against.
			float Dealt = 0.0f;
			UCataclysmSkillEffects::ApplyDirectDamage(
				this, Target, Scaled,
				MinionDelivery(this, /*bIsArea=*/true));
			Dealt = Scaled;
			// Designed, for the reason the melee attack above records.
			//
			// AND NOT TESTED FOR EVASION, DELIBERATELY. An explosion is area
			// damage and `UCataclysmDamageCalculation::Resolve` does not roll
			// evasion against area damage at all, so the test the melee attack
			// above makes for issue #1156 could never fire here. A check that
			// cannot fail is worse than no check, because it reads as one.
			if (bBurnsWhatItHits)
			{
				UCataclysmSkillEffects::ApplyBurn(
					this, Target, Dealt,
					/*bScalesWithInstigator=*/false,
					/*bBurnIsDesigned=*/true, /*DealtBy=*/this);
			}
		}
	}

	Destroy();
}
