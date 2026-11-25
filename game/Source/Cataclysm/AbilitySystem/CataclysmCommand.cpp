// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmSecondSelf.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
// For Crowned's attribute, which `ReserveFor` falls back to. Issue #2787.
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
// For healing a subjugated enemy to full when it is taken. Issue #340.
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
// A creature taken leaves the floor's list of enemies. Issue #2797.
#include "Dungeon/CataclysmDungeonGameMode.h"
// A thrall's figures are built from its kind and its commander's level at the take. Issue #2868.
#include "Character/CataclysmEnemyRarity.h"
#include "Dungeon/CataclysmEnemyStrength.h"
// The line of sight `ThingsCommandedInSightOf` traces. Issue #2792.
#include "CollisionQueryParams.h"
#include "Engine/World.h"
// For a minion type row's ThreatPercent, which decides which minion
// draws a nearby enemy off its summoner. Issue #1515.
#include "Data/CataclysmDataRows.h"
#include "AbilitySystemComponent.h"
// Cast<AController> in CommanderOf needs the whole type, not the forward
// declaration Pawn.h carries.
#include "GameFramework/Controller.h"
// For a commanded creature's walking speed, read off its commander's movement component. Issue #2793.
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "GameplayTagsManager.h"

// WHAT DOMINION GRANTS. Issue #1718: "A blow that leaves a target below 65%
// health can take it, rather than below half." The 50 it is added to is on the
// Subjugate skill's own row and is not restated here.
const TCHAR* UCataclysmCommand::PossessionThresholdBonusStat =
	TEXT("possession_threshold_bonus");

// WHAT CROWNED GRANTS. Issue #1718: "Each minion reserves 5 less Fervour, never
// less than 1." What it is subtracted from is on each skill's own row -- 30 on
// Subjugate, 10 on Summon Imp -- and is not restated here.
const TCHAR* UCataclysmCommand::MinionReserveReductionStat =
	TEXT("minion_reserve_reduction");

// AND THE PERCENT LESS A KIND RESERVES, WHICH REPLACED THE CAP BONUSES. Issue #2787: The Swarm, "Your minions reserve
// 12% reduced Fervour", and four enchantments, one of them negative.
const TCHAR* UCataclysmCommand::MinionReservePercentLessStat = TEXT("minion_reserve_percent_less");

// AND THE SHARE OF A RESERVE HELD THAT COUNTS AS SPENT: Bound Offering, the Death trees' slice C3b.
const TCHAR* UCataclysmCommand::ReservedFervourSpentPercentStat = TEXT("reserved_fervour_spent_percent");

namespace
{
	/** Whether this creature follows that commander, either way it can. */
	bool FollowsCommander(const AActor* Follower, const AActor* Commander)
	{
		// A SUMMONED MINION NAMES ITS SUMMONER. Set by `ACataclysmMinion::Spawn`,
		// which is the one route every imp, turret, ballista and mote takes.
		if (const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower))
		{
			return Minion->Summoner == Commander;
		}

		// AND A THRALL NAMES ITS COMMANDER AS ITS OWNER, which is what
		// `UCataclysmCommand::Subjugate` sets. A creature nobody has taken has
		// no owner, so this is false for every ordinary enemy in the level.
		return Follower->GetOwner() == Commander;
	}

	/**
	 * Whether this commander is the one that marked that creature.
	 *
	 * A TAG SAYS NOTHING ABOUT WHO APPLIED IT, so the running effect that granted
	 * it is asked and its context is read. Every lasting effect in this project
	 * attaches its tag through a target-tags component, which is what an
	 * owning-tags query matches -- the same query `UCataclysmDebuffs` uses to
	 * find the debuffs running on a character.
	 *
	 * MORE THAN ONE EFFECT MAY GRANT THE TAG, which is what a second cast before
	 * the first expired does, so any one of them being this commander's is
	 * enough.
	 */
	bool WasMarkedBy(const AActor* Marked, const AActor* Commander,
					 const FGameplayTag& Mark)
	{
		const UAbilitySystemComponent* Carrier =
			UCataclysmTargeting::AbilitySystemOf(Marked);
		if (!Carrier)
		{
			return false;
		}

		FGameplayTagContainer Wanted;
		Wanted.AddTag(Mark);

		for (const FActiveGameplayEffectHandle& Handle :
			 const_cast<UAbilitySystemComponent*>(Carrier)->GetActiveEffects(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Wanted)))
		{
			const FActiveGameplayEffect* Effect =
				Carrier->GetActiveGameplayEffect(Handle);
			if (Effect && Effect->Spec.GetContext().GetInstigator() == Commander)
			{
				return true;
			}
		}

		return false;
	}
}

TArray<ACataclysmMinion*> UCataclysmCommand::MinionsOfTypeCommandedBy(
	const AActor* Commander, const FString& TypeName)
{
	TArray<ACataclysmMinion*> Found;
	for (AActor* Follower : ThingsCommandedBy(Commander))
	{
		ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
		if (IsValid(Minion) && Minion->TypeName == TypeName)
		{
			Found.Add(Minion);
		}
	}

	// OLDEST FIRST, by the spawn order and not the spawn time, because two imps made in one frame share a time.
	Found.Sort([](const ACataclysmMinion& A, const ACataclysmMinion& B)
	{
		return A.SpawnOrder < B.SpawnOrder;
	});
	return Found;
}

AActor* UCataclysmCommand::LeastHealthCommandedBy(const AActor* Commander)
{
	AActor* Least = nullptr;
	float LeastHealth = 0.0f;
	for (AActor* Creature : ThingsCommandedBy(Commander))
	{
		const UAbilitySystemComponent* Its =
			UCataclysmTargeting::AbilitySystemOf(Creature);
		if (!Its)
		{
			continue;
		}
		const float Health =
			Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		// STRICTLY LESS, so the first of a tie -- the nearest -- is kept.
		if (!Least || Health < LeastHealth)
		{
			Least = Creature;
			LeastHealth = Health;
		}
	}
	return Least;
}

TArray<AActor*> UCataclysmCommand::ThingsCommandedBy(const AActor* Commander,
													 float WithinCm)
{
	TArray<AActor*> Found;
	if (!IsValid(Commander))
	{
		return Found;
	}

	const UWorld* World = Commander->GetWorld();
	if (!World)
	{
		return Found;
	}

	const FVector From = Commander->GetActorLocation();
	const float LimitSquared = WithinCm * WithinCm;

	// EVERY CHARACTER IN THE LEVEL, WHICH IS WHAT MAKES THIS WORK FOR BOTH KINDS.
	// A minion and a subjugated enemy share no class but this one, so iterating
	// the base is the only sweep that sees both. The header records why the world
	// is asked rather than a register kept.
	for (TActorIterator<ACataclysmCharacterBase> It(World); It; ++It)
	{
		AActor* Follower = *It;
		if (!IsValid(Follower) || Follower == Commander)
		{
			continue;
		}

		if (!FollowsCommander(Follower, Commander))
		{
			continue;
		}

		// A DEAD ONE IS NOT GOING TO BREAK OFF ONTO ANYTHING. Dropped here so no
		// caller has to remember that a death is recorded a tick before the actor
		// is removed.
		if (UCataclysmSkillEffects::IsDead(Follower))
		{
			continue;
		}

		if (WithinCm > 0.0f)
		{
			if (FVector::DistSquared(From, Follower->GetActorLocation())
				> LimitSquared)
			{
				continue;
			}
		}

		Found.Add(Follower);
	}

	// NEAREST FIRST, so a caller wanting one takes the front. Vesselstep trades
	// places with "a creature you command" and the row does not say which.
	Found.Sort([&From](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(From, A.GetActorLocation())
			 < FVector::DistSquared(From, B.GetActorLocation());
	});

	return Found;
}

