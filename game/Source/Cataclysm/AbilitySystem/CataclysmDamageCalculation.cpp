// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDamageCalculation.h"
// For asking the defender for a stat instead of reading its attribute. #1022.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmGameMode.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"

// The two stats saying what share of a hit a character takes. Issue #1026.
// `UCataclysmPlayerClassStats` names both, in two maps, so a literal here and a
// literal there could drift apart with nothing to notice.
const TCHAR* UCataclysmDamageCalculation::DamageTakenStat =
	TEXT("damage_taken");
const TCHAR* UCataclysmDamageCalculation::DamageOverTimeTakenStat =
	TEXT("damage_over_time_taken");
const TCHAR* UCataclysmDamageCalculation::DebuffDamageSuppressedStat =
	TEXT("debuff_damage_suppressed");

namespace
{
	/**
	 * One of the defender's own stats, asked for rather than read.
	 *
	 * WHY ASKING IS NOT THE SAME AS READING. Issue #1022. A modifier that
	 * carries a condition or a scale is never folded into a gameplay attribute:
	 * `UCataclysmPlayerClassStats::ApplyTo` evaluates the pipeline with a default
	 * `FCataclysmStatConditions`, in which every reading is unknown, so a
	 * conditional row answers false and a scaling row answers nothing. That is
	 * deliberate -- a bonus whose size depends on where health is must not be
	 * written onto an attribute where it would be stale the moment the next blow
	 * landed -- but it means a plain attribute read throws such a row away, and
	 * nothing reports it. The arithmetic runs and the number is simply the
	 * unmodified one.
	 *
	 * TWO MASOCHIST NODES ARE WHY THIS EXISTS. Battle-Scarred grants "+2%
	 * increased Armor per point for each unique debuff on you" and Endurance in
	 * Suffering grants damage reduction the same way. Every conditioned or
	 * scaled row authored before them sat on a stat that was already asked for.
	 *
	 * THE ATTRIBUTE IS THE FALLBACK AND NOT THE ANSWER. `StatForSkill` returns
	 * the fallback when no stat line was recorded for this character at all,
	 * which is ordinary rather than a fault: every enemy is in that case, because
	 * `ACataclysmEnemyCharacter` writes its armour straight onto the attribute.
	 * A player with a stat line re-evaluates the same base and the same modifier
	 * list the attribute was built from, so the two agree except on the rows this
	 * exists to rescue.
	 *
	 * NO SKILL TAGS, BECAUSE THIS IS THE DEFENDER'S STAT. `RequiredTags` scopes a
	 * modifier to the skill in the CHARACTER'S OWN hand, and the character here
	 * is being hit rather than swinging. An empty container is the honest reading
	 * and it is what the character sheet passes for the same stat.
	 */
	float DefenderStat(const UAbilitySystemComponent* Defender,
					   const TCHAR* Stat, float FromAttribute)
	{
		const UCataclysmAbilitySystemComponent* Asking =
			Cast<const UCataclysmAbilitySystemComponent>(Defender);
		return Asking ? Asking->StatForSkill(FName(Stat), FGameplayTagContainer(),
											 FromAttribute)
					  : FromAttribute;
	}

