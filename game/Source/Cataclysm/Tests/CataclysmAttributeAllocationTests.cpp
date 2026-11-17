// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/Actor.h"

/**
 * Attribute points: earning them, spending them, and what they are worth.
 *
 * WHAT THESE GUARD. Issues #50 and #897. Four separate pieces of machinery for
 * attributes existed in the project and none of them was joined to the others:
 * `UCataclysmPrimaryAttributeSet` declared all eight and every player carried
 * it, `game/Data/Attributes.csv` said what a point was worth,
 * `UCataclysmClassStats::AttributeModifierFor` did the arithmetic, and
 * `FCataclysmAttributePoints` held the counts. Nothing wrote an attribute,
 * nothing read one, and the eight gear affixes that increase an attribute
 * printed a number on a tool tip and changed nothing at all.
 *
 * THE ONE THAT MATTERS MOST IS THE ORDERING TEST. An attribute has to be
 * resolved before the sixteen stats it scales, and if it is not, every one of
 * those stats simply comes out at its base with nothing reporting an error --
 * which is indistinguishable from a character who spent no points. It is the
 * failure this whole feature is most likely to have and the least likely to be
 * noticed, so it is checked with a character whose attribute is raised by gear:
 * that can only come out right if the gear increase was applied to the attribute
 * BEFORE the attribute was used to scale anything.
 *
 * WHAT IS DELIBERATELY NOT HERE. Levelling. `Cataclysm.PlayerLevel` is still a
 * console variable and nothing grants experience, so "a character has one point
 * per level" is checked against that stand-in rather than against a level a
 * character earned. Issue #50 is still open for the rest.
 */

