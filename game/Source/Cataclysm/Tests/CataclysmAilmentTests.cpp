// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A chance to apply an ailment on a hit. Issue #899.
 *
 * WHAT IS UNDER TEST. Eleven gear affixes read "Chance to bleed", "Chance to
 * stun" and so on. `UCataclysmAilments` makes each chance a stat, `ApplyHit`
 * sends the attacker's chances with the blow, and the defender rolls them once
 * the blow has landed. Two neighbours live elsewhere: a blunt weapon's stun,
 * which shares one pool with the chance to stun, is tested beside the other
 * weapon sub-types in `CataclysmDamageTypeTests.cpp`, and the affix granting a
 * chance is tested in `CataclysmItemTests.cpp`.
 *
 * THE ROLL IS PINNED IN EVERY TEST, through `Cataclysm.AilmentRoll`, so each
 * test decides the outcome rather than drawing it.
 *
 * EVERY BLOW HERE IS A THOUSAND AGAINST A BARE DEFENDER, with no armour, no
 * evasion and no block, and from an attacker with no critical strike chance,
 * which is what a bare combat attribute set starts at. So the damage dealt to
 * health is the thousand sent, less any resistance a test gives the defender.
 */
namespace CataclysmAilmentTest
{
	using Combat = UCataclysmCombatAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	/** Requested by name, so a tag the vocabulary lost reads as invalid. */
	FGameplayTag TagNamed(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Name), /*ErrorIfNotFound=*/false);
	}

	/** The ailment the affix sheet calls this, or null. */
	const FCataclysmAilmentKind* KindOf(const TCHAR* Ailment)
	{
		return UCataclysmAilments::KindNamed(Ailment);
	}

	/** A bare actor holding every attribute set, usable as either side. */
	struct FScopedFighter
	{
		explicit FScopedFighter(UWorld* World, float MaxHealth = 5'000.0f)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAll =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAll);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			SetHealth(MaxHealth, MaxHealth);
		}

		~FScopedFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** The maximum first, because health is clamped to it. */
		void SetHealth(float Health, float MaxHealth) const
		{
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetMaxHealthAttribute(), MaxHealth);
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetHealthAttribute(), Health);
		}

		/** What `ApplyHit` deals at a hundred percent. */
		void ArmFor(float AttackDamage) const
		{
			AbilitySystem->SetNumericAttributeBase(
				Combat::GetAttackDamageAttribute(), AttackDamage);
		}

		/** What a worn affix would have written onto this character. */
		void SetChance(const FCataclysmAilmentKind& Kind, float Percent) const
		{
			AbilitySystem->SetNumericAttributeBase(Kind.Attribute(), Percent);
		}

		void SetAllResistance(float Percent) const
		{
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(),
				Percent);
		}

		float AllResistance() const
		{
			return AbilitySystem->GetNumericAttribute(
				UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute());
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		bool Carries(const TCHAR* TagName) const
		{
			const FGameplayTag Tag = TagNamed(TagName);
			return Tag.IsValid() && AbilitySystem->HasMatchingGameplayTag(Tag);
		}

		/** The longest time left on anything granting the tag, or zero. */
		float SecondsLeftOn(const TCHAR* TagName) const
		{
			const FGameplayTag Tag = TagNamed(TagName);
			float Longest = 0.0f;
			if (Tag.IsValid())
			{
				for (const float Seconds : AbilitySystem->GetActiveEffectsTimeRemaining(
						 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
							 FGameplayTagContainer(Tag))))
				{
					Longest = FMath::Max(Longest, Seconds);
				}
			}
			return Longest;
		}

		/**
		 * What the running application granting the tag stated, or -1.
		 *
		 * FOR DAMAGE OVER TIME IT IS THE DAMAGE A SECOND, after the attacker's
		 * three damage over time stats, which `ApplyDamageOverTime` puts on the
		 * effect for the next application to be compared with. Issue #1503.
		 */
		float StatedOn(const TCHAR* TagName) const
		{
			return CarriedOn(TagName, UCataclysmSkillEffects::StatedMagnitudeDataName);
		}

		/**
		 * The share of current health a tick of the running application takes,
		 * as a fraction, or -1. Only Void Splinter carries one. Issue #915.
		 */
		float ShareOn(const TCHAR* TagName) const
		{
			return CarriedOn(TagName,
				UCataclysmSkillEffects::ShareOfCurrentHealthDataName);
		}

		/**
		 * The largest number any application granting the tag carries under
		 * this name, or -1.
		 */
		float CarriedOn(const TCHAR* TagName, const TCHAR* DataName) const
		{
			const FGameplayTag Tag = TagNamed(TagName);
			float Carried = -1.0f;
			if (!Tag.IsValid())
			{
				return Carried;
			}

			for (const FActiveGameplayEffectHandle& Handle :
					AbilitySystem->GetActiveEffects(
						FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
							FGameplayTagContainer(Tag))))
			{
				if (const FActiveGameplayEffect* Active =
						AbilitySystem->GetActiveGameplayEffect(Handle))
				{
					Carried = FMath::Max(Carried, Active->Spec.GetSetByCallerMagnitude(
						FName(DataName), /*WarnIfNotFound=*/false, -1.0f));
				}
			}
			return Carried;
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};
}