	/**
	 * How much of a hit of this type the defender resists.
	 *
	 * IT ASKS THE DEFENDER WHICH KIND OF RESISTANCE IT HAS, because the two sides
	 * of a fight hold different attribute sets and never both. The project owner
	 * ruled it on 2026-08-12:
	 *
	 *     an ENEMY holds UCataclysmAllResistanceAttributeSet: one figure, met by
	 *     a hit of any type including an untyped one
	 *
	 *     a PLAYER holds UCataclysmResistanceAttributeSet: eight figures, and the
	 *     hit's own damage type selects which one applies
	 *
	 * The two are added rather than one winning, so a character that somehow held
	 * both would get a defined answer instead of an accidental one. Nothing holds
	 * both today.
	 *
	 * AN UNTYPED HIT STILL MEETS THE GENERIC FIGURE. That is what the generic
	 * figure is for. Player damage carries no type -- the enemy resists everything
	 * equally, so a type would be choosing between copies of one number -- and
	 * before issue #486 an untyped hit found no slot to read, so every resistance
	 * on either side did nothing at all.
	 */
	float ResistanceFor(const UAbilitySystemComponent* Defender,
						FName DamageType, int32 DifficultyTier)
	{
		if (!Defender)
		{
			return 0.0f;
		}

		float Total = 0.0f;
		if (const UCataclysmAllResistanceAttributeSet* Generic =
				Defender->GetSet<UCataclysmAllResistanceAttributeSet>())
		{
			Total += Generic->GetAllResistance();
		}

		// THE PAIRING OF A DAMAGE TYPE WITH ITS RESISTANCE SLOT MOVED ONTO THE
		// CLASS ON 2026-09-01, because the Wand's Shred writes the same slot this
		// reads. It was a lambda table here, and a second copy of it beside the
		// debuff is how the two would have come to disagree.
		const FGameplayAttribute Typed =
			UCataclysmDamageCalculation::ResistanceAttributeFor(DamageType);

		// A damage type nobody has heard of adds nothing, and the generic figure
		// still applies, because it applies to everything by definition.
		if (Typed.IsValid() && Defender->HasAttributeSetForAttribute(Typed))
		{
			Total += Defender->GetNumericAttribute(Typed);
		}

		// AND THE DIFFICULTY TIER TAKES ITS SHARE, FOR A PLAYER ONLY. Issue
		// #1229. Subtracted here rather than clamped, because
		// `EffectiveResistance` below is what bounds the result and a
		// penalised character IS meant to be able to go negative: at
		// difficulty tier 8 one wearing no resistance at all sits at -75 and
		// takes 75% more of every damage type. That is what makes the penalty
		// a difficulty lever rather than a tax.
		Total -= UCataclysmDamageCalculation::ResistancePenaltyFor(
			Defender, DifficultyTier);

		return Total;
	}
}

const TCHAR* UCataclysmDamageCalculation::ElementTagPrefix = TEXT("Element.");
const TCHAR* UCataclysmDamageCalculation::AreaDamageTagName = TEXT("Type.AOE");
const TCHAR* UCataclysmDamageCalculation::DamageOverTimeTagName = TEXT("Keyword.DoT");
const TCHAR* UCataclysmDamageCalculation::MeleeTagName = TEXT("Type.Melee");
const TCHAR* UCataclysmDamageCalculation::NoCriticalStrikeTagName =
	TEXT("Keyword.NoCrit");
const TCHAR* UCataclysmDamageCalculation::NoPenetrationTagName =
	TEXT("Keyword.NoPenetration");
const TCHAR* UCataclysmDamageCalculation::NoWeaponSubTypeTagName =
	TEXT("Keyword.NoWeaponSubType");
const TCHAR* UCataclysmDamageCalculation::NoLeechTagName =
	TEXT("Keyword.NoLeech");
const TCHAR* UCataclysmDamageCalculation::NoRetaliationTagName =
	TEXT("Keyword.NoRetaliation");
const TCHAR* UCataclysmDamageCalculation::SkillCritChanceDataTagName =
	TEXT("Data.SkillCritChance");
const TCHAR* UCataclysmDamageCalculation::ElementIsForColourOnlyTagName =
	TEXT("Data.ElementIsForColourOnly");

namespace
{
	/**
	 * Requested by name rather than declared natively, for the same reason
	 * UCataclysmSkillEffects::BurnTag is: a native declaration would create the
	 * tag whether or not the vocabulary still lists it, which hides exactly the
	 * disagreement worth catching. `CataclysmDamageTypeTests.cpp` checks each of
	 * these is valid, because an invalid one would silently stop a property
	 * travelling and every test that did not look would still pass.
	 */
	FGameplayTag TagNamed(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Name), /*ErrorIfNotFound=*/false);
	}
}

FGameplayTag UCataclysmDamageCalculation::AreaDamageTag()
{
	return TagNamed(AreaDamageTagName);
}

