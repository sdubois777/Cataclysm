// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPlayerState.h"
#include "Cataclysm.h"
#include "Player/CataclysmGameMode.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "Character/CataclysmCharacterCreation.h"
#include "Character/CataclysmExperience.h"
// For a skill's level from its experience and the character's. Issue #2764.
#include "Character/CataclysmSkillLevels.h"
#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPassiveTree.h"
// For a passive node's own MaxPoints, which decides whether choosing a capstone
// option still has a point to put in. Issue #1075.
#include "Data/CataclysmDataRows.h"
// For reaching the possessed character and the component that gathers every
// source of a stat, so a spent passive point is felt at once. Issue #1054.
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmWearing.h"
// For the pool a skill choice is made from. Issue #837 layer 3.
#include "Items/CataclysmWeaponSlotsComponent.h"
// For writing the character record when the player changes the build. Issue #2763.
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UObjectIterator.h"

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
	DOREPLIFETIME(ACataclysmPlayerState, RunKills);
	DOREPLIFETIME(ACataclysmPlayerState, FloorsClearedThisRun);
	DOREPLIFETIME(ACataclysmPlayerState, LifetimeKills);
	DOREPLIFETIME(ACataclysmPlayerState, CreationChoice);
	DOREPLIFETIME(ACataclysmPlayerState, PassiveAllocation);
	DOREPLIFETIME(ACataclysmPlayerState, DefeatedCataclysmBosses);
	DOREPLIFETIME(ACataclysmPlayerState, PointsAnAllocationWasHandedWith);
	DOREPLIFETIME(ACataclysmPlayerState, SkillAssignment);
	DOREPLIFETIME(ACataclysmPlayerState, RememberedDefaults);
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

	// AND THE WEAPON CREATION GAVE CARRIES IT. Issue #857 layer 1.
	CarryTheChosenTypeOnTheCreationWeapon();

	// A CHANGED DAMAGE TYPE IS A CHANGED STAT LINE. Issue #1055. The damage type
	// decides which passive trees apply at all -- points in a tree no equipped
	// weapon reaches stay spent and grant nothing, which is the project owner's
	// decision of 2026-08-25 -- and `UCataclysmEquipmentComponent::
	// RefreshAttributes` reads what the worn weapons carry to enforce it, the
	// creation weapon's type among them (issue #857 layer 1). Writing the
	// choice without re-running the line leaves the old damage type's trees on
	// the character's stats while the passive tree screen shows the new one's.
	//
	// IT LOOKED CORRECT BECAUSE CHANGING THE WEAPON HID IT.
	// `ACataclysmPlayerCharacter::ApplyCreationChoice` puts the chosen weapon on,
	// that broadcasts `EquipmentChanged`, and the refresh happened by accident.
	// The one path with no accident to rely on is a choice that changed only the
	// damage type, which `ApplyCreationChoice` returns early from by name.
	RefreshCharacterStats();
	return true;
}

void ACataclysmPlayerState::SetIdentity(const FString& InCharacterName, const FCataclysmCharacterPartition& InPartition)
{
	CharacterName = InCharacterName;
	Partition = InPartition;
	bHasIdentity = true;
}

void ACataclysmPlayerState::SetDifficultyTier(int32 Tier)
{
	DifficultyTier = FMath::Clamp(Tier, ACataclysmGameMode::LowestDifficultyTier, ACataclysmGameMode::HighestDifficultyTier);
}

int32 ACataclysmPlayerState::RaiseDifficultyTierForAWin()
{
	SetDifficultyTier(DifficultyTier + 1); // A won run's: one tier higher.
	return DifficultyTier;
}

