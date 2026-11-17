// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmPinnedLine.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Cataclysm.h"
#include "GameFramework/Actor.h"

int32 UCataclysmPinnedLine::BindTogether(const TArray<AActor*>& Line)
{
	TArray<AActor*> Members;
	for (AActor* Member : Line)
	{
		if (IsValid(Member))
		{
			// AddUnique, because a target listed twice would be bound to itself
			// and would count toward the "fewer than two" test below without
			// being a second creature.
			Members.AddUnique(Member);
		}
	}

	if (Members.Num() < 2)
	{
		// A LINE OF ONE FREES NOBODY. Skewer pierces up to 99 targets and often
		// catches exactly one, so this is the ordinary case rather than an edge.
		return 0;
	}

	TArray<TWeakObjectPtr<AActor>> Weak;
	Weak.Reserve(Members.Num());
	for (AActor* Member : Members)
	{
		Weak.Add(Member);
	}

	for (AActor* Member : Members)
	{
		// REPLACED RATHER THAN ADDED TO. A creature caught by a second Skewer
		// belongs to the second line only; see the header.
		UCataclysmPinnedLine* Bound =
			Member->FindComponentByClass<UCataclysmPinnedLine>();
		if (!Bound)
		{
			Bound = NewObject<UCataclysmPinnedLine>(Member);
			if (!Bound)
			{
				continue;
			}
			Bound->RegisterComponent();
		}

		Bound->Line = Weak;
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("%d creatures pinned together; killing any one frees the rest."),
		Members.Num());

	return Members.Num();
}

int32 UCataclysmPinnedLine::ReleaseFromDying(AActor* Dying)
{
	if (!IsValid(Dying))
	{
		return 0;
	}

	const UCataclysmPinnedLine* Bound =
		Dying->FindComponentByClass<UCataclysmPinnedLine>();
	if (!Bound)
	{
		// The ordinary case. Nothing but Skewer binds a line.
		return 0;
	}

	int32 Freed = 0;
	for (const TWeakObjectPtr<AActor>& Member : Bound->Line)
	{
		AActor* Fellow = Member.Get();
		if (!IsValid(Fellow) || Fellow == Dying)
		{
			// The dying creature is skipped by name rather than by asking
			// whether it is dead: this runs BEFORE the death is recorded, the
			// same way the curse spreading beside it does, so it is still an
			// ordinary member of its own line at this moment.
			continue;
		}

		if (UCataclysmSkillEffects::ReleasePin(Fellow))
		{
			++Freed;
		}
	}

	if (Freed > 0)
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("%s died on a shared spear; %d others were freed."),
			*GetNameSafe(Dying), Freed);
	}

	return Freed;
}
