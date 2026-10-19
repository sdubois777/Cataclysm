// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmReaperSoul.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmFloorContents.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayTagContainer.h"
#include "Interface/CataclysmSkillBar.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Reaper's Embrace's six-piece bonus, the mechanism. The owner's ruling #8 of 2026-10-17 (a Soul is picked up by
 * walking within about 1.5 m; it lasts the floor) and the coordinating session's rulings R1-R8 of 2026-10-18.
 *
 * "When a minion or pet dies, it leaves behind a "Soul" that can be picked up. Picking up a Soul grants a stack of Soul
 * Strike that increases your damage by 10% for 5 seconds. Max 10 stacks."
 *
 * THE ROWS ARE MADE UP HERE, IN THE SHAPE THE ENTRY GIVES THE ENCHANTMENT SESSION: `minion_death_leaves_soul` a flat 1,
 * and `attack_damage` and `spell_damage` each 10 increased scaled by its own stacks on `soul_picked_up`, step 1, at
 * most 10, for 5 s. They are put on a real non-set enchantment, "Double your energy shield", and passed through
 * `UCataclysmItemModifiers::AccumulateEnchantmentsInto`, as the Demon King's Regalia's ten-piece tests do, so the
 * loader's lines are what is tested. The real rows, worn at six pieces with a control at five, are the enchantment
 * session's test.
 *
 * EVERY ACTOR HAS A REAL TEAM. The commander is a player character, on the players' side, built by the client path
 * (`OnRep_PlayerState`) as `CataclysmDeathTests.cpp` builds one; its minions take its side at their spawning, and a
 * thrall takes it at `Subjugate`. A creature is of the monsters' side. Nothing here deals damage in an area: no
 * commander holds Shared Ruin or Every One Bursts, and no minion is told an explosion radius.
 *
 * TAKING IS `ACataclysmReaperSoul::TakeSoulsNear`, called with the pawn as the player controller's frame calls it (no
 * automation test has a player controller's frame). Deaths are health written to nought, as anything else dies. Time
 * is moved with `CataclysmTestWorld::RunClock`, which fires the engine's lifespan timers. Every body stands at least
 * 1 m from every other.
 */
namespace CataclysmSetBonusReaperSixTest
{
	using Vital = UCataclysmVitalAttributeSet;

	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The real enchantment the made-up rows are put on, and the drawback beside it, which has no effect row. */
	const TCHAR* const ReaperBenefit = TEXT("Positive_Double_your_energy_shield");
	const TCHAR* const ReaperDrawback = TEXT("Negative_Can_t_use_a_basic_attack");

	/** The sentence's figures: 10% a stack, at most 10, for 5 s. */
	constexpr float StrikePercent = 10.0f;
	constexpr int32 StrikeCap = 10;
	constexpr float StrikeSeconds = 5.0f;

	/** The base each damage stat line holds, so a stack's 10% is a figure of 10. */
	constexpr float DamageBase = 100.0f;

	/** How far either side of a window's end, or of the reach, a reading is taken. */
	constexpr float EitherSeconds = 0.1f;
	constexpr float EitherCm = 5.0f;

	/** Where a Soul lies below a capsule's centre: the floor under a standing character. */
	constexpr float FloorBelowCm = -90.0f;

	/** One of the shipped tables, read from `game/Data`. Null when it does not load. */
	template <typename RowType>
	UDataTable* LoadReaperSoulData(const TCHAR* FileName)
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

