// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonModifierEffects.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
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
const TCHAR* UCataclysmDungeonModifierEffects::MortalDecayKey =
	TEXT("Death_Mortal_Decay");
const TCHAR* UCataclysmDungeonModifierEffects::WastingSicknessKey =
	TEXT("Famine_Wasting_Sickness");
const TCHAR* UCataclysmDungeonModifierEffects::GraspingTentaclesKey =
	TEXT("Void_Grasping_Tentacles");
const TCHAR* UCataclysmDungeonModifierEffects::EdictOfSilenceKey =
	TEXT("Celestial_Edict_of_Silence");
const TCHAR* UCataclysmDungeonModifierEffects::ArtilleryStrikeKey =
	TEXT("War_Artillery_Strike");
const TCHAR* UCataclysmDungeonModifierEffects::HallowedGroundfallKey =
	TEXT("Celestial_Hallowed_Groundfall");
const TCHAR* UCataclysmDungeonModifierEffects::SporeCloudsKey =
	TEXT("Pestilence_Spore_Clouds");
const TCHAR* UCataclysmDungeonModifierEffects::HellfireKey =
	TEXT("Demonic_Hellfire");
const TCHAR* UCataclysmDungeonModifierEffects::BrandOfTheAggressorKey =
	TEXT("Demonic_Brand_of_the_Aggressor");
const TCHAR* UCataclysmDungeonModifierEffects::FungalOvergrowthKey =
	TEXT("Pestilence_Fungal_Overgrowth");
const TCHAR* UCataclysmDungeonModifierEffects::IllusoryEnemiesKey =
	TEXT("Chaos_Illusory_Enemies");

// THE COLOURS EACH KIND OF MUSHROOM IS DRAWN IN, which are row keys of
// `game/Data/ElementVisuals.csv` and not damage types this rule deals. The
// header says at length why the pair is these two and why neither name reaches
// anything but the drawing.
const TCHAR* UCataclysmDungeonModifierEffects::FungalOvergrowthBoostDrawnAs =
	TEXT("Celestial");
const TCHAR* UCataclysmDungeonModifierEffects::FungalOvergrowthSlowDrawnAs =
	TEXT("Void");

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
	// THE ROWS BELOW ARE FULLY BUILT: each does everything its row describes --
	// The Nihil's Embrace including its cleanse on a high tier enemy's defeat,
	// Death's Embrace including the reset on a new floor, the Field Medic
	// including "it does not attack", and Mortal Decay including the slowing a
	// kill buys -- so none of them is "partly". Count the arms rather than
	// reading a number here.
	//
	// THIS COMMENT USED TO WRITE THE COUNT OUT AND IT WENT STALE TWICE: it said
	// SIX until Withered Ground made it seven, then SEVEN until Mortal Decay
	// made it eight. Issue #1786. Correcting the number restores it for exactly
	// one commit, which is why the number is gone rather than updated -- the
	// same reasoning the struct comment in the header now carries, and the same
	// as `FCataclysmPlayerFloorEffects`'s "THE FIELDS BELOW THE FIRST THREE".
	//
	// THE FIELD MEDIC WAS `Partly` UNTIL ISSUE #1680, and it was marked so
	// deliberately: the healing worked and nothing in the game could stop a
	// creature attacking, so calling it built would have been a wrong answer
	// in the one place the project asks what is finished. #1680 built the
	// missing half, so the answer changes.
	if (RowKey == FName(StarvationKey) || RowKey == FName(DehydrationKey)
		|| RowKey == FName(ForcedMarchKey) || RowKey == FName(NihilsEmbraceKey)
		|| RowKey == FName(DeathsEmbraceKey) || RowKey == FName(FieldMedicKey)
		|| RowKey == FName(WitheredGroundKey) || RowKey == FName(MortalDecayKey)
		|| RowKey == FName(WastingSicknessKey)
		|| RowKey == FName(GraspingTentaclesKey)
		|| RowKey == FName(EdictOfSilenceKey)
		|| RowKey == FName(ArtilleryStrikeKey)
		|| RowKey == FName(HallowedGroundfallKey)
		|| RowKey == FName(SporeCloudsKey)
		|| RowKey == FName(HellfireKey)
		|| RowKey == FName(BrandOfTheAggressorKey)
		|| RowKey == FName(FungalOvergrowthKey)
		|| RowKey == FName(IllusoryEnemiesKey))
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
	// TWO KEYS WERE MISSING FROM HERE BEFORE ISSUE #1677. This list and
	// `BuiltStateOf` above are two statements of the same fact, and nothing
	// made them agree: `DeathsEmbraceKey` was returned as Built and was absent
	// from here. The test that reads these walks THIS list and asks
	// `BuiltStateOf` about each entry, so a key missing from here never enters
	// the loop and the gap could not be seen from either end. Since #1677 a
	// second test walks the table instead, which is the direction that catches
	// an absence.
	//
	// THIS COMMENT USED TO OPEN WITH THE COUNT AND WARN THAT COUNTS GO STALE,
	// AND IT WENT STALE ANYWAY -- it said EIGHT while the list below held ten.
	// A warning attached to a number does not maintain the number, so the
	// number is gone rather than corrected. Issue #1786. What holds this list
	// honest is the pair of automation tests named above, one walking each
	// direction, and not a word here.
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
		FName(MortalDecayKey),
		FName(WastingSicknessKey),
		FName(GraspingTentaclesKey),
		FName(EdictOfSilenceKey),
		FName(ArtilleryStrikeKey),
		FName(HallowedGroundfallKey),
		FName(SporeCloudsKey),
		FName(HellfireKey),
		FName(BrandOfTheAggressorKey),
		FName(FungalOvergrowthKey),
		FName(IllusoryEnemiesKey),
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

