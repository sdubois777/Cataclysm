// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonModifierEffects.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"

const TCHAR* UCataclysmDungeonModifierEffects::StarvationKey = TEXT("Famine_Starvation");
const TCHAR* UCataclysmDungeonModifierEffects::DehydrationKey = TEXT("Famine_Dehydration");
const TCHAR* UCataclysmDungeonModifierEffects::DeathsEmbraceKey =
	TEXT("Death_Death_s_Embrace");
const TCHAR* UCataclysmDungeonModifierEffects::ForcedMarchKey =
	TEXT("War_Forced_March");
const TCHAR* UCataclysmDungeonModifierEffects::InfernalRainKey =
	TEXT("Demonic_Infernal_Rain");
const TCHAR* UCataclysmDungeonModifierEffects::NihilsEmbraceKey =
	TEXT("Void_The_Nihil_s_Embrace");
const TCHAR* UCataclysmDungeonModifierEffects::FieldMedicKey =
	TEXT("War_Field_Medic");

const TCHAR* UCataclysmDungeonModifierEffects::SingularityWellsKey =
	TEXT("Void_Singularity_Wells");
const TCHAR* UCataclysmDungeonModifierEffects::WitheredGroundKey =
	TEXT("Famine_Withered_Ground");

namespace
{
	/**
	 * The character-sheet stat names the three maximums are written under.
	 *
	 * THE SAME SPELLINGS `UCataclysmPlayerClassStats::StatToAttribute` USES, and
	 * they have to be: a modifier keyed by a name that map does not hold is not
	 * written anywhere, silently. `Cataclysm.DungeonModifierEffects.Starvation
	 * LowersThePlayersMaximumsAndLeavingGivesThemBack` is what fails if either
	 * side is renamed.
	 *
	 * NAMED FOR THIS FILE, because Unreal merges a module's `.cpp` files into one
	 * translation unit and `tools/tests/test_no_two_files_share_an_anonymous_helper.py`
	 * fails when two files declare the same helper.
	 */
	const TCHAR* const DungeonModifierEffectsMaxHealthStat = TEXT("max_health");
	const TCHAR* const DungeonModifierEffectsMaxEnergyShieldStat = TEXT("max_energy_shield");
	const TCHAR* const DungeonModifierEffectsMaxManaStat = TEXT("max_mana");
	const TCHAR* const DungeonModifierEffectsHealingReceivedStat =
		TEXT("healing_received_reduction");
	const TCHAR* const DungeonModifierEffectsMovementSpeedStat =
		TEXT("movement_speed");

	/**
	 * The four stats Withered Ground's row calls "Health and Mana recovery
	 * (regen/leech)".
	 *
	 * THE SPELLINGS ARE THE ONES THE REST OF THE GAME USES, not new ones.
	 * `UCataclysmRegeneration::HealthRegenStat` and `ManaRegenStat` are the
	 * same two strings, and the leech pair are keys of
	 * `UCataclysmPlayerClassStats::StatToAttribute`. They are repeated here
	 * rather than included because this file already keeps its other five stat
	 * names this way, and the Python test on the row's wording is what holds
	 * them honest.
	 */
	const TCHAR* const DungeonModifierEffectsHealthRegenStat = TEXT("health_regen");
	const TCHAR* const DungeonModifierEffectsManaRegenStat = TEXT("mana_regen");
	const TCHAR* const DungeonModifierEffectsLifeLeechStat = TEXT("life_leech");
	const TCHAR* const DungeonModifierEffectsManaLeechStat = TEXT("mana_leech");

	/**
	 * One multiplier from a dungeon rule, or nothing for a value of nothing.
	 *
	 * SIGNED, AND THE SIGN IS WHAT MAKES A LESS. The pipeline multiplies by
	 * (1 + Value / 100), so -10 is x0.9 and +10 is x1.1, and it floors a Less at
	 * -99 so no rule can take a number to nothing. The most any rule here takes
	 * is 60, well inside that.
	 *
	 * ONE PLACE BUILDS THE MODIFIER, which is why the two callers below pass a
	 * sign rather than each building their own.
	 */
	void DungeonModifierEffectsAddMultiplier(
		TMap<FName, TArray<FCataclysmStatModifier>>& Into, FName Stat, float Value)
	{
		if (FMath::IsNearlyZero(Value))
		{
			return;
		}

		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::More;
		Modifier.Source = ECataclysmModifierSource::DungeonRule;
		Modifier.Value = Value;

		Into.FindOrAdd(Stat).Add(Modifier);
	}