TArray<AActor*> UCataclysmCommand::ThingsCommandedInSightOf(const AActor* Commander, float WithinCm)
{
	TArray<AActor*> Seen = ThingsCommandedBy(Commander, WithinCm);

	// `ThingsCommandedBy` ANSWERS EMPTY WITHOUT A COMMANDER OR A WORLD, so a list with anything in it has both.
	if (Seen.IsEmpty())
	{
		return Seen;
	}
	const UWorld* World = Commander->GetWorld();
	const FVector From = Commander->GetActorLocation();

	// FROM MIDDLE TO MIDDLE, ON THE VISIBILITY CHANNEL, WITH THE TWO BODIES LEFT OUT. The header says what blocks
	// the channel and what does not. `RemoveAll` keeps the order of what is left, so the nearest is still first.
	Seen.RemoveAll([World, Commander, &From](const AActor* Follower)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(CataclysmCommandedInSight), /*bInTraceComplex=*/false,
									Commander);
		Query.AddIgnoredActor(Follower);
		FHitResult Blocked;
		return World->LineTraceSingleByChannel(Blocked, From, Follower->GetActorLocation(), ECC_Visibility, Query);
	});

	return Seen;
}

AActor* UCataclysmCommand::CommanderOf(const AActor* Follower)
{
	if (!IsValid(Follower))
	{
		return nullptr;
	}

	// A SUMMONED MINION NAMES ITS SUMMONER, AND A THRALL NAMES ITS OWNER. The
	// same two cases `FollowsCommander` above tests, asked from the other side.
	if (const ACataclysmMinion* Minion = Cast<const ACataclysmMinion>(Follower))
	{
		return Minion->Summoner;
	}

	AActor* Owner = Follower->GetOwner();

	// A PAWN IS OWNED BY ITS OWN CONTROLLER, AND A CONTROLLER IS NOT A
	// COMMANDER. `APawn::PossessedBy` calls `SetOwner(NewController)` --
	// Engine/Source/Runtime/Engine/Private/Pawn.cpp -- so every possessed
	// character in the level has a non-null owner and almost none of them
	// follows anybody. Without this, this function answered every ordinary
	// monster's own AI controller.
	//
	// IT WENT UNNOTICED BECAUSE NOTHING ASKED WHETHER THE ANSWER WAS NULL.
	// `OrderedTargetFor` below passes the answer to `QuarryOf`, which sweeps
	// the level for an enemy marked by that actor and finds none, so a wrong
	// answer cost a level sweep per monster per thinking pass rather than a
	// visible fault. Issue #1517 added the first caller that cares: a
	// creature with a commander walks back to it, and every monster in the
	// game walked toward its own controller. Six tests said so.
	//
	// A THRALL IS STILL FOUND. `Subjugate` calls `SetOwner(Commander)` after
	// possession has already happened, so a taken creature's owner is the
	// character that took it rather than its controller.
	if (Cast<AController>(Owner))
	{
		return nullptr;
	}

	return Owner;
}

AActor* UCataclysmCommand::TopCommanderOf(const AActor* Follower)
{
	// UP THE CHAIN ONE `CommanderOf` AT A TIME, the question every other reader of command asks, so a thrall's owner and
	// a minion's summoner are each one step and nothing new decides who commands whom.
	AActor* Top = nullptr;
	const AActor* At = Follower;
	for (int32 Link = 0; Link < MostLinksOfCommand; ++Link)
	{
		AActor* Above = CommanderOf(At);
		if (!Above)
		{
			// `At` ANSWERS TO NOBODY, so it is the top: null when `At` is the follower itself.
			return Top;
		}
		if (!IsValid(Above))
		{
			// A COMMANDER THAT HAS BEEN DESTROYED: the chain stops at nobody who is still here. See the header.
			return nullptr;
		}
		Top = Above;
		At = Above;
	}

	// LONGER THAN ANY CHAIN THE GAME MAKES, which only a loop could be. Nobody stands at the top of a loop.
	return nullptr;
}

float UCataclysmCommand::CommandedWalkSpeedCmPerSecond(const AActor* Follower)
{
	// A DEPLOYED GADGET NEVER WALKS, COMMANDED OR NOT. A turret, a ballista and a trap stay where they are put, and
	// `ACataclysmEnemyController::FollowCommander` refuses them before it asks who commands them; this refuses them
	// in the same order, so nothing gives a gadget a speed.
	const ACataclysmCharacterBase* Body = Cast<const ACataclysmCharacterBase>(Follower);
	if (!Body || Body->StaysWhereItIsPut())
	{
		return 0.0f;
	}

	// NULL FOR EVERY ORDINARY MONSTER, which is what leaves every creature nobody has taken walking as it was designed.
	const ACharacter* Commander = Cast<const ACharacter>(CommanderOf(Follower));
	const UCharacterMovementComponent* Movement = Commander ? Commander->GetCharacterMovement() : nullptr;
	if (!Movement || Movement->MaxWalkSpeed <= 0.0f)
	{
		return 0.0f;
	}

	// AND RAISED BY THE COMMANDER'S `minion_movement_speed`, Hasty Dead (the Death trees' slice C6d1, 2026-11-25): one plus
	// its increases, times its more rows, with the follower's own tags, as every minion stat is read.
	const ACataclysmMinion* Minion = Cast<const ACataclysmMinion>(Follower);
	return Movement->MaxWalkSpeed * CommandedWalkSpeedShareOfCommander
		* SummonerMultiplierFor(Commander, TEXT("minion_movement_speed"),
								Minion ? Minion->TypeTags : ThrallMinionTags());
}

FGameplayTag UCataclysmCommand::QuarryTag()
{
	// Requested by name rather than declared natively, for the reason
	// `UCataclysmSkillEffects::BurnTag` gives: a native declaration would create
	// the tag whether or not the workbook still lists it, hiding exactly the
	// disagreement that matters.
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Status.Debuff.Quarry")), /*ErrorIfNotFound=*/false);
}

AActor* UCataclysmCommand::QuarryOf(const AActor* Commander)
{
	if (!IsValid(Commander))
	{
		return nullptr;
	}

	const UWorld* World = Commander->GetWorld();
	const FGameplayTag Quarry = QuarryTag();
	if (!World || !Quarry.IsValid())
	{
		return nullptr;
	}

	const FVector From = Commander->GetActorLocation();

	AActor* Nearest = nullptr;
	float NearestSquared = TNumericLimits<float>::Max();

	for (TActorIterator<ACataclysmCharacterBase> It(World); It; ++It)
	{
		AActor* Marked = *It;
		if (!IsValid(Marked) || Marked == Commander
			|| !UCataclysmSkillEffects::HasTag(Marked, Quarry)
			|| UCataclysmSkillEffects::IsDead(Marked))
		{
			continue;
		}

		// ONLY SOMETHING THIS COMMANDER IS AT WAR WITH. The mark orders minions
		// onto an ENEMY, and a debuff tag says nothing about whose side its
		// carrier is on. Without this, a Quarry applied to a creature that was
		// later subjugated would send the rest of the army onto its own thrall.
		if (!UCataclysmTargeting::IsHostileTo(Marked, Commander))
		{
			continue;
		}

		// AND ONLY A MARK THIS COMMANDER PUT THERE. A tag on its own says
		// nothing about who applied it, so the running effect that granted it is
		// asked instead: its context carries the instigator.
		//
		// WITHOUT THIS, ONE CHARACTER'S QUARRY ORDERS EVERY ARMY IN THE LEVEL.
		// That is wrong in a co-operative session, where two players each command
		// their own creatures, and it is wrong for a Ritualist fighting beside
		// anything else that summons. A test caught it: a second character's
		// minion took orders from a mark it had nothing to do with.
		if (!WasMarkedBy(Marked, Commander, Quarry))
		{
			continue;
		}

		const float Squared = FVector::DistSquared(From, Marked->GetActorLocation());
		if (Squared < NearestSquared)
		{
			NearestSquared = Squared;
			Nearest = Marked;
		}
	}

	return Nearest;
}

