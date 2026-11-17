// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSilence.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/SphereComponent.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Void, the Null Emperor set's second layer. Ruled 2026-10-17: the coordinating session's O7 and O8, recorded in
 * `docs/DECISIONS.md`. "Every time you hit a silenced enemy, you gain a stack of 'Void.' At 10 stacks, your next ability
 * costs no mana and deals 100% more damage."
 *
 * THE ROW IS MADE BY HAND, as a row of the effect table would give it: `kept_stacks_free_next_ability` on `hit_dealt`
 * under `target_is_silenced`, valued 100, capped at 10, its stacks named Void. No authored row carries the action; the
 * enchantment session writes it above this layer, with its real-row test wearing six pieces.
 *
 * TWO KINDS OF WORLD. A gain is read off a creature of the players' side wearing the row, in a world that has begun
 * play, as `Cataclysm.Silence.*` reads a silence; the hits are the row's own event raised by hand, as the tests of a
 * row's own stacks raise it. A use of an ability is read off a plain actor carrying health, mana and Fervour, in a world
 * that has not begun play, as `Cataclysm.Skills.*` reads one: its weapon damage is exact, so two uses compare. Every
 * damage figure is a difference against a control.
 *
 * THE GAIN TESTS NEED `State.Silenced` IN THE TAG VOCABULARY and ask for it first, for the reason
 * `CataclysmSilenceTests.cpp` gives. Actors stand at least a metre apart; separate scenes stand 40 m apart.
 */
namespace CataclysmVoidTest
{
	constexpr float M = 100.0f;

	/** What a fighter's weapon deals, so a use's damage is a whole figure. */
	constexpr float VoidWeaponDamage = 100.0f;

	/** The key the hand-made row keeps its stacks under. A worn row's is its enchantment, action and event. */
	const TCHAR* const VoidKey = TEXT("Test:kept_stacks_free_next_ability:hit_dealt");

	/** Whether the tag is registered; adds the reason as an error when it is not. */
	bool VoidSilenceTagIsRegistered(FAutomationTestBase& Test)
	{
		if (UCataclysmSilence::SilencedTag().IsValid())
		{
			return true;
		}
		Test.AddError(TEXT("State.Silenced is not in the tag vocabulary, so nothing can be silenced and no Void can be "
						   "gained. Regenerate the gameplay tags with python tools/generate_gameplay_tags.py"));
		return false;
	}

	/** The Null Emperor 6-piece's row, made by hand. */
	FCataclysmPoolAction VoidRow(int32 Cap = 10, float MorePercent = 100.0f)
	{
		FCataclysmPoolAction Row;
		Row.Event = FName(TEXT("hit_dealt"));
		Row.Pool = FName(UCataclysmAbilitySystemComponent::KeptStacksFreeNextAbilityAction);
		Row.Percent = MorePercent;
		Row.bKeptStacksFreeNextAbility = true;
		Row.KeptStackKey = FName(VoidKey);
		Row.KeptStackCap = Cap;
		Row.StatusName = TEXT("Void");
		Row.Condition = ECataclysmStatCondition::TargetIsSilenced;
		Row.TriggerKey = FName(VoidKey);
		return Row;
	}