#define CATACLYSM_AILMENT_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The arithmetic and the joins
// ---------------------------------------------------------------------------

CATACLYSM_AILMENT_TEST(FCataclysmAilmentApplicationTest,
	"Cataclysm.Ailments.ChanceAboveCertaintyBecomesMagnitude")
{
	// THE DESIGN DOCUMENT'S OWN TABLE, under "Chance to apply caps at 100%.
	// Everything above it becomes magnitude instead." Mirrors
	// `ailment_application` in sim/cataclysm_sim/affixes.py, which
	// tools/tests/test_ailment_magnitude_rule.py checks against the same table.
	struct FRow
	{
		float Total;
		float Chance;
		float Magnitude;
	};
	const FRow Rows[] = {
		{  60.0f,  60.0f, 1.0f },
		{ 100.0f, 100.0f, 1.0f },
		{ 250.0f, 100.0f, 2.5f },
		{ 800.0f, 100.0f, 8.0f },

		// AND TWO EDGES THE TABLE DOES NOT STATE. No chance is no chance, and a
		// negative total, which the model refuses with an error, is none either.
		{   0.0f,   0.0f, 1.0f },
		{ -10.0f,   0.0f, 1.0f },
	};

	for (const FRow& Row : Rows)
	{
		float Chance = -1.0f;
		float Magnitude = -1.0f;
		UCataclysmAilments::Application(Row.Total, Chance, Magnitude);

		TestEqual(FString::Printf(TEXT("%.0f%% applies on %.0f%% of hits"),
								  Row.Total, Row.Chance),
			Chance, Row.Chance, 0.001f);
		TestEqual(FString::Printf(TEXT("%.0f%% applies at %.1f times its magnitude"),
								  Row.Total, Row.Magnitude),
			Magnitude, Row.Magnitude, 0.001f);
	}

	TestEqual(TEXT("the cap is the model's 100"), UCataclysmAilments::ChanceCap, 100.0f);
	return true;
}