	/** One Less multiplier from a dungeon rule, or nothing for a share of zero. */
	void DungeonModifierEffectsAddLess(
		TMap<FName, TArray<FCataclysmStatModifier>>& Into, const TCHAR* Stat,
		float LessPercent)
	{
		if (LessPercent <= 0.0f)
		{
			return;
		}

		DungeonModifierEffectsAddMultiplier(Into, FName(Stat), -LessPercent);
	}

	/**
	 * One flat addition from a dungeon rule, or nothing for a value of nothing.
	 *
	 * FLAT AND NOT A MULTIPLIER, WHICH IS THE WHOLE REASON THIS EXISTS. The two
	 * above scale a stat that already has a value: maximum health, a resistance.
	 * A stat that is zero for every class cannot be moved by scaling it, and
	 * `healing_received_reduction` is exactly that -- a reduction nobody carries
	 * until something applies one. The flat bucket adds to the base before
	 * anything multiplies, in the stat's own units, which here are percentage
	 * points.
	 *
	 * A DUNGEON RULE MAY USE ANY BUCKET. The pipeline refuses gear the More
	 * bucket and this is not gear; `ECataclysmModifierSource::DungeonRule` is
	 * the same source the two above declare.
	 */
	void DungeonModifierEffectsAddFlat(
		TMap<FName, TArray<FCataclysmStatModifier>>& Into, const TCHAR* Stat,
		float Value)
	{
		if (Value <= 0.0f)
		{
			return;
		}

		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Flat;
		Modifier.Source = ECataclysmModifierSource::DungeonRule;
		Modifier.Value = Value;

		Into.FindOrAdd(FName(Stat)).Add(Modifier);
	}
}

ECataclysmModifierBuilt UCataclysmDungeonModifierEffects::BuiltStateOf(FName RowKey)
{
	// SEVEN ARE BUILT. Slice 2 added Forced March and The Nihil's Embrace, slice
	// 5 Death's Embrace, issue #1648 the Field Medic, and Withered Ground is
	// the seventh. Each does everything
	// its row describes -- The Nihil's Embrace including its cleanse on a high
	// tier enemy's defeat, Death's Embrace including the reset on a new floor,
	// and the Field Medic including "it does not attack" -- so none is
	// "partly".
	//
	// THE FIELD MEDIC WAS `Partly` UNTIL ISSUE #1680, and it was marked so
	// deliberately: the healing worked and nothing in the game could stop a
	// creature attacking, so calling it built would have been a wrong answer
	// in the one place the project asks what is finished. #1680 built the
	// missing half, so the answer changes.
	if (RowKey == FName(StarvationKey) || RowKey == FName(DehydrationKey)
		|| RowKey == FName(ForcedMarchKey) || RowKey == FName(NihilsEmbraceKey)
		|| RowKey == FName(DeathsEmbraceKey) || RowKey == FName(FieldMedicKey)
		|| RowKey == FName(WitheredGroundKey))
	{
		return ECataclysmModifierBuilt::Built;
	}

	// THE ROWS BELOW ARE PARTLY BUILT, EACH FOR ITS OWN REASON. Issue #1760:
	// this line used to write the count out and said "two" while listing
	// three, because a comment counting the thing under it goes wrong without
	// being touched. Count the arms rather than reading a number here.
	//
	// UNSTABLE DIMENSIONS. Its rule draws another dungeon modifier onto the
	// floor, where the row asks for "a new, random modifier to all enemies on the
	// next floor", and it adds that modifier on floor 1 as well, where no floor
	// has been cleared. Question 3 of the modifier plan asks the owner which it
	// should draw.
	//
	// INFERNAL RAIN. Its burning ground is built, typed off its own row and timed
	// to the ten seconds the row states; nothing draws a fireball falling into
	// it, so "fireballs rain" is not what a player sees. Issue #1699. Saying
	// `Built` here would put a wrong answer on the floor panel, which is the one
	// place the project tells the player what is finished -- the same reason the
	// Field Medic was held at `Partly` until #1680.
	//
	// SINGULARITY WELLS. Its orbs are placed, they deal void damage read off the
	// row's own type, and standing in one slows the player by the 40% the row
	// states. **Nothing pulls**, and the row names the pull before anything else,
	// so this cannot be `Built`. Pulling the player and pulling a projectile are
	// two further pieces of work; the key's comment in the header says what each
	// needs and why neither is a line or two.
	if (RowKey == FName(FCataclysmDungeonFloorRules::UnstableDimensionsKey)
		|| RowKey == FName(InfernalRainKey)
		|| RowKey == FName(SingularityWellsKey))
	{
		return ECataclysmModifierBuilt::Partly;
	}

	return ECataclysmModifierBuilt::NotBuilt;
}

