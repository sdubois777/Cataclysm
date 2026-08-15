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

	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(*FString::Printf(TEXT("Status.%s"), *Segment)),
		/*ErrorIfNotFound=*/false);
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
		const bool bIsNumber = !Key.Equals(TEXT("Mode"), ESearchCase::IgnoreCase)
							&& !Key.Equals(TEXT("Effect"), ESearchCase::IgnoreCase)
							&& !Key.Equals(TEXT("Minions"), ESearchCase::IgnoreCase);
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
		else if (Key.Equals(TEXT("IncreasePerBurning"), ESearchCase::IgnoreCase))
		{
			Params.IncreasePerBurning = Number;
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
