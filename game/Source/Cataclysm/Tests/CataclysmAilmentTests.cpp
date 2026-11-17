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

		/**
		 * What a passive node raising this ailment's magnitude would have
		 * written, in per cent, where 100 is the effect's own figure.
		 *
		 * ONLY TWO AILMENTS HAVE ONE. Issue #1767. Calling this for one that
		 * does not would dereference a null attribute accessor, so it refuses
		 * and says so rather than crashing the whole run.
		 */
		bool SetMagnitude(const FCataclysmAilmentKind& Kind, float Percent) const
		{
			if (!Kind.MagnitudeAttribute)
			{
				return false;
			}
			AbilitySystem->SetNumericAttributeBase(Kind.MagnitudeAttribute(),
												   Percent);
			return true;
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

		/** What this ailment's magnitude stat currently reads, or -1 for an
		 *  ailment that has no such stat. */
		float MagnitudeOf(const FCataclysmAilmentKind& Kind) const
		{
			return Kind.MagnitudeAttribute
				? AbilitySystem->GetNumericAttribute(Kind.MagnitudeAttribute())
				: -1.0f;
		}

		/**
		 * Resolve this character's stats the way a real player's are resolved,
		 * instead of writing an attribute by hand.
		 *
		 * WHY ANY TEST HERE NEEDS THIS. Every other helper on this fixture calls
		 * `SetNumericAttributeBase`, which writes an attribute directly. That is
		 * the fallback route and it hides a whole class of fault:
		 * `UCataclysmPlayerClassStats::ApplyTo` resolves every stat
		 * `StatToAttribute` names and writes the result over whatever the
		 * attribute set's constructor stated. A stat with no class line and no
		 * entry in `EngineSuppliedBases` resolves to zero, and no test that
		 * writes the attribute by hand can see it.
		 *
		 * `StartingClassName` AND NOT `UCataclysmClassStats::DefaultClassName`.
		 * The second is the shared line a class inherits from and carries no
		 * defensive layer; the first is the class a player actually plays as.
		 * Issue #806. `DefaultLevel` rather than a typed number, because it is a
		 * placeholder the console can change.
		 */
		bool ResolveFromTheClassTable() const
		{
			const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
			if (!Table)
			{
				return false;
			}
			UCataclysmPlayerClassStats::ApplyTo(
				AbilitySystem, Table,
				UCataclysmPlayerClassStats::StartingClassName,
				UCataclysmPlayerClassStats::DefaultLevel);
			return true;
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

CATACLYSM_AILMENT_TEST(FCataclysmAilmentChanceOf100Test,
	"Cataclysm.Ailments.AnAilmentChanceOf100AppliesAtARollOf100AndAChanceBelow100StillFailsAtItsChance")
{
	using namespace CataclysmAilmentTest;

	// WHAT IS UNDER TEST. Issue #2201. The roll is drawn from 0 to 100 and can be
	// exactly 100, and the comparison is "the roll is below the chance", so a blow
	// whose chance was 100 applied nothing on that one roll.
	// `UCataclysmAilments::RollOnLandedBlow` now applies a chance at the cap
	// without comparing it with the roll.
	//
	// TWO RULES DECIDE WHETHER A BLOW LEAVES AN AILMENT, and each assertion below
	// says which one it shows. "The roll" is the comparison of the pinned roll
	// with the chance. "The tenth rule" is the owner's rule of 2026-09-02 (issue
	// #917): the blow must take at least a tenth of the target's maximum health.
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

	// A BLOW OF 1,000. Against 5,000 maximum health it takes a fifth, which is
	// twice what the tenth rule asks. Against 20,000 it takes a twentieth, which
	// is half of it.
	const float Blow = 1000.0f;
	const float SmallPool = 5000.0f;
	const float DeepPool = 20000.0f;

	// ONE BLOW FROM A FRESH ATTACKER AT A FRESH DEFENDER, with the roll pinned
	// for that blow only. Says whether the variable read back the value pinned,
	// what the blow took, and whether the defender bleeds after it.
	struct FRolledBlow
	{
		bool bPinned = false;
		bool bBleeds = false;
		float Taken = 0.0f;
	};
	const auto Strike = [&](float Chance, float Roll, float MaxHealth)
	{
		FRolledBlow Out;
		const CataclysmTestWorld::FScopedAilmentRoll Pinned(Roll);
		Out.bPinned = Pinned.Variable != nullptr
			&& FMath::IsNearlyEqual(Pinned.Variable->GetFloat(), Roll, 0.0001f);

		const FScopedFighter Attacker(World);
		Attacker.ArmFor(Blow);
		Attacker.SetChance(*Bleed, Chance);

		const FScopedFighter Defender(World, MaxHealth);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		Out.Taken = MaxHealth - Defender.Health();
		Out.bBleeds = Defender.Carries(Bleed->TagName);
		return Out;
	};

	// THE SET-UP, ASSERTED BEFORE THE BEHAVIOUR. If the variable could not be
	// pinned every blow below would roll for real, and if the bleed could never
	// land every "does not apply" below would pass for that reason.
	const FRolledBlow AtNought = Strike(60.0f, 0.0f, SmallPool);
	if (!TestTrue(TEXT("set-up: Cataclysm.AilmentRoll is pinned and reads back 0"),
				  AtNought.bPinned)
		|| !TestTrue(FString::Printf(TEXT("set-up, the tenth rule: the blow takes "
										  "over a tenth of maximum health, %.1f "
										  "of %.0f"), AtNought.Taken, SmallPool),
					 AtNought.Taken >= SmallPool / 10.0f)
		|| !TestTrue(TEXT("set-up, the roll: a chance of 60 at a roll of 0 applies "
						  "the bleed, so the bleed can land"),
					 AtNought.bBleeds))
	{
		return false;
	}

	// A CHANCE OF EXACTLY 100 AT A ROLL OF 100. This is the assertion that fails
	// without the guard, because 100 is not below 100.
	const FRolledBlow Stated = Strike(100.0f, 100.0f, SmallPool);
	if (!TestTrue(TEXT("set-up: Cataclysm.AilmentRoll is pinned and reads back 100"),
				  Stated.bPinned))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("the tenth rule does not decide the next "
								  "assertion: the blow takes over a tenth, %.1f "
								  "of %.0f"), Stated.Taken, SmallPool),
			 Stated.Taken >= SmallPool / 10.0f);
	TestTrue(TEXT("the roll, a chance at the cap: a chance of 100 at a roll of 100 "
				  "applies the bleed"),
			 Stated.bBleeds);

	// A CHANCE SUMMED ABOVE 100. `Application` cuts it to exactly 100 before the
	// roll, so without the guard it failed at a roll of 100 as well.
	const FRolledBlow Summed = Strike(150.0f, 100.0f, SmallPool);
	TestTrue(TEXT("set-up: the roll reads back 100 for the chance of 150"),
			 Summed.bPinned);
	TestTrue(TEXT("the roll, a chance summed above the cap and cut to it: a chance "
				  "of 150 at a roll of 100 applies the bleed"),
			 Summed.bBleeds);

	// A CHANCE BELOW 100 IS COMPARED WITH THE ROLL AS IT WAS. A roll equal to the
	// chance fails, and so does every roll above it.
	const FRolledBlow BelowAtTheTop = Strike(60.0f, 100.0f, SmallPool);
	TestTrue(TEXT("set-up: the roll reads back 100 for the chance of 60"),
			 BelowAtTheTop.bPinned);
	TestFalse(TEXT("the roll, a chance below the cap: a chance of 60 at a roll of "
				   "100 applies nothing"),
			  BelowAtTheTop.bBleeds);

	const FRolledBlow BelowAtItsChance = Strike(60.0f, 60.0f, SmallPool);
	TestTrue(TEXT("set-up: the roll reads back 60"), BelowAtItsChance.bPinned);
	TestFalse(TEXT("the roll, a chance below the cap: a chance of 60 at a roll of "
				   "exactly 60 applies nothing"),
			  BelowAtItsChance.bBleeds);

	const FRolledBlow BelowUnderItsChance = Strike(60.0f, 59.9f, SmallPool);
	TestTrue(TEXT("set-up: the roll reads back 59.9"), BelowUnderItsChance.bPinned);
	TestTrue(TEXT("the roll, a chance below the cap: a chance of 60 at a roll of "
				  "59.9 applies the bleed"),
			 BelowUnderItsChance.bBleeds);

	// AND THE OTHER RULE IS WHAT IT WAS. The same chance and the same roll, on a
	// blow that takes under a tenth of maximum health, apply nothing. The guard
	// is inside the roll and the tenth rule is asked before any roll.
	const FRolledBlow Small = Strike(100.0f, 100.0f, DeepPool);
	TestTrue(TEXT("set-up: the roll reads back 100 for the small blow"),
			 Small.bPinned);
	TestTrue(FString::Printf(TEXT("the tenth rule: the small blow lands and takes "
								  "under a tenth, %.1f of %.0f"),
							 Small.Taken, DeepPool),
			 Small.Taken > 0.0f && Small.Taken < DeepPool / 10.0f);
	TestFalse(TEXT("the tenth rule, unchanged: a chance of 100 at a roll of 100 on "
				   "a blow that takes under a tenth applies nothing"),
			  Small.bBleeds);
	return true;
}