	/** The three made-up rows, as a CSV the effect table reads. */
	FString ReaperSoulRowsCsv()
	{
		FString Csv = TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,RequiredTags,Condition,ConditionValue,Scale,"
						   "ScaleStep,Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,EverySeconds,"
						   "EveryNth,ScaleStepHigh,StackSecondsHigh,Condition2,ConditionValue2,ConditionValueHigh,"
						   "TriggerCooldown,EventValue,Ailment,DamageShare\n");
		Csv += FString::Printf(TEXT("%s#1,%s,minion_death_leaves_soul,flat,1,1,,,0,,0,,,,0,0,0,0,0,0,0,,0,0,0,0,,0\n"),
							   ReaperBenefit, ReaperBenefit);
		Csv += FString::Printf(TEXT("%s#2,%s,attack_damage,increased,10,10,,,0,own_stacks,1,,soul_picked_up,,10,5,0,0,0,"
									"0,0,,0,0,0,0,,0\n"), ReaperBenefit, ReaperBenefit);
		Csv += FString::Printf(TEXT("%s#3,%s,spell_damage,increased,10,10,,,0,own_stacks,1,,soul_picked_up,,10,5,0,0,0,"
									"0,0,,0,0,0,0,,0\n"), ReaperBenefit, ReaperBenefit);
		return Csv;
	}

	/** What the made-up rows become on a wearer: the stat lines and the grants. */
	struct FReaperSoulMade
	{
		TMap<FName, TArray<FCataclysmStatModifier>> Totals;
		TArray<FCataclysmPoolAction> Actions;
	};

	/** The rows passed through the loader. False, with a failed set-up assertion, when a table did not load. */
	bool MakeReaperSoulRows(FAutomationTestBase& Test, FReaperSoulMade& Out)
	{
		UDataTable* Positive = LoadReaperSoulData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
		UDataTable* Negative = LoadReaperSoulData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
		UDataTable* Effects = NewObject<UDataTable>();
		Effects->RowStruct = FCataclysmEnchantmentEffectRow::StaticStruct();
		const bool bEffectsRead = Effects->CreateTableFromCSVString(ReaperSoulRowsCsv()).Num() == 0;
		if (!Test.TestNotNull(TEXT("set-up: the positive enchantments"), Positive)
			|| !Test.TestNotNull(TEXT("set-up: the negative enchantments"), Negative)
			|| !Test.TestTrue(TEXT("set-up: the three made-up rows read as an effect table"), bEffectsRead))
		{
			return false;
		}

		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(ReaperBenefit);
		Rolled.Negative = FName(ReaperDrawback);
		Item.Enchantments.Add(Rolled);
		Item.EnchantmentCount = 1;
		UCataclysmItemModifiers::AccumulateEnchantmentsInto(Out.Totals, {Item}, Effects, Positive, Negative,
															&Out.Actions);
		return true;
	}

	/** One stat line on a base, holding what the rows put on that stat. */
	void AddReaperSoulLine(TMap<FName, FCataclysmStatInputs>& Lines, const FReaperSoulMade& Made, const TCHAR* Stat,
						   float Base)
	{
		FCataclysmStatInputs& Line = Lines.Add(FName(Stat));
		Line.Base = Base;
		if (const TArray<FCataclysmStatModifier>* Modifiers = Made.Totals.Find(FName(Stat)))
		{
			Line.Modifiers = *Modifiers;
		}
	}

	/** One flat stat line, as a row gives one. For the two summons' seconds. */
	void AddReaperSoulFlatLine(TMap<FName, FCataclysmStatInputs>& Lines, const TCHAR* Stat, float Value)
	{
		FCataclysmStatModifier Flat;
		Flat.Bucket = ECataclysmStatBucket::Flat;
		Flat.Source = ECataclysmModifierSource::Enchantment;
		Flat.Value = Value;
		FCataclysmStatInputs& Line = Lines.Add(FName(Stat));
		Line.Base = 0.0f;
		Line.Modifiers = {Flat};
	}

	/** Everything a test of the bonus stands on: a player holding the made-up rows. */
	struct FReaperSoulScene
	{
		ACataclysmPlayerCharacter* Player = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		FReaperSoulMade Made;
	};

	/**
	 * REPLACES THE WHOLE STAT LINE: the two damage lines on their base with Soul Strike's rows, the flag's line when
	 * `bFlag`, and the Tyrant's Minion's and the Warhound's seconds when `SummonSeconds` is above nought.
	 */
	void HoldReaperSoulLines(const FReaperSoulScene& Scene, bool bFlag, float SummonSeconds = 0.0f)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		AddReaperSoulLine(Lines, Scene.Made, TEXT("attack_damage"), DamageBase);
		AddReaperSoulLine(Lines, Scene.Made, TEXT("spell_damage"), DamageBase);
		if (bFlag)
		{
			AddReaperSoulLine(Lines, Scene.Made, ACataclysmReaperSoul::MinionDeathLeavesSoulStat, 0.0f);
		}
		if (SummonSeconds > 0.0f)
		{
			AddReaperSoulFlatLine(Lines, ACataclysmMinion::TyrantsMinionSecondsStat, SummonSeconds);
			AddReaperSoulFlatLine(Lines, ACataclysmMinion::WarhoundSecondsStat, SummonSeconds);
		}
		Scene.System->SetStatInputs(MoveTemp(Lines));
	}

	/**
	 * A player at `Where` holding the made-up rows, the flag included, at full health. False, with a failed set-up
	 * assertion, when anything could not be built.
	 */
	bool BuildReaperSoulScene(FAutomationTestBase& Test, UWorld* World, const FVector& Where, FReaperSoulScene& Out)
	{
		if (!MakeReaperSoulRows(Test, Out.Made))
		{
			return false;
		}
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		Out.Player = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("set-up: a player state"), State)
			|| !Test.TestNotNull(TEXT("set-up: a player"), Out.Player))
		{
			return false;
		}
		Out.Player->SetPlayerState(State);
		Out.Player->OnRep_PlayerState();
		Out.System = State->GetCataclysmAbilitySystemComponent();
		if (!Test.TestNotNull(TEXT("set-up: the player's ability system"), Out.System))
		{
			return false;
		}
		HoldReaperSoulLines(Out, /*bFlag=*/true);
		TArray<FCataclysmPoolAction> Actions = Out.Made.Actions;
		Out.System->SetPoolActions(MoveTemp(Actions));

		// MAXIMUM FIRST, THEN CURRENT: the vital set clamps health to it.
		Out.System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
		Out.System->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
		return true;
	}

	/** A creature of the monsters' side, as `CataclysmCommandTests.cpp` makes one. */
	ACataclysmEnemyCharacter* ReaperSoulCreature(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(1000.0f);
		}
		return Made;
	}

	/** Write a body's health to nothing, which is how anything else dies. */
	void KillReaperSoulBody(AActor* Body)
	{
		if (UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Body))
		{
			Its->SetNumericAttributeBase(Vital::GetHealthAttribute(), 0.0f);
		}
	}

	/** Every Soul lying in `World`. */
	TArray<ACataclysmReaperSoul*> ReaperSoulsIn(UWorld* World)
	{
		TArray<ACataclysmReaperSoul*> Found;
		for (TActorIterator<ACataclysmReaperSoul> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				Found.Add(*It);
			}
		}
		return Found;
	}

	/** How many Souls lie within 1 cm of `Where` along the floor, left for `Commander`. */
	int32 ReaperSoulsAt(UWorld* World, const FVector& Where, const AActor* Commander)
	{
		int32 Count = 0;
		for (const ACataclysmReaperSoul* Soul : ReaperSoulsIn(World))
		{
			if (FVector::Dist2D(Soul->GetActorLocation(), Where) <= 1.0f && Soul->LeftByCommander.Get() == Commander)
			{
				++Count;
			}
		}
		return Count;
	}

	/** Counts the `soul_picked_up` events raised on one ability system while it lives. */
	struct FReaperSoulEventCount
	{
		explicit FReaperSoulEventCount(UCataclysmAbilitySystemComponent* InSystem)
			: Counted(InSystem)
		{
			if (Counted)
			{
				Handle = Counted->OnActionEvent.AddLambda([this](FName Raised)
				{
					if (Raised == FName(TEXT("soul_picked_up")))
					{
						++Count;
					}
				});
			}
		}

		~FReaperSoulEventCount()
		{
			if (IsValid(Counted))
			{
				Counted->OnActionEvent.Remove(Handle);
			}
		}

		UCataclysmAbilitySystemComponent* Counted = nullptr;
		FDelegateHandle Handle;
		int32 Count = 0;
	};

	/** Leave one Soul for the scene's player at `Where` and take it from where the player stands. How many it took. */
	int32 LeaveAndTakeReaperSoul(const FReaperSoulScene& Scene, const FVector& Where)
	{
		ACataclysmReaperSoul::LeaveFor(Scene.Player, Where);
		return ACataclysmReaperSoul::TakeSoulsNear(Scene.Player);
	}

	/** Soul Strike's own stacks as the line above the skill bar holds them: one entry, or an empty one. */
	UCataclysmAbilitySystemComponent::FHeldOwnStacks SoulStrikeHeld(const FReaperSoulScene& Scene)
	{
		const TArray<UCataclysmAbilitySystemComponent::FHeldOwnStacks> Shown = Scene.System->OwnStacksByEnchantment();
		return Shown.Num() == 1 ? Shown[0] : UCataclysmAbilitySystemComponent::FHeldOwnStacks();
	}

	/** The words the line above the skill bar draws for the own stacks held, built as `ACataclysmHUD` builds them. */
	FString SoulStrikeShown(const FReaperSoulScene& Scene)
	{
		TArray<FString> Entries;
		for (const UCataclysmAbilitySystemComponent::FHeldOwnStacks& Held : Scene.System->OwnStacksByEnchantment())
		{
			Entries.Add(UCataclysmSkillBar::OwnStacksEntry(Held.Stats, Held.Held, Held.Cap, Held.bConsecutiveHits));
		}
		return UCataclysmSkillBar::NextUseLine(0.0f, 0, 0.0f, 0, 0.0f, Entries);
	}

	/** The wearer's figure for a stat, asked with no tags. */
	float ReaperSoulFigure(const FReaperSoulScene& Scene, const TCHAR* Stat)
	{
		return Scene.System->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f);
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperSixDeathsTest,
	"Cataclysm.SetBonus.Reaper6.EachDeathTheHookAnswersLeavesOneSoulWhereItDiedAndTheSummonCapsRemovalLeavesNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * R1. With the flag at nought an imp's death leaves no Soul (the control). With it at 1, exactly one Soul, left for
 * the commander, where each of these died: an imp, a thrall, a gadget (a Bolt Turret), a Tyrant's Minion and a
 * Warhound, each by its health written to nought. An imp the summon cap removes (`Explode`, which writes no health)
 * leaves none.
 */
bool FCataclysmReaperSixDeathsTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReaperSoulScene Scene;
	if (!BuildReaperSoulScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	const FVector Here = Scene.Player->GetActorLocation();

	// THE CONTROL: the flag at nought.
	HoldReaperSoulLines(Scene, /*bFlag=*/false);
	ACataclysmMinion* Unflagged = ACataclysmMinion::Spawn(Scene.Player, Here + FVector(3 * M, 0, 0), 60.0f, false,
														  TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: the control's imp"), Unflagged))
	{
		return false;
	}
	KillReaperSoulBody(Unflagged);
	TestTrue(TEXT("set-up: the control's imp died"), UCataclysmSkillEffects::IsDead(Unflagged));
	TestEqual(TEXT("control: with the flag at nought an imp's death leaves no Soul"), ReaperSoulsIn(World).Num(), 0);

	// THE TYRANT'S MINION AND THE WARHOUND, SUMMONED WHILE THEIR SECONDS ARE HELD, then the seconds taken away again,
	// so no later death summons another.
	HoldReaperSoulLines(Scene, /*bFlag=*/true, /*SummonSeconds=*/60.0f);
	ACataclysmMinion* Tyrants =
		ACataclysmMinion::SummonTyrantsMinion(Scene.Player, /*bLostWasTyrantsMinion=*/false, Here + FVector(3 * M, 3 * M, 0));
	ACataclysmMinion* Warhound = ACataclysmMinion::SummonWarhound(Scene.Player);
	HoldReaperSoulLines(Scene, /*bFlag=*/true);
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Scene.Player, Here + FVector(0, 3 * M, 0), 60.0f, false,
													TEXT("Imp"));
	ACataclysmMinion* Turret = ACataclysmMinion::Spawn(Scene.Player, Here + FVector(0, -3 * M, 0), 60.0f, false,
													   TEXT("BoltTurret"));
	ACataclysmMinion* Removed = ACataclysmMinion::Spawn(Scene.Player, Here + FVector(-3 * M, -3 * M, 0), 60.0f, false,
														TEXT("Imp"));
	ACataclysmEnemyCharacter* Thrall = ReaperSoulCreature(World, Here + FVector(-3 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a Tyrant's Minion"), Tyrants) || !TestNotNull(TEXT("set-up: a Warhound"), Warhound)
		|| !TestNotNull(TEXT("set-up: an imp"), Imp) || !TestNotNull(TEXT("set-up: a Bolt Turret"), Turret)
		|| !TestNotNull(TEXT("set-up: the imp the cap removes"), Removed)
		|| !TestNotNull(TEXT("set-up: a creature"), Thrall))
	{
		return false;
	}
	// THE WARHOUND STANDS IN FRONT OF ITS SUMMONER WHEN SUMMONED, 1.5 m off; moved to a place of its own.
	Warhound->SetActorLocation(Here + FVector(-3 * M, 3 * M, 0));
	TestTrue(TEXT("set-up: the creature is taken as a thrall"), UCataclysmCommand::Subjugate(Scene.Player, Thrall));
	TestTrue(TEXT("set-up: the turret is a gadget"), Turret->IsDeployable());
	TestTrue(TEXT("set-up: the Tyrant's Minion is one"), Tyrants->bIsTyrantsMinion);
	TestTrue(TEXT("set-up: the Warhound is one"), Warhound->bIsWarhound);

	struct FReaperSoulDeath
	{
		const TCHAR* Said;
		AActor* Body;
	};
	const FReaperSoulDeath Deaths[] = {
		{TEXT("an imp's death leaves one Soul, its commander's, where it died"), Imp},
		{TEXT("a thrall's death leaves one Soul, its commander's, where it died"), Thrall},
		{TEXT("a gadget killed leaves one Soul, its commander's, where it died"), Turret},
		{TEXT("a Tyrant's Minion's own death leaves one Soul, its commander's, where it died"), Tyrants},
		{TEXT("a Warhound's own death leaves one Soul, its commander's, where it died"), Warhound},
	};
	// EACH COUNTED AS A DIFFERENCE: one more Soul than before the death, and that one where the body died.
	for (const FReaperSoulDeath& Death : Deaths)
	{
		const FVector Died = Death.Body->GetActorLocation();
		const int32 Lying = ReaperSoulsIn(World).Num();
		KillReaperSoulBody(Death.Body);
		// AN ENEMY'S DEATH REMOVES IT AT ONCE, so a thrall is asked whether it is gone before whether it is dead.
		TestTrue(*(FString(TEXT("set-up: it died, before: ")) + Death.Said),
				 !IsValid(Death.Body) || UCataclysmSkillEffects::IsDead(Death.Body));
		TestTrue(Death.Said, ReaperSoulsAt(World, Died, Scene.Player) == 1 && ReaperSoulsIn(World).Num() == Lying + 1);
	}

	const int32 BeforeTheCap = ReaperSoulsIn(World).Num();
	Removed->Explode();
	TestFalse(TEXT("set-up: the cap's removal destroyed the imp"), IsValid(Removed));
	TestEqual(TEXT("an imp the summon cap removes writes no health and leaves no Soul"), ReaperSoulsIn(World).Num(),
			  BeforeTheCap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperSixReachTest,
	"Cataclysm.SetBonus.Reaper6.ASoulIsTakenWithinOneAndAHalfMetresAlongTheFloorRaisingTheEventOnceAndNotFromBeyond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * R3, R4 and the event. A Soul 1.55 m off along the floor is not taken (the control) and raises nothing; the player
 * 0.1 m nearer, 1.45 m off, takes it, `soul_picked_up` is raised on it once, and the Soul is gone. The Soul lies 0.9 m
 * below the capsule's centre, as a floor does, which the reach does not count.
 */
bool FCataclysmReaperSixReachTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReaperSoulScene Scene;
	if (!BuildReaperSoulScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	FReaperSoulEventCount Events(Scene.System);
	const FVector Here = Scene.Player->GetActorLocation();
	const float Beyond = ACataclysmReaperSoul::PickUpReachCm + EitherCm;
	ACataclysmReaperSoul* Soul = ACataclysmReaperSoul::LeaveFor(Scene.Player, Here + FVector(Beyond, 0, FloorBelowCm));
	if (!TestNotNull(TEXT("set-up: a Soul"), Soul))
	{
		return false;
	}
	TestEqual(TEXT("set-up: the reach is the owner's 1.5 m"), ACataclysmReaperSoul::PickUpReachCm, 150.0f);

	TestEqual(TEXT("control: from 1.55 m along the floor nothing is taken"),
			  ACataclysmReaperSoul::TakeSoulsNear(Scene.Player), 0);
	TestTrue(TEXT("and the Soul still lies there"), IsValid(Soul));
	TestEqual(TEXT("and no soul_picked_up was raised"), Events.Count, 0);

	Scene.Player->SetActorLocation(Here + FVector(2.0f * EitherCm, 0, 0));
	TestEqual(TEXT("from 1.45 m along the floor, the Soul 0.9 m below, it is taken"),
			  ACataclysmReaperSoul::TakeSoulsNear(Scene.Player), 1);
	TestEqual(TEXT("taking it raises soul_picked_up on the taker once"), Events.Count, 1);
	TestFalse(TEXT("and the Soul is gone"), IsValid(Soul));
	TestEqual(TEXT("and none lies anywhere"), ReaperSoulsIn(World).Num(), 0);
	TestEqual(TEXT("a second look takes nothing"), ACataclysmReaperSoul::TakeSoulsNear(Scene.Player), 0);
	TestEqual(TEXT("and raises nothing more"), Events.Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperSixWhoTakesTest,
	"Cataclysm.SetBonus.Reaper6.OnlyTheCommanderWhoseMinionLeftASoulTakesItAndNotAnotherPlayerOrACreature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * R2. The commander takes its own Soul from 1 m (the control). Another player and a creature, each 1 m from a second
 * Soul of that commander's, take nothing and leave it lying; the second player raises nothing. The commander, brought
 * 1 m from it, takes it.
 */
bool FCataclysmReaperSixWhoTakesTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReaperSoulScene Scene;
	FReaperSoulScene Other;
	if (!BuildReaperSoulScene(*this, World, FVector::ZeroVector, Scene)
		|| !BuildReaperSoulScene(*this, World, FVector(10 * M, 0, 0), Other))
	{
		return false;
	}
	const FVector Here = Scene.Player->GetActorLocation();
	const FVector There = Other.Player->GetActorLocation();
	ACataclysmEnemyCharacter* Creature = ReaperSoulCreature(World, There + FVector(2 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	FReaperSoulEventCount Events(Scene.System);
	FReaperSoulEventCount OtherEvents(Other.System);

	TestEqual(TEXT("control: the commander takes its own Soul from 1 m"),
			  LeaveAndTakeReaperSoul(Scene, Here + FVector(M, 0, 0)), 1);
	TestEqual(TEXT("control: and raises soul_picked_up once"), Events.Count, 1);

	ACataclysmReaperSoul* Soul = ACataclysmReaperSoul::LeaveFor(Scene.Player, There + FVector(M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a second Soul, 1 m from the other player and from the creature"), Soul))
	{
		return false;
	}
	TestEqual(TEXT("another player 1 m from it takes nothing"), ACataclysmReaperSoul::TakeSoulsNear(Other.Player), 0);
	TestEqual(TEXT("and raises nothing"), OtherEvents.Count, 0);
	TestEqual(TEXT("a creature 1 m from it takes nothing"), ACataclysmReaperSoul::TakeSoulsNear(Creature), 0);
	TestTrue(TEXT("and the Soul still lies there"), IsValid(Soul));

	Scene.Player->SetActorLocation(There + FVector(M, M, 0));
	TestEqual(TEXT("its commander, brought 1 m from it, takes it"), ACataclysmReaperSoul::TakeSoulsNear(Scene.Player), 1);
	TestEqual(TEXT("and raises soul_picked_up a second time"), Events.Count, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperSixCommanderGoneTest,
	"Cataclysm.SetBonus.Reaper6.ADeadCommanderTakesNoSoulAndOneWhoseCommanderLeftStaysUntilTheFloorIsCleared",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A living commander takes a Soul within reach (the control); dead, it takes none and the Soul stays; alive again, it
 * takes it. Then two Souls, one of a commander still there (the control) and one of a commander that has left: after
 * two minutes each still lies there, and once the floor is cleared neither does.
 */
bool FCataclysmReaperSixCommanderGoneTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReaperSoulScene Scene;
	FReaperSoulScene Leaving;
	if (!BuildReaperSoulScene(*this, World, FVector::ZeroVector, Scene)
		|| !BuildReaperSoulScene(*this, World, FVector(10 * M, 0, 0), Leaving))
	{
		return false;
	}
	const FVector Here = Scene.Player->GetActorLocation();

	TestEqual(TEXT("control: a living commander takes a Soul 1 m off"),
			  LeaveAndTakeReaperSoul(Scene, Here + FVector(M, 0, 0)), 1);

	ACataclysmReaperSoul* Waiting = ACataclysmReaperSoul::LeaveFor(Scene.Player, Here + FVector(M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a second Soul 1 m off"), Waiting))
	{
		return false;
	}
	UCataclysmSkillEffects::MarkDead(Scene.Player);
	TestEqual(TEXT("a dead commander 1 m from its Soul takes nothing"),
			  ACataclysmReaperSoul::TakeSoulsNear(Scene.Player), 0);
	TestTrue(TEXT("and the Soul still lies there"), IsValid(Waiting));
	UCataclysmSkillEffects::ClearDead(Scene.Player);
	TestEqual(TEXT("alive again, it takes it"), ACataclysmReaperSoul::TakeSoulsNear(Scene.Player), 1);

	ACataclysmReaperSoul* Kept = ACataclysmReaperSoul::LeaveFor(Scene.Player, Here + FVector(-5 * M, 0, 0));
	ACataclysmReaperSoul* Orphaned =
		ACataclysmReaperSoul::LeaveFor(Leaving.Player, Leaving.Player->GetActorLocation() + FVector(5 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a Soul of the commander who stays"), Kept)
		|| !TestNotNull(TEXT("set-up: a Soul of the commander who leaves"), Orphaned))
	{
		return false;
	}
	Leaving.Player->Destroy();
	CataclysmTestWorld::RunClock(World, 120.0f, 0.5f);
	TestTrue(TEXT("control: two minutes on, the Soul whose commander is still there lies there"), IsValid(Kept));
	TestTrue(TEXT("two minutes on, the Soul whose commander has left lies there"), IsValid(Orphaned));

	UCataclysmFloorContents::ClearTheFloor(*World, /*bCarryFollowers=*/true);
	TestTrue(TEXT("once the floor is cleared, neither lies there"), !IsValid(Kept) && !IsValid(Orphaned));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperSixLastsTheFloorTest,
	"Cataclysm.SetBonus.Reaper6.ASoulHasNoLifetimeAndIsGoneWhenTheFloorIsClearedEitherWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * R7. An imp told a minute's life is gone two minutes on (the control: the clock ends a lifetime). A Soul left beside
 * it at the same moment has no lifespan and still lies there; the floor cleared as at a floor change takes it, and a
 * second Soul is taken by the floor cleared as a save's restore clears it.
 */
bool FCataclysmReaperSixLastsTheFloorTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReaperSoulScene Scene;
	if (!BuildReaperSoulScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	const FVector Here = Scene.Player->GetActorLocation();
	ACataclysmMinion* Timed = ACataclysmMinion::Spawn(Scene.Player, Here + FVector(3 * M, 0, 0), 60.0f, false,
													  TEXT("Imp"));
	ACataclysmReaperSoul* Soul = ACataclysmReaperSoul::LeaveFor(Scene.Player, Here + FVector(5 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: an imp told a minute"), Timed) || !TestNotNull(TEXT("set-up: a Soul"), Soul))
	{
		return false;
	}
	TestEqual(TEXT("the Soul has no lifespan"), Soul->GetLifeSpan(), 0.0f, 0.0001f);

	CataclysmTestWorld::RunClock(World, 120.0f, 0.5f);
	TestFalse(TEXT("control: two minutes on, the imp's minute has ended it"), IsValid(Timed));
	TestTrue(TEXT("two minutes on, the Soul still lies there"), IsValid(Soul));

	UCataclysmFloorContents::ClearTheFloor(*World, /*bCarryFollowers=*/true);
	TestFalse(TEXT("the floor cleared at a floor change takes it"), IsValid(Soul));

	ACataclysmReaperSoul* Second = ACataclysmReaperSoul::LeaveFor(Scene.Player, Here + FVector(5 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a second Soul"), Second))
	{
		return false;
	}
	UCataclysmFloorContents::ClearTheFloor(*World, /*bCarryFollowers=*/false);
	TestFalse(TEXT("and the floor cleared as a save's restore clears it takes the second"), IsValid(Second));
	TestEqual(TEXT("and none lies anywhere"), ReaperSoulsIn(World).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperSixStacksTest,
	"Cataclysm.SetBonus.Reaper6.EachSoulTakenAddsOneStackOfTenPercentUpToTenEndingFiveSecondsAfterTheLast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * R5, on the made-up rows. Before any Soul, no stack and the damage figures are their bases (the control). Each Soul
 * taken adds one stack, shown as one entry on two stats with a cap of 10; at three, each damage figure is 30 above its
 * base; at ten, ten; two more leave ten. 4.9 s after the last, ten still; 5.1 s after it, none, and the figures are
 * their bases again.
 */
bool FCataclysmReaperSixStacksTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReaperSoulScene Scene;
	if (!BuildReaperSoulScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	FReaperSoulEventCount Events(Scene.System);
	const FVector Near = Scene.Player->GetActorLocation() + FVector(M, 0, 0);

	const float AttackBase = ReaperSoulFigure(Scene, TEXT("attack_damage"));
	const float SpellBase = ReaperSoulFigure(Scene, TEXT("spell_damage"));
	TestEqual(TEXT("control: before any Soul, no stack is held"), SoulStrikeHeld(Scene).Held, 0);
	TestEqual(TEXT("control: and attack damage is its base"), AttackBase, DamageBase, 0.001f);
	TestEqual(TEXT("control: and spell damage is its base"), SpellBase, DamageBase, 0.001f);

	LeaveAndTakeReaperSoul(Scene, Near);
	TestEqual(TEXT("one Soul taken, one stack"), SoulStrikeHeld(Scene).Held, 1);
	TestEqual(TEXT("shown as one entry on two stats"), SoulStrikeHeld(Scene).Stats.Num(), 2);
	TestEqual(TEXT("with a cap of 10"), SoulStrikeHeld(Scene).Cap, StrikeCap);

	LeaveAndTakeReaperSoul(Scene, Near);
	LeaveAndTakeReaperSoul(Scene, Near);
	TestEqual(TEXT("three taken, three stacks"), SoulStrikeHeld(Scene).Held, 3);
	TestEqual(TEXT("at three, attack damage is 30% of its base above it"),
			  ReaperSoulFigure(Scene, TEXT("attack_damage")) - AttackBase, DamageBase * 3.0f * StrikePercent / 100.0f,
			  0.001f);
	TestEqual(TEXT("and spell damage too"), ReaperSoulFigure(Scene, TEXT("spell_damage")) - SpellBase,
			  DamageBase * 3.0f * StrikePercent / 100.0f, 0.001f);

	for (int32 Index = 3; Index < StrikeCap; ++Index)
	{
		LeaveAndTakeReaperSoul(Scene, Near);
	}
	TestEqual(TEXT("ten taken, ten stacks"), SoulStrikeHeld(Scene).Held, StrikeCap);
	LeaveAndTakeReaperSoul(Scene, Near);
	LeaveAndTakeReaperSoul(Scene, Near);
	TestEqual(TEXT("twelve taken, still ten"), SoulStrikeHeld(Scene).Held, StrikeCap);
	TestEqual(TEXT("and twelve soul_picked_up were raised"), Events.Count, 12);

	CataclysmTestWorld::RunClock(World, StrikeSeconds - EitherSeconds);
	TestEqual(TEXT("4.9 s after the last, ten still"), SoulStrikeHeld(Scene).Held, StrikeCap);
	CataclysmTestWorld::RunClock(World, 2.0f * EitherSeconds);
	TestEqual(TEXT("5.1 s after the last, none"), SoulStrikeHeld(Scene).Held, 0);
	TestEqual(TEXT("and attack damage is its base again"), ReaperSoulFigure(Scene, TEXT("attack_damage")), AttackBase,
			  0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperSixAtTheCapTest,
	"Cataclysm.SetBonus.Reaper6.ASoulTakenAtTenStacksIsStillTakenRaisesTheEventAndRestartsTheWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * R6. Ten taken and nothing after: 5.1 s on, none (the control: the window of the tenth alone). Ten taken again, 3 s
 * on an eleventh: it is taken, the event is raised, the Soul is gone and ten are held; 7 s after the tenth, 4 s after
 * the eleventh, ten still; 5.2 s after the eleventh, none.
 */
bool FCataclysmReaperSixAtTheCapTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReaperSoulScene Scene;
	if (!BuildReaperSoulScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	FReaperSoulEventCount Events(Scene.System);
	const FVector Near = Scene.Player->GetActorLocation() + FVector(M, 0, 0);

	for (int32 Index = 0; Index < StrikeCap; ++Index)
	{
		LeaveAndTakeReaperSoul(Scene, Near);
	}
	TestEqual(TEXT("set-up: ten taken, ten stacks"), SoulStrikeHeld(Scene).Held, StrikeCap);
	CataclysmTestWorld::RunClock(World, StrikeSeconds + EitherSeconds);
	TestEqual(TEXT("control: with nothing after the tenth, none 5.1 s on"), SoulStrikeHeld(Scene).Held, 0);

	for (int32 Index = 0; Index < StrikeCap; ++Index)
	{
		LeaveAndTakeReaperSoul(Scene, Near);
	}
	TestEqual(TEXT("set-up: ten taken again, ten stacks"), SoulStrikeHeld(Scene).Held, StrikeCap);
	CataclysmTestWorld::RunClock(World, 3.0f);
	const int32 Before = Events.Count;
	ACataclysmReaperSoul* Eleventh = ACataclysmReaperSoul::LeaveFor(Scene.Player, Near);
	if (!TestNotNull(TEXT("set-up: an eleventh Soul"), Eleventh))
	{
		return false;
	}
	TestEqual(TEXT("at ten stacks the eleventh is still taken"), ACataclysmReaperSoul::TakeSoulsNear(Scene.Player), 1);
	TestEqual(TEXT("and the event is raised"), Events.Count - Before, 1);
	TestFalse(TEXT("and the Soul is gone"), IsValid(Eleventh));
	TestEqual(TEXT("and ten are held"), SoulStrikeHeld(Scene).Held, StrikeCap);

	CataclysmTestWorld::RunClock(World, 4.0f);
	TestEqual(TEXT("7 s after the tenth, 4 s after the eleventh, ten still: the window restarted"),
			  SoulStrikeHeld(Scene).Held, StrikeCap);
	CataclysmTestWorld::RunClock(World, 1.0f + 2.0f * EitherSeconds);
	TestEqual(TEXT("5.2 s after the eleventh, none"), SoulStrikeHeld(Scene).Held, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperSixSeenTest,
	"Cataclysm.SetBonus.Reaper6.TheHudNamesEachSoulInItsColourAndTheLineAboveTheBarCountsSoulStrike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * R8 and its addition. Before any Soul the HUD's reading names none (the control). One left 3 m off is named, in its
 * one colour (THE TAG'S TEXT IS NOT TESTED: the HUD draws `ACataclysmReaperSoul::NameText` itself, and a comparison of
 * that constant with its own words could not fail, so it was taken out, ruled 2026-10-18); taken, it is named no more. Before any is taken the line above the skill bar holds nothing (the
 * control); after three, it reads "attack/spell damage 3/10", which is where the HUD already shows a row's own stacks.
 */
bool FCataclysmReaperSixSeenTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReaperSoulScene Scene;
	if (!BuildReaperSoulScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	const FVector Here = Scene.Player->GetActorLocation();
	const auto Named = [&]()
	{
		TArray<ACataclysmReaperSoul*> Out;
		ACataclysmReaperSoul::SoulsToName(World, Scene.Player->GetActorLocation(),
										  UCataclysmDropPickup::NameShownRangeCm, Out);
		return Out;
	};

	TestEqual(TEXT("control: before any Soul, the HUD names none"), Named().Num(), 0);
	TestEqual(TEXT("control: with none taken, the line above the bar holds nothing"), SoulStrikeShown(Scene),
			  FString());
	ACataclysmReaperSoul* Soul = ACataclysmReaperSoul::LeaveFor(Scene.Player, Here + FVector(3 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a Soul 3 m off"), Soul))
	{
		return false;
	}
	const TArray<ACataclysmReaperSoul*> Standing = Named();
	TestTrue(TEXT("a Soul 3 m off is named"), Standing.Num() == 1 && Standing[0] == Soul);
	TestTrue(TEXT("in its one colour, a pale blue"),
			 ACataclysmReaperSoul::NameColour().Equals(FLinearColor(0.55f, 0.85f, 1.0f, 1.0f), 0.0001f));
	Scene.Player->SetActorLocation(Here + FVector(2 * M, 0, 0));
	TestEqual(TEXT("set-up: walked to 1 m of it, it is taken"), ACataclysmReaperSoul::TakeSoulsNear(Scene.Player), 1);
	TestEqual(TEXT("taken, it is named no more"), Named().Num(), 0);

	const FVector Near = Scene.Player->GetActorLocation() + FVector(M, 0, 0);
	LeaveAndTakeReaperSoul(Scene, Near);
	LeaveAndTakeReaperSoul(Scene, Near);
	TestEqual(TEXT("three taken, the line above the bar reads \"attack/spell damage 3/10\""), SoulStrikeShown(Scene),
			  FString(TEXT("attack/spell damage 3/10")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperSixLifetimeTest,
	"Cataclysm.SetBonus.Reaper6.AMinionWhoseLifetimeRunsOutLeavesNoSoul",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * R1, the death left out. An imp killed leaves one Soul (the control). An imp told 1 s of life, 1.5 s on, is gone and
 * has left none.
 */
bool FCataclysmReaperSixLifetimeTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FReaperSoulScene Scene;
	if (!BuildReaperSoulScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	const FVector Here = Scene.Player->GetActorLocation();
	ACataclysmMinion* Killed = ACataclysmMinion::Spawn(Scene.Player, Here + FVector(3 * M, 0, 0), 60.0f, false,
													   TEXT("Imp"));
	ACataclysmMinion* Timed = ACataclysmMinion::Spawn(Scene.Player, Here + FVector(-3 * M, 0, 0), 1.0f, false,
													  TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: an imp to kill"), Killed) || !TestNotNull(TEXT("set-up: an imp told 1 s"), Timed))
	{
		return false;
	}
	KillReaperSoulBody(Killed);
	TestEqual(TEXT("control: an imp killed leaves one Soul"), ReaperSoulsIn(World).Num(), 1);

	CataclysmTestWorld::RunClock(World, 1.5f);
	TestFalse(TEXT("set-up: 1.5 s on, the imp told 1 s is gone"), IsValid(Timed));
	TestEqual(TEXT("and its lifetime running out left no Soul"), ReaperSoulsIn(World).Num(), 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
