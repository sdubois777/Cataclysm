// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillShape.h"
#include "Cataclysm.h"
#include "GameplayTagsManager.h"

namespace
{
	/**
	 * The shape names, spelled exactly as the Shape column writes them.
	 *
	 * The generator holds the same eight names in SHAPE_PARAMS. They are two
	 * lists of the same thing and they can disagree, so
	 * Cataclysm.SkillShape.EveryShapeInTheDataHasATemplate compares them: it
	 * reads every Shape value out of the generated table and fails on one this
	 * function does not know.
	 *
	 * THEY DID DISAGREE, AND THE TEST WAS THE ONLY THING THAT SAID SO. Deployable
	 * was added to the generator on issue #338 and not added here, so for as long
	 * as nobody ran the full automation suite the three skills naming it were
	 * granted a placeholder that filled the slot and did nothing. Issue #621.
	 */
	struct FShapeName
	{
		ECataclysmSkillShape Shape;
		const TCHAR* Name;
	};

	constexpr FShapeName ShapeNames[] = {
		{ ECataclysmSkillShape::Strike,     TEXT("Strike")     },
		{ ECataclysmSkillShape::Projectile, TEXT("Projectile") },
		{ ECataclysmSkillShape::SelfBuff,   TEXT("SelfBuff")   },
		{ ECataclysmSkillShape::Movement,   TEXT("Movement")   },
		{ ECataclysmSkillShape::Summon,     TEXT("Summon")     },
		{ ECataclysmSkillShape::Deployable, TEXT("Deployable") },
		{ ECataclysmSkillShape::Aura,       TEXT("Aura")       },
		{ ECataclysmSkillShape::Debuff,     TEXT("Debuff")     },
	};

	struct FMovementModeName
	{
		ECataclysmMovementMode Mode;
		const TCHAR* Name;
	};

	constexpr FMovementModeName MovementModeNames[] = {
		{ ECataclysmMovementMode::Leap,   TEXT("Leap")   },
		{ ECataclysmMovementMode::Charge, TEXT("Charge") },
		{ ECataclysmMovementMode::Blink,  TEXT("Blink")  },
		{ ECataclysmMovementMode::Swap,   TEXT("Swap")   },
		{ ECataclysmMovementMode::Recall, TEXT("Recall") },
		{ ECataclysmMovementMode::Flicker, TEXT("Flicker") },
	};
}

ECataclysmSkillShape UCataclysmSkillShapes::ShapeFromName(const FString& ShapeName)
{
	for (const FShapeName& Entry : ShapeNames)
	{
		if (ShapeName.Equals(Entry.Name, ESearchCase::IgnoreCase))
		{
			return Entry.Shape;
		}
	}
	return ECataclysmSkillShape::None;
}

FString UCataclysmSkillShapes::NameOfShape(ECataclysmSkillShape Shape)
{
	for (const FShapeName& Entry : ShapeNames)
	{
		if (Entry.Shape == Shape)
		{
			return Entry.Name;
		}
	}
	return FString();
}

FGameplayTag UCataclysmSkillShapes::StatusTagFor(const FString& EffectName)
{
	if (EffectName.IsEmpty())
	{
		return FGameplayTag();
	}

	// A tag segment allows only letters, digits and underscores, and the effect
	// names in the design carry spaces and apostrophes. The same rule is applied
	// by tag_segment in tools/generate_gameplay_tags.py, which is what creates
	// the tag this then asks for.
	FString Segment;
	Segment.Reserve(EffectName.Len());
	for (const TCHAR Character : EffectName)
	{
		if (FChar::IsAlnum(Character))
		{
			Segment.AppendChar(Character);
		}
	}
	if (Segment.IsEmpty())
	{
		return FGameplayTag();
	}

	// THE THREE BRANCHES IN TURN, AND THE VOCABULARY DECIDES WHICH. Issue #1145.
	// A status effect's tag now carries the sheet it came from --
	// `Status.Debuff.Cripple`, `Status.Buff.Commander`, `Status.DoT.Bleed` -- so
	// that anything asking whether an effect harms can read the tag instead of
	// keeping a list of names by hand.
	//
	// ASKED RATHER THAN LOOKED UP, so this needs no data table and cannot
	// disagree with the tag list. `tools/generate_gameplay_tags.py` refuses to
	// emit one effect name under two branches, so at most one of these three is
	// a declared tag and the order below cannot change the answer.
	//
	// THE CALLER STILL PASSES ONLY A NAME. Every skill cell in
	// `game/Data/WeaponSkills.csv` writes `Effect=Cripple`, which is the effect's
	// name and says nothing about its kind, and that is what makes the sheet the
	// single place the kind is decided.
	static const TCHAR* const Branches[] = {TEXT("Debuff"), TEXT("Buff"),
											TEXT("DoT")};
	for (const TCHAR* const Branch : Branches)
	{
		const FGameplayTag Found = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(*FString::Printf(TEXT("Status.%s.%s"), Branch, *Segment)),
			/*ErrorIfNotFound=*/false);
		if (Found.IsValid())
		{
			return Found;
		}
	}

	return FGameplayTag();
}