void ACataclysmPlayerState::SetCreationChoice(FName WeaponType, FName DamageType)
{
	CreationChoice.WeaponType = WeaponType;
	CreationChoice.DamageType = DamageType;

	// AND THE WEAPON CREATION GAVE CARRIES IT, on a standing character. Before
	// possession there is no pawn and nothing to change: possession's
	// `ACataclysmPlayerCharacter::ApplyCreationChoice` does it then.
	CarryTheChosenTypeOnTheCreationWeapon();

	// THE SAME REASON, ON THE SAVE-RESTORE PATH. This runs before possession
	// when a save is being loaded, in which case `RefreshCharacterStats` finds
	// no possessed character and does nothing, and possession applies the whole
	// line a moment later. It runs after possession when anything else writes a
	// choice onto a standing character, and then it is the only thing that
	// would. `SetPassiveAllocation` below carries the same pair of cases for
	// issue #1054.
	RefreshCharacterStats();
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

const TCHAR* ACataclysmPlayerState::ExperienceGainStat = TEXT("experience_gain");

const TCHAR* ACataclysmPlayerState::SkillExperienceGainStat = TEXT("skill_experience_gain");

double ACataclysmPlayerState::SkillExperienceGainFactor() const
{
	const UCataclysmAbilitySystemComponent* Earner =
		Cast<const UCataclysmAbilitySystemComponent>(GetAbilitySystemComponent());
	if (!Earner)
	{
		return 1.0;
	}
	// NO SKILL TAGS: every gem gives the whole character its bonus, and the ruling of 2026-11-21 (issue #2803) is that
	// this one raises every skill's share alike.
	return FMath::Max(0.0, static_cast<double>(Earner->StatAppliedTo(
							   FName(SkillExperienceGainStat), FGameplayTagContainer(), 100.0f)) / 100.0);
}

int64 ACataclysmPlayerState::ExperienceAfterGain(int32 KillScore) const
{
	const UCataclysmAbilitySystemComponent* Earner =
		Cast<const UCataclysmAbilitySystemComponent>(GetAbilitySystemComponent());
	if (!Earner)
	{
		return KillScore;
	}
	const float Share = FMath::Max(0.0f, Earner->StatAppliedTo(
		FName(ExperienceGainStat), FGameplayTagContainer(), 100.0f)) / 100.0f;
	// UNCHANGED TO THE POINT WHEN NOTHING MOVES IT, so no rounding reaches a
	// character without the row.
	return FMath::IsNearlyEqual(Share, 1.0f)
		? static_cast<int64>(KillScore)
		: static_cast<int64>(FMath::FloorToDouble(static_cast<double>(KillScore) * Share));
}

int32 ACataclysmPlayerState::GrantExperience(int64 Amount)
{
	// GRANTING NOTHING CHANGES NOTHING, INCLUDING THE LEVEL, and that is worth
	// a branch of its own rather than leaving it to the clamp below. A kill
	// worth zero or less is a real case -- a Common creature on the first floors
	// of a deep dungeon scores below zero (`UCataclysmEnemyScore::ScoreFor`) --
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
	const int32 LevelBefore = Level;
	// THE SKILLS' LEVELS BEFORE, because a character level can let a skill's kept experience through its cap. #2764.
	const TMap<FName, int32> SkillsBefore = SkillLevelsNow();
	const int32 Gained =
		UCataclysmExperience::Grant(Amount, Level, ExperienceIntoLevel);
	CharacterLevel = Level;

	// A LEVEL GAINED REBUILDS THE STAT LINE, HERE, whoever granted it. Issue #2744, ruled by the coordinating session
	// on 2026-11-18. A level moves every per-level term in the class table; until this only the console command
	// `Cataclysm.GrantExperience` rebuilt the line, so a level gained from a kill (`ACataclysmEnemyCharacter`'s award)
	// left the character on the old level's health, mana and damage until it next changed gear or spent a point. The
	// pools are LEFT WHERE THEY ARE, as every refresh leaves them: a raised maximum raises nothing, so a character at
	// 120 of 120 health that gains a level stands at 120 of the new maximum.
	//
	// AND ONE PLAY LOOP LINE SAYS IT, after the level is stored, so its figures are the new ones.
	if (Gained > 0)
	{
		RefreshCharacterStats(); // A level gained's: the stat line at the new level.
		FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::LevelGainedLine(LevelBefore, CharacterLevel,
																		  ExperienceIntoLevel));
		// AND A SKILL WHOSE KEPT EXPERIENCE THE NEW LEVEL LETS THROUGH SAYS ITS OWN LINE. Issue #2764.
		SaySkillLevelsGained(SkillsBefore);
		RepriceReservesForLevelsGained(SkillsBefore); // A character level can lift a capped skill's. Issue #2802.
	}
	return Gained;
}

void ACataclysmPlayerState::SetLevelAndExperience(int32 NewLevel,
												  int64 NewExperience)
{
	const TMap<FName, int32> SkillsBefore = SkillLevelsNow(); // Issue #2802: re-priced below for any level this lifts.
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
	RepriceReservesForLevelsGained(SkillsBefore); // Issue #2802.
}

void ACataclysmPlayerState::NoteFloorBegan(float WorldSeconds)
{
	FloorBeganAtSeconds = FMath::Max(0.0f, WorldSeconds);
}

float ACataclysmPlayerState::SecondsOnFloor(float WorldSeconds) const
{
	return FloorBeganAtSeconds < 0.0f ? -1.0f : FMath::Max(0.0f, WorldSeconds - FloorBeganAtSeconds);
}

void ACataclysmPlayerState::NoteFloorCleared()
{
	FloorsClearedThisRun = FloorsClearedThisRun < MAX_int32 ? FloorsClearedThisRun + 1 : FloorsClearedThisRun;
}

void ACataclysmPlayerState::NoteKill()
{
	// HELD AT THE LARGEST COUNT RATHER THAN WRAPPING, because a count that
	// wrapped negative would read to every kill scale as "no player state".
	RunKills = RunKills < MAX_int32 ? RunKills + 1 : RunKills;
	LifetimeKills = LifetimeKills < MAX_int32 ? LifetimeKills + 1 : LifetimeKills;
}

void ACataclysmPlayerState::SetLifetimeKills(int32 Kills)
{
	LifetimeKills = FMath::Max(0, Kills);
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

	// AND THE CHARACTER RECORD IS WRITTEN, which holds the points. Issue #2763: until this a point spent waited for
	// the next item, death or trip to reach the disk, and stopping play first lost it.
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::AttributePointLine(Count, Attribute,
		SpentAttributePoints.PointsIn(Attribute), AttributePointsUnspent()));
	UCataclysmSaveWriter::NoteTriggerIn(GetWorld(), ECataclysmSaveTrigger::CharacterBuildChanged);
	return true;
}

void ACataclysmPlayerState::ResetAttributePoints()
{
	const int32 PointsBack = SpentAttributePoints.Total();
	SpentAttributePoints = FCataclysmAttributePoints();

	// AND THE CHARACTER RECORD IS WRITTEN, for `SpendAttributePoints`' reason. Issue #2763.
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::AttributeResetLine(PointsBack, AttributePointsUnspent()));
	UCataclysmSaveWriter::NoteTriggerIn(GetWorld(), ECataclysmSaveTrigger::CharacterBuildChanged);
}

// ---------------------------------------------------------------------------
// Passive points
// ---------------------------------------------------------------------------

int32 ACataclysmPlayerState::PassivePointsAvailable() const
{
	return PassivePointsEarnedWith(PassivePointsFromGear());
}

