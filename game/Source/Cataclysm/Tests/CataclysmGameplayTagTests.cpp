// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "GameplayTagsManager.h"
#include "GameplayTagContainer.h"

/**
 * Proves the generated tag list actually reaches the engine.
 *
 * game/Config/Tags/CataclysmTags.ini is produced by
 * tools/generate_gameplay_tags.py from the Tags sheet of the design workbook. A
 * Python test checks the file is current; these check the engine loaded it.
 *
 * Both halves are needed. The file can be perfectly correct and still not load
 * if ImportTagsFromConfig is off or the file is in the wrong directory, and
 * nothing reports that -- tags simply resolve to nothing and every ability and
 * enchantment that uses them silently stops matching.
 */

namespace
{
	bool TagExists(const TCHAR* TagName)
	{
		return UGameplayTagsManager::Get()
			.RequestGameplayTag(FName(TagName), /*ErrorIfNotFound=*/false)
			.IsValid();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTagsAreLoadedTest,
	"Cataclysm.GameplayTags.GeneratedTagsAreLoaded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTagsAreLoadedTest::RunTest(const FString& Parameters)
{
	// One tag from each of the eight roots in the Tags sheet. If the file failed
	// to load, all eight fail together and the cause is obvious.
	const TCHAR* Sample[] = {
		TEXT("Element.Demonic"),
		TEXT("Type.Projectile"),
		TEXT("Slot.Ultimate"),
		TEXT("Item.Slot.Head"),
		TEXT("Stat.Resource.Mana"),
		TEXT("Keyword.DoT.Bleed"),
		TEXT("Scope.Global"),
		TEXT("Trigger.OnKill"),
	};

	for (const TCHAR* Name : Sample)
	{
		TestTrue(FString::Printf(TEXT("%s is a registered gameplay tag"), Name),
			TagExists(Name));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmTagCountTest,
	"Cataclysm.GameplayTags.AllEightElementsExist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmTagCountTest::RunTest(const FString& Parameters)
{
	// The design has exactly eight damage types and the whole skill, class and
	// enchantment structure is built on there being eight. A missing one would
	// break an entire branch of content.
	const TCHAR* Elements[] = {
		TEXT("Element.War"), TEXT("Element.Demonic"), TEXT("Element.Death"),
		TEXT("Element.Pestilence"), TEXT("Element.Famine"), TEXT("Element.Celestial"),
		TEXT("Element.Chaos"), TEXT("Element.Void"),
	};

	for (const TCHAR* Name : Elements)
	{
		TestTrue(FString::Printf(TEXT("%s exists"), Name), TagExists(Name));
	}

	// Parent tags are created implicitly by the engine, so data may reference
	// "Element" or "Item.Weapon" even though the sheet lists only leaves.
	TestTrue(TEXT("The implicit parent Element exists"), TagExists(TEXT("Element")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUnknownTagTest,
	"Cataclysm.GameplayTags.UnknownTagsDoNotResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUnknownTagTest::RunTest(const FString& Parameters)
{
	// Guards the tests above. If RequestGameplayTag returned something valid for
	// any name at all, they would pass whether or not the file loaded.
	TestFalse(TEXT("A tag that was never declared does not resolve"),
		TagExists(TEXT("Element.ThisTagDoesNotExist")));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
