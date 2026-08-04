// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "Input/CataclysmInputConfig.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagsManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedActionKeyMapping.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"

/**
 * Tests for input reaching the ability system.
 *
 * WHAT THESE COVER AND WHAT THEY DO NOT. They cover the chain from a slot tag to
 * an ability activating, and they cover the contents of the generated assets --
 * which is the guarantee tools/generate_input_assets.py cannot give by comparing
 * bytes, because a .uasset is not reproducible byte for byte.
 *
 * They do not cover whether a key press reaches Enhanced Input, whether the
 * camera reads well, or whether movement feels right. Those need a running game
 * and a person looking at it.
 */

namespace CataclysmInputTest
{
	const TCHAR* ConfigPath = TEXT("/Game/Input/DA_InputConfig.DA_InputConfig");
	const TCHAR* MouseContextPath = TEXT("/Game/Input/IMC_MouseMovement.IMC_MouseMovement");
	const TCHAR* KeyboardContextPath = TEXT("/Game/Input/IMC_KeyboardMovement.IMC_KeyboardMovement");

	/** An ability system component on a throwaway actor, with input plumbed in. */
	struct FScopedInputFixture
	{
		explicit FScopedInputFixture(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// Recording activations through the delegate rather than asking a spec
			// whether it is active. A plain UGameplayAbility with no work to do
			// ends the moment it activates, so IsActive() is false again by the
			// time the test looks.
			AbilitySystem->AbilityActivatedCallbacks.AddLambda(
				[this](UGameplayAbility* Ability)
				{
					if (Ability)
					{
						Activated.Add(Ability->GetCurrentAbilitySpecHandle());
					}
				});
		}

		~FScopedInputFixture()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Grants a do-nothing ability carrying one slot tag, as a real grant would. */
		FGameplayAbilitySpecHandle GrantAbilityInSlot(const FGameplayTag& SlotTag) const
		{
			FGameplayAbilitySpec Spec(UGameplayAbility::StaticClass(), /*InLevel=*/1);
			Spec.GetDynamicSpecSourceTags().AddTag(SlotTag);
			return AbilitySystem->GiveAbility(Spec);
		}

		bool DidActivate(const FGameplayAbilitySpecHandle& Handle) const
		{
			return Activated.Contains(Handle);
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		mutable TArray<FGameplayAbilitySpecHandle> Activated;
	};

	UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}