int32 ACataclysmPlayerState::PassivePointsEarnedWith(int32 PointsFromGear) const
{
	// AND THE POINTS AN ALLOCATION WAS HANDED WITH. Ruled 2026-10-10. Nought
	// unless `SetPassiveAllocation` was handed more points than the level and
	// the bosses of that call had earned. It is a fixed number, so it pays for
	// that many points and for no point spent afterwards.
	return UCataclysmPassivePoints::Available(GetCharacterLevel(),
											  DefeatedCataclysmBosses.Num())
		+ PointsAnAllocationWasHandedWith
		+ FMath::Max(0, PointsFromGear);
}

int32 ACataclysmPlayerState::PassivePointsFromGear() const
{
	// ASKED OF THE PAWN'S EQUIPMENT COMPONENT, WHICH KEEPS WHAT ITS LAST
	// REFRESH READ. Ruled 2026-10-09. `ClassPointsGranted` on that component
	// says why the figure is kept there.
	//
	// NOUGHT WITH NO PAWN, and that is the point of asking the pawn: this
	// player state outlives its pawn, and gear on a pawn that no longer exists
	// grants nothing. Nought also before the pawn's first refresh.
	const ACataclysmPlayerCharacter* Character =
		Cast<ACataclysmPlayerCharacter>(GetPawn());
	const UCataclysmEquipmentComponent* Equipment =
		Character ? Character->GetEquipment() : nullptr;
	return Equipment ? Equipment->ClassPointsGranted() : 0;
}

int32 ACataclysmPlayerState::PassivePointsUnspent() const
{
	// NEVER BELOW NOUGHT. Since 2026-10-09 more points can be spent than are
	// earned, when gear that granted points has come off. Every reader of this
	// prints it as points left to spend, and such a character has none.
	// `PassivePointsAddingNothing` is the other half of the subtraction.
	return FMath::Max(0, PassivePointsAvailable() - PassiveAllocation.Total());
}

int32 ACataclysmPlayerState::PassivePointsAddingNothing() const
{
	return UCataclysmPassiveTree::PointsAddingNothing(PassiveAllocation,
													  PassivePointsAvailable());
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

TArray<FName> ACataclysmPlayerState::CarriedDamageTypes() const
{
	// ASKED OF THE PAWN'S EQUIPMENT, as `PassivePointsFromGear` is and for its
	// reason: this player state outlives its pawn. With no pawn the creation
	// type alone, which is also what the component answers for nothing worn.
	const ACataclysmPlayerCharacter* Character =
		Cast<ACataclysmPlayerCharacter>(GetPawn());
	const UCataclysmEquipmentComponent* Equipment =
		Character ? Character->GetEquipment() : nullptr;
	return Equipment ? Equipment->CarriedDamageTypes(GetChosenDamageType())
					 : TArray<FName>{GetChosenDamageType()};
}

TArray<FString> ACataclysmPlayerState::ReachableTrees() const
{
	// THE DAMAGE TYPES THE WORN WEAPONS CARRY, since issue #857 layer 1. The
	// design says what a character has access to "is determined by the set of
	// damage types across ALL equipped weapons". This passed the creation type
	// alone until then.
	return UCataclysmPassiveTree::ReachableTrees(
		UCataclysmPassiveTree::LoadNodeTable(), CarriedDamageTypes());
}

bool ACataclysmPlayerState::ReachesTreeOf(const UDataTable* NodeTable,
										 FName Node, FString& OutReason) const
{
	// THE CHARACTER'S OWN QUESTION, ASKED BEFORE THE TREE'S RULES. Which trees
	// are reachable follows from the damage type this character carries, which
	// `UCataclysmPassiveTree` deliberately knows nothing about. Asking it first
	// also means the reason a player sees is the useful one: "your weapon does
	// not reach that tree" rather than "that node is shut".
	const FString Tree = UCataclysmPassiveTree::TreeOf(NodeTable, Node);
	if (Tree.IsEmpty())
	{
		// A NODE IN NO TREE, which is a node the table does not have. The tree's
		// own rules say so more plainly a moment later.
		return true;
	}

	// THE WORN WEAPONS' TYPES, for this check and for the class check below.
	// Issue #857 layer 1.
	const TArray<FName> Carried = CarriedDamageTypes();
	if (!UCataclysmPassiveTree::TreeIsReachable(Tree, Carried))
	{
		OutReason = FString::Printf(
			TEXT("No equipped weapon carries a damage type that unlocks the "
				 "%s tree. Your weapons carry %s."),
			*Tree,
			*FString::JoinBy(Carried, TEXT(" and "),
							 [](const FName& Type) { return Type.ToString(); }));
		return false;
	}

	// AND ONLY ONE OF A DAMAGE TYPE'S THREE TREES. Issue #2064. Asked here
	// because this is the gate both ways of putting a point in share, spending
	// one and choosing a capstone option.
	const FString NotYourClass = UCataclysmPassiveTree::RefusalForClassChoice(
		NodeTable, PassiveAllocation, Tree, Carried);
	if (!NotYourClass.IsEmpty())
	{
		OutReason = NotYourClass;
		return false;
	}
	return true;
}

bool ACataclysmPlayerState::SpendPassivePoint(FName Node, FString& OutReason)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* EdgeTable = UCataclysmPassiveTree::LoadEdgeTable();

	if (!ReachesTreeOf(NodeTable, Node, OutReason))
	{
		return false;
	}

	if (!UCataclysmPassiveTree::Spend(NodeTable, EdgeTable, PassiveAllocation,
									  Node, PassivePointsAvailable(), OutReason))
	{
		return false;
	}

	// AND THE CHARACTER FEELS IT NOW. Issue #1054. Without this the point is
	// recorded and changes nothing until the player happens to equip something,
	// spend an attribute point or gain a level.
	RefreshCharacterStats();

	// AND THE CHARACTER RECORD IS WRITTEN, which holds the tree. Issue #2763: the project owner spent points in a
	// dungeon on 2026-11-19, stopped play, and their record held none of them.
	const FCataclysmPassiveNodeRow* Row = UCataclysmPassiveTree::FindNode(NodeTable, Node);
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::PassivePointLine(
		Row ? Row->NodeName : Node.ToString(), UCataclysmPassiveTree::TreeOf(NodeTable, Node),
		PassiveAllocation.PointsIn(Node), Row ? Row->MaxPoints : 0, PassiveAllocation.Total(),
		PassivePointsAvailable()));
	UCataclysmSaveWriter::NoteTriggerIn(GetWorld(), ECataclysmSaveTrigger::CharacterBuildChanged); // A point's write.
	return true;
}

