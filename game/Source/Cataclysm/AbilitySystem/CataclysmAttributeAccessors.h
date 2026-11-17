// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "AttributeSet.h"
#include "AbilitySystemComponent.h"

/**
 * Shared helpers for every attribute set in this module.
 *
 * WHY THE UPROPERTY LINES ARE NOT MACROS. Unreal Header Tool parses header text
 * and does not expand macros, so a macro generating a UPROPERTY or a UFUNCTION
 * is invisible to it and the attribute never reflects, never replicates and
 * never appears in data. Every attribute below is therefore declared literally,
 * with only the plain C++ accessors and the .cpp-side bodies macroed. That
 * verbosity is a constraint of the tool, not a style choice.
 */

/**
 * Generates the four accessors the Gameplay Ability System expects for every
 * attribute: a getter, a setter, an initialiser, and the FGameplayAttribute
 * property accessor used to reference the attribute in data.
 */
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/** Body of an OnRep function. Goes in the .cpp, where UHT does not look. */
#define CATACLYSM_ON_REP(ClassName, PropertyName) \
	void ClassName::OnRep_##PropertyName(const FGameplayAttributeData& OldValue) \
	{ \
		GAMEPLAYATTRIBUTE_REPNOTIFY(ClassName, PropertyName, OldValue); \
	}

/**
 * Registers one attribute for replication. Goes in GetLifetimeReplicatedProps.
 *
 * REPNOTIFY_Always throughout. Without it, a value that replicates back to the
 * number it already held -- healing to full, taking zero damage -- fires no
 * notify, and any interface driven off the notify silently stops updating.
 */
#define CATACLYSM_REPLICATE(ClassName, PropertyName) \
	DOREPLIFETIME_CONDITION_NOTIFY(ClassName, PropertyName, COND_None, REPNOTIFY_Always)