bool UCataclysmDungeonModifierEffects::InfernalRainPatchIsDue(
	float SecondsSinceLastPatch, int32 PatchesAlive)
{
	// THE CAP IS CHECKED FIRST, so a floor already carrying its limit does no
	// arithmetic and, more importantly, does not swallow the clock: the caller
	// keeps counting and drops one the instant a patch expires, rather than
	// waiting a further whole cadence.
	if (PatchesAlive >= InfernalRainMostPatches)
	{
		return false;
	}

	// AT OR PAST, NOT PAST. The beat is a quarter of a second and the cadence is
	// five, so twenty beats in twenty-one answer no; insisting on strictly past
	// would put every patch one beat later than the figure says for no reason
	// anybody could observe.
	return SecondsSinceLastPatch >= InfernalRainSecondsBetweenPatches;
}

bool UCataclysmDungeonModifierEffects::SingularityWellIsDue(
	float SecondsSinceLastWell, int32 WellsAlive)
{
	// THE CAP FIRST, so a floor already carrying its limit does no arithmetic and
	// does not swallow the clock: the caller keeps counting, so the beat a well is
	// destroyed on places the next one at once.
	if (WellsAlive >= SingularityWellsMostWells)
	{
		return false;
	}

	// AT OR PAST, NOT PAST. The beat is a quarter of a second and the cadence is
	// eight, so insisting on strictly past would put every well one beat later
	// than the figure says for no reason anybody could observe.
	return SecondsSinceLastWell >= SingularityWellsSecondsBetweenWells;
}

float UCataclysmDungeonModifierEffects::SingularityWellDamagePerSecond(
	float MaximumHealth)
{
	// A CHARACTER WITH NO MAXIMUM HEALTH TAKES NOTHING, rather than a negative
	// figure reaching the well. A zero here would place a well that does nothing,
	// and the rule that places them refuses that rather than relying on the
	// ground zone to notice -- `ACataclysmGroundZone::Sweep` skips a patch only
	// when it neither damages nor applies an effect, which is a rule about other
	// patches.
	if (MaximumHealth <= 0.0f)
	{
		return 0.0f;
	}

	return MaximumHealth * SingularityWellsPercentPerSecond / 100.0f;
}

float UCataclysmDungeonModifierEffects::InfernalRainDamagePerSecond(
	float MaximumHealth)
{
	// A CHARACTER WITH NO MAXIMUM HEALTH TAKES NOTHING, rather than a negative
	// figure reaching the patch. A zero here would produce a patch that does
	// nothing at all, which is the right answer for a reading nobody can have.
	//
	// WHY "DOES NOTHING AT ALL" AND NOT "IS REFUSED". `ACataclysmGroundZone::Sweep`
	// skips a patch only when it neither damages nor applies an effect. An earlier
	// version of this comment said the sweep refuses a non-positive damage
	// outright, which WAS true and stopped being true with issue #1701: a patch
	// carrying an effect and no damage now does sweep, because Singularity Wells
	// needs a well that slows without damaging. Infernal Rain's patches carry no
	// effect, so for them a zero damage still means a patch that does nothing.
	if (MaximumHealth <= 0.0f)
	{
		return 0.0f;
	}

	return MaximumHealth * InfernalRainPercentPerSecond / 100.0f;
}

