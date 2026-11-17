// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageConversion.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A wearer immune to debuffs that lays none. Ruled 2026-10-13 under the owner's delegation, reading A, for "You
 * are immune to debuffs but also cannot apply them".
 *
 * WHAT IS BUILT. One flag stat, `immune_to_debuffs_and_applies_none`. The four functions that put a debuff on a
 * character (`ApplyDamageOverTime`, `ApplyShareOfHealthOverTime`, `ApplyTagForDuration`, `ApplyNamedEffectOnly`)
 * ask `UCataclysmDebuffs::RefusedByARow` and apply nothing when the target holds the flag, when the instigator
 * holds it, or when the instigator follows a character that holds it. `UCataclysmDebuffs::ImmunityStep`, a job
 * of the regeneration step, removes what a holder already carries. No row is authored, so the flag is written
 * by hand as the `flat` line of 1 a row would be.
 *
 * "EACH APPLYING FUNCTION" HERE IS FIVE CALLS: the three public ones of the four above; `ApplyNamedEffect`,
 * which is the only way to the private `ApplyNamedEffectOnly`, with Weaken, whose row moves a stat so the call
 * runs that function's own path and not its hand-over to `ApplyTagForDuration`; and `ApplyStun`, which lays its
 * tag through `ApplyTagForDuration`.
 *
 * THE RIG IS THE ONE `CataclysmDebuffTests.cpp` USES: a plain actor holding an ability system, a vital set and a
 * combat set. A plain actor has no root component and no body. Every rig is used for one application, so no
 * immunity window a stun or a knockdown opened is in the way of the next one.
 *
 * EACH TEST READS A CONTROL FIRST: the same call between two characters holding no flag.
 *
 * NOT COMPILED AND NOT RUN BY THE SESSION THAT WROTE IT.
 */
namespace CataclysmDebuffImmuneTest
{
	using ImmuneEffects = UCataclysmSkillEffects;
	using ImmuneDebuffs = UCataclysmDebuffs;

