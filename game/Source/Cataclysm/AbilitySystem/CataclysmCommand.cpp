// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "GameplayTagsManager.h"

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

	return Follower->GetOwner();
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

float UCataclysmCommand::AttackIntervalScaleFor(const AActor* Follower,
												const AActor* Target)
{
	if (!IsValid(Follower) || !IsValid(Target))
	{
		return 1.0f;
	}

	// THE BONUS IS FOR HITTING THE MARK, NOT FOR THE MARK EXISTING. A creature
	// ordered onto the quarry but swinging at something else on the way takes the
	// plain interval.
	if (OrderedTargetFor(Follower) != Target)
	{
		return 1.0f;
	}

	const float Percent =
		UCataclysmSkillEffects::NumbersForEffectTag(QuarryTag()).Strength;
	if (Percent <= 0.0f)
	{
		// The sheet gives the mark no attack speed. The mark still orders the
		// army; it simply does not hurry it.
		return 1.0f;
	}

	// A SHORTER INTERVAL, NOT A SMALLER ONE BY THE SAME PERCENTAGE. "30% attack
	// speed" means 30% more swings in the same time, which is an interval of
	// 1 / 1.30 -- about 0.769 -- and not 0.70. The header records why the two are
	// not the same number.
	return 1.0f / (1.0f + Percent / 100.0f);
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

	// THE MAXIMUM, NOT WHAT IS IN THE POOL RIGHT NOW. A reservation is a standing
	// claim rather than a payment, so spending Fervour does not cost a thrall.
	const float Pool = AbilitySystem->GetNumericAttribute(
		UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute());

	const float WouldBeClaimed = (ThrallCountOf(Commander) + 1) * PerThrall;
	return WouldBeClaimed <= Pool;
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
