// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPlayerBuild.h"

#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveGather.h"

FCataclysmPlayerBuild FCataclysmPlayerBuild::NumbersOf(const ACataclysmPlayerState& State)
{
	FCataclysmPlayerBuild Build;
	Build.Level = State.GetCharacterLevel();
	Build.SpentAttributePoints = State.GetSpentAttributePoints();
	Build.CreationDamageType = State.GetChosenDamageType();
	Build.PassiveAllocation = State.GetPassiveAllocation();
	// THE CLASS A PLAYER'S REFRESH APPLIES: the console's when set by hand, otherwise the character's own, from its
	// points and the damage type it was created with. Issue #932 (2026-11-27); see `ClassFor`.
	Build.ClassName = UCataclysmPlayerClassStats::ClassFor(
		UCataclysmPassiveTree::LoadNodeTable(), Build.PassiveAllocation, Build.CreationDamageType);
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
		// AND WHAT EACH OTHER KEY HELD BY DEFAULT, so the double holds the keys the player had (issue #837 slice 1).
		Build.RememberedDefaults = State->GetRememberedDefaults();
	}
	else
	{
		Build.Level = UCataclysmPlayerClassStats::ChosenLevel();
		Build.ClassName = UCataclysmPlayerClassStats::ChosenClass();
	}

	// THE ITEMS AS THEY ARE, gathered as the character record gathers them, so the character record saves exactly this.
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
