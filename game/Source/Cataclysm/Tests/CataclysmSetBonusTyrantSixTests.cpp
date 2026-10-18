// Copyright Stephen Dubois. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tyrant's Chains' six-piece bonus, the mechanism. Owner's ruling #3 of 2026-10-17 and the coordinating session's
 * rulings of 2026-10-18 (Q1-Q5).
 *
 * "When a pet or minion dies, it explodes, dealing 50% of its maximum health as Death damage to all nearby enemies.
 * Additionally, a new, powerful Tyrant's Minion is summoned to fight for you for a short duration"
 *
 * THE ROWS ARE GIVEN AS STAT LINES, IN THE SHAPE THE ENTRY GIVES THE ENCHANTMENT SESSION: Shared Ruin's two stats at a
 * flat 50 and 5, `minion_death_blast_deals_death` at a flat 1 and `minion_death_summons_tyrants_minion_seconds` at a
 * flat 10, each with no tags. The real rows, worn at six pieces with a control at five, are the enchantment session's
 * test.
 *
 * EVERY BODY HERE IS A BARE ACTOR, as the Shared Ruin tests in `CataclysmCommandTests.cpp` use: a sphere a search can
 * find, an ability system, health, and a resistance set, so a Death blow can be told from an untyped one by the Death
 * resistance it meets. Its health pool is large so a blast never kills it. Deaths are health written to nought, which
 * is how anything else dies. Time is moved with `CataclysmTestWorld::RunClock`, which fires the engine's lifespan
 * timers.
 *
 * THE TYRANT'S MINION IS MADE FROM THE IMP'S ROW FOR NOW (the stand-in `docs/DECISIONS.md` records), and nothing here
 * asserts the Imp's numbers as its own: every figure is a share of the dying creature's own maximum health, read from
 * it.
 */
namespace CataclysmSetBonusTyrantSixTest
{
	using Vital = UCataclysmVitalAttributeSet;

	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** A body's health, large enough that no blast here empties it. */
	constexpr float BodyHealth = 100000.0f;

	/** The ruled figures: 50% of the dying creature's maximum health, within 5 m, and 10 s. */
	constexpr float BlastPercent = 50.0f;
	constexpr float BlastMetres = 5.0f;
	constexpr float TyrantsSeconds = 10.0f;

	/** How far a float clock may fall from a figure it was built to equal, in seconds: one step of `RunClock`. */
	constexpr float ClockAllowanceSeconds = 0.06f;

	/** A bare actor a sphere search can find, holding health, a class resource pool and the eight resistances. */
	struct FScopedBody
	{
		FScopedBody(UWorld* World, const FVector& Where)
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
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(Vital::GetMaxHealthAttribute(), BodyHealth);
			Set(Vital::GetHealthAttribute(), BodyHealth);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
		}

		~FScopedBody()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		/** What it has lost since it was made. */
		float Lost() const
		{
			return BodyHealth - AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Give a summoner its whole stat line at once, each a flat figure, as `CataclysmCommandTests.cpp`
	 * gives them. WHOLESALE, because
	 * `SetStatInputs` replaces what was there. A figure of nought is left out, as a row not worn would be.
	 */
	void GiveFlats(FScopedBody& Who, const TMap<FName, float>& Flats)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		for (const TPair<FName, float>& Each : Flats)
		{
			if (Each.Value == 0.0f)
			{
				continue;
			}
			FCataclysmStatModifier Modifier;
			Modifier.Bucket = ECataclysmStatBucket::Flat;
			Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
			Modifier.Value = Each.Value;

			FCataclysmStatInputs Inputs;
			Inputs.Base = 0.0f;
			Inputs.Modifiers.Add(Modifier);
			Stats.Add(Each.Key, Inputs);
		}
		Who.AbilitySystem->SetStatInputs(MoveTemp(Stats));
	}

	/** The four rows of the bonus as stat lines, with the Death flag and the seconds as the case asks. */
	void GiveTheBonus(FScopedBody& Who, bool bDeath, float Seconds)
	{
		GiveFlats(Who, {
			{FName(ACataclysmMinion::DeathBlastPercentOfMaximumHealthStat), BlastPercent},
			{FName(ACataclysmMinion::DeathBlastRadiusMetresStat), BlastMetres},
			{FName(ACataclysmMinion::DeathBlastDealsDeathStat), bDeath ? 1.0f : 0.0f},
			{FName(ACataclysmMinion::TyrantsMinionSecondsStat), Seconds}});
	}

