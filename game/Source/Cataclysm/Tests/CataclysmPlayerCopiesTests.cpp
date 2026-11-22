// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Character/CataclysmConsumptionDoubleCharacter.h"
#include "Character/CataclysmCorruptedStalkerCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerBuild.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponMeshes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestSwings.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/SoftObjectPath.h"

/**
 * The creatures that copy a player character wear the player's body. Issue #2806, from the owner's playtest of
 * 2026-11-21: "The corrupted dopples and nemesis should be using the character model we use, it's kinda hard to see a
 * grey cylinder coming at you before it murders you".
 *
 * TWO CREATURES. The Corrupted Stalker, which the dungeon modifier `Generic_Corrupted_Stalker` raises from "the pool of
 * Nemesis characters" (the owner's words of 2026-08-05), and the Worn Residue double, a subclass of it carrying the
 * player's own build. Until this change neither set a mesh, so each kept `ACataclysmEnemyCharacter`'s
 * `PlaceholderBody`, the engine's basic cylinder.
 *
 * THE BODY HALF CHECKS FULLY IN EVERY CHECKOUT: the Mannequin the player wears is in git. THE WEAPON MESH HALF IS A
 * SKIPPED HALF WITHOUT THE `Medieval_Weapons` PACK, which git does not carry; what each hand draws is checked
 * without it through the item base the copy records for each hand.
 *
 * A WORLD THAT HAS BEGUN PLAY, so a copy is dressed as it is when the game spawns one: its components register with the
 * body put on first, and its `BeginPlay` runs on the spawn.
 */
namespace CataclysmPlayerCopiesTest
{
	template <typename TCopy>
	TCopy* SpawnCopy(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TCopy* Copy = World->SpawnActor<TCopy>(TCopy::StaticClass(), Where, FRotator::ZeroRotator, Params);
		if (Copy)
		{
			Copy->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		}
		return Copy;
	}

	/**
	 * THE CONTROL: a plain creature, which nothing dresses, keeps the cylinder and has no skeletal mesh. Asked first, so a
	 * failure below cannot be read as every creature having lost the placeholder.
	 */
	bool ThePlainCreatureKeepsTheCylinder(FAutomationTestBase& Test, UWorld* World)
	{
		ACataclysmEnemyCharacter* Plain = SpawnCopy<ACataclysmEnemyCharacter>(World, FVector(-1000.0f, 0.0f, 0.0f));
		if (!Test.TestNotNull(TEXT("control: a plain creature was spawned"), Plain)
			|| !Test.TestNotNull(TEXT("control: it has the placeholder cylinder"), Plain->PlaceholderBody.Get()))
		{
			return false;
		}
		Test.TestTrue(TEXT("control: a plain creature has no skeletal mesh"),
					  Plain->GetMesh() && !Plain->GetMesh()->GetSkeletalMeshAsset());
		Test.TestTrue(TEXT("control: and its cylinder is visible"), Plain->PlaceholderBody->IsVisible());
		return true;
	}

	/** What the player wears and plays, read from the player's own paths. */
	struct FTheMannequin
	{
		USkeletalMesh* Body = nullptr;
		UClass* Animation = nullptr;
	};

	FTheMannequin LoadTheMannequin()
	{
		FTheMannequin Out;
		Out.Body = Cast<USkeletalMesh>(FSoftObjectPath(ACataclysmPlayerCharacter::BodyMeshPath).TryLoad());
		Out.Animation =
			FSoftClassPath(ACataclysmPlayerCharacter::AnimationBlueprintPath).TryLoadClass<UAnimInstance>();
		return Out;
	}

