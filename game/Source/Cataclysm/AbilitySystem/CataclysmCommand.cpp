// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmSecondSelf.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
// For healing a subjugated enemy to full when it is taken. Issue #340.
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
// For a minion type row's ThreatPercent, which decides which minion
// draws a nearby enemy off its summoner. Issue #1515.
#include "Data/CataclysmDataRows.h"
#include "AbilitySystemComponent.h"
// Cast<AController> in CommanderOf needs the whole type, not the forward
// declaration Pawn.h carries.
#include "GameFramework/Controller.h"
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

// AND WHAT THE SWARM GRANTS. Issue #1718: "Each skill that limits how many of its
// minions may be active allows 2 more." The limit is on the skill's own row;
// Summon Imp's is 3.
const TCHAR* UCataclysmCommand::MinionCapBonusStat = TEXT("minion_cap_bonus");

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
		// ONE RAISED OUTSIDE THE CAPS IS LEFT OUT. Issue #1833 group E part 4b:
		// both callers are cap counts -- a summon skill's shared cap and the
		// risen imps' -- and a necrosis-risen minion holds a place under neither.
		if (IsValid(Minion) && Minion->TypeName == TypeName && !Minion->bOutsideSummonCaps)
		{
			Found.Add(Minion);
		}
	}

	// OLDEST FIRST, which is what a summon at the cap destroys. The spawn order
	// and not the spawn time, because two imps made in one frame share a time.
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
 * multiplicative. The increases are still SUMMED before they multiply, so
 * when the attribute channel is built it lands in the same bucket and adds.
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
float UCataclysmCommand::SummonerMultiplierFor(const AActor* Summoner, const TCHAR* Stat,
											 const FGameplayTagContainer& MinionTags)
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
	return Theirs->MultiplierForStatAgainst(
		FName(Stat), MinionTags, /*Target=*/nullptr);
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
												 const FGameplayTagContainer& MinionTags)
{
	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Summoner));
	if (!Theirs)
	{
		// NO STAT LINE, for the reason `SummonerMultiplierFor` gives.
		return 1.0f;
	}

	return Theirs->MultiplierForStatAgainst(FName(Stat), MinionTags, Target);
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
 * NO TYPE TAGS. A thrall is a taken creature and carries none of `MinionTypes.csv`'s; every
 * row granting minion damage on 2026-10-02 carried no required tag, so each reaches it.
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
		? SummonerMultiplierAgainst(Commander, TEXT("minion_damage"), Target, FGameplayTagContainer())
		: 1.0f;
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

float UCataclysmCommand::ReservedFervourOf(const AActor* Commander)
{
	float Reserved = 0.0f;
	for (const AActor* Follower : ThingsCommandedBy(Commander))
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

bool UCataclysmCommand::HasRoomToReserve(const AActor* Commander, float NewReserve,
										  float Freed)
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
	return ReservedFervourOf(Commander) - Freed + NewReserve
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

bool UCataclysmCommand::Subjugate(AActor* Commander, AActor* Enemy)
{
	if (!IsValid(Commander) || !IsValid(Enemy) || Commander == Enemy)
	{
		return false;
	}

	ACataclysmCharacterBase* Taken = Cast<ACataclysmCharacterBase>(Enemy);
	if (!Taken)
	{
		// Only a character can be taken. A patch of burning ground, a projectile
		// and a piece of terrain are all actors and none of them fights for
		// anybody.
		return false;
	}

	if (UCataclysmSkillEffects::IsDead(Taken))
	{
		// A corpse does not fight for you. The skill's own threshold leaves the
		// target alive by design -- below half health, not below none -- so this
		// is the case where the blow killed outright.
		return false;
	}

	// "BOSSES CANNOT BE TAKEN", which the row states outright. Read off the
	// rarity the spawner set, the same way `UCataclysmSkillEffects::ApplyStun`
	// reads boss immunity, so the two cannot drift apart.
	if (const ACataclysmEnemyCharacter* AsEnemy =
			Cast<ACataclysmEnemyCharacter>(Taken))
	{
		if (AsEnemy->IsBoss())
		{
			UE_LOG(LogCataclysm, Verbose,
				TEXT("'%s' is a boss and cannot be taken."), *Taken->GetName());
			return false;
		}

		// AND NOTHING THAT CANNOT BE HURT: The Reaper. Issues #1820 and #41, ruled by the
		// coordinating session under the owner's delegation, 2026-09-24. A thrall that
		// cannot die would be an ally nothing can remove, and Sacrificial Ward, which ends
		// a minion by writing its health, would reach it past the check that keeps it
		// alive.
		if (AsEnemy->bCannotBeHurt)
		{
			UE_LOG(LogCataclysm, Verbose,
				TEXT("'%s' cannot be hurt and cannot be taken."), *Taken->GetName());
			return false;
		}
	}

	// ALREADY OURS. Taking something twice would reserve a second 30 Fervour for
	// one creature, so the caller has to be told nothing happened.
	if (UCataclysmTargeting::IsFriendlyTo(Taken, Commander))
	{
		return false;
	}

	// THE OWNER FIRST, THEN THE SIDE, AND BOTH ARE LOAD-BEARING.
	// `UCataclysmTeams::TeamOf` walks the owner chain and `ThingsCommandedBy`
	// finds a thrall by asking who owns it, so a creature given one without the
	// other is half taken.
	Taken->SetOwner(Commander);
	Taken->SetGenericTeamId(UCataclysmTeams::TeamOf(Commander));

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
	// No type tags, for the reason `ThrallDamageMultiplierAgainst` gives.
	if (ACataclysmEnemyCharacter* AsCreature = Cast<ACataclysmEnemyCharacter>(Taken))
	{
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