bool UCataclysmDungeonModifierEffects::EdictOfSilenceIsDue(float SecondsSinceLast)
{
	// A NEGATIVE WAIT BRINGS NOTHING, which is the reading every counting rule in
	// this file takes of a figure it cannot have been given honestly. It also
	// keeps a caller that has never started its clock from being silenced on its
	// first beat.
	if (SecondsSinceLast < 0.0f)
	{
		return false;
	}

	// AT OR PAST, NOT PAST. The beat is a quarter of a second and the cadence is
	// ninety, so insisting on strictly past would put every silence one beat
	// later than the row says for no reason anybody could observe.
	return SecondsSinceLast >= EdictOfSilenceEverySeconds;
}

float UCataclysmDungeonModifierEffects::SkillsLockedWhile(bool bSilenced)
{
	// EVERY SILENCE IS THE SAME. The row describes one that prevents all skill
	// usage, not one that prevents more of it at depth or after a while, so there
	// is nothing here to scale with.
	return bSilenced ? EdictOfSilenceLockValue : 0.0f;
}

bool UCataclysmDungeonModifierEffects::GraspingTentacleIsDue(
	float SecondsSinceLast, int32 Alive)
{
	// THE CAP FIRST, so a floor already carrying its limit does no arithmetic and
	// does not swallow the clock: the caller keeps counting, so the beat one is
	// destroyed on places the next at once rather than waiting a further cadence.
	if (Alive >= GraspingTentaclesMostOnAFloor)
	{
		return false;
	}

	// AT OR PAST THE CADENCE, NOT PAST IT. The beat is a quarter of a second, so
	// insisting on strictly past would put every tentacle one beat later than the
	// figure says for no reason anybody could observe.
	return SecondsSinceLast >= GraspingTentaclesSecondsBetween;
}

float UCataclysmDungeonModifierEffects::GraspMovementLessPercentWhile(
	bool bGrabbed)
{
	// EVERY GRAB IS WORTH THE SAME. The row describes being grabbed, not being
	// grabbed harder, so there is nothing here to scale with.
	return bGrabbed ? GraspingTentaclesGrabMovementLessPercent : 0.0f;
}

int32 UCataclysmDungeonModifierEffects::WastingSicknessStacksAfterHit(
	int32 Stacks, bool bInflicts)
{
	// A COUNT BELOW NOTHING IS NOTHING, which is the reading every other counting
	// rule in this file takes of a figure it cannot have been given honestly.
	const int32 Held = FMath::Max(0, Stacks);
	if (!bInflicts)
	{
		return FMath::Min(Held, WastingSicknessMostStacks);
	}

	// THE CAP IS APPLIED TO THE RESULT AND NOT CHECKED BEFORE THE ADD, so a
	// caller that somehow holds more than the cap is brought back to it rather
	// than being allowed to keep what it has.
	return FMath::Min(Held + 1, WastingSicknessMostStacks);
}