CATACLYSM_AILMENT_TEST(FCataclysmAilmentJoinsTest,
	"Cataclysm.Ailments.EveryAilmentIsJoinedUpAcrossTheGame")
{
	using namespace CataclysmAilmentTest;

	const TArrayView<const FCataclysmAilmentKind> Every = UCataclysmAilments::Kinds();

	// ELEVEN, THE AFFIX SHEET'S COUNT, and `AILMENT_AFFIXES` in
	// sim/cataclysm_sim/affixes.py lists the same eleven.
	TestEqual(TEXT("eleven ailments"), Every.Num(), 11);

	const TMap<FString, FGameplayAttribute>& Map =
		UCataclysmPlayerClassStats::StatToAttribute();

	TSet<FString> Stats;
	TSet<FString> DataNames;
	for (const FCataclysmAilmentKind& Each : Every)
	{
		const FString Name = Each.Ailment;
		Stats.Add(Each.Stat);
		DataNames.Add(Each.DataName);

		// THE STAT REACHES AN ATTRIBUTE, AND ITS OWN. `ApplyTo` loops over that
		// map, so a stat missing from it is dropped before it reaches a
		// character, and one paired with another ailment's attribute would roll
		// the wrong chance.
		const FGameplayAttribute* Mapped = Map.Find(Each.Stat);
		TestTrue(FString::Printf(TEXT("%s's stat %s reaches an attribute"),
								 *Name, Each.Stat),
			Mapped != nullptr);
		if (Mapped)
		{
			TestTrue(FString::Printf(TEXT("and it is %s's own attribute"), *Name),
				*Mapped == Each.Attribute());
		}

		// THE AFFIX SHEET'S NAME FINDS IT, WHATEVER ITS CASE.
		TestTrue(FString::Printf(TEXT("'%s' finds its own ailment"), *Name),
			UCataclysmAilments::KindNamed(Name) == &Each);
		TestTrue(FString::Printf(TEXT("and so does '%s'"), *Name.ToLower()),
			UCataclysmAilments::KindNamed(Name.ToLower()) == &Each);

		// THE TAG IT GRANTS IS IN THE VOCABULARY, which is generated from the
		// workbook and could lose one without any C++ changing.
		TestTrue(FString::Printf(TEXT("%s's tag %s is a gameplay tag"),
								 *Name, Each.TagName),
			TagNamed(Each.TagName).IsValid());

		// AND ITS ROW STATES A DURATION, as every ailment's does.
		TestTrue(FString::Printf(TEXT("%s's row %s states a duration"),
								 *Name, Each.StatusRow),
			UCataclysmSkillEffects::StatusEffectNumbers(Each.StatusRow, *Name)
				.DurationSeconds > 0.0f);
	}

	TestEqual(TEXT("no two ailments share a stat"), Stats.Num(), Every.Num());
	TestEqual(TEXT("and no two share the name their chance travels under"),
		DataNames.Num(), Every.Num());
	TestNull(TEXT("a name no ailment has finds nothing"),
		UCataclysmAilments::KindNamed(TEXT("Sarcasm")));
	return true;
}

// ---------------------------------------------------------------------------
// What a chance does when a blow lands
// ---------------------------------------------------------------------------

