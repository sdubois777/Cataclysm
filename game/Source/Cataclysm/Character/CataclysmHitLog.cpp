// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmHitLog.h"

#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyRarity.h"
#include "GameFramework/Actor.h"

const TCHAR* const FCataclysmHitLog::Prefix = TEXT("HIT: ");
const TCHAR* const FCataclysmHitLog::OutgoingPrefix = TEXT("HIT OUT: ");
const TCHAR* const FCataclysmHitLog::CreatureDeathPrefix = TEXT("DIED: ");

namespace CataclysmHitLog
{
	/** A figure as the line says it: whole numbers from ten up, one decimal below, so a 0.4 tick does not read 0. */
	FString NumberText(float Value)
	{
		return FMath::Abs(Value) >= 10.0f ? FString::Printf(TEXT("%.0f"), Value) : FString::Printf(TEXT("%.1f"), Value);
	}

	/** The ailment one of these tags is, in lower case, or empty. */
	FString AilmentIn(const FGameplayTagContainer* Tags)
	{
		if (Tags == nullptr)
		{
			return FString();
		}
		for (const FGameplayTag& Tag : *Tags)
		{
			if (const FCataclysmAilmentKind* Kind = UCataclysmAilments::KindWithTag(Tag))
			{
				return FString(Kind->Ailment).ToLower();
			}
		}
		return FString();
	}
}

FString FCataclysmHitLog::AttackerText(const AActor* Attacker, const AActor* Target)
{
	if (Attacker == nullptr)
	{
		return TEXT("nobody");
	}
	if (Attacker == Target)
	{
		return TEXT("itself");
	}

	// A CREATURE'S RARITY, by the name the line saying it spawned uses. A step the table does not name is given as a
	// step, so the line still says something a reader can check.
	if (const ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Attacker))
	{
		const FString Rarity = UCataclysmEnemyRarity::RarityNameForStep(
			UCataclysmEnemyRarity::LoadEnemyRarityTable(), Creature->RarityStep);
		return FString::Printf(TEXT("%s (%s)"), *Creature->GetName(),
							   Rarity.IsEmpty() ? *FString::Printf(TEXT("rarity step %d"), Creature->RarityStep)
												: *Rarity);
	}
	return Attacker->GetName();
}

FString FCataclysmHitLog::WhatText(const FCataclysmHitNotice& Notice)
{
	const FString Skill = Notice.SkillName.IsNone() ? FString() : Notice.SkillName.ToString();

	// A TICK IS NAMED BY ITS AILMENT, which its effect grants rather than carries, and by the skill that applied it
	// when one did: a burn from a player's skill arrives carrying that skill.
	if (Notice.bDamageOverTime)
	{
		FString Ailment = CataclysmHitLog::AilmentIn(Notice.GrantedTags);
		if (Ailment.IsEmpty())
		{
			Ailment = CataclysmHitLog::AilmentIn(Notice.EffectTags);
		}
		if (Ailment.IsEmpty())
		{
			Ailment = TEXT("damage over time");
		}
		return Skill.IsEmpty() ? Ailment : FString::Printf(TEXT("%s of %s"), *Ailment, *Skill);
	}

	if (!Skill.IsEmpty())
	{
		return Skill;
	}

	// NO SKILL: a creature's blow, or one returned for a blow taken. Named by the announcement's own facts. A spell
	// that fires a projectile is both a spell and ranged, and is called a spell.
	if (Notice.bIsSpell)
	{
		return TEXT("spell");
	}
	if (Notice.bIsRanged)
	{
		return TEXT("ranged attack");
	}
	if (Notice.bIsMelee)
	{
		return TEXT("melee attack");
	}
	return TEXT("attack");
}

FCataclysmHitLog::FBlow FCataclysmHitLog::BlowOf(const FCataclysmHitNotice& Notice, float HealthBefore,
												 float MaxHealth)
{
	FBlow Blow;
	Blow.Attacker = AttackerText(Notice.Attacker, Notice.Target);
	Blow.What = WhatText(Notice);
	Blow.Landed = Notice.Landed;
	Blow.ToHealth = Notice.DealtToHealth;
	Blow.bCritical = Notice.bCritical;
	Blow.bBlocked = Notice.bBlocked;
	Blow.bRetaliation = Notice.bRetaliation;
	Blow.bDamageOverTime = Notice.bDamageOverTime;
	Blow.HealthBefore = HealthBefore;
	Blow.MaxHealth = MaxHealth;
	return Blow;
}

FString FCataclysmHitLog::Description(const FBlow& Blow)
{
	using CataclysmHitLog::NumberText;

	const FString Ticks = Blow.Ticks > 0
		? FString::Printf(TEXT(", %d tick%s within one second"), Blow.Ticks, Blow.Ticks == 1 ? TEXT("") : TEXT("s"))
		: FString();
	FString Out = FString::Printf(TEXT("%s from %s, %s%s: %s to health"), *NumberText(Blow.Landed), *Blow.Attacker,
								  *Blow.What, *Ticks, *NumberText(Blow.ToHealth));

	// THE FLAGS B1 NAMES, and blocked, which the announcement also carries. Only those that are true.
	if (Blow.bCritical)
	{
		Out += TEXT("; critical");
	}
	if (Blow.bBlocked)
	{
		Out += TEXT("; blocked");
	}
	if (Blow.bRetaliation)
	{
		Out += TEXT("; reflected or retaliation");
	}
	if (Blow.bDamageOverTime)
	{
		Out += TEXT("; damage over time");
	}
	return Out;
}