FGameplayTag UCataclysmDamageCalculation::DamageOverTimeTag()
{
	return TagNamed(DamageOverTimeTagName);
}

FGameplayTag UCataclysmDamageCalculation::MeleeTag()
{
	return TagNamed(MeleeTagName);
}

FGameplayTag UCataclysmDamageCalculation::NoCriticalStrikeTag()
{
	return TagNamed(NoCriticalStrikeTagName);
}

FGameplayTag UCataclysmDamageCalculation::NoPenetrationTag()
{
	return TagNamed(NoPenetrationTagName);
}

FGameplayTag UCataclysmDamageCalculation::NoWeaponSubTypeTag()
{
	return TagNamed(NoWeaponSubTypeTagName);
}

FGameplayTag UCataclysmDamageCalculation::NoLeechTag()
{
	return TagNamed(NoLeechTagName);
}

FGameplayTag UCataclysmDamageCalculation::NoRetaliationTag()
{
	return TagNamed(NoRetaliationTagName);
}

FGameplayTag UCataclysmDamageCalculation::ElementIsForColourOnlyTag()
{
	return TagNamed(ElementIsForColourOnlyTagName);
}

FGameplayTag UCataclysmDamageCalculation::SkillCritChanceDataTag()
{
	return TagNamed(SkillCritChanceDataTagName);
}

FGameplayAttribute UCataclysmDamageCalculation::ResistanceAttributeFor(
	FName DamageType)
{
	if (DamageType.IsNone())
	{
		// An untyped hit has no slot to read, which is what the mitigation order
		// above already means by falling back to the generic figure alone.
		return FGameplayAttribute();
	}

	// BUILT ONCE ON FIRST USE RATHER THAN AS A FILE-SCOPE STATIC, for the reason
	// UCataclysmPlayerClassStats::StatToAttribute gives: an FGameplayAttribute
	// wraps an FProperty found by reflection, and that data is not ready during
	// static initialisation.
	static const TMap<FName, FGameplayAttribute> Slots = []
	{
		using Resist = UCataclysmResistanceAttributeSet;
		return TMap<FName, FGameplayAttribute>{
			{TEXT("War"),        Resist::GetWarResistanceAttribute()},
			{TEXT("Demonic"),    Resist::GetDemonicResistanceAttribute()},
			{TEXT("Death"),      Resist::GetDeathResistanceAttribute()},
			{TEXT("Pestilence"), Resist::GetPestilenceResistanceAttribute()},
			{TEXT("Famine"),     Resist::GetFamineResistanceAttribute()},
			{TEXT("Celestial"),  Resist::GetCelestialResistanceAttribute()},
			{TEXT("Chaos"),      Resist::GetChaosResistanceAttribute()},
			{TEXT("Void"),       Resist::GetVoidResistanceAttribute()},
		};
	}();

	const FGameplayAttribute* Found = Slots.Find(DamageType);
	return Found ? *Found : FGameplayAttribute();
}

FGameplayTag UCataclysmDamageCalculation::ElementTagFor(FName DamageType)
{
	if (DamageType.IsNone())
	{
		return FGameplayTag();
	}

	// Requested by name rather than declared natively, for the same reason
	// UCataclysmSkillEffects::BurnTag is: a native declaration would create the
	// tag whether or not the vocabulary still lists it, which hides exactly the
	// disagreement worth catching.
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(*FString::Printf(TEXT("%s%s"), ElementTagPrefix, *DamageType.ToString())),
		/*ErrorIfNotFound=*/false);
}

FName UCataclysmDamageCalculation::DamageTypeFromTags(const FGameplayTagContainer& Tags)
{
	for (const FGameplayTag& Tag : Tags)
	{
		const FString Name = Tag.ToString();
		if (Name.StartsWith(ElementTagPrefix))
		{
			// The leaf, so `Element.Demonic` gives `Demonic`, which is what the
			// resistance lookup above is keyed by.
			return FName(*Name.RightChop(FCString::Strlen(ElementTagPrefix)));
		}
	}
	return NAME_None;
}