	FGameplayTag ImmuneTagNamed(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/** A plain actor with an ability system, 1,000 health and attacks worth 100. */
	struct FImmuneRig
	{
		explicit FImmuneRig(UWorld* World, bool bHoldsTheFlag = false)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper
			// rather than the set.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

			if (bHoldsTheFlag)
			{
				WriteTheFlag(true);
			}
		}

		~FImmuneRig()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/**
		 * Write this ability system's whole stat line: the flag at a flat 1 and nothing else, or nothing at all.
		 * WHOLESALE, because `SetStatInputs` is.
		 */
		void WriteTheFlag(bool bHeld) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			if (bHeld)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = 1.0f;

				FCataclysmStatInputs Line;
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row);
				Stats.Add(FName(ImmuneDebuffs::ImmuneAndAppliesNoneStat), Line);
			}
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		bool Carries(const FGameplayTag& Tag) const { return AbilitySystem->HasMatchingGameplayTag(Tag); }
		int32 Debuffs() const { return ImmuneDebuffs::CountOn(AbilitySystem); }
		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** One way a debuff is put on a character, and the tag it leaves. */
	struct FImmuneWay
	{
		const TCHAR* Name;
		FGameplayTag Leaves;
		TFunction<bool(AActor* By, AActor* On)> Lay;
	};

	/** The five calls the comment at the top of this file names. */
	TArray<FImmuneWay> EveryWay()
	{
		const FGameplayTag Bleed = ImmuneDebuffs::BleedTag();
		const FGameplayTag Splinter = ImmuneDebuffs::VoidSplinterTag();
		const FGameplayTag Cripple = ImmuneDebuffs::CrippleTag();
		const FGameplayTag Weaken = ImmuneDebuffs::WeakenTag();
		const FGameplayTag Stunned = ImmuneEffects::StunnedTag();
		return {
			{TEXT("ApplyDamageOverTime, a bleed"), Bleed,
			 [Bleed](AActor* By, AActor* On)
			 {
				 return ImmuneEffects::ApplyDamageOverTime(By, On, 10.0f, 20.0f, Bleed,
														   /*bScalesWithInstigator=*/false);
			 }},
			{TEXT("ApplyShareOfHealthOverTime, a Void Splinter"), Splinter,
			 [Splinter](AActor* By, AActor* On)
			 {
				 return ImmuneEffects::ApplyShareOfHealthOverTime(By, On, 0.01f, 20.0f, Splinter,
																  /*bScalesWithInstigator=*/false);
			 }},
			{TEXT("ApplyTagForDuration, Cripple"), Cripple,
			 [Cripple](AActor* By, AActor* On)
			 {
				 return ImmuneEffects::ApplyTagForDuration(By, On, Cripple, 20.0f);
			 }},
			{TEXT("ApplyNamedEffect, Weaken at 20"), Weaken,
			 [Weaken](AActor* By, AActor* On)
			 {
				 return ImmuneEffects::ApplyNamedEffect(By, On, Weaken, 20.0f, 20.0f);
			 }},
			{TEXT("ApplyStun, designed"), Stunned,
			 [](AActor* By, AActor* On)
			 {
				 return ImmuneEffects::ApplyStun(By, On, 2.0f, 0.0f, /*bStunIsDesigned=*/true);
			 }},
		};
	}

	using CataclysmTestWorld::RunClock;

	/**
	 * Everything written to the log while this is alive. The shape of `FScopedWarningCapture` in
	 * `CataclysmDamageOverTimeTests.cpp`, written again here because that one is in that file's own namespace. A
	 * warning does not fail an automation test, so reading the log is the only way to see whether one was written.
	 */
	struct FImmuneLogCapture : public FOutputDevice
	{
		struct FCapturedLine
		{
			FString Text;
			ELogVerbosity::Type Verbosity = ELogVerbosity::NoLogging;
			FName Category;
		};

		FImmuneLogCapture()
		{
			if (GLog)
			{
				GLog->AddOutputDevice(this);
			}
		}

		virtual ~FImmuneLogCapture()
		{
			if (GLog)
			{
				GLog->RemoveOutputDevice(this);
			}
		}

		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity,
							   const class FName& Category) override
		{
			Lines.Add({ FString(Text), Verbosity, Category });
		}

		/** How many LogCataclysm lines at exactly Warning verbosity hold Needle, case-sensitively. */
		int32 CataclysmWarningsContaining(const TCHAR* Needle)
		{
			if (GLog)
			{
				GLog->Flush();
			}

			int32 Found = 0;
			for (const FCapturedLine& Line : Lines)
			{
				if (Line.Category == FName(TEXT("LogCataclysm")) && Line.Verbosity == ELogVerbosity::Warning
					&& Line.Text.Contains(Needle, ESearchCase::CaseSensitive))
				{
					++Found;
				}
			}
			return Found;
		}

		TArray<FCapturedLine> Lines;
	};

	/** Words of the warning `UCataclysmDamageConversion::ConvertIfActive` writes when it could not lay its bleed. */
	const TCHAR* ConversionWarningNeedle = TEXT("into Bleeding and could not");

	/** What a direct blow of `Damage` takes from this rig's health at once: not evaded, not critical. */
	float TakenAtOnceBy(const FImmuneRig& Struck, AActor* Striker, float Damage)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotBeEvaded = true;
		Delivery.bCannotCriticallyStrike = true;
		Delivery.bCannotBeRetaliatedAgainst = true;
		const float Before = Struck.Health();
		UCataclysmSkillEffects::ApplyDirectDamage(Striker, Struck.Actor, Damage, Delivery);
		return Before - Struck.Health();
	}
}