	/** A creature of a side with a great deal of health, spawned 300 m away and moved, so it never spawns on another. */
	ACataclysmEnemyCharacter* VoidSpawnCreature(UWorld* World, ECataclysmTeam Side, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), FVector(30000.0f, 30000.0f, 0.0f), FRotator::ZeroRotator, Params);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(Side));
			Made->SetHealth(100000.0f);
			Made->SetAttackDamage(0.0f);
			Made->SetActorLocation(Where);
		}
		return Made;
	}

	UCataclysmAbilitySystemComponent* VoidSystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	/** One landed `hit_dealt` on `Struck`, carrying `Tags`, raised on the wearer as a blow raises it. */
	void VoidHit(UCataclysmAbilitySystemComponent* Wearer, const AActor* Struck, const FGameplayTagContainer* Tags,
				 bool bLanded = true)
	{
		Wearer->ActOnEvent(FName(TEXT("hit_dealt")), Tags, /*EventAmount=*/100.0f, bLanded, Struck);
	}

	/** An actor a sphere overlap can find, carrying health, mana, Fervour and an exact weapon damage. */
	struct FVoidFighter
	{
		FVoidFighter(UWorld* World, const FVector& Where)
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
			// FERVOUR, BECAUSE THE ULTIMATE SLOT COSTS 50 OF IT and the free next ability does not waive it.
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), VoidWeaponDamage);
		}

		~FVoidFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		float Health() const { return Get(UCataclysmVitalAttributeSet::GetHealthAttribute()); }
		float Mana() const { return Get(UCataclysmVitalAttributeSet::GetManaAttribute()); }
		float Fervour() const { return Get(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()); }

		/** Wear the row and hold this many of its stacks. */
		void WearVoidHolding(int32 Stacks)
		{
			AbilitySystem->SetPoolActions({VoidRow()});
			for (int32 Each = 0; Each < Stacks; ++Each)
			{
				AbilitySystem->GainKeptStack(FName(VoidKey), 10);
			}
		}

		int32 Void() const { return AbilitySystem->KeptStacksHeld(FName(VoidKey)); }

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A strike granted into a slot at level 100, so its cost is the Skill Slots sheet's figure. */
	UCataclysmStrikeSkill* VoidGrantStrike(FVoidFighter& Caster, ECataclysmAbilitySlot Slot)
	{
		const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
			UCataclysmStrikeSkill::StaticClass(), Slot, /*Level=*/100, Caster.Actor);
		FGameplayAbilitySpec* Spec =
			Handle.IsValid() ? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmStrikeSkill* Strike = Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Strike)
		{
			Strike->SkillName = TEXT("Void Test Strike");
			Strike->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=4; Angle=360"));
			Strike->SkillTags = UCataclysmSkillShapes::TagsFromCell(FString());
		}
		return Strike;
	}

	bool VoidActivate(FVoidFighter& Caster, UGameplayAbility* Ability)
	{
		return Ability && Caster.AbilitySystem->TryActivateAbility(
			Ability->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
	}

	/** What one use of a granted ability took and dealt, read as differences across it. */
	struct FVoidUse
	{
		bool bActivated = false;
		float ManaPaid = 0.0f;
		float FervourPaid = 0.0f;
		float Dealt = 0.0f;
	};

	FVoidUse VoidUse(FVoidFighter& Caster, FVoidFighter& Target, UGameplayAbility* Ability)
	{
		FVoidUse Use;
		const float ManaBefore = Caster.Mana();
		const float FervourBefore = Caster.Fervour();
		const float HealthBefore = Target.Health();
		Use.bActivated = VoidActivate(Caster, Ability);
		Use.ManaPaid = ManaBefore - Caster.Mana();
		Use.FervourPaid = FervourBefore - Caster.Fervour();
		Use.Dealt = HealthBefore - Target.Health();
		return Use;
	}
}

// ---------------------------------------------------------------------------
// The gain: one Void for each hit on a silenced enemy, to at most 10, with no lifetime.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidGainedOnASilencedEnemyTest,
	"Cataclysm.Silence.Void.AHitOnASilencedEnemyGainsOneAndAHitOnAnUnsilencedOneGainsNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * O7. A wearer of the row and two creatures 3 m off, one of them silenced. The controls first: nothing held, and a hit
 * on the creature that is not silenced gains nothing. Then a hit on the silenced one gains one; a hit on the other still
 * gains nothing; an evaded hit gains nothing; and a spell's hit on the silenced one gains one, because the sentence
 * says "every time you hit" and not "your attacks". The count is read by the row's key and by its name.
 */