bool ACataclysmPlayerState::ChoosePassiveOption(FName Node, int32 Option,
												FString& OutReason)
{
	const UDataTable* NodeTable = UCataclysmPassiveTree::LoadNodeTable();

	// REFUSED FOR AN UNREACHABLE TREE, WHICH IT WAS NOT UNTIL ISSUE #1075. This
	// act now spends a point, so it has to obey the rule spending obeys.
	if (!ReachesTreeOf(NodeTable, Node, OutReason))
	{
		return false;
	}

	// BOTH ACTS ON A COPY, AND THE COPY IS KEPT ONLY IF BOTH SUCCEED. Issue
	// #1075. The choice is permanent, so a character that made it and then found
	// it had no point left would be committed for ever to an option it could not
	// turn on. Nothing is written to this character until both have passed.
	FCataclysmPassiveAllocation Attempt = PassiveAllocation;

	if (!UCataclysmPassiveTree::ChooseOption(NodeTable, Attempt, Node, Option,
											 OutReason))
	{
		return false;
	}

	// AND THE POINT GOES IN WITH THE CHOICE. Issue #1075. Choosing used to
	// record the decision and nothing else, and a capstone holding a choice but
	// no point grants NOTHING: `UCataclysmPassiveTree::AccumulateInto` skips a
	// node with no points before it ever reads the chosen option. The project
	// owner took Water to Blood in play, was told the option was picked, and
	// still had a mana pool and abilities that still cost mana.
	//
	// UNLESS THE NODE ALREADY HOLDS ITS POINT, which is the ordinary case for a
	// player clicking the option that is already marked as taken, and for every
	// caller written before this changed. Charging a second point there would
	// refuse an act that used to succeed and that does no harm.
	const FCataclysmPassiveNodeRow* Row =
		UCataclysmPassiveTree::FindNode(NodeTable, Node);
	const bool bNeedsThePoint = Row && Attempt.PointsIn(Node) < Row->MaxPoints;

	if (bNeedsThePoint
		&& !UCataclysmPassiveTree::Spend(
			   NodeTable, UCataclysmPassiveTree::LoadEdgeTable(), Attempt, Node,
			   PassivePointsAvailable(), OutReason))
	{
		return false;
	}

	PassiveAllocation = Attempt;

	// A CHOICE TURNS ON THAT OPTION'S EFFECT ROWS, so it moves the stat line for
	// the same reason spending a point does. Issue #1054.
	RefreshCharacterStats();

	// AND THE CHARACTER RECORD IS WRITTEN, for `SpendPassivePoint`'s reason. Issue #2763. The option by its name,
	// or its number when the row names none.
	const TArray<FString> Options = Row ? UCataclysmPassiveTree::OptionNamesOf(*Row) : TArray<FString>();
	const FString Taken = Options.IsValidIndex(Option - 1) && !Options[Option - 1].IsEmpty()
		? Options[Option - 1]
		: FString::Printf(TEXT("%d"), Option);
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::PassiveOptionLine(
		Taken, Row ? Row->NodeName : Node.ToString(), PassiveAllocation.Total(), PassivePointsAvailable()));
	UCataclysmSaveWriter::NoteTriggerIn(GetWorld(), ECataclysmSaveTrigger::CharacterBuildChanged);
	return true;
}

void ACataclysmPlayerState::ResetPassivePoints()
{
	// THE CAPSTONE CHOICES GO WITH THE POINTS. Each capstone's own description
	// ends "The choice is permanent", and a respec that returned the points
	// while leaving the capstone decisions made would not be a respec at all.
	const int32 PointsBack = PassiveAllocation.Total();
	PassiveAllocation.Clear();

	// AND THE POINTS AN ALLOCATION WAS HANDED WITH. Ruled 2026-10-10. The figure
	// pays for points of one handed allocation, and that allocation is now
	// empty. Left standing it would be points to spend that nothing earned.
	PointsAnAllocationWasHandedWith = 0;

	// AND EVERYTHING THE TREE WAS GRANTING GOES WITH THEM. Issue #1054. A respec
	// that left the bonuses standing would be free power. Maximum health falling
	// clamps current health down to it, which `UCataclysmVitalAttributeSet`
	// already does for any other way of losing it.
	RefreshCharacterStats();

	// AND THE CHARACTER RECORD IS WRITTEN, for `SpendPassivePoint`'s reason. Issue #2763.
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::PassiveResetLine(PointsBack, PassivePointsUnspent()));
	UCataclysmSaveWriter::NoteTriggerIn(GetWorld(), ECataclysmSaveTrigger::CharacterBuildChanged);
}

