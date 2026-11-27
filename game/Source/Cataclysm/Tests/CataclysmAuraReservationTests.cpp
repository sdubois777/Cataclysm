// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmSkillLevels.h"
#include "Components/SphereComponent.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A toggled aura RESERVES maximum mana while it is on and drains none. Ruled 2026-11-27 by the coordinating session
 * under the owner's choice "Reserve, don't drain"; the owner: "Players will likely be running more than one at a time,
 * and right now that's impossible." The figures are the coordinating session's rulings: a flat 50 at skill level 1,
 * lowered by the skill's own level (0.9642 a level); the summed percent reduction held at 75% with a floor of 1; and a
 * condition reading a mana percentage reads the unreserved maximum.
 *
 * WHAT IS CHECKED, each with a control: room is measured against the maximum and not the pool; two auras fit a pool of
 * 100 and a third does not; the skill's level lowers the reservation and the character's does not; the cap and the
 * floor; a row scoped to auras reaches an aura and one scoped elsewhere does not; a maximum that falls lets the newest
 * aura go; regeneration fills only to the unreserved maximum; a mana percentage reads the unreserved maximum; and a
 * timed aura reserves nothing. AND, RULED THE SAME DAY ("reprice auras LIVE, as Fervour reserves are"): a level
 * gained lowers a held aura's reservation at once; a reduction taken off raises it at once and holds the mana; and a
 * reservation that no longer fits the whole maximum switches the aura off.
 *
 * THE CASTER IS A BARE ACTOR WITH THIS PROJECT'S ABILITY SYSTEM, as `Cataclysm.Skills.`'s is: no player state, so every
 * skill's level is 1 unless a test sets `PreviewSkillLevel`. Its pools are set by each test. Registered under
 * `Cataclysm.AuraReservation.`, which no other group's name begins with.
 */
namespace CataclysmAuraReservationTest
{
	using Vital = UCataclysmVitalAttributeSet;

	/** An actor carrying this project's ability system with health and mana. */
	struct FAuraReserver
	{
		explicit FAuraReserver(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			check(Actor);
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(Vital::GetMaxHealthAttribute(), 1000.0f);
			Set(Vital::GetHealthAttribute(), 1000.0f);
			Pools(1000.0f, 1000.0f);
		}

		~FAuraReserver()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		/** The maximum first, so the mana is not cut to a maximum about to rise. */
		void Pools(float MaximumMana, float Mana)
		{
			Set(Vital::GetMaxManaAttribute(), MaximumMana);
			Set(Vital::GetManaAttribute(), Mana);
		}

		float Mana() const { return AbilitySystem->GetNumericAttribute(Vital::GetManaAttribute()); }

		/**
		 * A toggled aura granted on `Key`, MADE FOR THE AURA SLOT whatever the key, as a player's aura moved to another
		 * key is (the 2026-10-25 ruling Q7). `Params` a shape cell; `TagCell` its tags.
		 */
		UCataclysmAuraSkill* Grant(ECataclysmAbilitySlot Key, const TCHAR* Params = TEXT("Radius=10; Interval=1"),
								   const TCHAR* TagCell = TEXT("Slot.Aura"))
		{
			const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
				UCataclysmAuraSkill::StaticClass(), Key, /*Level=*/1, Actor);
			FGameplayAbilitySpec* Spec = Handle.IsValid() ? AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmAuraSkill* Aura = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Aura)
			{
				Aura->DesignedSlot = ECataclysmAbilitySlot::Aura;
				Aura->SkillName = TEXT("An aura reservation test aura");
				Aura->Params = UCataclysmSkillShapes::ParseParams(Params);
				Aura->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
			}
			return Aura;
		}

		bool SwitchOn(UCataclysmAuraSkill* Aura)
		{
			return Aura && AbilitySystem->TryActivateAbility(Aura->GetCurrentAbilitySpecHandle(),
															 /*bAllowRemoteActivation=*/false);
		}

		void SwitchOff(UCataclysmAuraSkill* Aura)
		{
			if (Aura)
			{
				AbilitySystem->CancelAbilityHandle(Aura->GetCurrentAbilitySpecHandle());
			}
		}

		/** Replace the stat line with one row on one stat, scoped to `RequiredTag` when one is named. */
		void Holds(const TCHAR* Stat, ECataclysmStatBucket Bucket, float Value, const TCHAR* RequiredTag = nullptr)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = Bucket;
			Row.Source = ECataclysmModifierSource::Enchantment;
			Row.Value = Value;
			if (RequiredTag)
			{
				Row.RequiredTags.AddTag(
					UGameplayTagsManager::Get().RequestGameplayTag(FName(RequiredTag), /*ErrorIfNotFound=*/false));
			}
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Row};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	UWorld* MakeAuraReservationWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationRoomTest,
	"Cataclysm.AuraReservation.RoomIsMeasuredAgainstTheMaximumAndNotTheManaInHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A maximum of 40 has no room for 50, though the pool is full: refused. CONTROL: a maximum of 50 with NO mana in hand
 * has room, because a reservation is a claim on the maximum, as a Fervour reserve is (`UCataclysmCommand::
 * HasRoomToReserve`).
 */
bool FCataclysmAuraReservationRoomTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	UCataclysmAuraSkill* Aura = Caster.Grant(ECataclysmAbilitySlot::Aura);
	if (!TestNotNull(TEXT("set-up: an aura"), Aura))
	{
		return false;
	}
	Caster.Pools(40.0f, 40.0f);
	TestFalse(TEXT("a full pool of 40 has no room for 50"), Caster.SwitchOn(Aura));
	TestFalse(TEXT("so it is off"), Aura->IsHeld());
	TestEqual(TEXT("and nothing is reserved"), Caster.AbilitySystem->ManaReserved(), 0.0f, 0.001f);

	Caster.Pools(50.0f, 0.0f);
	TestTrue(TEXT("CONTROL: an empty pool of 50 has room for 50"), Caster.SwitchOn(Aura));
	TestEqual(TEXT("and holds all 50"), Caster.AbilitySystem->ManaReserved(), 50.0f, 0.001f);
	TestEqual(TEXT("leaving nothing unreserved"), Caster.AbilitySystem->UnreservedMaximumMana(), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationSeveralTest,
	"Cataclysm.AuraReservation.TwoAurasFitAPoolOfAHundredAndAThirdDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner: "Players will likely be running more than one at a time." Three auras on three keys, each made for the
 * Aura slot. On a pool of 100 the first two switch on and hold 50 each; the third is refused. CONTROL: with the first
 * switched off, the third fits.
 */
bool FCataclysmAuraReservationSeveralTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	UCataclysmAuraSkill* First = Caster.Grant(ECataclysmAbilitySlot::Aura);
	UCataclysmAuraSkill* Second = Caster.Grant(ECataclysmAbilitySlot::Heavy);
	UCataclysmAuraSkill* Third = Caster.Grant(ECataclysmAbilitySlot::Special);
	if (!TestTrue(TEXT("set-up: three auras"), First && Second && Third))
	{
		return false;
	}
	Caster.Pools(100.0f, 100.0f);
	TestTrue(TEXT("the first switches on"), Caster.SwitchOn(First));
	TestTrue(TEXT("the second, on the Heavy key, switches on"), Caster.SwitchOn(Second));
	TestEqual(TEXT("together they hold 100"), Caster.AbilitySystem->ManaReserved(), 100.0f, 0.001f);
	TestFalse(TEXT("the third has no room"), Caster.SwitchOn(Third));
	TestFalse(TEXT("so it is off"), Third->IsHeld());

	Caster.SwitchOff(First);
	TestFalse(TEXT("set-up: the first is off"), First->IsHeld());
	TestEqual(TEXT("which gave its 50 back"), Caster.AbilitySystem->ManaReserved(), 50.0f, 0.001f);
	TestTrue(TEXT("CONTROL: now the third fits"), Caster.SwitchOn(Third));
	TestEqual(TEXT("and the two on hold 100"), Caster.AbilitySystem->ManaReserved(), 100.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationLevelTest,
	"Cataclysm.AuraReservation.TheSkillsLevelLowersTheReservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-11-27: "A flat 50, lowered by the aura skill's own level, the existing 0.9642 a level (about 25 at skill
 * level 20)." The expected figure is worked from the rounded constant the code holds, 0.9642 to the 19th power, not from
 * the curve. CONTROL: at skill level 1 it is 50.
 */
bool FCataclysmAuraReservationLevelTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	UCataclysmAuraSkill* Aura = Caster.Grant(ECataclysmAbilitySlot::Aura);
	if (!TestNotNull(TEXT("set-up: an aura"), Aura))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: at skill level 1 it reserves 50"), Aura->ManaReservationFor(Caster.AbilitySystem), 50.0f,
			  0.001f);
	Aura->PreviewSkillLevel = 20;
	const float AtTwenty = 50.0f * FMath::Pow(static_cast<float>(UCataclysmSkillLevels::CostFactorPerLevel), 19.0f);
	TestEqual(FString::Printf(TEXT("at skill level 20 it reserves 50 x 0.9642^19 = %.3f"), AtTwenty),
			  Aura->ManaReservationFor(Caster.AbilitySystem), AtTwenty, 0.01f);
	TestTrue(TEXT("which is about 25"), FMath::Abs(AtTwenty - 25.0f) < 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationFlatAcrossLevelsTest,
	"Cataclysm.AuraReservation.AnAurasReservationIsTheSameAtCharacterLevelOneAndAHundred",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The reservation does not grow with the character, which is what makes an aura hard to hold early and easy late, as
 * the owner asked. A caster with no player state prices a skill at the level it was granted at. An aura granted at
 * level 1 and one granted at level 100, each made for the Aura slot, both reserve 50. CONTROL: two more granted at
 * those levels and made for the Heavy slot cost different amounts, so the level does reach a cost.
 */
bool FCataclysmAuraReservationFlatAcrossLevelsTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	const auto GrantAt = [&Caster](ECataclysmAbilitySlot Key, int32 Level, ECataclysmAbilitySlot MadeFor)
	{
		const FGameplayAbilitySpecHandle Handle =
			Caster.AbilitySystem->GiveAbilityInSlot(UCataclysmAuraSkill::StaticClass(), Key, Level, Caster.Actor);
		FGameplayAbilitySpec* Spec = Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UCataclysmAuraSkill* Aura = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Aura)
		{
			Aura->DesignedSlot = MadeFor;
		}
		return Aura;
	};
	UCataclysmAuraSkill* AuraAtOne = GrantAt(ECataclysmAbilitySlot::Aura, 1, ECataclysmAbilitySlot::Aura);
	UCataclysmAuraSkill* AuraAtHundred = GrantAt(ECataclysmAbilitySlot::Heavy, 100, ECataclysmAbilitySlot::Aura);
	UCataclysmAuraSkill* HeavyAtOne = GrantAt(ECataclysmAbilitySlot::Special, 1, ECataclysmAbilitySlot::Heavy);
	UCataclysmAuraSkill* HeavyAtHundred = GrantAt(ECataclysmAbilitySlot::Movement, 100, ECataclysmAbilitySlot::Heavy);
	if (!TestTrue(TEXT("set-up: four skills"), AuraAtOne && AuraAtHundred && HeavyAtOne && HeavyAtHundred))
	{
		return false;
	}
	const float CostAtOne = HeavyAtOne->ManaCostFor(Caster.AbilitySystem);
	const float CostAtHundred = HeavyAtHundred->ManaCostFor(Caster.AbilitySystem);
	TestTrue(FString::Printf(TEXT("CONTROL: a Heavy cost at level 1 (%.2f) is below its cost at level 100 (%.2f)"),
							 CostAtOne, CostAtHundred),
			 CostAtOne > 0.0f && CostAtOne < CostAtHundred);
	TestEqual(TEXT("granted at level 1 an aura reserves 50"), AuraAtOne->ManaReservationFor(Caster.AbilitySystem),
			  50.0f, 0.001f);
	TestEqual(TEXT("granted at level 100 it reserves 50 too"), AuraAtHundred->ManaReservationFor(Caster.AbilitySystem),
			  50.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationCapTest,
	"Cataclysm.AuraReservation.TheSummedReductionStopsAtSeventyFivePercentAndTheFloorIsOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-11-27: "The 75% cap on summed reductions applies to BOTH mana and Fervour: one rule in ReserveFor, with
 * the floor of 1." Of an aura's 50: CONTROL, 60% less is 20, under the cap; 90% less is held at 75% and is 12.5; 50%
 * MORE, a negative row, is not held by the cap and is 75; and 60 points off, more than all of it, leave the floor of 1.
 */
bool FCataclysmAuraReservationCapTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	UCataclysmAuraSkill* Aura = Caster.Grant(ECataclysmAbilitySlot::Aura);
	if (!TestNotNull(TEXT("set-up: an aura"), Aura))
	{
		return false;
	}
	const auto Reserves = [&]() { return Aura->ManaReservationFor(Caster.AbilitySystem); };

	Caster.Holds(UCataclysmCommand::ManaReservePercentLessStat, ECataclysmStatBucket::Flat, 60.0f);
	TestEqual(TEXT("CONTROL: 60% less of 50 is 20"), Reserves(), 20.0f, 0.001f);
	Caster.Holds(UCataclysmCommand::ManaReservePercentLessStat, ECataclysmStatBucket::Flat, 90.0f);
	TestEqual(TEXT("90% less is held at 75%: 12.5"), Reserves(), 12.5f, 0.001f);
	Caster.Holds(UCataclysmCommand::ManaReservePercentLessStat, ECataclysmStatBucket::Flat, -50.0f);
	TestEqual(TEXT("50% more is not held by the cap: 75"), Reserves(), 75.0f, 0.001f);
	Caster.Holds(UCataclysmCommand::ManaReserveReductionStat, ECataclysmStatBucket::Flat, 60.0f);
	TestEqual(TEXT("60 points off 50 leave the floor of 1"), Reserves(), UCataclysmCommand::SmallestManaReserve,
			  0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationScopeTest,
	"Cataclysm.AuraReservation.ARowScopedToAurasReachesAnAuraAndOneScopedToTheHeavySlotDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The enchantment rows carry `Slot.Aura` as their required tag. A `mana_reserve_percent_less` row of 50 requiring
 * `Slot.Aura` halves the reservation of an aura tagged `Slot.Aura`: 25, and switching on holds 25. CONTROL: the same
 * row requiring `Slot.Heavy` leaves it 50.
 */
bool FCataclysmAuraReservationScopeTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	UCataclysmAuraSkill* Aura = Caster.Grant(ECataclysmAbilitySlot::Aura);
	if (!TestNotNull(TEXT("set-up: an aura"), Aura))
	{
		return false;
	}
	Caster.Holds(UCataclysmCommand::ManaReservePercentLessStat, ECataclysmStatBucket::Flat, 50.0f, TEXT("Slot.Heavy"));
	TestEqual(TEXT("CONTROL: a row scoped to the Heavy slot leaves it 50"),
			  Aura->ManaReservationFor(Caster.AbilitySystem), 50.0f, 0.001f);
	Caster.Holds(UCataclysmCommand::ManaReservePercentLessStat, ECataclysmStatBucket::Flat, 50.0f, TEXT("Slot.Aura"));
	TestEqual(TEXT("a row scoped to auras halves it: 25"), Aura->ManaReservationFor(Caster.AbilitySystem), 25.0f,
			  0.001f);
	TestTrue(TEXT("it switches on"), Caster.SwitchOn(Aura));
	TestEqual(TEXT("and holds 25"), Caster.AbilitySystem->ManaReserved(), 25.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationFallingMaximumTest,
	"Cataclysm.AuraReservation.AMaximumThatFallsBelowWhatIsReservedLetsTheNewestAuraGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two auras on a pool of 100. CONTROL: at 100 nothing is let go. With the maximum lowered to 60 -- a passive point
 * taken back, a buff ending -- the newer is let go and says so (`bEndedForLackOfMana`), and the older still holds its
 * 50, which fits.
 */
bool FCataclysmAuraReservationFallingMaximumTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	UCataclysmAuraSkill* Older = Caster.Grant(ECataclysmAbilitySlot::Aura);
	UCataclysmAuraSkill* Newer = Caster.Grant(ECataclysmAbilitySlot::Heavy);
	if (!TestTrue(TEXT("set-up: two auras"), Older && Newer))
	{
		return false;
	}
	Caster.Pools(100.0f, 100.0f);
	if (!TestTrue(TEXT("set-up: both switch on"), Caster.SwitchOn(Older) && Caster.SwitchOn(Newer)))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: at a maximum of 100 nothing is let go"),
			  Caster.AbilitySystem->ReleaseAurasThatNoLongerFit(), 0);
	TestTrue(TEXT("CONTROL: both are on"), Older->IsHeld() && Newer->IsHeld());

	Caster.Set(Vital::GetMaxManaAttribute(), 60.0f);
	TestEqual(TEXT("at 60 one is let go"), Caster.AbilitySystem->ReleaseAurasThatNoLongerFit(), 1);
	TestFalse(TEXT("the newer"), Newer->IsHeld());
	TestTrue(TEXT("which says it no longer fitted"), Newer->bEndedForLackOfMana);
	TestTrue(TEXT("the older is still on"), Older->IsHeld());
	TestFalse(TEXT("and did not end"), Older->bEndedForLackOfMana);
	TestEqual(TEXT("holding its 50"), Caster.AbilitySystem->ManaReserved(), 50.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationRegenerationTest,
	"Cataclysm.AuraReservation.RegenerationFillsManaOnlyToTheUnreservedMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Reserved mana is never filled, as reserved health is not. With an aura on a pool of 1,000 and the mana emptied, a
 * gain of 5,000 through `UCataclysmRegeneration::TopUp` -- the one function regeneration and mana leech restore through
 * -- fills to 950. CONTROL: with the aura switched off the same gain fills to 1,000.
 */
bool FCataclysmAuraReservationRegenerationTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	UCataclysmAuraSkill* Aura = Caster.Grant(ECataclysmAbilitySlot::Aura);
	if (!TestNotNull(TEXT("set-up: an aura"), Aura) || !TestTrue(TEXT("set-up: it switches on"), Caster.SwitchOn(Aura)))
	{
		return false;
	}
	Caster.Set(Vital::GetManaAttribute(), 0.0f);
	UCataclysmRegeneration::TopUp(*Caster.AbilitySystem, Vital::GetManaAttribute(), Vital::GetMaxManaAttribute(),
								  5000.0f);
	TestEqual(TEXT("with the aura on, mana fills to 950"), Caster.Mana(), 950.0f, 0.01f);

	Caster.SwitchOff(Aura);
	Caster.Set(Vital::GetManaAttribute(), 0.0f);
	UCataclysmRegeneration::TopUp(*Caster.AbilitySystem, Vital::GetManaAttribute(), Vital::GetMaxManaAttribute(),
								  5000.0f);
	TestEqual(TEXT("CONTROL: with it off, mana fills to 1,000"), Caster.Mana(), 1000.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationPercentTest,
	"Cataclysm.AuraReservation.AManaPercentageReadsTheUnreservedMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-11-27: "Conditions that read a mana percentage read the unreserved maximum." With an aura on a pool of
 * 1,000, the 950 it may hold reads 100%. CONTROL: with the aura off, 950 of 1,000 reads 95%.
 */
bool FCataclysmAuraReservationPercentTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	UCataclysmAuraSkill* Aura = Caster.Grant(ECataclysmAbilitySlot::Aura);
	if (!TestNotNull(TEXT("set-up: an aura"), Aura) || !TestTrue(TEXT("set-up: it switches on"), Caster.SwitchOn(Aura)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: mana is held to 950"), Caster.Mana(), 950.0f, 0.01f);
	TestEqual(TEXT("with the aura on, 950 reads 100%"), Caster.AbilitySystem->CurrentConditions().ManaPercent, 100.0f,
			  0.01f);

	Caster.SwitchOff(Aura);
	TestEqual(TEXT("CONTROL: with it off, 950 of 1,000 reads 95%"),
			  Caster.AbilitySystem->CurrentConditions().ManaPercent, 95.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationTimedTest,
	"Cataclysm.AuraReservation.ATimedAuraReservesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A timed aura -- Living Pyre is an Ultimate of 6 seconds -- pays its slot's cost once and holds nothing, as it drained
 * nothing before 2026-11-27. An aura made for the Aura slot with a Duration of 6 switches on and reserves nothing.
 * CONTROL: the same aura with no Duration reserves 50.
 */
bool FCataclysmAuraReservationTimedTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	UCataclysmAuraSkill* Toggled = Caster.Grant(ECataclysmAbilitySlot::Aura);
	UCataclysmAuraSkill* Timed = Caster.Grant(ECataclysmAbilitySlot::Heavy, TEXT("Radius=10; Interval=1; Duration=6"));
	if (!TestTrue(TEXT("set-up: two auras"), Toggled && Timed))
	{
		return false;
	}
	TestTrue(TEXT("CONTROL: the toggled aura reserves"), Toggled->Reserves(Caster.AbilitySystem));
	TestFalse(TEXT("the timed one does not"), Timed->Reserves(Caster.AbilitySystem));
	TestTrue(TEXT("it switches on"), Caster.SwitchOn(Timed));
	TestEqual(TEXT("and holds nothing"), Caster.AbilitySystem->ManaReserved(), 0.0f, 0.001f);
	TestEqual(TEXT("so the mana is untouched"), Caster.Mana(), 1000.0f, 0.01f);
	return true;
}

namespace CataclysmAuraReservationTest
{
	/**
	 * A possessed player holding a Demonic Staff, as `Cataclysm.LevelCosts.`'s is, so its skills carry their rows and
	 * a level gained goes through `ACataclysmPlayerState::SetSkillExperience`. Character level 32, which a skill level of
	 * 10 is paired with. Null fields when the set-up fails.
	 */
	struct FAuraLevelPlayer
	{
		ACataclysmPlayerState* State = nullptr;
		UCataclysmAbilitySystemComponent* Abilities = nullptr;
		UCataclysmAuraSkill* Conflagration = nullptr;
	};

	FAuraLevelPlayer DemonicStaffAuraPlayerIn(UWorld* World)
	{
		FAuraLevelPlayer Out;
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!State || !Controller || !Character)
		{
			return Out;
		}
		Controller->SetPlayerState(State);
		Controller->Possess(Character);
		UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
		UCataclysmWeaponSlotsComponent* Slots = Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
		if (!System || !Slots)
		{
			return Out;
		}
		Slots->SetDamageType(TEXT("Demonic"));
		Slots->EquipWeaponType(TEXT("Staff"));
		System->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), 1000.0f);
		System->SetNumericAttributeBase(Vital::GetManaAttribute(), 1000.0f);
		State->SetLevelAndExperience(32, 0);
		for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
		{
			UCataclysmAuraSkill* Aura = Cast<UCataclysmAuraSkill>(Spec.GetPrimaryInstance());
			if (Aura && Aura->SkillRow == FName(TEXT("Demonic_All_Aura")))
			{
				Out.Conflagration = Aura;
			}
		}
		Out.State = State;
		Out.Abilities = System;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationLevelUpTest,
	"Cataclysm.AuraReservation.ALevelGainedLowersAHeldAurasReservationAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-11-27: "A skill level gained while the aura is on lowers its reservation at once." A Demonic Staff
 * player switches Conflagration on: CONTROL, it holds 50 at skill level 1. Its row is raised to skill level 10 through
 * `SetSkillExperience`, with the aura still on: it now holds 50 times 0.9642 to the ninth (0.72028, written out as
 * `Cataclysm.LevelCosts.` writes it), and is still on. Nothing switched it off and on again.
 */
bool FCataclysmAuraReservationLevelUpTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FAuraLevelPlayer Player = DemonicStaffAuraPlayerIn(World);
	if (!TestTrue(TEXT("set-up: a Demonic Staff player holding Conflagration"), Player.Conflagration != nullptr))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: it switches on"), Player.Abilities->TryActivateAbility(
			Player.Conflagration->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false)))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: at skill level 1 it holds 50"), Player.Conflagration->ReservedNow(), 50.0f, 0.01f);

	Player.State->SetSkillExperience(FName(TEXT("Demonic_All_Aura")), UCataclysmSkillLevels::ExperienceToReach(10));
	if (!TestEqual(TEXT("set-up: Conflagration is at skill level 10"),
				   Player.State->SkillLevelOf(FName(TEXT("Demonic_All_Aura"))), 10))
	{
		return false;
	}
	TestTrue(TEXT("it is still on"), Player.Conflagration->IsHeld());
	TestEqual(TEXT("and at once holds 50 x 0.72028 = 36.01"), Player.Conflagration->ReservedNow(), 50.0f * 0.72028f,
			  0.01f);
	TestEqual(TEXT("which is what the character's reserved mana reads"), Player.Abilities->ManaReserved(),
			  50.0f * 0.72028f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationRowOffTest,
	"Cataclysm.AuraReservation.AReductionTakenOffRaisesAHeldAurasReservationAtOnceAndHoldsTheMana",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-11-27: a change to the reduction stats, "from equipping gear or spending a point: up or down", moves a
 * held aura's reservation at once, and current mana is held to the new ceiling. On a pool of 1,000 a caster holding
 * "50% reduced" switches an aura on: CONTROL, it holds 25 and the mana stands at 975. The row is taken off by writing
 * the stat line without it, as every refresh does when gear comes off (`UCataclysmPlayerClassStats::ApplyTo` ends in
 * `SetStatInputs`): it holds 50 at once, the mana is held to 950, and the aura is still on. NOT GEAR ITSELF: the
 * equipment's own refresh is not driven here.
 */
bool FCataclysmAuraReservationRowOffTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Caster(World);
	UCataclysmAuraSkill* Aura = Caster.Grant(ECataclysmAbilitySlot::Aura);
	if (!TestNotNull(TEXT("set-up: an aura"), Aura))
	{
		return false;
	}
	Caster.Holds(UCataclysmCommand::ManaReservePercentLessStat, ECataclysmStatBucket::Flat, 50.0f);
	if (!TestTrue(TEXT("set-up: it switches on"), Caster.SwitchOn(Aura)))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: under 50% reduced it holds 25"), Aura->ReservedNow(), 25.0f, 0.001f);
	TestEqual(TEXT("CONTROL: and the mana stands at 975"), Caster.Mana(), 975.0f, 0.01f);

	Caster.AbilitySystem->SetStatInputs(TMap<FName, FCataclysmStatInputs>());
	TestTrue(TEXT("with the row taken off the aura is still on"), Aura->IsHeld());
	TestEqual(TEXT("and at once holds 50"), Aura->ReservedNow(), 50.0f, 0.001f);
	TestEqual(TEXT("and the mana is held to 950"), Caster.Mana(), 950.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraReservationImpossibleTest,
	"Cataclysm.AuraReservation.AReservationThatNoLongerFitsTheWholeMaximumSwitchesTheAuraOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-11-27: "The aura stays on unless the reservation would exceed the whole maximum. In that case it switches
 * off, with a log line." Two casters each switch an aura on under "50% reduced", holding 25. The row is taken off both.
 * CONTROL: on a maximum of 60 the 50 still fits, and the aura stays on. On a maximum of 40 it does not: the aura is
 * switched off, says it no longer fitted, and nothing is reserved.
 */
bool FCataclysmAuraReservationImpossibleTest::RunTest(const FString&)
{
	using namespace CataclysmAuraReservationTest;
	UWorld* World = MakeAuraReservationWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FAuraReserver Roomy(World);
	FAuraReserver Small(World);
	UCataclysmAuraSkill* RoomyAura = Roomy.Grant(ECataclysmAbilitySlot::Aura);
	UCataclysmAuraSkill* SmallAura = Small.Grant(ECataclysmAbilitySlot::Aura);
	if (!TestTrue(TEXT("set-up: two auras"), RoomyAura && SmallAura))
	{
		return false;
	}
	Roomy.Pools(60.0f, 60.0f);
	Small.Pools(40.0f, 40.0f);
	Roomy.Holds(UCataclysmCommand::ManaReservePercentLessStat, ECataclysmStatBucket::Flat, 50.0f);
	Small.Holds(UCataclysmCommand::ManaReservePercentLessStat, ECataclysmStatBucket::Flat, 50.0f);
	if (!TestTrue(TEXT("set-up: both switch on, holding 25"), Roomy.SwitchOn(RoomyAura) && Small.SwitchOn(SmallAura)))
	{
		return false;
	}

	Roomy.AbilitySystem->SetStatInputs(TMap<FName, FCataclysmStatInputs>());
	Small.AbilitySystem->SetStatInputs(TMap<FName, FCataclysmStatInputs>());

	TestTrue(TEXT("CONTROL: on a maximum of 60 the 50 fits and the aura stays on"), RoomyAura->IsHeld());
	TestEqual(TEXT("CONTROL: holding 50"), Roomy.AbilitySystem->ManaReserved(), 50.0f, 0.001f);
	TestFalse(TEXT("on a maximum of 40 the 50 does not fit, and the aura is switched off"), SmallAura->IsHeld());
	TestTrue(TEXT("saying it no longer fitted"), SmallAura->bEndedForLackOfMana);
	TestEqual(TEXT("and nothing is reserved"), Small.AbilitySystem->ManaReserved(), 0.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