float UCataclysmDungeonModifierEffects::WastingSicknessMaximumsLessPercent(
	int32 Stacks)
{
	return FMath::Clamp(Stacks, 0, WastingSicknessMostStacks)
		* WastingSicknessPercentPerStack;
}

float UCataclysmDungeonModifierEffects::MortalDecayPercentPerSecond(
	int32 FloorNumber, bool bSlowedByAKill)
{
	// THE DEPTH AND NOT THE WALK, AND THE SHARED PER-FLOOR ARITHMETIC RATHER
	// THAN A SECOND COPY OF IT. `ShareTakenOnFloor` already answers "N floors at
	// this rate, capped", refuses a floor of zero or below, and carries the
	// judgement that floor 1 counts. Repeating the multiply here would be a
	// second place for those to be decided.
	const float Rate = ShareTakenOnFloor(MortalDecayPercentPerSecondPerFloor,
										 MortalDecayMostPercentPerSecond,
										 FloorNumber);
	if (!bSlowedByAKill)
	{
		return Rate;
	}

	// THE SLOW IS TAKEN OFF THE CAPPED RATE, WHICH IS THE ONLY ORDER THAT KEEPS
	// THE ROW'S SECOND SENTENCE TRUE. Slowing the uncapped rate and capping
	// afterwards would leave any floor past 20 at the ceiling either way -- 0.1
	// times 20 halved is exactly the cap -- so reaping would buy nothing at
	// precisely the depths where the row asks it to buy the most.
	return Rate * (1.0f - MortalDecaySlowPercent / 100.0f);
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

	// AND WASTING SICKNESS, ON THE SAME TWO STATS AS STARVATION AND DEHYDRATION
	// AND FROM ITS OWN TWO FIELDS. Issues #1786 and #41.
	//
	// TWO MORE MULTIPLIERS RATHER THAN LARGER VERSIONS OF THE TWO ABOVE, WHICH IS
	// WHAT THE SEPARATE FIELDS BUY. `DungeonModifierEffectsAddMultiplier` appends
	// to the same stat's list, so a floor carrying Starvation and Wasting
	// Sickness gives `max_health` two entries, and `UCataclysmStatPipeline`
	// multiplies each source on its own -- its own comment says they are "NOT
	// summed first". Ten per cent and fifteen per cent leave 0.9 x 0.85 of the
	// maximum rather than 0.75 of it.
	//
	// NOTHING AT ALL FOR A PLAYER CARRYING NO STACKS, because the helper refuses
	// a share of zero, so a floor without this row adds no entry.
	DungeonModifierEffectsAddLess(Modifiers, DungeonModifierEffectsMaxHealthStat,
								  Effects.SicknessMaxHealthLessPercent);
	DungeonModifierEffectsAddLess(Modifiers, DungeonModifierEffectsMaxManaStat,
								  Effects.SicknessMaxManaLessPercent);

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

	// AND THE EDICT OF SILENCE, ON THE STAT THAT SAYS WHETHER SKILLS MAY BE USED.
	// Issues #1786 and #41.
	//
	// FLAT AND NOT A MULTIPLIER, for the reason Death's Embrace gives above:
	// `skill_locked` is zero for every class, and a multiplier on zero is zero
	// however large it is.
	//
	// UNSCOPED, WHICH IS THE WHOLE DIFFERENCE FROM THE TWO ENCHANTMENTS THAT
	// WRITE THIS STAT. `UCataclysmAbilitySystemComponent::StatForSkill` reads it
	// with the skill's own tags, so a value carrying `RequiredTags` reaches only
	// skills that match. A dungeon rule's modifier carries no tags, so it reaches
	// every skill -- which is what "preventing all skill usage" asks for, and
	// what the two slot-scoped enchantment rows deliberately do not do.
	//
	// AND BASIC ATTACKS NEED NOTHING HERE. `UCataclysmSkillTemplate::
	// CanActivateAbility` skips the lock check for that slot unconditionally, so
	// the row's "Only basic attacks function during this period" is already true
	// of an unscoped lock without this file knowing about the slot at all.
	//
	// THE STAT'S CANONICAL NAME RATHER THAN A SIXTH SPELLING IN THIS FILE. The
	// five stat names above are repeated here as local constants, which the
	// comment on them defends; this one is taken from
	// `UCataclysmSkillSlots::LockedStat`, the same constant the code that ENFORCES
	// the lock reads, so the writer and the reader of this stat cannot drift apart
	// by a typo.
	DungeonModifierEffectsAddFlat(Modifiers, UCataclysmSkillSlots::LockedStat,
								  Effects.SkillsLockedValue);

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

	// AND GRASPING TENTACLES, ON THE SAME STAT AND FROM ITS OWN FIELD. Issues
	// #1786 and #41.
	//
	// A SECOND MULTIPLIER RATHER THAN A LARGER VERSION OF THE ONE ABOVE, WHICH IS
	// WHY THE FIELD IS SEPARATE. Both rows are Void, so a floor can carry a
	// Singularity Well and a tentacle at once; sharing the field would mean
	// whichever rule wrote second erased the first, which is issue #1765. As two
	// entries the pipeline multiplies each on its own, and a player slowed 40% by
	// a well and grabbed at 99% moves at 0.6 x 0.01 of their speed rather than at
	// some single figure neither rule chose.
	DungeonModifierEffectsAddLess(Modifiers,
								  DungeonModifierEffectsMovementSpeedStat,
								  Effects.GraspMovementLessPercent);

	// AND FUNGAL OVERGROWTH, ON THE SAME STAT AGAIN AND FROM ITS OWN TWO FIELDS.
	// Issues #1820 and #41. Three rows now move `movement_speed` and each holds
	// its own field, for the reason the paragraph above gives.
	//
	// ONE OF THEM IS A `More` AND NOT A `Less`, WHICH MAKES THIS THE FIRST
	// DUNGEON RULE TO RAISE THE SPEED A PLAYER WALKS AT. The helper is signed
	// and `DungeonModifierEffectsAddLess` is a wrapper that negates, so raising
	// a stat needs no new machinery -- `ResistanceMorePercent` has called the
	// signed helper directly since issue #41's second slice and this copies it.
	//
	// THE TWO COMPOSE RATHER THAN CANCEL, which is the pipeline's doing and not
	// this rule's. A player standing where both kinds overlap gets 1.5 x 0.5,
	// which is three quarters of their speed; adding the fields first would give
	// them all of it, and neither figure in the row says the two undo each other.
	DungeonModifierEffectsAddMultiplier(Modifiers,
										DungeonModifierEffectsMovementSpeedStat,
										Effects.MushroomSpeedMorePercent);
	DungeonModifierEffectsAddLess(Modifiers,
								  DungeonModifierEffectsMovementSpeedStat,
								  Effects.MushroomSpeedLessPercent);

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

	// AND WASTING SICKNESS, AS ONE CLAUSE FOR ITS TWO FIELDS. Issues #1786 and
	// #41. The row states one figure for both maximums, so saying it twice would
	// tell a player two things where the row told them one.
	//
	// SAID SEPARATELY FROM STARVATION'S AND DEHYDRATION'S CLAUSES ABOVE, THOUGH
	// IT MOVES THE SAME TWO STATS. A player on a floor carrying both is under two
	// rules with two different cures -- one ends with the floor and one ends with
	// a boss -- so one combined figure would hide which of them to act on.
	//
	// THE TWO FIELDS CANNOT DISAGREE, so the first is printed. They are written
	// from one stack count through one function; if a later change gives them
	// separate sources this clause has to be split, and
	// `tools/tests/test_every_floor_effect_field_is_read_by_both_readers.py`
	// is what fails if either field stops being named here at all.
	// AND A GRAB, SAID AS WHAT IT IS RATHER THAN AS A PERCENTAGE. Issues #1786
	// and #41. "held by a tentacle" tells a player why they cannot move and that
	// it will pass; "movement speed 99% less" tells them a number and leaves them
	// to work out that they are not stunned.
	//
	// SAID SEPARATELY FROM THE SLOW ABOVE, THOUGH IT MOVES THE SAME STAT, because
	// a floor can carry both rows and their cures differ: a well is walked out
	// of, a grab ends on its own.
	if (Effects.GraspMovementLessPercent > 0.0f)
	{
		Clauses.Add(TEXT("held by a tentacle"));
	}

	// AND WHAT A MUSHROOM UNDERFOOT IS DOING, IN WHICHEVER DIRECTION. Issues
	// #1820 and #41. Said separately from the two slows above, though all three
	// move the same stat, because a floor can carry all three rows and their
	// cures differ: a well is walked out of, a grab ends on its own, and a
	// mushroom is stepped off.
	//
	// TWO CLAUSES AND NOT ONE, because a player standing where both kinds
	// overlap is under both and one clause would have to print a figure neither
	// mushroom has.
	if (Effects.MushroomSpeedMorePercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("movement speed %.0f%% more"),
									Effects.MushroomSpeedMorePercent));
	}
	if (Effects.MushroomSpeedLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(TEXT("movement speed %.0f%% less from a "
										 "mushroom"),
									Effects.MushroomSpeedLessPercent));
	}

	// AND THE EDICT OF SILENCE, SAID AS WHAT THE PLAYER CAN STILL DO. Issues
	// #1786 and #41. "skills silenced, basic attacks only" answers the question a
	// silenced player is actually asking, which is what to press; "skill_locked
	// 1" would answer a question nobody has.
	//
	// THIS SENTENCE IS NOT WHAT FIXES THE SILENCE PROBLEM, and saying so here
	// stops it being mistaken for the fix. `Describe` has one caller today -- the
	// per-floor log line -- so this reaches a log rather than a player mid-fight.
	// Issue #1810 is that nothing in the interface reads the lock at all.
	if (Effects.SkillsLockedValue > 0.0f)
	{
		Clauses.Add(TEXT("skills silenced, basic attacks only"));
	}

	if (Effects.SicknessMaxHealthLessPercent > 0.0f
		|| Effects.SicknessMaxManaLessPercent > 0.0f)
	{
		Clauses.Add(FString::Printf(
			TEXT("maximum health and mana %.0f%% less from wasting sickness"),
			FMath::Max(Effects.SicknessMaxHealthLessPercent,
					   Effects.SicknessMaxManaLessPercent)));
	}
	return FString::Join(Clauses, TEXT(", "));
}

