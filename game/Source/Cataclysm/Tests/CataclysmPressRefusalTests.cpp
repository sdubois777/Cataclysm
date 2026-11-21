// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A REFUSED PRESS NAMES ITS FIGURES. Issue #2801, from the owner's play-test of 2026-11-21: "subjugate constantly says
 * I can't use it even when I have enough resources and it's off cd". No check that refuses a press wrote anything, so
 * the play log could not say which refused. `UCataclysmSkillTemplate::PressRefusalLine` now names every check's
 * figures, and `UCataclysmAbilitySystemComponent::ProcessAbilityInput` logs it when a fresh press is refused.
 *
 * WHAT IS TESTED HERE IS THE FERVOUR PART, because it is the one with arithmetic in it: the ceiling the pool can be
 * filled to now is the maximum less what the player's army holds back, and the line names who holds it, by kind. Two
 * thralls holding 30 each and an imp holding 10 hold 70. That the line is logged, and at which level, is pinned in
 * `tools/tests/test_a_refused_press_and_a_take_say_why.py`, because no automation test reads a log.
 *
 * THE PLAYER IS POSSESSED, so it holds an ability system with a class resource pool, as `FPossessedPlayer` in
 * CataclysmDungeonModifierEffectsTests.cpp and #2793's T10 build one. Its maximum is read, not written here: it is
 * the class's own (Default 100, Ritualist 150), and the test is about what is subtracted from it.
 */
namespace CataclysmPressRefusalTest
{
	constexpr float M = 100.0f;

	template <typename TActor>
	TActor* SpawnForPressRefusal(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<TActor>(TActor::StaticClass(), Where, FRotator::ZeroRotator, Params);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPressRefusalFervourFiguresTest,
	"Cataclysm.PressRefusal.TheFervourFiguresNameTheCeilingAndEachKindHoldingTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPressRefusalFervourFiguresTest::RunTest(const FString&)
{
	using namespace CataclysmPressRefusalTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Player = SpawnForPressRefusal<ACataclysmPlayerCharacter>(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: a player state"), State) || !TestNotNull(TEXT("set-up: a controller"), Controller)
		|| !TestNotNull(TEXT("set-up: a player"), Player))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Player);
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent());
	if (!TestNotNull(TEXT("set-up: the possessed player has an ability system"), AbilitySystem)
		|| !TestNotNull(TEXT("set-up: and a class resource pool"),
						AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>()))
	{
		return false;
	}
	const float Maximum = AbilitySystem->MaximumClassResource();
	TestTrue(TEXT("set-up: the pool's maximum is at least the Default class's 100, more than the 70 reserved below"),
			 Maximum >= 100.0f);

	// NOTHING COMMANDED YET: nothing reserved, and the Imp is named at nought.
	TestEqual(TEXT("nothing commanded reserves nothing, and names the Imp at nought"),
			  UCataclysmSkillTemplate::ReservedFervourByKindText(Player), FString(TEXT("reserved 0: 0 thralls 0, 0 Imp 0")));

	// TWO THRALLS AT 30 EACH, AS SUBJUGATE RESERVES, AND ONE SUMMONED IMP AT 10.
	ACataclysmImpCharacter* First = SpawnForPressRefusal<ACataclysmImpCharacter>(World, FVector(10 * M, 0.0f, 0.0f));
	ACataclysmImpCharacter* Second = SpawnForPressRefusal<ACataclysmImpCharacter>(World, FVector(10 * M, 10 * M, 0.0f));
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Player, FVector(20 * M, 0.0f, 0.0f), /*Lifetime=*/20.0f,
													/*bBurns=*/false, TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: a first creature"), First) || !TestNotNull(TEXT("set-up: a second"), Second)
		|| !TestNotNull(TEXT("set-up: a summoned imp"), Imp))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };
	First->SetHealth(100.0f);
	Second->SetHealth(100.0f);
	if (!TestTrue(TEXT("set-up: the player takes the first"), UCataclysmCommand::Subjugate(Player, First))
		|| !TestTrue(TEXT("set-up: and the second"), UCataclysmCommand::Subjugate(Player, Second)))
	{
		return false;
	}
	First->ReservedFervour = 30.0f;
	Second->ReservedFervour = 30.0f;
	Imp->ReservedFervour = 10.0f;

	// THE POOL ASKED TO FILL: it stops at the ceiling, the maximum less the 70 held back.
	AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Maximum);
	const FString Held = UCataclysmSkillTemplate::ReservedFervourByKindText(Player);
	const FString Figures = UCataclysmSkillTemplate::FervourFiguresFor(AbilitySystem, Player, 50.0f);
	TestEqual(TEXT("the reserve is named by kind: two thralls 60 and one Imp 10, of 70"), Held,
			  FString(TEXT("reserved 70: 2 thralls 60, 1 Imp 10")));
	TestEqual(TEXT("the Fervour figures name what is held, the cost, the ceiling and the maximum"), Figures,
			  FString::Printf(TEXT("Fervour %.1f of 50 (ceiling %.0f of %.0f; reserved 70: 2 thralls 60, 1 Imp 10)"),
							  Maximum - 70.0f, Maximum - 70.0f, Maximum));

	// CONTROL: freed, the thralls hold nothing back any more, and the ceiling rises to the maximum less the imp's 10.
	First->SetOwner(nullptr);
	Second->SetOwner(nullptr);
	TestEqual(TEXT("CONTROL: two freed thralls are no longer counted"),
			  UCataclysmSkillTemplate::ReservedFervourByKindText(Player), FString(TEXT("reserved 10: 0 thralls 0, 1 Imp 10")));
	TestTrue(TEXT("CONTROL: and the ceiling is the maximum less the imp's 10"),
			 UCataclysmSkillTemplate::FervourFiguresFor(AbilitySystem, Player, 50.0f)
				 .Contains(FString::Printf(TEXT("(ceiling %.0f of %.0f;"), Maximum - 10.0f, Maximum)));

	AddInfo(FString::Printf(TEXT("PRESS FIGURES: %s"), *Figures));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