float UCataclysmDamageCalculation::ArmorReduction(float Armor, int32 Tier)
{
	if (Armor <= 0.0f)
	{
		return 0.0f;
	}
	const float K = ArmorConstantPerTier * static_cast<float>(FMath::Max(1, Tier));
	return FMath::Min(ArmorReductionCap, 100.0f * Armor / (Armor + K));
}

float UCataclysmDamageCalculation::EffectiveResistance(float Resistance,
													   float Penetration)
{
	// Penetration stops at zero. Anything past the target's own resistance is
	// wasted rather than pushing the figure negative, or over-stacking becomes a
	// damage multiplier against the targets that resist least. A resistance that
	// is ALREADY negative is left where it is: that state is inflicted
	// deliberately by enchantments and penetration must not manufacture it.
	// Issue #482, and `effective_resistance` in `sim/cataclysm_sim/damage.py`
	// carries the same rule.
	const float ReachableByPenetration = FMath::Min(Resistance, 0.0f);
	const float Penetrated =
		FMath::Max(Resistance - Penetration, ReachableByPenetration);
	return FMath::Clamp(Penetrated, ResistanceFloor, ResistanceCap);
}

float UCataclysmDamageCalculation::ResistancePenaltyAt(int32 DifficultyTier)
{
	// FLOORED AT ZERO RATHER THAN ALLOWED TO GO NEGATIVE. A tier below the
	// first penalised one takes nothing off; it does not hand resistance out.
	const int32 Penalised = DifficultyTier - FirstPenalisedDifficultyTier + 1;
	return FMath::Max(0, Penalised) * ResistancePenaltyPerTier;
}

float UCataclysmDamageCalculation::ResistancePenaltyFor(
	const UAbilitySystemComponent* Defender, int32 DifficultyTier)
{
	if (!Defender)
	{
		return 0.0f;
	}

	// THE AVATAR AND NOT THE OWNER. A player's ability system is owned by the
	// player state, which is not in the world and is not the character. The
	// same distinction is why retaliation and the hit effect both reach for
	// the avatar; issues #562 and #565.
	if (!Cast<const ACataclysmPlayerCharacter>(Defender->GetAvatarActor()))
	{
		// EVERY ENEMY IN THE GAME TAKES THIS BRANCH, which is the ordinary
		// case rather than a fault.
		return 0.0f;
	}

	return ResistancePenaltyAt(DifficultyTier);
}

float UCataclysmDamageCalculation::ResistancePenaltyFor(
	const UAbilitySystemComponent* Defender)
{
	// ASKS THE WORLD, AND ONLY THE CONSOLE COMMAND NEEDS THAT.
	// `Cataclysm.ShowResistances` is printing what the player has right now
	// and has no hit to read a tier off. Every hit uses the form above,
	// because `Resolve` already knows which tier it is resolving at.
	const AActor* Avatar = Defender ? Defender->GetAvatarActor() : nullptr;
	return ResistancePenaltyFor(
		Defender, ACataclysmGameMode::DifficultyTierIn(Avatar));
}

float UCataclysmDamageCalculation::EffectiveDamageReduction(float Reduction)
{
	return FMath::Clamp(Reduction, 0.0f, DamageReductionCap);
}

float UCataclysmDamageCalculation::CombinedMoreDamageReduction(
	const TArray<float>& Factors)
{
	float Remaining = 1.0f;
	for (const float Factor : Factors)
	{
		Remaining *=
			1.0f - FMath::Clamp(Factor, 0.0f, MoreDamageReductionCap) / 100.0f;
	}

	// BOUNDED AT THE END AS WELL AS PER SOURCE, AND THAT IS NOT BELT AND BRACES.
	// "Multiplicative stacking cannot reach 100%" is true of exact arithmetic and
	// FALSE of the arithmetic this actually runs in. A float carries about seven
	// decimal digits, so forty sources of 50% leave 9.1e-13 of the damage, and
	// `100 * (1 - 9.1e-13)` rounds to exactly 100.0f. That is immunity, reached
	// by the layer the design says cannot reach it.
	//
	// FOUND BY THE TEST BELOW RATHER THAN BY READING. The Python model computes
	// the same expression in double precision, where it comes out at
	// 99.99999999999991 and stays under, so the two languages disagreed and only
	// the engine was wrong. The model is bounded the same way now, so the two
	// agree by construction rather than by both being far from the edge.
	return FMath::Min(100.0f * (1.0f - Remaining), MoreDamageReductionCap);
}