bool FCataclysmVoidGainedOnASilencedEnemyTest::RunTest(const FString&)
{
	using namespace CataclysmVoidTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!VoidSilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Wearer = VoidSpawnCreature(World, ECataclysmTeam::Players, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Silenced = VoidSpawnCreature(World, ECataclysmTeam::Monsters, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Unsilenced = VoidSpawnCreature(World, ECataclysmTeam::Monsters, FVector(-3 * M, 0, 0));
	UCataclysmAbilitySystemComponent* System = VoidSystemOf(Wearer);
	const FGameplayTag MeleeTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee")), false);
	const FGameplayTag SpellTag = UCataclysmSkillEffects::SpellTag();
	if (!TestNotNull(TEXT("set-up: a wearer with an ability system of ours"), System)
		|| !TestNotNull(TEXT("set-up: a creature to silence"), Silenced)
		|| !TestNotNull(TEXT("set-up: a creature left alone"), Unsilenced)
		|| !TestTrue(TEXT("set-up: Type.Melee and Type.Spell are registered"), MeleeTag.IsValid() && SpellTag.IsValid()))
	{
		return false;
	}
	const FGameplayTagContainer Melee(MeleeTag);
	const FGameplayTagContainer Spell(SpellTag);
	const FName Key(VoidKey);
	System->SetPoolActions({VoidRow()});

	// THE CONTROLS, READ FIRST.
	TestEqual(TEXT("control: wearing the row, no Void is held"), System->KeptStacksHeld(Key), 0);
	VoidHit(System, Unsilenced, &Melee);
	TestEqual(TEXT("control: a hit on an enemy that is not silenced gains none"), System->KeptStacksHeld(Key), 0);

	if (!TestTrue(TEXT("set-up: the silence lands"),
				  UCataclysmSilence::ApplySilence(Wearer, Silenced, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}
	VoidHit(System, Silenced, &Melee);
	TestEqual(TEXT("a hit on the silenced enemy gains one"), System->KeptStacksHeld(Key), 1);
	VoidHit(System, Unsilenced, &Melee);
	TestEqual(TEXT("a hit on the other, still not silenced, gains none"), System->KeptStacksHeld(Key), 1);
	VoidHit(System, Silenced, &Melee, /*bLanded=*/false);
	TestEqual(TEXT("a hit on the silenced enemy that did not land gains none"), System->KeptStacksHeld(Key), 1);
	VoidHit(System, Silenced, &Spell);
	TestEqual(TEXT("a spell's hit on the silenced enemy gains one: every hit counts"), System->KeptStacksHeld(Key), 2);

	// THE READER A LATER LAYER ASKS BY THE NAME, with a control of another name.
	TestEqual(TEXT("read by its name, Void is two"), System->KeptStacksHeldNamed(TEXT("Void")), 2);
	TestEqual(TEXT("and the name's case does not matter"), System->KeptStacksHeldNamed(TEXT("void")), 2);
	TestEqual(TEXT("control: a name no worn row gives reads nought"), System->KeptStacksHeldNamed(TEXT("Temporal Flux")),
			  0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidStopsAtTenTest,
	"Cataclysm.Silence.Void.ItStopsAtTenAndTimeTakesNoneAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * O7 (c). Ten hits on a silenced enemy hold ten and the eleventh leaves ten. The next ability is free only at ten: the
 * control is the same wearer at nine. Ten minutes on the clock, with no timer fired so the silence does not run out
 * between the hits, take no Void away: none is stated to expire.
 */
bool FCataclysmVoidStopsAtTenTest::RunTest(const FString&)
{
	using namespace CataclysmVoidTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!VoidSilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Wearer = VoidSpawnCreature(World, ECataclysmTeam::Players, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Silenced = VoidSpawnCreature(World, ECataclysmTeam::Monsters, FVector(3 * M, 0, 0));
	UCataclysmAbilitySystemComponent* System = VoidSystemOf(Wearer);
	if (!TestNotNull(TEXT("set-up: a wearer with an ability system of ours"), System)
		|| !TestNotNull(TEXT("set-up: a creature to silence"), Silenced)
		|| !TestTrue(TEXT("set-up: the silence lands"),
					 UCataclysmSilence::ApplySilence(Wearer, Silenced, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}
	const FName Key(VoidKey);
	System->SetPoolActions({VoidRow()});

	for (int32 Hit = 0; Hit < 9; ++Hit)
	{
		VoidHit(System, Silenced, nullptr);
	}
	TestEqual(TEXT("control: nine hits on the silenced enemy hold nine"), System->KeptStacksHeld(Key), 9);
	TestFalse(TEXT("control: at nine the next ability is not free"), System->NextAbilityIsFree());

	VoidHit(System, Silenced, nullptr);
	TestEqual(TEXT("the tenth holds ten"), System->KeptStacksHeld(Key), 10);
	TestTrue(TEXT("and at ten the next ability is free"), System->NextAbilityIsFree());
	VoidHit(System, Silenced, nullptr);
	TestEqual(TEXT("the eleventh leaves ten"), System->KeptStacksHeld(Key), 10);

	World->TimeSeconds += 600.0;
	TestEqual(TEXT("ten minutes later ten are still held"), System->KeptStacksHeld(Key), 10);
	TestTrue(TEXT("and the next ability is still free"), System->NextAbilityIsFree());
	return true;
}

// ---------------------------------------------------------------------------
// The spend: at ten, the next ability but the basic attack costs no mana, deals twice, and spends all ten.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidFreeAndDoubledTest,
	"Cataclysm.Silence.Void.AtTenTheNextAbilityCostsNoManaDealsTwiceAndSpendsAllTen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * O7 (c), O8. Two casters 40 m apart, each wearing the row with a target 2 m off and the same Heavy and Special strikes.
 * The control holds nine Void, the other ten. The control's Heavy pays its cost and deals its damage; the other's costs
 * nothing, deals twice the control's, and leaves no Void. Then each uses its Special: the one that spent its Void pays
 * and deals exactly what the control does. An aura's upkeep is never free: the cost asked for one is the control's.
 */
bool FCataclysmVoidFreeAndDoubledTest::RunTest(const FString&)
{
	using namespace CataclysmVoidTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	FVoidFighter Nine(World, FVector::ZeroVector);
	FVoidFighter NinesTarget(World, FVector(2 * M, 0, 0));
	FVoidFighter Ten(World, FVector(40 * M, 0, 0));
	FVoidFighter TensTarget(World, FVector(42 * M, 0, 0));
	UCataclysmStrikeSkill* NineHeavy = VoidGrantStrike(Nine, ECataclysmAbilitySlot::Heavy);
	UCataclysmStrikeSkill* NineSpecial = VoidGrantStrike(Nine, ECataclysmAbilitySlot::Special);
	UCataclysmStrikeSkill* TenHeavy = VoidGrantStrike(Ten, ECataclysmAbilitySlot::Heavy);
	UCataclysmStrikeSkill* TenSpecial = VoidGrantStrike(Ten, ECataclysmAbilitySlot::Special);
	if (!TestTrue(TEXT("set-up: four strikes granted"), NineHeavy && NineSpecial && TenHeavy && TenSpecial))
	{
		return false;
	}
	Nine.WearVoidHolding(9);
	Ten.WearVoidHolding(10);
	const float HeavyCost = NineHeavy->ManaCostFor(Nine.AbilitySystem);
	if (!TestEqual(TEXT("set-up: the control holds nine Void"), Nine.Void(), 9)
		|| !TestEqual(TEXT("set-up: the other holds ten"), Ten.Void(), 10)
		|| !TestTrue(FString::Printf(TEXT("set-up: at nine the Heavy strike costs mana (%.1f)"), HeavyCost),
					 HeavyCost > 0.0f))
	{
		return false;
	}

	// WHAT THE BAR AND THE CHECK READ, BEFORE ANY USE.
	TestEqual(TEXT("at ten the Heavy strike costs nothing"), TenHeavy->ManaCostFor(Ten.AbilitySystem), 0.0f);
	TestEqual(TEXT("and an upkeep asked of it is the control's full cost: an upkeep is no use"),
			  TenHeavy->ManaCostFor(Ten.AbilitySystem, /*bForAUse=*/false),
			  NineHeavy->ManaCostFor(Nine.AbilitySystem, /*bForAUse=*/false), 0.001f);

	// THE CONTROL'S HEAVY, AT NINE.
	const FVoidUse NineUse = VoidUse(Nine, NinesTarget, NineHeavy);
	if (!TestTrue(TEXT("control: at nine the Heavy strike activates"), NineUse.bActivated)
		|| !TestTrue(TEXT("set-up: and it dealt damage"), NineUse.Dealt > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: at nine it pays its cost"), NineUse.ManaPaid, HeavyCost, 0.01f);
	TestEqual(TEXT("control: and keeps its nine Void"), Nine.Void(), 9);
	TestEqual(TEXT("control: and multiplied its damage by nothing"), NineHeavy->LastNextUseMoreMultiplier, 1.0f, 0.001f);

	// THE SAME USE AT TEN.
	const FVoidUse TenUse = VoidUse(Ten, TensTarget, TenHeavy);
	if (!TestTrue(TEXT("at ten the Heavy strike activates"), TenUse.bActivated))
	{
		return false;
	}
	TestEqual(TEXT("at ten it costs no mana"), TenUse.ManaPaid, 0.0f, 0.01f);
	TestEqual(TEXT("and deals twice what the same use at nine dealt"), TenUse.Dealt, 2.0f * NineUse.Dealt,
			  0.001f * NineUse.Dealt);
	TestEqual(TEXT("and recorded twice as its multiplier"), TenHeavy->LastNextUseMoreMultiplier, 2.0f, 0.001f);
	TestEqual(TEXT("and spent all ten Void"), Ten.Void(), 0);
	TestFalse(TEXT("so the next ability is not free"), Ten.AbilitySystem->NextAbilityIsFree());

	// THE USE AFTER IT PAYS AND DEALS AS NORMAL, against the control's use of the same strike.
	const FVoidUse NineAfter = VoidUse(Nine, NinesTarget, NineSpecial);
	const FVoidUse TenAfter = VoidUse(Ten, TensTarget, TenSpecial);
	if (!TestTrue(TEXT("control: the control's Special activates"), NineAfter.bActivated)
		|| !TestTrue(TEXT("the Special after the spend activates"), TenAfter.bActivated)
		|| !TestTrue(TEXT("set-up: the control's Special pays mana and deals damage"),
					 NineAfter.ManaPaid > 0.0f && NineAfter.Dealt > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the use after the spend pays what the control pays"), TenAfter.ManaPaid, NineAfter.ManaPaid, 0.01f);
	TestEqual(TEXT("and deals what the control deals"), TenAfter.Dealt, NineAfter.Dealt, 0.001f * NineAfter.Dealt);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidNotSpentTest,
	"Cataclysm.Silence.Void.ABasicAttackAndARefusedActivationSpendNoneAndFervourIsStillPaid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * O7 (c), and the judgement on Fervour. One caster wearing the row, with a target 2 m off. Its Heavy strike is used once
 * before any Void is held, so it is cooling down. At ten Void: a basic attack activates and spends nothing; the Heavy is
 * refused by its cooldown and spends nothing; the Ultimate, with no Fervour, is refused and spends nothing. Given 50
 * Fervour the Ultimate activates: it pays the 50 Fervour, no mana, and spends the ten.
 */
bool FCataclysmVoidNotSpentTest::RunTest(const FString&)
{
	using namespace CataclysmVoidTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	FVoidFighter Caster(World, FVector::ZeroVector);
	FVoidFighter Target(World, FVector(2 * M, 0, 0));
	UCataclysmStrikeSkill* Basic = VoidGrantStrike(Caster, ECataclysmAbilitySlot::BasicAttack);
	UCataclysmStrikeSkill* Heavy = VoidGrantStrike(Caster, ECataclysmAbilitySlot::Heavy);
	UCataclysmStrikeSkill* Ultimate = VoidGrantStrike(Caster, ECataclysmAbilitySlot::Ultimate);
	if (!TestTrue(TEXT("set-up: three strikes granted"), Basic && Heavy && Ultimate))
	{
		return false;
	}
	Caster.WearVoidHolding(0);

	// THE CONTROL FOR THE REFUSAL: with no Void held the Heavy activates, and so starts its cooldown.
	if (!TestTrue(TEXT("control: with no Void the Heavy strike activates"), VoidUse(Caster, Target, Heavy).bActivated))
	{
		return false;
	}
	Caster.WearVoidHolding(10);
	if (!TestEqual(TEXT("set-up: ten Void are held"), Caster.Void(), 10)
		|| !TestTrue(TEXT("set-up: so the next ability is free"), Caster.AbilitySystem->NextAbilityIsFree())
		|| !TestEqual(TEXT("set-up: the caster has no Fervour"), Caster.Fervour(), 0.0f))
	{
		return false;
	}

	// THE BASIC ATTACK. A swing that waits on a clip is finished on the clock.
	TestTrue(TEXT("the basic attack activates"), VoidUse(Caster, Target, Basic).bActivated);
	CataclysmTestWorld::RunClock(World, 1.0f);
	TestEqual(TEXT("and spends no Void"), Caster.Void(), 10);
	TestEqual(TEXT("and multiplied its damage by nothing"), Basic->LastNextUseMoreMultiplier, 1.0f, 0.001f);

	// A USE REFUSED BY ITS COOLDOWN.
	const FVoidUse Refused = VoidUse(Caster, Target, Heavy);
	TestFalse(TEXT("the Heavy strike is refused by its cooldown"), Refused.bActivated);
	TestEqual(TEXT("and spends no Void"), Caster.Void(), 10);

	// A USE REFUSED FOR WANT OF FERVOUR, WHICH THE FREE NEXT ABILITY DOES NOT WAIVE.
	TestFalse(TEXT("the Ultimate with no Fervour is refused"), VoidUse(Caster, Target, Ultimate).bActivated);
	TestEqual(TEXT("and spends no Void"), Caster.Void(), 10);

	Caster.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 50.0f);
	const FVoidUse Paid = VoidUse(Caster, Target, Ultimate);
	if (!TestTrue(TEXT("given 50 Fervour the Ultimate activates"), Paid.bActivated))
	{
		return false;
	}
	TestEqual(TEXT("it pays the 50 Fervour"), Paid.FervourPaid, 50.0f, 0.01f);
	TestEqual(TEXT("and no mana"), Paid.ManaPaid, 0.0f, 0.01f);
	TestEqual(TEXT("and spends all ten Void"), Caster.Void(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// What ends it, and what the wearer sees.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidEndsTest,
	"Cataclysm.Silence.Void.ADeathAndLeavingTheDungeonClearIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * O7 (c): cleared at death and on leaving the dungeon, where a kept stack is cleared. A death is
 * `ClearWhatDeathEnds`, which the player's revival runs. The dungeon is a dungeon game mode spawned into the world,
 * whose `EmpireDungeonId` says which is walked, as `Cataclysm.Enchantments.KeptStacks.*` moves it. Each clearing has
 * the control that ten were held before it, and each takes the free next ability with it.
 */
bool FCataclysmVoidEndsTest::RunTest(const FString&)
{
	using namespace CataclysmVoidTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Wearer = VoidSpawnCreature(World, ECataclysmTeam::Players, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* System = VoidSystemOf(Wearer);
	if (!TestNotNull(TEXT("set-up: a wearer with an ability system of ours"), System))
	{
		return false;
	}
	const FName Key(VoidKey);
	System->SetPoolActions({VoidRow()});
	const auto GainTen = [System, &Key]()
	{
		for (int32 Each = 0; Each < 10; ++Each)
		{
			System->GainKeptStack(Key, 10);
		}
	};

	// A DEATH.
	GainTen();
	if (TestEqual(TEXT("control: ten held before the death"), System->KeptStacksHeld(Key), 10)
		&& TestTrue(TEXT("control: and the next ability is free"), System->NextAbilityIsFree()))
	{
		System->ClearWhatDeathEnds();
		TestEqual(TEXT("after what a death ends is cleared, no Void is held"), System->KeptStacksHeld(Key), 0);
		TestFalse(TEXT("and the next ability is not free"), System->NextAbilityIsFree());
	}

	// THE DUNGEON LEFT.
	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("set-up: the dungeon game mode spawned"), Mode))
	{
		return false;
	}
	Mode->EmpireDungeonId = 7;
	GainTen();
	if (TestEqual(TEXT("control: ten held inside dungeon 7"), System->KeptStacksHeld(Key), 10)
		&& TestTrue(TEXT("control: and the next ability is free there"), System->NextAbilityIsFree()))
	{
		Mode->EmpireDungeonId = INDEX_NONE;
		TestEqual(TEXT("the dungeon left, no Void is held"), System->KeptStacksHeld(Key), 0);
		TestEqual(TEXT("and none is read by its name"), System->KeptStacksHeldNamed(TEXT("Void")), 0);
		TestFalse(TEXT("and the next ability is not free"), System->NextAbilityIsFree());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVoidLineTest,
	"Cataclysm.Silence.Void.TheLineReadsTheCountAndAtTenTheFreeNextAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every system ships with a basic display. The pure entry first, with the control that a kept-stack row which buys
 * nothing at its cap reads only its count there; then the line above a living wearer's bars, read as the heads-up
 * display reads it, at nought, at seven and at ten.
 */
bool FCataclysmVoidLineTest::RunTest(const FString&)
{
	using namespace CataclysmVoidTest;

	TestEqual(TEXT("control: a row whose stacks buy nothing reads its count at its cap"),
			  UCataclysmCombatOverlay::KeptStacksEntry(TEXT("Temporal Flux"), 5, 5), FString(TEXT("Temporal Flux 5/5")));
	TestEqual(TEXT("seven Void read their count"),
			  UCataclysmCombatOverlay::KeptStacksEntry(TEXT("Void"), 7, 10, /*bFreeNextAbility=*/true, 100.0f),
			  FString(TEXT("Void 7/10")));
	TestEqual(TEXT("ten Void say the next ability is free and what it deals"),
			  UCataclysmCombatOverlay::KeptStacksEntry(TEXT("Void"), 10, 10, /*bFreeNextAbility=*/true, 100.0f),
			  FString(TEXT("Void 10/10: next ability free, 100% more damage")));

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmEnemyCharacter* Wearer = VoidSpawnCreature(World, ECataclysmTeam::Players, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* System = VoidSystemOf(Wearer);
	if (!TestNotNull(TEXT("set-up: a wearer with an ability system of ours"), System))
	{
		return false;
	}
	TestEqual(TEXT("control: wearing nothing, the line is empty"), UCataclysmCombatOverlay::KeptStacksTextFor(Wearer),
			  FString());
	System->SetPoolActions({VoidRow()});
	TestEqual(TEXT("wearing the row, the line reads Void 0/10"), UCataclysmCombatOverlay::KeptStacksTextFor(Wearer),
			  FString(TEXT("Void 0/10")));
	for (int32 Each = 0; Each < 7; ++Each)
	{
		System->GainKeptStack(FName(VoidKey), 10);
	}
	TestEqual(TEXT("at seven it reads Void 7/10"), UCataclysmCombatOverlay::KeptStacksTextFor(Wearer),
			  FString(TEXT("Void 7/10")));
	for (int32 Each = 0; Each < 3; ++Each)
	{
		System->GainKeptStack(FName(VoidKey), 10);
	}
	TestEqual(TEXT("at ten it says the next ability is free"), UCataclysmCombatOverlay::KeptStacksTextFor(Wearer),
			  FString(TEXT("Void 10/10: next ability free, 100% more damage")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
