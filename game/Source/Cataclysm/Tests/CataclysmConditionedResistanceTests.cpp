// Copyright Stephen Dubois. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A resistance row under a condition reaches a blow. Ruled 2026-10-18 by the coordinating session, for Brute's Heart's
 * six-piece "increases your armor and resistances by 50%" below half health.
 *
 * `UCataclysmDamageCalculation::TypedResistanceOf` reads the attribute PLUS only what the stat refresh could not fold
 * into it (`UCataclysmAbilitySystemComponent::AttributeWithWhatWasNotFolded`). Four reads go through it: the blow's
 * typed resistance, a minion's share of its summoner's, the character sheet's line and `Cataclysm.ShowResistances`.
 *
 * THE ROWS ARE GIVEN AS STAT LINES, `SetStatInputs` as the refresh records them, and the attribute is written by hand
 * as the refresh writes it: every unconditioned row inside it, every conditioned row refused. The real rows, worn at
 * six pieces with a control at five, are the enchantment session's test.
 *
 * NO CLOCK RUNS AND NOTHING IS STRUCK BY A SKILL. A blow is `Resolve` with every roll pinned to miss. A fighter is put
 * below half health by writing its health. Every body stands at least 3 m from every other.
 */
namespace CataclysmConditionedResistanceTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Resist = UCataclysmResistanceAttributeSet;

	/** Centimetres in a metre. */
	constexpr float ConditionedResistanceMetre = 100.0f;

	/** Every fighter's maximum health: small enough that a blow reads as a difference. */
	constexpr float ConditionedResistancePool = 10000.0f;

	/** How far a figure may fall from one it was built to equal. */
	constexpr float ConditionedResistanceAllowance = 0.001f;

	FGameplayTag ConditionedResistanceTagNamed(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/** A bare fighter holding the eight per-type resistances, the generic one, health and the combat set. */
	struct FConditionedResistanceFighter
	{
		FConditionedResistanceFighter(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper
			// rather than the set.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist = NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResist = NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAllResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(Vital::GetMaxHealthAttribute(), ConditionedResistancePool);
			Set(Vital::GetHealthAttribute(), ConditionedResistancePool);
		}

		~FConditionedResistanceFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		/** Health at this share of the pool. */
		void AtHealthShare(float Share) const
		{
			Set(Vital::GetHealthAttribute(), ConditionedResistancePool * Share);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** The per-type resistance attribute of a damage type. */
	FGameplayAttribute ConditionedResistanceSlot(const TCHAR* Type)
	{
		return UCataclysmDamageCalculation::ResistanceAttributeFor(FName(Type));
	}

	/** One modifier, unconditioned unless `bBelowHalf`, which holds it only below half health. */
	FCataclysmStatModifier ConditionedResistanceModifier(ECataclysmStatBucket Bucket, ECataclysmModifierSource Source,
														 float Value, bool bBelowHalf)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = Source;
		Row.Value = Value;
		if (bBelowHalf)
		{
			Row.Condition = ECataclysmStatCondition::HealthBelowPercent;
			Row.ConditionValue = 50.0f;
		}
		return Row;
	}

	/** Brute's Heart's own shape: 50 increased, below half health. */
	FCataclysmStatModifier ConditionedResistanceBruteRow()
	{
		return ConditionedResistanceModifier(ECataclysmStatBucket::Increased, ECataclysmModifierSource::Enchantment,
											 50.0f, /*bBelowHalf=*/true);
	}

	/** Put one damage type's resistance line into a set of lines, under the name the refresh records it by. */
	void ConditionedResistanceLine(TMap<FName, FCataclysmStatInputs>& Lines, const TCHAR* Type, float Base,
								   const TArray<FCataclysmStatModifier>& Modifiers)
	{
		FCataclysmStatInputs& Line = Lines.FindOrAdd(UCataclysmItemModifiers::ResistanceStatFor(FName(Type)));
		Line.Base = Base;
		Line.Modifiers = Modifiers;
	}

	/** A summoner's flat share of its defences handed to its minions. */
	void ConditionedResistanceShareLine(TMap<FName, FCataclysmStatInputs>& Lines, float Percent)
	{
		FCataclysmStatInputs& Line =
			Lines.FindOrAdd(FName(UCataclysmDamageCalculation::MinionDefencesPercentOfYoursStat));
		Line.Base = 0.0f;
		Line.Modifiers = {ConditionedResistanceModifier(ECataclysmStatBucket::Flat, ECataclysmModifierSource::Enchantment,
														Percent, /*bBelowHalf=*/false)};
	}

	/** The per-type read, as a blow makes it. */
	float ConditionedResistanceRead(const UAbilitySystemComponent* Defender, const TCHAR* Type)
	{
		return UCataclysmDamageCalculation::TypedResistanceOf(Defender, FName(Type));
	}

	/** What a blow of 100 of this damage type takes from this character's health, with every roll pinned to miss. */
	float ConditionedResistanceBlow(const UAbilitySystemComponent* Defender, const TCHAR* Type)
	{
		FCataclysmIncomingHit Blow;
		Blow.Damage = 100.0f;
		Blow.DamageType = FName(Type);
		return UCataclysmDamageCalculation::Resolve(Blow, Defender, /*Tier=*/1,
			/*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f, /*CritRoll=*/100.0f).DealtToHealth;
	}

	/** The character sheet's line for one damage type's resistance, at the first difficulty tier. */
	FCataclysmStatLine ConditionedResistanceSheetLine(const UAbilitySystemComponent* Who, const TCHAR* Type)
	{
		return UCataclysmCharacterSheetLayout::LineFor(
			UCataclysmItemModifiers::ResistanceStatFor(FName(Type)).ToString(), Who, /*DifficultyTier=*/1);
	}

	/** An imp of this summoner's, with health enough that a blow of 100 is never cut short. */
	UAbilitySystemComponent* ConditionedResistanceImp(FAutomationTestBase& Test, AActor* Summoner, const FVector& Where)
	{
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Summoner, Where, /*Lifetime=*/60.0f, /*bBurns=*/false,
														TEXT("Imp"));
		UAbilitySystemComponent* System = Imp ? UCataclysmTargeting::AbilitySystemOf(Imp) : nullptr;
		if (!Test.TestNotNull(TEXT("set-up: an imp from DT_MinionTypes (run tools/generate_datatable_assets.py if not)"),
							  System))
		{
			return nullptr;
		}
		System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), ConditionedResistancePool);
		System->SetNumericAttributeBase(Vital::GetHealthAttribute(), ConditionedResistancePool);
		return System;
	}

	/** What `Cataclysm.ShowResistances` prints before a damage type's held figure, and that figure. */
	FString ConditionedResistanceHeldText(const TCHAR* Type, float Figure)
	{
		return FString::Printf(TEXT("%-12s held %6.1f,"), Type, Figure);
	}

	/** An output device that keeps what was printed instead of showing it. */
	struct FConditionedResistanceOutput : public FOutputDevice
	{
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type, const class FName&) override
		{
			Lines.Add(FString(Text));
		}

		FString All() const
		{
			return FString::Join(Lines, TEXT("\n"));
		}

		TArray<FString> Lines;
	};
}

