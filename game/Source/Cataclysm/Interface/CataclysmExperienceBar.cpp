// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmExperienceBar.h"
#include "Character/CataclysmExperience.h"
#include "GameFramework/Pawn.h"
#include "Player/CataclysmPlayerState.h"

const TCHAR* UCataclysmExperienceBar::FillHex = TEXT("B8912F");

float UCataclysmExperienceBar::FractionFor(int32 Level, int64 ExperienceIntoLevel)
{
	const int32 Held = FMath::Max(Level, UCataclysmExperience::FirstLevel);
	const int64 Cost = UCataclysmExperience::CostOfLevel(Held + 1);

	// NOTHING LEFT TO EARN: the maximum level, where the next level costs nothing.
	if (UCataclysmExperience::IsMaxLevel(Held) || Cost <= 0)
	{
		return 1.0f;
	}
	return static_cast<float>(FMath::Clamp(static_cast<double>(ExperienceIntoLevel) / static_cast<double>(Cost), 0.0, 1.0));
}

FString UCataclysmExperienceBar::TextFor(int32 Level, int64 ExperienceIntoLevel)
{
	const int32 Held = FMath::Clamp(Level, UCataclysmExperience::FirstLevel, UCataclysmExperience::MaxLevel);
	const int64 Cost = UCataclysmExperience::CostOfLevel(Held + 1);
	if (UCataclysmExperience::IsMaxLevel(Held) || Cost <= 0)
	{
		return FString::Printf(TEXT("Level %d: maximum"), Held);
	}

	// TENTHS OF A PERCENT, ROUNDED DOWN, in whole numbers: the experience into a level is below 520 million, so times
	// 1,000 it is far inside an int64, and no floating point can round 99.96% up to a level not yet reached.
	const int64 Tenths = FMath::Clamp<int64>(FMath::Max<int64>(0, ExperienceIntoLevel) * 1000 / Cost, 0, 999);
	return FString::Printf(TEXT("Level %d: %lld.%lld%%"), Held, Tenths / 10, Tenths % 10);
}

bool UCataclysmExperienceBar::LevelOf(const AActor* Pawn, int32& OutLevel, int64& OutExperienceIntoLevel)
{
	const APawn* AsPawn = Cast<APawn>(Pawn);
	const ACataclysmPlayerState* State = AsPawn ? AsPawn->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	if (!State)
	{
		return false;
	}
	OutLevel = State->GetCharacterLevel();
	OutExperienceIntoLevel = State->GetExperienceIntoLevel();
	return true;
}