FGameplayTagContainer UCataclysmSkillShapes::TagsFromCell(const FString& Cell)
{
	FGameplayTagContainer Tags;
	if (Cell.IsEmpty())
	{
		return Tags;
	}

	TArray<FString> Names;
	Cell.ParseIntoArray(Names, TEXT(","), /*InCullEmpty=*/true);
	for (FString& Name : Names)
	{
		Name.TrimStartAndEndInline();
		if (Name.IsEmpty())
		{
			continue;
		}

		const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(*Name), /*ErrorIfNotFound=*/false);
		if (Tag.IsValid())
		{
			Tags.AddTag(Tag);
		}
		else
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("'%s' is not a registered gameplay tag, so it scopes "
					 "nothing. Regenerate the tags with "
					 "tools/generate_gameplay_tags.py."), *Name);
		}
	}

	return Tags;
}

FCataclysmSkillShapeParams UCataclysmSkillShapes::ParseParams(
	const FString& Text, FString* OutError)
{
	FCataclysmSkillShapeParams Params;
	if (OutError)
	{
		OutError->Reset();
	}
	if (Text.IsEmpty())
	{
		return Params;
	}

	const auto Fail = [&Params, OutError](const FString& Reason)
	{
		Params.bValid = false;
		if (OutError && OutError->IsEmpty())
		{
			*OutError = Reason;
		}
	};

	TArray<FString> Entries;
	Text.ParseIntoArray(Entries, TEXT(";"), /*InCullEmpty=*/true);

	for (const FString& Entry : Entries)
	{
		FString Key;
		FString Value;
		if (!Entry.Split(TEXT("="), &Key, &Value))
		{
			Fail(FString::Printf(TEXT("%s is not Key=Value"), *Entry.TrimStartAndEnd()));
			continue;
		}
		Key.TrimStartAndEndInline();
		Value.TrimStartAndEndInline();

		// Everything but Mode, Effect and Minions is a number, so it is read once
		// here rather than in each branch below.
		//
		// THIS LIST IS `TEXT_PARAMS` IN `tools/generate_datatables.py` AND IT
		// WAS SHORT BY ONE. Minions was missing, so "Minions=Imp:1" went through
		// FCString::Atof, came back 0, and the guard below rejected the entire
		// parameter cell -- taking Count and MaxActive down with it. Every summon
		// skill therefore arrived with no idea what it summoned. Issue #622.
		// EIGHT MORE JOINED THIS LIST ON 2026-09-01. Each names a value from a
		// closed set the generator holds -- ForcedMovement=Pin, Terrain=Wall,
		// ScalingSource=Burning and so on. Left off this list they would each
		// have gone through Atof, come back 0, and failed the guard below,
		// which marks the WHOLE cell invalid: the same failure Minions caused
		// on issue #622, where every summon lost its Count and MaxActive too.
		// AND `SpreadWhen` JOINED ON 2026-09-02, for the same reason as the eight
		// above: it names a condition rather than a number, so leaving it off
		// would fail the guard below and take the whole cell down -- which for
		// Hex of Cinders means losing its range, its target cap and the curse it
		// applies, not only the spread.
		static const TCHAR* const TextKeys[] = {
			TEXT("Mode"), TEXT("Effect"), TEXT("Minions"),
			TEXT("ScalingSource"), TEXT("ForcedMovement"), TEXT("Terrain"),
			TEXT("Requires"), TEXT("ChargeBreaksOn"), TEXT("RefundsCooldown"),
			TEXT("TargetMode"), TEXT("OnDeath"), TEXT("SpreadWhen"),
			TEXT("Immune"), TEXT("HoldForbids"),
		};
		bool bIsNumber = true;
		for (const TCHAR* const TextKey : TextKeys)
		{
			if (Key.Equals(TextKey, ESearchCase::IgnoreCase))
			{
				bIsNumber = false;
				break;
			}
		}
		const float Number = bIsNumber ? FCString::Atof(*Value) : 0.0f;
		if (bIsNumber && Number == 0.0f && !Value.Equals(TEXT("0")))
		{
			// Atof returns 0 for text it cannot read, so a misspelled value and
			// a deliberate zero are the same number. Told apart here because a
			// zero radius is a skill that hits nothing.
			Fail(FString::Printf(TEXT("%s=%s is not a number"), *Key, *Value));
			continue;
		}

		static constexpr float ToCm = FCataclysmSkillShapeParams::CentimetresPerMetre;

		if (Key.Equals(TEXT("Radius"), ESearchCase::IgnoreCase))
		{
			Params.RadiusCm = Number * ToCm;
		}
		else if (Key.Equals(TEXT("Range"), ESearchCase::IgnoreCase))
		{
			Params.RangeCm = Number * ToCm;
		}
		else if (Key.Equals(TEXT("Angle"), ESearchCase::IgnoreCase))
		{
			Params.AngleDegrees = Number;
		}
		else if (Key.Equals(TEXT("MaxTargets"), ESearchCase::IgnoreCase))
		{
			Params.MaxTargets = FMath::RoundToInt(Number);
		}
		else if (Key.Equals(TEXT("Duration"), ESearchCase::IgnoreCase))
		{
			Params.Duration = Number;
		}
		else if (Key.Equals(TEXT("Interval"), ESearchCase::IgnoreCase))
		{
			Params.Interval = Number;
		}
		else if (Key.Equals(TEXT("Pierce"), ESearchCase::IgnoreCase))
		{
			Params.Pierce = FMath::RoundToInt(Number);
		}
		else if (Key.Equals(TEXT("Returns"), ESearchCase::IgnoreCase))
		{
			Params.bReturns = Number != 0.0f;
		}
		else if (Key.Equals(TEXT("Speed"), ESearchCase::IgnoreCase))
		{
			// Written in centimetres per second already. It is the one distance
			// the sheet does not write in metres, because a projectile speed in
			// metres per second reads oddly next to Unreal's own defaults.
			Params.SpeedCmPerSecond = Number;
		}
		else if (Key.Equals(TEXT("Knockback"), ESearchCase::IgnoreCase))
		{
			Params.KnockbackCm = Number * ToCm;
		}
		else if (Key.Equals(TEXT("Count"), ESearchCase::IgnoreCase))
		{
			Params.Count = FMath::RoundToInt(Number);
		}
		else if (Key.Equals(TEXT("MaxActive"), ESearchCase::IgnoreCase))
		{
			Params.MaxActive = FMath::RoundToInt(Number);
		}
		else if (Key.Equals(TEXT("Burn"), ESearchCase::IgnoreCase))
		{
			Params.bBurns = Number != 0.0f;
		}
		// AND A BUFF MAY BURN WHOEVER STRIKES ITS HOLDER, which is a different
		// question from whom the skill itself burns. The Greataxe's Burning
		// Wrath: "any enemy that strikes you in melee is set alight".
		else if (Key.Equals(TEXT("BurnsAttackers"), ESearchCase::IgnoreCase))
		{
			Params.bBurnsAttackers = Number != 0.0f;
		}
		// WHAT THE CASTER CANNOT BE SUBJECTED TO WHILE THIS RUNS. Seven rows
		// across four weapons state one, and section VI of the design document
		// sanctions skill-stated immunity outright.
		else if (Key.Equals(TEXT("Immune"), ESearchCase::IgnoreCase))
		{
			Params.Immune = Value;
		}
		else if (Key.Equals(TEXT("GroundRadius"), ESearchCase::IgnoreCase))
		{
			Params.GroundRadiusCm = Number * ToCm;
		}
		else if (Key.Equals(TEXT("GroundDuration"), ESearchCase::IgnoreCase))
		{
			Params.GroundDuration = Number;
		}
		else if (Key.Equals(TEXT("GroundPercent"), ESearchCase::IgnoreCase))
		{
			Params.GroundPercent = Number;
		}
		else if (Key.Equals(TEXT("GroundHitsAllies"), ESearchCase::IgnoreCase))
		{
			Params.bGroundHitsAllies = Number != 0.0f;
		}
		else if (Key.Equals(TEXT("Arc"), ESearchCase::IgnoreCase))
		{
			Params.ArcHeightFraction = Number;
		}
		else if (Key.Equals(TEXT("StunSeconds"), ESearchCase::IgnoreCase))
		{
			Params.StunSeconds = Number;
		}
		else if (Key.Equals(TEXT("FinalHitPercent"), ESearchCase::IgnoreCase))
		{
			Params.FinalHitPercent = Number;
		}
		else if (Key.Equals(TEXT("HealthCostPercent"), ESearchCase::IgnoreCase))
		{
			Params.HealthCostPercent = Number;
		}
		else if (Key.Equals(TEXT("HealthPercent"), ESearchCase::IgnoreCase))
		{
			// A DIFFERENT NUMBER FROM THE ONE ABOVE, despite the names. That one
			// is a cost in the caster's own health; this one raises the health of
			// what the skill deploys above that minion type's own. Iron Fortress
			// is the only skill that states it, at 150. Handled after
			// HealthCostPercent only because that is where the riders sit; the
			// comparison is exact, so the order does not matter.
			Params.MinionHealthPercent = Number;
		}
		// --- Scaling ---------------------------------------------------
		//
		// `IncreasePerBurning` WAS HERE UNTIL 2026-09-01. It said two things at
		// once -- what the number was per, and which bucket it joined -- and it
		// joined the additive one, where a self buff competed with every gear
		// affix the character wore. It is now MoreDamagePer with
		// ScalingSource=Burning beside it, and UCataclysmSelfBuffSkill puts it
		// in the multiplicative bucket, which is what "4% more" means.
		else if (Key.Equals(TEXT("MoreDamagePer"), ESearchCase::IgnoreCase))
		{
			Params.MoreDamagePer = Number;
		}
		// AND A FLAT MULTIPLIER WHEN THE BLOW CAME FROM BEHIND, which is
		// Emberpierce's "40% more damage from behind". No `ScalingSource`,
		// because it counts nothing: the condition is where the attacker was
		// standing.
		else if (Key.Equals(TEXT("MoreDamageFromBehind"),
							ESearchCase::IgnoreCase))
		{
			Params.MoreDamageFromBehind = Number;
		}
		else if (Key.Equals(TEXT("IncreasedDamagePer"), ESearchCase::IgnoreCase))
		{
			Params.IncreasedDamagePer = Number;
		}
		else if (Key.Equals(TEXT("DurationPer"), ESearchCase::IgnoreCase))
		{
			Params.DurationPer = Number;
		}
		else if (Key.Equals(TEXT("ScalingSource"), ESearchCase::IgnoreCase))
		{
			// Not checked against the closed list here, for the same reason
			// Effect is not checked against the effect sheets: the generator
			// holds the only copy of that list and refuses anything outside it.
			Params.ScalingSource = Value;
		}
		else if (Key.Equals(TEXT("MaxDamagePercent"), ESearchCase::IgnoreCase))
		{
			Params.MaxDamagePercent = Number;
		}
		else if (Key.Equals(TEXT("MinDamagePercent"), ESearchCase::IgnoreCase))
		{
			Params.MinDamagePercent = Number;
		}
		else if (Key.Equals(TEXT("RangeIncrease"), ESearchCase::IgnoreCase))
		{
			Params.RangeIncrease = Number;
		}
		// --- Applied effect detail --------------------------------------
		else if (Key.Equals(TEXT("EffectDuration"), ESearchCase::IgnoreCase))
		{
			Params.EffectDuration = Number;
		}
		else if (Key.Equals(TEXT("EffectMagnitude"), ESearchCase::IgnoreCase))
		{
			Params.EffectMagnitude = Number;
		}
		else if (Key.Equals(TEXT("AllyIncreasedDamage"), ESearchCase::IgnoreCase))
		{
			Params.AllyIncreasedDamage = Number;
		}
		else if (Key.Equals(TEXT("HealthFromHitTaken"), ESearchCase::IgnoreCase))
		{
			Params.HealthFromHitTaken = Number;
		}
		else if (Key.Equals(TEXT("StoresFromHitTaken"), ESearchCase::IgnoreCase))
		{
			Params.StoresFromHitTaken = Number;
		}
		else if (Key.Equals(TEXT("StoreCapPercent"), ESearchCase::IgnoreCase))
		{
			Params.StoreCapPercent = Number;
		}
		else if (Key.Equals(TEXT("StoreSpentPerHit"), ESearchCase::IgnoreCase))
		{
			Params.StoreSpentPerHit = Number;
		}
		else if (Key.Equals(TEXT("OwnGroundRegenPercent"), ESearchCase::IgnoreCase))
		{
			Params.OwnGroundRegenPercent = Number;
		}
		else if (Key.Equals(TEXT("HoldForbids"), ESearchCase::IgnoreCase))
		{
			Params.HoldForbids = Value;
		}
		// --- Forced movement --------------------------------------------
		else if (Key.Equals(TEXT("ForcedMovement"), ESearchCase::IgnoreCase))
		{
			Params.ForcedMovement = Value;
		}
		else if (Key.Equals(TEXT("ForcedMovementDistance"), ESearchCase::IgnoreCase))
		{
			Params.ForcedMovementDistanceCm = Number * ToCm;
		}
		else if (Key.Equals(TEXT("ForcedMovementDuration"), ESearchCase::IgnoreCase))
		{
			Params.ForcedMovementDuration = Number;
		}
		// --- Terrain ----------------------------------------------------
		else if (Key.Equals(TEXT("Terrain"), ESearchCase::IgnoreCase))
		{
			Params.Terrain = Value;
		}
		else if (Key.Equals(TEXT("TerrainSize"), ESearchCase::IgnoreCase))
		{
			Params.TerrainSizeCm = Number * ToCm;
		}
		else if (Key.Equals(TEXT("TerrainDuration"), ESearchCase::IgnoreCase))
		{
			Params.TerrainDuration = Number;
		}
		// --- Conditions and commitment ----------------------------------
		else if (Key.Equals(TEXT("Requires"), ESearchCase::IgnoreCase))
		{
			Params.Requires = Value;
		}
		else if (Key.Equals(TEXT("ChargeTime"), ESearchCase::IgnoreCase))
		{
			Params.ChargeTime = Number;
		}
		else if (Key.Equals(TEXT("ChargeBreaksOn"), ESearchCase::IgnoreCase))
		{
			Params.ChargeBreaksOn = Value;
		}
		// --- Consumption ------------------------------------------------
		else if (Key.Equals(TEXT("ConsumeBurn"), ESearchCase::IgnoreCase))
		{
			Params.bConsumeBurn = Number != 0.0f;
		}
		else if (Key.Equals(TEXT("ConsumeRadius"), ESearchCase::IgnoreCase))
		{
			Params.ConsumeRadiusCm = Number * ToCm;
		}
		// --- Spreading fire from a target that already carries something --
		else if (Key.Equals(TEXT("SpreadWhen"), ESearchCase::IgnoreCase))
		{
			Params.SpreadWhen = Value;
		}
		else if (Key.Equals(TEXT("SpreadRadius"), ESearchCase::IgnoreCase))
		{
			Params.SpreadRadiusCm = Number * ToCm;
		}
		// --- On death ---------------------------------------------------
		else if (Key.Equals(TEXT("OnDeath"), ESearchCase::IgnoreCase))
		{
			Params.OnDeath = Value;
		}
		else if (Key.Equals(TEXT("OnDeathRange"), ESearchCase::IgnoreCase))
		{
			Params.OnDeathRangeCm = Number * ToCm;
		}
		// --- Projectile extras ------------------------------------------
		else if (Key.Equals(TEXT("Bounces"), ESearchCase::IgnoreCase))
		{
			Params.Bounces = FMath::RoundToInt(Number);
		}
		else if (Key.Equals(TEXT("SpreadCurses"), ESearchCase::IgnoreCase))
		{
			Params.SpreadCurses = FMath::RoundToInt(Number);
		}
		else if (Key.Equals(TEXT("TargetMode"), ESearchCase::IgnoreCase))
		{
			Params.TargetMode = Value;
		}
		else if (Key.Equals(TEXT("ScalesWithAttackSpeed"), ESearchCase::IgnoreCase))
		{
			Params.bScalesWithAttackSpeed = Number != 0.0f;
		}
		else if (Key.Equals(TEXT("CommandStrike"), ESearchCase::IgnoreCase))
		{
			Params.bCommandStrike = Number != 0.0f;
		}
		else if (Key.Equals(TEXT("TetherTargets"), ESearchCase::IgnoreCase))
		{
			Params.TetherTargets = FMath::RoundToInt(Number);
		}
		else if (Key.Equals(TEXT("TetherLength"), ESearchCase::IgnoreCase))
		{
			Params.TetherLengthCm = Number * ToCm;
		}
		else if (Key.Equals(TEXT("TetherDuration"), ESearchCase::IgnoreCase))
		{
			Params.TetherDuration = Number;
		}
		// --- Summon extras ----------------------------------------------
		else if (Key.Equals(TEXT("Possess"), ESearchCase::IgnoreCase))
		{
			Params.bPossess = Number != 0.0f;
		}
		else if (Key.Equals(TEXT("FervourReserve"), ESearchCase::IgnoreCase))
		{
			Params.FervourReserve = Number;
		}
		else if (Key.Equals(TEXT("HealthThresholdPercent"), ESearchCase::IgnoreCase))
		{
			Params.HealthThresholdPercent = Number;
		}
		// --- Other riders -----------------------------------------------
		else if (Key.Equals(TEXT("RefundsCooldown"), ESearchCase::IgnoreCase))
		{
			Params.RefundsCooldown = Value;
		}
		else if (Key.Equals(TEXT("Untargetable"), ESearchCase::IgnoreCase))
		{
			Params.bUntargetable = Number != 0.0f;
		}
		else if (Key.Equals(TEXT("DisarmsUntilRecalled"), ESearchCase::IgnoreCase))
		{
			Params.bDisarmsUntilRecalled = Number != 0.0f;
		}
		else if (Key.Equals(TEXT("RearHits"), ESearchCase::IgnoreCase))
		{
			Params.bRearHits = Number != 0.0f;
		}
		else if (Key.Equals(TEXT("Effect"), ESearchCase::IgnoreCase))
		{
			// Not checked against the effect list here. The generator does that
			// against the Buffs, Debuffs and DoTs sheets, which is the only
			// place the real list lives; repeating it in C++ would be a second
			// copy that could go stale.
			Params.Effect = Value;
		}
		else if (Key.Equals(TEXT("Minions"), ESearchCase::IgnoreCase))
		{
			// `Imp:1`, or `Ballista:2, SpikeTrap:3`. The comma separates kinds
			// and the colon separates a kind from how many of it.
			//
			// THE TYPE NAMES ARE NOT CHECKED AGAINST THE MINION TABLE HERE, for
			// the same reason Effect is not checked above:
			// `validate_minion_references` in tools/generate_datatables.py
			// already refuses a name the Minion Types sheet does not have, so a
			// generated table cannot carry one. What IS checked is the shape of
			// the text, because a misread pair would silently summon nothing.
			TArray<FString> Pairs;
			Value.ParseIntoArray(Pairs, TEXT(","), /*InCullEmpty=*/true);

			for (const FString& Pair : Pairs)
			{
				FString TypeName;
				FString CountText;
				if (!Pair.Split(TEXT(":"), &TypeName, &CountText))
				{
					Fail(FString::Printf(
						TEXT("Minions entry %s is not Type:Count"),
						*Pair.TrimStartAndEnd()));
					continue;
				}

				TypeName.TrimStartAndEndInline();
				CountText.TrimStartAndEndInline();

				if (TypeName.IsEmpty() || !CountText.IsNumeric())
				{
					Fail(FString::Printf(
						TEXT("Minions entry %s is not Type:Count"),
						*Pair.TrimStartAndEnd()));
					continue;
				}

				const int32 HowMany = FCString::Atoi(*CountText);
				if (HowMany < 1)
				{
					// Naming a type and asking for none of it says nothing, and
					// would read as a skill that summons and then does not.
					Fail(FString::Printf(
						TEXT("Minions asks for %d %s"), HowMany, *TypeName));
					continue;
				}

				FCataclysmMinionSpawn Spawn;
				Spawn.Type = TypeName;
				Spawn.Count = HowMany;
				Params.Minions.Add(MoveTemp(Spawn));
			}

			if (Params.Minions.IsEmpty() && Params.bValid)
			{
				Fail(TEXT("Minions names nothing"));
			}
		}
		else if (Key.Equals(TEXT("Mode"), ESearchCase::IgnoreCase))
		{
			bool bFound = false;
			for (const FMovementModeName& Entry2 : MovementModeNames)
			{
				if (Value.Equals(Entry2.Name, ESearchCase::IgnoreCase))
				{
					Params.MovementMode = Entry2.Mode;
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				Fail(FString::Printf(TEXT("Mode=%s names no movement mode"), *Value));
			}
		}
		else
		{
			Fail(FString::Printf(TEXT("%s is not a parameter any shape reads"), *Key));
		}
	}

	if (!Params.bValid && OutError)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("Could not read the shape parameters '%s': %s. The skill will "
				 "run with whatever did read, which may be nothing."),
			*Text, **OutError);
	}

	return Params;
}
