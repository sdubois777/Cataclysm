// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Demon King's Regalia's ten-piece bonus, the mechanism. Ruled by the coordinating session on 2026-10-17 (the
 * window to make the kill is the 3 s of immunity; "Rare or higher" is Elite and above) and 2026-10-18 (Q1-Q7).
 *
 * "If you would be killed during Demonic Rage, instead gain 100% crit chance and crit multiplier and become immune to
 * all damage for 3 seconds. Killing a Rare or higher enemy instanly heals you to full, failing to results in your
 * death"
 *
 * THE ROWS ARE MADE UP HERE, IN THE SHAPE THE ENTRY GIVES THE ENCHANTMENT SESSION: `lethal_hit_reprieve_every_seconds`
 * a flat 10 scaled by its own stacks on `health_falls_below` at 25, one stack for 10 s with a trigger cooldown of
 * 300 s (the Demonic Rage's own event, length and cooldown, `EnchantmentEffects.csv` 424 to 426);
 * `lethal_hit_reprieve_seconds` a flat 3; `lethal_hit_reprieve_kill_rarity_at_least` a flat 1 (Elite); and
 * `crit_chance` and `crit_multiplier` each a flat 100 scaled by its own stacks on `lethal_hit_reprieved`, one stack
 * for 3 s. They are put on a real non-set enchantment, "Double your energy shield", and passed through
 * `UCataclysmItemModifiers::AccumulateEnchantmentsInto`, so the loader's lines are what is tested, as the Warlord's
 * Will ten-piece tests do. The real rows, worn at ten pieces with a control at nine, are the enchantment session's
 * test.
 *
 * THE WEARER IS A PLAYER, so a kill reaches it through its own death listener and a death is its own `HandleDeath`.
 * It is built by the client path (`OnRep_PlayerState`), as `CataclysmDeathTests.cpp` builds one, which leaves the
 * attribute sets' placeholder numbers rather than a class line, so no evasion or block stands between a blow and the
 * save. Its stat lines hold the made-up rows and nothing else; health is 1000. Blows are 100000 of direct damage from
 * a creature 3 m off. Kills are announced as the combat events announce one. Time is moved by adding to the world's
 * clock, and the sentence's step is called by hand, as the regeneration step would call it. Every creature stands at
 * least 3 m from the player and from each other, and nothing here deals damage in an area.
 */
namespace CataclysmSetBonusDemonKingTenTest
{
	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The real enchantment the made-up rows are put on, and the drawback beside it, which has no effect row. */
	const TCHAR* const Benefit = TEXT("Positive_Double_your_energy_shield");
	const TCHAR* const Drawback = TEXT("Negative_Can_t_use_a_basic_attack");

	/** The ruled figures: the Rage's 25%, its 10 s, the 3 s, the +100 points, and the wearer's health. */
	constexpr float RageSeconds = 10.0f;
	constexpr float ReprieveSeconds = 3.0f;
	constexpr float CritPoints = 100.0f;
	constexpr float WholeHealth = 1000.0f;
	constexpr float InTheRage = 200.0f;
	constexpr float Lethal = 100000.0f;

	/** The wearer's own critical strike figures, the bases the made-up rows add to. */
	constexpr float OwnCritChance = 5.0f;
	constexpr float OwnCritMultiplier = 150.0f;

	/** How far either side of a window's end a reading is taken, in seconds. */
	constexpr float Either = 0.1f;

	/** One of the shipped tables, read from `game/Data`. Null when it does not load. */
	template <typename RowType>
	UDataTable* LoadData(const TCHAR* FileName)
	{
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *(FPaths::ProjectDir() / TEXT("Data") / FileName)))
		{
			return nullptr;
		}
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowType::StaticStruct();
		return Table->CreateTableFromCSVString(Contents).Num() > 0 ? nullptr : Table;
	}

	/** The five made-up rows, as a CSV the effect table reads. */
	FString RowsCsv()
	{
		FString Csv = TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,RequiredTags,Condition,ConditionValue,Scale,"
						   "ScaleStep,Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,EverySeconds,"
						   "EveryNth,ScaleStepHigh,StackSecondsHigh,Condition2,ConditionValue2,ConditionValueHigh,"
						   "TriggerCooldown,EventValue,Ailment,DamageShare\n");
		Csv += FString::Printf(TEXT("%s#1,%s,lethal_hit_reprieve_every_seconds,flat,10,10,,,0,own_stacks,1,,"
									"health_falls_below,,1,10,0,0,0,0,0,,0,0,300,25,,0\n"), Benefit, Benefit);
		Csv += FString::Printf(TEXT("%s#2,%s,lethal_hit_reprieve_seconds,flat,3,3,,,0,,0,,,,0,0,0,0,0,0,0,,0,0,0,0,,0\n"),
							   Benefit, Benefit);
		Csv += FString::Printf(TEXT("%s#3,%s,lethal_hit_reprieve_kill_rarity_at_least,flat,1,1,,,0,,0,,,,0,0,0,0,0,0,0,,"
									"0,0,0,0,,0\n"), Benefit, Benefit);
		Csv += FString::Printf(TEXT("%s#4,%s,crit_chance,flat,100,100,,,0,own_stacks,1,,lethal_hit_reprieved,,1,3,0,0,0,0,"
									"0,,0,0,0,0,,0\n"), Benefit, Benefit);
		Csv += FString::Printf(TEXT("%s#5,%s,crit_multiplier,flat,100,100,,,0,own_stacks,1,,lethal_hit_reprieved,,1,3,0,0,"
									"0,0,0,,0,0,0,0,,0\n"), Benefit, Benefit);
		return Csv;
	}

	/** What the made-up rows become on a wearer: the stat lines and the grants. */
	struct FMade
	{
		TMap<FName, TArray<FCataclysmStatModifier>> Totals;
		TArray<FCataclysmPoolAction> Actions;
	};

	/** The rows passed through the loader. False, with a failed set-up assertion, when a table did not load. */
	bool Make(FAutomationTestBase& Test, FMade& Out)
	{
		UDataTable* Positive = LoadData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
		UDataTable* Negative = LoadData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
		UDataTable* Effects = NewObject<UDataTable>();
		Effects->RowStruct = FCataclysmEnchantmentEffectRow::StaticStruct();
		const bool bEffectsRead = Effects->CreateTableFromCSVString(RowsCsv()).Num() == 0;
		if (!Test.TestNotNull(TEXT("set-up: the positive enchantments"), Positive)
			|| !Test.TestNotNull(TEXT("set-up: the negative enchantments"), Negative)
			|| !Test.TestTrue(TEXT("set-up: the five made-up rows read as an effect table"), bEffectsRead))
		{
			return false;
		}

		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(Benefit);
		Rolled.Negative = FName(Drawback);
		Item.Enchantments.Add(Rolled);
		Item.EnchantmentCount = 1;
		UCataclysmItemModifiers::AccumulateEnchantmentsInto(Out.Totals, {Item}, Effects, Positive, Negative,
															&Out.Actions);
		return true;
	}

	/** One stat line on a base, holding what the rows put on that stat. */
	void AddLine(TMap<FName, FCataclysmStatInputs>& Lines, const FMade& Made, const TCHAR* Stat, float Base)
	{
		FCataclysmStatInputs& Line = Lines.Add(FName(Stat));
		Line.Base = Base;
		if (const TArray<FCataclysmStatModifier>* Modifiers = Made.Totals.Find(FName(Stat)))
		{
			Line.Modifiers = *Modifiers;
		}
	}

	/** One flat stat line, as a passive node's row gives one. For Nothing Stops It's two figures. */
	void AddFlatLine(TMap<FName, FCataclysmStatInputs>& Lines, const TCHAR* Stat, float Value)
	{
		FCataclysmStatModifier Flat;
		Flat.Bucket = ECataclysmStatBucket::Flat;
		Flat.Source = ECataclysmModifierSource::PassiveKeystone;
		Flat.Value = Value;
		FCataclysmStatInputs& Line = Lines.Add(FName(Stat));
		Line.Base = 0.0f;
		Line.Modifiers = {Flat};
	}

	/** A creature on a side, its rarity rung given. */
	ACataclysmEnemyCharacter* SpawnCreature(UWorld* World, const FVector& Where, int32 RarityStep)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetRarityStep(RarityStep);
			Made->SetHealth(1000.0f);
		}
		return Made;
	}

	/** Everything a test of the bonus stands on. */
	struct FScene
	{
		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		ACataclysmEnemyCharacter* Attacker = nullptr;
	};

	/**
	 * A player holding the made-up rows, at full health, and a creature 3 m off to strike it. With
	 * `NothingStopsItEvery` above nought, the player also holds Nothing Stops It's two figures: that interval, and 2 s
	 * of no damage. False, with a failed set-up assertion, when anything could not be built.
	 */
	bool Build(FAutomationTestBase& Test, UWorld* World, FScene& Out, float NothingStopsItEvery = 0.0f)
	{
		FMade Made;
		if (!Make(Test, Made))
		{
			return false;
		}
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		Out.Player = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("set-up: a player state"), State)
			|| !Test.TestNotNull(TEXT("set-up: a player"), Out.Player))
		{
			return false;
		}
		Out.Player->SetPlayerState(State);
		Out.Player->OnRep_PlayerState();
		Out.System = State->GetCataclysmAbilitySystemComponent();
		Out.Attacker = SpawnCreature(World, FVector(3.0f * M, 0.0f, 0.0f), /*RarityStep=*/0);
		if (!Test.TestNotNull(TEXT("set-up: the player's ability system"), Out.System)
			|| !Test.TestNotNull(TEXT("set-up: a creature to strike it"), Out.Attacker))
		{
			return false;
		}

		TMap<FName, FCataclysmStatInputs> Lines;
		AddLine(Lines, Made, UCataclysmAbilitySystemComponent::LethalHitReprieveEverySecondsStat, 0.0f);
		AddLine(Lines, Made, UCataclysmAbilitySystemComponent::LethalHitReprieveSecondsStat, 0.0f);
		AddLine(Lines, Made, UCataclysmAbilitySystemComponent::LethalHitReprieveKillRarityStat, 0.0f);
		AddLine(Lines, Made, TEXT("crit_chance"), OwnCritChance);
		AddLine(Lines, Made, TEXT("crit_multiplier"), OwnCritMultiplier);
		if (NothingStopsItEvery > 0.0f)
		{
			AddFlatLine(Lines, UCataclysmAbilitySystemComponent::LethalHitSurvivedEverySecondsStat, NothingStopsItEvery);
			AddFlatLine(Lines, UCataclysmAbilitySystemComponent::ImmuneAfterLethalHitSecondsStat, 2.0f);
		}
		Out.System->SetStatInputs(MoveTemp(Lines));
		Out.System->SetPoolActions(MoveTemp(Made.Actions));

		// MAXIMUM FIRST, THEN CURRENT: the vital set clamps health to it.
		Out.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), WholeHealth);
		Out.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), WholeHealth);
		return true;
	}

	float HealthOf(const FScene& Scene)
	{
		return Scene.System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	}

	/** A direct write of health, as the Demonic Rage test moves it: the fall below 25% starts the Rage. */
	void SetHealth(const FScene& Scene, float Current)
	{
		Scene.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Current);
	}

	/** A blow far larger than the wearer's health. */
	void Strike(const FScene& Scene)
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Scene.Attacker, Scene.Player, Lethal);
	}

	/** The wearer's figure for a stat, asked with no tags. */
	float Figure(const FScene& Scene, const TCHAR* Stat)
	{
		return Scene.System->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f);
	}

	/** A death announced as the combat events announce one, credited to `Killer`. */
	void AnnounceKill(UWorld* World, AActor* Killer, AActor* Victim)
	{
		if (UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World))
		{
			// NEVER NULL TAGS, as `UCataclysmCombatEvents::NoteDeath` never leaves them.
			static const FGameplayTagContainer NoTags;
			FCataclysmDeathNotice Notice;
			Notice.KillingTags = &NoTags;
			Notice.Victim = Victim;
			Notice.Killer = Killer;
			Notice.KillingCauser = Killer;
			Notice.Location = Victim ? Victim->GetActorLocation() : FVector::ZeroVector;
			Events->OnDeath.Broadcast(Notice);
		}
	}

	bool IsDead(const FScene& Scene)
	{
		return UCataclysmSkillEffects::IsDead(Scene.Player);
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonKingTenLoaderTest,
	"Cataclysm.SetBonus.DemonKing10.TheRowsBecomeAReprieveHeldByTheRageAndTwoCriticalStrikeGrantsOnTheReprieve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * What the loader makes of the five rows. One grant on `health_falls_below` at 25, one stack for 10 s with a 300 s
 * trigger cooldown, under the interval's key; two grants on `lethal_hit_reprieved`, one stack for 3 s, with no
 * cooldown. The interval's modifier is a flat 10 scaled by the stacks of its own grant; the seconds and the rung are
 * flat and unscaled; each critical strike stat is a flat 100 scaled by its own stacks. The engine's own check refuses
 * none of the five.
 */
bool FCataclysmDemonKingTenLoaderTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusDemonKingTenTest;
	FMade Made;
	if (!Make(*this, Made))
	{
		return false;
	}

	// THE CONTROL: the rows reached the loader at all.
	if (!TestEqual(TEXT("control: the rows made five stat lines"), Made.Totals.Num(), 5))
	{
		return false;
	}
	TestEqual(TEXT("three grants: the interval's and the two critical strike rows'"), Made.Actions.Num(), 3);

	const FCataclysmPoolAction* RageGrant = nullptr;
	int32 ReprieveGrants = 0;
	for (const FCataclysmPoolAction& Grant : Made.Actions)
	{
		if (Grant.Event == FName(TEXT("health_falls_below")))
		{
			RageGrant = &Grant;
			continue;
		}
		if (Grant.Event == FName(TEXT("lethal_hit_reprieved")) && Grant.StackCap == 1
			&& FMath::IsNearlyEqual(Grant.StackSeconds, ReprieveSeconds)
			&& FMath::IsNearlyZero(Grant.TriggerCooldownSeconds))
		{
			++ReprieveGrants;
		}
	}
	TestEqual(TEXT("two grants on lethal_hit_reprieved, each one stack for 3 s with no cooldown"), ReprieveGrants, 2);
	if (!TestNotNull(TEXT("one grant on health_falls_below, the Rage's event"), RageGrant))
	{
		return false;
	}
	TestTrue(TEXT("the Rage's grant: one stack for 10 s below 25%, waiting 300 s after it fires"),
			 RageGrant->StackCap == 1 && FMath::IsNearlyEqual(RageGrant->StackSeconds, RageSeconds)
				 && FMath::IsNearlyEqual(RageGrant->EventValue, 25.0f)
				 && FMath::IsNearlyEqual(RageGrant->TriggerCooldownSeconds, 300.0f));

	const TArray<FCataclysmStatModifier>* Every =
		Made.Totals.Find(FName(UCataclysmAbilitySystemComponent::LethalHitReprieveEverySecondsStat));
	TestTrue(TEXT("the interval is a flat 10 scaled by the stacks of the Rage's grant"),
			 Every && Every->Num() == 1 && (*Every)[0].Bucket == ECataclysmStatBucket::Flat
				 && FMath::IsNearlyEqual((*Every)[0].Value, 10.0f)
				 && (*Every)[0].Scale == ECataclysmStatScale::PerOwnStack
				 && (*Every)[0].StackKey == RageGrant->StackKey);
	for (const TCHAR* Stat : {UCataclysmAbilitySystemComponent::LethalHitReprieveSecondsStat,
							  UCataclysmAbilitySystemComponent::LethalHitReprieveKillRarityStat})
	{
		const TArray<FCataclysmStatModifier>* Line = Made.Totals.Find(FName(Stat));
		TestTrue(*FString::Printf(TEXT("%s is one flat modifier, unscaled"), Stat),
				 Line && Line->Num() == 1 && (*Line)[0].Bucket == ECataclysmStatBucket::Flat
					 && (*Line)[0].Scale == ECataclysmStatScale::Fixed);
	}
	for (const TCHAR* Stat : {TEXT("crit_chance"), TEXT("crit_multiplier")})
	{
		const TArray<FCataclysmStatModifier>* Line = Made.Totals.Find(FName(Stat));
		TestTrue(*FString::Printf(TEXT("%s is a flat 100 scaled by its own stacks, and the engine's own check refuses "
									   "it not"), Stat),
				 Line && Line->Num() == 1 && (*Line)[0].Bucket == ECataclysmStatBucket::Flat
					 && FMath::IsNearlyEqual((*Line)[0].Value, CritPoints)
					 && (*Line)[0].Scale == ECataclysmStatScale::PerOwnStack
					 && UCataclysmStatPipeline::ValidateModifier((*Line)[0]).IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonKingTenSaveTest,
	"Cataclysm.SetBonus.DemonKing10.ALethalBlowInTheRageLeavesOneHealthAndNoDamageForThreeSecondsAndAfterTheRageKills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two worlds. THE CONTROL FIRST: the Rage started by a fall to 20% and lapsed 11 s later, with health still at 20%;
 * the interval reads nought and the blow kills. Then the blow while the Rage holds: one health is left, the wearer is
 * alive and takes no damage 2.9 s on, and 3.1 s on the no-damage window has ended.
 */
bool FCataclysmDemonKingTenSaveTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusDemonKingTenTest;
	for (const bool bLapsed : {true, false})
	{
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("set-up: a world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		FScene Scene;
		if (!Build(*this, World, Scene))
		{
			return false;
		}
		SetHealth(Scene, InTheRage);
		if (!TestEqual(TEXT("set-up: the fall below 25% starts the Rage, so the interval reads 10"),
					   Figure(Scene, UCataclysmAbilitySystemComponent::LethalHitReprieveEverySecondsStat), 10.0f,
					   0.001f))
		{
			return false;
		}

		if (bLapsed)
		{
			World->TimeSeconds += RageSeconds + 1.0f;
			TestEqual(TEXT("control: 11 s later the Rage has lapsed and the interval reads nought"),
					  Figure(Scene, UCataclysmAbilitySystemComponent::LethalHitReprieveEverySecondsStat), 0.0f, 0.001f);
			Strike(Scene);
			TestEqual(TEXT("control: after the Rage the blow takes health to nought"), HealthOf(Scene), 0.0f, 0.01f);
			TestTrue(TEXT("control: and the wearer is dead"), IsDead(Scene));
			continue;
		}

		Strike(Scene);
		TestEqual(TEXT("in the Rage the blow leaves one health"), HealthOf(Scene), 1.0f, 0.01f);
		TestFalse(TEXT("and the wearer is alive"), IsDead(Scene));
		TestTrue(TEXT("and takes no damage"), Scene.System->IsDamageImmune());
		World->TimeSeconds += ReprieveSeconds - Either;
		Strike(Scene);
		TestEqual(TEXT("2.9 s on, a second blow deals nothing"), HealthOf(Scene), 1.0f, 0.01f);
		World->TimeSeconds += 2.0f * Either;
		TestFalse(TEXT("3.1 s on, the no-damage window has ended"), Scene.System->IsDamageImmune());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonKingTenCritTest,
	"Cataclysm.SetBonus.DemonKing10.TheReprieveGivesAHundredPointsOfCriticalStrikeChanceAndMultiplierForThreeSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q1 (a), "+100 points" of each. THE CONTROL IS THE WEARER'S OWN TWO FIGURES IN THE RAGE BEFORE THE BLOW, 5 and 150
 * on these lines: the Rage alone grants no critical strike. After the reprieve each is 100 points above its own, 2.9 s
 * on still, and 3.1 s on its own again. The multiplier is a percentage a critical strike multiplies by
 * (`UCataclysmDamageCalculation`: damage times the multiplier over 100), so 150 becomes 250.
 */
bool FCataclysmDemonKingTenCritTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusDemonKingTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FScene Scene;
	if (!Build(*this, World, Scene))
	{
		return false;
	}
	SetHealth(Scene, InTheRage);
	const float Chance = Figure(Scene, TEXT("crit_chance"));
	const float Multiplier = Figure(Scene, TEXT("crit_multiplier"));
	TestEqual(TEXT("control: in the Rage before the blow, the wearer's own chance"), Chance, OwnCritChance, 0.001f);
	TestEqual(TEXT("control: and its own multiplier"), Multiplier, OwnCritMultiplier, 0.001f);

	Strike(Scene);
	if (!TestEqual(TEXT("set-up: the blow is reprieved"), HealthOf(Scene), 1.0f, 0.01f))
	{
		return false;
	}
	TestEqual(TEXT("after the reprieve the chance is 100 points above its own"), Figure(Scene, TEXT("crit_chance")),
			  Chance + CritPoints, 0.001f);
	TestEqual(TEXT("and the multiplier is 100 points above its own"), Figure(Scene, TEXT("crit_multiplier")),
			  Multiplier + CritPoints, 0.001f);
	World->TimeSeconds += ReprieveSeconds - Either;
	TestEqual(TEXT("2.9 s on the chance is still raised"), Figure(Scene, TEXT("crit_chance")), Chance + CritPoints,
			  0.001f);
	TestEqual(TEXT("and the multiplier"), Figure(Scene, TEXT("crit_multiplier")), Multiplier + CritPoints, 0.001f);
	World->TimeSeconds += 2.0f * Either;
	TestEqual(TEXT("3.1 s on the chance is its own again"), Figure(Scene, TEXT("crit_chance")), Chance, 0.001f);
	TestEqual(TEXT("and the multiplier"), Figure(Scene, TEXT("crit_multiplier")), Multiplier, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonKingTenSentenceTest,
	"Cataclysm.SetBonus.DemonKing10.WithNoQualifyingKillTheWearerIsDeadWhenTheThreeSecondsEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The reprieve sentences the wearer, to be lifted by rung 1. THE CONTROL IS THE STEP 2.9 S ON: nothing falls due, and
 * the wearer is alive at one health. 3.1 s on the step makes the death: health nought, and the wearer marked dead by
 * its own `HandleDeath`.
 */
bool FCataclysmDemonKingTenSentenceTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusDemonKingTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FScene Scene;
	if (!Build(*this, World, Scene))
	{
		return false;
	}
	SetHealth(Scene, InTheRage);
	TestFalse(TEXT("control: before the blow no sentence runs"), Scene.System->IsUnderDeathSentence());
	Strike(Scene);
	TestEqual(TEXT("the reprieve sentences the wearer, lifted by an Elite or above"),
			  Scene.System->DeathSentenceLiftedByRarity(), 1);

	World->TimeSeconds += ReprieveSeconds - Either;
	TestFalse(TEXT("control: 2.9 s on the step makes no death"), Scene.System->CarryOutDueDeathSentence());
	TestEqual(TEXT("control: and the wearer has one health"), HealthOf(Scene), 1.0f, 0.01f);
	TestFalse(TEXT("control: and is alive"), IsDead(Scene));

	World->TimeSeconds += 2.0f * Either;
	TestTrue(TEXT("3.1 s on, with no kill made, the step makes the death"), Scene.System->CarryOutDueDeathSentence());
	TestEqual(TEXT("health is nought"), HealthOf(Scene), 0.0f, 0.01f);
	TestTrue(TEXT("and the wearer is dead"), IsDead(Scene));
	TestFalse(TEXT("and no sentence runs any more"), Scene.System->IsUnderDeathSentence());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonKingTenKillTest,
	"Cataclysm.SetBonus.DemonKing10.AnEliteKilledInTheThreeSecondsHealsToFullAndLiftsTheDeathAndACommonDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * After the reprieve, three kills in turn. THE CONTROLS FIRST: a Common killed by the wearer, and an Elite whose kill
 * is credited to another actor (a minion's own kill names the minion, `UCataclysmCombatEvents::AttackerOf`), each
 * leaves the sentence running and health at one. Then an Elite killed by the wearer: the sentence is lifted and
 * health is full, while the no-damage window and the critical strike run on (Q4). 3.1 s on the step makes no death,
 * the window has ended, and the chance is the wearer's own.
 */
bool FCataclysmDemonKingTenKillTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusDemonKingTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FScene Scene;
	if (!Build(*this, World, Scene))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Common = SpawnCreature(World, FVector(0.0f, 3.0f * M, 0.0f), /*RarityStep=*/0);
	ACataclysmEnemyCharacter* Elite = SpawnCreature(World, FVector(0.0f, -3.0f * M, 0.0f), /*RarityStep=*/1);
	AActor* Someone = World->SpawnActor<AActor>(FVector(-3.0f * M, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a Common creature"), Common) || !TestNotNull(TEXT("set-up: an Elite"), Elite)
		|| !TestNotNull(TEXT("set-up: another actor to be credited"), Someone)
		|| !TestEqual(TEXT("set-up: the Elite stands on rung 1"), Elite->RarityStep, 1))
	{
		return false;
	}
	SetHealth(Scene, InTheRage);
	const float Chance = Figure(Scene, TEXT("crit_chance"));
	Strike(Scene);
	if (!TestTrue(TEXT("set-up: the blow is reprieved and the wearer sentenced"),
				  FMath::IsNearlyEqual(HealthOf(Scene), 1.0f, 0.01f) && Scene.System->IsUnderDeathSentence()))
	{
		return false;
	}

	AnnounceKill(World, Scene.Player, Common);
	TestTrue(TEXT("control: the wearer's kill of a Common leaves the sentence running"),
			 Scene.System->IsUnderDeathSentence());
	TestEqual(TEXT("control: and health at one"), HealthOf(Scene), 1.0f, 0.01f);
	AnnounceKill(World, Someone, Elite);
	TestTrue(TEXT("control: an Elite's kill credited to another actor leaves the sentence running"),
			 Scene.System->IsUnderDeathSentence());
	TestEqual(TEXT("control: and health at one"), HealthOf(Scene), 1.0f, 0.01f);

	AnnounceKill(World, Scene.Player, Elite);
	TestFalse(TEXT("the wearer's kill of an Elite lifts the sentence"), Scene.System->IsUnderDeathSentence());
	TestEqual(TEXT("and heals to full"), HealthOf(Scene), WholeHealth, 0.01f);
	TestTrue(TEXT("and the no-damage window runs on"), Scene.System->IsDamageImmune());
	TestEqual(TEXT("and the critical strike runs on"), Figure(Scene, TEXT("crit_chance")), Chance + CritPoints, 0.001f);

	World->TimeSeconds += ReprieveSeconds + Either;
	TestFalse(TEXT("3.1 s on the step makes no death"), Scene.System->CarryOutDueDeathSentence());
	TestFalse(TEXT("and the wearer is alive"), IsDead(Scene));
	TestEqual(TEXT("at full health"), HealthOf(Scene), WholeHealth, 0.01f);
	TestFalse(TEXT("and the window has ended"), Scene.System->IsDamageImmune());
	TestEqual(TEXT("and the chance is its own again"), Figure(Scene, TEXT("crit_chance")), Chance, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonKingTenOnceTest,
	"Cataclysm.SetBonus.DemonKing10.ASecondLethalBlowInTheSameRageAfterTheThreeSecondsKills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q2 (a), at most once a Demonic Rage. The reprieve, an Elite's kill to lift the sentence, and 4 s on, inside the
 * Rage's 10 s: THE CONTROL IS THE RAGE STILL HELD, the interval reading 10, while the reprieve's own clock refuses.
 * The lethal blow then kills.
 */
bool FCataclysmDemonKingTenOnceTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusDemonKingTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FScene Scene;
	if (!Build(*this, World, Scene))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Elite = SpawnCreature(World, FVector(0.0f, -3.0f * M, 0.0f), /*RarityStep=*/1);
	if (!TestNotNull(TEXT("set-up: an Elite"), Elite))
	{
		return false;
	}
	SetHealth(Scene, InTheRage);
	Strike(Scene);
	AnnounceKill(World, Scene.Player, Elite);
	if (!TestTrue(TEXT("set-up: reprieved, and the Elite's kill healed to full"),
				  FMath::IsNearlyEqual(HealthOf(Scene), WholeHealth, 0.01f) && !Scene.System->IsUnderDeathSentence()))
	{
		return false;
	}

	World->TimeSeconds += ReprieveSeconds + 1.0f;
	TestEqual(TEXT("control: 4 s on the Rage still holds, so the interval reads 10"),
			  Figure(Scene, UCataclysmAbilitySystemComponent::LethalHitReprieveEverySecondsStat), 10.0f, 0.001f);
	TestFalse(TEXT("control: and the reprieve's own clock refuses a second"), Scene.System->MayReprieveLethalHit());
	Strike(Scene);
	TestEqual(TEXT("a second lethal blow in the same Rage takes health to nought"), HealthOf(Scene), 0.0f, 0.01f);
	TestTrue(TEXT("and the wearer is dead"), IsDead(Scene));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonKingTenOneBlowTest,
	"Cataclysm.SetBonus.DemonKing10.ASingleBlowFromAboveAQuarterStraightToDeathIsNotSaved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q7 (a): the save needs the Rage already running. THE CONTROL IS THE WEARER AT 30%: no stack is held and the interval
 * reads nought. One blow from there to death is not reprieved, though its own write takes health below 25%.
 */
bool FCataclysmDemonKingTenOneBlowTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusDemonKingTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FScene Scene;
	if (!Build(*this, World, Scene))
	{
		return false;
	}
	SetHealth(Scene, 300.0f);
	TestEqual(TEXT("control: at 30% no Rage is held and the interval reads nought"),
			  Figure(Scene, UCataclysmAbilitySystemComponent::LethalHitReprieveEverySecondsStat), 0.0f, 0.001f);
	Strike(Scene);
	TestEqual(TEXT("one blow from 30% takes health to nought"), HealthOf(Scene), 0.0f, 0.01f);
	TestTrue(TEXT("and the wearer is dead"), IsDead(Scene));
	TestFalse(TEXT("and no sentence was begun"), Scene.System->IsUnderDeathSentence());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDemonKingTenNothingStopsItTest,
	"Cataclysm.SetBonus.DemonKing10.NothingStopsItSavesFirstTheReprieveTakesTheNextBlowAndNeitherStopsTheDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q6 (a) and Q3 (a). The wearer also holds Nothing Stops It, at an interval of 3 s and 2 s of no damage. THE CONTROL
 * IS THE TWO FIGURES IN THE RAGE: Nothing Stops It's interval reads its own 3 and the reprieve's its own 10, so
 * neither adds to the other. The first blow is saved by Nothing Stops It: one health, no sentence, no critical strike.
 * 2.1 s on, its window over and its clock not ready, the second blow is reprieved: one health, a sentence, the
 * critical strike. 3.1 s after that Nothing Stops It is ready again, and the step still makes the death.
 */
bool FCataclysmDemonKingTenNothingStopsItTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusDemonKingTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FScene Scene;
	if (!Build(*this, World, Scene, /*NothingStopsItEvery=*/3.0f))
	{
		return false;
	}
	SetHealth(Scene, InTheRage);
	TestEqual(TEXT("control: in the Rage Nothing Stops It's interval is its own 3"),
			  Figure(Scene, UCataclysmAbilitySystemComponent::LethalHitSurvivedEverySecondsStat), 3.0f, 0.001f);
	TestEqual(TEXT("control: and the reprieve's is its own 10"),
			  Figure(Scene, UCataclysmAbilitySystemComponent::LethalHitReprieveEverySecondsStat), 10.0f, 0.001f);
	const float Chance = Figure(Scene, TEXT("crit_chance"));

	Strike(Scene);
	TestEqual(TEXT("the first blow leaves one health"), HealthOf(Scene), 1.0f, 0.01f);
	TestFalse(TEXT("and Nothing Stops It saved it: its clock is spent"), Scene.System->MaySurviveLethalHit());
	TestFalse(TEXT("and no sentence runs"), Scene.System->IsUnderDeathSentence());
	TestEqual(TEXT("and the chance is the wearer's own"), Figure(Scene, TEXT("crit_chance")), Chance, 0.001f);

	World->TimeSeconds += 2.0f + Either;
	if (!TestFalse(TEXT("set-up: 2.1 s on Nothing Stops It's window has ended"), Scene.System->IsDamageImmune()))
	{
		return false;
	}
	Strike(Scene);
	TestEqual(TEXT("the second blow leaves one health"), HealthOf(Scene), 1.0f, 0.01f);
	TestTrue(TEXT("and the reprieve took it: a sentence runs"), Scene.System->IsUnderDeathSentence());
	TestEqual(TEXT("and the chance is 100 points above its own"), Figure(Scene, TEXT("crit_chance")),
			  Chance + CritPoints, 0.001f);

	World->TimeSeconds += ReprieveSeconds + Either;
	TestTrue(TEXT("control: 3.1 s later Nothing Stops It is ready again"), Scene.System->MaySurviveLethalHit());
	TestTrue(TEXT("and the step still makes the death"), Scene.System->CarryOutDueDeathSentence());
	TestEqual(TEXT("health is nought"), HealthOf(Scene), 0.0f, 0.01f);
	TestTrue(TEXT("and the wearer is dead"), IsDead(Scene));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
