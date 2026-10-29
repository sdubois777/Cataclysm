// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * Any skill in the pool on any of the six keys, a saved choice, and the slot a skill was MADE for deciding every rule
 * but its key. Issue #837, layer 3, ruled 2026-10-25.
 *
 * THE RULINGS. The owner: Q5 "the slot shows its default and the choice is kept for when the weapon returns. The
 * screen offers "clear the slot"."; O1 "A skill is already tagged as heavy/basic/support/etc. we don't need to do
 * anything. Where the user assigns it is irrelevant"; O5 "Forget my choice"; and on the cooldown, "Each skill its own
 * cooldown". The coordinating session: O2 only the six and Basic refused; O4 an empty designed slot takes the next
 * unheld pool entry or stays empty, a row chosen for another key refused; Q6 one save field, no version bump; Q7 a
 * moved skill's fallback numbers from its designed slot; the cooldown and the charges per skill, their LENGTH from the
 * designed slot.
 *
 * WHAT THESE GUARD. `UCataclysmWeaponSkills::FillWithChoices` and `DesignedSlotOf`; the player state's
 * `AssignSkill`, `ClearSkillChoice` and `RefusalForSkillChoice`; the pawn handing the choices to the fill; the save
 * field; the two console commands; and the rules the designed slot decides: the numbers, the Fervour, the cooldown's
 * length on the key's own tag, Mana Weaver, the slot-scoped rows, the skill-use notice, the gadget conversion and the
 * rows that name a slot's cooldown.
 *
 * THE REAL TABLE, `DT_WeaponSkills`, and a possessed player wearing the Greataxe it begins with, which carries
 * Demonic: its pool is the Demonic Greataxe's six. The cooldown tests use a bare caster with a plain self buff, as
 * `Cataclysm.UltimateFervour.` does, because a designed skill's press waits on clips and targets.
 */
namespace CataclysmSkillAssignmentTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	const FName PyroclasmRow(TEXT("Demonic_Greataxe_Ultimate"));
	const FName MoltenCleaveRow(TEXT("Demonic_Greataxe_Heavy"));
	const FName WholeWeightRow(TEXT("Demonic_Greatsword_Ultimate"));

	/** The parts of one possessed player a test reads. */
	struct FSkillAssignmentCarrier
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		bool IsComplete() const { return Character && State && Equipment && Slots && AbilitySystem; }
	};

	/** A possessed player wearing the Greataxe it begins with, carrying Demonic. One set-up check. */
	FSkillAssignmentCarrier SpawnAssignmentCarrier(FAutomationTestBase& Test, UWorld* World,
												   const FVector& Where = FVector::ZeroVector)
	{
		FSkillAssignmentCarrier Made;
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		Made.Character = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (PlayerState && Controller && Made.Character)
		{
			Controller->SetPlayerState(PlayerState);
			Controller->Possess(Made.Character);
		}
		Made.State = Made.Character ? Made.Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
		Made.Equipment = Made.Character ? Made.Character->GetEquipment() : nullptr;
		Made.Slots = Made.Character ? Made.Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
		Made.AbilitySystem = Made.State ? Made.State->GetCataclysmAbilitySystemComponent() : nullptr;
		if (!Test.TestTrue(TEXT("set-up: a possessed player with its state, equipment, weapon slots and ability system"),
						   Made.IsComplete()))
		{
			return FSkillAssignmentCarrier();
		}
		return Made;
	}

	/** A weapon of this base carrying these types and nothing else. */
	FCataclysmItem AssignmentWeapon(const TCHAR* Base, const TArray<FName>& Types)
	{
		FCataclysmItem Made;
		Made.Base = FName(Base);
		Made.GearLevel = 0;
		Made.DamageTypes = Types;
		return Made;
	}

	/** Put a weapon on in Weapon1 through the component's own equip, which announces and so refills. One check. */
	bool WearInTheRightHand(FAutomationTestBase& Test, const FSkillAssignmentCarrier& Carrier, const FCataclysmItem& Item)
	{
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		const ECataclysmEquipResult Result =
			Carrier.Equipment->EquipInto(Item, ECataclysmGearSlot::Weapon1, Removed, AlsoRemoved);
		return Test.TestTrue(*FString::Printf(TEXT("set-up: %s goes on"), *Item.Base.ToString()),
							 Result == ECataclysmEquipResult::Equipped || Result == ECataclysmEquipResult::Swapped);
	}

	/** The held skill on a key, or null. */
	const FCataclysmWeaponSkill* HeldOnKey(const UCataclysmWeaponSlotsComponent* Slots, ECataclysmAbilitySlot Key)
	{
		return Slots->GetAvailableSkills().FindByPredicate(
			[Key](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == Key; });
	}

	/** The row held on a key, or None. */
	FName RowOnKey(const UCataclysmWeaponSlotsComponent* Slots, ECataclysmAbilitySlot Key)
	{
		const FCataclysmWeaponSkill* Held = HeldOnKey(Slots, Key);
		return Held ? Held->RowName : NAME_None;
	}

	/** The granted ability a press of the key reaches: the spec carrying the key's tag, as input finds it. */
	UCataclysmGameplayAbility* AbilityOnKey(UCataclysmAbilitySystemComponent* System, ECataclysmAbilitySlot Key)
	{
		const FGameplayTag KeyTag = CataclysmAbilitySlots::Tag(Key);
		for (const FGameplayAbilitySpec& Spec : System->GetActivatableAbilities())
		{
			if (Spec.GetDynamicSpecSourceTags().HasTagExact(KeyTag))
			{
				return Cast<UCataclysmGameplayAbility>(Spec.GetPrimaryInstance());
			}
		}
		return nullptr;
	}

	/** Every held skill as "Key:Name", in order, so two fills compare with one TestEqual of FString. */
	FString KeysAndNames(const TArray<FCataclysmWeaponSkill>& Skills)
	{
		return FString::JoinBy(Skills, TEXT(", "), [](const FCataclysmWeaponSkill& Skill)
		{
			return FString::Printf(TEXT("%s:%s"), *ACataclysmPlayerState::SkillSlotName(Skill.Slot), *Skill.Name);
		});
	}

	/** A choice of `Row` for `Key`. */
	FCataclysmSkillChoice ChoiceOf(ECataclysmAbilitySlot Key, FName Row)
	{
		FCataclysmSkillChoice Made;
		Made.Slot = Key;
		Made.SkillRow = Row;
		return Made;
	}

	/** The longest cooldown running under this key's tag, in seconds of its whole length, or 0 for none. */
	float CooldownLengthOn(const UAbilitySystemComponent* System, ECataclysmAbilitySlot Key)
	{
		const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Key);
		if (!Tag.IsValid())
		{
			return 0.0f;
		}
		float Longest = 0.0f;
		for (const TPair<float, float>& Wait : System->GetActiveEffectsTimeRemainingAndDuration(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
		{
			Longest = FMath::Max(Longest, Wait.Value);
		}
		return Longest;
	}

	/** A bare caster with mana, Fervour and a cooldown reduction attribute of nought. */
	struct FSkillAssignmentCaster
	{
		FSkillAssignmentCaster(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 100000.0f);
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);
		}

		~FSkillAssignmentCaster()
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

		/** A plain self buff on the key `Key`, made for the slot `Designed`, as `StampOnto` stamps a weapon's. */
		UCataclysmSelfBuffSkill* Grant(ECataclysmAbilitySlot Key, ECataclysmAbilitySlot Designed)
		{
			const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
				UCataclysmSelfBuffSkill::StaticClass(), Key, /*Level=*/100, Actor);
			FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
			UCataclysmSelfBuffSkill* Skill = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				FCataclysmWeaponSkill Row;
				Row.Slot = Key;
				Row.DesignedSlot = Designed;
				// A NAME OF ITS OWN FOR EACH, so no rule that knows a skill by its name takes two for one.
				Row.Name = FString::Printf(TEXT("Assignment Test %s on %s"),
										   *ACataclysmPlayerState::SkillSlotName(Designed),
										   *ACataclysmPlayerState::SkillSlotName(Key));
				Row.Shape = ECataclysmSkillShape::SelfBuff;
				Row.Params = UCataclysmSkillShapes::ParseParams(TEXT("Duration=6"));
				Row.Tags.AddTag(CataclysmAbilitySlots::Tag(Designed));
				UCataclysmWeaponSkills::StampOnto(*Skill, Row);
			}
			return Skill;
		}

		bool Use(UGameplayAbility* Ability)
		{
			return Ability
				&& AbilitySystem->TryActivateAbility(Ability->GetCurrentAbilitySpecHandle(),
													 /*bAllowRemoteActivation=*/false);
		}

		/** Whether a press now would pass the cooldown check. */
		bool IsReady(const UCataclysmGameplayAbility* Ability) const
		{
			return Ability->CheckCooldown(Ability->GetCurrentAbilitySpecHandle(), Ability->GetCurrentActorInfo(),
										  /*OptionalRelevantTags=*/nullptr);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** Whether a press of this granted ability now would pass the cooldown check. */
	bool KeyIsReady(const UCataclysmGameplayAbility* Ability)
	{
		return Ability->CheckCooldown(Ability->GetCurrentAbilitySpecHandle(), Ability->GetCurrentActorInfo(),
									  /*OptionalRelevantTags=*/nullptr);
	}

	/** How many cooldown effects run under this key's tag, or -1 when the key has no tag. */
	int32 CooldownEffectsOn(const UAbilitySystemComponent* System, ECataclysmAbilitySlot Key)
	{
		const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Key);
		return Tag.IsValid()
			? System->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))).Num()
			: -1;
	}

	/** Centimetres in a metre. */
	constexpr float AssignmentMetre = 100.0f;

	/** The health every creature here starts with. */
	constexpr float AssignmentEnemyHealth = 50000.0f;

	/** A creature of the monsters' side with no armour, evasion, block or resistance, as the trap tests make one. */
	ACataclysmEnemyCharacter* AssignmentEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(100000.0f);
		Made->SetArmour(0.0f);
		Made->SetAttackDamage(0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), AssignmentEnemyHealth);
		return Made;
	}

	/** What a creature has lost since it was made. */
	float AssignmentTakenFrom(const AActor* Who)
	{
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Who);
		return Its ? AssignmentEnemyHealth - Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute())
				   : -1.0f;
	}

	/** REPLACES THE WHOLE STAT LINE with one slot's trap flag: a flat 1 on `skill_slot_is_a_trap` requiring its tag. */
	void HoldTheTrapFlag(UCataclysmAbilitySystemComponent* System, ECataclysmAbilitySlot Slot)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::Enchantment;
		Row.Value = 1.0f;
		Row.RequiredTags.AddTag(CataclysmAbilitySlots::Tag(Slot));
		FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat));
		Line.Base = 0.0f;
		Line.Modifiers.Add(Row);
		System->SetStatInputs(MoveTemp(Stats));
	}

	/** The console output a command writes, line by line. */
	struct FSkillAssignmentOutput : public FOutputDevice
	{
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type, const class FName&) override
		{
			Lines.Add(FString(Text));
		}

		FString All() const { return FString::Join(Lines, TEXT("\n")); }

		TArray<FString> Lines;
	};
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentNoChoiceTest,
	"Cataclysm.SkillAssignment.WithNoChoiceTheKeysAreTheDefaultFillAndEachSkillSitsInTheSlotItWasMadeFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE REGRESSION ANCHOR. With nothing chosen the keys are layer 2's default fill element for element, every held skill
 * and every granted ability sits on the key it was made for, and a choice whose row is not in the pool changes nothing.
 */