void ACataclysmPlayerState::SetPassiveAllocation(
	const FCataclysmPassiveAllocation& Allocation, const TArray<FName>& Bosses)
{
	PassiveAllocation = Allocation;
	DefeatedCataclysmBosses = Bosses;

	// THE POINTS OF THIS ALLOCATION THAT THE LEVEL AND THESE BOSSES DO NOT PAY
	// FOR ARE RECORDED, AND COUNT AS EARNED. Ruled 2026-10-10. Since 2026-10-09
	// points spent above the points earned add nothing, and this function is
	// handed allocations nothing checked. Without the figure the late nodes of
	// such an allocation would be left off the stat line.
	//
	// GEAR IS NOT COUNTED, so the figure does not depend on what the character
	// wears when the allocation is handed over. REPLACED BY EVERY CALL, never
	// added to: it describes the allocation handed in this call and no other.
	//
	// IT DOES NOT TURN THE RULE OFF. Points spent after this call with points
	// that worn gear grants are above the points earned when that gear comes
	// off, whatever this figure is.
	PointsAnAllocationWasHandedWith = FMath::Max(
		0, Allocation.Total()
			   - UCataclysmPassivePoints::Available(GetCharacterLevel(),
													Bosses.Num()));

	// A HANDED ALLOCATION IS A CHANGED ONE. Issue #1054. Called before
	// possession this does nothing, and possession applies the whole line a
	// moment later; called after possession, on a standing character, it is the
	// only thing that would.
	RefreshCharacterStats();
}

void ACataclysmPlayerState::RefreshCharacterStats() const
{
	// THROUGH THE EQUIPMENT COMPONENT, WHICH IS WHERE EVERY SOURCE OF A STAT IS
	// GATHERED. `UCataclysmEquipmentComponent::RefreshAttributes` reads the
	// class line, the worn items, the spent attribute points and the passive
	// tree together. Working the passive part out again here would be a second
	// place holding the same knowledge, and that duplication is what issue #1054
	// was.
	const ACataclysmPlayerCharacter* Character =
		Cast<ACataclysmPlayerCharacter>(GetPawn());
	if (!Character || !Character->GetEquipment())
	{
		return;
	}

	Character->GetEquipment()->RefreshAttributes(
		GetCataclysmAbilitySystemComponent());

	// AND A SECOND TWO-HANDED WEAPON GOES BACK WHEN BOTH HANDS FULL HAS GONE.
	// Issue #1515. Every change to the allocation arrives here, a respec among
	// them, and the stat line just refreshed is what says whether the option is
	// still held. It goes to the bag, or on the floor in front of the character
	// when the bag is full; the respec is never refused. Ruled 2026-09-24.
	UCataclysmWearing::ReturnSecondTwoHandedWeapon(
		Character->GetInventory(), Character->GetEquipment(), Character->GetWorld(),
		UCataclysmDropSpawner::DropSpotInFrontOf(*Character));
}

void ACataclysmPlayerState::StartingLevelChanged()
{
	const int32 Asked = UCataclysmPlayerClassStats::ChosenLevel();
	for (TObjectIterator<ACataclysmPlayerState> It; It; ++It)
	{
		const ACataclysmPlayerState* State = *It;
		if (!IsValid(State) || State->IsTemplate()
			|| !Cast<ACataclysmPlayerCharacter>(State->GetPawn()))
		{
			continue;
		}
		// NOT IN A WORLD THAT HAS BEEN DESTROYED. `UWorld::DestroyWorld` cleans
		// the world up, which clears `bIsWorldInitialized`, but leaves its actors
		// valid until garbage collection: window w87 saw a destroyed test world's
		// player state answer a change made after it.
		const UWorld* World = State->GetWorld();
		if (!World || World->bIsTearingDown || !World->bIsWorldInitialized)
		{
			continue;
		}

		if (State->HasSettledLevel())
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Cataclysm.PlayerLevel is now %d, but the character in play has "
					 "settled its level at %d, so nothing changed. The variable sets "
					 "only the starting level of a character that has not gained or "
					 "loaded one."),
				Asked, State->GetCharacterLevel());
			continue;
		}

		State->RefreshCharacterStats();
		UE_LOG(LogCataclysm, Log,
			TEXT("Cataclysm.PlayerLevel applied at level %d: the character in play "
				 "has not settled a level, so its stat line was resolved again at "
				 "this one."),
			Asked);
	}
}

void ACataclysmPlayerState::CarryTheChosenTypeOnTheCreationWeapon()
{
	// NOTHING WITHOUT A POSSESSED CHARACTER, for the reason `RefreshCharacterStats`
	// gives. The caller refreshes the stat line next, so nothing is refreshed here.
	const ACataclysmPlayerCharacter* Character =
		Cast<ACataclysmPlayerCharacter>(GetPawn());
	if (Character && Character->GetEquipment())
	{
		Character->GetEquipment()->CarryCreationDamageType(GetChosenWeaponType(),
														   GetChosenDamageType());
	}
}

FString ACataclysmPlayerState::SkillSlotName(ECataclysmAbilitySlot Slot)
{
	// THE TAG'S LEAF, as `UCataclysmWeaponSkills::SlotFromName` reads one, so the console spells a key the way the
	// workbook does. Issue #837 layer 3.
	FString Leaf = CataclysmAbilitySlots::Tag(Slot).ToString();
	int32 Dot = INDEX_NONE;
	if (Leaf.FindLastChar(TEXT('.'), Dot))
	{
		Leaf = Leaf.RightChop(Dot + 1);
	}
	return Leaf;
}