// --------------------------------------------------------------------------------------------------------------------
// 1. WITH NO CONDITIONED OR SCALED ROW, EVERY READ IS THE ATTRIBUTE, AS IT WAS
// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConditionedResistanceUnchangedTest,
	"Cataclysm.ConditionedResistance.WithNoConditionedRowTheBlowTheMinionShareAndTheSheetReadTheAttributeAsBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A wearer whose four resistances come from a plain row, an affix, an affix's increase and nothing recorded, and a
 * twin holding the same four attributes and no resistance line at all. The twin's read is the attribute by the path the
 * change does not touch, so it is the figure read before the change. Both hand their minions half their defences.
 *
 * Four stages, each read for War (a plain row's flat 30), Demonic (an affix's flat 40), Death (a base of 20 and an
 * affix's 50 increased, 30) and Famine (15 written with no line): at full health; below half health, so a
 * condition about health would hold if any row had one; with a Shred of 30 on Demonic; and with an Abyssal Aura of 25,
 * which cuts War and Demonic. In each: the wearer's read is its attribute; a blow of each type takes from the wearer
 * what it takes from the twin; the sheet's line is the twin's; and each one's imp takes the same.
 */
bool FCataclysmConditionedResistanceUnchangedTest::RunTest(const FString&)
{
	using namespace CataclysmConditionedResistanceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float M = ConditionedResistanceMetre;
	FConditionedResistanceFighter Wearer(World, FVector::ZeroVector);
	FConditionedResistanceFighter Twin(World, FVector(3 * M, 0.0f, 0.0f));
	FConditionedResistanceFighter Instigator(World, FVector(0.0f, 3 * M, 0.0f));

	TMap<FName, FCataclysmStatInputs> WearerLines;
	ConditionedResistanceLine(WearerLines, TEXT("War"), 0.0f,
		{ConditionedResistanceModifier(ECataclysmStatBucket::Flat, ECataclysmModifierSource::Enchantment, 30.0f, false)});
	ConditionedResistanceLine(WearerLines, TEXT("Demonic"), 0.0f,
		{ConditionedResistanceModifier(ECataclysmStatBucket::Flat, ECataclysmModifierSource::GearAffix, 40.0f, false)});
	ConditionedResistanceLine(WearerLines, TEXT("Death"), 20.0f,
		{ConditionedResistanceModifier(ECataclysmStatBucket::Increased, ECataclysmModifierSource::GearAffix, 50.0f,
									   false)});
	ConditionedResistanceShareLine(WearerLines, 50.0f);
	Wearer.AbilitySystem->SetStatInputs(MoveTemp(WearerLines));
	TMap<FName, FCataclysmStatInputs> TwinLines;
	ConditionedResistanceShareLine(TwinLines, 50.0f);
	Twin.AbilitySystem->SetStatInputs(MoveTemp(TwinLines));

	const TCHAR* const Types[] = {TEXT("War"), TEXT("Demonic"), TEXT("Death"), TEXT("Famine")};
	const float Figures[] = {30.0f, 40.0f, 30.0f, 15.0f};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Wearer.Set(ConditionedResistanceSlot(Types[Index]), Figures[Index]);
		Twin.Set(ConditionedResistanceSlot(Types[Index]), Figures[Index]);
	}
	if (!TestEqual(TEXT("set-up: the wearer's Death line comes to 30, so the lines are recorded"),
				   Wearer.AbilitySystem->StatForSkill(FName(TEXT("resistance_death")), FGameplayTagContainer(), 0.0f),
				   30.0f, ConditionedResistanceAllowance))
	{
		return false;
	}

	UAbilitySystemComponent* WearerImp = ConditionedResistanceImp(*this, Wearer.Actor, FVector(-3 * M, 0.0f, 0.0f));
	UAbilitySystemComponent* TwinImp = ConditionedResistanceImp(*this, Twin.Actor, FVector(6 * M, 0.0f, 0.0f));
	if (!WearerImp || !TwinImp)
	{
		return false;
	}

	// THE CONTROL, READ FIRST: the twin has no line, so its read is its attribute by the path the change leaves alone.
	TestEqual(TEXT("control: the twin, with no resistance line, reads its War attribute"),
			  ConditionedResistanceRead(Twin.AbilitySystem, TEXT("War")), 30.0f, ConditionedResistanceAllowance);

	const auto EveryReadIsAsBefore = [&](const TCHAR* Stage)
	{
		for (const TCHAR* Type : Types)
		{
			TestEqual(*FString::Printf(TEXT("%s: %s reads its attribute, as before"), Stage, Type),
					  ConditionedResistanceRead(Wearer.AbilitySystem, Type),
					  Wearer.Get(ConditionedResistanceSlot(Type)), ConditionedResistanceAllowance);
			TestEqual(*FString::Printf(TEXT("%s: a %s blow takes from the wearer what it takes from the twin"),
									   Stage, Type),
					  ConditionedResistanceBlow(Wearer.AbilitySystem, Type),
					  ConditionedResistanceBlow(Twin.AbilitySystem, Type), ConditionedResistanceAllowance);
			TestEqual(*FString::Printf(TEXT("%s: the sheet's %s line is the twin's"), Stage, Type),
					  ConditionedResistanceSheetLine(Wearer.AbilitySystem, Type).AsLine(),
					  ConditionedResistanceSheetLine(Twin.AbilitySystem, Type).AsLine());
			TestEqual(*FString::Printf(TEXT("%s: a %s blow takes from the wearer's imp what it takes from the twin's"),
									   Stage, Type),
					  ConditionedResistanceBlow(WearerImp, Type),
					  ConditionedResistanceBlow(TwinImp, Type), ConditionedResistanceAllowance);
		}
	};

	EveryReadIsAsBefore(TEXT("at full health"));

	Wearer.AtHealthShare(0.4f);
	Twin.AtHealthShare(0.4f);
	EveryReadIsAsBefore(TEXT("below half health"));

	const FGameplayTag Shred = ConditionedResistanceTagNamed(TEXT("Status.Debuff.Shred"));
	for (const FConditionedResistanceFighter* Each : {&Wearer, &Twin})
	{
		UCataclysmSkillEffects::ApplyNamedEffect(Instigator.Actor, Each->Actor, Shred, /*DurationSeconds=*/6.0f,
												 /*Magnitude=*/30.0f, FName(TEXT("Demonic")));
	}
	TestEqual(TEXT("set-up: the Shred took the wearer's Demonic from 40 to 10"),
			  Wearer.Get(ConditionedResistanceSlot(TEXT("Demonic"))), 10.0f, 0.01f);
	TestEqual(TEXT("set-up: and the twin's"), Twin.Get(ConditionedResistanceSlot(TEXT("Demonic"))), 10.0f, 0.01f);
	EveryReadIsAsBefore(TEXT("with a Shred on Demonic"));

	const FGameplayTag Aura = ConditionedResistanceTagNamed(TEXT("Status.Debuff.AbyssalAura"));
	for (const FConditionedResistanceFighter* Each : {&Wearer, &Twin})
	{
		UCataclysmSkillEffects::ApplyNamedEffect(Instigator.Actor, Each->Actor, Aura, /*DurationSeconds=*/2.0f,
												 /*Magnitude=*/25.0f);
	}
	TestEqual(TEXT("set-up: the Abyssal Aura took the wearer's War from 30 to 5"),
			  Wearer.Get(ConditionedResistanceSlot(TEXT("War"))), 5.0f, 0.01f);
	TestEqual(TEXT("set-up: and the twin's"), Twin.Get(ConditionedResistanceSlot(TEXT("War"))), 5.0f, 0.01f);
	EveryReadIsAsBefore(TEXT("with an Abyssal Aura"));

	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// 2. A ROW UNDER `health_below` 50 ADDS TO A BLOW'S RESISTANCE BELOW HALF AND NOTHING ABOVE IT
// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConditionedResistanceBelowHalfTest,
	"Cataclysm.ConditionedResistance.ARowUnderHealthBelowHalfAddsToABlowsResistanceBelowHalfAndNothingAboveIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The wearer's War comes from an affix's flat 40 with Brute's Heart's row on it, 50 increased below half health; its
 * attribute is the 40 the refresh would write. The twin holds 40 and no line; a third fighter holds 60 and no line, the
 * figure the row should come to. A Demonic 40 with no line on the wearer is the type the row does not name.
 *
 * At full health and at exactly half the row adds nothing; at 49% it adds 20 to the read, the blow and the sheet, and
 * nothing to the attribute. A Shred of 10 on War then lowers the attribute to 30 and the read to 50: the Shred is kept,
 * which the armour step's shape (`StatForSkill` with the attribute as fallback) would not do. Healed, the read is 30.
 */
bool FCataclysmConditionedResistanceBelowHalfTest::RunTest(const FString&)
{
	using namespace CataclysmConditionedResistanceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float M = ConditionedResistanceMetre;
	FConditionedResistanceFighter Wearer(World, FVector::ZeroVector);
	FConditionedResistanceFighter Twin(World, FVector(3 * M, 0.0f, 0.0f));
	FConditionedResistanceFighter Sixty(World, FVector(0.0f, 3 * M, 0.0f));
	FConditionedResistanceFighter Instigator(World, FVector(-3 * M, 0.0f, 0.0f));

	TMap<FName, FCataclysmStatInputs> Lines;
	ConditionedResistanceLine(Lines, TEXT("War"), 0.0f,
		{ConditionedResistanceModifier(ECataclysmStatBucket::Flat, ECataclysmModifierSource::GearAffix, 40.0f, false),
		 ConditionedResistanceBruteRow()});
	Wearer.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	const FGameplayAttribute War = ConditionedResistanceSlot(TEXT("War"));
	const FGameplayAttribute Demonic = ConditionedResistanceSlot(TEXT("Demonic"));
	for (const FConditionedResistanceFighter* Each : {&Wearer, &Twin})
	{
		Each->Set(War, 40.0f);
		Each->Set(Demonic, 40.0f);
	}
	Sixty.Set(War, 60.0f);
	const UAbilitySystemComponent* ASC = Wearer.AbilitySystem;

	// THE CONTROL, READ FIRST: at full health the row adds nothing.
	TestEqual(TEXT("control: at full health the wearer's War reads its attribute's 40"),
			  ConditionedResistanceRead(ASC, TEXT("War")), 40.0f, ConditionedResistanceAllowance);
	TestEqual(TEXT("control: and a War blow takes from the wearer what it takes from the twin"),
			  ConditionedResistanceBlow(ASC, TEXT("War")), ConditionedResistanceBlow(Twin.AbilitySystem, TEXT("War")),
			  ConditionedResistanceAllowance);
	TestEqual(TEXT("control: and the sheet's War line reads 40%"),
			  ConditionedResistanceSheetLine(ASC, TEXT("War")).Value, FString(TEXT("40%")));

	// AT EXACTLY HALF, NOTHING: below is strictly below, as the armour row reads it.
	Wearer.AtHealthShare(0.5f);
	TestEqual(TEXT("at exactly half health the War read is still 40"),
			  ConditionedResistanceRead(ASC, TEXT("War")), 40.0f, ConditionedResistanceAllowance);

	// BELOW HALF, THE ROW'S 20.
	Wearer.AtHealthShare(0.49f);
	TestEqual(TEXT("at 49% health 50% increased on 40 reads 60"),
			  ConditionedResistanceRead(ASC, TEXT("War")), 60.0f, ConditionedResistanceAllowance);
	TestEqual(TEXT("and the attribute is still 40: nothing was folded into it"),
			  Wearer.Get(War), 40.0f, ConditionedResistanceAllowance);
	TestEqual(TEXT("and a War blow takes from the wearer what it takes from one holding 60"),
			  ConditionedResistanceBlow(ASC, TEXT("War")), ConditionedResistanceBlow(Sixty.AbilitySystem, TEXT("War")),
			  ConditionedResistanceAllowance);
	TestTrue(*FString::Printf(TEXT("and less than it takes from the twin: %.2f against %.2f"),
							  ConditionedResistanceBlow(ASC, TEXT("War")),
							  ConditionedResistanceBlow(Twin.AbilitySystem, TEXT("War"))),
			 ConditionedResistanceBlow(ASC, TEXT("War"))
				 < ConditionedResistanceBlow(Twin.AbilitySystem, TEXT("War")) - 1.0f);
	TestEqual(TEXT("and the sheet's War line reads 60%"),
			  ConditionedResistanceSheetLine(ASC, TEXT("War")).Value, FString(TEXT("60%")));
	TestEqual(TEXT("and Demonic, which the row does not name, still reads 40"),
			  ConditionedResistanceRead(ASC, TEXT("Demonic")), 40.0f, ConditionedResistanceAllowance);
	TestEqual(TEXT("and a Demonic blow takes from the wearer what it takes from the twin"),
			  ConditionedResistanceBlow(ASC, TEXT("Demonic")),
			  ConditionedResistanceBlow(Twin.AbilitySystem, TEXT("Demonic")), ConditionedResistanceAllowance);

	// A SHRED IS KEPT: the attribute is the starting point.
	TestTrue(TEXT("set-up: a War Shred of 10 lands on the wearer"),
			 UCataclysmSkillEffects::ApplyNamedEffect(Instigator.Actor, Wearer.Actor,
				 ConditionedResistanceTagNamed(TEXT("Status.Debuff.Shred")), /*DurationSeconds=*/6.0f,
				 /*Magnitude=*/10.0f, FName(TEXT("War"))));
	TestEqual(TEXT("and takes the War attribute from 40 to 30"), Wearer.Get(War), 30.0f, 0.01f);
	TestEqual(TEXT("and the War read is 50: the Shred kept, with the row's 20 on top"),
			  ConditionedResistanceRead(ASC, TEXT("War")), 50.0f, 0.01f);

	// HEALED, THE ROW ADDS NOTHING AND THE SHRED STAYS.
	Wearer.AtHealthShare(1.0f);
	TestEqual(TEXT("healed to full the War read is the attribute's 30"),
			  ConditionedResistanceRead(ASC, TEXT("War")), 30.0f, 0.01f);
	TestEqual(TEXT("and the sheet's War line reads 30%"),
			  ConditionedResistanceSheetLine(ASC, TEXT("War")).Value, FString(TEXT("30%")));

	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// 3. THE 2026-10-12 HOLD AT NOUGHT, ON THE SUM, BOTH HALVES
// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConditionedResistanceHeldTest,
	"Cataclysm.ConditionedResistance.AHeldResistanceWhoseSumIsAboveNoughtReadsNoughtAndOneBelowReadsAsItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled 2026-10-12: "A resistance above nought is held at nought. A negative resistance is left as it is." Ruled
 * 2026-10-18: the rule works on the sum, both halves.
 *
 * The wearer holds War 30, Demonic -40 and Death -10, each with a row of a flat 25 below half health and no base, so
 * below half the three sums are 55, -15 and 15. The draw is pinned (`Cataclysm.RandomResistancePick`: 0 War, 1 Demonic,
 * 2 Death) and called by hand, as the row's firing would call it.
 *
 * War held: the sum 55 reads nought. Demonic held: the sum -15 reads -15, the attribute and the row together, and at
 * full health -40. Death held: the attribute -10 is under nought and the sum 15 is not, and it reads nought, so it is
 * the sum that is compared; at full health it reads -10.
 */
bool FCataclysmConditionedResistanceHeldTest::RunTest(const FString&)
{
	using namespace CataclysmConditionedResistanceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	IConsoleVariable* Pick = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.RandomResistancePick"));
	if (!TestNotNull(TEXT("set-up: the pin for the draw"), Pick))
	{
		return false;
	}
	const int32 PickWas = Pick->GetInt();
	ON_SCOPE_EXIT { Pick->Set(PickWas, ECVF_SetByConsole); };

	FConditionedResistanceFighter Wearer(World, FVector::ZeroVector);
	TMap<FName, FCataclysmStatInputs> Lines;
	const FCataclysmStatModifier BelowHalfFlat = ConditionedResistanceModifier(
		ECataclysmStatBucket::Flat, ECataclysmModifierSource::Enchantment, 25.0f, /*bBelowHalf=*/true);
	for (const TCHAR* Type : {TEXT("War"), TEXT("Demonic"), TEXT("Death")})
	{
		ConditionedResistanceLine(Lines, Type, 0.0f, {BelowHalfFlat});
	}
	Wearer.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	Wearer.Set(ConditionedResistanceSlot(TEXT("War")), 30.0f);
	Wearer.Set(ConditionedResistanceSlot(TEXT("Demonic")), -40.0f);
	Wearer.Set(ConditionedResistanceSlot(TEXT("Death")), -10.0f);
	UCataclysmAbilitySystemComponent* ASC = Wearer.AbilitySystem;
	const auto Hold = [&](int32 Place, const TCHAR* Type)
	{
		Pick->Set(Place, ECVF_SetByConsole);
		return ASC->HoldARandomResistanceAtNought() && ASC->ResistanceHeldAtNought() == FName(Type);
	};

	// THE CONTROL, READ FIRST: below half, nothing held, each reads its sum.
	Wearer.AtHealthShare(0.4f);
	TestTrue(TEXT("control: nothing is held"), ASC->ResistanceHeldAtNought().IsNone());
	TestEqual(TEXT("control: below half War reads 30 and the row's 25"),
			  ConditionedResistanceRead(ASC, TEXT("War")), 55.0f, ConditionedResistanceAllowance);
	TestEqual(TEXT("control: Demonic reads -40 and the row's 25"),
			  ConditionedResistanceRead(ASC, TEXT("Demonic")), -15.0f, ConditionedResistanceAllowance);
	TestEqual(TEXT("control: Death reads -10 and the row's 25"),
			  ConditionedResistanceRead(ASC, TEXT("Death")), 15.0f, ConditionedResistanceAllowance);

	// A SUM ABOVE NOUGHT, HELD, READS NOUGHT.
	if (!TestTrue(TEXT("set-up: the draw holds War"), Hold(0, TEXT("War"))))
	{
		return false;
	}
	TestEqual(TEXT("War held, its sum of 55 above nought, reads nought"),
			  ConditionedResistanceRead(ASC, TEXT("War")), 0.0f, ConditionedResistanceAllowance);
	TestEqual(TEXT("and its attribute is still 30: the hold writes nothing"),
			  Wearer.Get(ConditionedResistanceSlot(TEXT("War"))), 30.0f, ConditionedResistanceAllowance);
	TestEqual(TEXT("and Demonic, not held, still reads its sum"),
			  ConditionedResistanceRead(ASC, TEXT("Demonic")), -15.0f, ConditionedResistanceAllowance);

	// A SUM BELOW NOUGHT, HELD, READS AS ITSELF.
	if (!TestTrue(TEXT("set-up: the draw holds Demonic"), Hold(1, TEXT("Demonic"))))
	{
		return false;
	}
	TestEqual(TEXT("Demonic held, its sum of -15 below nought, reads -15: the attribute and the row together"),
			  ConditionedResistanceRead(ASC, TEXT("Demonic")), -15.0f, ConditionedResistanceAllowance);
	TestEqual(TEXT("and War, let go, reads its sum again"),
			  ConditionedResistanceRead(ASC, TEXT("War")), 55.0f, ConditionedResistanceAllowance);
	Wearer.AtHealthShare(1.0f);
	TestEqual(TEXT("at full health Demonic held reads its attribute's -40, the row adding nothing"),
			  ConditionedResistanceRead(ASC, TEXT("Demonic")), -40.0f, ConditionedResistanceAllowance);

	// THE SUM AND NOT THE ATTRIBUTE IS COMPARED: an attribute under nought that the row lifts above it.
	Wearer.AtHealthShare(0.4f);
	if (!TestTrue(TEXT("set-up: the draw holds Death"), Hold(2, TEXT("Death"))))
	{
		return false;
	}
	TestEqual(TEXT("Death held, its attribute of -10 lifted to 15 by the row, reads nought"),
			  ConditionedResistanceRead(ASC, TEXT("Death")), 0.0f, ConditionedResistanceAllowance);
	Wearer.AtHealthShare(1.0f);
	TestEqual(TEXT("and at full health, its sum the attribute's -10, it reads -10"),
			  ConditionedResistanceRead(ASC, TEXT("Death")), -10.0f, ConditionedResistanceAllowance);

	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// 4. A MINION'S SHARE FOLLOWS ITS SUMMONER'S CONDITIONED FIGURE
// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConditionedResistanceMinionTest,
	"Cataclysm.ConditionedResistance.AMinionsShareFollowsItsSummonersConditionedResistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three summoners, each handing its imp half its defences (`minion_defences_percent_of_yours` 50) and each with an
 * affix's War: Braced 40 with Brute's Heart's row on it, Steady 40 with none, Sixty 60 with none. Each summoner stands
 * 6 m from the next and its imp 3 m from it.
 *
 * At full health Braced's imp takes what Steady's takes, and more than Sixty's, so the share is read. Below half
 * Braced reads 60 and its imp takes what Sixty's takes, and Steady's imp, its summoner below half too, takes what it
 * took. Healed, Braced's imp takes what it took at first.
 */
bool FCataclysmConditionedResistanceMinionTest::RunTest(const FString&)
{
	using namespace CataclysmConditionedResistanceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float M = ConditionedResistanceMetre;
	FConditionedResistanceFighter Braced(World, FVector::ZeroVector);
	FConditionedResistanceFighter Steady(World, FVector(0.0f, 6 * M, 0.0f));
	FConditionedResistanceFighter Sixty(World, FVector(0.0f, 12 * M, 0.0f));
	const auto Give = [](const FConditionedResistanceFighter& Summoner, float Affix, bool bWithTheRow)
	{
		TArray<FCataclysmStatModifier> Modifiers = {ConditionedResistanceModifier(
			ECataclysmStatBucket::Flat, ECataclysmModifierSource::GearAffix, Affix, /*bBelowHalf=*/false)};
		if (bWithTheRow)
		{
			Modifiers.Add(ConditionedResistanceBruteRow());
		}
		TMap<FName, FCataclysmStatInputs> Lines;
		ConditionedResistanceLine(Lines, TEXT("War"), 0.0f, Modifiers);
		ConditionedResistanceShareLine(Lines, 50.0f);
		Summoner.AbilitySystem->SetStatInputs(MoveTemp(Lines));
		Summoner.Set(ConditionedResistanceSlot(TEXT("War")), Affix);
	};
	Give(Braced, 40.0f, true);
	Give(Steady, 40.0f, false);
	Give(Sixty, 60.0f, false);

	UAbilitySystemComponent* BracedImp = ConditionedResistanceImp(*this, Braced.Actor, FVector(3 * M, 0.0f, 0.0f));
	UAbilitySystemComponent* SteadyImp = ConditionedResistanceImp(*this, Steady.Actor, FVector(3 * M, 6 * M, 0.0f));
	UAbilitySystemComponent* SixtyImp = ConditionedResistanceImp(*this, Sixty.Actor, FVector(3 * M, 12 * M, 0.0f));
	if (!BracedImp || !SteadyImp || !SixtyImp)
	{
		return false;
	}

	// THE CONTROL, READ FIRST.
	const float BracedFull = ConditionedResistanceBlow(BracedImp, TEXT("War"));
	const float SteadyFull = ConditionedResistanceBlow(SteadyImp, TEXT("War"));
	const float SixtyImpTakes = ConditionedResistanceBlow(SixtyImp, TEXT("War"));
	TestEqual(TEXT("control: at full health Braced's imp takes from a War blow what Steady's takes"),
			  BracedFull, SteadyFull, ConditionedResistanceAllowance);
	TestTrue(*FString::Printf(TEXT("control: and more than Sixty's, so the share is read: %.2f against %.2f"),
							  BracedFull, SixtyImpTakes),
			 BracedFull > SixtyImpTakes + 0.5f);

	Braced.AtHealthShare(0.4f);
	Steady.AtHealthShare(0.4f);
	TestEqual(TEXT("below half health Braced's own War reads 60"),
			  ConditionedResistanceRead(Braced.AbilitySystem, TEXT("War")), 60.0f, ConditionedResistanceAllowance);
	TestEqual(TEXT("and its imp takes from a War blow what Sixty's takes: its share follows the read"),
			  ConditionedResistanceBlow(BracedImp, TEXT("War")), SixtyImpTakes, ConditionedResistanceAllowance);
	TestEqual(TEXT("and Steady's imp, its summoner below half too and holding no row, takes what it took"),
			  ConditionedResistanceBlow(SteadyImp, TEXT("War")), SteadyFull, ConditionedResistanceAllowance);

	Braced.AtHealthShare(1.0f);
	TestEqual(TEXT("Braced healed, its imp takes what it took at first"),
			  ConditionedResistanceBlow(BracedImp, TEXT("War")), BracedFull, ConditionedResistanceAllowance);

	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// 5. `Cataclysm.ShowResistances` PRINTS THE NEW FIGURE, AND EVERY OTHER AS BEFORE
// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConditionedResistanceShownTest,
	"Cataclysm.ConditionedResistance.ShowResistancesPrintsTheConditionedFigureAndEveryOtherAsBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A possessed player whose War comes from a plain row's flat 30, Demonic from an affix's flat 40, and Death from an
 * affix's flat 20 with Brute's Heart's row on it; the attributes are 30, 40 and 20, as the refresh would write them.
 * The command is run as a person types it, into an output device that keeps the lines.
 *
 * At full health each prints its attribute. Below half Death prints 30 and the other two their attributes. A Shred of
 * 30 on Demonic and an Abyssal Aura of 10 then lower the attributes of Demonic and War, and each prints its attribute,
 * and Death still prints 30.
 */
bool FCataclysmConditionedResistanceShownTest::RunTest(const FString&)
{
	using namespace CataclysmConditionedResistanceTest;

	IConsoleObject* Object = IConsoleManager::Get().FindConsoleObject(TEXT("Cataclysm.ShowResistances"));
	IConsoleCommand* Command = Object ? Object->AsCommand() : nullptr;
	if (!TestNotNull(TEXT("set-up: Cataclysm.ShowResistances is registered"), Command))
	{
		return false;
	}
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (State && Controller && Character)
	{
		Controller->SetPlayerState(State);
		Controller->Possess(Character);
	}
	UCataclysmAbilitySystemComponent* ASC = Character
		? Cast<UCataclysmAbilitySystemComponent>(Character->GetAbilitySystemComponent())
		: nullptr;
	if (!TestNotNull(TEXT("set-up: a possessed player with an ability system"), ASC))
	{
		return false;
	}
	FConditionedResistanceFighter Instigator(World, FVector(3 * ConditionedResistanceMetre, 0.0f, 0.0f));

	// MAXIMUM BEFORE CURRENT, because the vital set clamps health to the maximum.
	ASC->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), ConditionedResistancePool);
	ASC->SetNumericAttributeBase(Vital::GetHealthAttribute(), ConditionedResistancePool);
	TMap<FName, FCataclysmStatInputs> Lines;
	ConditionedResistanceLine(Lines, TEXT("War"), 0.0f,
		{ConditionedResistanceModifier(ECataclysmStatBucket::Flat, ECataclysmModifierSource::Enchantment, 30.0f, false)});
	ConditionedResistanceLine(Lines, TEXT("Demonic"), 0.0f,
		{ConditionedResistanceModifier(ECataclysmStatBucket::Flat, ECataclysmModifierSource::GearAffix, 40.0f, false)});
	ConditionedResistanceLine(Lines, TEXT("Death"), 0.0f,
		{ConditionedResistanceModifier(ECataclysmStatBucket::Flat, ECataclysmModifierSource::GearAffix, 20.0f, false),
		 ConditionedResistanceBruteRow()});
	ASC->SetStatInputs(MoveTemp(Lines));
	ASC->SetNumericAttributeBase(ConditionedResistanceSlot(TEXT("War")), 30.0f);
	ASC->SetNumericAttributeBase(ConditionedResistanceSlot(TEXT("Demonic")), 40.0f);
	ASC->SetNumericAttributeBase(ConditionedResistanceSlot(TEXT("Death")), 20.0f);

	const auto Printed = [&]()
	{
		FConditionedResistanceOutput Out;
		Command->Execute(TArray<FString>(), World, Out);
		return Out.All();
	};
	const auto Prints = [&](const TCHAR* Stage, const TCHAR* Type, float Figure)
	{
		const FString Text = Printed();
		const FString Wanted = ConditionedResistanceHeldText(Type, Figure);
		TestTrue(*FString::Printf(TEXT("%s: it prints \"%s\""), Stage, *Wanted), Text.Contains(Wanted));
	};
	const auto Attribute = [&](const TCHAR* Type)
	{
		return ASC->GetNumericAttribute(ConditionedResistanceSlot(Type));
	};

	// THE CONTROL, READ FIRST: at full health every figure is its attribute.
	Prints(TEXT("control: at full health"), TEXT("War"), 30.0f);
	Prints(TEXT("control: at full health"), TEXT("Demonic"), 40.0f);
	Prints(TEXT("control: at full health"), TEXT("Death"), 20.0f);

	ASC->SetNumericAttributeBase(Vital::GetHealthAttribute(), ConditionedResistancePool * 0.4f);
	Prints(TEXT("below half health a plain row's War, as before"), TEXT("War"), 30.0f);
	Prints(TEXT("below half health an affix's Demonic, as before"), TEXT("Demonic"), 40.0f);
	Prints(TEXT("below half health Death, with the row's 10 on the affix's 20"), TEXT("Death"), 30.0f);

	TestTrue(TEXT("set-up: a Demonic Shred of 30 lands on the player"),
			 UCataclysmSkillEffects::ApplyNamedEffect(Instigator.Actor, Character,
				 ConditionedResistanceTagNamed(TEXT("Status.Debuff.Shred")), /*DurationSeconds=*/6.0f,
				 /*Magnitude=*/30.0f, FName(TEXT("Demonic"))));
	Prints(TEXT("with a Shred, Demonic prints its attribute, as before"), TEXT("Demonic"), Attribute(TEXT("Demonic")));

	TestTrue(TEXT("set-up: an Abyssal Aura of 10 lands on the player"),
			 UCataclysmSkillEffects::ApplyNamedEffect(Instigator.Actor, Character,
				 ConditionedResistanceTagNamed(TEXT("Status.Debuff.AbyssalAura")), /*DurationSeconds=*/2.0f,
				 /*Magnitude=*/10.0f));
	Prints(TEXT("with an Abyssal Aura, War prints its attribute, as before"), TEXT("War"), Attribute(TEXT("War")));
	Prints(TEXT("and Death, which the aura does not touch, still prints 30"), TEXT("Death"), 30.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
