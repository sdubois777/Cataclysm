// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmImpCharacter.h"
#include "Curves/CurveFloat.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmParagonHold.h"
#include "Tests/CataclysmTestSkip.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

/**
 * What the hold keeps survives a garbage collection, and what it does not keep does not. Issue #2968.
 *
 * THE MECHANISM, WITHOUT THE ART: two packages the test makes, each holding one object nothing else references. The hold
 * is asked to keep everything under the first package's name; after a collection the object in it is still there and
 * the one in the second is gone. THE CONTROL is that second object: it shows the collection really frees an object
 * nothing holds, so the first surviving is the hold's doing and not the test world's.
 *
 * THE PARAGON HALF, where the art exists: the Imp's body mesh lies under `ParagonPrefix`, and the hold keeps it. Without
 * the Paragon Minions pack, as in a worktree, it reports that half skipped. Prints "HOLD".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmParagonHoldTest,
	"Cataclysm.ParagonHold.WhatItHoldsSurvivesACollectionAndWhatItDoesNotIsFreed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmParagonHoldTest::RunTest(const FString& Parameters)
{
	// INSIDE A GAME INSTANCE, the only outer a game instance subsystem may have. The instance is kept for the test, and it
	// keeps the hold, as the game's own instance does in play.
	UGameInstance* Instance = NewObject<UGameInstance>(GetTransientPackage());
	UCataclysmParagonHold* Hold = Instance ? NewObject<UCataclysmParagonHold>(Instance) : nullptr;
	if (!TestNotNull(TEXT("set-up: a game instance"), Instance) || !TestNotNull(TEXT("set-up: a hold"), Hold))
	{
		return false;
	}
	Instance->AddToRoot();
	Hold->AddToRoot();
	ON_SCOPE_EXIT
	{
		Hold->RemoveFromRoot();
		Instance->RemoveFromRoot();
	};

	const FString HeldName = FString::Printf(TEXT("/Temp/CataclysmParagonHoldTest_Held_%u"), FPlatformTime::Cycles());
	const FString FreeName = FString::Printf(TEXT("/Temp/CataclysmParagonHoldTest_Free_%u"), FPlatformTime::Cycles());
	UPackage* HeldPackage = CreatePackage(*HeldName);
	UPackage* FreePackage = CreatePackage(*FreeName);
	if (!TestNotNull(TEXT("set-up: the held package"), HeldPackage) || !TestNotNull(TEXT("set-up: the free package"), FreePackage))
	{
		return false;
	}
	const TWeakObjectPtr<UCurveFloat> HeldObject = NewObject<UCurveFloat>(HeldPackage, TEXT("HeldCurve"));
	const TWeakObjectPtr<UCurveFloat> FreeObject = NewObject<UCurveFloat>(FreePackage, TEXT("FreeCurve"));

	const int32 Added = Hold->HoldNowForTests(HeldName);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	AddInfo(FString::Printf(TEXT("HOLD: %d held under %s; held object %s, free object %s after a collection"), Added,
							*HeldName, HeldObject.IsValid() ? TEXT("alive") : TEXT("freed"),
							FreeObject.IsValid() ? TEXT("alive") : TEXT("freed")));
	TestTrue(TEXT("the hold took the package and the object under the prefix"), Added >= 2);
	TestTrue(TEXT("the object under the held prefix survives a collection"), HeldObject.IsValid());
	TestFalse(TEXT("CONTROL: an object under another prefix, which nothing holds, is freed by the same collection"),
			  FreeObject.IsValid());

	// THE PARAGON HALF.
	UObject* ImpBody = FSoftObjectPath(ACataclysmImpCharacter::BodyMeshPath).TryLoad();
	if (!ImpBody)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Minions pack is absent, so the Imp's mesh being under the Paragon prefix and held is not checked; "
				 "the hold's mechanism above is"));
		return true;
	}
	TestTrue(FString::Printf(TEXT("the Imp's mesh package %s is under the Paragon prefix"), *ImpBody->GetPackage()->GetName()),
			 ImpBody->GetPackage()->GetName().StartsWith(UCataclysmParagonHold::ParagonPrefix()));
	const int32 ParagonAdded = Hold->HoldNowForTests(UCataclysmParagonHold::ParagonPrefix());
	AddInfo(FString::Printf(TEXT("HOLD: %d Paragon objects held"), ParagonAdded));
	TestTrue(TEXT("the hold takes the loaded Paragon objects"), ParagonAdded > 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