	/**
	 * A copy wears the player's body and animation Blueprint, hides the cylinder, stands with its feet at its capsule's
	 * bottom and the engine's -90 degree yaw, and holds the Mannequin's six death clips, none missing.
	 */
	void WearsThePlayersBody(FAutomationTestBase& Test, ACataclysmEnemyCharacter* Copy, const FTheMannequin& Mannequin)
	{
		USkeletalMeshComponent* MeshComponent = Copy->GetMesh();
		if (!Test.TestNotNull(TEXT("it has a mesh component"), MeshComponent))
		{
			return;
		}
		Test.TestTrue(TEXT("it wears the player's Mannequin body"),
					  MeshComponent->GetSkeletalMeshAsset() == Mannequin.Body);
		Test.TestTrue(TEXT("driven by the player's animation Blueprint"),
					  MeshComponent->GetAnimClass() == Mannequin.Animation);
		Test.TestTrue(TEXT("its placeholder cylinder is hidden"),
					  Copy->PlaceholderBody && !Copy->PlaceholderBody->IsVisible());

		// THE CREATURE'S OWN CAPSULE, KEPT, the coordinating session's ruling J1 of 2026-11-21: the feet at its bottom.
		const float HalfHeight = Copy->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		Test.TestEqual(TEXT("its feet at its capsule's bottom"),
					   static_cast<float>(MeshComponent->GetRelativeLocation().Z), -HalfHeight, 0.01f);
		Test.TestEqual(TEXT("facing the way it moves, at the engine's -90 degree yaw"),
					   MeshComponent->GetRelativeRotation().Yaw, -90.0, 0.01);

		int32 Found = 0;
		for (const TObjectPtr<UAnimSequence>& Clip : Copy->DeathAnimations)
		{
			Found += Clip ? 1 : 0;
		}
		Test.TestEqual(TEXT("and the Mannequin's six death clips, none missing"), Found,
					   static_cast<int32>(UE_ARRAY_COUNT(ACataclysmPlayerCharacter::DeathAnimationNames)));
	}

	/** What `Material` reads for the copies' tint parameter; false when it has no such parameter. */
	bool TintOf(UMaterialInterface* Material, FLinearColor& Out)
	{
		const FHashedMaterialParameterInfo Info(ACataclysmCorruptedStalkerCharacter::CopyTintParameter);
		return Material && Material->GetVectorParameterValue(Info, Out);
	}

	/**
	 * The owner's ruling of 2026-11-21, "give them a dark red tint": every material slot of a copy's body is its own
	 * dynamic instance, and reads the copies' dark red.
	 */
	void WearsTheDarkRedTint(FAutomationTestBase& Test, ACataclysmEnemyCharacter* Copy)
	{
		USkeletalMeshComponent* MeshComponent = Copy->GetMesh();
		const int32 Slots = MeshComponent ? MeshComponent->GetNumMaterials() : 0;
		Test.TestTrue(TEXT("set-up: its body has material slots"), Slots > 0);
		for (int32 Slot = 0; Slot < Slots; ++Slot)
		{
			UMaterialInterface* Material = MeshComponent->GetMaterial(Slot);
			FLinearColor Tint;
			Test.TestNotNull(*FString::Printf(TEXT("slot %d is a dynamic instance of its own"), Slot),
							 Cast<UMaterialInstanceDynamic>(Material));
			Test.TestTrue(*FString::Printf(TEXT("slot %d has the tint parameter"), Slot), TintOf(Material, Tint));
			Test.TestTrue(*FString::Printf(TEXT("slot %d reads the copies' dark red"), Slot),
						  Tint.Equals(ACataclysmCorruptedStalkerCharacter::CopyTint, 0.0001f));
		}
	}

