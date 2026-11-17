// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmReaperSoul.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Reaper's Embrace's ten-piece bonus, the mechanism. The owner's ruling #9 of 2026-10-17 (cull is a threshold of 5% of
 * maximum health per Soul Strike stack, capped by rarity: Common 50%, Elite and Legendary 25%, Herald 10%, never a
 * boss) and the coordinating session's rulings C1-C8 of 2026-10-19.
 *
 * "For each stack of Soul Strike you gain 5% cull on hit. Killing an enemy instanly spawns one of your minions, up to
 * your max"
 *
 * THE ROWS ARE MADE UP HERE, IN THE SHAPE THE ENTRY GIVES THE ENCHANTMENT SESSION, beside the six-piece's three (a
 * wearer of ten pieces wears the six-piece too): `cull_percent_per_soul_strike_stack` a flat 5 and
 * `minion_kill_spawns_minion` a flat 1, with `minion_death_leaves_soul` a flat 1 and Soul Strike's two own-stack rows
 * on `soul_picked_up`. They are put on a real non-set enchantment, "Double your energy shield", and passed through
 * `UCataclysmItemModifiers::AccumulateEnchantmentsInto`, as the six-piece's tests do. The real rows, worn at ten pieces
 * with a control at nine, are the enchantment session's test.
 *
 * SOUL STRIKE'S STACKS ARE GIVEN BY RAISING `soul_picked_up`, exactly what taking a Soul raises
 * (`ACataclysmReaperSoul::TakeSoulsNear`), so the six-piece's own grant rows hold them. Nothing moves the clock while
 * they are read, except where a test says so, and that is inside their 5 s.
 *
 * A REAL PLAYER CHARACTER STRIKES REAL CREATURES, as the execute's tests do: the pawn hears its own blows, asks the
 * cull, and the creature's attribute set acts on it once the blow's damage is written. Every creature has 10,000
 * maximum health and its RUNG IS WRITTEN TO ITS FIELD, as a rung typed onto a placed creature is, so no stat block, no
 * modifier and no body scale changes what a blow takes: one measured blow serves every creature of a test. What the
 * player's blow takes is measured on a creature 30 m along +Y with health to spare, after the stacks are given,
 * because Soul Strike's rows raise the player's damage.
 *
 * EVERY ACTOR HAS A REAL TEAM. The players are on the players' side, built by the client path; their minions take
 * their side; a creature is of the monsters' side, and so is a minion a creature summoned. No blow is a critical
 * strike: the roll is pinned after the world is made. Every body stands at least 1 m from every other, and nothing
 * here deals damage in an area.
 *
 * NO TEST PRESSES A SKILL: every blow is `UCataclysmSkillEffects::ApplyHit` or `ApplyDirectDamage` called with the
 * delivery the named kind of blow carries, so no test needs a resource a skill costs.
 */
namespace CataclysmSetBonusReaperTenTest
{
	using Vital = UCataclysmVitalAttributeSet;

	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** Every creature's maximum health, and the far creature's health to spare. */
	constexpr float ReaperCullHealth = 10000.0f;

	/** The real enchantment the made-up rows are put on, and the drawback beside it, which has no effect row. */
	const TCHAR* const ReaperCullBenefit = TEXT("Positive_Double_your_energy_shield");
	const TCHAR* const ReaperCullDrawback = TEXT("Negative_Can_t_use_a_basic_attack");

	/** The shipped execute row's enchantment, read from `game/Data/EnchantmentEffects.csv`. */
	const TCHAR* const ShippedExecute = TEXT("Positive_Strike_skills_have_a_10_20_chance_to_execute_e");

	/** The base each damage stat line holds. */
	constexpr float ReaperCullDamageBase = 100.0f;

	/** One of the shipped tables, read from `game/Data`. Null when it does not load. */
	template <typename RowType>
	UDataTable* LoadReaperCullData(const TCHAR* FileName)
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

