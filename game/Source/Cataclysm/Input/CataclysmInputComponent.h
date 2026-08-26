// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputComponent.h"
#include "Input/CataclysmInputConfig.h"
#include "CataclysmInputComponent.generated.h"

/**
 * The input component, which knows how to bind a whole input config at once.
 *
 * The two functions below are templates and therefore have to be defined in this
 * header. Unreal Header Tool does not expand macros or instantiate templates, so
 * neither carries a UFUNCTION and neither is reachable from Blueprint; that is
 * intended, because both take a member function pointer.
 */
UCLASS()
class CATACLYSM_API UCataclysmInputComponent : public UEnhancedInputComponent
{
	GENERATED_BODY()

public:
	/**
	 * Binds one named native action -- movement, the cursor, the stand-still
	 * modifier -- to a specific function.
	 *
	 * Silently does nothing when the config does not list the name, because a
	 * config that omits an optional binding is legitimate. The test
	 * Cataclysm.Input.ConfigListsEveryNativeAction is what catches an omission
	 * that was not intended, rather than an ensure firing at play time.
	 */
	template <typename UserClass, typename FuncType>
	void BindNativeAction(const UCataclysmInputConfig* Config, const FName& Name,
						  ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func);

	/**
	 * Binds every ability action in the config to one shared pressed handler and
	 * one shared released handler, each given the slot tag as its argument.
	 *
	 * This is the part that must not be per-ability. Adding an eighth slot means
	 * adding a row to the config asset and a tag to the workbook, and no C++
	 * change at all.
	 */
	template <typename UserClass, typename PressedFuncType, typename ReleasedFuncType>
	void BindAbilityActions(const UCataclysmInputConfig* Config, UserClass* Object,
							PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc,
							TArray<uint32>& OutBindHandles);
};

template <typename UserClass, typename FuncType>
void UCataclysmInputComponent::BindNativeAction(const UCataclysmInputConfig* Config, const FName& Name,
											    ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func)
{
	check(Config);

	if (const UInputAction* Action = Config->FindNativeAction(Name))
	{
		BindAction(Action, TriggerEvent, Object, Func);
	}
}

template <typename UserClass, typename PressedFuncType, typename ReleasedFuncType>
void UCataclysmInputComponent::BindAbilityActions(const UCataclysmInputConfig* Config, UserClass* Object,
												  PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc,
												  TArray<uint32>& OutBindHandles)
{
	check(Config);

	for (const FCataclysmAbilityInputAction& Action : Config->GetAbilityActions())
	{
		if (!Action.InputAction || !Action.SlotTag.IsValid())
		{
			continue;
		}

		if (PressedFunc)
		{
			OutBindHandles.Add(
				BindAction(Action.InputAction, ETriggerEvent::Triggered, Object, PressedFunc, Action.SlotTag).GetHandle());
		}

		if (ReleasedFunc)
		{
			OutBindHandles.Add(
				BindAction(Action.InputAction, ETriggerEvent::Completed, Object, ReleasedFunc, Action.SlotTag).GetHandle());

			// CANCELED AS WELL AS COMPLETED. A press interrupted by the mapping
			// context changing reports Canceled and never reports Completed, so
			// without this the ability system is never told the key came up, and
			// anything keeping per-press state for the slot keeps it for ever.
			// Issue #1016 added state of exactly that kind, so this stopped
			// being only tidy. The MoveToCursor binding in
			// `ACataclysmPlayerController::SetupInputComponent` already binds
			// both events for the same reason and says so there.
			OutBindHandles.Add(
				BindAction(Action.InputAction, ETriggerEvent::Canceled, Object, ReleasedFunc, Action.SlotTag).GetHandle());
		}
	}
}