AActor* UCataclysmCommand::OrderedTargetFor(const AActor* Follower)
{
	// ASKED OF THE COMMANDER'S MARK, so one lookup answers for every creature
	// that character commands and the answer cannot differ between two of them.
	return QuarryOf(CommanderOf(Follower));
}

namespace
{
	/**
	 * What a character's own stat line says, or zero when it says nothing.
	 *
	 * ASKED THROUGH THE PIPELINE RATHER THAN OFF AN ATTRIBUTE, the shape
	 * `ACataclysmMinion::HitsCountAsTheSummoners` uses for the sibling keystone.
	 * Neither stat this is called for has an attribute behind it, so the base is
	 * zero and the node's flat row is the whole of the answer: a character
	 * without the node is answered zero and every caller below stops there.
	 *
	 * THE SPELLING MUST MATCH `UCataclysmPlayerClassStats::StatsWithNoAttribute`.
	 * A name that does not match falls back in silence and reads as a character
	 * without the keystone rather than as a fault; what catches it is
	 * `Cataclysm.StatExemption.EveryStatWithNoAttributeIsActuallyRead`, which
	 * fails when a stat on that list is read by nothing.
	 */
	float KeystoneStat(const AActor* Who, const TCHAR* Stat)
	{
		const UCataclysmAbilitySystemComponent* Theirs =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Who));
		return Theirs
			? Theirs->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f)
			: 0.0f;
	}

	/**
	 * How much attention a commanded thing draws, from its own type row.
	 *
	 * ZERO FOR ANYTHING THAT IS NOT A MINION WITH A TYPE, which is a subjugated
	 * enemy and a minion summoned without one. Neither has a row stating a threat
	 * and this does not invent one for them.
	 */
	float ThreatDrawnBy(const AActor* Thing)
	{
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Thing);
		if (!Minion || Minion->TypeName.IsEmpty())
		{
			return 0.0f;
		}

		const FCataclysmMinionTypeRow* Row = ACataclysmMinion::FindType(
			ACataclysmMinion::LoadTypeTable(), Minion->TypeName);
		return Row ? Row->ThreatPercent : 0.0f;
	}
}

AActor* UCataclysmCommand::MinionDrawingEnemyFrom(const AActor* Defender,
												  const AActor* Deciding)
{
	if (!IsValid(Defender) || !IsValid(Deciding))
	{
		return nullptr;
	}

	// A BOSS CHOOSES FOR ITSELF. Ruled by the project owner on 2026-09-18, and
	// read off the rarity the spawner set, the same way `ApplyStun` reads boss
	// immunity and `Take` reads "bosses cannot be taken", so the three cannot
	// drift apart.
	if (const ACataclysmEnemyCharacter* AsEnemy =
			Cast<ACataclysmEnemyCharacter>(Deciding))
	{
		if (AsEnemy->IsBoss())
		{
			return nullptr;
		}
	}

	// THE REACH IS THE KEYSTONE'S PRESENCE, and the count is refused when it is
	// not stated. An unread reading refuses, which is the rule every condition in
	// the stat pipeline already follows.
	const float Metres = KeystoneStat(
		Defender, TEXT("minions_draw_nearby_enemies_metres"));
	const float Minimum = KeystoneStat(
		Defender, TEXT("minions_draw_nearby_enemies_minimum"));
	if (Metres <= 0.0f || Minimum <= 0.0f)
	{
		return nullptr;
	}

	// MEASURED TO THE CHARACTER AND NOT TO THE MINION. The sentence is about
	// enemies near YOU, and that is also what makes the keystone legible in play:
	// the bubble is around the character the player is looking at.
	const float Away = UCataclysmTargeting::MetresBetween(Deciding, Defender);
	if (Away < 0.0f || Away > Metres)
	{
		return nullptr;
	}

	// EVERYTHING COMMANDED COUNTS TOWARDS THE THREE, a subjugated enemy included,
	// ruled on 2026-09-18. `ThingsCommandedBy` already counts it, and the target
	// choice this feeds says outright that a thrall is part of the army.
	const TArray<AActor*> Commanded = ThingsCommandedBy(Defender);

	// A THING THE DEFENDER COMMANDS IS NEVER DRAWN TO ANOTHER. Until Madness could fall on a summoner, the defender was
	// never one of its own minions' nearest hostile; a maddened one is, and without this the minion turning on it was
	// told to attack itself or a fellow minion. Found for Insanity Bursts' "attack allies", 2026-09-26.
	if (Commanded.ContainsByPredicate([Deciding](const AActor* Thing) { return Thing == Deciding; }))
	{
		return nullptr;
	}
	if (Commanded.Num() < FMath::RoundToInt(Minimum))
	{
		return nullptr;
	}

	// AND THE ONE DRAWING MOST ATTENTION TAKES THE BLOW, ties to whichever stands
	// nearest the creature deciding. A thing drawing nothing is skipped rather
	// than ranked last, so a turret never becomes the army's shield.
	AActor* Drawing = nullptr;
	float MostThreat = 0.0f;
	float NearestMetres = 0.0f;
	for (AActor* Thing : Commanded)
	{
		const float Threat = ThreatDrawnBy(Thing);
		if (Threat <= 0.0f)
		{
			continue;
		}

		const float Distance = UCataclysmTargeting::MetresBetween(Deciding, Thing);
		if (Distance < 0.0f)
		{
			continue;
		}

		if (!Drawing || Threat > MostThreat
			|| (Threat == MostThreat && Distance < NearestMetres))
		{
			Drawing = Thing;
			MostThreat = Threat;
			NearestMetres = Distance;
		}
	}

	return Drawing;
}