#define CATACLYSM_DEBUFF_IMMUNE_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_DEBUFF_IMMUNE_TEST(FCataclysmDebuffImmuneAsTargetTest,
	"Cataclysm.DebuffImmune.EachApplyingFunctionIsRefusedForAHolderAsItsTarget")
{
	using namespace CataclysmDebuffImmuneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	for (const FImmuneWay& Way : EveryWay())
	{
		if (!TestTrue(*FString::Printf(TEXT("set-up, %s: its tag is in the vocabulary"), Way.Name),
					  Way.Leaves.IsValid()))
		{
			return false;
		}
		const FImmuneRig Applier(World);
		const FImmuneRig Plain(World);
		const FImmuneRig Holder(World, /*bHoldsTheFlag=*/true);

		TestTrue(*FString::Printf(TEXT("control, %s: it lands on a character holding no flag"), Way.Name),
				 Way.Lay(Applier.Actor, Plain.Actor));
		TestTrue(*FString::Printf(TEXT("control, %s: and that character carries its tag"), Way.Name),
				 Plain.Carries(Way.Leaves));

		TestFalse(*FString::Printf(TEXT("%s: it is refused for a holder as its target"), Way.Name),
				  Way.Lay(Applier.Actor, Holder.Actor));
		TestFalse(*FString::Printf(TEXT("%s: and the holder does not carry its tag"), Way.Name),
				  Holder.Carries(Way.Leaves));
		TestEqual(*FString::Printf(TEXT("%s: and the holder carries no debuff at all"), Way.Name),
				  Holder.Debuffs(), 0);
	}
	return true;
}

CATACLYSM_DEBUFF_IMMUNE_TEST(FCataclysmDebuffImmuneAsInstigatorTest,
	"Cataclysm.DebuffImmune.EachApplyingFunctionIsRefusedForAHolderAsItsInstigator")
{
	using namespace CataclysmDebuffImmuneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	for (const FImmuneWay& Way : EveryWay())
	{
		if (!TestTrue(*FString::Printf(TEXT("set-up, %s: its tag is in the vocabulary"), Way.Name),
					  Way.Leaves.IsValid()))
		{
			return false;
		}
		const FImmuneRig Plain(World);
		const FImmuneRig PlainsTarget(World);
		const FImmuneRig Holder(World, /*bHoldsTheFlag=*/true);
		const FImmuneRig HoldersTarget(World);

		TestTrue(*FString::Printf(TEXT("control, %s: a character holding no flag lays it"), Way.Name),
				 Way.Lay(Plain.Actor, PlainsTarget.Actor));
		TestTrue(*FString::Printf(TEXT("control, %s: and its target carries the tag"), Way.Name),
				 PlainsTarget.Carries(Way.Leaves));

		TestFalse(*FString::Printf(TEXT("%s: it is refused for a holder as its instigator"), Way.Name),
				  Way.Lay(Holder.Actor, HoldersTarget.Actor));
		TestFalse(*FString::Printf(TEXT("%s: and the holder's target does not carry its tag"), Way.Name),
				  HoldersTarget.Carries(Way.Leaves));
		TestEqual(*FString::Printf(TEXT("%s: and the holder's target carries no debuff at all"), Way.Name),
				  HoldersTarget.Debuffs(), 0);

		TestFalse(*FString::Printf(TEXT("%s: and a holder lays none on itself"), Way.Name),
				  Way.Lay(Holder.Actor, Holder.Actor));
		TestEqual(*FString::Printf(TEXT("%s: so the holder carries no debuff"), Way.Name), Holder.Debuffs(), 0);
	}
	return true;
}

