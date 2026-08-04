// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillShape.h"
#include "Cataclysm.h"
#include "GameplayTagsManager.h"

namespace
{
	/**
	 * The shape names, spelled exactly as the Shape column writes them.
	 *
	 * The generator holds the same seven names in SHAPE_PARAMS. They are two
	 * lists of the same thing and they can disagree, so
	 * Cataclysm.SkillShape.EveryShapeInTheDataHasATemplate compares them: it
	 * reads every Shape value out of the generated table and fails on one this
	 * function does not know.
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

		// Everything but Mode and Effect is a number, so it is read once here
		// rather than in each branch below.
		const bool bIsNumber = !Key.Equals(TEXT("Mode"), ESearchCase::IgnoreCase)
							&& !Key.Equals(TEXT("Effect"), ESearchCase::IgnoreCase);
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
		else if (Key.Equals(TEXT("FinalHitPercent"), ESearchCase::IgnoreCase))
		{
			Params.FinalHitPercent = Number;
		}
		else if (Key.Equals(TEXT("HealthCostPercent"), ESearchCase::IgnoreCase))
		{
			Params.HealthCostPercent = Number;
		}
		else if (Key.Equals(TEXT("Effect"), ESearchCase::IgnoreCase))
		{
			// Not checked against the effect list here. The generator does that
			// against the Buffs, Debuffs and DoTs sheets, which is the only
			// place the real list lives; repeating it in C++ would be a second
			// copy that could go stale.
			Params.Effect = Value;
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