namespace
{
	/**
	 * What this creature's commander adds to its attack speed, as a fraction.
	 * Zero when it follows nobody, when its commander has no such gear, and when
	 * the commander is an enemy -- enemies carry no character stat line.
	 *
	 * THE MINION'S OWN TYPE TAGS, SINCE ISSUE #1833'S DEPLOYABLE PART 1. A
	 * modifier with no required tags applies to everything, which is what all
	 * four minion affixes in `game/Data/Affixes.csv` are. A narrower one --
	 * "Gadgets fire 20%-40% faster", scoped to `Type.Deployable` -- reaches
	 * only a minion whose type row carries that tag. Until then the `Tags`
	 * column of `game/Data/MinionTypes.csv` was imported and read by nothing,
	 * and this passed an empty container. The test that checks every
	 * referenced tag resolves still covers `WeaponSkills.csv` and the two
	 * enchantment files and not this one, so a misspelled tag in that column
	 * would match nothing without an error.
	 */
	float MinionAttackSpeedFor(const AActor* Follower)
	{
		const AActor* Commander = UCataclysmCommand::CommanderOf(Follower);
		if (!IsValid(Commander))
		{
			return 0.0f;
		}

		const UCataclysmAbilitySystemComponent* Theirs =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Commander));
		if (!Theirs)
		{
			return 0.0f;
		}

		// WITH THE MINION'S OWN TYPE TAGS, since issue #1833's deployable Part 1,
		// so "Gadgets fire 20%-40% faster" reaches a machine and not an imp. A
		// thrall is no minion actor and carries none.
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
		return FMath::Max(0.0f, Theirs->IncreasesForStat(
			FName(TEXT("minion_attack_speed")),
			Minion ? Minion->TypeTags : FGameplayTagContainer()));
	}
}

float UCataclysmCommand::AttackIntervalScaleFor(const AActor* Follower,
												const AActor* Target)
{
	if (!IsValid(Follower))
	{
		return 1.0f;
	}

	// TWO THINGS SHORTEN THE INTERVAL AND THEY ARE INDEPENDENT, so they are
	// worked out separately and multiplied. Issue #898. The mark depends on WHAT
	// is being hit; the commander's minion attack speed does not, so a null
	// target refuses the first and not the second.
	float Scale = 1.0f;

	// THE BONUS IS FOR HITTING THE MARK, NOT FOR THE MARK EXISTING. A creature
	// ordered onto the quarry but swinging at something else on the way takes the
	// plain interval.
	if (IsValid(Target) && OrderedTargetFor(Follower) == Target)
	{
		const float Percent =
			UCataclysmSkillEffects::NumbersForEffectTag(QuarryTag()).Strength;

		// A SHORTER INTERVAL, NOT A SMALLER ONE BY THE SAME PERCENTAGE. "30%
		// attack speed" means 30% more swings in the same time, which is an
		// interval of 1 / 1.30 -- about 0.769 -- and not 0.70. The header
		// records why the two are not the same number.
		//
		// A SHEET THAT GIVES THE MARK NO ATTACK SPEED CHANGES NOTHING. The mark
		// still orders the army; it simply does not hurry it.
		if (Percent > 0.0f)
		{
			Scale /= 1.0f + Percent / 100.0f;
		}
	}

	// AND THE COMMANDER'S GEAR AND PASSIVES, WHICH IS NEW. Issue #898. Until
	// this, `game/Data/Affixes.csv` granted `minion_attack_speed` and nothing in
	// the engine read it: a player who found the affix got nothing at all.
	//
	// THE INCREASES, NOT THE STAT'S VALUE. `minion_attack_speed` has no base and
	// can have none -- a minion's interval comes from its own row in
	// `game/Data/MinionTypes.csv` -- so asking for the value would return zero
	// however much gear the summoner wore. `IncreasesForStat` exists for exactly
	// that and its comment records why.
	//
	// IT REACHES A SUBJUGATED ENEMY AS WELL AS A SUMMONED MINION, which is the
	// reason this is the first of the three minion stats to be built. The
	// comment in `ACataclysmEnemyController` calls this "the one place a minion
	// and a subjugated enemy share: they are different classes with different
	// overrides and one controller". Damage has no such shared place.
	Scale /= 1.0f + MinionAttackSpeedFor(Follower);

	return Scale;
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
 * `UCataclysmAbilitySystemComponent::MultiplierForStatAgainst` reads the
 * buckets without the base, which is what this needs.
 *
 * A SUM RATHER THAN A SEPARATE MULTIPLIER, AND THE DESIGN REQUIRES IT. The
 * same entry says "an attribute's contribution and an affix's contribution
 * add. They cannot multiply each other", and gives the reason: every
 * catastrophic minion scaling failure in the survey behind that decision was
 * multiplicative. The increases are still SUMMED before they multiply, and
 * the attribute channel, built for issue #340, lands in the same bucket and
 * adds: `UCataclysmPlayerClassStats::ApplyTo` records one increase per row of
 * `game/Data/MinionScaling.csv` on this same stat line, each requiring its
 * row's tag, so the type tags asked with here are what decide which reach.
 *
 * AND A "MORE" OR "LESS" ROW MULTIPLIES ON TOP, SINCE ISSUE #1833'S SMALL
 * ENGINE HALVES. "Your minions have 20%-50% less hp" is a More row, and
 * until then `IncreasesForStat` read the increases only, so it did nothing.
 * `minion_damage` already honoured the More bucket through
 * `SummonerMultiplierAgainst` below; this is the same reading with no
 * target. It reaches `minion_duration` and `minion_explosion_damage` too,
 * and on 2026-09-25 no row gave either a More, so nothing that existed
 * changed.
 *
 * A REDUCTION IS KEPT AND ONLY THE RESULT IS FLOORED. Ten rows of
 * `game/Data/PassiveEffects.csv` already carry a negative value, so a node
 * reducing minion damage is a thing the data can express. Clamping the
 * increases at zero would discard a designed drawback in silence; flooring
 * the multiplier only stops a figure below -100% turning into negative
 * damage or negative health.
 *
 * ASKED WITH THE MINION'S OWN TYPE TAGS, since issue #1833's deployable
 * Part 1, for the reason `MinionAttackSpeedFor` above records: the four minion
 * affix rows carry no scope tags and apply to every minion, and a row scoped
 * to `Type.Deployable` now reaches a machine and not an imp.
 */
const TCHAR* UCataclysmCommand::TurnOnLastAttackerStat = TEXT("minions_turn_on_your_last_attacker");

AActor* UCataclysmCommand::CommandersLastAttackerFor(const AActor* Follower)
{
	const ACataclysmMinion* Machine = Cast<ACataclysmMinion>(Follower);
	const AActor* Commander = Follower ? CommanderOf(Follower) : nullptr;
	const UCataclysmAbilitySystemComponent* Theirs = Commander
		? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Commander))
		: nullptr;
	if (!Theirs || (Machine && Machine->IsDeployable())
		|| Theirs->StatForSkill(FName(TurnOnLastAttackerStat), FGameplayTagContainer(), 0.0f) <= 0.0f
		|| !Theirs->GetLastBlow().IsOnRecord())
	{
		return nullptr;
	}
	AActor* Attacker = Theirs->GetLastBlow().Attacker.Get();
	const ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Follower);
	if (!Attacker || Attacker == Follower || UCataclysmSkillEffects::IsDead(Attacker)
		|| !UCataclysmTargeting::IsHostileTo(Attacker, Follower)
		|| (Body && FVector::Dist(Follower->GetActorLocation(), Attacker->GetActorLocation()) > Body->NoticesFromCm()))
	{
		return nullptr;
	}
	return Attacker;
}

float UCataclysmCommand::SummonerMultiplierFor(const AActor* Summoner, const TCHAR* Stat,
											 const FGameplayTagContainer& MinionTags,
											 const AActor* AskingMinion)
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

	// THE INCREASES FLOORED AT ZERO, THEN THE MORE MULTIPLIER, which is
	// what `MultiplierForStatAgainst` answers. No target: these figures
	// are read when the minion is made or when it dies, not against
	// anything it is striking.
	// AND WITH THE MINION'S OWN TYPE TAGS, since deployable Part 1, so a row
	// scoped to `Type.Deployable` reaches a machine and not an imp.
	// Measured 2026-09-25: no minion row in the enchantment or passive
	// sheets carried required tags before, so none changes.
	// AND THE MINION ASKING, WHEN THE CALLER NAMES IT, for Marshal's distance (the Death trees' slice C6d2).
	return Theirs->MultiplierForStatAgainst(
		FName(Stat), MinionTags, /*Target=*/nullptr, AskingMinion);
}

