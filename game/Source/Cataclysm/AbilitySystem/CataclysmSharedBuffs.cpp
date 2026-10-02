// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSharedBuffs.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"

namespace CataclysmSharedBuffsDetail
{
	/** One copy the step wants an ally to carry after it. */
	struct FWanted
	{
		AActor* Ally = nullptr;
		const UObject* Buff = nullptr;
		bool bNearbyRow = false;
		FGameplayTag Scope;
		float Value = 0.0f;
	};

	UCataclysmAbilitySystemComponent* CataclysmOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	/**
	 * The living allies within `RadiusCm` that can carry a modifier.
	 * `FindAlliesInSphere` leaves the wearer out. A DEAD ALLY IS LEFT OUT HERE,
	 * so a copy it holds is taken back on the step after it dies.
	 */
	TArray<AActor*> LivingAlliesWithin(AActor* Wearer, float RadiusCm)
	{
		TArray<AActor*> Living;
		for (AActor* Ally : UCataclysmTargeting::FindAlliesInSphere(
				 Wearer->GetWorld(), Wearer, Wearer->GetActorLocation(), RadiusCm))
		{
			if (IsValid(Ally) && Ally != Wearer && !UCataclysmSkillEffects::IsDead(Ally)
				&& CataclysmOf(Ally))
			{
				Living.Add(Ally);
			}
		}
		return Living;
	}

	/** Whether a held copy is the one a wanted copy describes. */
	bool IsTheSame(const FCataclysmSharedBuffCopy& Held, const FWanted& Wanted)
	{
		return Held.Ally.Get() == Wanted.Ally
			&& Held.bNearbyRow == Wanted.bNearbyRow
			&& (Wanted.bNearbyRow || Held.Buff.Get() == Wanted.Buff)
			&& Held.Scope == Wanted.Scope;
	}

	/** Take one held copy off its ally, when the ally is still there to hold it. */
	void TakeBack(const FCataclysmSharedBuffCopy& Held)
	{
		if (UCataclysmAbilitySystemComponent* Theirs = CataclysmOf(Held.Ally.Get()))
		{
			Theirs->RemoveStatModifier(Held.Handle);
		}
	}
}

