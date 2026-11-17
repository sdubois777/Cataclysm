// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "CataclysmWidgetAuthoring.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWidgetAuthoringRegistersEachWidgetTest,
	"Cataclysm.WidgetAuthoring.EachAddedWidgetIsRegisteredWithItsBlueprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #1566. `UCataclysmWidgetAuthoring::AddWidget` put a widget in the tree without registering its name with the
 * Widget Blueprint, so the compiler raised one ensure per widget. Adding two widgets to a blueprint made in memory
 * raises none, and each has an entry in `WidgetVariableNameToGuidMap`. CONTROL: a second widget refused for its name
 * adds no entry.
 *
 * THE ENSURE IS THE CHECK, AND THE MAP IS ONLY THE END STATE. `AddWidget` ends with
 * `FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified`, which compiles the skeleton at once, and that compile
 * runs `FWidgetBlueprintCompilerContext::ValidateAndFixUpVariableGuids`: it raises the ensure for a widget with no
 * entry and then adds the entry itself. So the map is full whether or not `AddWidget` registered the names, and only
 * the ensure tells the two apart.
 */
bool FCataclysmWidgetAuthoringRegistersEachWidgetTest::RunTest(const FString& Parameters)
{
	// EVERY LINE THE ENGINE WRITES WHILE THE WIDGETS ARE ADDED, whatever its category and verbosity: the ensure is
	// written by `LogOutputDevice` at Error.
	struct FCapture : public FOutputDevice
	{
		FCapture() { if (GLog) { GLog->AddOutputDevice(this); } }
		virtual ~FCapture() { if (GLog) { GLog->RemoveOutputDevice(this); } }
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type, const FName&) override { Lines.Add(Text); }
		int32 Count(const TCHAR* Needle)
		{
			if (GLog)
			{
				GLog->Flush();
			}
			int32 Found = 0;
			for (const FString& Line : Lines)
			{
				Found += Line.Contains(Needle, ESearchCase::CaseSensitive) ? 1 : 0;
			}
			return Found;
		}
		TArray<FString> Lines;
	};

	// IN MEMORY, IN THE TRANSIENT PACKAGE, so nothing is saved and no asset is left behind.
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UWidgetBlueprint::StaticClass(), TEXT("WBP_WidgetAuthoringTest")),
		BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
	if (!TestNotNull(TEXT("set-up: a Widget Blueprint made in memory"), Blueprint)
		|| !TestEqual(TEXT("set-up: it starts with no registered widget"), Blueprint->WidgetVariableNameToGuidMap.Num(), 0))
	{
		return false;
	}

	FCapture Capture;
	UWidget* Root = UCataclysmWidgetAuthoring::AddWidget(Blueprint, UCanvasPanel::StaticClass(), TEXT("Root"), FString());
	UWidget* Label = UCataclysmWidgetAuthoring::AddWidget(Blueprint, UTextBlock::StaticClass(), TEXT("Label"), TEXT("Root"));
	if (!TestNotNull(TEXT("set-up: the root was added"), Root) || !TestNotNull(TEXT("set-up: the label was added"), Label))
	{
		return false;
	}

	// THE CONTROL'S REFUSAL IS AN ERROR IN THE LOG BY DESIGN, so it is declared.
	AddExpectedError(TEXT("already holds a widget called Label"), EAutomationExpectedErrorFlags::Contains, 1);
	UWidget* Again = UCataclysmWidgetAuthoring::AddWidget(Blueprint, UTextBlock::StaticClass(), TEXT("Label"), TEXT("Root"));

	const int32 Ensures = Capture.Count(TEXT("did not get a GUID"));
	const FGuid* RootGuid = Blueprint->WidgetVariableNameToGuidMap.Find(FName(TEXT("Root")));
	const FGuid* LabelGuid = Blueprint->WidgetVariableNameToGuidMap.Find(FName(TEXT("Label")));
	AddInfo(FString::Printf(TEXT("WIDGET GUIDS: %d ensures; %d entries; Root %s, Label %s; a second Label %s"),
							Ensures, Blueprint->WidgetVariableNameToGuidMap.Num(),
							RootGuid ? *RootGuid->ToString() : TEXT("missing"),
							LabelGuid ? *LabelGuid->ToString() : TEXT("missing"),
							Again ? TEXT("was added") : TEXT("was refused")));
	TestEqual(TEXT("adding two widgets raises no missing-identifier ensure"), Ensures, 0);
	TestTrue(TEXT("the root is registered with a valid identifier"), RootGuid && RootGuid->IsValid());
	TestTrue(TEXT("the label is registered with a valid identifier"), LabelGuid && LabelGuid->IsValid());
	TestNull(TEXT("CONTROL: a second widget called Label is refused"), Again);
	TestEqual(TEXT("CONTROL: and the refusal adds no entry"), Blueprint->WidgetVariableNameToGuidMap.Num(), 2);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
