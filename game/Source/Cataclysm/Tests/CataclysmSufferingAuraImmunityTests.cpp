// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/SphereComponent.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A row can make its wearer immune to the mana drain of the dungeon rule Suffering Aura. Ruled 2026-10-09 under
 * the owner's delegation, for the first half of "Mana Weaver (6-Piece Bonus): You gain an aura that makes you and
 * nearby allies immune to all mana-draining effects".
 *
 * WHAT IS BUILT. One flag stat, `suffering_aura_mana_drain_immunity`, and one condition in
 * `ACataclysmDungeonGameMode::StepSufferingAura`: the rule's mana loss is not taken while the player's flag is
 * above nought. The rule's health loss is not asked and runs as before. No row is authored, so every flag here
 * is written by hand as the `flat` line of 1 a row would be.
 *
 * WHY THIS FILE AND NOT `CataclysmDungeonModifierEffectsTests.cpp`. That file is the dungeon session's. These
 * tests step the rule the way its tests there do, through what is public on the game mode: `DungeonModifiers`,
 * `FloorNumber`, `BuildFloor()`, `FloorBrief` and `Tick()`. `StepSufferingAura` itself is private and is not
 * called from here. The possessed player below is written again here from the same engine calls, because the
 * one in that file is in that file's own namespace.
 *
 * THE FLAG IS WRITTEN AFTER THE FLOOR IS BUILT, AND IT REPLACES THE WHOLE STAT LINE. `BuildFloor` refreshes the
 * player's stats, and a refresh writes the stat line wholesale, so a flag written before it would be gone.
 * `SetStatInputs` also writes wholesale, so a player holding the hand-written flag holds no other recorded line.
 * The rule reads maximum health and maximum mana off the gameplay attributes, which a write of the stat line does
 * not move. After every step taken with the flag held, the test checks that the flag is still held, so a beat
 * that refreshed the player would be reported as a set-up failure and not as a result.
 *
 * ONE BEAT IS ONE CALL OF `Tick` WITH `SecondsBetweenWaveChecks`, with the world clock standing still, as in the
 * dungeon session's tests of this rule, so no regeneration runs between two readings.
 *
 * EVERY FIGURE IS A DIFFERENCE OF TWO READINGS. A possessed player has pools of its own; none is assumed.
 *
 * NOT COMPILED AND NOT RUN BY THE SESSION THAT WROTE IT.
 */
namespace CataclysmSufferingAuraImmunityTest
{
	using ImmunityVital = UCataclysmVitalAttributeSet;

	/** The row key of the rule, as the dungeon game mode reads it off a floor's brief. */
	FName TheRule()
	{
		return FName(UCataclysmDungeonModifierEffects::SufferingAuraKey);
	}

	/** What one beat of the rule takes from a pool of this maximum at this share a second. */
	float OneBeatOfTheRule(float Maximum, float PercentPerSecond)
	{
		return Maximum * PercentPerSecond / 100.0f * ACataclysmDungeonGameMode::SecondsBetweenWaveChecks;
	}