bool FCataclysmSkillAssignmentNoChoiceTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}
	const TArray<FCataclysmWeaponSkill>& Pool = Carrier.Slots->GetSkillPool();

	// THE CONTROL: nothing is chosen, and the pool is the Demonic Greataxe's six, so nothing below compares empties.
	TestEqual(TEXT("control: nothing is chosen"), Carrier.State->GetSkillAssignment().Num(), 0);
	TestEqual(TEXT("control: the pool is six"), Pool.Num(), 6);

	const TArray<FCataclysmWeaponSkill> Default = UCataclysmWeaponSkills::DefaultFillFrom(Pool);
	TestEqual(TEXT("with no choice the fill is the default fill, key and skill, in order"),
			  KeysAndNames(UCataclysmWeaponSkills::FillWithChoices(Pool, {})), KeysAndNames(Default));
	TestEqual(TEXT("and a choice whose row is not in the pool changes nothing (Q5)"),
			  KeysAndNames(UCataclysmWeaponSkills::FillWithChoices(
				  Pool, {ChoiceOf(ECataclysmAbilitySlot::Heavy, FName(TEXT("War_Sword_Heavy")))})),
			  KeysAndNames(Default));

	int32 Moved = 0;
	for (const FCataclysmWeaponSkill& Held : Carrier.Slots->GetAvailableSkills())
	{
		Moved += UCataclysmWeaponSkills::DesignedSlotOf(Held) == Held.Slot ? 0 : 1;
	}
	TestEqual(TEXT("every held skill sits on the key it was made for"), Moved, 0);

	int32 AbilitiesMoved = 0;
	int32 AbilitiesFound = 0;
	for (const ECataclysmAbilitySlot Key : CataclysmAbilitySlots::All())
	{
		if (const UCataclysmGameplayAbility* Ability = AbilityOnKey(Carrier.AbilitySystem, Key))
		{
			++AbilitiesFound;
			AbilitiesMoved += Ability->GetDesignedSlot() == Ability->Slot ? 0 : 1;
		}
	}
	TestEqual(TEXT("seven abilities are granted, the basic attack and six"), AbilitiesFound, 7);
	TestEqual(TEXT("and each one's designed slot is its key"), AbilitiesMoved, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentAnyKeyTest,
	"Cataclysm.SkillAssignment.AnyPoolSkillGoesOnAnyKeyAndTheKeyItLeftTakesTheNextOrStaysEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Pyroclasm, made for the Ultimate slot, chosen for the Heavy key: the Heavy key casts it, the Ultimate key it left has
 * no other Ultimate in the pool and stays empty, and Molten Cleave, made for the Heavy slot, is held nowhere (O4). On
 * the pure function with a Greataxe carrying Demonic and War, the Ultimate key takes War's Ultimate instead. Then the
 * refusals, each with its sentence, none of which records anything.
 */
bool FCataclysmSkillAssignmentAnyKeyTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	// THE CONTROL: the default keys.
	TestEqual(TEXT("control: the Heavy key holds Molten Cleave"), RowOnKey(Carrier.Slots, ECataclysmAbilitySlot::Heavy),
			  MoltenCleaveRow);
	TestEqual(TEXT("control: the Ultimate key holds Pyroclasm"),
			  RowOnKey(Carrier.Slots, ECataclysmAbilitySlot::Ultimate), PyroclasmRow);

	FString Reason;
	TestTrue(TEXT("Pyroclasm is chosen for the Heavy key"),
			 Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, PyroclasmRow, Reason));
	TestEqual(TEXT("with no refusal"), Reason, FString());
	TestEqual(TEXT("the Heavy key holds Pyroclasm"), RowOnKey(Carrier.Slots, ECataclysmAbilitySlot::Heavy),
			  PyroclasmRow);
	const UCataclysmGameplayAbility* OnHeavy = AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("and a press of the Heavy key reaches Pyroclasm"),
			 OnHeavy && OnHeavy->DisplayedName() == TEXT("Pyroclasm"));
	TestTrue(TEXT("the Ultimate key it left holds nothing: the pool has no other Ultimate (O4)"),
			 HeldOnKey(Carrier.Slots, ECataclysmAbilitySlot::Ultimate) == nullptr
				 && AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate) == nullptr);
	TestFalse(TEXT("and Molten Cleave, made for the Heavy slot, is held nowhere"),
			  Carrier.Slots->GetAvailableSkills().ContainsByPredicate(
				  [](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == MoltenCleaveRow; }));

	// O4 WITH A SECOND TYPE, on the pure function: the next unheld Ultimate of the pool fills the key left.
	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	const TArray<FCataclysmWeaponSkill> TwoTypes =
		UCataclysmWeaponSkills::PoolFor(Table, {TEXT("Greataxe")}, {TEXT("Demonic"), TEXT("War")});
	const TArray<FCataclysmWeaponSkill> Filled = UCataclysmWeaponSkills::FillWithChoices(
		TwoTypes, {ChoiceOf(ECataclysmAbilitySlot::Heavy, PyroclasmRow)});
	const FCataclysmWeaponSkill* UltimateKey = Filled.FindByPredicate(
		[](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == ECataclysmAbilitySlot::Ultimate; });
	TestTrue(TEXT("with War carried too, the Ultimate key takes War's Greataxe Ultimate"),
			 UltimateKey && UltimateKey->RowName == FName(TEXT("War_Greataxe_Ultimate")));

	// THE REFUSALS, each its own sentence, and none records anything.
	Carrier.State->AssignSkill(ECataclysmAbilitySlot::BasicAttack, MoltenCleaveRow, Reason);
	TestEqual(TEXT("the basic attack's key is refused (O2)"), Reason,
			  FString(TEXT("The basic attack comes from the weapon and cannot be chosen.")));
	Carrier.State->AssignSkill(ECataclysmAbilitySlot::Special, FName(TEXT("War_Greataxe_Heavy")), Reason);
	// BY THE SKILLS' NAMES AND NOT THEIR ROWS, ruled Q5 of layer 4 (2026-10-25); this and the last sentence below
	// named rows until then.
	TestTrue(TEXT("a row not in the pool is refused, naming the pool"),
			 Reason.StartsWith(TEXT("Sundering Arc is not in your skill pool. Your weapons offer: "))
				 && Reason.Contains(TEXT("Molten Cleave")));
	Carrier.State->AssignSkill(ECataclysmAbilitySlot::Special, FName(TEXT("No_Such_Row")), Reason);
	TestEqual(TEXT("a row the table does not have is refused"), Reason,
			  FString(TEXT("There is no skill row called No_Such_Row.")));
	Carrier.State->AssignSkill(ECataclysmAbilitySlot::Special, PyroclasmRow, Reason);
	TestEqual(TEXT("a row chosen for another key is refused, naming the key (O4)"), Reason,
			  FString(TEXT("Pyroclasm is already chosen for the Heavy slot. Clear that slot first.")));
	TestEqual(TEXT("and the refusals recorded nothing"), Carrier.State->GetSkillAssignment().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentNumbersTest,
	"Cataclysm.SkillAssignment.AMovedSkillKeepsTheNumbersOfTheSlotItWasMadeFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q7: Pyroclasm on the Heavy key reads the Ultimate slot's 400% damage, 20 second cooldown, 150 mana at level 100 and
 * 50 Fervour, and not the Heavy slot's. Molten Cleave on its own key is the control, read first. The Whole Weight, which
 * states its own 350%, keeps it on the Heavy key. Every figure is the slot table's, read here.
 */
bool FCataclysmSkillAssignmentNumbersTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	const UDataTable* SlotTable = UCataclysmSkillSlots::LoadGeneratedTable();
	const FCataclysmSkillSlotNumbers Heavy = UCataclysmSkillSlots::NumbersFor(SlotTable, ECataclysmAbilitySlot::Heavy);
	const FCataclysmSkillSlotNumbers Ultimate =
		UCataclysmSkillSlots::NumbersFor(SlotTable, ECataclysmAbilitySlot::Ultimate);
	if (!TestTrue(TEXT("set-up: the slot table has the Heavy and Ultimate rows, and they differ"),
				  Heavy.bFound && Ultimate.bFound && Heavy.DamagePercent != Ultimate.DamagePercent
					  && Heavy.Cooldown != Ultimate.Cooldown && Heavy.ManaCostAtLevel100 != Ultimate.ManaCostAtLevel100))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	// THE CONTROL: Molten Cleave, on the key it was made for, reads the Heavy slot's numbers.
	const UCataclysmSkillTemplate* Cleave =
		Cast<UCataclysmSkillTemplate>(AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Heavy));
	if (!TestNotNull(TEXT("set-up: Molten Cleave is on the Heavy key"), Cleave))
	{
		return false;
	}
	TestEqual(TEXT("control: Molten Cleave deals the Heavy slot's damage"), Cleave->GetDamagePercent(),
			  Heavy.DamagePercent, 0.001f);
	TestEqual(TEXT("control: and waits the Heavy slot's cooldown"), Cleave->GetBaseCooldown(), Heavy.Cooldown, 0.001f);

	FString Reason;
	if (!TestTrue(TEXT("set-up: Pyroclasm is chosen for the Heavy key"),
				  Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, PyroclasmRow, Reason)))
	{
		return false;
	}
	const UCataclysmSkillTemplate* Moved =
		Cast<UCataclysmSkillTemplate>(AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Heavy));
	if (!TestNotNull(TEXT("set-up: a skill template is on the Heavy key"), Moved))
	{
		return false;
	}
	TestTrue(TEXT("its key is Heavy and the slot it was made for is Ultimate"),
			 Moved->Slot == ECataclysmAbilitySlot::Heavy && Moved->GetDesignedSlot() == ECataclysmAbilitySlot::Ultimate);
	TestEqual(TEXT("Pyroclasm on the Heavy key deals the Ultimate slot's damage"), Moved->GetDamagePercent(),
			  Ultimate.DamagePercent, 0.001f);
	TestEqual(TEXT("waits the Ultimate slot's cooldown"), Moved->GetBaseCooldown(), Ultimate.Cooldown, 0.001f);
	TestEqual(TEXT("costs the Ultimate slot's mana at its level"), Moved->GetManaCost(),
			  UCataclysmSkillSlots::ManaCostAtLevel(Ultimate.ManaCostAtLevel100, Moved->GetAbilityLevel()), 0.001f);
	TestEqual(TEXT("and the Ultimate slot's Fervour"), Moved->FervourCostFor(Carrier.AbilitySystem),
			  Ultimate.FervourCost, 0.001f);
	const FCataclysmWeaponSkill* HeldEntry = HeldOnKey(Carrier.Slots, ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("the shared helper answers the designed slot for the moved entry"),
			 HeldEntry && UCataclysmWeaponSkills::DesignedSlotOf(*HeldEntry) == ECataclysmAbilitySlot::Ultimate);
	FCataclysmWeaponSkill ByHand;
	ByHand.Slot = ECataclysmAbilitySlot::Special;
	TestTrue(TEXT("and the key for an entry with no designed slot"),
			 UCataclysmWeaponSkills::DesignedSlotOf(ByHand) == ECataclysmAbilitySlot::Special);

	// A ROW'S OWN FIGURE STILL WINS: The Whole Weight states 350% and keeps it on the Heavy key.
	Carrier.Equipment->UnequipEverything();
	if (!WearInTheRightHand(*this, Carrier, AssignmentWeapon(TEXT("Weapon_Greatsword"), {FName(TEXT("Demonic"))}))
		|| !TestTrue(TEXT("set-up: The Whole Weight is chosen for the Heavy key"),
					 Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, WholeWeightRow, Reason)))
	{
		return false;
	}
	const UCataclysmSkillTemplate* Weight =
		Cast<UCataclysmSkillTemplate>(AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Heavy));
	TestTrue(TEXT("The Whole Weight on the Heavy key keeps its stated 350%"),
			 Weight && FMath::IsNearlyEqual(Weight->GetDamagePercent(), 350.0f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentKeptTest,
	"Cataclysm.SkillAssignment.AChoiceIsKeptWhileItsWeaponIsOffAndReturnsWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q5: Pyroclasm chosen for the Heavy key; a War Sword replaces the Greataxe, so the choice is not in the pool, the Heavy
 * key shows the Sword's own heavy attack and the choice is kept; the Greataxe back, the Heavy key holds Pyroclasm again.
 */
bool FCataclysmSkillAssignmentKeptTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	FString Reason;
	if (!Carrier.IsComplete()
		|| !TestTrue(TEXT("set-up: Pyroclasm is chosen for the Heavy key"),
					 Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, PyroclasmRow, Reason)))
	{
		return false;
	}

	// THE CONTROL: a choice in the pool holds through a refill.
	Carrier.Character->RefillAbilitySlots();
	TestEqual(TEXT("control: with its weapon worn the choice holds through a refill"),
			  RowOnKey(Carrier.Slots, ECataclysmAbilitySlot::Heavy), PyroclasmRow);

	if (!WearInTheRightHand(*this, Carrier, AssignmentWeapon(TEXT("Weapon_Sword"), {FName(TEXT("War"))})))
	{
		return false;
	}
	TestEqual(TEXT("with the Greataxe off, the Heavy key shows its default, the Sword's heavy attack"),
			  RowOnKey(Carrier.Slots, ECataclysmAbilitySlot::Heavy), FName(TEXT("War_Sword_Heavy")));
	TestTrue(TEXT("and the choice is kept"), Carrier.State->GetSkillAssignment().Num() == 1
				 && Carrier.State->GetSkillAssignment()[0].SkillRow == PyroclasmRow);

	if (!WearInTheRightHand(*this, Carrier, AssignmentWeapon(TEXT("Weapon_Greataxe"), {FName(TEXT("Demonic"))})))
	{
		return false;
	}
	TestEqual(TEXT("the Greataxe back, the Heavy key holds Pyroclasm again"),
			  RowOnKey(Carrier.Slots, ECataclysmAbilitySlot::Heavy), PyroclasmRow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentClearTest,
	"Cataclysm.SkillAssignment.ClearingASlotForgetsTheChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * O5, "Forget my choice": clearing the Heavy key's choice puts Molten Cleave and Pyroclasm back on their own keys and
 * records nothing; cleared while the weapon was off, the choice does not come back with it.
 */
bool FCataclysmSkillAssignmentClearTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	FString Reason;
	if (!Carrier.IsComplete()
		|| !TestTrue(TEXT("set-up: Pyroclasm is chosen for the Heavy key"),
					 Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, PyroclasmRow, Reason)))
	{
		return false;
	}
	TestEqual(TEXT("control: before the clear the Heavy key holds Pyroclasm"),
			  RowOnKey(Carrier.Slots, ECataclysmAbilitySlot::Heavy), PyroclasmRow);

	TestTrue(TEXT("clearing the Heavy key forgets a choice"),
			 Carrier.State->ClearSkillChoice(ECataclysmAbilitySlot::Heavy));
	TestEqual(TEXT("nothing is chosen now"), Carrier.State->GetSkillAssignment().Num(), 0);
	TestEqual(TEXT("the Heavy key is back to Molten Cleave"), RowOnKey(Carrier.Slots, ECataclysmAbilitySlot::Heavy),
			  MoltenCleaveRow);
	TestEqual(TEXT("and the Ultimate key to Pyroclasm"), RowOnKey(Carrier.Slots, ECataclysmAbilitySlot::Ultimate),
			  PyroclasmRow);
	TestFalse(TEXT("a second clear has nothing to forget"), Carrier.State->ClearSkillChoice(ECataclysmAbilitySlot::Heavy));

	// CLEARED WHILE THE WEAPON IS OFF, IT DOES NOT RETURN WITH IT.
	if (!TestTrue(TEXT("set-up: Pyroclasm is chosen again"),
				  Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, PyroclasmRow, Reason))
		|| !WearInTheRightHand(*this, Carrier, AssignmentWeapon(TEXT("Weapon_Sword"), {FName(TEXT("War"))})))
	{
		return false;
	}
	Carrier.State->ClearSkillChoice(ECataclysmAbilitySlot::Heavy);
	if (!WearInTheRightHand(*this, Carrier, AssignmentWeapon(TEXT("Weapon_Greataxe"), {FName(TEXT("Demonic"))})))
	{
		return false;
	}
	TestEqual(TEXT("cleared while the Greataxe was off, the Heavy key holds Molten Cleave when it returns"),
			  RowOnKey(Carrier.Slots, ECataclysmAbilitySlot::Heavy), MoltenCleaveRow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentConsoleTest,
	"Cataclysm.SkillAssignment.TheConsoleCommandsAssignClearAndRefuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Cataclysm.AssignSkill` and `Cataclysm.ShowSkills`, run as a person types them into an output device that keeps the
 * lines: an assignment, `ShowSkills` naming it and then a kept choice, every refusal's sentence, and `default` clearing.
 */
bool FCataclysmSkillAssignmentConsoleTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	IConsoleObject* AssignObject = IConsoleManager::Get().FindConsoleObject(TEXT("Cataclysm.AssignSkill"));
	IConsoleObject* ShowObject = IConsoleManager::Get().FindConsoleObject(TEXT("Cataclysm.ShowSkills"));
	IConsoleCommand* Assign = AssignObject ? AssignObject->AsCommand() : nullptr;
	IConsoleCommand* Show = ShowObject ? ShowObject->AsCommand() : nullptr;
	if (!TestTrue(TEXT("set-up: both commands are registered"), Assign && Show))
	{
		return false;
	}
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	const auto Run = [World](IConsoleCommand* Command, const TArray<FString>& Args)
	{
		FSkillAssignmentOutput Out;
		Command->Execute(Args, World, Out);
		return Out.All();
	};

	// THE CONTROL: nothing saved.
	TestTrue(TEXT("control: ShowSkills lists no saved choice"),
			 Run(Show, {}).Contains(TEXT("--- saved choices ---\n  none")));

	TestTrue(TEXT("AssignSkill puts Pyroclasm on the Heavy key"),
			 Run(Assign, {TEXT("Heavy"), PyroclasmRow.ToString()})
				 .Contains(TEXT("Demonic_Greataxe_Ultimate is on the Heavy key.")));
	TestEqual(TEXT("and the player state records it"), Carrier.State->GetSkillAssignment().Num(), 1);
	TestTrue(TEXT("ShowSkills names it on the Heavy key, made for Ultimate, chosen"),
			 Run(Show, {}).Contains(TEXT("Pyroclasm (Demonic_Greataxe_Ultimate, made for Ultimate) chosen")));

	TestTrue(TEXT("one word is refused with the usage"),
			 Run(Assign, {TEXT("Heavy")}).Contains(TEXT("Cataclysm.AssignSkill <slot> <row | default>.")));
	TestTrue(TEXT("a slot that is not one is refused"),
			 Run(Assign, {TEXT("Sideways"), PyroclasmRow.ToString()})
				 .Contains(TEXT("There is no skill slot called Sideways.")));
	TestTrue(TEXT("the basic attack's key is refused"),
			 Run(Assign, {TEXT("Basic"), MoltenCleaveRow.ToString()})
				 .Contains(TEXT("The basic attack comes from the weapon and cannot be chosen.")));
	TestTrue(TEXT("a row the table does not have is refused"),
			 Run(Assign, {TEXT("Special"), TEXT("No_Such_Row")}).Contains(TEXT("There is no skill row called No_Such_Row.")));
	// BY THE SKILL'S NAME, ruled Q5 of layer 4 (2026-10-25); it named the row until then.
	TestTrue(TEXT("a row not in the pool is refused"),
			 Run(Assign, {TEXT("Special"), TEXT("War_Greataxe_Heavy")})
				 .Contains(TEXT("Sundering Arc is not in your skill pool.")));
	TestTrue(TEXT("a row chosen for another key is refused, naming it"),
			 Run(Assign, {TEXT("Special"), PyroclasmRow.ToString()})
				 .Contains(TEXT("Pyroclasm is already chosen for the Heavy slot. Clear that slot first.")));

	// A KEPT CHOICE, NAMED.
	if (!WearInTheRightHand(*this, Carrier, AssignmentWeapon(TEXT("Weapon_Sword"), {FName(TEXT("War"))})))
	{
		return false;
	}
	TestTrue(TEXT("with the Greataxe off, ShowSkills names the kept choice"),
			 Run(Show, {}).Contains(TEXT("default; kept: Demonic_Greataxe_Ultimate is not in your pool")));

	TestTrue(TEXT("'default' clears the slot"),
			 Run(Assign, {TEXT("Heavy"), TEXT("default")})
				 .Contains(TEXT("The Heavy slot is back to its default, and the choice is forgotten.")));
	TestEqual(TEXT("and the choice is forgotten"), Carrier.State->GetSkillAssignment().Num(), 0);
	TestTrue(TEXT("a second 'default' says nothing was chosen"),
			 Run(Assign, {TEXT("Heavy"), TEXT("default")})
				 .Contains(TEXT("Nothing was chosen for the Heavy slot, so it already shows its default.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentSaveTest,
	"Cataclysm.SkillAssignment.TheChoiceIsWrittenToTheCharacterRecordBySlotName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Q6: the character record carries the choices, the key by its NAME in the JSON; with none, an empty list. */
bool FCataclysmSkillAssignmentSaveTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}
	UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();

	// THE CONTROL: nothing chosen writes an empty list.
	TestTrue(TEXT("control: the record is gathered"), FCataclysmSaveGather::CharacterFrom(*Carrier.Character, *Record));
	TestEqual(TEXT("control: with nothing chosen it holds no choice"), Record->SkillAssignment.Num(), 0);

	FString Reason;
	if (!TestTrue(TEXT("set-up: Pyroclasm is chosen for the Heavy key"),
				  Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, PyroclasmRow, Reason)))
	{
		return false;
	}
	TestTrue(TEXT("the record is gathered again"), FCataclysmSaveGather::CharacterFrom(*Carrier.Character, *Record));
	FString Json;
	FString Error;
	if (!TestTrue(TEXT("set-up: the record writes"), FCataclysmSaveStorage::ToJson(Record, Json, Error)))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Written = CataclysmSaveFixtures::Parse(Json);
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!TestTrue(TEXT("the file holds a list of chosen skills"),
				  Written.IsValid() && Written->TryGetArrayField(TEXT("SkillAssignment"), Choices) && Choices
					  && Choices->Num() == 1 && (*Choices)[0]->AsObject().IsValid()))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Choice = (*Choices)[0]->AsObject();
	TestEqual(TEXT("its key is written by name"), Choice->GetStringField(TEXT("Slot")), FString(TEXT("Heavy")));
	TestEqual(TEXT("and its row by name"), Choice->GetStringField(TEXT("SkillRow")), PyroclasmRow.ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentSlotRulesTest,
	"Cataclysm.SkillAssignment.AMovedUltimateIsReachedByUltimateRowsAndConvertedAsAnUltimate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * O1: Pyroclasm on the Heavy key is reached by a row requiring `Slot.Ultimate` ("Your ultimate ability deals ...") and
 * not by one requiring `Slot.Heavy` ("Your heavy attack deals ..."); the gadget conversion asks the Heavy key with
 * Pyroclasm's tags and not the key's; and a row naming the Ultimate's cooldown reaches the Heavy key's tag, where its
 * cooldown is kept, and also `Cooldown.Ultimate`, because the Ultimate key is empty and no skill made for another slot
 * sits there, so the slot keeps its own tag (see `CooldownTagsOfSkillsDesignedFor`'s header); with that key empty the
 * tag acts on nothing. Molten Cleave on its own key is the control, read first.
 */
bool FCataclysmSkillAssignmentSlotRulesTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}
	const FGameplayTag HeavyTag = CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Heavy);
	const FGameplayTag UltimateTag = CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Ultimate);
	FCataclysmStatModifier HeavyRow;
	HeavyRow.Bucket = ECataclysmStatBucket::Increased;
	HeavyRow.Source = ECataclysmModifierSource::Enchantment;
	HeavyRow.Value = 30.0f;
	HeavyRow.RequiredTags.AddTag(HeavyTag);
	FCataclysmStatModifier UltimateRow = HeavyRow;
	UltimateRow.RequiredTags = FGameplayTagContainer(UltimateTag);

	// THE CONTROL: Molten Cleave on its own key.
	const UCataclysmGameplayAbility* Cleave = AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Heavy);
	if (!TestNotNull(TEXT("set-up: Molten Cleave is on the Heavy key"), Cleave))
	{
		return false;
	}
	TestTrue(TEXT("control: a heavy-attack row reaches Molten Cleave"),
			 UCataclysmStatPipeline::ModifierApplies(HeavyRow, Cleave->SkillTagsForStats()));
	TestTrue(TEXT("control: the Heavy key's gadget tags carry Slot.Heavy"),
			 Carrier.AbilitySystem->SkillGadgetSlotTags(ECataclysmAbilitySlot::Heavy).HasTagExact(HeavyTag));
	TestTrue(TEXT("control: an Ultimate-cooldown row names Cooldown.Ultimate"),
			 Carrier.AbilitySystem->CooldownTagsOfSkillsDesignedFor(ECataclysmAbilitySlot::Ultimate)
				 == FGameplayTagContainer(UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Ultimate)));

	FString Reason;
	if (!TestTrue(TEXT("set-up: Pyroclasm is chosen for the Heavy key"),
				  Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, PyroclasmRow, Reason)))
	{
		return false;
	}
	const UCataclysmGameplayAbility* Moved = AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Heavy);
	if (!TestNotNull(TEXT("set-up: Pyroclasm is on the Heavy key"), Moved))
	{
		return false;
	}
	TestTrue(TEXT("an ultimate-ability row reaches Pyroclasm on the Heavy key"),
			 UCataclysmStatPipeline::ModifierApplies(UltimateRow, Moved->SkillTagsForStats()));
	TestFalse(TEXT("and a heavy-attack row does not"),
			  UCataclysmStatPipeline::ModifierApplies(HeavyRow, Moved->SkillTagsForStats()));
	const FGameplayTagContainer Gadget = Carrier.AbilitySystem->SkillGadgetSlotTags(ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("the Heavy key's gadget conversion is asked as an Ultimate"),
			 Gadget.HasTagExact(UltimateTag) && !Gadget.HasTagExact(HeavyTag));
	FGameplayTagContainer HeavyKeyAndEmptyUltimateKey(UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Heavy));
	HeavyKeyAndEmptyUltimateKey.AddTag(UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Ultimate));
	TestTrue(TEXT("a row naming the Ultimate's cooldown reaches the Heavy key's tag, and the empty Ultimate key keeps its own"),
			 Carrier.AbilitySystem->CooldownTagsOfSkillsDesignedFor(ECataclysmAbilitySlot::Ultimate)
				 == HeavyKeyAndEmptyUltimateKey);
	TestEqual(TEXT("and a row naming the Heavy cooldown reaches nothing: no Heavy skill is held"),
			  Carrier.AbilitySystem->CooldownTagsOfSkillsDesignedFor(ECataclysmAbilitySlot::Heavy).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentOwnCooldownTest,
	"Cataclysm.SkillAssignment.TwoSkillsMadeForOneSlotEachKeepTheirOwnCooldownOfThatSlotsLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner, 2026-10-25: "Each skill its own cooldown". Two skills made for the Heavy slot, on the Heavy and Support
 * keys: using one leaves the other ready, and each waits the Heavy slot's cooldown, on its own key's tag. The control,
 * read first: a Heavy and a Special skill on their own keys, which also do not share, each waiting its own slot's.
 */
bool FCataclysmSkillAssignmentOwnCooldownTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	const UDataTable* SlotTable = UCataclysmSkillSlots::LoadGeneratedTable();
	const float HeavySeconds = UCataclysmSkillSlots::NumbersFor(SlotTable, ECataclysmAbilitySlot::Heavy).Cooldown;
	const float SpecialSeconds = UCataclysmSkillSlots::NumbersFor(SlotTable, ECataclysmAbilitySlot::Special).Cooldown;
	const float SupportSeconds = UCataclysmSkillSlots::NumbersFor(SlotTable, ECataclysmAbilitySlot::Support).Cooldown;
	if (!TestTrue(TEXT("set-up: the Heavy, Special and Support cooldowns are above nought and differ"),
				  HeavySeconds > 0.0f && SpecialSeconds > 0.0f && SupportSeconds > 0.0f
					  && HeavySeconds != SpecialSeconds && HeavySeconds != SupportSeconds))
	{
		return false;
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE CONTROL: a Heavy skill and a Special skill, each on its own key.
	{
		FSkillAssignmentCaster Caster(World, FVector::ZeroVector);
		UCataclysmSelfBuffSkill* Heavy = Caster.Grant(ECataclysmAbilitySlot::Heavy, ECataclysmAbilitySlot::Heavy);
		UCataclysmSelfBuffSkill* Special = Caster.Grant(ECataclysmAbilitySlot::Special, ECataclysmAbilitySlot::Special);
		if (!TestTrue(TEXT("set-up: the control's two skills are granted"), Heavy && Special))
		{
			return false;
		}
		TestTrue(TEXT("control: the Heavy skill is used"), Caster.Use(Heavy));
		TestTrue(TEXT("control: the Special skill is still ready"), Caster.IsReady(Special));
		TestTrue(TEXT("control: the Special skill is used"), Caster.Use(Special));
		TestEqual(TEXT("control: the Heavy key waits the Heavy cooldown"),
				  CooldownLengthOn(Caster.AbilitySystem, ECataclysmAbilitySlot::Heavy), HeavySeconds, 0.001f);
		TestEqual(TEXT("control: the Special key waits the Special cooldown"),
				  CooldownLengthOn(Caster.AbilitySystem, ECataclysmAbilitySlot::Special), SpecialSeconds, 0.001f);
	}

	// TWO SKILLS MADE FOR THE HEAVY SLOT, on the Heavy and the Support keys.
	FSkillAssignmentCaster Caster(World, FVector(500.0f, 0.0f, 0.0f));
	UCataclysmSelfBuffSkill* OnHeavy = Caster.Grant(ECataclysmAbilitySlot::Heavy, ECataclysmAbilitySlot::Heavy);
	UCataclysmSelfBuffSkill* OnSupport = Caster.Grant(ECataclysmAbilitySlot::Support, ECataclysmAbilitySlot::Heavy);
	if (!TestTrue(TEXT("set-up: two Heavy skills are granted, on the Heavy and the Support keys"), OnHeavy && OnSupport))
	{
		return false;
	}
	TestTrue(TEXT("the one on the Heavy key is used"), Caster.Use(OnHeavy));
	TestFalse(TEXT("which puts it on cooldown"), Caster.IsReady(OnHeavy));
	TestTrue(TEXT("and leaves the one on the Support key ready: each skill its own cooldown"), Caster.IsReady(OnSupport));
	TestTrue(TEXT("the one on the Support key is used"), Caster.Use(OnSupport));
	TestFalse(TEXT("and is on cooldown in turn"), Caster.IsReady(OnSupport));
	TestEqual(TEXT("the Heavy key waits the Heavy cooldown"),
			  CooldownLengthOn(Caster.AbilitySystem, ECataclysmAbilitySlot::Heavy), HeavySeconds, 0.001f);
	TestEqual(TEXT("and the Support key waits the Heavy cooldown too, the length of the slot it was made for"),
			  CooldownLengthOn(Caster.AbilitySystem, ECataclysmAbilitySlot::Support), HeavySeconds, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentUltimateOnHeavyTest,
	"Cataclysm.SkillAssignment.AnUltimateOnTheHeavyKeyWaitsTheUltimatesCooldownPaysItsFervourAndKeepsManaWeaver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * An Ultimate on the Heavy key, used: it waits the Ultimate's 20 seconds on the Heavy key's tag and takes 50 Fervour
 * (O6). Mana Weaver's ten-piece row takes its cooldown away, and does not take one from a Heavy skill on the Ultimate
 * key (O6, "Mana Weaver follows the designed slot"). The control, read first: a Heavy skill on the Heavy key.
 */
bool FCataclysmSkillAssignmentUltimateOnHeavyTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	const UDataTable* SlotTable = UCataclysmSkillSlots::LoadGeneratedTable();
	const FCataclysmSkillSlotNumbers Heavy = UCataclysmSkillSlots::NumbersFor(SlotTable, ECataclysmAbilitySlot::Heavy);
	const FCataclysmSkillSlotNumbers Ultimate =
		UCataclysmSkillSlots::NumbersFor(SlotTable, ECataclysmAbilitySlot::Ultimate);
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayAttribute Fervour = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	// THE CONTROL: a Heavy skill on the Heavy key waits the Heavy cooldown and takes no Fervour.
	{
		FSkillAssignmentCaster Caster(World, FVector::ZeroVector);
		UCataclysmSelfBuffSkill* Plain = Caster.Grant(ECataclysmAbilitySlot::Heavy, ECataclysmAbilitySlot::Heavy);
		if (!TestNotNull(TEXT("set-up: the control's skill is granted"), Plain))
		{
			return false;
		}
		TestTrue(TEXT("control: it is used"), Caster.Use(Plain));
		TestEqual(TEXT("control: it waits the Heavy cooldown"),
				  CooldownLengthOn(Caster.AbilitySystem, ECataclysmAbilitySlot::Heavy), Heavy.Cooldown, 0.001f);
		TestEqual(TEXT("control: and took no Fervour"), Caster.AbilitySystem->GetNumericAttribute(Fervour), 100.0f,
				  0.001f);
	}

	FSkillAssignmentCaster Caster(World, FVector(500.0f, 0.0f, 0.0f));
	UCataclysmSelfBuffSkill* Moved = Caster.Grant(ECataclysmAbilitySlot::Heavy, ECataclysmAbilitySlot::Ultimate);
	if (!TestNotNull(TEXT("set-up: an Ultimate is granted on the Heavy key"), Moved))
	{
		return false;
	}
	TestTrue(TEXT("the Ultimate on the Heavy key is used"), Caster.Use(Moved));
	TestEqual(TEXT("it waits the Ultimate's cooldown, on the Heavy key's tag"),
			  CooldownLengthOn(Caster.AbilitySystem, ECataclysmAbilitySlot::Heavy), Ultimate.Cooldown, 0.001f);
	TestEqual(TEXT("and nothing waits on the Ultimate key's tag"),
			  CooldownLengthOn(Caster.AbilitySystem, ECataclysmAbilitySlot::Ultimate), 0.0f, 0.001f);
	TestEqual(TEXT("it took the Ultimate's Fervour"), Caster.AbilitySystem->GetNumericAttribute(Fervour),
			  100.0f - Ultimate.FervourCost, 0.001f);

	// MANA WEAVER FOLLOWS THE DESIGNED SLOT.
	UCataclysmSelfBuffSkill* HeavyOnUltimate =
		Caster.Grant(ECataclysmAbilitySlot::Ultimate, ECataclysmAbilitySlot::Heavy);
	if (!TestNotNull(TEXT("set-up: a Heavy skill is granted on the Ultimate key"), HeavyOnUltimate))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Stats;
	FCataclysmStatModifier Row;
	Row.Bucket = ECataclysmStatBucket::Flat;
	Row.Source = ECataclysmModifierSource::Enchantment;
	Row.Value = 1.0f;
	FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(UCataclysmGameplayAbility::UltimateCostDoublesInsteadOfCooldownStat));
	Line.Base = 0.0f;
	Line.Modifiers.Add(Row);
	Caster.AbilitySystem->SetStatInputs(MoveTemp(Stats));
	TestEqual(TEXT("with Mana Weaver's row the Ultimate on the Heavy key has no cooldown"), Moved->GetBaseCooldown(),
			  0.0f, 0.001f);
	TestEqual(TEXT("and the Heavy skill on the Ultimate key keeps the Heavy cooldown"),
			  HeavyOnUltimate->GetBaseCooldown(), Heavy.Cooldown, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentNoticeTest,
	"Cataclysm.SkillAssignment.ASkillUseAnnouncesTheSlotTheSkillWasMadeFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The skill-use notice carries the designed slot, which Edict of Stillness, the basic attack's exclusions, Echo
 * Chamber and Wild Magic read: a skill made for the Movement slot on the Heavy key announces Movement. The control,
 * read first: a skill made for the Heavy slot on its own key announces Heavy.
 */
bool FCataclysmSkillAssignmentNoticeTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("set-up: the combat events exist"), Events))
	{
		return false;
	}
	TArray<ECataclysmAbilitySlot> Heard;
	const FDelegateHandle Listening = Events->OnSkillUsed.AddLambda(
		[&Heard](const FCataclysmSkillUsedNotice& Notice) { Heard.Add(Notice.Slot); });
	ON_SCOPE_EXIT { Events->OnSkillUsed.Remove(Listening); };

	FSkillAssignmentCaster Caster(World, FVector::ZeroVector);
	UCataclysmSelfBuffSkill* OwnKey = Caster.Grant(ECataclysmAbilitySlot::Heavy, ECataclysmAbilitySlot::Heavy);
	UCataclysmSelfBuffSkill* Moved = Caster.Grant(ECataclysmAbilitySlot::Special, ECataclysmAbilitySlot::Movement);
	if (!TestTrue(TEXT("set-up: both skills are granted"), OwnKey && Moved))
	{
		return false;
	}
	TestTrue(TEXT("control: the Heavy skill on its own key is used"), Caster.Use(OwnKey));
	TestTrue(TEXT("control: and announces Heavy"),
			 Heard.Num() == 1 && Heard[0] == ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("the Movement skill on the Special key is used"), Caster.Use(Moved));
	TestTrue(TEXT("and announces Movement, the slot it was made for"),
			 Heard.Num() == 2 && Heard[1] == ECataclysmAbilitySlot::Movement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentAuraKeyTest,
	"Cataclysm.SkillAssignment.MoltenCleaveOnTheAuraKeyWaitsItsOwnCooldownAndAnAuraThereWaitsForNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q-L3a, "Add Cooldown.Aura": Molten Cleave chosen for the Aura key waits its own 1.5 seconds, under `Cooldown.Aura`.
 * The control, read first: Conflagration, the aura on its own Aura key, has no cooldown, and its cooldown being
 * committed applies no effect at all, so it is ready again at once. The cooldown is committed by the ability's own
 * `ApplyCooldown`, the call a press commits, because a press of a strike waits on clips and targets.
 *
 * IT NEEDS THE TAG: until `Cooldown.Aura` is in the generated tag list the set-up check fails and nothing else is asked.
 */
bool FCataclysmSkillAssignmentAuraKeyTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	if (!TestTrue(TEXT("set-up: Cooldown.Aura is in the tag list"),
				  UCataclysmSkillSlots::CooldownTag(ECataclysmAbilitySlot::Aura).IsValid()))
	{
		return false;
	}
	const float HeavySeconds = UCataclysmSkillSlots::NumbersFor(UCataclysmSkillSlots::LoadGeneratedTable(),
																ECataclysmAbilitySlot::Heavy).Cooldown;
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	// THE CONTROL: the aura on its own key waits for nothing, and committing its cooldown applies nothing.
	const UCataclysmGameplayAbility* Aura = AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Aura);
	if (!TestNotNull(TEXT("set-up: Conflagration is on the Aura key"), Aura))
	{
		return false;
	}
	TestEqual(TEXT("control: the aura's own cooldown is nought"), Aura->GetBaseCooldown(), 0.0f, 0.001f);
	Aura->ApplyCooldown(Aura->GetCurrentAbilitySpecHandle(), Aura->GetCurrentActorInfo(),
						FGameplayAbilityActivationInfo());
	TestEqual(TEXT("control: committing it puts no effect under Cooldown.Aura"),
			  CooldownEffectsOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Aura), 0);
	TestTrue(TEXT("control: and the aura is ready again at once"), KeyIsReady(Aura));

	FString Reason;
	if (!TestTrue(TEXT("set-up: Molten Cleave is chosen for the Aura key"),
				  Carrier.State->AssignSkill(ECataclysmAbilitySlot::Aura, MoltenCleaveRow, Reason)))
	{
		return false;
	}
	const UCataclysmGameplayAbility* Cleave = AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Aura);
	if (!TestTrue(TEXT("set-up: Molten Cleave is on the Aura key"),
				  Cleave && Cleave->DisplayedName() == TEXT("Molten Cleave")))
	{
		return false;
	}
	TestEqual(TEXT("its cooldown is the Heavy slot's, the slot it was made for"), Cleave->GetBaseCooldown(),
			  HeavySeconds, 0.001f);
	Cleave->ApplyCooldown(Cleave->GetCurrentAbilitySpecHandle(), Cleave->GetCurrentActorInfo(),
						  FGameplayAbilityActivationInfo());
	TestFalse(TEXT("committed, it is on cooldown"), KeyIsReady(Cleave));
	TestEqual(TEXT("for its own 1.5 seconds, under Cooldown.Aura"),
			  CooldownLengthOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Aura), HeavySeconds, 0.001f);
	TestEqual(TEXT("and nothing waits under Cooldown.Heavy"),
			  CooldownLengthOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Heavy), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentTrapSentenceTest,
	"Cataclysm.SkillAssignment.AnUltimateOnTheHeavyKeyMadeATrapByTheUltimateRowCastsByTheUltimatesSentence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q-L3b: a trap casts by the sentence of the slot its skill was MADE for. Two players 100 m apart, each wearing a
 * Demonic Whip, each with a trap placed from the Heavy key 20 m north, an enemy 2.5 m east of the trap and a second 15
 * m east of it. The control, read first: Searing Lash on the Heavy key, made a trap by the Heavy row, "fires at the
 * nearest enemy" from the trap, a 5 m cone, and the far enemy is untouched. Then The Gathering, made for the Ultimate
 * slot, chosen for the Heavy key and made a trap by the Ultimate row: it casts "centered on the triggering enemy", and
 * its 13 to 14.5 m point-blank strike reaches the far enemy 12.5 m from it.
 */