CATACLYSM_DEBUFF_IMMUNE_TEST(FCataclysmDebuffImmuneDamageLostTest,
	"Cataclysm.DebuffImmune.ARefusedDamageOverTimeTakesNothing")
{
	using namespace CataclysmDebuffImmuneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FImmuneRig Plain(World);
	const FImmuneRig PlainsTarget(World);
	const FImmuneRig Holder(World, /*bHoldsTheFlag=*/true);
	const FImmuneRig HoldersTarget(World);
	const FGameplayTag Bleed = ImmuneDebuffs::BleedTag();
	if (!TestTrue(TEXT("set-up: the bleed tag"), Bleed.IsValid()))
	{
		return false;
	}

	// EVERY ONE THAT COULD BLEED IS MOVING, because a bleed ticks only while its target moves (issue #918, ruled
	// 2026-10-31). The control's target, so its bleed's ticks land; and the two the refused bleeds were aimed at, so
	// that the refusal, and not a still target, is the only reason they lose nothing. Plain actors, which no movement
	// sample reads, so one note each holds; the three ticks in 3.5 s are inside the four seconds.
	PlainsTarget.AbilitySystem->NoteMovedMetres(1.0f);
	HoldersTarget.AbilitySystem->NoteMovedMetres(1.0f);
	Holder.AbilitySystem->NoteMovedMetres(1.0f);

	const float PlainsTargetBefore = PlainsTarget.Health();
	const float HoldersTargetBefore = HoldersTarget.Health();
	const float HolderBefore = Holder.Health();

	// A BLEED OF 10 A TICK FOR 20 SECONDS, three ways, and the clock moved 3.5 seconds by hand: three ticks.
	const auto Lay = [&Bleed](AActor* By, AActor* On)
	{
		return ImmuneEffects::ApplyDamageOverTime(By, On, 10.0f, 20.0f, Bleed, /*bScalesWithInstigator=*/false);
	};
	TestTrue(TEXT("set-up: the bleed of a character holding no flag lands"), Lay(Plain.Actor, PlainsTarget.Actor));
	TestFalse(TEXT("the holder's bleed is refused"), Lay(Holder.Actor, HoldersTarget.Actor));
	TestFalse(TEXT("and a bleed on the holder is refused"), Lay(Plain.Actor, Holder.Actor));
	RunClock(World, 3.5f);

	const float ControlLost = PlainsTargetBefore - PlainsTarget.Health();
	TestTrue(*FString::Printf(TEXT("control: 3.5 seconds of a bleed took health from the target of a character "
								   "holding no flag: %.2f"), ControlLost),
			 ControlLost > 0.0f);
	TestEqual(TEXT("the holder's target lost nothing: the damage of the refused bleed is lost, not dealt at once "
				   "and not dealt later"),
			  HoldersTargetBefore - HoldersTarget.Health(), 0.0f, 0.001f);
	TestEqual(TEXT("and the holder lost nothing"), HolderBefore - Holder.Health(), 0.0f, 0.001f);
	return true;
}

CATACLYSM_DEBUFF_IMMUNE_TEST(FCataclysmDebuffImmuneNotDebuffsTest,
	"Cataclysm.DebuffImmune.AStaggerAPinAndAKnockdownStillLandBothWays")
{
	using namespace CataclysmDebuffImmuneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	struct FKind
	{
		const TCHAR* Name;
		FGameplayTag Leaves;
		TFunction<bool(AActor* By, AActor* On)> Lay;
	};
	const TArray<FKind> Kinds = {
		{TEXT("a stagger"), ImmuneEffects::StaggeredTag(),
		 [](AActor* By, AActor* On) { return ImmuneEffects::ApplyStagger(By, On, 2.0f); }},
		{TEXT("a pin"), ImmuneEffects::PinnedTag(),
		 [](AActor* By, AActor* On) { return ImmuneEffects::ApplyPin(By, On, 2.0f); }},
		{TEXT("a knockdown"), ImmuneEffects::KnockedDownTag(),
		 [](AActor* By, AActor* On)
		 {
			 return ImmuneEffects::ApplyKnockdown(By, On, 2.0f, 0.0f, /*bKnockdownIsDesigned=*/true);
		 }},
	};

	for (const FKind& Kind : Kinds)
	{
		if (!TestTrue(*FString::Printf(TEXT("set-up, %s: its tag is in the vocabulary"), Kind.Name),
					  Kind.Leaves.IsValid()))
		{
			return false;
		}
		TestFalse(*FString::Printf(TEXT("%s is not a debuff"), Kind.Name), ImmuneDebuffs::IsADebuff(Kind.Leaves));

		const FImmuneRig Plain(World);
		const FImmuneRig PlainsTarget(World);
		const FImmuneRig Holder(World, /*bHoldsTheFlag=*/true);
		const FImmuneRig HoldersTarget(World);
		const FImmuneRig Striker(World);
		const FImmuneRig HeldTarget(World, /*bHoldsTheFlag=*/true);

		TestTrue(*FString::Printf(TEXT("control, %s: it lands between two characters holding no flag"), Kind.Name),
				 Kind.Lay(Plain.Actor, PlainsTarget.Actor) && PlainsTarget.Carries(Kind.Leaves));
		TestTrue(*FString::Printf(TEXT("%s from a holder lands"), Kind.Name),
				 Kind.Lay(Holder.Actor, HoldersTarget.Actor));
		TestTrue(*FString::Printf(TEXT("%s from a holder is carried by its target"), Kind.Name),
				 HoldersTarget.Carries(Kind.Leaves));
		TestTrue(*FString::Printf(TEXT("%s lands on a holder"), Kind.Name),
				 Kind.Lay(Striker.Actor, HeldTarget.Actor));
		TestTrue(*FString::Printf(TEXT("%s is carried by the holder"), Kind.Name), HeldTarget.Carries(Kind.Leaves));
	}
	return true;
}