	FGameplayTag Tag(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/** Every mapping in a context, across the mapping data the engine stores them in. */
	TArray<FEnhancedActionKeyMapping> MappingsOf(const UInputMappingContext* Context)
	{
		return Context ? Context->GetMappings() : TArray<FEnhancedActionKeyMapping>();
	}
}

/**
 * The guard against the two slot lists drifting apart.
 *
 * ECataclysmAbilitySlot is hand-written C++. The Slot.* tags are generated from
 * the Tags sheet of the design workbook. If one gains a slot the other does not,
 * an ability authored into that slot is granted a tag no input produces, and it
 * never fires with no error anywhere.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSlotTagsMatchTheEnumTest,
	"Cataclysm.Input.EveryAbilitySlotHasAGeneratedTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSlotTagsMatchTheEnumTest::RunTest(const FString& Parameters)
{
	const TArrayView<const ECataclysmAbilitySlot> Slots = CataclysmAbilitySlots::All();

	TestEqual(TEXT("The design has seven ability slots"), Slots.Num(), 7);

	// Every enum value resolves to a registered tag.
	TSet<FGameplayTag> FromEnum;
	for (const ECataclysmAbilitySlot Slot : Slots)
	{
		const FGameplayTag SlotTag = CataclysmAbilitySlots::Tag(Slot);
		TestTrue(FString::Printf(TEXT("slot %d resolves to a registered tag"), static_cast<int32>(Slot)),
			SlotTag.IsValid());
		FromEnum.Add(SlotTag);
	}

	TestEqual(TEXT("No two slots share a tag"), FromEnum.Num(), Slots.Num());

	// And the other direction, which is the half that is easy to forget: a tag
	// added to the workbook that the enum knows nothing about.
	FGameplayTagContainer FromRegistry =
		UGameplayTagsManager::Get().RequestGameplayTagChildren(CataclysmInputTest::Tag(TEXT("Slot")));

	TestEqual(TEXT("The generated tag list has exactly as many Slot children as the enum has slots"),
		FromRegistry.Num(), Slots.Num());

	for (const FGameplayTag& RegistryTag : FromRegistry)
	{
		TestTrue(FString::Printf(TEXT("%s is covered by ECataclysmAbilitySlot"), *RegistryTag.ToString()),
			FromEnum.Contains(RegistryTag));
	}

	// None must map to nothing. Passives and anything the player does not press a
	// key for rely on this, and a valid tag here would give every one of them an
	// input binding.
	TestFalse(TEXT("The None slot has no tag"),
		CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::None).IsValid());

	return true;
}

/**
 * A slot press activates the ability in that slot, and no other.
 *
 * This is the acceptance criterion from issue #16 that says slot-to-input
 * mapping must be data-driven. Nothing here names an ability class.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSlotInputActivatesTest,
	"Cataclysm.Input.PressingASlotActivatesTheAbilityInThatSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSlotInputActivatesTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmInputTest::MakeWorld();
	{
		const CataclysmInputTest::FScopedInputFixture Fixture(World);

		const FGameplayTag HeavyTag = CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Heavy);
		const FGameplayTag UltimateTag = CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Ultimate);

		const FGameplayAbilitySpecHandle Heavy = Fixture.GrantAbilityInSlot(HeavyTag);
		const FGameplayAbilitySpecHandle Ultimate = Fixture.GrantAbilityInSlot(UltimateTag);

		// Nothing happens until the frame's input is processed. If this fails,
		// the press activated immediately and the deferral is not working.
		Fixture.AbilitySystem->AbilityInputTagPressed(HeavyTag);
		TestFalse(TEXT("A press alone activates nothing"), Fixture.DidActivate(Heavy));

		Fixture.AbilitySystem->ProcessAbilityInput();

		TestTrue(TEXT("The ability in the pressed slot activated"), Fixture.DidActivate(Heavy));
		TestFalse(TEXT("The ability in a different slot did not"), Fixture.DidActivate(Ultimate));
	}
	World->DestroyWorld(false);
	return true;
}

/** A slot with nothing in it, and a press of a tag no ability carries. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmptySlotInputTest,
	"Cataclysm.Input.PressingAnEmptySlotDoesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmptySlotInputTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmInputTest::MakeWorld();
	{
		const CataclysmInputTest::FScopedInputFixture Fixture(World);

		const FGameplayAbilitySpecHandle Heavy =
			Fixture.GrantAbilityInSlot(CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Heavy));

		// A slot the player has assigned nothing to. This happens constantly in
		// normal play, because the skill pool comes from the equipped weapon.
		Fixture.AbilitySystem->AbilityInputTagPressed(
			CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Aura));
		Fixture.AbilitySystem->ProcessAbilityInput();

		TestFalse(TEXT("Pressing an empty slot activates nothing"), Fixture.DidActivate(Heavy));

		// An invalid tag must not match every ability, which is what a container
		// check written the wrong way round would do.
		Fixture.AbilitySystem->AbilityInputTagPressed(FGameplayTag());
		Fixture.AbilitySystem->ProcessAbilityInput();

		TestFalse(TEXT("Pressing an invalid tag activates nothing"), Fixture.DidActivate(Heavy));
	}
	World->DestroyWorld(false);
	return true;
}

/** ClearAbilityInput drops presses that have not been processed yet. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClearAbilityInputTest,
	"Cataclysm.Input.ClearingInputDropsPendingPresses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClearAbilityInputTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmInputTest::MakeWorld();
	{
		const CataclysmInputTest::FScopedInputFixture Fixture(World);

		const FGameplayTag HeavyTag = CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Heavy);
		const FGameplayAbilitySpecHandle Heavy = Fixture.GrantAbilityInSlot(HeavyTag);

		Fixture.AbilitySystem->AbilityInputTagPressed(HeavyTag);

		// The pawn is unpossessed, or a menu opened, between the press and the
		// frame's processing. The press must not survive it.
		Fixture.AbilitySystem->ClearAbilityInput();
		Fixture.AbilitySystem->ProcessAbilityInput();

		TestFalse(TEXT("A press cleared before processing does not activate"),
			Fixture.DidActivate(Heavy));
	}
	World->DestroyWorld(false);
	return true;
}

/**
 * The generated input config lists every binding the code looks for.
 *
 * This is what stands in for a byte comparison of the generated assets. If
 * tools/generate_input_assets.py stops writing an entry, or writes it under a
 * different name, the controller binds nothing for it and that key does nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInputConfigContentsTest,
	"Cataclysm.Input.ConfigListsEverySlotAndNativeAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInputConfigContentsTest::RunTest(const FString& Parameters)
{
	const UCataclysmInputConfig* Config =
		LoadObject<UCataclysmInputConfig>(nullptr, CataclysmInputTest::ConfigPath);

	if (!Config)
	{
		AddError(FString::Printf(
			TEXT("%s does not exist. Run tools/generate_input_assets.py."),
			CataclysmInputTest::ConfigPath));
		return false;
	}

	// Three native bindings, found by the names the controller uses.
	const FName NativeNames[] = {
		CataclysmInputActionNames::Move,
		CataclysmInputActionNames::MoveToCursor,
		CataclysmInputActionNames::StandStill,
	};

	for (const FName& Name : NativeNames)
	{
		TestNotNull(FString::Printf(TEXT("the config lists a native action named %s"), *Name.ToString()),
			Config->FindNativeAction(Name));
	}

	TestEqual(TEXT("and lists no others"),
		Config->GetNativeActions().Num(), static_cast<int32>(UE_ARRAY_COUNT(NativeNames)));

	TestNull(TEXT("a name the config does not list resolves to nothing"),
		Config->FindNativeAction(FName(TEXT("NotABinding"))));

	// Six ability bindings: the seven slots less the basic attack, which the
	// design makes automatic and gives no key.
	const TArray<FCataclysmAbilityInputAction>& Ability = Config->GetAbilityActions();
	TestEqual(TEXT("six ability slots have a key"), Ability.Num(), 6);

	TSet<FGameplayTag> SeenTags;
	TSet<const UInputAction*> SeenActions;
	for (const FCataclysmAbilityInputAction& Entry : Ability)
	{
		TestNotNull(TEXT("every ability binding names an input action"), Entry.InputAction.Get());
		TestTrue(FString::Printf(TEXT("%s is a registered tag"), *Entry.SlotTag.ToString()),
			Entry.SlotTag.IsValid());

		SeenTags.Add(Entry.SlotTag);
		SeenActions.Add(Entry.InputAction);
	}

	// Two slots sharing one action would mean one key firing both.
	TestEqual(TEXT("no two ability bindings share a slot tag"), SeenTags.Num(), Ability.Num());
	TestEqual(TEXT("no two ability bindings share an input action"), SeenActions.Num(), Ability.Num());

	TestFalse(TEXT("the basic attack slot has no key, because it is automatic"),
		SeenTags.Contains(CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::BasicAttack)));

	return true;
}

/**
 * Both control schemes bind every action, and neither binds one key twice.
 *
 * THE SECOND HALF IS THE POINT. The design document puts the Support ability on
 * W and also lists WASD as directional movement. Two actions on one key in one
 * context means pressing it does both, which is why the two schemes are separate
 * contexts rather than one. This test is what stops them being merged by
 * accident later.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMappingContextsTest,
	"Cataclysm.Input.MappingContextsCoverEveryActionWithoutCollisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMappingContextsTest::RunTest(const FString& Parameters)
{
	const UCataclysmInputConfig* Config =
		LoadObject<UCataclysmInputConfig>(nullptr, CataclysmInputTest::ConfigPath);

	if (!Config)
	{
		AddError(TEXT("The input config does not exist. Run tools/generate_input_assets.py."));
		return false;
	}

	// Every ability slot has a key in both schemes. So does directional movement,
	// which the mouse scheme puts on the gamepad stick alone, and the stand-still
	// modifier.
	TSet<const UInputAction*> RequiredEverywhere;
	for (const FCataclysmAbilityInputAction& Entry : Config->GetAbilityActions())
	{
		RequiredEverywhere.Add(Entry.InputAction);
	}
	RequiredEverywhere.Add(Config->FindNativeAction(CataclysmInputActionNames::Move));
	RequiredEverywhere.Add(Config->FindNativeAction(CataclysmInputActionNames::StandStill));

	// Click-to-move is the one binding the two schemes disagree about, and the
	// disagreement is the design rather than an omission. Under keyboard movement
	// the left mouse button is left free, which is what Diablo 4 does in its
	// keyboard preset. Asserting it is ABSENT there, not merely optional, is what
	// makes this a guard rather than a shrug.
	const UInputAction* MoveToCursor =
		Config->FindNativeAction(CataclysmInputActionNames::MoveToCursor);

	struct FContextExpectation
	{
		const TCHAR* Path;
		bool bBindsMoveToCursor;
	};

	const FContextExpectation Contexts[] = {
		{ CataclysmInputTest::MouseContextPath,    true  },
		{ CataclysmInputTest::KeyboardContextPath, false },
	};

	for (const FContextExpectation& Expectation : Contexts)
	{
		const TCHAR* Path = Expectation.Path;
		const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, Path);
		if (!Context)
		{
			AddError(FString::Printf(TEXT("%s does not exist. Run tools/generate_input_assets.py."), Path));
			continue;
		}

		const TArray<FEnhancedActionKeyMapping> Mappings = CataclysmInputTest::MappingsOf(Context);

		TSet<const UInputAction*> Mapped;
		TMap<FKey, const UInputAction*> KeyOwner;

		for (const FEnhancedActionKeyMapping& Mapping : Mappings)
		{
			if (!Mapping.Action)
			{
				AddError(FString::Printf(TEXT("%s has a mapping with no action"), Path));
				continue;
			}

			Mapped.Add(Mapping.Action);

			if (const UInputAction** Existing = KeyOwner.Find(Mapping.Key))
			{
				// Two different actions on the same key. The same action on the
				// same key twice is only redundant; this is a real conflict.
				if (*Existing != Mapping.Action)
				{
					AddError(FString::Printf(
						TEXT("%s binds %s to both %s and %s"),
						Path, *Mapping.Key.ToString(),
						*GetNameSafe(*Existing), *GetNameSafe(Mapping.Action)));
				}
			}
			else
			{
				KeyOwner.Add(Mapping.Key, Mapping.Action);
			}
		}

		for (const UInputAction* Action : RequiredEverywhere)
		{
			TestTrue(FString::Printf(TEXT("%s binds a key for %s"), Path, *GetNameSafe(Action)),
				Mapped.Contains(Action));
		}

		TestEqual(
			FString::Printf(TEXT("%s binds click-to-move only if its scheme uses it"), Path),
			Mapped.Contains(MoveToCursor), Expectation.bBindsMoveToCursor);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