bool FCataclysmSkillAssignmentTrapSentenceTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	// PINNED AFTER THE WORLD, which sets the roll itself.
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned at 100"), NeverCritical.Variable))
	{
		return false;
	}
	const float M = AssignmentMetre;
	const FVector North(0.0f, 20.0f * M, 0.0f);
	const FVector Near(2.5f * M, 0.0f, 0.0f);
	const FVector Far(15.0f * M, 0.0f, 0.0f);

	// THE CONTROL: the Heavy skill on its own key.
	const FSkillAssignmentCarrier Plain = SpawnAssignmentCarrier(*this, World);
	if (!Plain.IsComplete()
		|| !WearInTheRightHand(*this, Plain, AssignmentWeapon(TEXT("Weapon_Whip"), {FName(TEXT("Demonic"))})))
	{
		return false;
	}
	const FCataclysmWeaponSkill* Lash = HeldOnKey(Plain.Slots, ECataclysmAbilitySlot::Heavy);
	if (!TestTrue(TEXT("set-up: Searing Lash, made for the Heavy slot, is on the Heavy key"),
				  Lash && Lash->Name == TEXT("Searing Lash")
					  && UCataclysmWeaponSkills::DesignedSlotOf(*Lash) == ECataclysmAbilitySlot::Heavy))
	{
		return false;
	}
	ACataclysmEnemyCharacter* PlainNear = AssignmentEnemy(World, North + Near);
	ACataclysmEnemyCharacter* PlainFar = AssignmentEnemy(World, North + Far);
	if (!TestNotNull(TEXT("set-up: the control trap's enemy"), PlainNear)
		|| !TestNotNull(TEXT("set-up: the control's far enemy"), PlainFar))
	{
		return false;
	}
	HoldTheTrapFlag(Plain.AbilitySystem, ECataclysmAbilitySlot::Heavy);
	ACataclysmMinion* PlainTrap = ACataclysmMinion::PlaceSkillTrap(Plain.Character, ECataclysmAbilitySlot::Heavy, North);
	if (!TestNotNull(TEXT("set-up: the control's trap is placed from the Heavy key"), PlainTrap))
	{
		return false;
	}

	// THE ULTIMATE ON THE HEAVY KEY.
	const FVector Home(100.0f * M, 0.0f, 0.0f);
	const FSkillAssignmentCarrier Moved = SpawnAssignmentCarrier(*this, World, Home);
	FString Reason;
	if (!Moved.IsComplete()
		|| !WearInTheRightHand(*this, Moved, AssignmentWeapon(TEXT("Weapon_Whip"), {FName(TEXT("Demonic"))}))
		|| !TestTrue(TEXT("set-up: The Gathering is chosen for the Heavy key"),
					 Moved.State->AssignSkill(ECataclysmAbilitySlot::Heavy, FName(TEXT("Demonic_Whip_Ultimate")), Reason)))
	{
		return false;
	}
	const FCataclysmWeaponSkill* Gathering = HeldOnKey(Moved.Slots, ECataclysmAbilitySlot::Heavy);
	if (!TestTrue(TEXT("set-up: The Gathering, made for the Ultimate slot, is on the Heavy key"),
				  Gathering && Gathering->Name == TEXT("The Gathering")
					  && UCataclysmWeaponSkills::DesignedSlotOf(*Gathering) == ECataclysmAbilitySlot::Ultimate)
		|| !TestTrue(TEXT("set-up: The Gathering reaches past the far enemy's 12.5 m from the near one and short of its 15 m "
						  "from the trap"),
					 Gathering->Params.RadiusCm > 1300.0f && Gathering->Params.RadiusCm < 1450.0f))
	{
		return false;
	}
	ACataclysmEnemyCharacter* MovedNear = AssignmentEnemy(World, Home + North + Near);
	ACataclysmEnemyCharacter* MovedFar = AssignmentEnemy(World, Home + North + Far);
	if (!TestNotNull(TEXT("set-up: the moved trap's enemy"), MovedNear)
		|| !TestNotNull(TEXT("set-up: the moved trap's far enemy"), MovedFar))
	{
		return false;
	}
	HoldTheTrapFlag(Moved.AbilitySystem, ECataclysmAbilitySlot::Ultimate);
	TestTrue(TEXT("the Ultimate trap row converts the Heavy key, which holds an Ultimate"),
			 Moved.AbilitySystem->SlotIsASkillTrap(ECataclysmAbilitySlot::Heavy));
	ACataclysmMinion* MovedTrap =
		ACataclysmMinion::PlaceSkillTrap(Moved.Character, ECataclysmAbilitySlot::Heavy, Home + North);
	if (!TestNotNull(TEXT("set-up: the moved trap is placed from the Heavy key"), MovedTrap))
	{
		return false;
	}

	// BOTH ARMED, A SECOND AND A QUARTER LATER, AND STEPPED BY HAND.
	World->TimeSeconds += 1.25f;
	ACataclysmMinion::GadgetStep(PlainTrap);
	ACataclysmMinion::GadgetStep(MovedTrap);
	TestTrue(*FString::Printf(TEXT("control: the Heavy trap struck its enemy: %.2f"), AssignmentTakenFrom(PlainNear)),
			 AssignmentTakenFrom(PlainNear) > 0.0f);
	TestEqual(TEXT("control: and, fired from the trap at the nearest enemy, left the far enemy untouched"),
			  AssignmentTakenFrom(PlainFar), 0.0f, 0.001f);
	TestTrue(*FString::Printf(TEXT("the Ultimate on the Heavy key struck its enemy: %.2f"), AssignmentTakenFrom(MovedNear)),
			 AssignmentTakenFrom(MovedNear) > 0.0f);
	TestTrue(*FString::Printf(TEXT("and the far enemy, so it was cast centred on the triggering enemy, by the Ultimate's "
								   "sentence: %.2f"), AssignmentTakenFrom(MovedFar)),
			 AssignmentTakenFrom(MovedFar) > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentCarriedCooldownTest,
	"Cataclysm.SkillAssignment.AMovedSkillTakesItsRunningCooldownToTheKeyItLandsOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Layer 4, ruled Q3a 2026-10-25: "carry the running cooldown and charges with the skill". Pyroclasm used on the
 * Ultimate key and then chosen for the Heavy key is still waiting the time it had left, now under `Cooldown.Heavy`;
 * `Cooldown.Ultimate` has ended; and it cannot be pressed. The control, read first: Molten Cleave, never used, chosen
 * for the Special key is ready there at once, so carrying invents no cooldown.
 *
 * The cooldown is committed by the ability's own `ApplyCooldown`, the call a press commits, as the Aura key test above
 * does, because a press of a strike waits on clips and targets. The clock then runs 2.5 seconds, so what is carried is
 * the time LEFT and not the whole length. The time left is read on the world clock before and after the move, with no
 * clock moved between, so they agree to within 0.05 seconds (one clock step), which is the tolerance.
 */
bool FCataclysmSkillAssignmentCarriedCooldownTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	// THE LONGEST TIME LEFT UNDER A KEY'S COOLDOWN TAG, as the skill bar reads it; 0 for none.
	const auto SecondsLeftOn = [](const UAbilitySystemComponent* System, ECataclysmAbilitySlot Key)
	{
		float Longest = 0.0f;
		for (const TPair<float, float>& Wait : System->GetActiveEffectsTimeRemainingAndDuration(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
					 FGameplayTagContainer(UCataclysmSkillSlots::CooldownTag(Key)))))
		{
			Longest = FMath::Max(Longest, Wait.Key);
		}
		return Longest;
	};

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	// THE CONTROL: an unused skill moved is ready at once, and nothing waits on the key it lands on.
	FString Reason;
	if (!TestTrue(TEXT("set-up: Molten Cleave, unused, is chosen for the Special key"),
				  Carrier.State->AssignSkill(ECataclysmAbilitySlot::Special, MoltenCleaveRow, Reason)))
	{
		return false;
	}
	const UCataclysmGameplayAbility* CleaveOnSpecial = AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Special);
	TestTrue(TEXT("control: Molten Cleave moved unused is ready on the Special key at once"),
			 CleaveOnSpecial && CleaveOnSpecial->DisplayedName() == TEXT("Molten Cleave") && KeyIsReady(CleaveOnSpecial));
	TestEqual(TEXT("control: and no cooldown runs under Cooldown.Special"),
			  CooldownEffectsOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Special), 0);
	Carrier.State->ClearSkillChoice(ECataclysmAbilitySlot::Special);

	// PYROCLASM USED ON ITS OWN KEY, and 2.5 seconds pass.
	const UCataclysmGameplayAbility* OnUltimate = AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate);
	if (!TestTrue(TEXT("set-up: Pyroclasm is on the Ultimate key"),
				  OnUltimate && OnUltimate->DisplayedName() == TEXT("Pyroclasm")))
	{
		return false;
	}
	OnUltimate->ApplyCooldown(OnUltimate->GetCurrentAbilitySpecHandle(), OnUltimate->GetCurrentActorInfo(),
							  FGameplayAbilityActivationInfo());
	CataclysmTestWorld::RunClock(World, 2.5f);
	const float LeftBefore = SecondsLeftOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate);
	if (!TestTrue(TEXT("set-up: Pyroclasm waits under Cooldown.Ultimate, with less than its whole length left"),
				  LeftBefore > 0.0f && LeftBefore < CooldownLengthOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate)))
	{
		return false;
	}

	// MOVED TO THE HEAVY KEY, IT TAKES THE TIME IT HAD LEFT WITH IT.
	if (!TestTrue(TEXT("set-up: Pyroclasm is chosen for the Heavy key"),
				  Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, PyroclasmRow, Reason)))
	{
		return false;
	}
	const UCataclysmGameplayAbility* OnHeavy = AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Heavy);
	if (!TestTrue(TEXT("set-up: Pyroclasm is on the Heavy key"), OnHeavy && OnHeavy->DisplayedName() == TEXT("Pyroclasm")))
	{
		return false;
	}
	TestEqual(TEXT("it waits under Cooldown.Heavy the time it had left on the Ultimate key"),
			  SecondsLeftOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Heavy), LeftBefore, 0.05f);
	TestEqual(TEXT("and Cooldown.Ultimate has ended"),
			  CooldownEffectsOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate), 0);
	TestFalse(TEXT("so a press of the Heavy key cannot cast it"), KeyIsReady(OnHeavy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentOffTheKeysCooldownTest,
	"Cataclysm.SkillAssignment.ASkillTakenOffTheKeysWaitsWhatIsLeftOfItsCooldownWhenItReturns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #2524. Pyroclasm used on the Ultimate key, then taken off the keys by choosing Emberhurl for that key, and
 * returned by clearing the slot two seconds later, waits what was left of its cooldown less those two seconds; the
 * skill that held its key in between waits nothing of it. The control, read first: the same two choices with Pyroclasm
 * never used return it ready.
 */
bool FCataclysmSkillAssignmentOffTheKeysCooldownTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	const FName EmberhurlRow(TEXT("Demonic_Greataxe_Special"));
	const auto SecondsLeftOn = [](const UAbilitySystemComponent* System, ECataclysmAbilitySlot Key)
	{
		float Longest = 0.0f;
		for (const TPair<float, float>& Wait : System->GetActiveEffectsTimeRemainingAndDuration(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
					 FGameplayTagContainer(UCataclysmSkillSlots::CooldownTag(Key)))))
		{
			Longest = FMath::Max(Longest, Wait.Key);
		}
		return Longest;
	};
	const auto OnKeyNamed = [](UCataclysmAbilitySystemComponent* System, ECataclysmAbilitySlot Key, const TCHAR* Name)
	{
		const UCataclysmGameplayAbility* Ability = AbilityOnKey(System, Key);
		return Ability && Ability->DisplayedName() == Name ? Ability : nullptr;
	};

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	// THE CONTROL: Pyroclasm, never used, taken off the keys and returned, is ready at once.
	FString Reason;
	if (!TestTrue(TEXT("set-up: Emberhurl is chosen for the Ultimate key, taking Pyroclasm off the keys"),
				  Carrier.State->AssignSkill(ECataclysmAbilitySlot::Ultimate, EmberhurlRow, Reason)
					  && OnKeyNamed(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate, TEXT("Emberhurl"))))
	{
		return false;
	}
	Carrier.State->ClearSkillChoice(ECataclysmAbilitySlot::Ultimate);
	const UCataclysmGameplayAbility* Unused =
		OnKeyNamed(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate, TEXT("Pyroclasm"));
	TestTrue(TEXT("control: Pyroclasm, never used, returns to the Ultimate key ready"), Unused && KeyIsReady(Unused));
	TestEqual(TEXT("control: and no cooldown runs under Cooldown.Ultimate"),
			  CooldownEffectsOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate), 0);

	// PYROCLASM USED, 2.5 SECONDS PASS, AND IT IS TAKEN OFF THE KEYS.
	if (!TestNotNull(TEXT("set-up: Pyroclasm is on the Ultimate key"), Unused))
	{
		return false;
	}
	Unused->ApplyCooldown(Unused->GetCurrentAbilitySpecHandle(), Unused->GetCurrentActorInfo(),
						  FGameplayAbilityActivationInfo());
	CataclysmTestWorld::RunClock(World, 2.5f);
	const float LeftBefore = SecondsLeftOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate);
	if (!TestTrue(TEXT("set-up: Pyroclasm waits with more than 2.5 seconds left"), LeftBefore > 2.5f)
		|| !TestTrue(TEXT("set-up: Emberhurl is chosen for the Ultimate key again, taking Pyroclasm off the keys"),
					 Carrier.State->AssignSkill(ECataclysmAbilitySlot::Ultimate, EmberhurlRow, Reason)))
	{
		return false;
	}
	const UCataclysmGameplayAbility* Between =
		OnKeyNamed(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate, TEXT("Emberhurl"));
	TestTrue(TEXT("Emberhurl, holding the key in between, waits nothing of Pyroclasm's cooldown"),
			 Between && KeyIsReady(Between));

	// TWO SECONDS OFF THE KEYS, THEN BACK.
	CataclysmTestWorld::RunClock(World, 2.0f);
	Carrier.State->ClearSkillChoice(ECataclysmAbilitySlot::Ultimate);
	const UCataclysmGameplayAbility* Back =
		OnKeyNamed(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate, TEXT("Pyroclasm"));
	if (!TestNotNull(TEXT("set-up: Pyroclasm is back on the Ultimate key"), Back))
	{
		return false;
	}
	TestEqual(TEXT("it waits what it had left, less the two seconds it was off the keys"),
			  SecondsLeftOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate), LeftBefore - 2.0f, 0.1f);
	TestFalse(TEXT("so a press of the Ultimate key cannot cast it"), KeyIsReady(Back));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillAssignmentResetReachesOffTheKeysTest,
	"Cataclysm.SkillAssignment.AResetOfEveryCooldownReachesASkillHeldOffTheKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #2528. Pyroclasm, used and then taken off the keys by choosing Emberhurl for the Ultimate key, keeps its
 * cooldown (#2524); a reset of every cooldown and a death end it, as they end the running ones, and a reduction of
 * every cooldown shortens it by the same seconds. The control, read first: with nothing in between, it comes back
 * still waiting.
 */
bool FCataclysmSkillAssignmentResetReachesOffTheKeysTest::RunTest(const FString&)
{
	using namespace CataclysmSkillAssignmentTest;

	const FName EmberhurlRow(TEXT("Demonic_Greataxe_Special"));
	const auto SecondsLeftOn = [](const UAbilitySystemComponent* System, ECataclysmAbilitySlot Key)
	{
		float Longest = 0.0f;
		for (const TPair<float, float>& Wait : System->GetActiveEffectsTimeRemainingAndDuration(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
					 FGameplayTagContainer(UCataclysmSkillSlots::CooldownTag(Key)))))
		{
			Longest = FMath::Max(Longest, Wait.Key);
		}
		return Longest;
	};

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillAssignmentCarrier Carrier = SpawnAssignmentCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}
	FString Reason;
	const auto Pyroclasm = [&Carrier]() -> const UCataclysmGameplayAbility*
	{
		const UCataclysmGameplayAbility* Ability = AbilityOnKey(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate);
		return Ability && Ability->DisplayedName() == TEXT("Pyroclasm") ? Ability : nullptr;
	};
	// PYROCLASM USED, ONE SECOND PASSES, AND EMBERHURL IS CHOSEN FOR ITS KEY: the seconds it had left, or -1.
	const auto UseAndTakeOff = [&]() -> float
	{
		const UCataclysmGameplayAbility* Used = Pyroclasm();
		if (!Used)
		{
			return -1.0f;
		}
		Used->ApplyCooldown(Used->GetCurrentAbilitySpecHandle(), Used->GetCurrentActorInfo(),
							FGameplayAbilityActivationInfo());
		CataclysmTestWorld::RunClock(World, 1.0f);
		const float Left = SecondsLeftOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate);
		return Carrier.State->AssignSkill(ECataclysmAbilitySlot::Ultimate, EmberhurlRow, Reason) && Left > 5.0f
			? Left
			: -1.0f;
	};
	const auto Return = [&]() -> const UCataclysmGameplayAbility*
	{
		Carrier.State->ClearSkillChoice(ECataclysmAbilitySlot::Ultimate);
		return Pyroclasm();
	};

	// THE CONTROL: nothing in between, and it comes back still waiting.
	if (!TestTrue(TEXT("set-up: Pyroclasm used and taken off the keys"), UseAndTakeOff() > 0.0f))
	{
		return false;
	}
	const UCataclysmGameplayAbility* Back = Return();
	if (!TestNotNull(TEXT("set-up: Pyroclasm is back on the Ultimate key"), Back))
	{
		return false;
	}
	TestFalse(TEXT("control: with no reset in between, it cannot be cast"), KeyIsReady(Back));
	TestTrue(TEXT("control: and it still waits"),
			 SecondsLeftOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate) > 0.0f);

	// A RESET OF EVERY COOLDOWN, while it is off the keys.
	IConsoleVariable* Roll = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.CooldownResetRoll"));
	if (!TestNotNull(TEXT("set-up: the reset roll can be pinned"), Roll)
		|| !TestTrue(TEXT("set-up: used and taken off again"), UseAndTakeOff() > 0.0f))
	{
		return false;
	}
	Roll->Set(0.0f, ECVF_SetByConsole);
	ON_SCOPE_EXIT { Roll->Set(-1.0f, ECVF_SetByConsole); };
	FCataclysmPoolAction ResetAll;
	ResetAll.CooldownReset = ECataclysmCooldownReset::All;
	ResetAll.Percent = 100.0f;
	Carrier.AbilitySystem->RollAndResetCooldowns(ResetAll, nullptr);
	Back = Return();
	if (!TestNotNull(TEXT("set-up: back after the reset"), Back))
	{
		return false;
	}
	TestTrue(TEXT("after a reset of every cooldown it comes back ready"), KeyIsReady(Back));
	TestEqual(TEXT("and no cooldown runs under Cooldown.Ultimate"),
			  CooldownEffectsOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate), 0);

	// A DEATH, while it is off the keys.
	if (!TestTrue(TEXT("set-up: used and taken off a third time"), UseAndTakeOff() > 0.0f))
	{
		return false;
	}
	Carrier.AbilitySystem->ClearWhatDeathEnds();
	Back = Return();
	if (!TestNotNull(TEXT("set-up: back after the death"), Back))
	{
		return false;
	}
	TestTrue(TEXT("after a death it comes back ready"), KeyIsReady(Back));

	// A REDUCTION OF EVERY COOLDOWN BY FIVE SECONDS, while it is off the keys.
	const float LeftBefore = UseAndTakeOff();
	if (!TestTrue(TEXT("set-up: used and taken off a fourth time, with more than five seconds left"), LeftBefore > 0.0f))
	{
		return false;
	}
	FCataclysmPoolAction ReduceAll;
	ReduceAll.CooldownReduce = ECataclysmCooldownReset::All;
	ReduceAll.Percent = 5.0f;
	Carrier.AbilitySystem->ReduceCooldowns(ReduceAll);
	Back = Return();
	if (!TestNotNull(TEXT("set-up: back after the reduction"), Back))
	{
		return false;
	}
	TestEqual(TEXT("after a reduction of five seconds it waits five fewer"),
			  SecondsLeftOn(Carrier.AbilitySystem, ECataclysmAbilitySlot::Ultimate), LeftBefore - 5.0f, 0.1f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
