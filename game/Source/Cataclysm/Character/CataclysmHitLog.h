// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
struct FCataclysmHitNotice;

/**
 * THE HIT LOG: one plain line for every blow the player's character takes, so that a play session's log says what
 * killed the character. Issue #2749 slice 1, ruled by the coordinating session on 2026-11-18 (B1).
 *
 * WHY IT EXISTS. On 2026-11-18 the project owner played a level 1 character that died 11.8, 10.7 and 17.4 seconds into
 * floor 1, and nothing in the log said what hit it: `UCataclysmCombatEvents::NoteBlow` announced every blow and nothing
 * wrote one down. A Display line is what a play session's `Saved/Logs` keeps at the editor's default verbosity.
 *
 *   HIT: took 114 from CataclysmBruteCharacter_3 (Boss), melee attack: 100 to health; critical; health 130 of 130 before it
 *   HIT: took 27 from CataclysmImpCharacter_9 (Legendary), burn, 4 ticks within one second: 27 to health; damage over time; health 60 of 130 before the first
 *
 * WHO. The attacker's object name, which the line saying it spawned and the line naming its modifiers also use, so a
 * reader can find what the creature carried; and a creature's rarity. "itself" for a blow the character dealt to
 * itself (Echo Chamber's copy), "nobody" for a blow with no attacker (converted damage).
 *
 * WHAT. The skill's name when the blow carries one; the ailment for damage over time; otherwise the kind of blow from
 * the announcement's own facts: a spell, a ranged attack, a melee attack, or an attack. A CREATURE'S NAMED ABILITY
 * CANNOT BE NAMED: the damage effect it applies carries no ability name. Ruled acceptable for slice 1.
 *
 * WHICH BLOWS. Any blow the announcement says landed above nought, on mana, energy shield, a temporary pool or health.
 * Ruled 2026-11-18 (Q3): a blow a pool absorbed still reached the character. An evaded blow, and one every defence
 * removed, lands nought and says nothing.
 *
 * DAMAGE OVER TIME IS ADDED UP, one line per attacker and ailment for each second, because a burn ticks several times a
 * second and a line each would bury every other line (B1). A blow that is not damage over time is said at once.
 *
 * THE DEATH. `DeathClause` says which blow killed, or that none did. `ACataclysmPlayerCharacter::HandleDeath` puts it
 * on the character's death line (Q2). It is worked out from this log's own record because the record of the last blow
 * kept on the ability system (`FCataclysmLastBlow`) holds neither what landed nor the flags.
 *
 * AND ONE LINE FOR EVERY BLOW THE CHARACTER DEALS that is not damage over time: `OutgoingLine`, which begins "HIT OUT: ".
 * Issue #2749, ruled by the coordinating session on 2026-11-19, so a play session's log says how many blows a creature
 * took to fall and what each sent and landed; the owner's Brute on 2026-11-19 took at least seven where the model said
 * two, and nothing logged a blow the character dealt. It is said at once and never added up.
 *
 * PLAIN C++, LIKE `FCataclysmPlayLoopLog`: every line is built here from what it is given, and the character that owns
 * it says the lines, so a test can check every word and the one-second totals without a world.
 */
struct CATACLYSM_API FCataclysmHitLog
{
	/** What every line begins with, which the Output Log's filter finds. */
	static const TCHAR* const Prefix;

	/** How long damage over time from one attacker and ailment is added up before its line is said. */
	static constexpr double WindowSeconds = 1.0;

	/** One blow, or one second of ticks, as its line says it. */
	struct FBlow
	{
		/** See `AttackerText`. */
		FString Attacker;

		/** See `WhatText`. */
		FString What;

		/** What landed, on every pool together. */
		float Landed = 0.0f;

		/** The part of it that reached health. */
		float ToHealth = 0.0f;

		bool bCritical = false;
		bool bBlocked = false;
		bool bRetaliation = false;
		bool bDamageOverTime = false;

		/** Health before the blow, or before the first tick of a second's ticks, and the maximum then. */
		float HealthBefore = 0.0f;
		float MaxHealth = 0.0f;

		/** How many ticks a second's total holds. Nought for a single blow. */
		int32 Ticks = 0;
	};

	/** "CataclysmBruteCharacter_3 (Boss)", "itself", "nobody", or another actor's object name. */
	static FString AttackerText(const AActor* Attacker, const AActor* Target);

	/** The skill's name, "burn" or "burn of Molten Cleave", "spell", "ranged attack", "melee attack" or "attack". */
	static FString WhatText(const FCataclysmHitNotice& Notice);

	/** The blow a notice describes, with the health the character held before it. */
	static FBlow BlowOf(const FCataclysmHitNotice& Notice, float HealthBefore, float MaxHealth);

	/** "114 from CataclysmBruteCharacter_3 (Boss), melee attack: 100 to health; critical": a line without its health. */
	static FString Description(const FBlow& Blow);

	/** The whole line: `Prefix`, "took ", the description, and the health before it. */
	static FString Line(const FBlow& Blow);

	/** What every line of a blow dealt begins with. Not `Prefix` followed by more, so a filter on either finds one. */
	static const TCHAR* const OutgoingPrefix;

	/**
	 * The line of a blow the character dealt: "HIT OUT: Molten Cleave on CataclysmBruteCharacter_30 (Elite): sent 158,
	 * landed 145, 145 to health; critical". What struck, as `WhatText` names it; on whom, as `AttackerText` names an
	 * attacker; `FCataclysmHitNotice::Sent`, which is before the critical strike multiplier; what landed on every pool
	 * and on health; and the flags that are true. AN EVADED BLOW says only "sent 158, evaded", since nothing landed.
	 */
	static FString OutgoingLine(const FCataclysmHitNotice& Notice);

	/**
	 * Notes a blow the character took at world time `Now`, and answers the lines to say now: every second's total
	 * that has run out by `Now`, then this blow's own line unless it is damage over time, which is added to its
	 * attacker's and ailment's total instead. A blow that landed nought answers only the totals that ran out.
	 */
	TArray<FString> Note(const FCataclysmHitNotice& Notice, float HealthBefore, float MaxHealth, double Now);

	/** The lines of every total that has run `WindowSeconds` or more by `Now`, which are then closed. */
	TArray<FString> Due(double Now);

	/** The lines of every total still open, which are then closed: for a death and for the end of play. */
	TArray<FString> All();

	/** Whether any total is open, which is whether the owner's timer still has anything to say. */
	bool HasOpenTotals() const { return Totals.Num() > 0; }

	/**
	 * What killed the character, for its death line at world time `Now`: "the killing blow: <description>" when the
	 * last blow noted came at `Now` and took at least the health held before it; otherwise "no blow killed it: health
	 * reached zero another way (a cost, a debt or a direct write)", and the last blow taken and how long before, or
	 * that none was.
	 */
	FString DeathClause(double Now) const;

private:
	/** One attacker's and ailment's ticks for one second. */
	struct FTotal
	{
		FString Key;
		FBlow Sum;
		double OpenedAt = 0.0;
	};

	TArray<FTotal> Totals;

	/** The last blow noted, one tick of damage over time included, and when. */
	FBlow LastBlow;
	double LastBlowAt = -1.0;

	/** Whether that blow took at least the health held before it. */
	bool bLastBlowTookTheHealthLeft = false;
};