CATACLYSM_DEBUFF_IMMUNE_TEST(FCataclysmDebuffImmuneStepTest,
	"Cataclysm.DebuffImmune.WhatAHolderAlreadyCarriesIsRemovedAtItsNextStepAndConvertedDamageIsKept")
{
	using namespace CataclysmDebuffImmuneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FImmuneRig Applier(World);
	const FImmuneRig Wearer(World);
	const FGameplayTag Bleed = ImmuneDebuffs::BleedTag();
	const FGameplayTag Burn = ImmuneEffects::BurnTag();
	const FGameplayTag Cripple = ImmuneDebuffs::CrippleTag();
	const FGameplayTag Staggered = ImmuneEffects::StaggeredTag();
	if (!TestTrue(TEXT("set-up: the four tags"),
				  Bleed.IsValid() && Burn.IsValid() && Cripple.IsValid() && Staggered.IsValid()))
	{
		return false;
	}

	// BEFORE THE ROW IS PUT ON: a burn and a Cripple from another character, a stagger, and a bleed that is
	// converted damage, laid by the character on itself as `UCataclysmDamageConversion` lays one.
	const bool bLaid =
		ImmuneEffects::ApplyDamageOverTime(Applier.Actor, Wearer.Actor, 10.0f, 20.0f, Burn,
										   /*bScalesWithInstigator=*/false)
		&& ImmuneEffects::ApplyTagForDuration(Applier.Actor, Wearer.Actor, Cripple, 20.0f)
		&& ImmuneEffects::ApplyTagForDuration(Applier.Actor, Wearer.Actor, Staggered, 20.0f)
		&& ImmuneEffects::ApplyDamageOverTime(Wearer.Actor, Wearer.Actor, 10.0f, 20.0f, Bleed,
											  /*bScalesWithInstigator=*/false, /*DealtBy=*/nullptr,
											  /*Skill=*/nullptr, NAME_None, /*bIsConvertedDamage=*/true);
	if (!TestTrue(TEXT("set-up: all four land on a character holding no flag"), bLaid)
		|| !TestEqual(TEXT("set-up: it carries three debuffs"), Wearer.Debuffs(), 3))
	{
		return false;
	}

	TestEqual(TEXT("control: a step for a character holding no flag removes nothing"),
			  ImmuneDebuffs::ImmunityStep(Wearer.Actor), 0);
	TestEqual(TEXT("control: and it still carries the three"), Wearer.Debuffs(), 3);

	Wearer.WriteTheFlag(true);
	TestEqual(TEXT("with the flag held, the next step removes the burn and the Cripple"),
			  ImmuneDebuffs::ImmunityStep(Wearer.Actor), 2);
	TestFalse(TEXT("the burn is gone"), Wearer.Carries(Burn));
	TestFalse(TEXT("the Cripple is gone"), Wearer.Carries(Cripple));
	TestTrue(TEXT("the converted bleed, damage taken and not yet paid, is kept, as a cleanse keeps it"),
			 Wearer.Carries(Bleed));
	TestTrue(TEXT("and the stagger, which is not a debuff, is left"), Wearer.Carries(Staggered));
	TestEqual(TEXT("a second step finds nothing more to remove"), ImmuneDebuffs::ImmunityStep(Wearer.Actor), 0);

	// AND A NEW CONVERTED BLEED IS REFUSED WHILE THE FLAG IS HELD, on another holder that carries none.
	const FImmuneRig Holder(World, /*bHoldsTheFlag=*/true);
	TestFalse(TEXT("a holder's own converted bleed is refused, so the damage it would have stored is not stored"),
			  ImmuneEffects::ApplyDamageOverTime(Holder.Actor, Holder.Actor, 10.0f, 20.0f, Bleed,
												 /*bScalesWithInstigator=*/false, /*DealtBy=*/nullptr,
												 /*Skill=*/nullptr, NAME_None, /*bIsConvertedDamage=*/true));
	TestFalse(TEXT("and the holder is not bleeding"), Holder.Carries(Bleed));

	// WITH THE ROW OFF AGAIN the character takes debuffs as it did.
	Wearer.WriteTheFlag(false);
	TestTrue(TEXT("with the flag gone, a Cripple lands on it again"),
			 ImmuneEffects::ApplyTagForDuration(Applier.Actor, Wearer.Actor, Cripple, 20.0f));
	return true;
}