void UCataclysmDamageCalculation::StunApplication(float TotalChance,
												  float& OutChance,
												  float& OutSeconds)
{
	const float Chance = FMath::Max(0.0f, TotalChance);
	OutChance = FMath::Min(StunChanceCap, Chance);

	// EVERYTHING PAST CERTAINTY BECOMES DURATION rather than being wasted, which
	// is what stops a stun build hitting a ceiling and every point past it being
	// dead. The multiplier is never below one, so chance under 100% shortens
	// nothing.
	const float Multiplier = FMath::Max(1.0f, Chance / StunChanceCap);
	OutSeconds = FMath::Min(LongestStunSeconds,
							IncidentalStunSeconds * Multiplier);
}

FCataclysmDamageResult UCataclysmDamageCalculation::Resolve(
	const FCataclysmIncomingHit& Hit,
	const UAbilitySystemComponent* Defender,
	int32 Tier,
	float EvasionRoll,
	float BlockRoll,
	float CritRoll)
{
	FCataclysmDamageResult Result;
	if (!Defender || Hit.Damage <= 0.0f)
	{
		return Result;
	}

	const UCataclysmVitalAttributeSet* Vitals =
		Defender->GetSet<UCataclysmVitalAttributeSet>();
	const UCataclysmCombatAttributeSet* Combat =
		Defender->GetSet<UCataclysmCombatAttributeSet>();

	if (!Vitals)
	{
		// Nothing to damage. Better to do nothing than to guess.
		return Result;
	}

	// 1. Evasion. Direct attacks only; area damage lands regardless.
	if (!Hit.bIsArea && Combat)
	{
		const float Roll = EvasionRoll >= 0.0f ? EvasionRoll
											   : FMath::FRandRange(0.0f, 100.0f);
		if (Roll < Combat->GetEvasion())
		{
			Result.bEvaded = true;
			return Result;
		}
	}

	float Damage = Hit.Damage;

	// THE CRITICAL STRIKE, WHICH IS NOT ONE OF THE DESIGN'S EIGHT STEPS. Those
	// eight are what the defender does to a hit. A critical strike belongs to
	// whoever is swinging, and it multiplies the whole finished hit before any
	// mitigation touches it. That is what the model does:
	// `average_damage_per_hit` in `sim/cataclysm_sim/enemy_stats.py` scales the
	// per-hit damage, and `sim/cataclysm_sim/reference_build.py` hands the scaled
	// figure to the mitigation code as the raw hit.
	//
	// IT IS ROLLED HERE AND AVERAGED THERE, and that difference is deliberate.
	// The model has no use for a single hit, so it multiplies every hit by
	// (1 - chance + chance x multiplier), the long-run average. A game cannot do
	// that: a hit that is 15.8% larger than usual is not a critical strike and
	// cannot be drawn as one. Over many hits the two agree, which is what keeps
	// the model's damage targets true of the game.
	//
	// AFTER EVASION AND NOT BEFORE IT. Every step below is a multiplication or a
	// minimum against what is left, so where the multiplier sits among them does
	// not change the number by a fraction. It sits after the evasion step's early
	// return so that a hit which never landed is never reported as a critical
	// strike, which would put an exclamation mark on the word "Evaded".
	if (Hit.CritChance > 0.0f && Hit.CritMultiplier > 0.0f)
	{
		const float Roll = CritRoll >= 0.0f ? CritRoll
											: FMath::FRandRange(0.0f, 100.0f);
		if (Roll < Hit.CritChance)
		{
			Result.bWasCritical = true;
			Damage *= Hit.CritMultiplier / 100.0f;
		}
	}

	// 2. Block. Applies to area damage too, and removes half rather than all.
	if (Combat)
	{
		const float Roll = BlockRoll >= 0.0f ? BlockRoll
											 : FMath::FRandRange(0.0f, 100.0f);
		if (Roll < Combat->GetBlockChance())
		{
			Result.bBlocked = true;
			Damage *= 1.0f - BlockDamageReduction / 100.0f;
		}
	}

	// 3. Armor, after whatever share of it the attacker ignores.
	if (Combat)
	{
		// THE ATTACKER'S OWN STAT PLUS WHAT THE WEAPON IGNORES, clamped once at the
		// end. Mirrors `Attacker.total_armor_ignored` in
		// `sim/cataclysm_sim/damage.py`: gear and sub-type add, and the sum is what
		// the armour step sees. Issue #639 gave the sub-type half somewhere to
		// arrive from; issue #520 gave the gear half an attribute to come from.
		const float FromWeapon = Hit.bIsPiercing ? PiercingArmorIgnored : 0.0f;
		const float Ignored =
			FMath::Clamp(Hit.ArmorPenetration + FromWeapon, 0.0f, 100.0f);
		const float Armor =
			DefenderStat(Defender, TEXT("armor"), Combat->GetArmor())
			* (1.0f - Ignored / 100.0f);
		Damage *= 1.0f - ArmorReduction(Armor, Tier) / 100.0f;
	}

	// 4. Resistance, penetrated first and capped second.
	const float Resist = EffectiveResistance(
		ResistanceFor(Defender, Hit.DamageType, Tier),
		Hit.ResistancePenetration);
	Damage *= 1.0f - Resist / 100.0f;

	// 5. Flat damage reduction, capped. Until issue #644 this was the one step
	// that read a defender's attribute straight into the arithmetic with nothing
	// bounding it, so at 100 a character was exactly immune.
	if (Combat)
	{
		Damage *= 1.0f
			- EffectiveDamageReduction(
				  DefenderStat(Defender, TEXT("damage_reduction"),
							   Combat->GetDamageReduction()))
				  / 100.0f;

		// AND THE MULTIPLICATIVE BUCKET, WHICH THAT CAP DOES NOT REACH. Twelve
		// passive tree nodes grant damage reduction and call it
		// "(multiplicative)". The project owner confirmed on 2026-08-17 that
		// multiplicative means "more", the same word Path of Exile and Last Epoch
		// use, so each source removes a share of what the ones before it left
		// rather than joining the pool above. The 75% cap binds that pool only,
		// and this bucket cannot reach 100% however many sources feed it.
		//
		// ONE ATTRIBUTE HOLDING THE PRODUCT, because an attribute is one number.
		// CombinedMoreDamageReduction is where several sources are multiplied
		// together, and it is what whoever writes the attribute must use.
		//
		// EVERY CHARACTER SITS AT ZERO TODAY, so this changes nothing yet:
		// nothing in game/Source loads a passive tree, so there is no source for
		// it. Issue #665.
		Damage *= 1.0f - FMath::Clamp(Combat->GetDamageReductionMore(),
									  0.0f, MoreDamageReductionCap) / 100.0f;

		// 6. HOW MUCH DAMAGE THIS CHARACTER TAKES, as a percentage where 100 is
		// normal. Issue #1026. Three Masochist nodes move it and issue #964
		// counts five more across the other trees.
		//
		// A NINTH STEP RATHER THAN A SIXTH BUCKET ON ONE OF THE LAYERS ABOVE.
		// Those are each a named defence with a cap of its own; this is a plain
		// multiplier on the finished blow, and the design decision of 2026-08-14
		// under issue #600 says it multiplies against what the attacker's own
		// increases produced rather than joining anybody's pool.
		//
		// HERE AND NOT AMONG THE LAYERS ABOVE, THOUGH THE NUMBER IS THE SAME
		// EITHER WAY. Steps 2 to 5 are all multiplications, so a further one
		// placed anywhere among them gives an identical answer. The boundary that
		// is real is the energy shield below, which is a minimum: before it means
		// a bigger blow spends more shield, which is what "you take 20% more
		// damage" says, and after it would make a shield a partial immunity to a
		// damage-taken debuff. Path of Exile resolves a hit in this order.
		// `docs/DECISIONS.md` carries the sources.
		//
		// TWO STATS, AND A DAMAGE OVER TIME TICK MEETS BOTH. A modifier cannot be
		// scoped to the kind of blow arriving -- `RequiredTags` means the skill in
		// the DEFENDER'S own hand, as `DefenderStat` above explains -- so the
		// hit's own nature picks which stats are read, exactly as `ResistanceFor`
		// picks a resistance slot from the hit's damage type.
		//
		// FLOORED AT NOTHING RATHER THAN CLAMPED AT BOTH ENDS. There is no
		// ceiling: taking more damage is a real thing for a node to grant and
		// Communion of Pain grants it. A NEGATIVE would turn a hit into healing,
		// which no sentence in the design asks for, so the floor is here rather
		// than left to the sum of a future set of reductions.
		Damage *= FMath::Max(0.0f,
			DefenderStat(Defender, DamageTakenStat,
						 Combat->GetDamageTaken())) / 100.0f;

		if (Hit.bIsDamageOverTime)
		{
			// AND ONE CHARACTER IN THE GAME TAKES NONE OF IT AT ALL. Issue
			// #1039. The Masochist's Vessel Unbroken capstone option reads
			// "Debuffs on you deal no damage at all", which cannot be written as
			// a multiplier: `UCataclysmStatPipeline::LessMultiplierFloor` clamps
			// a Less multiplier to -99 on purpose, and 99% less is not none.
			//
			// IT ZEROES THE DAMAGE AND LEAVES THE EFFECT RUNNING, which the rest
			// of that option depends on. Its other two clauses count the debuffs
			// the character carries, so an effect removed rather than silenced
			// would make the option cancel itself. Nothing here touches the
			// effect; only the number it would have dealt.
			//
			// INSTEAD OF THE MULTIPLICATION RATHER THAN BEFORE IT, so no stat
			// that multiplies can bring the figure back above zero afterwards.
			if (DefenderStat(Defender, DebuffDamageSuppressedStat,
							 Combat->GetDebuffDamageSuppressed()) > 0.0f)
			{
				Damage = 0.0f;
			}
			else
			{
				Damage *= FMath::Max(0.0f,
					DefenderStat(Defender, DamageOverTimeTakenStat,
								 Combat->GetDamageOverTimeTaken())) / 100.0f;
			}
		}
	}

	// 7. Mana, but only for damage over time and only for a character built for
	// it. Routing damage to mana comes from an enchantment, so there is nothing
	// to read here yet; the step is left in place and does nothing.
	// See the issue on the affix pool.

	// 8. Energy shield. It does not absorb damage over time, which is what makes
	// it a distinct defence rather than a second health bar.
	const bool bShieldApplies = !Hit.bIsDamageOverTime;
	if (bShieldApplies && Vitals->GetEnergyShield() > 0.0f)
	{
		const float Magic = Hit.bIsMagic ? 1.0f + SubtypeBonus / 100.0f : 1.0f;
		Result.AbsorbedByShield =
			FMath::Min(Vitals->GetEnergyShield(), Damage * Magic);
		// Convert what the shield stopped back into raw damage, so the magic
		// bonus never destroys more raw damage than the hit contained.
		Damage = FMath::Max(0.0f, Damage - Result.AbsorbedByShield / Magic);
	}

	// 9. Health takes the remainder.
	if (Hit.bIsSlashing)
	{
		Damage *= 1.0f + SubtypeBonus / 100.0f;
	}
	Result.DealtToHealth = FMath::Min(Damage, Vitals->GetHealth());
	return Result;
}