bool UCataclysmDungeonModifierEffects::ArtilleryStrikeIsDue(
	float SecondsSinceLast, bool bOneInTheAir)
{
	// ONE AT A TIME, ASKED BEFORE THE CLOCK. The shape `GraspingTentacleIsDue`
	// uses, for its reason: while a circle is on the ground no arithmetic is
	// done and the caller goes on counting, so the beat it lands on can place
	// the next rather than waiting a further thirty seconds.
	//
	// ONE AT A TIME IS NOT A NUMBER SOMEBODY CHOSE. The row says "a massive red
	// circle", singular, and a second circle drawn while the first is still on
	// the ground would make the warning ambiguous -- the player could not tell
	// which one was about to land.
	if (bOneInTheAir)
	{
		return false;
	}

	return SecondsSinceLast >= ArtilleryStrikeSecondsBetween;
}

bool UCataclysmDungeonModifierEffects::ArtilleryStrikeHasLanded(
	float SecondsSinceItAppeared)
{
	return SecondsSinceItAppeared >= ArtilleryStrikeWarningSeconds;
}

float UCataclysmDungeonModifierEffects::ArtilleryStrikeDamage(float MaximumHealth)
{
	// NOTHING FROM A TARGET WHOSE MAXIMUM HEALTH IS UNKNOWN. A creature whose
	// attributes have not been set yet answers zero, and a strike that dealt
	// zero damage would still count as a hit -- it would announce itself, feed
	// anything listening for a blow, and read in a log as a strike that landed
	// for nothing. The caller checks this and skips instead.
	if (MaximumHealth <= 0.0f)
	{
		return 0.0f;
	}

	return MaximumHealth * ArtilleryStrikeMaxHealthPercent / 100.0f;
}

