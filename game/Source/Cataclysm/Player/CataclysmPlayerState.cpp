// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPlayerState.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "Character/CataclysmCharacterCreation.h"
#include "Character/CataclysmExperience.h"
#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPassiveTree.h"
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
	DOREPLIFETIME(ACataclysmPlayerState, CharacterLevel);
	DOREPLIFETIME(ACataclysmPlayerState, ExperienceIntoLevel);
	DOREPLIFETIME(ACataclysmPlayerState, CreationChoice);
	DOREPLIFETIME(ACataclysmPlayerState, PassiveAllocation);
	DOREPLIFETIME(ACataclysmPlayerState, DefeatedCataclysmBosses);
}

FCataclysmCreationChoice ACataclysmPlayerState::GetCreationChoice() const
{
	// THE DEFAULTS FILL IN EACH HALF SEPARATELY, not the pair together. Nothing
	// today writes one half without the other -- `ChooseAtCreation` refuses an
	// incomplete pair and `SetCreationChoice` is handed both out of one record
	// -- but a half-filled choice would otherwise answer NAME_None for the
	// other half, and NAME_None is not a weapon type any table can be asked
	// about.
	FCataclysmCreationChoice Answer;
	Answer.WeaponType = GetChosenWeaponType();
	Answer.DamageType = GetChosenDamageType();
	return Answer;
}

FName ACataclysmPlayerState::GetChosenWeaponType() const
{
	return CreationChoice.WeaponType.IsNone()
		? UCataclysmCharacterCreation::DefaultWeaponType
		: CreationChoice.WeaponType;
}

FName ACataclysmPlayerState::GetChosenDamageType() const
{
	return CreationChoice.DamageType.IsNone()
		? UCataclysmCharacterCreation::DefaultDamageType
		: CreationChoice.DamageType;
}

bool ACataclysmPlayerState::ChooseAtCreation(const UDataTable* WeaponSkillTable,
											 const UDataTable* BaseTable,
											 FName WeaponType, FName DamageType,
											 FString& OutReason)
{
	FCataclysmCreationChoice Asked;
	Asked.WeaponType = WeaponType;
	Asked.DamageType = DamageType;

	OutReason = UCataclysmCharacterCreation::RefusalFor(WeaponSkillTable,
													   BaseTable, Asked);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	CreationChoice = Asked;
	return true;
}

void ACataclysmPlayerState::SetCreationChoice(FName WeaponType, FName DamageType)
{
	CreationChoice.WeaponType = WeaponType;
	CreationChoice.DamageType = DamageType;
}

int32 ACataclysmPlayerState::GetCharacterLevel() const
{
	// THE CONSOLE VARIABLE IS THE STARTING LEVEL, not a competing answer. Until
	// a character gains a level or loads a save, `Cataclysm.PlayerLevel` decides
	// what level it is, exactly as it did before levelling existed. That is what
	// keeps every automation test that sets it working unchanged.
	return CharacterLevel > LevelNotYetDecided
		? CharacterLevel
		: UCataclysmPlayerClassStats::ChosenLevel();
}

int32 ACataclysmPlayerState::GrantExperience(int64 Amount)
{
	// GRANTING NOTHING CHANGES NOTHING, INCLUDING THE LEVEL, and that is worth
	// a branch of its own rather than leaving it to the clamp below. A kill
	// worth zero is a real case while Enemy Score has no port -- issue #926 --
	// and without this the first such kill would settle the character's level
	// at whatever `Cataclysm.PlayerLevel` happened to say at that instant, which
	// is a lasting change made by an event that did nothing.
	if (Amount <= 0)
	{
		return 0;
	}

	// SETTLED BEFORE IT IS RAISED. Until now the level has been whatever the
	// console variable says, which can change under the character; the first
	// real grant is the moment that stops being acceptable, because a level
	// gained from a base that then moves is a level gained from nothing.
	int32 Level = GetCharacterLevel();
	const int32 Gained =
		UCataclysmExperience::Grant(Amount, Level, ExperienceIntoLevel);
	CharacterLevel = Level;
	return Gained;
}

void ACataclysmPlayerState::SetLevelAndExperience(int32 NewLevel,
												  int64 NewExperience)
{
	CharacterLevel = FMath::Clamp(NewLevel, UCataclysmExperience::FirstLevel,
								  UCataclysmExperience::MaxLevel);

	// PROGRESS IS CLAMPED TO WHAT THE NEXT LEVEL COSTS, not just to zero. A
	// record holding more than the next level costs would describe a character
	// that should already have levelled up, and leaving it there would mean the
	// next grant of one point jumped a level. At the maximum level the next
	// level costs nothing, so this correctly stores nothing.
	const int64 Ceiling = UCataclysmExperience::CostOfLevel(CharacterLevel + 1);
	const int64 Most = FMath::Max<int64>(0, Ceiling - 1);
	ExperienceIntoLevel = FMath::Clamp<int64>(NewExperience, 0, Most);
}