CATACLYSM_AILMENT_TEST(FCataclysmStunChanceOf100Test,
	"Cataclysm.Ailments.AStunChanceOf100StunsAtARollOf100AndAChanceBelow100StillFailsAtItsChance")
{
	using namespace CataclysmAilmentTest;

	// THE STUN ROLL, which is a comparison of its own in
	// `UCataclysmAilments::RollOnLandedBlow` and has the same guard. Issue #2201.
	//
	// NONE OF `UCataclysmSkillEffects::ApplyStun`'S OWN RULES REFUSES THESE BLOWS,
	// and the set-up assertion at a roll of 0 is what shows it. The attacker is a
	// bare actor, so it holds no row that puts a health ceiling on its crowd
	// control. The defender is a bare actor made for one blow, so it has no crowd
	// control resistance, is in no window of immunity after an earlier stun, runs
	// no skill that makes it immune and is not a boss. The blow takes a fifth of
	// its maximum health and leaves it alive.
	//
	// THE ATTACKER HOLDS NO WEAPON, so a blunt weapon's own 10 is not added and
	// the chance rolled is the chance set. The control at a roll of exactly 60
	// shows that: with 10 added the chance would be 70 and that blow would stun.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FCataclysmAilmentKind* Stun = KindOf(TEXT("Stun"));
	if (!TestNotNull(TEXT("Stun is an ailment"), Stun))
	{
		return false;
	}

	const float Blow = 1000.0f;
	const float Pool = 5000.0f;

	// ONE BLOW FROM A FRESH ATTACKER AT A FRESH DEFENDER, with the roll pinned
	// for that blow only.
	struct FRolledBlow
	{
		bool bPinned = false;
		bool bStunned = false;
		float Taken = 0.0f;
	};
	const auto Strike = [&](float Chance, float Roll)
	{
		FRolledBlow Out;
		const CataclysmTestWorld::FScopedAilmentRoll Pinned(Roll);
		Out.bPinned = Pinned.Variable != nullptr
			&& FMath::IsNearlyEqual(Pinned.Variable->GetFloat(), Roll, 0.0001f);

		const FScopedFighter Attacker(World);
		Attacker.ArmFor(Blow);
		Attacker.SetChance(*Stun, Chance);

		const FScopedFighter Defender(World, Pool);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

		Out.Taken = Pool - Defender.Health();
		Out.bStunned = Defender.Carries(Stun->TagName);
		return Out;
	};

	// THE SET-UP, ASSERTED BEFORE THE BEHAVIOUR.
	const FRolledBlow AtNought = Strike(60.0f, 0.0f);
	if (!TestTrue(TEXT("set-up: Cataclysm.AilmentRoll is pinned and reads back 0"),
				  AtNought.bPinned)
		|| !TestTrue(FString::Printf(TEXT("set-up, the tenth rule: the blow takes "
										  "over a tenth of maximum health, %.1f "
										  "of %.0f"), AtNought.Taken, Pool),
					 AtNought.Taken >= Pool / 10.0f && AtNought.Taken < Pool)
		|| !TestTrue(TEXT("set-up, the stun's own rules: a chance of 60 at a roll "
						  "of 0 stuns, so none of them refuses this target and "
						  "this blow"),
					 AtNought.bStunned))
	{
		return false;
	}

	// NO CHANCE IS STILL NO STUN, at the roll every chance above nought beats.
	const FRolledBlow NoChance = Strike(0.0f, 0.0f);
	TestTrue(TEXT("set-up: the roll reads back 0 for the blow with no chance"),
			 NoChance.bPinned);
	TestFalse(TEXT("the roll, no chance: a chance of nought at a roll of 0 does "
				   "not stun"),
			  NoChance.bStunned);

	// A CHANCE OF EXACTLY 100 AT A ROLL OF 100, which did not stun without the
	// guard.
	const FRolledBlow Stated = Strike(100.0f, 100.0f);
	if (!TestTrue(TEXT("set-up: Cataclysm.AilmentRoll is pinned and reads back 100"),
				  Stated.bPinned))
	{
		return false;
	}
	TestTrue(TEXT("the roll, a chance at the cap: a chance of 100 at a roll of 100 "
				  "stuns"),
			 Stated.bStunned);

	// A CHANCE BELOW 100 IS COMPARED WITH THE ROLL AS IT WAS.
	const FRolledBlow BelowAtTheTop = Strike(60.0f, 100.0f);
	TestTrue(TEXT("set-up: the roll reads back 100 for the chance of 60"),
			 BelowAtTheTop.bPinned);
	TestFalse(TEXT("the roll, a chance below the cap: a chance of 60 at a roll of "
				   "100 does not stun"),
			  BelowAtTheTop.bStunned);

	const FRolledBlow BelowAtItsChance = Strike(60.0f, 60.0f);
	TestTrue(TEXT("set-up: the roll reads back 60"), BelowAtItsChance.bPinned);
	TestFalse(TEXT("the roll, a chance below the cap: a chance of 60 at a roll of "
				   "exactly 60 does not stun"),
			  BelowAtItsChance.bStunned);

	const FRolledBlow BelowUnderItsChance = Strike(60.0f, 59.9f);
	TestTrue(TEXT("set-up: the roll reads back 59.9"), BelowUnderItsChance.bPinned);
	TestTrue(TEXT("the roll, a chance below the cap: a chance of 60 at a roll of "
				  "59.9 stuns"),
			 BelowUnderItsChance.bStunned);
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

// ---------------------------------------------------------------------------
// The magnitude stats. Issue #1767.
// ---------------------------------------------------------------------------

CATACLYSM_AILMENT_TEST(FCataclysmAilmentMagnitudeStatArithmeticTest,
	"Cataclysm.Ailments.AMagnitudeStatScalesWhatChanceOverflowProduced")
{
	// A HUNDRED IS UNCHANGED, which is what the two attributes start at and the
	// reason `NormalMagnitude` exists. Following `UCataclysmDebuffs::
	// NormalDuration`, the same shape for the nearest existing stat.
	const float Cap = UCataclysmAilments::ChanceCap;
	const float Normal = UCataclysmAilments::NormalMagnitude;

	float Chance = 0.0f;
	float Magnitude = 0.0f;

	UCataclysmAilments::Application(Cap, Chance, Magnitude);
	TestEqual(TEXT("at the cap and no stat, magnitude is one"), Magnitude, 1.0f,
			  0.001f);

	UCataclysmAilments::Application(Cap, Chance, Magnitude, Normal);
	TestEqual(TEXT("and a stat of 100 changes nothing"), Magnitude, 1.0f, 0.001f);

	// THE HALF THAT WOULD BE LOST BY MULTIPLYING BEFORE THE FLOOR RATHER THAN
	// AFTER IT. A character at or below the cap has a magnitude of exactly one
	// from chance alone, so a build that applied the stat first and floored
	// afterwards would floor the investment away and read 1.0 here. Most
	// characters are below the cap, so that build would look correct in play
	// only for the few above it.
	UCataclysmAilments::Application(Cap, Chance, Magnitude, 150.0f);
	TestEqual(TEXT("a stat of 150 at the cap gives one and a half"), Magnitude,
			  1.5f, 0.001f);

	UCataclysmAilments::Application(Cap / 2.0f, Chance, Magnitude, 150.0f);
	TestEqual(TEXT("and the same below the cap, where chance overflow gives nothing"),
			  Magnitude, 1.5f, 0.001f);

	// AND IT MULTIPLIES THE OVERFLOW RATHER THAN REPLACING IT. 250% chance is
	// magnitude 2.5 on its own; a stat of 200 makes it 5.
	UCataclysmAilments::Application(Cap * 2.5f, Chance, Magnitude);
	TestEqual(TEXT("250% chance alone is two and a half"), Magnitude, 2.5f, 0.001f);

	UCataclysmAilments::Application(Cap * 2.5f, Chance, Magnitude, 200.0f);
	TestEqual(TEXT("and a stat of 200 on top of it is five"), Magnitude, 5.0f,
			  0.001f);

	// A NEGATIVE STAT IS REFUSED RATHER THAN INVERTING THE EFFECT.
	UCataclysmAilments::Application(Cap, Chance, Magnitude, -50.0f);
	TestEqual(TEXT("a negative stat applies nothing rather than reversing it"),
			  Magnitude, 0.0f, 0.001f);

	return true;
}

CATACLYSM_AILMENT_TEST(FCataclysmCrippleMagnitudeStatReachesTheEnemyTest,
	"Cataclysm.Ailments.ACrippleMagnitudeStatMakesTheAppliedCrippleLarger")
{
	using namespace CataclysmAilmentTest;

	// WHY A REAL BLOW RATHER THAN CALLING `Apply` DIRECTLY. The stat is resolved
	// on the attacker's side in `ChancesFor`, travels on the damage effect, and
	// is read back where the roll is made. A test that handed the magnitude in
	// would prove the arithmetic above and nothing about that journey -- which
	// is the half that was missing for `Wearing Them Down` and is the failure
	// this whole class of work produces: a row that validates and grants
	// nothing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FCataclysmAilmentKind* Cripple = KindOf(TEXT("Cripple"));
	if (!TestNotNull(TEXT("Cripple is an ailment"), Cripple))
	{
		return false;
	}

	// THE STATE THIS TEST BUILDS IS ASSERTED BEFORE THE BEHAVIOUR IS. An ailment
	// with no magnitude stat would make both halves below measure the same
	// thing, and `SetMagnitude` returning false is how that shows.
	const FScopedFighter Plain(World);
	const FScopedFighter Invested(World);
	Plain.ArmFor(1'000.0f);
	Invested.ArmFor(1'000.0f);
	Plain.SetChance(*Cripple, UCataclysmAilments::ChanceCap);
	Invested.SetChance(*Cripple, UCataclysmAilments::ChanceCap);

	if (!TestTrue(TEXT("Cripple has a magnitude stat to raise"),
				  Invested.SetMagnitude(*Cripple, 150.0f))
		|| !TestTrue(TEXT("and the plain attacker is left at the normal figure"),
					 Plain.SetMagnitude(*Cripple,
										UCataclysmAilments::NormalMagnitude)))
	{
		return false;
	}

	// A ROLL OF ZERO LANDS EVERY CHANCE ABOVE NOTHING, so both blows apply.
	const CataclysmTestWorld::FScopedAilmentRoll Roll(0.0f);

	const FScopedFighter StruckByPlain(World);
	const FScopedFighter StruckByInvested(World);
	UCataclysmSkillEffects::ApplyHit(Plain.Actor, StruckByPlain.Actor, 100.0f);
	UCataclysmSkillEffects::ApplyHit(Invested.Actor, StruckByInvested.Actor,
									 100.0f);

	if (!TestTrue(TEXT("both targets carry the Cripple"),
				  StruckByPlain.Carries(Cripple->TagName)
				  && StruckByInvested.Carries(Cripple->TagName)))
	{
		return false;
	}

	// THE ROW'S OWN FIGURES, READ RATHER THAN TYPED, so re-tuning the curse in
	// the sheet does not break this. Cripple states 30 with a cap of 80, so 150%
	// gives 45 and both are under the cap -- which is deliberate: a figure at the
	// cap would be identical for both attackers and prove nothing.
	// READ BY THE ROW NAME THE AILMENT ITSELF CARRIES, so this needs no tag and
	// no second spelling of "Cripple" that could drift from the table's.
	const FCataclysmStatusEffectNumbers Row =
		UCataclysmSkillEffects::StatusEffectNumbers(Cripple->StatusRow,
												   Cripple->Ailment);

	const float FromPlain = StruckByPlain.StatedOn(Cripple->TagName);
	const float FromInvested = StruckByInvested.StatedOn(Cripple->TagName);

	if (!TestTrue(FString::Printf(
			TEXT("the row's strength and cap leave room to scale, got %.1f and %.1f"),
			Row.Strength, Row.StrengthCap),
		Row.Strength > 0.0f && Row.StrengthCap > Row.Strength * 1.5f))
	{
		return false;
	}

	TestEqual(TEXT("an attacker with no investment applies the row's own figure"),
			  FromPlain, Row.Strength, 0.01f);
	TestEqual(TEXT("and one with 150% magnitude applies half again as much"),
			  FromInvested, Row.Strength * 1.5f, 0.01f);

	return true;
}

CATACLYSM_AILMENT_TEST(FCataclysmResolvedCrippleMagnitudeTest,
	"Cataclysm.Ailments.ACharacterResolvedFromTheClassTableAppliesAnOrdinaryCripple")
{
	using namespace CataclysmAilmentTest;

	// THIS TEST EXISTS BECAUSE THE TWO ABOVE IT WOULD BOTH PASS WHILE THE
	// FEATURE WAS BROKEN FOR EVERY PLAYER. `cripple_magnitude` has no line in
	// `game/Data/ClassStats.csv`, so `UCataclysmClassStats::BaseFor` answers
	// zero for it; `UCataclysmPlayerClassStats::ApplyTo` then writes that zero
	// over the 100 the attribute set's constructor states, and `Application`
	// multiplies the magnitude by zero. Every Cripple a player applied would
	// land at a strength of nothing -- worse than the stat not existing, which
	// would at least have left the curse at its row's figure. The repair is an
	// entry in `UCataclysmPlayerClassStats::EngineSuppliedBases`.
	//
	// WHY THE EXISTING BASE TEST IS NOT ENOUGH ON ITS OWN.
	// `Cataclysm.PlayerStats.EveryEngineSuppliedBaseReachesACharacter` walks the
	// entries that ARE in that map. Delete the magnitude entry and it walks two
	// fewer and passes, so it cannot see an entry go missing -- which is exactly
	// how this defect would come back. This test asks for the behaviour instead.
	//
	// IT SETS NO MAGNITUDE STAT. Every other test in this file writes attributes
	// by hand, which is the fallback route. This one takes the resolved route
	// and asserts the ordinary, unmodified outcome: a Cripple at the figure its
	// own row states.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FCataclysmAilmentKind* Cripple = KindOf(TEXT("Cripple"));
	if (!TestNotNull(TEXT("Cripple is an ailment"), Cripple))
	{
		return false;
	}

	const FScopedFighter Attacker(World);
	if (!TestTrue(TEXT("the class stats table loaded"),
				  Attacker.ResolveFromTheClassTable()))
	{
		return false;
	}

	// THE BASE ARRIVED. Checked before the blow so a failure says which half
	// broke: a wrong figure here means the base never reached the character, and
	// a wrong strength below means it reached it and something later lost it.
	if (!TestEqual(
			TEXT("a resolved character holds the normal magnitude, not zero. A "
				 "zero here means the EngineSuppliedBases entry is missing"),
			Attacker.MagnitudeOf(*Cripple), UCataclysmAilments::NormalMagnitude,
			0.01f))
	{
		return false;
	}

	// AFTER RESOLVING AND NOT BEFORE. `ApplyTo` writes every stat the map names,
	// so a chance set first would be overwritten with the class table's zero.
	Attacker.ArmFor(1'000.0f);
	Attacker.SetChance(*Cripple, UCataclysmAilments::ChanceCap);

	const CataclysmTestWorld::FScopedAilmentRoll Roll(0.0f);

	const FScopedFighter Defender(World);
	UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);

	if (!TestTrue(TEXT("the target carries the Cripple"),
				  Defender.Carries(Cripple->TagName)))
	{
		return false;
	}

	const FCataclysmStatusEffectNumbers Row =
		UCataclysmSkillEffects::StatusEffectNumbers(Cripple->StatusRow,
												   Cripple->Ailment);

	TestEqual(TEXT("and it lands at the figure its own row states"),
			  Defender.StatedOn(Cripple->TagName), Row.Strength, 0.01f);

	return true;
}

#undef CATACLYSM_AILMENT_TEST

#endif // WITH_AUTOMATION_TESTS