CATACLYSM_AILMENT_TEST(FCataclysmAilmentRollTest,
	"Cataclysm.Ailments.AChanceToBleedIsComparedWithTheRoll")
{
	using namespace CataclysmAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FCataclysmAilmentKind* Bleed = KindOf(TEXT("Bleed"));
	if (!TestNotNull(TEXT("Bleed is an ailment"), Bleed))
	{
		return false;
	}

	// 15, THE CHANCE TO BLEED AFFIX AT THE TOP TIER ON A FULLY UPGRADED PIECE.
	const FScopedFighter Attacker(World);
	Attacker.ArmFor(1'000.0f);
	Attacker.SetChance(*Bleed, 15.0f);

	{
		// A ROLL JUST UNDER THE CHANCE LANDS IT...
		const CataclysmTestWorld::FScopedAilmentRoll Roll(14.9f);
		const FScopedFighter Defender(World);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		TestTrue(TEXT("a roll of 14.9 against a 15% chance makes the target bleed"),
			Defender.Carries(Bleed->TagName));

		// ...AT THE DoT_Bleed ROW'S OWN FIGURES: 20 a second for 5 seconds.
		TestEqual(TEXT("for the Bleed row's five seconds"),
			Defender.SecondsLeftOn(Bleed->TagName), 5.0f, 0.05f);
		TestEqual(TEXT("at its 20 a second"),
			Defender.StatedOn(Bleed->TagName), 20.0f, 0.01f);
	}

	{
		// AND A ROLL AT THE CHANCE DOES NOT, because the comparison is strictly
		// less than.
		const CataclysmTestWorld::FScopedAilmentRoll Roll(15.0f);
		const FScopedFighter Defender(World);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		TestFalse(TEXT("a roll of 15 against a 15% chance does not"),
			Defender.Carries(Bleed->TagName));
	}
	return true;
}

CATACLYSM_AILMENT_TEST(FCataclysmEachAilmentAppliesItsRowTest,
	"Cataclysm.Ailments.EachAilmentAppliesWhatItsStatusRowSays")
{
	using namespace CataclysmAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const CataclysmTestWorld::FScopedAilmentRoll Always(0.0f);

	// WRITTEN OUT FROM game/Data/StatusEffects.csv RATHER THAN READ FROM IT, so
	// an ailment pointing at another ailment's row fails here instead of
	// agreeing with itself. Seconds, and damage a second where the row deals
	// damage.
	struct FExpected
	{
		const TCHAR* Ailment;
		float Seconds;
		float PerSecond;
	};
	const FExpected Rows[] = {
		{ TEXT("Bleed"),     5.0f, 20.0f },
		{ TEXT("Poison"),    8.0f, 20.0f },
		{ TEXT("Disease"),   6.0f, 12.0f },
		{ TEXT("Necrosis"), 10.0f, 10.0f },
		{ TEXT("Burn"),      4.0f, 25.0f },
		{ TEXT("Madness"),   3.0f,  0.0f },
		{ TEXT("Cripple"),   4.0f,  0.0f },
		{ TEXT("Weaken"),    5.0f,  0.0f },
		{ TEXT("Shred"),     6.0f,  0.0f },
	};

	for (const FExpected& Row : Rows)
	{
		const FCataclysmAilmentKind* Wanted = KindOf(Row.Ailment);
		if (!TestNotNull(FString::Printf(TEXT("%s is an ailment"), Row.Ailment),
						 Wanted))
		{
			continue;
		}

		const FScopedFighter Attacker(World);
		Attacker.ArmFor(1'000.0f);
		Attacker.SetChance(*Wanted, 100.0f);

		const FScopedFighter Defender(World);
		Defender.SetAllResistance(40.0f);

		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		TestTrue(FString::Printf(TEXT("a certain chance to apply %s applies it"),
								 Row.Ailment),
			Defender.Carries(Wanted->TagName));
		TestEqual(FString::Printf(TEXT("%s runs for its row's %.0f seconds"),
								  Row.Ailment, Row.Seconds),
			Defender.SecondsLeftOn(Wanted->TagName), Row.Seconds, 0.05f);
		if (Row.PerSecond > 0.0f)
		{
			TestEqual(FString::Printf(TEXT("%s deals its row's %.0f a second"),
									  Row.Ailment, Row.PerSecond),
				Defender.StatedOn(Wanted->TagName), Row.PerSecond, 0.01f);
		}

		// AND ONLY ITS OWN.
		for (const FCataclysmAilmentKind& Other : UCataclysmAilments::Kinds())
		{
			if (&Other != Wanted)
			{
				TestFalse(FString::Printf(TEXT("a chance to apply %s applies no %s"),
										  Row.Ailment, Other.Ailment),
					Defender.Carries(Other.TagName));
			}
		}

		// SHRED CUTS THE ONE GENERIC RESISTANCE AN ENEMY HOLDS BY ITS ROW'S 10,
		// after the blow, and nothing else moves it.
		const bool bIsShred = FCString::Stricmp(Row.Ailment, TEXT("Shred")) == 0;
		const float Expected = bIsShred ? 30.0f : 40.0f;
		TestEqual(FString::Printf(TEXT("after %s a resistance of 40 reads %.0f"),
								  Row.Ailment, Expected),
			Defender.AllResistance(), Expected, 0.01f);
	}

	// VOID SPLINTER STATES A SHARE OF CURRENT HEALTH AND NOT DAMAGE A SECOND, so
	// its row's 1% is read off the effect as the share it carries. Issue #915.
	const FCataclysmAilmentKind* Splinter = KindOf(TEXT("Void Splinter"));
	if (TestNotNull(TEXT("Void Splinter is an ailment"), Splinter))
	{
		const FScopedFighter Attacker(World);
		Attacker.ArmFor(1'000.0f);
		Attacker.SetChance(*Splinter, 100.0f);

		const FScopedFighter Defender(World);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		TestTrue(TEXT("a certain chance to apply void splinter applies it"),
			Defender.Carries(Splinter->TagName));
		TestEqual(TEXT("Void Splinter runs for its row's 4 seconds"),
			Defender.SecondsLeftOn(Splinter->TagName), 4.0f, 0.05f);
		TestEqual(TEXT("taking its row's 1% of current health a tick"),
			Defender.ShareOn(Splinter->TagName), 0.01f, 0.0001f);

		// AND ONLY ITS OWN, as for every row above.
		TestEqual(TEXT("and it is the one debuff the defender carries"),
			UCataclysmDebuffs::CountOn(Defender.AbilitySystem), 1);
	}
	return true;
}

CATACLYSM_AILMENT_TEST(FCataclysmChancePastCertaintyTest,
	"Cataclysm.Ailments.AChancePastCertaintyAppliesTheAilmentHarder")
{
	using namespace CataclysmAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A ROLL OF 99.9 LANDS ONLY A CHANCE OF 100 OR MORE. 250 is capped to 100
	// before it is rolled, which is the first half of the rule: it applies on
	// every hit. The second half is what each case below measures.
	const CataclysmTestWorld::FScopedAilmentRoll AlmostNever(99.9f);

	const FCataclysmAilmentKind* Bleed = KindOf(TEXT("Bleed"));
	const FCataclysmAilmentKind* Madness = KindOf(TEXT("Madness"));
	const FCataclysmAilmentKind* Shred = KindOf(TEXT("Shred"));
	const FCataclysmAilmentKind* Cripple = KindOf(TEXT("Cripple"));
	if (!TestTrue(TEXT("all four are ailments"),
				  Bleed && Madness && Shred && Cripple))
	{
		return false;
	}

	{
		// A BLEED'S ROW SAYS "MAGNITUDE SCALES THE DAMAGE", so 250% deals 2.5
		// times its 20 a second, for its own five seconds.
		const FScopedFighter Attacker(World);
		Attacker.ArmFor(1'000.0f);
		Attacker.SetChance(*Bleed, 250.0f);
		const FScopedFighter Defender(World);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		TestEqual(TEXT("250% to bleed deals 50 a second"),
			Defender.StatedOn(Bleed->TagName), 50.0f, 0.01f);
		TestEqual(TEXT("for the row's five seconds, no longer"),
			Defender.SecondsLeftOn(Bleed->TagName), 5.0f, 0.05f);
	}

	{
		// MADNESS'S ROW SAYS "MAGNITUDE EXTENDS THE DURATION".
		const FScopedFighter Attacker(World);
		Attacker.ArmFor(1'000.0f);
		Attacker.SetChance(*Madness, 250.0f);
		const FScopedFighter Defender(World);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		TestEqual(TEXT("250% to madden lasts 7.5 seconds"),
			Defender.SecondsLeftOn(Madness->TagName), 7.5f, 0.05f);
	}

	{
		// SHRED'S SAYS MAGNITUDE RAISES THE REDUCTION.
		const FScopedFighter Attacker(World);
		Attacker.ArmFor(1'000.0f);
		Attacker.SetChance(*Shred, 250.0f);
		const FScopedFighter Defender(World);
		Defender.SetAllResistance(40.0f);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		TestEqual(TEXT("250% to shred cuts a resistance of 40 by 25"),
			Defender.AllResistance(), 15.0f, 0.01f);
	}

	{
		// AND CRIPPLE'S MAGNITUDE IS NOT BUILT. Its row says magnitude raises the
		// slow to 80% and then extends the duration; until that exists a chance
		// past 100% applies it at the row's own figures, and this says so rather
		// than letting it pass unnoticed.
		const FScopedFighter Attacker(World);
		Attacker.ArmFor(1'000.0f);
		Attacker.SetChance(*Cripple, 250.0f);
		const FScopedFighter Defender(World);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		TestEqual(TEXT("250% to cripple still lasts the row's four seconds"),
			Defender.SecondsLeftOn(Cripple->TagName), 4.0f, 0.05f);
	}

	{
		// AND VOID SPLINTER'S MAGNITUDE RAISES ITS SHARE, which the design
		// document's table says of its damage and the owner's answer on #915
		// keeps: 250% takes 2.5% of current health a tick, for the row's own four
		// seconds.
		const FCataclysmAilmentKind* Splinter = KindOf(TEXT("Void Splinter"));
		if (TestNotNull(TEXT("Void Splinter is an ailment"), Splinter))
		{
			const FScopedFighter Attacker(World);
			Attacker.ArmFor(1'000.0f);
			Attacker.SetChance(*Splinter, 250.0f);
			const FScopedFighter Defender(World);
			UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

			TestEqual(TEXT("250% to splinter takes 2.5% of current health a tick"),
				Defender.ShareOn(Splinter->TagName), 0.025f, 0.0001f);
			TestEqual(TEXT("for the row's four seconds, no longer"),
				Defender.SecondsLeftOn(Splinter->TagName), 4.0f, 0.05f);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// When a chance is not rolled at all
// ---------------------------------------------------------------------------

CATACLYSM_AILMENT_TEST(FCataclysmAilmentThresholdTest,
	"Cataclysm.Ailments.ABlowMustTakeATenthOfMaximumHealthAndLeaveItsTargetAlive")
{
	using namespace CataclysmAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const CataclysmTestWorld::FScopedAilmentRoll Always(0.0f);

	const FCataclysmAilmentKind* Bleed = KindOf(TEXT("Bleed"));
	if (!TestNotNull(TEXT("Bleed is an ailment"), Bleed))
	{
		return false;
	}

	// ONE BLOW AT A FRESH DEFENDER. Says whether it bled, and what the blow took.
	const auto Bleeds = [World, Bleed](float Damage, float Health, float MaxHealth,
									   float& OutTaken)
	{
		const FScopedFighter Attacker(World);
		Attacker.ArmFor(Damage);
		Attacker.SetChance(*Bleed, 100.0f);

		const FScopedFighter Defender(World, MaxHealth);
		Defender.SetHealth(Health, MaxHealth);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		OutTaken = Health - Defender.Health();
		return Defender.Carries(Bleed->TagName);
	};

	// THE PROJECT OWNER'S RULE FOR AN AILMENT NOT FROM THE SKILL'S OWN ROW,
	// settled on #917: the blow must take at least a tenth of maximum health.
	float Taken = 0.0f;
	TestFalse(TEXT("a blow taking 9.9% of maximum health applies nothing"),
		Bleeds(990.0f, 10'000.0f, 10'000.0f, Taken));
	TestEqual(TEXT("and it did land"), Taken, 990.0f, 0.5f);

	TestTrue(TEXT("a blow taking 10.1% applies the bleed"),
		Bleeds(1'010.0f, 10'000.0f, 10'000.0f, Taken));

	// AND A BLOW THAT KILLED APPLIES NOTHING, as a burn is never set on a
	// corpse. 150 is more than a tenth of 1,000, so the threshold is not what
	// stops it.
	TestFalse(TEXT("a blow that kills applies nothing"),
		Bleeds(500.0f, 150.0f, 1'000.0f, Taken));
	TestEqual(TEXT("though it took more than a tenth"), Taken, 150.0f, 0.5f);

	TestTrue(TEXT("while the same blow at full health applies the bleed"),
		Bleeds(500.0f, 1'000.0f, 1'000.0f, Taken));
	return true;
}

CATACLYSM_AILMENT_TEST(FCataclysmAilmentExclusionsTest,
	"Cataclysm.Ailments.AMinionsBlowAndATickOfDamageOverTimeCarryNoChance")
{
	using namespace CataclysmAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const CataclysmTestWorld::FScopedAilmentRoll Always(0.0f);

	const FCataclysmAilmentKind* Bleed = KindOf(TEXT("Bleed"));
	if (!TestNotNull(TEXT("Bleed is an ailment"), Bleed))
	{
		return false;
	}

	const FScopedFighter Attacker(World);
	Attacker.ArmFor(1'000.0f);
	Attacker.SetChance(*Bleed, 100.0f);

	// ONE BLOW AT A FRESH DEFENDER, DELIVERED THIS WAY. Says whether it bled,
	// and whether it landed at all, so a blow that bled nobody because it
	// missed cannot pass for one that carried nothing.
	const auto Bleeds = [World, &Attacker, Bleed](const FCataclysmHitDelivery& Delivery,
												  bool& bOutLanded)
	{
		const FScopedFighter Defender(World);
		const float Before = Defender.Health();
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), Delivery);
		bOutLanded = Defender.Health() < Before;
		return Defender.Carries(Bleed->TagName);
	};

	bool bLanded = false;
	TestTrue(TEXT("an ordinary blow carries the attacker's chance"),
		Bleeds(FCataclysmHitDelivery(), bLanded));

	// A MINION'S BLOW IS DEALT IN ITS SUMMONER'S NAME, and the design names
	// "chance to apply an ailment" among what does not cross from a summoner.
	FCataclysmHitDelivery MinionBlow;
	MinionBlow.bCarriesNoAilmentChance = true;
	TestFalse(TEXT("a blow marked as a minion's carries none"),
		Bleeds(MinionBlow, bLanded));
	TestTrue(TEXT("though it landed"), bLanded);

	// A TICK IS NOT A HIT.
	FCataclysmHitDelivery Tick;
	Tick.bIsDamageOverTime = true;
	TestFalse(TEXT("a tick of damage over time carries none"), Bleeds(Tick, bLanded));
	TestTrue(TEXT("though it landed too"), bLanded);
	return true;
}

CATACLYSM_AILMENT_TEST(FCataclysmAilmentSkillTagsTest,
	"Cataclysm.Ailments.AChanceIsAskedForWithTheSkillsOwnTags")
{
	using namespace CataclysmAilmentTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const CataclysmTestWorld::FScopedAilmentRoll Always(0.0f);

	const FCataclysmAilmentKind* Bleed = KindOf(TEXT("Bleed"));
	const FGameplayTag Melee = UCataclysmDamageCalculation::MeleeTag();
	if (!TestNotNull(TEXT("Bleed is an ailment"), Bleed)
		|| !TestTrue(TEXT("the vocabulary still has Type.Melee"), Melee.IsValid()))
	{
		return false;
	}

	// THE STAT'S INPUTS AS `UCataclysmPlayerClassStats::ApplyTo` WOULD LEAVE
	// THEM for a row granting a chance to bleed with melee skills only, which is
	// the shape a passive node or an enchantment row may take. The attribute
	// stays at zero, because a row requiring a tag is never folded into one, so
	// the only way this chance reaches a blow is by being asked for.
	const FScopedFighter Attacker(World);
	Attacker.ArmFor(1'000.0f);

	FCataclysmStatInputs Inputs;
	Inputs.Base = 0.0f;
	FCataclysmStatModifier WithMeleeSkills;
	WithMeleeSkills.Bucket = ECataclysmStatBucket::Flat;
	WithMeleeSkills.Source = ECataclysmModifierSource::PassiveKeystone;
	WithMeleeSkills.Value = 100.0f;
	WithMeleeSkills.RequiredTags.AddTag(Melee);
	Inputs.Modifiers.Add(WithMeleeSkills);

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(Bleed->Stat), Inputs);
	Attacker.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	{
		const FScopedFighter Defender(World);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(Melee));
		TestTrue(TEXT("a melee skill carries a chance scoped to melee"),
			Defender.Carries(Bleed->TagName));
	}

	{
		const FScopedFighter Defender(World);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);
		TestFalse(TEXT("and a skill without the tag does not"),
			Defender.Carries(Bleed->TagName));
	}
	return true;
}

#undef CATACLYSM_AILMENT_TEST

#endif // WITH_AUTOMATION_TESTS