FString FCataclysmHitLog::Line(const FBlow& Blow)
{
	using CataclysmHitLog::NumberText;

	return FString::Printf(TEXT("%stook %s; health %s of %s before %s"), Prefix, *Description(Blow),
						   *NumberText(Blow.HealthBefore), *NumberText(Blow.MaxHealth),
						   Blow.Ticks > 0 ? TEXT("the first") : TEXT("it"));
}

FString FCataclysmHitLog::OutgoingLine(const FCataclysmHitNotice& Notice)
{
	using CataclysmHitLog::NumberText;

	// ON WHOM, named as a blow taken names who struck: a creature with its rarity, "itself" for a blow the character
	// dealt itself (Echo Chamber's copy), "nobody" when no target was named.
	FString Out = FString::Printf(TEXT("%s%s on %s: sent %s"), OutgoingPrefix, *WhatText(Notice),
								  *AttackerText(Notice.Target, Notice.Attacker), *NumberText(Notice.Sent));
	if (Notice.bEvaded)
	{
		return Out + TEXT(", evaded");
	}
	Out += FString::Printf(TEXT(", landed %s, %s to health"), *NumberText(Notice.Landed),
						   *NumberText(Notice.DealtToHealth));

	// THE FLAGS A BLOW TAKEN SAYS, but damage over time, which this line is never said for.
	if (Notice.bCritical)
	{
		Out += TEXT("; critical");
	}
	if (Notice.bBlocked)
	{
		Out += TEXT("; blocked");
	}
	if (Notice.bRetaliation)
	{
		Out += TEXT("; reflected or retaliation");
	}
	return Out;
}

FString FCataclysmHitLog::CreatureDeathLine(const AActor* Creature, float Health, float MaxHealth,
											const AActor* LastStruckBy)
{
	using CataclysmHitLog::NumberText;

	// THE CREATURE AS A BLOW'S LINE NAMES IT, with its rarity; WHO STRUCK LAST AS A BLOW'S LINE NAMES AN ATTACKER, so
	// "itself" for a creature that dealt its own last blow.
	const FString StruckBy = LastStruckBy != nullptr
		? AttackerText(LastStruckBy, Creature)
		: FString(TEXT("nobody on record"));
	return FString::Printf(TEXT("%s%s at %s of %s health; last struck by %s"), CreatureDeathPrefix,
						   *AttackerText(Creature, nullptr), *NumberText(Health), *NumberText(MaxHealth), *StruckBy);
}

TArray<FString> FCataclysmHitLog::Note(const FCataclysmHitNotice& Notice, float HealthBefore, float MaxHealth,
									   double Now)
{
	// THE TOTALS THAT HAVE RUN OUT FIRST, so the lines come out in the order the blows were taken.
	TArray<FString> Lines = Due(Now);

	if (Notice.Landed <= 0.0f)
	{
		return Lines;
	}

	const FBlow Blow = BlowOf(Notice, HealthBefore, MaxHealth);
	LastBlow = Blow;
	LastBlowAt = Now;
	bLastBlowTookTheHealthLeft = Blow.ToHealth > 0.0f && Blow.ToHealth >= HealthBefore;

	if (!Blow.bDamageOverTime)
	{
		Lines.Add(Line(Blow));
		return Lines;
	}

	// ONE TOTAL PER ATTACKER AND AILMENT. A tick that reaches a total `Due` has just closed opens a new one.
	const FString Key = Blow.Attacker + TEXT("|") + Blow.What;
	if (FTotal* Open = Totals.FindByPredicate([&Key](const FTotal& Total) { return Total.Key == Key; }))
	{
		Open->Sum.Landed += Blow.Landed;
		Open->Sum.ToHealth += Blow.ToHealth;
		Open->Sum.bCritical |= Blow.bCritical;
		Open->Sum.bBlocked |= Blow.bBlocked;
		Open->Sum.bRetaliation |= Blow.bRetaliation;
		++Open->Sum.Ticks;
	}
	else
	{
		FTotal Opened;
		Opened.Key = Key;
		Opened.Sum = Blow;
		Opened.Sum.Ticks = 1;
		Opened.OpenedAt = Now;
		Totals.Add(MoveTemp(Opened));
	}
	return Lines;
}

TArray<FString> FCataclysmHitLog::Due(double Now)
{
	TArray<FString> Lines;
	for (int32 Index = 0; Index < Totals.Num();)
	{
		if (Now - Totals[Index].OpenedAt >= WindowSeconds)
		{
			Lines.Add(Line(Totals[Index].Sum));
			Totals.RemoveAt(Index);
		}
		else
		{
			++Index;
		}
	}
	return Lines;
}

TArray<FString> FCataclysmHitLog::All()
{
	TArray<FString> Lines;
	for (const FTotal& Total : Totals)
	{
		Lines.Add(Line(Total.Sum));
	}
	Totals.Reset();
	return Lines;
}

FString FCataclysmHitLog::DeathClause(double Now) const
{
	// THE SAME MOMENT: the death is handled inside the write of the blow that killed, which is announced, and so
	// noted here, just before that write. A blow noted at another moment did not kill.
	if (bLastBlowTookTheHealthLeft && LastBlowAt == Now)
	{
		return TEXT("the killing blow: ") + Description(LastBlow);
	}

	const FString Other = TEXT("no blow killed it: health reached zero another way (a cost, a debt or a direct write)");
	if (LastBlowAt < 0.0)
	{
		return Other + TEXT("; no blow had been taken");
	}
	return FString::Printf(TEXT("%s; the last blow taken was %s, %.1f s earlier"), *Other, *Description(LastBlow),
						   Now - LastBlowAt);
}