/**
 * `SummonerMultiplierFor` against the character a minion is striking, with
 * the "more" bucket. Issue #1515, Set Upon and Set the Pack On, whose rows
 * ask about the enemy struck. Only minion damage asks this: health and
 * duration are settled at the summoning, with no enemy in hand.
 *
 * ON THE SUMMONER'S COMPONENT AND NEVER THE MINION'S, which is what keeps
 * `seconds_after_summon` holding here: a minion's own clock is never
 * stamped by a summon.
 *
 * A "MORE" ROW IS A SEPARATE MULTIPLIER, WHICH THE COMMENT ABOVE SAYS THE
 * DESIGN FORBIDS, AND IT DOES NOT CONTRADICT IT. That rule is that an
 * attribute's increases and an affix's increases add; they still do, in
 * the one sum. Set the Pack On is a capstone option whose sentence says
 * "more", and no shipped row put "more" on minion damage before it,
 * measured 2026-09-23.
 */
float UCataclysmCommand::SummonerMultiplierAgainst(const AActor* Summoner, const TCHAR* Stat,
												 const AActor* Target,
												 const FGameplayTagContainer& MinionTags,
												 const AActor* AskingMinion)
{
	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Summoner));
	if (!Theirs)
	{
		// NO STAT LINE, for the reason `SummonerMultiplierFor` gives.
		return 1.0f;
	}

	return Theirs->MultiplierForStatAgainst(FName(Stat), MinionTags, Target, AskingMinion);
}

/**
 * A thrall's blow takes its commander's minion damage. Issue #1715.
 *
 * THE OWNER'S RULING OF 2026-09-13, recorded in `docs/DECISIONS.md` that day: the enemy
 * Subjugate takes "should be considered a minion", so minion gear scales it.
 *
 * READ AT THE BLOW AND AGAINST ITS TARGET, as a summoned minion's swing reads it in
 * `ACataclysmMinion::AttackTarget`, so a row asking about the enemy struck -- Set Upon, and
 * The Third Pact's first option -- reaches a thrall too, and a change to the commander's
 * gear or passives reaches it at its next blow.
 *
 * ASKED AS A `Minion.Creature`, AND NOTHING ELSE. A thrall is a taken creature and carries none of
 * `MinionTypes.csv`'s tags, so `ThrallMinionTags` stands in for them. Issue #340, ruled 2026-11-02 by the
 * coordinating session under the owner's delegation: the owner's decision of 2026-09-16 makes the Ritualist
 * tree's "minion" mean imps and thralls, so a thrall counts as a summoned creature and its commander's Spirit
 * reaches its blow through `game/Data/MinionScaling.csv`, as it reaches an imp's. Until then it was asked with no
 * tags at all, which every row granting minion damage on 2026-10-02 still reached, carrying none; a row requiring
 * `Minion.Creature` reached nothing a thrall dealt.
 *
 * ONE FOR EVERYTHING THAT IS NOT A THRALL: a player, an unowned creature, and a summoned
 * minion, whose blow reads minion damage itself and does not come through `ApplyHit`.
 */
float UCataclysmCommand::ThrallDamageMultiplierAgainst(const AActor* Striker, const AActor* Target)
{
	if (!Striker || Striker->IsA<ACataclysmMinion>())
	{
		return 1.0f;
	}
	const AActor* Commander = CommanderOf(Striker);
	return Commander
		? SummonerMultiplierAgainst(Commander, TEXT("minion_damage"), Target, ThrallMinionTags(), /*AskingMinion=*/Striker)
		: 1.0f;
}

FGameplayTagContainer UCataclysmCommand::ThrallMinionTags()
{
	// BUILT AT EACH CALL RATHER THAN HELD IN A STATIC, so a first call made before the tag table is ready cannot
	// fix an empty container for the rest of the session. One tag lookup a blow is nothing.
	FGameplayTagContainer Tags;
	const FGameplayTag Creature = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Minion.Creature")), /*ErrorIfNotFound=*/false);
	if (Creature.IsValid())
	{
		Tags.AddTag(Creature);
	}
	return Tags;
}

int32 UCataclysmCommand::ThrallCountOf(const AActor* Commander)
{
	int32 Taken = 0;
	for (const AActor* Follower : ThingsCommandedBy(Commander))
	{
		// MADE OR TAKEN, AND THE CLASS IS THE ANSWER. A summoned creature is an
		// `ACataclysmMinion`; a subjugated one is whatever it already was, which
		// is the point of subjugating it.
		if (!Follower->IsA<ACataclysmMinion>())
		{
			++Taken;
		}
	}
	return Taken;
}

bool UCataclysmCommand::HasRoomForAnotherThrall(const AActor* Commander,
												float PerThrall)
{
	if (PerThrall <= 0.0f)
	{
		// A row claiming nothing per thrall is capped by nothing. That is not
		// this function's decision to refuse.
		return true;
	}

	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Commander);
	if (!AbilitySystem)
	{
		return false;
	}

	// THE NEW THRALL'S SHARE, TWICE WHEN IT WOULD BE THE ONE CHOSEN. A Second
	// Self chooses among what is held; while it is held and nothing is chosen
	// yet, the thrall being taken is the one it would choose. A chosen follower
	// already held is counted twice by `ReservedFervourOf` itself. Issue #1515.
	const bool bWouldBeChosen = UCataclysmSecondSelf::ExtraThrallShares(Commander) > 0
		&& !UCataclysmSecondSelf::Chosen(Commander);
	return HasRoomToReserve(Commander, PerThrall * (bWouldBeChosen ? 2.0f : 1.0f));
}

TArray<ACataclysmCharacterBase*> UCataclysmCommand::ReleasableOldestFirst(const AActor* Commander)
{
	TArray<ACataclysmCharacterBase*> Releasable;
	for (AActor* Follower : ThingsCommandedBy(Commander))
	{
		ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Follower);
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
		if (Body && !(Minion && Minion->bIsMachine))
		{
			Releasable.Add(Body);
		}
	}

	// OLDEST FIRST, BY THE STAMP; NEVER STAMPED IS NEWEST. A stable sort, so a tie keeps `ThingsCommandedBy`'s order.
	Releasable.StableSort([](const ACataclysmCharacterBase& A, const ACataclysmCharacterBase& B)
	{
		const float AFrom = A.CommandedSinceSeconds >= 0.0f ? A.CommandedSinceSeconds : TNumericLimits<float>::Max();
		const float BFrom = B.CommandedSinceSeconds >= 0.0f ? B.CommandedSinceSeconds : TNumericLimits<float>::Max();
		return AFrom < BFrom;
	});
	return Releasable;
}

float UCataclysmCommand::ThrallReserveScaleFor(const AActor* Taken)
{
	const ACataclysmEnemyCharacter* Creature = Cast<const ACataclysmEnemyCharacter>(Taken);
	if (!Creature)
	{
		return 1.0f;
	}
	// BY THE RUNG, as `game/Data/EnemyRarities.csv` numbers it: Common 0, Elite 1, Legendary 2, Herald 3. The header
	// says why these four. A boss's rungs answer the Herald's, never asked since a boss cannot be taken.
	switch (Creature->RarityStep)
	{
	case 0:
		return 1.0f;
	case 1:
		return 1.5f;
	case 2:
		return 2.0f;
	default:
		return 3.0f;
	}
}