CATACLYSM_DEBUFF_IMMUNE_TEST(FCataclysmDebuffImmuneMinionTest,
	"Cataclysm.DebuffImmune.AMinionOfAHolderLaysNoneWithOrWithoutConduitAndIsNotItselfImmune")
{
	using namespace CataclysmDebuffImmuneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FImmuneRig Plain(World);
	const FImmuneRig Holder(World, /*bHoldsTheFlag=*/true);
	const FImmuneRig PlainsEnemy(World);
	const FImmuneRig HoldersEnemy(World);
	const FGameplayTag Bleed = ImmuneDebuffs::BleedTag();
	const FGameplayTag Cripple = ImmuneDebuffs::CrippleTag();

	// EACH MINION STANDS WHERE NOTHING ELSE IN THIS TEST HAS A BODY, 50 m from the origin and 50 m apart.
	ACataclysmMinion* PlainsImp = ACataclysmMinion::Spawn(Plain.Actor, FVector(5000.0f, 0.0f, 0.0f),
														  /*Lifetime=*/60.0f, /*bBurns=*/false,
														  /*TypeName=*/TEXT("Imp"));
	ACataclysmMinion* HoldersImp = ACataclysmMinion::Spawn(Holder.Actor, FVector(0.0f, 5000.0f, 0.0f),
														   /*Lifetime=*/60.0f, /*bBurns=*/false,
														   /*TypeName=*/TEXT("Imp"));
	if (!TestTrue(TEXT("set-up: two minions with ability systems, and the two tags"),
				  PlainsImp && HoldersImp && UCataclysmTargeting::AbilitySystemOf(PlainsImp)
					  && UCataclysmTargeting::AbilitySystemOf(HoldersImp) && Bleed.IsValid() && Cripple.IsValid()))
	{
		return false;
	}
	const auto Lay = [&Bleed](AActor* By, AActor* On)
	{
		return ImmuneEffects::ApplyDamageOverTime(By, On, 10.0f, 20.0f, Bleed, /*bScalesWithInstigator=*/false);
	};

	TestTrue(TEXT("control: the minion of a character holding no flag lays its bleed"),
			 Lay(PlainsImp, PlainsEnemy.Actor) && PlainsEnemy.Carries(Bleed));
	TestFalse(TEXT("a holder's minion lays none, though the holder does not hold Conduit"),
			  Lay(HoldersImp, HoldersEnemy.Actor));
	TestFalse(TEXT("and the enemy is not bleeding"), HoldersEnemy.Carries(Bleed));

	TestTrue(TEXT("a holder's minion is not itself immune: a Cripple from an enemy lands on it"),
			 ImmuneEffects::ApplyTagForDuration(HoldersEnemy.Actor, HoldersImp, Cripple, 20.0f));
	return true;
}

