// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Components/SphereComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The third channel: a summoner's primary attribute raising its minions' damage. Issue #340.
 *
 * WHAT THE DESIGN SAYS. The decision of 2026-08-06 (issue #209) lets a minion reach its summoner through three
 * channels: its side, its base health and damage raised by the summoner's level, and "increased damage from one
 * primary attribute declared per minion type". `docs/Cataclysm_GDD_v2.md`: Spirit for summoned creatures, Agility
 * for deployed machines, "Each grants 1.0% increased minion damage per point", reaching a minion "by tag" through
 * `game/Data/MinionScaling.csv`. Until this change nothing in the engine read that table.
 *
 * THE RULINGS THESE PIN, made 2026-11-02 by the coordinating session under the owner's delegation:
 *   (a) the figure is the summoner's RESOLVED attribute, gear included, not its spent points, and it lands in the
 *       increased bucket beside gear's increased minion damage, adding rather than multiplying;
 *   (b) a thrall counts as a `Minion.Creature`, so Spirit reaches its blow and its explosion;
 *   (d) an imp's explosion is a share of its blow AS DEALT, so it carries the summoner's minion damage.
 *
 * THROUGH THE REAL `UCataclysmPlayerClassStats::ApplyTo`, NEVER A HAND-BUILT STAT LINE. The channel lives in that
 * function's third pass, so a test that wrote the summoner's `minion_damage` line itself would test nothing that
 * changed. The spent points go in as base overrides through `MergeAttributeBases`, as both of the game's callers do.
 *
 * EVERY EXPECTED FIGURE IS COMPUTED HERE from the authored rows, written out, rather than asked of the engine.
 * `Cataclysm.DataTable` pins both tables, so a row changed underneath these is caught there and named.
 */
namespace CataclysmMinionAttributeTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	//~ `game/Data/MinionTypes.csv`.
	constexpr float ImpBaseDamage = 10.5f;
	constexpr float ImpDamagePerLevel = 10.5f;
	constexpr float BallistaBaseDamage = 63.0f;
	constexpr float BallistaDamagePerLevel = 63.0f;
	constexpr float ImpExplosionPercentOfOwnDamage = 300.0f;

	//~ `game/Data/MinionScaling.csv`: one percent of increased minion damage a point, for both rows.
	constexpr float PercentPerPoint = 1.0f;

	/** The thrall's own figures, as `CataclysmMinionGearTests.cpp` gives its creatures. */
	constexpr float ThrallHealth = 1'000.0f;
	constexpr float ThrallAttackDamage = 100.0f;

	/** The share of its own blow a thrall's explosion deals, stated in `CataclysmMinion.h`. */
	constexpr float ThrallExplosionPercentOfOwnDamage = 300.0f;

	/** A creature's attack damage as its ability system holds it, which is what a thrall's explosion is a share of. */
	float AttackDamageOf(const AActor* Creature)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Creature);
		return System ? System->GetNumericAttribute(Combat::GetAttackDamageAttribute()) : 0.0f;
	}

	/**
	 * The health a target is given: small enough that a blow read as a difference of two readings resolves to well
	 * inside the 0.01 asked for. `CataclysmMinionGearTests.cpp`'s `TargetHealthPool` has the measurement that forced
	 * it: near 1,000,000 a float steps in sixteenths, and 308.7 is not a sixteenth.
	 */
	constexpr float TargetHealthPool = 10'000.0f;

	float RaisedByLevel(float Base, float PerLevel)
	{
		// A SUMMONER HERE HAS NO PLAYER STATE, so its minion takes the level the class stats are previewed at, the
		// path issue #1702 records.
		return Base + PerLevel * static_cast<float>(UCataclysmPlayerClassStats::ChosenLevel());
	}

	/**
	 * A bare actor with the attribute sets a blow needs at both ends, and the primary set when it is to summon.
	 *
	 * THE PRIMARY SET IS NOT OPTIONAL FOR A SUMMONER. `ApplyTo`'s first pass resolves an attribute only when the
	 * component holds a set for it, so a summoner without one has Spirit nought however many points it spent, and
	 * every reading below would be the control's.
	 */
	struct FMinionAttributeFighter
	{
		FMinionAttributeFighter(UWorld* World, const FVector& Where, bool bWithPrimaryAttributes)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);

			// A PAWN-CHANNEL SPHERE AS ITS ROOT, as `CataclysmStatExemptionTests.cpp`'s `FScopedSwinger` has: an
			// explosion finds what it hits by overlapping that channel, and an actor with no root stands at the
			// origin wherever it was spawned.
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

			// Raw pointers: AddAttributeSetSubobject deduces its type from the argument.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist = NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResist = NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAllResist);
			if (bWithPrimaryAttributes)
			{
				UCataclysmPrimaryAttributeSet* NewPrimary = NewObject<UCataclysmPrimaryAttributeSet>(Actor);
				AbilitySystem->AddAttributeSetSubobject(NewPrimary);
			}
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), TargetHealthPool);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), TargetHealthPool);
		}

		~FMinionAttributeFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** One modifier a gear affix would grant, by stat, for `Refresh`. */
	struct FMinionAttributeGearLine
	{
		const TCHAR* Stat;
		ECataclysmStatBucket Bucket;
		float Value;
	};

	/**
	 * Work out a summoner's whole stat line the way the game does: spent points as base overrides, gear as
	 * modifiers, a Ritualist at the default level, pools left as they are. Replaces whatever line it had.
	 */
	void Refresh(const FMinionAttributeFighter& Summoner, const FCataclysmAttributePoints& Points,
				 const TArray<FMinionAttributeGearLine>& Gear = {})
	{
		TMap<FName, float> Bases;
		UCataclysmPlayerClassStats::MergeAttributeBases(Points, Bases);

		TMap<FName, TArray<FCataclysmStatModifier>> Modifiers;
		for (const FMinionAttributeGearLine& Line : Gear)
		{
			FCataclysmStatModifier Modifier;
			Modifier.Bucket = Line.Bucket;
			Modifier.Source = ECataclysmModifierSource::GearAffix;
			Modifier.Value = Line.Value;
			Modifiers.FindOrAdd(FName(Line.Stat)).Add(Modifier);
		}

		UCataclysmPlayerClassStats::ApplyTo(
			Summoner.AbilitySystem, UCataclysmPlayerClassStats::LoadTable(), TEXT("Ritualist"),
			UCataclysmPlayerClassStats::DefaultLevel, &Modifiers, ECataclysmPoolFill::LeaveAsTheyAre, &Bases);
	}

	FCataclysmAttributePoints SpiritOf(int32 Points)
	{
		FCataclysmAttributePoints Spent;
		Spent.Spirit = Points;
		return Spent;
	}

	FCataclysmAttributePoints AgilityOf(int32 Points)
	{
		FCataclysmAttributePoints Spent;
		Spent.Agility = Points;
		return Spent;
	}

	/**
	 * A minion of the authored type, or null with the reason: a minion whose row could not be found carries no
	 * figures, so every reading below would fail for a reason that has nothing to do with the channel.
	 */
	ACataclysmMinion* Summon(FAutomationTestBase& Test, AActor* Summoner, const FVector& Where, const TCHAR* Type)
	{
		ACataclysmMinion* Minion = ACataclysmMinion::Spawn(Summoner, Where, /*Lifetime=*/20.0f, /*bBurns=*/false, Type);
		if (!Test.TestNotNull(TEXT("set-up: a minion"), Minion))
		{
			return nullptr;
		}
		if (Minion->TypeName != FString(Type))
		{
			Test.AddError(TEXT("set-up: DT_MinionTypes could not supply the row. Run tools/generate_datatable_assets.py."));
			return nullptr;
		}
		return Minion;
	}

	/** What one swing of `Minion` took from `Target`. */
	float SwingAt(ACataclysmMinion* Minion, const FMinionAttributeFighter& Target)
	{
		const float Before = Target.Health();
		Minion->AttackTarget(Target.Actor);
		return Before - Target.Health();
	}

	/** A creature at Common with its own figures, that nobody commands yet. */
	ACataclysmEnemyCharacter* Creature(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetHealth(ThrallHealth);
			Made->SetAttackDamage(ThrallAttackDamage);
		}
		return Made;
	}

	/** What one blow from `Striker` at 100% of its weapon took from `Target`. */
	float HitFrom(AActor* Striker, const FMinionAttributeFighter& Target)
	{
		const float Before = Target.Health();
		UCataclysmSkillEffects::ApplyHit(Striker, Target.Actor, 100.0f);
		return Before - Target.Health();
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY: the module is built as a unity blob, and a `using namespace` at
// file scope reaches the files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionSpiritRaisesAnImpTest,
	"Cataclysm.MinionStats.SpiritRaisesAnImpsBlowByOnePercentAPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The channel itself. THE SAME IMP BEFORE AND AFTER, so a build where an imp deals nothing fails the control rather
 * than passing the "does not change" half, and the reading is live: the summoner's line is worked out again between
 * the two swings, as a player spending points does.
 */
bool FCataclysmMinionSpiritRaisesAnImpTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttributeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionAttributeFighter Summoner(World, FVector::ZeroVector, /*bWithPrimaryAttributes=*/true);
	FMinionAttributeFighter Target(World, FVector(2 * M, 0, 0), /*bWithPrimaryAttributes=*/false);
	ACataclysmMinion* Imp = Summon(*this, Summoner.Actor, FVector(1 * M, 0, 0), TEXT("Imp"));
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	const float ItsOwn = RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel);

	Refresh(Summoner, SpiritOf(0));
	TestEqual(TEXT("control: with no Spirit an imp deals its own figure"), SwingAt(Imp, Target), ItsOwn, 0.01f);

	Refresh(Summoner, SpiritOf(40));
	TestEqual(TEXT("forty Spirit raises its blow by forty per cent"), SwingAt(Imp, Target),
			  ItsOwn * (1.0f + 40.0f * PercentPerPoint / 100.0f), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionSpiritWithGearTest,
	"Cataclysm.MinionStats.TheSummonersSpiritIsReadWithItsGearNotItsPointsAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruling (a): the resolved attribute and not the spent points. Forty points increased by 25% is fifty Spirit, so the
 * blow is raised by fifty per cent; read from the points it would be forty, which is what the message names.
 */
bool FCataclysmMinionSpiritWithGearTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttributeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionAttributeFighter Summoner(World, FVector::ZeroVector, /*bWithPrimaryAttributes=*/true);
	FMinionAttributeFighter Target(World, FVector(2 * M, 0, 0), /*bWithPrimaryAttributes=*/false);
	ACataclysmMinion* Imp = Summon(*this, Summoner.Actor, FVector(1 * M, 0, 0), TEXT("Imp"));
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	Refresh(Summoner, SpiritOf(40), {{TEXT("spirit"), ECataclysmStatBucket::Increased, 25.0f}});
	TestEqual(TEXT("set-up: forty points increased by 25% resolve to fifty Spirit"),
			  Summoner.AbilitySystem->GetNumericAttribute(UCataclysmPrimaryAttributeSet::GetSpiritAttribute()),
			  50.0f, 0.001f);
	TestEqual(TEXT("fifty Spirit, not the forty points spent, raises the imp's blow by fifty per cent"),
			  SwingAt(Imp, Target),
			  RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel) * (1.0f + 50.0f * PercentPerPoint / 100.0f), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionSpiritAddsToGearTest,
	"Cataclysm.MinionStats.SpiritAndIncreasedMinionDamageAddRatherThanMultiply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruling (a) again, and the design's "an attribute's contribution and an affix's contribution add. They cannot
 * multiply each other". Forty Spirit and 25% increased minion damage is 65% increased, 1.65; multiplied it would be
 * 1.4 x 1.25 = 1.75.
 */
bool FCataclysmMinionSpiritAddsToGearTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttributeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionAttributeFighter Summoner(World, FVector::ZeroVector, /*bWithPrimaryAttributes=*/true);
	FMinionAttributeFighter Target(World, FVector(2 * M, 0, 0), /*bWithPrimaryAttributes=*/false);
	ACataclysmMinion* Imp = Summon(*this, Summoner.Actor, FVector(1 * M, 0, 0), TEXT("Imp"));
	if (!Imp)
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	Refresh(Summoner, SpiritOf(40), {{TEXT("minion_damage"), ECataclysmStatBucket::Increased, 25.0f}});
	TestEqual(TEXT("forty Spirit and 25% increased minion damage add to 65% increased"),
			  SwingAt(Imp, Target), RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel) * 1.65f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionAgilityRaisesAMachineTest,
	"Cataclysm.MinionStats.AgilityRaisesAMachineAndNotAnImp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The scoping by tag. The design: "It is also what stops one attribute raising the wrong minion". One summoner with
 * thirty Agility; its ballista is a `Minion.Machine` and is raised by thirty per cent, its imp a `Minion.Creature`
 * and is not raised at all. THE IMP IS THE CONTROL for an unscoped increase, which would raise both.
 */
bool FCataclysmMinionAgilityRaisesAMachineTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttributeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionAttributeFighter Summoner(World, FVector::ZeroVector, /*bWithPrimaryAttributes=*/true);
	FMinionAttributeFighter Target(World, FVector(2 * M, 0, 0), /*bWithPrimaryAttributes=*/false);
	ACataclysmMinion* Imp = Summon(*this, Summoner.Actor, FVector(1 * M, 0, 0), TEXT("Imp"));
	ACataclysmMinion* Ballista = Summon(*this, Summoner.Actor, FVector(0, 3 * M, 0), TEXT("Ballista"));
	if (!Imp || !Ballista)
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		for (ACataclysmMinion* One : {Imp, Ballista})
		{
			if (IsValid(One))
			{
				One->Destroy();
			}
		}
	};

	Refresh(Summoner, AgilityOf(30));
	TestEqual(TEXT("thirty Agility multiplies a machine's minion damage by 1.30"),
			  UCataclysmCommand::SummonerMultiplierFor(Summoner.Actor, TEXT("minion_damage"), Ballista->TypeTags),
			  1.0f + 30.0f * PercentPerPoint / 100.0f, 0.0001f);
	TestEqual(TEXT("and the ballista's blow is thirty per cent above its own figure"), SwingAt(Ballista, Target),
			  RaisedByLevel(BallistaBaseDamage, BallistaDamagePerLevel) * (1.0f + 30.0f * PercentPerPoint / 100.0f),
			  0.01f);
	TestEqual(TEXT("an imp's minion damage is not raised by Agility"),
			  UCataclysmCommand::SummonerMultiplierFor(Summoner.Actor, TEXT("minion_damage"), Imp->TypeTags),
			  1.0f, 0.0001f);
	TestEqual(TEXT("and the imp's blow is its own figure"), SwingAt(Imp, Target),
			  RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel), 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionSpiritRaisesAThrallTest,
	"Cataclysm.MinionStats.SpiritRaisesAThrallsBlowAndItsExplosionAndAgilityDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruling (b): a thrall counts as a `Minion.Creature`, so its commander's Spirit reaches its blow and its explosion,
 * and Agility reaches neither. THREE CONTROLS: the thrall's blow before its commander has any attribute, an unowned
 * creature's blow beside it, and a commander with Agility instead, which an unscoped increase would let through.
 */