bool UCataclysmDungeonModifierEffects::HallowedGroundfallIsDue(float SecondsSinceLast)
{
	// NO CAP ON CRATERS, AND THAT IS ARITHMETIC RATHER THAN AN OVERSIGHT. A
	// bombardment leaves its craters every thirty seconds and each burns for
	// fifteen, so the last are gone before the next arrive and the floor can
	// never carry more than one bombardment's worth. The static assertion that
	// a crater outlasts neither the gap nor the empowerment is what keeps that
	// true if either figure moves.
	//
	// NO ALIVE COUNT IS ASKED FOR EITHER, which is the difference from
	// `InfernalRainPatchIsDue` and `GraspingTentacleIsDue`. Both of those place
	// ONE thing on a short clock and need a cap to stop a floor filling up. This
	// places a fixed number on a long one.
	return SecondsSinceLast >= HallowedGroundfallSecondsBetween;
}

float UCataclysmDungeonModifierEffects::HallowedGroundfallBurnPerSecond(
	float MaximumHealth)
{
	if (MaximumHealth <= 0.0f)
	{
		return 0.0f;
	}

	return MaximumHealth * HallowedGroundfallPercentPerSecond / 100.0f;
}

bool UCataclysmDungeonModifierEffects::SporeCloudsRelease(float Roll)
{
	return Roll < SporeCloudsChancePercentOnDeath;
}