CATACLYSM_DEBUFF_IMMUNE_TEST(FCataclysmDebuffImmuneConversionTest,
	"Cataclysm.DebuffImmune.AHoldersConversionToBleedingTakesTheBlowAtOnceLeavesItNotBleedingAndWritesNoWarning")
{
	using namespace CataclysmDebuffImmuneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FImmuneRig Striker(World);
	const FImmuneRig NoWindow(World);
	const FImmuneRig Converting(World);
	const FImmuneRig Holder(World, /*bHoldsTheFlag=*/true);
	const FGameplayTag Bleed = ImmuneDebuffs::BleedTag();
	if (!TestTrue(TEXT("set-up: the bleed tag"), Bleed.IsValid()))
	{
		return false;
	}

	// THE WINDOW OF THE BREAKING POINT, OPENED BY HAND on two of the three, as `CataclysmDeathTests.cpp` opens it:
	// four seconds, and a cooldown that does not matter here.
	Converting.AbilitySystem->NoteDamageConversionStarted(/*WindowSeconds=*/4.0f, /*CooldownSeconds=*/10.0f);
	Holder.AbilitySystem->NoteDamageConversionStarted(/*WindowSeconds=*/4.0f, /*CooldownSeconds=*/10.0f);
	if (!TestTrue(TEXT("set-up: the window is open on both and shut on the third"),
				  Converting.AbilitySystem->IsConvertingDamageToBleeding()
					  && Holder.AbilitySystem->IsConvertingDamageToBleeding()
					  && !NoWindow.AbilitySystem->IsConvertingDamageToBleeding()))
	{
		return false;
	}

	// A BLOW OF 200, WELL UNDER HALF OF 1,000 HEALTH, so no rig crosses the line that opens a window.
	const float Worth = TakenAtOnceBy(NoWindow, Striker.Actor, 200.0f);
	if (!TestTrue(*FString::Printf(TEXT("control: with no window open the blow takes health at once: %.2f"), Worth),
				  Worth > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: with the window open and no flag the blow takes nothing at once"),
			  TakenAtOnceBy(Converting, Striker.Actor, 200.0f), 0.0f, 0.01f);
	TestTrue(TEXT("control: and that character is bleeding, so the conversion works on this rig"),
			 Converting.Carries(Bleed));

	FImmuneLogCapture Capture;
	UE_LOG(LogCataclysm, Warning, TEXT("A line this test writes: %s, said by the test."), ConversionWarningNeedle);
	TestEqual(TEXT("control: the capture counts a warning holding those words when one is written"),
			  Capture.CataclysmWarningsContaining(ConversionWarningNeedle), 1);

	TestEqual(TEXT("a holder with the window open takes the whole blow at once, as one with no window does"),
			  TakenAtOnceBy(Holder, Striker.Actor, 200.0f), Worth, 0.01f);
	TestFalse(TEXT("and the holder is not bleeding"), Holder.Carries(Bleed));
	TestEqual(TEXT("and carries no debuff"), Holder.Debuffs(), 0);
	TestEqual(TEXT("and the refusal wrote no warning: the one line counted is still the test's own"),
			  Capture.CataclysmWarningsContaining(ConversionWarningNeedle), 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