	/**
	 * THE CONTROL: a player character keeps its own colours. Its body's slots are not dynamic instances, and none reads
	 * the copies' dark red, so the tint is in the copies' dressing and not in the steps the player shares with them.
	 */
	void ThePlayerKeepsItsOwnColours(FAutomationTestBase& Test, UWorld* World)
	{
		// WITH A PLAYER STATE, as `Cataclysm.WeaponMesh.`'s tests spawn one.
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Player =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector(0.0f, -1000.0f, 0.0f), FRotator::ZeroRotator);
		if (State && Player)
		{
			Player->SetPlayerState(State);
			Player->OnRep_PlayerState();
		}
		USkeletalMeshComponent* MeshComponent = Player ? Player->GetMesh() : nullptr;
		if (!Test.TestNotNull(TEXT("control: a player character's body"), MeshComponent)
			|| !Test.TestTrue(TEXT("control: it wears the Mannequin"), MeshComponent->GetSkeletalMeshAsset() != nullptr))
		{
			return;
		}
		for (int32 Slot = 0; Slot < MeshComponent->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Material = MeshComponent->GetMaterial(Slot);
			FLinearColor Tint;
			Test.TestNull(*FString::Printf(TEXT("control: the player's slot %d is not a dynamic instance"), Slot),
						  Cast<UMaterialInstanceDynamic>(Material));
			Test.TestFalse(*FString::Printf(TEXT("control: the player's slot %d does not read the dark red"), Slot),
						   TintOf(Material, Tint) && Tint.Equals(ACataclysmCorruptedStalkerCharacter::CopyTint, 0.0001f));
		}
	}

	/** The mesh `Base` draws in a hand, and whether the weapon pack is on this machine for it. */
	UStaticMesh* MeshOf(const TCHAR* Base)
	{
		float Scale = 1.0f;
		return UCataclysmWeaponMeshes::MeshFor(UCataclysmWeaponMeshes::LoadTable(), FName(Base), Scale);
	}

	FString NoWeaponArtReason()
	{
		return TEXT("game/Content/Medieval_Weapons/ is not on this machine, so every weapon mesh lookup answers null: "
					"the weapon mesh in each hand is NOT checked. The item base each hand draws is");
	}

	/** A weapon of `Base`, at gear level 3, carrying the Demonic type, as the double tests build theirs. */
	FCataclysmItem Weapon(const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = 3;
		Item.DamageTypes = {FName(TEXT("Demonic"))};
		return Item;
	}

	FCataclysmWornItem WornIn(ECataclysmGearSlot Slot, const FCataclysmItem& Item)
	{
		FCataclysmWornItem Worn;
		Worn.Slot = Slot;
		Worn.Item = Item;
		return Worn;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkerWearsThePlayersBodyTest,
	"Cataclysm.PlayerCopies.AStalkerWearsThePlayersBodyAndAnimationAndHidesTheCylinder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStalkerWearsThePlayersBodyTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerCopiesTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FTheMannequin Mannequin = LoadTheMannequin();
	if (!TestNotNull(TEXT("set-up: the Mannequin body, which git carries"), Mannequin.Body)
		|| !TestNotNull(TEXT("set-up: and its animation Blueprint"), Mannequin.Animation)
		|| !ThePlainCreatureKeepsTheCylinder(*this, World))
	{
		return false;
	}

	ACataclysmCorruptedStalkerCharacter* Stalker =
		SpawnCopy<ACataclysmCorruptedStalkerCharacter>(World, FVector(1000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a Corrupted Stalker was spawned"), Stalker))
	{
		return false;
	}
	WearsThePlayersBody(*this, Stalker, Mannequin);
	WearsTheDarkRedTint(*this, Stalker);
	ThePlayerKeepsItsOwnColours(*this, World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDoubleWearsThePlayersBodyTest,
	"Cataclysm.PlayerCopies.AWornResidueDoubleWearsThePlayersBodyAndAnimationAndHidesTheCylinder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDoubleWearsThePlayersBodyTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerCopiesTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FTheMannequin Mannequin = LoadTheMannequin();
	if (!TestNotNull(TEXT("set-up: the Mannequin body, which git carries"), Mannequin.Body)
		|| !TestNotNull(TEXT("set-up: and its animation Blueprint"), Mannequin.Animation)
		|| !ThePlainCreatureKeepsTheCylinder(*this, World))
	{
		return false;
	}

	ACataclysmConsumptionDoubleCharacter* Double =
		SpawnCopy<ACataclysmConsumptionDoubleCharacter>(World, FVector(1000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a Worn Residue double was spawned"), Double))
	{
		return false;
	}
	WearsThePlayersBody(*this, Double, Mannequin);
	WearsTheDarkRedTint(*this, Double);
	ThePlayerKeepsItsOwnColours(*this, World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkerHoldsItsWeaponTest,
	"Cataclysm.PlayerCopies.AStalkerHoldsTheWeaponItsEntryWears",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Famine's entry, Hesk the Starveling, wears a Sword in its first weapon slot and nothing in its second: the Sword in
 * the right hand and the left empty. THE CONTROL is the same Stalker before it wears the entry, both hands empty.
 */
bool FCataclysmStalkerHoldsItsWeaponTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerCopiesTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmStalkerEntry* Famine = nullptr;
	const TArray<FCataclysmStalkerEntry> Entries = ACataclysmCorruptedStalkerCharacter::Entries();
	for (const FCataclysmStalkerEntry& Entry : Entries)
	{
		Famine = Entry.DamageType == FName(TEXT("Famine")) ? &Entry : Famine;
	}
	ACataclysmCorruptedStalkerCharacter* Stalker =
		SpawnCopy<ACataclysmCorruptedStalkerCharacter>(World, FVector(1000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: Famine's entry"), Famine)
		|| !TestNotNull(TEXT("set-up: a Corrupted Stalker was spawned"), Stalker)
		|| !TestNotNull(TEXT("set-up: it has a right hand"), Stalker->RightHandWeapon.Get())
		|| !TestNotNull(TEXT("set-up: and a left hand"), Stalker->LeftHandWeapon.Get()))
	{
		return false;
	}

	TestEqual(TEXT("control: before it wears the entry its right hand draws nothing"), Stalker->RightHandBase, FName());
	TestEqual(TEXT("control: nor its left"), Stalker->LeftHandBase, FName());

	FRandomStream Stream(2806);
	if (!TestTrue(TEXT("set-up: it wears Famine's entry"), Stalker->WearTheBuild(*Famine, 1, Stream)))
	{
		return false;
	}
	TestEqual(TEXT("its right hand draws the entry's Sword"), Stalker->RightHandBase, FName(TEXT("Weapon_Sword")));
	TestEqual(TEXT("and its left hand nothing"), Stalker->LeftHandBase, FName());

	UStaticMesh* Sword = MeshOf(TEXT("Weapon_Sword"));
	if (Sword)
	{
		TestTrue(TEXT("the Sword's mesh is in its right hand"), Stalker->RightHandWeapon->GetStaticMesh() == Sword);
		TestTrue(TEXT("and no mesh in its left"), Stalker->LeftHandWeapon->GetStaticMesh() == nullptr);
	}
	else
	{
		CataclysmTestSkip::ReportSkippedHalf(*this, NoWeaponArtReason());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDoubleHoldsTheWeaponsTest,
	"Cataclysm.PlayerCopies.ADoubleHoldsTheWeaponsThePlayersBuildWears",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A build wearing a Sword in its first weapon slot and a Dagger in its second: the Sword in the right hand and the
 * Dagger in the left, as the player's own hands draw two weapons by slot (`ACataclysmPlayerCharacter::WeaponsInHands`).
 * THE CONTROL is the same double before it wears the build, both hands empty.
 */
bool FCataclysmDoubleHoldsTheWeaponsTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerCopiesTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmConsumptionDoubleCharacter* Double =
		SpawnCopy<ACataclysmConsumptionDoubleCharacter>(World, FVector(1000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a Worn Residue double was spawned"), Double)
		|| !TestNotNull(TEXT("set-up: it has a right hand"), Double->RightHandWeapon.Get())
		|| !TestNotNull(TEXT("set-up: and a left hand"), Double->LeftHandWeapon.Get()))
	{
		return false;
	}

	TestEqual(TEXT("control: before it wears the build its right hand draws nothing"), Double->RightHandBase, FName());
	TestEqual(TEXT("control: nor its left"), Double->LeftHandBase, FName());

	FCataclysmPlayerBuild Build;
	Build.Worn = {WornIn(ECataclysmGearSlot::Weapon1, Weapon(TEXT("Weapon_Sword"))),
				  WornIn(ECataclysmGearSlot::Weapon2, Weapon(TEXT("Weapon_Dagger")))};
	Build.Level = 20;
	Build.ClassName = TEXT("Ravager");
	Build.CreationDamageType = FName(TEXT("Demonic"));
	if (!TestTrue(TEXT("set-up: it wears the build"), Double->WearThePlayersBuild(Build)))
	{
		return false;
	}
	TestEqual(TEXT("its right hand draws the build's Sword"), Double->RightHandBase, FName(TEXT("Weapon_Sword")));
	TestEqual(TEXT("and its left hand the build's Dagger"), Double->LeftHandBase, FName(TEXT("Weapon_Dagger")));

	UStaticMesh* Sword = MeshOf(TEXT("Weapon_Sword"));
	UStaticMesh* Dagger = MeshOf(TEXT("Weapon_Dagger"));
	if (Sword && Dagger)
	{
		TestTrue(TEXT("the Sword's mesh is in its right hand"), Double->RightHandWeapon->GetStaticMesh() == Sword);
		TestTrue(TEXT("and the Dagger's in its left"), Double->LeftHandWeapon->GetStaticMesh() == Dagger);
	}
	else
	{
		CataclysmTestSkip::ReportSkippedHalf(*this, NoWeaponArtReason());
	}
	return true;
}

// ---------------------------------------------------------------------------
// Their blows swing as the player's do. Issue #2811.
// ---------------------------------------------------------------------------

namespace CataclysmPlayerCopiesTest
{
	/** An entry by its damage type, or null. */
	const FCataclysmStalkerEntry* EntryOf(const TArray<FCataclysmStalkerEntry>& Entries, const TCHAR* DamageType)
	{
		return Entries.FindByPredicate([DamageType](const FCataclysmStalkerEntry& Entry)
			{ return Entry.DamageType == FName(DamageType); });
	}

	/** A creature of the players' side at `Where`, with no armour and a great deal of health, for a blow to land on. */
	ACataclysmEnemyCharacter* ATarget(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Target = SpawnCopy<ACataclysmEnemyCharacter>(World, Where);
		if (Target)
		{
			Target->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Target->SetHealth(100000.0f);
			Target->SetArmour(0.0f);
		}
		return Target;
	}

	float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Actor);
		return Abilities ? Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** The clip a dynamic montage was built around, or null: as `CataclysmPlayerAttackAnimationTests.cpp` reads it. */
	const UAnimSequenceBase* ClipInside(const UAnimMontage* Montage)
	{
		if (!Montage || Montage->SlotAnimTracks.Num() == 0 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 0)
		{
			return nullptr;
		}
		return Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
	}

	/** The name of the clip `Copy`'s body is playing in a slot, or none, and the slot it plays in. */
	FName SwingPlaying(ACataclysmEnemyCharacter* Copy, FName& OutSlot)
	{
		OutSlot = NAME_None;
		UAnimInstance* AnimInstance = Copy && Copy->GetMesh() ? Copy->GetMesh()->GetAnimInstance() : nullptr;
		const UAnimMontage* Montage = AnimInstance ? AnimInstance->GetCurrentActiveMontage() : nullptr;
		if (Montage && Montage->SlotAnimTracks.Num() > 0)
		{
			OutSlot = Montage->SlotAnimTracks[0].SlotName;
		}
		const UAnimSequenceBase* Clip = ClipInside(Montage);
		return Clip ? Clip->GetFName() : NAME_None;
	}

	/**
	 * `Copy` strikes `Target` with its basic attack, and the blow is what the player's is: the Mannequin's clip `Clip`
	 * in the animation Blueprint's `DefaultSlot`, a wait until the swing connects, the blow not landed until it does,
	 * and the copy turned to face `Target`. Then the swing is finished and the blow lands.
	 */
	void StrikesWithAPlayersSwing(FAutomationTestBase& Test, const TCHAR* Who, ACataclysmCorruptedStalkerCharacter* Copy,
								  ACataclysmEnemyCharacter* Target, UWorld* World, const TCHAR* Clip)
	{
		const float Before = HealthOf(Target);
		Copy->AttackTarget(Target);

		FName Slot;
		const FName Playing = SwingPlaying(Copy, Slot);
		Test.TestEqual(*FString::Printf(TEXT("%s: its blow plays the Mannequin's %s"), Who, Clip), Playing, FName(Clip));
		Test.TestEqual(*FString::Printf(TEXT("%s: into the animation Blueprint's slot"), Who), Slot,
					   ACataclysmPlayerCharacter::AttackSlotName);
		Test.TestTrue(*FString::Printf(TEXT("%s: the swing connects later than now: %.3f s"), Who,
									   Copy->SecondsUntilTheSwingConnects()),
					  Copy->SecondsUntilTheSwingConnects() > 0.0f);
		Test.TestTrue(*FString::Printf(TEXT("%s: its blow waits for the swing"), Who),
					  CataclysmTestSwings::IsWaitingForASwing(Copy));
		Test.TestEqual(*FString::Printf(TEXT("%s: and has not landed yet"), Who), HealthOf(Target), Before, 0.01f);

		// IT TURNED TO FACE ITS TARGET, which stands a quarter turn to its left: `CommitAndBegin` turns a caster that
		// was given no cast place.
		const FVector ToTarget = Target->GetActorLocation() - Copy->GetActorLocation();
		Test.TestEqual(*FString::Printf(TEXT("%s: it turned to face its target"), Who),
					   FRotator::NormalizeAxis(Copy->GetActorRotation().Yaw - ToTarget.Rotation().Yaw), 0.0, 1.0);

		Test.TestTrue(*FString::Printf(TEXT("%s: the swing connected"), Who),
					  CataclysmTestSwings::FinishTheSwingsOf(World, Copy));
		Test.TestTrue(*FString::Printf(TEXT("%s: and its blow landed then: %.1f to %.1f"), Who, Before, HealthOf(Target)),
					  HealthOf(Target) < Before);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkersBlowSwingsTest,
	"Cataclysm.PlayerCopies.AStalkersBlowSwingsThePlayersClipAndLandsWhenItConnects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Famine's entry, Hesk the Starveling, strikes a target a quarter turn to its left, inside its Sword's reach: it turns to
 * face it, plays `MM_Attack_01` into `DefaultSlot`, and its blow lands when the swing connects, not before. Its second
 * blow plays `MM_Attack_02`: the player's three clips, in turn. Ruled by the coordinating session on 2026-11-21 (#2811,
 * option A with the wait).
 *
 * THE CONTROL: a plain creature casting the same Sword's basic attack through the same `UCataclysmTriggeredSkill::Trigger`
 * swings nothing and waits for nothing, so the swing and the wait come from the copy, not from the path.
 */
bool FCataclysmStalkersBlowSwingsTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerCopiesTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const TArray<FCataclysmStalkerEntry> Entries = ACataclysmCorruptedStalkerCharacter::Entries();
	const FCataclysmStalkerEntry* Famine = EntryOf(Entries, TEXT("Famine"));
	ACataclysmCorruptedStalkerCharacter* Stalker =
		SpawnCopy<ACataclysmCorruptedStalkerCharacter>(World, FVector(0.0f, 0.0f, 0.0f));
	FRandomStream Stream(2811);
	if (!TestNotNull(TEXT("set-up: Famine's entry"), Famine)
		|| !TestNotNull(TEXT("set-up: a Corrupted Stalker was spawned"), Stalker)
		|| !TestTrue(TEXT("set-up: it wears Famine's entry"), Stalker->WearTheBuild(*Famine, 1, Stream))
		|| !TestNotNull(TEXT("set-up: its body is driven by an animation Blueprint"), Stalker->GetMesh()->GetAnimInstance()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Target = ATarget(World, FVector(0.0f, Stalker->AttackReachCm() * 0.5f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a target to its left"), Target))
	{
		return false;
	}
	FName Slot;
	TestEqual(TEXT("before it strikes its body plays no swing"), SwingPlaying(Stalker, Slot), FName());

	StrikesWithAPlayersSwing(*this, TEXT("its first blow"), Stalker, Target, World, TEXT("MM_Attack_01"));
	StrikesWithAPlayersSwing(*this, TEXT("its second blow"), Stalker, Target, World, TEXT("MM_Attack_02"));

	// THE CONTROL.
	const FCataclysmWeaponSkill Sword = UCataclysmWeaponSkills::BasicAttackFor(
		UCataclysmItemModifiers::LoadBaseTable(), Famine->WeaponType);
	ACataclysmEnemyCharacter* Plain = SpawnCopy<ACataclysmEnemyCharacter>(World, FVector(5000.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Beside = ATarget(World, FVector(5000.0f, Stalker->AttackReachCm() * 0.5f, 0.0f));
	if (!TestNotNull(TEXT("control set-up: a plain creature"), Plain) || !TestNotNull(TEXT("control set-up: a target"), Beside)
		|| !TestTrue(TEXT("control set-up: the Sword's basic attack is a skill"), Sword.Slot != ECataclysmAbilitySlot::None))
	{
		return false;
	}
	TestTrue(TEXT("control: the plain creature casts the same basic attack"),
			 UCataclysmTriggeredSkill::Trigger(Plain, Sword, Beside->GetActorLocation()));
	TestEqual(TEXT("control: it swings nothing"), SwingPlaying(Plain, Slot), FName());
	TestEqual(TEXT("control: and leaves no swing to wait for"), Plain->SecondsUntilTheSwingConnects(), 0.0f, 0.0001f);
	TestFalse(TEXT("control: so nothing of its waits"), CataclysmTestSwings::IsWaitingForASwing(Plain));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDoublesBlowSwingsTest,
	"Cataclysm.PlayerCopies.AWornResidueDoublesBlowSwingsAsTheStalkersDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Worn Residue double, wearing a build with a Sword, strikes a target a quarter turn to its left: the same swing,
 * wait and turn as the Stalker's, which it inherits. Issue #2811.
 */
bool FCataclysmDoublesBlowSwingsTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerCopiesTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmConsumptionDoubleCharacter* Double =
		SpawnCopy<ACataclysmConsumptionDoubleCharacter>(World, FVector(0.0f, 0.0f, 0.0f));
	FCataclysmPlayerBuild Build;
	Build.Worn = {WornIn(ECataclysmGearSlot::Weapon1, Weapon(TEXT("Weapon_Sword")))};
	Build.Level = 20;
	Build.ClassName = TEXT("Ravager");
	Build.CreationDamageType = FName(TEXT("Demonic"));
	if (!TestNotNull(TEXT("set-up: a Worn Residue double was spawned"), Double)
		|| !TestTrue(TEXT("set-up: it wears the build"), Double->WearThePlayersBuild(Build))
		|| !TestNotNull(TEXT("set-up: its body is driven by an animation Blueprint"), Double->GetMesh()->GetAnimInstance()))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Target = ATarget(World, FVector(0.0f, Double->AttackReachCm() * 0.5f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a target to its left"), Target))
	{
		return false;
	}
	StrikesWithAPlayersSwing(*this, TEXT("the double"), Double, Target, World, TEXT("MM_Attack_01"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkersMovementSkillSwingsNothingTest,
	"Cataclysm.PlayerCopies.AStalkersMovementSkillSwingsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A movement skill is not a swing, for a copy as for the player: `UCataclysmSkillTemplate::CommitAndBegin` plays no
 * clip for the Movement shape (the owner's report of 2026-09-02). Famine's Stalker casts its movement skill at a target
 * 8 m away; the cast is paid for, so the skill started, and its body plays no swing. Issue #2811.
 */
bool FCataclysmStalkersMovementSkillSwingsNothingTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerCopiesTest;
	using Vital = UCataclysmVitalAttributeSet;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const TArray<FCataclysmStalkerEntry> Entries = ACataclysmCorruptedStalkerCharacter::Entries();
	const FCataclysmStalkerEntry* Famine = EntryOf(Entries, TEXT("Famine"));
	ACataclysmCorruptedStalkerCharacter* Stalker =
		SpawnCopy<ACataclysmCorruptedStalkerCharacter>(World, FVector(0.0f, 0.0f, 0.0f));
	FRandomStream Stream(2811);
	const int32 Movement = ACataclysmCorruptedStalkerCharacter::ChoiceOrder().IndexOfByKey(ECataclysmAbilitySlot::Movement);
	if (!TestNotNull(TEXT("set-up: Famine's entry"), Famine)
		|| !TestNotNull(TEXT("set-up: a Corrupted Stalker was spawned"), Stalker)
		|| !TestTrue(TEXT("set-up: it wears Famine's entry"), Stalker->WearTheBuild(*Famine, 1, Stream))
		|| !TestNotNull(TEXT("set-up: its body is driven by an animation Blueprint"), Stalker->GetMesh()->GetAnimInstance())
		|| !TestTrue(TEXT("set-up: it has a movement slot"), Movement != INDEX_NONE))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Target = ATarget(World, FVector(800.0f, 0.0f, 0.0f));
	UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Stalker);
	if (!TestNotNull(TEXT("set-up: a target 8 m away"), Target) || !TestNotNull(TEXT("set-up: its abilities"), Abilities))
	{
		return false;
	}
	Abilities->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), 1000.0f);
	Abilities->SetNumericAttributeBase(Vital::GetManaAttribute(), 1000.0f);

	Stalker->UseEnemyAbility(Movement, Target, Target->GetActorLocation());
	TestTrue(TEXT("its movement skill was cast and paid for"),
			 Abilities->GetNumericAttribute(Vital::GetManaAttribute()) < 1000.0f);
	FName Slot;
	TestEqual(TEXT("and its body plays no swing"), SwingPlaying(Stalker, Slot), FName());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