int32 UCataclysmSharedBuffs::Step(AActor* Wearer)
{
	using namespace CataclysmSharedBuffsDetail;

	UCataclysmAbilitySystemComponent* Mine = Wearer ? CataclysmOf(Wearer) : nullptr;
	if (!Mine)
	{
		return 0;
	}

	// A DEAD WEARER GIVES NOTHING. Its self buffs end at the death anyway; the
	// nearby row would otherwise go on reaching allies from a corpse.
	if (UCataclysmSkillEffects::IsDead(Wearer))
	{
		TakeBackAll(Mine);
		return 0;
	}

	const FGameplayTagContainer NoTags;
	const float SelfMetres = Mine->StatForSkill(
		FName(UCataclysmAbilitySystemComponent::SelfBuffSharedWithinMetresStat), NoTags, 0.0f);
	const float SupportMetres = Mine->StatForSkill(
		FName(UCataclysmAbilitySystemComponent::SupportBuffSharedWithinMetresStat), NoTags, 0.0f);
	const float NearbyMore = Mine->StatForSkill(
		FName(UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat), NoTags, 0.0f);
	if (SelfMetres <= 0.0f && SupportMetres <= 0.0f && NearbyMore <= 0.0f
		&& Mine->SharedBuffCopies.IsEmpty())
	{
		return 0;
	}

	TArray<FWanted> Wanted;

	// EACH RUNNING SELF BUFF THAT GRANTS MORE DAMAGE, to the allies within the
	// larger of the two reaches that apply to it. The support reach applies
	// only to a buff in the Support slot; the self-buff reach to any.
	if (SelfMetres > 0.0f || SupportMetres > 0.0f)
	{
		for (const FGameplayAbilitySpec& Spec : Mine->GetActivatableAbilities())
		{
			const UCataclysmSelfBuffSkill* Buff =
				Spec.IsActive() ? Cast<UCataclysmSelfBuffSkill>(Spec.GetPrimaryInstance()) : nullptr;
			if (!Buff || Buff->GrantedIncrease <= 0.0f)
			{
				continue;
			}
			float Metres = SelfMetres;
			if (Buff->Slot == ECataclysmAbilitySlot::Support)
			{
				Metres = FMath::Max(Metres, SupportMetres);
			}
			if (Metres <= 0.0f)
			{
				continue;
			}
			for (AActor* Ally : LivingAlliesWithin(Wearer, Metres * 100.0f))
			{
				Wanted.Add({Ally, Buff, /*bNearbyRow=*/false, Buff->GrantedScope, Buff->GrantedIncrease});
			}
		}
	}

	// AND THE STANDING ROW, unscoped, within the "nearby" radius.
	if (NearbyMore > 0.0f)
	{
		for (AActor* Ally : LivingAlliesWithin(
				 Wearer, UCataclysmAbilitySystemComponent::NearbyActionRadiusCm))
		{
			Wanted.Add({Ally, nullptr, /*bNearbyRow=*/true, FGameplayTag(), NearbyMore});
		}
	}

	// KEEP OR TAKE BACK WHAT IS HELD. A copy still wanted keeps its handle and
	// follows a moved value, so Butcher's Heat's growing bonus is changed in
	// place rather than given twice.
	TArray<bool> Placed;
	Placed.Init(false, Wanted.Num());
	for (int32 Index = Mine->SharedBuffCopies.Num() - 1; Index >= 0; --Index)
	{
		FCataclysmSharedBuffCopy& Held = Mine->SharedBuffCopies[Index];
		int32 Match = INDEX_NONE;
		for (int32 W = 0; W < Wanted.Num(); ++W)
		{
			if (!Placed[W] && IsTheSame(Held, Wanted[W]))
			{
				Match = W;
				break;
			}
		}
		if (Match == INDEX_NONE)
		{
			TakeBack(Held);
			Mine->SharedBuffCopies.RemoveAt(Index);
			continue;
		}
		Placed[Match] = true;
		if (Held.Value != Wanted[Match].Value)
		{
			if (UCataclysmAbilitySystemComponent* Theirs = CataclysmOf(Held.Ally.Get()))
			{
				Theirs->SetStatModifierValue(Held.Handle, Wanted[Match].Value);
			}
			Held.Value = Wanted[Match].Value;
		}
	}

	// AND GIVE WHAT IS WANTED AND NOT YET HELD. More, from `SkillBuff`, which is
	// the bucket and source `UCataclysmSelfBuffSkill::GrantIncrease` gives its
	// own caster, so the ally carries exactly what the wearer carries.
	for (int32 W = 0; W < Wanted.Num(); ++W)
	{
		if (Placed[W])
		{
			continue;
		}
		UCataclysmAbilitySystemComponent* Theirs = CataclysmOf(Wanted[W].Ally);
		if (!Theirs)
		{
			continue;
		}
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::More;
		Modifier.Source = ECataclysmModifierSource::SkillBuff;
		Modifier.Value = Wanted[W].Value;
		if (Wanted[W].Scope.IsValid())
		{
			Modifier.RequiredTags.AddTag(Wanted[W].Scope);
		}
		const int32 Handle = Theirs->AddStatModifier(Modifier);
		if (Handle == 0)
		{
			continue;
		}
		FCataclysmSharedBuffCopy Held;
		Held.Ally = Wanted[W].Ally;
		Held.Buff = Wanted[W].Buff;
		Held.bNearbyRow = Wanted[W].bNearbyRow;
		Held.Scope = Wanted[W].Scope;
		Held.Handle = Handle;
		Held.Value = Wanted[W].Value;
		Mine->SharedBuffCopies.Add(Held);
	}

	return Mine->SharedBuffCopies.Num();
}

int32 UCataclysmSharedBuffs::TakeBackAll(UCataclysmAbilitySystemComponent* Wearer)
{
	if (!Wearer)
	{
		return 0;
	}
	const int32 Taken = Wearer->SharedBuffCopies.Num();
	for (const FCataclysmSharedBuffCopy& Held : Wearer->SharedBuffCopies)
	{
		CataclysmSharedBuffsDetail::TakeBack(Held);
	}
	Wearer->SharedBuffCopies.Reset();
	return Taken;
}