FString ACataclysmPlayerState::RefusalForSkillChoice(ECataclysmAbilitySlot Slot, FName SkillRow, const UDataTable* Table,
													 const TArray<FCataclysmWeaponSkill>& Pool,
													 const TArray<FCataclysmSkillChoice>& Choices)
{
	// ONLY THE SIX (O2, ruled 2026-10-25): the basic attack comes from the weapon.
	if (Slot == ECataclysmAbilitySlot::BasicAttack)
	{
		return TEXT("The basic attack comes from the weapon and cannot be chosen.");
	}
	if (!UCataclysmWeaponSkills::IsChoosableSlot(Slot))
	{
		return TEXT("That is not one of the six skill slots: Heavy, Special, Support, Aura, Ultimate and Movement.");
	}
	if (SkillRow.IsNone())
	{
		return TEXT("Name the skill by its row, as Cataclysm.ShowSkills lists it.");
	}

	// BY THE SKILL'S NAME, NEVER ITS ROW (ruled Q5 of layer 4, 2026-10-25: "A player-facing screen must not show row
	// names"). The pool's name, else the table row's `SkillName`; the row name only for a row with no name.
	const auto NameOf = [&Pool, Table](FName Row)
	{
		const FCataclysmWeaponSkill* InPool =
			Pool.FindByPredicate([Row](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == Row; });
		const FCataclysmWeaponSkillRow* InTable = InPool || !Table ? nullptr
			: Table->FindRow<FCataclysmWeaponSkillRow>(Row, TEXT(""), /*bWarnIfRowMissing=*/false);
		const FString Named = InPool ? InPool->Name : (InTable ? InTable->SkillName : FString());
		return Named.IsEmpty() ? Row.ToString() : Named;
	};

	// FROM THE POOL THE WORN WEAPONS MAKE NOW, naming what they offer.
	if (!Pool.ContainsByPredicate([SkillRow](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == SkillRow; }))
	{
		// A ROW THE TABLE DOES NOT HAVE HAS NO NAME, so it is named as it was typed.
		if (Table && !Table->FindRowUnchecked(SkillRow))
		{
			return FString::Printf(TEXT("There is no skill row called %s."), *SkillRow.ToString());
		}
		const FString Offered = FString::JoinBy(Pool, TEXT(", "),
			[&NameOf](const FCataclysmWeaponSkill& Skill) { return NameOf(Skill.RowName); });
		return FString::Printf(TEXT("%s is not in your skill pool. Your weapons offer: %s."), *NameOf(SkillRow),
							   Offered.IsEmpty() ? TEXT("nothing") : *Offered);
	}

	// A ROW ALREADY CHOSEN FOR ANOTHER KEY IS REFUSED, NAMING IT, AND NOT MOVED (O4).
	for (const FCataclysmSkillChoice& Choice : Choices)
	{
		if (Choice.SkillRow != SkillRow)
		{
			continue;
		}
		if (Choice.Slot == Slot)
		{
			return FString::Printf(TEXT("%s is already chosen for the %s slot."), *NameOf(SkillRow),
								   *SkillSlotName(Slot));
		}
		return FString::Printf(TEXT("%s is already chosen for the %s slot. Clear that slot first."),
							   *NameOf(SkillRow), *SkillSlotName(Choice.Slot));
	}
	return FString();
}

void ACataclysmPlayerState::RestorePassiveAllocation(const FCataclysmPassiveAllocation& Allocation,
													const TArray<FName>& Bosses, int32 HandedWith)
{
	PassiveAllocation = Allocation;
	DefeatedCataclysmBosses = Bosses;
	PointsAnAllocationWasHandedWith = FMath::Max(0, HandedWith);
}

void ACataclysmPlayerState::RestoreSpentAttributePoints(const FCataclysmAttributePoints& Points)
{
	SpentAttributePoints = Points;
}

void ACataclysmPlayerState::RestoreRunKills(int32 Kills)
{
	RunKills = FMath::Max(0, Kills);
}

bool ACataclysmPlayerState::AssignSkill(ECataclysmAbilitySlot Slot, FName SkillRow, FString& OutReason)
{
	ACataclysmPlayerCharacter* Character = Cast<ACataclysmPlayerCharacter>(GetPawn());
	const UCataclysmWeaponSlotsComponent* Slots =
		Character ? Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
	const TArray<FCataclysmWeaponSkill> Pool = Slots ? Slots->GetSkillPool() : TArray<FCataclysmWeaponSkill>();
	OutReason = RefusalForSkillChoice(Slot, SkillRow, UCataclysmWeaponSkills::LoadGeneratedTable(), Pool,
									  SkillAssignment);
	if (!OutReason.IsEmpty())
	{
		return false;
	}

	// ONE CHOICE PER KEY: a new one for a key replaces the old.
	if (FCataclysmSkillChoice* Existing = SkillAssignment.FindByPredicate(
			[Slot](const FCataclysmSkillChoice& Choice) { return Choice.Slot == Slot; }))
	{
		Existing->SkillRow = SkillRow;
	}
	else
	{
		FCataclysmSkillChoice& Added = SkillAssignment.AddDefaulted_GetRef();
		Added.Slot = Slot;
		Added.SkillRow = SkillRow;
	}

	// AT ONCE, so the key casts it now.
	if (Character)
	{
		Character->RefillAbilitySlots();
	}
	return true;
}

bool ACataclysmPlayerState::ClearSkillChoice(ECataclysmAbilitySlot Slot)
{
	// FORGOTTEN, NOT EMPTIED (O5, the owner: "Forget my choice"): the key returns to its default.
	const int32 Removed = SkillAssignment.RemoveAll(
		[Slot](const FCataclysmSkillChoice& Choice) { return Choice.Slot == Slot; });
	if (ACataclysmPlayerCharacter* Character = Cast<ACataclysmPlayerCharacter>(GetPawn()))
	{
		Character->RefillAbilitySlots();
	}
	return Removed > 0;
}