namespace CataclysmAttributeAllocationTest
{
	/** An actor carrying an ability system component with all five sets. */
	struct FScopedCharacter
	{
		explicit FScopedCharacter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers rather than TObjectPtr, because
			// AddAttributeSetSubobject deduces its type from the argument and
			// would deduce the wrapper.
			UCataclysmVitalAttributeSet* Vitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* Combat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* Resource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* Resistance =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmPrimaryAttributeSet* Primary =
				NewObject<UCataclysmPrimaryAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(Vitals);
			AbilitySystem->AddAttributeSetSubobject(Combat);
			AbilitySystem->AddAttributeSetSubobject(Resource);
			AbilitySystem->AddAttributeSetSubobject(Resistance);
			AbilitySystem->AddAttributeSetSubobject(Primary);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Read(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Apply a class line at a level with an allocation, and optional gear.
	 *
	 * THE POINTS GO IN AS BASE OVERRIDES, which is what the game does:
	 * `UCataclysmPlayerClassStats::MergeAttributeBases` is the one place that
	 * decides it, and calling it here rather than building the map by hand means
	 * this test cannot disagree with the game about where a point lands.
	 */
	void Apply(const FScopedCharacter& Character, const UDataTable* Classes,
			   const TCHAR* ClassName, int32 Level,
			   const FCataclysmAttributePoints& Points,
			   const TMap<FName, TArray<FCataclysmStatModifier>>* Gear = nullptr)
	{
		TMap<FName, float> Bases;
		UCataclysmPlayerClassStats::MergeAttributeBases(Points, Bases);

		UCataclysmPlayerClassStats::ApplyTo(
			Character.AbilitySystem, Classes, ClassName, Level,
			Gear, ECataclysmPoolFill::LeaveAsTheyAre, &Bases);
	}

	/** One increased modifier for a stat, as a gear affix produces. */
	TMap<FName, TArray<FCataclysmStatModifier>> IncreasedBy(const TCHAR* Stat,
														   float Percent)
	{
		FCataclysmStatModifier Modifier;
		Modifier.Bucket = ECataclysmStatBucket::Increased;
		Modifier.Source = ECataclysmModifierSource::GearAffix;
		Modifier.Value = Percent;

		TMap<FName, TArray<FCataclysmStatModifier>> Gear;
		Gear.Add(FName(Stat), {Modifier});
		return Gear;
	}
}

// --------------------------------------------------------------------------
// What a point is worth
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttributePointsScaleTheirStats,
	"Cataclysm.Attributes.SpendingPointsRaisesTheStatsThoseAttributesScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttributePointsScaleTheirStats::RunTest(const FString&)
{
	using namespace CataclysmAttributeAllocationTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const UDataTable* Classes = UCataclysmPlayerClassStats::LoadTable();
	if (!Classes)
	{
		AddError(TEXT("the class stat table would not load"));
		return false;
	}

	constexpr int32 Level = 100;

	// Nothing spent, which is the character the game had before issue #50.
	FScopedCharacter Bare(World);
	Apply(Bare, Classes, TEXT("Ravager"), Level, FCataclysmAttributePoints());
	const float WithoutPoints =
		Bare.Read(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());

	// Sixty into Vitality. game/Data/Attributes.csv gives vitality 2% increased
	// maximum health a point, so this is 120 percentage points of increase.
	FCataclysmAttributePoints Points;
	Points.Vitality = 60;

	FScopedCharacter Spent(World);
	Apply(Spent, Classes, TEXT("Ravager"), Level, Points);
	const float WithPoints =
		Spent.Read(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());

	TestTrue(TEXT("a character who spent nothing still has its class health"),
		WithoutPoints > 0.0f);

	// READ OFF THE TABLE RATHER THAN WRITTEN HERE. Quoting 2.0 in this file
	// would let the design change and this test keep agreeing with a stale copy.
	const UDataTable* Attributes = UCataclysmPlayerClassStats::LoadAttributeTable();
	FCataclysmStatModifier FromVitality;
	if (!TestTrue(TEXT("vitality touches maximum health at all"),
			UCataclysmClassStats::AttributeModifierFor(
				Attributes, Points, TEXT("max_health"), FromVitality)))
	{
		return false;
	}

	const float Expected = WithoutPoints * (1.0f + FromVitality.Value / 100.0f);
	TestTrue(FString::Printf(
			TEXT("60 vitality raises maximum health from %.1f to %.1f, and the "
				 "table says it should be %.1f"),
			WithoutPoints, WithPoints, Expected),
		FMath::IsNearlyEqual(WithPoints, Expected, 0.5f));

	// AND THE ATTRIBUTE ITSELF WAS WRITTEN, which nothing did before. A stat
	// that moved would not prove that on its own.
	TestEqual(TEXT("the vitality attribute holds the points spent"),
		Spent.Read(UCataclysmPrimaryAttributeSet::GetVitalityAttribute()), 60.0f);

	return true;
}

// --------------------------------------------------------------------------
// The ordering, which is the failure most likely to go unnoticed
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttributesResolveBeforeWhatTheyScale,
	"Cataclysm.Attributes.GearRaisesAnAttributeBeforeThatAttributeScalesAnything",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttributesResolveBeforeWhatTheyScale::RunTest(const FString&)
{
	using namespace CataclysmAttributeAllocationTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const UDataTable* Classes = UCataclysmPlayerClassStats::LoadTable();
	const UDataTable* Attributes = UCataclysmPlayerClassStats::LoadAttributeTable();
	if (!Classes || !Attributes)
	{
		AddError(TEXT("the class stat or attribute table would not load"));
		return false;
	}

	constexpr int32 Level = 100;
	constexpr float IncreasePercent = 50.0f;

	FCataclysmAttributePoints Points;
	Points.Vitality = 10;

	// The same character, once with gear increasing vitality and once without.
	FScopedCharacter Plain(World);
	Apply(Plain, Classes, TEXT("Ravager"), Level, Points);

	const TMap<FName, TArray<FCataclysmStatModifier>> Gear =
		IncreasedBy(TEXT("vitality"), IncreasePercent);
	FScopedCharacter Geared(World);
	Apply(Geared, Classes, TEXT("Ravager"), Level, Points, &Gear);

	// FIRST: THE GEAR REACHED THE ATTRIBUTE. Ten points increased by 50% is
	// fifteen. This half is issue #897 on its own.
	TestEqual(TEXT("gear increasing vitality raises the vitality attribute"),
		Geared.Read(UCataclysmPrimaryAttributeSet::GetVitalityAttribute()),
		15.0f);
	TestEqual(TEXT("and the same character without it has the bare ten"),
		Plain.Read(UCataclysmPrimaryAttributeSet::GetVitalityAttribute()),
		10.0f);

	// SECOND, AND THIS IS THE ORDERING: maximum health has to reflect FIFTEEN
	// vitality, not ten. It can only do that if the attribute was finished
	// before it was used to scale anything, which is why ApplyTo runs two
	// passes rather than walking StatToAttribute once in hash order.
	FCataclysmAttributePoints AsIfResolved;
	AsIfResolved.Vitality = 15;

	FCataclysmStatModifier FromTen;
	FCataclysmStatModifier FromFifteen;
	UCataclysmClassStats::AttributeModifierFor(
		Attributes, Points, TEXT("max_health"), FromTen);
	UCataclysmClassStats::AttributeModifierFor(
		Attributes, AsIfResolved, TEXT("max_health"), FromFifteen);

	const float PlainHealth =
		Plain.Read(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	const float GearedHealth =
		Geared.Read(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());

	// The class base, backed out of the plain character so this compares like
	// with like whatever the Ravager's health happens to be.
	const float Base = PlainHealth / (1.0f + FromTen.Value / 100.0f);
	const float ExpectedGeared = Base * (1.0f + FromFifteen.Value / 100.0f);

	TestTrue(FString::Printf(
			TEXT("maximum health follows the RESOLVED vitality: %.1f against "
				 "the %.1f fifteen vitality should give, where ten would give "
				 "%.1f"),
			GearedHealth, ExpectedGeared, PlainHealth),
		FMath::IsNearlyEqual(GearedHealth, ExpectedGeared, 0.5f));

	TestTrue(TEXT("and that is more than the same character without the gear"),
		GearedHealth > PlainHealth + 0.5f);

	return true;
}

// --------------------------------------------------------------------------
// An attribute with nothing to scale
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttributeWithNoBaseChangesNothing,
	"Cataclysm.Attributes.APointScalingAStatTheClassHasNoBaseForChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttributeWithNoBaseChangesNothing::RunTest(const FString&)
{
	using namespace CataclysmAttributeAllocationTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const UDataTable* Classes = UCataclysmPlayerClassStats::LoadTable();
	if (!Classes)
	{
		AddError(TEXT("the class stat table would not load"));
		return false;
	}

	// SPIRIT SCALES MAXIMUM ENERGY SHIELD, AND A RAVAGER HAS NONE.
	// `docs/Cataclysm_GDD_v2.md`: "A class does not need a base above zero for
	// every stat... that is the system working rather than failing -- it is how
	// a class declines to care about a stat." An attribute point is an increase
	// and an increase on nothing is nothing, so this is the design and not a gap.
	FCataclysmAttributePoints Points;
	Points.Spirit = 50;

	FScopedCharacter Character(World);
	Apply(Character, Classes, TEXT("Ravager"), 100, Points);

	TestEqual(TEXT("the spirit attribute still holds the points spent"),
		Character.Read(UCataclysmPrimaryAttributeSet::GetSpiritAttribute()),
		50.0f);
	TestEqual(TEXT("and a Ravager still has no energy shield to scale"),
		Character.Read(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()),
		0.0f);

	// THE SAME POINTS ON A CLASS THAT DOES HAVE ONE, so the test above cannot
	// pass because attributes are broken rather than because the base is zero.
	FScopedCharacter Ritualist(World);
	Apply(Ritualist, Classes, TEXT("Ritualist"), 100, Points);

	TestTrue(TEXT("a Ritualist, which has an energy shield, does gain from it"),
		Ritualist.Read(
			UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute()) > 0.0f);

	return true;
}

// --------------------------------------------------------------------------
// Earning and spending
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttributeSpendingIsCappedByLevel,
	"Cataclysm.Attributes.ACharacterMaySpendOnePointForEveryLevelAndNoMore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttributeSpendingIsCappedByLevel::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmPlayerState* State =
		World->SpawnActor<ACataclysmPlayerState>();
	if (!State)
	{
		AddError(TEXT("a player state would not spawn"));
		return false;
	}

	const int32 Available = State->AttributePointsAvailable();
	TestEqual(TEXT("the points available are the character's level"),
		Available, UCataclysmPlayerClassStats::ChosenLevel());
	TestEqual(TEXT("and none of them are spent to begin with"),
		State->AttributePointsUnspent(), Available);

	FString Reason;

	// SPENDING EVERYTHING IS ALLOWED. The design gives one point per level and
	// expects them spent; a cap that refused the last one would be off by one.
	TestTrue(TEXT("spending every point is allowed"),
		State->SpendAttributePoints(TEXT("vitality"), Available, Reason));
	TestEqual(TEXT("and then nothing is left"),
		State->AttributePointsUnspent(), 0);

	// ONE MORE IS REFUSED.
	TestFalse(TEXT("spending one more than the level grants is refused"),
		State->SpendAttributePoints(TEXT("agility"), 1, Reason));
	TestTrue(TEXT("and the refusal says how many were left"),
		Reason.Contains(TEXT("unspent")));
	TestEqual(TEXT("the refused point was not partly spent"),
		State->GetSpentAttributePoints().Agility, 0);

	// A MISTYPED NAME IS A DIFFERENT REFUSAL, and says so. Reading "you do not
	// have enough points" after typing "vitalty" sends somebody looking for a
	// problem they do not have.
	State->ResetAttributePoints();
	TestFalse(TEXT("an attribute that does not exist is refused"),
		State->SpendAttributePoints(TEXT("vitalty"), 1, Reason));
	TestTrue(FString::Printf(
			TEXT("and the refusal names the eight rather than the count: %s"),
			*Reason),
		Reason.Contains(TEXT("not one of the eight")));

	// RESETTING RETURNS THEM ALL.
	TestTrue(TEXT("spending after a reset works again"),
		State->SpendAttributePoints(TEXT("mind"), 3, Reason));
	State->ResetAttributePoints();
	TestEqual(TEXT("a reset returns every point"),
		State->AttributePointsUnspent(), Available);
	TestEqual(TEXT("and empties every attribute"),
		State->GetSpentAttributePoints().Total(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttributeNamesAreTheDataTablesNames,
	"Cataclysm.Attributes.TheEightNamesMatchTheOnesTheDataTableUses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttributeNamesAreTheDataTablesNames::RunTest(const FString&)
{
	// WHY THIS IS WORTH A TEST. `FCataclysmAttributePoints::Names` is a list
	// written in C++ and `game/Data/Attributes.csv` is generated from the design
	// workbook. A name spelled differently in the two would make that
	// attribute's points reach nothing, silently: PointsIn would answer zero for
	// a row naming an attribute it does not recognise, and every stat that
	// attribute scales would come out at its base.
	const UDataTable* Attributes = UCataclysmPlayerClassStats::LoadAttributeTable();
	if (!Attributes)
	{
		AddError(TEXT("the attribute table would not load"));
		return false;
	}

	const TArray<FString> Known = FCataclysmAttributePoints::Names();
	TestEqual(TEXT("the design has eight primary attributes"), Known.Num(), 8);

	TSet<FString> InTheTable;
	for (const TPair<FName, uint8*>& Row : Attributes->GetRowMap())
	{
		const auto* Effect =
			reinterpret_cast<const FCataclysmAttributeEffectRow*>(Row.Value);
		InTheTable.Add(Effect->Attribute);
	}

	for (const FString& Name : Known)
	{
		TestTrue(FString::Printf(
				TEXT("'%s' is an attribute game/Data/Attributes.csv names"),
				*Name),
			InTheTable.Contains(Name));
	}

	for (const FString& Named : InTheTable)
	{
		TestTrue(FString::Printf(
				TEXT("'%s' from the data table is one of the eight this code "
					 "knows about"), *Named),
			Known.Contains(Named));
	}

	// AND EVERY ONE OF THE EIGHT REACHES A GAMEPLAY ATTRIBUTE. Without this the
	// names could agree perfectly and ApplyTo would still write nothing.
	for (const FString& Name : Known)
	{
		TestTrue(FString::Printf(
				TEXT("'%s' has a gameplay attribute behind it"), *Name),
			UCataclysmPlayerClassStats::StatToAttribute().Contains(Name));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