bool FCataclysmMinionSpiritRaisesAThrallTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttributeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionAttributeFighter Commander(World, FVector(0, 10 * M, 0), /*bWithPrimaryAttributes=*/true);
	FMinionAttributeFighter Target(World, FVector(0, -10 * M, 0), /*bWithPrimaryAttributes=*/false);
	ACataclysmEnemyCharacter* Thrall = Creature(World, FVector(0, 0, 0));
	ACataclysmEnemyCharacter* Unowned = Creature(World, FVector(5 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a creature to take"), Thrall)
		|| !TestNotNull(TEXT("set-up: a creature nobody takes"), Unowned))
	{
		return false;
	}

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	const float Own = HitFrom(Thrall, Target);
	if (!TestTrue(TEXT("set-up: the creature's own blow takes health"), Own > 0.0f)
		|| !TestTrue(TEXT("set-up: Subjugate takes it"), UCataclysmCommand::Subjugate(Commander.Actor, Thrall)))
	{
		return false;
	}

	// THE EXPLOSION NEEDS THE FLAG, so every refresh below carries it.
	const TArray<FMinionAttributeGearLine> Explodes = {
		{TEXT("minion_explodes_on_death"), ECataclysmStatBucket::Flat, 1.0f}};
	// ITS ATTACK DAMAGE AS ITS ABILITY SYSTEM HOLDS IT, the figure `ThrallExplosionDamage` takes a share of. Read
	// rather than assumed to be the 100 it was given, since a creature's figures pass through its rung.
	const float ItsAttackDamage = AttackDamageOf(Thrall);
	if (!TestTrue(TEXT("set-up: the thrall has an attack damage to take a share of"), ItsAttackDamage > 0.0f))
	{
		return false;
	}
	const float OwnExplosion = ItsAttackDamage * ThrallExplosionPercentOfOwnDamage / 100.0f;

	Refresh(Commander, AgilityOf(40), Explodes);
	TestEqual(TEXT("control: its commander's forty Agility leaves its blow as it was"), HitFrom(Thrall, Target), Own,
			  0.01f);
	TestEqual(TEXT("control: and its explosion at three of its own blows"),
			  ACataclysmMinion::ThrallExplosionDamage(Thrall, Commander.Actor), OwnExplosion, 0.01f);

	Refresh(Commander, SpiritOf(40), Explodes);
	TestEqual(TEXT("its commander's forty Spirit raises its blow by forty per cent"), HitFrom(Thrall, Target),
			  Own * (1.0f + 40.0f * PercentPerPoint / 100.0f), 0.01f);
	TestEqual(TEXT("and its explosion by forty per cent"),
			  ACataclysmMinion::ThrallExplosionDamage(Thrall, Commander.Actor),
			  OwnExplosion * (1.0f + 40.0f * PercentPerPoint / 100.0f), 0.01f);
	TestEqual(TEXT("control: an unowned creature's blow is unchanged"), HitFrom(Unowned, Target), Own, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionScalingRowsTest,
	"Cataclysm.MinionStats.TheMinionScalingTableGivesOneTagScopedIncreasePerRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmClassStats::MinionScalingModifiersForValues` on the shipped table, alone. Each row becomes one
 * increase from an attribute, worth the attribute times its percent per point, requiring the row's tag; a stat no
 * row names gets none, and an attribute at nought adds nothing rather than an increase of nothing.
 */
bool FCataclysmMinionScalingRowsTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttributeTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadMinionScalingTable();
	if (!TestNotNull(TEXT("set-up: DT_MinionScaling loads"), Table))
	{
		return false;
	}
	const FGameplayTag Creature = FGameplayTag::RequestGameplayTag(FName(TEXT("Minion.Creature")), false);
	const FGameplayTag Machine = FGameplayTag::RequestGameplayTag(FName(TEXT("Minion.Machine")), false);
	if (!TestTrue(TEXT("set-up: both family tags are registered"), Creature.IsValid() && Machine.IsValid()))
	{
		return false;
	}

	FCataclysmAttributeValues Values;
	Values.Spirit = 40.0f;
	Values.Agility = 30.0f;

	TArray<FCataclysmStatModifier> Damage;
	UCataclysmClassStats::MinionScalingModifiersForValues(Table, Values, TEXT("minion_damage"), Damage);
	if (!TestEqual(TEXT("Spirit and Agility each give minion damage one increase"), Damage.Num(), 2))
	{
		return false;
	}
	for (const FCataclysmStatModifier& One : Damage)
	{
		const bool bCreature = One.RequiredTags.HasTagExact(Creature);
		const bool bMachine = One.RequiredTags.HasTagExact(Machine);
		TestTrue(TEXT("each requires exactly one family tag"), (bCreature != bMachine) && One.RequiredTags.Num() == 1);
		TestTrue(TEXT("each is an increase from an attribute"),
				 One.Bucket == ECataclysmStatBucket::Increased && One.Source == ECataclysmModifierSource::Attribute);
		TestEqual(TEXT("a creature's is forty, a machine's thirty"), One.Value, bCreature ? 40.0f : 30.0f, 0.0001f);
	}

	TArray<FCataclysmStatModifier> Health;
	UCataclysmClassStats::MinionScalingModifiersForValues(Table, Values, TEXT("minion_health"), Health);
	TestEqual(TEXT("no row names minion health, so it gets none"), Health.Num(), 0);

	FCataclysmAttributeValues SpiritOnly;
	SpiritOnly.Spirit = 40.0f;
	TArray<FCataclysmStatModifier> OneRow;
	UCataclysmClassStats::MinionScalingModifiersForValues(Table, SpiritOnly, TEXT("minion_damage"), OneRow);
	TestEqual(TEXT("with no Agility the machine row adds nothing at all"), OneRow.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionExplosionAsDealtTest,
	"Cataclysm.MinionStats.AnImpsExplosionIsAShareOfItsBlowAsDealt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruling (d): an imp's explosion is three of its blows AS DEALT, so its summoner's increased minion damage raises it
 * as it raises the swing. Until this change `Explode` priced it from the type row's figure alone, and its comment said
 * the multiplier was "in the blow already", which it was not.
 *
 * GEAR RATHER THAN SPIRIT, so this pins ruling (d) alone and a break in the attribute channel does not reach it. TWO
 * SUMMONERS A HUNDRED METRES APART, each with an imp a metre away and a target a metre past it: the control's imp
 * explodes for three of its own figure, the other for three of that figure raised by 25%.
 */
bool FCataclysmMinionExplosionAsDealtTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttributeTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionAttributeFighter Plain(World, FVector::ZeroVector, /*bWithPrimaryAttributes=*/true);
	FMinionAttributeFighter PlainTarget(World, FVector(2 * M, 0, 0), /*bWithPrimaryAttributes=*/false);
	FMinionAttributeFighter Raised(World, FVector(0, 100 * M, 0), /*bWithPrimaryAttributes=*/true);
	FMinionAttributeFighter RaisedTarget(World, FVector(2 * M, 100 * M, 0), /*bWithPrimaryAttributes=*/false);
	Refresh(Plain, SpiritOf(0));
	Refresh(Raised, SpiritOf(0), {{TEXT("minion_damage"), ECataclysmStatBucket::Increased, 25.0f}});

	ACataclysmMinion* PlainImp = Summon(*this, Plain.Actor, FVector(1 * M, 0, 0), TEXT("Imp"));
	ACataclysmMinion* RaisedImp = Summon(*this, Raised.Actor, FVector(1 * M, 100 * M, 0), TEXT("Imp"));
	if (!PlainImp || !RaisedImp)
	{
		return false;
	}
	PlainImp->RecordExplosionRadius(3.0f * M);
	RaisedImp->RecordExplosionRadius(3.0f * M);

	const float OwnExplosion =
		RaisedByLevel(ImpBaseDamage, ImpDamagePerLevel) * ImpExplosionPercentOfOwnDamage / 100.0f;

	PlainImp->Explode();
	RaisedImp->Explode();

	TestEqual(TEXT("control: with no minion damage an imp explodes for three of its own blows"),
			  TargetHealthPool - PlainTarget.Health(), OwnExplosion, 0.01f);
	TestEqual(TEXT("with 25% increased minion damage it explodes for three of its blows as dealt, 25% more"),
			  TargetHealthPool - RaisedTarget.Health(), OwnExplosion * 1.25f, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
