// Copyright Stephen Dubois. All Rights Reserved.

#include "Input/CataclysmInputConfig.h"
#include "InputAction.h"

const UInputAction* UCataclysmInputConfig::FindNativeAction(const FName& Name) const
{
	for (const FCataclysmNativeInputAction& Action : NativeInputActions)
	{
		if (Action.InputAction && Action.Name == Name)
		{
			return Action.InputAction;
		}
	}

	return nullptr;
}