bool ACataclysmPlayerState::ChooseSkill(ECataclysmAbilitySlot Slot, FName SkillRow, FString& OutReason)
{
	// THE PLAYER'S ACT, AND NOT `AssignSkill`, RAISES THE SAVE: a load calls `AssignSkill` to put saved choices back,
	// and a load writing back what it has just read would be a write for nothing. Issue #2763, ruled by the
	// coordinating session on 2026-11-19.
	if (!AssignSkill(Slot, SkillRow, OutReason))
	{
		return false;
	}

	// BY THE SKILL'S NAME, NEVER ITS ROW, as the screen names it; the row only for a row with no name.
	const ACataclysmPlayerCharacter* Character = Cast<ACataclysmPlayerCharacter>(GetPawn());
	const UCataclysmWeaponSlotsComponent* Slots =
		Character ? Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
	const FCataclysmWeaponSkill* InPool = Slots
		? Slots->GetSkillPool().FindByPredicate(
			  [SkillRow](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == SkillRow; })
		: nullptr;
	const FString Named = InPool && !InPool->Name.IsEmpty() ? InPool->Name : SkillRow.ToString();

	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::SkillChosenLine(Named, SkillSlotName(Slot)));
	UCataclysmSaveWriter::NoteTriggerIn(GetWorld(), ECataclysmSaveTrigger::CharacterBuildChanged);
	return true;
}

bool ACataclysmPlayerState::ForgetSkillChoice(ECataclysmAbilitySlot Slot)
{
	// NOTHING TO FORGET, NOTHING SAID OR WRITTEN: the record already holds no choice for the key.
	if (!ClearSkillChoice(Slot))
	{
		return false;
	}
	FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::SkillForgottenLine(SkillSlotName(Slot)));
	UCataclysmSaveWriter::NoteTriggerIn(GetWorld(), ECataclysmSaveTrigger::CharacterBuildChanged);
	return true;
}

void ACataclysmPlayerState::RememberDefaults(const TArray<FCataclysmWeaponSkill>& OnKeys)
{
	// REPLACED, NEVER ADDED TO: what the keys hold now is the whole of what is remembered. A key holding the player's
	// own choice is left out, so "chosen" keeps meaning the player chose it (Q10); a key whose choice is off with its
	// weapon holds a default, and that default is remembered.
	RememberedDefaults.Reset();
	for (const FCataclysmWeaponSkill& Skill : OnKeys)
	{
		if (!UCataclysmWeaponSkills::IsChoosableSlot(Skill.Slot) || Skill.RowName.IsNone())
		{
			continue;
		}
		const bool bChosen = SkillAssignment.ContainsByPredicate([&Skill](const FCataclysmSkillChoice& Choice)
			{ return Choice.Slot == Skill.Slot && Choice.SkillRow == Skill.RowName; });
		if (bChosen)
		{
			continue;
		}
		FCataclysmSkillChoice& Remembered = RememberedDefaults.AddDefaulted_GetRef();
		Remembered.Slot = Skill.Slot;
		Remembered.SkillRow = Skill.RowName;
	}
}

void ACataclysmPlayerState::RestoreSkillChoices(const TArray<FCataclysmSkillChoice>& Choices,
												const TArray<FCataclysmSkillChoice>& Remembered, const UDataTable* Table,
												int32& OutRefused)
{
	// AS DATA, CHECKED AGAINST THE TABLE AND NOT THE POOL: a choice whose weapon was off at saving is kept (Q7), as
	// `AssignSkill` would have refused it. A key or a row named twice keeps its first entry, as the fill reads them.
	// ONLY A REFUSED CHOICE IS COUNTED: a remembered default the table no longer has is dropped and the key takes the
	// next default, as it would in play, and the player chose nothing to lose.
	const auto Kept = [Table](const TArray<FCataclysmSkillChoice>& Listed, int32& Refused)
	{
		TArray<FCataclysmSkillChoice> Out;
		for (const FCataclysmSkillChoice& Entry : Listed)
		{
			const bool bRepeated = Out.ContainsByPredicate([&Entry](const FCataclysmSkillChoice& Earlier)
				{ return Earlier.Slot == Entry.Slot || Earlier.SkillRow == Entry.SkillRow; });
			if (!UCataclysmWeaponSkills::IsChoosableSlot(Entry.Slot) || Entry.SkillRow.IsNone() || bRepeated
				|| (Table && !Table->FindRowUnchecked(Entry.SkillRow)))
			{
				++Refused;
				continue;
			}
			Out.Add(Entry);
		}
		return Out;
	};
	int32 DefaultsDropped = 0;
	SkillAssignment = Kept(Choices, OutRefused);
	RememberedDefaults = Kept(Remembered, DefaultsDropped);
}

// ---------------------------------------------------------------------------
// Skill levels. Issue #2764.
// ---------------------------------------------------------------------------

double ACataclysmPlayerState::GetSkillExperience(FName SkillRow) const
{
	const double* Held = SkillExperience.Find(SkillRow);
	return Held ? *Held : 0.0;
}

int32 ACataclysmPlayerState::SkillLevelOf(FName SkillRow) const
{
	return UCataclysmSkillLevels::LevelFor(GetSkillExperience(SkillRow), GetCharacterLevel());
}