	/** An imp of the authored type, standing where the case says, lasting a minute. */
	ACataclysmMinion* ImpOf(FScopedBody& Summoner, const FVector& Where)
	{
		return ACataclysmMinion::Spawn(Summoner.Actor, Where, /*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
	}

	/** Write a minion's health to nothing, which is how anything else dies. */
	void Kill(ACataclysmMinion* Minion)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Minion))
		{
			System->SetNumericAttributeBase(Vital::GetHealthAttribute(), 0.0f);
		}
	}

	float MaximumHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Who);
		return System ? System->GetNumericAttribute(Vital::GetMaxHealthAttribute()) : 0.0f;
	}

	float HealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Who);
		return System ? System->GetNumericAttribute(Vital::GetHealthAttribute()) : 0.0f;
	}

	/** How many Tyrant's Minions `Commander` has standing, counted rather than asked of the one-at-most helper. */
	int32 TyrantsStanding(const AActor* Commander)
	{
		int32 Count = 0;
		for (AActor* Follower : UCataclysmCommand::ThingsCommandedBy(Commander))
		{
			const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
			if (IsValid(Minion) && Minion->bIsTyrantsMinion)
			{
				++Count;
			}
		}
		return Count;
	}

	/** Destroy whatever is still standing at the end of a case. */
	void Clear(const TArray<ACataclysmMinion*>& Minions)
	{
		for (ACataclysmMinion* Minion : Minions)
		{
			if (IsValid(Minion))
			{
				Minion->Destroy();
			}
		}
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a `using
// namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantSixBlastTest,
	"Cataclysm.SetBonus.Tyrant6.ADyingMinionDealsHalfItsMaximumHealthAsDeathWithinFiveMetresAndNothingAtSix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The blast, Q1: Death damage by the new flag, on Shared Ruin's two stats at 50 and 5.
 *
 * THE CONTROL IS READ FIRST, A HUNDRED METRES AWAY: a summoner holding the same two figures and no flag loses an imp,
 * and a target four metres from it holding 50 Death resistance loses half the imp's maximum health whole, because the
 * blow is untyped and meets no Death resistance.
 *
 * THEN THE BONUS: three targets of the second imp, one at 4 m holding no resistance, one at 4 m holding 50 Death
 * resistance, and one at 6 m. The first loses half the imp's maximum health; the second a quarter, which is the
 * Death resistance met; the third nothing.
 */
bool FCataclysmTyrantSixBlastTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusTyrantSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayAttribute DeathResistance = UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute();

	FScopedBody Plain(World, FVector(0, 100 * M, 0));
	GiveTheBonus(Plain, /*bDeath=*/false, /*Seconds=*/0.0f);
	FScopedBody PlainResisting(World, FVector(14 * M, 100 * M, 0));
	PlainResisting.Set(DeathResistance, 50.0f);

	FScopedBody Summoner(World, FVector::ZeroVector);
	GiveTheBonus(Summoner, /*bDeath=*/true, /*Seconds=*/0.0f);
	FScopedBody Inside(World, FVector(14 * M, 0, 0));
	FScopedBody InsideResisting(World, FVector(10 * M, 4 * M, 0));
	InsideResisting.Set(DeathResistance, 50.0f);
	FScopedBody Outside(World, FVector(16 * M, 0, 0));

	ACataclysmMinion* PlainImp = ImpOf(Plain, FVector(10 * M, 100 * M, 0));
	ACataclysmMinion* Imp = ImpOf(Summoner, FVector(10 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: the control's imp"), PlainImp) || !TestNotNull(TEXT("set-up: an imp"), Imp))
	{
		return false;
	}
	ON_SCOPE_EXIT { Clear({PlainImp, Imp}); };

	const float Maximum = MaximumHealthOf(Imp);
	if (!TestTrue(FString::Printf(TEXT("set-up: the imp has a maximum health: %.2f"), Maximum), Maximum > 0.0f)
		|| !TestEqual(TEXT("set-up: the two imps have the same maximum health"), MaximumHealthOf(PlainImp), Maximum,
					  0.01f))
	{
		return false;
	}

	Kill(PlainImp);
	TestEqual(TEXT("control: with no flag the blast is untyped and lands whole on a target holding Death resistance"),
			  PlainResisting.Lost(), Maximum * 0.5f, 0.01f);

	Kill(Imp);
	TestEqual(TEXT("a target 4 m away holding no resistance loses half the imp's maximum health"),
			  Inside.Lost(), Maximum * 0.5f, 0.01f);
	TestEqual(TEXT("one 4 m away holding 50 Death resistance loses a quarter, so the blast is Death damage"),
			  InsideResisting.Lost(), Maximum * 0.25f, 0.01f);
	TestEqual(TEXT("one 6 m away loses nothing"), Outside.Lost(), 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantSixSummonTest,
	"Cataclysm.SetBonus.Tyrant6.ADyingMinionSummonsATyrantsMinionOutsideTheCapsThatIsGoneAfterTenSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The summon, Q2 and Q4: a minion's death summons a Tyrant's Minion where it died, outside the summon caps, for the
 * row's 10 s. THE CONTROL IS READ FIRST: before the death the summoner commands no Tyrant's Minion. After 9.5 s it
 * still stands, and a second later it is gone.
 */
bool FCataclysmTyrantSixSummonTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusTyrantSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedBody Summoner(World, FVector::ZeroVector);
	GiveTheBonus(Summoner, /*bDeath=*/true, TyrantsSeconds);

	const FVector Where(3 * M, 0, 0);
	ACataclysmMinion* Imp = ImpOf(Summoner, Where);
	if (!TestNotNull(TEXT("set-up: an imp"), Imp))
	{
		return false;
	}
	ON_SCOPE_EXIT { Clear({Imp, ACataclysmMinion::TyrantsMinionOf(Summoner.Actor)}); };

	TestEqual(TEXT("control: before any death the summoner has no Tyrant's Minion"),
			  TyrantsStanding(Summoner.Actor), 0);

	Kill(Imp);
	ACataclysmMinion* Tyrants = ACataclysmMinion::TyrantsMinionOf(Summoner.Actor);
	if (!TestNotNull(TEXT("the imp's death summons a Tyrant's Minion"), Tyrants))
	{
		return false;
	}
	TestEqual(TEXT("one, and only one"), TyrantsStanding(Summoner.Actor), 1);
	TestTrue(TEXT("it is the summoner's"), Tyrants->Summoner == Summoner.Actor);
	TestEqual(TEXT("it stands where the imp died"),
			  static_cast<float>(FVector::Dist(Tyrants->GetActorLocation(), Where)), 0.0f, 1.0f);
	TestTrue(TEXT("it holds no place under a summon cap"), Tyrants->bOutsideSummonCaps);
	TestEqual(TEXT("so no cap count of its type counts it"),
			  UCataclysmCommand::MinionsOfTypeCommandedBy(Summoner.Actor, Tyrants->TypeName).Num(), 0);
	TestEqual(TEXT("it is given the row's ten seconds"), Tyrants->GetLifeSpan(), TyrantsSeconds,
			  ClockAllowanceSeconds);

	CataclysmTestWorld::RunClock(World, 9.5f);
	TestTrue(TEXT("after 9.5 s it still stands"), IsValid(Tyrants));

	CataclysmTestWorld::RunClock(World, 1.0f);
	TestFalse(TEXT("and a second later it is gone"), IsValid(Tyrants));
	TestEqual(TEXT("leaving the summoner none"), TyrantsStanding(Summoner.Actor), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantSixOwnDeathTest,
	"Cataclysm.SetBonus.Tyrant6.ATyrantsMinionsOwnDeathExplodesAndSummonsNoneAndIsNotReplaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruling #3 and Q4: the Tyrant's Minion's own death explodes, summons none, and is not replaced.
 *
 * THE CONTROL IS READ FIRST, A HUNDRED METRES AWAY: a summoner holding a Summon Imp skill and Press-Ganged
 * (`minion_death_replaced_every_seconds`) loses an imp no skill made, and `ReplaceLost` replaces it with one of the
 * skill's imps. That is what would happen to a Tyrant's Minion, which no skill makes either, if its death were handed
 * to the replacement paths.
 *
 * THEN THE BONUS, beside the same skill and Press-Ganged: a Tyrant's Minion summoned 3 m from a target is killed. The
 * target loses half the Tyrant's Minion's maximum health, no Tyrant's Minion stands afterwards, and the skill holds no
 * replacement.
 */
bool FCataclysmTyrantSixOwnDeathTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusTyrantSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// SUMMON IMP'S OWN FIGURES, as `CataclysmCommandTests.cpp` grants them for Press-Ganged.
	const auto GrantSummon = [](FScopedBody& Who) -> UCataclysmSummonSkill*
	{
		const FGameplayAbilitySpecHandle Handle = Who.AbilitySystem->GiveAbilityInSlot(
			UCataclysmSummonSkill::StaticClass(), ECataclysmAbilitySlot::Special, /*Level=*/100, Who.Actor);
		FGameplayAbilitySpec* Spec = Who.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UCataclysmSummonSkill* Skill = Spec ? Cast<UCataclysmSummonSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = TEXT("Summon Imp");
			Skill->Params = UCataclysmSkillShapes::ParseParams(
				TEXT("Count=1; MaxActive=3; Duration=20; Radius=3; Minions=Imp:1"));
		}
		return Skill;
	};

	FScopedBody Control(World, FVector(0, 100 * M, 0));
	UCataclysmSummonSkill* ControlSkill = GrantSummon(Control);
	GiveFlats(Control, {{FName(UCataclysmSummonSkill::ReplacedOnDeathStat), 10.0f}});

	FScopedBody Summoner(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Skill = GrantSummon(Summoner);
	GiveFlats(Summoner, {
		{FName(ACataclysmMinion::DeathBlastPercentOfMaximumHealthStat), BlastPercent},
		{FName(ACataclysmMinion::DeathBlastRadiusMetresStat), BlastMetres},
		{FName(ACataclysmMinion::DeathBlastDealsDeathStat), 1.0f},
		{FName(ACataclysmMinion::TyrantsMinionSecondsStat), TyrantsSeconds},
		{FName(UCataclysmSummonSkill::ReplacedOnDeathStat), 10.0f}});
	FScopedBody Target(World, FVector(30 * M, 0, 0));

	if (!TestNotNull(TEXT("set-up: the control's summon skill"), ControlSkill)
		|| !TestNotNull(TEXT("set-up: a summon skill"), Skill))
	{
		return false;
	}

	ACataclysmMinion* Unmade = ImpOf(Control, FVector(3 * M, 100 * M, 0));
	if (!TestNotNull(TEXT("set-up: the control's imp"), Unmade))
	{
		return false;
	}
	// SUMMONED BY THE CALL THE DEATH HOOK MAKES, AND NOT BY A DEATH, so Press-Ganged's one replacement in ten seconds
	// is unspent when the Tyrant's Minion dies: "not replaced" below cannot be that clock's refusal.
	ACataclysmMinion* Tyrants = ACataclysmMinion::SummonTyrantsMinion(
		Summoner.Actor, /*bLostWasTyrantsMinion=*/false, FVector(27 * M, 0, 0));
	ON_SCOPE_EXIT
	{
		TArray<ACataclysmMinion*> Left = {Unmade, Tyrants};
		for (UCataclysmSummonSkill* Each : {ControlSkill, Skill})
		{
			for (const TObjectPtr<ACataclysmMinion>& Made : Each->Minions)
			{
				Left.Add(Made.Get());
			}
		}
		Clear(Left);
	};

	Kill(Unmade);
	TestEqual(TEXT("control: Press-Ganged replaces a dying imp that no skill made with one of the skill's"),
			  ControlSkill->LivingMinionCount(), 1);

	if (!TestNotNull(TEXT("set-up: a Tyrant's Minion 3 m from the target"), Tyrants)
		|| !TestEqual(TEXT("set-up: the skill holds no imp"), Skill->LivingMinionCount(), 0))
	{
		return false;
	}
	const float Maximum = MaximumHealthOf(Tyrants);
	if (!TestTrue(FString::Printf(TEXT("set-up: the Tyrant's Minion has a maximum health: %.2f"), Maximum),
				  Maximum > 0.0f))
	{
		return false;
	}
	Kill(Tyrants);

	TestEqual(TEXT("its own death explodes: a target 3 m away loses half its maximum health, as Death"),
			  Target.Lost(), Maximum * 0.5f, 0.01f);
	TestEqual(TEXT("and it summons none"), TyrantsStanding(Summoner.Actor), 0);
	TestEqual(TEXT("and Press-Ganged does not replace it"), Skill->LivingMinionCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantSixRefreshTest,
	"Cataclysm.SetBonus.Tyrant6.ASecondDeathWhileOneStandsRestartsItsTimeKeepsItsHealthAndMakesNoSecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruling #3 and Q5: at most one; a new summon restarts the time of the one standing at full and keeps its health.
 *
 * THE CONTROL IS READ FIRST: six seconds after it was summoned the Tyrant's Minion has four left. Then a second imp
 * dies. The same Tyrant's Minion stands, no second is made, its health is what it was a moment before, and its time
 * is ten seconds again. Six seconds later it still stands, which it would not have without the refresh, and four and
 * a half after that it is gone.
 */
bool FCataclysmTyrantSixRefreshTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusTyrantSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedBody Summoner(World, FVector::ZeroVector);
	GiveTheBonus(Summoner, /*bDeath=*/true, TyrantsSeconds);

	ACataclysmMinion* First = ImpOf(Summoner, FVector(3 * M, 0, 0));
	ACataclysmMinion* Second = ImpOf(Summoner, FVector(0, 3 * M, 0));
	if (!TestNotNull(TEXT("set-up: a first imp"), First) || !TestNotNull(TEXT("set-up: a second imp"), Second))
	{
		return false;
	}
	ON_SCOPE_EXIT { Clear({First, Second, ACataclysmMinion::TyrantsMinionOf(Summoner.Actor)}); };

	Kill(First);
	ACataclysmMinion* Tyrants = ACataclysmMinion::TyrantsMinionOf(Summoner.Actor);
	if (!TestNotNull(TEXT("set-up: the first imp's death summons a Tyrant's Minion"), Tyrants))
	{
		return false;
	}
	// HURT, so "keeps its health" is not a full pool compared with a full pool.
	UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Tyrants);
	Its->SetNumericAttributeBase(Vital::GetHealthAttribute(), MaximumHealthOf(Tyrants) * 0.5f);

	CataclysmTestWorld::RunClock(World, 6.0f);
	TestEqual(TEXT("control: six seconds on it has four left"), Tyrants->GetLifeSpan(), TyrantsSeconds - 6.0f,
			  ClockAllowanceSeconds);
	const float HealthBefore = HealthOf(Tyrants);

	Kill(Second);
	TestTrue(TEXT("the second death leaves the same Tyrant's Minion standing"),
			 ACataclysmMinion::TyrantsMinionOf(Summoner.Actor) == Tyrants);
	TestEqual(TEXT("and makes no second"), TyrantsStanding(Summoner.Actor), 1);
	TestEqual(TEXT("its health is kept"), HealthOf(Tyrants), HealthBefore, 0.01f);
	TestEqual(TEXT("its time restarts at the full ten seconds"), Tyrants->GetLifeSpan(), TyrantsSeconds,
			  ClockAllowanceSeconds);

	CataclysmTestWorld::RunClock(World, 6.0f);
	TestTrue(TEXT("six seconds later it still stands, two past where its first time ended"), IsValid(Tyrants));

	CataclysmTestWorld::RunClock(World, 4.5f);
	TestFalse(TEXT("and it is gone once the restarted ten seconds have run"), IsValid(Tyrants));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTyrantSixNoughtTest,
	"Cataclysm.SetBonus.Tyrant6.WithTheSecondsAtNoughtTheBlastStillLandsAndNothingIsSummoned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Nought seconds means no summon. THE CONTROL IS READ FIRST, A HUNDRED METRES AWAY: a summoner holding the seconds at
 * 10 loses an imp and has a Tyrant's Minion. The other holds the blast and the flag and no seconds: its imp's blast
 * lands on a target 3 m away, so the death reached the hook, and no Tyrant's Minion is summoned.
 */
bool FCataclysmTyrantSixNoughtTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusTyrantSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedBody Control(World, FVector(0, 100 * M, 0));
	GiveTheBonus(Control, /*bDeath=*/true, TyrantsSeconds);
	FScopedBody Summoner(World, FVector::ZeroVector);
	GiveTheBonus(Summoner, /*bDeath=*/true, /*Seconds=*/0.0f);
	FScopedBody Target(World, FVector(13 * M, 0, 0));

	ACataclysmMinion* ControlImp = ImpOf(Control, FVector(10 * M, 100 * M, 0));
	ACataclysmMinion* Imp = ImpOf(Summoner, FVector(10 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: the control's imp"), ControlImp) || !TestNotNull(TEXT("set-up: an imp"), Imp))
	{
		return false;
	}
	ON_SCOPE_EXIT { Clear({ControlImp, Imp, ACataclysmMinion::TyrantsMinionOf(Control.Actor)}); };

	Kill(ControlImp);
	TestEqual(TEXT("control: with the seconds at 10 an imp's death summons a Tyrant's Minion"),
			  TyrantsStanding(Control.Actor), 1);

	Kill(Imp);
	TestTrue(FString::Printf(TEXT("with the seconds at nought the blast still lands, so the death reached the hook: "
								  "%.2f"), Target.Lost()),
			 Target.Lost() > 0.0f);
	TestEqual(TEXT("and nothing is summoned"), TyrantsStanding(Summoner.Actor), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