int32 ACataclysmPlayerState::AttributePointsAvailable() const
{
	// THE LEVEL AND NOTHING ELSE. `docs/Cataclysm_GDD_v2.md` names two sources,
	// "1 attribute point per level" and the Maw, which "consumes items and
	// enemies for Attribute points". The Maw does not exist, so a term for it
	// here would be a number nobody chose. Issue #50.
	return GetCharacterLevel();
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

// ---------------------------------------------------------------------------
// Passive points
// ---------------------------------------------------------------------------

int32 ACataclysmPlayerState::PassivePointsAvailable() const
{
	return UCataclysmPassivePoints::Available(GetCharacterLevel(),
											  DefeatedCataclysmBosses.Num());
}

int32 ACataclysmPlayerState::PassivePointsUnspent() const
{
	return PassivePointsAvailable() - PassiveAllocation.Total();
}

bool ACataclysmPlayerState::RecordCataclysmBossDefeat(FName Boss)
{
	if (Boss.IsNone())
	{
		return false;
	}

	// ALREADY BEATEN MEANS NOTHING HAPPENS, which is the design's own word:
	// ten points for the FIRST defeat of each unique boss. Without the check a
	// player could farm one boss for the whole 80.
	if (DefeatedCataclysmBosses.Contains(Boss))
	{
		return false;
	}

	DefeatedCataclysmBosses.Add(Boss);
	return true;
}

TArray<FString> ACataclysmPlayerState::ReachableTrees() const
{
	// ONE DAMAGE TYPE TODAY, AND THE SHAPE IS ALREADY THE ONE FOR SEVERAL. The
	// design says what a character has access to "is determined by the set of
	// damage types across ALL equipped weapons", and a weapon can carry up to
	// eight. The game holds a single damage type on the weapon slots component
	// as a stand-in until a rolled item carries its own; passing an array of one
	// means nothing here changes when it does.
	const TArray<FName> Carried = {GetChosenDamageType()};
	return UCataclysmPassiveTree::ReachableTrees(
		UCataclysmPassiveTree::LoadNodeTable(), Carried);
}

bool ACataclysmPlayerState::SpendPassivePoint(FName Node, FString& OutReason)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EdgeTable = UCataclysmPassiveTree::LoadEdgeTable();

	// THE CHARACTER'S OWN QUESTION FIRST, BEFORE THE TREE'S RULES. Which trees
	// are reachable follows from the damage type this character carries, which
	// `UCataclysmPassiveTree` deliberately knows nothing about. Asking it first
	// also means the reason a player sees is the useful one: "your weapon does
	// not reach that tree" rather than "that node is shut".
	const FString Tree = UCataclysmPassiveTree::TreeOf(NodeTable, Node);
	if (!Tree.IsEmpty())
	{
		const TArray<FName> Carried = {GetChosenDamageType()};
		if (!UCataclysmPassiveTree::TreeIsReachable(Tree, Carried))
		{
			OutReason = FString::Printf(
				TEXT("No equipped weapon carries a damage type that unlocks the "
					 "%s tree. This character is %s."),
				*Tree, *GetChosenDamageType().ToString());
			return false;
		}
	}

	return UCataclysmPassiveTree::Spend(NodeTable, EdgeTable, PassiveAllocation,
										Node, PassivePointsAvailable(),
										OutReason);
}

bool ACataclysmPlayerState::ChoosePassiveOption(FName Node, int32 Option,
												FString& OutReason)
{
	return UCataclysmPassiveTree::ChooseOption(
		UCataclysmPassiveTree::LoadNodeTable(), PassiveAllocation, Node, Option,
		OutReason);
}

void ACataclysmPlayerState::ResetPassivePoints()
{
	// THE CAPSTONE CHOICES GO WITH THE POINTS. Each capstone's own description
	// ends "The choice is permanent", and a respec that returned the points
	// while leaving the four decisions made would not be a respec at all.
	PassiveAllocation.Clear();
}

void ACataclysmPlayerState::SetPassiveAllocation(
	const FCataclysmPassiveAllocation& Allocation, const TArray<FName>& Bosses)
{
	PassiveAllocation = Allocation;
	DefeatedCataclysmBosses = Bosses;
}