	/**
	 * Write this ability system's whole stat line: the flag at a flat 1 and nothing else, or nothing at all.
	 *
	 * WHOLESALE, because `SetStatInputs` is. The line a row would make: bucket flat, value 1, no tags, no
	 * condition, from an enchantment.
	 */
	void WriteTheFlag(UCataclysmAbilitySystemComponent* System, bool bHeld)
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
			Stats.Add(FName(UCataclysmSkillEffects::SufferingAuraManaDrainImmunityStat), Line);
		}
		System->SetStatInputs(MoveTemp(Stats));
	}

	/**
	 * A player the dungeon game mode's beat can find: a player state, a controller and a possessed player
	 * character, standing at the origin.
	 *
	 * `AController::Possess` AND NOT `APawn::PossessedBy`, for the reason the dungeon session's own possessed
	 * player gives: the beat reaches the player through `GetFirstPlayerController()->GetPawn()`, and only the
	 * controller's half tells the controller which pawn it has.
	 */
	struct FImmunityTestPlayer
	{
		explicit FImmunityTestPlayer(UWorld* World)
		{
			PlayerState = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<APlayerController>(APlayerController::StaticClass());
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!PlayerState || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(PlayerState);
			Controller->Possess(Character);
			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(Character->GetAbilitySystemComponent());
		}

		bool IsUsable() const { return Character && AbilitySystem; }

		float Health() const { return AbilitySystem->GetNumericAttribute(ImmunityVital::GetHealthAttribute()); }
		float Mana() const { return AbilitySystem->GetNumericAttribute(ImmunityVital::GetManaAttribute()); }
		float MaxHealth() const { return AbilitySystem->GetNumericAttribute(ImmunityVital::GetMaxHealthAttribute()); }
		float MaxMana() const { return AbilitySystem->GetNumericAttribute(ImmunityVital::GetMaxManaAttribute()); }

		/** Whether the rule's one question answers yes for this player right now. */
		bool HoldsTheFlag() const
		{
			return UCataclysmSkillEffects::ImmuneToSufferingAuraManaDrainByARow(Character);
		}

		ACataclysmPlayerState* PlayerState = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** What one beat took from a player's two pools. */
	struct FTakenByABeat
	{
		float Health = 0.0f;
		float Mana = 0.0f;
	};

	/** Step the game mode one beat and say what the player's health and mana lost. */
	FTakenByABeat TakeOneBeat(ACataclysmDungeonGameMode* Mode, const FImmunityTestPlayer& Player)
	{
		const float HealthBefore = Player.Health();
		const float ManaBefore = Player.Mana();
		Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);

		FTakenByABeat Taken;
		Taken.Health = HealthBefore - Player.Health();
		Taken.Mana = ManaBefore - Player.Mana();
		return Taken;
	}

	/**
	 * A floor at depth 1 carrying the rule and nothing else, and the checks every test here makes before it
	 * steps: the floor was built with the rule on its brief, the player holds no flag, and each pool holds more
	 * than `Beats` beats take, so no loss below is cut short by a pool running out.
	 */
	bool BuildTheRulesFloor(FAutomationTestBase& Test, ACataclysmDungeonGameMode* Mode,
							const FImmunityTestPlayer& Player, int32 Beats)
	{
		using Effects = UCataclysmDungeonModifierEffects;

		Mode->DungeonModifiers = {TheRule()};
		Mode->FloorNumber = 1;
		if (!Test.TestNotNull(TEXT("set-up: the floor was built"), Mode->BuildFloor())
			|| !Test.TestTrue(TEXT("set-up: the floor's brief carries Suffering Aura"),
							  Mode->FloorBrief.Modifiers.Contains(TheRule()))
			|| !Test.TestFalse(TEXT("set-up: a player with no row does not hold the flag"), Player.HoldsTheFlag()))
		{
			return false;
		}

		const float HealthABeat = OneBeatOfTheRule(Player.MaxHealth(), Effects::SufferingAuraHealthPercentPerSecond);
		const float ManaABeat = OneBeatOfTheRule(Player.MaxMana(), Effects::SufferingAuraManaPercentPerSecond);
		return Test.TestTrue(TEXT("set-up: a beat of the rule takes some health"), HealthABeat > 0.0f)
			&& Test.TestTrue(TEXT("set-up: a beat of the rule takes some mana"), ManaABeat > 0.0f)
			&& Test.TestTrue(FString::Printf(TEXT("set-up: health for every beat taken here: %.2f held, %.2f a beat"),
											 Player.Health(), HealthABeat),
							 Player.Health() > HealthABeat * Beats)
			&& Test.TestTrue(FString::Printf(TEXT("set-up: mana for every beat taken here: %.2f held, %.2f a beat"),
											 Player.Mana(), ManaABeat),
							 Player.Mana() > ManaABeat * Beats);
	}

	/**
	 * An actor with an ability system that can use a skill and pay for it: the four attribute sets a skill's
	 * cost and blow read, 1,000 mana and 10,000 health. The shape the stat exemption probes give their own.
	 */
	struct FImmunityTestCaster
	{
		FImmunityTestCaster(UWorld* World, const FVector& Where)
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

			// Raw pointers, not TObjectPtr: AddAttributeSetSubobject deduces its type from the argument.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist = NewObject<UCataclysmResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(ImmunityVital::GetMaxHealthAttribute(), 10000.0f);
			AbilitySystem->SetNumericAttributeBase(ImmunityVital::GetHealthAttribute(), 10000.0f);
			AbilitySystem->SetNumericAttributeBase(ImmunityVital::GetMaxManaAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(ImmunityVital::GetManaAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FImmunityTestCaster()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Mana() const { return AbilitySystem->GetNumericAttribute(ImmunityVital::GetManaAttribute()); }

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** What using one skill did to its user's mana. */
	struct FPaidForASkill
	{
		/** The skill was granted and its use was allowed. */
		bool bUsed = false;
		/** What the skill says this user pays, asked before the use. */
		float Cost = 0.0f;
		/** What the user's mana lost over the use. */
		float ManaLost = 0.0f;
	};

	/**
	 * Grant this caster a Heavy strike that states a cost of 40 mana, use it once, and say what was paid. The
	 * strike is a full circle of 3 m with nobody in it; it is used for its cost and strikes nobody.
	 */
	FPaidForASkill UseAStrikeCostingForty(FImmunityTestCaster& Who)
	{
		FPaidForASkill Paid;

		const FGameplayAbilitySpecHandle Handle = Who.AbilitySystem->GiveAbilityInSlot(
			UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Heavy, /*Level=*/1, Who.Actor);
		FGameplayAbilitySpec* Spec = Who.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UCataclysmStrikeSkill* Skill = Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			return Paid;
		}
		Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=3; Angle=360"));
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Melee"));
		Skill->ManaCostOverride = 40.0f;

		Paid.Cost = Skill->ManaCostFor(Who.AbilitySystem);
		const float ManaBefore = Who.Mana();
		Paid.bUsed = Who.AbilitySystem->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false);
		Paid.ManaLost = ManaBefore - Who.Mana();
		return Paid;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSufferingAuraImmunityKeepsManaTest,
	"Cataclysm.SufferingAuraImmunity.AHolderOfTheFlagKeepsItsManaAndStillLosesTheRulesShareOfHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE CONTROL IS READ FIRST: with no flag, one beat of the rule takes health and mana. Then the flag is written
 * and the next beat takes the same health and no mana.
 *
 * STANDING: the player at the origin, on the floor `BuildFloor` makes for depth 1.
 */
bool FCataclysmSufferingAuraImmunityKeepsManaTest::RunTest(const FString&)
{
	using namespace CataclysmSufferingAuraImmunityTest;
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	const FImmunityTestPlayer Player(World);
	if (!TestNotNull(TEXT("set-up: the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("set-up: a possessed player with an ability system"), Player.IsUsable())
		|| !BuildTheRulesFloor(*this, Mode, Player, /*Beats=*/2))
	{
		return false;
	}

	const float HealthShare = OneBeatOfTheRule(Player.MaxHealth(), Effects::SufferingAuraHealthPercentPerSecond);
	const float ManaShare = OneBeatOfTheRule(Player.MaxMana(), Effects::SufferingAuraManaPercentPerSecond);

	// THE CONTROL: NO FLAG. Both pools fall, each by the rule's share. If this fails the rule did not step, and
	// nothing below would mean anything, so the test stops here.
	const FTakenByABeat Plain = TakeOneBeat(Mode, Player);
	if (!TestEqual(TEXT("control: with no flag a beat takes the rule's share of maximum health"),
				   Plain.Health, HealthShare, 0.01f)
		|| !TestEqual(TEXT("control: with no flag a beat takes the rule's share of maximum mana"),
					  Plain.Mana, ManaShare, 0.01f))
	{
		return false;
	}

	// THE FLAG IS WRITTEN, and the rule's question now answers yes.
	WriteTheFlag(Player.AbilitySystem, /*bHeld=*/true);
	if (!TestTrue(TEXT("set-up: the player holds the flag once its line is written"), Player.HoldsTheFlag()))
	{
		return false;
	}

	const FTakenByABeat Immune = TakeOneBeat(Mode, Player);
	if (!TestTrue(TEXT("set-up: the beat did not rewrite the stat line, so the flag is still held after it"),
				  Player.HoldsTheFlag()))
	{
		return false;
	}
	TestEqual(TEXT("with the flag held a beat takes no mana"), Immune.Mana, 0.0f, 0.001f);
	TestEqual(TEXT("and still takes the rule's share of maximum health"), Immune.Health, HealthShare, 0.01f);
	TestEqual(TEXT("which is what the beat with no flag took"), Immune.Health, Plain.Health, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSufferingAuraImmunityEndsWithTheFlagTest,
	"Cataclysm.SufferingAuraImmunity.OnceTheFlagIsRemovedTheNextBeatTakesManaAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The flag is written, a beat takes no mana, the flag is removed, and the next beat takes the rule's share of
 * mana. The flag is read on every beat; nothing is remembered from the beat before.
 *
 * REMOVED MEANS AN EMPTY STAT LINE HERE, written by hand: the player then holds no recorded line at all, which
 * is not what a refresh after taking a piece off would leave. What the rule asks answers no either way.
 *
 * STANDING: the player at the origin, on the floor `BuildFloor` makes for depth 1.
 */
bool FCataclysmSufferingAuraImmunityEndsWithTheFlagTest::RunTest(const FString&)
{
	using namespace CataclysmSufferingAuraImmunityTest;
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	const FImmunityTestPlayer Player(World);
	if (!TestNotNull(TEXT("set-up: the dungeon game mode spawned"), Mode)
		|| !TestTrue(TEXT("set-up: a possessed player with an ability system"), Player.IsUsable())
		|| !BuildTheRulesFloor(*this, Mode, Player, /*Beats=*/2))
	{
		return false;
	}

	const float ManaShare = OneBeatOfTheRule(Player.MaxMana(), Effects::SufferingAuraManaPercentPerSecond);

	// HELD: a beat takes no mana. Checked as set-up, because the removal below shows nothing unless the flag was
	// doing something before it went.
	WriteTheFlag(Player.AbilitySystem, /*bHeld=*/true);
	if (!TestTrue(TEXT("set-up: the player holds the flag once its line is written"), Player.HoldsTheFlag()))
	{
		return false;
	}
	const FTakenByABeat Held = TakeOneBeat(Mode, Player);
	if (!TestTrue(TEXT("set-up: the flag is still held after the beat"), Player.HoldsTheFlag())
		|| !TestEqual(TEXT("set-up: with the flag held the beat took no mana"), Held.Mana, 0.0f, 0.001f))
	{
		return false;
	}

	// REMOVED: the next beat takes the rule's share.
	WriteTheFlag(Player.AbilitySystem, /*bHeld=*/false);
	if (!TestFalse(TEXT("set-up: the player does not hold the flag once its line is gone"), Player.HoldsTheFlag()))
	{
		return false;
	}
	const FTakenByABeat After = TakeOneBeat(Mode, Player);
	TestEqual(TEXT("with the flag removed the next beat takes the rule's share of maximum mana"),
			  After.Mana, ManaShare, 0.01f);
	TestEqual(TEXT("and health went on falling by the same amount on both beats"), After.Health, Held.Health, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSufferingAuraImmunitySparesNoSkillCostTest,
	"Cataclysm.SufferingAuraImmunity.AHolderOfTheFlagStillPaysASkillsManaCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The flag stops one rule's drain and no other loss of mana. Ruled 2026-10-09. A SKILL'S COST is the other loss
 * read here, because it is the cheaper of the two the ruling names to reach: a strike that states its cost, used
 * once. An aura's upkeep is not read.
 *
 * TWO CASTERS, the second holding the flag. Each uses the same strike costing 40 mana. Both pay the same, and
 * what each pays is what the skill said it would cost.
 *
 * STANDING: the plain caster at the origin, the holder 100 m along Y. Nobody is struck. No dungeon game mode.
 */
bool FCataclysmSufferingAuraImmunitySparesNoSkillCostTest::RunTest(const FString&)
{
	using namespace CataclysmSufferingAuraImmunityTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FImmunityTestCaster Plain(World, FVector::ZeroVector);
	FImmunityTestCaster Holder(World, FVector(0.0f, 10000.0f, 0.0f));
	WriteTheFlag(Holder.AbilitySystem, /*bHeld=*/true);
	if (!TestFalse(TEXT("set-up: the plain caster does not hold the flag"),
				   UCataclysmSkillEffects::ImmuneToSufferingAuraManaDrainByARow(Plain.Actor))
		|| !TestTrue(TEXT("set-up: the holder holds the flag"),
					 UCataclysmSkillEffects::ImmuneToSufferingAuraManaDrainByARow(Holder.Actor)))
	{
		return false;
	}

	const FPaidForASkill PlainPaid = UseAStrikeCostingForty(Plain);
	const FPaidForASkill HolderPaid = UseAStrikeCostingForty(Holder);
	if (!TestTrue(TEXT("set-up: the plain caster was granted the strike and used it"), PlainPaid.bUsed)
		|| !TestTrue(TEXT("set-up: the holder was granted the strike and used it"), HolderPaid.bUsed)
		|| !TestTrue(FString::Printf(TEXT("set-up: the strike costs the plain caster mana: %.2f"), PlainPaid.Cost),
					 PlainPaid.Cost > 0.0f)
		|| !TestEqual(TEXT("control: the plain caster's mana fell by the strike's cost"),
					  PlainPaid.ManaLost, PlainPaid.Cost, 0.01f))
	{
		return false;
	}

	TestTrue(TEXT("the holder still holds the flag after the use"),
			 UCataclysmSkillEffects::ImmuneToSufferingAuraManaDrainByARow(Holder.Actor));
	TestEqual(TEXT("the strike costs a holder of the flag what it costs anybody"),
			  HolderPaid.Cost, PlainPaid.Cost, 0.01f);
	TestEqual(TEXT("and the holder's mana fell by that cost"), HolderPaid.ManaLost, PlainPaid.Cost, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