bool UCataclysmDungeonModifierEffects::SporeCloudsReach(float DistanceCm)
{
	// AT OR WITHIN, UNLIKE THE CHANCE ABOVE. A distance exactly equal to the
	// reach is inside it, which is the reading every sweep in the game already
	// makes: `ACataclysmGroundZone::Covers` asks `UCataclysmTargeting::IsInLine`,
	// and that compares `<=` against the half width squared. The two comparisons
	// differ because one is a random draw and the other is a place.
	return DistanceCm >= 0.0f && DistanceCm <= SporeCloudsReachCm;
}

bool UCataclysmDungeonModifierEffects::HellfireExplodes(float Roll)
{
	return Roll < HellfireChancePercentOnDeath;
}

float UCataclysmDungeonModifierEffects::HellfireDamage(float CreatureAttackDamage)
{
	if (CreatureAttackDamage <= 0.0f)
	{
		return 0.0f;
	}

	return CreatureAttackDamage * HellfireExplosionHits;
}

int32 UCataclysmDungeonModifierEffects::BrandStacksAfterHit(int32 Stacks,
														   bool bBrands)
{
	if (!bBrands)
	{
		return Stacks;
	}

	const int32 Raised = Stacks + 1;

	// THE COUNT THAT ERUPTS IS THE COUNT THAT CLEARS. See the header: left at the
	// threshold, every later blow would erupt again.
	return Raised >= BrandStacksToErupt ? 0 : Raised;
}

bool UCataclysmDungeonModifierEffects::BrandErupts(int32 StacksBeforeThisBlow)
{
	// ASKED OF THE COUNT BEFORE THE BLOW, because the count after it has already
	// been cleared by the line above and cannot answer this. The caller raises
	// and asks in either order only if the two read the same field, which is the
	// bug this signature prevents.
	return StacksBeforeThisBlow + 1 >= BrandStacksToErupt;
}

bool UCataclysmDungeonModifierEffects::FungalOvergrowthBoosts(float Roll)
{
	return Roll < FungalOvergrowthBoostChancePercent;
}

bool UCataclysmDungeonModifierEffects::IllusoryEnemiesIsAnIllusion(float Roll)
{
	// BELOW AND NOT AT OR BELOW, the comparison every roll in this file makes,
	// so a roll of exactly the share falls on the real side and a pinned 0
	// always gives an illusion.
	return Roll < IllusoryEnemiesSharePercent;
}

float UCataclysmDungeonModifierEffects::BrandNovaDamage(float MaximumHealth)
{
	if (MaximumHealth <= 0.0f)
	{
		return 0.0f;
	}

	return MaximumHealth * BrandNovaMaxHealthPercent / 100.0f;
}