TArray<FName> UCataclysmDungeonModifierEffects::KeysWithARule()
{
	// EIGHT, AND TWO WERE MISSING BEFORE ISSUE #1677. This list and
	// `BuiltStateOf` above are two statements of the same fact, and nothing
	// made them agree: `DeathsEmbraceKey` was returned as Built and was absent
	// from here. The test that reads these walks THIS list and asks
	// `BuiltStateOf` about each entry, so a key missing from here never enters
	// the loop and the gap could not be seen from either end. Since #1677 a
	// second test walks the table instead, which is the direction that catches
	// an absence.
	//
	// THE COUNT IN THIS COMMENT IS THE KIND OF THING THAT GOES STALE. It is here
	// because it made the #1677 gap visible to a reader, and the two tests are
	// what actually hold it. Count the entries rather than trusting the word.
	return {
		FName(StarvationKey),
		FName(DehydrationKey),
		FName(ForcedMarchKey),
		FName(NihilsEmbraceKey),
		FName(DeathsEmbraceKey),
		FName(FieldMedicKey),
		FName(InfernalRainKey),
		FName(SingularityWellsKey),
		FName(WitheredGroundKey),
		FName(FCataclysmDungeonFloorRules::UnstableDimensionsKey),
	};
}

float UCataclysmDungeonModifierEffects::ShareTakenOnFloor(float PercentPerFloor,
														  float MostPercent,
														  int32 FloorNumber)
{
	if (FloorNumber <= 0 || PercentPerFloor <= 0.0f || MostPercent <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Min(PercentPerFloor * static_cast<float>(FloorNumber), MostPercent);
}

int32 UCataclysmDungeonModifierEffects::ForcedMarchStacksAfter(
	float SecondsStoodStill)
{
	// NOTHING UNTIL THE ROW'S THRESHOLD, and nothing at all for a character that
	// cannot be asked: a negative wait means "no character to read", which is the
	// same reading the movement conditions refuse on. Both fail this comparison.
	if (SecondsStoodStill < ForcedMarchSecondsBeforeDamage)
	{
		return 0;
	}

	// ONE A SECOND PAST THE THRESHOLD, in whole seconds, so three seconds exactly
	// is the first stack and four seconds is the second.
	const int32 Stacks =
		1 + FMath::FloorToInt(SecondsStoodStill - ForcedMarchSecondsBeforeDamage);
	return FMath::Min(Stacks, ForcedMarchMostStacks);
}

float UCataclysmDungeonModifierEffects::ForcedMarchSharePerSecond(int32 Stacks)
{
	return FMath::Max(0, Stacks) * ForcedMarchPercentPerStackPerSecond;
}

float UCataclysmDungeonModifierEffects::NihilsEmbraceResistanceLost(
	float MetresWalked)
{
	if (MetresWalked <= 0.0f || NihilsEmbraceMetresPerResistancePercent <= 0.0f)
	{
		return 0.0f;
	}

	const float Points = FMath::FloorToFloat(
		MetresWalked / NihilsEmbraceMetresPerResistancePercent);
	return FMath::Min(Points, NihilsEmbraceMostResistancePercent);
}

int32 UCataclysmDungeonModifierEffects::DeathsEmbraceStacksAfter(
	float SecondsOnFloor)
{
	// A SIGN TEST AND NOT A THRESHOLD, AND IT WAS WRITTEN AS A THRESHOLD UNTIL A
	// GUARD PROOF SHOWED THE THRESHOLD DECIDED NOTHING. Issue #41, slice 5. It
	// read `SecondsOnFloor < DeathsEmbraceSecondsPerStack`, which looks like the
	// rule -- no stack before ten seconds -- and is already answered by the
	// floor division below: FloorToInt(9.9 / 10) is 0. The only input the two
	// disagree about is a NEGATIVE one, where the division gives
	// FloorToInt(-0.5) == -1 and a caller would be handed a negative stack
	// count. So the condition does the work of a sign test and now says so.
	//
	// THE SECOND HALF GUARDS A DIVISION, not a rule. The interval is a
	// compile-time 10.0f and can never be zero today; taking the test out would
	// leave a division by a constant somebody could later set to zero, and
	// FloorToInt of an infinity is undefined.
	if (SecondsOnFloor <= 0.0f || DeathsEmbraceSecondsPerStack <= 0.0f)
	{
		return 0;
	}

	// WHOLE STACKS, COUNTED DOWN. A player eleven seconds into a floor carries
	// one and not one and a tenth: the row calls them stacks of a debuff, and a
	// fractional stack is not a thing a player could be shown.
	const int32 Stacks = FMath::FloorToInt(
		SecondsOnFloor / DeathsEmbraceSecondsPerStack);
	return FMath::Min(Stacks, DeathsEmbraceMostStacks);
}

float UCataclysmDungeonModifierEffects::DeathsEmbraceHealingLessPercent(
	int32 Stacks)
{
	// CLAMPED HERE AS WELL AS IN THE COUNT ABOVE, because this is public and a
	// caller holding a stack count from somewhere else must not be able to ask
	// for more than the cap. The attribute clamps at a hundred too, so three
	// things would have to be wrong at once for a player to be unhealable.
	return FMath::Clamp(Stacks, 0, DeathsEmbraceMostStacks)
		* DeathsEmbracePercentPerStack;
}

FCataclysmPlayerFloorEffects UCataclysmDungeonModifierEffects::PlayerEffectsFor(
	const TArray<FName>& FloorModifiers, int32 FloorNumber)
{
	FCataclysmPlayerFloorEffects Effects;

	// STARVATION: HEALTH AND SHIELD TOGETHER, by the same share. The row names
	// both in one sentence and gives them one number.
	if (FloorModifiers.Contains(FName(StarvationKey)))
	{
		const float Share = ShareTakenOnFloor(
			StarvationPercentPerFloor, StarvationMostPercent, FloorNumber);
		Effects.MaxHealthLessPercent = Share;
		Effects.MaxEnergyShieldLessPercent = Share;
	}

	// DEHYDRATION: "MAXIMUM RESOURCE", READ AS MAXIMUM MANA. Mana is what every
	// class's skills are paid from (`UCataclysmGameplayAbility::CheckCost`); a
	// class resource such as Fervour is built up in a fight and starts empty, so
	// taking a share of its maximum would take nothing a player had. A judgement,
	// recorded in `docs/DECISIONS.md`.
	if (FloorModifiers.Contains(FName(DehydrationKey)))
	{
		Effects.MaxManaLessPercent = ShareTakenOnFloor(
			DehydrationPercentPerFloor, DehydrationMostPercent, FloorNumber);
	}

	return Effects;
}

TMap<FName, TArray<FCataclysmStatModifier>> UCataclysmDungeonModifierEffects::StatModifiersFor(
	const FCataclysmPlayerFloorEffects& Effects)
{
	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers;
	DungeonModifierEffectsAddLess(Modifiers, DungeonModifierEffectsMaxHealthStat,
								  Effects.MaxHealthLessPercent);
	DungeonModifierEffectsAddLess(Modifiers, DungeonModifierEffectsMaxEnergyShieldStat,
								  Effects.MaxEnergyShieldLessPercent);
	DungeonModifierEffectsAddLess(Modifiers, DungeonModifierEffectsMaxManaStat,
								  Effects.MaxManaLessPercent);

	// AND THE NIHIL'S EMBRACE, ON ALL EIGHT RESISTANCES. Issue #41, slice 2. The
	// row says "your resistances", and this game holds one resistance per
	// Cataclysm damage type rather than a single number, so that is eight
	// modifiers rather than one.
	//
	// BUILT FROM THE SHIPPING LIST OF DAMAGE TYPES rather than eight names typed
	// here, so a type renamed in the design workbook moves this with it.
	//
	// THE LOSS AND THE REWARD SHARE EVERY STAT AND THE SAME BUCKET, so the
	// pipeline adds them: a cleanse puts the loss back to nothing and starts the
	// reward, and a character that walks again while the reward runs carries both
	// at once.
	for (const FName DamageType : UCataclysmItemModifiers::DamageTypeNames())
	{
		const FName Stat = UCataclysmItemModifiers::ResistanceStatFor(DamageType);
		DungeonModifierEffectsAddMultiplier(Modifiers, Stat,
											-Effects.ResistanceLessPercent);
		DungeonModifierEffectsAddMultiplier(Modifiers, Stat,
											Effects.ResistanceMorePercent);
	}

	// AND DEATH'S EMBRACE, ON THE STAT THAT SAYS HOW MUCH HEALING ARRIVES. Issue
	// #41, slice 5. One stat rather than eight, because the reduction is read at
	// each site that restores health rather than being spread over pools.
	//
	// FLAT, AND SEE `DungeonModifierEffectsAddFlat` FOR WHY: this stat is zero
	// for every class, so there is nothing for a multiplier to scale.
	DungeonModifierEffectsAddFlat(Modifiers,
								  DungeonModifierEffectsHealingReceivedStat,
								  Effects.HealingReceivedLessPercent);

	// AND SINGULARITY WELLS, ON THE SPEED THE CHARACTER WALKS AT. Issues #1605
	// and #41.
	//
	// A MULTIPLIER RATHER THAN A FLAT TAKE, because `movement_speed` is a real
	// number for every class -- 4.0 by default, 4.6 for the Ravager, 3.5 for the
	// Ritualist -- so a share of it means the same thing to each of them, and the
	// row says "by 40%" rather than by an amount.
	//
	// NOTHING ELSE IS NEEDED TO REACH THE CHARACTER, which is worth saying because
	// it looks too easy. `movement_speed` is in
	// `UCataclysmPlayerClassStats::StatToAttribute`, so it is recorded; writing
	// the attribute fires the delegate
	// `ACataclysmPlayerCharacter::OnMovementSpeedChanged` bound in
	// `InitAbilityActorInfo`; that re-reads the attribute through
	// `RefreshMovementSpeed` and writes `MaxWalkSpeed`. The whole chain existed
	// before this rule.
	DungeonModifierEffectsAddLess(Modifiers,
								  DungeonModifierEffectsMovementSpeedStat,
								  Effects.MovementSpeedLessPercent);

	// AND WITHERED GROUND, WHICH IS ONE FIELD AND FOUR STATS. The row states
	// one figure for all of them: "your Health and Mana recovery (regen/leech)
	// is reduced by 80%".
	//
	// THE REGENERATION PAIR REACH THE CHARACTER AND THE LEECH PAIR MOSTLY DO
	// NOT, and that is a property of the class data rather than of this code.
	// A Less multiplies, so a multiplier on a base of zero is zero.
	// `game/Data/ClassStats.csv` gives `health_regen` and `mana_regen` a
	// Default row, so every class carries both; it gives `life_leech` to the
	// Ravager alone and gives no class any `mana_leech`. All four are written
	// because the row names leech, because the reduction is right for whoever
	// does carry it, and because it becomes right for everyone the day a class
	// line or an affix grants leech -- with no change here.
	//
	// `UCataclysmRegeneration::ApplyStep` IS WHAT PICKS THE FIRST TWO UP. It
	// reads each rate by stat name through `StatForSkill` with an empty tag
	// container -- its own comment says "NO TAGS, because nothing is
	// happening" -- so an unscoped rule like this one applies to both. Leech
	// reads its attribute directly, which the recorded modifiers reach the
	// same way every other dungeon rule's do.
	DungeonModifierEffectsAddLess(Modifiers,
								  DungeonModifierEffectsHealthRegenStat,
								  Effects.RecoveryLessPercent);
	DungeonModifierEffectsAddLess(Modifiers,
								  DungeonModifierEffectsManaRegenStat,
								  Effects.RecoveryLessPercent);
	DungeonModifierEffectsAddLess(Modifiers,
								  DungeonModifierEffectsLifeLeechStat,
								  Effects.RecoveryLessPercent);
	DungeonModifierEffectsAddLess(Modifiers,
								  DungeonModifierEffectsManaLeechStat,
								  Effects.RecoveryLessPercent);

	return Modifiers;
}

bool UCataclysmDungeonModifierEffects::ApplyToCharacter(
	const FCataclysmPlayerFloorEffects& Effects,
	UCataclysmAbilitySystemComponent* AbilitySystem,
	UCataclysmEquipmentComponent* Equipment)
{
	if (!AbilitySystem)
	{
		return false;
	}

	// HELD FIRST, AND HELD EVEN WHEN THERE IS NO EQUIPMENT TO REFRESH WITH, so
	// the next refresh made for any reason picks them up.
	AbilitySystem->SetDungeonStatModifiers(StatModifiersFor(Effects));

	if (!Equipment)
	{
		return false;
	}

	Equipment->RefreshAttributes(AbilitySystem);
	return true;
}

FString UCataclysmDungeonModifierEffects::Describe(const FCataclysmPlayerFloorEffects& Effects)
{
	TArray<FString> Clauses;
	if (Effects.MaxHealthLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("maximum health %.0f%% less"),
									Effects.MaxHealthLessPercent));
	}
	if (Effects.MaxEnergyShieldLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("maximum energy shield %.0f%% less"),
									Effects.MaxEnergyShieldLessPercent));
	}
	if (Effects.MaxManaLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("maximum mana %.0f%% less"),
									Effects.MaxManaLessPercent));
	}

	// THE NIHIL'S EMBRACE, IN BOTH DIRECTIONS. Issue #41, slice 2. The per-floor
	// log and the floor panel both read this, and a rule taking a player's
	// resistances with nothing saying so would read as a fault in the game.
	if (Effects.ResistanceLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("all resistances %.0f%% less"),
									Effects.ResistanceLessPercent));
	}
	if (Effects.ResistanceMorePercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("all resistances %.0f%% more"),
									Effects.ResistanceMorePercent));
	}

	// AND DEATH'S EMBRACE. Issue #41, slice 5. Said as what the player loses
	// rather than as a stack count, because the floor panel is read by someone
	// deciding whether to go on and "healing 30% less" answers that where "3
	// stacks of Embrace of Death" does not.
	if (Effects.HealingReceivedLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("healing %.0f%% less"),
									Effects.HealingReceivedLessPercent));
	}

	// AND THE SLOW A PLAYER IS UNDER RIGHT NOW. Added for Singularity Wells
	// in #1719 and left out of here, while being present in `IsEmpty` and in
	// `StatModifiersFor`. Issue #41.
	//
	// NOTHING OBSERVABLE WAS WRONG, AND SAYING SO IS THE POINT. This function
	// has exactly one caller -- the floor's log line in
	// `ACataclysmDungeonGameMode::ApplyFloorRulesToPlayer` -- and that caller
	// passes the output of `PlayerEffectsFor`, which fills the three per-floor
	// fields and leaves the four beat-driven ones at zero. So the clause that
	// was missing could not have fired through the only route that reaches it.
	//
	// THE GAP IS REAL ANYWAY, and it is the shape rather than the symptom. The
	// function takes a whole `FCataclysmPlayerFloorEffects` and is named for
	// describing one; `ApplyChangingFloorEffects` builds a complete one four
	// times a second. The first caller that passes a full struct -- a floor
	// panel showing what is on the player right now is the obvious one -- gets
	// a sentence with one effect silently absent from it, and nothing would
	// report that. The Python test named after this pair of readers,
	// `test_every_floor_effect_field_is_read_by_both_readers.py` under
	// `tools/tests/`, now fails when a field is missing from here.
	//
	// WORTH KNOWING IF YOU ADD A CALLER: this is the only field that reports
	// where the player is STANDING rather than what the floor is. It turns on
	// and off as they walk in and out of a well, where every other clause holds
	// for a floor or changes slowly.
	if (Effects.MovementSpeedLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("movement speed %.0f%% less"),
									Effects.MovementSpeedLessPercent));
	}

	// AND WITHERED GROUND, SAID AS ONE CLAUSE FOR FOUR STATS. The field is one
	// figure covering health regeneration, mana regeneration and both leeches,
	// and "recovery 80% less" is what the row itself calls them together.
	// Naming all four would be longer and would tell a player about two stats
	// most characters do not have.
	if (Effects.RecoveryLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("health and mana recovery %.0f%% less"),
									Effects.RecoveryLessPercent));
	}
	return FString::Join(Clauses, TEXT(", "));
}
