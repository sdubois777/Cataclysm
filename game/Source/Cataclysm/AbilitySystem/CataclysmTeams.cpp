// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTeams.h"
#include "GameFramework/Actor.h"

FGenericTeamId UCataclysmTeams::TeamOf(const AActor* Actor)
{
	// The actor first, then whatever owns it, and so on. A player character
	// answers on the first step; a patch of burning ground answers on the
	// second, through the character whose skill left it.
	for (const AActor* At = Actor; At; At = At->GetOwner())
	{
		const FGenericTeamId Id = FGenericTeamId::GetTeamIdentifier(At);
		if (Id != FGenericTeamId::NoTeam)
		{
			return Id;
		}
	}

	return FGenericTeamId::NoTeam;
}

bool UCataclysmTeams::SharesAnOwnerChain(const AActor* A, const AActor* B)
{
	if (!A || !B)
	{
		return false;
	}

	for (const AActor* Owner = A->GetOwner(); Owner; Owner = Owner->GetOwner())
	{
		if (Owner == B)
		{
			return true;
		}
	}
	for (const AActor* Owner = B->GetOwner(); Owner; Owner = Owner->GetOwner())
	{
		if (Owner == A)
		{
			return true;
		}
	}

	return false;
}

ETeamAttitude::Type UCataclysmTeams::AttitudeBetween(const AActor* Actor, const AActor* Other)
{
	if (!Actor || !Other)
	{
		return ETeamAttitude::Neutral;
	}

	if (Actor == Other)
	{
		return ETeamAttitude::Friendly;
	}

	// OWNERSHIP BEATS THE TEAM NUMBER, and it has to come first. A minion takes
	// its summoner's team when it is spawned, so in the ordinary case the two
	// tests agree. This one is what still holds when they do not: anything a
	// character puts in the world is on that character's side even if nothing
	// gave it a team.
	if (SharesAnOwnerChain(Actor, Other))
	{
		return ETeamAttitude::Friendly;
	}

	const FGenericTeamId Mine = TeamOf(Actor);
	const FGenericTeamId Theirs = TeamOf(Other);

	// See the class comment. Having no team is hostile rather than neutral,
	// because an enemy nobody can hit is a worse bug than one that can be hit
	// by something it should not be.
	if (Mine == FGenericTeamId::NoTeam || Theirs == FGenericTeamId::NoTeam)
	{
		return ETeamAttitude::Hostile;
	}

	return Mine == Theirs ? ETeamAttitude::Friendly : ETeamAttitude::Hostile;
}
