// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Items/CataclysmItem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Spellblade's Will's six-piece bonus: Dervish and Spellslinger. Ruled 2026-10-06 and 2026-10-17.
 *
 * "When you use a melee attack, you gain a stack of Spellslinger which reduces the mana cost of your spells by 10% per
 * stack, and when you use a spell, you gain a stack of Dervish which increases your attack speed for melee attacks by
 * 10% per stack"
 *
 * DERVISH IS ONE ROW AND NEEDS NO GAME CHANGE: `attack_speed`, increased 10, scaled by its own stacks, granted on the
 * event `spell`, five stacks lasting 5 s, no Required Tags (ruled: "Dervish raises every swing").
 *
 * SPELLSLINGER IS ONE ROW ON THE EVENT RULED FOR IT ON 2026-10-17 (R1), `melee_attack_use`, raised for every paid use
 * tagged `Type.Melee`, the basic attack included: `mana_cost`, more -10, Required Tags `Type.Spell`, scaled by its own
 * stacks, five stacks lasting 5 s.
 *
 * Each row is made up here in that shape, on the real non-set enchantment "Double your energy shield", and passed
 * through `UCataclysmItemModifiers::AccumulateEnchantmentsInto`. The real rows, worn at six pieces with a control at
 * five, are the enchantment session's test.
 *
 * A USE IS HANDED TO THE ROWS BY HAND: a spell's use is `NoteSpellCast`, the call `UCataclysmSkillTemplate::
 * CommitAndBegin` makes for a paid use of a skill tagged `Type.Spell`, and a melee use is `ActOnSkillUse` with a melee
 * strike's tags, the call that use's notice makes. Time is moved by adding to the world's clock.
 */
namespace CataclysmSetBonusSpellbladeSixTest
{
	/** The real enchantment the made-up row is put on, and the drawback beside it, which has no effect row. */
	const TCHAR* const Benefit = TEXT("Positive_Double_your_energy_shield");
	const TCHAR* const Drawback = TEXT("Negative_Can_t_use_a_basic_attack");

	/** How long a stack lasts, and the most held. */
	constexpr float StackSeconds = 5.0f;
	constexpr int32 MostStacks = 5;

	/** The tags of a melee strike in the Heavy slot. */
	const TCHAR* const MeleeStrikeTagCell = TEXT("Type.Strike, Type.Melee, Slot.Heavy");

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

	/** An actor carrying all five attribute sets, so the swing interval has an attack speed to read. */
	struct FWearer
	{
		explicit FWearer(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			System = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			System->RegisterComponent();
			System->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			System->AddAttributeSetSubobject(NewObject<UCataclysmPrimaryAttributeSet>(Actor));
			System->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			System->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			System->InitAbilityActorInfo(Actor, Actor);
		}

		~FWearer()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/**
	 * Gives the wearer the made-up Dervish row and answers its stack key. None, with a failed set-up assertion, when
	 * the row did not become one attack speed modifier and one grant of the shape above. The attack speed line's base
	 * is one swing a second, and the attribute says the same.
	 */
	FName WearDervish(FAutomationTestBase& Test, FWearer& Wearer)
	{
		UDataTable* Positive = LoadData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
		UDataTable* Negative = LoadData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
		UDataTable* Effects = NewObject<UDataTable>();
		Effects->RowStruct = FCataclysmEnchantmentEffectRow::StaticStruct();
		const bool bEffectsRead = Effects->CreateTableFromCSVString(
			FString(TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,RequiredTags,Condition,ConditionValue,"
						 "Scale,ScaleStep,Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,"
						 "EverySeconds,EveryNth,ScaleStepHigh,StackSecondsHigh,Condition2,ConditionValue2,"
						 "ConditionValueHigh,TriggerCooldown,EventValue,Ailment,DamageShare\n"))
			+ FString::Printf(TEXT("%s#1,%s,attack_speed,increased,10,10,,,0,own_stacks,1,,spell,,5,5,0,0,0,0,0,,0,0,0,0,,0\n"),
							  Benefit, Benefit)).Num() == 0;
		if (!Test.TestNotNull(TEXT("set-up: the positive enchantments"), Positive)
			|| !Test.TestNotNull(TEXT("set-up: the negative enchantments"), Negative)
			|| !Test.TestTrue(TEXT("set-up: the made-up row reads as an effect table"), bEffectsRead))
		{
			return NAME_None;
		}

		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(Benefit);
		Rolled.Negative = FName(Drawback);
		Item.Enchantments.Add(Rolled);
		Item.EnchantmentCount = 1;
		TMap<FName, TArray<FCataclysmStatModifier>> Totals;
		TArray<FCataclysmPoolAction> Actions;
		UCataclysmItemModifiers::AccumulateEnchantmentsInto(Totals, {Item}, Effects, Positive, Negative, &Actions);

		const TArray<FCataclysmStatModifier>* Made = Totals.Find(FName(TEXT("attack_speed")));
		if (!Test.TestTrue(TEXT("set-up: the row became one attack speed modifier and one grant"),
				Made && Made->Num() == 1 && Totals.Num() == 1 && Actions.Num() == 1))
		{
			return NAME_None;
		}
		const FCataclysmStatModifier Modifier = (*Made)[0];
		const FCataclysmPoolAction Grant = Actions[0];
		if (!Test.TestTrue(TEXT("set-up: the modifier is a 10% increase a stack, under a key, requiring no tag"),
				Modifier.Bucket == ECataclysmStatBucket::Increased && FMath::IsNearlyEqual(Modifier.Value, 10.0f)
					&& Modifier.Scale == ECataclysmStatScale::PerOwnStack && !Modifier.StackKey.IsNone()
					&& Modifier.RequiredTags.IsEmpty())
			|| !Test.TestTrue(TEXT("set-up: the grant is on spell under the same key, five stacks, five seconds"),
				Grant.Event == FName(TEXT("spell")) && Grant.StackKey == Modifier.StackKey
					&& Grant.StackCap == MostStacks && FMath::IsNearlyEqual(Grant.StackSeconds, StackSeconds)))
		{
			return NAME_None;
		}

		Wearer.System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackSpeedAttribute(), 1.0f);
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Line = Lines.Add(FName(TEXT("attack_speed")));
		Line.Base = 1.0f;
		Line.Modifiers = {Modifier};
		Wearer.System->SetStatInputs(MoveTemp(Lines));
		Wearer.System->SetPoolActions(MoveTemp(Actions));
		return Grant.StackKey;
	}

	/** The seconds between the wearer's swings, as the basic attack asks them. */
	float SwingGap(const FWearer& Wearer)
	{
		return UCataclysmBasicAttack::SecondsBetweenSwingsFor(Wearer.System);
	}

	/** One paid use of a melee strike, handed to the rows. */
	void UseAMeleeStrike(FWearer& Wearer, bool bBasicAttack)
	{
		const FGameplayTagContainer Melee = UCataclysmSkillShapes::TagsFromCell(MeleeStrikeTagCell);
		Wearer.System->ActOnSkillUse(FName(TEXT("A melee strike")), &Melee, FVector(300.0f, 0.0f, 0.0f), bBasicAttack);
	}

	/** The tags of a spell in the Special slot, and of a ranged basic attack. */
	const TCHAR* const SpellTagCell = TEXT("Type.Spell, Type.Projectile, Slot.Special");
	const TCHAR* const RangedTagCell = TEXT("Type.Projectile, Type.Ranged");

	/** One paid use of a spell, handed to the rows as its use's notice and `CommitAndBegin` hand it: both events. */
	void UseASpell(FWearer& Wearer)
	{
		const FGameplayTagContainer Spell = UCataclysmSkillShapes::TagsFromCell(SpellTagCell);
		Wearer.System->ActOnSkillUse(FName(TEXT("A spell")), &Spell, FVector(300.0f, 0.0f, 0.0f));
		Wearer.System->NoteSpellCast();
	}

	/** One ranged basic attack, handed to the rows. */
	void UseARangedBasicAttack(FWearer& Wearer)
	{
		const FGameplayTagContainer Ranged = UCataclysmSkillShapes::TagsFromCell(RangedTagCell);
		Wearer.System->ActOnSkillUse(FName(TEXT("A shot")), &Ranged, FVector(300.0f, 0.0f, 0.0f),
									 /*bBasicAttack=*/true);
	}

	/**
	 * Gives the wearer the made-up Spellslinger row and answers its stack key. None, with a failed set-up assertion,
	 * when the row did not become one mana cost modifier and one grant of the shape above.
	 */
	FName WearSpellslinger(FAutomationTestBase& Test, FWearer& Wearer)
	{
		UDataTable* Positive = LoadData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsPositive.csv"));
		UDataTable* Negative = LoadData<FCataclysmEnchantmentRow>(TEXT("EnchantmentsNegative.csv"));
		UDataTable* Effects = NewObject<UDataTable>();
		Effects->RowStruct = FCataclysmEnchantmentEffectRow::StaticStruct();
		const bool bEffectsRead = Effects->CreateTableFromCSVString(
			FString(TEXT("Name,Enchantment,Stat,ValueKind,ValueLow,ValueHigh,RequiredTags,Condition,ConditionValue,"
						 "Scale,ScaleStep,Action,ActionEvent,FractionOf,ScaleMaxSteps,StackSeconds,ScaleOffset,"
						 "EverySeconds,EveryNth,ScaleStepHigh,StackSecondsHigh,Condition2,ConditionValue2,"
						 "ConditionValueHigh,TriggerCooldown,EventValue,Ailment,DamageShare\n"))
			+ FString::Printf(
				TEXT("%s#1,%s,mana_cost,more,-10,-10,Type.Spell,,0,own_stacks,1,,melee_attack_use,,5,5,0,0,0,0,0,,0,0,0,0,,0\n"),
				Benefit, Benefit)).Num() == 0;
		if (!Test.TestNotNull(TEXT("set-up: the positive enchantments"), Positive)
			|| !Test.TestNotNull(TEXT("set-up: the negative enchantments"), Negative)
			|| !Test.TestTrue(TEXT("set-up: the made-up row reads as an effect table"), bEffectsRead))
		{
			return NAME_None;
		}

		FCataclysmItem Item;
		Item.Base = FName(TEXT("Head_Helm"));
		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = FName(Benefit);
		Rolled.Negative = FName(Drawback);
		Item.Enchantments.Add(Rolled);
		Item.EnchantmentCount = 1;
		TMap<FName, TArray<FCataclysmStatModifier>> Totals;
		TArray<FCataclysmPoolAction> Actions;
		UCataclysmItemModifiers::AccumulateEnchantmentsInto(Totals, {Item}, Effects, Positive, Negative, &Actions);

		const TArray<FCataclysmStatModifier>* Made = Totals.Find(FName(TEXT("mana_cost")));
		if (!Test.TestTrue(TEXT("set-up: the row became one mana cost modifier and one grant"),
				Made && Made->Num() == 1 && Totals.Num() == 1 && Actions.Num() == 1))
		{
			return NAME_None;
		}
		const FCataclysmStatModifier Modifier = (*Made)[0];
		const FCataclysmPoolAction Grant = Actions[0];
		const FGameplayTag Spell = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Spell")), /*ErrorIfNotFound=*/false);
		if (!Test.TestTrue(TEXT("set-up: the modifier is a 10% cut a stack, under a key, requiring Type.Spell"),
				Modifier.Bucket == ECataclysmStatBucket::More && FMath::IsNearlyEqual(Modifier.Value, -10.0f)
					&& Modifier.Scale == ECataclysmStatScale::PerOwnStack && !Modifier.StackKey.IsNone()
					&& Spell.IsValid() && Modifier.RequiredTags.HasTagExact(Spell) && Modifier.RequiredTags.Num() == 1)
			|| !Test.TestTrue(TEXT("set-up: the grant is on melee_attack_use under the same key, five stacks, five "
								   "seconds"),
				Grant.Event == FName(TEXT("melee_attack_use")) && Grant.StackKey == Modifier.StackKey
					&& Grant.StackCap == MostStacks && FMath::IsNearlyEqual(Grant.StackSeconds, StackSeconds)))
		{
			return NAME_None;
		}

		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Line = Lines.Add(FName(TEXT("mana_cost")));
		Line.Base = 0.0f;
		Line.Modifiers = {Modifier};
		Wearer.System->SetStatInputs(MoveTemp(Lines));
		Wearer.System->SetPoolActions(MoveTemp(Actions));
		return Grant.StackKey;
	}

	/** A skill the wearer holds in a slot, with these tags and a cost of 40, so `ManaCostFor` has a cost to read. */
	UCataclysmSelfBuffSkill* GrantCosting(FWearer& Wearer, ECataclysmAbilitySlot Slot, const TCHAR* TagCell)
	{
		const FGameplayAbilitySpecHandle Handle = Wearer.System->GiveAbilityInSlot(
			UCataclysmSelfBuffSkill::StaticClass(), Slot, /*Level=*/100, Wearer.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? Wearer.System->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmSelfBuffSkill* Skill = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = TEXT("A costing skill");
			Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Duration=6"));
			Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
			Skill->ManaCostOverride = 40.0f;
		}
		return Skill;
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellbladeSixDervishGrantedTest,
	"Cataclysm.SetBonus.Spellblade6.ASpellGivesAStackOfDervishAndEachRaisesTheSwingRateByATenthUpToFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * In order. Control: with no stack the swing gap is a second. One spell: one stack, and the gap is 1 / 1.1 s. Four
 * more: five stacks, 1 / 1.5 s. A sixth: still five, still 1 / 1.5 s. Asked for a melee strike's tags the attack speed
 * is the same 1.5, so the row reaches every swing and not only the basic attack's. 4.9 s after the last spell, five
 * stacks; 5.1 s after, none, and a second again.
 */
bool FCataclysmSpellbladeSixDervishGrantedTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusSpellbladeSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FWearer Wearer(World);
	const FName Key = WearDervish(*this, Wearer);
	if (Key.IsNone())
	{
		return false;
	}

	const float Plain = SwingGap(Wearer);
	if (!TestEqual(TEXT("control: with no stack the swing gap is a second"), Plain, 1.0f, 0.0001f)
		|| !TestEqual(TEXT("control: and no stack is held"), Wearer.System->OwnStacksHeld(Key), 0))
	{
		return false;
	}

	Wearer.System->NoteSpellCast();
	TestEqual(TEXT("one spell gives one stack of Dervish"), Wearer.System->OwnStacksHeld(Key), 1);
	TestEqual(TEXT("and the swing gap is a second over 1.1"), SwingGap(Wearer), 1.0f / 1.1f, 0.0001f);

	for (int32 More = 0; More < 4; ++More)
	{
		Wearer.System->NoteSpellCast();
	}
	TestEqual(TEXT("five spells give five stacks"), Wearer.System->OwnStacksHeld(Key), MostStacks);
	TestEqual(TEXT("and the swing gap is a second over 1.5"), SwingGap(Wearer), 1.0f / 1.5f, 0.0001f);

	Wearer.System->NoteSpellCast();
	TestEqual(TEXT("a sixth spell leaves five stacks"), Wearer.System->OwnStacksHeld(Key), MostStacks);
	TestEqual(TEXT("and the swing gap is still a second over 1.5"), SwingGap(Wearer), 1.0f / 1.5f, 0.0001f);

	const FGameplayTagContainer Melee = UCataclysmSkillShapes::TagsFromCell(MeleeStrikeTagCell);
	TestEqual(TEXT("asked for a melee strike's tags, attack speed is 1.5 too: the row has no scope"),
			  Wearer.System->StatForSkill(FName(TEXT("attack_speed")), Melee, 1.0f), 1.5f, 0.0001f);

	World->TimeSeconds += StackSeconds - 0.1f;
	TestEqual(TEXT("4.9 s after the last spell, five stacks"), Wearer.System->OwnStacksHeld(Key), MostStacks);
	World->TimeSeconds += 0.2f;
	TestEqual(TEXT("5.1 s after it, none"), Wearer.System->OwnStacksHeld(Key), 0);
	TestEqual(TEXT("and the swing gap is a second again"), SwingGap(Wearer), Plain, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellbladeSixDervishNotByMeleeTest,
	"Cataclysm.SetBonus.Spellblade6.AMeleeAttackGivesNoStackOfDervish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * In order. Control: a spell gives the wearer a stack, which is let run out. A melee strike's use, which raises
 * `skill_use` and `attack_use`, gives none; nor does a melee basic attack, which raises `attack_use`. The control for
 * each is a stack made by hand on `attack_use`, which each of the two uses grants.
 */
bool FCataclysmSpellbladeSixDervishNotByMeleeTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusSpellbladeSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FWearer Wearer(World);
	const FName Key = WearDervish(*this, Wearer);
	if (Key.IsNone())
	{
		return false;
	}

	// A STACK MADE BY HAND ON `attack_use`, up to ten for a minute, so each use below is shown to reach the rows.
	TArray<FCataclysmPoolAction> Actions = Wearer.System->GetPoolActions();
	FCataclysmPoolAction OnAttackUse;
	OnAttackUse.Event = FName(TEXT("attack_use"));
	OnAttackUse.StackKey = FName(TEXT("Test:on_attack_use"));
	OnAttackUse.StackSeconds = 60.0f;
	OnAttackUse.StackCap = 10;
	Actions.Add(OnAttackUse);
	Wearer.System->SetPoolActions(MoveTemp(Actions));

	Wearer.System->NoteSpellCast();
	if (!TestEqual(TEXT("control: a spell gives this wearer a stack of Dervish"), Wearer.System->OwnStacksHeld(Key), 1))
	{
		return false;
	}
	World->TimeSeconds += StackSeconds + 0.1f;
	if (!TestEqual(TEXT("set-up: that stack has run out"), Wearer.System->OwnStacksHeld(Key), 0))
	{
		return false;
	}

	UseAMeleeStrike(Wearer, /*bBasicAttack=*/false);
	TestEqual(TEXT("control: the melee strike's use reached the rows"),
			  Wearer.System->OwnStacksHeld(OnAttackUse.StackKey), 1);
	TestEqual(TEXT("a melee strike's use gives no stack of Dervish"), Wearer.System->OwnStacksHeld(Key), 0);

	UseAMeleeStrike(Wearer, /*bBasicAttack=*/true);
	TestEqual(TEXT("control: the melee basic attack reached the rows"),
			  Wearer.System->OwnStacksHeld(OnAttackUse.StackKey), 2);
	TestEqual(TEXT("a melee basic attack gives no stack of Dervish"), Wearer.System->OwnStacksHeld(Key), 0);
	TestEqual(TEXT("and the swing gap is a second"), SwingGap(Wearer), 1.0f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellbladeSixSpellslingerGrantedTest,
	"Cataclysm.SetBonus.Spellblade6.AMeleeAttackTheBasicAttackIncludedGivesSpellslingerAndEachStackCutsASpellsCostByATenth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * In order. Control: with no stack a spell and a melee skill each cost what they state, read through
 * `UCataclysmGameplayAbility::ManaCostFor`. A melee strike's use: one stack, and the spell costs 0.9 of that. A melee
 * BASIC attack: two stacks, 0.8. Three more melee uses: five stacks, 0.5. A sixth: still five, still 0.5. The melee
 * skill's own cost is unchanged throughout: the row is scoped to spells. 4.9 s after the last melee use, five stacks;
 * 5.1 s after, none, and the spell costs what it states again.
 */
bool FCataclysmSpellbladeSixSpellslingerGrantedTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusSpellbladeSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FWearer Wearer(World);
	const FName Key = WearSpellslinger(*this, Wearer);
	UCataclysmSelfBuffSkill* Spell = GrantCosting(Wearer, ECataclysmAbilitySlot::Special, SpellTagCell);
	UCataclysmSelfBuffSkill* Strike = GrantCosting(Wearer, ECataclysmAbilitySlot::Heavy, MeleeStrikeTagCell);
	if (Key.IsNone() || !TestNotNull(TEXT("set-up: a spell"), Spell)
		|| !TestNotNull(TEXT("set-up: a melee skill"), Strike))
	{
		return false;
	}

	const float SpellCost = Spell->ManaCostFor(Wearer.System);
	const float StrikeCost = Strike->ManaCostFor(Wearer.System);
	if (!TestTrue(TEXT("control: with no stack the spell costs something"), SpellCost > 0.0f)
		|| !TestTrue(TEXT("control: and so does the melee skill"), StrikeCost > 0.0f)
		|| !TestEqual(TEXT("control: and no stack is held"), Wearer.System->OwnStacksHeld(Key), 0))
	{
		return false;
	}

	UseAMeleeStrike(Wearer, /*bBasicAttack=*/false);
	TestEqual(TEXT("a melee strike's use gives one stack of Spellslinger"), Wearer.System->OwnStacksHeld(Key), 1);
	TestEqual(TEXT("and the spell costs a tenth less"), Spell->ManaCostFor(Wearer.System) / SpellCost, 0.9f, 0.0001f);

	UseAMeleeStrike(Wearer, /*bBasicAttack=*/true);
	TestEqual(TEXT("a melee basic attack gives a second stack"), Wearer.System->OwnStacksHeld(Key), 2);
	TestEqual(TEXT("and the spell costs two tenths less"), Spell->ManaCostFor(Wearer.System) / SpellCost, 0.8f,
			  0.0001f);

	for (int32 More = 0; More < 3; ++More)
	{
		UseAMeleeStrike(Wearer, /*bBasicAttack=*/false);
	}
	TestEqual(TEXT("five melee uses give five stacks"), Wearer.System->OwnStacksHeld(Key), MostStacks);
	TestEqual(TEXT("and the spell costs half"), Spell->ManaCostFor(Wearer.System) / SpellCost, 0.5f, 0.0001f);

	UseAMeleeStrike(Wearer, /*bBasicAttack=*/false);
	TestEqual(TEXT("a sixth leaves five stacks"), Wearer.System->OwnStacksHeld(Key), MostStacks);
	TestEqual(TEXT("and the spell still costs half"), Spell->ManaCostFor(Wearer.System) / SpellCost, 0.5f, 0.0001f);
	TestEqual(TEXT("the melee skill's own cost is unchanged: the row is scoped to spells"),
			  Strike->ManaCostFor(Wearer.System) / StrikeCost, 1.0f, 0.0001f);

	World->TimeSeconds += StackSeconds - 0.1f;
	TestEqual(TEXT("4.9 s after the last melee use, five stacks"), Wearer.System->OwnStacksHeld(Key), MostStacks);
	World->TimeSeconds += 0.2f;
	TestEqual(TEXT("5.1 s after it, none"), Wearer.System->OwnStacksHeld(Key), 0);
	TestEqual(TEXT("and the spell costs what it states again"), Spell->ManaCostFor(Wearer.System) / SpellCost, 1.0f,
			  0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpellbladeSixSpellslingerNotBySpellTest,
	"Cataclysm.SetBonus.Spellblade6.ASpellOrARangedBasicAttackGivesNoStackOfSpellslinger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * In order. Control: a melee strike's use gives the wearer a stack, which is let run out. A spell's use, which raises
 * `skill_use`, `attack_use` and `spell`, gives none; nor does a ranged basic attack, which raises `attack_use`. The
 * control for each is a stack made by hand on `attack_use`, which each of the two uses grants.
 */
bool FCataclysmSpellbladeSixSpellslingerNotBySpellTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusSpellbladeSixTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FWearer Wearer(World);
	const FName Key = WearSpellslinger(*this, Wearer);
	if (Key.IsNone())
	{
		return false;
	}

	TArray<FCataclysmPoolAction> Actions = Wearer.System->GetPoolActions();
	FCataclysmPoolAction OnAttackUse;
	OnAttackUse.Event = FName(TEXT("attack_use"));
	OnAttackUse.StackKey = FName(TEXT("Test:on_attack_use"));
	OnAttackUse.StackSeconds = 60.0f;
	OnAttackUse.StackCap = 10;
	Actions.Add(OnAttackUse);
	Wearer.System->SetPoolActions(MoveTemp(Actions));

	UseAMeleeStrike(Wearer, /*bBasicAttack=*/false);
	if (!TestEqual(TEXT("control: a melee strike's use gives this wearer a stack of Spellslinger"),
			Wearer.System->OwnStacksHeld(Key), 1))
	{
		return false;
	}
	World->TimeSeconds += StackSeconds + 0.1f;
	if (!TestEqual(TEXT("set-up: that stack has run out"), Wearer.System->OwnStacksHeld(Key), 0))
	{
		return false;
	}
	const int32 Before = Wearer.System->OwnStacksHeld(OnAttackUse.StackKey);

	UseASpell(Wearer);
	TestEqual(TEXT("control: the spell's use reached the rows"),
			  Wearer.System->OwnStacksHeld(OnAttackUse.StackKey), Before + 1);
	TestEqual(TEXT("a spell's use gives no stack of Spellslinger"), Wearer.System->OwnStacksHeld(Key), 0);

	UseARangedBasicAttack(Wearer);
	TestEqual(TEXT("control: the ranged basic attack reached the rows"),
			  Wearer.System->OwnStacksHeld(OnAttackUse.StackKey), Before + 2);
	TestEqual(TEXT("a ranged basic attack gives no stack of Spellslinger"), Wearer.System->OwnStacksHeld(Key), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