bool UCataclysmCommand::RoomCanBeMadeForAThrall(const AActor* Commander, float PerThrall)
{
	if (HasRoomForAnotherThrall(Commander, PerThrall))
	{
		return true;
	}
	const UCataclysmAbilitySystemComponent* Pool =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Commander));
	if (!Pool)
	{
		return false;
	}

	// WHAT STAYS WHEN EVERY RELEASABLE FOLLOWER IS GONE: the machines' reserves, each once, since none is ever chosen.
	const TArray<ACataclysmCharacterBase*> Releasable = ReleasableOldestFirst(Commander);
	float Staying = 0.0f;
	for (AActor* Follower : ThingsCommandedBy(Commander))
	{
		const ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Follower);
		if (Body && !Releasable.Contains(Body))
		{
			Staying += Body->ReservedFervour;
		}
	}

	// AND THE NEW THRALL, TWICE WHILE A SECOND SELF IS HELD, because nothing is left to be chosen before it. Held is
	// what `UCataclysmSecondSelf` answers either way: a chosen follower, or a share owed to the next one taken.
	const bool bHoldsSecondSelf = UCataclysmSecondSelf::Chosen(Commander) != nullptr
		|| UCataclysmSecondSelf::ExtraThrallShares(Commander) > 0;
	return Staying + PerThrall * (bHoldsSecondSelf ? 2.0f : 1.0f) <= Pool->MaximumClassResource();
}

int32 UCataclysmCommand::ReleaseOldestForAThrall(const AActor* Commander, float PerThrall)
{
	if (!IsValid(Commander) || HasRoomForAnotherThrall(Commander, PerThrall)
		|| !RoomCanBeMadeForAThrall(Commander, PerThrall))
	{
		return 0;
	}

	int32 Released = 0;
	for (ACataclysmCharacterBase* Oldest : ReleasableOldestFirst(Commander))
	{
		if (HasRoomForAnotherThrall(Commander, PerThrall))
		{
			break;
		}
		if (!IsValid(Oldest))
		{
			continue;
		}
		// SAID BEFORE IT GOES, with what it held and since when, so a play log shows which follower made the room.
		UE_LOG(LogCataclysm, Log,
			TEXT("RELEASED: '%s' released '%s', which held %.0f Fervour back and was commanded from %.1f s, to make room "
				 "for a thrall reserving %.0f."),
			*Commander->GetName(), *Oldest->GetName(), Oldest->ReservedFervour, Oldest->CommandedSinceSeconds,
			PerThrall);
		Oldest->Destroy();
		++Released;
	}
	return Released;
}

float UCataclysmCommand::ReservedFervourOf(const AActor* Commander)
{
	return ReservedFervourAmong(Commander, ThingsCommandedBy(Commander));
}

float UCataclysmCommand::ReservedFervourAmong(const AActor* Commander, const TArray<AActor*>& Commanded)
{
	float Reserved = 0.0f;
	for (const AActor* Follower : Commanded)
	{
		if (const ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Follower))
		{
			Reserved += Body->ReservedFervour;
		}
	}

	// A SECOND SELF'S CHOSEN ONE, ONCE MORE: "reserves twice the Fervour it
	// would". Asked of the function that decides who is chosen, so the total
	// and the choice cannot disagree.
	if (const ACataclysmCharacterBase* Chosen =
			Cast<ACataclysmCharacterBase>(UCataclysmSecondSelf::Chosen(Commander)))
	{
		Reserved += Chosen->ReservedFervour;
	}
	return Reserved;
}

bool UCataclysmCommand::HasRoomToReserve(const AActor* Commander, float NewReserve)
{
	if (NewReserve <= 0.0f)
	{
		return true;
	}

	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Commander));
	if (!Cataclysm)
	{
		return false;
	}

	// AGAINST THE WHOLE MAXIMUM, NOT WHAT IS IN THE POOL RIGHT NOW. A reservation
	// is a standing claim rather than a payment, so spending Fervour does not
	// cost a thrall or an imp.
	return ReservedFervourOf(Commander) + NewReserve
		<= Cataclysm->MaximumClassResource();
}

void UCataclysmCommand::ClampFervourToSpendable(const AActor* Commander)
{
	UAbilitySystemComponent* AbilitySystem = UCataclysmTargeting::AbilitySystemOf(Commander);
	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem ? AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>() : nullptr;
	if (!Resource)
	{
		return;
	}
	const float Spendable = Resource->MaximumClassResourceAsked();
	if (Resource->GetClassResource() > Spendable)
	{
		AbilitySystem->SetNumericAttributeBase(
			UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Spendable);
	}
}

float UCataclysmCommand::ReserveFor(const AActor* Commander, float Stated,
									const FGameplayTagContainer& Tags)
{
	if (Stated <= 0.0f)
	{
		return Stated;
	}

	// CROWNED'S REDUCTION, read as `FervourReserveFor` in CataclysmSkillTemplates.cpp
	// read it before issue #2787 moved it here: the pipeline's figure with the
	// attribute as the fallback, and the attribute only when the component holds
	// its set, because reading one it does not hold raises an engine ensure.
	float Reduction = 0.0f;
	float PercentLess = 0.0f;
	const FGameplayAttribute Stat = UCataclysmCombatAttributeSet::GetMinionReserveReductionAttribute();
	if (const UCataclysmAbilitySystemComponent* Theirs =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Commander)))
	{
		if (Theirs->HasAttributeSetForAttribute(Stat))
		{
			Reduction = Theirs->StatForSkill(FName(MinionReserveReductionStat), Tags,
											 Theirs->GetNumericAttribute(Stat));
		}
		// AND THE PERCENT LESS, issue #2787: every row reaching this kind's tags, added into one figure, a negative
		// row raising it. No attribute, so the pipeline's figure with nought as the fallback.
		PercentLess = Theirs->StatForSkill(FName(MinionReservePercentLessStat), Tags, 0.0f);
	}
	// CROWNED FIRST, THEN THE PERCENTAGE, and the floor last, a judgement of issue #2787: Crowned's sentence is a
	// number of points off the figure, the percentage a share of what is reserved. A net 100 percent or more less
	// would be nothing reserved, which the floor turns into one point.
	const float Multiplier = FMath::Max(0.0f, 1.0f - PercentLess / 100.0f);
	return FMath::Max(SmallestMinionReserve, (Stated - Reduction) * Multiplier);
}

void UCataclysmCommand::HoldReserve(const AActor* Commander, ACataclysmCharacterBase* Follower,
									float Reserve, const FGameplayTagContainer* SpendingTags)
{
	if (!IsValid(Follower))
	{
		return;
	}
	Follower->ReservedFervour = Reserve;
	ClampFervourToSpendable(Commander);

	// BOUND OFFERING: the share of what is held back that counts as spent, raised after the hold so a row reading the
	// pool sees it as it now is. See the declaration.
	UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Commander));
	if (!Theirs || Reserve <= 0.0f)
	{
		return;
	}
	const FGameplayTagContainer& Tags = SpendingTags ? *SpendingTags : FGameplayTagContainer::EmptyContainer;
	const float Percent = Theirs->StatForSkill(FName(ReservedFervourSpentPercentStat), Tags, 0.0f);
	if (Percent > 0.0f)
	{
		Theirs->NoteFervourSpent(Reserve * Percent / 100.0f, SpendingTags, /*bTakenFromThePool=*/false);
	}
}

