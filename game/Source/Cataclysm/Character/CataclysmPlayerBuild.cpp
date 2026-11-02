// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPlayerBuild.h"

#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveGather.h"

FCataclysmPlayerBuild FCataclysmPlayerBuild::NumbersOf(const ACataclysmPlayerState& State)
{
	FCataclysmPlayerBuild Build;
	Build.Level = State.GetCharacterLevel();
	// THE CLASS A PLAYER'S REFRESH APPLIES, which is the console's choice and not a field of the player state.
	Build.ClassName = UCataclysmPlayerClassStats::ChosenClass();
	Build.SpentAttributePoints = State.GetSpentAttributePoints();
	Build.CreationDamageType = State.GetChosenDamageType();
	Build.PassiveAllocation = State.GetPassiveAllocation();
	// GEAR'S CLASS POINTS LEFT OUT, and added where the build is applied: `PassivePointsEarnedWith` adds them as
	// `Max(0, PointsFromGear)` to this same figure.
	Build.PassivePointsBesidesGear = State.PassivePointsEarnedWith(0);
	return Build;
}

FCataclysmPlayerBuild FCataclysmPlayerBuild::Of(const ACataclysmPlayerCharacter& Player)
{
	const ACataclysmPlayerState* State = Player.GetPlayerState<ACataclysmPlayerState>();
	FCataclysmPlayerBuild Build;
	if (State)
	{
		Build = NumbersOf(*State);
		Build.SkillAssignment = State->GetSkillAssignment();
	}
	else
	{
		Build.Level = UCataclysmPlayerClassStats::ChosenLevel();
		Build.ClassName = UCataclysmPlayerClassStats::ChosenClass();
	}

	// THE ITEMS AS THEY ARE, gathered as the character record gathers them, so a later slice saves exactly this.
	if (const UCataclysmEquipmentComponent* Equipment = Player.GetEquipment())
	{
		FCataclysmSaveGather::WornGearFrom(*Equipment, Build.Worn);
	}
	return Build;
}

TArray<TPair<ECataclysmGearSlot, FCataclysmItem>> FCataclysmPlayerBuild::WornPairs() const
{
	TArray<TPair<ECataclysmGearSlot, FCataclysmItem>> Pairs;
	Pairs.Reserve(Worn.Num());
	for (const FCataclysmWornItem& Entry : Worn)
	{
		Pairs.Emplace(Entry.Slot, Entry.Item);
	}
	return Pairs;
}