	/** The five made-up rows, as a CSV the effect table reads: the six-piece's three and the ten-piece's two. */
	FString ReaperCullRowsCsv()
	{
		FString Csv = TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,RequiredTags,Condition,ConditionValue,Scale,"
						   "ScaleStep,Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,EverySeconds,"
						   "EveryNth,ScaleStepHigh,StackSecondsHigh,Condition2,ConditionValue2,ConditionValueHigh,"
						   "TriggerCooldown,EventValue,Ailment,DamageShare\n");
		Csv += FString::Printf(TEXT("%s#1,%s,minion_death_leaves_soul,flat,1,1,,,0,,0,,,,0,0,0,0,0,0,0,,0,0,0,0,,0\n"),
							   ReaperCullBenefit, ReaperCullBenefit);
		Csv += FString::Printf(TEXT("%s#2,%s,attack_damage,increased,10,10,,,0,own_stacks,1,,soul_picked_up,,10,5,0,0,0,"
									"0,0,,0,0,0,0,,0\n"), ReaperCullBenefit, ReaperCullBenefit);
		Csv += FString::Printf(TEXT("%s#3,%s,spell_damage,increased,10,10,,,0,own_stacks,1,,soul_picked_up,,10,5,0,0,0,"
									"0,0,,0,0,0,0,,0\n"), ReaperCullBenefit, ReaperCullBenefit);
		Csv += FString::Printf(TEXT("%s#4,%s,cull_percent_per_soul_strike_stack,flat,5,5,,,0,,0,,,,0,0,0,0,0,0,0,,0,0,0,"
									"0,,0\n"), ReaperCullBenefit, ReaperCullBenefit);
		Csv += FString::Printf(TEXT("%s#5,%s,minion_kill_spawns_minion,flat,1,1,,,0,,0,,,,0,0,0,0,0,0,0,,0,0,0,0,,0\n"),
							   ReaperCullBenefit, ReaperCullBenefit);
		return Csv;
	}

	/** What a set of rows becomes on a wearer: the stat lines and the worn actions. */
	struct FReaperCullMade
	{
		TMap<FName, TArray<FCataclysmStatModifier>> Totals;
		TArray<FCataclysmPoolAction> Actions;
	};

	/** One item carrying `Benefit`, passed through the loader with `Effects`. False when a table did not load. */
	bool AccumulateReaperCullItem(FAutomationTestBase& Test, UDataTable* Effects, const TCHAR* Benefit,
								  FReaperCullMade& Out)
	{
		UDataTable* Positive = LoadReaperCullData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
		UDataTable* Negative = LoadReaperCullData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
		if (!Test.TestNotNull(TEXT("set-up: the positive enchantments"), Positive)
			|| !Test.TestNotNull(TEXT("set-up: the negative enchantments"), Negative)
			|| !Test.TestNotNull(TEXT("set-up: an effect table"), Effects))
		{
			return false;
		}
		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(Benefit);
		Rolled.Negative = FName(ReaperCullDrawback);
		Item.Enchantments.Add(Rolled);
		Item.EnchantmentCount = 1;
		UCataclysmItemModifiers::AccumulateEnchantmentsInto(Out.Totals, {Item}, Effects, Positive, Negative,
															&Out.Actions);
		return true;
	}

	/** The five made-up rows passed through the loader. */
	bool MakeReaperCullRows(FAutomationTestBase& Test, FReaperCullMade& Out)
	{
		UDataTable* Effects = NewObject<UDataTable>();
		Effects->RowStruct = FCataclysmEnchantmentEffectRow::StaticStruct();
		if (!Test.TestTrue(TEXT("set-up: the five made-up rows read as an effect table"),
						   Effects->CreateTableFromCSVString(ReaperCullRowsCsv()).Num() == 0))
		{
			return false;
		}
		return AccumulateReaperCullItem(Test, Effects, ReaperCullBenefit, Out);
	}

	/** A player character on a player state, and what it wears. */
	struct FReaperCullScene
	{
		ACataclysmPlayerCharacter* Player = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
		FReaperCullMade Made;
	};

	/**
	 * REPLACES THE WHOLE STAT LINE: the two damage lines on their base with Soul Strike's rows; the three flags' and the
	 * cull's lines when `bTenPiece`, from the made-up rows; and a flat line for each of `Flats`. And the worn actions:
	 * Soul Strike's grants and `Extra`.
	 */
	void WearReaperCull(const FReaperCullScene& Scene, bool bTenPiece, const TMap<FName, float>& Flats = {},
						const TArray<FCataclysmPoolAction>& Extra = {})
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		const auto Add = [&Lines, &Scene](const TCHAR* Stat, float Base)
		{
			FCataclysmStatInputs& Line = Lines.Add(FName(Stat));
			Line.Base = Base;
			if (const TArray<FCataclysmStatModifier>* Modifiers = Scene.Made.Totals.Find(FName(Stat)))
			{
				Line.Modifiers = *Modifiers;
			}
		};
		Add(TEXT("attack_damage"), ReaperCullDamageBase);
		Add(TEXT("spell_damage"), ReaperCullDamageBase);
		if (bTenPiece)
		{
			Add(ACataclysmReaperSoul::MinionDeathLeavesSoulStat, 0.0f);
			Add(UCataclysmAbilitySystemComponent::CullPercentPerSoulStrikeStackStat, 0.0f);
			Add(UCataclysmSummonSkill::KillSpawnsMinionStat, 0.0f);
		}
		for (const TPair<FName, float>& Each : Flats)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = Each.Value;
			FCataclysmStatInputs& Line = Lines.Add(Each.Key);
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
		}
		Scene.System->SetStatInputs(MoveTemp(Lines));
		TArray<FCataclysmPoolAction> Actions = Scene.Made.Actions;
		Actions.Append(Extra);
		Scene.System->SetPoolActions(MoveTemp(Actions));
	}

	/**
	 * A player at `Where` wearing the made-up rows, at full health and mana, with attack damage 100. False, with a failed
	 * set-up assertion, when anything could not be built.
	 */
	bool BuildReaperCullScene(FAutomationTestBase& Test, UWorld* World, const FVector& Where, FReaperCullScene& Out)
	{
		if (!MakeReaperCullRows(Test, Out.Made))
		{
			return false;
		}
		Out.State = World->SpawnActor<ACataclysmPlayerState>();
		Out.Player = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("set-up: a player state"), Out.State)
			|| !Test.TestNotNull(TEXT("set-up: a player"), Out.Player))
		{
			return false;
		}
		Out.Player->SetPlayerState(Out.State);
		Out.Player->OnRep_PlayerState();
		Out.System = Out.State->GetCataclysmAbilitySystemComponent();
		if (!Test.TestNotNull(TEXT("set-up: the player's ability system"), Out.System))
		{
			return false;
		}
		Out.System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		Out.System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
		Out.System->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
		Out.System->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), 1000.0f);
		Out.System->SetNumericAttributeBase(Vital::GetManaAttribute(), 1000.0f);
		WearReaperCull(Out, /*bTenPiece=*/true);
		return true;
	}

	/** Give `Count` stacks of Soul Strike, as `Count` Souls taken give them. */
	void GiveReaperCullStacks(const FReaperCullScene& Scene, int32 Count)
	{
		for (int32 Each = 0; Each < Count; ++Each)
		{
			Scene.System->ActOnEvent(FName(UCataclysmAbilitySystemComponent::SoulPickedUpEvent));
		}
	}

	/** A creature of the monsters' side standing here, with 10,000 maximum health and full, on this rung. */
	ACataclysmEnemyCharacter* ReaperCullCreature(UWorld* World, const FVector& At, int32 Rung = 0)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(ReaperCullHealth);
			Made->RarityStep = Rung;
		}
		return Made;
	}

	float ReaperCullHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* System = IsValid(Who) ? UCataclysmTargeting::AbilitySystemOf(Who) : nullptr;
		return System ? System->GetNumericAttribute(Vital::GetHealthAttribute()) : 0.0f;
	}

	float ReaperCullMaximumOf(const AActor* Who)
	{
		const UAbilitySystemComponent* System = IsValid(Who) ? UCataclysmTargeting::AbilitySystemOf(Who) : nullptr;
		return System ? System->GetNumericAttribute(Vital::GetMaxHealthAttribute()) : 0.0f;
	}

	/** This much health, written to the character's health and nothing else. */
	void ReaperCullHold(AActor* Who, float Health)
	{
		if (UAbilitySystemComponent* System = IsValid(Who) ? UCataclysmTargeting::AbilitySystemOf(Who) : nullptr)
		{
			System->SetNumericAttributeBase(Vital::GetHealthAttribute(), Health);
		}
	}

	/** `SharePercent` of the character's maximum health. */
	float ReaperCullShare(const AActor* Who, float SharePercent)
	{
		return ReaperCullMaximumOf(Who) * SharePercent / 100.0f;
	}

	bool ReaperCullIsGone(const AActor* Who) { return !IsValid(Who) || UCataclysmSkillEffects::IsDead(Who); }

	/** A strike skill of the player's carrying `Tag`, in `Slot`, for a blow to be dealt in the name of. */
	UCataclysmStrikeSkill* ReaperCullSkill(ACataclysmPlayerCharacter* Owner, const TCHAR* Name, const TCHAR* Tag,
										   ECataclysmAbilitySlot InSlot = ECataclysmAbilitySlot::Special)
	{
		UCataclysmStrikeSkill* Made = NewObject<UCataclysmStrikeSkill>(Owner);
		Made->SkillName = Name;
		Made->Slot = InSlot;
		Made->SkillTags.AddTag(FGameplayTag::RequestGameplayTag(FName(Tag)));
		return Made;
	}

	/** One blow of the player's at 100%, never a critical strike, in the name of `Skill`, cast from `CastFrom` if any. */
	void ReaperCullBlow(const FReaperCullScene& Scene, AActor* Target, const UCataclysmSkillTemplate* Skill,
						AActor* CastFrom = nullptr)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = 0.0f;
		Delivery.Skill = Skill;
		Delivery.CastBy = CastFrom;
		UCataclysmSkillEffects::ApplyHit(Scene.Player, Target, /*DamagePercent=*/100.0f,
										 Skill ? Skill->SkillTags : FGameplayTagContainer(), Delivery);
	}

	/** What one such blow takes from the far creature's health, which is then given back. */
	float ReaperCullTaken(const FReaperCullScene& Scene, AActor* Far, const UCataclysmSkillTemplate* Skill)
	{
		ReaperCullHold(Far, ReaperCullHealth);
		ReaperCullBlow(Scene, Far, Skill);
		const float Taken = ReaperCullHealth - ReaperCullHealthOf(Far);
		ReaperCullHold(Far, ReaperCullHealth);
		return Taken;
	}

	/** A stack granted each time `Event` is heard, so the count of stacks is the count of events. */
	FCataclysmPoolAction ReaperCullCountOn(const TCHAR* Event, FName Key)
	{
		FCataclysmPoolAction Grant;
		Grant.Event = FName(Event);
		Grant.StackKey = Key;
		Grant.StackSeconds = 60.0f;
		Grant.StackCap = 10;
		Grant.TriggerKey = Key;
		return Grant;
	}

	/** A summon skill granted to the scene's player in `InSlot`, summoning `Kind`, up to 3 at once. */
	UCataclysmSummonSkill* GrantReaperCullSummon(const FReaperCullScene& Scene, const TCHAR* Kind,
												 ECataclysmAbilitySlot InSlot)
	{
		const FGameplayAbilitySpecHandle Handle = Scene.System->GiveAbilityInSlot(
			UCataclysmSummonSkill::StaticClass(), InSlot, /*Level=*/100, Scene.Player);
		FGameplayAbilitySpec* Spec = Scene.System->FindAbilitySpecFromHandle(Handle);
		UCataclysmSummonSkill* Made = Spec ? Cast<UCataclysmSummonSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Made)
		{
			Made->SkillName = FString::Printf(TEXT("Summon %s"), Kind);
			Made->Params = UCataclysmSkillShapes::ParseParams(
				*FString::Printf(TEXT("Count=1; MaxActive=3; Duration=20; Radius=3; Minions=%s:1"), Kind));
		}
		return Made;
	}

	/** The minions `Commander` commands now, by class. */
	TArray<ACataclysmMinion*> ReaperCullMinionsOf(const AActor* Commander)
	{
		TArray<ACataclysmMinion*> Found;
		for (AActor* Follower : UCataclysmCommand::ThingsCommandedBy(Commander))
		{
			if (ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower))
			{
				Found.Add(Minion);
			}
		}
		return Found;
	}

	/** What one death notice said. */
	struct FReaperCullDeathHeard
	{
		const AActor* Victim = nullptr;
		const AActor* Killer = nullptr;
		FName Skill;
		float Overkill = 0.0f;
	};

	/** The execute roll pinned, and put back to "rolled" afterwards. */
	struct FReaperCullExecuteRoll
	{
		explicit FReaperCullExecuteRoll(const TCHAR* Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.ExecuteRoll"));
			Set(Roll);
		}

		~FReaperCullExecuteRoll()
		{
			Set(TEXT("-1"));
		}

		void Set(const TCHAR* Roll)
		{
			if (Variable)
			{
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
	};
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperTenThresholdTest,
	"Cataclysm.SetBonus.Reaper10.WithThreeStacksAHitLeavingACommonEnemyJustBelowFifteenPercentCullsItAndJustAboveDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * C1, C3 and C4 on a Common creature. THE CONTROL IS READ FIRST: with no stack of Soul Strike, a blow leaving a creature
 * just below 15% of its maximum leaves it alive, and asks nothing of it. Then with three stacks, 5% each: a blow
 * leaving a creature just below 15% kills it, and one leaving another just above 15% does not, having taken what the
 * blow takes and no more.
 *
 * STANDING: the player at the origin; three creatures at 4 m, 8 m and 12 m along +X; the far creature 30 m along +Y.
 */
bool FCataclysmReaperTenThresholdTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	FReaperCullScene Scene;
	if (!BuildReaperCullScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Unstacked = ReaperCullCreature(World, FVector(4 * M, 0, 0));
	ACataclysmEnemyCharacter* Below = ReaperCullCreature(World, FVector(8 * M, 0, 0));
	ACataclysmEnemyCharacter* Above = ReaperCullCreature(World, FVector(12 * M, 0, 0));
	ACataclysmEnemyCharacter* Far = ReaperCullCreature(World, FVector(0, 30 * M, 0));
	if (!TestTrue(TEXT("set-up: four creatures"), Unstacked && Below && Above && Far))
	{
		return false;
	}
	UCataclysmStrikeSkill* Swing = ReaperCullSkill(Scene.Player, TEXT("Test Swing"), TEXT("Type.Strike"));

	// THE CONTROL: no stack.
	const float TakenUnstacked = ReaperCullTaken(Scene, Far, Swing);
	const float Share = ReaperCullShare(Unstacked, 15.0f);
	if (!TestEqual(TEXT("set-up: no stack of Soul Strike is held"), Scene.System->SoulStrikeStacksHeld(), 0)
		|| !TestTrue(*FString::Printf(TEXT("set-up: the blow takes something, under half the share: %.2f of %.2f"),
									  TakenUnstacked, Share),
					 TakenUnstacked > 1.0f && TakenUnstacked < Share / 2.0f))
	{
		return false;
	}
	ReaperCullHold(Unstacked, Share + TakenUnstacked / 2.0f);
	ReaperCullBlow(Scene, Unstacked, Swing);
	TestFalse(TEXT("control: with no stack a creature left just below 15% lives"), ReaperCullIsGone(Unstacked));
	TestTrue(TEXT("control: and it is below 15%"), ReaperCullHealthOf(Unstacked) < Share);

	// THREE STACKS, 15%.
	GiveReaperCullStacks(Scene, 3);
	const float Taken = ReaperCullTaken(Scene, Far, Swing);
	if (!TestEqual(TEXT("set-up: three stacks of Soul Strike are held"), Scene.System->SoulStrikeStacksHeld(), 3)
		|| !TestTrue(*FString::Printf(TEXT("set-up: the blow takes something, under half the share: %.2f of %.2f"),
									  Taken, Share),
					 Taken > 1.0f && Taken < Share / 2.0f))
	{
		return false;
	}
	TestEqual(TEXT("three stacks cull a Common creature below 15%"),
			  Scene.System->CullSharePercentAgainst(Below), 15.0f, 0.001f);

	ReaperCullHold(Below, Share + Taken / 2.0f);
	ReaperCullBlow(Scene, Below, Swing);
	TestTrue(TEXT("a creature the blow left just below 15% is culled"), ReaperCullIsGone(Below));

	ReaperCullHold(Above, Share + Taken * 2.0f);
	ReaperCullBlow(Scene, Above, Swing);
	TestFalse(TEXT("a creature the blow left just above 15% lives"), ReaperCullIsGone(Above));
	TestEqual(TEXT("having lost what the blow takes and no more"), ReaperCullHealthOf(Above), Share + Taken, 0.01f);
	const UCataclysmAbilitySystemComponent* AboveSystem =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Above));
	TestFalse(TEXT("and no cull is left waiting on it"), AboveSystem && AboveSystem->CullAskedNow().IsAsked());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperTenRarityTest,
	"Cataclysm.SetBonus.Reaper10.AtTenStacksTheCapsAreFiftyTwentyFiveTwentyFiveAndTenAndABossIsNeverCulled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner's ruling #9 and C3, at ten stacks, 50% uncapped. For each rung, a creature the blow leaves just below the
 * cap is culled and one it leaves just above the next figure is not: Common below 50% and not at 55%; Elite and
 * Legendary below 25% and not at 30%; Herald below 10% and not at 15%. A Boss and a Cataclysm Boss left at 5% live.
 * THE CONTROL IS READ FIRST: the Common creature at 55% lives, so ten stacks are not culling everything. Each rung's cap
 * is read from `CullCapPercentFor` against the owner's figures.
 *
 * STANDING: the player at the origin; ten creatures 4 m apart along +X from 4 m to 40 m; the far creature 30 m along +Y.
 */
bool FCataclysmReaperTenRarityTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	FReaperCullScene Scene;
	if (!BuildReaperCullScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Far = ReaperCullCreature(World, FVector(0, 30 * M, 0));
	UCataclysmStrikeSkill* Swing = ReaperCullSkill(Scene.Player, TEXT("Test Swing"), TEXT("Type.Strike"));
	GiveReaperCullStacks(Scene, 10);
	const float Taken = Far ? ReaperCullTaken(Scene, Far, Swing) : 0.0f;
	if (!TestNotNull(TEXT("set-up: the far creature"), Far)
		|| !TestEqual(TEXT("set-up: ten stacks of Soul Strike are held"), Scene.System->SoulStrikeStacksHeld(), 10)
		|| !TestTrue(*FString::Printf(TEXT("set-up: the blow takes something, under 5%% of a maximum: %.2f"), Taken),
					 Taken > 1.0f && Taken < ReaperCullHealth * 0.05f))
	{
		return false;
	}

	struct FReaperCullRung
	{
		const TCHAR* Said;
		int32 Rung;
		float Cap;
		float SparedAt;
	};
	// THE OWNER'S FIGURES, written here as numbers and not read from the engine's constants.
	const FReaperCullRung Rungs[] = {
		{TEXT("Common"), 0, 50.0f, 55.0f},
		{TEXT("Elite"), 1, 25.0f, 30.0f},
		{TEXT("Legendary"), 2, 25.0f, 30.0f},
		{TEXT("Herald"), 3, 10.0f, 15.0f},
	};
	int32 Place = 1;
	bool bControlRead = false;
	for (const FReaperCullRung& Each : Rungs)
	{
		ACataclysmEnemyCharacter* Spared = ReaperCullCreature(World, FVector(4 * M * Place++, 0, 0), Each.Rung);
		ACataclysmEnemyCharacter* Culled = ReaperCullCreature(World, FVector(4 * M * Place++, 0, 0), Each.Rung);
		if (!TestTrue(*FString::Printf(TEXT("set-up: two %s creatures"), Each.Said), Spared && Culled))
		{
			return false;
		}
		// LEFT JUST ABOVE THE NEXT FIGURE: lives. The Common one is the control.
		ReaperCullHold(Spared, ReaperCullShare(Spared, Each.SparedAt) + Taken * 1.5f);
		ReaperCullBlow(Scene, Spared, Swing);
		const bool bSpared = TestFalse(*FString::Printf(TEXT("%sthe %s creature left just above %.0f%% lives"),
													   bControlRead ? TEXT("") : TEXT("control: "), Each.Said,
													   Each.SparedAt),
									   ReaperCullIsGone(Spared));
		if (!bControlRead && !bSpared)
		{
			return false;
		}
		bControlRead = true;
		TestEqual(*FString::Printf(TEXT("the %s creature's cap is %.0f%%"), Each.Said, Each.Cap),
				  UCataclysmAbilitySystemComponent::CullCapPercentFor(Culled), Each.Cap, 0.001f);
		// LEFT JUST BELOW THE CAP: culled.
		ReaperCullHold(Culled, ReaperCullShare(Culled, Each.Cap) + Taken / 2.0f);
		ReaperCullBlow(Scene, Culled, Swing);
		TestTrue(*FString::Printf(TEXT("the %s creature left just below %.0f%% is culled"), Each.Said, Each.Cap),
				 ReaperCullIsGone(Culled));
	}

	// A BOSS AND A CATACLYSM BOSS, LEFT AT 5%: never.
	const TPair<const TCHAR*, int32> Bosses[] = {{TEXT("Boss"), 4}, {TEXT("Cataclysm Boss"), 5}};
	for (const TPair<const TCHAR*, int32>& Each : Bosses)
	{
		ACataclysmEnemyCharacter* Boss = ReaperCullCreature(World, FVector(4 * M * Place++, 0, 0), Each.Value);
		if (!TestNotNull(*FString::Printf(TEXT("set-up: a %s"), Each.Key), Boss))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("the %s's cap is nought"), Each.Key),
				  UCataclysmAbilitySystemComponent::CullCapPercentFor(Boss), 0.0f, 0.001f);
		ReaperCullHold(Boss, ReaperCullShare(Boss, 5.0f) + Taken / 2.0f);
		ReaperCullBlow(Scene, Boss, Swing);
		TestFalse(*FString::Printf(TEXT("the %s left below 5%% lives"), Each.Key), ReaperCullIsGone(Boss));
		TestTrue(*FString::Printf(TEXT("and the blow reached the %s's health"), Each.Key),
				 ReaperCullHealthOf(Boss) < ReaperCullShare(Boss, 5.0f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperTenKillCreditTest,
	"Cataclysm.SetBonus.Reaper10.ACullsDeathIsTheWearersKillAndADamageOverTimeTickNeverCulls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The cull's death is the wearer's kill, as an execute's is (C1, C5), and a tick never culls (C2).
 *
 * THE CONTROL IS READ FIRST: before any blow nobody has died, and the player has no kill. With two stacks, 10%: a
 * creature the blow leaves below 10% dies once; its death notice names the player as the killer and the striking skill
 * as the killing skill, with no overkill; the player's `kill` row acts once; the player state counts one kill; one blow
 * is announced, not two. Then a creature held at 5%, below the share, is burnt by the player for two seconds: it loses
 * health to the ticks, stays below 10%, and lives.
 *
 * STANDING: the player at the origin; two creatures at 4 m and 8 m along +X; the far creature 30 m along +Y.
 */
bool FCataclysmReaperTenKillCreditTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	FReaperCullScene Scene;
	if (!TestNotNull(TEXT("set-up: the announcements"), Events)
		|| !BuildReaperCullScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Culled = ReaperCullCreature(World, FVector(4 * M, 0, 0));
	ACataclysmEnemyCharacter* Burnt = ReaperCullCreature(World, FVector(8 * M, 0, 0));
	ACataclysmEnemyCharacter* Far = ReaperCullCreature(World, FVector(0, 30 * M, 0));
	if (!TestTrue(TEXT("set-up: three creatures"), Culled && Burnt && Far))
	{
		return false;
	}
	const FName Kills(TEXT("Test:kills"));
	WearReaperCull(Scene, /*bTenPiece=*/true, {}, {ReaperCullCountOn(TEXT("kill"), Kills)});

	TArray<FReaperCullDeathHeard> Heard;
	const FDelegateHandle Listening = Events->OnDeath.AddLambda([&Heard](const FCataclysmDeathNotice& Notice)
	{
		FReaperCullDeathHeard One;
		One.Victim = Notice.Victim;
		One.Killer = Notice.Killer;
		One.Skill = Notice.KillingSkillName;
		One.Overkill = Notice.Overkill;
		Heard.Add(One);
	});
	ON_SCOPE_EXIT { Events->OnDeath.Remove(Listening); };

	UCataclysmStrikeSkill* Swing = ReaperCullSkill(Scene.Player, TEXT("Test Swing"), TEXT("Type.Strike"));
	GiveReaperCullStacks(Scene, 2);
	const float Taken = ReaperCullTaken(Scene, Far, Swing);
	const float Share = ReaperCullShare(Culled, 10.0f);
	const int32 KillsBefore = Scene.State->GetRunKills();
	if (!TestEqual(TEXT("control: nobody has died"), static_cast<int32>(Events->DeathsSent()), 0)
		|| !TestEqual(TEXT("control: and the kill row has not acted"), Scene.System->OwnStacksHeld(Kills), 0)
		|| !TestTrue(*FString::Printf(TEXT("set-up: the blow takes something, under half the share: %.2f of %.2f"),
									  Taken, Share),
					 Taken > 1.0f && Taken < Share / 2.0f))
	{
		return false;
	}

	ReaperCullHold(Culled, Share + Taken / 2.0f);
	const uint32 BlowsBefore = Events->HitsSent();
	ReaperCullBlow(Scene, Culled, Swing);
	TestTrue(TEXT("a creature the blow left below 10% is culled"), ReaperCullIsGone(Culled));
	TestEqual(TEXT("one death was announced"), static_cast<int32>(Events->DeathsSent()), 1);
	TestEqual(TEXT("the player's kill row acted once"), Scene.System->OwnStacksHeld(Kills), 1);
	TestEqual(TEXT("the player state counts one kill more"), Scene.State->GetRunKills() - KillsBefore, 1);
	TestEqual(TEXT("one blow was announced, not two: the cull is not a hit"),
			  static_cast<int32>(Events->HitsSent() - BlowsBefore), 1);
	if (TestEqual(TEXT("one death notice was heard"), Heard.Num(), 1))
	{
		TestTrue(TEXT("it names the creature culled"), Heard[0].Victim == Culled);
		TestTrue(TEXT("it names the player as the killer"), Heard[0].Killer == Scene.Player);
		TestTrue(TEXT("it names the striking skill as the killing skill"), Heard[0].Skill == FName(TEXT("Test Swing")));
		TestEqual(TEXT("its overkill is nought"), Heard[0].Overkill, 0.0f, 0.0001f);
	}

	// A TICK NEVER CULLS. Held at 5%, below the share of 10%, and burnt for two seconds of the burn's four.
	const float Held = ReaperCullShare(Burnt, 5.0f);
	ReaperCullHold(Burnt, Held);
	if (!TestTrue(TEXT("set-up: the burn is laid"),
				  UCataclysmSkillEffects::ApplyDamageOverTime(Scene.Player, Burnt, /*DamagePerTick=*/5.0f,
															  /*DurationSeconds=*/4.0f,
															  UCataclysmSkillEffects::BurnTag(),
															  /*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	CataclysmTestWorld::RunClock(World, 2.0f);
	TestTrue(TEXT("the burn's ticks took health from the creature"), ReaperCullHealthOf(Burnt) < Held);
	TestFalse(TEXT("and below the share, burnt, it lives: a tick does not cull"), ReaperCullIsGone(Burnt));
	TestEqual(TEXT("still one death"), static_cast<int32>(Events->DeathsSent()), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperTenSpawnTest,
	"Cataclysm.SetBonus.Reaper10.AKillSpawnsOneFreeMinionOfTheFirstSummonSkillWhereTheEnemyDiedAndNoneAtTheCapOrWithoutOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * C5-C7. A player holding Summon Imp, granted first, and Summon Mote, granted second.
 *
 * THE CONTROL IS READ FIRST: before any kill the player commands no minion. A kill by the blow itself spawns one imp
 * where the creature died, of Summon Imp and not of Summon Mote, and the player's mana is as it was. A cull's kill
 * spawns a second imp (C5: the cull's own kills included). With the imps at Summon Imp's cap of 3, a kill spawns none,
 * and none of the three is gone.
 *
 * "FIRST" IS THE GRANT ORDER: a second player granted Summon Mote first and Summon Imp second spawns a mote. And a third
 * player, wearing the same rows and holding no summon skill, spawns nothing at its kill.
 *
 * STANDING: the players at the origin, 20 m along +Y and 40 m along +Y; each one's creatures 4 m apart along +X from
 * it, from 4 m; the far creature 30 m along -Y.
 */
bool FCataclysmReaperTenSpawnTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	FReaperCullScene Scene;
	FReaperCullScene MoteFirst;
	FReaperCullScene NoSummon;
	if (!BuildReaperCullScene(*this, World, FVector::ZeroVector, Scene)
		|| !BuildReaperCullScene(*this, World, FVector(0, 20 * M, 0), MoteFirst)
		|| !BuildReaperCullScene(*this, World, FVector(0, 40 * M, 0), NoSummon))
	{
		return false;
	}
	UCataclysmSummonSkill* Imps = GrantReaperCullSummon(Scene, TEXT("Imp"), ECataclysmAbilitySlot::Special);
	UCataclysmSummonSkill* Motes = GrantReaperCullSummon(Scene, TEXT("Mote"), ECataclysmAbilitySlot::Support);
	UCataclysmSummonSkill* FirstMotes = GrantReaperCullSummon(MoteFirst, TEXT("Mote"), ECataclysmAbilitySlot::Special);
	UCataclysmSummonSkill* SecondImps = GrantReaperCullSummon(MoteFirst, TEXT("Imp"), ECataclysmAbilitySlot::Support);
	ACataclysmEnemyCharacter* Far = ReaperCullCreature(World, FVector(0, -30 * M, 0));
	if (!TestTrue(TEXT("set-up: the four summon skills"), Imps && Motes && FirstMotes && SecondImps)
		|| !TestNotNull(TEXT("set-up: the far creature"), Far))
	{
		return false;
	}
	UCataclysmStrikeSkill* Swing = ReaperCullSkill(Scene.Player, TEXT("Test Swing"), TEXT("Type.Strike"));
	const float Taken = ReaperCullTaken(Scene, Far, Swing);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the blow takes something, under 5%% of a maximum: %.2f"), Taken),
				  Taken > 1.0f && Taken < ReaperCullHealth * 0.05f))
	{
		return false;
	}

	if (!TestEqual(TEXT("control: before any kill the player commands no minion"),
				   ReaperCullMinionsOf(Scene.Player).Num(), 0))
	{
		return false;
	}

	// A KILL BY THE BLOW ITSELF: one imp, where the creature died, free.
	ACataclysmEnemyCharacter* Frail = ReaperCullCreature(World, FVector(4 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a creature the blow kills"), Frail))
	{
		return false;
	}
	const FVector Died = Frail->GetActorLocation();
	const float ManaBefore = Scene.System->GetNumericAttribute(Vital::GetManaAttribute());
	ReaperCullHold(Frail, Taken / 2.0f);
	ReaperCullBlow(Scene, Frail, Swing);
	TestTrue(TEXT("set-up: the blow killed it"), ReaperCullIsGone(Frail));
	const TArray<ACataclysmMinion*> AfterOne = ReaperCullMinionsOf(Scene.Player);
	if (TestEqual(TEXT("the kill spawned one minion"), AfterOne.Num(), 1))
	{
		TestEqual(TEXT("it stands where the creature died"),
				  static_cast<float>(FVector::Dist(AfterOne[0]->GetActorLocation(), Died)), 0.0f, 1.0f);
		TestTrue(TEXT("it is an imp of Summon Imp, the first summon skill granted"),
				 AfterOne[0]->TypeName == TEXT("Imp") && AfterOne[0]->SummonedBy.Get() == Imps);
	}
	TestEqual(TEXT("Summon Mote, granted second, holds none"), Motes->LivingMinionCount(), 0);
	TestEqual(TEXT("and it was free: the player's mana is as it was"),
			  Scene.System->GetNumericAttribute(Vital::GetManaAttribute()), ManaBefore, 0.001f);

	// A CULL'S KILL SPAWNS ONE TOO.
	GiveReaperCullStacks(Scene, 2);
	const float StackedTaken = ReaperCullTaken(Scene, Far, Swing);
	ACataclysmEnemyCharacter* Culled = ReaperCullCreature(World, FVector(8 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a creature to cull"), Culled))
	{
		return false;
	}
	ReaperCullHold(Culled, ReaperCullShare(Culled, 10.0f) + StackedTaken / 2.0f);
	ReaperCullBlow(Scene, Culled, Swing);
	TestTrue(TEXT("set-up: the creature was culled"), ReaperCullIsGone(Culled));
	TestEqual(TEXT("the cull's kill spawned a second imp"), Imps->LivingMinionCount(), 2);

	// AT THE CAP: none spawned, none gone.
	const int32 Cap = UCataclysmSummonSkill::CapFor(Scene.Player, Imps->Params, Imps->SkillTags);
	while (Cap > 0 && Imps->LivingMinionCount() < Cap)
	{
		if (!Imps->SummonReplacementAt(Scene.Player->GetActorLocation() + FVector(-3 * M * Imps->LivingMinionCount(),
																					 3 * M, 0)))
		{
			break;
		}
	}
	const TArray<ACataclysmMinion*> AtTheCap = ReaperCullMinionsOf(Scene.Player);
	if (!TestEqual(TEXT("set-up: Summon Imp is at its cap of 3"), Imps->LivingMinionCount(), 3)
		|| !TestEqual(TEXT("set-up: Summon Imp's cap is 3"), Cap, 3))
	{
		return false;
	}
	ACataclysmEnemyCharacter* AtCapKill = ReaperCullCreature(World, FVector(12 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a creature to kill at the cap"), AtCapKill))
	{
		return false;
	}
	ReaperCullHold(AtCapKill, StackedTaken / 2.0f);
	ReaperCullBlow(Scene, AtCapKill, Swing);
	TestTrue(TEXT("set-up: the blow killed it"), ReaperCullIsGone(AtCapKill));
	TestEqual(TEXT("at the cap a kill spawns none"), ReaperCullMinionsOf(Scene.Player).Num(), AtTheCap.Num());
	bool bNoneGone = true;
	for (const ACataclysmMinion* Each : AtTheCap)
	{
		bNoneGone = bNoneGone && IsValid(Each) && !UCataclysmSkillEffects::IsDead(Each);
	}
	TestTrue(TEXT("and none of the three is evicted"), bNoneGone);

	// "FIRST" IS THE GRANT ORDER.
	UCataclysmStrikeSkill* MoteSwing = ReaperCullSkill(MoteFirst.Player, TEXT("Test Swing"), TEXT("Type.Strike"));
	ACataclysmEnemyCharacter* MoteKill = ReaperCullCreature(World, FVector(4 * M, 20 * M, 0));
	ACataclysmEnemyCharacter* NoneKill = ReaperCullCreature(World, FVector(4 * M, 40 * M, 0));
	UCataclysmStrikeSkill* NoneSwing = ReaperCullSkill(NoSummon.Player, TEXT("Test Swing"), TEXT("Type.Strike"));
	if (!TestTrue(TEXT("set-up: the other players' creatures"), MoteKill && NoneKill))
	{
		return false;
	}
	ReaperCullHold(MoteKill, Taken / 2.0f);
	ReaperCullBlow(MoteFirst, MoteKill, MoteSwing);
	const TArray<ACataclysmMinion*> Moted = ReaperCullMinionsOf(MoteFirst.Player);
	TestTrue(TEXT("a player granted Summon Mote first spawns one mote of it"),
			 Moted.Num() == 1 && Moted[0]->TypeName == TEXT("Mote") && Moted[0]->SummonedBy.Get() == FirstMotes);

	// NO SUMMON SKILL: nothing.
	ReaperCullHold(NoneKill, Taken / 2.0f);
	ReaperCullBlow(NoSummon, NoneKill, NoneSwing);
	TestTrue(TEXT("set-up: the third player's blow killed its creature"), ReaperCullIsGone(NoneKill));
	TestEqual(TEXT("a player holding no summon skill spawns nothing at its kill"),
			  ReaperCullMinionsOf(NoSummon.Player).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperTenOrdinaryMinionTest,
	"Cataclysm.SetBonus.Reaper10.ASpawnedMinionCountsInsideItsSkillsCapAndLeavesASoulWhenItDies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * C7, and the six-piece's rule. A kill spawns an imp of Summon Imp: it is in the skill's own list, counted among the
 * minions sharing its cap, and its summoner is the player. THE CONTROL IS READ FIRST: before it dies no Soul lies
 * anywhere. Killed, it leaves one Soul, the player's, where it died.
 *
 * STANDING: the player at the origin; the creature 4 m along +X, so the imp stands there; the far creature 30 m along +Y.
 */
bool FCataclysmReaperTenOrdinaryMinionTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	FReaperCullScene Scene;
	if (!BuildReaperCullScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	UCataclysmSummonSkill* Imps = GrantReaperCullSummon(Scene, TEXT("Imp"), ECataclysmAbilitySlot::Special);
	ACataclysmEnemyCharacter* Frail = ReaperCullCreature(World, FVector(4 * M, 0, 0));
	ACataclysmEnemyCharacter* Far = ReaperCullCreature(World, FVector(0, 30 * M, 0));
	if (!TestNotNull(TEXT("set-up: Summon Imp"), Imps) || !TestTrue(TEXT("set-up: two creatures"), Frail && Far))
	{
		return false;
	}
	UCataclysmStrikeSkill* Swing = ReaperCullSkill(Scene.Player, TEXT("Test Swing"), TEXT("Type.Strike"));
	const float Taken = ReaperCullTaken(Scene, Far, Swing);
	ReaperCullHold(Frail, Taken / 2.0f);
	ReaperCullBlow(Scene, Frail, Swing);
	const TArray<ACataclysmMinion*> Spawned = ReaperCullMinionsOf(Scene.Player);
	if (!TestTrue(TEXT("set-up: the blow killed the creature"), ReaperCullIsGone(Frail))
		|| !TestEqual(TEXT("set-up: the kill spawned one minion"), Spawned.Num(), 1))
	{
		return false;
	}
	ACataclysmMinion* Imp = Spawned[0];
	TestTrue(TEXT("it is in Summon Imp's own list"),
			 Imps->Minions.ContainsByPredicate([Imp](const TObjectPtr<ACataclysmMinion>& Each) { return Each.Get() == Imp; }));
	TestTrue(TEXT("it is counted among the minions sharing Summon Imp's cap"),
			 Imps->MinionsSharingTheCap().Contains(Imp));
	TestTrue(TEXT("its summoner is the player"), Imp->Summoner.Get() == Scene.Player);
	TestFalse(TEXT("it holds a place under the summon caps"), Imp->bOutsideSummonCaps);

	int32 SoulsBefore = 0;
	for (TActorIterator<ACataclysmReaperSoul> It(World); It; ++It)
	{
		SoulsBefore += IsValid(*It) ? 1 : 0;
	}
	if (!TestEqual(TEXT("control: before it dies no Soul lies anywhere"), SoulsBefore, 0))
	{
		return false;
	}
	const FVector Where = Imp->GetActorLocation();
	ReaperCullHold(Imp, 0.0f);
	int32 Souls = 0;
	for (TActorIterator<ACataclysmReaperSoul> It(World); It; ++It)
	{
		const ACataclysmReaperSoul* Soul = *It;
		Souls += IsValid(Soul) && Soul->LeftByCommander.Get() == Scene.Player
			&& FVector::Dist2D(Soul->GetActorLocation(), Where) <= 1.0f ? 1 : 0;
	}
	TestEqual(TEXT("killed, it leaves one Soul, the player's, where it died"), Souls, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperTenWhoseHitsTest,
	"Cataclysm.SetBonus.Reaper10.TheWearersOwnHitsCullAndAMinionsOwnBlowDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * C2. Two stacks, 10%, and Conduit (`minion_hits_count_as_yours`), so a minion's blow is the player's and raises its
 * `hit_dealt`. Every creature is held so that the blow leaves it below 10%.
 *
 * THE CONTROL IS READ FIRST: a skill's blow culls. Then the basic attack's blow (a skill in the basic attack's slot), the
 * blow of a cast a skill gadget made (the use's `FreeRepeatCastBy` and the delivery's `CastBy` the gadget) and the blow
 * of a cast a Tyrant's Minion made (the same, the Tyrant's Minion) each cull. AN IMP'S OWN BLOW raises the player's
 * `hit_dealt` once, which is the control for what follows, and leaves its creature below 10% alive.
 *
 * WHAT THESE BLOWS ARE: each is `ApplyHit` or `ApplyDirectDamage` with the delivery the kind of blow carries, not the
 * cast itself. A gadget's and a Tyrant's Minion's cast are granted to the player and strike as the player's skill
 * (`ACataclysmMinion::StartPlayersSkillFromHere`), which is the delivery given here.
 *
 * STANDING: the player at the origin; five creatures 4 m apart along +X from 4 m; the gadget, the Tyrant's Minion and the
 * imp 3 m along -Y and 3 m apart; the far creature 30 m along +Y.
 */
bool FCataclysmReaperTenWhoseHitsTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	FReaperCullScene Scene;
	if (!BuildReaperCullScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	const FName Hits(TEXT("Test:hits"));
	WearReaperCull(Scene, /*bTenPiece=*/true,
				   {{FName(TEXT("minion_hits_count_as_yours")), 1.0f},
					{FName(ACataclysmMinion::TyrantsMinionSecondsStat), 60.0f}},
				   {ReaperCullCountOn(TEXT("hit_dealt"), Hits)});
	ACataclysmMinion* Gadget = ACataclysmMinion::Spawn(Scene.Player, FVector(0, -3 * M, 0), 60.0f, false,
													   TEXT("SkillGadget"));
	ACataclysmMinion* Tyrants = ACataclysmMinion::SummonTyrantsMinion(Scene.Player, /*bLostWasTyrantsMinion=*/false,
																	   FVector(3 * M, -3 * M, 0));
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Scene.Player, FVector(-3 * M, -3 * M, 0), 60.0f, false, TEXT("Imp"));
	ACataclysmEnemyCharacter* Far = ReaperCullCreature(World, FVector(0, 30 * M, 0));
	TArray<ACataclysmEnemyCharacter*> Creatures;
	for (int32 Place = 1; Place <= 5; ++Place)
	{
		Creatures.Add(ReaperCullCreature(World, FVector(4 * M * Place, 0, 0)));
	}
	if (!TestTrue(TEXT("set-up: a skill gadget, a Tyrant's Minion and an imp of the player's"), Gadget && Tyrants && Imp)
		|| !TestTrue(TEXT("set-up: six creatures"), Far && !Creatures.Contains(nullptr)))
	{
		return false;
	}

	UCataclysmStrikeSkill* Swing = ReaperCullSkill(Scene.Player, TEXT("Test Swing"), TEXT("Type.Strike"));
	UCataclysmStrikeSkill* Basic = ReaperCullSkill(Scene.Player, TEXT("Test Basic"), TEXT("Type.Strike"),
												   ECataclysmAbilitySlot::BasicAttack);
	UCataclysmStrikeSkill* GadgetCast = ReaperCullSkill(Scene.Player, TEXT("Test Gadget Cast"), TEXT("Type.Strike"));
	GadgetCast->FreeRepeatCastBy = Gadget;
	UCataclysmStrikeSkill* TyrantsCast = ReaperCullSkill(Scene.Player, TEXT("Test Tyrant Cast"), TEXT("Type.Strike"));
	TyrantsCast->FreeRepeatCastBy = Tyrants;

	GiveReaperCullStacks(Scene, 2);
	const float Taken = ReaperCullTaken(Scene, Far, Swing);
	const float Share = ReaperCullShare(Creatures[0], 10.0f);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the blow takes something, under half the share: %.2f of %.2f"),
								   Taken, Share),
				  Taken > 1.0f && Taken < Share / 2.0f))
	{
		return false;
	}

	// THE CONTROL: a skill's blow.
	ReaperCullHold(Creatures[0], Share + Taken / 2.0f);
	ReaperCullBlow(Scene, Creatures[0], Swing);
	if (!TestTrue(TEXT("control: a skill's blow leaving a creature below 10% culls it"), ReaperCullIsGone(Creatures[0])))
	{
		return false;
	}

	ReaperCullHold(Creatures[1], Share + Taken / 2.0f);
	ReaperCullBlow(Scene, Creatures[1], Basic);
	TestTrue(TEXT("the basic attack's blow culls"), ReaperCullIsGone(Creatures[1]));

	ReaperCullHold(Creatures[2], Share + Taken / 2.0f);
	ReaperCullBlow(Scene, Creatures[2], GadgetCast, Gadget);
	TestTrue(TEXT("the blow of a skill gadget's cast culls"), ReaperCullIsGone(Creatures[2]));

	ReaperCullHold(Creatures[3], Share + Taken / 2.0f);
	ReaperCullBlow(Scene, Creatures[3], TyrantsCast, Tyrants);
	TestTrue(TEXT("the blow of a Tyrant's Minion's cast culls"), ReaperCullIsGone(Creatures[3]));

	// AN IMP'S OWN BLOW, MEASURED ON THE FAR CREATURE FIRST.
	const FCataclysmHitDelivery ImpBlow = ACataclysmMinion::OwnBlowDelivery(Imp);
	ReaperCullHold(Far, ReaperCullHealth);
	UCataclysmSkillEffects::ApplyDirectDamage(Imp, Far, /*Damage=*/100.0f, ImpBlow);
	const float ImpTaken = ReaperCullHealth - ReaperCullHealthOf(Far);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the imp's blow takes something, under half the share: %.2f"), ImpTaken),
				  ImpTaken > 1.0f && ImpTaken < Share / 2.0f))
	{
		return false;
	}
	ReaperCullHold(Creatures[4], Share + ImpTaken / 2.0f);
	const int32 HitsBefore = Scene.System->OwnStacksHeld(Hits);
	UCataclysmSkillEffects::ApplyDirectDamage(Imp, Creatures[4], /*Damage=*/100.0f, ImpBlow);
	TestEqual(TEXT("control: with Conduit the imp's blow raised the player's hit_dealt once"),
			  Scene.System->OwnStacksHeld(Hits) - HitsBefore, 1);
	TestFalse(TEXT("an imp's own blow leaving a creature below 10% does not cull it"), ReaperCullIsGone(Creatures[4]));
	TestTrue(TEXT("and it was left below 10%"), ReaperCullHealthOf(Creatures[4]) < Share);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperTenNoRarityTest,
	"Cataclysm.SetBonus.Reaper10.ACreatureWithNoRarityIsCulledAsCommon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * C3. At ten stacks. A minion a creature summoned is of the monsters' side and is no enemy character, so it has no
 * rarity. THE CONTROL IS READ FIRST: an Elite creature the blow leaves at 40% lives. The minion's cap is 50%, and the
 * blow leaving it at 40% culls it.
 *
 * STANDING: the player at the origin; the Elite 4 m along +X; the summoning creature 8 m along +X and its imp 4 m beyond
 * it; the far creature 30 m along +Y.
 */
bool FCataclysmReaperTenNoRarityTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperTenTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	FReaperCullScene Scene;
	if (!BuildReaperCullScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Elite = ReaperCullCreature(World, FVector(4 * M, 0, 0), /*Rung=*/1);
	ACataclysmEnemyCharacter* Summoner = ReaperCullCreature(World, FVector(8 * M, 0, 0));
	ACataclysmEnemyCharacter* Far = ReaperCullCreature(World, FVector(0, 30 * M, 0));
	ACataclysmMinion* Hostile = Summoner
		? ACataclysmMinion::Spawn(Summoner, FVector(12 * M, 0, 0), 60.0f, false, TEXT("Imp"))
		: nullptr;
	if (!TestTrue(TEXT("set-up: three creatures"), Elite && Summoner && Far)
		|| !TestNotNull(TEXT("set-up: the creature's imp"), Hostile)
		|| !TestTrue(TEXT("set-up: the imp is of the monsters' side"),
					 UCataclysmTeams::TeamOf(Hostile) == UCataclysmTeams::IdFor(ECataclysmTeam::Monsters)))
	{
		return false;
	}
	if (UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Hostile))
	{
		Its->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), ReaperCullHealth);
		Its->SetNumericAttributeBase(Vital::GetHealthAttribute(), ReaperCullHealth);
	}
	UCataclysmStrikeSkill* Swing = ReaperCullSkill(Scene.Player, TEXT("Test Swing"), TEXT("Type.Strike"));
	GiveReaperCullStacks(Scene, 10);
	const float Taken = ReaperCullTaken(Scene, Far, Swing);
	if (!TestEqual(TEXT("set-up: the imp's maximum health is 10,000"), ReaperCullMaximumOf(Hostile), ReaperCullHealth,
				   0.01f)
		|| !TestTrue(*FString::Printf(TEXT("set-up: the blow takes something, under 5%% of a maximum: %.2f"), Taken),
					 Taken > 1.0f && Taken < ReaperCullHealth * 0.05f))
	{
		return false;
	}

	ReaperCullHold(Elite, ReaperCullShare(Elite, 40.0f) + Taken);
	ReaperCullBlow(Scene, Elite, Swing);
	if (!TestFalse(TEXT("control: an Elite creature left at 40% lives"), ReaperCullIsGone(Elite)))
	{
		return false;
	}
	TestEqual(TEXT("a creature with no rarity has the Common cap of 50%"),
			  UCataclysmAbilitySystemComponent::CullCapPercentFor(Hostile), 50.0f, 0.001f);
	ReaperCullHold(Hostile, ReaperCullShare(Hostile, 40.0f) + Taken);
	ReaperCullBlow(Scene, Hostile, Swing);
	TestTrue(TEXT("and the blow leaving it at 40% culls it"), ReaperCullIsGone(Hostile));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmReaperTenShippedExecuteTest,
	"Cataclysm.SetBonus.Reaper10.TheOneShippedExecuteRowStillExecutesAsItDidBesideTheCull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * C1: every existing `execute_target` row behaves as before. The shipped table is read from
 * `game/Data/EnchantmentEffects.csv` and holds ONE row carrying the action (counted 2026-10-19, pinned here); it is
 * passed through the loader on its own enchantment, at the item's default roll of 1, so its chance is 20.
 *
 * WORN ALONE, AS BEFORE: at the roll pinned 0, a Strike blow leaving a creature below 15% executes it (the control);
 * at the roll pinned 99 one left below 15% lives; a Spell blow at roll 0 executes nobody.
 *
 * THEN BESIDE THE TEN-PIECE AT TWO STACKS, 10%: at roll 99 a creature left at 12% lives (the cull is 10%) and one left
 * at 8% is culled; at roll 0 a creature left at 12% is executed, so the execute acts by its own share beside the cull.
 *
 * STANDING: the player at the origin; six creatures 4 m apart along +X from 4 m; the far creature 30 m along +Y.
 */
bool FCataclysmReaperTenShippedExecuteTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusReaperTenTest;
	FReaperCullExecuteRoll Roll(TEXT("0"));
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World)
		|| !TestNotNull(TEXT("set-up: the execute roll's console variable"), Roll.Variable))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	// THE SHIPPED ROWS THAT CARRY THE ACTION, COUNTED.
	UDataTable* Shipped = LoadReaperCullData<FCataclysmEnchantmentEffectRow>(TEXT("EnchantmentEffects.csv"));
	if (!TestNotNull(TEXT("set-up: the shipped effect table"), Shipped))
	{
		return false;
	}
	int32 Carrying = 0;
	bool bOnTheShippedEnchantment = true;
	Shipped->ForeachRow<FCataclysmEnchantmentEffectRow>(TEXT("Reaper10"),
		[&Carrying, &bOnTheShippedEnchantment](const FName&, const FCataclysmEnchantmentEffectRow& Row)
		{
			if (Row.Action.Equals(UCataclysmAbilitySystemComponent::ExecuteTargetAction, ESearchCase::IgnoreCase))
			{
				++Carrying;
				bOnTheShippedEnchantment = bOnTheShippedEnchantment && Row.Enchantment == ShippedExecute;
			}
		});
	TestEqual(TEXT("the shipped table holds one execute_target row"), Carrying, 1);
	TestTrue(TEXT("and it is the Strike skills' execute"), bOnTheShippedEnchantment);

	FReaperCullScene Scene;
	FReaperCullMade Execute;
	if (!BuildReaperCullScene(*this, World, FVector::ZeroVector, Scene)
		|| !AccumulateReaperCullItem(*this, Shipped, ShippedExecute, Execute))
	{
		return false;
	}
	TArray<FCataclysmPoolAction> Executes;
	for (const FCataclysmPoolAction& Action : Execute.Actions)
	{
		if (Action.bExecuteTarget)
		{
			Executes.Add(Action);
		}
	}
	if (!TestEqual(TEXT("set-up: the loader makes one execute action of it"), Executes.Num(), 1)
		|| !TestEqual(TEXT("set-up: its share is 15"), Executes[0].ConditionValue, 15.0f, 0.001f)
		|| !TestEqual(TEXT("set-up: its chance is 20, the item's roll of 1"), Executes[0].Percent, 20.0f, 0.001f))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Far = ReaperCullCreature(World, FVector(0, 30 * M, 0));
	TArray<ACataclysmEnemyCharacter*> Creatures;
	for (int32 Place = 1; Place <= 6; ++Place)
	{
		Creatures.Add(ReaperCullCreature(World, FVector(4 * M * Place, 0, 0)));
	}
	if (!TestTrue(TEXT("set-up: seven creatures"), Far && !Creatures.Contains(nullptr)))
	{
		return false;
	}
	UCataclysmStrikeSkill* Swing = ReaperCullSkill(Scene.Player, TEXT("Test Swing"), TEXT("Type.Strike"));
	UCataclysmStrikeSkill* Spell = ReaperCullSkill(Scene.Player, TEXT("Test Spell"), TEXT("Type.Spell"));

	// WORN ALONE: the execute and no ten-piece, so no cull.
	WearReaperCull(Scene, /*bTenPiece=*/false, {}, Executes);
	const float Taken = ReaperCullTaken(Scene, Far, Swing);
	const float SpellTaken = ReaperCullTaken(Scene, Far, Spell);
	const float Share = ReaperCullShare(Creatures[0], 15.0f);
	if (!TestTrue(*FString::Printf(TEXT("set-up: each blow takes something, under a quarter of the share: %.2f, %.2f"),
								   Taken, SpellTaken),
				  FMath::Min(Taken, SpellTaken) > 1.0f && FMath::Max(Taken, SpellTaken) < Share / 4.0f))
	{
		return false;
	}
	ReaperCullHold(Creatures[0], Share + Taken / 2.0f);
	ReaperCullBlow(Scene, Creatures[0], Swing);
	if (!TestTrue(TEXT("control: worn alone, roll 0, a Strike blow leaving a creature below 15% executes it"),
				  ReaperCullIsGone(Creatures[0])))
	{
		return false;
	}
	Roll.Set(TEXT("99"));
	ReaperCullHold(Creatures[1], Share + Taken / 2.0f);
	ReaperCullBlow(Scene, Creatures[1], Swing);
	TestFalse(TEXT("worn alone, roll 99: the creature left below 15% lives"), ReaperCullIsGone(Creatures[1]));
	Roll.Set(TEXT("0"));
	ReaperCullHold(Creatures[2], Share + SpellTaken / 2.0f);
	ReaperCullBlow(Scene, Creatures[2], Spell);
	TestFalse(TEXT("worn alone, roll 0: a Spell blow leaving a creature below 15% executes nobody"),
			  ReaperCullIsGone(Creatures[2]));

	// BESIDE THE TEN-PIECE, AT TWO STACKS: 10%.
	WearReaperCull(Scene, /*bTenPiece=*/true, {}, Executes);
	GiveReaperCullStacks(Scene, 2);
	const float StackedTaken = ReaperCullTaken(Scene, Far, Swing);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the blow takes something, under a fiftieth of a maximum: %.2f"),
								   StackedTaken),
				  StackedTaken > 1.0f && StackedTaken < ReaperCullHealth / 50.0f))
	{
		return false;
	}
	Roll.Set(TEXT("99"));
	ReaperCullHold(Creatures[3], ReaperCullShare(Creatures[3], 12.0f) + StackedTaken);
	ReaperCullBlow(Scene, Creatures[3], Swing);
	TestFalse(TEXT("beside the cull, roll 99: a creature left at 12% lives"), ReaperCullIsGone(Creatures[3]));
	ReaperCullHold(Creatures[4], ReaperCullShare(Creatures[4], 8.0f) + StackedTaken);
	ReaperCullBlow(Scene, Creatures[4], Swing);
	TestTrue(TEXT("beside the cull, roll 99: a creature left at 8% is culled"), ReaperCullIsGone(Creatures[4]));
	Roll.Set(TEXT("0"));
	ReaperCullHold(Creatures[5], ReaperCullShare(Creatures[5], 12.0f) + StackedTaken);
	ReaperCullBlow(Scene, Creatures[5], Swing);
	TestTrue(TEXT("beside the cull, roll 0: a creature left at 12% is executed"), ReaperCullIsGone(Creatures[5]));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