void ACataclysmPlayerState::GrantSkillExperience(int64 KillExperience, const TMap<FName, float>& HealthRemovedBySkill,
												 float MaxHealth)
{
	if (KillExperience <= 0 || HealthRemovedBySkill.IsEmpty())
	{
		return;
	}

	// AGAINST THE CREATURE'S MAXIMUM HEALTH, OR EVERYTHING THE SKILLS REMOVED WHEN THAT IS MORE. A creature that healed
	// lost more than its maximum in all, and measured against the maximum alone its skills' shares would pass the whole
	// kill. The basic attack's share and damage no skill dealt go to no skill: they are simply not in the list.
	double Removed = 0.0;
	for (const TPair<FName, float>& Each : HealthRemovedBySkill)
	{
		Removed += FMath::Max(0.0f, Each.Value);
	}
	const double Whole = FMath::Max(static_cast<double>(MaxHealth), Removed);
	if (Whole <= 0.0)
	{
		return;
	}

	// RAISED BEFORE IT IS SHARED, by `skill_experience_gain` (issue #2803), so every skill's share rises alike. A kill
	// the character's own `experience_gain` reduced to nothing never reaches here.
	const double Raised = static_cast<double>(KillExperience) * SkillExperienceGainFactor();

	const TMap<FName, int32> Before = SkillLevelsNow();
	for (const TPair<FName, float>& Each : HealthRemovedBySkill)
	{
		if (!Each.Key.IsNone() && Each.Value > 0.0f)
		{
			SkillExperience.FindOrAdd(Each.Key) += Raised * Each.Value / Whole;
		}
	}
	SaySkillLevelsGained(Before);
	RepriceReservesForLevelsGained(Before); // Issue #2802.
}

TArray<FCataclysmSkillExperience> ACataclysmPlayerState::GetSkillExperienceRecord() const
{
	TArray<FCataclysmSkillExperience> Out;
	for (const TPair<FName, double>& Each : SkillExperience)
	{
		FCataclysmSkillExperience& Entry = Out.AddDefaulted_GetRef();
		Entry.SkillRow = Each.Key;
		Entry.Experience = Each.Value;
	}
	return Out;
}

void ACataclysmPlayerState::RestoreSkillExperience(const TArray<FCataclysmSkillExperience>& Record)
{
	SkillExperience.Reset();
	for (const FCataclysmSkillExperience& Entry : Record)
	{
		if (!Entry.SkillRow.IsNone())
		{
			SkillExperience.FindOrAdd(Entry.SkillRow) = FMath::Max(0.0, Entry.Experience);
		}
	}
}

void ACataclysmPlayerState::SetSkillExperience(FName SkillRow, double Experience)
{
	if (!SkillRow.IsNone())
	{
		const TMap<FName, int32> Before = SkillLevelsNow();
		SkillExperience.FindOrAdd(SkillRow) = FMath::Max(0.0, Experience);
		RepriceReservesForLevelsGained(Before); // Issue #2802.
	}
}

void ACataclysmPlayerState::RepriceReservesForLevelsGained(const TMap<FName, int32>& Before) const
{
	bool bAnyGained = false;
	for (const TPair<FName, double>& Each : SkillExperience)
	{
		const int32* Was = Before.Find(Each.Key);
		const int32 From = Was ? *Was : UCataclysmSkillLevels::FirstLevel;
		if (UCataclysmSkillLevels::LevelFor(Each.Value, GetCharacterLevel()) > From)
		{
			UCataclysmSkillLevels::RepriceReservesOf(this, Each.Key);
			bAnyGained = true;
		}
	}
	// AND THE MANA A HELD AURA RESERVES, AT ONCE, ruled 2026-11-27: "A skill level gained while the aura is on lowers
	// its reservation at once." Every held aura is asked again; one whose skill did not level answers what it did.
	if (bAnyGained && AbilitySystemComponent)
	{
		AbilitySystemComponent->RepriceAuraReservations();
	}
}

TMap<FName, int32> ACataclysmPlayerState::SkillLevelsNow() const
{
	TMap<FName, int32> Levels;
	for (const TPair<FName, double>& Each : SkillExperience)
	{
		Levels.Add(Each.Key, UCataclysmSkillLevels::LevelFor(Each.Value, GetCharacterLevel()));
	}
	return Levels;
}

void ACataclysmPlayerState::SaySkillLevelsGained(const TMap<FName, int32>& Before) const
{
	for (const TPair<FName, double>& Each : SkillExperience)
	{
		const int32* Was = Before.Find(Each.Key);
		const int32 From = Was ? *Was : UCataclysmSkillLevels::FirstLevel;
		const int32 To = UCataclysmSkillLevels::LevelFor(Each.Value, GetCharacterLevel());
		if (To > From)
		{
			FCataclysmPlayLoopLog::Say(FCataclysmPlayLoopLog::SkillLevelLine(SkillNameOf(Each.Key), From, To));
		}
	}
}

FString ACataclysmPlayerState::SkillNameOf(FName SkillRow) const
{
	// BY THE SKILL'S NAME, NEVER ITS ROW, as the screen names it: the pool's name, else the table row's.
	const ACataclysmPlayerCharacter* Character = Cast<ACataclysmPlayerCharacter>(GetPawn());
	const UCataclysmWeaponSlotsComponent* Slots =
		Character ? Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
	const FCataclysmWeaponSkill* InPool = Slots
		? Slots->GetSkillPool().FindByPredicate(
			  [SkillRow](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == SkillRow; })
		: nullptr;
	if (InPool && !InPool->Name.IsEmpty())
	{
		return InPool->Name;
	}
	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	const FCataclysmWeaponSkillRow* Row =
		Table ? Table->FindRow<FCataclysmWeaponSkillRow>(SkillRow, TEXT(""), /*bWarnIfRowMissing=*/false) : nullptr;
	return Row && !Row->SkillName.IsEmpty() ? Row->SkillName : SkillRow.ToString();
}