bool UCataclysmCommand::SubjugateRefusalHolds(ECataclysmSubjugateRefusal Reason, const AActor* Commander,
											  const AActor* Enemy)
{
	// EACH REASON IS ASKED ON ITS OWN, so the log line can say "yes" or "no" for every one. The ones after the first two
	// are about a creature, so each answers no for a pair the first two already refuse.
	const bool bAPair = IsValid(Commander) && IsValid(Enemy) && Commander != Enemy;
	const ACataclysmCharacterBase* Character = bAPair ? Cast<const ACataclysmCharacterBase>(Enemy) : nullptr;
	const ACataclysmEnemyCharacter* Creature = Cast<const ACataclysmEnemyCharacter>(Character);

	switch (Reason)
	{
	case ECataclysmSubjugateRefusal::NothingToTake:
		return !bAPair;

	case ECataclysmSubjugateRefusal::NotACharacter:
		// Only a character can be taken. A patch of burning ground, a projectile
		// and a piece of terrain are all actors and none of them fights for
		// anybody.
		return bAPair && !Character;

	case ECataclysmSubjugateRefusal::Dead:
		// A corpse does not fight for you. The skill's own threshold leaves the
		// target alive by design -- below half health, not below none -- so this
		// is the case where something killed it before the take. Since issue #2856
		// Subjugate's own blow cannot be that something for a creature it could take:
		// `UCataclysmSummonSkill::Possess` holds such a creature at one health.
		return Character && UCataclysmSkillEffects::IsDead(Character);

	case ECataclysmSubjugateRefusal::Boss:
		// "BOSSES CANNOT BE TAKEN", which the row states outright. Read off the
		// rarity the spawner set, the same way `UCataclysmSkillEffects::ApplyStun`
		// reads boss immunity, so the two cannot drift apart.
		return Creature && Creature->IsBoss();

	case ECataclysmSubjugateRefusal::CannotBeHurt:
		// AND NOTHING THAT CANNOT BE HURT: The Reaper. Issues #1820 and #41, ruled by the
		// coordinating session under the owner's delegation, 2026-09-24. A thrall that
		// cannot die would be an ally nothing can remove, and Sacrificial Ward, which ends
		// a minion by writing its health, would reach it past the check that keeps it
		// alive.
		return Creature && Creature->bCannotBeHurt;

	case ECataclysmSubjugateRefusal::AlreadyOnItsSide:
		// ALREADY OURS. Taking something twice would reserve a second 30 Fervour for
		// one creature, so the caller has to be told nothing happened.
		return Character && UCataclysmTargeting::IsFriendlyTo(Character, Commander);

	default:
		return false;
	}
}

ECataclysmSubjugateRefusal UCataclysmCommand::WhySubjugateRefuses(const AActor* Commander, const AActor* Enemy)
{
	// IN THE ORDER OF THE LIST, which is the order `Subjugate` asked them in before issue #2856 gathered them here.
	for (int32 Raw = static_cast<int32>(ECataclysmSubjugateRefusal::None) + 1;
		 Raw < static_cast<int32>(ECataclysmSubjugateRefusal::Count); ++Raw)
	{
		const ECataclysmSubjugateRefusal Reason = static_cast<ECataclysmSubjugateRefusal>(Raw);
		if (SubjugateRefusalHolds(Reason, Commander, Enemy))
		{
			return Reason;
		}
	}
	return ECataclysmSubjugateRefusal::None;
}

const TCHAR* UCataclysmCommand::SubjugateRefusalName(ECataclysmSubjugateRefusal Reason)
{
	switch (Reason)
	{
	case ECataclysmSubjugateRefusal::None:
		return TEXT("nothing refuses");
	case ECataclysmSubjugateRefusal::NothingToTake:
		return TEXT("nothing to take");
	case ECataclysmSubjugateRefusal::NotACharacter:
		return TEXT("not a character");
	case ECataclysmSubjugateRefusal::Dead:
		return TEXT("dead");
	case ECataclysmSubjugateRefusal::Boss:
		return TEXT("boss");
	case ECataclysmSubjugateRefusal::CannotBeHurt:
		return TEXT("cannot be hurt");
	case ECataclysmSubjugateRefusal::AlreadyOnItsSide:
		return TEXT("already on its side");
	default:
		return TEXT("");
	}
}

FString UCataclysmCommand::SubjugateRefusalsText(const AActor* Commander, const AActor* Enemy)
{
	// EVERY REASON ON THE LIST, BY THE SAME WALK `WhySubjugateRefuses` MAKES, so a reason added to the list is printed
	// without anybody remembering this line. Issue #2856.
	FString Text;
	for (int32 Raw = static_cast<int32>(ECataclysmSubjugateRefusal::None) + 1;
		 Raw < static_cast<int32>(ECataclysmSubjugateRefusal::Count); ++Raw)
	{
		const ECataclysmSubjugateRefusal Reason = static_cast<ECataclysmSubjugateRefusal>(Raw);
		if (!Text.IsEmpty())
		{
			Text += TEXT(", ");
		}
		Text += FString::Printf(TEXT("%s %s"), SubjugateRefusalName(Reason),
								SubjugateRefusalHolds(Reason, Commander, Enemy) ? TEXT("yes") : TEXT("no"));
	}
	return Text;
}

bool UCataclysmCommand::IsHeldAliveForATake(const AActor* Creature)
{
	const ACataclysmCharacterBase* Character = Cast<const ACataclysmCharacterBase>(Creature);
	return Character && Character->bHeldAliveForATake;
}

bool UCataclysmCommand::Subjugate(AActor* Commander, AActor* Enemy)
{
	// EVERY REFUSAL IS ONE OF `WhySubjugateRefuses`'S, which carries each reason's own comment. Issue #2856: the play
	// log reports a refusal from the same answer, so it can no longer print a refusal whose every reason reads "no".
	const ECataclysmSubjugateRefusal Refusal = WhySubjugateRefuses(Commander, Enemy);
	if (Refusal != ECataclysmSubjugateRefusal::None)
	{
		UE_LOG(LogCataclysm, Verbose, TEXT("'%s' cannot be taken: %s."), *GetNameSafe(Enemy),
			   SubjugateRefusalName(Refusal));
		return false;
	}

	// A CHARACTER, WHICH NOTHING REFUSING HAS ESTABLISHED: `NotACharacter` holds for anything else.
	ACataclysmCharacterBase* Taken = Cast<ACataclysmCharacterBase>(Enemy);
	if (!Taken)
	{
		return false;
	}

	// THE OWNER FIRST, THEN THE SIDE, AND BOTH ARE LOAD-BEARING.
	// `UCataclysmTeams::TeamOf` walks the owner chain and `ThingsCommandedBy`
	// finds a thrall by asking who owns it, so a creature given one without the
	// other is half taken.
	Taken->SetOwner(Commander);
	Taken->SetGenericTeamId(UCataclysmTeams::TeamOf(Commander));

	// AND IT IS NO LONGER ONE OF THE FLOOR'S ENEMIES. Issue #2797, ruled by the coordinating session 2026-11-20: a load
	// never puts a thrall back on `FloorEnemies` and the next floor's clear takes it off, so the list is the floor's
	// enemies, and the take is where a creature stops being one. Here, because every take comes through this function:
	// the Subjugate skill's `UCataclysmSummonSkill::Possess` and the Warzone's allies alike. The game mode is found by
	// walking the level, so a world built for a test finds it too; a world with none has no list to leave.
	if (ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Taken))
	{
		if (ACataclysmDungeonGameMode* Floor = ACataclysmDungeonGameMode::InWorld(Creature->GetWorld()))
		{
			Floor->ForgetACreatureTaken(Creature);
		}
	}

	// WHEN IT CAME UNDER COMMAND, for A Second Self's "held longest", and not
	// anybody's Second Self yet. Issue #1515.
	if (const UWorld* World = Taken->GetWorld())
	{
		Taken->CommandedSinceSeconds = World->GetTimeSeconds();
	}
	Taken->bIsSecondSelf = false;

	// AND ITS COMMANDER'S MINION HEALTH, ONCE, AT THE TAKE. Issue #1715: "considered a
	// minion", ruled 2026-09-13. Through the creature's own figures rather than a write to
	// the attribute, because `ApplyStartingAttributes` rewrites maximum health from them
	// whenever anything sets a creature's figures, and a rung raised later would drop it.
	// Fixed at the take, as a summoned minion's health is fixed at its summoning; the
	// owner's "minions update live rather than snapshotting" is recorded and not built.
	// NO TYPE TAGS, UNLIKE ITS DAMAGE. Issue #340's ruling of 2026-11-02 made a thrall a `Minion.Creature` for minion
	// damage, which is the only stat `game/Data/MinionScaling.csv` scales; its health was left as it was asked, with no
	// tags, so a minion_health row requiring a tag does not reach a thrall. Not ruled either way.
	if (ACataclysmEnemyCharacter* AsCreature = Cast<ACataclysmEnemyCharacter>(Taken))
	{
		// AND FIRST ITS OWN FIGURES ARE BUILT AGAIN, FROM ITS KIND AND ITS COMMANDER'S LEVEL AND NOT FROM THE FLOOR.
		// Issue #2868, the owner's ruling of 2026-11-24: "Shouldn't minions scale based off your stats? Like not 1 for
		// 1, but if A has 100 health, and B has 500 health, and minion health scales at like 2x per your level, A would
		// have 200 health and B would have 1000 health." `UCataclysmEnemyStrength::ThrallFiguresFor` has the arithmetic:
		// a summoned Imp's figures at the commander's level times the kind's multiple of an Imp. The creature still
		// applies its rarity's scale and the modifiers it carries itself, so a taken Herald is a Herald's multiple of a
		// Common of its kind, and the minion health below multiplies the result.
		//
		// SET AS ITS STARTING FIGURES, through the three setters the dungeon builds a creature with, so a rung raised
		// later scales these and a save keeps them. FIXED AT THE TAKE, as a summoned minion's are at its summoning.
		//
		// A CREATURE OF NO DESIGNED KIND KEEPS ITS OWN FIGURES: the sandbox's training dummy, which the tests take, has
		// no archetype row, and a missing table leaves every creature as it was.
		FCataclysmStartingFigures AsAMinion;
		if (!AsCreature->ArchetypeRow.IsNone()
			&& UCataclysmEnemyStrength::ThrallFiguresFor(
				UCataclysmEnemyRarity::LoadEnemyRarityTable(), UCataclysmEnemyStrength::LoadEnemyArchetypeTable(),
				ACataclysmMinion::LoadTypeTable(), AsCreature->ArchetypeRow,
				ACataclysmMinion::LevelOfSummonerFor(Commander), AsAMinion))
		{
			AsCreature->SetHealth(AsAMinion.Health);
			AsCreature->SetAttackDamage(AsAMinion.AttackDamage);
			AsCreature->SetArmour(AsAMinion.Armour);
			UE_LOG(LogCataclysm, Log,
				TEXT("'%s' takes '%s' as a minion of level %d: %.0f health, %.1f attack damage and %.0f armour before "
					 "its rarity and modifiers."),
				*Commander->GetName(), *AsCreature->GetName(), ACataclysmMinion::LevelOfSummonerFor(Commander),
				AsAMinion.Health, AsAMinion.AttackDamage, AsAMinion.Armour);
		}

		AsCreature->SetCommanderHealthMultiplier(SummonerMultiplierFor(
			Commander, TEXT("minion_health"), FGameplayTagContainer()));
	}

	// AND IT IS HEALED TO FULL, ONCE, AT THE MOMENT IT IS TAKEN. The project
	// owner's ruling: "it should heal to full, and the enemy you take over should
	// be considered a minion". Only the first half is here; the second is the
	// gear-modifier path and a separate change.
	//
	// WHY IT NEEDS SAYING AT ALL. The skill only works on a target below half
	// health, so without this every thrall arrives damaged, and one taken at a
	// sliver dies to the first blow after joining. That is a creature the player
	// spent an ultimate and 30 reserved Fervour on.
	//
	// A DIRECT WRITE OF THE ATTRIBUTE, NOT A HEALING EFFECT, AND THAT IS NOT A
	// BYPASS. `HealingCeilingReduction` is applied inside
	// `UCataclysmRegeneration::TopUp`, which regeneration, leech, an
	// enchantment's restore and the other capped heals call; no heal in the
	// game's code arrives as a gameplay effect. This writes the base value
	// without calling `TopUp`, so no ceiling is in the path to ignore. It is
	// also what "to full" has to mean: a ceiling that left the creature short
	// would contradict the words of the ruling.
	//
	// WHETHER A CURSE SHOULD CUT IT IS OPEN, AND THIS DOES NOT DECIDE IT. Issue
	// #1713. The owner also ruled, on 2026-09-12, that healing received is "one
	// stat covering every route that restores health", and taking a creature and
	// healing it is such a route -- so on a floor carrying Death's Embrace the
	// two rulings pull opposite ways.
	//
	// IT CANNOT ARISE YET, WHICH IS WHY THIS SHIPS RATHER THAN WAITS. Measured:
	// `healing_received_reduction` reaches a character through the player's class
	// stat map and through a dungeon rule, and both of the dungeon rule's call
	// sites in `CataclysmDungeonGameMode` apply it to the PLAYER. A thrall is a
	// creature, so its reduction is zero whatever the floor and every reading of
	// the two rulings behaves identically. The enchantment row "Disease effects
	// reduce enemy healing by 50%-100%" is what will make it live.
	//
	// THE MAXIMUM IS READ RATHER THAN ASSUMED, because a creature's maximum is
	// whatever its archetype and the difficulty tier gave it, and nothing here
	// knows that figure.
	if (UAbilitySystemComponent* System =
			UCataclysmTargeting::AbilitySystemOf(Taken))
	{
		const float Full = System->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		if (Full > 0.0f)
		{
			System->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), Full);
		}
	}

	// NOTHING HAS TO BE DONE TO ITS BRAIN, and it is worth saying why rather
	// than leaving the absence to be read as an oversight.
	// `ACataclysmEnemyController::Think` calls `ChooseTarget` on every pass and
	// that search asks about the BODY's side, not the controller's, so the very
	// next think finds the thrall's new enemies and drops the old target on its
	// own. A stale target survives at most one pass of the brain.
	UE_LOG(LogCataclysm, Verbose,
		TEXT("'%s' took '%s' into its command."),
		*Commander->GetName(), *Taken->GetName());

	return true;
}
